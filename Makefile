CFLAGS=-O3 -Wall -Wall -Wextra -Wcast-qual
all: crcany mincrc
crcany: crcany.o crc.o crcdbl.o model.o
crcany.o: crcany.c crc.h crcdbl.h model.h
mincrc: mincrc.o model.o
mincrc.o: mincrc.c model.h
crc.o: crc.c crc.h model.h
crcdbl.o: crcdbl.c crcdbl.h crc.h model.h
model.o: model.c model.h
test: crcany mincrc
	./crcany < allcrcs-abbrev.txt
	./mincrc < allcrcs.txt | cmp - allcrcs-abbrev.txt
clean:
	@rm -f *.o crcany mincrc
