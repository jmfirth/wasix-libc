#ifndef _PTHREAD_IMPL_H
#define _PTHREAD_IMPL_H

#include <pthread.h>
#ifdef __wasilibc_unmodified_upstream
#include <signal.h>
#else
/* WASIX needs signal.h here for _NSIG (used in struct pthread's
 * per-thread blocked sigmask field added for issue #24). */
#include <signal.h>
#endif
#include <errno.h>
#include <limits.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/mman.h>
#endif
#include "libc.h"
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#include "atomic.h"
#include "futex.h"

#include "pthread_arch.h"
#include <wasi/libc.h>

#define pthread __pthread

struct pthread {
	/* Part 1 -- these fields may be external or
	 * internal (accessed via asm) ABI. Do not change. */
	struct pthread *self;
#ifdef __wasilibc_unmodified_upstream
#ifndef TLS_ABOVE_TP
	uintptr_t *dtv;
#endif
#endif
	struct pthread *prev, *next; /* non-ABI */
	uintptr_t sysinfo;
#ifndef TLS_ABOVE_TP
#ifdef CANARY_PAD
	uintptr_t canary_pad;
#endif
	uintptr_t canary;
#endif

	/* Part 2 -- implementation details, non-ABI. */
	int tid;
	int errno_val;
	volatile int detach_state;
	volatile int cancel;
	volatile unsigned char canceldisable, cancelasync;
	unsigned char tsd_used:1;
	unsigned char dlerror_flag:1;
	unsigned char *map_base;
	size_t map_size;
	void *stack;
	size_t stack_size;
	size_t guard_size;
	void *result;
	struct __ptcb *cancelbuf;
	/* Align the tsd field — and therefore the entire struct pthread —
	 * to 8 bytes. This forces _Alignof(struct pthread) to 8, which
	 * propagates to aligned_alloc() in __wasi_init_tp / __pthread_create
	 * so every pthread struct base address is 8-aligned. The downstream
	 * effect: fields like pending_sigs[] (used in atomic cmpxchg via
	 * a_or / a_swap in __wasm_signal / __wasm_drain_pending_sigs) end
	 * up at addresses that satisfy V8's strict atomic-alignment check
	 * on shared memory. Without this, browser-side fork+pipeline
	 * (e.g. `echo X | grep X` in bash) traps with
	 * "operation does not support unaligned accesses" inside the
	 * forked child's signal-dispatch path. See issue #234. */
	void **tsd __attribute__((aligned(8)));
	struct {
		volatile void *volatile head;
		long off;
		volatile void *volatile pending;
	} robust_list;
	int h_errno_val;
	volatile int timer_id;
	locale_t locale;
	volatile int killlock[1];
	char *dlerror_buf;
	void *stdio_locks;
#ifndef __wasilibc_unmodified_upstream
	/* Per-thread blocked signal mask, consulted by __wasm_signal to
	 * decide whether to dispatch or enqueue. sigset_t on WASIX is
	 * sized from _NSIG via musl; `blocked[0]` bit N represents
	 * signal N+1 being blocked. Written by pthread_sigmask. */
	unsigned long blocked_sigmask[_NSIG/(8*sizeof(long))];
	/* Per-thread pending signal bitmask. When __wasm_signal on THIS
	 * thread receives a blocked signal, the bit (sig-1) is set here.
	 * pthread_sigmask on SIG_UNBLOCK drains this per-thread bitmask,
	 * re-raising only signals that were pended on THIS thread. This
	 * avoids a cross-thread coalescing bug when the process-wide
	 * bitmask was drained by whichever thread ran UNBLOCK first. */
	volatile int pending_sigs[((_NSIG + 31) / 32)];
	/* Signal delivery counter — incremented by __wasm_signal after it
	 * calls a handler. sigsuspend(2) samples this before entering the
	 * wait loop and polls for a change, so it can return -1/EINTR
	 * once a handler has run. volatile int so __futexwait / __wake see
	 * a consistent address. */
	volatile int sigsuspend_tick;
	/* firebox#456 Phase 9 — per-thread held-lock array for the
	 * sweep-wake fix to the wasi-libc __pthread_exit asyncify-escape
	 * orphan-futex class.
	 *
	 * Background: the worker-thread teardown chain in __pthread_exit
	 * goes through Binaryen-asyncify-instrumented basic blocks gated
	 * on global.get __asyncify_state == 1 (Unwinding). A
	 * guest-managed asyncify cycle (e.g. Ruby's rb_wasm_rt_start
	 * Fiber driver) can land a rewound continuation that runs
	 * __lock(acquire) but skips the matching __unlock(release) — the
	 * lock-word futex is left at the locked-contended state (= 2 in
	 * musl's encoding) with no matching __wake ever fired. A
	 * subsequent acquirer's __futexwait blocks forever; the host's
	 * AsyncifyPoller never re-polls because no wake notification ever
	 * arrives. Witnesses: firebox#456 (Ruby Thread#join), #444
	 * residual (Python proc-macro side-module M5.2), #380 H-δ.1 (Go).
	 *
	 * Sweep-wake fix shape: every successful __lock acquisition
	 * records the lock-word address in this array; every __unlock
	 * release removes it. __pthread_exit walks the array just before
	 * __wasi_thread_exit and emits __wake(addr, INT_MAX, 1) for every
	 * remaining entry — closing the orphan regardless of which side
	 * of the asyncify state machine the teardown actually executed.
	 *
	 * 16 slots is empirically ample — the deepest measured __lock
	 * nesting in any cluster witness (Ruby gem install, Python
	 * proc-macro side-module, Go #380 H-δ.1) is 3. Overflow on
	 * registration is silently ignored (the lock still operates
	 * correctly; the sweep-wake guarantee weakens to "best effort
	 * for the first 16 currently-held locks"), strictly better than
	 * the pre-fix baseline of zero coverage. `_overflow` is a
	 * monotonic counter for post-hoc inspection; if it ever becomes
	 * non-zero in production traces, lift the cap.
	 *
	 * See class_lesson_thread_teardown_via_guest_asyncify_escape.md
	 * and work/tasks/459-pthread-exit-wake-emit-fix/README.md. */
#define __FIREBOX_HELD_LOCKS_CAP 16
	volatile int *firebox_held_locks[__FIREBOX_HELD_LOCKS_CAP];
	unsigned firebox_held_locks_count;
	unsigned firebox_held_locks_overflow;
#endif

