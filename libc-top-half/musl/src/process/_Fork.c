#include <unistd.h>
#include <signal.h>
#include "syscall.h"
#ifdef __wasilibc_unmodified_upstream
#else
#include <wasi/api.h>
#include <errno.h>
#include <sys/resource.h>
#include <__wasilibc_rlimit.h>
#endif
#include "libc.h"
#include "lock.h"
#include "pthread_impl.h"
#include "aio_impl.h"

/* firebox#CBP — _Fork() is DEFINED in EH builds too; see the rationale header
 * in fork.c. Upstream 0008d18 hid this TU behind
 * `!defined(__wasm_exception_handling__)`, which erased `_Fork` and
 * `__aio_atfork` from every EH libc. The only host contact below is the
 * `__wasi_proc_fork` import — nothing here interacts with the EH/unwind model,
 * and a host that cannot fork answers Errno::Notsup, which this function
 * propagates as -1/errno (the faithful POSIX failure) rather than the program
 * failing to link. */

static void dummy(int x) { }
weak_alias(dummy, __aio_atfork);

#ifndef __wasilibc_unmodified_upstream
/* firebox#VYD — clear the child's queued/pending signal state (defined in
 * signal/sigaction.c). POSIX fork(2): the child's set of pending signals is
 * empty. The child inherits a COPY of the parent's linear memory, so without
 * this its __fbx_rtsigq RT ring keeps the parent's queued records — and since
 * regression/raise-race's handler1 fork()s from INSIDE the ring-drain loop, a
 * child with a non-empty ring would re-enter that loop and fork grandchildren
 * unboundedly. Clearing here makes the child's drain re-check see an empty ring
 * and exit. */
extern void __fbx_clear_pending_on_fork(void);
#endif

pid_t _Fork(int copy_mem)
{
	pid_t ret;
	sigset_t set;
#ifndef __wasilibc_unmodified_upstream
	// firebox#AB2 — RLIMIT_NPROC fork admission control.
	//
	// Linux fails fork()/clone() with EAGAIN in copy_process when the calling
	// user's live process count already meets or exceeds RLIMIT_NPROC (the soft
	// limit). WASI exposes no host process-count authority the guest can
	// consult, but the calling process ITSELF always counts as at least one
	// process against that per-user limit — so whenever the soft limit is <= 1
	// the count is already met with certainty and fork MUST fail EAGAIN. That
	// reproduces Linux exactly for RLIMIT_NPROC 0 and 1 (the cases decidable in
	// the guest) with zero risk of a false denial; the default limit is
	// RLIM_INFINITY, so an unconstrained process is never touched. Soft limits
	// >= 2 need a global live-process count only the host control plane tracks
	// and are left unenforced here (documented in work/tasks/AB2). Both fork()
	// and the non-EH vfork() (which is _fork_internal(0)) funnel through here,
	// so both honor the limit. The atfork PARENT handlers still run on this
	// failure path — _fork_internal calls __fork_handler(!ret)==__fork_handler(0)
	// for ret<0, the parent phase — and the EAGAIN errno survives their clobber
	// via _fork_internal's errno_save/restore. That errno-across-a-failed-fork
	// contract is exactly what regression/pthread_atfork-errno-clobber pins.
	{
		struct rlimit __fbx_nproc_rlim;
		__wasilibc_get_stored_rlimit(RLIMIT_NPROC, &__fbx_nproc_rlim);
		if (__fbx_nproc_rlim.rlim_cur != RLIM_INFINITY &&
		    __fbx_nproc_rlim.rlim_cur <= 1) {
			errno = EAGAIN;
			return -1;
		}
	}
#endif
	__block_all_sigs(&set);
	__aio_atfork(-1);
	LOCK(__abort_lock);
#ifdef __wasilibc_unmodified_upstream
#ifdef SYS_fork
	ret = __syscall(SYS_fork);
#else
	ret = __syscall(SYS_clone, SIGCHLD, 0);
#endif
#else
	__wasi_pid_t pid = -1;
    int err = __wasi_proc_fork(copy_mem, &pid);
	if (err != 0) {
		ret = -err;
	} else {
		ret = (int)pid;
	}
#endif
	if (!ret) {
		pthread_t self = __pthread_self();
#ifndef __wasilibc_unmodified_upstream
		/* firebox#VYD — POSIX: the child starts with an EMPTY pending-signal
		 * set. Clear the inherited __fbx_rtsigq ring + pending bitmasks so a
		 * child forked from inside an RT signal handler's ring-drain does not
		 * re-deliver the parent's queued instances (regression/raise-race). */
		__fbx_clear_pending_on_fork();
#endif
#ifdef __wasilibc_unmodified_upstream
		self->tid = __syscall(SYS_gettid);
#else
		int r = __wasi_thread_id(&self->tid);
		if (r != 0) {
			/* Beyond this point should be unreachable. */
			a_crash();
			raise(SIGKILL);
			_Exit(127);
		}
#endif
		self->robust_list.off = 0;
		self->robust_list.pending = 0;
		self->next = self->prev = self;
		__thread_list_lock = 0;
		libc.threads_minus_1 = 0;
		if (libc.need_locks) libc.need_locks = -1;
	}
	UNLOCK(__abort_lock);
	__aio_atfork(!ret);
	__restore_sigs(&set);
	return __syscall_ret(ret);
}