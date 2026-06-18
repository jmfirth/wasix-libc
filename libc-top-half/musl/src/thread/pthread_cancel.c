#define _GNU_SOURCE
#include <string.h>
#include "pthread_impl.h"
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
int pthread_kill(pthread_t t, int sig);
#endif

#ifdef __wasilibc_unmodified_upstream
hidden long __cancel(), __syscall_cp_asm(), __syscall_cp_c();
#endif

long __cancel()
{
	pthread_t self = __pthread_self();
	if (self->canceldisable == PTHREAD_CANCEL_ENABLE || self->cancelasync)
		pthread_exit(PTHREAD_CANCELED);
	self->canceldisable = PTHREAD_CANCEL_DISABLE;
	return -ECANCELED;
}

#ifndef __wasilibc_unmodified_upstream
/* firebox#5RE — WASIX deferred-cancellation model.
 *
 * Upstream musl delivers a cancel as SIGCANCEL and a kernel-installed
 * `cancel_handler` redirects the target thread's PC to `__cp_cancel` while
 * it is inside the async-cancel region (`__cp_begin`..`__cp_end`) of
 * `__syscall_cp_asm`. WASIX has no signal-context PC redirection and no
 * inline-asm syscall region, so that machinery (all `#ifdef
 * __wasilibc_unmodified_upstream` below) is absent.
 *
 * The faithful WASIX equivalent is POSIX *deferred* cancellation (the
 * default `PTHREAD_CANCEL_DEFERRED` type): the cancel-requested flag is
 * set by `pthread_cancel` and observed at the next *cancellation point*.
 * `__testcancel` is that observation; it calls `__cancel`, which (when
 * cancellation is enabled) calls `pthread_exit(PTHREAD_CANCELED)` — and
 * `__pthread_exit` already runs the `pthread_cleanup_push` handler LIFO
 * unwind + TSD destructors (libc-top-half/musl/src/thread/pthread_create.c).
 * So the only WASIX-specific work is: (1) define a real `__testcancel`
 * (overriding the weak `dummy` in pthread_testcancel.c), and (2) make the
 * cancellation points (`pthread_testcancel`, the cancellable wait
 * `__timedwait_cp`, and `sleep`/`nanosleep`) call it. The SIGCANCEL sent by
 * `pthread_cancel` below exists only to *wake* a target parked in a
 * blocking syscall so it re-runs the cancellation point promptly; the guest
 * `__wasm_signal` benignly drops 33 (musl reserves 32/33/34), the runtime
 * wake interrupts the parked wait, and the wait wrapper's `__testcancel`
 * then unwinds the thread. POSIX: a cancellation point "shall act as a
 * point at which a pending cancellation … is acted upon."
 */
void __testcancel()
{
	pthread_t self = __pthread_self();
	if (self->cancel && self->canceldisable != PTHREAD_CANCEL_DISABLE)
		__cancel();
}

/* firebox#5RE — act on a pending ASYNCHRONOUS cancel regardless of the
 * deferred-cancellation disable bracket. Async cancellation
 * (PTHREAD_CANCEL_ASYNCHRONOUS) is defined to take effect at any point in
 * execution, so it must fire even inside the non-cancellation-point
 * `__timedwait` wrapper (which brackets the wait in PTHREAD_CANCEL_DISABLE).
 * Deferred cancellation is left to `__testcancel`, which honors the bracket.
 * No-op unless this thread is in ASYNCHRONOUS mode with a pending cancel. */
void __testcancel_async()
{
	pthread_t self = __pthread_self();
	if (self->cancel && self->cancelasync)
		pthread_exit(PTHREAD_CANCELED);
}
#endif

#ifdef __wasilibc_unmodified_upstream
long __syscall_cp_asm(volatile void *, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t,
                      syscall_arg_t, syscall_arg_t, syscall_arg_t);

