CC = gcc
CFLAGS = -Wall -pedantic -std=gnu99 

all :  rnotify test.c
	$(CC) $(CFLAGS) test.c -L. -lrnotify

rnotify : rnotify.c rnotify.h 
	$(CC) $(CFLAGS) -c rnotify.c
	ar cr librnotify.a rnotify.o

clean :
	rm -f *.o
	rm -f *.a

