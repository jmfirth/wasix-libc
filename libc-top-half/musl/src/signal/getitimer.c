#include <sys/time.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif

int getitimer(int which, struct itimerval *old)
{
#ifdef __wasilibc_unmodified_upstream
	if (sizeof(time_t) > sizeof(long)) {
		long old32[4];
		int r = __syscall(SYS_getitimer, which, old32);
		if (!r) {
			old->it_interval.tv_sec = old32[0];
			old->it_interval.tv_usec = old32[1];
			old->it_value.tv_sec = old32[2];
			old->it_value.tv_usec = old32[3];
		}
		return __syscall_ret(r);
	}
	return syscall(SYS_getitimer, which, old);
#else
	/* firebox#KZ0 — query the host's authoritative interval-timer state.
	 *
	 * The host is the single source of truth: setitimer() arms the timer via
	 * proc_raise_interval, and proc_fork gives the child a fresh WasiProcess
	 * whose interval map is empty — so in a forked child every timer reads
	 * back disarmed (it_value == 0), exactly POSIX "interval timers are reset
	 * in the child" (Open POSIX fork/13-1). The guest keeps NO timer state of
	 * its own, so there is nothing to re-zero at the fork boundary.
	 *
	 * Map `which` to the SAME WASI signal setitimer arms (REAL→ALRM,
	 * VIRTUAL→VTALRM, PROF→PROF). Use the faithful POSIX error convention
	 * (return -1 + errno), unlike the prior stub which returned a bare EINVAL. */
	__wasi_signal_t sig;
	switch (which) {
		case ITIMER_REAL:    sig = (__wasi_signal_t)__WASI_SIGNAL_ALRM;   break;
		case ITIMER_VIRTUAL: sig = (__wasi_signal_t)__WASI_SIGNAL_VTALRM; break;
		case ITIMER_PROF:    sig = (__wasi_signal_t)__WASI_SIGNAL_PROF;   break;
		default: errno = EINVAL; return -1;
	}
	if (!old) {
		errno = EFAULT;
		return -1;
	}
	/* Flat {value.sec, value.usec, interval.sec, interval.usec} buffer — the
	 * ABI the itimer_get host handler writes. */
	uint64_t buf[4] = {0, 0, 0, 0};
	__wasi_errno_t e = __wasix_itimer_get((uint32_t)sig, buf);
	if (e != 0) {
		errno = (int)e;
		return -1;
	}
	old->it_value.tv_sec     = (time_t)buf[0];
	old->it_value.tv_usec    = (suseconds_t)buf[1];
	old->it_interval.tv_sec  = (time_t)buf[2];
	old->it_interval.tv_usec = (suseconds_t)buf[3];
	return 0;
#endif
}
