/*
  mincrc version 1.0, 17 December 2014

  Copyright (C) 2014 Mark Adler

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
 */

/* Maximally compress the CRC representations by abbreviating parameter names,
   eliminating parameters that have the default value, quoting the name only if
   necessary, and using decimal for numbers unless hexadecimal is shorter. */

#include <stddef.h>
#include <inttypes.h>
#include <limits.h>

/* Type to use for CRC calculations.  In general this should be the largest
   unsigned integer type available, to maximize the cases that can be computed.
   word_t could be made a smaller type if speed is paramount and the size of
   the word_t type is known to cover the CRC polynomials that will be
   presented. */
typedef uintmax_t word_t;       /* unsigned type for crc values */
#define WORDBITS ((int)sizeof(word_t)*CHAR_BIT)

/* CRC description, permitting up to double-word CRCs. */
typedef struct {
    unsigned short width;       /* number of bits in CRC */
    char ref;                   /* if true, reflect input and output */
    char rev;                   /* if true, reverse output */
    word_t poly, poly_hi;       /* polynomial representation */
    word_t init, init_hi;       /* initial CRC register contents */
    word_t xorout, xorout_hi;   /* final CRC is exclusive-or'ed with this */
    word_t check, check_hi;     /* CRC of the nine ASCII bytes "12345679" */
    char *name;                 /* Textual description of this CRC */
} model_t;

/* --- Parameter processing routines --- */

#include <string.h>
#include <ctype.h>

/* Read one variable name and value from a string.  The format is name=value,
   possibly preceded by white space, with the value ended by white space or the
   end of the string.  White space is not permitted in the name, value, or
   around the = sign.  Except that value can contain white space if it starts
   with a double quote.  In that case, the value is taken from after the
   initial double-quote to before the closing double quote.  A double-quote can
   be included in the value with two adjacent double-quotes.  A double-quote in
   a name or in a value when the first character isn't a double-quote is
   treated like any other non-white-space character.  On return *str points
   after the value, name is the zero-terminated name and value is the
   zero-terminated value. *str is modified to contain the zero-terminated name
   and value.  read_vars() returns 1 on success, 0 on end of string, or -1 if
   there was an error, such as no name, no "=", no value, or no closing quote.
   If -1, *str is not modified, though *next and *value may be modified. */
static int read_var(char **str, char **name, char **value)
{
    char *next, *copy;

    /* skip any leading white space, check for end of string */
    next = *str;
    while (isspace(*next))
        next++;
    if (*next == 0)
        return 0;

    /* get name */
    *name = next;
    while (*next && !isspace(*next) && *next != '=')
        next++;
    if (*next != '=' || next == *name)
        return -1;
    *next++ = 0;

    /* get value */
    if (*next == '"') {
        /* get quoted value */
        *value = ++next;
        next = strchr(next, '"');
        if (next == NULL)
            return -1;

        /* handle embedded quotes */
        copy = next;
        while (next[1] == '"') {
            next++;
            do {
                *copy++ = *next++;
                if (*next == 0)
                    return -1;
            } while (*next != '"');
            *copy = 0;
        }
    }
    else {
        /* get non-quoted value */
        *value = next;
        while (*next && !isspace(*next))
            next++;
        if (next == *value)
            return -1;
    }

    /* skip terminating character if not end of string, terminate value */
    if (*next)
        *next++ = 0;

    /* return updated string location */
    *str = next;
    return 1;
}

/* Shift left a double-word quantity by n bits: r = a << n, 0 < n < WORDBITS.
   Return NULL from the enclosing function on overflow.  rh and rl must be
   word_t lvalues.  It is allowed for a to be r, shifting in place.  ah and al
   are each used twice in this macro, so beware of side effects.  WORDBITS is
   the number of bits in a word_t, which must be an unsigned integer type. */
#define SHLO(rh, rl, ah, al, n) \
    do { \
        if ((word_t)(ah) >> (WORDBITS - (n))) \
            return NULL; \
        rh = ((word_t)(ah) << (n)) | ((word_t)(al) >> (WORDBITS - (n))); \
        rl = (word_t)(al) << (n); \
    } while (0)

/* Add two double-word quantities: r += a.  Return NULL from the enclosing
   function on overflow.  rh and rl must be word_t lvalues.  Note that rh and
   rl are referenced more than once, so beware of side effects. */
#define ADDO(rh, rl, ah, al) \
    do { \
        word_t t; \
        t = rh; \
        rh += (ah); \
        if (rh < t) \
            return NULL; \
        t = rl; \
        rl += (al); \
        if (rl < t && ++(rh) == 0) \
            return NULL; \
    } while (0)

