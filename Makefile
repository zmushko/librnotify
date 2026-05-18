CC      ?= gcc
AR      ?= ar
INSTALL ?= install

CFLAGS  += -g -Wall -pedantic -std=gnu11 -D_FILE_OFFSET_BITS=64

PREFIX       ?= /usr/local
LIBDIR       ?= $(PREFIX)/lib
INCLUDEDIR   ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

LIBNAME  = librnotify
MAJOR    = 2
MINOR    = 0
PATCH    = 0
VERSION  = $(MAJOR).$(MINOR).$(PATCH)

REAL_SO  = $(LIBNAME).so.$(VERSION)
SONAME   = $(LIBNAME).so.$(MAJOR)
LINK_SO  = $(LIBNAME).so
STATIC   = $(LIBNAME).a
PC       = $(LIBNAME).pc

HEADERS  = rnotify.h
OBJS     = rnotify.o liblst.o

.PHONY: all clean install uninstall test

all: $(LINK_SO) $(STATIC) $(PC)

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(REAL_SO): $(OBJS)
	$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^

$(SONAME): $(REAL_SO)
	ln -sf $(REAL_SO) $@

$(LINK_SO): $(SONAME)
	ln -sf $(SONAME) $@

$(STATIC): $(OBJS)
	$(AR) rcs $@ $^

$(PC): $(PC).in
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
	    -e 's|@LIBDIR@|$(LIBDIR)|g' \
	    -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
	    -e 's|@VERSION@|$(VERSION)|g' \
	    $< > $@

test: test.c $(STATIC)
	$(CC) $(CFLAGS) -o $@ test.c $(STATIC)

install: all
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -m 0644 $(HEADERS) $(DESTDIR)$(INCLUDEDIR)/
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0755 $(REAL_SO) $(DESTDIR)$(LIBDIR)/
	ln -sf $(REAL_SO) $(DESTDIR)$(LIBDIR)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(LIBDIR)/$(LINK_SO)
	$(INSTALL) -m 0644 $(STATIC) $(DESTDIR)$(LIBDIR)/
	$(INSTALL) -d $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 0644 $(PC) $(DESTDIR)$(PKGCONFIGDIR)/

uninstall:
	rm -f $(DESTDIR)$(INCLUDEDIR)/$(HEADERS)
	rm -f $(DESTDIR)$(LIBDIR)/$(REAL_SO)
	rm -f $(DESTDIR)$(LIBDIR)/$(SONAME)
	rm -f $(DESTDIR)$(LIBDIR)/$(LINK_SO)
	rm -f $(DESTDIR)$(LIBDIR)/$(STATIC)
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/$(PC)

clean:
	rm -f $(OBJS)
	rm -f $(REAL_SO) $(SONAME) $(LINK_SO)
	rm -f $(STATIC) $(PC)
	rm -f test
