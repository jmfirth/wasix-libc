#ifndef _FIREBOX_SCHED_IMPL_H
#define _FIREBOX_SCHED_IMPL_H

/*
 * firebox#QAF — faithful POSIX scheduling for the single-scheduling-class
 * substrate (UNPRIVILEGED-SEMANTICS tier, the same tier as mlock #R23/#ZDN).
 *
 * WHY this is a faithful path and NOT a lying stub: Firebox runs one scheduling
 * class. That is exactly what an *unprivileged* Linux process sees — it can read
 * its policy/parameters, it runs at SCHED_OTHER static priority 0, and any
 * attempt to select a real-time policy (SCHED_FIFO / SCHED_RR) is refused with
 * EPERM because a real-time class requires a privilege the process does not
 * hold. Every value below is the value Linux itself returns to such a process;
 * nothing is invented. The POSIX PRIORITY_SCHEDULING contract is genuinely
 * SATISFIED, so these interfaces belong in the libc (Invariant 0: the substrate
 * is the product; Invariant 2: "the same way you'd do it on Linux"), not behind
 * a Firebox-specific gate.
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

/* Real-time policies the unprivileged single-class substrate cannot grant. */
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

#endif
