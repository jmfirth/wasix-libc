#include <unistd.h>

#ifdef __wasilibc_unmodified_upstream
extern char **__environ;
#else
#include <wasi/libc-environ.h>	/* __wasilibc_ensure_environ */
extern char **__wasilibc_environ;
#endif

int execv(const char *path, char *const argv[])
{
#ifdef __wasilibc_unmodified_upstream
	return execve(path, argv, __environ);
#else
	/*
	 * firebox #97D: execv MUST carry the process environment to the
	 * exec'd image, exactly as upstream musl's `execve(path, argv,
	 * __environ)`. The prior firebox body combined only argv and passed
	 * NULL for the env buffer to __wasi_proc_exec3 — so the host mapped
	 * NULL -> envs=None -> _prepare_wasi skipped the env update, and the
	 * child inherited the STALE parent-boot snapshot, dropping every
	 * post-fork setenv() (e.g. wordexp's fork+execl /bin/sh saw $FOO
	 * empty). execle worked only because it routed an EXPLICIT envp
	 * through execve -> __execvpe, which marshals combined_env.
	 *
	 * Delegate to execve with the live environ: __execvpe then combines
	 * __wasilibc_environ into combined_env and carries the __vfork_restore
	 * asyncify-unwind guard — the single correct exec-marshal path. This
	 * also deletes the duplicated arg-combine loop (invariant 1: one home
	 * for the packing protocol, in __wasilibc_exec_combine_strings).
	 */
	__wasilibc_ensure_environ();
	return execve(path, argv, __wasilibc_environ);
#endif
}
