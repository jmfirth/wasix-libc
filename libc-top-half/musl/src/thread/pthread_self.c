/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
#include "pthread_impl.h"
#include <threads.h>

#if !defined(__wasilibc_unmodified_upstream) && defined(__wasm__) &&           \
    defined(_REENTRANT)
__FBX_THREAD_LOCAL struct pthread *__wasilibc_pthread_self;
#endif

static pthread_t __pthread_self_internal()
{
	return __pthread_self();
}

weak_alias(__pthread_self_internal, pthread_self);
weak_alias(__pthread_self_internal, thrd_current);
