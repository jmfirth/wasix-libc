#include <unistd.h>
#include <errno.h>
#include "libc.h"
#include "lock.h"
#include "pthread_impl.h"
#include "fork_impl.h"

static volatile int *const dummy_lockptr = 0;

#ifdef __wasilibc_unmodified_upstream
weak_alias(dummy_lockptr, __at_quick_exit_lockptr);
weak_alias(dummy_lockptr, __atexit_lockptr);
weak_alias(dummy_lockptr, __locale_lockptr);
weak_alias(dummy_lockptr, __random_lockptr);
weak_alias(dummy_lockptr, __sem_open_lockptr);
weak_alias(dummy_lockptr, __stdio_ofl_lockptr);
weak_alias(dummy_lockptr, __syslog_lockptr);
weak_alias(dummy_lockptr, __timezone_lockptr);

weak_alias(dummy_lockptr, __vmlock_lockptr);
#else
extern volatile int *const __gettext_lockptr;
weak_alias(dummy_lockptr, __dlerror_lockptr);
weak_alias(dummy_lockptr, __bump_lockptr);
#endif

static volatile int *const *const atfork_locks[] = {
	&__at_quick_exit_lockptr,
	&__atexit_lockptr,
	&__dlerror_lockptr,
	&__gettext_lockptr,
	&__locale_lockptr,
	&__random_lockptr,
	&__sem_open_lockptr,
	&__stdio_ofl_lockptr,
	&__syslog_lockptr,
	&__timezone_lockptr,
	&__bump_lockptr,
};

static void dummy(int x) { }
#ifdef __wasilibc_unmodified_upstream
weak_alias(dummy, __fork_handler);
#endif
weak_alias(dummy, __malloc_atfork);
weak_alias(dummy, __ldso_atfork);

#ifdef __wasilibc_unmodified_upstream
static void dummy_0(void) { }
weak_alias(dummy_0, __tl_lock);
weak_alias(dummy_0, __tl_unlock);
#endif

/* firebox#CBP — fork() is DEFINED in EH builds too.
 *
 * Upstream 0008d18 ("Disallow fork/vfork in EH configuration, since it's not
 * supported") wrapped this whole file — and _Fork.c — in
 * `!defined(__wasm_exception_handling__)`. Every EH variant compiles with
 * -fwasm-exceptions (Makefile-eh) or gets it via EXTRA_CFLAGS
 * (scripts/wasix-libc/build.sh --ehpic), and clang predefines
 * __wasm_exception_handling__ under that flag, so BOTH TUs compiled to EMPTY
 * objects and `fork` / `_fork_internal` / `_Fork` were absent from every EH
 * libc.a and from the shipped shared libc.so.
 *
 * That is an Invariant-0 hole, not a config choice. Three measured facts:
 *   1. `daemon()` (legacy/daemon.c) and `wordexp()` (misc/wordexp.c) CALL
 *      fork(). The libc group links --whole-archive, so both members are pulled
 *      in unconditionally and every wasixcc guest without its own fork() shim
 *      FAILED TO LINK (cpython died at configure: "C compiler cannot create
 *      executables").
 *   2. The shared libc.so imported `env.fork` — the moat's flagship syscall
 *      delegated back to whatever thin main happened to define it.
 *   3. firebox#FD6 already un-gated the PROTOTYPE in unistd.h on the claim that
 *      the definition shipped in every EH libc. It did not. This closes that
 *      half.
 *
 * Nothing in fork()/_Fork() is EH-sensitive: the guest side is plain C that
 * reaches the host through the `__wasi_proc_fork` import — no asyncify
 * intrinsic, no setjmp/longjmp, no unwind interaction. Whether the HOST can
 * service the fork is a runtime question the host answers at the syscall
 * boundary (wasmer returns Errno::Notsup for a dynamically-linked module or one
 * with no exported __stack_pointer, and _Fork propagates it as -1/errno — the
 * honest POSIX failure). Deleting the SYMBOL at compile time converts that
 * recoverable runtime absence into an unlinkable program, which is strictly
 * less faithful and is what broke python.
 *
 * vfork KEEPS its EH branch (vfork.c) — that one genuinely differs under EH
 * (setjmp/longjmp + proc_fork_env), and so does __clone (pthread_impl.h). */
pid_t fork(void)
{
	return _fork_internal(1);
}

pid_t _fork_internal(int copy_mem)
{
	sigset_t set;
	__fork_handler(-1);
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
	int need_locks = libc.need_locks > 0;
	if (need_locks) {
		__ldso_atfork(-1);
		__inhibit_ptc();
		for (int i=0; i<sizeof atfork_locks/sizeof *atfork_locks; i++)
			if (*atfork_locks[i]) LOCK(*atfork_locks[i]);
		__malloc_atfork(-1);
		__tl_lock();
	}
	pthread_t self=__pthread_self(), next=self->next;
	pid_t ret = _Fork(copy_mem);
	int errno_save = errno;
	if (need_locks) {
		if (!ret) {
			for (pthread_t td=next; td!=self; td=td->next)
				td->tid = -1;
			if (__vmlock_lockptr) {
				__vmlock_lockptr[0] = 0;
				__vmlock_lockptr[1] = 0;
			}
		}
		__tl_unlock();
		__malloc_atfork(!ret);
		for (int i=0; i<sizeof atfork_locks/sizeof *atfork_locks; i++)
			if (*atfork_locks[i])
				if (ret) UNLOCK(*atfork_locks[i]);
				else **atfork_locks[i] = 0;
		__release_ptc();
		__ldso_atfork(!ret);
	}
#ifdef __wasilibc_unmodified_upstream
	__restore_sigs(&set);
#endif
	__fork_handler(!ret);
	if (ret<0) errno = errno_save;
	return ret;
}