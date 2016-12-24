CC ?= gcc
CFLAGS += -g -Wall -pedantic -std=gnu99

static :
	$(CC) $(CFLAGS) -fPIC -c rnotify.c -D_FILE_OFFSET_BITS=64
	ar cr librnotify.a rnotify.o	

all :	rnotify test.c
	$(CC) $(CFLAGS) test.c rnotify.o liblst.o
	#$(CC) $(CFLAGS) rnotifyd.c rnotify.o liblst.o

rnotify : rnotify.c rnotify.h 
	$(CC) $(CFLAGS) -fPIC -c rnotify.c liblst.c -D_FILE_OFFSET_BITS=64
	$(CC) -shared -o librnotify.so rnotify.o liblst.o
	ar cr librnotify.a rnotify.o liblst.o

clean :
	rm -f *.o
	rm -f *.so
	rm -f *.a
