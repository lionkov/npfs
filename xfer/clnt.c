/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include "npfs.h"
#include "npclient.h"
#ifdef _WIN32
  #include "winhelp.c"
#else
  #include <unistd.h>
  #define __cdecl
#endif

extern int npc_chatty;

static u32 msize;
static int rdmaonly;	// only try to connect using RDMA
static int tcponly;	// only try to connect using TCP
static int bypass;	// don't do file system I/O

static Npcfsys *mount(char *addr, int port, Npuser *user, u32 msize)
{
	Npcfsys *fs;
	struct addrinfo *ai;

	fs = NULL;
	ai = npc_netaddr(addr, port);
	if (!ai)
		return NULL;

#ifdef NPRDMA
	if (!tcponly)
		fs = npc_rdmamount(ai, 0, user, msize + IOHDRSZ, port, NULL, NULL);

	if (!fs && !rdmaonly)
		fs = npc_netmount(ai, 0, user, msize + IOHDRSZ, port, NULL, NULL);
#else
	if (rdmaonly)
		return NULL;

	fs = npc_netmount(ai, 0, user, msize + IOHDRSZ, port, NULL, NULL);
#endif

	return fs;
}

// Copy from the 9P file server to the local file system
static int64_t copy_from(Npcfsys *fs, char *from, char *to)
{
	int i, n, fd;
	u64 off;
	int64_t ret;
	Npcfid *fid;
	char *buf;

	fid = npc_open(fs, from, Oread);
	if (!fid)
		return -1;

	fd = open(to, O_CREAT | O_TRUNC | O_RDWR);
	if (fd < 0) {
		np_uerror(errno);
		return -1;
	}

	off = 0;
	ret = 0;
	buf = malloc(msize);
	while ((n = npc_read(fid, (u8*) buf, msize, off)) > 0) {
		if (!bypass) {
			i = write(fd, buf, n);
			if (i < 0) {
				np_uerror(errno);
				ret = -1;
				break;
			} else if (i != n)
				break;
		}

		off += n;
		ret += n;
	}

	npc_close(fid);
	close(fd);
	free(buf);
	return ret;
}

static int64_t copy_to(Npcfsys *fs, char *from, char *to)
{
	int i, n, fd;
	u64 off, fsz;
	int64_t ret;
	Npcfid *fid;
	char *buf;

//	printf("copy from '%s' to '%s'\n", from, to);
	fd = open(from, O_RDONLY);
	if (fd < 0) {
		np_uerror(errno);
		return -1;
	}

	fid = npc_create(fs, to, 0666, Owrite);
	if (!fid)
		return -1;

	buf = malloc(msize);
	if (bypass) {
		struct stat statbuf;

		memset(buf, 0, msize);
		n = msize;
		if (fstat(fd, &statbuf) < 0) {
			np_uerror(errno);
			return -1;
		}

		fsz = statbuf.st_size;
	}

	off = 0;
	ret = 0;
	while (1) {
		if (bypass) {
			if (fsz - off > msize)
				n = msize;
			else
				n = fsz - off;
		} else
			n = read(fd, buf, msize);

		if (n < 0) {
			np_uerror(errno);
			ret = -1;
			break;
		} else if (n == 0)
			break;

		i = npc_write(fid, (u8*) buf, n, off);
		if (i < 0) {
			ret = -1;
			break;
		} else if (i != n)
			break;

		off += n;
		ret += n;
	}

	npc_close(fid);
	close(fd);
	free(buf);
	return ret;
}

static int xrename(Npcfsys *fs, char *from, char *to)
{
	Npwstat st;

	npc_emptystat(&st);
	st.name = strdup(to);
	return npc_wstat(fs, from, &st);
}

static void
usage()
{
	fprintf(stderr, "clnt [-db] [-r] [-t] [-p port] [-u user] [-m msize] -a addr fromfile tofile\n");
	exit(1);
}

int __cdecl
main(int argc, char **argv)
{
	int c, port;
	char *addr, *s;
	char *fromfile, *tofile;
	Npuser *user;
	Npcfsys *fs;
	struct timespec sts, ets;
	double st, et;
	int64_t sz;

	port = 5640;
	msize = 1024*1024;
//	npc_chatty = 1;

#ifdef _WIN32
	init();
	user = np_default_users->uname2user(np_default_users, "nobody");
#else
	user = np_default_users->uid2user(np_default_users, geteuid());
	if (!user) {
		fprintf(stderr, "cannot retrieve user %d\n", geteuid());
		exit(1);
	}
#endif

	while ((c = getopt(argc, argv, "a:bdm:p:rtu:")) != -1) {
		switch (c) {
		case 'a':
			addr = strdup(optarg);
			break;

		case 'b':
			bypass++;
			break;

		case 'd':
			npc_chatty = 1;
			break;

		case 'm':
			msize = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'r':
			rdmaonly++;
			break;

		case 't':
			tcponly++;
			break;

		case 'u':
			user = np_default_users->uname2user(np_default_users, optarg);
			break;

		default:
			usage();
		}
	}

	

	if (argc - optind < 2)
		usage();

	fromfile = argv[optind];
	tofile = argv[optind+1];

	fs = mount(addr, port, user, msize);
	if(!fs) {
		char *estr;
		int eno;

		np_rerror(&estr, &eno);
		fprintf(stderr, "error mounting: (%d) %s\n", eno, estr);
		return -1;
	}

	clock_gettime(CLOCK_MONOTONIC, &sts);
	sz = copy_to(fs, fromfile, tofile);
	if (sz < 0) {
		char *estr;
		int eno;

		np_rerror(&estr, &eno);
		fprintf(stderr, "error copying: (%d) %s\n", eno, estr);
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &ets);

	st = sts.tv_sec + sts.tv_nsec * 1e-9;
	et = ets.tv_sec + ets.tv_nsec * 1e-9;
	printf("%g MB/s\n", sz / (1024*1024*(et - st)));

	npc_umount(fs);
	return 0;
}
