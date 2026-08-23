#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

static unsigned short map[] = {
	[EAGAIN]          = 11,
	[EINPROGRESS]     = 115,
	[EALREADY]        = 114,
	[ENOTSOCK]        = 88,
	[EDESTADDRREQ]    = 89,
	[EMSGSIZE]        = 90,
	[EPROTOTYPE]      = 91,
	[ENOPROTOOPT]     = 92,
	[EPROTONOSUPPORT] = 93,
	[ESOCKTNOSUPPORT] = 94,
	[ENOTSUP]         = 95,
	[EPFNOSUPPORT]    = 96,
	[EAFNOSUPPORT]    = 97,
	[EADDRINUSE]      = 98,
	[EADDRNOTAVAIL]   = 99,
	[ENETDOWN]        = 100,
	[ENETUNREACH]     = 101,
	[ENETRESET]       = 102,
	[ECONNABORTED]    = 103,
	[ECONNRESET]      = 104,
	[ENOBUFS]         = 105,
	[EISCONN]         = 106,
	[ENOTCONN]        = 107,
	[ESHUTDOWN]       = 108,
	[ETOOMANYREFS]    = 109,
	[ETIMEDOUT]       = 110,
	[ECONNREFUSED]    = 111,
	[ELOOP]           = 40,
	[ENAMETOOLONG]    = 36,
	[EHOSTDOWN]       = 112,
	[EHOSTUNREACH]    = 113,
	[ENOTEMPTY]       = 39,
	[EUSERS]          = 87,
	[EDQUOT]          = 122,
	[ESTALE]          = 116,
	[EREMOTE]         = 66,
	[ENOLCK]          = 37,
	[ENOSYS]          = 38,
	[EOVERFLOW]       = 75,
	[ECANCELED]       = 125,
	[EIDRM]           = 43,
	[ENOMSG]          = 42,
	[EILSEQ]          = 84,
	[EBADMSG]         = 74,
	[EMULTIHOP]       = 72,
	[ENODATA]         = 61,
	[ENOLINK]         = 67,
	[ENOSR]           = 63,
	[ENOSTR]          = 60,
	[EPROTO]          = 71,
	[ETIME]           = 62,
	[EOPNOTSUPP]      = 95,
	[ENOTRECOVERABLE] = 131,
	[EOWNERDEAD]      = 130,
	[ENOATTR]         = 61,
	[EPROCLIM]        = 11,   /* fork limit: Linux says EAGAIN */
	[EBADRPC]         = 5,    /* RPC failures have no Linux */
	[ERPCMISMATCH]    = 5,    /* equivalent; they are I/O */
	[EPROGUNAVAIL]    = 5,    /* errors as far as a caller */
	[EPROGMISMATCH]   = 5,    /* can tell */
	[EPROCUNAVAIL]    = 5,
	[EFTYPE]          = 22,   /* inappropriate type -> EINVAL */
	[EAUTH]           = 13,   /* authentication -> EACCES */
	[ENEEDAUTH]       = 13,
	[EPWROFF]         = 5,    /* device is off -> EIO */
	[EDEVERR]         = 5,
	[EBADEXEC]        = 8,    /* the four Mach-O load failures */
	[EBADARCH]        = 8,    /* are all ENOEXEC on Linux */
	[ESHLIBVERS]      = 8,
	[EBADMACHO]       = 8,
	[ENOPOLICY]       = 22,   /* no such policy -> EINVAL */
	[EQFULL]          = 105,  /* output queue full -> ENOBUFS */
#ifdef ENOTCAPABLE
	[ENOTCAPABLE]     = 1,    /* capabilities insufficient -> EPERM */
#endif
};


int np_mount(char *mntpt, int mntflags, char *opts)
{
	return -1;
}

int
sreuid(int a, int b)
{
	return setreuid(a, b);
}

int
sregid(int a, int b)
{
	return setregid(a, b);
}

u32
np_errno_to_linux(u32 ecode)
{
	if (ecode <= ERANGE)
		return ecode==EDEADLK? 35: ecode;	/* Linux's EDEADLK */

	if (ecode < sizeof(map)/sizeof(map[0]) && map[ecode])
		return map[ecode];

	return EIO;	/* 5 on both */
}
