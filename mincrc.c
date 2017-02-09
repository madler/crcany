/*
  mincrc version 1.2, 10 February 2017

  Copyright (C) 2014, 2016, 2017 Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Mark Adler
  madler@alumni.caltech.edu
*/

/* Version history:
   1.0  17 Dec 2014  First version
   1.1  15 Jul 2016  Allow negative numbers
                     Move common code to model.[ch]
   1.2  10 Feb 2017  Add residue parameter
 */

/* Maximally compress the CRC representations by abbreviating parameter names,
   eliminating parameters that have the default value, quoting the name only if
   necessary, and using decimal for numbers unless hexadecimal is shorter. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

#include "model.h"

/* --- Run on model input from stdin --- */

#include <inttypes.h>

/* Divide a double width integer hi:lo by ten, returning the remainder, and
   updating hi:lo with the quotient. */
static inline unsigned div10(word_t *lo, word_t *hi)
{
    if (*hi == 0) {
        unsigned rem = *lo % 10;
        *lo /= 10;
        return rem;
    }
    word_t hq = *hi / 10;
    word_t hr = *hi % 10;
    word_t lq = *lo / 10;
    word_t lr = *lo % 10;
    unsigned rem = hr * (((word_t)0 - 1) % 10 + 1) + lr;
    hr *= ((word_t)0 - 1) / 10;
    lq += hr;
    if (lq < hr)
        hq++;
    hr = rem / 10;
    rem %= 10;
    lq += hr;
    if (lq < hr)
        hq++;
    *lo = lq;
    *hi = hq;
    return rem;
}

/* Convert the double-width integer hi:lo to an ASCII decimal integer in str[].
   str[] is assumed to have enough space for the decimal digits and a
   terminating nul. Return str. */
static char *dbl2dec(word_t lo, word_t hi, char *str)
{
    char *p = str;
    do {
        *p++ = '0' + div10(&lo, &hi);
    } while (lo | hi);
    *p-- = 0;
    char *q = str;
    while (q < p) {
        char tmp = *q;
        *q++ = *p;
        *p-- = tmp;
    }
    return str;
}

/* Return the number of hexadecimal digits required to represent hi:lo. */
static unsigned hexdigs(word_t lo, word_t hi)
{
    unsigned n = (WORDBITS << 1) - 4;
    while (n && ((n < WORDBITS ? lo >> n : hi >> (n - WORDBITS)) & 0xf) == 0)
        n -= 4;
    return (n >> 2) + 1;
}

/* Convert the low four bits of n to an ASCII hexadecimal digit. */
static inline unsigned hex(unsigned n)
{
    n &= 0xf;
    return n < 10 ? n + '0' : 'a' + n - 10;
}

/* Convert the double-width integer hi:lo to an ASCII decimal integer or to a
   hexadecimal integer preceded by "0x", whichever is shorter, or the decimal
   version if they are the same length.  Return the result in str[], and return
   str.  str[] is assumed to have enough space for either. */
static char *dbl2str(word_t lo, word_t hi, char *str)
{
    dbl2dec(lo, hi, str);
    unsigned digs = hexdigs(lo, hi);
    if (digs + 2 < strlen(str)) {
        char *p = str + digs + 2;
        *p = 0;
        for (unsigned n = 0; p > str + 2; n += 4)
            *--p = hex(n < WORDBITS ? lo >> n : hi >> (n - WORDBITS));
        *--p = 'x';
        *--p = '0';
    }
    return str;
}

/* Sign-extend the double-width integer hi:lo with the sign bit at position
   width-1.  width is assumed to not be zero and hi:lo is assumed to have all
   zero bits above the width bits. */
static void sign_extend(word_t *lo, word_t *hi, unsigned width)
{
    if (width <= WORDBITS) {
        word_t sign = *lo & ((word_t)1 << (width - 1));
        *lo -= sign << 1;
        if (sign)
            (*hi)--;
    }
    else {
        word_t sign = *hi & ((word_t)1 << (width - 1 - WORDBITS));
        *hi -= sign << 1;
    }
}

/* Print a numeric parameter, using hexadecimal and/or a negative value if that
   would use fewer characters.  End with a space. */
static void parm(char *name, word_t lo, word_t hi, unsigned width, FILE *out)
{
    char str[2][5*WORDCHARS+3];
    dbl2str(lo, hi, str[0]);
    str[1][0] = '-';
    sign_extend(&lo, &hi, width);
    word_t nlo = ~lo + 1;
    word_t nhi = ~hi + (lo == 0 ? 1 : 0);
    dbl2str(nlo, nhi, str[1] + 1);
    fprintf(out, "%s=%s ", name,
            strlen(str[1]) < strlen(str[0]) ? str[1] : str[0]);
}

/* Print a string parameter, in double quotes if the string contains white
   space.  End with a newline. */
static void quoted(char *name, char *str, FILE *out)
{
    char *next, *quote;

    next = str;
    while (*next) {
        if (isspace(*next))
            break;
        next++;
    }
    fprintf(out, "%s=", name);
    if (*next == 0) {
        fprintf(out, "%s\n", str);
        return;
    }
    putc('"', out);
    next = str;
    while ((quote = strchr(next, '"')) != NULL) {
        fwrite(next, 1, quote - next, out);
        fputs("\"\"", out);
        next = quote + 1;
    }
    fprintf(out, "%s\"\n", next);
}

/* Read a series of CRC model descriptions, one per line, from stdin and write
   the model back out maximally compressed to stdout. */
int main(void)
{
    int ret;
    model_t model;
    char *line = NULL;
    size_t size;
    ssize_t len;
    FILE *out = stdout;

    model.name = NULL;
    while ((len = getcleanline(&line, &size, stdin)) != -1) {
        if (len == 0)
            continue;
        ret = read_model(&model, line);
        if (ret == 2) {
            fputs("out of memory -- aborting\n", stderr);
            break;
        }
        else if (ret == 1) {
            fprintf(stderr, "%s: -- unusable model\n",
                    model.name == NULL ? "<no name>" : model.name);
        }
        else {
            parm("w", model.width, 0, WORDBITS, out);
            parm("p", model.poly, model.poly_hi, model.width, out);
            if (model.init || model.init_hi)
                parm("i", model.init, model.init_hi, model.width, out);
            fprintf(out, "r=%s ", model.ref ? "t" : "f");
            if (model.ref != model.rev)
                fprintf(out, "refo=%s ", model.rev ? "t" : "f");
            if (model.xorout || model.xorout_hi)
                parm("x", model.xorout, model.xorout_hi, model.width, out);
            parm("c", model.check, model.check_hi, model.width, out);
            if (model.res || model.res_hi)
                parm("res", model.res, model.res_hi, model.width, out);
            quoted("n", model.name, out);
        }
        free(model.name);
        model.name = NULL;
    }
    free(line);
    line = NULL;
    return 0;
}
