#include "pthread_impl.h"

int __pthread_rwlock_unlock(pthread_rwlock_t *rw)
{
	int val, cnt, waiters, new, priv = rw->_rw_shared^128;

	do {
		val = rw->_rw_lock;
		cnt = val & 0x7fffffff;
		waiters = rw->_rw_waiters;
		new = (cnt == 0x7fffffff || cnt == 1) ? 0 : val-1;
		/* firebox (pthread_rwlock_wrlock/3-1) — clear the recorded
		 * write-owner BEFORE the atomic release when dropping the write
		 * hold (cnt==0x7fffffff is exclusively the write sentinel; readers
		 * cap at 0x7ffffffe in tryrdlock). Clearing before the a_cas
		 * ensures a thread that acquires next records its own ownership
		 * without racing this clear, and the a_cas's release orders the
		 * store ahead of the lock handoff. Read-unlocks leave _rw_owner
		 * (already 0) untouched. */
		if (cnt == 0x7fffffff) rw->_rw_owner = 0;
	} while (a_cas(&rw->_rw_lock, val, new) != val);

	if (!new && (waiters || val<0))
		__wake(&rw->_rw_lock, cnt, priv);

	return 0;
}

weak_alias(__pthread_rwlock_unlock, pthread_rwlock_unlock);
