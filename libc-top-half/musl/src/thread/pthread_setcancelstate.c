#include "pthread_impl.h"

int __pthread_setcancelstate(int new, int *old)
{
/* firebox#5RE — `-pthread` (the WASIX posix-threads build flag) makes the
 * clang driver define `_REENTRANT=1`, so this body IS compiled and
 * `self->canceldisable` IS tracked. The setcancelstate/1-1 conformance
 * FAILURE was never a state-tracking gap: it was that the cancel was never
 * *delivered* (the dropped runtime enqueue, firebox#5RE wasmer arm) and the
 * cancellation point (`__testcancel`) was a no-op dummy. Left unchanged;
 * documented here so a future reader doesn't mistake this gate for the bug. */
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
	if (new > 2U) return EINVAL;
	struct pthread *self = __pthread_self();
	if (old) *old = self->canceldisable;
	self->canceldisable = new;
#endif
	return 0;
}

weak_alias(__pthread_setcancelstate, pthread_setcancelstate);
