#define _GNU_SOURCE
#include <spawn.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/wait.h>
#include "syscall.h"
#else
#include <wasi/api.h>
#include <wasi/api_firebox.h>  /* firebox#BZ5 — __wasix_proc_stage_spawn_pgid */
#endif
#include "lock.h"
#include "pthread_impl.h"
#include "fdop.h"
#include "libc.h"
#include <wasi/libc.h>

#ifndef __wasilibc_unmodified_upstream
/* firebox#DNG/#J04 — where an OPEN_MODE action's create mode lives: the uint32_t
 * right after `fdflagsext`, inside what the generated struct leaves as tail
 * padding, so the wire size (and every older guest's array stride) is unchanged.
 * The runtime's ProcSpawnFdOp declares the same field at the same offset. */
#define FBX_SPAWN_FD_OP_MODE_OFFSET \
	(offsetof(__wasi_proc_spawn_fd_op_t, fdflagsext) + sizeof(__wasi_fdflagsext_t))
_Static_assert(FBX_SPAWN_FD_OP_MODE_OFFSET % 4 == 0, "mode is 4-aligned");
_Static_assert(FBX_SPAWN_FD_OP_MODE_OFFSET + 4 <= sizeof(__wasi_proc_spawn_fd_op_t),
	       "mode fits in the tail padding");
#endif

#ifdef __wasilibc_unmodified_upstream
#else
pid_t waitpid(pid_t pid, int *status, int options);
#endif

#ifdef __wasilibc_unmodified_upstream
#elif defined(__wasilibc_fork_based_posix_spawn)
struct args {
	int p[2];
	sigset_t oldmask;
	const char *path;
	const posix_spawn_file_actions_t *fa;
	const posix_spawnattr_t *restrict attr;
	char *const *argv, *const *envp;
};

static int __sys_dup2(int old, int new)
{
#ifdef __wasilibc_unmodified_upstream
#ifdef SYS_dup2
	return __syscall(SYS_dup2, old, new);
#else
	return __syscall(SYS_dup3, old, new, 0);
#endif
#else
	__wasi_errno_t error = __wasi_fd_renumber(old, new);
	if (error != 0) {
		errno = __wasilibc_errno_from_wasi(error);
		return -1;
	}
	return 0;
#endif
}

