#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>  /* firebox#KKR — offsetof() for __fbx_blocked_off */
#include <string.h>
#include <sysexits.h>
#ifndef __wasilibc_unmodified_upstream
#include <wasi/api.h>
#endif
#include "syscall.h"
#include "pthread_impl.h"
#include "libc.h"
#include "lock.h"
#include "ksigaction.h"
#ifndef __wasilibc_unmodified_upstream
#include "atomic.h"
#include "firebox_altstack.h"  /* firebox#9PX — per-thread alt-stack TLS state */
#endif

static int unmask_done;

/* Per-signal "a REAL handler is installed" bitmask: bit (sig-1) set ⇔ the
 * guest called sigaction() with a handler that is neither SIG_DFL nor SIG_IGN.
 * Read by __wasm_signal's decision tree and copied out by __get_handler_set.
 *
 * firebox#8B5term: this was `static`. It is now a non-static, export-friendly
 * symbol so the FIREBOX RUNTIME can read the per-signal disposition WITHOUT a
 * guest call. The runtime's only prior handler flag (`signal_set`) is
 * universally true for any wasix-libc program — wasix-libc registers the
 * generic `__wasm_signal` dispatcher at libc init (callback_signal), before
 * main — so it cannot distinguish "SIG_DFL, no handler" (default-terminate)
 * from "user handler installed". The faithful disposition lives HERE, in this
 * bitmask. The threaded link `--export`s this symbol, which makes wasm-ld emit
 * an immutable wasm global holding its linear-memory address; the runtime reads
 * that global once at instance setup and reads the bitmask bytes straight from
 * the live MemoryView. No guest call is involved — critical, because the case
 * that needs the disposition is a thread SPINNING in JIT'd wasm that can never
 * be re-entered cooperatively. With the disposition visible, the runtime can
 * faithfully terminate (rc=128+signo) an undeliverable SIG_DFL default-terminate
 * signal (Linux behavior) instead of hanging forever, while LEAVING the
 * with-handler case alone (the bit is set → the deferred resume-half). */
unsigned long __fbx_handler_set[_NSIG/(8*sizeof(long))];

/* firebox#KKR — the host reads these two to SKIP a BLOCKED signal in its
 * no-handler default-terminate/stop routing (env.rs
 * first_no_handler_default_terminate): a blocked signal must PEND, not
 * terminate, but the host's __fbx_handler_set check alone cannot tell a blocked
 * no-handler signal from a deliverable one. The block mask is PER-THREAD —
 * blocked_sigmask lives in the heap-allocated struct pthread (NOT at tls_base),
 * so the host needs the draining thread's struct base + the field offset:
 *   __fbx_main_pthread — the MAIN thread's struct pthread base, published by
 *     __wasi_init_tp. A SPAWNED thread instead passes its base to the host via
 *     the thread-spawn args (start_args.pthread_self_ptr); only the main thread
 *     has no spawn args, hence this export.
 *   __fbx_blocked_off  — offsetof(struct pthread, blocked_sigmask), so the host
 *     carries no struct-layout knowledge (robust to field reordering).
 * Same posture as __fbx_handler_set: the threaded/edge link --export[-if-defined]s
 * them; the host reads the immutable global = the symbol's linear address, then
 * the bytes from the live MemoryView. Backward-compatible — a host predating the
 * read just ignores them; the guest imports NOTHING new, so wasm built with this
 * libc still instantiates on an older runtime (the fix is simply dormant). */
volatile uintptr_t __fbx_main_pthread;
const uint32_t __fbx_blocked_off = offsetof(struct pthread, blocked_sigmask);

#ifdef __wasilibc_unmodified_upstream
#else
static volatile int __eintr_callback_registered = 0;
static volatile struct k_sigaction __eintr_handler_callbacks[_NSIG];
/* Pure futex-protected lock guarding __eintr_handler_callbacks[].
 * MUST NEVER be touched by a_store() or raw writes — that conflates
 * the futex "has waiters" bit (1) with a "signals blocked" flag and
 * deadlocks under re-entrant __wasm_signal dispatch. See issue #24. */
volatile int __eintr_handler_lock[1];

/* Process-wide "signals currently blocked by __block_all_sigs" flag.
 * Separate from __eintr_handler_lock so the dispatcher can tell the
 * two apart. Written atomically by __block_all_sigs / __restore_sigs.
 * Read by __wasm_signal to decide whether to dispatch or enqueue. */
volatile int __wasm_signals_blocked = 0;

/* Process-wide pending-signal bitmask. Bit (sig-1) set means a signal
 * was raised during a blocked window and needs redelivery at
 * __restore_sigs time. Implemented as an array of ints for a_or /
 * a_and_l atomics; musl's sigset_t semantics (bit N-1) mirrored. */
#define __WASM_PENDING_WORDS ((_NSIG + 31) / 32)
volatile int __wasm_pending_sigs[__WASM_PENDING_WORDS];

/* SA_NODEFER in-handler recursion guards: per-signal depth counters.
 * Written only from within __wasm_signal, so no atomic needed — the
 * WASM runtime delivers signals serially to a single thread and we
 * guard re-entrant dispatch via __wasm_signals_blocked. */
static int __wasm_in_handler[_NSIG];

/* firebox signal-mask machinery — per-signal "a handler is currently
 * executing on SOME thread for this signal, and it was installed with
 * SA_NODEFER" flag. Distinct from __wasm_in_handler[] (which is only
 * maintained for the NON-NODEFER case, as its sole job is the
 * defer-recursion guard). __wasm_nodefer_active[] is maintained for
 * the SA_NODEFER case and read by raise()/pthread_kill() (via
 * __wasm_raise_self) to decide whether a self-raise of `sig` must be
 * dispatched SYNCHRONOUSLY in-guest.
 *
 * WHY a synchronous in-guest dispatch is required: POSIX SA_NODEFER
 * means the handled signal is NOT blocked while its handler runs, so a
 * raise() of that same signal from inside the handler must re-enter the
 * handler IMMEDIATELY (before raise() returns) — Linux delivers the
 * tkill on the syscall return path, nesting the handler synchronously.
 * Firebox's normal delivery path routes raise() → host
 * __wasi_thread_signal → host __wasm_signal, but the host refuses a
 * NESTED dispatch on a thread already in signal-dispatch (firebox#912's
 * `in_signal_dispatch` guard, which prevents the dispatcher's
 * __eintr_handler_lock-acquire syscalls from self-deadlocking) and
 * DEFERS the re-raise to the next syscall boundary. That defers — and
 * for a program that exits straight after the outer handler returns,
 * effectively drops — the synchronous re-entry SA_NODEFER mandates
 * (Open POSIX sigaction/22-*: handler must reenter while inside_handler
 * is still set). The faithful fix is to dispatch the re-raise in-guest,
 * synchronously, here: __wasm_signal releases __eintr_handler_lock
 * BEFORE calling the handler, so a nested __wasm_signal from the handler
 * body re-acquires it uncontended — it does NOT hit the #912 deadlock
 * window (which is specifically the futex_register_held syscall issued
 * from INSIDE the lock's own acquire). Serial single-thread delivery
 * makes file scope safe; a per-signal counter handles legitimate
 * SA_NODEFER recursion depth. */
