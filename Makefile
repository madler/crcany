CFLAGS=-O3 -Wall -Wall -Wextra -Wcast-qual
all: crcany mincrc
crcany: crcany.o model.o
crcany.o: crcany.c model.h
mincrc: mincrc.o model.o
mincrc.o: mincrc.c model.h
model.o: model.c model.h
test: crcany mincrc
	./crcany < allcrcs-abbrev.txt
	./mincrc < allcrcs.txt | cmp - allcrcs-abbrev.txt
clean:
	@rm -f *.o crcany mincrc
