CC ?= gcc
CFLAGS += -g -Wall -pedantic -std=gnu99

all :	rnotify test.c
	$(CC) $(CFLAGS) test.c rnotify.o

rnotify : rnotify.c rnotify.h 
	$(CC) $(CFLAGS) -fPIC -c rnotify.c -D_FILE_OFFSET_BITS=64
	$(CC) -shared -o librnotify.so rnotify.o
	ar cr librnotify.a rnotify.o

clean :
	rm -f *.o
	rm -f *.a
