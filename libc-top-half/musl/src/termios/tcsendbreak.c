#include <termios.h>
#ifndef __wasilibc_unmodified_upstream
#include <sys/ioctl.h>
#include <stdint.h>
#endif
#ifdef __wasilibc_unmodified_upstream
#include <sys/ioctl.h>
#endif

int tcsendbreak(int fd, int dur)
{
#ifdef __wasilibc_unmodified_upstream
	/* nonzero duration is implementation-defined, so ignore it */
	return ioctl(fd, TCSBRK, 0);
#else
	/* firebox#7P3: use tcgetattr's descriptor-specific tty test first.
	 * The general WASIX ioctl path performs a supported host operation or
	 * returns its error (ENOTTY for an unsupported device request). No
	 * output-queue guarantee exists that would justify a successful no-op. */
	struct termios tio;
	if (tcgetattr(fd, &tio) < 0) return -1;
	return ioctl(fd, TCSBRK, (void *)(uintptr_t)0);
#endif
}
