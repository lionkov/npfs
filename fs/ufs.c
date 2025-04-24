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
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
//#define _XOPEN_SOURCE 500
#define _DEFAULT_SOURCE
//#define _BSD_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/xattr.h>
#include <sys/vfs.h>
#include <sys/time.h>
#include "npfs.h"
#include "ufs.h"

#if SYSNAME == Linux
#define NPFS_USE_AIO
#else
#undef NPFS_USE_AIO
#endif

#undef NPFS_USE_AIO

#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct Fid Fid;

struct Fid {
	char*		path;
	int		omode;
	int		fd;
	DIR*		dir;
	int		diroffset;
	char*		direntname;
	struct stat	stat;

	char*		xattrname;	// xattrcreate
	u32		xattrflags;	// xattrcreate
	u64		xattrsz;
	u8*		xattrdata;
};

Npsrv *srv;
int debuglevel;
int sameuser;

char *Estatfailed = "stat failed";
char *Ebadfid = "fid unknown or out of range";
char *Enoextension = "empty extension while creating special file";
char *Eformat = "incorrect extension format";
char *Ecreatesocket = "cannot create socket";
//char *E = "";

static int fidstat(Fid *fid);
static void ustat2qid(struct stat *st, Npqid *qid);
static u8 ustat2qidtype(struct stat *st);
static void ustat2attrs(struct stat *st, Npattrs *at);
static u32 umode2npmode(mode_t umode, int dotu);
static mode_t npstat2umode(Npstat *st, int dotu);
static void ustat2npwstat(char *path, struct stat *st, Npwstat *wstat, int dotu, Npuserpool *up);

static int npfs_aio_read(Npfid *fid, Npfcall *rread, u64 offset, u32 count, Npreq *);
static int npfs_aio_write(Npfid *fid, u8 *data, u64 offset, u32 count, Npreq *);

#ifdef NPFS_USE_AIO
#include <libaio.h>
typedef struct Aioreq Aioreq;

struct Aioreq {
	struct iocb	iocb;
	Npreq*		req;
	Npfid*		fid;
	Npfcall*	rread;
	Aioreq*		next;
	Aioreq*		prev;
};

int use_aio = 1;
io_context_t aio_ctx;
pthread_mutex_t aio_lock = PTHREAD_MUTEX_INITIALIZER;
Aioreq *aio_reqs;
#else
int use_aio = 0;
#endif

pthread_t aio_thread;

static int
fidstat(Fid *fid)
{
	if (lstat(fid->path, &fid->stat) < 0)
		return errno;

//	if (S_ISDIR(fid->stat.st_mode))
//		fid->stat.st_size = 0;

	return 0;
}

static Fid*
npfs_fidalloc() {
	Fid *f;

	f = malloc(sizeof(*f));

	f->path = NULL;
	f->omode = -1;
	f->fd = -1;
	f->dir = NULL;
	f->diroffset = 0;
	f->direntname = NULL;
	f->xattrname = NULL;
	f->xattrdata = NULL;
	f->xattrsz = 0;
	f->xattrflags = 0;

	return f;
}

void
npfs_fiddestroy(Npfid *fid)
{
	Fid *f;

	f = fid->aux;
	if (!f)
		return;

	if (f->fd != -1)
		close(f->fd);

	if (f->dir)
		closedir(f->dir);

	free(f->xattrname);
	free(f->xattrdata);
	free(f->path);
	free(f);
}

static void
create_rerror(int ecode)
{
	char buf[256];

	strerror_r(ecode, buf, sizeof(buf));
	np_werror(buf, ecode);
}

static int
omode2uflags(u8 mode)
{
	int ret;

	ret = 0;
	switch (mode & 3) {
	case Oread:
		ret = O_RDONLY;
		break;

	case Ordwr:
		ret = O_RDWR;
		break;

	case Owrite:
		ret = O_WRONLY;
		break;

	case Oexec:
		ret = O_RDONLY;
		break;
	}

	if (mode & Otrunc)
		ret |= O_TRUNC;

	if (mode & Oappend)
		ret |= O_APPEND;

	if (mode & Oexcl)
		ret |= O_EXCL;

	return ret;
}

static void
ustat2qid(struct stat *st, Npqid *qid)
{
	int n;

	qid->path = 0;
	n = sizeof(qid->path);
	if (n > sizeof(st->st_ino))
		n = sizeof(st->st_ino);
	memmove(&qid->path, &st->st_ino, n);
	qid->version = st->st_mtime ^ (st->st_size << 8);
	qid->type = ustat2qidtype(st);
}

static void
ustat2attrs(struct stat *st, Npattrs *at)
{
	memset(at, 0, sizeof(*at));
	at->mode = st->st_mode;
	at->uid = st->st_uid;
	at->gid = st->st_gid;
	at->nlink = st->st_nlink;
	at->rdev = st->st_rdev;
	at->size = st->st_size;
	at->blksize = st->st_blksize;
	at->blocks = st->st_blocks;
	at->atime_sec = st->st_atim.tv_sec;
	at->atime_nsec = st->st_atim.tv_nsec;
	at->mtime_sec = st->st_mtim.tv_sec;
	at->mtime_nsec = st->st_mtim.tv_nsec;
	at->ctime_sec = st->st_ctim.tv_sec;
	at->ctime_nsec = st->st_ctim.tv_nsec;
	at->mask = AGbasic;
}

