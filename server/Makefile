CC=$(CROSS_COMPILE)gcc
CFLAGS=-g -Wall
LIBDEPS=-lpthread

.PHONY: clean aesdsocket

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBDEPS)

clean:
	rm -f aesdsocket
