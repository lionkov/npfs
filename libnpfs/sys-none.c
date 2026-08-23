#include <stdio.h>
#include <unistd.h>
#include "npfs.h"
#include "npfsimpl.h"

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
	return ecode;
}
