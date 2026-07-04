#include "pthread_impl.h"
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

static volatile int check_robust_result = -1;

#ifdef __wasilibc_unmodified_upstream
int pthread_mutexattr_setrobust(pthread_mutexattr_t *a, int robust)
{
#ifdef __wasilibc_unmodified_upstream
	if (robust > 1U) return EINVAL;
	if (robust) {
		int r = check_robust_result;
		if (r < 0) {
			void *p;
			size_t l;
			r = -__syscall(SYS_get_robust_list, 0, &p, &l);
			a_store(&check_robust_result, r);
		}
		if (r) return r;
		a->__attr |= 4;
		return 0;
	}
	a->__attr &= ~4;
	return 0;
#else
	return EINVAL;
#endif
}
#else
int pthread_mutexattr_setrobust(pthread_mutexattr_t *a, int robust)
{
	/* firebox#1VY — implement the robust attribute. This shipping branch was
	 * a no-op `return 0;` stub, so PTHREAD_MUTEX_ROBUST silently produced a
	 * plain NORMAL mutex: bit 2 of __attr was never set, pthread_mutex_init
	 * (m->_m_type = a->__attr) gave a _m_type without the robust flag, the
	 * mutex never linked into robust_list.head, and the userspace robust
	 * sweep in __pthread_exit (pthread_create.c:170, a_swap(&_m_lock,
	 * 0x40000000)) never marked a dead owner's lock with the OWNER_DIED
	 * sentinel. A thread locking an abandoned robust mutex therefore got
	 * ETIMEDOUT instead of EOWNERDEAD, and the whole EOWNERDEAD/
	 * ENOTRECOVERABLE machinery (pthread_mutex_trylock, pthread_mutex_unlock,
	 * pthread_mutex_consistent) was present-but-unreachable because every
	 * branch gates on _m_type & 4. Set the canonical upstream-musl robust bit
	 * (2) — strictly ABI-improving (Inv 8), same bit wasmer.io/upstream musl
	 * use. The kernel SYS_get_robust_list capability probe is intentionally
	 * dropped: firebox processes the robust list entirely in userspace at
	 * thread exit, so there is no kernel gate to consult. Sibling of #CDZ,
	 * which fixed pthread_mutexattr_setprotocol the same stubbed way. */
	if (robust > 1U) return EINVAL;
	if (robust) a->__attr |= 4;
	else a->__attr &= ~4;
	return 0;
}
#endif