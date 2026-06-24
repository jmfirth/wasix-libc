#include "pthread_impl.h"
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

static volatile int check_pi_result = -1;

#ifdef __wasilibc_unmodified_upstream
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int protocol)
{
	int r;
	switch (protocol) {
	case PTHREAD_PRIO_NONE:
		a->__attr &= ~8;
		return 0;
	case PTHREAD_PRIO_INHERIT:
#ifdef __wasilibc_unmodified_upstream
		r = check_pi_result;
		if (r < 0) {
			volatile int lk = 0;
			r = -__syscall(SYS_futex, &lk, FUTEX_LOCK_PI, 0, 0);
			a_store(&check_pi_result, r);
		}
		if (r) return r;
		a->__attr |= 8;
		return 0;
#else
		return ENOTSUP;
#endif
	case PTHREAD_PRIO_PROTECT:
		return ENOTSUP;
	default:
		return EINVAL;
	}
}
#else
/* firebox#CDZ — faithful attr round-trip for the mutex protocol attribute.
 *
 * The prior wasi-libc stub `return 0;` (a) accepted *any* value including
 * invalid ones, and (b) stored nothing, so pthread_mutexattr_getprotocol
 * always read back PTHREAD_PRIO_NONE. Open POSIX
 * pthread_mutexattr_setprotocol/1-1 (set→get must round-trip all three valid
 * protocols) and /3-1 (an invalid protocol must fail ENOTSUP/EINVAL) both
 * failed against that stub.
 *
 * WASI has no priority scheduler, so PRIO_INHERIT/PRIO_PROTECT have no
 * behavioral effect (the protocol is a no-op at lock time) — but the attribute
 * object must still report faithfully what was set, per POSIX. So we store the
 * value and let getprotocol read it back; the only observable consequence is
 * the honest round-trip, not any scheduling change.
 *
 * Storage bits — IMPORTANT: NOT bit 3. Bit 3 (value 8) is musl's live
 * PRIO_INHERIT flag, consumed by the lock machinery
 * (pthread_mutex_{trylock,timedlock,unlock}.c test `type & 8` to route through
 * the FUTEX_*_PI priority-inheritance paths). wasix-libc has no PI-futex
 * support — those syscalls are compiled out — so setting bit 3 would activate
 * the half-wired PI branches in trylock (e.g. `(type&8) && m->_m_waiters`
 * returning a spurious EBUSY on a contended acquire) and corrupt ordinary
 * locking. Instead the protocol is recorded in a dedicated 2-bit field at bits
 * 4-5 (mask 0x30, otherwise unused in the mutexattr/_m_type bitmap), so it
 * round-trips through getprotocol without ever reaching the lock path's PI
 * logic. getprotocol decodes the same field (see pthread_attr_get.c). */
#define __MUTEXATTR_PROTOCOL_SHIFT 4
#define __MUTEXATTR_PROTOCOL_MASK  (3U << __MUTEXATTR_PROTOCOL_SHIFT)
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int protocol) {
	switch (protocol) {
	case PTHREAD_PRIO_NONE:
	case PTHREAD_PRIO_INHERIT:
	case PTHREAD_PRIO_PROTECT:
		a->__attr = (a->__attr & ~__MUTEXATTR_PROTOCOL_MASK)
			| ((unsigned)protocol << __MUTEXATTR_PROTOCOL_SHIFT);
		return 0;
	default:
		return EINVAL;
	}
}
#endif