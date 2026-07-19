#include "pthread_impl.h"
#include <sched.h>	/* firebox#BNJ — sched_get_priority_min(SCHED_FIFO) for the
			 * prioceiling default clamp below (transitively via
			 * <pthread.h>, made explicit at the use site). */

int pthread_attr_getdetachstate(const pthread_attr_t *a, int *state)
{
	*state = a->_a_detach;
	return 0;
}
int pthread_attr_getguardsize(const pthread_attr_t *restrict a, size_t *restrict size)
{
	*size = a->_a_guardsize;
	return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t *restrict a, int *restrict inherit)
{
	*inherit = a->_a_sched;
	return 0;
}

/* firebox#QAF — un-guarded from the upstream __wasilibc_unmodified_upstream
 * block, completing the schedparam attr get/set pair (the setter
 * pthread_attr_setschedparam already stores a->_a_prio and links). The header
 * declaration (pthread.h:167) is un-guarded in the same #QAF batch. A pthread
 * attr is a passive config object, so — like the #WRQ schedpolicy round-trip —
 * it must faithfully report whatever priority was stored, independent of what
 * the substrate's single scheduling class will actually honor at create time. */
int pthread_attr_getschedparam(const pthread_attr_t *restrict a, struct sched_param *restrict param)
{
	param->sched_priority = a->_a_prio;
	return 0;
}

/* firebox#WRQ — un-guarded from the upstream __wasilibc_unmodified_upstream
 * block. The companion setter pthread_attr_setschedpolicy is already built and
 * linking (it stores a->_a_policy after validating the policy), and the header
 * declaration (pthread.h:164) is ALREADY un-guarded — only this definition was
 * left inside the guard, so pthread_attr_getschedpolicy was an undefined symbol
 * and Open POSIX pthread_attr_getschedpolicy/2-1 (set SCHED_FIFO/RR/OTHER, read
 * each back) BUILDFAILed at link. WASI has no CPU scheduler so the policy has no
 * runtime effect, but the attribute object must still report faithfully what was
 * set, per POSIX — the same round-trip stance as the #CDZ mutex-protocol fix.
 * pthread_attr_getschedparam stays guarded above because its header declaration
 * (pthread.h:167) is still guarded: the schedpolicy(un-guarded)/schedparam
 * (guarded) get+set asymmetry mirrors the header exactly. */
int pthread_attr_getschedpolicy(const pthread_attr_t *restrict a, int *restrict policy)
{
	*policy = a->_a_policy;
	return 0;
}

int pthread_attr_getscope(const pthread_attr_t *restrict a, int *restrict scope)
{
	*scope = PTHREAD_SCOPE_SYSTEM;
	return 0;
}

int pthread_attr_getstack(const pthread_attr_t *restrict a, void **restrict addr, size_t *restrict size)
{
	if (!a->_a_stackaddr)
		return EINVAL;
	*size = a->_a_stacksize;
	*addr = (void *)(a->_a_stackaddr - *size);
	return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *restrict a, size_t *restrict size)
{
	*size = a->_a_stacksize;
	return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *restrict a, int *restrict pshared)
{
	*pshared = !!a->__attr;
	return 0;
}

/* firebox#WRQ — un-guarded from the upstream __wasilibc_unmodified_upstream
 * block. The companion setter pthread_condattr_setclock already links and the
 * header declaration (pthread.h:190) is ALREADY un-guarded — only this
 * definition was left guarded, so pthread_condattr_getclock was an undefined
 * symbol and 12 Open POSIX rows (the two direct getclock tests plus 10 moat cond
 * broadcast/signal/wait tests that build a CLOCK_MONOTONIC condvar) BUILDFAILed
 * at link.
 *
 * Body note (corrects the audit's assumption): the built musl top-half compiles
 * with clockid_t == `int` (the fork's <time.h> takes musl's `typedef int
 * clockid_t`, NOT the pointer-to-`struct __clockid` from
 * libc-bottom-half/headers/public/__typedef_clockid_t.h). The linking setter is
 * ground truth — pthread_condattr_setclock.c does `a->__attr |= clk` treating
 * clk as an integer. So the getter must mirror that with `*clk = ...`; the
 * upstream #else branch `clk->id = ...` assumes the struct shape and would fail
 * to compile here (`int` has no member `id`). Reading back the low 31 bits
 * round-trips exactly what setclock stored. */
