#include <setjmp.h>
#include <signal.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

hidden int __sigsetjmp_tail(sigjmp_buf jb, int ret)
{
#ifdef __wasilibc_unmodified_upstream
	void *p = jb->__ss;
	__syscall(SYS_rt_sigprocmask, SIG_SETMASK, ret?p:0, ret?0:p, _NSIG/8);
	return ret;
#else
	/* WASIX mask save/restore is handled inline in sigsetjmp / siglongjmp
	 * via a thread-local slot (see src/signal/sigsetjmp_wasix.c). The
	 * musl-style __sigsetjmp_tail is not used on this path; it exists
	 * only because the musl sources reference it as a hidden helper.
	 * Returning 0 here is a no-op pass-through for any stray caller. */
	(void)jb;
	return ret;
#endif
}