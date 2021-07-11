/* crcgen.c -- Generate CRC code
 * Copyright (C) 2014, 2016, 2017, 2020 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "crc.h"
#include "crcgen.h"

// printf() directive to print a uintmax_t in hexadecimal (e.g. "llx" or "jx").
#define X PRIxMAX

// Maximum line length (not including new line character) for printing tables.
#define COLS 84

// Mask value below which to print in decimal in generated code.
#define DEC 10

// See crcgen.h.
int rev_gen(int bits, FILE *src) {
    // Check for a valid argument.
    if (bits < 2 || bits > 64)
        return 1;

    // Pick the argument and return type.
    char *type;
    if (bits <= 8)
        type = "uint8_t";
    else if (bits <= 16)
        type = "uint16_t";
    else if (bits <= 32)
        type = "uint32_t";
    else
        type = "uint64_t";

    // Open function with header.
    fprintf(src,
            "\n"
            "static inline %s revlow%d(%s val) {\n",
            type, bits, type);

    // Pick optimal approach, based on counting arithmetic operations. For some
    // values of bits, it is fewer operations to reverse the next power of two
    // bits, and then shift down.
    int down = 0;
    if (bits == 31) {
        down = 1;
        bits = 32;
    }
    else if (bits == 47 || bits == 55 || bits == 59 || bits == 61 ||
             bits == 62 || bits == 63) {
        down = 64 - bits;
        bits = 64;
    }

    // Mask of the low bits bits.
    uintmax_t all = (((uintmax_t)1 << (bits - 1)) << 1) - 1;

    // Generate shift, mask, and combine operations.
    uintmax_t kept = 0;         // accumulator of the kept middle bits
    uintmax_t mask = all;       // mask for left shift at each step
    do {
        // bits becomes the number of bits in each segment that is moved, and
        // mid becomes 1 if there is a middle bit between segments to keep --
        // the amount to shift becomes bits + mid
        int mid = bits & 1;
        bits >>= 1;

        if (mid) {
            // compute the locations of the new middle bits
            uintmax_t keep = (mask >> bits) ^ (mask >> (bits + 1));

            // create or update mid variable in generated code with middle bits
            // to keep
            if (kept)
                fprintf(src,
            "    mid |=");                              // update middle bits
            else
                fprintf(src,
            "    %s mid =", type);                      // save middle bits
            if (keep < DEC)
                fprintf(src, " val & %ju;\n", keep);
            else
                fprintf(src, " val & 0x%jx;\n", keep);

            // kept is the accumuation of middle bits so far
            kept |= keep;
        }

        // update mask with the bits to keep for the left shift
        mask ^= mask >> (bits + mid);

        // compute the masks for the left and right shifts, removing the middle
        // bits
        uintmax_t left = mask & ~kept;
        uintmax_t right = all ^ kept ^ left;

        // update the value in the generated code with the masked shifts
        if (right < DEC)
            fprintf(src,
            "    val = ((val >> %d) & %ju) | ", bits + mid, right);
        else
            fprintf(src,
            "    val = ((val >> %d) & 0x%jx) | ", bits + mid, right);
        if (left < DEC)
            fprintf(src, "((val << %d) & %ju);\n", bits + mid, left);
        else
            fprintf(src, "((val << %d) & 0x%jx);\n", bits + mid, left);
    } while (bits > 1);

    // Finish with returned value and close function.
    if (down)
        fprintf(src,
            "    return val >> %d;\n}\n", down);
    else
        fputs(kept ?
            "    return val | mid;\n"
            "}\n" :
            "    return val;\n"
            "}\n", src);
    return 0;
}

// See crcgen.h.
int crc_gen(model_t *model, char *name,
                   unsigned little, unsigned word_bits,
                   FILE *head, FILE *code) {
    // check input -- if invalid, do nothing
    if ((word_bits != 32 && word_bits != 64) || model->width > word_bits)
        return 1;

    // fill in the combine table
    crc_table_combine(model);

    // select the unsigned integer type to be used for CRC calculations
    char *crc_type;
    unsigned crc_bits;
    if (model->width <= 8) {
        crc_type = "uint8_t";
        crc_bits = 8;
    }
    else if (model->width <= 16) {
        crc_type = "uint16_t";
        crc_bits = 16;
    }
    else if (model->width <= 32) {
        crc_type = "uint32_t";
        crc_bits = 32;
    }
    else {
        crc_type = "uint64_t";
        crc_bits = 64;
    }

    // set the unsigned integer type to be used internally by word-wise CRC
    // calculation routines -- this size is fetched from memory and
    // exlusive-ored to the CRC at each step of the word-wise calculation
    char *word_type = word_bits == 32 ? "uint32_t" : "uint64_t";
    unsigned word_bytes = word_bits >> 3;
    unsigned word_shift = 0;
    for (unsigned n = word_bytes; n > 1; n >>= 1)
        word_shift++;

    // provide usage information in the header, and define the integer types
    fprintf(head,
        "// The _bit, _byte, and _word routines return the CRC of the len bytes at mem,\n"
        "// applied to the previous CRC value, crc. If mem is NULL, then the other\n"
        "// arguments are ignored, and the initial CRC, i.e. the CRC of zero bytes, is\n"
        "// returned. Those routines will all return the same result, differing only in\n"
        "// speed and code complexity. The _rem routine returns the CRC of the remaining\n"
        "// bits in the last byte, for when the number of bits in the message is not a\n"
        "// multiple of eight. The %s bits bits of the low byte of val are applied to\n"
        "// crc. bits must be in 0..8.\n"
        "\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n", model->ref ? "low" : "high");

    // include that header in the code
    fprintf(code,
        "#include \"%s.h\"\n", name);
    if (model->back == -1)
        fputs(
        "#include <assert.h>\n", code);

    // function to reverse the low model->width bits, if needed (unlikely)
    if (model->rev)
        rev_gen(model->width, code);

    // bit-wise CRC calculation function
    fprintf(head,
        "\n"
        "// Compute the CRC a bit at a time.\n"
        "%s %s_bit(%s crc, void const *mem, size_t len);\n",
            crc_type, name, crc_type);
    fprintf(code,
        "\n"
        "%s %s_bit(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n", crc_type, name, crc_type, model->init);
    if (model->xorout) {
        if (model->xorout == ONES(model->width))
            fputs(
        "    crc = ~crc;\n", code);
        else
            fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
    if (model->rev)
        fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    if (model->ref) {
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        fprintf(code,
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc ^= data[i];\n"
        "        for (unsigned k = 0; k < 8; k++) {\n"
        "            crc = crc & 1 ? (crc >> 1) ^ %#"X" : crc >> 1;\n"
        "        }\n"
        "    }\n", model->poly);
        if (model->rev)
            fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) &&
                crc_bits == model->width)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
    }
    else if (model->width <= 8) {
        if (model->width < 8)
            fprintf(code,
        "    crc <<= %u;\n", 8 - model->width);
        fprintf(code,
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc ^= data[i];\n"
        "        for (unsigned k = 0; k < 8; k++) {\n"
        "            crc = crc & 0x80 ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "        }\n"
        "    }\n", model->poly << (8 - model->width));
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && !model->rev)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout << (8 - model->width));
        }
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", 8 - model->width);
        if (model->rev)
            fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    }
    else {
        fprintf(code,
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc ^= (%s)data[i] << %u;\n"
        "        for (unsigned k = 0; k < 8; k++) {\n"
        "            crc = crc & %#"X" ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "        }\n"
        "    }\n",
                crc_type, model->width - 8, (word_t)1 << (model->width - 1),
                model->poly);
        if (model->rev)
            fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && !model->rev)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    fputs("    return crc;\n"
          "}\n", code);

    // bit-wise CRC calculation function for a small number of bits (0..8)
    fprintf(head,
        "\n"
        "// Compute the CRC of the %s bits bits in %sval.\n"
        "%s %s_rem(%s crc, unsigned val, unsigned bits);\n",
            model->ref ? "low" : "high",
            model->ref ? "" : "the low byte of ",
            crc_type, name, crc_type);
    fprintf(code,
        "\n"
        "%s %s_rem(%s crc, unsigned val, unsigned bits) {\n",
            crc_type, name, crc_type);
    if (model->xorout) {
        if (model->xorout == ONES(model->width))
            fputs(
        "    crc = ~crc;\n", code);
        else
            fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
    if (model->rev)
        fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    if (model->ref) {
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        fprintf(code,
        "    val &= (1U << bits) - 1;\n"
        "    crc ^= val;\n"
        "    for (unsigned i = 0; i < bits; i++) {\n"
        "        crc = crc & 1 ? (crc >> 1) ^ %#"X" : crc >> 1;\n"
        "    }\n", model->poly);
        if (model->rev)
            fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) &&
                crc_bits == model->width)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
    }
    else if (model->width <= 8) {
        if (model->width < 8)
            fprintf(code,
        "    crc <<= %u;\n", 8 - model->width);
        fprintf(code,
        "    val &= 0x100 - (0x100 >> bits);\n"
        "    crc ^= val;\n"
        "    for (unsigned i = 0; i < bits; i++) {\n"
        "        crc = crc & 0x80 ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "    }\n", model->poly << (8 - model->width));
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && !model->rev)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout << (8 - model->width));
        }
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", 8 - model->width);
        if (model->rev)
            fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    }
    else {
        fprintf(code,
        "    val &= 0x100 - (0x100 >> bits) ;\n"
        "    crc ^= (%s)val << %u;\n"
        "    for (unsigned i = 0; i < bits; i++) {\n"
        "        crc = crc & %#"X" ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "    }\n",
                crc_type, model->width - 8, (word_t)1 << (model->width - 1),
                model->poly);
        if (model->rev)
            fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && !model->rev)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    fputs(
        "    return crc;\n"
        "}\n", code);

    // generate byte-wise and word-wise tables
    crc_table_wordwise(model, little, word_bits);

    // byte-wise table
    if ((little && (model->ref || model->width <= 8)) ||
        (!little && !model->ref && model->width == word_bits))
        fputs(
        "\n"
        "#define table_byte table_word[0]\n", code);
    else {
        fprintf(code,
        "\n"
        "static %s const table_byte[] = {\n", crc_type);
        {
            word_t most = 0;
            for (unsigned k = 0; k < 256; k++)
                if (model->table_byte[k] > most)
                    most = model->table_byte[k];
            int hex = most > 9;
            int digits = 0;
            while (most) {
                most >>= 4;
                digits++;
            }
            char const *pre = "   ";    // this plus one space is line prefix
            unsigned const max = COLS;  // maximum length before new line
            unsigned n = 0;             // characters on this line, so far
            for (unsigned k = 0; k < 255; k++) {
                if (n == 0)
                    n += fprintf(code, "%s", pre);
                n += fprintf(code, " %s%0*"X",",
                             hex ? "0x" : "", digits, model->table_byte[k]);
                if (n + digits + (hex ? 4 : 2) > max) {
                    putc('\n', code);
                    n = 0;
                }
            }
            fprintf(code, "%s %s%0*"X, n ? "" : pre,
                    hex ? "0x" : "", digits, model->table_byte[255]);
        }
        fputs(
        "\n"
        "};\n", code);
    }

    // word-wise table
    fprintf(code,
        "\n"
        "static %s const table_word[][256] = {\n",
            little ? crc_type : word_type);
    {
        word_t most = 0;
        for (unsigned j = 0; j < word_bytes; j++)
            for (unsigned k = 0; k < 256; k++)
                if (model->table_word[j][k] > most)
                    most = model->table_word[j][k];
        int hex = most > 9;
        int digits = 0;
        while (most) {
            most >>= 4;
            digits++;
        }
        char const *pre = "   ";        // this plus one space is line prefix
        unsigned const max = COLS;      // maximum length before new line
        unsigned n = 0;                 // characters on this line, so far
        for (unsigned j = 0; j < word_bytes; j++) {
            for (unsigned k = 0; k < 256; k++) {
                if (n == 0)
                    n += fprintf(code, "%s", pre);
                n += fprintf(code, "%s%s%0*"X"%s",
                             k ? " " : "{", hex ? "0x" : "", digits,
                             model->table_word[j][k],
                             k != 255 ? "," :
                                        j != word_bytes - 1 ? "}," : "}");
                if (n + digits + (hex ? 5 : 3) > max || k == 255) {
                    putc('\n', code);
                    n = 0;
                }
            }
        }
    }
    fputs(
        "};\n", code);

    // byte-wise CRC calculation function
    fprintf(head,
        "\n"
        "// Compute the CRC a byte at a time.\n"
        "%s %s_byte(%s crc, void const *mem, size_t len);\n",
            crc_type, name, crc_type);
    fprintf(code,
        "\n"
        "%s %s_byte(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n", crc_type, name, crc_type, model->init);
    if (model->rev)
        fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    if (model->ref) {
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        if (model->width > 8)
            fputs(
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc = (crc >> 8) ^\n"
        "              table_byte[(crc ^ data[i]) & 0xff];\n"
        "    }\n", code);
        else
            fputs(
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc = table_byte[crc ^ data[i]];\n"
        "    }\n", code);

    }
    else if (model->width <= 8) {
        if (model->width < 8)
            fprintf(code,
        "    crc <<= %u;\n", 8 - model->width);
        fputs(
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc = table_byte[crc ^ data[i]];\n"
        "    }\n", code);
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", 8 - model->width);
    }
    else {
        fprintf(code,
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc = (crc << 8) ^\n"
        "              table_byte[((crc >> %u) ^ data[i]) & 0xff];\n"
        "    }\n", model->width - 8);
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    if (model->rev)
        fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    fputs(
        "    return crc;\n"
        "}\n", code);

    // word-wise CRC calculation function
    unsigned shift = model->width <= 8 ? 8 - model->width : model->width - 8;
    if ((little && !model->ref && model->width > 8) ||
        (!little && model->ref)) {
        // function to swap low bytes, just enough to contain CRC for
        // little-endian
        fprintf(code,
        "\n"
        "static inline %s swap%s(%s crc) {\n"
        "    return\n",
        little ? crc_type : word_type, little ? "low" : "max",
        little ? crc_type : word_type);
        uintmax_t pick = 0xff;
        int mid = little ? (model->width - 1) & ~7 : (int)word_bits - 8;
        int last = -mid;
        do {
            fprintf(code,
        "        ((crc & %#"X") << %d) +\n", pick, mid);
            mid -= 16;
            pick <<= 8;
        } while (mid > 0);
        if (mid == 0) {
            fprintf(code,
        "        (crc & %#"X") +\n", pick);
            mid -= 16;
            pick <<= 8;
        }
        while (mid > last) {
            fprintf(code,
        "        ((crc & %#"X") >> %d) +\n", pick, -mid);
            mid -= 16;
            pick <<= 8;
        }
        fprintf(code,
        "        ((crc & %#"X") >> %d);\n"
        "}\n", pick, -mid);
    }
    fprintf(head,
        "\n"
        "// Compute the CRC a word at a time.\n"
        "%s %s_word(%s crc, void const *mem, size_t len);\n",
            crc_type, name, crc_type);
    fprintf(code,
        "\n"
        "// This code assumes that integers are stored %s-endian.\n"
        "\n"
        "%s %s_word(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n",
            little ? "little" : "big", crc_type, name, crc_type, model->init);
    if (model->rev)
        fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);

    // do bytes up to word boundary
    if (model->ref) {
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        fprintf(code,
        "    while (len && ((ptrdiff_t)data & %#x)) {\n", word_bytes - 1);
        if (model->width > 8)
            fputs(
        "        len--;\n"
        "        crc = (crc >> 8) ^\n"
        "              table_byte[(crc ^ *data++) & 0xff];\n", code);
        else
            fputs(
        "        len--;\n"
        "        crc = table_byte[crc ^ *data++];\n", code);
        fputs(
        "    }\n", code);
    }
    else if (model->width <= 8) {
        if (model->width < 8)
            fprintf(code,
        "    crc <<= %u;\n", shift);
        fprintf(code,
        "    while (len && ((ptrdiff_t)data & %#x)) {\n"
        "        len--;\n"
        "        crc = table_byte[crc ^ *data++];\n"
        "    }\n", word_bytes - 1);
    }
    else {
        fprintf(code,
        "    while (len && ((ptrdiff_t)data & %#x)) {\n"
        "        len--;\n"
        "        crc = (crc << 8) ^\n"
        "              table_byte[((crc >> %u) ^ *data++) & 0xff];\n"
        "    }\n", word_bytes - 1, shift);
    }

    // do full words for little-endian
    if (little) {
        unsigned top = model->width > 8 ? -model->width & 7 : 0;
        if (!model->ref) {
            if (top)
                fprintf(code,
        "    crc <<= %u;\n", top);
            if (model->width > 8)
                fputs(
                      "    crc = swaplow(crc);\n", code);
        }
        fprintf(code,
        "    size_t n = len >> %u;\n"
        "    for (size_t i = 0; i < n; i++) {\n"
        "        %s word = crc ^ ((%s const *)data)[i];\n"
        "        crc = table_word[%u][word & 0xff] ^\n",
                word_shift, word_type, word_type, word_bytes - 1);
        for (unsigned k = 1; k < word_bytes - 1; k++) {
            fprintf(code,
        "              table_word[%u][(word >> %u) & 0xff] ^\n",
                    word_bytes - k - 1, k << 3);
        }
        fprintf(code,
        "              table_word[0][word >> %u];\n"
        "    }\n"
        "    data += n << %u;\n"
        "    len &= %u;\n",
                (word_bytes - 1) << 3, word_shift, word_bytes - 1);
        if (!model->ref) {
            if (model->width > 8)
                fputs(
        "    crc = swaplow(crc);\n", code);
            if (top)
                fprintf(code,
        "    crc >>= %u;\n", top);
        }
    }

    // do full words for big-endian
    else {
        unsigned top = model->ref ? 0 :
                       word_bits - (model->width > 8 ? model->width : 8);
        if (model->ref)
            fprintf(code,
        "    %s word = swapmax(crc);\n", word_type);
        else
            fprintf(code,
        "    %s word = (%s)crc << %u;\n", word_type, word_type, top);
        fprintf(code,
        "    size_t n = len >> %u;\n"
        "    for (size_t i = 0; i < n; i++) {\n"
        "        word ^= ((%s const *)data)[i];\n"
        "        word = table_word[0][word & 0xff] ^\n",
                word_shift, word_type);
        for (unsigned k = 1; k < word_bytes - 1; k++) {
            fprintf(code,
        "               table_word[%u][(word >> %u) & 0xff] ^\n", k, k << 3);
        }
        fprintf(code,
        "               table_word[%u][word >> %u];\n"
        "    };\n"
        "    data += n << %u;\n"
        "    len &= %u;\n",
                word_bytes - 1, (word_bytes - 1) << 3, word_shift,
                word_bytes - 1);
        if (model->ref)
            fputs(
        "    crc = swapmax(word);\n", code);
        else
            fprintf(code,
        "    crc = word >> %u;\n", top);
    }

    // do last few bytes
    if (model->ref) {
        if (model->width > 8)
            fputs(
        "    while (len) {\n"
        "        len--;\n"
        "        crc = (crc >> 8) ^\n"
        "              table_byte[(crc ^ *data++) & 0xff];\n"
        "    }\n", code);
        else
            fputs(
        "    while (len) {\n"
        "        len--;\n"
        "        crc = table_byte[crc ^ *data++];\n"
        "    }\n", code);
    }
    else if (model->width <= 8) {
        fputs(
        "    while (len) {\n"
        "        len--;\n"
        "        crc = table_byte[crc ^ *data++];\n"
        "    }\n", code);
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", shift);
    }
    else {
        fprintf(code,
        "    while (len) {\n"
        "        len--;\n"
        "        crc = (crc << 8) ^\n"
        "              table_byte[((crc >> %u) ^ *data++) & 0xff];\n"
        "    }\n",
        shift);
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    if (model->rev)
        fprintf(code,
        "    crc = revlow%d(crc);\n", model->width);
    fputs(
        "    return crc;\n"
        "}\n", code);

    // Multiply mod poly for CRC combination.
    if (model->ref)
        fprintf(code,
        "\n"
        "static %s multmodp(%s a, %s b) {\n"
        "    %s prod = 0;\n"
        "    for (;;) {\n"
        "        if (a & %#"X") {\n"
        "            prod ^= b;\n"
        "            if ((a & %#"X") == 0)\n"
        "                break;\n"
        "        }\n"
        "        a <<= 1;\n"
        "        b = b & 1 ? (b >> 1) ^ %#"X" : b >> 1;\n"
        "    }\n"
        "    return prod;\n"
        "}\n",
            crc_type, crc_type, crc_type, crc_type,
                (word_t)1 << (model->width - 1),
                ((word_t)1 << (model->width - 1)) - 1, model->poly);
    else {
        fprintf(code,
        "\n"
        "static %s multmodp(%s a, %s b) {\n"
        "    %s prod = 0;\n"
        "    for (;;) {\n"
        "        if (a & 1) {\n"
        "            prod ^= b;\n"
        "            if (a == 1)\n"
        "                break;\n"
        "        }\n"
        "        a >>= 1;\n"
        "        b = b & %#"X" ? (b << 1) ^ %#"X" : b << 1;\n"
        "    }\n",
            crc_type, crc_type, crc_type, crc_type,
            (word_t)1 << (model->width - 1), model->poly);
        if (model->width != crc_bits)
            fprintf(code,
        "    prod &= %#"X";\n", ONES(model->width));
        fputs(
        "    return prod;\n"
        "}\n", code);
    }

    // CRC combination table.
    fprintf(head,
        "\n"
        "// Compute the combination of two CRCs.\n"
        "%s %s_comb(%s crc1, %s crc2, uintmax_t len2);\n",
            crc_type, name, crc_type, crc_type);
    fprintf(code,
        "\n"
        "static %s const table_comb[] = {\n", crc_type);
    {
        word_t most = 0;
        int len = model->cycle;
        for (int k = 0; k < len; k++)
            if (model->table_comb[k] > most)
                most = model->table_comb[k];
        int hex = most > 9;
        int digits = 0;
        while (most) {
            most >>= 4;
            digits++;
        }
        char const *pre = "   ";    // this plus one space is line prefix
        unsigned const max = COLS;  // maximum length before new line
        unsigned n = 0;             // characters on this line, so far
        for (int k = 0; k < len - 1; k++) {
            if (n == 0)
                n += fprintf(code, "%s", pre);
            n += fprintf(code, " %s%0*"X",",
                         hex ? "0x" : "", digits, model->table_comb[k]);
            if (n + digits + (hex ? 4 : 2) > max) {
                putc('\n', code);
                n = 0;
            }
        }
        fprintf(code, "%s %s%0*"X, n ? "" : pre,
                hex ? "0x" : "", digits, model->table_comb[len - 1]);
    }
    fputs(
        "\n"
        "};\n", code);

    // Calculate x^(8n) mod poly for CRC combination.
    fprintf(code,
        "\n"
        "static %s x8nmodp(uintmax_t n) {\n", crc_type);
    if (model->ref)
        fprintf(code,
        "    %s xp = %#"X";\n",
        crc_type, (word_t)1 << (model->width - 1));
    else
        fprintf(code,
        "    %s xp = 1;\n", crc_type);
    fprintf(code,
        "    int k = %d;\n", model->cycle > 3 ? 3 :
                             model->cycle == 3 ? model->back :
                             model->cycle - 1);
    fputs(
        "    for (;;) {\n"
        "        if (n & 1)\n"
        "            xp = multmodp(table_comb[k], xp);\n"
        "        n >>= 1;\n"
        "        if (n == 0)\n"
        "            break;\n", code);
    if (model->back != -1)
        fprintf(code,
        "        if (++k == %d)\n"
        "            k = %d;\n",
                model->cycle, model->back);
    else
        fprintf(code,
        "        k++;\n"
        "        assert(k < %d);\n", model->cycle);
    fputs(
        "    }\n"
        "    return xp;\n"
        "}\n", code);

    // Combine CRCs.
    fprintf(code,
        "\n"
        "%s %s_comb(%s crc1, %s crc2,\n"
        "        uintmax_t len2) {\n",
        crc_type, name, crc_type, crc_type);
    if (model->init)
        fprintf(code,
        "    crc1 ^= %#"X";\n",
        model->init);
    if (model->rev)
        fprintf(code,
        "    return revlow%u(multmodp(x8nmodp(len2), revlow%u(crc1)) ^ revlow%u(crc2));\n",
            model->width, model->width, model->width);
    else
        fputs(
        "    return multmodp(x8nmodp(len2), crc1) ^ crc2;\n", code);
    fputs(
        "}\n", code);
    return 0;
}
