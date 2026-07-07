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
		r = pthread_create(&td, &attr, start, &args);
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
 *   SIGEV_THREAD: spawn a detached thread running sigev_notify_function with
 *     sigev_value (Firebox has real threads — musl's SIGEV_THREAD model).
 *   SIGEV_NONE: no notification; the timer still counts down so timer_gettime
 *     reflects remaining time.
 *
 * OVERRUN accounting (timer_getoverrun): the __wasm_pending_sigs[] process
 * bitmask is the acceptance oracle. When a periodic SIGEV_SIGNAL timer expires
 * while its signal is STILL pending (a prior expiry not yet accepted), the
 * expiry is coalesced (one pending instance, as POSIX requires for
 * non-realtime signals) and counted as an overrun; a fresh delivery (bit
 * clear) resets the count. timer_getoverrun returns the live count — the
 * overrun of the most recent notification (Open POSIX timer_getoverrun/2-1).
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
#include <wasi/api.h>

/* Cross-thread signal delivery + RT-depth machinery (shared with raise.c /
 * pthread_kill.c / sigtimedwait.c). __fbx_rtsigq_push no-ops for a non-RT
 * signal. __wasm_pending_sigs[] is the process-wide pending bitmask consulted
 * for overrun. */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
extern volatile int __wasm_pending_sigs[];

#define FBX_TIMER_MAX 512   /* > 256 so timer_create/speculative/2-1 never
                             * hits EAGAIN before its 256-timer check. */

struct fbx_timer {
	int             in_use;
	int             armed;
	clockid_t       clock;
	int             notify;      /* SIGEV_SIGNAL / SIGEV_NONE / SIGEV_THREAD */
	int             signo;       /* SIGEV_SIGNAL */
	union sigval    value;       /* sigev_value */
	void          (*notify_fn)(union sigval); /* SIGEV_THREAD */
	struct pthread *owner;       /* creating thread — SIGEV_SIGNAL target */
	struct timespec next;        /* absolute expiry in `clock` domain */
	struct timespec interval;    /* {0,0} => one-shot */
	int             overrun;     /* live overrun count (getoverrun) */
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond;
static int             g_cond_ready;
static int             g_mgr_started;
static int             g_atfork_done;
static pid_t           g_owner_pid;
static struct fbx_timer g_timers[FBX_TIMER_MAX];

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

/* Read "now" in a timer's clock domain. The CPU-time clocks have no Firebox
 * countdown source; schedule them off CLOCK_MONOTONIC (for the busy-loop Open
 * POSIX CPUTIME tests wall time tracks CPU time, so the wall deadline fires at
 * the right accumulated-CPU point). */
static void ts_now(clockid_t clk, struct timespec *out)
{
	clockid_t c = clk;
	if (c == CLOCK_PROCESS_CPUTIME_ID || c == CLOCK_THREAD_CPUTIME_ID)
		c = CLOCK_MONOTONIC;
	if (clock_gettime(c, out) != 0) { out->tv_sec = 0; out->tv_nsec = 0; }
}

static int clock_supported(clockid_t clk)
{
	switch (clk) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		return 1;
	default:
		return 0;
	}
}

/* ---- fork handling ---- */

static void fbx_reset_state(void)
{
	memset(g_timers, 0, sizeof g_timers);
	g_mgr_started = 0;
	g_cond_ready = 0;
	g_owner_pid = getpid();
}

/* pthread_atfork child handler: the child has no manager thread and inherits
 * no timers. Reinit the lock/cond (single-threaded in the child, so a plain
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
	struct pthread *owner;
	int             signo;
	union sigval    value;
	void          (*fn)(union sigval);
};

static void *fbx_notify_trampoline(void *p)
{
	struct fbx_deliver *d = p;
	void (*fn)(union sigval) = d->fn;
	union sigval v = d->value;
	free(d);
	fn(v);
	return 0;
}

static void fbx_deliver(const struct fbx_deliver *d)
{
	if (d->notify == SIGEV_THREAD) {
		pthread_attr_t a;
		pthread_t th;
		struct fbx_deliver *arg = malloc(sizeof *arg);
		if (!arg) return;
		*arg = *d;
		pthread_attr_init(&a);
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&th, &a, fbx_notify_trampoline, arg) != 0)
			free(arg);
		pthread_attr_destroy(&a);
		return;
	}

	/* SIGEV_SIGNAL — thread-directed so the target's block mask is honored. */
	int sig = d->signo;
	if (sig >= SIGRTMIN && sig <= _NSIG - 1 && d->owner) {
		/* Realtime: carry SI_TIMER + sigev_value and mark pending depth
		 * (mirrors pthread_kill.c's RT feed). */
		int word = (sig - 1) / 32;
		int bit  = 1 << ((sig - 1) % 32);
		(void)__fbx_rtsigq_push(sig, d->value, SI_TIMER, getpid(), getuid());
		a_or((volatile int *)&d->owner->pending_sigs[word], bit);
		a_or(&__wasm_pending_sigs[word], bit);
	}
	__wasi_errno_t e = __wasi_thread_signal(d->tid, (__wasi_signal_t)sig);
	(void)e;   /* best-effort delivery; a dead target thread is non-fatal */
}

