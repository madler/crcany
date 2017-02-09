/* model.c -- Generic CRC parameter model routines
 * Copyright (C) 2014, 2016, 2017 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include "model.h"

/* --- Parameter processing routines --- */

#include <stdio.h>
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
   zero-terminated value.  *str is modified to contain the zero-terminated name
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
   consider it to be decimal. If the string starts with "-", return the two's
   complement of the number that follows. Return a pointer to the first
   character in the string that is not a valid digit, or the end of the string
   if all were valid.  If the provided digits result in an overflow of the
   double-length integer, then NULL is returned.  If NULL is returned, *high
   and *low are unaltered. */
static char *strtobig(char *str, word_t *high, word_t *low)
{
    unsigned neg = 0;   /* true if "-" prefix */
    unsigned k;         /* base, then digits */
    word_t nh, nl;      /* double-length number accumulated */
    word_t th, tl;      /* temporary double-length number */

    /* look for minus sign */
    if (*str == '-') {
        str++;
        neg = 1;
    }

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
                        16) != 16) {
                SHLO(nh, nl, nh, nl, 4);
                nl |= k;
            }
    }

    /* if negative, negate */
    if (neg) {
        nl = ~nl + 1;
        nh = ~nh + (nl == 0 ? 1 : 0);
    }

    /* return result and string position after number */
    *high = nh;
    *low = nl;
    return str;
}

/* Check that high:low bits above width bits are either all zeros or all ones.
   If they are all ones, make them all zeros.  Return true if those bits were
   not all zeros or all ones. */
