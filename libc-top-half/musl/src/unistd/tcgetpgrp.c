#include <unistd.h>
#include <termios.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/ioctl.h>
#endif

#ifdef __wasilibc_unmodified_upstream
#else
/* firebox#Y42 -- the terminal foreground process group; 0 = never explicitly
 * set. Defined in tcsetpgrp.c, its only writer. NOT the process's pgid, which
 * lives host-side and is reached via getpgrp()/getpgid(). */
extern int __wasilibc_pgrp;
#endif

pid_t tcgetpgrp(int fd)
{
#ifdef __wasilibc_unmodified_upstream
	int pgrp;
	if (ioctl(fd, TIOCGPGRP, &pgrp) < 0)
		return -1;
	return pgrp;
#else
	/*
	 * firebox#Y42: fall back to the caller's own process group when nothing
	 * has called tcsetpgrp(). This PRESERVES an identity that held before the
	 * pgid ABI landed and that job control depends on -- tcgetpgrp(fd) ==
	 * getpgrp() -- which used to be true only because both read this same
	 * global. A shell asks exactly that question to decide whether it is in
	 * the foreground; answering 0 against a real pgid would tell every shell
	 * it had been backgrounded.
	 */
	if (__wasilibc_pgrp == 0)
		return getpgrp();
	return __wasilibc_pgrp;
#endif
}