static u8
ustat2qidtype(struct stat *st)
{
	u8 ret;

	ret = 0;
	if (S_ISDIR(st->st_mode))
		ret |= Qtdir;

	if (S_ISLNK(st->st_mode))
		ret |= Qtsymlink;

	return ret;
}

static u32
umode2npmode(mode_t umode, int dotu)
{
	u32 ret;

	ret = umode & 0777;
	if (S_ISDIR(umode))
		ret |= Dmdir;

	if (dotu) {
		if (S_ISLNK(umode))
			ret |= Dmsymlink;
		if (S_ISSOCK(umode))
			ret |= Dmsocket;
		if (S_ISFIFO(umode))
			ret |= Dmnamedpipe;
		if (S_ISBLK(umode))
			ret |= Dmdevice;
		if (S_ISCHR(umode))
			ret |= Dmdevice;
		if (umode & S_ISUID)
			ret |= Dmsetuid;
		if (umode & S_ISGID)
			ret |= Dmsetgid;
	}

	return ret;
}

static mode_t
np2umode(u32 mode, Npstr *extension, int dotu)
{
	mode_t ret;

	ret = mode & 0777;
	if (mode & Dmdir)
		ret |= S_IFDIR;

	if (dotu) {
		if (mode & Dmsymlink)
			ret |= S_IFLNK;
		if (mode & Dmsocket)
			ret |= S_IFSOCK;
		if (mode & Dmnamedpipe)
			ret |= S_IFIFO;
		if (mode & Dmdevice) {
			if (extension && extension->str[0] == 'c')
				ret |= S_IFCHR;
			else
				ret |= S_IFBLK;
		}
	}

	if (!(ret&~0777))
		ret |= S_IFREG;

	if (mode & Dmsetuid)
		ret |= S_ISUID;
	if (mode & Dmsetgid)
		ret |= S_ISGID;

	return ret;
}

static mode_t
npstat2umode(Npstat *st, int dotu)
{
	return np2umode(st->mode, &st->extension, dotu);
}

static void
ustat2npwstat(char *path, struct stat *st, Npwstat *wstat, int dotu, Npuserpool *up)
{
	int err;
	Npuser *u;
	Npgroup *g;
	char *s, ext[256];

	memset(wstat, 0, sizeof(*wstat));
	ustat2qid(st, &wstat->qid);
	wstat->mode = umode2npmode(st->st_mode, dotu);
	wstat->atime = st->st_atime;
	wstat->mtime = st->st_mtime;
	wstat->length = st->st_size;

	u = up->uid2user(up, st->st_uid);
	g = up->gid2group(up, st->st_gid);
	
	wstat->uid = u?u->uname:"???";
	wstat->gid = g?g->gname:"???";
	wstat->muid = "";

	wstat->extension = NULL;
	if (dotu) {
		wstat->n_uid = st->st_uid;
		wstat->n_gid = st->st_gid;

		if (wstat->mode & Dmsymlink) {
			err = readlink(path, ext, sizeof(ext) - 1);
			if (err < 0)
				err = 0;

			ext[err] = '\0';
		} else if (wstat->mode & Dmdevice) {
			snprintf(ext, sizeof(ext), "%c %u %u", 
				S_ISCHR(st->st_mode)?'c':'b',
				major(st->st_rdev), minor(st->st_rdev));
		} else {
			ext[0] = '\0';
		}

		wstat->extension = strdup(ext);
	}

	s = strrchr(path, '/');
	if (s)
		wstat->name = s + 1;
	else
		wstat->name = path;
}

static inline void
npfs_set_user(Npuser *user)
{
	if (sameuser)
		return;

	if (geteuid() == user->uid)
		return;

	np_change_user(user);
}

// Check if the fid points to a real file or xattr
// Set an error (EINVAL) response if xattr
static inline int
npfs_check_regular(Fid *f)
{
	if (f->xattrdata) {
		create_rerror(EINVAL);
		return 0;
	}

	return 1;
}

Npfcall*
npfs_attach(Npfid *nfid, Npfid *nafid, Npstr *uname, Npstr *aname)
{
	int err;
	Npfcall* ret;
	Fid *fid;
	Npqid qid;

	ret = NULL;

	npfs_set_user(nfid->user);
	if (nafid != NULL) {
		np_werror(Enoauth, EIO);
		goto done;
	}

	fid = npfs_fidalloc();
	fid->omode = -1;
	if (aname->len==0 || *aname->str!='/')
		fid->path = strdup("/");
	else
		fid->path = np_strdup(aname);
	
	nfid->aux = fid;
	err = fidstat(fid);
	if (err < 0) {
		create_rerror(err);
		goto done;
	}

	ustat2qid(&fid->stat, &qid);
	nfid->type = qid.type;
	ret = np_create_rattach(&qid);
	np_fid_incref(nfid);

done:
	return ret;
}

int
npfs_clone(Npfid *fid, Npfid *newfid)
{
	Fid *f, *nf;

	f = fid->aux;
	nf = npfs_fidalloc();
	nf->path = strdup(f->path);
	newfid->aux = nf;
//	newfid->type = fid->type;

	return 1;	
}


