#include <time.h>
#include <setjmp.h>
#include <limits.h>
#include "pthread_impl.h"
#include "atomic.h"

#ifdef __wasilibc_unmodified_upstream /* WASI has no Linux timer syscalls */

struct ksigevent {
	union sigval sigev_value;
	int sigev_signo;
	int sigev_notify;
	int sigev_tid;
};

struct start_args {
	pthread_barrier_t b;
	struct sigevent *sev;
};

static void dummy_0()
{
}
weak_alias(dummy_0, __pthread_tsd_run_dtors);

static void cleanup_fromsig(void *p)
{
	pthread_t self = __pthread_self();
	__pthread_tsd_run_dtors();
	self->cancel = 0;
	self->cancelbuf = 0;
	self->canceldisable = 0;
	self->cancelasync = 0;
	__reset_tls();
	longjmp(p, 1);
}

static void *start(void *arg)
{
	pthread_t self = __pthread_self();
	struct start_args *args = arg;
	jmp_buf jb;

	void (*notify)(union sigval) = args->sev->sigev_notify_function;
	union sigval val = args->sev->sigev_value;

	pthread_barrier_wait(&args->b);
	for (;;) {
		siginfo_t si;
		while (sigwaitinfo(SIGTIMER_SET, &si) < 0);
		if (si.si_code == SI_TIMER && !setjmp(jb)) {
			pthread_cleanup_push(cleanup_fromsig, jb);
			notify(val);
			pthread_cleanup_pop(1);
		}
		if (self->timer_id < 0) break;
	}
	__syscall(SYS_timer_delete, self->timer_id & INT_MAX);
	return 0;
}

