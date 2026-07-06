#include <signal.h>

int sigpause(int sig)
{
	sigset_t mask;
	sigprocmask(0, 0, &mask);
	/* firebox#7B5: XSI sigpause must reject an invalid signal, not block forever.
	 * Stock musl ignores sigdelset's return, so sigpause(-1) leaves the mask
	 * unchanged and sigsuspend blocks indefinitely (conformance sigpause/4-1
	 * hangs -> harness timeout). glibc's XSI __sigpause returns -1/EINVAL here;
	 * sigdelset already sets errno=EINVAL on failure, so propagate it. */
	if (sigdelset(&mask, sig) < 0) return -1;
	return sigsuspend(&mask);
}
