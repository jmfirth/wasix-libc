#include <sys/stat.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"

int mknodat(int fd, const char *path, mode_t mode, dev_t dev)
{
	return syscall(SYS_mknodat, fd, path, mode, dev);
}
#else
/* WASIX does not currently model special file types in its filestat
 * enum (no S_IFIFO / S_IFBLK / S_IFCHR on the runtime side), so
 * mknodat cannot round-trip through wasix-libc without a corresponding
 * runtime syscall. This libc-side stub returns ENOSYS with errno set
 * so consumers see a clean error instead of a linker-missing abort.
 *
 * Making S_IFIFO work end-to-end requires adding a WASIX-side import
 * (__wasi_path_mknodat or similar) and a runtime-level handler; that
 * is tracked as a follow-up to issue #24.
 *
 * References:
 *   - wasi/api_wasix.h — __wasi_filetype_t has no FIFO variant
 *   - docs/runtime-gotchas.md — VFS filetype surface
 */
int mknodat(int fd, const char *path, mode_t mode, dev_t dev)
{
	(void)fd;
	(void)path;
	(void)mode;
	(void)dev;
	errno = ENOSYS;
	return -1;
}
#endif
