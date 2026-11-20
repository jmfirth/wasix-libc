#ifndef _FIREBOX_SCHED_IMPL_H
#define _FIREBOX_SCHED_IMPL_H

/*
 * firebox#QAF/#HSE — scheduling in the unprivileged profile. Threads start
 * at SCHED_OTHER and can record OTHER/BATCH/IDLE at static priority zero.
 * The hint is guest state shared by the pthread and sched_* APIs. RT needs
 * a privilege this profile does not grant.
 *
 * These are shared, side-effect-free helpers so the per-function sources and the
 * pthread thread-scheduling wrappers cannot drift (Invariant 1 — fix the class,
 * one home for the numbers). `static inline` keeps them link-local: no new
 * exported symbol, so the upstream wasix-libc / wasmer.io ABI is unchanged
 * (Invariant 8 — additive/behavioral only).
 */

#include <sched.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <wasi/api.h> /* firebox#R63 — __wasix_sched_check_owner + __WASI_ERRNO_* */
#include <wasi/libc.h> /* firebox#87F — __wasilibc_errno_from_wasi */
#ifndef __wasilibc_unmodified_upstream
#include "pthread_impl.h"
#endif

/* The scheduling policies Linux accepts. SCHED_DEADLINE is intentionally not a
 * settable policy through sched_setscheduler(2) (it needs sched_setattr), so it
 * is excluded from the "valid to select" set. */
static inline int __sched_policy_valid(int policy)
{
	switch (policy) {
	case SCHED_OTHER:
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_BATCH:
	case SCHED_IDLE:
		return 1;
	default:
		return 0;
	}
}

/* Real-time policies the unprivileged profile cannot grant. */
static inline int __sched_policy_is_realtime(int policy)
{
	return policy == SCHED_FIFO || policy == SCHED_RR;
}

/* Linux priority ranges, per policy: [1,99] for the real-time policies,
 * [0,0] for SCHED_OTHER/BATCH/IDLE. Matches sched_get_priority_{min,max}(2). */
static inline int __sched_priority_max(int policy)
{
	return __sched_policy_is_realtime(policy) ? 99 : 0;
}

static inline int __sched_priority_min(int policy)
{
	return __sched_policy_is_realtime(policy) ? 1 : 0;
}

static inline int __sched_validate_param(int policy, int priority)
{
	if (!__sched_policy_valid(policy) || priority < __sched_priority_min(policy)
	    || priority > __sched_priority_max(policy))
		return EINVAL;
	return 0;
}

/* The profile has no CAP_SYS_NICE or RLIMIT_NICE allowance for leaving IDLE. */
static inline int __sched_policy_permission(int current, int policy)
{
	return (__sched_policy_is_realtime(policy) ||
	        (current == SCHED_IDLE && policy != SCHED_IDLE)) ? EPERM : 0;
}

#ifndef __wasilibc_unmodified_upstream
static inline unsigned char __sched_get_thread_policy(pthread_t t)
{
	return __atomic_load_n(&t->sched_policy, __ATOMIC_RELAXED);
}

