#include <sys/stat.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"

int mknodat(int fd, const char *path, mode_t mode, dev_t dev)
{
	return syscall(SYS_mknodat, fd, path, mode, dev);
}
#else
#include <string.h>
#include <wasi/api.h>
/* mknodat: route S_IFIFO to the WASIX runtime via __wasix_path_mknod
 * (firebox #28). Other node types (S_IFBLK, S_IFCHR, S_IFREG) are not
 * supported by the runtime; we surface that as ENOTSUP so callers can
 * branch instead of seeing a generic ENOSYS that suggests the syscall
 * is missing entirely.
 *
 * References:
 *   - jmfirth/wasmer firebox-patches: lib/wasix/src/syscalls/wasix/path_mknod.rs
 *   - wasi/api_wasix.h — __WASI_FILETYPE_FIFO + __wasix_path_mknod decl
 */
int mknodat(int fd, const char *path, mode_t mode, dev_t dev)
{
	if (!S_ISFIFO(mode)) {
		errno = ENOTSUP;
		return -1;
	}
	__wasi_errno_t err = __wasix_path_mknod(
		(__wasi_fd_t) fd,
		path,
		strlen(path),
		(uint32_t)(mode & 07777),
		(uint64_t) dev
	);
	if (err != 0) {
		errno = err;
		return -1;
	}
	return 0;
}
#endif
