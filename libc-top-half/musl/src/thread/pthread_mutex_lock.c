#include "pthread_impl.h"

int __pthread_mutex_lock(pthread_mutex_t *m)
{
	if ((m->_m_type&15) == PTHREAD_MUTEX_NORMAL
	    && !a_cas(&m->_m_lock, 0, EBUSY)) {
#if !defined(__wasilibc_unmodified_upstream) && defined(FIREBOX_HOST_HELD_LIST)
		/* firebox#811 Phase 2 — register the lock word with the host
		 * held-list on the uncontended NORMAL fast-path acquire, so a
		 * thread that exits holding this mutex (libuv #808) is swept by
		 * #811 Phase 1. The contended/slow path delegates to
		 * __pthread_mutex_timedlock → __pthread_mutex_trylock, which
		 * registers there; registering here covers ONLY the fast path
		 * this function takes directly.
		 *
		 * firebox#ZFF/#811 — GATED OFF by default: the orphan class this
		 * targeted was rooted in the #435 cross-thread-signal regression,
		 * fixed by #5RE; the per-acquire host syscall buys nothing now.
		 * Compiled OUT for both wasm32 and wasm64 unless
		 * FIREBOX_HOST_HELD_LIST is defined. */
		__firebox_host_register_held(&m->_m_lock);
#endif
		return 0;
	}

	return __pthread_mutex_timedlock(m, 0);
}

weak_alias(__pthread_mutex_lock, pthread_mutex_lock);