static int child(void *args_vp)
{
	int i, ret;
	struct sigaction sa = {0};
	struct args *args = args_vp;
	int p = args->p[1];
	const posix_spawn_file_actions_t *fa = args->fa;
	const posix_spawnattr_t *restrict attr = args->attr;
	sigset_t hset;

	close(args->p[0]);

	/* All signal dispositions must be either SIG_DFL or SIG_IGN
	 * before signals are unblocked. Otherwise a signal handler
	 * from the parent might get run in the child while sharing
	 * memory, with unpredictable and dangerous results. To
	 * reduce overhead, sigaction has tracked for us which signals
	 * potentially have a signal handler. */
	__get_handler_set(&hset);
	for (i=1; i<_NSIG; i++) {
		if ((attr->__flags & POSIX_SPAWN_SETSIGDEF)
&& sigismember(&attr->__def, i)) {
			sa.sa_handler = SIG_DFL;
		} else if (sigismember(&hset, i)) {
			if (i-32<3U) {
				sa.sa_handler = SIG_IGN;
			} else {
#ifdef __wasilibc_unmodified_upstream
				__libc_sigaction(i, 0, &sa);
#else
				sigaction(i, &sa, &sa);
#endif
				if (sa.sa_handler==SIG_IGN) continue;
				sa.sa_handler = SIG_DFL;
			}
		} else {
			continue;
		}
#ifdef __wasilibc_unmodified_upstream
		__libc_sigaction(i, &sa, 0);
#else
		sigaction(i, &sa, &sa);
#endif
	}

#ifdef __wasilibc_unmodified_upstream
	if (attr->__flags & POSIX_SPAWN_SETSID)
		if ((ret=__syscall(SYS_setsid)) < 0)
			goto fail;

	if (attr->__flags & POSIX_SPAWN_SETPGROUP)
		if ((ret=__syscall(SYS_setpgid, 0, attr->__pgrp)))
			goto fail;

	/* Use syscalls directly because the library functions attempt
	 * to do a multi-threaded synchronized id-change, which would
	 * trash the parent's state. */
	if (attr->__flags & POSIX_SPAWN_RESETIDS)
		if ((ret=__syscall(SYS_setgid, __syscall(SYS_getgid))) ||
			(ret=__syscall(SYS_setuid, __syscall(SYS_getuid))) )
			goto fail;
#endif

	if (fa && fa->__actions) {
		struct fdop *op;
		int fd;
		int err;
		for (op = fa->__actions; op->next; op = op->next);
		for (; op; op = op->prev) {
			/* It's possible that a file operation would clobber
			 * the pipe fd used for synchronizing with the
			 * parent. To avoid that, we dup the pipe onto
			 * an unoccupied fd. */
			if (op->fd == p) {
#ifdef __wasilibc_unmodified_upstream
				ret = __syscall(SYS_dup, p);
#else
				ret = dup(p);
#endif
				if (ret < 0) goto fail;
#ifdef __wasilibc_unmodified_upstream
				__syscall(SYS_close, p);
#else
				err = __wasi_fd_close(p);
#endif
				p = ret;
			}
			switch(op->cmd) {
			case FDOP_CLOSE:
#ifdef __wasilibc_unmodified_upstream
				__syscall(SYS_close, op->fd);
#else
				err = __wasi_fd_close(op->fd);
#endif
				break;
			case FDOP_DUP2:
				fd = op->srcfd;
				if (fd == p) {
					ret = -EBADF;
					goto fail;
				}

				if (fd != op->fd) {
#ifdef __wasilibc_unmodified_upstream
					if ((ret=__sys_dup2(fd, op->fd))<0)
						goto fail;
#else
					if ((ret=dup2(fd, op->fd))<0)
						goto fail;
#endif
				} else {
#ifdef __wasilibc_unmodified_upstream
					ret = __syscall(SYS_fcntl, fd, F_GETFD);
					ret = __syscall(SYS_fcntl, fd, F_SETFD,
									ret & ~FD_CLOEXEC);
					if (ret<0)
						goto fail;
#else
					ret = -EBADF;
					goto fail;
#endif
				}
				break;
			case FDOP_OPEN:
#ifdef __wasilibc_unmodified_upstream
				fd = __sys_open(op->path, op->oflag, op->mode);
#else
				/* firebox#DNG/#J04 — the mode is open's third argument (it
				 * was OR-ed into the flags, so the create mode was garbage). */
				fd = open(op->path, op->oflag, op->mode);
#endif
				if ((ret=fd) < 0) goto fail;
				if (fd != op->fd) {
#ifdef __wasilibc_unmodified_upstream
					if ((ret=__sys_dup2(fd, op->fd))<0)
#else
					if ((ret=dup2(fd, op->fd))<0)
#endif
						goto fail;
#ifdef __wasilibc_unmodified_upstream
					__syscall(SYS_close, fd);
#else
					close(fd);
#endif
				}
				break;
			case FDOP_CHDIR:
#ifdef __wasilibc_unmodified_upstream
				ret = __syscall(SYS_chdir, op->path);
#else
				ret = chdir(op->path);
#endif
				if (ret<0) goto fail;
				break;
			case FDOP_FCHDIR:
#ifdef __wasilibc_unmodified_upstream
				ret = __syscall(SYS_fchdir, op->fd);
#else
				ret = -EINVAL;
				goto fail;
#endif
				if (ret<0) goto fail;
				break;
			}
		}
	}

	/* Close-on-exec flag may have been lost if we moved the pipe
	 * to a different fd. We don't use F_DUPFD_CLOEXEC above because
	 * it would fail on older kernels and atomicity is not needed --
	 * in this process there are no threads or signal handlers. */
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_fcntl, p, F_SETFD, FD_CLOEXEC);
#endif

	pthread_sigmask(SIG_SETMASK, (attr->__flags & POSIX_SPAWN_SETSIGMASK)
? &attr->__mask : &args->oldmask, 0);

	int (*exec)(const char *, char *const *, char *const *) =
		attr->__fn ? (int (*)())attr->__fn : execve;

	exec(args->path, args->argv, args->envp);
	ret = -errno;

