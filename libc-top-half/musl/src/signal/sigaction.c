#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>  /* firebox#KKR — offsetof() for __fbx_blocked_off */
#include <string.h>
#include <unistd.h>  /* firebox#VYD — getpid()/getuid() for the self-raise ring record */
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

/* firebox#C44 — WEAK reference to pthread_exit so a NON-threaded program is not
 * force-pulled into the thread-start machinery.
 *
 * sigaction.o is UNIVERSALLY linked: crt1 calls __wasi_init_signals (below), so
 * every program drags this TU in. The one caller of pthread_exit here is the
 * #93A SIGCANCEL async-cancel arm in __wasm_signal (pthread_exit(PTHREAD_CANCELED)
 * ~L890). As a STRONG undefined reference that forces the linker to extract
 * pthread_create.o (which defines pthread_exit) -> wasi_thread_start.o ->
 * __wasm_init_tls, a symbol wasm-ld SYNTHESIZES only under --shared-memory. A
 * plain `clang hello.c` link (no -pthread, no --shared-memory) then fails
 * `undefined symbol: __wasm_init_tls`. On wasm32 this is masked because the
 * non-threads sysroot's pthread_create.o is a single-thread stub that never
 * references wasi_thread_start; on wasm64 the SAME threads-built libc.a sits at
 * the non-threads path, so the real thread-start chain gets pulled.
 *
 * Making the reference WEAK is the faithful Linux-parity fix (Inv-2): a
 * non-threaded program links non-threaded and never carries the thread runtime;
 * -pthread opts in. A weak undefined reference does NOT force archive
 * extraction, so a non-threaded link leaves pthread_exit unresolved (address 0),
 * and the cancel arm that would call it is UNREACHABLE without pthreads
 * (self->cancel / self->cancelasync are only ever set by pthread_cancel /
 * pthread_setcanceltype, and SIGCANCEL is never raised in a single-threaded
 * process). A THREADED program calls pthread_create -> pthread_create.o is
 * force-extracted by that strong reference, providing pthread_exit's definition,
 * to which this weak reference binds -> the SIGCANCEL cancel path is fully
 * intact. Harmless on wasm32 (the weak ref binds to the same def whenever the
 * program is threaded; is never reached otherwise). */
extern _Noreturn void pthread_exit(void *) __attribute__((__weak__));

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

/* firebox#HDH — offsetof(struct pthread, pending_sigs), the PER-THREAD pending
 * bitmask (pthread_impl.h). Mirrors __fbx_blocked_off's posture exactly: the
 * threaded/edge link --export[-if-defined]s it, the host reads the immutable
 * global (= this const's linear address), then the offset value from the live
 * MemoryView — so the host carries no struct-layout knowledge (robust to field
 * reordering), just like the blocked-mask offset above.
 *
 * WHY the host needs it: POSIX fork(2) gives the child an EMPTY pending-signal
 * set — the child inherits the parent's signal DISPOSITIONS and its blocked
 * mask, but NOT its pending signals. Firebox forks by copying the parent's
 * entire linear memory (wasmer SpawnType::CopyMemory), so the child WRONGLY
 * inherits the parent's pended signals in BOTH guest-side pending stores: the
 * process-wide __wasm_pending_sigs[] (sigpending(2)'s authoritative view) and
 * this main thread's per-thread pending_sigs[] (what __wasm_drain_pending_sigs
 * re-raises on a later unblock). The host zeroes both in the child at the
 * proc_fork child rewind — a deterministic post-copy / pre-resume point that a
 * guest-side clear cannot pin in Firebox's cooperative/asyncify runtime (the
 * #HDH Heisenbug: a libc-only _fork_internal clear FAILED clean but PASSED the
 * instant a debug write() perturbed the signal-drain timing). It reads the main
 * thread's struct base from __fbx_main_pthread and this field offset.
 *
 * The process-wide __wasm_pending_sigs[] is already a defined symbol (below);
 * the host reaches it by the SAME --export-if-defined mechanism, no new symbol.
 * Same backward-compat story as __fbx_blocked_off: a host predating the read
 * just ignores it; the guest imports NOTHING new, so wasm built with this libc
 * still instantiates on an older runtime (the fix is simply dormant). */
const uint32_t __fbx_pending_off = offsetof(struct pthread, pending_sigs);

/* firebox#2JV — offsetof(struct pthread, sigsuspend_tick), the PER-THREAD futex
 * word a sigtimedwait/sigwait/sigsuspend waiter parks on (`__timedwait(&self->
 * sigsuspend_tick, ...)`). Exact mirror of __fbx_pending_off's posture: the
 * threaded/edge link --export[-if-defined]s it, the host reads the immutable
 * global (= this const's linear address), then the offset value from the live
 * MemoryView — so the host carries no struct-layout knowledge, just the field
 * offset.
 *
 * WHY the host needs it: when the host PENDS a directed no-handler blocked
 * signal into the target's claim surface (__wasm_pending_sigs + the per-thread
 * pending_sigs) on behalf of a pid-1 tini parked in a bounded
 * `sigtimedwait(&all, &si, &poll)` (env.rs first_no_handler_default_terminate,
 * the #2JV target-drain pend — the cross-process + timed shape the sender-side
 * try_pend_process_directed structurally cannot cover), it must ALSO replicate
 * the last two steps the guest's own __wasm_pend_signal does after the two
 * bitmask ORs: `a_inc(&self->sigsuspend_tick)` + `__wake(&self->sigsuspend_tick,
 * 1, 1)`. Without the tick bump the parked FutexPoller's value-recheck never
 * fires (the word is unchanged), and without the wake nothing re-drives the
 * poll — so the pended signal is never claimed and tini hangs. The host resolves
 * this thread's `struct pthread` base (__fbx_main_pthread / pthread_self_ptr)
 * and adds this field offset to reach the same futex word, then increments it
 * and futex-wakes — byte-identical in effect to the guest's own pend.
 *
 * Same backward-compat story as __fbx_pending_off: a host predating the read
 * just ignores it; the guest imports NOTHING new, so wasm built with this libc
 * still instantiates on an older runtime (the fix is simply dormant). */
const uint32_t __fbx_sigsuspend_off = offsetof(struct pthread, sigsuspend_tick);

/* firebox#GF1 — per-signal sa_flags, host-readable. Written by __libc_sigaction
 * on every disposition change; read by the FIREBOX RUNTIME to detect
 * SA_NOCLDWAIT on the SIGCHLD disposition. POSIX: a parent whose SIGCHLD action
 * carries SA_NOCLDWAIT does NOT accumulate zombie children, and once it has no
 * unwaited children wait()/waitpid() fails with ECHILD. Firebox's zombie table
 * (wasmer #42) and proc_join have no other channel to that flag — the WASI
 * proc_signal/proc_join seams carry no sa_flags — so the guest exposes it here.
 *
 * Same posture as __fbx_handler_set / __fbx_pending_off: the threaded/edge link
 * `--export[-if-defined]`s this symbol, wasm-ld emits an immutable global holding
 * its linear-memory base address, and the host reads the SIGCHLD slot straight
 * from the live MemoryView (no guest call — the parent that must observe the flag
 * is parked in proc_join, not runnable cooperatively). `volatile` so the store is
 * never dead-store-eliminated under LTO: the ONLY reader is the host, invisible to
 * the compiler. `uint32_t` (not `unsigned long`) fixes the element width at 4
 * bytes on BOTH wasm32 and wasm64, so the host reads slot `sig` at `sig*4` without
 * pointer-width knowledge; sa_flags is an `int`, so the value is exact. Indexed by
 * [sig] (not sig-1), mirroring __eintr_handler_callbacks[]. Backward-compatible: a
 * host predating the read ignores it, and the guest imports NOTHING new, so wasm
 * built with this libc still instantiates on an older runtime (fix simply dormant). */
