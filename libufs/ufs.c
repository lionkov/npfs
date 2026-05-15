/*
 * Copyright (C) 2005-2025 by Latchesar Ionkov <lucho@ionkov.net>
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
//#define _BSD_SOURCE
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
#include <npfs.h>
#include <ufs.h>

#include "ufsimpl.h"

Npsrv *ufs_start(char *rootdir, int debuglevel, int nwthreads, int same_user, int port)
{
	Npsrv *srv;

	if (port <= 0)
		srv = np_pipesrv_create(nwthreads);
	else
		srv = np_socksrv_create_tcp(nwthreads, &port);

	if (!srv)
		return NULL;

	srv->dotu = 1;
	srv->dotl = 1;
	srv->treeaux = rootdir;
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

	srv->statfs = npfs_statfs;
	srv->lopen = npfs_lopen;
	srv->lcreate = npfs_lcreate;
	srv->symlink = npfs_symlink;
	srv->mknod = npfs_mknod;
	srv->rename = npfs_rename;
	srv->readlink = npfs_readlink;
	srv->getattr = npfs_getattr;
	srv->setattr = npfs_setattr;
	srv->xattrwalk = npfs_xattrwalk;
	srv->xattrcreate = npfs_xattrcreate;
	srv->readdir = npfs_readdir;
	srv->fsync = npfs_fsync;
	srv->flock = npfs_flock;
	srv->getlock = npfs_getlock;
	srv->link = npfs_link;
	srv->mkdir = npfs_mkdir;
	srv->renameat = npfs_renameat;
	srv->unlinkat = npfs_unlinkat;
	
	srv->fiddestroy = npfs_fiddestroy;
	srv->debuglevel = debuglevel;

	sameuser = same_user;
	np_srv_start(srv);

	return srv;
}

void ufs_get_fds(Npsrv *srv, int *rfd, int *wfd)
{
	return np_pipesrv_getfds(srv, rfd, wfd);
}

int ufs_checkpoint(Npsrv *srv, void **buf)
{
	int i, sz, nfids;
	int fidsz;
	Npconn *conn;
	Npfid **fids;
	char *data, *p;
	struct cbuf cbuf;

	conn = srv->conns;	// there is only one connection
	sz = 4 + 1 + 1 + 4;	// msize[4] dotu[1] dotl[1] nfids[4]
	fidsz = 4 + 2 + 2 + 4 + 4;	// fid[4] omode[2] type[2] diroffset[4] uid[4]
	fidsz += 2 + 4 + 2 + 4 + 8;	// path[s] omode[4] xattrname[s] xattrflags[4] xattrsz[8]

	nfids = np_conn_list_fids(conn, &fids);
	for(i = 0; i < nfids; i++) {
		Npfid *fid = fids[i];
		Fid *f = fid->aux;

		sz += fidsz + strlen(f->path);
		sz += (f->xattrname != NULL)?strlen(f->xattrname):0;
		sz += f->xattrsz;
	}

	data = malloc(sz);

	buf_init(&cbuf, data, sz);
	buf_put_int32(&cbuf, conn->msize);
	buf_put_int8(&cbuf, conn->dotu);
	buf_put_int8(&cbuf, conn->dotl);
	buf_put_int32(&cbuf, nfids);
	for(i = 0; i < nfids; i++) {
		Npfid *fid = fids[i];
		Fid *f = fid->aux;

		buf_put_int32(&cbuf, fid->fid);
		buf_put_int16(&cbuf, fid->omode);
		buf_put_int16(&cbuf, fid->type);
		buf_put_int32(&cbuf, fid->diroffset);
		buf_put_int32(&cbuf, fid->user->uid);

		buf_put_str(&cbuf, f->path);
		buf_put_int32(&cbuf, f->omode);
//		buf_put_int32(&cbuf, f->diroffset);
		buf_put_str(&cbuf, f->xattrname);
		buf_put_int32(&cbuf, f->xattrflags);
		buf_put_int64(&cbuf, f->xattrsz);
		p = buf_alloc(&cbuf, f->xattrsz);
		if (p)
			memmove(p, f->xattrdata, f->xattrsz);
	}

	if (buf_check_overflow(&cbuf)) {
		free(data);
		return -1;
	}

	*buf = data;
	return sz;
}

int ufs_restore(Npsrv *srv, void *buf, int sz, char *err, int errsz)
{
	int i, n, nfids, errval;
	Npconn *conn;
	Npfid **fids;
	char *p;
	uid_t uid;
	struct cbuf cbuf;
	Npstr str;

	n = 0;
	conn = srv->conns;	// there is only one connection
	buf_init(&cbuf, buf, sz);
	conn->msize = buf_get_int32(&cbuf);
	conn->dotu = buf_get_int8(&cbuf);
	conn->dotl = buf_get_int8(&cbuf);
	nfids = buf_get_int32(&cbuf);
	fids = malloc(nfids * sizeof(Fid *));
	memset(fids, 0, nfids * sizeof(Fid *));
	for(i = 0; i < nfids; i++) {
		u32 fidno;
		Npfid *fid;
		Fid *f;

		f = npfs_fidalloc();
		fidno = buf_get_int32(&cbuf);
		if (buf_check_overflow(&cbuf))
			break;

		fid = np_fid_create(conn, fidno, f);
		fids[i] = fid;

		fid->omode = buf_get_int16(&cbuf);
		fid->type = buf_get_int16(&cbuf);
		fid->diroffset = buf_get_int32(&cbuf);

		uid = buf_get_int32(&cbuf);
		fid->user = srv->upool->uid2user(srv->upool, uid);

		buf_get_str(&cbuf, &str);
		f->path = np_strdup(&str);

		f->omode = buf_get_int32(&cbuf);
//		f->diroffset = buf_get_int32(&cbuf);
		buf_get_str(&cbuf, &str);
		if (str.len == 0)
			f->xattrname = NULL;
		else
			f->xattrname = np_strdup(&str);

		f->xattrflags = buf_get_int32(&cbuf);
		f->xattrsz = buf_get_int64(&cbuf);
		p = buf_alloc(&cbuf, f->xattrsz);
		if (f->xattrsz > 0 && p) {
			f->xattrdata = malloc(f->xattrsz);
			memmove(f->xattrdata, p, f->xattrsz);
		} else {
			f->xattrdata = NULL;
		}
	}

	if (buf_check_overflow(&cbuf)) {
		if (errsz > n)
			n += snprintf(err, errsz - n, "Unexpected end of restore data\n");

		goto error;
	}

	if (buf_check_size(&cbuf, 1)) {
		if (errsz > n)
			n += snprintf(err, errsz - n, "Extra data at the end of the restore buffer\n");

		// we haven't consumed the whole buffer, something went wrong?
		goto error;
	}

	// Go over the fids and set them up. If there are errors, return as many as possible
	for(i = 0; i < nfids; i++) {
		Npfid *fid = fids[i];
		Fid *f = fid->aux;

		if ((errval = fidstat(f)) != 0) {
			if (errsz > n)
				n += snprintf(err, errsz - n, "Can't stat file %s: %d\n", f->path, errval);
		}

		if (fid->omode != Onotopen) {
			if (S_ISDIR(f->stat.st_mode)) {
				printf("\t%d open dir %s\n", fid->fid, f->path);
				f->dir = opendir(f->path);
				if (!f->dir) {
					if (errsz > n)
						n += snprintf(err, errsz - n, "Can't opendir file %s: %d\n", f->path, errno);
				}
			} else {
				int flags;

				printf("\t%d open file '%s'\n", fid->fid, f->path);
				flags = omode2uflags(fid->omode);
				flags &= ~(O_TRUNC | O_EXCL);
				f->fd = open(f->path, flags);
				if (f->fd < 0)
					if (errsz > n)
						n += snprintf(err, errsz - n, "Can't open file %s: %d\n", f->path, errno);
			}
		}

		np_fid_incref(fid);
	}
	free(fids);

	if (n > 0)
		goto error;

	return 0;

error:
	// we may leak fids, but we are going to leak a lot of stuff if we fail
	return -1;
}
