#include "pthread_impl.h"

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
#ifdef __wasilibc_unmodified_upstream
	/* If the mutex being destroyed is process-shared and has nontrivial
	 * type (tracking ownership), it might be in the pending slot of a
	 * robust_list; wait for quiescence. */
	if (mutex->_m_type > 128) __vm_wait();
#else
	/* firebox#CDZ — destroy ANY initialized, unlocked mutex with rc=0,
	 * matching upstream musl (whose only divergence for `_m_type > 128` is
	 * the __vm_wait() robust-list *quiescence wait* — it NEVER changes the
	 * return value).
	 *
	 * The prior `if (_m_type > 128) return EINVAL;` was wrong twice over:
	 *
	 *   1. It mistook a quiescence *wait* for a *rejection*. Upstream waits
	 *      out a possible robust_list pending-slot race, then returns 0; the
	 *      fork turned that into an error.
	 *
	 *   2. The `> 128` predicate is far too broad. `_m_type` is the attr
	 *      bitmap copied by pthread_mutex_init: bits 0-1 = type, bit 2 =
	 *      robust, bit 3 = PRIO_INHERIT, bit 7 (128) = process-shared. So a
	 *      perfectly ordinary process-shared RECURSIVE (128|1 = 129) or
	 *      process-shared ERRORCHECK (128|2 = 130) mutex tripped the guard
	 *      and was rejected. Open POSIX pthread_mutex_destroy/{2-2,5-2}
	 *      destroy exactly these (the "Pshared Recursive/Errorcheck"
	 *      scenarios) and the spurious EINVAL failed them.
	 *
	 * On WASI there is a single process, so process-shared mutexes are
	 * semantically identical to private ones (one address space, shared
	 * across threads); there is no kernel robust_list, so there is never a
	 * pending slot to wait for and the quiescence wait is a no-op. A
	 * non-robust mutex therefore always destroys cleanly — exactly upstream's
	 * behavior with nothing to wait on. (Robust mutexes are unsupported here
	 * and cannot be created: pthread_mutexattr_setrobust never sets bit 2, so
	 * `_m_type & 4` is never true on a live mutex.) */
#endif
	return 0;
}
