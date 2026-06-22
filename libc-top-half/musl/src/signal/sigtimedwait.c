#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#define IS32BIT(x) !((x)+0x80000000ULL>>32)
#define CLAMP(x) (int)(IS32BIT(x) ? (x) : 0x7fffffffU+((0ULL+(x))>>63))

#ifdef __wasilibc_unmodified_upstream
static int do_sigtimedwait(const sigset_t *restrict mask, siginfo_t *restrict si, const struct timespec *restrict ts)
{
#ifdef SYS_rt_sigtimedwait_time64
	time_t s = ts ? ts->tv_sec : 0;
	long ns = ts ? ts->tv_nsec : 0;
	int r = -ENOSYS;
	if (SYS_rt_sigtimedwait == SYS_rt_sigtimedwait_time64 || !IS32BIT(s))
		r = __syscall_cp(SYS_rt_sigtimedwait_time64, mask, si,
			ts ? ((long long[]){s, ns}) : 0, _NSIG/8);
	if (SYS_rt_sigtimedwait == SYS_rt_sigtimedwait_time64 || r!=-ENOSYS)
		return r;
	return __syscall_cp(SYS_rt_sigtimedwait, mask, si,
		ts ? ((long[]){CLAMP(s), ns}) : 0, _NSIG/8);;
#else
	return __syscall_cp(SYS_rt_sigtimedwait, mask, si, ts, _NSIG/8);
#endif
}
#endif

#ifdef __wasilibc_unmodified_upstream
int sigtimedwait(const sigset_t *restrict mask, siginfo_t *restrict si, const struct timespec *restrict timeout)
{
	int ret;
	do ret = do_sigtimedwait(mask, si, timeout);
	while (ret==-EINTR);
	return __syscall_ret(ret);
}
#else
#include "pthread_impl.h"
#include "atomic.h"

/* firebox#43B / S5 — faithful synchronous signal acceptance (sigtimedwait,
 * and via it sigwait/sigwaitinfo).
 *
 * POSIX: sigtimedwait() suspends the calling thread until one of the signals
 * in `set` becomes pending, then atomically SELECTS it, CLEARS it from the
 * pending set, and RETURNS its number — WITHOUT running the signal's handler.
 * (The signals in `set` must be blocked by the caller for this to be the only
 * acceptance path; every Open POSIX S5 test blocks them first via
 * sigprocmask/sighold.) With `timeout` non-NULL, if no signal arrives within
 * the interval it returns -1/EAGAIN; a zero-valued timeout polls and returns
 * immediately. With `timeout` NULL it blocks indefinitely (sigwait/sigwaitinfo).
 *
 * WHY this lives in the substrate's libc and is built on the existing pending
 * machinery: __wasm_signal already ENQUEUES a blocked signal into both the
 * per-thread (self->pending_sigs[]) and process-wide (__wasm_pending_sigs[])
 * bitmasks instead of dispatching it (sigaction.c). Synchronous acceptance is
 * therefore "atomically claim a pending bit in `set` and clear it" — no host
 * round-trip, no handler. The wait is on the existing per-thread sigsuspend
 * wake counter, which __wasm_pend_signal now bumps (see sigaction.c) so a
 * thread parked here is woken the instant a signal it is waiting for pends —
 * including a cross-thread pthread_kill targeting this thread (sigwait/6-2)
 * and a process-wide raise that exactly one of several waiters claims
 * (sigwait/6-1, the atomic test-and-clear gives single-consumer semantics). */

/* Process-wide pending bitmask (per-thread copy is self->pending_sigs[]). */
extern volatile int __wasm_pending_sigs[];

/* Try to atomically claim a pending signal that is a member of `set`,
 * preferring the LOWEST-numbered signal (POSIX leaves selection unspecified
 * for non-RT signals but REQUIRES the lowest-numbered for queued RT signals —
 * sigwait/7-1). Checks the calling thread's own pending bitmask first (where a
 * self-raise or a pthread_kill-to-this-thread lands), then the process-wide
 * bitmask (where a process-directed raise lands and any single waiter may
 * claim it). On success clears the bit from BOTH bitmasks (so sigpending no
 * longer reports it and no other waiter re-claims it) and returns the signo;
 * returns 0 if nothing in `set` is pending. */
static int __sigtw_claim(const sigset_t *set)
{
	struct pthread *self = __pthread_self();
	for (int sig = 1; sig < _NSIG; sig++) {
		if (!sigismember(set, sig)) continue;
		int word = (sig - 1) / 32;
		int bit  = 1 << ((sig - 1) % 32);
		/* Per-thread first. a_fetch_and clears the bit atomically; if it
		 * was set, this thread won the claim. */
		if (self) {
			int prev = a_fetch_and((volatile int *)&self->pending_sigs[word], ~bit);
			if (prev & bit) {
				/* Mirror-clear the process-wide view. */
				a_and(&__wasm_pending_sigs[word], ~bit);
				return sig;
			}
		}
		/* Process-wide: a process-directed signal that pended without a
		 * specific target thread. Exactly one waiter claims it. */
		int pprev = a_fetch_and(&__wasm_pending_sigs[word], ~bit);
		if (pprev & bit) {
			/* Also clear any stale per-thread copy of this bit so it
			 * isn't redelivered on a later unblock. */
			if (self) a_and((volatile int *)&self->pending_sigs[word], ~bit);
			return sig;
		}
	}
	return 0;
}

