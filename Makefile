CFLAGS=-O3 -Wall -Wextra -Wcast-qual -std=c99 -pedantic
OBJS=$(patsubst %.c,%.o,$(wildcard src/crc*.c))
all: src/allcrcs.c crctest mincrc
src/allcrcs.c: crcgen allcrcs-abbrev.txt
	@rm -rf src
	./crcgen < allcrcs-abbrev.txt
	@cat src/allcrcs.h >> src/allcrcs.c
	@rm src/allcrcs.h
	make src
src: crcany src/test_src
src/test_src: src/test_src.o $(OBJS)
crcany: crcany.o $(OBJS)
crcany.o: crcany.c src/allcrcs.c
crctest: crctest.o crc.o crcdbl.o model.o
crctest.o: crctest.c crc.h crcdbl.h model.h
crcgen: crcgen.o crc.o model.o
crcgen.o: crcgen.c crc.h model.h
mincrc: mincrc.o model.o
mincrc.o: mincrc.c model.h
crc.o: crc.c crc.h model.h
crcdbl.o: crcdbl.c crcdbl.h crc.h model.h
model.o: model.c model.h
test: src/allcrcs.c crctest mincrc allcrcs.txt allcrcs-abbrev.txt
	./crctest < allcrcs-abbrev.txt
	src/test_src
	./mincrc < allcrcs.txt | cmp - allcrcs-abbrev.txt
	./getcrcs | diff - allcrcs.txt
clean:
	@rm -rf *.o crctest crcgen mincrc crcany src