/* Convert a string of digits to a double-word unsigned integer, that is, two
   word_t unsigned integers making up a single unsigned integer with twice as
   many bits.  If the string starts with "0x" or "0X", consider the string to
   be hexadecimal.  If it starts with "0", consider it to be octal.  Otherwise
   consider it to be decimal.  Return a pointer to the first character in the
   string that is not a valid digit, or the end of the string if all were
   valid.  If the provided digits result in an overflow of the double-length
   integer, then NULL is returned.  If NULL is returned, *high and *low are
   unaltered. */
static char *strtobig(char *str, word_t *high, word_t *low)
{
    unsigned k;         /* base, then digits */
    word_t nh, nl;      /* double-length number accumulated */
    word_t th, tl;      /* temporary double-length number */

    /* determine base from prefix */
    k = 10;
    if (*str == '0') {
        str++;
        k = 8;
        if (*str == 'x' || *str == 'X') {
            str++;
            k = 16;
        }
    }

    /* accumulate digits until a non-digit */
    nh = nl = 0;
    switch (k) {
        case 8:
            while (*str >= '0' && *str <= '7') {
                k = *str++ - '0';
                SHLO(nh, nl, nh, nl, 3);
                nl |= k;
            }
            break;
        case 10:
            while (*str >= '0' && *str <= '9') {
                k = *str++ - '0';
                SHLO(nh, nl, nh, nl, 1);        /* n <<= 1 */
                SHLO(th, tl, nh, nl, 2);        /* t = n << 2 */
                ADDO(nh, nl, th, tl);           /* n += t */
                ADDO(nh, nl, 0, k);             /* n += k */
            }
            break;
        case 16:
            while ((k = *str >= '0' && *str <= '9' ? *str++ - '0' :
                        *str >= 'A' && *str <= 'F' ? *str++ - 'A' + 10 :
                        *str >= 'a' && *str <= 'f' ? *str++ - 'a' + 10 :
                        -1) != -1) {
                SHLO(nh, nl, nh, nl, 4);
                nl |= k;
            }
    }

    /* return result and string position after number */
    *high = nh;
    *low = nl;
    return str;
}

/* --- Model input routines --- */

#include <stdio.h>
#include <stdlib.h>

/* Masks for parameters. */
#define WIDTH 1
#define POLY 2
#define INIT 4
#define REFIN 8
#define REFOUT 16
#define XOROUT 32
#define CHECK 64
#define NAME 128
#define ALL (WIDTH|POLY|INIT|REFIN|REFOUT|XOROUT|CHECK|NAME)

/* Read and verify a CRC model description from the string str, returning the
   result in *model.  Return 0 on success, 1 on invalid input, or 2 if out of
   memory.  model->name is allocated and should be freed when done.

   The parameters are "width", "poly", "init", "refin", "refout", "xorout",
   "check", and "name".  The names may be abbrievated to "w", "p", "i", "r",
   "refo", "x", "c", and "n" respectively.  Each name is followed by an "="
   sign, followed by the value for that parameter.  There are no spaces
   permitted around the "=".  "width", "poly", "init", "xorout", and "check"
   are non-negative integers, and can be provided in decimal (no leading zero),
   octal (prefixed with "0"), or hexadecimal (prefixed by "0x").  refin and
   refout must be true or false, and can be abbreviated to t or f.  Upper and
   lower case are considered equivalent for all parameter names and values.
   The value for "name" may be in quotes to permit spaces in the name.  The
   parameters may be provided in any order.

   "width" is the number of bits in the CRC, referred to herein as n.  "poly"
   is the binary representation of the CRC polynomial, sans the x^n term.
   "poly" is never provided in a reflected form.  So x^16 + x^12 + x^5 + 1 is
   "width=16 poly=0x1021".

   "init" is the initial contents of the CRC register.  If "refin" is true,
   then the input bytes are reflected.  If "refout" is true, then the CRC
   register is reflected on output.  "xorout" is exclusive-ored with the CRC,
   after reflection if any. "check" is the CRC of the nine bytes "123456789"
   encoded in ASCII.  "name" is the name of the CRC.

   "width" can be as much as twice the number of bits in the word_t type, set
   here to the largest integer type available to the compiler (uintmax_t).  On
   most modern systems, this permits up to 128-bit CRCs.

   "poly", "init", "xorout", and "check" must all be less than 2^n.  The least
   significant bit of "poly" must be one.

   "init" and "xorout" are optional, and set to zero if not provided.  Either
   "refin" or "refout" can be omitted, in which case the one missing is set to
   the one provided.  At least one of "refin" or "refout" must be provided.
   All other parameters must be provided.

   Example (from the catalogue linked in the comments):

      width=16 poly=0x1021 init=0x0000 refin=true refout=true xorout=0x0000 check=0x2189 name="KERMIT"

   The same model maximally abbreviated:

      w=16 p=4129 r=t c=8585 n=KERMIT
 */