static volatile int __wasm_nodefer_active[_NSIG];
#endif

void __get_handler_set(sigset_t *set)
{
	memcpy(set, __fbx_handler_set, sizeof __fbx_handler_set);
}

_Noreturn
static void core_handler(int sig) {
    fprintf(stderr, "Program recieved fatal signal: %s\n", strsignal(sig));
    abort();
}

_Noreturn
static void terminate_handler(int sig) {
    fprintf(stderr, "Program recieved termination signal: %s\n", strsignal(sig));
    abort();
}

_Noreturn
static void stop_handler(int sig) {
    fprintf(stderr, "Program recieved stop signal: %s\n", strsignal(sig));
    abort();
}

static void continue_handler(int sig) {
    // do nothing
}

#ifdef __wasilibc_unmodified_upstream
typedef void (*sighandler_t)(int);
static const sighandler_t default_handlers[_NSIG] = {
    // Default behavior: "core".
    [SIGABRT] = core_handler,
    [SIGBUS] = core_handler,
    [SIGFPE] = core_handler,
    [SIGILL] = core_handler,
#if SIGIOT != SIGABRT
    [SIGIOT] = core_handler,
#endif
    [SIGQUIT] = core_handler,
    [SIGSEGV] = core_handler,
    [SIGSYS] = core_handler,
    [SIGTRAP] = core_handler,
    [SIGXCPU] = core_handler,
    [SIGXFSZ] = core_handler,
#if defined(SIGUNUSED) && SIGUNUSED != SIGSYS
    [SIGUNUSED] = core_handler,
#endif

    // Default behavior: ignore.
    [SIGCHLD] = SIG_IGN,
#if defined(SIGCLD) && SIGCLD != SIGCHLD
    [SIGCLD] = SIG_IGN,
#endif
    [SIGURG] = SIG_IGN,
    [SIGWINCH] = SIG_IGN,

    // Default behavior: "continue".
    [SIGCONT] = continue_handler,

    // Default behavior: "stop".
    [SIGSTOP] = stop_handler,
    [SIGTSTP] = stop_handler,
    [SIGTTIN] = stop_handler,
    [SIGTTOU] = stop_handler,

    // Default behavior: "terminate".
    [SIGHUP] = terminate_handler,
    [SIGINT] = terminate_handler,
    [SIGKILL] = terminate_handler,
    [SIGUSR1] = terminate_handler,
    [SIGUSR2] = terminate_handler,
    [SIGPIPE] = terminate_handler,
    [SIGALRM] = terminate_handler,
    [SIGTERM] = terminate_handler,
    [SIGSTKFLT] = terminate_handler,
    [SIGVTALRM] = terminate_handler,
    [SIGPROF] = terminate_handler,
    [SIGIO] = terminate_handler,
#if SIGPOLL != SIGIO
    [SIGPOLL] = terminate_handler,
#endif
    [SIGPWR] = terminate_handler,
};
#else
typedef void (*sighandler_t)(int);
static sighandler_t default_handler = NULL;

static void __default_handler(int sig) {
	switch (sig) {
		// Default behavior: "core".
		case SIGABRT:
		case SIGBUS:
		case SIGFPE:
		case SIGILL:
	#if SIGIOT != SIGABRT
		case SIGIOT:
	#endif
		case SIGQUIT:
		case SIGSEGV:
		case SIGSYS:
		case SIGTRAP:
		case SIGXCPU:
		case SIGXFSZ:
	#if defined(SIGUNUSED) && SIGUNUSED != SIGSYS
		case SIGUNUSED:
	#endif
			core_handler(sig);
			break;

		// Default behavior: ignore.
		case SIGCHLD:
#if defined(SIGCLD) && SIGCLD != SIGCHLD
		case SIGCLD:
#endif
		case SIGURG:
		case SIGWINCH:
			/* SIG_IGN is the sentinel value (void(*)(int))1, not a
			 * real function. Calling it traps "uninitialized element"
			 * at runtime. POSIX "ignore" default means do nothing. */
			(void)sig;
			break;

		// Default behavior: "continue".
		case SIGCONT:
			continue_handler(sig);
			break;

		// Default behavior: "stop".
		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			stop_handler(sig);
			break;

		// Default behavior: "terminate".
		case SIGHUP:
		case SIGINT:
		case SIGKILL:
		case SIGUSR1:
		case SIGUSR2:
		case SIGPIPE:
		case SIGALRM:
		case SIGTERM:
		case SIGSTKFLT:
		case SIGVTALRM:
		case SIGPROF:
		case SIGIO:
#if SIGPOLL != SIGIO
		case SIGPOLL:
#endif
		case SIGPWR:
			terminate_handler(sig);
			break;
	}
}
#endif

static sighandler_t handlers[_NSIG];

volatile int __eintr_valid_flag;

#ifdef __wasilibc_unmodified_upstream
#else
/* Forward declarations for the internal helpers the dispatcher consults.
 * All three are defined in block.c / thread/pthread_sigmask.c. */
extern volatile int __wasm_signals_blocked;
extern volatile int __wasm_pending_sigs[];
int __wasm_thread_sig_blocked(int sig);

/* Record a signal as pending on THIS thread. Bit (sig-1) set = pending.
 * Per-thread storage avoids a cross-thread coalescing bug when multiple
 * threads concurrently block/raise — otherwise one thread's UNBLOCK would
 * drain another thread's pending signals and re-raise on the wrong tid. */
