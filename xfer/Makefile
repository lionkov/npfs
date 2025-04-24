SYSNAME:=${shell uname}
SYSNAME!=uname
CFLAGS=-Wall -g -I ../include -DSYSNAME=$(SYSNAME)
LFLAGS=-L../libnpclient -lnpclient -L../libnpfs -lnpfs -pthread
LIBDIRS:=/usr/lib/ /usr/local/lib/ /opt/lib/
LD=$(CC)
ALL=srv clnt tcpsrv tcpclnt

ifeq ($(SYSNAME),Linux)
	RDMADEPS:=\
		$(and $(foreach dir,$(LIBDIRS),$(wildcard $(dir)/libibverbs.*)), \
		$(foreach dir,$(LIBDIRS), $(wildcard $(dir)/librdmacm.*)))
	ifneq ($(strip $(RDMADEPS)),)
		RDMACFLAGS += -DNPRDMA
		RDMALFLAGS += -libverbs -lrdmacm
        endif
endif

.PHONY: all clean

all: $(ALL)

srv: ufs.o srv.o
	$(LD) -o srv ufs.o srv.o $(LFLAGS) $(RDMALFLAGS)

clnt: clnt.o
	$(LD) -o clnt $(CFLAGS) clnt.o $(LFLAGS) $(RDMALFLAGS)

tcpsrv: ufs.o tcpsrv.o
	$(LD) -o tcpsrv ufs.o tcpsrv.o $(LFLAGS)

tcpclnt: tcpclnt.o
	$(LD) -o tcpclnt $(CFLAGS) tcpclnt.o $(LFLAGS)

clean:
	rm -f $(ALL) *.o *~

%.c: ../include/npfs.h ../include/npclient.h Makefile

srv: ../libnpfs/libnpfs.a
clnt: ../libnpfs/libnpfs.a ../libnpclient/libnpclient.a

tcpsrv.o: srv.c
	$(CC) $(CFLAGS) -o $@ -c $<

tcpclnt.o: clnt.c
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.c 
	$(CC) $(CFLAGS) $(RDMACFLAGS) -o $*.o -c $*.c

