SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
HFILES=../include/ufs.h ../include/npfs.h ufsimpl.h
CFLAGS=-Wall -Wundef -g -I../include -DSYSNAME_$(SYSNAME)
LFLAGS=-L../libnpfs -L.
INCLUDEDIRS:=/usr/include/ /usr/local/include/ /opt/include/

ifeq ($(SYSNAME), SunOS)
	CFLAGS += -D_POSIX_PTHREAD_SEMANTICS
endif

.PHONY: all clean

.all: libufs.a

LIBFILES=ufs.o ops.o
libufs.a: $(LIBFILES)
	ar rc libufs.a $(LIBFILES)
	ranlib libufs.a

srv: srv.o libufs.a
	$(CC) -o srv srv.o -L. -lufs -L../libnpfs -lnpfs

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/ufs.h $(INSTALLPREFIX)/include
	cp libufs.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~ core.*

$(LIBFILES): Makefile $(HFILES)

%.o: %.c
	$(CC) $(CFLAGS) -o $*.o -c $*.c