int timer_create(clockid_t clk, struct sigevent *restrict evp, timer_t *restrict res)
{
	volatile static int init = 0;
	pthread_t td;
	pthread_attr_t attr;
	int r;
	struct start_args args;
	struct ksigevent ksev, *ksevp=0;
	int timerid;
	sigset_t set;

	switch (evp ? evp->sigev_notify : SIGEV_SIGNAL) {
	case SIGEV_NONE:
	case SIGEV_SIGNAL:
	case SIGEV_THREAD_ID:
		if (evp) {
			ksev.sigev_value = evp->sigev_value;
			ksev.sigev_signo = evp->sigev_signo;
			ksev.sigev_notify = evp->sigev_notify;
			if (evp->sigev_notify == SIGEV_THREAD_ID)
				ksev.sigev_tid = evp->sigev_notify_thread_id;
			else
				ksev.sigev_tid = 0;
			ksevp = &ksev;
		}
		if (syscall(SYS_timer_create, clk, ksevp, &timerid) < 0)
			return -1;
		*res = (void *)(intptr_t)timerid;
		break;
	case SIGEV_THREAD:
		if (!init) {
			struct sigaction sa = { .sa_handler = SIG_DFL };
			__libc_sigaction(SIGTIMER, &sa, 0);
			a_store(&init, 1);
		}
		if (evp->sigev_notify_attributes)
			attr = *evp->sigev_notify_attributes;
		else
			pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_barrier_init(&args.b, 0, 2);
		args.sev = evp;

#ifdef __wasilibc_unmodified_upstream
		__block_app_sigs(&set);
#endif
		__syscall(SYS_rt_sigprocmask, SIG_BLOCK, SIGTIMER_SET, 0, _NSIG/8);
		r = __pthread_create(&td, &attr, start, &args);
#ifdef __wasilibc_unmodified_upstream
		__restore_sigs(&set);
#endif
		if (r) {
			errno = r;
			return -1;
		}

		ksev.sigev_value.sival_ptr = 0;
		ksev.sigev_signo = SIGTIMER;
		ksev.sigev_notify = SIGEV_THREAD_ID;
		ksev.sigev_tid = td->tid;
		if (syscall(SYS_timer_create, clk, &ksev, &timerid) < 0)
			timerid = -1;
		td->timer_id = timerid;
		pthread_barrier_wait(&args.b);
		if (timerid < 0) return -1;
		*res = (void *)(INTPTR_MIN | (uintptr_t)td>>1);
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	return 0;
}

#else /* !__wasilibc_unmodified_upstream — firebox#GGW */

/* firebox#GGW — faithful POSIX per-process timers (timer_create /
 * timer_settime / timer_gettime / timer_delete / timer_getoverrun) on the
 * Firebox substrate.
 *
 * WHY THIS IS A GROUND-UP REIMPLEMENTATION, NOT A WIRE-UP OF MUSL'S SOURCES:
 * upstream musl's timer_*.c (preserved verbatim above under the
 * __wasilibc_unmodified_upstream guard) marshals every operation to a Linux
 * kernel timer syscall — SYS_timer_create/settime/gettime/delete/getoverrun,
 * plus SYS_tkill / SYS_rt_sigprocmask for the SIGEV_THREAD helper. The kernel
 * owns the timer: it counts down, fires the signal, and tracks overruns.
 * wasm32-wasi/wasm64 have NONE of those syscalls, so the machinery cannot be
 * "enabled" — the timekeeping, expiry, delivery and overrun accounting must
 * live in the guest, backed by primitives Firebox DOES provide faithfully:
 * real threads (pthreads), CLOCK_REALTIME/CLOCK_MONOTONIC (clock_gettime),
 * and cross-thread signal delivery that honors the target's block mask
 * (__wasi_thread_signal → the host's __wasm_signal, which pends a blocked
 * signal into self->pending_sigs[] + __wasm_pending_sigs[] and wakes a
 * sigwait/sigtimedwait parked on it — see sigtimedwait.c).
 *
 * ARCHITECTURE: a SINGLE per-process manager thread services ALL timers from a
 * fixed table. It sleeps on a CLOCK_MONOTONIC condvar until the nearest armed
 * expiry (or until a create/settime/delete signals it), then fires every due
 * timer. This matches Linux's resource model (no thread-per-timer; Open POSIX
 * timer_create/speculative/2-1 creates 256 timers and must not exhaust
 * resources) and keeps SIGEV_THREAD notifications off the manager's critical
 * path (each notification spawns a detached thread, as POSIX specifies "the
 * function shall be executed as if it were the start_routine of a new thread").
 *
 * NOTIFICATION → Firebox primitive mapping:
 *   SIGEV_SIGNAL: deliver sigev_signo to the timer's CREATING thread via
 *     __wasi_thread_signal (the thread that installed the handler / will
 *     sigwait). Process-directed __wasi_proc_signal is deliberately NOT used:
 *     it bypasses the in-guest block mask and would terminate the process on a
 *     blocked timer signal (the same hazard sigqueue.c documents). The
 *     thread-directed path pends-when-blocked (sigwait/getoverrun tests) and
 *     interrupts-and-runs-the-handler-when-unblocked (nanosleep tests) — the
 *     two behaviors every Open POSIX timer test relies on. For a realtime
 *     signal we additionally push an SI_TIMER siginfo record (with
 *     sigev_value) into the shared __fbx_rtsigq ring so an SA_SIGINFO / accept
 *     consumer sees si_code==SI_TIMER and the queued value, mirroring
 *     pthread_kill.c's RT-depth feed.
 *   SIGEV_THREAD: a detached notification thread runs sigev_notify_function
 *     with sigev_value, one thread per timer at a time (musl's model;
 *     fbx_notify_thread).
 *   SIGEV_NONE: no notification; the timer still counts down so timer_gettime
 *     reflects remaining time.
 *
 * OVERRUN accounting (timer_getoverrun, si_overrun): Linux's — at most one
 * expiry in flight, the timer re-armed and its overrun fixed when that expiry
 * is consumed. firebox#SGV, at `enum { FBX_IDLE ...` below, has the whole
 * argument.
 *
 * FORK: POSIX — timers are NOT inherited across fork(). The child inherits the
 * table's MEMORY but not the manager thread, so an inherited timer is inert.
 * We make that faithful+safe two ways: (1) a pid guard resets the table on the
 * first timer call in a new process, and (2) a pthread_atfork child handler
 * resets the table and reinitializes the lock/cond (covering the case where
 * fork raced the manager holding the lock). A post-fork child that creates its
 * own timer therefore gets a fresh manager (Open POSIX timer_create/8-1,
 * fork/18-1, functional/twoptimers). */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <common/clock.h>
#include <wasi/api.h>

/* Cross-thread signal delivery + RT-depth machinery (shared with raise.c /
 * pthread_kill.c / sigtimedwait.c). __fbx_rtsigq_push_timer records an
 * SI_TIMER siginfo for a realtime signal (and nothing for a standard one).
 * __wasm_pending_sigs[] is the process-wide pending bitmask. */
extern int __fbx_rtsigq_push_timer(int sig, union sigval value, int code,
                                   pid_t pid, uid_t uid, int timer);
extern volatile int __wasm_pending_sigs[];

#define FBX_TIMER_MAX 512   /* > 256 so timer_create/speculative/2-1 never
                             * hits EAGAIN before its 256-timer check. */

/* firebox#SGV — the life of one expiry's notification (`state`).
 *
 * Linux sends a timer's signal from one preallocated sigqueue entry. While
 * that entry is queued the timer is not re-armed; when the signal is dequeued
 * (delivered to a handler or default action, or accepted by sigwait) the
 * kernel forwards the timer past "now", and the number of intervals it skipped
 * is the overrun, kept as it_overrun_last for si_overrun and
 * timer_getoverrun. The count is exact arithmetic, capped at DELAYTIMER_MAX.
 *
 * This file counted instead: each manager wake that found the signal still
 * pending added 1, and the catch-up loop skipped the intervals between wakes
 * uncounted. A 1-2 us interval gets a handful of wakes in a second, so Open
 * POSIX timer_getoverrun/2-2 and 2-3 read 0 where Linux reads 75+ and
 * 499999. The live count was also reset by the manager's next wake, not by
 * the acceptance, and si_overrun was never filled.
 *
 * Now the timer follows Linux:
 *   IDLE        armed or not, nothing in flight.
 *   SENT        the manager sent the expiry scheduled at `e0`. No further
 *               expiry is sent while SENT.
 *   CONSUMED    the signal was consumed. The consumer computed `overrun_last`
 *               from `e0` and its own clock reading and stored the re-arm
 *               point; the manager, kicked, moves `next` there and returns the
 *               timer to IDLE.
 *
 * Consumption is reported by __fbx_timer_signal_consumed, which sigaction.c's
 * dispatcher and sigtimedwait.c call for every instance they consume. It runs
 * in signal context, maybe on a thread that holds g_lock, so it takes no lock:
 * it claims SENT -> CONSUMED with a CAS and kicks the manager through a futex
 * word. SIGEV_THREAD timers consume in their notification thread, below. */
enum { FBX_IDLE = 0, FBX_SENT = 1, FBX_CONSUMED = 2 };

/* A SENT signal that no guest consumer reported (the host disposed of it
 * itself) is recovered by the manager after this long, if the owner does not
 * block the signal and no pending bit shows it. 50 ms is far above any
 * delivery latency of an unblocked signal. */
#define FBX_LOST_NS 50000000LL

struct fbx_timer {
	int             in_use;
	int             armed;
	unsigned        gen;         /* bumped by create and delete */
	clockid_t       clock;
	int             notify;      /* SIGEV_SIGNAL / SIGEV_NONE / SIGEV_THREAD */
	int             signo;       /* SIGEV_SIGNAL */
	union sigval    value;       /* sigev_value */
	void          (*notify_fn)(union sigval); /* SIGEV_THREAD */
	pthread_attr_t  attr;        /* SIGEV_THREAD sigev_notify_attributes */
	int             has_attr;
	struct pthread *owner;       /* creating thread — SIGEV_SIGNAL target */
	struct timespec next;        /* absolute expiry in `clock` domain */
	struct timespec interval;    /* {0,0} => one-shot */
	/* firebox#SGV — the in-flight notification (see the state comment). */
	volatile int    state;
	struct timespec e0;          /* scheduled expiry that was sent */
	struct timespec sent_at;     /* manager clock reading at the send */
	struct timespec rearm;       /* CONSUMED: the next expiry */
	volatile int    overrun_last;
	int             helper;      /* SIGEV_THREAD: notification thread alive */
	int             sent_blocked; /* the owner blocked signo when it was sent */
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_mgr_started;
static int             g_atfork_done;
static pid_t           g_owner_pid;
static struct fbx_timer g_timers[FBX_TIMER_MAX];
/* firebox#SGV — the manager parks on this word, not on a condvar: a
 * consumption is reported from signal context, which may not take g_lock, and
 * signalling a condvar without its mutex can be lost between the manager's
 * scan and its wait. Every state change increments it and wakes. */
static volatile int    g_kick;
/* Signals with a SENT timer, so a consumption of any other signal costs a
 * bit test. Set on send; cleared only by the manager's rescan. */
static volatile int    g_sent_sigs[(_NSIG + 31) / 32];

static void fbx_kick(void)
{
	a_inc(&g_kick);
	__wake(&g_kick, 1, 1);
}

/* ---- timespec helpers ---- */

static int ts_ge(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec != b->tv_sec) return a->tv_sec > b->tv_sec;
	return a->tv_nsec >= b->tv_nsec;
}

static void ts_add(struct timespec *a, const struct timespec *b)
{
	a->tv_sec += b->tv_sec;
	a->tv_nsec += b->tv_nsec;
	if (a->tv_nsec >= 1000000000L) { a->tv_sec++; a->tv_nsec -= 1000000000L; }
}

/* out = max(a - b, 0) */
static void ts_diff(const struct timespec *a, const struct timespec *b,
                    struct timespec *out)
{
	if (!ts_ge(a, b)) { out->tv_sec = 0; out->tv_nsec = 0; return; }
	out->tv_sec = a->tv_sec - b->tv_sec;
	out->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (out->tv_nsec < 0) { out->tv_sec--; out->tv_nsec += 1000000000L; }
}

static int ts_lt(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec;
	return a->tv_nsec < b->tv_nsec;
}

static long long ts_ns(const struct timespec *a)
{
	return (long long)a->tv_sec * 1000000000LL + a->tv_nsec;
}

static void ts_add_ns(struct timespec *a, long long ns)
{
	a->tv_sec += ns / 1000000000LL;
	a->tv_nsec += ns % 1000000000LL;
	if (a->tv_nsec >= 1000000000L) { a->tv_sec++; a->tv_nsec -= 1000000000L; }
}

/* Read "now" in a timer's own clock domain (firebox#CPU: the CPU-time clocks
 * are real host-accounted clocks now, not CLOCK_MONOTONIC stand-ins). Returns
 * nonzero, with *out zeroed, when the clock can no longer be read (a thread
 * CPU clock whose thread has exited). */
static int ts_now(clockid_t clk, struct timespec *out)
{
	if (clock_gettime(clk, out) != 0) {
		out->tv_sec = 0;
		out->tv_nsec = 0;
		return -1;
	}
	return 0;
}

static int fbx_periodic(const struct fbx_timer *t)
{
	return t->interval.tv_sec || t->interval.tv_nsec;
}

/* firebox#SGV — the Linux dequeue arithmetic. For the expiry `e0` consumed at
 * `now`: overrun = whole intervals elapsed since e0, capped at
 * DELAYTIMER_MAX; the re-arm point is the first expiry after now. */
static int fbx_overrun_at(const struct fbx_timer *t, const struct timespec *now,
                          struct timespec *rearm)
{
	if (!fbx_periodic(t)) return 0;
	long long iv = ts_ns(&t->interval);
	long long d = ts_ge(now, &t->e0) ? ts_ns(now) - ts_ns(&t->e0) : 0;
	long long k = d / iv;
	if (rearm) {
		*rearm = t->e0;
		ts_add_ns(rearm, (k + 1) * iv);
	}
	return k > DELAYTIMER_MAX ? DELAYTIMER_MAX : (int)k;
}

/* SENT -> CONSUMED at `now`. Lock-free: called from signal context. Returns
 * the overrun, or -1 if another consumer claimed the expiry first. Between the
 * CAS and the final store the state is FBX_CLAIMING; that stretch is pure
 * arithmetic (no host call, so no signal can be dispatched inside it), which
 * is what lets a g_lock holder wait it out (fbx_quiesce). */
#define FBX_CLAIMING (FBX_SENT + 16)
static int fbx_consume_as(struct fbx_timer *t, const struct timespec *now, int dequeued)
{
	if (a_cas(&t->state, FBX_SENT, FBX_CLAIMING) != FBX_SENT) return -1;
	struct timespec rearm;
	int ov = fbx_overrun_at(t, now, &rearm);
	t->rearm = rearm;
	if (dequeued) a_store(&t->overrun_last, ov);
	a_store(&t->state, FBX_CONSUMED);
	return ov;
}

static int fbx_consume(struct fbx_timer *t, const struct timespec *now)
{
	return fbx_consume_as(t, now, 1);
}

/* firebox#SGV — was this expiry DEQUEUED (it sets it_overrun_last) or
 * DISCARDED at generation (it does not)? Linux discards a signal whose action
 * is to ignore it, SIG_DFL for SIGCONT/SIGCHLD/SIGURG/SIGWINCH included, when
 * it is generated and not blocked; a blocked one is queued and dequeued at the
 * unblock. Open POSIX timer_getoverrun/2-1..2-3 block SIGCONT, sleep, unblock
 * and read the overrun of THAT dequeue. With a 1-2 us interval the timer
 * re-arms and fires again before timer_getoverrun runs; that expiry, sent
 * while unblocked, reaches a default-ignore action and must not overwrite the
 * count. A consumer with a handler (si != NULL) is always a dequeue. */
static int fbx_dequeued(const struct fbx_timer *t, const siginfo_t *si)
{
	return si != NULL || t->sent_blocked;
}

/* g_lock held: wait out a consumer inside fbx_consume's claim. */
static void fbx_quiesce(struct fbx_timer *t)
{
	while (t->state == FBX_CLAIMING) a_spin();
}

/* Signal context: does `owner` block `sig`? (pthread_sigmask.c's
 * __wasm_thread_sig_blocked reads the same word, for the calling thread.) */
static int fbx_owner_blocks(const struct fbx_timer *t)
{
	if (!t->owner) return 0;
	int sig = t->signo;
	unsigned long w = t->owner->blocked_sigmask[(sig - 1) / (8 * sizeof(long))];
	return (w >> ((sig - 1) % (8 * sizeof(long)))) & 1;
}

static int fbx_shows_pending(const struct fbx_timer *t)
{
	int word = (t->signo - 1) / 32;
	int bit  = 1 << ((t->signo - 1) % 32);
	int owner_bits = t->owner ? ((volatile int *)t->owner->pending_sigs)[word] : 0;
	return ((owner_bits | __wasm_pending_sigs[word]) & bit) != 0;
}

/* firebox#SGV — the consumption hook (declared weak in sigaction.c and
 * sigtimedwait.c). `si`, when non-null, is the siginfo the consumer will hand
 * out: an RT instance's record names its timer (si_timerid); a standard
 * signal's instance carries no record (si_code SI_USER), so the first SENT
 * timer on that signal is the one consumed, as on Linux, where a second timer
 * on a pending standard signal does not queue. Fills si_code, si_value,
 * si_timerid and si_overrun for the consumed expiry. */
void __fbx_timer_signal_consumed(int sig, siginfo_t *si)
{
	if (sig < 1 || sig >= _NSIG) return;
	int word = (sig - 1) / 32;
	int bit  = 1 << ((sig - 1) % 32);
	if (!(g_sent_sigs[word] & bit)) return;
	int want = 0;
	if (si) {
		if (si->si_code == SI_TIMER) want = si->si_timerid;
		else if (si->si_code != SI_USER) return;   /* someone else's instance */
	}
	for (int i = 0; i < FBX_TIMER_MAX; i++) {
		struct fbx_timer *t = &g_timers[i];
		if (want && i + 1 != want) continue;
		if (t->state != FBX_SENT || t->notify != SIGEV_SIGNAL || t->signo != sig)
			continue;
		struct timespec now;
		ts_now(t->clock, &now);
		int ov = fbx_consume_as(t, &now, fbx_dequeued(t, si));
		if (ov < 0) continue;
		if (si) {
			si->si_code = SI_TIMER;
			si->si_value = t->value;
			si->si_timerid = i + 1;
			si->si_overrun = ov;
		}
		fbx_kick();
		return;
	}
}

/* ---- fork handling ---- */

static void fbx_reset_state(void)
{
	memset(g_timers, 0, sizeof g_timers);
	memset((void *)g_sent_sigs, 0, sizeof g_sent_sigs);
	g_mgr_started = 0;
	g_owner_pid = getpid();
}

/* pthread_atfork child handler: the child has no manager thread and inherits
 * no timers. Reinit the lock (single-threaded in the child, so a plain
 * re-initialization is safe even if fork raced a held lock) and clear state. */
static void fbx_atfork_child(void)
{
	pthread_mutex_t fresh = PTHREAD_MUTEX_INITIALIZER;
	g_lock = fresh;
	fbx_reset_state();
}

/* Called with g_lock held from every entry point. Primary fork guard: if we
 * are a fresh process (pid changed, e.g. atfork did not run on this fork
 * path), drop the inherited timers so nothing is spuriously "armed". */
static void fbx_check_fork(void)
{
	if (g_owner_pid != getpid())
		fbx_reset_state();
}

static struct fbx_timer *fbx_lookup(timer_t tt)
{
	intptr_t v = (intptr_t)tt;
	if (v < 1 || v > FBX_TIMER_MAX) return 0;
	struct fbx_timer *t = &g_timers[v - 1];
	return t->in_use ? t : 0;
}

/* ---- notification delivery (called WITHOUT g_lock held) ---- */

struct fbx_deliver {
	int             notify;
	int             tid;
	int             timer;       /* index + 1 */
	unsigned        gen;
	struct pthread *owner;
	int             signo;
	union sigval    value;
};

extern int32_t __wasilibc_thread_signal_posix_timer(int32_t tid, int32_t signo);

/* firebox#SGV — SIGEV_THREAD: one notification thread per timer at a time,
 * running the notification function once per consumed expiry, as musl's
 * per-timer helper thread does (MEASURED on alpine:3.24: a 30 ms notification
 * on a 5 ms timer runs serially, concurrency 1, and timer_getoverrun inside it
 * reports the expiries it covered). The old body spawned a thread per expiry:
 * concurrency 7-8 and overrun 0. glibc spawns per expiry; POSIX allows both,
 * and musl is the libc Firebox is. The thread consumes the SENT expiry before
 * each call; it exits when none is waiting. */
static void *fbx_notify_thread(void *p)
{
	int idx = (int)(intptr_t)p - 1;
	unsigned gen;
	struct fbx_timer *t = &g_timers[idx];
	pthread_mutex_lock(&g_lock);
	gen = t->gen;
	for (;;) {
		if (!t->in_use || t->gen != gen || t->state != FBX_SENT) break;
		struct timespec now;
		ts_now(t->clock, &now);
		(void)fbx_consume(t, &now);
		void (*fn)(union sigval) = t->notify_fn;
		union sigval v = t->value;
		fbx_kick();
		pthread_mutex_unlock(&g_lock);
		fn(v);
		pthread_mutex_lock(&g_lock);
	}
	/* timer_create reuses no slot whose helper flag is set, so the flag is
	 * this thread's whatever happened to the timer meanwhile. */
	t->helper = 0;
	pthread_mutex_unlock(&g_lock);
	return 0;
}

static void fbx_deliver(const struct fbx_deliver *d)
{
	if (d->notify == SIGEV_THREAD) {
		pthread_attr_t a;
		pthread_t th;
		struct fbx_timer *t = &g_timers[d->timer - 1];
		int has_attr = 0;
		pthread_mutex_lock(&g_lock);
		if (t->in_use && t->gen == d->gen && t->has_attr) {
			a = t->attr;
			has_attr = 1;
		}
		pthread_mutex_unlock(&g_lock);
		if (!has_attr) pthread_attr_init(&a);
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
		/* Spawned even for a timer deleted meanwhile: the thread finds no
		 * SENT expiry and clears the helper flag that keeps the slot. */
		if (__pthread_create(&th, &a, fbx_notify_thread,
		                     (void *)(intptr_t)d->timer) != 0) {
			pthread_mutex_lock(&g_lock);
			t->helper = 0;
			/* Nothing will consume this expiry: drop it, so the timer
			 * re-arms instead of stalling SENT. */
			if (t->state == FBX_SENT) {
				struct timespec now;
				ts_now(t->clock, &now);
				(void)fbx_consume(t, &now);
			}
			pthread_mutex_unlock(&g_lock);
			fbx_kick();
		}
		if (!has_attr) pthread_attr_destroy(&a);
		return;
	}

	/* SIGEV_SIGNAL — thread-directed so the target's block mask is honored. */
	int sig = d->signo;
	if (sig >= SIGRTMIN && sig <= _NSIG - 1 && d->owner) {
		/* Realtime: carry SI_TIMER + sigev_value + the timer and mark
		 * pending depth (mirrors pthread_kill.c's RT feed). */
		int word = (sig - 1) / 32;
		int bit  = 1 << ((sig - 1) % 32);
		(void)__fbx_rtsigq_push_timer(sig, d->value, SI_TIMER, getpid(), getuid(),
		                              d->timer);
		a_or((volatile int *)&d->owner->pending_sigs[word], bit);
		a_or(&__wasm_pending_sigs[word], bit);
	}
	(void)__wasilibc_thread_signal_posix_timer(d->tid, sig);
}

/* ---- the manager thread ---- */

/* Send the expiry scheduled at t->next. Called with g_lock held; appends at
 * most one delivery to `out` (returns 0 if `out` is full: the caller retries
 * on its next pass). */
static int fbx_fire_locked(struct fbx_timer *t, int idx,
                           const struct timespec *now,
                           struct fbx_deliver *out, int *n, int cap)
{
	if (t->notify == SIGEV_NONE) {
		/* No notification and so no consumption: just keep counting. */
		if (fbx_periodic(t)) {
			t->e0 = t->next;
			(void)fbx_overrun_at(t, now, &t->next);
		} else {
			t->armed = 0;
		}
		return 1;
	}
	if (*n >= cap) return 0;
	struct fbx_deliver *d = &out[(*n)++];
	t->e0 = t->next;
	t->sent_at = *now;
	t->sent_blocked = t->notify == SIGEV_SIGNAL && fbx_owner_blocks(t);
	t->state = FBX_SENT;
	if (!fbx_periodic(t)) t->armed = 0;
	d->notify = t->notify;
	d->timer  = idx + 1;
	d->gen    = t->gen;
	d->owner  = t->owner;
	d->tid    = t->owner ? t->owner->tid : 0;
	d->signo  = t->signo;
	d->value  = t->value;
	if (t->notify == SIGEV_SIGNAL) {
		int word = (t->signo - 1) / 32;
		a_or(&g_sent_sigs[word], 1 << ((t->signo - 1) % 32));
	} else if (t->helper) {
		(*n)--;          /* the running notification thread consumes it */
	} else {
		t->helper = 1;
	}
	return 1;
}

/* A SENT SIGEV_SIGNAL expiry nobody reported consuming: lost if, after
 * FBX_LOST_NS, its owner does not block the signal and no pending bit shows
 * it (the host disposed of it without entering the guest). */
static int fbx_lost_locked(const struct fbx_timer *t, const struct timespec *now)
{
	if (t->notify != SIGEV_SIGNAL) return 0;
	if (ts_ns(now) - ts_ns(&t->sent_at) < FBX_LOST_NS) return 0;
	return !fbx_owner_blocks(t) && !fbx_shows_pending(t);
}

static void *fbx_timer_manager(void *arg)
{
	(void)arg;
	sigset_t all;
	sigfillset(&all);
	pthread_sigmask(SIG_BLOCK, &all, 0);   /* never eat a timer's own signal */

	pthread_mutex_lock(&g_lock);
	for (;;) {
		struct fbx_deliver batch[32];
		int nbatch = 0;
		int kick = g_kick;
		int sent[(_NSIG + 31) / 32] = {0};
		int have = 0;
		struct timespec min_rem = {0, 0};

		for (int i = 0; i < FBX_TIMER_MAX; i++) {
			struct fbx_timer *t = &g_timers[i];
			if (!t->in_use) continue;
			struct timespec now, wake;
			if (ts_now(t->clock, &now)) {
				t->armed = 0;
				continue;
			}
			/* Lost while unblocked: the host discarded it (default-ignore),
			 * which is not a dequeue (fbx_dequeued). */
			if (t->state == FBX_SENT && fbx_lost_locked(t, &now))
				(void)fbx_consume_as(t, &now, t->sent_blocked);
			fbx_quiesce(t);
			/* Settle a consumed expiry: re-arm past the consumption. */
			if (t->state == FBX_CONSUMED) {
				if (t->armed && fbx_periodic(t)) t->next = t->rearm;
				t->state = FBX_IDLE;
			}
			if (t->state == FBX_SENT) {
				if (t->notify != SIGEV_SIGNAL)
					continue;   /* its notification thread kicks us */
				sent[(t->signo - 1) / 32] |= 1 << ((t->signo - 1) % 32);
				/* Look again when it could count as lost. */
				wake = t->sent_at;
				ts_add_ns(&wake, FBX_LOST_NS);
				if (!ts_lt(&now, &wake)) {
					wake = now;
					ts_add_ns(&wake, FBX_LOST_NS);
				}
			} else {
				if (!t->armed) continue;
				if (ts_ge(&now, &t->next)) {
					if (fbx_fire_locked(t, i, &now, batch, &nbatch, 32)
					    && t->state == FBX_SENT && t->notify == SIGEV_SIGNAL)
						sent[(t->signo - 1) / 32] |= 1 << ((t->signo - 1) % 32);
					if (t->state == FBX_SENT || !t->armed) continue;
				}
				wake = t->next;
			}
			struct timespec rem;
			ts_diff(&wake, &now, &rem);
			/* firebox#CPU — CPU time can stop during sleep or advance faster
			 * than wall time across workers. Poll an armed CPU-clock timer
			 * at a fixed interval so a parked clock with very little CPU
			 * time remaining cannot spin. */
			if (t->state != FBX_SENT
			    && (t->clock == CLOCK_PROCESS_CPUTIME_ID
			        || __wasilibc_is_named_cpu_clock((uintptr_t)t->clock)))
				rem = (struct timespec){0, 10000000};
			if (!have || ts_lt(&rem, &min_rem)) { min_rem = rem; have = 1; }
		}
		/* The signals with a SENT timer, as this scan found them. Only the
		 * manager sends, so none can be missing; one consumed meanwhile
		 * leaves a stale bit, which costs that signal a scan until the
		 * kick this consumption issued brings the manager round again. */
		for (int w = 0; w < (_NSIG + 31) / 32; w++)
			a_store(&g_sent_sigs[w], sent[w]);

		if (nbatch) {
			pthread_mutex_unlock(&g_lock);
			for (int i = 0; i < nbatch; i++) fbx_deliver(&batch[i]);
			pthread_mutex_lock(&g_lock);
			continue;   /* recompute after delivery */
		}

		pthread_mutex_unlock(&g_lock);
		struct timespec deadline;
		if (have && clock_gettime(CLOCK_MONOTONIC, &deadline) == 0) {
			ts_add(&deadline, &min_rem);
			(void)__timedwait(&g_kick, kick, CLOCK_MONOTONIC, &deadline, 1);
		} else {
			(void)__timedwait(&g_kick, kick, CLOCK_MONOTONIC, 0, 1);
		}
		pthread_mutex_lock(&g_lock);
	}
	/* not reached */
}

/* Ensure the manager thread exists. Called with g_lock held. */
static int fbx_ensure_manager_locked(void)
{
	if (!g_atfork_done) {
		pthread_atfork(0, 0, fbx_atfork_child);
		g_atfork_done = 1;
	}
	if (!g_mgr_started) {
		pthread_attr_t a;
		pthread_t mt;
		pthread_attr_init(&a);
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
		int r = __pthread_create(&mt, &a, fbx_timer_manager, 0);
		pthread_attr_destroy(&a);
		if (r != 0) return -1;
		g_mgr_started = 1;
	}
	return 0;
}

/* ---- gettime helper (g_lock held) ---- */

static int fbx_gettime_locked(struct fbx_timer *t, struct itimerspec *out)
{
	if (t->armed) {
		struct timespec now, next = t->next;
		if (ts_now(t->clock, &now)) return -1;
		out->it_interval = t->interval;
		/* firebox#SGV — a periodic timer is not re-armed while its signal is
		 * in flight, but it still has a next expiry: the one after now
		 * (Linux common_timer_get forwards the timer the same way). */
		if (t->state != FBX_IDLE && fbx_periodic(t))
			(void)fbx_overrun_at(t, &now, &next);
		ts_diff(&next, &now, &out->it_value);
	} else {
		out->it_interval = t->interval;
		out->it_value.tv_sec = 0;
		out->it_value.tv_nsec = 0;
	}
	return 0;
}

/* ---- public API ---- */

int timer_create(clockid_t clk, struct sigevent *restrict evp,
                 timer_t *restrict res)
{
	if (clock_getres(clk, NULL)) return -1;
	if (!res) { errno = EFAULT; return -1; }
	/* firebox#CPU — the manager runs on its own pthread. Bind the creator's
	 * thread clock while that creator is still the calling thread. */
	if (clk == CLOCK_THREAD_CPUTIME_ID) {
		int error = pthread_getcpuclockid(pthread_self(), &clk);
		if (error) { errno = error; return -1; }
	}

	int notify = evp ? evp->sigev_notify : SIGEV_SIGNAL;
	int signo = SIGALRM;
	union sigval value;
	void (*fn)(union sigval) = 0;
	memset(&value, 0, sizeof value);

	if (evp) {
		switch (notify) {
		case SIGEV_SIGNAL:
			signo = evp->sigev_signo;
			if (signo <= 0 || signo >= _NSIG) { errno = EINVAL; return -1; }
			value = evp->sigev_value;
			break;
		case SIGEV_NONE:
			value = evp->sigev_value;
			break;
		case SIGEV_THREAD:
			fn = evp->sigev_notify_function;
			if (!fn) { errno = EINVAL; return -1; }
			value = evp->sigev_value;
			break;
		default:
			errno = EINVAL;
			return -1;
		}
	}

	pthread_mutex_lock(&g_lock);
	fbx_check_fork();

	int idx = -1;
	for (int i = 0; i < FBX_TIMER_MAX; i++)
		if (!g_timers[i].in_use && !g_timers[i].helper) { idx = i; break; }
	if (idx < 0) { pthread_mutex_unlock(&g_lock); errno = EAGAIN; return -1; }

	struct fbx_timer *t = &g_timers[idx];
	unsigned gen = t->gen;
	memset(t, 0, sizeof *t);
	t->gen       = gen + 1;
	t->in_use    = 1;
	t->clock     = clk;
	t->notify    = notify;
	t->signo     = signo;
	t->value     = value;
	t->notify_fn = fn;
	if (notify == SIGEV_THREAD && evp->sigev_notify_attributes) {
		t->attr = *evp->sigev_notify_attributes;
		t->has_attr = 1;
	}
	t->owner     = __pthread_self();
	/* evp==NULL is defined as SIGEV_SIGNAL with the default signal (SIGALRM)
	 * and sigev_value carrying the timer id (Open POSIX timer_create/spec/5-1). */
	if (!evp) t->value.sival_int = idx + 1;

	pthread_mutex_unlock(&g_lock);
	*res = (timer_t)(intptr_t)(idx + 1);
	return 0;
}

int timer_settime(timer_t tt, int flags, const struct itimerspec *restrict val,
                  struct itimerspec *restrict old)
{
	if (!val) { errno = EINVAL; return -1; }
	/* Validate the FULL itimerspec, not just tv_nsec. POSIX/Linux (timespec64_valid)
	 * require EINVAL for a negative tv_sec as well as an out-of-range tv_nsec, on
	 * BOTH it_value and it_interval. Without the tv_sec check a negative it_value
	 * armed the timer with a past expiry (or a negative interval armed it via a
	 * valid it_value), and with no SIGALRM handler installed the default disposition
	 * terminated the process — timer_settime/13-1 rows 51-56 (negative tv_sec). */
	if (val->it_value.tv_nsec < 0 || val->it_value.tv_nsec >= 1000000000L ||
	    val->it_interval.tv_nsec < 0 || val->it_interval.tv_nsec >= 1000000000L ||
	    val->it_value.tv_sec < 0 || val->it_interval.tv_sec < 0) {
		errno = EINVAL;
		return -1;
	}

	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }

	if (old && fbx_gettime_locked(t, old)) {
		pthread_mutex_unlock(&g_lock);
		return -1;
	}

	int arm = (val->it_value.tv_sec != 0 || val->it_value.tv_nsec != 0);
	/* firebox#CPU — read the clock and start the manager before changing
	 * anything, so a failed timer_settime leaves the timer as it was. */
	struct timespec arm_now;
	if (arm) {
		if (ts_now(t->clock, &arm_now)) {
			pthread_mutex_unlock(&g_lock);
			return -1;
		}
		if (fbx_ensure_manager_locked() != 0) {
			pthread_mutex_unlock(&g_lock);
			errno = EAGAIN;
			return -1;
		}
	}
	t->interval = val->it_interval;
	/* firebox#SGV — a new setting forgets the in-flight expiry (Linux bumps
	 * it_requeue_pending so its dequeue does not re-arm, and zeroes
	 * it_overrun_last). A signal still pending is delivered, but no longer
	 * settles this timer. The SIGEV_THREAD notification thread finds no SENT
	 * expiry and exits. */
	if (t->state == FBX_SENT) {
		struct timespec now;
		ts_now(t->clock, &now);
		(void)fbx_consume(t, &now);
	}
	fbx_quiesce(t);
	t->state = FBX_IDLE;
	a_store(&t->overrun_last, 0);
	if (arm) {
		if (flags & TIMER_ABSTIME) {
			t->next = val->it_value;      /* absolute in the clock domain */
		} else {
			t->next = arm_now;
			ts_add(&t->next, &val->it_value);
		}
		t->armed = 1;
	} else {
		t->armed = 0;                     /* it_value == 0 disarms */
	}

	fbx_kick();
	pthread_mutex_unlock(&g_lock);
	return 0;
}