static inline void __wasm_pend_signal(int sig) {
	struct pthread *self = __pthread_self();
	if (!self) return;
	int word = (sig - 1) / 32;
	int bit = (sig - 1) % 32;
	a_or((volatile int *)&self->pending_sigs[word], 1 << bit);
	/* Also OR into the process-wide bitmask for sigpending(2)'s query
	 * surface. Consumers of sigpending treat the process-wide bitmask
	 * as the authoritative "what's queued" view. */
	a_or(&__wasm_pending_sigs[word], 1 << bit);
	/* firebox#43B / S5 — a signal just became PENDING on this thread.
	 * Wake any sigtimedwait/sigwait/sigsuspend waiter parked on this
	 * thread's wake counter so it can claim the newly-pending signal.
	 * sigtimedwait parks on sigsuspend_tick with __timedwait; without this
	 * bump a thread that pends a signal and then waits for it (or a
	 * cross-thread pthread_kill that pends on the TARGET thread, which
	 * runs __wasm_signal on its own stack — sigwait/6-2) would block until
	 * the timeout. The dispatch path (handler ran) bumps this counter at
	 * the bottom of __wasm_signal; the ENQUEUE path (blocked → pended)
	 * returns early before that bump, so it must wake here. */
	a_inc(&self->sigsuspend_tick);
	__wake(&self->sigsuspend_tick, 1, 1);
}

/* firebox signal-mask machinery — apply a handler's sa_mask (plus the
 * handled signal itself unless SA_NODEFER) to the calling thread's
 * blocked_sigmask for the duration of the handler call, then restore.
 *
 * POSIX: while a signal-catching function runs, the signals in the
 * action's sa_mask, AND (unless SA_NODEFER) the signal being handled,
 * are added to the thread's signal mask; on return the mask is restored
 * to its value at handler entry. Without this, a signal raised inside
 * the handler that the handler explicitly blocked via sa_mask is
 * delivered/handled immediately instead of being held pending — which
 * is exactly what made sigpending() report an empty set inside a
 * handler (Open POSIX sigpending/1-2, 1-3): the raised-and-supposed-to-
 * be-blocked signals never reached __wasm_pending_sigs because
 * __wasm_thread_sig_blocked() saw them unblocked.
 *
 * `saved` receives the thread's blocked_sigmask snapshot so the caller
 * can restore it after the handler returns. Width-agnostic: operates on
 * the unsigned-long words of blocked_sigmask (same layout as sigset_t /
 * k_sigaction.mask). */
static inline void __wasm_apply_handler_mask(const struct k_sigaction *ksa,
                                             int sig, int nodefer,
                                             unsigned long *saved) {
	struct pthread *self = __pthread_self();
	if (!self) return;
	const size_t nwords = _NSIG / (8 * sizeof(long));
	/* ksa->mask is k_sigaction's mask field — a byte array of _NSIG/8
	 * bytes mirroring sigset_t's bit (sig-1) layout. Read it as
	 * unsigned-long words to OR into blocked_sigmask. */
	const unsigned long *add = (const unsigned long *)(const void *)&ksa->mask;
	/* firebox#H2F — SIGKILL and SIGSTOP can never be blocked, including via a
	 * handler's sa_mask (POSIX sigaction: "If sa_mask names SIGKILL or
	 * SIGSTOP, those signals shall not be blocked"). Strip those two bits
	 * from the mask we apply to blocked_sigmask, so an in-handler
	 * raise(SIGKILL)/raise(SIGSTOP) is NOT pended in-guest but routed to the
	 * host, which terminates/stops the process (Open POSIX sigaction/4-*). */
	const size_t kw = (size_t)(SIGKILL - 1) / (8 * sizeof(long));
	const unsigned long kb = 1UL << ((SIGKILL - 1) % (8 * sizeof(long)));
	const size_t sw = (size_t)(SIGSTOP - 1) / (8 * sizeof(long));
	const unsigned long sb = 1UL << ((SIGSTOP - 1) % (8 * sizeof(long)));
	for (size_t i = 0; i < nwords; i++) {
		unsigned long a = add[i];
		if (i == kw) a &= ~kb;
		if (i == sw) a &= ~sb;
		saved[i] = self->blocked_sigmask[i];
		self->blocked_sigmask[i] |= a;
	}
	/* Unless SA_NODEFER, the handled signal is also blocked for the
	 * handler's duration (the default: a second instance of the same
	 * signal is held pending, not re-entered). */
	if (!nodefer && sig >= 1 && sig < _NSIG) {
		size_t w = (size_t)(sig - 1) / (8 * sizeof(long));
		unsigned long b = 1UL << ((sig - 1) % (8 * sizeof(long)));
		self->blocked_sigmask[w] |= b;
	}
}

/* Restore the thread's blocked_sigmask to the snapshot taken at handler
 * entry. Mirrors __wasm_apply_handler_mask. */
static inline void __wasm_restore_handler_mask(const unsigned long *saved) {
	struct pthread *self = __pthread_self();
	if (!self) return;
	const size_t nwords = _NSIG / (8 * sizeof(long));
	for (size_t i = 0; i < nwords; i++) {
		self->blocked_sigmask[i] = saved[i];
	}
}

/* How many signal handlers are currently executing on the calling
 * thread (per-thread in_handler_depth). Read by __wasm_raise_self to
 * scope in-guest synchronous self-raise handling to the in-handler
 * window. */
static inline int __wasm_in_any_handler(void) {
	struct pthread *self = __pthread_self();
	return self ? self->in_handler_depth : 0;
}

/* Drain the calling thread's pending bitmask and re-raise each bit via
 * __wasi_thread_signal on the calling thread. Called from __restore_sigs
 * after clearing the blocked flag, and from pthread_sigmask after a
 * SIG_UNBLOCK. The re-raise loops back through the runtime →
 * __wasm_signal, which will then find the signal unblocked and dispatch
 * normally. */
void __wasm_drain_pending_sigs(void) {
	struct pthread *self = __pthread_self();
	if (!self) return;
	int tid = self->tid;
	for (int word = 0; word < __WASM_PENDING_WORDS; word++) {
		int bits = a_swap((volatile int *)&self->pending_sigs[word], 0);
		/* Mirror the clear into the process-wide bitmask so
		 * sigpending doesn't continue to report these as pending. */
		a_and(&__wasm_pending_sigs[word], ~bits);
		while (bits) {
			int bit = a_ctz_32((uint32_t)bits);
			bits &= ~(1 << bit);
			int sig = word * 32 + bit + 1;
			if (sig >= 1 && sig < _NSIG) {
				/* Re-raise on the current thread so it flows back
				 * through the dispatch path and rechecks blocking. */
				(void)__wasi_thread_signal(tid,
				                           (__wasi_signal_t)sig);
			}
		}
	}
}

