#include "pthread_impl.h"
#include "firebox_lock_trace.h"
#ifndef __wasilibc_unmodified_upstream
#include "assert.h"
#endif

/* Firebox #398: pthread_impl.h installs a `__wait` function-style macro
 * when __FIREBOX_TRACE_MUSL_LOCK__ is set, which would otherwise rewrite
 * this TU's own definition. Undefine after the header to recover the
 * bare function symbol. */
#ifdef __FIREBOX_TRACE_MUSL_LOCK__
#undef __wait
#endif

#ifndef __wasilibc_unmodified_upstream
// Use WebAssembly's `wait` instruction to implement a futex. Note that `op` is
// unused but retained as a parameter to match the original signature of the
// syscall and that, for `max_wait_ns`, -1 (or any negative number) means wait
// indefinitely.
//
// Adapted from Emscripten: see
// https://github.com/emscripten-core/emscripten/blob/058a9fff/system/lib/pthread/emscripten_futex_wait.c#L111-L150.
int __wasilibc_futex_wait(volatile void *addr, int op, int val, int64_t max_wait_ns)
{
    if ((((intptr_t)addr) & 3) != 0) {
        return -EINVAL;
    }

    int ret = __builtin_wasm_memory_atomic_wait32((int *)addr, val, max_wait_ns);

    // memory.atomic.wait32 returns:
    //   0 => "ok", woken by another agent.
    //   1 => "not-equal", loaded value != expected value
    //   2 => "timed-out", the timeout expired
    if (ret == 1) {
        return -EWOULDBLOCK;
    }
    if (ret == 2) {
        return -ETIMEDOUT;
    }
    assert(ret == 0);
    return 0;
}
#endif

void __wait(volatile int *addr, volatile int *waiters, int val, int priv)
{
	int spins=100;
	if (priv) priv = FUTEX_PRIVATE;
	/* Firebox #384: trace entry. Hypothesis-pinning data: which
	 * abstract lock is the source of the bash futex_wait{expected=2}
	 * hang. The (addr, val) pair here is what the futex_wait syscall
	 * will use; matching against the hang trace's futex_idx and
	 * expected value is a direct identification. */
	FIREBOX_LOCK_TRACE("wait_enter", "__wait", addr, val);
	while (spins-- && (!waiters || !*waiters)) {
		if (*addr==val) a_spin();
		else {
			FIREBOX_LOCK_TRACE("wait_exit", "__wait_spin_value_changed", addr, *addr);
			return;
		}
	}
	if (waiters) a_inc(waiters);
	while (*addr==val) {
#ifdef __wasilibc_unmodified_upstream
		__syscall(SYS_futex, addr, FUTEX_WAIT|priv, val, 0) != -ENOSYS
		|| __syscall(SYS_futex, addr, FUTEX_WAIT, val, 0);
#else
        // we feed the wait operation to a syscall rather than
        // use atomics so that the asyncify and journaling can
        // work properly
		FIREBOX_LOCK_TRACE("wait_futex_pre", "__wait_loop", addr, val);
		__wasilibc_futex_wait_wasix(addr, FUTEX_WAIT, val, -1);
		FIREBOX_LOCK_TRACE("wait_futex_post", "__wait_loop", addr, *addr);
#endif
	}
	if (waiters) a_dec(waiters);
	FIREBOX_LOCK_TRACE("wait_exit", "__wait_value_changed", addr, *addr);
}

#ifdef __FIREBOX_TRACE_MUSL_LOCK__
/* Firebox #398: caller-source-location-aware trampoline.
 *
 * Installed by pthread_impl.h's __wait / __futexwait macros under the
 * trace gate. Emits a `wait_caller` event carrying the caller's
 * __FILE__/__LINE__ BEFORE entering the bare __wait body. The bare
 * __wait body still emits its own wait_enter / wait_futex_pre /
 * wait_futex_post / wait_exit events; correlating them with the
 * preceding wait_caller event (same pid, same addr, same val) pins
 * the exact musl primitive that issued the wait.
 *
 * Cost: one extra trace line per __wait entry when active. The bare
 * __wait reference (after the #undef above) is the function symbol, so
 * this is one direct call. */
void __wait_traced(volatile int *addr, volatile int *waiters,
                   int val, int priv,
                   const char *caller_file, int caller_line)
{
    FIREBOX_LOCK_TRACE_CALLER("wait_caller", "__wait_traced",
                              addr, val, caller_file, caller_line);
    __wait(addr, waiters, val, priv);
}
#endif