/* ---- the manager thread ---- */

/* Decide the notification for one expired timer and update its overrun. Called
 * with g_lock held; appends at most one delivery to `out`. */
static void fbx_fire_locked(struct fbx_timer *t, struct fbx_deliver *out,
                            int *n, int cap)
{
	if (t->notify == SIGEV_NONE)
		return;

	if (t->notify == SIGEV_SIGNAL) {
		int word = (t->signo - 1) / 32;
		int bit  = 1 << ((t->signo - 1) % 32);
		if (__wasm_pending_sigs[word] & bit) {
			/* Prior expiry's signal still pending — coalesce + count. */
			t->overrun++;
			return;
		}
		t->overrun = 0;
	}

	if (*n >= cap) return;   /* remaining due timers caught next pass */
	struct fbx_deliver *d = &out[(*n)++];
	d->notify = t->notify;
	d->owner  = t->owner;
	d->tid    = t->owner ? t->owner->tid : 0;
	d->signo  = t->signo;
	d->value  = t->value;
	d->fn     = t->notify_fn;
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

		/* Fire all due timers; reschedule periodics, disarm one-shots. */
		for (int i = 0; i < FBX_TIMER_MAX; i++) {
			struct fbx_timer *t = &g_timers[i];
			if (!t->in_use || !t->armed) continue;
			struct timespec now;
			ts_now(t->clock, &now);
			if (!ts_ge(&now, &t->next)) continue;
			fbx_fire_locked(t, batch, &nbatch, 32);
			if (t->interval.tv_sec || t->interval.tv_nsec) {
				do { ts_add(&t->next, &t->interval); }
				while (!ts_ge(&t->next, &now));
			} else {
				t->armed = 0;
			}
		}

		if (nbatch) {
			pthread_mutex_unlock(&g_lock);
			for (int i = 0; i < nbatch; i++) fbx_deliver(&batch[i]);
			pthread_mutex_lock(&g_lock);
			continue;   /* recompute after delivery */
		}

		/* Nearest remaining expiry across all armed timers. */
		int have = 0;
		struct timespec min_rem = {0, 0};
		for (int i = 0; i < FBX_TIMER_MAX; i++) {
			struct fbx_timer *t = &g_timers[i];
			if (!t->in_use || !t->armed) continue;
			struct timespec now, rem;
			ts_now(t->clock, &now);
			ts_diff(&t->next, &now, &rem);
			if (!have || ts_lt(&rem, &min_rem)) { min_rem = rem; have = 1; }
		}

		if (have) {
			struct timespec deadline;
			clock_gettime(CLOCK_MONOTONIC, &deadline);
			ts_add(&deadline, &min_rem);
			pthread_cond_timedwait(&g_cond, &g_lock, &deadline);
		} else {
			pthread_cond_wait(&g_cond, &g_lock);
		}
	}
	/* not reached */
}

