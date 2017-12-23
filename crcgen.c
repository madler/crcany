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

// Define WORD_BIT and LONG_BIT for a non-POSIX-compliant limit.h. Also define
// similar constants SHRT_BIT and INTMAX_BIT.
#ifndef SHRT_BIT
#  if SHRT_MAX == 127
#    define SHRT_BIT 8
#  elif SHRT_MAX == 32767
#    define SHRT_BIT 16
#  elif SHRT_MAX == 2147483647
#    define SHRT_BIT 32
#  elif SHRT_MAX == 9223372036854775807
#    define SHRT_BIT 64
#  else
#    error Unexpected integer size
#  endif
#endif
#ifndef WORD_BIT
#  if INT_MAX == 32767
#    define WORD_BIT 16
#  elif INT_MAX == 2147483647
#    define WORD_BIT 32
#  elif INT_MAX == 9223372036854775807
#    define WORD_BIT 64
#  else
#    error Unexpected integer size
#  endif
#endif
#ifndef LONG_BIT
#  if LONG_MAX == 32767
#    define LONG_BIT 16
#  elif LONG_MAX == 2147483647
#    define LONG_BIT 32
#  elif LONG_MAX == 9223372036854775807
#    define LONG_BIT 64
#  else
#    error Unexpected integer size
#  endif
#endif
#ifndef INTMAX_BIT
#  if INTMAX_MAX == 32767
#    define INTMAX_BIT 16
#  elif INTMAX_MAX == 2147483647
#    define INTMAX_BIT 32
#  elif INTMAX_MAX == 9223372036854775807
#    define INTMAX_BIT 64
#  else
#    error Unexpected integer size
#  endif
#endif

// printf() directive to print a uintmax_t in hexadecimal (e.g. "llx" or "jx").
#define X PRIxMAX

// Maximum line length (not including new line character) for printing tables.
#define COLS 84

