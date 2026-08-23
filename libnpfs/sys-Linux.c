#include <stdio.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include "npfs.h"
#include "npfsimpl.h"

int np_mount(char *mntpt, int mntflags, char *opts)
{
	return mount("none", mntpt, "9p", mntflags, opts);
}

int
sreuid(int a, int b)
{
	return syscall(SYS_setreuid, a, b);
}

int
sregid(int a, int b)
{
	return syscall(SYS_setregid, a, b);
}

/* Rlerror.ecode wants a Linux errno and this host's errno already is one. */
u32
np_errno_to_linux(u32 ecode)
{
	return ecode;
}
