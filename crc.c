/* crc.c -- Generic CRC calculations
 * Copyright (C) 2014, 2016, 2017, 2020, 2021 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include <stddef.h>
#include <assert.h>
#include "crc.h"

word_t crc_bitwise(model_t *model, word_t crc, void const *dat, size_t len) {
    unsigned char const *buf = dat;
    word_t poly = model->poly;

    // If requested, return the initial CRC.
    if (buf == NULL)
        return model->init;

    // Pre-process the CRC.
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    // Process the input data a bit at a time.
    if (model->ref) {
        crc &= ONES(model->width);
        while (len--) {
            crc ^= *buf++;
            for (int k = 0; k < 8; k++)
                crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
        }
    }
    else if (model->width <= 8) {
        unsigned shift = 8 - model->width;  // 0..7
        poly <<= shift;
        crc <<= shift;
        while (len--) {
            crc ^= *buf++;
            for (int k = 0; k < 8; k++)
                crc = crc & 0x80 ? (crc << 1) ^ poly : crc << 1;
        }
        crc >>= shift;
        crc &= ONES(model->width);
    }
    else {
        word_t mask = (word_t)1 << (model->width - 1);
        unsigned shift = model->width - 8;  // 1..WORDBITS-8
        while (len--) {
            crc ^= (word_t)(*buf++) << shift;
            for (int k = 0; k < 8; k++)
                crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
        }
        crc &= ONES(model->width);
    }

    // Post-process and return the CRC.
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc ^ model->xorout;
}

void crc_table_bytewise(model_t *model) {
    unsigned char k = 0;
    do {
        word_t crc = crc_bitwise(model, 0, &k, 1);
        if (model->rev)
            crc = reverse(crc, model->width);
        if (model->width < 8 && !model->ref)
            crc <<= 8 - model->width;
        model->table_byte[k] = crc;
    } while (++k);
}

word_t crc_bytewise(model_t *model, word_t crc, void const *dat, size_t len) {
    unsigned char const *buf = dat;

    // If requested, return the initial CRC.
    if (buf == NULL)
        return model->init;

    // Pre-process the CRC.
    if (model->rev)
        crc = reverse(crc, model->width);

    // Process the input data a byte at a time.
    if (model->ref) {
        crc &= ONES(model->width);
        while (len--)
            crc = (crc >> 8) ^ model->table_byte[(crc ^ *buf++) & 0xff];
    }
    else if (model->width <= 8) {
        unsigned shift = 8 - model->width;  // 0..7
        crc <<= shift;
        while (len--)
            crc = model->table_byte[crc ^ *buf++];
        crc >>= shift;
    }
    else {
        unsigned shift = model->width - 8;  // 1..WORDBITS-8
        while (len--)
            crc = (crc << 8) ^
                  model->table_byte[((crc >> shift) ^ *buf++) & 0xff];
        crc &= ONES(model->width);
    }

    // Post-process and return the CRC
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc;
}

// Swap the low n bytes of x. Bytes above those are discarded.
static inline word_t swaplow(word_t x, unsigned n) {
    if (n == 0)
        return 0;
    word_t y = x & 0xff;
    while (--n) {
        x >>= 8;
        y <<= 8;
        y |= x & 0xff;
    }
    return y;
}

// Swap the bytes in a word_t. swap() is used at most twice per crc_wordwise()
// call, and then only on little-endian machines if the CRC is not reflected,
// or on big-endian machines if the CRC is reflected.
static inline word_t swap(word_t x) {
    return swaplow(x, WORDCHARS);
}

void crc_table_wordwise(model_t *model, unsigned little, unsigned word_bits) {
    unsigned opp = little ^ model->ref;
    unsigned top =
        model->ref ? 0 :
                     word_bits - (model->width > 8 ? model->width : 8);
    word_t xor = model->xorout;
    if (model->width < 8 && !model->ref)
        xor <<= 8 - model->width;
    unsigned word_bytes = word_bits >> 3;
    for (unsigned k = 0; k < 256; k++) {
        word_t crc = model->table_byte[k];
        model->table_word[0][k] = opp ? swaplow(crc << top, word_bytes) :
                                        crc << top;
        for (unsigned n = 1; n < (word_bits >> 3); n++) {
            crc ^= xor;
            if (model->ref)
                crc = (crc >> 8) ^ model->table_byte[crc & 0xff];
            else if (model->width <= 8)
                crc = model->table_byte[crc];
            else {
                crc = (crc << 8) ^
                      model->table_byte[(crc >> (model->width - 8)) & 0xff];
                crc &= ONES(model->width);
            }
            crc ^= xor;
            model->table_word[n][k] = opp ? swaplow(crc << top, word_bytes) :
                                            crc << top;
        }
    }
}

word_t crc_wordwise(model_t *model, word_t crc, void const *dat, size_t len) {
    unsigned char const *buf = dat;

    // If requested, return the initial CRC.
    if (buf == NULL)
        return model->init;

    // Prepare common constants.
    unsigned little = 1;
    little = *((unsigned char *)(&little));
    unsigned top = model->ref ? 0 :
                   WORDBITS - (model->width > 8 ? model->width : 8);
    unsigned shift = model->width <= 8 ? 8 - model->width : model->width - 8;

    // Pre-process the CRC.
    if (model->rev)
        crc = reverse(crc, model->width);

    // Process the first few bytes up to a word_t boundary, if any.
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

    // Process as many word_t's as are available.
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

    // Process any remaining bytes after the last word_t.
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

    // Post-process and return the CRC.
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc;
}

// Return a(x) multiplied by b(x) modulo p(x), where p(x) is the CRC
// polynomial. For speed, this requires that a not be zero.
static word_t multmodp(model_t *model, word_t a, word_t b) {
    word_t top = (word_t)1 << (model->width - 1);
    word_t prod = 0;
    if (model->ref) {
        // Reflected polynomial.
        for (;;) {
            if (a & top) {
                prod ^= b;
                if ((a & (top - 1)) == 0)
                    break;
            }
            a <<= 1;
            b = b & 1 ? (b >> 1) ^ model->poly : b >> 1;
        }
    }
    else {
        // Normal polynomial.
        for (;;) {
            if (a & 1) {
                prod ^= b;
                if (a == 1)
                    break;
            }
            a >>= 1;
            b = b & top ? (b << 1) ^ model->poly : b << 1;
        }
        prod &= ((top << 1) - 1);
    }
    return prod;
}

// Build table_comb[] for model. Stop when a cycle is detected, or the table is
// full. On return, model->cycle is the number of entries in the table, which
// is the index at which to cycle. model->back is the index to go to when
// model->cycle is reached. If no cycle was detected, then model->back is -1.
void crc_table_combine(model_t *model) {
    // Keep squaring x^1 modulo p(x), where p(x) is the CRC polynomial, to
    // generate x^2^n modulo p(x).
    word_t sq = model->ref ? (word_t)1 << (model->width - 2) : 2;   // x^1
    model->table_comb[0] = sq;
    int n = 1;
    while ((unsigned)n < sizeof(model->table_comb) / sizeof(word_t)) {
        sq = multmodp(model, sq, sq);       // x^2^n

        // If this value has already appeared, then done.
        for (int j = 0; j < n; j++)
            if (model->table_comb[j] == sq) {
                model->cycle = n;
                model->back = j;
                return;
            }

        // New value -- append to table.
        model->table_comb[n++] = sq;
    }

    // No cycle was found, up to the size of the table.
    model->cycle = n;
    model->back = -1;

#ifdef FIND_CYCLE
#   define GIVEUP 10000
    // Just out of curiosity, see when x^2^n cycles for this CRC.
    word_t comb[GIVEUP];
    for (int k = 0; k < n; k++)
        comb[k] = model->table_comb[k];
    while (n < GIVEUP) {
        sq = multmodp(model, sq, sq);
        for (int j = 0; j < n; j++)
            if (comb[j] == sq) {
                fprintf(stderr, "%s cycled at %u to %u\n",
                        model->name, n, j);
                return;
            }
        comb[n++] = sq;
    }
    fprintf(stderr, "%s never cycled?\n", model->name);
#endif

}

word_t crc_zeros(model_t *model, word_t crc, uintmax_t n) {
    // Pre-process the CRC.
    crc ^= model->xorout;
    if (model->rev)
        crc = reverse(crc, model->width);

    // Apply n zero bits to crc.
    if (n < 128) {
        word_t poly = model->poly;
        if (model->ref) {
            crc &= ONES(model->width);
            while (n--)
                crc = crc & 1 ? (crc >> 1) ^ poly : crc >> 1;
        }
        else {
            word_t mask = (word_t)1 << (model->width - 1);
            while (n--)
                crc = crc & mask ? (crc << 1) ^ poly : crc << 1;
            crc &= ONES(model->width);
        }
    }
    else {
        crc &= ONES(model->width);
        int k = 0;
        for (;;) {
            if (n & 1)
                crc = multmodp(model, model->table_comb[k], crc);
            n >>= 1;
            if (n == 0)
                break;
            if (++k == model->cycle) {
                assert(model->back != -1);
                k = model->back;
            }
        }
    }

    // Post-process and return the CRC.
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc ^ model->xorout;
}

// Return x^(8n) modulo p(x), where p(x) is the CRC polynomial. model->cycle
// and model->table_comb[] must first be initialized by crc_table_combine().
static word_t x8nmodp(model_t *model, uintmax_t n) {
    word_t xp = model->ref ? (word_t)1 << (model->width - 1) : 1;   // x^0
    int k = model->cycle > 3 ? 3 :
            model->cycle == 3 ? model->back :
            model->cycle - 1;
    for (;;) {
        if (n & 1)
            xp = multmodp(model, model->table_comb[k], xp);
        n >>= 1;
        if (n == 0)
            break;
        if (++k == model->cycle) {
            assert(model->back != -1);
            k = model->back;
        }
    }
    return xp;
}

word_t crc_combine(model_t *model, word_t crc1, word_t crc2,
                   uintmax_t len2) {
    crc1 ^= model->init;
    if (model->rev) {
        crc1 = reverse(crc1, model->width);
        crc2 = reverse(crc2, model->width);
    }
    word_t crc = multmodp(model, x8nmodp(model, len2), crc1) ^ crc2;
    if (model->rev)
        crc = reverse(crc, model->width);
    return crc;
}
