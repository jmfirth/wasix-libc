#include "stdio_impl.h"
#include "pthread_impl.h"

int __lockfile(FILE *f)
{
	int owner = f->lock, tid = __pthread_self()->tid;
	if ((owner & ~MAYBE_WAITERS) == tid)
		return 0;
	owner = a_cas(&f->lock, 0, tid);
#ifndef __wasilibc_unmodified_upstream
	/* firebox#473: register the lock-word address into the calling
	 * thread's held-lock array on successful acquire (both the fast
	 * CAS path below and the contended loop path further down). The
	 * Phase 9 __pthread_exit sweep walks this array and __wakes every
	 * remaining entry; participating means any FILE-lock orphaned by
	 * an asyncify-rewound stdio call (fclose, fflush, fread, __fseeko,
	 * ftell, fwrite, vfprintf — all 7 callers of __lockfile use the
	 * FLOCK macro family) gets transparently woken at thread teardown
	 * with no per-caller change. The pointer is stable: &f->lock is
	 * inside the FILE struct, which outlives any individual stdio
	 * call. Worst case (FILE freed before sweep): the address is dead
	 * memory but Phase 9 just reads `addr` and emits __wake(addr,...)
	 * which is harmless on freed memory in the asyncify class (no
	 * dereference, just an opaque host-side wake). See work/tasks/473-*
	 * and class_lesson_thread_teardown_via_guest_asyncify_escape. */
	if (!owner) {
		__firebox_register_held_lock(&f->lock);
		/* firebox#489 Phase 4a: probe fast-path file-lock acquire (kind=2 FILE) */
		__firebox_trace_lock_acquire((uintptr_t)&f->lock, 2u);
		return 1;
	}
#else
	if (!owner) return 1;
#endif
	while ((owner = a_cas(&f->lock, 0, tid|MAYBE_WAITERS))) {
		if ((owner & MAYBE_WAITERS) ||
		    a_cas(&f->lock, owner, owner|MAYBE_WAITERS)==owner) {
#ifndef __wasilibc_unmodified_upstream
			/* firebox#489 Phase 4a: probe file-lock contended wait (kind=2 FILE) */
			__firebox_trace_lock_contended((uintptr_t)&f->lock, 2u);
#endif
			__futexwait(&f->lock, owner|MAYBE_WAITERS, 1);
		}
	}
#ifndef __wasilibc_unmodified_upstream
	__firebox_register_held_lock(&f->lock);
	/* firebox#489 Phase 4a: probe contended-loop file-lock acquire (kind=2 FILE) */
	__firebox_trace_lock_acquire((uintptr_t)&f->lock, 2u);
#endif
	return 1;
}

void __unlockfile(FILE *f)
{
#ifndef __wasilibc_unmodified_upstream
	/* firebox#473: deregister BEFORE the a_swap release-to-waiters,
	 * same ordering rationale as __unlock in __lock.c — a concurrent
	 * __pthread_exit sweep on this thread must not observe the lock
	 * as on-our-held-list AFTER it has been released to other
	 * threads, or we'd emit a spurious wake on an address some other
	 * stdio caller now legitimately holds. Note we deregister
	 * unconditionally (regardless of MAYBE_WAITERS): the held-list
	 * entry exists for any acquire that returned 1 from __lockfile,
	 * regardless of whether waiters arrived later. */
	__firebox_deregister_held_lock(&f->lock);
	/* firebox#489 Phase 4a: probe file-lock release (kind=2 FILE) */
	__firebox_trace_lock_release((uintptr_t)&f->lock, 2u);
#endif
	if (a_swap(&f->lock, 0) & MAYBE_WAITERS)
		__wake(&f->lock, 1, 1);
}