/* firebox#9PX — SA_ONSTACK alternate-stack delivery.
 *
 * POSIX: a handler installed with SA_ONSTACK, when a valid alternate stack
 * has been registered via sigaltstack(2), executes ON that alternate stack
 * (the canonical use is a SIGSEGV/SIGABRT crash handler that must run when
 * the primary stack is exhausted/corrupt — Go, the Rust backtrace printer,
 * ASan, JVM-likes all rely on it). Linux delivers the handler with the
 * machine stack pointer pointed into the alt region; firebox must match.
 *
 * On wasm there is no machine stack — the C "stack" is the linear-memory
 * shadow stack addressed by the `__stack_pointer` global. To run a handler
 * on the alt stack we point `__stack_pointer` at the top of the registered
 * region for the duration of the handler call, then restore it. The
 * handler's own prologue then carves its frame (and every address-taken
 * local / alloca / __builtin_frame_address) out of the alt region.
 *
 * Mechanism notes / why this is sound:
 *  - The shadow stack grows DOWN, so the live top-of-stack is the HIGHEST
 *    usable address: base ss_sp + ss_size, rounded DOWN to 16 (the wasm C
 *    ABI stack alignment). The callee subtracts from there.
 *  - The swap/restore and the saved-SP value are held in plain locals that
 *    are never address-taken, so the compiler keeps them in wasm locals and
 *    this trampoline needs no shadow-stack frame of its own. The ONLY writes
 *    to `__stack_pointer` here are the two explicit asm stores; nothing else
 *    in the function body touches it. (Verified by wasm-objdump: the only
 *    `global.set __stack_pointer` in __fbx_call_on_altstack_{1,3} are the two
 *    explicit asm stores plus the one that restores the saved value.)
 *  - noinline is load-bearing: inlining back into __wasm_signal would
 *    interleave the swap with __wasm_signal's own frame management (siginfo
 *    is on __wasm_signal's frame) and corrupt it.
 *  - The siginfo_t the SA_SIGINFO handler reads lives on __wasm_signal's
 *    (primary-stack) frame and is passed by pointer; only the handler's OWN
 *    locals move to the alt stack — exactly the Linux contract (siginfo is
 *    in the kernel-built frame; handler locals are on the altstack).
 *  - __fbx_altstack_depth (the per-thread TLS depth, see firebox_altstack.h)
 *    is bumped across the call by the caller so a reentrant sigaltstack(2)
 *    reports SS_ONSTACK and refuses to swap the stack out from under a running
 *    handler (EPERM), and a nested SA_ONSTACK delivery does NOT re-switch
 *    (POSIX: don't recurse onto the same alt stack — keep running on it).
 */
#if defined(__wasm64__)
#define __FBX_SP_GLOBALTYPE ".globaltype __stack_pointer, i64\n"
#else
#define __FBX_SP_GLOBALTYPE ".globaltype __stack_pointer, i32\n"
#endif

/* Compute the alt-stack top (highest usable addr, 16-aligned down). */
static inline uintptr_t __fbx_altstack_top(void) {
	uintptr_t base = (uintptr_t)__fbx_altstack_sp;
	uintptr_t top = base + __fbx_altstack_size;
	return top & ~(uintptr_t)15;
}

__attribute__((noinline))
static void __fbx_call_on_altstack_1(void (*handler)(int), int sig,
                                     uintptr_t alt_top) {
	uintptr_t saved_sp;
	/* saved_sp = __stack_pointer */
	__asm__ volatile(__FBX_SP_GLOBALTYPE
		"global.get __stack_pointer\n"
		"local.set %0\n"
		: "=r"(saved_sp));
	/* __stack_pointer = alt_top */
	__asm__ volatile(__FBX_SP_GLOBALTYPE
		"local.get %0\n"
		"global.set __stack_pointer\n"
		:
		: "r"(alt_top));
	handler(sig);
	/* __stack_pointer = saved_sp */
	__asm__ volatile(__FBX_SP_GLOBALTYPE
		"local.get %0\n"
		"global.set __stack_pointer\n"
		:
		: "r"(saved_sp));
}

__attribute__((noinline))
static void __fbx_call_on_altstack_3(
	void (*handler)(int, siginfo_t *, void *), int sig,
	siginfo_t *si, void *uc, uintptr_t alt_top) {
	uintptr_t saved_sp;
	__asm__ volatile(__FBX_SP_GLOBALTYPE
		"global.get __stack_pointer\n"
		"local.set %0\n"
		: "=r"(saved_sp));
	__asm__ volatile(__FBX_SP_GLOBALTYPE
		"local.get %0\n"
		"global.set __stack_pointer\n"
		:
		: "r"(alt_top));
	handler(sig, si, uc);
	__asm__ volatile(__FBX_SP_GLOBALTYPE
		"local.get %0\n"
		"global.set __stack_pointer\n"
		:
		: "r"(saved_sp));
}

/* True iff the active disposition wants on-altstack delivery AND a usable
 * alt stack is registered AND we are not already executing on it. */
static inline int __fbx_should_use_altstack(const struct k_sigaction *ksa) {
	return (ksa->flags & SA_ONSTACK)
		&& __fbx_altstack_sp != 0
		&& __fbx_altstack_size >= MINSIGSTKSZ
		&& __fbx_altstack_depth == 0;
}