fail:
	/* Since sizeof errno < PIPE_BUF, the write is atomic. */
	ret = -ret;
#ifdef __wasilibc_unmodified_upstream
	if (ret) while (__syscall(SYS_write, p, &ret, sizeof ret) < 0);
#else
	if (ret) while (write(p, &ret, sizeof ret) < 0);
#endif
	_exit(127);
}


int posix_spawn(pid_t *restrict res, const char *restrict path,
				const posix_spawn_file_actions_t *fa,
				const posix_spawnattr_t *restrict attr,
				char *const argv[restrict], char *const envp[restrict])
{
	pid_t pid;
	char stack[1024+PATH_MAX];
	int ec=0, cs;
	struct args args;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);

	args.path = path;
	args.fa = fa;
	args.attr = attr ? attr : &(const posix_spawnattr_t){0};
	args.argv = argv;
	args.envp = envp;
	pthread_sigmask(SIG_BLOCK, SIGALL_SET, &args.oldmask);

	/* The lock guards both against seeing a SIGABRT disposition change
	 * by abort and against leaking the pipe fd to fork-without-exec. */
	LOCK(__abort_lock);

	if (pipe2(args.p, O_CLOEXEC)) {
		UNLOCK(__abort_lock);
		ec = errno;
		goto fail;
	}

	pid = __clone(child, stack+sizeof stack,
				  CLONE_VM|CLONE_VFORK|SIGCHLD, &args);
	close(args.p[1]);
	UNLOCK(__abort_lock);

	if (pid > 0) {
		if (read(args.p[0], &ec, sizeof ec) != sizeof ec) ec = 0;
		else waitpid(pid, &(int){0}, 0);
	} else {
		ec = -pid;
	}

	close(args.p[0]);

	if (!ec && res) *res = pid;

fail:
	pthread_sigmask(SIG_SETMASK, &args.oldmask, 0);
	pthread_setcancelstate(cs, 0);

	return ec;
}
#else
char *__wasilibc_exec_combine_strings(char *const strings[]);
/* firebox#39G: the length-carrying packer and the length-carrying wrapper. The
 * double-NUL scan they replace cannot tell an empty argv element from the end
 * of the buffer, so posix_spawn silently truncated argv at the first `""`. */
char *__wasilibc_exec_combine_strings_len(char *const strings[], size_t *out_len);
__wasi_errno_t __wasilibc_proc_spawn2_n(
	const char *name, const char *args, size_t args_len, const char *envs,
	size_t envs_len, const __wasi_proc_spawn_fd_op_t *fd_ops, size_t fd_ops_len,
	const __wasi_signal_disposition_t *signal_dispositions,
	size_t signal_dispositions_len, __wasi_bool_t search_path, const char *path,
	__wasi_pid_t *retptr0);

