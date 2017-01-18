CC ?= gcc
CFLAGS += -g -Wall -pedantic -std=gnu99

all :	rnotify test.c
	$(CC) $(CFLAGS) -o test test.c rnotify.o liblst.o

objects :  *.c *.h 
	$(CC) $(CFLAGS) -fPIC -c liblst.c -D_FILE_OFFSET_BITS=64
	$(CC) $(CFLAGS) -fPIC -c rnotify.c -D_FILE_OFFSET_BITS=64

rnotify : objects
	$(CC) -shared -o librnotify.so rnotify.o liblst.o
	ar cr librnotify.a rnotify.o liblst.o

static : objects
	ar cr librnotify.a rnotify.o liblst.o	

clean :
	rm -f *.o
	rm -f *.so
	rm -f *.a
	rm -f test
	rm -f *.out
