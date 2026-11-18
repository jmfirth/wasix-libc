#include <mqueue.h>
#include "syscall.h"
#ifndef __wasilibc_unmodified_upstream
#include "mq_impl.h"
#endif

int mq_close(mqd_t mqd)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_close, mqd);
#else
	return __fbx_mq_close(mqd);
#endif
}