int
npfs_walk(Npfid *fid, Npstr* wname, Npqid *wqid)
{
	int n;
	Fid *f;
	struct stat st;
	char *path;

	f = fid->aux;
	npfs_set_user(fid->user);
	n = fidstat(f);
	if (n < 0)
		create_rerror(n);

	n = strlen(f->path);
	path = malloc(n + wname->len + 2);
	memcpy(path, f->path, n);
	path[n] = '/';
	memcpy(path + n + 1, wname->str, wname->len);
	path[n + wname->len + 1] = '\0';

	if (lstat(path, &st) < 0) {
		free(path);
		create_rerror(errno);
		return 0;
	}

	free(f->path);
	f->path = path;
	ustat2qid(&st, wqid);

	return 1;
}

Npfcall*
npfs_open(Npfid *fid, u8 mode)
{
	int err;
	Fid *f;
	Npqid qid;

	f = fid->aux;
	npfs_set_user(fid->user);
	if ((err = fidstat(f)) < 0)
		create_rerror(err);

	if (S_ISDIR(f->stat.st_mode)) {
		f->dir = opendir(f->path);
		if (!f->dir)
			create_rerror(errno);
	} else {
		f->fd = open(f->path, omode2uflags(mode));
		if (f->fd < 0)
			create_rerror(errno);
	}

	err = fidstat(f);
	if (err < 0)
		create_rerror(err);

	f->omode = mode;
	ustat2qid(&f->stat, &qid);
	return np_create_ropen(&qid, 0);
}

static int
npfs_create_special(Npfid *fid, char *path, u32 perm, Npstr *extension)
{
	int nfid, err;
	int nmode, major, minor;
	char ctype;
	mode_t umode;
	Npfid *ofid;
	Fid *f, *of;
	char *ext;

	f = fid->aux;
	if (!(perm&Dmnamedpipe) && !extension->len) {
		np_werror(Enoextension, EIO);
		return -1;
	}

	umode = np2umode(perm, extension, fid->conn->dotu);
	ext = np_strdup(extension);
	if (perm & Dmsymlink) {
		if (symlink(ext, path) < 0) {
			err = errno;
			fprintf(stderr, "symlink %s %s %d\n", ext, path, err);
			create_rerror(err);
			goto error;
		}
	} else if (perm & Dmlink) {
		if (sscanf(ext, "%d", &nfid) == 0) {
			np_werror(Eformat, EIO);
			goto error;
		}

		ofid = np_fid_find(fid->conn, nfid);
		if (!ofid) {
			np_werror(Eunknownfid, EIO);
			goto error;
		}

		of = ofid->aux;
		if (link(of->path, path) < 0) {
			create_rerror(errno);
			goto error;
		}
	} else if (perm & Dmdevice) {
		if (sscanf(ext, "%c %u %u", &ctype, &major, &minor) != 3) {
			np_werror(Eformat, EIO);
			goto error;
		}

		nmode = 0;
		switch (ctype) {
		case 'c':
			nmode = S_IFCHR;
			break;

		case 'b':
			nmode = S_IFBLK;
			break;

		default:
			np_werror(Eformat, EIO);
			goto error;
		}

		nmode |= perm & 0777;
		if (mknod(path, nmode, makedev(major, minor)) < 0) {
			create_rerror(errno);
			goto error;
		}
	} else if (perm & Dmnamedpipe) {
		if (mknod(path, S_IFIFO | (umode&0777), 0) < 0) {
			create_rerror(errno);
			goto error;
		}
	}

	f->omode = 0;
	if (!(perm&Dmsymlink) && chmod(path, umode)<0) {
		create_rerror(errno);
		goto error;
	}

	free(ext);
	return 0;

error:
	free(ext);
	return -1;
}


Npfcall*
npfs_create(Npfid *fid, Npstr *name, u32 perm, u8 mode, Npstr *extension)
{
	int n, err, omode;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;
	struct stat st;

	ret = NULL;
	omode = mode;
	f = fid->aux;
	if ((err = fidstat(f)) < 0)
		create_rerror(err);

	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	if (lstat(npath, &st)==0 || errno!=ENOENT) {
		np_werror(Eexist, EEXIST);
		goto out;
	}

	if (perm & Dmdir) {
		if (mkdir(npath, perm & 0777) < 0) {
			create_rerror(errno);
			goto out;
		}

		if (lstat(npath, &f->stat) < 0) {
			create_rerror(errno);
			rmdir(npath);
			goto out;
		}
		
		f->dir = opendir(npath);
		if (!f->dir) {
			create_rerror(errno);
			remove(npath);
			goto out;
		}
	} else if (perm & (Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice)) {
		if (npfs_create_special(fid, npath, perm, extension) < 0)
			goto out;

		if (lstat(npath, &f->stat) < 0) {
			create_rerror(errno);
			remove(npath);
			goto out;
		}
	} else {
		f->fd = open(npath, O_CREAT|omode2uflags(mode), 
			perm & 0777);
		if (f->fd < 0) {
			create_rerror(errno);
			goto out;
		}

		if (lstat(npath, &f->stat) < 0) {
			create_rerror(errno);
			remove(npath);
			goto out;
		}
	}

	free(f->path);
	f->path = npath;
	f->omode = omode;
	npath = NULL;
	ustat2qid(&f->stat, &qid);
	ret = np_create_rcreate(&qid, 0);

out:
	free(npath);
	return ret;
}

