SYSNAME:=${shell uname}
SYSNAME!=uname
CFLAGS=-Wall -g -I ../../include
LFLAGS=-L../../libnpfs -L.. -lnpclient -lnpfs -lpthread
INCLUDEDIRS:=/usr/include/ /usr/local/include/ /opt/include/

CMD=\
	9ls\
	9write\
	9read\

ifeq ($(SYSNAME),Linux)
	RDMADEPS:=\
		$(and $(foreach dir,$(INCLUDEDIRS),$(wildcard $(dir)/rdma/rdma_cma.h)), \
		$(foreach dir,$(INCLUDEDIRS), $(wildcard $(dir)/infiniband/verbs.h)))
	ifneq ($(strip $(RDMADEPS)),)
		CFLAGS += -DNPRDMA
		CMD += rdmals
		LFLAGS += -libverbs -lrdmacm
	endif
endif

all: $(CMD)

9ls: 9ls.o ../../libnpfs/libnpfs.a ../libnpclient.a 
	$(CC) -o 9ls $(CFLAGS) 9ls.o $(LFLAGS)

rdmals: rdmals.o ../../libnpfs/libnpfs.a ../libnpclient.a 
	$(CC) -o rdmals $(CFLAGS) rdmals.o $(LFLAGS) 

9write: 9write.o ../../libnpfs/libnpfs.a ../libnpclient.a
	$(CC) -o 9write $(CFLAGS) 9write.o $(LFLAGS)

9read: 9read.o ../../libnpfs/libnpfs.a ../libnpclient.a
	$(CC) -o 9read $(CFLAGS) 9read.o $(LFLAGS)

clean:
	rm -f *.o *.a *~ $(CMD)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
