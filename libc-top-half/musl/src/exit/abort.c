#include <stdlib.h>
#include <signal.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif
#include "pthread_impl.h"
#include "atomic.h"
#include "lock.h"
#include "ksigaction.h"

/* firebox#R3M — declare to the host that THIS process is dying of SIGABRT's
 * default action, so the `_Exit(127)` below is read as a signal death and not
 * as a program that happened to exit 127. First-wins (see sigaction.c), so an
 * outer terminate_handler(SIGTERM) that called us keeps its own attribution and
 * is not relabelled SIGABRT.
 *
 * WHY HERE AND NOT ONLY IN core_handler. MEASURED 2026-08-08: when abort() is
 * called from inside a user signal handler, the raise(SIGABRT) below never
 * delivers — the host refuses a nested __wasm_signal dispatch (firebox#912) —
 * so core_handler is never entered and every SIGABRT-in-a-handler termination
 * would go unattributed. This is the one place every abort() path passes
 * through. */
__attribute__((__visibility__("hidden")))
void __fbx_note_terminating(int sig);

#ifndef __wasilibc_unmodified_upstream
/* Defined in signal/sigaction.c; pthread_impl.h declares it upstream-only. */
int __libc_sigaction(int, const struct sigaction *, struct sigaction *);

/* firebox#DW1 — tid of the thread whose abort() is inside its FINAL raise, or
 * 0. See the re-raise below for why a re-entry must not reach LOCK again. */
static volatile int __fbx_abort_final_tid;
#endif

_Noreturn void abort(void)
{
#ifndef __wasilibc_unmodified_upstream
	/* firebox#DW1 — re-entered from our own final raise: the default action
	 * was dispatched in-guest (core_handler -> abort) because the host could
	 * not read the disposition. We already hold __abort_lock, so taking it
	 * again would self-deadlock. We are inside the SIGABRT dispatch, so this
	 * _Exit(127) is attributed SIGABRT by the host (__fbx_terminating_sig was
	 * declared by the outermost abort()). */
	if (__fbx_abort_final_tid && __fbx_abort_final_tid == __pthread_self()->tid)
		_Exit(127);
#endif
	__fbx_note_terminating(SIGABRT);
	raise(SIGABRT);

	/* If there was a SIGABRT handler installed and it returned, or if
	 * SIGABRT was blocked or ignored, take an AS-safe lock to prevent
	 * sigaction from installing a new SIGABRT handler, uninstall any
	 * handler that may be present, and re-raise the signal to generate
	 * the default action of abnormal termination. */
	__block_all_sigs(0);
	LOCK(__abort_lock);
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigaction, SIGABRT,
		&(struct k_sigaction){.handler = SIG_DFL}, 0, _NSIG/8);
	__syscall(SYS_tkill, __pthread_self()->tid, SIGABRT);
	__syscall(SYS_rt_sigprocmask, SIG_UNBLOCK,
		&(long[_NSIG/(8*sizeof(long))]){1UL<<(SIGABRT-1)}, 0, _NSIG/8);
#else
	/* firebox#DW1 — the upstream sequence above, on the wasix primitives.
	 * This branch used to re-raise with SIGABRT still BLOCKED (by the
	 * __block_all_sigs just above) and its handler still installed, so the
	 * re-raise could only pend and the process fell through to a plain
	 * _Exit(127): a SIGABRT handler that returned, SIGABRT blocked by the
	 * caller, all reported WIFEXITED(127)
	 * where Linux — and POSIX ("abort() shall override blocking or ignoring
	 * the SIGABRT signal") — report WIFSIGNALED/SIGABRT. MEASURED 2026-09-26
	 * on main AND worker threads alike.
	 *
	 * Reset the disposition to SIG_DFL (__libc_sigaction, not sigaction: the
	 * latter takes __abort_lock, which we hold), unblock SIGABRT alone on THIS
	 * thread's mask (firebox#35F: the mask is per-thread), and re-raise. The
	 * host now reads "no handler" off __fbx_handler_set and applies SIGABRT's
	 * default action, killing the whole process with WTERMSIG=6. Where it
	 * cannot read the disposition the raise dispatches core_handler in-guest,
	 * which re-enters abort() — the guard at the top turns that into the
	 * attributed in-dispatch _Exit(127). Inside a user signal handler the
	 * host refuses the nested dispatch (firebox#912) and the _Exit(127) below
	 * is attributed by the OUTER dispatch, as before. */
	__libc_sigaction(SIGABRT, &(struct sigaction){.sa_handler = SIG_DFL}, 0);
	{
		struct pthread *self = __pthread_self();
		self->blocked_sigmask[(SIGABRT-1)/(8*sizeof(long))] &=
			~(1UL << ((SIGABRT-1) % (8*sizeof(long))));
		__fbx_abort_final_tid = self->tid;
		(void)__wasi_thread_signal(self->tid, SIGABRT);
	}
	_Exit(127);
#endif

	/* Beyond this point should be unreachable. */
	a_crash();
	raise(SIGKILL);
	_Exit(127);
}
