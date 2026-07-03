#include "pthread_impl.h"

int __pthread_rwlock_timedwrlock(pthread_rwlock_t *restrict rw, const struct timespec *restrict at)
{
	int r, t;

	/* firebox (pthread_rwlock_wrlock/3-1) — self-deadlock detection, the
	 * POSIX "may fail" [EDEADLK] case ("the current thread already owns the
	 * rwlock for writing"). Without this, a thread re-acquiring a write lock
	 * it already holds falls through to __timedwait(&_rw_lock, ..., at, ...)
	 * below with at==0 (pthread_rwlock_wrlock passes at=0 = wait forever) and
	 * blocks permanently — a hard hang that times out the conformance test,
	 * NOT a wrong errno. glibc returns EDEADLK here; we match that faithful
	 * Linux behavior. _rw_owner is 0 when the lock is unheld or read-held and
	 * a live tid is never 0, so this never fires on the first acquire. Scope:
	 * write-owner only (a single owner slot cannot track multiple readers),
	 * which is exactly the write-then-write case 3-1 exercises. */
	if (rw->_rw_owner == __pthread_self()->tid)
		return EDEADLK;

	r = pthread_rwlock_trywrlock(rw);
	if (r != EBUSY) return r;
	
	int spins = 100;
	while (spins-- && rw->_rw_lock && !rw->_rw_waiters) a_spin();

	while ((r=__pthread_rwlock_trywrlock(rw))==EBUSY) {
		if (!(r=rw->_rw_lock)) continue;
		t = r | 0x80000000;
		a_inc(&rw->_rw_waiters);
		a_cas(&rw->_rw_lock, r, t);
		r = __timedwait(&rw->_rw_lock, t, CLOCK_REALTIME, at, rw->_rw_shared^128);
		a_dec(&rw->_rw_waiters);
		if (r && r != EINTR) return r;
	}
	return r;
}

weak_alias(__pthread_rwlock_timedwrlock, pthread_rwlock_timedwrlock);
