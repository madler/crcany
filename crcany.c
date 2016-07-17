/*
  crcany version 1.1, 15 July 2016

  Copyright (C) 2014, 2016 Mark Adler

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
   1.0  22 Dec 2014  First version
   1.1  15 Jul 2016  Allow negative numbers
                     Move common code to model.[ch]

 */

/* Generalized CRC algorithm.  Compute any specified CRC up to 128 bits long.
   Take model inputs from http://reveng.sourceforge.net/crc-catalogue/all.htm
   and verify the check values.  This verifies all 72 CRCs on that page (as of
   the version date above).  The lines on that page that start with "width="
   can be fed verbatim to this program.  The 128-bit limit assumes that
   uintmax_t is 64 bits.  The bit-wise algorithms here will compute CRCs up to
   a width twice that of the typedef'ed word_t type.

   This code also generates and tests table-driven algorithms for high-speed.
   The byte-wise algorithm processes one byte at a time instead of one bit at a
   time, and the word-wise algorithm ingests one word_t at a time.  The table
   driven algorithms here only work for CRCs that fit in a word_t, though they
   could be extended in the same way the bit-wise algorithm is extended here.

   The CRC parameters used in the linked catalogue were originally defined in
   Ross Williams' "A Painless Guide to CRC Error Detection Algorithms", which
   can be found here: http://zlib.net/crc_v3.txt .
 */

/* --- Generalized CRC routines --- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "model.h"

/* Run buf[0..len-1] through the CRC described in model.  If buf is NULL, then
   return the initial CRC for this model.  This allows for the calculation of a
   CRC in pieces, but the first call must be with crc equal to the initial
   value for this CRC model.  crc_bitwise() must only be used for model->width
   less than or equal to WORDBITS.  Otherwise use crc_bitwise_dbl().

   This routine and all of the crc routines here can process the data a chunk
   at a time.  For example, this will calculate the CRC of the sequence chunk1,
   chunk2, and chunk3:

      word_t crc;
      crc = crc_bitwise(model, 0, NULL, 0);
      crc = crc_bitwise(model, crc, chunk1, len1);
      crc = crc_bitwise(model, crc, chunk2, len2);
      crc = crc_bitwise(model, crc, chunk3, len3);

   The final value of crc is the CRC of the chunks in sequence.  The first call
   of crc_bitwise() gets the initial CRC value for this model.
 */
static inline word_t crc_bitwise(model_t *model, word_t crc,
                                 unsigned char *buf, size_t len)
{
    word_t poly = model->poly;

    /* if requested, return the initial CRC */
    if (buf == NULL)
        return model->init;

    /* pre-process the CRC */
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process the input data a bit at a time */
    if (model->ref) {
        crc &= ONES(model->width);
        while (len--) {
            crc ^= *buf++;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
        }
    }
    else if (model->width <= 8) {
        unsigned shift;

        shift = 8 - model->width;           /* 0..7 */
        poly <<= shift;
        crc <<= shift;
        while (len--) {
            crc ^= *buf++;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
            crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
        }
        crc >>= shift;
        crc &= ONES(model->width);
    }
    else {
        word_t mask;
        unsigned shift;

        mask = (word_t)1 << (model->width - 1);
        shift = model->width - 8;           /* 1..WORDBITS-8 */
        while (len--) {
            crc ^= (word_t)(*buf++) << shift;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
        }
        crc &= ONES(model->width);
    }

    /* post-process and return the CRC */
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc ^ model->xorout;
}

/* Fill in the 256-entry table in model with the CRC of the bytes 0..255, for a
   byte-wise calculation of the given CRC model.  The table value is the
   internal CRC register contents after processing the byte.  If not reflected
   and the CRC width is less than 8, then the CRC is pre-shifted left to the
   high end of the low 8 bits so that the incoming byte can be exclusive-ored
   directly into a shifted CRC. */
static void crc_table_bytewise(model_t *model)
{
    unsigned char k;
    word_t crc;

    k = 0;
    do {
        crc = model->xorout;
        crc = crc_bitwise(model, crc, &k, 1);
        crc ^= model->xorout;
        if (model->rev)
            crc = reverse(crc, model->width);
        if (model->width < 8 && !model->ref)
            crc <<= 8 - model->width;
        model->table_byte[k] = crc;
    } while (++k);
}

/* Equivalent to crc_bitwise(), but use a faster byte-wise table-based
   approach. This assumes that model->table_byte has been initialized using
   crc_table_bytewise(). */
