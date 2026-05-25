#include <signal.h>
#include <errno.h>
#include <stdint.h>
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
#endif

#ifndef __wasilibc_unmodified_upstream
/* firebox#526 — breadcrumb stamps for the signal-driven abort path.
 * core_handler / terminate_handler / stop_handler are the default
 * sighandlers that wasix-libc installs for SIGABRT and friends; each
 * fprintf(stderr,...) + abort() if the corresponding signal fires.
 * The fprintf may or may not flush before abort takes us down (the
 * #516 evidence trail did NOT show "Program received fatal signal"
 * messages, suggesting either: (a) the wedge is NOT signal-driven,
 * or (b) the fprintf-stderr path is blocked by the same wedge — the
 * breadcrumb disambiguates because it bypasses stdio). See
 * libc-top-half/musl/src/exit/abort.c for the full mechanism. */
extern void firebox_526_stamp(const char *crumb);
#endif

static int unmask_done;
static unsigned long handler_set[_NSIG/(8*sizeof(long))];
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
#endif

void __get_handler_set(sigset_t *set)
{
	memcpy(set, handler_set, sizeof handler_set);
}

_Noreturn
static void core_handler(int sig) {
#ifndef __wasilibc_unmodified_upstream
    firebox_526_stamp("core_handler");
#endif
    fprintf(stderr, "Program recieved fatal signal: %s\n", strsignal(sig));
    abort();
}

_Noreturn
static void terminate_handler(int sig) {
#ifndef __wasilibc_unmodified_upstream
    firebox_526_stamp("terminate_handler");
#endif
    fprintf(stderr, "Program recieved termination signal: %s\n", strsignal(sig));
    abort();
}

_Noreturn
static void stop_handler(int sig) {
#ifndef __wasilibc_unmodified_upstream
    firebox_526_stamp("stop_handler");
#endif
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

__attribute__((export_name("__wasm_signal")))
void __wasm_signal(int sig) {
	if (sig-32U < 3 || sig-1U >= _NSIG-1) {
		return;
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
		ksa.handler(sig);
	} else if (default_handler != 0) {
		default_handler(sig);
	}
	/* else: SIG_DFL with default_handler unset — drop. */

	if (!(ksa.flags & SA_NODEFER)) {
		__wasm_in_handler[sig] = 0;
	}

	/* Bump the sigsuspend wake counter and wake any waiters on it.
	 * This lets sigsuspend(2) observe that a signal was delivered and
	 * return -1/EINTR. Touching the counter from the dispatch path
	 * (rather than from the handler itself) avoids forcing handlers
	 * to be sigsuspend-aware. */
	{
		struct pthread *self = __pthread_self();
		if (self) {
			a_inc(&self->sigsuspend_tick);
			__wake(&self->sigsuspend_tick, 1, 1);
		}
	}
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
			a_or_l(handler_set+(sig-1)/(8*sizeof(long)),
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
	if (sig-32U < 3 || sig-1U >= _NSIG-1) {
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
 * (patch F); mknodat (patch I). Declared with minimal prototypes here
 * because src/signal/ doesn't have setjmp.h / sys/stat.h in scope. */
extern int sigsetjmp();
extern void siglongjmp();
extern int mknodat();
__attribute__((used))
static void *__firebox_force_link_signals[] = {
	(void *)&sigsetjmp,
	(void *)&siglongjmp,
	(void *)&sigpending,
	(void *)&sigsuspend,
	(void *)&mknodat,
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
}
#endif
