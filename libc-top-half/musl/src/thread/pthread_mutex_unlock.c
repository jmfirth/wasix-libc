#include "pthread_impl.h"

int __pthread_mutex_unlock(pthread_mutex_t *m)
{
	pthread_t self;
	int waiters = m->_m_waiters;
	int cont;
	int type = m->_m_type & 15;
	int priv = (m->_m_type & 128) ^ 128;
	int new = 0;
	int old;

	if (type != PTHREAD_MUTEX_NORMAL) {
		self = __pthread_self();
		old = m->_m_lock;
		int own = old & 0x3fffffff;
		if (own != self->tid)
			return EPERM;
		if ((type&3) == PTHREAD_MUTEX_RECURSIVE && m->_m_count)
			return m->_m_count--, 0;
		if ((type&4) && (old&0x40000000))
			new = 0x7fffffff;
		if (!priv) {
			self->robust_list.pending = &m->_m_next;
#ifdef __wasilibc_unmodified_upstream
			__vm_lock();
#endif
		}
		volatile void *prev = m->_m_prev;
		volatile void *next = m->_m_next;
		*(volatile void *volatile *)prev = next;
		if (next != &self->robust_list.head) *(volatile void *volatile *)
			((char *)next - sizeof(void *)) = prev;
	}
#ifdef __wasilibc_unmodified_upstream
	if (type&8) {
		if (old<0 || a_cas(&m->_m_lock, old, new)!=old) {
			if (new) a_store(&m->_m_waiters, -1);
#ifdef __wasilibc_unmodified_upstream
			__syscall(SYS_futex, &m->_m_lock, FUTEX_UNLOCK_PI|priv);
#endif
		}
		cont = 0;
		waiters = 0;
	} else {
		cont = a_swap(&m->_m_lock, new);
	}
#else
		/* firebox#811 Phase 2 — deregister from the host held-list BEFORE
		 * the a_swap release-to-waiters, mirroring __unlock's
		 * deregister-before-release ordering: drop our ownership claim
		 * before the word announces the lock to other threads, so a
		 * concurrent host sweep never wakes an address we no longer hold.
		 * Reached ONLY on the genuine release path — the EPERM (:18) and
		 * RECURSIVE m_count-- (:20) early returns above never get here, so
		 * a recursively-held mutex is not deregistered until its final
		 * release. Balanced against the single register in trylock's
		 * success path. */
		__firebox_host_deregister_held(&m->_m_lock);
		cont = a_swap(&m->_m_lock, new);
#endif
	if (type != PTHREAD_MUTEX_NORMAL && !priv) {
		self->robust_list.pending = 0;
#ifdef __wasilibc_unmodified_upstream
		__vm_unlock();
#endif
	}
	if (waiters || cont<0)
		__wake(&m->_m_lock, 1, priv);
	return 0;
}

weak_alias(__pthread_mutex_unlock, pthread_mutex_unlock);