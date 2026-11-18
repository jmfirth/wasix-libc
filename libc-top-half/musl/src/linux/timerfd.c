#include <sys/timerfd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"

#define IS32BIT(x) !((x)+0x80000000ULL>>32)

int timerfd_create(int clockid, int flags)
{
	return syscall(SYS_timerfd_create, clockid, flags);
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new, struct itimerspec *old)
{
#ifdef SYS_timerfd_settime64
	time_t is = new->it_interval.tv_sec, vs = new->it_value.tv_sec;
	long ins = new->it_interval.tv_nsec, vns = new->it_value.tv_nsec;
	int r = -ENOSYS;
	if (SYS_timerfd_settime == SYS_timerfd_settime64
	    || !IS32BIT(is) || !IS32BIT(vs) || (sizeof(time_t)>4 && old))
		r = __syscall(SYS_timerfd_settime64, fd, flags,
			((long long[]){is, ins, vs, vns}), old);
	if (SYS_timerfd_settime == SYS_timerfd_settime64 || r!=-ENOSYS)
		return __syscall_ret(r);
	if (!IS32BIT(is) || !IS32BIT(vs))
		return __syscall_ret(-ENOTSUP);
	long old32[4];
	r = __syscall(SYS_timerfd_settime, fd, flags,
		((long[]){is, ins, vs, vns}), old32);
	if (!r && old) {
		old->it_interval.tv_sec = old32[0];
		old->it_interval.tv_nsec = old32[1];
		old->it_value.tv_sec = old32[2];
		old->it_value.tv_nsec = old32[3];
	}
	return __syscall_ret(r);
#endif
	return syscall(SYS_timerfd_settime, fd, flags, new, old);
}

int timerfd_gettime(int fd, struct itimerspec *cur)
{
#ifdef SYS_timerfd_gettime64
	int r = -ENOSYS;
	if (sizeof(time_t) > 4)
		r = __syscall(SYS_timerfd_gettime64, fd, cur);
	if (SYS_timerfd_gettime == SYS_timerfd_gettime64 || r!=-ENOSYS)
		return __syscall_ret(r);
	long cur32[4];
	r = __syscall(SYS_timerfd_gettime, fd, cur32);
	if (!r) {
		cur->it_interval.tv_sec = cur32[0];
		cur->it_interval.tv_nsec = cur32[1];
		cur->it_value.tv_sec = cur32[2];
		cur->it_value.tv_nsec = cur32[3];
	}
	return __syscall_ret(r);
#endif
	return syscall(SYS_timerfd_gettime, fd, cur);
}
#else
#include <wasi/api_firebox.h>
#include <wasi/libc.h>

static void unpack(struct itimerspec *out, const int64_t value[4])
{
	out->it_interval.tv_sec = value[0];
	out->it_interval.tv_nsec = value[1];
	out->it_value.tv_sec = value[2];
	out->it_value.tv_nsec = value[3];
}

int timerfd_create(int clockid, int flags)
{
	int32_t fd;
	__wasi_errno_t error = __wasix_timerfd_create(clockid, flags, &fd);
	if (!error) return fd;
	errno = __wasilibc_errno_from_wasi(error);
	return -1;
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new, struct itimerspec *old)
{
	int64_t input[4];
	int64_t output[4];
	if (new) {
		input[0] = new->it_interval.tv_sec;
		input[1] = new->it_interval.tv_nsec;
		input[2] = new->it_value.tv_sec;
		input[3] = new->it_value.tv_nsec;
	}
	__wasi_errno_t error = __wasix_timerfd_settime(fd, flags,
	                                           new ? input : 0, old ? output : 0);
	if (error) {
		errno = __wasilibc_errno_from_wasi(error);
		return -1;
	}
	if (old) unpack(old, output);
	return 0;
}

int timerfd_gettime(int fd, struct itimerspec *cur)
{
	int64_t output[4];
	__wasi_errno_t error = __wasix_timerfd_gettime(fd, cur ? output : 0);
	if (error) {
		errno = __wasilibc_errno_from_wasi(error);
		return -1;
	}
	unpack(cur, output);
	return 0;
}
#endif