u32
npfs_read_dir(Npfid *fid, u8* buf, u64 offset, u32 count, int dotu)
{
	int i, n, plen;
	char *dname, *path;
	struct dirent *dirent;
	struct stat st;
	Npwstat wstat;
	Fid *f;

	f = fid->aux;
/*
	if (f->dir == NULL) {
		f->dir = fdopendir(f->fd);
		if (f->dir == NULL) {
			create_rerror(errno);
			return 0;
		}
	}
*/

	if (offset == 0) {
		rewinddir(f->dir);
		f->diroffset = 0;
	}

	plen = strlen(f->path);
	n = 0;
	dirent = NULL;
	dname = f->direntname;
	while (n < count) {
		if (!dname) {
			dirent = readdir(f->dir);
			if (!dirent)
				break;

			if (strcmp(dirent->d_name, ".") == 0
			|| strcmp(dirent->d_name, "..") == 0)
				continue;

			dname = dirent->d_name;
		}

		path = malloc(plen + strlen(dname) + 2);
		sprintf(path, "%s/%s", f->path, dname);
		if (lstat(path, &st) < 0) {
			free(path);
			create_rerror(errno);
			return 0;
		}

		ustat2npwstat(path, &st, &wstat, dotu, fid->conn->srv->upool);
		i = np_serialize_stat(&wstat, buf + n, count - n - 1, dotu);
		free(wstat.extension);
		free(path);
		path = NULL;
		if (i==0)
			break;

		dname = NULL;
		n += i;
	}

	if (f->direntname) {
		free(f->direntname);
		f->direntname = NULL;
	}

	if (dirent)
		f->direntname = strdup(dirent->d_name);

	f->diroffset += n;
	return n;
}

Npfcall*
npfs_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	int n;
	Fid *f;
	Npfcall *ret;

	f = fid->aux;
	ret = np_alloc_rread(count);
	npfs_set_user(fid->user);
	if (f->xattrdata) {
		n = count;
		if (offset > f->xattrsz)
			n = 0;
		else if (offset + count > f->xattrsz)
			n = f->xattrsz - offset;

		memmove(ret->data, f->xattrdata+offset, n);
	} else if (f->dir) {
		n = npfs_read_dir(fid, ret->data, offset, count, fid->conn->dotu);
	} else {
		if (use_aio) {
			n = npfs_aio_read(fid, ret, offset, count, req);
			if (n >= 0)
				return NULL;
		}
			
		n = pread(f->fd, ret->data, count, offset);
		if (n < 0)
			create_rerror(errno);
	}

	if (np_haserror()) {
		free(ret);
		ret = NULL;
	} else
		np_set_rread_count(ret, n);

	return ret;
}

