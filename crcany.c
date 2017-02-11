/*
  crcany version 1.6, 10 February 2017

  Copyright (C) 2014, 2016, 2017 Mark Adler

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
   1.2  17 Jul 2016  Move generic CRC code to crc.[ch] and crcdbl.[ch]
   1.3  23 Jul 2016  Build xorout into the tables
   1.4  30 Jul 2016  Fix a bug in word-wise table generation
                     Reduce verbosity of testing
   1.5  23 Oct 2016  Improve use of data types and C99 compatibility
                     Add verifications summary message
   1.6  10 Feb 2017  Add new residue parameter parsing and checking

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "model.h"
#include "crc.h"
#include "crcdbl.h"

/* --- Test on model input from stdin --- */

/* Read a series of CRC model descriptions, one per line, and verify the check
   value for each using the bit-wise, byte-wise, and word-wise algorithms.
   Checks are not done for those cases where word_t is not wide enough to
   permit the calculation. */
int main(void)
{
    int ret;
    unsigned tests;
    unsigned inval = 0, num = 0, good = 0, goodres = 0;
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
            crc = crc_hi = 0;
            crc_zeros_dbl(&model, &crc_hi, &crc, model.width);
            crc ^= model.xorout;
            crc_hi ^= model.xorout_hi;
            if (crc == model.res && crc_hi == model.res_hi) {
                tests |= 2;
                goodres++;
            }
            if (model.width > WORDBITS)
                tests |= 4;
            else {
                /* initialize tables for byte-wise and word-wise */
                crc_table_wordwise(&model);

                /* byte-wise */
                crc = crc_bytewise(&model, 0, NULL, 0);
                crc = crc_bytewise(&model, crc, test, 9);
                if (crc == model.check) {
                    tests |= 8;
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
                        tests |= 16;
                        goodword++;
                    }
                }
                numall++;
            }
            num++;
            if (tests & 4)
                printf("%s:%s%s%s (CRC too long for byte, word)\n",
                       model.name,
                       tests & 1 ? "" : " bit fail",
                       tests & 3 ? "" : ",",
                       tests & 2 ? "" : " residue fail");
            else if (tests == 0)
                printf("%s: all tests failed\n", model.name);
            else if (tests != 1 + 2 + 8 + 16)
                printf("%s:%s%s%s%s%s%s%s\n",
                       model.name,
                       tests & 1 ? "" : " bit fail",
                       tests & 3 ? "" : ",",
                       tests & 2 ? "" : " residue fail",
                       (tests & 3) == 3 || (tests & 8) ? "" : ",",
                       tests & 8 ? "" : " byte fail",
                       (tests & 11) == 11 || (tests & 16) ? "" : ",",
                       tests & 16 ? "" : " word fail");
        }
        free(model.name);
        model.name = NULL;
    }
    free(line);
    free(test);
    printf("%u models verified bit-wise out of %u usable "
           "(%u unusable models)\n", good, num, inval);
    printf("%u model residues verified out of %u usable "
           "(%u unusable models)\n", goodres, num, inval);
    printf("%u models verified byte-wise out of %u usable\n",
           goodbyte, numall);
    crc = 1;
    printf("%u models verified word-wise out of %u usable (%s-endian)\n",
           goodword, numall, *((unsigned char *)(&crc)) ? "little" : "big");
    puts(good == num && goodres == num && goodbyte == numall &&
         goodword == numall ? "-- all good" : "** verification failed");
    return 0;
}
