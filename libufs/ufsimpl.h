/*
 * Copyright (C) 2025 by Latchesar Ionkov <lucho@ionkov.net>
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

typedef struct Fid Fid;

struct Fid {
	char*		path;
	int		omode;
	int		fd;
	DIR*		dir;
//	int		diroffset;
	struct stat	stat;

	char*		xattrname;	// xattrcreate
	u32		xattrflags;	// xattrcreate
	u64		xattrsz;
	u8*		xattrdata;
};

extern Npsrv *srv;
extern int debuglevel;
extern int sameuser;

int fidstat(Fid *fid);
Fid* npfs_fidalloc();
int omode2uflags(u8 mode);


Npfcall* npfs_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
int npfs_clone(Npfid *fid, Npfid *newfid);
int npfs_walk(Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall* npfs_open(Npfid *fid, u8 mode);
Npfcall* npfs_create(Npfid *fid, Npstr *name, u32 perm, u8 mode, 
	Npstr *extension);
Npfcall* npfs_read(Npfid *fid, u64 offset, u32 count, Npreq *);
Npfcall* npfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *);
Npfcall* npfs_clunk(Npfid *fid);
Npfcall* npfs_remove(Npfid *fid);
Npfcall* npfs_stat(Npfid *fid);
Npfcall* npfs_wstat(Npfid *fid, Npstat *stat);

Npfcall* npfs_statfs(Npfid *fid);
Npfcall* npfs_lopen(Npfid *fid, u32 flags);
Npfcall* npfs_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 perm, u32 gid);
Npfcall* npfs_symlink(Npfid *dfid, Npstr *name, Npstr *symtgt, u32 gid);
Npfcall* npfs_mknod(Npfid *dfid, Npstr *name, u32 perm, u32 major, u32 minor, u32 gid);
Npfcall* npfs_rename(Npfid *fid, Npfid *dfid, Npstr *name);
Npfcall* npfs_readlink(Npfid *fid);
Npfcall* npfs_getattr(Npfid *fid, u64 mask);
Npfcall* npfs_setattr(Npfid *fid, Npattrs *attrs);
Npfcall* npfs_xattrwalk(Npfid *fid, Npfid *newfid, Npstr *name);
Npfcall* npfs_xattrcreate(Npfid *fid, Npfid *newfid, Npstr *name, u32 size, u32 flags);
Npfcall* npfs_readdir(Npfid *dfid, u64 offset, u32 count, Npreq *req);
Npfcall* npfs_fsync(Npfid *fid);
Npfcall* npfs_flock(Npfid *fid, u8 type, u32 flags, u64 offset, u64 length, u32 procid, Npstr *clientid);
Npfcall* npfs_getlock(Npfid *fid, u8 type, u64 offset, u64 length, u32 procid, Npstr *clientid);
Npfcall* npfs_link(Npfid *dfid, Npfid *fid, Npstr *name);
Npfcall* npfs_mkdir(Npfid *dfid, Npstr *name, u32 perm, u32 gid);
Npfcall* npfs_renameat(Npfid *dfid, Npstr *oname, Npfid *newfid, Npstr *name);
Npfcall* npfs_unlinkat(Npfid *dfid, Npstr *name);

void npfs_flush(Npreq *req);
void npfs_fiddestroy(Npfid *fid);

// Copying this from libnpfs/np.c for now, not sure if it makes sense to add it to npfs.h
struct cbuf {
	unsigned char *sp;
	unsigned char *p;
	unsigned char *ep;
};

static inline void
buf_init(struct cbuf *buf, void *data, int datalen)
{
	buf->sp = buf->p = data;
	buf->ep = (unsigned char*)data + datalen;
}

static inline int
buf_check_overflow(struct cbuf *buf)
{
	return buf->p > buf->ep;
}

static inline int
buf_check_size(struct cbuf *buf, int len)
{
	if (buf->p+len > buf->ep) {
		if (buf->p < buf->ep)
			buf->p = buf->ep + 1;

		return 0;
	}

	return 1;
}

static inline void *
buf_alloc(struct cbuf *buf, int len)
{
	void *ret = NULL;

	if (buf_check_size(buf, len)) {
		ret = buf->p;
		buf->p += len;
	}

	return ret;
}

static inline void
buf_put_int8(struct cbuf *buf, u8 val)
{
	if (buf_check_size(buf, 1)) {
		buf->p[0] = val;
		buf->p++;
	}
}

static inline void
buf_put_int16(struct cbuf *buf, u16 val)
{
	if (buf_check_size(buf, 2)) {
		buf->p[0] = val;
		buf->p[1] = val >> 8;
		buf->p += 2;

	}
}

static inline void
buf_put_int32(struct cbuf *buf, u32 val)
{
	if (buf_check_size(buf, 4)) {
		buf->p[0] = val;
		buf->p[1] = val >> 8;
		buf->p[2] = val >> 16;
		buf->p[3] = val >> 24;
		buf->p += 4;
	}
}

static inline void
buf_put_int64(struct cbuf *buf, u64 val)
{
	if (buf_check_size(buf, 8)) {
		buf->p[0] = val;
		buf->p[1] = val >> 8;
		buf->p[2] = val >> 16;
		buf->p[3] = val >> 24;
		buf->p[4] = val >> 32;
		buf->p[5] = val >> 40;
		buf->p[6] = val >> 48;
		buf->p[7] = val >> 56;
		buf->p += 8;
	}
}

static inline void
buf_put_str(struct cbuf *buf, char *s)
{
	int slen = 0;
	char *str;

	if (s)
		slen = strlen(s);

	if (buf_check_size(buf, 2+slen)) {
		buf_put_int16(buf, slen);
		str = buf_alloc(buf, slen);
		memmove(str, s, slen);
	}
}

static inline u8
buf_get_int8(struct cbuf *buf)
{
	u8 ret = 0;

	if (buf_check_size(buf, 1)) {
		ret = buf->p[0];
		buf->p++;
	}

	return ret;
}

static inline u16
buf_get_int16(struct cbuf *buf)
{
	u16 ret = 0;

	if (buf_check_size(buf, 2)) {
		ret = buf->p[0] | (buf->p[1] << 8);
		buf->p += 2;
	}

	return ret;
}

static inline u32
buf_get_int32(struct cbuf *buf)
{
	u32 ret = 0;

	if (buf_check_size(buf, 4)) {
		ret = buf->p[0] | (buf->p[1] << 8) | (buf->p[2] << 16) | 
			(buf->p[3] << 24);
		buf->p += 4;
	}

	return ret;
}

static inline u64
buf_get_int64(struct cbuf *buf)
{
	u64 ret = 0;

	if (buf_check_size(buf, 8)) {
		ret = (u64) buf->p[0] | 
			((u64) buf->p[1] << 8) |
			((u64) buf->p[2] << 16) | 
			((u64) buf->p[3] << 24) |
			((u64) buf->p[4] << 32) | 
			((u64) buf->p[5] << 40) |
			((u64) buf->p[6] << 48) | 
			((u64) buf->p[7] << 56);
		buf->p += 8;
	}

	return ret;
}

static inline void
buf_get_str(struct cbuf *buf, Npstr *str)
{
	str->len = buf_get_int16(buf);
	str->str = buf_alloc(buf, str->len);
}