static int read_model(model_t *model, char *str)
{
    int ret;
    char *name, *value, *end;
    size_t n, k;
    unsigned got, bad, rep;
    char *unk;
    word_t hi, lo;
    const char *parm[] = {"width", "poly", "init", "refin", "refout", "xorout",
                          "check", "name"};

    /* read name=value pairs from line */
    got = bad = rep = 0;
    unk = NULL;
    model->name = NULL;
    while ((ret = read_var(&str, &name, &value)) == 1) {
        n = strlen(name);
        k = strlen(value);
        if (strncasecmp(name, "width", n) == 0) {
            if (got & WIDTH) {
                rep |= WIDTH;
                continue;
            }
            if ((end = strtobig(value, &hi, &lo)) == NULL || *end || hi) {
                bad |= WIDTH;
                continue;
            }
            model->width = lo;
            got |= WIDTH;
        }
        else if (strncasecmp(name, "poly", n) == 0) {
            if (got & POLY) {
                rep |= POLY;
                continue;
            }
            if ((end = strtobig(value, &hi, &lo)) == NULL || *end) {
                bad |= POLY;
                continue;
            }
            model->poly = lo;
            model->poly_hi = hi;
            got |= POLY;
        }
        else if (strncasecmp(name, "init", n) == 0) {
            if (got & INIT) {
                rep |= INIT;
                continue;
            }
            if ((end = strtobig(value, &hi, &lo)) == NULL || *end) {
                bad |= POLY;
                continue;
            }
            model->init = lo;
            model->init_hi = hi;
            got |= INIT;
        }
        else if (strncasecmp(name, "refin", n) == 0) {
            if (got & REFIN) {
                rep |= REFIN;
                continue;
            }
            if (strncasecmp(value, "true", k) &&
                    strncasecmp(value, "false", k)) {
                bad |= REFIN;
                continue;
            }
            model->ref = *value == 't' ? 1 : 0;
            got |= REFIN;
        }
        else if (strncasecmp(name, "refout", n < 4 ? 4 : n) == 0) {
            if (got & REFOUT) {
                rep |= REFOUT;
                continue;
            }
            if (strncasecmp(value, "true", k) &&
                    strncasecmp(value, "false", k)) {
                bad |= REFOUT;
                continue;
            }
            model->rev = *value == 't' ? 1 : 0;
            got |= REFOUT;
        }
        else if (strncasecmp(name, "xorout", n) == 0) {
            if (got & XOROUT) {
                rep |= XOROUT;
                continue;
            }
            if ((end = strtobig(value, &hi, &lo)) == NULL || *end) {
                bad |= XOROUT;
                continue;
            }
            model->xorout = lo;
            model->xorout_hi = hi;
            got |= XOROUT;
        }
        else if (strncasecmp(name, "check", n) == 0) {
            if (got & CHECK) {
                rep |= CHECK;
                continue;
            }
            if ((end = strtobig(value, &hi, &lo)) == NULL || *end) {
                bad |= CHECK;
                continue;
            }
            model->check = lo;
            model->check_hi = hi;
            got |= CHECK;
        }
        else if (strncasecmp(name, "name", n) == 0) {
            if (got & NAME) {
                rep |= NAME;
                continue;
            }
            model->name = malloc(strlen(value) + 1);
            if (model->name == NULL)
                return 2;
            strcpy(model->name, value);
            got |= NAME;
        }
        else
            unk = name;
    }

    /* provide defaults for some parameters */
    if ((got & INIT) == 0) {
        model->init = 0;
        model->init_hi = 0;
        got |= INIT;
    }
    if ((got & (REFIN|REFOUT)) == REFIN) {
        model->rev = model->ref;
        got |= REFOUT;
    }
    else if ((got & (REFIN|REFOUT)) == REFOUT) {
        model->ref = model->rev;
        got |= REFIN;
    }
    if ((got & XOROUT) == 0) {
        model->xorout = 0;
        model->xorout_hi = 0;
        got |= XOROUT;
    }

    /* check for parameter values out of range */
    if (got & WIDTH) {
        if (model->width < 1 || model->width > WORDBITS*2)
            bad |= WIDTH;
        else {
            hi = model->width <= WORDBITS ? 0 :
                 model->width == WORDBITS*2 ? (word_t)0 - 1 :
                 ((word_t)1 << (model->width - WORDBITS)) - 1;
            lo = model->width < WORDBITS ? ((word_t)1 << model->width) - 1 :
                 (word_t)0 - 1;
            if ((got & POLY) && (model->poly > lo || model->poly_hi > hi ||
                                 (model->poly & 1) != 1))
                bad |= POLY;
            if (model->init > lo || model->init_hi > hi)
                bad |= INIT;
            if (model->xorout > lo || model->xorout_hi > hi)
                bad |= XOROUT;
            if ((got & CHECK) && (model->check > lo || model->check_hi > hi))
                bad |= CHECK;
        }
    }

    /* issue error messages for noted problems (this section can be safely
       removed if error messages are not desired) */
    if (ret == -1)
        fprintf(stderr, "bad syntax (not 'parm=value') at: '%s'\n", str);
    else {
        name = model->name == NULL ? "<no name>" : model->name;
        if (unk != NULL)
            fprintf(stderr, "%s: unknown parameter %s\n", name, unk);
        for (n = rep, k = 0; n; n >>= 1, k++)
            if (n & 1)
                fprintf(stderr, "%s: %s repeated\n", name, parm[k]);
        for (n = bad, k = 0; n; n >>= 1, k++)
            if (n & 1)
                fprintf(stderr, "%s: %s out of range\n", name, parm[k]);
        for (n = (got ^ ALL) & ~bad, k = 0; n; n >>= 1, k++)
            if (n & 1)
                fprintf(stderr, "%s: %s missing\n", name, parm[k]);
    }

    /* return error if model not fully specified and valid */
    if (ret == -1 || unk != NULL || rep || bad || got != ALL)
        return 1;

    /* all good */
    return 0;
}