volatile uint32_t __fbx_sa_flags[_NSIG];

#ifdef __wasilibc_unmodified_upstream
#else
static volatile int __eintr_callback_registered = 0;
static volatile struct k_sigaction __eintr_handler_callbacks[_NSIG];
/* Pure futex-protected lock guarding __eintr_handler_callbacks[].
 * MUST NEVER be touched by a_store() or raw writes — that conflates
 * the futex "has waiters" bit (1) with a "signals blocked" flag and
 * deadlocks under re-entrant __wasm_signal dispatch. See issue #24. */
volatile int __eintr_handler_lock[1];

/* Replace one signal action and its host-readable mirrors as one serialized
 * maintenance operation.
 *
 * Contract:
 * - The caller has validated sig and knows the operation will not be rejected.
 * - The caller holds __eintr_handler_lock; this helper must not acquire it.
 * - This helper alone writes __eintr_handler_callbacks[sig] and maintains both
 *   __fbx_handler_set and __fbx_sa_flags so the guest-side state cannot drift.
 * - No code outside this helper may assign __eintr_handler_callbacks[sig] or
 *   __eintr_handler_callbacks[sig].handler.
 */
static void __fbx_replace_action_locked(int sig, const struct k_sigaction *ksa,
	unsigned long user_flags)
{
	unsigned long *word = __fbx_handler_set+(sig-1)/(8*sizeof(long));
	unsigned long bit = 1UL<<(sig-1)%(8*sizeof(long));

	/* The host reads these separate objects without this lock, so it can still
	 * observe the stores between states. Keep them adjacent; closing that
	 * residual window is deliberately left to the separate seqlock task. */
	__eintr_handler_callbacks[sig] = *ksa;
	if (ksa->handler != SIG_DFL && ksa->handler != SIG_IGN)
		a_or_l(word, bit);
	else
		a_and_l(word, ~bit);
	__fbx_sa_flags[sig] = (uint32_t)user_flags;
}

/* firebox#35F — the process-wide `__wasm_signals_blocked` flag that used to
 * live here is GONE. __block_all_sigs/__block_app_sigs now fold into the
 * calling thread's `struct pthread::blocked_sigmask` (block.c), which is what
 * POSIX means by a signal mask; a process-wide flag let thread A's block
 * window swallow thread B's handler (firebox#NHJ). Every gate that read the
 * flag now reads the per-thread `__wasm_thread_sig_blocked()` alone.
 *
 * Process-wide pending-signal bitmask. Bit (sig-1) set means a signal
 * was raised during a blocked window and needs redelivery at
 * __restore_sigs time. Implemented as an array of ints for a_or /
 * a_and_l atomics; musl's sigset_t semantics (bit N-1) mirrored. */
#define __WASM_PENDING_WORDS ((_NSIG + 31) / 32)
volatile int __wasm_pending_sigs[__WASM_PENDING_WORDS];

/* firebox#8YM — the disposition surface is authored BY THE LIBC, not by each
 * consumer's link line.
 *
 * The seven symbols below are the host's ONLY window onto a signal's real
 * disposition: the guest installs one generic __wasm_signal dispatcher at libc
 * init, so from outside the module "SIG_DFL, no handler" and "user handler
 * installed" are indistinguishable. The host reads the truth out of these
 * objects' linear addresses via a non-executing MemoryView read.
 *
 * Until #8YM that surface existed only when a consumer passed
 * `-Wl,--export-if-defined=<sym>` for each symbol on its OWN link line. A user
 * who runs `git clone X && cd X && cmake . && make` inside `firebox run`
 * passes no such flag, so guest_handler_installed() returned None, the host
 * declined to terminate, and the program silently got UNFAITHFUL default
 * signal semantics. That is invariant 0: the faithful fix lives in the
 * substrate or invisibly in the toolchain we ship, NEVER on the user's command
 * line — and the correct denominator is every link that uses this libc, not
 * every build script somebody remembered to edit.
 *
 * `.export_name <sym>, <name>` sets WASM_SYMBOL_EXPORTED on the DATA symbol in
 * this object's linking-section entry, so wasm-ld exports it with no
 * command-line flag at all. sigaction.o is universally linked (crt1 calls
 * __wasi_init_signals below), so every program that links this libc carries
 * the surface. The export is an immutable global holding the symbol's linear
 * address — the exact shape the host already reads — so there is no new
 * import, no ABI change, and an older host simply ignores the extra exports.
 * Because it is a STATIC property of the module, a fork child that never
 * reruns CRT init still carries it.
 *
 * ⚠️ THE SET IS DEFINED IN BASH, AND THIS IS ITS SECOND HOME. The one home is
 * `scripts/lib/fbx-disposition-exports.sh:FBX_DISPOSITION_SYMBOLS` in the
 * firebox repo; a C translation unit in this fork cannot source it. These
 * lines were GENERATED from that variable, not retyped. Adding a symbol there
 * requires adding it here — `scripts/check-disposition-exports.sh` measures
 * the artifact and fails closed if you don't, which is what keeps the two
 * homes honest.
 *
 * ⚠️ DO NOT DELETE THESE BECAUSE A MEASUREMENT SHOWS THEM REDUNDANT. The
 * `libc.so` link line currently passes `--export-all`, which masks the
 * directive there (measured: identical export count with and without). The
 * directive is what makes the property survive anyone tightening that flag,
 * and what makes it true of a libc.so built by any other recipe. */
__asm__(".export_name __fbx_handler_set, __fbx_handler_set");
__asm__(".export_name __fbx_main_pthread, __fbx_main_pthread");
__asm__(".export_name __fbx_blocked_off, __fbx_blocked_off");
__asm__(".export_name __wasm_pending_sigs, __wasm_pending_sigs");
__asm__(".export_name __fbx_pending_off, __fbx_pending_off");
__asm__(".export_name __fbx_sigsuspend_off, __fbx_sigsuspend_off");
__asm__(".export_name __fbx_sa_flags, __fbx_sa_flags");

/* SA_NODEFER in-handler recursion guards: per-signal depth counters.
 * Written only from within __wasm_signal, so no atomic needed — the
 * WASM runtime delivers signals serially to a single thread and we
 * guard re-entrant dispatch via the SA_NODEFER check below.
 * firebox#TW2: this array is itself process-wide and has the same
 * thread-locality defect firebox#35F fixed for the block mask — thread
 * A inside a handler makes thread B pend its own. Tracked separately. */
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

