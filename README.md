Synopsis
--------

_crcany_ can compute any CRC, given the parameters that describe it. Those
parameters are given in the form used on Greg Cook's catalog of CRCs at
http://reveng.sourceforge.net/crc-catalogue/all.htm , where that set of
parameters were first defined by Ross Williams in his excellent tutorial on
CRCs at http://zlib.net/crc_v3.txt .

_crcany_ computes each CRC three different ways. First, computing a bit at a
time, as you would in any self-respecting hardware implementation. Second,
using the bit-at-a-time algorithm to create a table of 256 CRCs that can be
used to compute that CRC a byte at a time. Third, using the byte-wise table to
generate another _n-1_ tables, where _n_ is the number of bytes in the largest
integer, to enable computing a CRC a word at a time. The word-wise approach has
two flavors, one for little-endian machines, and one for big-endian machines.

The bit-wise calculation can be done on CRCs up to twice the word length, e.g.
128 bits on machines with 64-bit integers. The byte and word-wise calculations
can be done on CRCs up to the word size, e.g. up to 64-bit CRCs using 64-bit
integers.

_crcgen_ generates C code for each CRC definition read from stdin, putting the
resulting code into .h and .c files for each definition in the src/
subdirectory. Test code is also generated in src/crc_test.c. Code is generated
for the machine being run on (i.e. with respect to the lengths of the integer
types and their endianess), and will generate code for any CRC whose width is
less than or equal to the maximum integer size.

Motivation
----------

Provide once and for all a definitive reference for how to compute any CRC with
any of bit, byte, or word-at-a-time algorithms, on any big or little-endian
architecture.

Installation
------------

This will compile the crcany, crcgen, and mincrc executables:

    make

Test
----

Test crcany and crcgen using the list of all catalogued CRCs, available here in
the file allcrcs.txt, and test mincrc against the abbreviated list using the
Makefile:

    make test

License
-------

This code is under the zlib license, permitting free commercial use.
