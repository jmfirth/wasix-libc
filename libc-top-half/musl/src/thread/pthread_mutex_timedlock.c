#include "pthread_impl.h"

#ifndef __wasilibc_unmodified_upstream
#include <wasi/api.h>
#include <stdint.h>
#include <stddef.h>

/* firebox#533 — pthread_mutex_timedlock return-path instrumentation.
 * See pthread_mutex_trylock.c for the cycle-8 framing. Stages 21..29
 * reserved for this TU; 11..17 belong to pthread_mutex_trylock.c.
 *
 * Stages:
 *   stage=21: post-__pthread_mutex_trylock non-EBUSY return at top
 *             (line 76). Forwards e.g. EAGAIN/ENOTRECOVERABLE/EOWNERDEAD
 *             from the trylock layer; if the trylock stamps emit a
 *             matching stage=11..17, the wedge is at the trylock layer.
 *             If THIS stamps but NO trylock stamp matches, the wedge is
 *             between trylock's return and timedlock's return-check
 *             (extremely unlikely — direct stack return).
 *   stage=22: pthread_mutex_timedlock_pi non-zero return (gated on
 *             __wasilibc_unmodified_upstream — won't fire under wasi
 *             since the entire pi branch is upstream-only).
 *   stage=23: final non-zero exit (line 101). Catches any non-zero r
 *             surfaced from the __timedwait + relock retry loop.
 *
 * Emission identical to firebox_529_cond_wait_trace per #529's discipline.
 */
static __attribute__((noinline,used))
void firebox_533_timedlock_trace(int stage, int rc) {
	char buf[80];
	size_t off = 0;
	static const char prefix[] = "FIREBOX_533_MUTEX_LOCK_RETURN stage=";
	for (size_t i = 0; i < sizeof(prefix) - 1; ++i) {
		buf[off++] = prefix[i];
	}
	if (stage < 0) { buf[off++] = '-'; stage = -stage; }
	if (stage == 0) {
		buf[off++] = '0';
	} else {
		char tmp[12]; size_t n = 0;
		while (stage > 0) { tmp[n++] = (char)('0' + (stage % 10)); stage /= 10; }
		while (n--) buf[off++] = tmp[n];
	}
	static const char mid[] = " rc=";
	for (size_t i = 0; i < sizeof(mid) - 1; ++i) {
		buf[off++] = mid[i];
	}
	if (rc < 0) { buf[off++] = '-'; rc = -rc; }
	if (rc == 0) {
		buf[off++] = '0';
	} else {
		char tmp[12]; size_t n = 0;
		while (rc > 0) { tmp[n++] = (char)('0' + (rc % 10)); rc /= 10; }
		while (n--) buf[off++] = tmp[n];
	}
	buf[off++] = '\n';

	__wasi_ciovec_t iov;
	iov.buf = (const uint8_t *)buf;
	iov.buf_len = off;
	__wasi_size_t nwritten;
	(void)__wasi_fd_write(2, &iov, 1, &nwritten);
}
#endif

#ifdef __wasilibc_unmodified_upstream
#define IS32BIT(x) !((x)+0x80000000ULL>>32)
#define CLAMP(x) (int)(IS32BIT(x) ? (x) : 0x7fffffffU+((0ULL+(x))>>63))

#ifdef __wasilibc_unmodified_upstream
static int __futex4(volatile void *addr, int op, int val, const struct timespec *to)
{
#ifdef SYS_futex_time64
	time_t s = to ? to->tv_sec : 0;
	long ns = to ? to->tv_nsec : 0;
	int r = -ENOSYS;
	if (SYS_futex == SYS_futex_time64 || !IS32BIT(s))
		r = __syscall(SYS_futex_time64, addr, op, val,
			to ? ((long long[]){s, ns}) : 0);
	if (SYS_futex == SYS_futex_time64 || r!=-ENOSYS) return r;
	to = to ? (void *)(long[]){CLAMP(s), ns} : 0;
#endif
	return __syscall(SYS_futex, addr, op, val, to);
}
#endif

static int pthread_mutex_timedlock_pi(pthread_mutex_t *restrict m, const struct timespec *restrict at)
{
	int type = m->_m_type;
	int priv = (type & 128) ^ 128;
	pthread_t self = __pthread_self();
	int e;

	if (!priv) self->robust_list.pending = &m->_m_next;

#ifdef __wasilibc_unmodified_upstream
	do e = -__futex4(&m->_m_lock, FUTEX_LOCK_PI|priv, 0, at);
	while (e==EINTR);
	if (e) self->robust_list.pending = 0;
#else
   e = 0;
#endif

	switch (e) {
	case 0:
		/* Catch spurious success for non-robust mutexes. */
		if (!(type&4) && ((m->_m_lock & 0x40000000) || m->_m_waiters)) {
			a_store(&m->_m_waiters, -1);
#ifdef __wasilibc_unmodified_upstream
			__syscall(SYS_futex, &m->_m_lock, FUTEX_UNLOCK_PI|priv);
#endif
			self->robust_list.pending = 0;
			break;
		}
		/* Signal to trylock that we already have the lock. */
		m->_m_count = -1;
		return __pthread_mutex_trylock(m);
	case ETIMEDOUT:
		return e;
	case EDEADLK:
		if ((type&3) == PTHREAD_MUTEX_ERRORCHECK) return e;
	}
	do e = __timedwait(&(int){0}, 0, CLOCK_REALTIME, at, 1);
	while (e != ETIMEDOUT);
	return e;
}
#endif

int __pthread_mutex_timedlock(pthread_mutex_t *restrict m, const struct timespec *restrict at)
{
	if ((m->_m_type&15) == PTHREAD_MUTEX_NORMAL
	    && !a_cas(&m->_m_lock, 0, EBUSY))
		return 0;

	int type = m->_m_type;
	int r, t, priv = (type & 128) ^ 128;

	r = __pthread_mutex_trylock(m);
	if (r != EBUSY) {
#ifndef __wasilibc_unmodified_upstream
		/* firebox#533 stage=21: top-level __pthread_mutex_trylock
		 * returned non-EBUSY (may be 0 success or any other errno).
		 * Stamp only on non-zero, since zero is the success path. */
		if (r != 0) firebox_533_timedlock_trace(21, r);
#endif
		return r;
	}

#ifdef __wasilibc_unmodified_upstream
	if (type&8) return pthread_mutex_timedlock_pi(m, at);
#endif

	int spins = 100;
	while (spins-- && m->_m_lock && !m->_m_waiters) a_spin();

	while ((r=__pthread_mutex_trylock(m)) == EBUSY) {
		r = m->_m_lock;
		int own = r & 0x3fffffff;
		if (!own && (!r || (type&4)))
			continue;
		if ((type&3) == PTHREAD_MUTEX_ERRORCHECK
		    && own == __pthread_self()->tid)
			return EDEADLK;

		a_inc(&m->_m_waiters);
		t = r | 0x80000000;
		a_cas(&m->_m_lock, r, t);
		r = __timedwait(&m->_m_lock, t, CLOCK_REALTIME, at, priv);
		a_dec(&m->_m_waiters);
		if (r && r != EINTR) break;
	}
#ifndef __wasilibc_unmodified_upstream
	/* firebox#533 stage=23: final exit from the spin+wait+retry loop.
	 * Only stamps on non-zero (the success path normally exits via the
	 * trylock branch at stage=21 with r==0, but the loop can also exit
	 * with r==0 after a successful contended acquire). */
	if (r != 0) firebox_533_timedlock_trace(23, r);
#endif
	return r;
}

weak_alias(__pthread_mutex_timedlock, pthread_mutex_timedlock);