/* firebox#C2Q / #HPT — POSIX realtime-signal queuing (depth + FIFO si_value).
 *
 * POSIX requires that a signal in the SIGRTMIN..SIGRTMAX range queued with
 * sigqueue(2) DELIVERS ONCE PER QUEUED INSTANCE, in FIFO order, each carrying
 * the application's `union sigval` (si_value), si_code == SI_QUEUE, and the
 * sender's si_pid/si_uid. (Standard signals 1..31 instead COALESCE — at most
 * one instance pends — which the existing one-bit-per-signal pending mask
 * already models faithfully; only RT signals need a real queue.)
 *
 * Two facts force this queue to live HERE, in the guest libc, rather than in
 * the existing pending bitmask or in the host:
 *   1. The pending store (self->pending_sigs[] / __wasm_pending_sigs[]) is a
 *      BITMASK — one bit per signal. It records presence, not depth, and has
 *      no slot for si_value. Queuing SIGRTMAX ten times sets one bit once, so
 *      both the count (10) and every value (1..10) are lost. That is the
 *      "Got signal 0, expected 1" failure (sigaction/29-1, #HPT) and the
 *      "queued 1x instead of N" depth loss (sigqueue/4-1, /8-1).
 *   2. The WASI signal seam carries NO payload in either direction:
 *      __wasi_thread_signal / __wasi_proc_signal take (tid/pid, signal) only,
 *      and the host->guest dispatch is __wasm_signal(int sig) — the number
 *      alone. So for a SELF-directed sigqueue (every queuing witness:
 *      sigqueue(getpid(),...)), the value never needs to cross the seam — it
 *      is already in the sender's (== receiver's) guest memory. We keep the
 *      full siginfo record locally in this ring; sigqueue ENQUEUES it before
 *      nudging the host, and the delivery consumers (the SA_SIGINFO async
 *      dispatch in __wasm_signal, and the synchronous sigtimedwait/sigwaitinfo
 *      accept) DEQUEUE it. (CROSS-PROCESS sigqueue si_value still needs a host
 *      signal-queue assist — that is #27M, a deliberate WASI-ABI extension,
 *      out of scope for this guest-only fix; sigqueue/1-1, sigwaitinfo/8-1.)
 *
 * The host coalesces the N self-nudges into a SINGLE __wasm_signal dispatch
 * (WasiThread::signal dedups its pending Vec — wasmer os/task/thread.rs), so
 * the guest, which owns the depth, redelivers the full ring on that one
 * dispatch (the drain loop in __wasm_signal below). One ring per RT signal
 * gives intra-signal FIFO; the existing low-to-high signal scan gives
 * inter-signal ordering (POSIX: lowest-numbered RT signal first).
 *
 * Inv 8 (upstream-ABI compat): this is a pure guest-libc data structure — the
 * WASI imports are untouched, so prebuilt wasmer.io artifacts still run. */
#define __FBX_RTSIG_MIN   35              /* == __libc_current_sigrtmin() */
#define __FBX_RTSIG_MAX   (_NSIG - 1)     /* 64 == __libc_current_sigrtmax() */
#define __FBX_RTSIG_N     (__FBX_RTSIG_MAX - __FBX_RTSIG_MIN + 1)   /* 30 */
/* firebox#VYD — per-signal RT queue depth. Raised 32 -> 128: regression/raise-race
 * cross-thread-kills a compute-bound worker 100x on ONE RT signal, so up to ~100
 * instances can be queued on a single signo before a drain point runs. 128 = the
 * test's worst case + margin. This is our RLIMIT_SIGPENDING analog (over-cap policy
 * in __fbx_rtsigq_push: EAGAIN for SI_QUEUE, degrade-to-coalesce for SI_USER/SI_TKILL
 * — both Linux-faithful). BSS cost: 30 sigs * 128 * sizeof(ent) ~= 92KB static, fine
 * in wasm linear memory. sysconf(_SC_SIGQUEUE_MAX) still reports the POSIX floor (32). */
#define __FBX_SIGQ_DEPTH  128             /* >= _POSIX_SIGQUEUE_MAX (32); #VYD RLIMIT_SIGPENDING analog */

/* Field names deliberately avoid si_pid/si_uid/si_value: those are <signal.h>
 * accessor MACROS for siginfo_t (e.g. si_pid ->
 * __si_fields.__si_common.__first.__piduid.si_pid), so a struct member named
 * `si_pid` would macro-expand to garbage. We store under plain names and copy
 * into the siginfo_t via the macros on the consumer side. */
struct __fbx_sigq_ent {
	union sigval value;
	int   code;
	pid_t pid;
	uid_t uid;
};
struct __fbx_sigq {
	volatile int lock[1];   /* futex lock, same discipline as __eintr_handler_lock */
	int head;               /* index of the FIFO head */
	int count;              /* live entries (0..__FBX_SIGQ_DEPTH) */
	struct __fbx_sigq_ent ent[__FBX_SIGQ_DEPTH];
};
static struct __fbx_sigq __fbx_rtsigq[__FBX_RTSIG_N];

static inline int __fbx_sig_is_rt(int sig) {
	return sig >= __FBX_RTSIG_MIN && sig <= __FBX_RTSIG_MAX;
}

/* Enqueue one queued siginfo record for RT signal `sig` (FIFO tail). A non-RT
 * signal has no queue: return 0 (success, no-op) so the caller proceeds with
 * the existing coalescing delivery. Returns 0 on success, -1 if this signal's
 * queue is full (caller maps to EAGAIN, the POSIX sigqueue over-limit error).
 * The lock is held only across the tiny record copy — never across a handler
 * call — so it cannot deadlock the cooperative dispatcher. */
int __fbx_rtsigq_push(int sig, union sigval value, int code,
                      pid_t pid, uid_t uid) {
	if (!__fbx_sig_is_rt(sig)) return 0;
	struct __fbx_sigq *q = &__fbx_rtsigq[sig - __FBX_RTSIG_MIN];
	int rc = 0;
	LOCK(q->lock);
	if (q->count >= __FBX_SIGQ_DEPTH) {
		rc = -1;
	} else {
		int tail = (q->head + q->count) % __FBX_SIGQ_DEPTH;
		q->ent[tail].value = value;
		q->ent[tail].code  = code;
		q->ent[tail].pid   = pid;
		q->ent[tail].uid   = uid;
		q->count++;
	}
	UNLOCK(q->lock);
	return rc;
}

/* Dequeue the FIFO-head queued record for RT signal `sig` into *si (clearing
 * *si first, then setting si_signo/si_code/si_value/si_pid/si_uid). Returns 1
 * if a record was dequeued, 0 if the queue was empty (caller builds the
 * minimal SI_USER siginfo for a raise()/kill()-delivered RT signal that
 * carries no queued value). The lock is dropped before *si is populated. */
int __fbx_rtsigq_pop_si(int sig, siginfo_t *si) {
	if (!si || !__fbx_sig_is_rt(sig)) return 0;
	struct __fbx_sigq *q = &__fbx_rtsigq[sig - __FBX_RTSIG_MIN];
	struct __fbx_sigq_ent e;
	int got = 0;
	LOCK(q->lock);
	if (q->count > 0) {
		e = q->ent[q->head];
		q->head = (q->head + 1) % __FBX_SIGQ_DEPTH;
		q->count--;
		got = 1;
	}
	UNLOCK(q->lock);
	if (!got) return 0;
	memset(si, 0, sizeof *si);
	si->si_signo = sig;
	si->si_code  = e.code;
	si->si_value = e.value;
	si->si_pid   = e.pid;
	si->si_uid   = e.uid;
	return 1;
}

