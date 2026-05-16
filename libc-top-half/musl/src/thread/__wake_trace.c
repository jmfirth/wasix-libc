/*
 * Firebox #384: out-of-line, traced __wake.
 *
 * Compiled into libc.a only when __FIREBOX_TRACE_MUSL_LOCK__ is defined
 * at libc build time. The trace stream from this TU is the "wake side"
 * of the per-lock event correlation against __wait / __lock / __tl_lock.
 *
 * When __FIREBOX_TRACE_MUSL_LOCK__ is NOT defined, this entire TU
 * compiles to nothing (the #ifdef wraps the whole function body and
 * declaration mirrors the static inline in pthread_impl.h).
 */
#include "pthread_impl.h"
#include "firebox_lock_trace.h"

#ifdef __FIREBOX_TRACE_MUSL_LOCK__

hidden void __wake(volatile void *addr, int cnt, int priv)
{
	FIREBOX_LOCK_TRACE("wake_enter", "__wake", addr, cnt);
	if (priv) priv = FUTEX_PRIVATE;
	if (cnt<0) cnt = INT_MAX;
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_futex, addr, FUTEX_WAKE|priv, cnt) != -ENOSYS ||
	__syscall(SYS_futex, addr, FUTEX_WAKE, cnt);
#else
	__wasilibc_futex_wake_wasix((int*)addr, cnt);
	//__builtin_wasm_memory_atomic_notify((int*)addr, cnt);
#endif
	FIREBOX_LOCK_TRACE("wake_exit", "__wake", addr, cnt);
}

#endif /* __FIREBOX_TRACE_MUSL_LOCK__ */
