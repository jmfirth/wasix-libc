#include <termios.h>
#ifndef __wasilibc_unmodified_upstream
#include <sys/ioctl.h>
#include <stdint.h>
#endif
#include <sys/ioctl.h>
#ifdef __wasilibc_unmodified_upstream	
#include "syscall.h"
#endif

int tcdrain(int fd)
{
#ifdef __wasilibc_unmodified_upstream	
	return syscall_cp(SYS_ioctl, fd, TCSBRK, 1);
#else
	/* firebox#7P3: use tcgetattr's descriptor-specific tty test first.
	 * The general WASIX ioctl path performs a supported host operation or
	 * returns its error (ENOTTY for an unsupported device request). No
	 * output-queue guarantee exists that would justify a successful no-op. */
	struct termios tio;
	if (tcgetattr(fd, &tio) < 0) return -1;
	return ioctl(fd, TCSBRK, (void *)(uintptr_t)1);
#endif
}
