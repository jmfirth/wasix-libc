#include <setjmp.h>
#include <signal.h>
#include <setjmp.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#include "pthread_impl.h"

#ifdef __wasilibc_unmodified_upstream
_Noreturn void siglongjmp(sigjmp_buf buf, int ret)
{
	longjmp(buf, ret);
}
#else
/* WASIX siglongjmp is defined in src/signal/sigsetjmp_wasix.c alongside
 * the sigsetjmp counterpart, so both can share the TLS saved-mask slot.
 * See issue #24 patch E and issue #37. */
#endif