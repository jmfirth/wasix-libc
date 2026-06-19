#include <sys/time.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif

#define IS32BIT(x) !((x)+0x80000000ULL>>32)

int setitimer(int which, const struct itimerval *restrict new, struct itimerval *restrict old)
{
#ifdef __wasilibc_unmodified_upstream
	if (sizeof(time_t) > sizeof(long)) {
		time_t is = new->it_interval.tv_sec, vs = new->it_value.tv_sec;
		long ius = new->it_interval.tv_usec, vus = new->it_value.tv_usec;
		if (!IS32BIT(is) || !IS32BIT(vs))
			return __syscall_ret(-ENOTSUP);
		long old32[4];
		int r = __syscall(SYS_setitimer, which,
			((long[]){is, ius, vs, vus}), old32);
		if (!r && old) {
			old->it_interval.tv_sec = old32[0];
			old->it_interval.tv_usec = old32[1];
			old->it_value.tv_sec = old32[2];
			old->it_value.tv_usec = old32[3];
		}
		return __syscall_ret(r);
	}
	return syscall(SYS_setitimer, which, new, old);
#else
	/* firebox#8B5term — three faithfulness fixes to the wasm itimer mapping:
	 *
	 * (1) FIRE DELAY comes from `it_value`, NOT `it_interval`. POSIX:
	 *     `it_value` is the time until the timer FIRST expires; `it_interval`
	 *     is the repeat PERIOD after that first expiry. The old code armed the
	 *     timer off `it_interval`, so `alarm(N)` — which sets
	 *     `{it_value.tv_sec=N, it_interval=0}` — armed a ZERO interval, which
	 *     the runtime treats as "disarm" (`proc_raise_interval` interval==0 ⇒
	 *     remove). Net: `alarm()`/one-shot `setitimer()` NEVER fired. Use
	 *     `it_value` for the delay the runtime arms.
	 *
	 * (2) `it_value == 0` DISARMS the timer (POSIX). A zero `ts` flows through
	 *     to `proc_raise_interval`, which removes the interval — correct.
	 *
	 * (3) The TARGET SIGNAL depends on `which`: ITIMER_REAL → SIGALRM,
	 *     ITIMER_VIRTUAL → SIGVTALRM, ITIMER_PROF → SIGPROF. The old code
	 *     hardcoded SIGALRM, so `setitimer(ITIMER_VIRTUAL, …)` mis-delivered.
	 *
	 * `repeat` is TRUE iff a non-zero `it_interval` was requested (a periodic
	 * timer); a one-shot (`it_interval == 0`, e.g. `alarm`) passes FALSE.
	 *
	 * NOTE (single-interval model): the runtime arms ONE interval, so when
	 * `it_value != it_interval` (a periodic timer whose first delay differs
	 * from its period) only the `it_value` delay is honored faithfully; the
	 * common cases — `alarm` (one-shot) and a periodic timer with
	 * `it_value == it_interval` — are exact. */
	__wasi_timestamp_t ts = (new->it_value.tv_sec * (__wasi_timestamp_t)1000000000)
		+ (new->it_value.tv_usec * (__wasi_timestamp_t)1000);
	__wasi_bool_t repeat = (new->it_interval.tv_sec != 0 || new->it_interval.tv_usec != 0)
		? __WASI_BOOL_TRUE : __WASI_BOOL_FALSE;

	__wasi_signal_t sig;
	switch (which) {
		case ITIMER_VIRTUAL: sig = (__wasi_signal_t)__WASI_SIGNAL_VTALRM; break;
		case ITIMER_PROF:    sig = (__wasi_signal_t)__WASI_SIGNAL_PROF;   break;
		case ITIMER_REAL:
		default:             sig = (__wasi_signal_t)__WASI_SIGNAL_ALRM;   break;
	}

	/* POSIX: report the PREVIOUS timer state in `old`. We do not track the
	 * remaining time in the guest, so report a disarmed timer (zeroed) — the
	 * same posture as before this fix (it left `old` untouched). `alarm()`'s
	 * return value (seconds left on a prior alarm) is therefore 0; matching
	 * the pre-existing behavior. */
	if (old) {
		old->it_interval.tv_sec = 0;
		old->it_interval.tv_usec = 0;
		old->it_value.tv_sec = 0;
		old->it_value.tv_usec = 0;
	}

	int ret = __wasi_proc_raise_interval(sig, ts, repeat);
	if (ret != 0) {
		errno = ret;
		return -1;
	}
	return 0;
#endif
}
