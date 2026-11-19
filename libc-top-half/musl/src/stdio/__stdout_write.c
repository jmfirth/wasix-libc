#include "stdio_impl.h"
#include <sys/ioctl.h>
#ifdef __wasilibc_unmodified_upstream // use isatty rather than manual ioctl
#else
#include <errno.h>
#include <__function___isatty.h>
#endif

size_t __stdout_write(FILE *f, const unsigned char *buf, size_t len)
{
#ifdef __wasilibc_unmodified_upstream // use isatty rather than manual ioctl
	struct winsize wsz;
#endif
	f->write = __stdio_write;
#ifdef __wasilibc_unmodified_upstream // use isatty rather than manual ioctl
	if (!(f->flags & F_SVB) && __syscall(SYS_ioctl, f->fd, TIOCGWINSZ, &wsz))
		f->lbf = -1;
#else
	if (!(f->flags & F_SVB)) {
		/* Like musl's raw ioctl, this buffering probe preserves errno. */
		int saved_errno = errno;
		if (!__isatty(f->fd))
			f->lbf = -1;
		errno = saved_errno;
	}
#endif
	return __stdio_write(f, buf, len);
}