/* Live queued-record count for RT signal `sig` (0 for a non-RT signal). */
int __fbx_rtsigq_count(int sig) {
	if (!__fbx_sig_is_rt(sig)) return 0;
	struct __fbx_sigq *q = &__fbx_rtsigq[sig - __FBX_RTSIG_MIN];
	int c;
	LOCK(q->lock);
	c = q->count;
	UNLOCK(q->lock);
	return c;
}

/* Re-mark RT signal `sig` pending after a synchronous accept (sigtimedwait/
 * sigwaitinfo) popped one queued record but MORE remain, so the pending bit
 * stays ≡ "this RT signal's queue is non-empty" and the next accept finds the
 * remaining FIFO instances (sigwaitinfo/7-1). Mirrors __wasm_pend_signal's bit
 * writes (per-thread + process-wide masks) minus the wake — the caller is
 * returning from the accept, not parking. */
void __fbx_rtsig_repend(int sig) {
	struct pthread *self = __pthread_self();
	int word = (sig - 1) / 32;
	int bit  = 1 << ((sig - 1) % 32);
	if (self) a_or((volatile int *)&self->pending_sigs[word], bit);
	a_or(&__wasm_pending_sigs[word], bit);
}

/* firebox#VYD — is a real USER handler installed for `sig` (as opposed to
 * SIG_DFL or SIG_IGN)? __fbx_handler_set records the bit ONLY for a real
 * handler (see __libc_sigaction: `sa_handler != SIG_DFL && != SIG_IGN`), so
 * this is the exact discriminator __wasm_raise_self needs to decide whether an
 * unblocked self-raise can be delivered synchronously in-guest (a handler runs)
 * versus routed to the host (which owns the SIG_DFL terminate/core/ignore
 * default + parent waitpid encoding). */
static inline int __fbx_handler_installed(int sig) {
	if (sig < 1 || sig >= _NSIG) return 0;
	return (__fbx_handler_set[(sig-1)/(8*sizeof(long))]
	        >> ((sig-1)%(8*sizeof(long)))) & 1UL;
}

/* firebox#VYD — clear ALL queued/pending signal state for the CALLING thread's
 * process. POSIX fork(2): "the child process shall have ... the set of signals
 * pending for the child process is cleared." Called from _Fork's child branch
 * (the child inherits a COPY of the parent's linear memory, so without this the
 * child's __fbx_rtsigq ring keeps the parent's queued RT records). This is both
 * the faithful "child pending set is empty" AND the fix that prevents the
 * async-fork-in-handler explosion regression/raise-race pins: handler1 fork()s
 * from INSIDE the RT ring-drain loop, so a child that inherited a non-empty ring
 * would re-enter the drain loop and fork grandchildren unboundedly. Clearing
 * here makes the child's `while(__fbx_rtsigq_pop_si(...))` re-check see an empty
 * ring and exit cleanly. Runs on the calling (sole surviving) thread only; the
 * host gives the child a fresh empty per-thread signal queue (proc_fork), so
 * this reconciles the guest depth store with the host's fresh state. */
void __fbx_clear_pending_on_fork(void) {
	/* Clear every per-signal RT ring. Reset lock words too — the child is
	 * single-threaded at this point, so any lock held by a now-gone sibling
	 * (or by this thread mid-op at the fork snapshot) must not wedge it. */
	for (int i = 0; i < __FBX_RTSIG_N; i++) {
		__fbx_rtsigq[i].lock[0] = 0;
		__fbx_rtsigq[i].head = 0;
		__fbx_rtsigq[i].count = 0;
	}
	/* Clear the process-wide + this-thread pending bitmasks. */
	for (int w = 0; w < __WASM_PENDING_WORDS; w++) {
		__wasm_pending_sigs[w] = 0;
	}
	struct pthread *self = __pthread_self();
	if (self) {
		for (int w = 0; w < ((_NSIG + 31) / 32); w++) {
			self->pending_sigs[w] = 0;
		}
	}
}

/* firebox#EMN — cross-process sigqueue si_value pull.
 *
 * A `sigqueue(pid, sig, value)` targeting ANOTHER process cannot deliver the
 * `si_value` through the machinery above: the forked receiver holds a SEPARATE
 * linear memory (so the sender's `__fbx_rtsigq` self-ring is unreachable), and
 * the WASI `proc_signal` seam is `(pid, signal)` only. The value round-trips
 * through the HOST instead — the sender routes an RT cross-process sigqueue
 * through `proc_signal_info` (sigqueue.c), the host stashes the payload on the
 * target process, and this helper (called from `__wasm_signal`'s SA_SIGINFO
 * delivery when the local self-ring is empty) pulls it back via `signal_info_get`.
 * Returns 1 and fills *si on a hit; 0 when the host has no queued cross-process
 * value for `sig` (a bare kill()/raise()), so the caller builds the SI_USER
 * default.
 *
 * Inv 8: `signal_info_get`/`proc_signal_info` are ADDITIVE WASIX imports — a
 * deliberate, recorded divergence, needed only because faithful cross-process
 * sigqueue payload delivery has no home in the (pid, signal) seam. A guest that
 * never sigqueues cross-process never emits them. */
#if defined(__wasm64__)
#define __FBX_WASIX_MODULE "wasix_64v1"
#else
#define __FBX_WASIX_MODULE "wasix_32v1"
#endif

extern int32_t __imported_fbx_signal_info_get(int32_t sig, void *ret_value,
                                              void *ret_pid)
	__attribute__((__import_module__(__FBX_WASIX_MODULE),
	               __import_name__("signal_info_get")));

/* firebox#4WF — host-provided si_code pull for a host-originated signal.
 *
 * A SIGCHLD delivered to a parent because a child was STOPPED or CONTINUED
 * carries si_code == CLD_STOPPED / CLD_CONTINUED on Linux (Open POSIX
 * sigaction/10-1). Firebox synthesises those edges in the host
 * (WasiProcess::set_stopped / set_continued), but the WASI (pid, signal)
 * delivery seam has no si_code field, so the guest cannot know the code the
 * host chose. This import returns it: 0 (SUCCESS) with *ret_code written when
 * the host has a code queued for `sig`, nonzero (ENOENT) for a bare
 * kill()/raise()-delivered signal (caller keeps SI_USER).
 *
 * Inv 8: ADDITIVE (a NEW import, not a widening of signal_info_get — widening
 * the 3-param signal_info_get would break every #EMN sigqueue guest already
 * emitting the 3-arg form). A guest that never handles a host-coded SIGCHLD
 * never emits this import, so prebuilt upstream wasmer.io artifacts are
 * unaffected. Arity: 2 params (i32 sig, ptr ret_code) -> i32 errno. */
extern int32_t __imported_fbx_signal_code_get(int32_t sig, void *ret_code)
	__attribute__((__import_module__(__FBX_WASIX_MODULE),
	               __import_name__("signal_code_get")));