static inline word_t crc_bytewise(model_t *model, word_t crc,
                                  unsigned char *buf, size_t len)
{
    /* if requested, return the initial CRC */
    if (buf == NULL)
        return model->init;

    /* pre-process the CRC */
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process the input data a byte at a time */
    if (model->ref) {
        crc &= ONES(model->width);
        while (len--)
            crc = (crc >> 8) ^ model->table_byte[(crc ^ *buf++) & 0xff];
    }
    else if (model->width <= 8) {
        unsigned shift;

        shift = 8 - model->width;           /* 0..7 */
        crc <<= shift;
        while (len--)
            crc = model->table_byte[crc ^ *buf++];
        crc >>= shift;
    }
    else {
        unsigned shift;

        shift = model->width - 8;           /* 1..WORDBITS-8 */
        while (len--)
            crc = (crc << 8) ^
                  model->table_byte[((crc >> shift) ^ *buf++) & 0xff];
        crc &= ONES(model->width);
    }

    /* post-process and return the CRC */
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc ^ model->xorout;
}

/* Swap the bytes in a word_t.  This can be replaced by a byte-swap builtin, if
   available on the compiler.  E.g. __builtin_bswap64() on gcc and clang.  The
   speed of swap() is inconsequential however, being used at most twice per
   crc_wordwise() call.  It is only used on little-endian machines if the CRC
   is not reflected, or on big-endian machines if the CRC is reflected. */
static inline word_t swap(word_t x)
{
    word_t y;
    unsigned n = WORDCHARS - 1;

    y = x & 0xff;
    while (x >>= 8) {
        y <<= 8;
        y |= x & 0xff;
        n--;
    }
    return y << (n << 3);
}

/* Fill in the tables for a word-wise CRC calculation.  This also fills in the
   byte-wise table since that is needed for the word-wise calculation.

   The entry in table_word[n][k] is the CRC register contents for the sequence
   of bytes: k followed by n zero bytes.  For non-reflected CRCs, the CRC is
   shifted up to the top of the word.  The CRC is byte-swapped if necessary so
   that the first byte of the CRC to be shifted out is in the same place in the
   word_t as the first byte that comes from memory.

   If model->ref is true and the machine is little-endian, then table_word[0]
   is the same as table_byte.  In that case, the two could be combined,
   reducing the total size of the tables.  This is also true if model->ref is
   false, the machine is big-endian, and model->width is equal to WORDBITS. */
static void crc_table_wordwise(model_t *model)
{
    unsigned n, k, opp, top;
    word_t crc;

    crc_table_bytewise(model);
    opp = 1;
    opp = *((unsigned char *)(&opp)) ^ model->ref;
    top = model->ref ? 0 : WORDBITS - (model->width > 8 ? model->width : 8);
    for (k = 0; k < 256; k++) {
        crc = model->table_byte[k];
        model->table_word[0][k] = opp ? swap(crc << top) : crc << top;
        for (n = 1; n < WORDCHARS; n++) {
            if (model->ref)
                crc = (crc >> 8) ^ model->table_byte[crc & 0xff];
            else if (model->width <= 8)
                crc = model->table_byte[crc];
            else
                crc = (crc << 8) ^
                      model->table_byte[(crc >> (model->width - 8)) & 0xff];
            model->table_word[n][k] = opp ? swap(crc << top) : crc << top;
        }
    }
}

/* Equivalent to crc_bitwise(), but use an even faster word-wise table-based
   approach.  This assumes that model->table_byte and model->table_word have
   been initialized using crc_table_wordwise(). */