	/* Part 3 -- the positions of these fields relative to
	 * the end of the structure is external and internal ABI. */
#ifdef TLS_ABOVE_TP
	uintptr_t canary;
	uintptr_t *dtv;
#endif
};

enum {
	DT_EXITED = 0,
	DT_EXITING,
	DT_JOINABLE,
	DT_DETACHED,
};

#define __SU (sizeof(size_t)/sizeof(int))

#define _a_stacksize __u.__s[0]
#define _a_guardsize __u.__s[1]
#define _a_stackaddr __u.__s[2]
#define _a_detach __u.__i[3*__SU+0]
#define _a_sched __u.__i[3*__SU+1]
#define _a_policy __u.__i[3*__SU+2]
#define _a_prio __u.__i[3*__SU+3]
#define _m_type __u.__i[0]
#define _m_lock __u.__vi[1]
#define _m_waiters __u.__vi[2]
#define _m_prev __u.__p[3]
#define _m_next __u.__p[4]
#define _m_count __u.__i[5]
#define _c_shared __u.__p[0]
#define _c_seq __u.__vi[2]
#define _c_waiters __u.__vi[3]
#define _c_clock __u.__i[4]
#define _c_lock __u.__vi[8]
#define _c_head __u.__p[1]
#define _c_tail __u.__p[5]
#define _rw_lock __u.__vi[0]
#define _rw_waiters __u.__vi[1]
#define _rw_shared __u.__i[2]
#define _b_lock __u.__vi[0]
#define _b_waiters __u.__vi[1]
#define _b_limit __u.__i[2]
#define _b_count __u.__vi[3]
#define _b_waiters2 __u.__vi[4]
#define _b_inst __u.__p[3]

