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
		switch (how) {
		case SIG_BLOCK:
			for (size_t i = 0; i < nwords; i++) {
				cur[i] |= set->__bits[i];
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
				cur[i] = set->__bits[i];
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