#include <sys/resource.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#ifndef __wasilibc_unmodified_upstream
#include <__wasilibc_mq.h>
#include <__wasilibc_rlimit.h>
// firebox#48H: __wasix_resource_get_nofile (api.h pulls in api_firebox.h) and
// __wasilibc_errno_from_wasi (wasi/libc.h).
#include <stdint.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#endif

#define FIX(x) do{ if ((x)>=SYSCALL_RLIM_INFINITY) (x)=RLIM_INFINITY; }while(0)

int getrlimit(int resource, struct rlimit *rlim)
{
#ifdef __wasilibc_unmodified_upstream
	unsigned long k_rlim[2];
	int ret = syscall(SYS_prlimit64, 0, resource, 0, rlim);
	if (!ret) {
		FIX(rlim->rlim_cur);
		FIX(rlim->rlim_max);
	}
	if (!ret || errno != ENOSYS)
		return ret;
	if (syscall(SYS_getrlimit, resource, k_rlim) < 0)
		return -1;
	rlim->rlim_cur = k_rlim[0] == -1UL ? RLIM_INFINITY : k_rlim[0];
	rlim->rlim_max = k_rlim[1] == -1UL ? RLIM_INFINITY : k_rlim[1];
	FIX(rlim->rlim_cur);
	FIX(rlim->rlim_max);
#else
	// firebox#KZ1: WASI has no host getrlimit syscall; return the limits
	// setrlimit() stored for this resource (RLIM_INFINITY if never set).
	if (resource < 0 || resource >= RLIM_NLIMITS) {
		errno = EINVAL;
		return -1;
	}
	// firebox#48H: RLIMIT_NOFILE lives on the host fd table, which enforces it
	// (EMFILE at fd number >= soft) and holds a finite default (1024, 4096) from
	// the first instruction. Report THAT limit, not this table's echo: the echo
	// read RLIM_INFINITY until a program called setrlimit, a value Linux never
	// reports and programs size tables from (GNU patch's dirfd cache died "out of
	// memory" on it), and a read-modify-write then passed hard=INFINITY back to
	// setrlimit, which the host rightly refused as a hard-limit raise (EPERM).
	if (resource == RLIMIT_NOFILE ||
	    (resource == RLIMIT_MSGQUEUE && __wasilibc_mq_host_supported())) {
		uint64_t lim[2];
		int32_t e = resource == RLIMIT_NOFILE ? __wasix_resource_get_nofile(lim)
		                                     : __wasilibc_mq_rlimit(0, lim);
		if (e != __WASI_ERRNO_SUCCESS) {
			errno = __wasilibc_errno_from_wasi((int)e);
			return -1;
		}
		rlim->rlim_cur = lim[0];
		rlim->rlim_max = lim[1];
		return 0;
	}
	__wasilibc_get_stored_rlimit(resource, rlim);
#endif
	return 0;
}

weak_alias(getrlimit, getrlimit64);
