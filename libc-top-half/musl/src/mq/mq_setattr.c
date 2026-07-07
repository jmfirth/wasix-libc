#include <mqueue.h>
#include "syscall.h"
#ifndef __wasilibc_unmodified_upstream
#include "mq_impl.h" /* firebox#KS9 */
#endif

int mq_setattr(mqd_t mqd, const struct mq_attr *restrict new, struct mq_attr *restrict old)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_mq_getsetattr, mqd, new, old);
#else
	return __fbx_mq_getsetattr(mqd, new, old);
#endif
}
