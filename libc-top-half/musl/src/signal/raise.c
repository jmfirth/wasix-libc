#include <signal.h>
#include <stdint.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif
#include "pthread_impl.h"

#ifndef __wasilibc_unmodified_upstream
/* firebox#526 — breadcrumb stamp. raise(SIGABRT) is the canonical
 * predecessor of abort()'s SIGABRT delivery path; if the wedge actor
 * is calling raise() directly (instead of abort()), the breadcrumb
 * pinpoints raise() rather than abort()'s internal raise() call.
 * See libc-top-half/musl/src/exit/abort.c for the full mechanism. */
extern void firebox_526_stamp(const char *crumb);
#endif

int raise(int sig)
{
	sigset_t set;
#ifndef __wasilibc_unmodified_upstream
	firebox_526_stamp("raise");
#endif
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
#ifdef __wasilibc_unmodified_upstream
	int ret = syscall(SYS_tkill, __pthread_self()->tid, sig);
#else
	int ret = __wasi_thread_signal(__pthread_self()->tid, (__wasi_signal_t)sig);
#endif
#ifdef __wasilibc_unmodified_upstream
	__restore_sigs(&set);
#endif
	return ret;
}