__attribute__((export_name("__wasm_signal")))
void __wasm_signal(int sig) {
	if (sig-32U < 3 || sig-1U >= _NSIG-1) {
		return;
	}

	/* firebox#43B / S5 — synchronous sigwait acceptance. If this thread is
	 * parked in sigtimedwait/sigwait/sigwaitinfo with `sig` in its awaited
	 * set, PEND it (do not dispatch the handler) so the waiter claims it
	 * synchronously. POSIX: a signal selected by sigwait is accepted by the
	 * waiting thread, NOT delivered to its handler, even if the signal is
	 * unblocked and has a handler installed (sigwaitinfo/3-1). Checked
	 * BEFORE the block tests because the awaited signal is typically also
	 * blocked (the usual pattern) but need not be (sigwaitinfo/3-1 installs
	 * a handler and does not block). __wasm_pend_signal wakes the waiter. */
	{
		struct pthread *self = __pthread_self();
		if (self) {
			int word = (sig - 1) / 32;
			int bit = 1 << ((sig - 1) % 32);
			if (self->sigwait_set[word] & bit) {
				__wasm_pend_signal(sig);
				return;
			}
		}
	}

	/* Process-wide block (from __block_all_sigs). Enqueue and return.
	 * __restore_sigs will drain this bitmask. */
	if (__wasm_signals_blocked) {
		__wasm_pend_signal(sig);
		return;
	}

	/* Per-thread block (from pthread_sigmask). The signal is queued
	 * into the calling thread's own pending bitmask; pthread_sigmask
	 * on SIG_UNBLOCK (or SIG_SETMASK) drains and redelivers. */
	if (__wasm_thread_sig_blocked(sig)) {
		__wasm_pend_signal(sig);
		return;
	}

	LOCK(__eintr_handler_lock);
	struct k_sigaction ksa = __eintr_handler_callbacks[sig];
	/* SA_RESETHAND: reset handler to SIG_DFL atomically with snapshot,
	 * so a concurrent sigaction() on another thread sees the reset. */
	if (ksa.handler != 0 && (ksa.flags & SA_RESETHAND)) {
		__eintr_handler_callbacks[sig].handler = 0;
	}
	UNLOCK(__eintr_handler_lock);

	/* SA_NODEFER recursion guard. Without SA_NODEFER, don't re-enter
	 * the same signal's handler; enqueue instead. */
	if (!(ksa.flags & SA_NODEFER)) {
		if (__wasm_in_handler[sig] > 0) {
			__wasm_pend_signal(sig);
			return;
		}
		__wasm_in_handler[sig] = 1;
	}

	/* Dispatch decision tree (#468 fix — fnptr-as-sentinel collision):
	 *
	 *   ksa.handler != 0    → user handler (or SIG_IGN, which is
	 *                         &__SIG_IGN — a real no-op function in
	 *                         wasix-libc, not the upstream-musl literal
	 *                         (void*)1 sentinel). Call it; SIG_IGN
	 *                         self-implements "ignore" by being a no-op.
	 *   default_handler != 0 → SIG_DFL with the libc default installed.
	 *   else                → SIG_DFL with no default: drop. Correct for
	 *                         SIGCHLD/SIGURG/SIGWINCH (POSIX "ignore"
	 *                         default) when no handler is registered.
	 *
	 * Previously the dispatch arm carried `(uintptr_t)ksa.handler != 1`
	 * as a SIG_IGN guard inherited from upstream musl (where SIG_IGN is
	 * the in-band sentinel `(void(*)(int))1`). That guard is double-wrong
	 * on wasix-libc: (a) wasix-libc's <signal.h> already redefines
	 * SIG_IGN to `&__SIG_IGN` (a real callable no-op function in
	 * src/signal/signal.c) precisely BECAUSE wasm function pointers are
	 * __indirect_function_table indices and literal 1 IS a valid slot;
	 * (b) the guard then mis-rejects any real handler that wasm-ld
	 * happens to place at table slot 1 — silently dropping every signal
	 * for the smallest possible C canary. See #468 RCA + class lesson
	 * class_lesson_wasm_fnptr_is_table_index_not_address. */
	if (ksa.handler != 0) {
		/* SA_SIGINFO 3-arg calling convention (#WJJ).
		 *
		 * POSIX: when SA_SIGINFO is set in sa_flags, the handler was
		 * registered via the union's sa_sigaction member and MUST be
		 * called as `void (*)(int, siginfo_t *, void *)`, not the
		 * 1-arg sa_handler form. On wasm this is not merely an ABI
		 * nicety: function pointers are __indirect_function_table
		 * indices and `call_indirect` validates the STATIC signature
		 * at the call site against the callee's real type. Calling a
		 * 3-arg sa_sigaction handler through the 1-arg `void(*)(int)`
		 * ksa.handler type traps with "indirect call type mismatch"
		 * BEFORE the handler body runs — which is exactly why the
		 * Open POSIX sigaction/19-* and sigaction/6-* SA_SIGINFO
		 * tests reported "The sa_handler was not called" (the trap
		 * unwinds the runtime callback; the test's `called` flag is
		 * never set). See #WJJ + #17H (W1 fixed the host-side
		 * delivery enqueue; this fixes the libc-side convention).
		 *
		 * siginfo_t is built on this dispatch frame; it stays live
		 * for the entire (synchronous) handler call. Only si_signo is
		 * authoritative from the libc layer — the __wasm_signal export
		 * receives only the signal number, so si_code/si_pid/si_uid/
		 * si_value have no faithful source here and are zeroed (musl's
		 * own minimal-siginfo behavior for signals lacking queued
		 * info). si_code == 0 == SI_USER, the correct code for a
		 * kill()/raise()/tkill()-delivered signal with no si_value.
		 * The ucontext argument is passed as NULL: every Open POSIX
		 * sigaction test marks it unused and never dereferences it,
		 * and wasm has no machine context to materialize.
		 *
		 * The handler is stored as void(*)(int); cast to the 3-arg
		 * type so the emitted call_indirect carries the matching
		 * (i32, i32, i32) signature the SA_SIGINFO callee expects. */
		/* firebox#9PX — when SA_ONSTACK is set and a valid alt stack is
		 * registered (and we're not already on it), run the handler on
		 * the alternate stack. The altstack_onstack depth guard makes
		 * sigaltstack(2) report SS_ONSTACK / refuse a mid-handler swap,
		 * and prevents a nested SA_ONSTACK delivery from re-switching. */
		int on_alt = __fbx_should_use_altstack(&ksa);
		uintptr_t alt_top = on_alt ? __fbx_altstack_top() : 0;
		if (on_alt) __fbx_altstack_depth++;
		/* firebox signal-mask machinery — apply the handler's sa_mask
		 * (plus the handled signal unless SA_NODEFER) to this thread's
		 * blocked_sigmask for the handler's duration, restoring on
		 * return. This is what makes a signal raised-and-blocked inside
		 * the handler land in __wasm_pending_sigs so sigpending() reports
		 * it (sigpending/1-2, 1-3), and what gives the default (non-
		 * NODEFER) "same signal held pending during handler" behavior at
		 * the MASK layer (complementing the __wasm_in_handler recursion
		 * guard above). */
		int nodefer = (ksa.flags & SA_NODEFER) ? 1 : 0;
		unsigned long saved_mask[_NSIG/(8*sizeof(long))];
		__wasm_apply_handler_mask(&ksa, sig, nodefer, saved_mask);
		/* firebox signal-mask machinery — mark "a handler is executing on
		 * this thread" (per-thread depth) so __wasm_raise_self handles an
		 * in-handler self-raise in-guest (see its comment). And mark this
		 * signal's SA_NODEFER disposition active so the synchronous
		 * re-entry path fires for it. */
		struct pthread *__self_dispatch = __pthread_self();
		if (__self_dispatch) __self_dispatch->in_handler_depth++;
		if (nodefer) __wasm_nodefer_active[sig]++;
		if (ksa.flags & SA_SIGINFO) {
			siginfo_t si;
			memset(&si, 0, sizeof si);
			si.si_signo = sig;
			si.si_code = SI_USER;
			void (*h3)(int, siginfo_t *, void *) =
				(void (*)(int, siginfo_t *, void *))(void *)ksa.handler;
			if (on_alt) {
				__fbx_call_on_altstack_3(h3, sig, &si, NULL, alt_top);
			} else {
				h3(sig, &si, NULL);
			}
		} else {
			if (on_alt) {
				__fbx_call_on_altstack_1(ksa.handler, sig, alt_top);
			} else {
				ksa.handler(sig);
			}
		}
		if (nodefer) __wasm_nodefer_active[sig]--;
		if (__self_dispatch) __self_dispatch->in_handler_depth--;
		__wasm_restore_handler_mask(saved_mask);
		if (on_alt) __fbx_altstack_depth--;
		/* firebox signal-mask machinery — POSIX: on handler return the
		 * thread's signal mask is restored, and any signals that became
		 * pending while blocked by sa_mask (or by the handled signal's
		 * own default block) are delivered now. Drain this thread's
		 * pending bitmask: __wasm_drain_pending_sigs re-raises each bit,
		 * and the resulting __wasm_signal re-checks the (restored) mask —
		 * a still-blocked signal re-pends, an unblocked one dispatches.
		 * Only walk the drain when something is actually pending on this
		 * thread (the common no-pending case is a single cheap read), so
		 * a handler that neither blocked nor re-raised anything pays no
		 * host round-trip. Skipped entirely when a handler exit()s (the
		 * sigpending/1-2,1-3 and sigaction/22-* cases never reach here),
		 * so this only fires for handlers that block-then-raise-then-
		 * return — the held-pending-delivered-on-return path. */
		{
			struct pthread *self = __pthread_self();
			int has_pending = 0;
			if (self) {
				for (int w = 0; w < __WASM_PENDING_WORDS; w++) {
					if (self->pending_sigs[w]) { has_pending = 1; break; }
				}
			}
			if (has_pending) __wasm_drain_pending_sigs();
		}
	} else if (default_handler != 0) {
		default_handler(sig);
	}
	/* else: SIG_DFL with default_handler unset — drop. */

	if (!(ksa.flags & SA_NODEFER)) {
		__wasm_in_handler[sig] = 0;
	}

	/* A handler actually RAN on this thread. Bump both wake counters:
	 *
	 *  - sigsuspend_tick: the sigtimedwait/sigwait family parks here and
	 *    must re-scan when any handler runs (a non-awaited signal's handler
	 *    advances the counter; the waiter re-checks for a claimable signal
	 *    and re-parks).
	 *  - sigdispatch_tick (firebox#XT7): sigsuspend(2) parks HERE, and only
	 *    a real handler dispatch (this point) bumps it — never the
	 *    enqueue/pend path. This lets sigsuspend observe that a signal was
	 *    actually DELIVERED (not merely pended behind its temporary mask)
	 *    and return -1/EINTR, preserving the POSIX rule that a signal
	 *    blocked by the sigsuspend mask stays pending until sigsuspend
	 *    returns and unblocks it.
	 *
	 * Touching the counters from the dispatch path (rather than from the
	 * handler itself) avoids forcing handlers to be sigsuspend-aware. */
	{
		struct pthread *self = __pthread_self();
		if (self) {
			a_inc(&self->sigsuspend_tick);
			__wake(&self->sigsuspend_tick, 1, 1);
			a_inc(&self->sigdispatch_tick);
			__wake(&self->sigdispatch_tick, 1, 1);
		}
	}
}