/* --- Run on model input from stdin --- */

/* Read a newline-terminated or EOF-terminated line from in.  The trailing
   newline and any other trailing space characters are removed, and any
   embedded nuls are deleted.  The line is terminated with a nul.  The return
   value is the number of characters in the returned line, not including the
   terminating nul, or -1 for EOF or read error.  The returned line may have
   zero length, which represents a blank line from the input.  *line is
   allocated space with the returned line, where *size is the size of that
   space.  *line can be reused in subsequent calls, and needs to be freed when
   done.  The first call can be with *line == NULL, in which case a line buffer
   will be allocated.  *line will be reallocated as needed for longer input
   lines. */
static ssize_t getcleanline(char **line, size_t *size, FILE *in)
{
    ssize_t len, n, k;
    char *ln;

    /* get a line, return -1 on EOF or error */
    len = getline(line, size, in);    /* POSIX function */
    if (len == -1)
        return -1;

    /* delete any embedded nulls */
    ln = *line;
    n = 0;
    while (n < len && ln[n])
        n++;
    k = n;
    while (++n < len)
        if (ln[n])
            ln[k++] = ln[n];

    /* delete any trailing space */
    while (k && isspace(ln[k - 1]))
        k--;
    ln[k] = 0;
    return k;
}

/* Print a numeric parameter, using hexadecimal if that would use fewer
   characters.  End with a space. */
static void parm(char *name, word_t lo, word_t hi, FILE *out)
{
    fprintf(out, "%s=", name);
    if (hi)
        fprintf(out, "0x%" PRIxMAX "%0*" PRIxMAX " ", hi, WORDBITS/4, lo);
    else
        fprintf(out, lo > 999999999999 ? "0x%" PRIxMAX " " : "%" PRIuMAX " ",
                lo);
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
            parm("w", model.width, 0, out);
            parm("p", model.poly, model.poly_hi, out);
            if (model.init || model.init_hi)
                parm("i", model.init, model.init_hi, out);
            fprintf(out, "r=%s ", model.ref ? "t" : "f");
            if (model.ref != model.rev)
                fprintf(out, "refo=%s ", model.rev ? "t" : "f");
            if (model.xorout || model.xorout_hi)
                parm("x", model.xorout, model.xorout_hi, out);
            parm("c", model.check, model.check_hi, out);
            quoted("n", model.name, out);
        }
        free(model.name);
        model.name = NULL;
    }
    free(line);
    line = NULL;
    return 0;
}
