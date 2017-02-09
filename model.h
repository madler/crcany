/* model.h -- Generic CRC parameter model routines
 * Copyright (C) 2014, 2016, 2017 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

/*
  Define a generic model for a CRC and interpret a description of CRC model
  using the standard set of parameters.
 */

#ifndef _MODEL_H_
#define _MODEL_H_

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

/* Verify that the number of bits in a char is eight, using limits.h. */
#if CHAR_BIT != 8
#  error The number of bits in a char must be 8 for this code.
#endif

/* Type to use for CRC calculations.  This should be the largest unsigned
   integer type available, to maximize the cases that can be computed.  word_t
   can be any unsigned integer type, except for unsigned char.  All of the
   algorithms here can process CRCs up to the size of a word_t.  The bit-wise
   algorithm here can process CRCs up to twice the size of a word_t. */
typedef uintmax_t word_t;

/* Determine the size of uintmax_t at pre-processor time.  (sizeof is not
   evaluated at pre-processor time.)  If word_t is instead set to an explicit
   size above, e.g. uint64_t, then #define WORDCHARS appropriately, e.g. as 8.
   WORDCHARS must be 2, 4, or 8. */
#if UINTMAX_MAX == UINT16_MAX
#  define WORDCHARS 2
#elif UINTMAX_MAX == UINT32_MAX
#  define WORDCHARS 4
#elif UINTMAX_MAX == UINT64_MAX
#  define WORDCHARS 8
#else
#  error uintmax_t must be 2, 4, or 8 bytes for this code.
#endif

/* The number of bits in a word_t (assumes CHAR_BIT is 8). */
#define WORDBITS (WORDCHARS<<3)

/* Mask for the low n bits of a word_t (n must be greater than zero). */
#define ONES(n) (((word_t)0 - 1) >> (WORDBITS - (n)))

/* CRC description and tables, allowing for double-word CRCs.

   The description is based on Ross William's parameters, but with some changes
   to the parameters as described below.

   ref and rev are derived from refin and refout.  ref and rev must be 0 or 1.
   ref is the same as refin.  rev is true only if refin and refout are
   different.  rev true is very uncommon, and occurs in only one of the 72 CRCs
   in the RevEng catalogue.  When rev is false, the common case, ref true means
   that both the input and output are reflected. Reflected CRCs are quite
   common.

   init is different here as well, representing the CRC of a zero-length
   sequence, instead of the initial contents of the CRC register.

   poly is reflected for refin true.  xorout is reflected for refout true.

   The structure includes space for pre-computed CRC tables used to speed up
   the CRC calculation.  Both are filled in by the crc_table_wordwise()
   routine, using the CRC parameters already defined in the structure. */
typedef struct {
    unsigned short width;       /* number of bits in the CRC (the degree of the
                                   polynomial) */
    char ref;                   /* if true, reflect input and output */
    char rev;                   /* if true, reverse output */
    word_t poly, poly_hi;       /* polynomial representation (sans x^width) */
    word_t init, init_hi;       /* CRC of a zero-length sequence */
    word_t xorout, xorout_hi;   /* final CRC is exclusive-or'ed with this */
    word_t check, check_hi;     /* CRC of the nine ASCII bytes "12345679" */
    word_t res, res_hi;         /* Residue of the CRC */
    char *name;                 /* text description of this CRC */
    word_t table_byte[256];             /* table for byte-wise calculation */
    word_t table_word[WORDCHARS][256];  /* tables for word-wise calculation */
} model_t;

/* Read and verify a CRC model description from the string str, returning the
   result in *model.  Return 0 on success, 1 on invalid input, or 2 if out of
   memory.  model->name is allocated and should be freed when done. str is
   modified in the process, and so it cannot be a literal string.

   The parameters are "width", "poly", "init", "refin", "refout", "xorout",
   "check", "residue", and "name".  The names may be abbreviated to "w", "p",
   "i", "r", "refo", "x", "c", "res", and "n" respectively.  Each name is
   followed by an "=" sign, followed by the value for that parameter.  There
   are no spaces permitted around the "=".  "width", "poly", "init", "xorout",
   "check", and "residue" are non-negative integers, and can be provided in
   decimal (no leading zero), octal (prefixed with "0"), or hexadecimal
   (prefixed by "0x"). refin and refout must be "true" or "false", and can be
   abbreviated to "t" or "f". Upper and lower case are considered equivalent
   for all parameter names and values. The value for "name" may be in quotes to
   permit spaces in the name. The parameters may be provided in any order.

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

   "poly", "init", "xorout", "check", and "residue" must all be less than 2^n.
   The least significant bit of "poly" must be one.

   "init", "xorout", and "residue" are optional, and are set to zero if not
   provided.  Either "refin" or "refout" can be omitted, in which case the one
   missing is set to the one provided.  At least one of "refin" or "refout"
   must be provided. All other parameters must be provided.

   Example (from the RevEng catalogue at
   http://reveng.sourceforge.net/crc-catalogue/all.htm ):

      width=16 poly=0x1021 init=0x0000 refin=true refout=true xorout=0x0000 check=0x2189 residue=0x0000 name="KERMIT"

   The same model maximally abbreviated:

      w=16 p=4129 r=t c=8585 n=KERMIT
 */
int read_model(model_t *model, char *str);

/* Return the reversal of the low n-bits of x.  1 <= n <= WORDBITS.  The high
   WORDBITS - n bits in x are ignored, and are set to zero in the returned
   result.  A table-driven implementation would be faster, but the speed of
   reverse() is of no consequence since it is used at most twice per crc()
   call.  Even then, it is only used in the rare case that refin and refout are
   different. */
word_t reverse(word_t x, unsigned n);

/* Return the reversal of the low n-bits of hi/lo in hi/lo.
   1 <= n <= WORDBITS*2. */
void reverse_dbl(word_t *hi, word_t *lo, unsigned n);

/* Process values for use in crc routines -- note that this reflects the
   polynomial and init values for ready use in the crc routines if necessary,
   changes the meaning of init, and replaces refin and refout with the
   different meanings reflect and reverse (reverse is very rarely used) */
void process_model(model_t *model);

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
ssize_t getcleanline(char **line, size_t *size, FILE *in);

#endif
