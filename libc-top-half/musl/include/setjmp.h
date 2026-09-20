#ifndef	_SETJMP_H
#define	_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#ifdef __wasilibc_unmodified_upstream
#include <bits/setjmp.h>

typedef struct __jmp_buf_tag {
	__jmp_buf __jb;
	unsigned long __fl;
	unsigned long __ss[128/sizeof(long)];
} jmp_buf[1];

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
typedef jmp_buf sigjmp_buf;
int sigsetjmp (sigjmp_buf, int);
_Noreturn void siglongjmp (sigjmp_buf, int);
#endif

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
int _setjmp (jmp_buf);
_Noreturn void _longjmp (jmp_buf, int);
#endif
#elif defined(__wasm_exception_handling__)
    typedef int __jmp_buf[6];

	typedef struct __jmp_buf_tag
	{
		__jmp_buf __jb;
		unsigned long __fl;
		unsigned long __ss[128 / sizeof(long)];
	} jmp_buf[1];

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
#define __setjmp_attr __attribute__((__returns_twice__))
#else
#define __setjmp_attr
#endif

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || \
	defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
	typedef jmp_buf sigjmp_buf;
	/* These two out-of-line symbols stay DEFINED and EXPORTED on this shelf
	 * even though the macros below mean no consumer calls them, because
	 * autoconf's AC_CHECK_FUNCS(sigsetjmp) is a LINK probe: it compiles a
	 * reference with its own prototype (the macro never sees it) and asks the
	 * linker to resolve it. Remove them and every such package decides it has
	 * no sigsetjmp. See src/signal/sigsetjmp_wasix.c for why they cannot also
	 * be the working implementation here. */
	int sigsetjmp(sigjmp_buf, int) __setjmp_attr;
	_Noreturn void siglongjmp(sigjmp_buf, int);

	/* firebox#SGG: the two halves of sigsetjmp/siglongjmp that are NOT
	 * frame-sensitive -- the signal-mask save and the signal-mask restore.
	 * Defined out-of-line in src/signal/sigsetjmp_wasix.c.
	 *
	 * The frame-sensitive halves (the capture and the jump) must compile in
	 * the CALLER's translation unit, so sigsetjmp/siglongjmp are macros here,
	 * exactly like vfork() in <unistd.h> and for exactly the same reason.
	 *
	 * MEASURED 2026-09-20 (firebox#SGG, 4 arms x 5 trials against the shipped
	 * libc-g755aa6a2bec73357.so): macro'ing ONLY the capture FAILS 5/5. The
	 * caller's setjmp lowers to `__wasm_setjmp` + a `try_table` landing pad,
	 * but its `siglongjmp` stays an out-of-line `env.siglongjmp`, and in libc
	 * that runs `siglongjmp -> longjmp -> __wasilibc_longjmp ->
	 * __wasi_stack_restore`. That is the WASIX asyncify stack-snapshot
	 * mechanism, and this shelf carries no asyncify instrumentation, so it
	 * traps `unreachable`. A `try_table` landing pad is reachable only by
	 * `__wasm_longjmp` throwing `__c_longjmp`; the snapshot path cannot reach
	 * it by construction. Macro'ing BOTH halves passes 5/5.
	 *
	 * The jump half deliberately spells `longjmp`, not `__wasm_longjmp`: in
	 * the caller's frame the SJLJ pass rewrites it to `__wasm_longjmp` when
	 * that caller's setjmp was also lowered, and leaves both halves on the
	 * snapshot mechanism when it was not. Naming `__wasm_longjmp` directly
	 * (what __vfork_restore must do, because it is in LIBC's frame where the
	 * pass never runs) would throw `__c_longjmp` at a caller that may have no
	 * landing pad. The two halves degrade together this way. */
	void __firebox_sigsetjmp_record(void *, int);
	void __firebox_siglongjmp_restore(void *);

	/* The comma expression runs the mask save BEFORE the capture, so a
	 * re-entry via siglongjmp resumes at the `setjmp` and does NOT re-run the
	 * recorder -- which is what keeps the saved mask the one captured on the
	 * first pass. */
	#define sigsetjmp(buf, savesigs) \
		(__firebox_sigsetjmp_record((void *)(buf), (savesigs)), setjmp(buf))
	#define siglongjmp(buf, val) \
		(__firebox_siglongjmp_restore((void *)(buf)), longjmp(buf, val))
#endif

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
	int _setjmp(jmp_buf) __setjmp_attr;
	_Noreturn void _longjmp(jmp_buf, int);
#endif

#undef __setjmp_attr
#else
#include <wasi/api.h>
typedef __wasi_stack_snapshot_t __jmp_buf_tag;
typedef __wasi_stack_snapshot_t jmp_buf[1];
typedef jmp_buf sigjmp_buf;

/* firebox#WJJ: this fallback (non-EH, stack-snapshot) branch declared
 * sigjmp_buf but NOT sigsetjmp/siglongjmp/_setjmp/_longjmp, so a POSIX
 * program that uses them (musl libc-test functional/setjmp) failed to BUILD
 * (-Werror=implicit-function-declaration) even though the sysroot libc
 * already ships the implementations — src/signal/sigsetjmp_wasix.c does real
 * signal-mask save/restore via pthread_sigmask + a TLS slot. Mirror the other
 * two branches' declarations so consumers see the identical setjmp API
 * regardless of which branch the header selects. */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
#define __setjmp_attr __attribute__((__returns_twice__))
#else
#define __setjmp_attr
#endif

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || \
	defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
int sigsetjmp(sigjmp_buf, int) __setjmp_attr;
_Noreturn void siglongjmp(sigjmp_buf, int);
#endif

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
int _setjmp(jmp_buf) __setjmp_attr;
_Noreturn void _longjmp(jmp_buf, int);
#endif

#undef __setjmp_attr
#endif

int setjmp (jmp_buf);
_Noreturn void longjmp (jmp_buf, int);

#define setjmp setjmp

#ifdef __cplusplus
}
#endif

#endif
