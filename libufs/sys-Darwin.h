#include <sys/param.h>
#include <sys/mount.h>

#define STAT_MTIME(st) ((st)->st_mtimespec)
#define STAT_ATIME(st) ((st)->st_atimespec)
#define STAT_CTIME(st) ((st)->st_ctimespec)
#define SETXATTR(path, name, val, valsz, flags) setxattr(path, name, val, valsz, 0, flags)
#define LISTXATTR(path, buf, bufsz) listxattr(path, buf, bufsz, 0)
#define GETXATTR(path, name, val, valsz) getxattr(path, name, val, valsz, 0, 0)
#define DIRENT_OFF(d) ((d)->d_seekoff)
#define STATFS_NAMELEN(sf) 0