static int normal_big(word_t *low, word_t *high, unsigned width)
{
    const word_t ones = (word_t)0 - 1;
    word_t mask_lo = width < WORDBITS ? ones << width : 0;
    word_t mask_hi = width <= WORDBITS ? ones :
        width < WORDBITS*2 ? ones << (width - WORDBITS) : 0;
    if ((*low & mask_lo) == mask_lo && (*high & mask_hi) == mask_hi) {
        *low &= ~mask_lo;
        *high &= ~mask_hi;
        return 0;
    }
    return (*low & mask_lo) != 0 || (*high & mask_hi) != 0;
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
#define RES 128
#define NAME 256
#define ALL (WIDTH|POLY|INIT|REFIN|REFOUT|XOROUT|CHECK|RES|NAME)

/* See model.h. */
int read_model(model_t *model, char *str)
{
    int ret;
    char *name, *value, *end;
    size_t n, k;
    unsigned got, bad, rep;
    char *unk;
    word_t hi, lo;

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
        else if (strncasecmp(name, "residue", n < 3 ? 3 : n) == 0) {
            if (got & RES) {
                rep |= RES;
                continue;
            }
            if ((end = strtobig(value, &hi, &lo)) == NULL || *end) {
                bad |= RES;
                continue;
            }
            model->res = lo;
            model->res_hi = hi;
            got |= RES;
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
    if ((got & RES) == 0) {
        model->res = 0;
        model->res_hi = 0;
        got |= RES;
    }

    /* check for parameter values out of range */
    if (got & WIDTH) {
        if (model->width < 1 || model->width > WORDBITS*2)
            bad |= WIDTH;
        else {
            if ((got & POLY) &&
                (normal_big(&model->poly, &model->poly_hi, model->width) ||
                 (model->poly & 1) != 1))
                bad |= POLY;
            if (normal_big(&model->init, &model->init_hi, model->width))
                bad |= INIT;
            if (normal_big(&model->xorout, &model->xorout_hi, model->width))
                bad |= XOROUT;
            if ((got & CHECK) &&
                normal_big(&model->check, &model->check_hi, model->width))
                bad |= CHECK;
            if ((got & RES) &&
                normal_big(&model->res, &model->res_hi, model->width))
                bad |= RES;
        }
    }

    /* issue error messages for noted problems (this section can be safely
       removed if error messages are not desired) */
    if (ret == -1)
        fprintf(stderr, "bad syntax (not 'parm=value') at: '%s'\n", str);
    else {
        const char *parm[] = {"width", "poly", "init", "refin", "refout",
            "xorout", "check", "residue", "name"};
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

/* See model.h. */
word_t reverse(word_t x, unsigned n)
{
    if (n == 1)
        return x & 1;
    if (n == 2)
        return ((x >> 1) & 1) + ((x << 1) & 2);
    if (n <= 4) {
        x = ((x >> 2) & 3) + ((x << 2) & 0xc);
        x = ((x >> 1) & 5) + ((x << 1) & 0xa);
        return x >> (4 - n);
    }
    if (n <= 8) {
        x = ((x >> 4) & 0xf) + ((x << 4) & 0xf0);
        x = ((x >> 2) & 0x33) + ((x << 2) & 0xcc);
        x = ((x >> 1) & 0x55) + ((x << 1) & 0xaa);
        return x >> (8 - n);
    }
    if (n <= 16) {
        x = ((x >> 8) & 0xff) + ((x << 8) & 0xff00);
        x = ((x >> 4) & 0xf0f) + ((x << 4) & 0xf0f0);
        x = ((x >> 2) & 0x3333) + ((x << 2) & 0xcccc);
        x = ((x >> 1) & 0x5555) + ((x << 1) & 0xaaaa);
        return x >> (16 - n);
    }
#if WORDBITS >= 32
    if (n <= 32) {
        x = ((x >> 16) & 0xffff) + ((x << 16) & 0xffff0000);
        x = ((x >> 8) & 0xff00ff) + ((x << 8) & 0xff00ff00);
        x = ((x >> 4) & 0xf0f0f0f) + ((x << 4) & 0xf0f0f0f0);
        x = ((x >> 2) & 0x33333333) + ((x << 2) & 0xcccccccc);
        x = ((x >> 1) & 0x55555555) + ((x << 1) & 0xaaaaaaaa);
        return x >> (32 - n);
    }
#  if WORDBITS >= 64
    if (n <= 64) {
        x = ((x >> 32) & 0xffffffff) + ((x << 32) & 0xffffffff00000000);
        x = ((x >> 16) & 0xffff0000ffff) + ((x << 16) & 0xffff0000ffff0000);
        x = ((x >> 8) & 0xff00ff00ff00ff) + ((x << 8) & 0xff00ff00ff00ff00);
        x = ((x >> 4) & 0xf0f0f0f0f0f0f0f) + ((x << 4) & 0xf0f0f0f0f0f0f0f0);
        x = ((x >> 2) & 0x3333333333333333) + ((x << 2) & 0xcccccccccccccccc);
        x = ((x >> 1) & 0x5555555555555555) + ((x << 1) & 0xaaaaaaaaaaaaaaaa);
        return x >> (64 - n);
    }
#  endif
#endif
    return n < 2*WORDBITS ? reverse(x, WORDBITS) << (n - WORDBITS) : 0;
}

/* See model.h. */
void reverse_dbl(word_t *hi, word_t *lo, unsigned n)
{
    word_t tmp;

    if (n <= WORDBITS) {
        *lo = reverse(*lo, n);
        *hi = 0;
    }
    else {
        tmp = reverse(*lo, WORDBITS);
        *lo = reverse(*hi, n - WORDBITS);
        if (n < WORDBITS*2) {
            *lo |= tmp << (n - WORDBITS);
            *hi = tmp >> (WORDBITS*2 - n);
        }
        else
            *hi = tmp;
    }
}

/* See model.h. */
void process_model(model_t *model)
{
    if (model->ref)
        reverse_dbl(&model->poly_hi, &model->poly, model->width);
    if (model->rev)
        reverse_dbl(&model->init_hi, &model->init, model->width);
    model->init ^= model->xorout;
    model->init_hi ^= model->xorout_hi;
    model->rev ^= model->ref;
}

/* See model.h. */
ssize_t getcleanline(char **line, size_t *size, FILE *in)
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
