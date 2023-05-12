CFLAGS=-O3 -Wall -Wextra -Wcast-qual -std=c99 -pedantic
OBJS=$(patsubst %.c,%.o,$(wildcard src/crc*.c))
all: src/allcrcs.c crctest crcadd mincrc
src/allcrcs.c: crcall allcrcs-abbrev.txt crcany.c
	@rm -rf src
	@if test -n "$$(find . -maxdepth 1 -name '*.def' -print -quit)"; \
	then \
		cat allcrcs-abbrev.txt *.def | ./crcall; \
	else \
		./crcall < allcrcs-abbrev.txt; \
	fi
	@cat src/allcrcs.h >> src/allcrcs.c
	@rm src/allcrcs.h
	make src
src: crcany src/test_src
src/test_src: src/test_src.o $(OBJS)
crcany: crcany.o $(OBJS)
crcany.o: crcany.c src/allcrcs.c
crctest: crctest.o crc.o crcdbl.o model.o
crctest.o: crctest.c crc.h crcdbl.h model.h
crcgen.o: crcgen.c crcgen.h crc.h model.h
crcall.o: crcall.c crcgen.h crc.h model.h
crcall: crcall.o crcgen.o crc.o model.o
crcadd.o: crcadd.c crcgen.h crc.h model.h
crcadd: crcadd.o crcgen.o crc.o model.o
mincrc: mincrc.o model.o
mincrc.o: mincrc.c model.h
crc.o: crc.c crc.h model.h
crcdbl.o: crcdbl.c crcdbl.h crc.h model.h
model.o: model.c model.h
test: src/allcrcs.c crctest allcrcs-abbrev.txt
	@if test -n "$$(find . -maxdepth 1 -name '*.def' -print -quit)"; \
	then \
		cat allcrcs-abbrev.txt *.def | ./crctest; \
	else \
		./crctest < allcrcs-abbrev.txt; \
	fi
	src/test_src
checklists: mincrc allcrcs.txt allcrcs-abbrev.txt
	./mincrc < allcrcs.txt | diff -qb - allcrcs-abbrev.txt
	./getcrcs | diff - allcrcs.txt
clean:
	@rm -rf *.o crctest crcall mincrc crcany crcadd src
