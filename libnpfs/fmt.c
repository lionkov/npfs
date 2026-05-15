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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

static int
np_printperm(FILE *f, int perm)
{
	int n;
	char b[10];

	n = 0;
	if (perm & Dmdir)
		b[n++] = 'd';
	if (perm & Dmappend)
		b[n++] = 'a';
	if (perm & Dmauth)
		b[n++] = 'A';
	if (perm & Dmexcl)
		b[n++] = 'l';
	if (perm & Dmtmp)
		b[n++] = 't';
	if (perm & Dmdevice)
		b[n++] = 'D';
	if (perm & Dmsocket)
		b[n++] = 'S';
	if (perm & Dmnamedpipe)
		b[n++] = 'P';
        if (perm & Dmsymlink)
                b[n++] = 'L';
        b[n] = '\0';

        return fprintf(f, "%s%03o", b, perm&0777);
}             

static int
np_printqid(FILE *f, Npqid *q)
{
	int n;
	char buf[10];

	n = 0;
	if (q->type & Qtdir)
		buf[n++] = 'd';
	if (q->type & Qtappend)
		buf[n++] = 'a';
	if (q->type & Qtauth)
		buf[n++] = 'A';
	if (q->type & Qtexcl)
		buf[n++] = 'l';
	if (q->type & Qttmp)
		buf[n++] = 't';
	if (q->type & Qtsymlink)
		buf[n++] = 'L';
	buf[n] = '\0';

#ifdef _WIN32
	return fprintf(f, " (%.16I64x %x '%s')", (unsigned long long)q->path, q->version, buf);
#else
	return fprintf(f, " (%.16llx %x '%s')", (unsigned long long)q->path, q->version, buf);
#endif
}

int
np_printstat(FILE *f, Npstat *st, int dotu)
{
	int n;

	n = fprintf(f, "'%.*s' '%.*s' '%.*s' '%.*s' q ", 
		st->name.len, st->name.str, st->uid.len, st->uid.str,
		st->gid.len, st->gid.str, st->muid.len, st->muid.str);

	n += np_printqid(f, &st->qid);
	n += fprintf(f, " m ");
	n += np_printperm(f, st->mode);
#ifdef _WIN32
	n += fprintf(f, " at %d mt %d l %I64u t %d d %d",
		st->atime, st->mtime, (unsigned long long)st->length, st->type, st->dev);
#else
	n += fprintf(f, " at %d mt %d l %llu t %d d %d",
		st->atime, st->mtime, (unsigned long long)st->length, st->type, st->dev);
#endif
	if (dotu)
		n += fprintf(f, " ext '%.*s'", st->extension.len, 
			st->extension.str);

	return n;
}

int
np_dump(FILE *f, u8 *data, int datalen)
{
	int i, n;

	i = n = 0;
	while (i < datalen) {
		n += fprintf(f, "%02x", data[i]);
		if (i%4 == 3)
			n += fprintf(f, " ");
		if (i%32 == 31)
			n += fprintf(f, "\n");

		i++;
	}
	n += fprintf(f, "\n");

	return n;
}

static int
np_printdata(FILE *f, u8 *buf, int buflen)
{
	return np_dump(f, buf, buflen<64?buflen:64);
}

int
np_dumpdata(u8 *buf, int buflen)
{
	return np_dump(stderr, buf, buflen);
}