static int __fbx_signal_info_fill_si(int sig, siginfo_t *si) {
	uint64_t value = 0;
	uint32_t spid = 0;
	/* 0 == __WASI_ERRNO_SUCCESS (payload written); nonzero == ENOENT (none). */
	if (__imported_fbx_signal_info_get(sig, &value, &spid) != 0)
		return 0;
	union sigval sv;
	memset(&sv, 0, sizeof sv);
	memcpy(&sv, &value, sizeof sv);   /* width-correct: 4 bytes wasm32, 8 wasm64 */
	memset(si, 0, sizeof *si);
	si->si_signo = sig;
	si->si_code  = SI_QUEUE;
	si->si_value = sv;
	si->si_pid   = (pid_t)spid;
	return 1;
}
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

/* firebox#AQH — SIGNAL_UNKILLABLE for the SIG_DFL "terminate" default action.
 *
 * WHEN THIS FUNCTION RUNS AT ALL. Only when the host could not PROVE our
 * disposition. The host applies the default action itself whenever it can read
 * the `__fbx_handler_set` mirror (wasmer `env.rs::first_no_handler_default_
 * terminate`); an unprovable disposition returns `None` there and is
 * deliberately LEFT ALONE, because reading "unknown" as "no handler" would
 * convert an absence into a kill. The signal then falls through to the
 * cooperative `__wasm_signal` dispatch, which lands here — and at this point
 * the guest is the only party that still knows the disposition is SIG_DFL.
 *
 * THE DEFECT, MEASURED 2026-08-05. Arm A3 vs A4 of #AQH's in-process harness:
 * the same guest (no handler, blocks forever), the same sysroot, the same link,
 * with the seven disposition exports stripped from an otherwise BYTE-IDENTICAL
 * module — so exactly one axis varies. Signal delivered through the shipped
 * `StopHandle` channel; the known-positive (identical guest WITH a handler)
 * exits 77 on both postures, so delivery is proven on the noexport posture too
 * and A3's outcome is a real measurement rather than a dead channel.
 *
 *   A4  exports present  ->  ForceKilled / 137   pid 1 SURVIVED the SIGTERM
 *   A3  exports absent   ->  ExitedOnSignal / 127 + a stderr line   pid 1 DIED
 *
 * Reproduced through the real CLI, `firebox run <prog>` on the same 2x2:
 * noexport pid-1 gave rc=127 and `Program recieved termination signal:
 * Terminated`, where the export arm survived to its own `_exit(66)`.
 *
 * pid 1 is `SIGNAL_UNKILLABLE`: the kernel drops a SIG_DFL fatal-default signal
 * aimed at the namespace child-reaper before delivery ("Attempted to kill
 * init!"), and only an out-of-namespace FORCED kill breaks through. The host
 * encodes exactly that rule (`WasiProcess::reaper_suppresses_default_signal`)
 * and A4 shows it working. Losing the rule because a SYMBOL was unreadable is
 * invariant 4's silent divergence, between two artifacts of the same program.
 *
 * WHY `getpid() == 1` IS THE RULE HERE AND NOT A LOOKALIKE. #AQH's own C5
 * correction objected that the guest cannot see namespace child-reaper
 * identity or force/ancestor origin, so a guest-side pid test would implement
 * a lookalike. Checked, and it holds on this substrate:
 *   - IDENTITY: the host stamps `SIGNAL_UNKILLABLE` at `pid == 1` and only
 *     there (`control_plane.rs::new_process`, never inherited by a fork child)
 *     — the same test, on the same numbering the guest reads back. Under
 *     `firebox run --init` the injected tini is pid 1 and the user program is
 *     pid 2, and `getpid()` reports 2, so the role still tracks. The one
 *     divergence would be a nested `unshare(CLONE_NEWPID)` init, and no
 *     nested-ns syscall exists yet (process.rs, the 3T8 (c) test says so).
 *   - FORCE: an ancestor `firebox stop`/`kill` does not come through here at
 *     all — `force_terminate_from_ancestor` flips the finished-status atomic
 *     directly. MEASURED: with this fix in place A3 reaps as ForceKilled/137,
 *     so ignoring here does NOT make pid 1 unstoppable.
 *   - DISPOSITION: this function is reached only for SIG_DFL by construction,
 *     which is the very fact the host was missing.
 *
 * WHAT IS DELIBERATELY NOT CHANGED — the pid >= 2 arm, byte for byte.
 * MEASURED 2026-07-30 on this task (`reports/witness-aqh/`, 20/20): at pid >= 2
 * a fork child raising with SIG_DFL is observed by its parent as
 * `WIFSIGNALED=1, WTERMSIG == the signal raised` — FAITHFUL, in both export
 * postures. Which layer supplies that status is UNATTRIBUTED (this task's C1),
 * and `abort()` is documented to race its own `_Exit`, so any edit below this
 * line — including deleting the `fprintf`, which would make the `abort()`
 * happen sooner — perturbs an unattributed race that currently lands right.
 * Replacing it with a "faithful" `_Exit(128 + sig)` would be a REGRESSION: the
 * number would match but the process would become `WIFEXITED`, where the
 * measurement says a parent sees `WIFSIGNALED`. The pid-1 return happens
 * FIRST, so pid 1 never reaches the `fprintf` either — the spurious line is
 * gone at the persona measured here, and survives at pid >= 2 as a separate,
 * separately-tracked inv-0 defect.
 *
 * WHY NOT `__wasi_proc_raise(sig)` — the "hand the signal back to the host"
 * shape an earlier #AQH design proposed, guarded by a one-shot latch. It is
 * provably useless, not merely risky: this function is reached ONLY on the
 * declining-probe posture, so a raise re-enters the same drain, declines
 * again, and dispatches straight back here. The latch would fire on the first
 * re-entry every single time and fall through to whatever follows it — so the
 * design buys nothing over doing that thing directly. On the posture where the
 * host CAN claim the signal, this function is never reached at all.
 */