/* firebox in-handler self-raise — the raise()/pthread_kill() fast path
 * for a thread raising a signal to ITSELF while a signal handler is
 * currently executing on this thread. Returns 1 if the raise was handled
 * here (caller returns success WITHOUT the host __wasi_thread_signal
 * round-trip); 0 to fall through to the normal host delivery path.
 *
 * WHY this exists: while a handler runs on this thread, the host has set
 * its firebox#912 `in_signal_dispatch` guard, so a nested
 * __wasi_thread_signal is DEFERRED to the next syscall boundary instead
 * of being evaluated now. That breaks two POSIX behaviors that the
 * conformance suite exercises from inside a handler:
 *
 *   (1) BLOCKED self-raise (sigpending/1-2, 1-3): a signal in the
 *       handler's applied mask (sa_mask, or the handled signal itself
 *       absent SA_NODEFER) that is raised inside the handler must be held
 *       PENDING and be visible to sigpending() immediately. With the host
 *       deferring the raise, __wasm_signal never ran to record the pend,
 *       so sigpending() saw an empty set. We record the pend in-guest
 *       here, synchronously.
 *
 *   (2) SA_NODEFER UNBLOCKED self-raise (sigaction/22-*): a signal whose
 *       SA_NODEFER handler is currently running is NOT blocked, so a
 *       re-raise must RE-ENTER the handler immediately (Linux delivers it
 *       on the raise() syscall return path, nesting the handler before
 *       raise() returns). We dispatch __wasm_signal(sig) synchronously
 *       in-guest.
 *
 * `__wasm_in_any_handler` (a per-thread depth, bumped around every
 * handler call in __wasm_signal) scopes BOTH cases to the in-handler
 * window — the FIRST raise from main (no handler on the stack) always
 * takes the host path, so non-handler raise() semantics are unchanged.
 *
 * Re-entrancy safety of case (2): by the time the handler body runs,
 * __wasm_signal has already released __eintr_handler_lock (acquired and
 * released around the disposition snapshot, before the handler call), so
 * the nested __wasm_signal re-acquires it uncontended and does NOT
 * reproduce the firebox#912 deadlock window (the futex_register_held
 * syscall issued from INSIDE the lock's own contended acquire — a window
 * that does not overlap the handler body). */
int __wasm_raise_self(int sig) {
	if (sig < 1 || sig >= _NSIG) return 0;
	/* Only intercept when a handler is actually executing on this
	 * thread; otherwise the host path is correct and unchanged. */
	if (__wasm_in_any_handler() <= 0) return 0;
	/* Blocked (process-wide or by this thread's mask, which now carries
	 * the active handler's applied sa_mask): hold pending in-guest so
	 * sigpending() reports it. */
	if (__wasm_signals_blocked || __wasm_thread_sig_blocked(sig)) {
		__wasm_pend_signal(sig);
		return 1;
	}
	/* Unblocked AND this signal's SA_NODEFER handler is active: dispatch
	 * synchronously (immediate re-entry). */
	if (__wasm_nodefer_active[sig] > 0) {
		__wasm_signal(sig);
		return 1;
	}
	/* Unblocked, no active SA_NODEFER for this signal: fall through to
	 * the host. The host's #912 guard will defer it to the next syscall
	 * boundary after the outer dispatch returns — the faithful "deliver
	 * after the handler returns" behavior for the non-NODEFER case. */
	return 0;
}

