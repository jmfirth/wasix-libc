#include "pthread_impl.h"

#ifndef __wasilibc_unmodified_upstream
#include <common/clock.h>
#endif

int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t clk)
{
#ifdef __wasilibc_unmodified_upstream
	if (clk < 0 || clk-2U < 2) return EINVAL;
#else
	__wasi_clockid_t id;
	if (!__wasilibc_clockid_from_any_checked((uintptr_t)clk, &id) ||
	    (id != __WASI_CLOCKID_REALTIME && id != __WASI_CLOCKID_MONOTONIC))
		return EINVAL;
	clk = id;
#endif
	a->__attr &= 0x80000000;
	a->__attr |= clk;
	return 0;
}
