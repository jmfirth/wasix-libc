#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

extern char **__wasilibc_environ;

#ifdef __wasilibc_unmodified_upstream
int __execvpe(const char *file, char *const argv[], char *const envp[])
{
	const char *p, *z, *path = getenv("PATH");
	size_t l, k;
	int seen_eacces = 0;

	errno = ENOENT;
	if (!*file)
		return -1;

	if (strchr(file, '/'))
		return execve(file, argv, envp);

	if (!path)
		path = "/usr/local/bin:/bin:/usr/bin";
	k = strnlen(file, NAME_MAX + 1);
	if (k > NAME_MAX)
	{
		errno = ENAMETOOLONG;
		return -1;
	}
	l = strnlen(path, PATH_MAX - 1) + 1;

	for (p = path;; p = z)
	{
		char b[l + k + 1];
		z = __strchrnul(p, ':');
		if (z - p >= l)
		{
			if (!*z++)
				break;
			continue;
		}
		memcpy(b, p, z - p);
		b[z - p] = '/';
		memcpy(b + (z - p) + (z > p), file, k + 1);
		execve(b, argv, envp);
		switch (errno)
		{
		case EACCES:
			seen_eacces = 1;
		case ENOENT:
		case ENOTDIR:
			break;
		default:
			return -1;
		}
		if (!*z++)
			break;
	}
	if (seen_eacces)
		errno = EACCES;
	return -1;
}
#else
/*
 * firebox #54: emit NUL between entries instead of '\n', and end the
 * buffer with a DOUBLE NUL so wrappers can compute the real length.
 *
 * The old '\n' separator broke any argv element containing an embedded
 * newline (multi-line `python3 -c`, bash heredocs, cmake probe
 * scripts, etc.). NUL is illegal inside a C string, so it is a safe
 * separator for the combined argv/envp buffer the WASIX host splits
 * back apart.
 *
 * The firebox wasmer fork accepts both separators for backward compat
 * (NUL preferred, '\n' fallback).
 *
 * The buffer terminates with two zero bytes. The wrappers in
 * __wasixlibc_real.c (proc_exec3, proc_spawn2, proc_spawn) use that
 * double-NUL as their end-of-buffer sentinel for computing `args_len`
 * / `envs_len`, because strlen() stops at the first intra-buffer NUL
 * and would pass a truncated length to the host.
 */
char *__wasilibc_exec_combine_strings(char *const strings[])
{
	int combined_len = 0;
	for (char **ptr = (char **)strings; *ptr != NULL; ptr++)
	{
		combined_len += strlen(*ptr) + 1;
	}

	char *combined = malloc((combined_len + 1));
	char *combined_p = combined;
	for (char **ptr = (char **)strings; *ptr != NULL; ptr++)
	{
		memcpy(combined_p, *ptr, strlen(*ptr));
		combined_p += strlen(*ptr);
		*combined_p = '\0';
		combined_p++;
	}
	/* Final NUL. Combined with the separator NUL of the last entry this
	 * produces the `...\0\0` end-of-buffer marker. For an empty argv
	 * (zero entries), combined_p still points at combined[0], so the
	 * whole buffer is a single `\0` — wrappers treat that as length 0. */
	*combined_p = 0;

	return combined;
}

int __execvpe(const char *path, char *const argv[], char *const envp[], uint8_t use_path)
{
	char *combined_argv = __wasilibc_exec_combine_strings(argv);
	char *combined_env = __wasilibc_exec_combine_strings(envp);

	int e = __wasi_proc_exec3(
		path, combined_argv, combined_env,
		use_path ? __WASI_BOOL_TRUE : __WASI_BOOL_FALSE, getenv("PATH"));
#ifdef __wasm_exception_handling__
	extern _Noreturn void __vfork_restore();
	if (e == 0) {
		__vfork_restore();
	}
#endif

	free(combined_argv);
	free(combined_env);

	// A return from proc_exec automatically means it failed
	errno = e;
	return -1;
}
#endif

int __execvp(const char *file, char *const argv[])
{
#ifndef __wasilibc_unmodified_upstream
	__wasilibc_ensure_environ();
#endif
	return __execvpe(file, argv, __wasilibc_environ, 1);
}

weak_alias(__execvp, execvp);

/* execvpe(file, argv, envp) is the GNU extension: PATH search like execvp, but
 * with a caller-supplied environment. It CANNOT be an alias of __execvp, which
 * takes two parameters and supplies __wasilibc_environ itself -- that alias gave
 * the symbol a two-parameter type and was wrong from the day it was written. It
 * stayed invisible only because nothing DECLARED execvpe, so no translation unit
 * ever compared the alias's type against a prototype; firebox#H18/#DEK adding
 * the declaration to <unistd.h> is what turned it into a build error. Give it a
 * real definition instead, forwarding to the live four-parameter __execvpe with
 * use_path=1. (firebox #P47)
 */
int __execvpe_env(const char *file, char *const argv[], char *const envp[])
{
	return __execvpe(file, argv, envp, 1);
}

weak_alias(__execvpe_env, execvpe);
