#include <mqueue.h>
#include <errno.h>
#include "syscall.h"
#ifndef __wasilibc_unmodified_upstream
#include "mq_impl.h" /* firebox#KS9 */
#endif

int mq_unlink(const char *name)
{
#ifdef __wasilibc_unmodified_upstream
	int ret;
	if (*name == '/') name++;
	ret = __syscall(SYS_mq_unlink, name);
	if (ret < 0) {
		if (ret == -EPERM) ret = -EACCES;
		errno = -ret;
		return -1;
	}
	return ret;
#else
	if (*name == '/') name++;
	return __fbx_mq_unlink(name);
#endif
}
