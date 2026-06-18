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
#endif

int __timedwait_cp(volatile int *addr, int val,
	clockid_t clk, const struct timespec *at, int priv)
{
	int r;
	struct timespec to, *top=0;

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
	/* firebox#5RE — act on a pending ASYNCHRONOUS cancel BEFORE parking, so a
	 * cancel that arrived after the caller decided to wait but before this
	 * thread actually parks in `__futex4_cp` is honored without depending on a
	 * subsequent futex wake. Together with the post-wait `__testcancel_async`
	 * below this covers both orderings (cancel-before-park / cancel-during-
	 * park) — the cancel-during-park case is delivered by the runtime's futex
	 * EINTR-epoch wake. Without this pre-check, an async cancel that races
	 * ahead of a PERMANENT park (e.g. a contended `pthread_mutex_lock` whose
	 * holder never releases) would never be acted on (stochastic
	 * setcanceltype/1-1). Deferred cancellation is intentionally NOT checked
	 * here: this is the cancel-DISABLED `__timedwait` wrapper path, and only
	 * async cancellation is defined to fire where deferred is bracketed off. */
	__testcancel_async();
#endif

	r = -__futex4_cp(addr, FUTEX_WAIT|priv, val, top);
	if (r != EINTR && r != ETIMEDOUT && r != ECANCELED) r = 0;
#ifdef __wasilibc_unmodified_upstream
	/* Mitigate bug in old kernels wrongly reporting EINTR for non-
	 * interrupting (SA_RESTART) signal handlers. This is only practical
	 * when NO interrupting signal handlers have been installed, and
	 * works by sigaction tracking whether that's the case. */
	if (r == EINTR && !__eintr_valid_flag) r = 0;
#endif

#ifndef __wasilibc_unmodified_upstream
	/* firebox#5RE — __timedwait_cp is a POSIX cancellation point (the
	 * cancellable form used by pthread_cond_wait / sem_wait). Upstream musl
	 * acts on a pending cancel via the SIGCANCEL handler redirecting the PC
	 * to __cp_cancel mid-syscall; WASIX has no such handler. Instead, a
	 * pthread_cancel wakes a parked target (the runtime enqueue interrupts
	 * the futex wait — firebox#5RE wasmer arm), `__futex4_cp` returns, and we
	 * observe the pending cancel HERE and unwind. `__testcancel` is a no-op
	 * unless this thread has a pending cancel with cancellation enabled, in
	 * which case it calls `__cancel` → `pthread_exit(PTHREAD_CANCELED)`,
	 * running the registered cleanup handlers (LIFO) + TSD destructors.
	 * Placed after the wait so a thread that blocks, then gets a cancel, acts
	 * on it on return — the deferred-cancellation contract.
	 *
	 * `__testcancel_async` additionally honors a pending ASYNCHRONOUS cancel
	 * EVEN when deferred cancellation is bracketed off — async cancellation
	 * (PTHREAD_CANCEL_ASYNCHRONOUS) is defined to take effect at any time,
	 * including inside the non-cancellation-point `__timedwait` wrapper that
	 * pthread_mutex_lock uses (which sets PTHREAD_CANCEL_DISABLE around the
	 * wait). Without this, an async-cancel of a thread blocked on a contended
	 * mutex is never acted on (the deferred `__testcancel` sees DISABLE and
	 * the mutex wrapper just re-waits). matches glibc/upstream-musl, where the
	 * SIGCANCEL handler's `cancelasync` branch fires regardless of the
	 * deferred state. */
	__testcancel_async();
	__testcancel();
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
