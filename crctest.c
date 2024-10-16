/* crctest.c -- Generic CRC tests
 * Copyright (C) 2014, 2016, 2017, 2020, 2021 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

/*
   Use a generalized CRC algorithm for CRCs up to 128 bits long to take model
   inputs as found in https://reveng.sourceforge.io/crc-catalogue/all.htm , and
   verify the check values. This verifies all 102 CRCs on that page (as of the
   version date above). The lines on that page that start with "width=" can be
   fed verbatim to this program. The 128-bit limit assumes that uintmax_t is 64
   bits. The bit-wise algorithms here will compute CRCs up to a width twice
   that of the typedef'ed word_t type.

   This code also generates and tests table-driven algorithms for high-speed.
   The byte-wise algorithm processes one byte at a time instead of one bit at a
   time, and the word-wise algorithm ingests one word_t at a time. The table-
   driven algorithms here only work for CRCs that fit in a word_t, though they
   could be extended in the same way the bit-wise algorithm is extended here.

   Lastly, this code tests generalized CRC combination algorithms for all of
   the models.

   The CRC parameters used in the linked catalogue were originally defined in
   Ross Williams' "A Painless Guide to CRC Error Detection Algorithms", which
   can be found here: http://zlib.net/crc_v3.txt .
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

#include "model.h"
#include "crc.h"
#include "crcdbl.h"

// --- Test on model input from stdin ---

// Read a series of CRC model descriptions from stdin, one per line, and verify
// the check value for each using the bit-wise, byte-wise, and word-wise
// algorithms. Checks are not done for those cases where word_t is not wide
// enough to permit the calculation.
int main(void) {
    // Create test data.
    unsigned char *test = malloc(32);       // get memory on a word boundary
    if (test == NULL) {
        fputs("out of memory -- aborting\n", stderr);
        return 1;
    }
    memcpy(test, "123456789", 9);       // test string for check value
    memcpy(test + 15, "123456789", 9);  // one off from word boundary
    unsigned char random_data[65521];   // generate random test vector
    {
        // Yes, rand() is terrible. But it's good enough for this application,
        // and it's part of the C99 standard. Avoid some of the problems by
        // running it for a while before using it, and only using the high bits
        // of the returned value.
        unsigned max = (unsigned)RAND_MAX + 1;
        int shft = 0;
        do {
            max >>= 1;
            shft++;
        } while (max > 256);
        srand(time(NULL));
        for (int i = 0; i < 997; i++)
            (void)rand();
        size_t n = sizeof(random_data);
        do {
            random_data[--n] = rand() >> shft;
        } while (n);
    }

    // Read and test models from stdin.
    unsigned inval = 0, num = 0, good = 0, goodres = 0;
    unsigned numall = 0, goodbyte = 0, goodword = 0, goodcomb = 0;
    char *line = NULL;
    size_t size;
    ptrdiff_t len;
    while ((len = getcleanline(&line, &size, stdin)) != -1) {
        if (len == 0)
            continue;
        model_t model;
        model.name = NULL;
        int ret = read_model(&model, line, 0);
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
            // Convert the parameters for calculation, and fill in the tables
            // that are independent of endianess and word length.
            process_model(&model);
            crc_table_combine(&model);
            crc_table_bytewise(&model);

            // No tests have passed yet. Bits in tests are set for each
            // successful or bypassed test.
            unsigned tests = 0;

            // Bit-wise.
            word_t crc_hi, crc;
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
                // Initialize tables for byte-wise and word-wise.
                unsigned little = 1;
                little = *((unsigned char *)(&little));
                crc_table_wordwise(&model, little, WORDBITS);

                // Byte-wise.
                crc = crc_bytewise(&model, 0, NULL, 0);
                crc = crc_bytewise(&model, crc, test, 9);
                if (crc == model.check) {
                    tests |= 8;
                    goodbyte++;
                }

                // Word-wise (check on and off boundary in order to exercise
                // all loops).
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

                // Combine.
                size_t len = sizeof(random_data);
                size_t len2 = 61417;
                size_t len1 = sizeof(random_data) - len2;
                crc = crc_bytewise(&model, 0, NULL, 0);
                word_t crc1 = crc, crc2 = crc;
                crc = crc_bytewise(&model, crc, random_data, len);
                crc1 = crc_bytewise(&model, crc1, random_data, len1);
                crc2 = crc_bytewise(&model, crc2, random_data + len1, len2);
                if (crc == crc_combine(&model, crc1, crc2, len2)) {
                    tests |= 32;
                    goodcomb++;
                }
            }
            num++;
            if (tests & 4)
                printf("%s:%s%s%s (CRC too long for byte, word, comb)\n",
                       model.name,
                       tests & 1 ? "" : " bit fail",
                       tests & 3 ? "" : ",",
                       tests & 2 ? "" : " residue fail");
            else if (tests == 0)
                printf("%s: all tests failed\n", model.name);
            else if (tests != 1 + 2 + 8 + 16 + 32)
                printf("%s:%s%s%s%s%s%s%s%s%s\n",
                       model.name,
                       tests & 1 ? "" : " bit fail",
                       tests & 3 ? "" : ",",
                       tests & 2 ? "" : " residue fail",
                       (tests & 3) == 3 || (tests & 8) ? "" : ",",
                       tests & 8 ? "" : " byte fail",
                       (tests & 11) == 11 || (tests & 16) ? "" : ",",
                       tests & 16 ? "" : " word fail",
                       (tests & 27) || (tests & 32) ? "" : ",",
                       tests & 32 ? "" : " combine fail");
        }
        free(model.name);
    }

    // Clean up.
    free(line);
    free(test);

    // Print test results.
    printf("%u models verified bit-wise out of %u usable "
           "(%u unusable models)\n", good, num, inval);
    printf("%u model residues verified out of %u usable "
           "(%u unusable models)\n", goodres, num, inval);
    printf("%u models verified byte-wise out of %u usable\n",
           goodbyte, numall);
    word_t endian = 1;
    printf("%u models verified word-wise out of %u usable (%s-endian)\n",
           goodword, numall, *((unsigned char *)(&endian)) ? "little" : "big");
    printf("%u models verified combine out of %u usable\n",
           goodcomb, numall);
    puts(good == num && goodres == num && goodbyte == numall &&
         goodword == numall && goodcomb == numall ?
            "-- all good" : "** verification failed");
    return 0;
}
