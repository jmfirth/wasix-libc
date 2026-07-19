#include "pthread_impl.h"
#include <sched.h>	/* firebox#BNJ — sched_get_priority_{min,max}(SCHED_FIFO)
			 * for the glibc-matching range validation below. */

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
 * same way to avoid the live PI bit. The SCHED_FIFO max (99) fits the 8-bit
 * field comfortably (99 < 255), so the valid range round-trips exactly.
 *
 * Validation (firebox#BNJ): reject any ceiling outside the real SCHED_FIFO
 * priority range [sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(
 * SCHED_FIFO)] = [1,99] with EINVAL, exactly as glibc does. This SUPERSEDES the
 * earlier storage-field-only [0,255] check, whose rationale ("WASI exposes no
 * SCHED_FIFO range to check against; sched_get_priority_min/max are guarded
 * out") is now obsolete — the #QAF sched work ships faithful, privilege-free
 * sched_get_priority_{min,max}(SCHED_FIFO) = 1/99 (see src/internal/
 * sched_impl.h). Matching glibc's range check is what makes the getter (#BNJ)
 * sound: because a ceiling < min is now un-storable (EINVAL), a 0 field can only
 * mean unset/init'd, so the getter may faithfully report it as the RT floor
 * without ever lying about a user-set value. POSIX makes [EINVAL] for an
 * out-of-range ceiling OPTIONAL ("may fail"); glibc takes it and so do we. WASI
 * has no priority scheduler so PRIO_PROTECT has no runtime effect, but per POSIX
 * the attribute must round-trip faithfully what was set. ABI-safe (behavioral
 * only, no new symbol — Inv-8).
 *
 * Keep the field layout in sync with pthread_attr_get.c's getter. */
#define __MUTEXATTR_PRIOCEILING_SHIFT 8
#define __MUTEXATTR_PRIOCEILING_MASK  (0xffU << __MUTEXATTR_PRIOCEILING_SHIFT)

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int prioceiling)
{
	/* SCHED_FIFO is always a valid policy in this libc, so the queries return
	 * the static [1,99] range (sched_impl.h) and cannot fail (-1) here. */
	if (prioceiling < sched_get_priority_min(SCHED_FIFO)
	    || prioceiling > sched_get_priority_max(SCHED_FIFO))
		return EINVAL;

	a->__attr = (a->__attr & ~__MUTEXATTR_PRIOCEILING_MASK)
		| ((unsigned)prioceiling << __MUTEXATTR_PRIOCEILING_SHIFT);
	return 0;
}
