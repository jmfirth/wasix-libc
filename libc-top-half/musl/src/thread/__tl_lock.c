/*
 * firebox R7 (Ruby M2): split __tl_lock / __tl_unlock / __tl_sync out
 * of pthread_create.c into their own translation unit.
 *
 * Background — the bug this fixes
 * ===============================
 * On non-threaded wasm32-wasi targets, linking a program that calls
 * fork() failed with:
 *
 *     wasm-ld: error: libc.a(wasi_thread_start.o): undefined symbol:
 *              __wasm_init_tls
 *
 * The trace showed this dependency chain:
 *
 *     fork.o           -> __tl_lock         (undefined reference)
 *     pthread_create.o -> __tl_lock         (strong definition)
 *                      -> wasi_thread_start (via __dummy_reference,
 *                                            forced by pthread_create.c
 *                                            line ~312 to keep the
 *                                            wasm-export-only symbol
 *                                            from being DCE'd)
 *     wasi_thread_start.o -> __wasm_init_tls  (compiler-synthesized
 *                                              global, only defined
 *                                              when wasm-ld sees
 *                                              -pthread / shared-memory)
 *
 * fork.c originally had a weak `dummy_0` alias for __tl_lock to break
 * this cycle (musl's design — see synccall.c, pthread_key_create.c for
 * the same pattern), but it was gated behind
 * `#ifdef __wasilibc_unmodified_upstream` and therefore disabled on
 * the wasix-libc patched build. Even restoring the weak alias does
 * not break the cycle: wasm-ld's archive resolution still pulls the
 * pthread_create.o strong definition because of how wasm-ld scans
 * archive members for strong global symbols that match references in
 * other archive members.
 *
 * The architecturally correct fix is to move __tl_lock / __tl_unlock
 * / __tl_sync (and their shared state, tl_lock_count + tl_lock_waiters)
 * out of pthread_create.c entirely. Then:
 *
 *   * Non-threaded build:   fork.o needs __tl_lock → wasm-ld pulls in
 *                           this small TU (tl_lock.o). pthread_create.o
 *                           is not pulled in. wasi_thread_start.o is not
 *                           pulled in. __wasm_init_tls is never
 *                           referenced. fork() links cleanly and calls
 *                           into _Fork() / proc_fork().
 *
 *   * Threaded build:       User code calls pthread_create() → wasm-ld
 *                           pulls in pthread_create.o for pthread_create
 *                           itself; pthread_create.o still references
 *                           __tl_lock, but it's already satisfied by
 *                           tl_lock.o. wasi_thread_start.o is pulled in
 *                           because of pthread_create.o's __dummy_reference,
 *                           and __wasm_init_tls is provided by the linker
 *                           (because -pthread is on). Same behaviour as
 *                           before for threaded programs.
 *
 * The implementation below is byte-identical to the previous definitions
 * in pthread_create.c — just relocated.
 *
 * Consumer-visible impact
 * =======================
 * Ruby (M2 R7), Perl, Python, and any other static wasm32-wasi consumer
 * that calls fork() / posix_spawn() now correctly detects HAVE_FORK at
 * configure time, populates Process.spawn / IO.popen / Kernel#system,
 * etc.
 */

#include "pthread_impl.h"

/* The thread-list lock state. tl_lock_count is a per-thread reentrance
 * counter; tl_lock_waiters tracks how many threads are blocked. Both
 * are referenced only by the three functions below — they migrate
 * cleanly out of pthread_create.c without any other change. */
static int tl_lock_count;
static int tl_lock_waiters;

void __tl_lock(void)
{
	int tid = __pthread_self()->tid;
	int val = __thread_list_lock;
	if (val == tid) {
		tl_lock_count++;
		return;
	}
	while ((val = a_cas(&__thread_list_lock, 0, tid)))
		__wait(&__thread_list_lock, &tl_lock_waiters, val, 0);
}

void __tl_unlock(void)
{
	if (tl_lock_count) {
		tl_lock_count--;
		return;
	}
	a_store(&__thread_list_lock, 0);
	if (tl_lock_waiters) __wake(&__thread_list_lock, 1, 0);
}

void __tl_sync(pthread_t td)
{
	a_barrier();
	int val = __thread_list_lock;
	if (!val) return;
	__wait(&__thread_list_lock, &tl_lock_waiters, val, 0);
	if (tl_lock_waiters) __wake(&__thread_list_lock, 1, 0);
}
