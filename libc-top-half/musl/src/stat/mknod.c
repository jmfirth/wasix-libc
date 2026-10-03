#include <sys/stat.h>
#include <fcntl.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

int mknod(const char *path, mode_t mode, dev_t dev)
{
#ifdef __wasilibc_unmodified_upstream
#ifdef SYS_mknod
	return syscall(SYS_mknod, path, mode, dev);
#else
	return syscall(SYS_mknodat, AT_FDCWD, path, mode, dev);
#endif
#else
	/* firebox#DNG — mknodat resolves AT_FDCWD in the runtime (path_mknod),
	 * which also applies the umask. */
	return mknodat(AT_FDCWD, path, mode, dev);
#endif
}