// Generate the header and code for the CRC described in model, writing to head
// and code respectively. This assumes that the code will be compiled on the
// same architecture that this is running on, for the purpose of selecting word
// sizes and endianess. name is the prefix used for all externally visible
// names in the source files. Test code is written to test, which consists of a
// computation of the CRC of "123456789" (nine bytes), and a comparison to the
// check value. If the check value does not match the computed CRC, then the
// generated code prints an error to stderr.
static void crc_gen(model_t *model, char *name, FILE *head, FILE *code,
                    FILE *defs, FILE *test) {
    // provide usage information in the header, and make sure size_t is defined
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
        "#include <stddef.h>\n", model->ref ? "low" : "high");

    // select the unsigned integer type to be used for CRC calculations
    char *crc_t;
    unsigned crc_t_bit;
    if (model->width <= WORD_BIT) {
        crc_t = "unsigned";
        crc_t_bit = WORD_BIT;
    }
    else if (model->width <= LONG_BIT) {
        crc_t = "unsigned long";
        crc_t_bit = LONG_BIT;
    }
    else {
        fputs(
        "#include <stdint.h>\n", head);
        crc_t = "uintmax_t";
        crc_t_bit = INTMAX_BIT;
    }

    // select the unsigned integer type to be used for table entries
    char *crc_table_t;
    unsigned crc_table_t_bit;
    if (model->width <= CHAR_BIT) {
        crc_table_t = "unsigned char";
        crc_table_t_bit = CHAR_BIT;
    }
    else if (model->width <= SHRT_BIT) {
        crc_table_t = "unsigned short";
        crc_table_t_bit = SHRT_BIT;
    }
    else {
        crc_table_t = crc_t;
        crc_table_t_bit = crc_t_bit;
    }

    // include the header in the code
    fprintf(code,
        "#include <stdint.h>\n"
        "#include \"%s.h\"\n", name);

    // function to reverse the low model->width bits, if needed (unlikely)
    if (model->rev) {
        fprintf(code,
        "\n"
        "static inline %s revlow(%s crc) {\n", crc_t, crc_t);
        unsigned dist = crc_table_t_bit;
        uintmax_t mask = (((uintmax_t)1 << (dist - 1)) << 1) - 1;
        uintmax_t pick = mask;
        while (dist >>= 1) {
            pick ^= pick << dist;
            fprintf(code,
        "    crc = ((crc >> %u) & %#"X") + ((crc << %u) & %#"X");\n",
                    dist, pick & mask, dist, ~pick & mask);
        }
        if (crc_table_t_bit != model->width)
            fprintf(code,
        "    return crc >> %u;\n"
        "}\n", crc_table_t_bit - model->width);
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
            crc_t, name, crc_t);
    fprintf(code,
        "\n"
        "// This code assumes that %s is %u bytes.\n"
        "\n"
        "%s %s_bit(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n",
            crc_t, crc_t_bit >> 3, crc_t, name, crc_t, model->init);
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
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        fprintf(code,
        "    while (len--) {\n"
        "        crc ^= *data++;\n"
        "        for (unsigned k = 0; k < 8; k++)\n"
        "            crc = crc & 1 ? (crc >> 1) ^ %#"X" : crc >> 1;\n"
        "    }\n", model->poly);
        if (model->rev)
            fputs(
        "    crc = revlow(crc);\n", code);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && crc_t_bit == model->width)
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
        "    while (len--) {\n"
        "        crc ^= *data++;\n"
        "        for (unsigned k = 0; k < 8; k++)\n"
        "            crc = crc & 0x80 ? (crc << 1) ^ %#"X" : crc << 1;\n"
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
        "    while (len--) {\n"
        "        crc ^= (%s)(*data++) << %u;\n"
        "        for (unsigned k = 0; k < 8; k++)\n"
        "            crc = crc & %#"X" ? (crc << 1) ^ %#"X" : crc << 1;\n"
        "    }\n",
        crc_t, model->width - 8, (word_t)1 << (model->width - 1), model->poly);
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
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    fputs("    return crc;\n"
          "}\n", code);

    // write test code for bit-wise function
    fprintf(defs,
        "#include \"%s.h\"\n", name);
    fprintf(test,
        "\n"
        "    // %s\n"
        "    init = %s_bit(0, NULL, 0);\n"
        "    if (%s_bit(init, \"123456789\", 9) != %#"X")\n"
        "        fputs(\"bit-wise mismatch for %s\\n\", stderr);\n"
        "    crc = %s_bit(init, data + 1, sizeof(data) - 1);\n",
            name, name, name, model->check, name, name);

    // bit-wise CRC calculation function for a small number of bits (0..8)
    fprintf(head,
        "\n"
        "// Compute the CRC of the %s bits bits in %sval.\n"
        "%s %s_rem(%s crc, unsigned val, unsigned bits);\n",
            model->ref ? "low" : "high",
            model->ref ? "" : "the low byte of ",
            crc_t, name, crc_t);
    fprintf(code,
        "\n"
        "%s %s_rem(%s crc, unsigned val, unsigned bits) {\n", crc_t, name, crc_t);
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
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        fprintf(code,
        "    val &= (1U << bits) - 1;\n"
        "    crc ^= val;\n"
        "    while (bits--)\n"
        "        crc = crc & 1 ? (crc >> 1) ^ %#"X" : crc >> 1;\n", model->poly);
        if (model->rev)
            fputs(
        "    crc = revlow(crc);\n", code);
        if (model->xorout) {
            if (model->xorout == ONES(model->width) && crc_t_bit == model->width)
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
        "    val &= 0x100 - (0x100 >> bits) ;\n"
        "    crc ^= val;\n"
        "    while (bits--)\n"
        "        crc = crc & 0x80 ? (crc << 1) ^ %#"X" : crc << 1;\n",
                model->poly << (8 - model->width));
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
        "    while (bits--)\n"
        "        crc = crc & %#"X" ? (crc << 1) ^ %#"X" : crc << 1;\n",
        crc_t, model->width - 8, (word_t)1 << (model->width - 1), model->poly);
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
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    fputs("    return crc;\n"
          "}\n", code);

    // write test code for small number of bits function
    if (model->ref)
        fprintf(test,
            "    if (%s_bit(init, \"\\xda\", 1) !=\n"
            "        %s_rem(%s_rem(init, 0xda, 3), 0x1b, 5))\n"
            "        fputs(\"small bits mismatch for %s\\n\", stderr);\n",
                name, name, name, name);
    else
        fprintf(test,
            "    if (%s_bit(init, \"\\xda\", 1) !=\n"
            "        %s_rem(%s_rem(init, 0xda, 3), 0xd0, 5))\n"
            "        fputs(\"small bits mismatch for %s\\n\", stderr);\n",
                name, name, name, name);

    // determine endianess of this machine
    unsigned little = 1;
    little = *((unsigned char *)(&little));

    // generate byte-wise and word-wise tables
    crc_table_wordwise(model);

    // byte-wise table
    if ((little && (model->ref || model->width <= 8)) ||
        (!little && !model->ref && model->width == INTMAX_BIT))
        fputs(
        "\n"
        "#define table_byte table_word[0]\n", code);
    else {
        fprintf(code,
        "\n"
        "static %s const table_byte[] = {\n", crc_table_t);
        {
            unsigned const d = (model->width + 3) >> 2; // hex digits per entry
            char const *pre = "   ";        // this plus one space is line prefix
            unsigned const max = COLS;      // maximum length before new line
            unsigned n = 0;                 // characters on this line, so far
            for (unsigned k = 0; k < 255; k++) {
                if (n == 0)
                    n += fprintf(code, "%s", pre);
                n += fprintf(code, " %s%0*"X",",
                             d == 1 && model->width < 4 ? "" : "0x", d,
                             model->table_byte[k]);
                if (n + d + 4 > max) {
                    putc('\n', code);
                    n = 0;
                }
            }
            fprintf(code, "%s %s%0*"X, n ? "" : pre,
                    d == 1 && model->width < 4 ? "" : "0x", d,
                    model->table_byte[255]);
        }
        fputs(
        "\n"
        "};\n", code);
    }

    // word-wise table
    fprintf(code,
        "\n"
        "static %s const table_word[][256] = {\n", little ? crc_table_t : "uintmax_t");
    {
        unsigned d = little ? model->ref ? (model->width + 3) >> 2 :
                                           ((model->width + 7) >> 3) << 1 :
                              WORDCHARS << 1;   // hex digits per entry
        char const *pre = "   ";        // this plus one space is line prefix
        unsigned const max = COLS;      // maximum length before new line
        unsigned n = 0;                 // characters on this line, so far
        for (unsigned j = 0; j < WORDCHARS; j++) {
            for (unsigned k = 0; k < 256; k++) {
                if (n == 0)
                    n += fprintf(code, "%s", pre);
                n += fprintf(code, "%s%s%0*"X"%s",
                             k ? " " : "{",
                             d == 1 && model->width < 4 ? "" : "0x", d,
                             model->table_word[j][k],
                             k != 255 ? "," : j != WORDCHARS-1 ? "}," : "}");
                if (n + d + 5 > max || k == 255) {
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
        "%s %s_byte(%s crc, void const *mem, size_t len);\n", crc_t, name, crc_t);
    fprintf(code,
        "\n"
        "%s %s_byte(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n", crc_t, name, crc_t, model->init);
    if (model->rev)
        fputs(
        "    crc = revlow(crc);\n", code);
    if (model->ref) {
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        if (model->width > 8)
            fputs(
        "    while (len--)\n"
        "        crc = (crc >> 8) ^\n"
        "              table_byte[(crc ^ *data++) & 0xff];\n", code);
        else
            fputs(
        "    while (len--)\n"
        "        crc = table_byte[crc ^ *data++];\n", code);

    }
    else if (model->width <= 8) {
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        if (model->width < 8)
            fprintf(code,
        "    crc <<= %u;\n", 8 - model->width);
        fputs(
        "    while (len--)\n"
        "        crc = table_byte[crc ^ *data++];\n", code);
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", 8 - model->width);
    }
    else {
        fprintf(code,
        "    while (len--)\n"
        "        crc = (crc << 8) ^\n"
        "              table_byte[((crc >> %u) ^ *data++) & 0xff];\n",
                model->width - 8);
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    if (model->rev)
        fputs(
        "    crc = revlow(crc);\n", code);
    fputs("    return crc;\n"
          "}\n", code);

    // write test code for byte-wise function
    fprintf(test,
        "    if (%s_byte(0, NULL, 0) != init ||\n"
        "        %s_byte(init, \"123456789\", 9) != %#"X" ||\n"
        "        %s_byte(init, data + 1, sizeof(data) - 1) != crc)\n"
        "        fputs(\"byte-wise mismatch for %s\\n\", stderr);\n",
            name, name, model->check, name, name);

    // word-wise CRC calculation function
    unsigned shift = model->width <= 8 ? 8 - model->width : model->width - 8;
    if ((little && !model->ref && model->width > 8) || (!little && model->ref)) {
        // function to swap low bytes, just enough to contain CRC for little-endian
        fprintf(code,
        "\n"
        "static inline %s swap%s(%s crc) {\n"
        "    return\n",
        little ? crc_t : "uintmax_t", little ? "low" : "max",
        little ? crc_t : "uintmax_t");
        uintmax_t pick = 0xff;
        int mid = little ? (model->width - 1) & ~7 : WORDBITS - 8;
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
        crc_t, name, crc_t);
    fprintf(code,
        "\n"
        "// This code assumes that integers are stored %s-endian.\n"
        "\n"
        "%s %s_word(%s crc, void const *mem, size_t len) {\n"
        "    unsigned char const *data = mem;\n"
        "    if (data == NULL)\n"
        "        return %#"X";\n",
            little ? "little" : "big", crc_t, name, crc_t, model->init);
    if (model->rev)
        fputs(
        "    crc = revlow(crc);\n", code);

    // do bytes up to word boundary
    if (model->ref) {
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        fprintf(code,
        "    while (len && ((ptrdiff_t)data & %#x)) {\n", WORDCHARS - 1);
        if (model->width > 8)
            fputs(
        "        crc = (crc >> 8) ^\n"
        "              table_byte[(crc ^ *data++) & 0xff];\n", code);
        else
            fputs(
        "        crc = table_byte[crc ^ *data++];\n", code);
        fputs(
        "        len--;\n"
        "    }\n", code);

    }
    else if (model->width <= 8) {
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
        if (model->width < 8)
            fprintf(code,
        "    crc <<= %u;\n", shift);
        fprintf(code,
        "    while (len && ((ptrdiff_t)data & %#x)) {\n"
        "        crc = table_byte[crc ^ *data++];\n"
        "        len--;\n"
        "    }\n", WORDCHARS - 1);
    }
    else {
        fprintf(code,
        "    while (len && ((ptrdiff_t)data & %#x)) {\n"
        "        crc = (crc << 8) ^\n"
        "              table_byte[((crc >> %u) ^ *data++) & 0xff];\n"
        "        len--;\n"
        "    }\n", WORDCHARS - 1, shift);
    }

    // do full words if there are any
    fprintf(code,
        "    if (len >= %u) {\n", WORDCHARS);

    // do full words for little-endian
    if (little) {
        unsigned top = model->width > 8 ? -model->width & 7 : 0;
        if (!model->ref) {
            if (top)
                fprintf(code,
        "        crc <<= %u;\n", top);
            if (model->width > 8)
                fputs(
        "        crc = swaplow(crc);\n", code);
        }
        fprintf(code,
        "        do {\n"
        "            uintmax_t word = crc ^ *(uintmax_t const *)data;\n"
        "            crc = table_word[%u][word & 0xff] ^\n", WORDCHARS - 1);
        for (unsigned k = 1; k < WORDCHARS - 1; k++) {
            fprintf(code,
        "                  table_word[%u][(word >> %u) & 0xff] ^\n",
                    WORDCHARS - k - 1, k << 3);
        }
        fprintf(code,
        "                  table_word[0][word >> %u];\n"
        "            data += %u;\n"
        "            len -= %u;\n"
        "        } while (len >= %u);\n",
                (WORDCHARS - 1) << 3, WORDCHARS, WORDCHARS, WORDCHARS);
        if (!model->ref) {
            if (model->width > 8)
                fputs(
        "        crc = swaplow(crc);\n", code);
            if (top)
                fprintf(code,
        "        crc >>= %u;\n", top);
        }
    }

    // do full words for big-endian
    else {
        unsigned top = model->ref ? 0 : WORDBITS - (model->width > 8 ? model->width : 8);
        if (model->ref)
            fputs(
        "        uintmax_t word = swapmax(crc);\n", code);
        else
            fprintf(code,
        "        uintmax_t word = (uintmax_t)crc << %u;\n", top);
        fputs(
        "        do {\n"
        "            word ^= *(uintmax_t const *)data;\n"
        "            word = table_word[0][word & 0xff] ^\n", code);
        for (unsigned k = 1; k < WORDCHARS - 1; k++) {
            fprintf(code,
        "                   table_word[%u][(word >> %u) & 0xff] ^\n", k, k << 3);
        }
        fprintf(code,
        "                   table_word[%u][word >> %u];\n"
        "            data += %u;\n"
        "            len -= %u;\n"
        "        } while (len >= %u);\n",
                WORDCHARS - 1, (WORDCHARS - 1) << 3,
                WORDCHARS, WORDCHARS, WORDCHARS);
        if (model->ref)
            fputs(
        "        crc = swapmax(word);\n", code);
        else
            fprintf(code,
        "        crc = word >> %u;\n", top);
    }

    // no more full words
    fputs(
        "    }\n", code);

    // do last few bytes
    if (model->ref) {
        if (model->width > 8)
            fputs(
        "    while (len--)\n"
        "        crc = (crc >> 8) ^\n"
        "              table_byte[(crc ^ *data++) & 0xff];\n", code);
        else
            fputs(
        "    while (len--)\n"
        "        crc = table_byte[crc ^ *data++];\n", code);
    }
    else if (model->width <= 8) {
        fputs(
        "    while (len--)\n"
        "        crc = table_byte[crc ^ *data++];\n", code);
        if (model->width < 8)
            fprintf(code,
        "    crc >>= %u;\n", shift);
    }
    else {
        fprintf(code,
        "    while (len--)\n"
        "        crc = (crc << 8) ^\n"
        "              table_byte[((crc >> %u) ^ *data++) & 0xff];\n",
        shift);
        if (model->width != crc_t_bit && !model->rev)
            fprintf(code,
        "    crc &= %#"X";\n", ONES(model->width));
    }
    if (model->rev)
        fputs(
        "    crc = revlow(crc);\n", code);
    fputs(
        "    return crc;\n"
        "}\n", code);

    // write test code for word-wise function
    fprintf(test,
        "    if (%s_word(0, NULL, 0) != init ||\n"
        "        %s_word(init, \"123456789\", 9) != %#"X" ||\n"
        "        %s_word(init, data + 1, sizeof(data) - 1) != crc)\n"
        "        fputs(\"word-wise mismatch for %s\\n\", stderr);\n",
            name, name, model->check, name, name);
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
// respectively. If a file by either of those names already exists, then an
// error is returned. A failure to create the directory or writable files will
// return true, with no open handles and *head and *code containing NULL. If
// the problem was a source file that already existed, then create_source()
// will return 2. Otherwise it will return 1 on error, 0 on success.
static int create_source(char *src, char *name, FILE **head, FILE **code) {
    // for error return
    *head = NULL;
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
    *suff = 'h';
    *head = fopen(path, "wx");
    if (*head == NULL)
        return errno == EEXIST ? 2 : 1;

    // create code file
    *suff = 'c';
    *code = fopen(path, "wx");
    if (*code == NULL) {
        int err = errno;
        fclose(*head);
        *head = NULL;
        *suff = 'h';
        unlink(path);
        return err == EEXIST ? 2 : 1;
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
    // create test source files
    FILE *defs, *test;
    {
        int ret = create_source(SRC, "crc_test", &defs, &test);
        if (ret) {
            fputs("could not create test code files -- aborting\n", stderr);
            return 1;
        }
    }
    fputs(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdint.h>\n"
        "#include <time.h>\n"
        "#include \"crc_test.h\"\n"
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
        "    uintmax_t init, crc;\n", test);

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
        else if (model.width > WORDBITS)
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
                crc_gen(&model, name, head, code, defs, test);
                fclose(code);
                fclose(head);
            }
            free(name);
        }
        free(model.name);
    }
    free(line);

    fputs(
        "\n"
        "    return 0;\n"
        "}\n", test);
    fclose(test);
    return 0;
}
