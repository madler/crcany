/* crcdbl.h -- Generic bit-wise CRC calculation for a double-wide CRC
 * Copyright (C) 2014, 2016, 2017 Mark Adler
 * For conditions of distribution and use, see copyright notice in crcany.c.
 */

#ifndef _CRCDBL_H_
#define _CRCDBL_H_

#include "model.h"

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
void crc_bitwise_dbl(model_t *, word_t *, word_t *,
                     unsigned char const *, size_t);

/* Similar to crc_zeros(), but works for CRCs up to twice as long as a
   word_t. */
void crc_zeros_dbl(model_t *, word_t *, word_t *, size_t);

#endif
