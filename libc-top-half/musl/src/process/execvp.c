#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <wasi/api.h>

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
/*
 * firebox#39G: the same packing, but it also hands back the TRUE payload
 * length.
 *
 * The double-NUL end marker described above cannot represent an empty argv
 * element: `foo "" bar` packs to `foo\0\0bar\0\0`, and the `\0\0` that ends
 * the empty element is byte-identical to the end-of-buffer marker. Any scan
 * of this buffer therefore stops early and silently drops every argument
 * after the first empty one — a fail-open the caller cannot see.
 *
 * On Linux an empty argv element is an ordinary string; execve carries it and
 * the child's argc is unchanged. The only way to keep that is to stop
 * deriving the length from the payload. `*out_len` is the exact number of
 * bytes of `n` NUL-terminated entries, so an empty entry is just an entry of
 * length zero and nothing is ambiguous. The trailing NUL is still written —
 * it keeps the allocation a valid C string for anything that strlen()s it —
 * but it is NOT counted, so the host sees exactly one phantom split piece
 * after the last entry's own terminator and drops precisely that one.
 */
char *__wasilibc_exec_combine_strings_len(char *const strings[], size_t *out_len);

char *__wasilibc_exec_combine_strings(char *const strings[])
{
	return __wasilibc_exec_combine_strings_len(strings, NULL);
}

char *__wasilibc_exec_combine_strings_len(char *const strings[], size_t *out_len)
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

	/* firebox#39G: the true payload length, EXCLUDING the trailing NUL above. */
	if (out_len != NULL)
		*out_len = (size_t)combined_len;

	return combined;
}

/* firebox#39G: length-carrying wrapper, defined in __wasixlibc_real.c. */
__wasi_errno_t __wasilibc_proc_exec3_n(const char *name, const char *args,
									   size_t args_len, const char *envs,
									   size_t envs_len,
									   __wasi_bool_t search_path,
									   const char *path);

int __execvpe(const char *path, char *const argv[], char *const envp[], uint8_t use_path)
{
	size_t argv_len = 0, env_len = 0;
	char *combined_argv = __wasilibc_exec_combine_strings_len(argv, &argv_len);
	char *combined_env = __wasilibc_exec_combine_strings_len(envp, &env_len);

	/* firebox#39G: pass the packed length rather than letting the wrapper
	 * rediscover it with a double-NUL scan, which truncates at the first
	 * empty argument. */
	int e = __wasilibc_proc_exec3_n(
		path, combined_argv, argv_len, combined_env, env_len,
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