/* firebox#QQN — a DISCONTINUOUS CLOCK_REALTIME change must immediately
 * re-evaluate armed absolute CLOCK_REALTIME timers. POSIX requires it and Linux
 * re-arms them; without it an absolute timer that the clock jump has already
 * made overdue still fires only at its ORIGINAL deadline.
 *
 * WHY THE MANAGER CANNOT NOTICE ON ITS OWN: fbx_timer_manager computes the
 * nearest remaining expiry in each timer's own clock domain, converts it to a
 * CLOCK_MONOTONIC deadline, and blocks until then. A REALTIME offset change
 * moves no monotonic deadline and signals nothing, so the manager sleeps out
 * the stale interval. Before this hook the timer still usually fired roughly
 * on time -- but only because SOME unrelated wakeup happened to land first.
 * That is correctness resting on an accident, and it lost the signal ~2.5% of
 * the time (Open POSIX clock_settime/speculative/4-3).
 *
 * The kick cannot be lost (firebox#SGV): the manager samples g_kick under
 * g_lock BEFORE it scans, and its wait returns at once if the word moved since.
 * So either the manager already scanned with the new clock, or its wait sees
 * this increment.
 *
 * WEAK-LINKED FROM THE CALLER: clock_settime declares this weak, so a program
 * that never links the timer machinery resolves it to NULL and pays nothing. */
