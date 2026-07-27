#include "pthread_impl.h"
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#include "atomic.h"
#include <stddef.h>
#endif
#include <signal.h>

#ifdef __wasilibc_unmodified_upstream
#else
/* Drain helper — re-raises signals that queued on THIS thread while the
 * mask held them, after __restore_sigs puts the mask back. Defined in
 * sigaction.c. */
void __wasm_drain_pending_sigs(void);
#endif

static const unsigned long all_mask[] = {
#if ULONG_MAX == 0xffffffff && _NSIG == 129
	-1UL, -1UL, -1UL, -1UL
#elif ULONG_MAX == 0xffffffff
	-1UL, -1UL
#else
	-1UL
#endif
};

static const unsigned long app_mask[] = {
#if ULONG_MAX == 0xffffffff
#if _NSIG == 65
	0x7fffffff, 0xfffffffc
#else
	0x7fffffff, 0xfffffffc, -1UL, -1UL
#endif
#else
#if _NSIG == 65
	0xfffffffc7fffffff
#else
	0xfffffffc7fffffff, -1UL
#endif
#endif
};

#ifdef __wasilibc_unmodified_upstream
#else
/* firebox#35F — __block_all_sigs / __block_app_sigs / __restore_sigs operate on
 * the CALLING THREAD's mask, never a process-wide one.
 *
 * WHY (the defect this replaced): these three used to set/clear a process-wide
 * `__wasm_signals_blocked` flag plus swap the host's dispatch callback to the
 * `__wasm_signal_blocked` no-op stub — both process-wide. __wasm_signal
 * consulted that flag BEFORE the per-thread mask, so any thread inside a block
 * window (pthread_kill, _Fork, abort, dlopen, the SIGABRT sigaction path, ...)
 * SWALLOWED every other thread's signal for the window's duration: the handler
 * never ran, while the host — which derives `interrupting` from its own
 * handler-installed mirror — still returned EINTR. That is a fail-open drop
 * (firebox#NHJ: 30% at 2x cores, 0% idle, proven with an in-guest `blocked=1
 * pend=1` sample taken the instant select() returned).
 *
 * POSIX masks are strictly per-thread; upstream musl's rt_sigprocmask above
 * cannot, even in principle, suppress delivery to another thread. The machinery
 * to model it correctly already exists — `struct pthread::blocked_sigmask`,
 * read by `__wasm_thread_sig_blocked()` — so these now fold into it.
 *
 * `set` (a `sigset_t *` or an `unsigned long[_NSIG/(8*sizeof(long))]`; every
 * caller passes at least that many words) receives the PREVIOUS mask, matching
 * rt_sigprocmask's old-set out-parameter. A NULL `set` means "block WITHOUT
 * saving" (abort, setxid, synccall's inner block) — it is not a request to
 * clear. Nested save/restore therefore composes exactly as upstream:
 * synccall's `__block_app_sigs(&oldmask); ...; __block_all_sigs(0); ...;
 * __restore_sigs(&oldmask)` restores the mask from before the OUTER block. */
#define __WASM_SIGMASK_WORDS (_NSIG / (8 * sizeof(long)))

static void __wasm_block_sigs(const unsigned long *add, void *set)
{
	struct pthread *self = __pthread_self();
	if (!self) return;
	unsigned long *cur = self->blocked_sigmask;
	/* firebox#H2F — SIGKILL and SIGSTOP can NEVER be blocked. Both masks
	 * above are pre-#H2F upstream constants that name them, so strip the two
	 * bits here for the same reason __wasm_apply_handler_mask and
	 * pthread_sigmask do: blocked_sigmask must never carry them, or an
	 * in-window raise(SIGKILL) would be pended in-guest instead of routed to
	 * the host that terminates the process. */
	const size_t kw = (size_t)(SIGKILL - 1) / (8 * sizeof(long));
	const unsigned long kb = 1UL << ((SIGKILL - 1) % (8 * sizeof(long)));
	const size_t sw = (size_t)(SIGSTOP - 1) / (8 * sizeof(long));
	const unsigned long sb = 1UL << ((SIGSTOP - 1) % (8 * sizeof(long)));
	unsigned long *out = (unsigned long *)set;
	for (size_t i = 0; i < __WASM_SIGMASK_WORDS; i++) {
		unsigned long a = add[i];
		if (i == kw) a &= ~kb;
		if (i == sw) a &= ~sb;
		if (out) out[i] = cur[i];
		cur[i] |= a;
	}
}
#endif

void __block_all_sigs(void *set)
{
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigprocmask, SIG_BLOCK, &all_mask, set, _NSIG/8);
#else
	__wasm_block_sigs(all_mask, set);
#endif
}

void __block_app_sigs(void *set)
{
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigprocmask, SIG_BLOCK, &app_mask, set, _NSIG/8);
#else
	__wasm_block_sigs(app_mask, set);
#endif
}

void __restore_sigs(void *set)
{
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigprocmask, SIG_SETMASK, set, 0, _NSIG/8);
#else
	/* Put this thread's mask back, then drain the signals that queued on
	 * this thread while it held them. Drain must happen AFTER the mask is
	 * restored so a re-raised signal dispatches instead of re-pending.
	 * A NULL `set` mirrors rt_sigprocmask(SIG_SETMASK, NULL, ...): no mask
	 * change (no caller does this today; kept faithful rather than
	 * reinterpreting it as "clear"). */
	struct pthread *self = __pthread_self();
	if (self && set) {
		const unsigned long *saved = (const unsigned long *)set;
		unsigned long *cur = self->blocked_sigmask;
		for (size_t i = 0; i < __WASM_SIGMASK_WORDS; i++)
			cur[i] = saved[i];
	}
	__wasm_drain_pending_sigs();
#endif
}