#ifndef TP_OFFSET
#define TP_OFFSET 0
#endif

#ifndef DTP_OFFSET
#define DTP_OFFSET 0
#endif

#ifdef TLS_ABOVE_TP
#define TP_ADJ(p) ((char *)(p) + sizeof(struct pthread) + TP_OFFSET)
#define __pthread_self() ((pthread_t)(__get_tp() - sizeof(struct __pthread) - TP_OFFSET))
#else
#define TP_ADJ(p) (p)
#define __pthread_self() ((pthread_t)__get_tp())
#endif

#ifndef tls_mod_off_t
#define tls_mod_off_t size_t
#endif

#define SIGTIMER 32
#define SIGCANCEL 33
#define SIGSYNCCALL 34

#define SIGALL_SET ((sigset_t *)(const unsigned long long [2]){ -1,-1 })
#define SIGPT_SET \
	((sigset_t *)(const unsigned long [_NSIG/8/sizeof(long)]){ \
	[sizeof(long)==4] = 3UL<<(32*(sizeof(long)>4)) })
#define SIGTIMER_SET \
	((sigset_t *)(const unsigned long [_NSIG/8/sizeof(long)]){ \
	 0x80000000 })

void *__tls_get_addr(tls_mod_off_t *);
hidden int __init_tp(void *);
hidden void *__copy_tls(unsigned char *);
hidden void __reset_tls();

hidden void __membarrier_init(void);
hidden void __dl_thread_cleanup(void);
hidden void __testcancel();
hidden void __do_cleanup_push(struct __ptcb *);
hidden void __do_cleanup_pop(struct __ptcb *);
hidden void __pthread_tsd_run_dtors();

hidden void __pthread_key_delete_synccall(void (*)(void *), void *);
hidden int __pthread_key_delete_impl(pthread_key_t);

extern hidden volatile size_t __pthread_tsd_size;
extern hidden void *__pthread_tsd_main[];
extern hidden volatile int __eintr_valid_flag;

#if defined(__wasilibc_unmodified_upstream) || !defined(__wasm_exception_handling__)
hidden int __clone(int (*)(void *), void *, int, void *, ...);
#endif
hidden int __set_thread_area(void *);
#ifdef __wasilibc_unmodified_upstream /* WASI has no sigaction */
hidden int __libc_sigaction(int, const struct sigaction *, struct sigaction *);
#endif
hidden void __unmapself(void *, size_t);

#ifndef __wasilibc_unmodified_upstream
hidden int __wasilibc_futex_wait(volatile void *, int, int, int64_t);
#endif
hidden int __timedwait(volatile int *, int, clockid_t, const struct timespec *, int);
hidden int __timedwait_cp(volatile int *, int, clockid_t, const struct timespec *, int);
hidden void __wait(volatile int *, volatile int *, int, int);
static inline void __wake(volatile void *addr, int cnt, int priv)
{
	if (priv) priv = FUTEX_PRIVATE;
	if (cnt<0) cnt = INT_MAX;
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_futex, addr, FUTEX_WAKE|priv, cnt) != -ENOSYS ||
	__syscall(SYS_futex, addr, FUTEX_WAKE, cnt);
#else
	__wasilibc_futex_wake_wasix((int*)addr, cnt);
	//__builtin_wasm_memory_atomic_notify((int*)addr, cnt);
