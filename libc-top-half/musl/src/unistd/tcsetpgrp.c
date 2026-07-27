#include <unistd.h>
#include <termios.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/ioctl.h>
#endif

#ifdef __wasilibc_unmodified_upstream
#else
/*
 * firebox#Y42 -- `__wasilibc_pgrp` is now the TERMINAL FOREGROUND process
 * group and NOTHING ELSE. It moved here, to its only writer, when the pgid ABI
 * (#HS6) gave getpgid/setpgid/getpgrp/setpgrp a real host model to ask, so this
 * global stopped being the process's pgid store. Keeping the two conflated
 * would have made a tcsetpgrp() silently rewrite the caller's own pgid.
 *
 * It is still a fiction, and an honest gap rather than a fixed one: there is
 * no terminal-session model host-side (no sid on WasiProcess at all -- see
 * firebox#E4T's tcgetsid entry and #HS6's "deliberately NOT implemented"
 * section). It is per-process userspace state, so a tcsetpgrp() in one process
 * is invisible to another. `0` means "never explicitly set", which tcgetpgrp
 * reads as "the caller's own group is in the foreground" -- the state a fresh
 * controlling terminal is actually in.
 */
int __wasilibc_pgrp = 0;
#endif

int tcsetpgrp(int fd, pid_t pgrp)
{
#ifdef __wasilibc_unmodified_upstream
	int pgrp_int = pgrp;
	return ioctl(fd, TIOCSPGRP, &pgrp_int);
#else
	__wasilibc_pgrp = pgrp;
	return 0;
#endif
}
