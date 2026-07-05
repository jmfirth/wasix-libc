#define _GNU_SOURCE
#define __NEED_gid_t

#include <bits/alltypes.h>

#ifdef __wasilibc_unmodified_upstream
#include <unistd.h>
#include <signal.h>
#include "syscall.h"
#include "libc.h"

struct ctx {
	size_t count;
	const gid_t *list;
	int ret;
};

static void do_setgroups(void *p)
{
	struct ctx *c = p;
	if (c->ret<0) return;
	int ret = __syscall(SYS_setgroups, c->count, c->list);
	if (ret && !c->ret) {
		/* If one thread fails to set groups after another has already
		 * succeeded, forcibly killing the process is the only safe
		 * thing to do. State is inconsistent and dangerous. Use
		 * SIGKILL because it is uncatchable. */
		__block_all_sigs(0);
		__syscall(SYS_kill, __syscall(SYS_getpid), SIGKILL);
	}
	c->ret = ret;
}
#else
#include <errno.h>
#include <wasi/api_firebox.h> /* firebox#K9N: __wasix_proc_setgroups (#BDY) */
#endif

int setgroups(size_t count, const gid_t list[])
{
#ifdef __wasilibc_unmodified_upstream
	/* ret is initially nonzero so that failure of the first thread does not
	 * trigger the safety kill above. */
	struct ctx c = { .count = count, .list = list, .ret = 1 };
	__synccall(do_setgroups, &c);
	return __syscall_ret(c.ret);
#else
	/* firebox#K9N — setgroups(2) via the #BDY proc_setgroups import (was
	 * ENOTSUP). Privileged-only (host checks euid==0 → EPERM BEFORE the
	 * NGROUPS_MAX size check, the Linux errno order); count==0 clears the
	 * supplementary set. The wasix credential is per-PROCESS (not the raw
	 * Linux per-thread state), so musl's __synccall broadcast above is
	 * unnecessary — one host call is already process-wide. gid_t is
	 * uint32_t, so `list` passes directly. Note the import's swapped arg
	 * order: (list, count). */
	__wasi_errno_t err = __wasix_proc_setgroups((const uint32_t *)list, count);
	if (err != __WASI_ERRNO_SUCCESS) {
		errno = (int)err;
		return -1;
	}
	return 0;
#endif
}
