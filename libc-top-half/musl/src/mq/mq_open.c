#include <mqueue.h>
#include <fcntl.h>
#include <stdarg.h>
#include "syscall.h"
#ifndef __wasilibc_unmodified_upstream
#include "mq_impl.h" /* firebox#KS9 — guest named-queue registry */
#endif

mqd_t mq_open(const char *name, int flags, ...)
{
	mode_t mode = 0;
	struct mq_attr *attr = 0;
	if (*name == '/') name++;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		attr = va_arg(ap, struct mq_attr *);
		va_end(ap);
	}
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_mq_open, name, flags, mode, attr);
#else
	return __fbx_mq_open(name, flags, mode, attr);
#endif
}
