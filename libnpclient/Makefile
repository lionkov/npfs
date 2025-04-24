SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
HFILES=../include/npfs.h ../include/npclient.h npcimpl.h
CFLAGS=-Wall -g -I ../include -D$(SYSNAME)
INCLUDEDIRS:=/usr/include/ /usr/local/include/ /opt/include/

LIBFILES=\
	dirread.o\
	fid.o\
	fsys.o\
	mount.o\
	netmount.o\
	open.o\
	pool.o\
	read.o\
	remove.o\
	stat.o\
	stat2wstat.o\
	walk.o\
	write.o\

ifeq ($(SYSNAME),Linux)
	RDMADEPS:=\
		$(and $(foreach dir,$(INCLUDEDIRS),$(wildcard $(dir)/rdma/rdma_cma.h)), \
		$(foreach dir,$(INCLUDEDIRS), $(wildcard $(dir)/infiniband/verbs.h)))
	ifneq ($(strip $(RDMADEPS)),)
		CFLAGS += -DNPRDMA
		LIBFILES += rdmamount.o
	endif
endif

.PHONY: all clean

.all: libnpclient.a

libnpclient.a: $(LIBFILES)
	echo $(INCLUDEDIRS)
	ar rc libnpclient.a $(LIBFILES)
	ranlib libnpclient.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/npclient.h $(INSTALLPREFIX)/include
	cp libnpclient.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~
	make -C examples clean

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -o $*.o -c $*.c
