include config.mk

all: pq

pq: pq.c config.h
	$(CC) $(CFLAGS) -o $@ pq.c $(LIBS)

config.h:
	cp config.def.h $@

clean:
	rm -f pq config.h record.txt

.PHONY: all clean