Npfcall*
npfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	Fid *f;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	npfs_set_user(fid->user);

	if (f->xattrdata) {
		n = count;
		if (offset > f->xattrsz)
			n = 0;
		else if (offset + count > f->xattrsz)
			n = f->xattrsz - offset;

		memmove(f->xattrdata+offset, data, n);
	} else {
		if (use_aio) {
			n = npfs_aio_write(fid, data, offset, count, req);
//			fprintf(stderr, "$$ %d\n", n);
			if (n >= 0)
				return NULL;
		}

		n = pwrite(f->fd, data, count, offset);
		if (n < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	ret = np_create_rwrite(n);

out:
	return ret;
}

Npfcall*
npfs_clunk(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	if (f!= NULL && f->xattrname) {
		if ((fid->omode&3) != Oread) {
			// an xattr was created, store it
			if (lsetxattr(f->path, f->xattrname, f->xattrdata, f->xattrsz, f->xattrflags) < 0) {
				create_rerror(errno);
				goto out;
			}
		}
	}

	ret = np_create_rclunk();
//	np_fid_decref(fid);

out:
	return ret;
}

Npfcall*
npfs_remove(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	npfs_set_user(fid->user);
	if (remove(f->path) < 0) {
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rremove();

out:
//	np_fid_decref(fid);
	return ret;

}

Npfcall*
npfs_stat(Npfid *fid)
{
	int err;
	Fid *f;
	Npfcall *ret;
	Npwstat wstat;

	f = fid->aux;
	npfs_set_user(fid->user);
	err = fidstat(f);
	if (err < 0)
		create_rerror(err);

	ustat2npwstat(f->path, &f->stat, &wstat, fid->conn->dotu, fid->conn->srv->upool);

	ret = np_create_rstat(&wstat, fid->conn->dotu);
	free(wstat.extension);

	return ret;
}

Npfcall*
npfs_wstat(Npfid *fid, Npstat *stat)
{
	int err;
	Fid *f;
	Npfcall *ret;
	uid_t uid;
	gid_t gid;
	char *npath, *p, *s;
	Npuser *user;
	Npgroup *group;
	struct utimbuf tb;
	Npuserpool *up;

	ret = NULL;
	f = fid->aux;
	up = fid->conn->srv->upool;
	npfs_set_user(fid->user);
	err = fidstat(f);
	if (err < 0) {
		create_rerror(err);
		goto out;
	}

	if (fid->conn->dotu) {
		uid = stat->n_uid;
		gid = stat->n_gid;
	} else {
		uid = (uid_t) -1;
		gid = (gid_t) -1;
	}

	if (uid == -1 && stat->uid.len) {
		s = np_strdup(&stat->uid);
		user = up->uname2user(up, s);
		free(s);
		if (!user) {
			np_werror(Eunknownuser, EIO);
			goto out;
		}

		uid = user->uid;
	}

	if (gid == -1 && stat->gid.len) {
		s = np_strdup(&stat->gid);
		group = up->gname2group(up, s);
		free(s);
		if (!group) {
			np_werror(Eunknownuser, EIO);
			goto out;
		}

		gid = group->gid;
	}

	if (stat->mode != (u32)~0) {
		if (stat->mode&Dmdir && !S_ISDIR(f->stat.st_mode)) {
			np_werror(Edirchange, EIO);
			goto out;
		}

		if (chmod(f->path, npstat2umode(stat, fid->conn->dotu)) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (stat->mtime != (u32)~0) {
		tb.actime = 0;
		tb.modtime = stat->mtime;
		if (utime(f->path, &tb) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (gid != -1) {
		if (chown(f->path, uid, gid) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (stat->name.len != 0) {
		p = strrchr(f->path, '/');
		if (!p)
			p = f->path + strlen(f->path);

		npath = malloc(stat->name.len + (p - f->path) + 2);
		memcpy(npath, f->path, p - f->path);
		npath[p - f->path] = '/';
		memcpy(npath + (p - f->path) + 1, stat->name.str, stat->name.len);
		npath[(p - f->path) + 1 + stat->name.len] = 0;
		if (strcmp(npath, f->path) != 0) {
			if (rename(f->path, npath) < 0) {
				create_rerror(errno);
				goto out;
			}

			free(f->path);
			f->path = npath;
		}
	}

	if (stat->length != ~0) {
		if (truncate(f->path, stat->length) < 0) {
			create_rerror(errno);
			goto out;
		}
	}
	ret = np_create_rwstat();
	
out:
	return ret;
}

#ifdef NPFS_USE_AIO
static void
npfs_aio_respond(Aioreq *areq, struct io_event *event, int flush)
{
	int count;
	Npfcall *rc;
	char buf[128];

	count = event->res;
	rc = NULL;

	if (count<0 && !flush) {
		if (areq->req->tcall->type == Tread)
			free(areq->rread);

		if (strerror_r(count, buf, sizeof(buf)))
			strcpy(buf, "unknown error");

		rc = np_create_rerror(buf, count, areq->req->conn->dotu);
	} else {
		if (areq->req->tcall->type == Tread) {
			rc = areq->rread;
			np_set_rread_count(rc, count);
		} else {
			rc = np_create_rwrite(count);
		}
	}

//	np_fid_decref(areq->fid);
	np_respond(areq->req, rc);
	free(areq);
}
#endif

void
npfs_flush(Npreq *req)
{
	if (req->tcall->type!=Tread && req->tcall->type!=Twrite)
		return;

#ifdef NPFS_USE_AIO
	{
	struct io_event event;
	Aioreq *areq;

	pthread_mutex_lock(&aio_lock);
	for(areq = aio_reqs; areq != NULL; areq = areq->next) {
		if (areq->req == req) {
			io_cancel(aio_ctx, &areq->iocb, &event);
			npfs_aio_respond(areq, &event, 1);

			if (areq->prev)
				areq->prev->next = areq->next;
			else
				aio_reqs = areq->next;
			if (areq->next)
				areq->next->prev = areq->prev;

			free(areq);
			break;
		}
	}
	pthread_mutex_unlock(&aio_lock);
	}
#endif

	return;
}

Npfcall* npfs_statfs(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;
	struct statfs stfs;
	Npstatfs nst;


	ret = NULL;
	f = fid->aux;
	npfs_set_user(fid->user);
	if (statfs(f->path, &stfs) < 0) {
		create_rerror(errno);
		goto out;
	}

	nst.type = stfs.f_type;
	nst.bsize = stfs.f_bsize;
	nst.blocks = stfs.f_blocks;
	nst.bfree = stfs.f_bfree;
	nst.bavail = stfs.f_bavail;
	nst.files = stfs.f_files;
	nst.ffree = stfs.f_ffree;
	nst.fsid = *((u64 *) (&stfs.f_fsid));
	nst.namelen = stfs.f_namelen;

	ret = np_create_rstatfs(&nst);

out:
	return ret;
}

Npfcall* npfs_lopen(Npfid *fid, u32 flags)
{
	int err;
	Fid *f;
	Npqid qid;

	f = fid->aux;
	npfs_set_user(fid->user);
	if ((err = fidstat(f)) < 0)
		create_rerror(err);

	if (S_ISDIR(f->stat.st_mode)) {
		f->dir = opendir(f->path);
		if (!f->dir)
			create_rerror(errno);
	} else {
		f->fd = open(f->path, flags);
		if (f->fd < 0)
			create_rerror(errno);
	}

	err = fidstat(f);
	if (err < 0)
		create_rerror(err);

	// FIXME: do we need to keep the flags for anything?
	ustat2qid(&f->stat, &qid);
	return np_create_ropen(&qid, 0);
}

Npfcall* npfs_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 perm, u32 gid)
{
	int n, err;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;

	ret = NULL;
	f = fid->aux;
	if ((err = fidstat(f)) < 0)
		create_rerror(err);

	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	flags |= O_CREAT;
	f->fd = open(npath, O_CREAT|flags, perm);
	if (f->fd < 0) {
		create_rerror(errno);
		goto out;
	}

	if (lstat(npath, &f->stat) < 0) {
		create_rerror(errno);
		remove(npath);
		goto out;
	}

	free(f->path);
	f->path = npath;
	npath = NULL;
	ustat2qid(&f->stat, &qid);
	ret = np_create_rcreate(&qid, 0);

out:
	free(npath);
	return ret;
}

Npfcall* npfs_symlink(Npfid *dfid, Npstr *name, Npstr *symtgt, u32 gid)
{
	int n;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath, *target;
	struct stat st;

	ret = NULL;
	f = dfid->aux;
	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	target = malloc(symtgt->len + 1);
	memmove(target, symtgt->str, symtgt->len);
	target[symtgt->len] = '\0';

	if (symlink(target, npath) < 0) {
		create_rerror(errno);
		goto out;
	}

	if (stat(target, &st) < 0) {
		create_rerror(errno);
		goto out;
	}

	ustat2qid(&st, &qid);
	ret = np_create_rsymlink(&qid);

out:
	free(target);
	free(npath);
	return ret;
}

Npfcall* npfs_mknod(Npfid *dfid, Npstr *name, u32 perm, u32 major, u32 minor, u32 gid)
{
	int n;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;
	struct stat st;

	ret = NULL;
	f = dfid->aux;
	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	if (mknod(npath, perm, makedev(major, minor)) < 0) {
		create_rerror(errno);
		goto out;
	}


	if (stat(npath, &st) < 0) {
		create_rerror(errno);
		goto out;
	}

	ustat2qid(&st, &qid);
	ret = np_create_rmknod(&qid);
	
out:
	free(npath);
	return ret;
}

Npfcall* npfs_rename(Npfid *fid, Npfid *dfid, Npstr *name)
{
	int n, err;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;

	ret = NULL;
	f = dfid->aux;
	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	if (rename(f->path, npath) < 0) {
		create_rerror(errno);
		goto out;
	}

	// FIXME: do we need to lock?
	free(f->path);
	f->path = npath;
	npath = NULL;
	if ((err = fidstat(f)) < 0) {
		create_rerror(err);
		goto out;
	}

	ustat2qid(&f->stat, &qid);
	ret = np_create_rrename(&qid);

out:
	free(npath);
	return ret;
}

Npfcall* npfs_readlink(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;
	char buf[1024];

	ret = NULL;
	f = fid->aux;
	if (readlink(f->path, buf, sizeof(buf)) < 0) {
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rreadlink(buf);

out:
	return ret;
}

Npfcall* npfs_getattr(Npfid *fid, u64 mask)
{
	int err;
	Fid *f;
	Npfcall *ret;
	Npattrs at;
	Npqid qid;

	ret = NULL;
	f = fid->aux;
	if ((err = fidstat(f)) < 0) {
		create_rerror(err);
		goto out;
	}

	ustat2attrs(&f->stat, &at);
	ustat2qid(&f->stat, &qid);
	ret = np_create_rgetattr(at.mask, &at, &qid);

out:
	return ret;
}

Npfcall* npfs_setattr(Npfid *fid, Npattrs *attrs)
{
	int err;
	Fid *f;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;

	if (attrs->mask & ASmode) {
		if (chmod(f->path, attrs->mode) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (attrs->mask & (ASuid | ASgid | ASctime)) {
		uid_t uid = -1;
		gid_t gid = -1;

		if (attrs->mask & ASuid) {
			uid = attrs->uid;
		}

		if (attrs->mask & ASgid) {
			gid = attrs->gid;
		}

		// ASctime changes ctime without changing the uid/gid
		if (chown(f->path, uid, gid) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (attrs->mask & ASsize) {
		if (truncate(f->path, attrs->size) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (attrs->mask & (ASatime|ASmtime)) {
		struct timeval tv[2], now;

		gettimeofday(&now, NULL);
		if (attrs->mask & ASatime) {
			if (attrs->mask & ASatimeuse) {
				tv[0].tv_sec = attrs->atime_sec;
				tv[0].tv_usec = attrs->atime_nsec / 1000;
			} else {
				tv[0].tv_sec = now.tv_sec;
				tv[0].tv_usec = now.tv_usec;
			}
		} else {
			// atime not changed, take the value from f->stat
			tv[0].tv_sec = f->stat.st_atim.tv_sec;
			tv[0].tv_usec = f->stat.st_atim.tv_nsec / 1000;
		}

		if (attrs->mask & ASmtime) {
			if (attrs->mask & ASmtimeuse) {
				tv[1].tv_sec = attrs->mtime_sec;
				tv[1].tv_usec = attrs->mtime_nsec / 1000;
			} else {
				tv[1].tv_sec = now.tv_sec;
				tv[1].tv_usec = now.tv_usec;
			}
		} else {
			// mtime not changed, take the value from f->stat
			tv[1].tv_sec = f->stat.st_mtim.tv_sec;
			tv[1].tv_usec = f->stat.st_mtim.tv_nsec / 1000;
		}

		if (utimes(f->path, tv) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if ((err = fidstat(f)) < 0) {
		create_rerror(err);
		goto out;
	}

	ret = np_create_rsetattr();

out:
	return ret;
}

Npfcall* npfs_xattrwalk(Npfid *fid, Npfid *newfid, Npstr *name)
{
	Fid *f, *nf;
	Npfcall *ret;
	ssize_t n, sz;
	void *buf;
	char *xname;

	ret = NULL;
	xname = NULL;
	f = fid->aux;
	if (!npfs_check_regular(f))
		goto out;

	nf = npfs_fidalloc();
	nf->path = strdup(f->path);
	newfid->aux = nf;
	newfid->omode = Oread;

	if (name->len == 0) {
lagain:
		sz = llistxattr(nf->path, NULL, 0);
		if (sz < 0) {
			create_rerror(errno);
			goto out;
		}

		buf = malloc(sz);
		n = llistxattr(nf->path, buf, sz);
		if (n != sz) {
			free(buf);
			goto lagain;
		}

		nf->xattrsz = n;
		nf->xattrdata = buf;
	} else {
		xname = np_strdup(name);

aagain:
		sz = lgetxattr(nf->path, xname, NULL, 0);
		if (sz < 0) {
			create_rerror(errno);
			goto out;
		}

		buf = malloc(sz);
		n = lgetxattr(nf->path, xname, buf, sz);
		if (n != sz) {
			free(buf);
			goto aagain;
		}

		nf->xattrsz = n;
		nf->xattrdata = buf;
	}

	np_fid_incref(newfid);
	ret = np_create_rxattrwalk(nf->xattrsz);

out:
	free(xname);

	return ret;
}

Npfcall* npfs_xattrcreate(Npfid *fid, Npfid *newfid, Npstr *name, u32 size, u32 flags)
{
	Fid *f, *nf;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	if (!npfs_check_regular(f))
		goto out;

	nf = newfid->aux;
	nf->xattrname = np_strdup(name);
	nf->xattrflags = flags;
	nf->xattrsz = size;
	nf->xattrdata = malloc(size);
	newfid->omode = Owrite;

	np_fid_incref(newfid);
	ret = np_create_rxattrcreate();

out:
	return ret;
}

Npfcall* npfs_readdir(Npfid *dfid, u64 offset, u32 count, Npreq *req)
{
	int i, n, plen;
	char *dname, *path;
	struct dirent *d;
	struct stat st;
	Npqid qid;
	Fid *f;
	Npfcall *ret;

	f = dfid->aux;
	ret = np_alloc_rread(count);
	npfs_set_user(dfid->user);

	if (f->dir == NULL) {
		f->dir = fdopendir(f->fd);
		if (f->dir == NULL) {
			create_rerror(errno);
			return 0;
		}
	}

	if (offset == 0) {
		rewinddir(f->dir);
		f->diroffset = 0;
	}

	plen = strlen(f->path);
	n = 0;
	d = NULL;
	dname = f->direntname;
	while (n < count) {
		if (!dname) {
			d = readdir(f->dir);
			if (!d)
				break;

			if (strcmp(d->d_name, ".") == 0
			|| strcmp(d->d_name, "..") == 0)
				continue;

			dname = d->d_name;
		}

		memset(&qid, 0, sizeof(qid));
		if (d->d_type == DT_UNKNOWN) {
			path = malloc(plen + strlen(dname) + 2);
			sprintf(path, "%s/%s", f->path, dname);
		
			if (lstat(path, &st) < 0) {
				free(path);
				create_rerror(errno);
				return 0;
			}

			ustat2qid(&st, &qid);
			free(path);
			path = NULL;
		} else {
			int m;

			qid.path = 0;
			m = sizeof(qid.path);
			if (m > sizeof(d->d_ino))
				m = sizeof(d->d_ino);
			memmove(&qid.path, &d->d_ino, m);
			qid.version = 0;
			if (d->d_type == DT_DIR)
				qid.type |= Qtdir;
			if (d->d_type == DT_LNK)
				qid.type |= Qtsymlink;
		}

		i = np_serialize_dirent(&qid, d->d_off, d->d_type, dname, ret->data + n, count - n - 1);
		if (i==0)
			break;

		dname = NULL;
		n += i;
	}

	if (f->direntname) {
		free(f->direntname);
		f->direntname = NULL;
	}

	if (d)
		f->direntname = strdup(d->d_name);

	f->diroffset += n;
	np_set_rread_count(ret, n);

	return ret;
}

Npfcall* npfs_fsync(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	if (f->fd == -1) {
		create_rerror(EINVAL);
		goto out;
	}

	if (fsync(f->fd) < 0) {
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rfsync();

out:
	return ret;
}

Npfcall* npfs_flock(Npfid *fid, u8 type, u32 flags, u64 offset, u64 length, u32 procid, Npstr *clientid)
{
	Fid *f;
	Npfcall *ret;
	struct flock fl;
	int op;

	ret = NULL;
	f = fid->aux;

	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = offset;
	fl.l_len = length;
	fl.l_pid = procid;

	op = F_SETLK;
	if (flags & LFblock) {
		op = F_SETLKW;
	}

	if (fcntl(f->fd, op, &fl) < 0) {
		// FIXME: Should we return the actual error, or LSerror in Rlock?
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rlock(LSsuccess);

out:
	return ret;
}

Npfcall* npfs_getlock(Npfid *fid, u8 type, u64 offset, u64 length, u32 procid, Npstr *clientid)
{
	Fid *f;
	Npfcall *ret;
	struct flock fl;

	ret = NULL;
	f = fid->aux;

	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = offset;
	fl.l_len = length;
	fl.l_pid = procid;

	if (fcntl(f->fd, F_GETLK, &fl) < 0) {
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rgetlock(fl.l_type, fl.l_start, fl.l_len, fl.l_pid, NULL);

out:
	return ret;
}

Npfcall* npfs_link(Npfid *dfid, Npfid *fid, Npstr *name)
{
	int n;
	Fid *f;
	Npfcall *ret;
	char *npath;

	ret = NULL;
	f = dfid->aux;
	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	if (link(f->path, npath) < 0) {
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rlink();

out:
	free(npath);
	return ret;
}

Npfcall* npfs_mkdir(Npfid *dfid, Npstr *name, u32 perm, u32 gid)
{
	int n;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;
	struct stat st;

	ret = NULL;
	f = dfid->aux;
	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	// TODO: gid???
	if (mkdir(npath, perm&0777) < 0) {
		create_rerror(errno);
		goto out;
	}

	if (stat(npath, &st) < 0) {
		create_rerror(errno);
		goto out;
	}

	ustat2qid(&st, &qid);
	ret = np_create_rmkdir(&qid);

out:
	free(npath);
	return ret;
}

Npfcall* npfs_renameat(Npfid *dfid, Npstr *oname, Npfid *newfid, Npstr *name)
{
	Fid *of, *nf;
	Npfcall *ret;
	char *opath, *npath;

	ret = NULL;
	of = dfid->aux;
	nf = newfid->aux;

	opath = malloc(oname->len + 1);
	memmove(opath, oname->str, oname->len);
	opath[oname->len] = '\0';

	npath = malloc(name->len + 1);
	memmove(npath, name->str, name->len);
	npath[name->len] = '\0';

	if (renameat(of->fd, opath, nf->fd, npath) < 0) {
		create_rerror(errno);
		goto out;
	}

out:
	free(npath);
	return ret;
}

Npfcall* npfs_unlinkat(Npfid *dfid, Npstr *name)
{
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;
	struct stat st;

	ret = NULL;
	f = dfid->aux;
	npath = malloc(name->len + 1);
	memmove(npath, name->str, name->len);
	npath[name->len] = '\0';

	if (f->fd < 0) {
		f->fd = open(f->path, O_RDONLY);
		if (f->fd < 0) {
			create_rerror(errno);
			goto out;
		}

//		dfid->omode = Oread;
	}

	if (unlinkat(f->fd, npath, 0) < 0) {
		create_rerror(errno);
		goto out;
	}

	ustat2qid(&st, &qid);
	ret = np_create_runlinkat(&qid);

out:
	free(npath);
	return ret;
}

int
npfs_aio_init(int n)
{
	int ret = 0;

#ifdef NPFS_USE_AIO
	ret = io_queue_init(n, &aio_ctx);
#endif

	return ret;
}


/*static*/ int
npfs_aio_read(Npfid *fid, Npfcall *rread, u64 offset, u32 count, Npreq *req)
{
	int ret = ENOSYS;

#ifdef NPFS_USE_AIO
	Fid *f;
	Aioreq *areq;
	struct iocb *iocbs[1];

	f = fid->aux;
	areq = malloc(sizeof(*areq));
	areq->req = req;
	areq->fid = fid;
	areq->rread = rread;
	pthread_mutex_lock(&aio_lock);
	areq->next = aio_reqs;
	areq->prev = NULL;
	aio_reqs = areq;
	pthread_mutex_unlock(&aio_lock);
	io_prep_pread(&areq->iocb, f->fd, rread->data, count, offset);
	iocbs[0] = &areq->iocb;
	ret = io_submit(aio_ctx, 1, iocbs);
#endif

	return ret;
}

/*static*/ int
npfs_aio_write(Npfid *fid, u8 *data, u64 offset, u32 count, Npreq *req)
{
	int ret = ENOSYS;

#ifdef NPFS_USE_AIO
	Fid *f;
	Aioreq *areq;
	struct iocb *iocbs[1];

	f = fid->aux;
	areq = malloc(sizeof(*areq));
	areq->req = req;
	areq->fid = fid;
	pthread_mutex_lock(&aio_lock);
	areq->next = aio_reqs;
	areq->prev = NULL;
	aio_reqs = areq;
	pthread_mutex_unlock(&aio_lock);
	io_prep_pwrite(&areq->iocb, f->fd, data, count, offset);
	iocbs[0] = &areq->iocb;
	ret = io_submit(aio_ctx, 1, iocbs);
#endif

	return ret;
}

void*
npfs_aio_proc(void *a)
{
#ifdef NPFS_USE_AIO
	int i, n, count;
	Aioreq *areq;
	struct timespec ts;
	struct io_event events[8];

	ts.tv_sec = 100;
	ts.tv_nsec = 0;

	for(;;) {
		n = io_getevents(aio_ctx, 1, 1 /* sizeof(events) / sizeof(events[0]) */,
			events, &ts);

//		fprintf(stderr,"++ %d\n", n);
		for(i = 0; i < n; i++) {
			count = events[i].res;
			areq = (Aioreq *) ((char *) events[i].obj - 
				(int) (&((Aioreq *)0)->iocb));

			npfs_aio_respond(areq, &events[i], 0);
		}
	}
#endif

	return NULL;
}
