/*
  crcgen version 1.7, 23 December 2017

  Copyright (C) 2016, 2017 Mark Adler

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
   1.0  17 Jul 2016  First version (bit-wise and byte-wise only)
   1.1  18 Jul 2016  Improve generated code
   1.2  22 Jul 2016  Add word-wise code generation
                     Define WORD_BIT and LONG_BIT for non-posix-compliant
                     Add comments in .h files with architecture assumptions
                     Add random data testing from middle to middle of words
                     Test CRC header files as well
                     Use word table for byte table when possible
   1.3  24 Jul 2016  Build xorout into the tables
                     Use word table for byte table for 8-bit or less CRCs
                     Avoid use of uintmax_t outside loop for little endian
                     Improve bit reverse function
   1.4  30 Jul 2016  Avoid generating byte-wise table twice in crcgen
                     Fix a bug in word-wise table generation
                     Use void * type to pass crc data
                     Use x = ~x instead of x ^= 0xff... where appropriate
   1.5  23 Oct 2016  Improve use of data types and C99 compatibility
   1.6  11 Feb 2017  Add creation of remaining bits function (_rem)
                     Improve the generated comments and prototypes
   1.7  23 Dec 2017  Update to the latest CRC catalog
                     Minor improvements to code generation
 */

/* Generate C code to compute the given CRC. This generates code that will work
   with the architecture and compiler that it is being run on. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "model.h"
#include "crc.h"

// Define INTMAX_BITS.
#ifndef INTMAX_BITS
#  if INTMAX_MAX == 2147483647
#    define INTMAX_BITS 32
#  elif INTMAX_MAX == 9223372036854775807
#    define INTMAX_BITS 64
#  else
#    error Unexpected maximum integer size
#  endif
#endif

// printf() directive to print a uintmax_t in hexadecimal (e.g. "llx" or "jx").
#define X PRIxMAX

// Maximum line length (not including new line character) for printing tables.
#define COLS 84

// Generate the header and code for the CRC described in model. name is the
// prefix used for all externally visible names in the source files. The
// generated code is written to head and code. The generated word-wise CRC code
// uses the endianess little (1 for little endian, 0 for big endian), and the
// word size in bits word_bits, which must be 32 or 64. The width of the CRC in
// model must be less than or equal to word_bits. Return 0 on success, non-zero
// if word_bits and model->width are invalid.
static int crc_gen(model_t *model, char *name,
                   unsigned little, unsigned word_bits,
                   FILE *head, FILE *code) {
    // check input -- if invalid, do nothing
    if ((word_bits != 32 && word_bits != 64) || model->width > word_bits)
        return 1;

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

    // include the header in the code
    fprintf(code,
        "#include \"%s.h\"\n", name);

    // function to reverse the low model->width bits, if needed (unlikely)
    if (model->rev) {
        fprintf(code,
        "\n"
        "static inline %s revlow(%s crc) {\n", crc_type, crc_type);
        unsigned dist = crc_bits;
        uintmax_t mask = (((uintmax_t)1 << (dist - 1)) << 1) - 1;
        uintmax_t pick = mask;
        while (dist >>= 1) {
            pick ^= pick << dist;
            fprintf(code,
        "    crc = ((crc >> %u) & %#"X") + ((crc << %u) & %#"X");\n",
                    dist, pick & mask, dist, ~pick & mask);
        }
        if (crc_bits != model->width)
            fprintf(code,
        "    return crc >> %u;\n"
        "}\n", crc_bits - model->width);
        else
            fputs(
        "    return crc;\n"
        "}\n", code);
    }

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
        fputs(
        "    crc = revlow(crc);\n", code);
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
            fputs(
        "    crc = revlow(crc);\n", code);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && crc_bits == model->width)
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
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", 8 - model->width);
        if (model->rev)
            fputs(
        "    crc = revlow(crc);\n", code);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && !model->rev)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
        if (!model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    else {
        fprintf(code,
        "    for (size_t i = 0; i < len; i++) {\n"
        "        crc ^= (%s)data[i] << %u;\n"
        "        for (unsigned k = 0; k < 8; k++) {\n"
        "            crc = crc & %#"X" ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "        }\n"
        "    }\n",
                crc_type, model->width - 8, (word_t)1 << (model->width - 1), model->poly);
        if (model->rev)
            fputs(
        "    crc = revlow(crc);\n", code);
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
        "%s %s_rem(%s crc, unsigned val, unsigned bits) {\n", crc_type, name, crc_type);
    if (model->xorout) {
        if (model->xorout == ONES(model->width))
            fputs(
        "    crc = ~crc;\n", code);
        else
            fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
    if (model->rev)
        fputs(
        "    crc = revlow(crc);\n", code);
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
            fputs(
        "    crc = revlow(crc);\n", code);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && crc_bits == model->width)
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
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", 8 - model->width);
        if (model->rev)
            fputs(
        "    crc = revlow(crc);\n", code);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && !model->rev)
                fputs(
        "    crc = ~crc;\n", code);
            else
                fprintf(code,
        "    crc ^= %#"X";\n", model->xorout);
        }
        if (!model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    else {
        fprintf(code,
        "    val &= 0x100 - (0x100 >> bits) ;\n"
        "    crc ^= (%s)val << %u;\n"
        "    for (unsigned i = 0; i < bits; i++) {\n"
        "        crc = crc & %#"X" ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "    }\n", crc_type, model->width - 8, (word_t)1 << (model->width - 1), model->poly);
        if (model->rev)
            fputs(
        "    crc = revlow(crc);\n", code);
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
            char const *pre = "   ";        // this plus one space is line prefix
            unsigned const max = COLS;      // maximum length before new line
            unsigned n = 0;                 // characters on this line, so far
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
        "static %s const table_word[][256] = {\n", little ? crc_type : word_type);
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
                             k != 255 ? "," : j != word_bytes - 1 ? "}," : "}");
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
        "%s %s_byte(%s crc, void const *mem, size_t len);\n", crc_type, name, crc_type);
    fprintf(code,
        "\n"
        "%s %s_byte(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n", crc_type, name, crc_type, model->init);
    if (model->rev)
        fputs(
        "    crc = revlow(crc);\n", code);
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
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
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
        fputs(
        "    crc = revlow(crc);\n", code);
    fputs("    return crc;\n"
          "}\n", code);

    // word-wise CRC calculation function
    unsigned shift = model->width <= 8 ? 8 - model->width : model->width - 8;
    if ((little && !model->ref && model->width > 8) || (!little && model->ref)) {
        // function to swap low bytes, just enough to contain CRC for little-endian
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
        fputs(
        "    crc = revlow(crc);\n", code);

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
        if (model->width != crc_bits && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
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
        unsigned top = model->ref ? 0 : word_bits - (model->width > 8 ? model->width : 8);
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
                word_bytes - 1, (word_bytes - 1) << 3, word_shift, word_bytes - 1);
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
        fputs(
        "    crc = revlow(crc);\n", code);
    fputs(
        "    return crc;\n"
        "}\n", code);
    return 0;
}

// Generate test code for model and name. Append the include for the header
// file for this model to defs, test code for each function for this model to
// test, a unified interface to the word-wise function of this model to allc,
// and a table of names, widths, and function pointers to allh. The test code
// computes the CRC of "123456789" (nine bytes), and compares that to the
// provided check value. If the check value does not match the computed CRC,
// then the generated code prints an error to stderr.
static int test_gen(model_t *model, char *name,
                    FILE *defs, FILE *test, FILE *allc, FILE *allh) {
    // write test and all code for bit-wise function
    fprintf(defs,
        "#include \"%s.h\"\n", name);
    fprintf(test,
        "\n"
        "    // %s\n"
        "    init = %s_bit(0, NULL, 0);\n"
        "    if (%s_bit(init, \"123456789\", 9) != %#"X")\n"
        "        fputs(\"bit-wise mismatch for %s\\n\", stderr), err++;\n"
        "    crc = %s_bit(init, data + 1, sizeof(data) - 1);\n",
            name, name, name, model->check, name, name);
    fprintf(allc,
        "\n"
        "#include \"%s.h\"\n"
        "uintmax_t %s(uintmax_t crc, void const *mem, size_t len) {\n"
        "    return %s_word(crc, mem, len);\n"
        "}\n", name, name, name);
    fprintf(allh,
        "    {\"%s\", \"", model->name);
    for (char *p = name + 3; *p; p++)
        if (isalnum(*p))
            putc(*p, allh);
    fprintf(allh,
        "\", %u, %s},\n", model->width, name);

    // write test code for small number of bits function
    if (model->ref)
        fprintf(test,
            "    if (%s_bit(init, \"\\xda\", 1) !=\n"
            "        %s_rem(%s_rem(init, 0xda, 3), 0x1b, 5))\n"
            "        fputs(\"small bits mismatch for %s\\n\", stderr), err++;\n",
                name, name, name, name);
    else
        fprintf(test,
            "    if (%s_bit(init, \"\\xda\", 1) !=\n"
            "        %s_rem(%s_rem(init, 0xda, 3), 0xd0, 5))\n"
            "        fputs(\"small bits mismatch for %s\\n\", stderr), err++;\n",
                name, name, name, name);

    // write test code for byte-wise function
    fprintf(test,
        "    if (%s_byte(0, NULL, 0) != init ||\n"
        "        %s_byte(init, \"123456789\", 9) != %#"X" ||\n"
        "        %s_byte(init, data + 1, sizeof(data) - 1) != crc)\n"
        "        fputs(\"byte-wise mismatch for %s\\n\", stderr), err++;\n",
            name, name, model->check, name, name);

    // write test code for word-wise function
    fprintf(test,
        "    if (%s_word(0, NULL, 0) != init ||\n"
        "        %s_word(init, \"123456789\", 9) != %#"X" ||\n"
        "        %s_word(init, data + 1, sizeof(data) - 1) != crc)\n"
        "        fputs(\"word-wise mismatch for %s\\n\", stderr), err++;\n",
            name, name, model->check, name, name);
    return 0;
}

// Make a base name for the CRC routines and source files, making use of the
// name in the model description. All names start with "crc*", where "*" is
// replaced by the number of bits in the CRC. The returned name is allocated
// space, or NULL if there was an error. This transformation is tuned to the
// names that appear in the RevEng CRC catalogue.
static char *crc_name(model_t *model) {
    char *id = model->name;
    char *name = malloc(8 + strlen(id));
    if (name == NULL)
        return NULL;
    char *next = stpcpy(name, "crc");
    next += sprintf(next, "%u", model->width);
    if (strncasecmp(id, "crc", 3) == 0) {
        id += 3;
        if (*id == '-')
            id++;
        while (*id >= '0' && *id <= '9')
            id++;
        if (*id == '/')
            id++;
    }
    if (*id) {
        char *suff = next;
        do {
            if (isalnum(*id)) {
                if (next == suff && isdigit(*id))
                    *next++ = '_';
                *next++ = tolower(*id);
            }
            else if (*id == '-')
                *next++ = '_';
        } while (*++id);
    }
    *next = 0;
    return name;
}

// Create the src directory if necessary, and create src/name.h and src/name.c
// source files for writing, returning their handles in *head and *code
// respectively. If head or code is NULL, then the respective file is not
// created. If a file by either of those names already exists, then an error is
// returned. A failure to create the directory or writable files will return
// true, with no open handles and *head and *code containing NULL. If the
// problem was a source file that already existed, then create_source() will
// return 2. Otherwise it will return 1 on error, 0 on success.
static int create_source(char *src, char *name, FILE **head, FILE **code) {
    // for error return
    if (head != NULL)
        *head = NULL;
    if (code != NULL)
        *code = NULL;

    // create the src directory if it does not exist
    int ret = mkdir(src, 0755);
    if (ret && errno != EEXIST)
        return 1;

    // construct the path for the source files, leaving suff pointing to the
    // position for the 'h' or 'c'.
    char path[strlen(src) + 1 + strlen(name) + 2 + 1];
    char *suff = stpcpy(path, src);
    *suff++ = '/';
    suff = stpcpy(suff, name);
    *suff++ = '.';
    suff[1] = 0;

    // create header file
    if (head != NULL) {
        *suff = 'h';
        *head = fopen(path, "wx");
        if (*head == NULL)
            return errno == EEXIST ? 2 : 1;
    }

    // create code file
    if (code != NULL) {
        *suff = 'c';
        *code = fopen(path, "wx");
        if (*code == NULL) {
            int err = errno;
            if (head != NULL) {
                fclose(*head);
                *head = NULL;
                *suff = 'h';
                unlink(path);
            }
            return err == EEXIST ? 2 : 1;
        }
    }

    // all good -- return handles for header and code
    return 0;
}

// Subdirectory for source files.
#define SRC "src"

// Read CRC models from stdin, one per line, and generate C tables and routines
// to compute each one. Each CRC goes into it's own .h and .c source files in
// the "src" subdirectory of the current directory.
int main(void) {
    // determine endianess of this machine (for testing on this machine, we
    // need to match its endianess)
    unsigned little = 1;
    little = *((unsigned char *)(&little));

    // create test source files
    FILE *defs, *test, *allc, *allh;
    if (create_source(SRC, "test_src", &defs, &test) ||
        create_source(SRC, "allcrcs", &allh, &allc)) {
        fputs("could not create test code files -- aborting\n", stderr);
        return 1;
    }
    fputs(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdint.h>\n"
        "#include <time.h>\n"
        "#include \"test_src.h\"\n"
        "\n"
        "int main(void) {\n"
        "    unsigned char data[31];\n"
        "    srandom(time(NULL));\n"
        "    {\n"
        "        uint64_t ran = 1;\n"
        "        size_t n = sizeof(data);\n"
        "        do {\n"
        "            if (ran < 0x100)\n"
        "                ran = (ran << 31) + random();\n"
        "            data[--n] = ran;\n"
        "            ran >>= 8;\n"
        "        } while (n);\n"
        "    }\n"
        "    uintmax_t init, crc;\n"
        "    int err = 0;\n", test);
    fputs(
        "#include <stdint.h>\n", allc);
    fputs(
        "\n"
        "typedef uintmax_t (*crc_f)(uintmax_t, void const *, size_t);\n"
        "\n"
        "struct {\n"
        "    char const *name;\n"
        "    char const *match;\n"
        "    unsigned short width;\n"
        "    crc_f func;\n"
        "} const all[] = {\n", allh);

    // read each line from stdin, process the CRC description
    char *line = NULL;
    size_t size;
    ssize_t len;
    while ((len = getcleanline(&line, &size, stdin)) != -1) {
        if (len == 0)
            continue;
        model_t model;

        // read the model
        model.name = NULL;
        int ret = read_model(&model, line);
        if (ret == 2) {
            fputs("out of memory -- aborting\n", stderr);
            break;
        }
        else if (ret == 1)
            fprintf(stderr, "%s is an unusable model -- skipping\n",
                    model.name);
        else if (model.width > INTMAX_BITS)
            fprintf(stderr, "%s is too wide (%u bits) -- skipping\n",
                    model.name, model.width);
        else {
            // convert the parameters to form for calculation
            process_model(&model);

            // generate the routine name prefix for this model
            char *name = crc_name(&model);
            if (name == NULL) {
                fputs("out of memory -- aborting\n", stderr);
                break;
            }

            // generate the code
            FILE *head, *code;
            int ret = create_source(SRC, name, &head, &code);
            if (ret)
                fprintf(stderr, "%s/%s.[ch] %s -- skipping\n", SRC, name,
                        errno == 1 ? "create error" : "exists");
            else {
                crc_gen(&model, name, little, INTMAX_BITS, head, code);
                test_gen(&model, name, defs, test, allc, allh);
                fclose(code);
                fclose(head);
            }
            free(name);
        }
        free(model.name);
    }
    free(line);

    fputs(
        "    {\"\", \"\", 0, NULL}\n"
        "};\n", allh);
    fclose(allh);
    fclose(allc);
    fputs(
        "\n"
        "    // done\n"
        "    fputs(err ? \"** verification failed\\n\" :\n"
        "                \"-- all good\\n\", stderr);\n"
        "    return 0;\n"
        "}\n", test);
    fclose(test);
    fclose(defs);
    return 0;
}