#endif
}
static inline void __futexwait(volatile void *addr, int val, int priv)
{
#ifdef __wasilibc_unmodified_upstream
	if (priv) priv = FUTEX_PRIVATE;
	__syscall(SYS_futex, addr, FUTEX_WAIT|priv, val, 0) != -ENOSYS ||
	__syscall(SYS_futex, addr, FUTEX_WAIT, val, 0);
#else
	__wait(addr, NULL, val, priv);
#endif
}

hidden void __acquire_ptc(void);
hidden void __release_ptc(void);
hidden void __inhibit_ptc(void);

hidden void __tl_lock(void);
hidden void __tl_unlock(void);
hidden void __tl_sync(pthread_t);

hidden void __vm_wait(void);
hidden void __vm_lock(void);
hidden void __vm_unlock(void);

#ifndef __wasilibc_unmodified_upstream
/* firebox#470/#472 — targeted per-export-boundary sweep-wake helper for
 * the asyncify-rewind orphan class (see __lock.c for full rationale).
 *
 * Callers: any libc helper that LOCK/UNLOCKs a process-wide static
 * lock around an indirect callback that may asyncify-rewind past the
 * UNLOCK position. Today: __funcs_on_exit (atexit lock) and
 * __fork_handler (atfork lock). Call this AFTER the normal UNLOCK at
 * the function's export boundary; healthy paths are no-ops, escape
 * paths force-clear + wake the orphan. */
hidden void __firebox_lock_sweep_wake_one(volatile int *l);

/* firebox#473 / #489 — register/deregister a lock-word address into
 * the calling thread's held-lock array. Exposed as hidden symbols so
 * lock primitives outside __lock.c can participate in the Phase 9
 * __pthread_exit sweep-wake without needing a parallel held-list.
 *
 * Registered participants (extends the class lesson
 * [[thread_teardown_via_guest_asyncify_escape]]):
 *   1. __lock.c          — generic __lock/__unlock (atexit, atfork,
 *                          malloc, thread-list — every LOCK()-macro
 *                          caller).         [#456 Phase 9]
 *   2. stdio/__lockfile.c — per-FILE f->lock (fclose, fflush, fread,
 *                          __fseeko, ftell, fwrite, vfprintf — all 7
 *                          callers via FLOCK macros).    [#473]
 *   3. pthread_cond_timedwait.c — static-inline lock()/unlock()
 *                          protecting pthread_cond_t._c_lock AND
 *                          per-waiter node.barrier.  Diagnosed via
 *                          Phase 4 wasm-memory snapshot pinpointing
 *                          cargo's std::sync::Condvar wedge.   [#489]
 *
 * Same shape, same sweep, same orphan-protection guarantee. */
hidden void __firebox_register_held_lock(volatile int *l);
hidden void __firebox_deregister_held_lock(volatile int *l);

/* firebox#811 Phase 2 — thin wrappers over the host futex_register_held /
 * futex_deregister_held syscalls (defined in the #800 overlay
 * libc-bottom-half/sources/__wasixlibc_firebox.c). Called from the shared
 * __firebox_{register,deregister}_held_lock helpers in __lock.c so the
 * musl lock family (cv-internal lock, __lock, __lockfile) also routes to
 * the HOST held-list that #811 Phase 1's thread-exit sweep covers. */
hidden void __firebox_host_register_held(volatile int *l);
hidden void __firebox_host_deregister_held(volatile int *l);
#endif

extern hidden volatile int __thread_list_lock;

extern hidden volatile int __abort_lock[1];

extern hidden unsigned __default_stacksize;
extern hidden unsigned __default_guardsize;

#define DEFAULT_STACK_SIZE 131072
#ifdef __wasilibc_unmodified_upstream
#define DEFAULT_GUARD_SIZE 8192
#else
#define DEFAULT_GUARD_SIZE 4096
#endif

#define DEFAULT_STACK_MAX (8<<20)
#define DEFAULT_GUARD_MAX (1<<20)

#define __ATTRP_C11_THREAD ((void*)(uintptr_t)-1)

#endif