long __syscall_cp_c(syscall_arg_t nr,
                    syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
                    syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	pthread_t self;
	long r;
	int st;

	if ((st=(self=__pthread_self())->canceldisable)
	    && (st==PTHREAD_CANCEL_DISABLE || nr==SYS_close))
		return __syscall(nr, u, v, w, x, y, z);

	r = __syscall_cp_asm(&self->cancel, nr, u, v, w, x, y, z);
	if (r==-EINTR && nr!=SYS_close && self->cancel &&
	    self->canceldisable != PTHREAD_CANCEL_DISABLE)
		r = __cancel();
	return r;
}
#endif

static void _sigaddset(sigset_t *set, int sig)
{
	unsigned s = sig-1;
	set->__bits[s/8/sizeof *set->__bits] |= 1UL<<(s&8*sizeof *set->__bits-1);
}

extern hidden const char __cp_begin[1], __cp_end[1], __cp_cancel[1];

#ifdef __wasilibc_unmodified_upstream
static void cancel_handler(int sig, siginfo_t *si, void *ctx)
{
	pthread_t self = __pthread_self();
	ucontext_t *uc = ctx;
	uintptr_t pc = uc->uc_mcontext.MC_PC;

	a_barrier();
	if (!self->cancel || self->canceldisable == PTHREAD_CANCEL_DISABLE) return;

	_sigaddset(&uc->uc_sigmask, SIGCANCEL);

	if (self->cancelasync || pc >= (uintptr_t)__cp_begin && pc < (uintptr_t)__cp_end) {
		uc->uc_mcontext.MC_PC = (uintptr_t)__cp_cancel;
#ifdef CANCEL_GOT
		uc->uc_mcontext.MC_GOT = CANCEL_GOT;
#endif
		return;
	}
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_tkill, self->tid, SIGCANCEL);
#else
	pthread_kill(self, SIGCANCEL);
#endif
}

void __testcancel()
{
	pthread_t self = __pthread_self();
	if (self->cancel && !self->canceldisable)
		__cancel();
}

static void init_cancellation()
{
	struct sigaction sa = {
		.sa_flags = SA_SIGINFO | SA_RESTART,
		.sa_sigaction = cancel_handler
	};
	memset(&sa.sa_mask, -1, _NSIG/8);
	__libc_sigaction(SIGCANCEL, &sa, 0);
}
#endif

int pthread_cancel(pthread_t t)
{
#ifdef __wasilibc_unmodified_upstream
	static int init;
	if (!init) {
		init_cancellation();
		init = 1;
	}
	a_store(&t->cancel, 1);
	if (t == pthread_self()) {
		if (t->canceldisable == PTHREAD_CANCEL_ENABLE && t->cancelasync)
			pthread_exit(PTHREAD_CANCELED);
		return 0;
	}
#else
	/* firebox#5RE — record the cancel request on the target. The store must
	 * be visible before the wake so the woken target observes it at its next
	 * cancellation point. (Upstream sets this too — line above — but only on
	 * the unmodified_upstream path; on WASIX the flag was never set, so
	 * pthread_cancel was a pure no-op even when delivery worked.) */
	a_store(&t->cancel, 1);
	if (t == pthread_self()) {
		/* Self-cancel: an asynchronous self-cancel takes effect immediately;
		 * a deferred self-cancel is acted on at the next cancellation point
		 * (e.g. the pthread_testcancel that pthread_setcanceltype issues, or
		 * the next blocking wait). */
		if (t->canceldisable == PTHREAD_CANCEL_ENABLE && t->cancelasync)
			pthread_exit(PTHREAD_CANCELED);
		return 0;
	}
	/* Cross-thread: wake the target so a thread parked in a blocking syscall
	 * re-runs its cancellation point promptly. SIGCANCEL itself is dropped by
	 * the guest dispatcher (musl-reserved 33); the wake is the load-bearing
	 * effect. A running (non-blocked) target acts on the flag at its next
	 * cancellation point, exactly as deferred cancellation prescribes. */
#endif
	return pthread_kill(t, SIGCANCEL);
}
