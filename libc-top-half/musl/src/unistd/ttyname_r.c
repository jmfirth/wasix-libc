#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <wasi/libc.h>
#ifdef __wasilibc_unmodified_upstream /* WASI has no ttyname */
#include "syscall.h"
#else
#include <wasi/api.h>
#include <string.h>
#endif

int ttyname_r(int fd, char *name, size_t size)
{
#ifdef __wasilibc_unmodified_upstream /* WASI has no ttyname */
	struct stat st1, st2;
	char procname[sizeof "/proc/self/fd/" + 3*sizeof(int) + 2];
	ssize_t l;

	if (!isatty(fd)) return errno;

	__procfdname(procname, fd);
	l = readlink(procname, name, size);

	if (l < 0) return errno;
	else if (l == size) return ERANGE;

	name[l] = 0;

	if (stat(name, &st1) || fstat(fd, &st2))
		return errno;
	if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino)
		return ENODEV;

	return 0;
#else
	const char *path = fd == 0 ? "/dev/stdin" : fd == 1 ? "/dev/stdout" : fd == 2 ? "/dev/stderr" : "/dev/tty";
	__wasi_tty_t tty;
	int r = __wasilibc_tty_get_for_fd(fd, &tty);
	if (r != 0) {
		return __wasilibc_errno_from_wasi(r);
	}
	if (size <= strlen(path)) return ERANGE;
	strcpy(name, path);
	return 0;
#endif
}
