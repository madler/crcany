CFLAGS=-O3 -Wall -Wextra -Wcast-qual -std=gnu99 -pedantic
all: crcany crcgen mincrc
crcany: crcany.o crc.o crcdbl.o model.o
crcany.o: crcany.c crc.h crcdbl.h model.h
crcgen: crcgen.o crc.o model.o
crcgen.o: crcgen.c crc.h model.h
mincrc: mincrc.o model.o
mincrc.o: mincrc.c model.h
crc.o: crc.c crc.h model.h
crcdbl.o: crcdbl.c crcdbl.h crc.h model.h
model.o: model.c model.h
test: crcany crcgen mincrc
	./crcany < allcrcs-abbrev.txt
	@rm -rf src
	./crcgen < allcrcs.txt
	$(CC) $(CFLAGS) -o src/crc_test src/*.c
	src/crc_test
	./mincrc < allcrcs.txt | cmp - allcrcs-abbrev.txt
clean:
	@rm -rf *.o crcany crcgen mincrc src
