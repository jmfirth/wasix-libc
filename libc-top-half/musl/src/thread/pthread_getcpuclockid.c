#include "pthread_impl.h"

#ifdef __wasilibc_unmodified_upstream
int pthread_getcpuclockid(pthread_t t, clockid_t *clockid)
{
	*clockid = (-t->tid-1)*8U + 6;
	return 0;
}
#else
#include <common/clock.h>
#include <stdatomic.h>
#include <wasi/libc.h>

int pthread_getcpuclockid(pthread_t t, clockid_t *clockid)
{
	pthread_t self = __pthread_self();
	int tid = 0;
	if (t == self) {
		tid = self->tid;
		if (tid == WASIX_INITIAL_LOCK_TID) {
			__wasi_errno_t error = __wasi_thread_id(&tid);
			if (error)
				return __wasilibc_errno_from_wasi(error);
		}
	} else {
		/* Exit removes the target under this lock before freeing its TCB. */
		__tl_lock();
		for (pthread_t peer = self->next; peer != self; peer = peer->next) {
			if (peer == t) {
				tid = atomic_load((atomic_int *)&peer->tid);
				break;
			}
		}
		__tl_unlock();
		if (tid == WASIX_INITIAL_LOCK_TID)
			tid = __wasilibc_initial_thread_id;
	}
	if (tid <= 0 || (uint32_t)tid > (INT32_MAX >> 3))
		return ESRCH;
	clockid_t id = __wasilibc_cpu_clockid(tid, true);
	__wasi_timestamp_t resolution;
	__wasi_errno_t error = __wasilibc_cpu_clock_get(id, 1, &resolution);
	if (error)
		return error == __WASI_ERRNO_INVAL ? ENOENT : __wasilibc_errno_from_wasi(error);
	*clockid = id;
	return 0;
}
#endif