/* __timedwait(addr, val, clk, abs_timeout|NULL, priv) — declared in
 * pthread_impl.h (included above). Parks on *addr while it == val, until the
 * value changes (futex wake), EINTR, or the absolute deadline passes; returns
 * ETIMEDOUT on deadline, 0 otherwise. A NULL deadline blocks indefinitely. */

/* Register / clear THIS thread's sigwait acceptance set (the bits of `set`),
 * so __wasm_signal pends-rather-than-dispatches the awaited signals while we
 * wait. Cleared on EVERY exit path so a returned-from-sigwait thread resumes
 * normal handler delivery. */
static void __sigtw_set_acceptance(const sigset_t *set, int on)
{
	struct pthread *self = __pthread_self();
	if (!self) return;
	const int nwords = (_NSIG + 31) / 32;
	for (int w = 0; w < nwords; w++) {
		int bits = 0;
		if (on) {
			for (int b = 0; b < 32; b++) {
				int sig = w * 32 + b + 1;
				if (sig >= 1 && sig < _NSIG && sigismember(set, sig))
					bits |= (1 << b);
			}
		}
		a_store((volatile int *)&self->sigwait_set[w], bits);
	}
}

static void __sigtw_fill_si(siginfo_t *si, int sig)
{
	if (!si) return;
	/* Minimal siginfo: only si_signo is authoritative from the libc
	 * pending bitmask (it carries no queued value — carrying a queued
	 * si_value would need a host signal-queue assist, tracked separately).
	 * si_code SI_USER matches a kill()/raise()-delivered signal. */
	memset(si, 0, sizeof *si);
	si->si_signo = sig;
	si->si_code = SI_USER;
}

int sigtimedwait(const sigset_t *restrict set, siginfo_t *restrict si, const struct timespec *restrict timeout)
{
	if (!set) { errno = EINVAL; return -1; }
	if (timeout && (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L
	                || timeout->tv_sec < 0)) {
		errno = EINVAL; return -1;
	}
	struct pthread *self = __pthread_self();
	if (!self) { errno = EINVAL; return -1; }

	/* Compute an absolute deadline for the bounded-wait case. A
	 * zero-valued timeout means "poll once, then EAGAIN". */
	struct timespec deadline;
	int have_deadline = 0;
	int zero_timeout = 0;
	if (timeout) {
		have_deadline = 1;
		if (timeout->tv_sec == 0 && timeout->tv_nsec == 0) zero_timeout = 1;
		struct timespec now;
		if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
			/* Fall back to a relative-from-zero deadline; still bounded. */
			now.tv_sec = 0; now.tv_nsec = 0;
		}
		deadline.tv_sec = now.tv_sec + timeout->tv_sec;
		deadline.tv_nsec = now.tv_nsec + timeout->tv_nsec;
		if (deadline.tv_nsec >= 1000000000L) {
			deadline.tv_sec++; deadline.tv_nsec -= 1000000000L;
		}
	}

	/* Make the awaited signals accepted-by-this-thread (pended, not
	 * dispatched) for the lifetime of the wait. Set BEFORE the first scan
	 * so a signal arriving between the scan and the park is pended, not
	 * lost to its handler. Cleared on every return below. */
	__sigtw_set_acceptance(set, 1);

	for (;;) {
		/* Sample the wake counter BEFORE scanning so a pend that races in
		 * between the scan and the park is not missed (the standard
		 * sample-then-recheck pattern, mirroring sigsuspend). */
		int tick = self->sigsuspend_tick;

		int sig = __sigtw_claim(set);
		if (sig) {
			__sigtw_set_acceptance(set, 0);
			__sigtw_fill_si(si, sig);
			return sig;
		}

		if (zero_timeout) {
			/* Polled, nothing ready: POSIX EAGAIN. */
			__sigtw_set_acceptance(set, 0);
			errno = EAGAIN;
			return -1;
		}

		/* Park until the wake counter advances (a signal pended / a
		 * handler ran) or the deadline passes. __timedwait re-checks
		 * the value, so a wake that already moved `tick` returns at once. */
		int r = __timedwait(&self->sigsuspend_tick, tick, CLOCK_MONOTONIC,
		                    have_deadline ? &deadline : 0, 1);
		if (r == ETIMEDOUT) {
			/* One last claim attempt to close the race where the signal
			 * pended exactly as the wait timed out. */
			sig = __sigtw_claim(set);
			__sigtw_set_acceptance(set, 0);
			if (sig) {
				__sigtw_fill_si(si, sig);
				return sig;
			}
			errno = EAGAIN;
			return -1;
		}
		/* EINTR / spurious wake / a different signal pended: loop and
		 * re-scan. (A signal NOT in `set` whose handler ran will have
		 * advanced the tick; we simply rescan and re-park.) The acceptance
		 * set stays registered across the loop. */
	}
}
#endif
