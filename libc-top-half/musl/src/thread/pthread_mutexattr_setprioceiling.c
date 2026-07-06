#include "pthread_impl.h"

/* firebox#WRQ — mutex-attribute priority-ceiling setter.
 *
 * The header declares pthread_mutexattr_setprioceiling (pthread.h:180) but
 * upstream wasi-libc ships no definition (WASI has no priority scheduler), so
 * both it and its getter were undefined symbols. This is the get+set half of
 * the #WRQ un-guarding batch; the getter lives in pthread_attr_get.c.
 *
 * Storage: the ceiling is a small non-negative priority value; we keep it in
 * bits 8-15 of the single `unsigned __attr` word that is a mutexattr's only
 * state. That field is inert to the lock machinery — pthread_mutex_init copies
 * __attr into _m_type, and every lock/unlock path masks _m_type only with
 * 3/4/8/12/15/128 (all bits 0-7), so bits 8-15 never reach the type/robust/PI/
 * pshared logic. This mirrors the #CDZ protocol field (bits 4-5), chosen the
 * same way to avoid the live PI bit.
 *
 * Validation: POSIX makes the [EINVAL] for an out-of-range ceiling OPTIONAL
 * ("may fail"). WASI exposes no SCHED_FIFO priority range to check against —
 * sched_get_priority_min/max are themselves guarded out of <sched.h> under
 * __wasilibc_unmodified_upstream and have no runtime scheduler behind them — so
 * we validate against the storage field instead: a non-negative value that fits
 * the dedicated 8-bit ceiling field round-trips faithfully; anything else is
 * rejected EINVAL. WASI has no priority scheduler so PRIO_PROTECT has no runtime
 * effect, but per POSIX the attribute must round-trip faithfully what was set.
 *
 * Keep the field layout in sync with pthread_attr_get.c's getter. */
#define __MUTEXATTR_PRIOCEILING_SHIFT 8
#define __MUTEXATTR_PRIOCEILING_MASK  (0xffU << __MUTEXATTR_PRIOCEILING_SHIFT)

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int prioceiling)
{
	if (prioceiling < 0
	    || prioceiling > (int)(__MUTEXATTR_PRIOCEILING_MASK >> __MUTEXATTR_PRIOCEILING_SHIFT))
		return EINVAL;

	a->__attr = (a->__attr & ~__MUTEXATTR_PRIOCEILING_MASK)
		| ((unsigned)prioceiling << __MUTEXATTR_PRIOCEILING_SHIFT);
	return 0;
}
