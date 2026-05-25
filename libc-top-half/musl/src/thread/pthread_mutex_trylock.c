#include "pthread_impl.h"

#ifndef __wasilibc_unmodified_upstream
#include <wasi/api.h>
#include <stdint.h>
#include <stddef.h>

/* firebox#533 — pthread_mutex_lock return-path instrumentation.
 *
 * Predecessor: #529 (cascade-9 cycle-7) instrumented all non-zero return
 * paths from __pthread_cond_timedwait and found ZERO sentinels emitted
 * despite the libuv cond_wait_fail crumb firing on 3-4 runs across 30+
 * attempts — AND found the dominant failure-crumb distribution had
 * shifted to mutex_lock_fail (77% vs 23% cond_wait_fail, falsifying
 * #527's "100% cond_wait_fail" pin). #533 instruments the actual
 * dominant surface: pthread_mutex_trylock + pthread_mutex_timedlock,
 * which together implement pthread_mutex_lock (one-liner forwarder).
 *
 * Stages for this TU (pthread_mutex_trylock):
 *   stage=11: EAGAIN early — RECURSIVE overflow (line 19)
 *   stage=12: ENOTRECOVERABLE — robust dead-owner (line 24)
 *   stage=13: EBUSY — already held (line 25)
 *   stage=14: ENOTRECOVERABLE — robust waiters after CAS-fail (line 41)
 *   stage=15: EBUSY — CAS failed (line 42)
 *   stage=16: ENOTRECOVERABLE/EBUSY — _m_count<0 fast-path waiters (success path)
 *   stage=17: EOWNERDEAD — robust hand-off (success path, old!=0)
 *
 * Stages 21..29 reserved for pthread_mutex_timedlock TU.
 *
 * Stage 30 (reached-probe) reserved for pthread_cond_timedwait TU.
 *
 * Emission shape mirrors firebox_529_cond_wait_trace per its discipline:
 *   - raw __wasi_fd_write (no stdio — stdio mutex may be the wedge actor)
 *   - hand-rolled itoa (no snprintf — locale lock + reentrancy)
 *   - stack buffer (no malloc — heap may be wedged)
 *   - noinline,used + file-static — survives LTO + DCE
 *
 * Sentinel literal: "FIREBOX_533_MUTEX_LOCK_RETURN stage=<N> rc=<errno>\n"
 *
 * Retirement: when cascade-9 closes at the actual layer (tracked in
 * docs/reference/forks.md §5 retirement registry).
 */
static __attribute__((noinline,used))
void firebox_533_mutex_lock_trace(int stage, int rc) {
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
			if ((unsigned)m->_m_count >= INT_MAX) {
#ifndef __wasilibc_unmodified_upstream
				/* firebox#533 stage=11: EAGAIN — RECURSIVE overflow.
				 * Caller is holding a recursive mutex with INT_MAX
				 * acquisitions. Vanishingly rare in well-behaved code. */
				firebox_533_mutex_lock_trace(11, EAGAIN);
#endif
				return EAGAIN;
			}
			m->_m_count++;
			return 0;
		}
	}
	if (own == 0x3fffffff) {
#ifndef __wasilibc_unmodified_upstream
		/* firebox#533 stage=12: ENOTRECOVERABLE — owner field marker
		 * indicates a robust-mutex dead-owner unrecoverable state.
		 * Possible if a prior holder thread exited without unlocking
		 * AND the mutex has been marked irrecoverable. */
		firebox_533_mutex_lock_trace(12, ENOTRECOVERABLE);
#endif
		return ENOTRECOVERABLE;
	}
	if (own || (old && !(type & 4))) {
		/* firebox#533: stage=13 (EBUSY) intentionally NOT stamped.
		 * Standard contention path — fires on every contended trylock
		 * (millions of calls under libuv load). Earlier draft emitted
		 * here and caused 22/22 OOB regression via stderr-write storm
		 * + thread-OOB on the resulting fd-table contention. EBUSY
		 * isn't a terminal abort cause for libuv anyway — libuv loops
		 * around it via __pthread_mutex_timedlock's retry. */
		return EBUSY;
	}

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
		if ((type&12)==12 && m->_m_waiters) {
#ifndef __wasilibc_unmodified_upstream
			/* firebox#533 stage=14: ENOTRECOVERABLE — robust mutex
			 * with waiters where CAS lost; promoted from EBUSY by
			 * the type&12 robust-with-waiters discriminator. */
			firebox_533_mutex_lock_trace(14, ENOTRECOVERABLE);
#endif
			return ENOTRECOVERABLE;
		}
		/* firebox#533: stage=15 (EBUSY-CAS-lost) intentionally NOT
		 * stamped. Same hot-path rationale as stage=13 above. */
		return EBUSY;
	}

success:
	if ((type&8) && m->_m_waiters) {
		int priv = (type & 128) ^ 128;
#ifdef __wasilibc_unmodified_upstream
		__syscall(SYS_futex, &m->_m_lock, FUTEX_UNLOCK_PI|priv);
#endif
		self->robust_list.pending = 0;
#ifndef __wasilibc_unmodified_upstream
		/* firebox#533 stage=16: ENOTRECOVERABLE or EBUSY — PI mutex
		 * with waiters where we acquired but must hand off; rare in
		 * wasi (PI codepath is __wasilibc_unmodified_upstream-gated
		 * for the actual futex op, but the libc bookkeeping runs). */
		firebox_533_mutex_lock_trace(16, (type&4) ? ENOTRECOVERABLE : EBUSY);
#endif
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
#ifndef __wasilibc_unmodified_upstream
		/* firebox#533 stage=17: EOWNERDEAD — robust mutex acquired
		 * but a prior owner died holding it; caller is now the new
		 * owner but must validate the protected state. POSIX permits
		 * libuv (and any caller) to treat this as non-zero and abort. */
		firebox_533_mutex_lock_trace(17, EOWNERDEAD);
#endif
		return EOWNERDEAD;
	}

	return 0;
}

int __pthread_mutex_trylock(pthread_mutex_t *m)
{
	if ((m->_m_type&15) == PTHREAD_MUTEX_NORMAL)
		return a_cas(&m->_m_lock, 0, EBUSY) & EBUSY;
	return __pthread_mutex_trylock_owner(m);
}

weak_alias(__pthread_mutex_trylock, pthread_mutex_trylock);