int
np_printfcall(FILE *f, Npfcall *fc, int dotu, int dotl)
{
	int i, ret, type, fid, tag;
	Npstatfs *sf;
	Npattrs *attrs;

	if (!fc)
		return fprintf(f, "NULL");

	type = fc->type;
	fid = fc->fid;
	tag = fc->tag;

	ret = 0;
	switch (type) {
	case Tversion:
		ret += fprintf(f, "Tversion tag %u msize %u version '%.*s'", 
			tag, fc->msize, fc->version.len, fc->version.str);
		break;

	case Rversion:
		ret += fprintf(f, "Rversion tag %u msize %u version '%.*s'", 
			tag, fc->msize, fc->version.len, fc->version.str);
		break;

	case Tauth:
		ret += fprintf(f, "Tauth tag %u afid %d uname '%.*s' aname '%.*s'",
			tag, fc->afid, fc->uname.len, fc->uname.str, 
			fc->aname.len, fc->aname.str);
		break;

	case Rauth:
		ret += fprintf(f, "Rauth tag %u qid ", tag); 
		np_printqid(f, &fc->qid);
		break;

	case Tattach:
		ret += fprintf(f, "Tattach tag %u fid %d afid %d uname '%.*s' aname '%.*s'",
			tag, fid, fc->afid, fc->uname.len, fc->uname.str, 
			fc->aname.len, fc->aname.str);

		if (dotu || dotl)
			ret += fprintf(f, " uid %d", fc->n_uname);

		break;

	case Rattach:
		ret += fprintf(f, "Rattach tag %u qid ", tag); 
		np_printqid(f, &fc->qid);
		break;

	case Rerror:
		ret += fprintf(f, "Rerror tag %u ename '%.*s'", tag, 
			fc->ename.len, fc->ename.str);
		if (dotu)
			ret += fprintf(f, " ecode %d", fc->ecode);
		break;

	case Tflush:
		ret += fprintf(f, "Tflush tag %u oldtag %u", tag, fc->oldtag);
		break;

	case Rflush:
		ret += fprintf(f, "Rflush tag %u", tag);
		break;

	case Twalk:
		ret += fprintf(f, "Twalk tag %u fid %d newfid %d nwname %d", 
			tag, fid, fc->newfid, fc->nwname);
		for(i = 0; i < fc->nwname; i++)
			ret += fprintf(f, " '%.*s'", fc->wnames[i].len, 
				fc->wnames[i].str);
		break;
		
	case Rwalk:
		ret += fprintf(f, "Rwalk tag %u nwqid %d", tag, fc->nwqid);
		for(i = 0; i < fc->nwqid; i++)
			ret += np_printqid(f, &fc->wqids[i]);
		break;
		
	case Topen:
		ret += fprintf(f, "Topen tag %u fid %d mode %d", tag, fid, 
			fc->mode);
		break;
		
	case Ropen:
		ret += fprintf(f, "Ropen tag %u", tag);
		ret += np_printqid(f, &fc->qid);
		ret += fprintf(f, " iounit %d", fc->iounit);
		break;
		
	case Tcreate:
		ret += fprintf(f, "Tcreate tag %u fid %d name '%.*s' perm ",
			tag, fid, fc->name.len, fc->name.str);
		ret += np_printperm(f, fc->perm);
		ret += fprintf(f, " mode %d", fc->mode);
		if (dotu)
			ret += fprintf(f, " ext '%.*s'", fc->extension.len,
				fc->extension.str);
		break;
		
	case Rcreate:
		ret += fprintf(f, "Rcreate tag %u", tag);
		ret += np_printqid(f, &fc->qid);
		ret += fprintf(f, " iounit %d", fc->iounit);
		break;
		
	case Tread:
#ifdef _WIN32
		ret += fprintf(f, "Tread tag %u fid %d offset %I64u count %u", 
			tag, fid, (unsigned long long)fc->offset, fc->count);
#else
		ret += fprintf(f, "Tread tag %u fid %d offset %llu count %u", 
			tag, fid, (unsigned long long)fc->offset, fc->count);
#endif
		break;
		
	case Rread:
		ret += fprintf(f, "Rread tag %u count %u data ", tag, fc->count);
		ret += np_printdata(f, fc->data, fc->count);
		break;
		
	case Twrite:
#ifdef _WIN32
		ret += fprintf(f, "Twrite tag %u fid %d offset %I64u count %u data ",
			tag, fid, (unsigned long long)fc->offset, fc->count);
#else
		ret += fprintf(f, "Twrite tag %u fid %d offset %llu count %u data ",
			tag, fid, (unsigned long long)fc->offset, fc->count);
#endif
		ret += np_printdata(f, fc->data, fc->count);
		break;
		
	case Rwrite:
		ret += fprintf(f, "Rwrite tag %u count %u", tag, fc->count);
		break;
		
	case Tclunk:
		ret += fprintf(f, "Tclunk tag %u fid %d", tag, fid);
		break;
		
	case Rclunk:
		ret += fprintf(f, "Rclunk tag %u", tag);
		break;
		
	case Tremove:
		ret += fprintf(f, "Tremove tag %u fid %d", tag, fid);
		break;
		
	case Rremove:
		ret += fprintf(f, "Rremove tag %u", tag);
		break;
		
	case Tstat:
		ret += fprintf(f, "Tstat tag %u fid %d", tag, fid);
		break;
		
	case Rstat:
		ret += fprintf(f, "Rstat tag %u ", tag);
		ret += np_printstat(f, &fc->stat, dotu);
		break;
		
	case Twstat:
		ret += fprintf(f, "Twstat tag %u fid %d ", tag, fid);
		ret += np_printstat(f, &fc->stat, dotu);
		break;
		
	case Rwstat:
		ret += fprintf(f, "Rwstat tag %u", tag);
		break;

	case Rlerror:
		ret += fprintf(f, "Rlerror tag %u ecode %d", tag, fc->ecode);
		break;

	case Tstatfs:
		ret += fprintf(f, "Tstatfs tag %u fid %d", tag, fid);
		break;

	case Rstatfs:
		sf = &fc->stfs;
		ret += fprintf(f, "Rstatfs tag %u type %d bsize %d blocks %llu bfree %llu bavail %llu files %llu ffree %llu fsid %llu namelen %d",
			tag, sf->type, sf->bsize, sf->blocks, sf->bfree,
			sf->bavail, sf->files, sf->ffree, sf->fsid, sf->namelen);
		break;

	case Tlopen:
		ret += fprintf(f, "Tlopen tag %u fid %d flags %d", tag, fid, fc->flags);
		break;

	case Rlopen:
		ret += fprintf(f, "Rlopen tag %u", tag);
		ret += np_printqid(f, &fc->qid);
		ret += fprintf(f, " iounit %d", fc->iounit);
		break;

	case Tlcreate:
		ret += fprintf(f, "Tlcreate tag %u fid %d name '%.*s' flags %d perm ",
			tag, fid, fc->name.len, fc->name.str, fc->flags);
		ret += np_printperm(f, fc->perm);
		ret += fprintf(f, " gid %d", fc->gid);
		break;

	case Rlcreate:
		ret += fprintf(f, "Rlcreate tag %u ", tag);
		ret += np_printqid(f, &fc->qid);
		ret += fprintf(f, " iounit %d", fc->iounit);
		break;

	case Tsymlink:
		ret += fprintf(f, "Tsymlink tag %u dfid %d name '%.*s' target '%.*s' gid %d",
			tag, fc->dfid, fc->name.len, fc->name.str, fc->symtgt.len,
			fc->symtgt.str, fc->gid);
		break;

	case Rsymlink:
		ret += fprintf(f, "Rsymlink tag %u ", tag);
		ret += np_printqid(f, &fc->qid);
		break;

	case Tmknod:
		ret += fprintf(f, "Tmknod tag %u fid %d name '%.*s' perm ", tag,
			fc->fid, fc->name.len, fc->name.str);
		ret += np_printperm(f, fc->perm);
		ret += fprintf(f, " major %d minor %d gid %d", fc->major,
			fc->minor, fc->gid);
		break;

	case Rmknod:
		ret += fprintf(f, "Rmknod tag %u ", tag);
		ret += np_printqid(f, &fc->qid);
		break;

	case Trename:
		ret += fprintf(f, "Trename tag %u fid %d dfid %d name '%.*s'",
			tag, fc->fid, fc->dfid, fc->name.len, fc->name.str);
		break;

	case Rrename:
		ret += fprintf(f, "Rrename tag %u", tag);
		break;

	case Treadlink:
		ret += fprintf(f, "Treadlink tag %u fid %d", tag, fc->fid);
		break;

	case Rreadlink:
		ret += fprintf(f, "Rreadlink tag %u target '%.*s'", tag,
			fc->symtgt.len, fc->symtgt.str);
		break;

	case Tgetattr:
		ret += fprintf(f, "Tgetattr tag %u fid %d mask %llu", tag, fc->fid, fc->mask);
		break;

	case Rgetattr:
		attrs = &fc->attrs;
		ret += fprintf(f, "Rgetattr tag %u mask %llu qid ", tag, fc->mask);
		ret += np_printqid(f, &fc->qid);
		ret += fprintf(f, "mode %d uid %d gid %d nlink %llu rdev %llu ",
			attrs->mode, attrs->uid, attrs->gid, attrs->nlink, attrs->rdev);
		ret += fprintf(f, "size %llu blksize %llu blocks %llu ",
			attrs->size, attrs->blksize, attrs->blocks);
		ret += fprintf(f, "atime_sec %llu atime_nsec %llu ", attrs->atime_sec, attrs->atime_nsec);
		ret += fprintf(f, "mtime_sec %llu mtime_nsec %llu ", attrs->mtime_sec, attrs->mtime_nsec);
		ret += fprintf(f, "ctime_sec %llu ctime_nsec %llu ", attrs->ctime_sec, attrs->ctime_nsec);
		ret += fprintf(f, "btime_sec %llu btime_nsec %llu ", attrs->btime_sec, attrs->btime_nsec);
		ret += fprintf(f, "gen %llu data_version %llu", attrs->gen, attrs->data_version);
		break;

	case Tsetattr:
		attrs = &fc->attrs;
		ret += fprintf(f, "Tsetattr tag %u fid %d mask %llu", tag, fc->fid, fc->mask);
		ret += fprintf(f, "mode %d uid %d gid %d ", attrs->mode, attrs->uid, attrs->gid);
		ret += fprintf(f, "atime_sec %llu atime_nsec %llu ", attrs->atime_sec, attrs->atime_nsec);
		ret += fprintf(f, "mtime_sec %llu mtime_nsec %llu", attrs->mtime_sec, attrs->mtime_nsec);
		break;

	case Rsetattr:
		ret += fprintf(f, "Rsetattr tag %u", tag);
		break;

	case Txattrwalk:
		ret += fprintf(f, "Txattrwalk tag %u fid %d newfid %d name '%.*s'",
			tag, fc->fid, fc->newfid, fc->name.len, fc->name.str);
		break;

	case Rxattrwalk:
		ret += fprintf(f, "Rxattrwalk tag %u size %d", tag, fc->size);
		break;

	case Txattrcreate:
		ret += fprintf(f, "Txattrcreate tag %u fid %d name '%.*s' size %d flags %d",
			tag, fc->fid, fc->name.len, fc->name.str, fc->size, fc->flags);
		break;

	case Rxattrcreate:
		ret += fprintf(f, "Rxattrcreate tag %u", tag);
		break;

	case Treaddir:
		ret += fprintf(f, "Treaddir tag %u fid %d offset %llu count %d", tag, fc->fid, fc->offset, fc->count);
		break;

	case Rreaddir:
		ret += fprintf(f, "Rreaddir tag %u count %u data ", tag, fc->count);
		ret += np_printdata(f, fc->data, fc->count);
		break;

	case Tfsync:
		ret += fprintf(f, "Tfsync tag %u fid %d", tag, fc->fid);
		break;

	case Rfsync:
		ret += fprintf(f, "Rfsync tag %u", tag);
		break;

	case Tlock:
		ret += fprintf(f, "Tlock tag %u fid %d type %d flags %d offset %llu length %llu procid %d clientid '%.*s'",
			tag, fc->fid, fc->locktype, fc->flags, fc->offset,
			fc->locklength, fc->procid, fc->clientid.len, fc->clientid.str);
		break;

	case Rlock:
		ret += fprintf(f, "Rlock tag %u status %d", tag, fc->lockstatus);
		break;

	case Tgetlock:
		ret += fprintf(f, "Tgetlock tag %u fid %d type %d offset %llu length %llu procid %d clientid '%.*s'",
			tag, fc->fid, fc->locktype, fc->offset, fc->locklength,
			fc->procid, fc->clientid.len, fc->clientid.str);
		break;

	case Rgetlock:
		ret += fprintf(f, "Rgetlock tag %u fid %d type %d offset %llu length %llu procid %d clientid '%.*s'",
			tag, fc->fid, fc->locktype, fc->offset, fc->locklength,
			fc->procid, fc->clientid.len, fc->clientid.str);
		break;

	case Tlink:
		ret += fprintf(f, "Tlink tag %u fid %d dfid %d name '%.*s'",
			tag, fc->fid, fc->dfid, fc->name.len, fc->name.str);
		break;

	case Rlink:
		ret += fprintf(f, "Rlink tag %u", tag);
		break;

	case Tmkdir:
		ret += fprintf(f, "Tlink tag %u fid %d name '%.*s' perm ",
			tag, fc->fid, fc->name.len, fc->name.str);

		ret += np_printperm(f, fc->perm);
		ret += fprintf(f, " gid %d", fc->gid);
		break;

	case Rmkdir:
		ret += fprintf(f, "Rmkdir tag %u", tag);
		ret += np_printqid(f, &fc->qid);
		break;

	case Trenameat:
		ret += fprintf(f, "Trenameat tag %u olddfid %d oldname '%.*s' newdfid %d newname '%.*s'",
			tag, fc->fid, fc->oname.len, fc->oname.str, 
			fc->newfid, fc->name.len, fc->name.str);
		break;

	case Rrenameat:
		ret += fprintf(f, "Rrenameat tag %u", tag);
		break;

	case Tunlinkat:
		ret += fprintf(f, "Tunlinkat tag %u fid %d name '%.*s' flags %d",
			tag, fc->fid, fc->name.len, fc->name.str, fc->flags);
		break;

	case Runlinkat:
		ret += fprintf(f, "Runlinkat tag %u", tag);
		break;

	default:
		ret += fprintf(f, "unknown type %d", type);
		break;
	}

	return ret;
}
