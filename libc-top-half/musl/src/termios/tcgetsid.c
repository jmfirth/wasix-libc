#include <termios.h>
#include <unistd.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/ioctl.h>
#else
#include <errno.h>
#endif

/*
 * firebox#388 (sibling of #347): WASIX has no host-side session table
 * and no TIOCGSID ioctl. Upstream stub returned EINVAL unconditionally,
 * which trips any program that queries the session id of a terminal.
 *
 * Per POSIX (man 3 tcgetsid):
 *   - tcgetsid(fd) returns the session id of the foreground process
 *     group's session for the terminal at fd
 *   - returns -1 with EBADF if fd is not a valid file descriptor
 *   - returns -1 with ENOTTY if fd is not a terminal
 *
 * In our single-process model sid == pid for the caller. We return
 * getpid() for any non-negative fd. We don't validate isatty(fd)
 * because the wasix-libc tcgetpgrp companion doesn't either, and the
 * net cost of doing so without a real session table is negative.
 */
pid_t tcgetsid(int fd)
{
#ifdef __wasilibc_unmodified_upstream
	int sid;
	if (ioctl(fd, TIOCGSID, &sid) < 0)
		return -1;
	return sid;
#else
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	return getpid();
#endif
}