int __posix_spawn(pid_t *restrict res, const char *restrict path,
				  const posix_spawn_file_actions_t *fa,
				  const posix_spawnattr_t *restrict attr,
				  char *const argv[restrict], char *const envp[restrict],
				  uint8_t use_path)
{
	/* POSIX: `attrp` may be NULL, meaning "all defaults". The fork-based path
	 * normalises it (`args.attr = attr ? attr : &(const posix_spawnattr_t){0}`)
	 * but this one never did, so every `attr->__flags` read below — and the
	 * SETSIGDEF read that has been here all along — dereferenced NULL for the
	 * extremely ordinary `posix_spawn(&pid, path, NULL, NULL, argv, envp)`.
	 * Normalise identically, at the top, so the whole function can read `attr`
	 * unconditionally the way the fork path does. */
	const posix_spawnattr_t __fbx_default_attr = {0};
	if (!attr) attr = &__fbx_default_attr;

	/* firebox#7K0 — POSIX_SPAWN_RESETIDS.
	 *
	 * It is read ONLY in the `__wasilibc_unmodified_upstream` block above. The
	 * live `proc_spawn2` path ignored it, so a caller who set the flag got 0
	 * back and a child still under the parent's effective ids, with nothing
	 * saying so. Invariant 0 forbids that false success: an honest ENOSYS is
	 * faithful, a made-up 0 never is. Invariant 4 also requires an absent
	 * capability to be discoverable through a standard POSIX mechanism. For
	 * `posix_spawn` that mechanism is its RETURN VALUE, which carries the error
	 * number directly and does not set `errno`.
	 *
	 * The flag is refused HERE, at the top, before `fdops` is allocated and
	 * before any host call. POSIX lets the implementation fail the call
	 * outright, and a caller who asked for a guarantee we cannot give is better
	 * served by no child than by a half-configured one it believes is correct.
	 *
	 * RESETIDS is a missing CHANNEL, not a missing mechanism. The host carries
	 * the full POSIX id triple (`os/task/process.rs`:
	 * `Credential { ruid, euid, suid, rgid, egid, sgid, groups }`), with real
	 * `setuid`/`setreuid`/`setresuid` transitions and the
	 * `proc_getcred`/`proc_setcred` syscalls (firebox#MHZ/#BDY). What is missing
	 * is the WIRING: `proc_spawn2` never mentions `cred`/`Credential`, so the
	 * parent cannot ask for the child's effective ids to be reset to its real
	 * ids. That is the firebox#BZ5 class, and it RETIRES the way #BZ5 and SETSID
	 * did: through a staging call on the calling `WasiThread` that
	 * `proc_spawn2` consumes before dispatch.
	 *
	 * POSIX_SPAWN_SETSID was refused here too until firebox#7RS gave the host a
	 * session model. It is now staged below, beside SETPGROUP.
	 *
	 * ⛔ DO NOT FOLD IN SETSCHEDPARAM / SETSCHEDULER / USEVFORK. musl upstream
	 * ignores those in BOTH paths, so ignoring them here MATCHES the reference
	 * implementation. Refusing them would be a divergence FROM musl presented
	 * as faithfulness, and would break callers that pass them harmlessly today.
	 *
	 * BLAST RADIUS. The shipped `packages/` were censused before this landed
	 * (2026-09-19). Every `posix_spawnattr_setflags` call site in the tree sets
	 * a literal that excludes RESETIDS:
	 *   - `packages/tini/tini.c` (SETSIGMASK|SETSIGDEF)
	 *   - cmake's vendored libuv patch (SETSIGDEF|SETSIGMASK)
	 *   - cmake's kwsys patch (SETSIGDEF alone)
	 *   - ccache's patched `execute.c`, which passes a NULL `attrp` outright.
	 * CPython's `posixmodule.c` sets the flag only when a Python caller passes
	 * `resetids=True` to `os.posix_spawn`, an opt-in whose documented contract
	 * already allows an error. */
	if (attr->__flags & POSIX_SPAWN_RESETIDS)
		return ENOSYS;

	int nfdops = 0;
	__wasi_proc_spawn_fd_op_t *fdops = NULL;

	if (fa && fa->__actions)
	{
		struct fdop *op;
		__wasi_proc_spawn_fd_op_t *wop;
		__wasi_proc_spawn_fd_op_name_t op_name;

		for (op = fa->__actions; op->next; op = op->next)
		{
			++nfdops;
		}
		// If op is null, there were zero ops. But if it's not, we counted one less
		// due to the op->next condition; compensate now.
		if (op) ++nfdops;
		wop = fdops = calloc(nfdops, sizeof(*fdops));

		for (; op; op = op->prev)
		{
			switch (op->cmd)
			{
			case FDOP_OPEN:
				// firebox#DNG/#J04 — carry the addopen mode so the runtime
				// creates at mode & ~umask in one step (plain OPEN creates
				// at 0666 & ~umask: the mode argument was dropped).
				op_name = __WASIX_PROC_SPAWN_FD_OP_NAME_OPEN_MODE;
				break;
			case FDOP_CLOSE:
				op_name = __WASI_PROC_SPAWN_FD_OP_NAME_CLOSE;
				break;
			case FDOP_DUP2:
				op_name = __WASI_PROC_SPAWN_FD_OP_NAME_DUP2;
				break;
			case FDOP_CHDIR:
				op_name = __WASI_PROC_SPAWN_FD_OP_NAME_CHDIR;
				break;
			case FDOP_FCHDIR:
				op_name = __WASI_PROC_SPAWN_FD_OP_NAME_FCHDIR;
				break;
			default:
				free(fdops);
				return EINVAL;
			}

			__wasi_lookupflags_t lookup_flags = 0;
			if ((op->oflag & O_NOFOLLOW) == 0)
				lookup_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

			// Open file with appropriate rights.
			// firebox#87F — the second wire encoder. Was a shift and a
			// mask, which only worked while the guest O_* names WERE the
			// wire bits. Routed through the one translator so a renumber
			// cannot leave this site behind silently appending.
			__wasi_fdflags_t fs_flags = __wasilibc_fdflags_to_wasi(op->oflag);
			__wasi_oflags_t oflags = __wasilibc_oflags_to_wasi(op->oflag);
			__wasi_fdflagsext_t fd_flags = __wasilibc_fdflagsext_to_wasi(op->oflag);

			__wasi_rights_t rights =
				~(__WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_READ |
				  __WASI_RIGHTS_FD_WRITE | __WASI_RIGHTS_FD_ALLOCATE |
				  __WASI_RIGHTS_FD_READDIR | __WASI_RIGHTS_FD_FILESTAT_SET_SIZE);
			// firebox#87F — access mode by VALUE, not by bit. Same reasoning as
			// libc-bottom-half/cloudlibc/src/libc/fcntl/openat.c: this block is
			// the second wire encoder for O_*, and `(oflag & O_RDONLY) != 0` is
			// only ever true because our O_RDONLY is 0x04000000. Under Linux
			// numbering O_RDONLY is 0 and the test dies silently, granting an
			// O_RDONLY spawn-open no FD_READ — a fail-open on the rights side to
			// match the O_APPEND fail-open on the flags side. Note the `default`
			// here is a bare `break`, NOT the EINVAL openat() returns; that
			// difference is pre-existing and deliberately preserved.
			switch (op->oflag & O_ACCMODE)
			{
			case O_RDONLY:
				rights |= __WASI_RIGHTS_FD_READ | __WASI_RIGHTS_FD_READDIR;
				break;
			case O_WRONLY:
				rights |= __WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_WRITE |
						  __WASI_RIGHTS_FD_ALLOCATE |
						  __WASI_RIGHTS_FD_FILESTAT_SET_SIZE;
				break;
			case O_RDWR:
				rights |= __WASI_RIGHTS_FD_READ | __WASI_RIGHTS_FD_READDIR |
						  __WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_WRITE |
						  __WASI_RIGHTS_FD_ALLOCATE |
						  __WASI_RIGHTS_FD_FILESTAT_SET_SIZE;
				break;
			default:
				break;
			}

			uint8_t *path =
				op->cmd == FDOP_OPEN || op->cmd == FDOP_CHDIR ? (uint8_t *)op->path : NULL;

			*wop = (__wasi_proc_spawn_fd_op_t){
				.cmd = op_name,
				.fd = op->fd,
				.src_fd = op->srcfd,
				.path_len = path ? strlen(op->path) : 0,
				.path = path,

				.oflags = oflags,
				.fdflags = fs_flags,
				.fdflagsext = fd_flags,
				.dirflags = lookup_flags,

				// Just give it every permission, since
				.fs_rights_base = rights,
				.fs_rights_inheriting = rights,
			};
			// firebox#DNG/#J04 — the create mode rides the struct's tail
			// padding, read by the runtime only for OPEN_MODE.
			if (op_name == __WASIX_PROC_SPAWN_FD_OP_NAME_OPEN_MODE) {
				uint32_t mode = (uint32_t)op->mode;
				memcpy((char *)wop + FBX_SPAWN_FD_OP_MODE_OFFSET, &mode, sizeof mode);
			}
			wop++;
		}
	}

	int nsignals = 0;
	// There can be at most twice as many entries as there are signals, since
	// we look through current signal handlers for SIG_IGN first and then
	// add entried from attr->__def on top of those. This is safe to do even
	// in the presence of duplicate signals, since the entries are processed
	// in order and later entries take precedence over earlier ones.
	__wasi_signal_disposition_t *signals = calloc(_NSIG * 2, sizeof(*signals));

	for (int sig = 1; sig < _NSIG; sig++)
	{
		struct sigaction old;
		if (sigaction(sig, NULL, &old) == 0)
		{
			if (old.sa_handler == SIG_IGN)
			{
				signals[nsignals++] = (__wasi_signal_disposition_t){
					.sig = sig,
					.disp = __WASI_DISPOSITION_IGNORE,
				};
			}
		}
	}

	if (attr->__flags & POSIX_SPAWN_SETSIGDEF)
	{
		for (int sig = 1; sig < _NSIG; sig++)
		{
			/* firebox#9AV — never emit SIGKILL/SIGSTOP into the inherited
			 * disposition array. Their disposition is fixed by POSIX and can
			 * never be anything but the default, so the entry carries no
			 * information; it only hands the child a request it cannot honor.
			 * posix_spawnattr_setsigdefault(sigfillset(...)) is the normal
			 * idiom (libuv does exactly this), so the malformed entry is the
			 * common case, not the odd one. Stripping it here is what tini was
			 * hand-patched to do per-guest (firebox#3T8); doing it in libc means
			 * no parent needs that workaround.
			 *
			 * This is hygiene at the source, NOT the load-bearing fix — the
			 * child-side guarantee lives in __wasi_init_signals/__wasm_sigaction
			 * so that a parent we do not own cannot kill its child either. */
			if (sig == SIGKILL || sig == SIGSTOP)
				continue;
			if (sigismember(&attr->__def, sig))
			{
				signals[nsignals++] = (__wasi_signal_disposition_t){
					.sig = sig,
					.disp = __WASI_DISPOSITION_DEFAULT,
				};
			}
		}
	}

	/* firebox#BZ5 — POSIX_SPAWN_SETPGROUP.
	 *
	 * This path read `attr->__flags` exactly once — for POSIX_SPAWN_SETSIGDEF
	 * (firebox#1QR added the second read, for SETSIGMASK) — and dropped
	 * SETPGROUP outright while returning 0. The caller asked for the child to
	 * be in a named process group before it could run, `posix_spawn` said yes,
	 * and the child landed in the parent's group instead. That is the false
	 * success invariant 0 forbids, and it is not cosmetic: `pgid` is what the
	 * host's `proc_signal` group scan delivers on, so a job-control shell's
	 * `kill(-pgid, SIGINT)` — and terminal signal delivery to the foreground
	 * group — targeted the wrong set of processes.
	 *
	 * ⛔ WHY THE PARENT CANNOT SIMPLY CALL `setpgid(child, pgrp)` AFTERWARDS.
	 * `proc_spawn2` ends at `dispatch_exec_task`, which hands the child to the
	 * task manager and returns WITHOUT waiting for it to begin — so the child
	 * is already runnable by the time this function regains control. A
	 * parent-side `setpgid` after the syscall therefore re-opens exactly the
	 * window SETPGROUP exists to close; POSIX has the child do it between
	 * `fork` and `exec` for that reason, and the parent's half of the classic
	 * shell double-`setpgid` is a race-narrower, never a substitute.
	 *
	 * ⛔ AND IT CANNOT RIDE firebox#1QR's TRANSPORT. That one works because the
	 * mask travels as the parent's OWN live `blocked_sigmask`, which the host
	 * reads out of guest memory. There is no equivalent for pgid: `__pgrp == 0`
	 * means "make the CHILD a group leader", which the parent's own pgid cannot
	 * represent, and temporarily moving the parent between groups would open a
	 * worse window than the one it closes.
	 *
	 * So the request is STAGED on the calling thread host-side, immediately
	 * before the one syscall that consumes it. Per-THREAD, not per-process: a
	 * single `posix_spawn` runs entirely on one guest thread, so two threads
	 * spawning concurrently cannot see each other's staged value. `proc_spawn2`
	 * takes the slot unconditionally at its top and stamps the resolved pgid on
	 * the child's `WasiProcess` BEFORE dispatch — the child cannot execute an
	 * instruction outside its requested group.
	 *
	 * ⛔ AN UNHONOURABLE REQUEST MUST NOT RETURN 0. `posix_spawn` reports its
	 * failure through the RETURN VALUE, not `errno`. A negative `__pgrp` is
	 * EINVAL (POSIX lists it for `setpgid`, and the host applies the same
	 * argument rules); anything else that keeps the host from recording the
	 * request — notably a runtime predating this staging call — is ENOSYS, the
	 * POSIX answer for an unsupported optional feature. Reporting either beats
	 * the pre-fix 0, which told the caller a guarantee held when it did not. */
	/* firebox#7RS — POSIX_SPAWN_SETSID, staged for the same reason and on the
	 * same terms as SETPGROUP below: the child must lead its new session before
	 * it can run, and `proc_spawn2` returns with the child already dispatched.
	 * The host applies SETSID before SETPGROUP, in musl's child order, so the
	 * pair is EPERM there as on Linux (a session leader cannot change group).
	 *
	 * ⛔ STAGED FIRST, AND ONLY AFTER THE ONE SETPGROUP FAILURE THE HOST COULD
	 * REPORT. A staged request is consumed only by `proc_spawn2`, so one that
	 * is staged and then followed by an early return here would be applied to
	 * some LATER, unrelated child. The negative-pgid EINVAL is therefore
	 * decided before anything is staged. That is the same rule the host
	 * applies, and Linux's child would hit it in `setpgid` too. After that, the
	 * SETPGROUP staging below cannot fail on a runtime that has this import,
	 * because the import is newer than that one. A runtime without this import
	 * is ENOSYS, with nothing staged. */
	if ((attr->__flags & POSIX_SPAWN_SETPGROUP) && attr->__pgrp < 0)
	{
		free(signals);
		if (fdops)
			free(fdops);
		return EINVAL;
	}
	if (attr->__flags & POSIX_SPAWN_SETSID)
	{
		if (__wasix_proc_stage_spawn_setsid() != __WASI_ERRNO_SUCCESS)
		{
			free(signals);
			if (fdops)
				free(fdops);
			return ENOSYS;
		}
	}

	if (attr->__flags & POSIX_SPAWN_SETPGROUP)
	{
		__wasi_errno_t pgerr =
			__wasix_proc_stage_spawn_pgid((uint32_t)attr->__pgrp);
		if (pgerr != __WASI_ERRNO_SUCCESS)
		{
			free(signals);
			if (fdops)
				free(fdops);
			return pgerr == __WASI_ERRNO_INVAL ? EINVAL : ENOSYS;
		}
	}

	size_t combined_argv_len = 0, combined_env_len = 0;
	char *combined_argv = __wasilibc_exec_combine_strings_len(argv, &combined_argv_len);
	char *combined_env = __wasilibc_exec_combine_strings_len(envp, &combined_env_len);

	/* firebox#1QR — POSIX_SPAWN_SETSIGMASK.
	 *
	 * The child's blocked mask travels to the host as the CALLING THREAD'S LIVE
	 * `blocked_sigmask`, which the runtime reads out of guest memory at the top
	 * of `proc_spawn2` through the layout-independent `__fbx_main_pthread` /
	 * `__fbx_blocked_off` anchors (firebox#KKR) and records on the child, where
	 * `__wasi_init_signals` pulls it back. That read alone implements POSIX mask
	 * INHERITANCE — the no-flag case, and the larger population, since the child
	 * is a fresh instance whose mask would otherwise start all-zero.
	 *
	 * `SETSIGMASK` therefore only has to make the value the host reads be
	 * `attr->__mask` for the duration of the one host call. There is no room in
	 * the syscall for it: `proc_spawn2`'s witx signature has seven params and
	 * none is a mask, widening it is an instantiation-time link error for every
	 * prebuilt `.webc`, and the disposition array is a two-variant enum with no
	 * `blocked` state (riding it would conflate *blocked* with *ignored*).
	 *
	 * ⛔ WHY THE FIELD IS WRITTEN DIRECTLY AND NOT VIA `pthread_sigmask`.
	 * `pthread_sigmask(SIG_SETMASK, …)` calls `__wasm_drain_pending_sigs()`
	 * (pthread_sigmask.c:115) because a SETMASK may lift blocks. Using it here
	 * would DELIVER, inside `posix_spawn`, every signal the parent had
	 * deliberately blocked that `attr->__mask` does not — and the common idiom is
	 * exactly that shape: a parent with SIGCHLD blocked spawning with
	 * `sigemptyset` to hand the child a guaranteed clean mask (libuv, cmake).
	 * Running the parent's SIGCHLD handler mid-spawn is a behaviour Linux does
	 * not have. Writing the words directly and restoring them immediately keeps
	 * every guest-side delivery point out of the window: the only call in
	 * between is the host one.
	 *
	 * ⚠️ RESIDUAL, and it is filed rather than hidden: the HOST can still read
	 * the temporarily-swapped mask during that single call (`proc_spawn2` runs
	 * `do_pending_operations` at its top, and the #KKR no-handler routing may run
	 * concurrently), so a signal that is pending AND in `parent_mask &
	 * ~attr->__mask` could be routed as deliverable. Closing that needs a
	 * parent-side staging import so the parent never has to hold a foreign mask
	 * at all; see the follow-up task. The window is one host call wide and
	 * strictly smaller than the pre-fix behaviour, which was to drop the flag
	 * entirely and return 0.
	 *
	 * SIGKILL/SIGSTOP are masked off on the way in, the same invariant
	 * `pthread_sigmask` enforces (firebox#H2F): `blocked_sigmask` must never
	 * carry them, whoever writes it. */
	const size_t __fbx_nwords = _NSIG / (8 * sizeof(long));
	struct pthread *__fbx_self = __pthread_self();
	unsigned long __fbx_saved_mask[_NSIG / (8 * sizeof(long))];
	int __fbx_mask_swapped = 0;
	if (__fbx_self && (attr->__flags & POSIX_SPAWN_SETSIGMASK)) {
		const size_t kw = (size_t)(SIGKILL - 1) / (8 * sizeof(long));
		const unsigned long kb = 1UL << ((SIGKILL - 1) % (8 * sizeof(long)));
		const size_t sw = (size_t)(SIGSTOP - 1) / (8 * sizeof(long));
		const unsigned long sb = 1UL << ((SIGSTOP - 1) % (8 * sizeof(long)));
		for (size_t i = 0; i < __fbx_nwords; i++) {
			unsigned long nw = attr->__mask.__bits[i];
			if (i == kw) nw &= ~kb;
			if (i == sw) nw &= ~sb;
			__fbx_saved_mask[i] = __fbx_self->blocked_sigmask[i];
			__fbx_self->blocked_sigmask[i] = nw;
		}
		__fbx_mask_swapped = 1;
	}

	__wasi_pid_t ret_pid;
	/* firebox#39G: pass the packed lengths rather than letting the wrapper
	 * rediscover them with a double-NUL scan, which truncates at the first
	 * empty argument. */
	int err = __wasilibc_proc_spawn2_n(
		path, combined_argv, combined_argv_len, combined_env, combined_env_len,
		fdops, nfdops, signals, nsignals,
		use_path ? __WASI_BOOL_TRUE : __WASI_BOOL_FALSE, getenv("PATH"), &ret_pid);

	if (__fbx_mask_swapped) {
		for (size_t i = 0; i < __fbx_nwords; i++)
			__fbx_self->blocked_sigmask[i] = __fbx_saved_mask[i];
	}

	free(combined_argv);
	free(combined_env);
	free(signals);
	if (fdops)
		free(fdops);

	if (err == 0 && res) {
		*res = ret_pid;
	}

	/* firebox#87F: posix_spawn reports through its RETURN VALUE (POSIX), and
	 * `err` came straight back from __wasilibc_proc_spawn2_n, which is declared
	 * __wasi_errno_t. Host space out, guest space in. */
	return __wasilibc_errno_from_wasi(err);
}

int posix_spawn(pid_t *restrict res, const char *restrict path,
				const posix_spawn_file_actions_t *fa,
				const posix_spawnattr_t *restrict attr,
				char *const argv[restrict], char *const envp[restrict])
{
	return __posix_spawn(res, path, fa, attr, argv, envp, 0);
}
#endif