static inline int __sched_set_thread_policy(pthread_t t, int policy)
{
	/* TID callers hold the list lock. pthread callers hold killlock. Exit
	 * takes killlock before the list lock, so share the update atomically. */
	unsigned char current = __sched_get_thread_policy(t);
	do {
		int error = __sched_policy_permission(current, policy);
		if (error) return error;
	} while (!__atomic_compare_exchange_n(&t->sched_policy, &current, policy,
	                                     1, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
	return 0;
}

/* Return 0 on a local read/write or an errno. Keep the list locked through
 * the operation so the target cannot unlink itself and free its TCB. */
static inline int __sched_access_local(pid_t tid, const int *request, int *observed)
{
	pthread_t self = __pthread_self();
	if (tid == 0) {
		if (request) return __sched_set_thread_policy(self, *request);
		*observed = __sched_get_thread_policy(self);
		return 0;
	}
	if (tid < 0) return EINVAL;

	/* For the initial thread Wasmer assigns TID == PID. Libc uses a
	 * separate sentinel as that thread's lock owner. */
	pid_t leader = getpid();
	int error = ESRCH;
	sigset_t mask;
	__block_app_sigs(&mask);
	__tl_lock();
	pthread_t t = self;
	do {
		int id = __atomic_load_n(&t->tid, __ATOMIC_RELAXED);
		if ((id == WASIX_INITIAL_LOCK_TID ? leader : id) == tid) {
			if (request) error = __sched_set_thread_policy(t, *request);
			else {
				*observed = __sched_get_thread_policy(t);
				error = 0;
			}
			break;
		}
		t = t->next;
	} while (t != self);
	__tl_unlock();
	__restore_sigs(&mask);
	return error;
}
#endif

/*
 * Faithful process existence / permission probe, using the POSIX null-signal
 * idiom kill(pid, 0). Firebox's kill() routes to __wasi_proc_signal, which
 * returns ESRCH for a nonexistent/reaped process and EPERM for a live process
 * we may not signal (firebox#XCJ/#KZ0/#MK4). That is precisely how the kernel
 * validates the pid argument of sched_*(2), so it gives the real ESRCH the
 * Open POSIX pid-lifetime tests (e.g. sched_getscheduler/5-1, sched_getparam/
 * 4-1, sched_setscheduler/21-1) assert after fork()+wait() reaps a child.
 *
 * Returns 0 when the process exists (kill succeeded, or failed EPERM => the
 * process is there, just not ours), leaving errno untouched on the EPERM leg.
 * Returns -1 with errno already set (ESRCH for a missing pid, EINVAL for a
 * negative pid) otherwise. pid == 0 is the calling process, which always
 * exists. Only the caller's own pid and children are queried here, so kill(2)'s
 * process-group meaning of pid <= 0 never applies on the non-zero path.
 */
static inline int __sched_pid_check(pid_t pid)
{
	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0)
		return 0;
	if (kill(pid, 0) == 0)
		return 0;
	if (errno == EPERM) {
		errno = 0;
		return 0;
	}
	/* errno is ESRCH (missing/reaped) — surface it unchanged. */
	return -1;
}

/*
 * Faithful write-permission probe for the sched_set*(2) mutators
 * (sched_setparam / sched_setscheduler). Same pid validation as
 * __sched_pid_check — EINVAL for a negative pid, pid == 0 is the calling
 * process, ESRCH for a missing/reaped pid — but the PERMISSION model is the
 * kernel's check_same_owner(), NOT kill(2)'s. This is the write/read asymmetry
 * (firebox#R63): reading another process's policy/parameters needs no privilege
 * (so the read probe above swallows the EPERM leg), but CHANGING them requires
 * the caller's EFFECTIVE uid to match the target's (check_same_owner is
 * euid-only), so a cross-owner mutation is refused EPERM.
 *
 * Why NOT kill(pid, 0) here (the crux, firebox#R63): kill(2) permission is
 * ruid-INCLUSIVE (the kernel grants when the sender's real OR effective uid
 * matches the target), whereas check_same_owner is EUID-ONLY. Under a PARTIAL
 * privilege drop — seteuid(nobody) while ruid stays 0 (exactly Open POSIX
 * sched_setparam/26-1) — kill(1, 0) is PERMITTED (the caller's ruid 0 matches
 * init's ruid 0) but sched_setparam(1, …) must be EPERM (the caller's euid is
 * now nobody, matching neither of init's uids). A kill-based probe therefore
 * answers "allowed" exactly when sched must answer "denied". The guest cannot
 * compare effective uids across processes, so the host performs the whole
 * check_same_owner comparison (__wasix_sched_check_owner) and returns the errno
 * — the same host-credential channel #KKM uses for the signal EPERM gate.
 *
 * Returns 0 when the mutation is permitted (self, same-owner, or privileged);
 * -1 with errno set (EPERM for a foreign owner, ESRCH for a missing/reaped pid,
 * EINVAL for a negative pid) when it must be denied.
 *
 * NOTE (class-sibling, deferred): sched_setaffinity is also a write and shares
 * this asymmetry, but no conformance test exercises an unprivileged foreign
 * setaffinity, so affinity.c keeps the read probe until a witness exists —
 * flipping it blind would be unvalidated blast radius (firebox#R63 follow-up).
 */
static inline int __sched_pid_check_write(pid_t pid)
{
	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0)
		return 0; /* the calling process — self-set always permitted */
	/* Host check_same_owner (euid-only) — see the block comment for why this
	 * is the host import and NOT kill(pid, 0). */
	__wasi_errno_t e = __wasix_sched_check_owner((uint32_t)pid);
	if (e == __WASI_ERRNO_SUCCESS)
		return 0;
	/* Host verdict: EPERM (foreign owner) or ESRCH (missing/reaped).
	 * firebox#87F: this used to store the raw host code and lean on the
	 * sysroot aliasing the POSIX errno macros to the __WASI_ERRNO_* numbers.
	 * That aliasing is being retired, so translate instead of relying on it. */
	errno = __wasilibc_errno_from_wasi((int)e);
	return -1;
}

#endif