int pthread_condattr_getclock(const pthread_condattr_t *restrict a, clockid_t *restrict clk)
{
	*clk = a->__attr & 0x7fffffff;
	return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t *restrict a, int *restrict pshared)
{
	*pshared = a->__attr>>31;
	return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *restrict a, int *restrict protocol)
{
#ifdef __wasilibc_unmodified_upstream
	*protocol = a->__attr / 8U % 2;
#else
	/* firebox#CDZ — decode the 2-bit protocol field (bits 4-5) that
	 * wasix-libc's pthread_mutexattr_setprotocol stores. Upstream encodes the
	 * protocol in bit 3, but on WASI that bit is reserved for the lock path's
	 * PI machinery and must stay clear; setprotocol therefore uses bits 4-5 so
	 * all three valid protocols (NONE/INHERIT/PROTECT) round-trip without
	 * touching the live PI bit. See pthread_mutexattr_setprotocol.c. */
	*protocol = (a->__attr >> 4) & 3;
#endif
	return 0;
}
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *restrict a, int *restrict pshared)
{
	*pshared = a->__attr / 128U % 2;
	return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *restrict a, int *restrict robust)
{
	*robust = a->__attr / 4U % 2;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *restrict a, int *restrict type)
{
	*type = a->__attr & 3;
	return 0;
}

/* firebox#WRQ — mutex-attribute priority-ceiling getter. The header declares it
 * (pthread.h:174) but neither get nor set had a definition, so it was an
 * undefined symbol and Open POSIX pthread_mutexattr_getprioceiling/3-1
 * BUILDFAILed at link. The companion setter
 * (pthread_mutexattr_setprioceiling.c) stores the ceiling in bits 8-15 of
 * __attr — a field the mutex lock path never reads (trylock/lock/timedlock/
 * unlock mask __attr/_m_type only with 3/4/8/12/15/128, all in bits 0-7; the
 * #CDZ protocol field at bits 4-5 is likewise excluded from those masks), so
 * the ceiling round-trips through the attribute object without ever reaching
 * the PI/robust lock logic. WASI models no priority scheduler, so
 * PTHREAD_PRIO_PROTECT has no runtime effect; per POSIX the attribute must still
 * report faithfully what was set. See pthread_mutexattr_setprioceiling.c for the
 * field layout + range validation.
 *
 * firebox#BNJ — report an UNSET (0) ceiling as the SCHED_FIFO floor, matching
 * glibc. A freshly pthread_mutexattr_init'd attr has a zeroed ceiling field (0);
 * glibc's getter clamps that unset default up to sched_get_priority_min(
 * SCHED_FIFO) rather than reporting a raw 0 — a PRIO_PROTECT ceiling below the
 * real-time floor is meaningless. Open POSIX pthread_mutexattr_getprioceiling/
 * 1-1 asserts the default lies in [sched_get_priority_min(SCHED_FIFO),
 * sched_get_priority_max(SCHED_FIFO)] = [1,99], so it PASSes on glibc but FAILed
 * here (raw 0 < 1). Matching glibc's clamp fixes it faithfully (dominant-Linux
 * behavior, the always-more-faithful "musl-vs-glibc optional -> provide the
 * common-Linux behavior" pattern, not a fabricated value).
 *
 * Soundness — the clamp NEVER lies about a user-set value: the companion setter
 * (#BNJ) now rejects any ceiling outside [min,max] with EINVAL, exactly like
 * glibc, so a 0 field can ONLY mean unset/init'd (0 < min=1 is un-storable).
 * Privilege-free (a pure attribute default, no RT grant — unprivileged-semantics
 * tier, delivered identically on native + browser); ABI-safe (behavioral only,
 * no new exported symbol — sched_get_priority_min is already exported, Inv-8).
 * Test 3-1 reads an UNINITIALIZED attr and checks only the RETURN CODE (0 or
 * EINVAL), never the value, so the clamp leaves its rc=0 -> PASS untouched. */
#define __MUTEXATTR_PRIOCEILING_SHIFT 8
#define __MUTEXATTR_PRIOCEILING_MASK  (0xffU << __MUTEXATTR_PRIOCEILING_SHIFT)
int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *restrict a, int *restrict prioceiling)
{
	int ceiling = (int)((a->__attr & __MUTEXATTR_PRIOCEILING_MASK) >> __MUTEXATTR_PRIOCEILING_SHIFT);
	/* firebox#BNJ — an unset (0) field means init'd (the setter EINVALs any
	 * ceiling < min); report it as the SCHED_FIFO floor, as glibc does. */
	if (ceiling == 0)
		ceiling = sched_get_priority_min(SCHED_FIFO);
	*prioceiling = ceiling;
	return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *restrict a, int *restrict pshared)
{
	*pshared = a->__attr[0];
	return 0;
}
