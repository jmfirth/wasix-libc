#include <mqueue.h>
#include "syscall.h"
#ifndef __wasilibc_unmodified_upstream
#include <unistd.h>
#endif

int mq_close(mqd_t mqd)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_close, mqd);
#else
	return close(mqd);
#endif
}
