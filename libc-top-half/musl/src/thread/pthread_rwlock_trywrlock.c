#include "pthread_impl.h"

int __pthread_rwlock_trywrlock(pthread_rwlock_t *rw)
{
	if (a_cas(&rw->_rw_lock, 0, 0x7fffffff)) return EBUSY;
	/* firebox (pthread_rwlock_wrlock/3-1) — record the write-owner so
	 * pthread_rwlock_wrlock/timedwrlock can detect a self-deadlocking
	 * re-acquire and return EDEADLK instead of hanging. Set only on a
	 * successful write acquisition (this is also the acquire path the
	 * timedwrlock slow-loop drives to). The PUBLIC pthread_rwlock_trywrlock
	 * deliberately still returns EBUSY (above) for a self-owner: a non-
	 * blocking trylock cannot deadlock, so glibc/POSIX report EBUSY, not
	 * EDEADLK — the deadlock check lives only in the blocking wrlock path. */
	rw->_rw_owner = __pthread_self()->tid;
	return 0;
}

weak_alias(__pthread_rwlock_trywrlock, pthread_rwlock_trywrlock);