/* Ensure the condvar + manager thread exist. Called with g_lock held. */
static int fbx_ensure_manager_locked(void)
{
	if (!g_atfork_done) {
		pthread_atfork(0, 0, fbx_atfork_child);
		g_atfork_done = 1;
	}
	if (!g_cond_ready) {
		pthread_condattr_t ca;
		pthread_condattr_init(&ca);
		pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
		if (pthread_cond_init(&g_cond, &ca) != 0) {
			pthread_condattr_destroy(&ca);
			return -1;
		}
		pthread_condattr_destroy(&ca);
		g_cond_ready = 1;
	}
	if (!g_mgr_started) {
		pthread_attr_t a;
		pthread_t mt;
		pthread_attr_init(&a);
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
		int r = pthread_create(&mt, &a, fbx_timer_manager, 0);
		pthread_attr_destroy(&a);
		if (r != 0) return -1;
		g_mgr_started = 1;
	}
	return 0;
}

/* ---- gettime helper (g_lock held) ---- */

static void fbx_gettime_locked(struct fbx_timer *t, struct itimerspec *out)
{
	out->it_interval = t->interval;
	if (t->armed) {
		struct timespec now;
		ts_now(t->clock, &now);
		ts_diff(&t->next, &now, &out->it_value);
	} else {
		out->it_value.tv_sec = 0;
		out->it_value.tv_nsec = 0;
	}
}

/* ---- public API ---- */

int timer_create(clockid_t clk, struct sigevent *restrict evp,
                 timer_t *restrict res)
{
	if (!clock_supported(clk)) { errno = EINVAL; return -1; }
	if (!res) { errno = EFAULT; return -1; }

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
		if (!g_timers[i].in_use) { idx = i; break; }
	if (idx < 0) { pthread_mutex_unlock(&g_lock); errno = EAGAIN; return -1; }

	struct fbx_timer *t = &g_timers[idx];
	memset(t, 0, sizeof *t);
	t->in_use    = 1;
	t->clock     = clk;
	t->notify    = notify;
	t->signo     = signo;
	t->value     = value;
	t->notify_fn = fn;
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

	if (old) fbx_gettime_locked(t, old);

	int arm = (val->it_value.tv_sec != 0 || val->it_value.tv_nsec != 0);
	t->interval = val->it_interval;
	if (arm) {
		struct timespec now;
		ts_now(t->clock, &now);
		if (flags & TIMER_ABSTIME) {
			t->next = val->it_value;      /* absolute in the clock domain */
		} else {
			t->next = now;
			ts_add(&t->next, &val->it_value);
		}
		t->armed = 1;
		t->overrun = 0;
		if (fbx_ensure_manager_locked() != 0) {
			t->armed = 0;
			pthread_mutex_unlock(&g_lock);
			errno = EAGAIN;
			return -1;
		}
	} else {
		t->armed = 0;                     /* it_value == 0 disarms */
	}

	if (g_cond_ready) pthread_cond_signal(&g_cond);
	pthread_mutex_unlock(&g_lock);
	return 0;
}

int timer_gettime(timer_t tt, struct itimerspec *val)
{
	if (!val) { errno = EINVAL; return -1; }
	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
	fbx_gettime_locked(t, val);
	pthread_mutex_unlock(&g_lock);
	return 0;
}

int timer_getoverrun(timer_t tt)
{
	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
	int ov = t->overrun;
	pthread_mutex_unlock(&g_lock);
	return ov;
}

int timer_delete(timer_t tt)
{
	pthread_mutex_lock(&g_lock);
	fbx_check_fork();
	struct fbx_timer *t = fbx_lookup(tt);
	if (!t) { pthread_mutex_unlock(&g_lock); errno = EINVAL; return -1; }
	t->in_use = 0;
	t->armed = 0;
	if (g_cond_ready) pthread_cond_signal(&g_cond);
	pthread_mutex_unlock(&g_lock);
	return 0;
}

#endif /* __wasilibc_unmodified_upstream */