static inline word_t crc_wordwise(model_t *model, word_t crc,
                                  unsigned char *buf, size_t len)
{
    unsigned little, top, shift;

    /* if requested, return the initial CRC */
    if (buf == NULL)
        return model->init;

    /* prepare common constants */
    little = 1;
    little = *((unsigned char *)(&little));
    top = model->ref ? 0 : WORDBITS - (model->width > 8 ? model->width : 8);
    shift = model->width <= 8 ? 8 - model->width : model->width - 8;

    /* pre-process the CRC */
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process the first few bytes up to a word_t boundary, if any */
    if (model->ref) {
        crc &= ONES(model->width);
        while (len && ((ptrdiff_t)buf & (WORDCHARS - 1))) {
            crc = (crc >> 8) ^ model->table_byte[(crc ^ *buf++) & 0xff];
            len--;
        }
    }
    else if (model->width <= 8) {
        crc <<= shift;
        while (len && ((ptrdiff_t)buf & (WORDCHARS - 1))) {
            crc = model->table_byte[(crc ^ *buf++) & 0xff];
            len--;
        }
    }
    else
        while (len && ((ptrdiff_t)buf & (WORDCHARS - 1))) {
            crc = (crc << 8) ^
                  model->table_byte[((crc >> shift) ^ *buf++) & 0xff];
            len--;
        }

    /* process as many word_t's as are available */
    if (len >= WORDCHARS) {
        crc <<= top;
        if (little) {
            if (!model->ref)
                crc = swap(crc);
            do {
                crc ^= *(word_t *)buf;
                crc = model->table_word[WORDCHARS - 1][crc & 0xff]
                    ^ model->table_word[WORDCHARS - 2][(crc >> 8)
#if WORDCHARS > 2
                                                                  & 0xff]
                    ^ model->table_word[WORDCHARS - 3][(crc >> 16) & 0xff]
                    ^ model->table_word[WORDCHARS - 4][(crc >> 24)
#if WORDCHARS > 4
                                                                   & 0xff]
                    ^ model->table_word[WORDCHARS - 5][(crc >> 32) & 0xff]
                    ^ model->table_word[WORDCHARS - 6][(crc >> 40) & 0xff]
                    ^ model->table_word[WORDCHARS - 7][(crc >> 48) & 0xff]
                    ^ model->table_word[WORDCHARS - 8][(crc >> 56)
#if WORDCHARS > 8
                                                                   & 0xff]
                    ^ model->table_word[WORDCHARS - 9][(crc >> 64) & 0xff]
                    ^ model->table_word[WORDCHARS - 10][(crc >> 72) & 0xff]
                    ^ model->table_word[WORDCHARS - 11][(crc >> 80) & 0xff]
                    ^ model->table_word[WORDCHARS - 12][(crc >> 88) & 0xff]
                    ^ model->table_word[WORDCHARS - 13][(crc >> 96) & 0xff]
                    ^ model->table_word[WORDCHARS - 14][(crc >> 104) & 0xff]
                    ^ model->table_word[WORDCHARS - 15][(crc >> 112) & 0xff]
                    ^ model->table_word[WORDCHARS - 16][(crc >> 120)
#endif
#endif
#endif
                                                                    ];
                buf += WORDCHARS;
                len -= WORDCHARS;
            } while (len >= WORDCHARS);
            if (!model->ref)
                crc = swap(crc);
        }
        else {
            if (model->ref)
                crc = swap(crc);
            do {
                crc ^= *(word_t *)buf;
                crc = model->table_word[0][crc & 0xff]
                    ^ model->table_word[1][(crc >> 8)
#if WORDCHARS > 2
                                                      & 0xff]
                    ^ model->table_word[2][(crc >> 16) & 0xff]
                    ^ model->table_word[3][(crc >> 24)
#if WORDCHARS > 4
                                                       & 0xff]
                    ^ model->table_word[4][(crc >> 32) & 0xff]
                    ^ model->table_word[5][(crc >> 40) & 0xff]
                    ^ model->table_word[6][(crc >> 48) & 0xff]
                    ^ model->table_word[7][(crc >> 56)
#if WORDCHARS > 8
                                                       & 0xff]
                    ^ model->table_word[8][(crc >> 64) & 0xff]
                    ^ model->table_word[9][(crc >> 72) & 0xff]
                    ^ model->table_word[10][(crc >> 80) & 0xff]
                    ^ model->table_word[11][(crc >> 88) & 0xff]
                    ^ model->table_word[12][(crc >> 96) & 0xff]
                    ^ model->table_word[13][(crc >> 104) & 0xff]
                    ^ model->table_word[14][(crc >> 112) & 0xff]
                    ^ model->table_word[15][(crc >> 120)
#endif
#endif
#endif
                                                        ];
                buf += WORDCHARS;
                len -= WORDCHARS;
            } while (len >= WORDCHARS);
            if (model->ref)
                crc = swap(crc);
        }
        crc >>= top;
    }

    /* process any remaining bytes after the last word_t */
    if (model->ref)
        while (len--)
            crc = (crc >> 8) ^ model->table_byte[(crc ^ *buf++) & 0xff];
    else if (model->width <= 8) {
        while (len--)
            crc = model->table_byte[(crc ^ *buf++) & 0xff];
        crc >>= shift;
    }
    else {
        while (len--)
            crc = (crc << 8) ^
                  model->table_byte[((crc >> shift) ^ *buf++) & 0xff];
        crc &= ONES(model->width);
    }

    /* post-process and return the CRC */
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc ^ model->xorout;
}