void __fbx_timers_clock_changed(void)
{
	fbx_kick();
}

int timer_gettime(timer_t tt, struct itimerspec *val)
{
	if (!val) { errno = EINVAL; return -1; }
	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
	int result = fbx_gettime_locked(t, val);
	pthread_mutex_unlock(&g_lock);
	return result;
}

/* firebox#SGV — the overrun of the last consumed expiry (it_overrun_last).
 *
 * One gap remains between "consumed" and "reported consumed": a pending
 * signal the caller just unblocked may still sit in the host's queue (a
 * thread-directed instance is dispatched at this thread's next host entry).
 * Linux dequeues it on the sigprocmask return. So when the caller owns the
 * timer, does not block its signal and the expiry is still SENT, enter the
 * host once (sched_yield) to let that delivery land. If none landed, the
 * instance is deliverable and unreported: count it consumed now. */
int timer_getoverrun(timer_t tt)
{
	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
	if (t->state == FBX_SENT && t->notify == SIGEV_SIGNAL &&
	    t->owner == __pthread_self() && !fbx_owner_blocks(t)) {
		pthread_mutex_unlock(&g_lock);
		(void)__wasi_sched_yield();
		pthread_mutex_lock(&g_lock);
		t = fbx_lookup(tt);
		if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
		if (t->state == FBX_SENT && t->notify == SIGEV_SIGNAL &&
		    !fbx_owner_blocks(t)) {
			struct timespec now;
			ts_now(t->clock, &now);
			if (fbx_consume(t, &now) >= 0) fbx_kick();
		}
	}
	fbx_quiesce(t);
	int ov = t->overrun_last;
	pthread_mutex_unlock(&g_lock);
	return ov;
}

int timer_delete(timer_t tt)
{
	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
	fbx_quiesce(t);
	t->in_use = 0;
	t->armed = 0;
	t->state = FBX_IDLE;
	t->gen++;
	fbx_kick();
	pthread_mutex_unlock(&g_lock);
	return 0;
}

#endif /* __wasilibc_unmodified_upstream */
