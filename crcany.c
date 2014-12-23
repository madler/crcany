/*
  crcany version 1.0, 22 December 2014

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
   1.0  22 Dec 2014  First version
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

#include <stddef.h>
#include <stdint.h>

/* Verify that the number of bits in a char is eight, using limits.h. */
#include <limits.h>
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
   WORDCHARS must be 2, 4, 8, or 16. */
#if UINTMAX_MAX == UINT16_MAX
#  define WORDCHARS 2
#elif UINTMAX_MAX == UINT32_MAX
#  define WORDCHARS 4
#elif UINTMAX_MAX == UINT64_MAX
#  define WORDCHARS 8
#elif UINTMAX_MAX == UINT128_MAX
#  define WORDCHARS 16
#else
#  error uintmax_t must be 2, 4, 8, or 16 bytes for this code.
#endif

/* The number of bits in a word_t (assumes CHAR_BIT is 8). */
#define WORDBITS (WORDCHARS<<3)

/* Mask for the low n bits of a word_t (n must be greater than zero). */
#define ONES(n) (((word_t)0 - 1) >> (WORDBITS - (n)))

/* CRC description and tables, allowing for double-word CRCs.

   The description is based on Ross William's parameters, but with some changes
   to the parameters as described below.

   ref and rev are derived from refin and refout.  rev and rev must be 0 or 1.
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
    char *name;                 /* text description of this CRC */
    word_t table_byte[256];             /* table for byte-wise calculation */
    word_t table_word[WORDCHARS][256];  /* tables for word-wise calculation */
} model_t;

/* Return the reversal of the low n-bits of x.  1 <= n <= WORDBITS.  The high
   WORDBITS - n bits in x are ignored, and are set to zero in the returned
   result.  A table-driven implementation would be faster, but the speed of
   reverse() is of no consequence since it is used at most twice per crc()
   call.  Even then, it is only used in the rare case that refin and refout are
   different. */
static inline word_t reverse(word_t x, unsigned n)
{
    word_t y;

    y = x & 1;
    while (--n) {
        x >>= 1;
        if (x == 0)
            return y << n;
        y <<= 1;
        y |= x & 1;
    }
    return y;
}

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
    crc &= ONES(model->width);
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process the input data a bit at a time */
    if (model->ref) {
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
    crc &= ONES(model->width);
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process the input data a byte at a time */
    if (model->ref)
        while (len--)
            crc = (crc >> 8) ^ model->table_byte[(crc ^ *buf++) & 0xff];
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
    crc &= ONES(model->width);
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process the first few bytes up to a word_t boundary, if any */
    if (model->ref)
        while (len && ((ptrdiff_t)buf & (WORDCHARS - 1))) {
            crc = (crc >> 8) ^ model->table_byte[(crc ^ *buf++) & 0xff];
            len--;
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

/* Return the reversal of the low n-bits of hi/lo in hi/lo.
   1 <= n <= WORDBITS*2. */
static inline void reverse_dbl(word_t *hi, word_t *lo, unsigned n)
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
                        16) != 16) {
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

/* Read, verify, and process a CRC model description from the string str,
   returning the result in *model.  Return 0 on success, 1 on invalid input, or
   2 if out of memory.  model->name is allocated and should be freed when done.

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

    /* process values for use in crc routines -- note that this reflects the
       polynomial and init values for ready use in the crc routines if
       necessary, changes the meaning of init, and replaces refin and refout
       with the different meanings reflect and reverse (reverse is very
       rarely used) */
    if (model->ref)
        reverse_dbl(&model->poly_hi, &model->poly, model->width);
    if (model->rev)
        reverse_dbl(&model->init_hi, &model->init, model->width);
    model->init ^= model->xorout;
    model->init_hi ^= model->xorout_hi;
    model->rev ^= model->ref;
    return 0;
}

/* --- Test on model input from stdin --- */

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
