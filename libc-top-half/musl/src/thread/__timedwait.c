#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "futex.h"
#include "syscall.h"
#include "pthread_impl.h"

#define IS32BIT(x) !((x)+0x80000000ULL>>32)
#define CLAMP(x) (int)(IS32BIT(x) ? (x) : 0x7fffffffU+((0ULL+(x))>>63))

#ifdef __wasilibc_unmodified_upstream
static int __futex4_cp(volatile void *addr, int op, int val, const struct timespec *to)
{
	int r;
#ifdef SYS_futex_time64
	time_t s = to ? to->tv_sec : 0;
	long ns = to ? to->tv_nsec : 0;
	r = -ENOSYS;
	if (SYS_futex == SYS_futex_time64 || !IS32BIT(s))
		r = __syscall_cp(SYS_futex_time64, addr, op, val,
			to ? ((long long[]){s, ns}) : 0);
	if (SYS_futex == SYS_futex_time64 || r!=-ENOSYS) return r;
	to = to ? (void *)(long[]){CLAMP(s), ns} : 0;
#endif
	r = __syscall_cp(SYS_futex, addr, op, val, to);
	if (r != -ENOSYS) return r;
	return __syscall_cp(SYS_futex, addr, op & ~FUTEX_PRIVATE, val, to);
}

static volatile int dummy = 0;
weak_alias(dummy, __eintr_valid_flag);
#else
static int __futex4_cp(volatile void *addr, int op, int val, const struct timespec *to)
{
	int64_t max_wait_ns = -1;
	if (to) {
		max_wait_ns = (int64_t)(to->tv_sec * 1000000000 + to->tv_nsec);
	}
	return __wasilibc_futex_wait_wasix(addr, op, val, max_wait_ns);
}

static int cancel_at_wait(struct pthread *self, int result)
{
	__testcancel_async();
	if (!self || self->canceldisable == PTHREAD_CANCEL_DISABLE
	    || !__atomic_load_n(&self->cancel, __ATOMIC_ACQUIRE))
		return result;

	/* Only this thread changes canceldisable. Once cancel is observed
	 * even a MASKED cancellation must be reported to the caller, which
	 * removes its waiter and reacquires the mutex before unwinding. */
	int masked = self->canceldisable == PTHREAD_CANCEL_MASKED;
	if (masked && result == ETIMEDOUT)
		return result;
	__testcancel();
	return masked ? ECANCELED : result;
}
#endif

