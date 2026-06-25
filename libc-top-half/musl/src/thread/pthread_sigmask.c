#include <signal.h>
#include <errno.h>
#include <string.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"

int pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict old)
{
	int ret;
	if (set && (unsigned)how - SIG_BLOCK > 2U) return EINVAL;
	ret = -__syscall(SYS_rt_sigprocmask, how, set, old, _NSIG/8);
	if (!ret && old) {
		if (sizeof old->__bits[0] == 8) {
			old->__bits[0] &= ~0x380000000ULL;
		} else {
			old->__bits[0] &= ~0x80000000UL;
			old->__bits[1] &= ~0x3UL;
		}
	}
	return ret;
}
#else
#include "pthread_impl.h"

/* Implemented in sigaction.c — drains the process-wide pending bitmask
 * so signals blocked during a window get delivered on unblock. */
extern void __wasm_drain_pending_sigs(void);

/* Query whether signal `sig` is blocked on the calling thread.
 * Consumed by __wasm_signal in sigaction.c to gate dispatch. Lives
 * here because it reads struct pthread's blocked_sigmask field. */
int __wasm_thread_sig_blocked(int sig) {
	if (sig < 1 || sig >= _NSIG) return 0;
	/* firebox#H2F — SIGKILL and SIGSTOP can NEVER be blocked (POSIX:
	 * "the signals SIGKILL and SIGSTOP cannot be added to a signal mask";
	 * the bits are silently ignored by sigprocmask/pthread_sigmask and by
	 * a handler's sa_mask). Report them as ALWAYS-unblocked here, the single
	 * authority every consumer (the __wasm_signal dispatch gate, the
	 * __wasm_raise_self self-raise fast path, sigsuspend) reads. Without
	 * this, a SIGKILL listed in a running handler's sa_mask (Open POSIX
	 * sigaction/4-1..4-26) made __wasm_raise_self PEND the in-handler
	 * raise(SIGKILL) in-guest instead of routing it to the host; the guest
	 * then exited cleanly and the process was never killed. The host's
	 * undeliverable-default-terminate path (firebox#S2,
	 * env.rs::first_no_handler_default_terminate) is the matching half — it
	 * faithfully terminates on a SIGKILL that REACHES the host, but only the
	 * libc routing it (rather than blocking it) gets it there. Defense in
	 * depth: __wasm_apply_handler_mask and pthread_sigmask also refuse to
	 * SET these bits, so blocked_sigmask never carries them — but enforcing
	 * it HERE too makes the invariant hold even for a mask written by some
	 * other path. */
	if (sig == SIGKILL || sig == SIGSTOP) return 0;
	struct pthread *self = __pthread_self();
	if (!self) return 0;
	unsigned long w = self->blocked_sigmask[(sig - 1) / (8 * sizeof(long))];
	unsigned long b = 1UL << ((sig - 1) % (8 * sizeof(long)));
	return (w & b) ? 1 : 0;
}

/* POSIX: pthread_sigmask manipulates the calling thread's blocked
 * signal mask. how ∈ {SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK}. Returns
 * 0 on success or an errno-style positive code. */
int pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict old)
{
	if (set && (unsigned)how - SIG_BLOCK > 2U) return EINVAL;
	struct pthread *self = __pthread_self();
	if (!self) return EINVAL;

	unsigned long *cur = self->blocked_sigmask;
	const size_t nwords = _NSIG / (8 * sizeof(long));

	if (old) {
		memcpy(old->__bits, cur, nwords * sizeof(long));
	}

	if (set) {
		/* firebox#H2F — POSIX: "the system shall not allow SIGKILL or
		 * SIGSTOP to be blocked"; an attempt to block them via
		 * sigprocmask/pthread_sigmask succeeds (no error) but the bits are
		 * silently dropped from the resulting mask. Mask off the two
		 * uncatchable signals from the incoming set BEFORE applying it, for
		 * SIG_BLOCK and SIG_SETMASK alike (SIG_UNBLOCK clears, so it can
		 * never introduce them). This keeps blocked_sigmask from ever
		 * carrying SIGKILL/SIGSTOP — the complement to
		 * __wasm_thread_sig_blocked reporting them always-unblocked. */
		const size_t kw = (size_t)(SIGKILL - 1) / (8 * sizeof(long));
		const unsigned long kb = 1UL << ((SIGKILL - 1) % (8 * sizeof(long)));
		const size_t sw = (size_t)(SIGSTOP - 1) / (8 * sizeof(long));
		const unsigned long sb = 1UL << ((SIGSTOP - 1) % (8 * sizeof(long)));
		switch (how) {
		case SIG_BLOCK:
			for (size_t i = 0; i < nwords; i++) {
				unsigned long add = set->__bits[i];
				if (i == kw) add &= ~kb;
				if (i == sw) add &= ~sb;
				cur[i] |= add;
			}
			break;
		case SIG_UNBLOCK:
			for (size_t i = 0; i < nwords; i++) {
				cur[i] &= ~set->__bits[i];
			}
			/* On unblock, any signals that queued on this thread
			 * during a block window should be redelivered. */
			__wasm_drain_pending_sigs();
			break;
		case SIG_SETMASK:
			for (size_t i = 0; i < nwords; i++) {
				unsigned long nw = set->__bits[i];
				if (i == kw) nw &= ~kb;
				if (i == sw) nw &= ~sb;
				cur[i] = nw;
			}
			/* SETMASK may lift blocks; drain pending. */
			__wasm_drain_pending_sigs();
			break;
		default:
			return EINVAL;
		}
	}
	return 0;
}
#endif