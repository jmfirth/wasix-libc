#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/wait.h>
#endif
#include <spawn.h>
#include <errno.h>
#include "pthread_impl.h"

#ifdef __wasilibc_unmodified_upstream
extern char **__environ;
#else
#include <wasi/libc-environ.h>	/* __wasilibc_ensure_environ */
extern char **__wasilibc_environ;
pid_t waitpid(pid_t pid, int *status, int options);
#endif

int system(const char *cmd)
{
	pid_t pid;
	sigset_t old, reset;
	struct sigaction sa = { .sa_handler = SIG_IGN }, oldint, oldquit;
	int status = -1, ret;
	posix_spawnattr_t attr;

	pthread_testcancel();

	if (!cmd) return 1;

	sigaction(SIGINT, &sa, &oldint);
	sigaction(SIGQUIT, &sa, &oldquit);
	sigaddset(&sa.sa_mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sa.sa_mask, &old);

	sigemptyset(&reset);
	if (oldint.sa_handler != SIG_IGN) sigaddset(&reset, SIGINT);
	if (oldquit.sa_handler != SIG_IGN) sigaddset(&reset, SIGQUIT);
	posix_spawnattr_init(&attr);
	posix_spawnattr_setsigmask(&attr, &old);
	posix_spawnattr_setsigdefault(&attr, &reset);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETSIGMASK);
#ifdef __wasilibc_unmodified_upstream
	ret = posix_spawn(&pid, "/bin/sh", 0, &attr,
		(char *[]){"sh", "-c", (char *)cmd, 0}, __environ);
#else
	/*
	 * firebox #97D (same class as execv): pass the LIVE environ, not an
	 * empty {NULL} array. The prior `&envp` (envp==NULL) marshalled a
	 * zero-length env buffer, so /bin/sh saw only the stale parent-boot
	 * snapshot and dropped post-fork setenv() changes. posix_spawn's
	 * __posix_spawn combines envp via the same __wasilibc_exec_combine_strings
	 * path execve/__execvpe use, so an ensured __wasilibc_environ marshals
	 * correctly (matches upstream musl's __environ pass-through).
	 */
	__wasilibc_ensure_environ();
	ret = posix_spawn(&pid, "/bin/sh", 0, &attr,
		(char *[]){"sh", "-c", (char *)cmd, 0}, __wasilibc_environ);
#endif
	posix_spawnattr_destroy(&attr);

	if (!ret) while (waitpid(pid, &status, 0)<0 && errno == EINTR);
	sigaction(SIGINT, &oldint, NULL);
	sigaction(SIGQUIT, &oldquit, NULL);
	sigprocmask(SIG_SETMASK, &old, NULL);

	if (ret) errno = ret;
	return status;
}