/* Stub export for the runtime's callback_signal("__wasm_signal_blocked").
 * Historically wasix-libc's __block_all_sigs calls __wasi_callback_signal
 * with this name, but no program exported the target. Now that
 * __block_all_sigs sets __wasm_signals_blocked=1 BEFORE calling the
 * callback, and __wasm_signal consults the flag, this export is a
 * compatibility no-op: it exists so the runtime's callback wiring
 * doesn't see "export not found" and doesn't disable dispatch. */
__attribute__((export_name("__wasm_signal_blocked")))
void __wasm_signal_blocked(int sig) {
	(void)sig;
}
#endif

int __libc_sigaction(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	struct k_sigaction ksa, ksa_old;
	if (sa) {
#ifdef __wasilibc_unmodified_upstream
		/* Native: SIG_IGN is the in-band sentinel (void(*)(int))1 and
		 * address 1 is unmapped, so `> 1UL` is the exact "real
		 * callable handler" filter. */
		if ((uintptr_t)sa->sa_handler > 1UL) {
#else
		/* Wasm: function pointers are __indirect_function_table
		 * indices; slot 1 is the first ordinary user-code slot.
		 * The `> 1UL` filter (companion to the SIG_IGN literal-1
		 * sentinel) would misclassify a real handler placed by
		 * wasm-ld at table slot 1 as SIG_IGN, failing to record it
		 * in handler_set and failing to set __eintr_valid_flag.
		 * wasix-libc redefines SIG_IGN as &__SIG_IGN (a real callable
		 * no-op function); the correct filter is therefore sentinel
		 * equality against the macros, not address-range arithmetic.
		 * See #468 RCA + class lesson
		 * class_lesson_wasm_fnptr_is_table_index_not_address. */
		if (sa->sa_handler != SIG_DFL && sa->sa_handler != SIG_IGN) {
#endif
			a_or_l(__fbx_handler_set+(sig-1)/(8*sizeof(long)),
				1UL<<(sig-1)%(8*sizeof(long)));

			/* If pthread_create has not yet been called,
			 * implementation-internal signals might not
			 * yet have been unblocked. They must be
			 * unblocked before any signal handler is
			 * installed, so that an application cannot
			 * receive an illegal sigset_t (with them
			 * blocked) as part of the ucontext_t passed
			 * to the signal handler. */
			if (!libc.threaded && !unmask_done) {
#ifdef __wasilibc_unmodified_upstream
				__syscall(SYS_rt_sigprocmask, SIG_UNBLOCK,
					SIGPT_SET, 0, _NSIG/8);
#endif
				unmask_done = 1;
			}

			if (!(sa->sa_flags & SA_RESTART)) {
				a_store(&__eintr_valid_flag, 1);
			}
		}
		ksa.handler = sa->sa_handler;
		ksa.flags = sa->sa_flags | SA_RESTORER;
		ksa.restorer = (sa->sa_flags & SA_SIGINFO) ? __restore_rt : __restore;
		memcpy(&ksa.mask, &sa->sa_mask, _NSIG/8);
	}
#ifdef __wasilibc_unmodified_upstream
	int r = __syscall(SYS_rt_sigaction, sig, sa?&ksa:0, old?&ksa_old:0, _NSIG/8);
#else
	if (a_cas(&__eintr_callback_registered, 0, 1) == 0) {
		__wasi_callback_signal("__wasm_signal");
	}
	int r = 0;
	/* firebox#XCJ — reject out-of-range signos AND an attempt to change the
	 * disposition of the uncatchable SIGKILL/SIGSTOP (sa != NULL). This mirrors
	 * the user-facing check in __sigaction_inner; keeping it here too means a
	 * direct __libc_sigaction caller (bypassing __sigaction_inner) is also
	 * EINVAL-guarded. POSIX: SIGKILL/SIGSTOP dispositions cannot be set. */
	if (sig-32U < 3 || sig-1U >= _NSIG-1 ||
	    (sa && (sig == SIGKILL || sig == SIGSTOP))) {
		r = EINVAL;
	} else {
		LOCK(__eintr_handler_lock);
		ksa_old = __eintr_handler_callbacks[sig];
		if (sa) {
			__eintr_handler_callbacks[sig] = ksa;
		}
		UNLOCK(__eintr_handler_lock);
		r = 0;
	}
#endif
	if (old && !r) {
		old->sa_handler = ksa_old.handler;
		old->sa_flags = ksa_old.flags;
		memcpy(&old->sa_mask, &ksa_old.mask, _NSIG/8);
	}
	return __syscall_ret(r);
}

#ifdef __wasilibc_unmodified_upstream
int __sigaction(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	unsigned long set[_NSIG/(8*sizeof(long))];

	if (sig-32U < 3 || sig-1U >= _NSIG-1) {
		errno = EINVAL;
		return -1;
	}

	/* Doing anything with the disposition of SIGABRT requires a lock,
	 * so that it cannot be changed while abort is terminating the
	 * process and so any change made by abort can't be observed. */
	if (sig == SIGABRT) {
		__block_all_sigs(&set);
		LOCK(__abort_lock);
	}
	int r = __libc_sigaction(sig, sa, old);
	if (sig == SIGABRT) {
		UNLOCK(__abort_lock);
		__restore_sigs(&set);
	}
	return r;
}
#else
static int __sigaction_inner(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	unsigned long set[_NSIG/(8*sizeof(long))];

	if (sig-32U < 3 || sig-1U >= _NSIG-1) {
		errno = EINVAL;
		return -1;
	}

	/* firebox#XCJ — SIGKILL and SIGSTOP are uncatchable: their disposition
	 * cannot be changed (POSIX sigaction: "SIGKILL or SIGSTOP ... it is
	 * impossible to ... set the action for [them] to SIG_IGN ... or to a
	 * signal-catching function"). An attempt to install OR ignore a handler
	 * for either must fail with -1/EINVAL (Open POSIX sigaction/30-1, and the
	 * SIG_ERR path of signal/7-1 which routes through here). Only reject when
	 * actually changing the disposition (sa != NULL); a pure old-disposition
	 * query (sa == NULL) is harmless and left to fall through. */
	if (sa && (sig == SIGKILL || sig == SIGSTOP)) {
		errno = EINVAL;
		return -1;
	}

	/* Doing anything with the disposition of SIGABRT requires a lock,
	 * so that it cannot be changed while abort is terminating the
	 * process and so any change made by abort can't be observed. */
	if (sig == SIGABRT) {
		__block_all_sigs(&set);
		LOCK(__abort_lock);
	}
	int r = __libc_sigaction(sig, sa, old);
	if (sig == SIGABRT) {
		UNLOCK(__abort_lock);
		__restore_sigs(&set);
	}
	return r;
}

int __sigaction(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	if (default_handler == NULL) default_handler = &__default_handler;
	return __sigaction_inner(sig, sa, old);
}
#endif

weak_alias(__sigaction, sigaction);

#ifdef __wasilibc_unmodified_upstream
#else
/* Currently, core_handler cannot be compiled in a rust program.
 * To keep that out of the compilation, the rust libc does not
 * use __sigaction above (which references __default_handler),
 * instead using this function to pass in a default handler of
 * its own. */
int __sigaction_external_default(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old, sighandler_t _default_handler)
{
	if (default_handler == NULL) default_handler = _default_handler;
	return __sigaction_inner(sig, sa, old);
}

weak_alias(__sigaction_external_default, sigaction_external_default);

__attribute__((export_name("__wasm_sigaction")))
int __wasm_sigaction(int sig, int action) {
	void (*a)(int);

	switch (action) {
		case __WASI_DISPOSITION_DEFAULT:
			a = SIG_DFL;
			break;
		case __WASI_DISPOSITION_IGNORE:
			a = SIG_IGN;
			break;
		default:
			return -1;
	}

	struct sigaction sa = { .sa_handler = a, .sa_flags = SA_RESTART };
	if (__sigaction(sig, &sa, NULL) < 0) {
		return -1;
	}

	return 0;
}

/* Force-link references for symbols shipped in archive members that a
 * linker wouldn't otherwise pull in when the only consumer-side reference
 * is a weak declaration (e.g. the wasix-libc regression harness probing
 * feature presence). By referencing them from sigaction.c — which is
 * always pulled in because crt1 calls __wasi_init_signals below — the
 * linker resolves these at link time. See issue #24.
 *
 * The symbols: sigsetjmp, siglongjmp (patch E); sigpending, sigsuspend
 * (patch F); mknodat (patch I); __fbx_signal_poll (firebox#8B5 HYBRID).
 * Declared with minimal prototypes here because src/signal/ doesn't have
 * setjmp.h / sys/stat.h in scope.
 *
 * firebox#8B5 HYBRID — __fbx_signal_poll is the cooperative signal-poll host
 * import (declared in libc-bottom-half/sources/__wasixlibc_firebox.c). The
 * wasmer `SignalPoll` middleware injects throttled `Call`s to it at loop
 * headers, but the import has no source-level caller (the middleware is the
 * caller, injected at compile time), so wasm-ld would GC it. Force-linking it
 * here — alongside the other always-present signal symbols — guarantees the
 * import is present in EVERY program's module so the middleware always has a
 * function-index to call. */
extern int sigsetjmp();
extern void siglongjmp();
extern int mknodat();
extern int __fbx_signal_poll(void);
__attribute__((used))
static void *__firebox_force_link_signals[] = {
	(void *)&sigsetjmp,
	(void *)&siglongjmp,
	(void *)&sigpending,
	(void *)&sigsuspend,
	(void *)&mknodat,
	(void *)&__fbx_signal_poll,
};

void __wasi_init_signals() {
    __wasi_errno_t err;
	int sigaction_ret;

	/* Ensure default_handler is set BEFORE any signal can be delivered.
	 * Otherwise a program that never calls sigaction() (e.g. a Rust
	 * binary like coreutils/ls that relies on wasix-libc's default
	 * dispositions) leaves default_handler == NULL, and the first
	 * signal that fires (commonly a pending SIGCHLD delivered across
	 * a proc_exec3 module swap) traps __wasm_signal with
	 * "uninitialized element". This used to be reached only via
	 * __sigaction, which __wasi_init_signals can skip entirely when
	 * the builder has no signal dispositions. */
	if (default_handler == 0) default_handler = &__default_handler;

    __wasi_size_t signal_count;
    err = __wasi_proc_signals_sizes_get(&signal_count);
    if (err != __WASI_ERRNO_SUCCESS) {
        _Exit(EX_OSERR);
    }
	
	__wasi_signal_disposition_t *sig_dispositions = calloc(signal_count, sizeof(__wasi_signal_disposition_t));
    if (sig_dispositions == NULL) {
        _Exit(EX_SOFTWARE);
    }

    err = __wasi_proc_signals_get((uint8_t *)sig_dispositions);
    if (err != __WASI_ERRNO_SUCCESS) {
        free(sig_dispositions);
        _Exit(EX_OSERR);
    }

	for (int i = 0; i < signal_count; ++i) {
		sigaction_ret = __wasm_sigaction((int)sig_dispositions[i].sig, (int)sig_dispositions[i].disp);
		if (sigaction_ret == -1) {
			free(sig_dispositions);
			_Exit(EX_OSERR);
		}
	}

	free(sig_dispositions);

	// Unconditionally register the signal handler at startup - otherwise, the host will
	// eat up signals that are sent before the first sigaction call.
	if (a_cas(&__eintr_callback_registered, 0, 1) == 0) {
		__wasi_callback_signal("__wasm_signal");
	}

	/* firebox#8B5 HYBRID — make the __fbx_signal_poll import a LIVE reference.
	 *
	 * The wasmer `SignalPoll` compiler middleware injects throttled `Call`s to
	 * the __fbx_signal_poll import at loop headers, but those injected calls do
	 * NOT exist in the source the linker sees — so without a real source-level
	 * caller, wasm-ld's --gc-sections DROPS the import entirely (the
	 * `__attribute__((used))` force-link array keeps the C symbol but not an
	 * import that nothing reachable CALLS). Then the middleware would have no
	 * function-index to call. Calling it ONCE here — from __wasi_init_signals,
	 * which crt1 always invokes before main — makes the import genuinely
	 * reachable so it survives GC. At init there are no pending signals, so the
	 * host drain is a no-op (one cheap host round-trip at process start). */
	(void)__fbx_signal_poll();
}
#endif
