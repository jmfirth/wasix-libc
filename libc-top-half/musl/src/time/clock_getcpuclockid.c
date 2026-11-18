#include <time.h>
#include <errno.h>
#include <unistd.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <common/clock.h>
#include <wasi/libc.h>
#endif

int clock_getcpuclockid(pid_t pid, clockid_t *clk)
{
#ifdef __wasilibc_unmodified_upstream
	struct timespec ts;
	clockid_t id = (-pid-1)*8U + 2;
	int ret = __syscall(SYS_clock_getres, id, &ts);
	if (ret) return -ret;
	*clk = id;
#else
	if (pid < 0 || (uint32_t)pid > (INT32_MAX >> 3))
		return ESRCH;
	clockid_t id = __wasilibc_cpu_clockid(pid, false);
	__wasi_timestamp_t resolution;
	__wasi_errno_t error = __wasilibc_cpu_clock_get(id, 1, &resolution);
	if (error)
		return error == __WASI_ERRNO_INVAL ? ENOSYS : __wasilibc_errno_from_wasi(error);
	*clk = id;
#endif
	return 0;
}
