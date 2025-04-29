#include <sys/sysmacros.h>
#include <sys/vfs.h>

#define STAT_MTIME(st) ((st)->st_mtim)
#define STAT_ATIME(st) ((st)->st_atim)
#define STAT_CTIME(st) ((st)->st_ctim)
#define SETXATTR(path, name, val, valsz, flags) lsetxattr(path, name, val, valsz, flags)
#define LISTXATTR(path, buf, bufsz) llistxattr(path, buf, bufsz)
#define GETXATTR(path, name, val, valsz) lgetxattr(path, name, val, valsz, 0)
#define DIRENT_OFF(d) ((d)->d_off)
#define STATFS_NAMELEN(sf) ((sf)->f_namelen