int __timedwait_cp(volatile int *addr, int val,
	clockid_t clk, const struct timespec *at, int priv)
{
	int r;
	struct timespec to, *top=0;
#ifndef __wasilibc_unmodified_upstream
	/* firebox#G13 / #NSL (futex-funnel arm) — state for restoring the Linux
	 * futex(FUTEX_WAIT)==-EINTR contract across the host spurious-Woken model.
	 * `self` and the dispatch-counter baseline are captured just before the
	 * park below; the EINTR-synthesis block after `__futex4_cp` consults them.
	 * Declared here (not in the upstream build) so the upstream arm carries no
	 * unused locals. */
	struct pthread *self = __pthread_self();
#endif

	if (priv) priv = FUTEX_PRIVATE;

	if (at) {
		if (at->tv_nsec >= 1000000000UL) return EINVAL;
		if (__clock_gettime(clk, &to)) return EINVAL;
		to.tv_sec = at->tv_sec - to.tv_sec;
		if ((to.tv_nsec = at->tv_nsec - to.tv_nsec) < 0) {
			to.tv_sec--;
			to.tv_nsec += 1000000000;
		}
		if (to.tv_sec < 0) return ETIMEDOUT;
		top = &to;
	}

#ifndef __wasilibc_unmodified_upstream
	/* A request can arrive after the caller's cancellation check and
	 * before it parks, for example while cond_wait unlocks the mutex.
	 * Check the request itself here. The futex interrupt epoch covers
	 * requests arriving after this check. */
	r = cancel_at_wait(self, 0);
	if (r) return r;

	/* firebox#G13 / #NSL — snapshot THIS thread signal-dispatch counter
	 * immediately before the park. `sigdispatch_tick` is bumped by
	 * `__wasm_signal` ONLY when a signal handler actually DISPATCHED on this
	 * thread (never on the block/enqueue/pend path) — the same counter, with
	 * the same "a handler ran" meaning, that `sigsuspend` parks on for its
	 * POSIX EINTR wake (sigaction.c bump site + sigsuspend.c #XT7). Capturing
	 * it here (vs. at function entry) anchors the baseline to the wait window,
	 * so only a handler that runs DURING the park is counted; the local is
	 * preserved across the asyncify deep-sleep/rewind of `__futex4_cp` exactly
	 * like `r`/`top`. */
	int sigdisp_before = self ? self->sigdispatch_tick : 0;
#endif

	r = -__futex4_cp(addr, FUTEX_WAIT|priv, val, top);

#ifndef __wasilibc_unmodified_upstream
	/* The host waits on a relative timer. Only the caller's clock can
	 * decide whether the absolute deadline has passed. If the timer
	 * expired first, let the caller recheck its condition and deadline. */
	if (r == ETIMEDOUT && at) {
		if (__clock_gettime(clk, &to)) return EINVAL;
		if (to.tv_sec < at->tv_sec ||
		    (to.tv_sec == at->tv_sec && to.tv_nsec < at->tv_nsec))
			r = 0;
	}

	/* firebox#G13 / #NSL — restore the Linux futex(FUTEX_WAIT)==-EINTR contract
	 * at the shared funnel. On Linux a parked futex woken by a delivered signal
	 * returns -EINTR, so `__timedwait_cp` returns EINTR and every musl primitive
	 * built on it reacts the POSIX-correct way for ITS contract: sem_wait /
	 * sem_timedwait / aio_suspend RETURN -1/EINTR (Open POSIX sem_timedwait/9-1,
	 * sem_wait, aio_suspend), while pthread_cond_timedwait / pthread_join /
	 * pthread_mutex_timedlock / pthread_rwlock_timed*lock / sigtimedwait already
	 * loop-on-EINTR (cond literally encodes `e==EINTR` in its wait loop). The
	 * WASIX port broke that contract: the host cannot surface -EINTR through the
	 * guest futex (`__wasilibc_futex_wait_wasix` `__builtin_trap`s on any
	 * non-zero return), so firebox#5RE models a signal interrupt of a parked
	 * futex as a spurious Woken (r==0) and relies on the caller re-testing its
	 * condition. That self-heal is correct for the loop-on-EINTR primitives but
	 * SILENTLY SWALLOWS the interrupt for sem/aio_suspend, which must report
	 * EINTR and never re-park here — pre-fix sem_timedwait re-looped and ran its
	 * full timeout, returning ETIMEDOUT instead of EINTR.
	 *
	 * Detect it here, once, faithfully: `r == 0` immediately after `__futex4_cp`
	 * means the host returned Woken from an ACTUAL park (the value-changed fast
	 * path returns -EWOULDBLOCK, a distinct non-zero value, and a timeout
	 * returns -ETIMEDOUT — neither is 0), and a `sigdispatch_tick` delta means a
	 * handler ran on this thread during that park. Together that is precisely
	 * the Linux signal-interrupted-park, so synthesize EINTR. Set BEFORE the
	 * fold below so the EINTR survives (the fold preserves EINTR). A value-
	 * changed wake (r==EWOULDBLOCK→0 via the fold) is left as 0 so the caller
	 * re-tests and proceeds, matching Linux (futex returns EAGAIN there, not
	 * EINTR). This is NOT keyed on the wait being interruptible: sem_wait and
	 * aio_suspend are on the POSIX never-restart list, so SA_RESTART does not
	 * apply, and the loop-on-EINTR primitives discard the EINTR regardless. */
	if (r == 0 && self && self->sigdispatch_tick != sigdisp_before)
		r = EINTR;
#endif

	if (r != EINTR && r != ETIMEDOUT && r != ECANCELED) r = 0;
#ifdef __wasilibc_unmodified_upstream
	/* Mitigate bug in old kernels wrongly reporting EINTR for non-
	 * interrupting (SA_RESTART) signal handlers. This is only practical
	 * when NO interrupting signal handlers have been installed, and
	 * works by sigaction tracking whether that's the case. */
	if (r == EINTR && !__eintr_valid_flag) r = 0;
#endif

#ifndef __wasilibc_unmodified_upstream
	/* A cancel arriving after this check stays pending. Every invocation
	 * that disarms a MASKED cancel must also publish its outcome. */
	r = cancel_at_wait(self, r);
#endif

	return r;
}

int __timedwait(volatile int *addr, int val,
	clockid_t clk, const struct timespec *at, int priv)
{
	int cs, r;
	__pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	r = __timedwait_cp(addr, val, clk, at, priv);
	__pthread_setcancelstate(cs, 0);
	return r;
}
