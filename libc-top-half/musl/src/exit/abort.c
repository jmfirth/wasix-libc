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

_Noreturn void abort(void)
{
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
	int r;
	r = __wasi_thread_signal(__pthread_self()->tid, SIGABRT);
	_Exit(127);
#endif

	/* Beyond this point should be unreachable. */
	a_crash();
	raise(SIGKILL);
	_Exit(127);
}