/* --- Double-wide CRC routines --- */

/* Shift left a double-word quantity by n bits: a <<= n, 0 <= n < WORDBITS.  ah
   and al must be word_t lvalues.  WORDBITS is the number of bits in a word_t,
   which must be an unsigned integer type. */
#define SHL(ah, al, n) \
    do { \
        if ((n) == 0) \
            break; \
        ah <<= n; \
        ah |= al >> (WORDBITS - (n)); \
        al <<= n; \
    } while (0)

/* Shift right a double-word quantity by n bits: a >>= n, 0 <= n < WORDBITS.
   ah and al must be word_t lvalues.  WORDBITS is the number of bits in a
   word_t, which must be an unsigned integer type. */
#define SHR(ah, al, n) \
    do { \
        if ((n) == 0) \
            break; \
        al >>= n; \
        al |= ah << (WORDBITS - (n)); \
        ah >>= n; \
    } while (0)

/* Process one bit in a big reflected CRC in crc_bitwise_dbl(). */
#define BIGREF \
    do { \
        word_t tmp; \
        tmp = lo & 1; \
        lo = (lo >> 1) | (hi << (WORDBITS - 1)); \
        hi >>= 1; \
        if (tmp) { \
            lo ^= poly_lo; \
            hi ^= poly_hi; \
        } \
    } while (0)

/* Process one bit in a non-reflected CRC that has been shifted to put the high
   byte at the bottom of hi in crc_bitwise_dbl(). */
#define BIGCROSS \
    do { \
        word_t tmp; \
        tmp = hi & 0x80; \
        hi = (hi << 1) | (lo >> (WORDBITS - 1)); \
        lo <<= 1; \
        if (tmp) { \
            lo ^= poly_lo; \
            hi ^= poly_hi; \
        } \
    } while (0)

/* Process one bit in a big non-reflected CRC in crc_bitwise_dbl(). */
#define BIGNORM \
    do { \
        word_t tmp; \
        tmp = hi & mask; \
        hi = (hi << 1) | (lo >> (WORDBITS - 1)); \
        lo <<= 1; \
        if (tmp) { \
            lo ^= poly_lo; \
            hi ^= poly_hi; \
        } \
    } while (0)

/* Similar to crc_bitwise(), but works for CRCs up to twice as long as a
   word_t. This processes long CRCs stored in two word_t values, *crc_hi and
   *crc_lo. The final CRC is returned in *crc_hi and *crc_lo.  If buf is NULL,
   then return the initial CRC for this model.  This allows for the calculation
   of a CRC in pieces, but the first call must be with the initial value for
   this CRC model.  This calls crc_bitwise() for short CRC models.  For long
   CRC models, this does the same thing crc_bitwise() does, but with the shift
   and exclusive-or operations extended across two word_t's.

   An example to compute the CRC of three chunks in sequence:

     word_t hi, lo;
     crc_bitwise_dbl(model, &hi, &lo, NULL, 0);
     crc_bitwise_dbl(model, &hi, &lo, chunk1, len1);
     crc_bitwise_dbl(model, &hi, &lo, chunk2, len2);
     crc_bitwise_dbl(model, &hi, &lo, chunk3, len3);

   The CRC of the sequence is left in hi, lo.
 */