static void terminate_handler(int sig) {
    /* Re-read the pid rather than caching it: `fork()` changes it, and a
     * cached 1 in a child would make an ordinary process unkillable. */
    if (getpid() == 1)
        return;
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
 * Defined above / in thread/pthread_sigmask.c. */
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
	/* firebox#93A — SIGCANCEL(33): act on a pending ASYNCHRONOUS cancel at this
	 * cooperative delivery boundary (the #8B5 loop-header poll, or any syscall
	 * drain). This is the RUNNING-thread half of the #5RE deferred-cancellation
	 * model, completing its consumer matrix: (running, deferred) acts at a
	 * cancellation point via __testcancel; (parked, async) acts via
	 * __testcancel_async at the wait wrapper (pthread_cancel.c); (running,
	 * async) — this arm — is the "any point" a compute-bound thread reaches.
	 * Without it, an async pthread_cancel of a `for(;;)` spinner set the
	 * cancel flag but never acted (no syscall → no cancellation point), so
	 * pthread_join hung forever (functional/pthread_cancel TIMEOUT).
	 *
	 * POSIX: async cancellation (PTHREAD_CANCEL_ASYNCHRONOUS) is defined to take
	 * effect at ANY point when cancellation is ENABLED. canceldisable is
	 * honored: inside a PTHREAD_CANCEL_DISABLE bracket (including __timedwait's
	 * internal bracket) this no-ops and the wait wrapper's __testcancel_async
	 * (which deliberately ignores that internal bracket) acts instead — no
	 * double-exit. A DEFERRED cancel (cancelasync==0) also no-ops here: the wake
	 * was the effect and __testcancel acts at the next cancellation point.
	 *
	 * pthread_exit runs the pthread_cleanup_push handler LIFO walk + TSD dtors +
	 * detach publish + join-wake and never returns; the interrupted computation
	 * is ABANDONED (no asyncify rewind — the host unwinds the JIT frames via
	 * WasiError::ThreadExit). Placed at the very top, before the reserved-range
	 * drop and before any lock/in-handler bookkeeping (__eintr_handler_lock,
	 * __wasm_in_handler), so the no-return exit strands no lock or dispatch
	 * state. Keyed on sig==SIGCANCEL only: every other signal is byte-unchanged.
	 * The reserved-range drop below then narrows to 32/34 (SIGTIMER/SIGSYNCCALL,
	 * unused by wasix-libc); SIGCANCEL is no longer among the dropped. */
	if (sig == SIGCANCEL) {
		struct pthread *self = __pthread_self();
		if (self && self->cancel && self->cancelasync
		    && self->canceldisable == PTHREAD_CANCEL_ENABLE)
			pthread_exit(PTHREAD_CANCELED);
		return;
	}
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

	/* Per-thread block — from pthread_sigmask, from a running handler's
	 * applied sa_mask, or from __block_all_sigs/__block_app_sigs (which
	 * firebox#35F folded into this same per-thread mask; they used to set a
	 * PROCESS-WIDE flag checked here, so a sibling thread's block window
	 * swallowed THIS thread's handler — firebox#NHJ). The signal is queued
	 * into the calling thread's own pending bitmask; pthread_sigmask on
	 * SIG_UNBLOCK (or SIG_SETMASK) and __restore_sigs drain and redeliver. */
	if (__wasm_thread_sig_blocked(sig)) {
		__wasm_pend_signal(sig);
		return;
	}

	LOCK(__eintr_handler_lock);
	struct k_sigaction ksa = __eintr_handler_callbacks[sig];
	/* SA_RESETHAND: reset handler to SIG_DFL atomically with snapshot,
	 * so a concurrent sigaction() on another thread sees the reset. */
	if (ksa.handler != 0 && (ksa.flags & SA_RESETHAND)) {
		const struct k_sigaction ksa_default = { .handler = SIG_DFL };
		__fbx_replace_action_locked(sig, &ksa_default, 0);
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
			void (*h3)(int, siginfo_t *, void *) =
				(void (*)(int, siginfo_t *, void *))(void *)ksa.handler;
			siginfo_t si;
			/* firebox#C2Q/#HPT — RT-signal queued delivery. If sigqueue
			 * enqueued one or more siginfo records for this RT signal,
			 * deliver ONE handler invocation per queued record, in FIFO
			 * order, each carrying the real si_value/si_code/si_pid/si_uid.
			 * Linux delivers every queued instance of a now-unblocked RT
			 * signal back-to-back; the host coalesces the N self-nudges into
			 * ONE dispatch (WasiThread::signal dedups its pending Vec), so
			 * the guest — which owns the depth — drains the whole ring on
			 * this single dispatch. For a non-RT signal, or an RT signal with
			 * no queued record (a raise()/kill()-delivered RT), fall back to
			 * the minimal SI_USER siginfo (musl's behavior for a signal that
			 * carries no queued value). The handler's sa_mask (applied above)
			 * holds for the whole drain — `sig` stays blocked for its
			 * duration, identical to the per-delivery mask Linux re-applies,
			 * since every drained instance is the same signal number. */
			if (__fbx_rtsigq_pop_si(sig, &si)) {
				do {
					if (on_alt) {
						__fbx_call_on_altstack_3(h3, sig, &si, NULL, alt_top);
					} else {
						h3(sig, &si, NULL);
					}
				} while (__fbx_rtsigq_pop_si(sig, &si));
			} else if (__fbx_sig_is_rt(sig) && __fbx_signal_info_fill_si(sig, &si)) {
				/* firebox#EMN — cross-process sigqueue si_value. The queued
				 * value was sent by ANOTHER process, so it lives in the HOST,
				 * not this process's `__fbx_rtsigq` ring (a forked child cannot
				 * see the sender's guest memory, and the WASI proc_signal seam
				 * carries no payload). `__fbx_signal_info_fill_si` pulls it via
				 * the `signal_info_get` import and populates the real si_value/
				 * si_code/si_pid, so the SA_SIGINFO handler observes the sent
				 * value instead of a zero (sigqueue/1-1). When the host has no
				 * queued value either, an RT dispatch delivers NOTHING (the
				 * firebox#PNB rule stated at the end of this branch); a non-RT
				 * signal falls through to the SI_USER default below. */
				if (on_alt) {
					__fbx_call_on_altstack_3(h3, sig, &si, NULL, alt_top);
				} else {
					h3(sig, &si, NULL);
				}
			} else if (!__fbx_sig_is_rt(sig)) {
				memset(&si, 0, sizeof si);
				si.si_signo = sig;
				si.si_code = SI_USER;
				/* firebox#4WF — a SIGCHLD raised because a child was STOPPED
				 * or CONTINUED carries si_code == CLD_STOPPED / CLD_CONTINUED
				 * (Open POSIX sigaction/10-1). The host stamps that code
				 * (set_stopped/set_continued) but the (pid, signal) WASI seam
				 * can't carry it, so pull it via the additive signal_code_get
				 * import. Scoped to SIGCHLD: it is the only signal for which
				 * the host synthesises a si_code, and gating here keeps every
				 * other SA_SIGINFO delivery free of the extra host round-trip.
				 * A bare kill()/raise() SIGCHLD (no host code) returns ENOENT
				 * and keeps the SI_USER default set above. */
				if (sig == SIGCHLD) {
					int32_t __fbx_code;
					if (__imported_fbx_signal_code_get(sig, &__fbx_code) == 0) {
						si.si_code = __fbx_code;
					}
				}
				if (on_alt) {
					__fbx_call_on_altstack_3(h3, sig, &si, NULL, alt_top);
				} else {
					h3(sig, &si, NULL);
				}
			}
			/* firebox#PNB — RT signal, NO record consumed => deliver ZERO.
			 *
			 * This is the #VYD spurious-doorbell rule, which the 1-arg RT
			 * branch below has had since #VYD but this SA_SIGINFO branch did
			 * not: the host's WasiThread::signal dedups its pending Vec, so a
			 * second (coalesced) dispatch can land after a prior dispatch
			 * already drained the ring. Falling through to the SI_USER default
			 * invoked the handler an EXTRA time per surplus doorbell — Linux
			 * never over-delivers an RT signal.
			 *
			 * Safe because every genuine same-process RT instance is recorded
			 * in the ring BEFORE its doorbell — sigqueue (SELF arm), kill
			 * (kill.c self-RT arm), pthread_kill, aio (__aio_notify_signal),
			 * timer_create, raise of a BLOCKED signal (raise.c #RSQ), and
			 * raise of an UNBLOCKED signal (__wasm_raise_self pushes exactly
			 * one record before dispatching) — and every external instance is
			 * recorded in the host stash, drained by the `fill_si` arm above.
			 * So "RT dispatch with zero records" is *only* a surplus doorbell.
			 * Non-RT keeps the SI_USER default (its pending bitmask has no
			 * depth, so its dispatch carries no record by construction).
			 *
			 * Measured (RC-2 probe, wasm32, 2026-07-26): Open POSIX
			 * lio_listio/{4-1,7-1,15-1} count SIGRTMIN+1 completions and
			 * assert an exact total. Across 80 instrumented runs the genuine
			 * SI_ASYNCIO records were correct in EVERY run (7/7, 8/8); the
			 * only errors were surplus invocations carrying si_code SI_USER —
			 * this fallback firing on an empty ring.
			 *
			 * NOT fixed here, deliberately: the `fill_si` arm above delivers
			 * the host stash ONCE though `fill_si` is a consuming pop, so N
			 * coalesced EXTERNAL records may be under-delivered — the mirror
			 * image of this over-count. That is a HYPOTHESIS with no
			 * measurement behind it (this probe's records were all
			 * same-process, i.e. the ring arm), so it is split to firebox#9MS
			 * rather than shipped unvalidated next to a measured fix. */
		} else if (__fbx_sig_is_rt(sig)) {
			/* firebox#VYD — 1-arg RT dispatch delivers RECORDS-ONLY.
			 *
			 * A 1-arg handler (installed via signal() or sigaction() without
			 * SA_SIGINFO — the regression/raise-race case) carries no siginfo,
			 * so it just needs the COUNT of queued instances. Drain the guest
			 * __fbx_rtsigq ring (self-raise via __wasm_raise_self/#RSQ, and
			 * cross-thread pthread_kill / self kill()/killpg via #VYD-G2), then
			 * the host external stash (cross-process kill/sigqueue via #EMN/#VYD-H1),
			 * calling the handler ONCE per record. This is the branch raise-race
			 * executes; before #VYD it called the handler once regardless of ring
			 * depth (under-delivering a queued RT signal to a 1-arg handler).
			 *
			 * If NO record is consumed, deliver ZERO (a spurious doorbell — the
			 * host WasiThread::signal Vec coalesces, so a second dispatch can land
			 * after the ring was already drained by a prior one; Linux never
			 * over-delivers). This closes the §3.4 over-count race that G2 opens.
			 * Every genuine RT instance is recorded exactly once before its
			 * doorbell (guest ring for same-process, host stash for external), so
			 * handler-invocation count == records consumed regardless of how many
			 * (coalesced) doorbells fire. The applied sa_mask holds across the
			 * whole drain (`sig` stays blocked), identical to Linux re-applying
			 * the mask per back-to-back delivery of the same signo. */
			siginfo_t si;
			while (__fbx_rtsigq_pop_si(sig, &si)) {
				if (on_alt) __fbx_call_on_altstack_1(ksa.handler, sig, alt_top);
				else ksa.handler(sig);
			}
			while (__fbx_signal_info_fill_si(sig, &si)) {
				if (on_alt) __fbx_call_on_altstack_1(ksa.handler, sig, alt_top);
				else ksa.handler(sig);
			}
			/* no record consumed -> deliver ZERO (spurious doorbell) */
		} else {
			/* Standard signal (1..31): at-most-one-pending coalescing — deliver
			 * ONCE (the pending bitmask has no depth; unchanged pre-#VYD path). */
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

/* firebox#VYD — deliver every queued RT signal that is currently deliverable on
 * THIS thread, synchronously in-guest. This is the raise()-boundary drain: Linux
 * delivers all deliverable pending signals at every kernel->user transition, so
 * each of regression/raise-race's 1000 raise() calls is a delivery point for the
 * 100 cross-thread SIGRTMIN+1 kills main fires concurrently — which is what lets
 * them land DURING the raise loop (where handler1's fork() hits `if(child)_exit`)
 * rather than during the later `while(c1<100)` spin (where a fork would inherit a
 * frozen c1 and hang — exactly Linux's own timing envelope for this test).
 *
 * Ring-DRIVEN (not pending-bit-driven): dispatches __wasm_signal(s) for every RT
 * signal with a non-empty ring, an installed user handler, unblocked on this
 * thread, and not already mid-dispatch. __wasm_signal drains the whole ring for
 * `s` (the #VYD 1-arg / SA_SIGINFO drain), so ONE call delivers the full queued
 * depth. Ring-driven so it is robust against the post-handler __wasm_drain_pending
 * host-re-raise clearing the pending bit while the ring records remain (that path
 * re-raises via the host, which DEFERS a self-target — thread_signal.rs — so it
 * cannot deliver inline; the ring is the depth truth and we drain it directly).
 *
 * We call __wasm_signal directly (as __wasm_raise_self's NODEFER arm already
 * does) — NOT __wasi_thread_signal — precisely because a self-target host nudge
 * is deferred, not dispatched. */
void __wasm_deliver_pending_rt_inline(void) {
	/* firebox#35F — the process-wide `__wasm_signals_blocked` early-return
	 * that stood here is gone; the per-signal `__wasm_thread_sig_blocked(s)`
	 * test in the loop below is the same predicate, correctly scoped to THIS
	 * thread, and now also carries __block_all_sigs/__block_app_sigs. */
	for (int s = __FBX_RTSIG_MIN; s <= __FBX_RTSIG_MAX; s++) {
		if (__fbx_rtsigq_count(s) <= 0) continue;
		if (!__fbx_handler_installed(s)) continue;
		if (__wasm_in_handler[s] > 0) continue;
		if (__wasm_thread_sig_blocked(s)) continue;
		__wasm_signal(s);
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
	if (__wasm_in_any_handler() > 0) {
		/* IN-HANDLER cases (existing, unchanged). */
		/* Blocked by this thread's mask — which carries the active handler's
		 * applied sa_mask, any pthread_sigmask block, and (firebox#35F) any
		 * __block_all_sigs/__block_app_sigs window: hold pending in-guest so
		 * sigpending() reports it. */
		if (__wasm_thread_sig_blocked(sig)) {
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

	/* firebox#VYD — TOP-LEVEL (not in any handler) unblocked RT self-raise with
	 * a real user handler installed: deliver SYNCHRONOUSLY in-guest, matching
	 * Linux's "an unblocked self-tkill delivers on the syscall-return path before
	 * raise() returns". This is what makes regression/raise-race's worker reach
	 * c0 == 1000 (1000 self-raise(SIGRTMIN) each deliver handler0 once) — before
	 * #VYD these coalesced into a single host-deferred delivery (the compute-bound
	 * worker makes no blocking syscall, so the host drain never runs → c0 == 1).
	 *
	 * Scoped narrowly so nothing else changes:
	 *   - RT only. A standard-signal self-raise keeps its host-deferred path
	 *     (at-most-one-pending coalescing is already conformant; no regression).
	 *   - Real user handler only (__fbx_handler_installed). SIG_DFL / SIG_IGN keep
	 *     the host path (host owns terminate/core/ignore + parent waitpid encoding).
	 *   - Unblocked only. A blocked RT self-raise keeps the host path and pends via
	 *     raise.c's #RSQ ring push.
	 *   - Not already mid-dispatch for `sig` (the in-handler arm above owns that).
	 *
	 * Push exactly ONE ring record then dispatch: __wasm_signal's #VYD RT 1-arg
	 * drain delivers exactly the records present (deliver-zero on none), so a
	 * synchronous self-raise must enqueue its own instance for the drain to find.
	 * Net-zero ring depth (pushed then immediately popped) — exactly Linux's
	 * queue-one-then-deliver-one for an unblocked self-directed RT signal. No host
	 * round-trip (a host nudge after in-guest delivery would double-deliver).
	 *
	 * Then drain any OTHER deliverable queued RT signal inline — the raise()
	 * boundary drain that lands raise-race's 100 cross-thread SIGRTMIN+1 kills
	 * DURING the raise loop (§__wasm_deliver_pending_rt_inline). */
	if (!__fbx_sig_is_rt(sig)) return 0;
	if (__wasm_thread_sig_blocked(sig)) return 0;
	if (!__fbx_handler_installed(sig)) return 0;
	if (__wasm_in_handler[sig] > 0) return 0;
	{
		union sigval sv;
		memset(&sv, 0, sizeof sv);
		(void)__fbx_rtsigq_push(sig, sv, SI_USER, getpid(), getuid());
	}
	__wasm_signal(sig);
	__wasm_deliver_pending_rt_inline();
	return 1;
}

/* Compatibility-only export. NOTHING IN THIS LIBC REQUESTS IT ANY MORE.
 *
 * firebox#35F removed the `__wasi_callback_signal("__wasm_signal_blocked")`
 * swap from __block_all_sigs/__block_app_sigs. (`__restore_sigs`'s
 * `__wasi_callback_signal("__wasm_signal")` STAYS — it is not the matching
 * swap-back any more but the post-fork re-arm; see the ⚠️ in block.c.) That
 * swap was process-wide — it retargeted the host's
 * single `inner.signal` slot for the whole instance — so it was the same
 * thread-locality defect as the flag, in a second guise, with a strictly worse
 * failure mode: a signal delivered to the blocking thread itself during the
 * window reached this no-op and was CONSUMED without even being pended.
 * Keeping __wasm_signal registered and letting the per-thread mask pend is the
 * faithful behavior.
 *
 * The export stays so an OLDER runtime, or an older object linked against this
 * libc, that still requests this callback name resolves it rather than seeing
 * "export not found" and setting inner.signal = None (which disables dispatch
 * process-wide). Delete it only once no shipped runtime requests the name. */
__attribute__((export_name("__wasm_signal_blocked")))
void __wasm_signal_blocked(int sig) {
	(void)sig;
}
#endif

int __libc_sigaction(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	struct k_sigaction ksa, ksa_old;
#ifndef __wasilibc_unmodified_upstream
	/* Reject before callback registration, state changes, or sig-based indexing.
	 * A pure SIGKILL/SIGSTOP query remains valid; only replacement is forbidden. */
	if (sig-32U < 3 || sig-1U >= _NSIG-1 ||
	    (sa && (sig == SIGKILL || sig == SIGSTOP)))
		return __syscall_ret(EINVAL);
#endif
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
	LOCK(__eintr_handler_lock);
	ksa_old = __eintr_handler_callbacks[sig];
	if (sa)
		__fbx_replace_action_locked(sig, &ksa, sa->sa_flags);
	UNLOCK(__eintr_handler_lock);
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

	/* firebox#9AV — SIGKILL/SIGSTOP have a FIXED disposition that POSIX makes
	 * unchangeable, so #XCJ's `sigaction(SIGKILL, non-NULL, ...) -> EINVAL`
	 * guard is correct and stays. But this function is NOT the POSIX API: it is
	 * the host->guest disposition-RESTORE ABI, replaying a set the parent
	 * already holds. Replaying "SIGKILL is at its default" is a request for the
	 * state the guest is already in — a satisfied no-op, not a failed set — so
	 * report success without routing through __sigaction. Answering -1 here made
	 * __wasi_init_signals kill the guest before main (exit 71) for every parent
	 * whose SETSIGDEF set named SIGKILL, which is what libuv's sigfillset() does
	 * and what tini was hand-patched to avoid one guest at a time.
	 *
	 * IGNORE is deliberately NOT excused: an uncatchable signal cannot be
	 * ignored, that state is unrepresentable, and reporting success for it would
	 * be a false success (invariant 0). It stays -1/EINVAL — and per the loop in
	 * __wasi_init_signals a rejected entry no longer terminates the guest. */
	if (sig == SIGKILL || sig == SIGSTOP) {
		if (a == SIG_DFL) {
			return 0;
		}
		errno = EINVAL;
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
/* firebox#FD6: real prototypes, not K&R empty-parens. Under clang 22 / C23 an
 * empty `()` means `(void)`, which CONFLICTS with <setjmp.h>'s real prototypes
 * `int sigsetjmp(sigjmp_buf,int)` / `_Noreturn void siglongjmp(sigjmp_buf,int)`
 * (-Werror -Wdeprecated-non-prototype) whenever that header is in scope, breaking
 * every from-scratch EH libc rebuild. So match the header.
 * firebox#N2R: but the real prototypes reference the `sigjmp_buf` TYPEDEF, which
 * lives in <setjmp.h> — and sigaction.c only includes <signal.h>. In the EH
 * variant <setjmp.h> happened to be transitively in scope; in the --pic / static
 * / --threaded variants it is NOT, so clang K&R-parsed `sigsetjmp(sigjmp_buf,int)`
 * (sigjmp_buf as an unknown param NAME) and errored "expected identifier" at
 * `int`, breaking every from-scratch non-EH rebuild. Include <setjmp.h>
 * explicitly so `sigjmp_buf` is a visible type and the prototypes match the header
 * in ALL variants. (mknodat below keeps `()` — no conflicting prototype is in
 * scope for it, so it does not error; only address-taken for force-link.) */
#include <setjmp.h>
extern int sigsetjmp(sigjmp_buf, int);
extern _Noreturn void siglongjmp(sigjmp_buf, int);
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

	/* firebox#9AV — a disposition entry we cannot apply must NEVER be fatal.
	 *
	 * This loop replays the disposition set inherited from the parent. On Linux
	 * there is no execve path where inheriting a disposition set can kill the
	 * child before main: unhandleable entries simply do not take effect. The
	 * previous `_Exit(EX_OSERR)` gave this host-supplied array veto power over
	 * whether the guest runs at all — one odd entry and the process died at
	 * exit 71 with no output, before main, indistinguishable from a crash.
	 *
	 * That is the class: the fatal-on-reject shape, not any one rejected signo.
	 * SIGKILL (the #XCJ interaction that surfaced this) is now a no-op success
	 * inside __wasm_sigaction, but an entry carrying an unknown disposition
	 * enum, a reserved RT signo, or an out-of-range signo would land here just
	 * the same. Skipping leaves that one signal at its default disposition —
	 * recoverable and observable; _Exit is neither. */
	for (int i = 0; i < signal_count; ++i) {
		sigaction_ret = __wasm_sigaction((int)sig_dispositions[i].sig, (int)sig_dispositions[i].disp);
		(void)sigaction_ret;
	}
	errno = 0;

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
