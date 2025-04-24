/*
 * Copyright (C) 2005-2008 by Latchesar Ionkov <lucho@ionkov.net>
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
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
//#define _XOPEN_SOURCE 500
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include "npfs.h"
#include "ufs.h"

void
usage()
{
	fprintf(stderr, "npfs: -d -s -r -p port -w nthreads\n");
	exit(-1);
}

int
main(int argc, char **argv)
{
	int c;
	int port, nwthreads;
	int tcponly, rdmaonly;
	u32 msize;
	char *s;

	port = 5640;
	nwthreads = 16;
	msize = 1024*1024;
	rdmaonly = 0;
	tcponly = 0;
	while ((c = getopt(argc, argv, "drstp:w:")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'r':
			rdmaonly++;
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 's':
			sameuser = 1;
			break;

		case 't':
			tcponly++;
			break;

		case 'm':
			msize = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		default:
			usage();
		}
	}

	srv = NULL;

#ifdef NPRDMA
	if (!tcponly)
		srv = np_rdmasrv_create(nwthreads, &port);

	if (!srv && !rdmaonly)
		srv = np_socksrv_create_tcp(nwthreads, &port);
#else
	if (rdmaonly) {
		fprintf(stderr, "RDMA not available at compile time\n");
		return -1;
	}

	srv = np_socksrv_create_tcp(nwthreads, &port);
#endif

	if (!srv)
		return -1;

	srv->dotu = 1;
	srv->msize = msize + IOHDRSZ;
	srv->attach = npfs_attach;
	srv->clone = npfs_clone;
	srv->walk = npfs_walk;
	srv->open = npfs_open;
	srv->create = npfs_create;
	srv->read = npfs_read;
	srv->write = npfs_write;
	srv->clunk = npfs_clunk;
	srv->remove = npfs_remove;
	srv->stat = npfs_stat;
	srv->wstat = npfs_wstat;
	srv->flush = npfs_flush;
	srv->fiddestroy = npfs_fiddestroy;
	srv->debuglevel = debuglevel;

	np_srv_start(srv);
	while (1) {
		sleep(100);
	}

	return 0;
}