static void crc_bitwise_dbl(model_t *model, word_t *crc_hi, word_t *crc_lo,
                            unsigned char *buf, size_t len)
{
    word_t poly_lo = model->poly;
    word_t poly_hi = model->poly_hi;
    word_t lo, hi;

    /* use crc_bitwise() for CRCs that fit in a word_t */
    if (model->width <= WORDBITS) {
        *crc_lo = crc_bitwise(model, *crc_lo, buf, len);
        *crc_hi = 0;
        return;
    }

    /* if requested, return the initial CRC */
    if (buf == NULL) {
        *crc_lo = model->init;
        *crc_hi = model->init_hi;
        return;
    }

    /* pre-process the CRC */
    lo = *crc_lo ^ model->xorout;
    hi = *crc_hi ^ model->xorout_hi;
    hi &= ONES(model->width - WORDBITS);
    if (model->rev)
        reverse_dbl(&hi, &lo, model->width);

    /* process the input data a bit at a time */
    if (model->ref) {
        while (len--) {
            lo ^= *buf++;
            BIGREF;  BIGREF;  BIGREF;  BIGREF;
            BIGREF;  BIGREF;  BIGREF;  BIGREF;
        }
    }
    else if (model->width - WORDBITS <= 8) {
        unsigned shift;

        shift = 8 - (model->width - WORDBITS);      /* 0..7 */
        SHL(poly_hi, poly_lo, shift);
        SHL(hi, lo, shift);
        while (len--) {
            hi ^= *buf++;
            BIGCROSS;  BIGCROSS;  BIGCROSS;  BIGCROSS;
            BIGCROSS;  BIGCROSS;  BIGCROSS;  BIGCROSS;
        }
        SHR(hi, lo, shift);
        hi &= ONES(model->width - WORDBITS);
    }
    else {
        word_t mask;
        unsigned shift;

        mask = (word_t)1 << (model->width - WORDBITS - 1);
        shift = model->width - WORDBITS - 8;        /* 1..WORDBITS-8 */
        while (len--) {
            hi ^= (word_t)(*buf++) << shift;
            BIGNORM;  BIGNORM;  BIGNORM;  BIGNORM;
            BIGNORM;  BIGNORM;  BIGNORM;  BIGNORM;
        }
        hi &= ONES(model->width - WORDBITS);
    }

    /* post-process and return the CRC */
    if (model->rev)
        reverse_dbl(&hi, &lo, model->width);
    lo ^= model->xorout;
    hi ^= model->xorout_hi;
    *crc_lo = lo;
    *crc_hi = hi;
}

/* --- Test on model input from stdin --- */

/* Read a series of CRC model descriptions, one per line, and verify the check
   value for each using the bit-wise, byte-wise, and word-wise algorithms.
   Checks are not done for those cases where word_t is not wide enough to
   permit the calculation. */
int main(void)
{
    int ret;
    unsigned tests;
    unsigned inval = 0, num = 0, good = 0;
    unsigned numall = 0, goodbyte = 0, goodword = 0;
    char *line = NULL;
    size_t size;
    ssize_t len;
    word_t crc_hi, crc;
    model_t model;
    unsigned char *test;

    test = malloc(32);                  /* get memory on a word boundary */
    if (test == NULL) {
        fputs("out of memory -- aborting\n", stderr);
        return 1;
    }
    memcpy(test, "123456789", 9);       /* test string for check value */
    memcpy(test + 15, "123456789", 9);  /* one off from word boundary */
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
            inval++;
        }
        else {
            process_model(&model);
            tests = 0;

            /* bit-wise */
            crc_bitwise_dbl(&model, &crc_hi, &crc, NULL, 0);
            crc_bitwise_dbl(&model, &crc_hi, &crc, test, 9);
            if (crc == model.check && crc_hi == model.check_hi) {
                tests |= 1;
                good++;
            }
            if (model.width > WORDBITS)
                tests |= 2;
            else {
                /* initialize tables for byte-wise and word-wise */
                crc_table_wordwise(&model);

                /* byte-wise */
                crc = crc_bytewise(&model, 0, NULL, 0);
                crc = crc_bytewise(&model, crc, test, 9);
                if (crc == model.check) {
                    tests |= 4;
                    goodbyte++;
                }

                /* word-wise (check on and off boundary in order to exercise
                   all loops) */
                crc = crc_wordwise(&model, 0, NULL, 0);
                crc = crc_wordwise(&model, crc, test, 9);
                if (crc == model.check) {
                    crc = crc_wordwise(&model, 0, NULL, 0);
                    crc = crc_wordwise(&model, crc, test + 15, 9);
                    if (crc == model.check) {
                        tests |= 8;
                        goodword++;
                    }
                }
                numall++;
            }
            num++;
            if (tests & 2)
                printf("%s: bitwise test %s (CRC too long for others)\n",
                       model.name, tests & 1 ? "passed" : "failed");
            else if (tests == 13)
                printf("%s: all tests passed\n", model.name);
            else if (tests == 0)
                printf("%s: all tests failed\n", model.name);
            else
                printf("%s: bitwise %s, bytewise %s, wordwise %s\n",
                       model.name, tests & 1 ? "passed" : "failed",
                       tests & 4 ? "passed" : "failed",
                       tests & 8 ? "passed" : "failed");
        }
        free(model.name);
        model.name = NULL;
    }
    free(line);
    free(test);
    printf("%u models verified bit-wise out of %u usable "
           "(%u unusable models)\n", good, num, inval);
    printf("%u models verified byte-wise out of %u usable\n",
           goodbyte, numall);
    crc = 1;
    printf("%u models verified word-wise out of %u usable (%s-endian)\n",
           goodword, numall, *((unsigned char *)(&crc)) ? "little" : "big");
    return 0;
}
