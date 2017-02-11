/* crc.c -- Generic CRC calculations
 * Copyright (C) 2014, 2016, 2017 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include <stddef.h>
#include "crc.h"

word_t crc_bitwise(model_t *model, word_t crc, void const *dat, size_t len)
{
    unsigned char const *buf = dat;
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

word_t crc_zeros(model_t *model, word_t crc, size_t count)
{
    word_t poly = model->poly;

    /* pre-process the CRC */
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    /* process count zeros */
    if (model->ref) {
        crc &= ONES(model->width);
        while (count--)
            crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
    }
    else {
        word_t mask = (word_t)1 << (model->width - 1);
        while (count--)
            crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
        crc &= ONES(model->width);
    }

    /* post-process and return the CRC */
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc ^ model->xorout;
}

void crc_table_bytewise(model_t *model)
{
    unsigned char k;
    word_t crc;

    k = 0;
    do {
        crc = crc_bitwise(model, 0, &k, 1);
        if (model->rev)
            crc = reverse(crc, model->width);
        if (model->width < 8 && !model->ref)
            crc <<= 8 - model->width;
        model->table_byte[k] = crc;
    } while (++k);
}

word_t crc_bytewise(model_t *model, word_t crc, void const *dat, size_t len)
{
    unsigned char const *buf = dat;

    /* if requested, return the initial CRC */
    if (buf == NULL)
        return model->init;

    /* pre-process the CRC */
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
    return crc;
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

void crc_table_wordwise(model_t *model)
{
    crc_table_bytewise(model);
    unsigned opp = 1;
    opp = *((unsigned char *)(&opp)) ^ model->ref;
    unsigned top =
        model->ref ? 0 :
                     WORDBITS - (model->width > 8 ? model->width : 8);
    word_t xor = model->xorout;
    if (model->width < 8 && !model->ref)
        xor <<= 8 - model->width;
    for (unsigned k = 0; k < 256; k++) {
        word_t crc = model->table_byte[k];
        model->table_word[0][k] = opp ? swap(crc << top) : crc << top;
        for (unsigned n = 1; n < WORDCHARS; n++) {
            crc ^= xor;
            if (model->ref)
                crc = (crc >> 8) ^ model->table_byte[crc & 0xff];
            else if (model->width <= 8)
                crc = model->table_byte[crc];
            else
                crc = (crc << 8) ^
                      model->table_byte[(crc >> (model->width - 8)) & 0xff];
            crc ^= xor;
            model->table_word[n][k] = opp ? swap(crc << top) : crc << top;
        }
    }
}

word_t crc_wordwise(model_t *model, word_t crc, void const *dat, size_t len)
{
    unsigned char const *buf = dat;
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
                crc ^= *(word_t const *)buf;
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
                crc ^= *(word_t const *)buf;
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
    return crc;
}
