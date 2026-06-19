#include "pthread_impl.h"

int __pthread_mutex_trylock_owner(pthread_mutex_t *m)
{
	int old, own;
	int type = m->_m_type;
	pthread_t self = __pthread_self();
	int tid = self->tid;

	old = m->_m_lock;
	own = old & 0x3fffffff;
	if (own == tid) {
		if ((type&8) && m->_m_count<0) {
			old &= 0x40000000;
			m->_m_count = 0;
			goto success;
		}
		if ((type&3) == PTHREAD_MUTEX_RECURSIVE) {
			if ((unsigned)m->_m_count >= INT_MAX) return EAGAIN;
			m->_m_count++;
			return 0;
		}
	}
	/* firebox#550 — gate ENOTRECOVERABLE on PTHREAD_MUTEX_ROBUST (type & 4).
	 *
	 * Upstream musl unconditionally returns ENOTRECOVERABLE if the lock-word's
	 * owner field equals the robust-mutex "owner died, state unrecoverable"
	 * sentinel (0x3fffffff). For a non-robust mutex this is wrong: per POSIX
	 * 2017 (pthread_mutex_lock §RETURN VALUE), ENOTRECOVERABLE is reserved for
	 * robust mutexes — it returns when "the state protected by the mutex is
	 * not recoverable", a concept that only exists for the robust protocol.
	 *
	 * On non-robust mutexes the 0x3fffffff value should never legitimately
	 * occur; nothing in the wasix-libc thread code writes that owner-field
	 * value to a non-robust mutex's _m_lock (pthread_mutex_unlock's write of
	 * 0x7fffffff is gated on (type&4); pthread_create.c's robust_list-sweep
	 * swap writes 0x40000000, giving own=0). Under the firebox#548 cascade-9
	 * reproducer however the value DOES appear (29/30 cond_wait_exit rc=56
	 * + 1/30 mutex_lock_exit rc=56 on libuv's PTHREAD_MUTEX_ERRORCHECK
	 * threadpool mutex), almost certainly from a race interaction between
	 * the robust_list-sweep teardown (pthread_create.c:163's a_swap to
	 * 0x40000000), the cond_wait barrier-unlock path (pthread_cond_timedwait.c
	 * setting val|0x80000000), and a concurrent contended acquire — but the
	 * exact write race is not load-bearing for this fix.
	 *
	 * Gating the return on (type & 4) makes the path POSIX-correct: robust
	 * mutexes still surface ENOTRECOVERABLE as documented; non-robust mutexes
	 * fall through to the next predicate (`if (own || ...)`) and return
	 * EBUSY, letting pthread_mutex_timedlock's wait-retry loop reacquire the
	 * mutex once the race window clears. This matches the upstream-Linux
	 * glibc behavior, where non-robust mutexes never implement the dead-owner
	 * sentinel and the code path is effectively unreachable.
	 *
	 * Witness: firebox#548 phase-1 probe — `cond_wait_exit rc=56` (29×) +
	 * `mutex_lock_exit rc=56` (1×) at 100% of FAIL_127 corpus runs.
	 * Closes: firebox#550, cascade-9 cycle 12 (final).
	 */
	if ((type & 4) && own == 0x3fffffff) return ENOTRECOVERABLE;
	if (own || (old && !(type & 4))) return EBUSY;

	if (type & 128) {
		if (!self->robust_list.off) {
			self->robust_list.off = (char*)&m->_m_lock-(char *)&m->_m_next;
#ifdef __wasilibc_unmodified_upstream
			__syscall(SYS_set_robust_list, &self->robust_list, 3*sizeof(long));
#endif
		}
		if (m->_m_waiters) tid |= 0x80000000;
		self->robust_list.pending = &m->_m_next;
	}
	tid |= old & 0x40000000;

	if (a_cas(&m->_m_lock, old, tid) != old) {
		self->robust_list.pending = 0;
		if ((type&12)==12 && m->_m_waiters) return ENOTRECOVERABLE;
		return EBUSY;
	}

success:
#if !defined(__wasilibc_unmodified_upstream) && defined(FIREBOX_HOST_HELD_LIST)
	/* firebox#811 Phase 2 — register the lock word with the host
	 * held-list on a successful FIRST acquisition (the word-transition
	 * paths: the a_cas at :73, or the goto from the robust
	 * ENOTRECOVERABLE-clear at :16). The RECURSIVE re-lock returns early
	 * at :21 (m_count++) WITHOUT reaching here, so this fires once per
	 * logical ownership — balanced against the single deregister on the
	 * a_swap-reaching unlock path. The host dedups (addr,tid), so the
	 * rare double via the :16 goto is a benign no-op. Covers libuv #808
	 * and the contended __pthread_mutex_lock/timedlock slow paths, which
	 * delegate here.
	 *
	 * firebox#ZFF/#811 — GATED OFF by default (see __pthread_mutex_lock
	 * for the rationale: #5RE cured the real root). Compiled OUT for both
	 * widths unless FIREBOX_HOST_HELD_LIST is defined. */
	__firebox_host_register_held(&m->_m_lock);
#endif
	if ((type&8) && m->_m_waiters) {
		int priv = (type & 128) ^ 128;
#ifdef __wasilibc_unmodified_upstream
		__syscall(SYS_futex, &m->_m_lock, FUTEX_UNLOCK_PI|priv);
#endif
		self->robust_list.pending = 0;
		return (type&4) ? ENOTRECOVERABLE : EBUSY;
	}

	volatile void *next = self->robust_list.head;
	m->_m_next = next;
	m->_m_prev = &self->robust_list.head;
	if (next != &self->robust_list.head) *(volatile void *volatile *)
		((char *)next - sizeof(void *)) = &m->_m_next;
	self->robust_list.head = &m->_m_next;
	self->robust_list.pending = 0;

	if (old) {
		m->_m_count = 0;
		return EOWNERDEAD;
	}

	return 0;
}

int __pthread_mutex_trylock(pthread_mutex_t *m)
{
	if ((m->_m_type&15) == PTHREAD_MUTEX_NORMAL) {
#if !defined(__wasilibc_unmodified_upstream) && defined(FIREBOX_HOST_HELD_LIST)
		/* firebox#811 Phase 2 — NORMAL trylock: a_cas returns the OLD
		 * word; 0 means we just acquired (the lock was free). Register
		 * with the host held-list only on that acquired transition. This
		 * also covers the contended __pthread_mutex_timedlock retry loop,
		 * which calls back here on each spin until it wins.
		 *
		 * firebox#ZFF/#811 — GATED OFF by default (see __pthread_mutex_lock
		 * for the rationale: #5RE cured the real root). When OFF, this
		 * reverts to the plain upstream a_cas form below for both widths. */
		int prev = a_cas(&m->_m_lock, 0, EBUSY);
		if (!prev) __firebox_host_register_held(&m->_m_lock);
		return prev & EBUSY;
#else
		return a_cas(&m->_m_lock, 0, EBUSY) & EBUSY;
#endif
	}
	return __pthread_mutex_trylock_owner(m);
}

weak_alias(__pthread_mutex_trylock, pthread_mutex_trylock);
