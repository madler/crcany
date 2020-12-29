/* crcgen.h -- Generate CRC computation code
 * Copyright (C) 2014, 2016, 2017, 2020 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#include "model.h"

// Generate a local function to reverse the low bits of an unsigned integer.
// The first argument is the number of bits, and the second argument is the
// file to write the generated code to. The name of the function will be
// revlow<n>, where <n> is bits. The type of the argument and the return value
// will be the smallest unsigned integer with at least bits bits. Return 0 on
// success, non-zero if the first argument is not valid.
int rev_gen(int, FILE *);

// Generate the header and code for the CRC described in the first argument.
// The second argument is the prefix string used for all externally visible
// names in the source files. The generated word-wise CRC code uses the
// endianess in the third argument (1 for little endian, 0 for big endian), and
// the word size in bits in the fourth argument, which must be 32 or 64. The
// width of the CRC in model must be less than or equal to the word size. The
// generated header is written to the fifth argument, and the code is written
// to the last argument. Return 0 on success, non-zero if word_bits and
// model->width are invalid.
int crc_gen(model_t *, char *, unsigned, unsigned, FILE *, FILE *);
