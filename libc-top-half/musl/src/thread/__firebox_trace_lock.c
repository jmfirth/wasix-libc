/* firebox#489 Phase 4a — wasix-libc lock-acquire/release probe.
 *
 * Investigation-only instrumentation that emits one host-visible event
 * per lock acquire/release inside wasix-libc.  The probe routes through
 * the EXISTING wasix `futex_wake` import which is already host-traced by
 * the FIREBOX_TRACE=futex bit (`firebox#484` Phase 3) — no new wasix
 * import surface, no new host syscall, no new subsystem bit required.
 *
 * Wire shape (paired with firebox#489 Phase 4b host-side change):
 *   1. Each thread owns a 16-byte TLS scratch buffer.
 *   2. __firebox_trace_lock(op, addr, owner_tid, kind) writes:
 *        word0 = LOCK_SENTINEL_TAG | op
 *        word1 = lock address (the wasm pointer the libc primitive is
 *                operating on — &mutex->_lock or &file->lock or similar)
 *        word2 = owner thread id (__pthread_self()->tid)
 *        word3 = kind tag (LOCK_KIND_LOCK | LOCK_KIND_FILE)
 *      into the TLS scratch buffer.
 *   3. __wasi_futex_wake(&scratch[0]) is invoked.  No waiter ever
 *      registers on the scratch address, so this generates a host-side
 *      `futex_wake/miss` trace event with `futex_idx=<scratch addr>`.
 *   4. The Phase 4b host change reads 4 u32s at `futex_idx` whenever
 *      futex_wait/register OR futex_wake fires, so the same trace
 *      machinery captures (op, addr, owner_tid, kind) from our scratch
 *      for free.
 *
 * Why this shape:
 *   - ZERO new wasix import surface — uses an existing call.
 *   - ZERO new subsystem bit on the host side — piggybacks on FUTEX.
 *   - Per-thread scratch so the probe is thread-safe without locks
 *     (locks-inside-lock-probe would deadlock).
 *   - Scratch lives at a stable address inside the pthread struct's
 *     TLS arena, so the host's `futex_idx=<addr>` is recognizable: it
 *     matches the sentinel tag in word0 even when read out of order
 *     across the trace.
 *
 * Disable behavior:
 *   - At runtime: gated on __firebox_trace_lock_enabled (set by
 *     __libc_start_main from FIREBOX_TRACE_LOCK env var presence — but
 *     for this probe build, gated on `getenv("FIREBOX_TRACE_LOCK") != 0`
 *     would require a libc-side getenv which is heavy.  Simpler: gate on
 *     "always on in this probe build, opt-out via never linking the
 *     instrumented variants".  This build ships always-on; the host
 *     side gates on FIREBOX_TRACE=futex, so unless the env var is set
 *     the events are emitted but discarded at the host layer (~3 ns
 *     per event for the disabled-path).
 *
 * Eviction / retirement: after the Phase 4 investigation completes, the
 * probe can be dropped wholesale.  No other code depends on this file.
 * See work/tasks/489-*.
 */

#include "pthread_impl.h"
#include <wasi/api.h>
#include <stdint.h>

/* Per-thread 16-byte scratch.  Aligned for atomic 32-bit reads from
 * the host side.  4 words: [op, addr, owner_tid, kind]. */
static _Thread_local volatile uint32_t __firebox_trace_lock_scratch[4]
    __attribute__((aligned(16))) = {0, 0, 0, 0};

/* Sentinel tag in word0 high bits, so post-processing can recognize a
 * Phase 4a trace event vs an unrelated futex_wake/miss.  The lower bits
 * encode the operation enum.  Tag value chosen to be visually distinct
 * in trace logs and unlikely to collide with real lock-word values
 * (musl's __lock uses INT_MIN | count; __lockfile uses tid|flags). */
#define LOCK_SENTINEL_TAG  0xFEBE0000u

/* Operation enum — encoded in low bits of scratch[0]. */
#define LOCK_OP_ACQUIRE    0x01u
#define LOCK_OP_RELEASE    0x02u
#define LOCK_OP_CONTENDED  0x03u  /* fast-path failed, going into __futexwait */

/* Kind enum — written into scratch[3] to distinguish which libc
 * subsystem owns this lock primitive. */
#define LOCK_KIND_LOCK     1u  /* musl __lock / __unlock (lock.c) */
#define LOCK_KIND_FILE     2u  /* stdio __lockfile / __unlockfile */
#define LOCK_KIND_DLOPEN   3u  /* dlopen/dlclose/dlsym registry mutex */
#define LOCK_KIND_MALLOC   4u  /* malloc arena */

/* Emit one trace event.  Called only from probe wrappers below;
 * INTENTIONALLY not exposed in pthread_impl.h to keep the call surface
 * narrow. */
static inline void __firebox_trace_lock_emit(uint32_t op,
                                              uintptr_t addr,
                                              uint32_t owner_tid,
                                              uint32_t kind)
{
    __firebox_trace_lock_scratch[0] = LOCK_SENTINEL_TAG | op;
    __firebox_trace_lock_scratch[1] = (uint32_t)addr;
    __firebox_trace_lock_scratch[2] = owner_tid;
    __firebox_trace_lock_scratch[3] = kind;
    /* Compiler barrier so the host sees all 4 stores before the wake. */
    __atomic_thread_fence(__ATOMIC_RELEASE);

    __wasi_bool_t woken = 0;
    /* Cast away volatile for the syscall; we're not racing on this
     * address (per-thread scratch).  We don't care about errno or
     * `woken` — no real waiter exists; we're using the host-side
     * futex_wake trace as a one-way notification channel. */
    (void)__wasi_futex_wake((uint32_t *)(uintptr_t)&__firebox_trace_lock_scratch[0],
                             &woken);
}

/* Public probe entry-points.  Hidden so other libc TUs can call them
 * without polluting the user-visible symbol table.  Called from
 * __lock.c / __lockfile.c instrumentation points. */
hidden void __firebox_trace_lock_acquire(uintptr_t addr, uint32_t kind)
{
    pthread_t self = __pthread_self();
    __firebox_trace_lock_emit(LOCK_OP_ACQUIRE, addr, (uint32_t)self->tid, kind);
}

hidden void __firebox_trace_lock_release(uintptr_t addr, uint32_t kind)
{
    pthread_t self = __pthread_self();
    __firebox_trace_lock_emit(LOCK_OP_RELEASE, addr, (uint32_t)self->tid, kind);
}

hidden void __firebox_trace_lock_contended(uintptr_t addr, uint32_t kind)
{
    pthread_t self = __pthread_self();
    __firebox_trace_lock_emit(LOCK_OP_CONTENDED, addr, (uint32_t)self->tid, kind);
}
