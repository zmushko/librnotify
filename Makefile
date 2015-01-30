CC ?= gcc
CFLAGS += -g -Wall -pedantic -std=gnu99 

all :  rnotify 

rnotify : rnotify.c rnotify.h 
	$(CC) $(CFLAGS) -c rnotify.c -D_SDJOURNAL_LOG -D_FILE_OFFSET_BITS=64
	ar cr librnotify.a rnotify.o

clean :
	rm -f *.o
	rm -f *.a

