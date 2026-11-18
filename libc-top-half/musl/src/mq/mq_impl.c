#define _GNU_SOURCE
#ifndef __wasilibc_unmodified_upstream

#include "mq_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wasi/libc.h>

#include "pthread_impl.h"

#ifdef __wasm64__
#define MQ_MODULE "wasix_64v1"
#else
#define MQ_MODULE "wasix_32v1"
#endif
#define MQ_IMPORT(name) __attribute__((__import_module__(MQ_MODULE), __import_name__(name)))

extern int32_t mq_host_open(const char *, size_t, uint32_t, uint32_t,
                            int64_t, int64_t, int32_t *) MQ_IMPORT("mq_open");
extern int32_t mq_host_unlink(const char *, size_t) MQ_IMPORT("mq_unlink");
extern int32_t mq_host_attr(int32_t, int64_t, int64_t *) MQ_IMPORT("mq_getsetattr");
extern int32_t mq_host_send(int32_t, const char *, size_t, uint32_t,
                            const int64_t *) MQ_IMPORT("mq_timedsend");
extern int32_t mq_host_receive(int32_t, char *, size_t, uint32_t *,
                               const int64_t *, uint32_t *) MQ_IMPORT("mq_timedreceive");
extern int32_t mq_host_notify(int32_t, int32_t, int32_t, uint64_t) MQ_IMPORT("mq_notify");

static int mq_result(int32_t result)
{
	if (!result) return 0;
	errno = __wasilibc_errno_from_wasi(result);
	return -1;
}

mqd_t __fbx_mq_open(const char *name, int flags, mode_t mode, struct mq_attr *attr)
{
	int32_t fd;
	if (!name) {
		errno = EFAULT;
		return -1;
	}
	if (*name == '/') name++;
	if (!(flags & O_CREAT)) attr = 0;
	if (mq_result(mq_host_open(name, strnlen(name, NAME_MAX + 1), flags, mode,
	                          attr ? attr->mq_maxmsg : 10,
	                          attr ? attr->mq_msgsize : 8192, &fd)))
		return -1;
	return fd;
}

int __fbx_mq_unlink(const char *name)
{
	if (!name) {
		errno = EFAULT;
		return -1;
	}
	if (*name == '/') name++;
	return mq_result(mq_host_unlink(name, strnlen(name, NAME_MAX + 1)));
}

int __fbx_mq_getsetattr(mqd_t mqd, const struct mq_attr *neu, struct mq_attr *old)
{
	int64_t slots[4];
	if (neu && (neu->mq_flags & ~O_NONBLOCK)) {
		errno = EINVAL;
		return -1;
	}
	if (mq_result(mq_host_attr(mqd, neu ? neu->mq_flags : -1, old ? slots : 0)))
		return -1;
	if (old) {
		memset(old, 0, sizeof *old);
		old->mq_flags = slots[0];
		old->mq_maxmsg = slots[1];
		old->mq_msgsize = slots[2];
		old->mq_curmsgs = slots[3];
	}
	return 0;
}

int __fbx_mq_timedsend(mqd_t mqd, const char *msg, size_t len, unsigned prio,
                      const struct timespec *at)
{
	int64_t deadline[2];
	if (at) {
		deadline[0] = at->tv_sec;
		deadline[1] = at->tv_nsec;
	}
	for (;;) {
		pthread_testcancel();
		int r = mq_result(mq_host_send(mqd, msg, len, prio, at ? deadline : 0));
		if (!r || errno != ECANCELED) return r;
		/* The host interrupted the park for SIGCANCEL. If cancellation
		 * is disabled, re-enter with the original absolute deadline. */
	}
}

ssize_t __fbx_mq_timedreceive(mqd_t mqd, char *msg, size_t len, unsigned *prio,
                             const struct timespec *at)
{
	int64_t deadline[2];
	uint32_t written;
	if (at) {
		deadline[0] = at->tv_sec;
		deadline[1] = at->tv_nsec;
	}
	for (;;) {
		pthread_testcancel();
		int r = mq_result(mq_host_receive(mqd, msg, len, prio,
		                                at ? deadline : 0, &written));
		if (!r) return written;
		if (errno != ECANCELED) return -1;
	}
}

struct mq_callback {
	int fd;
	void (*function)(union sigval);
	union sigval value;
};

static void *run_callback(void *ptr)
{
	struct mq_callback callback = *(struct mq_callback *)ptr;
	free(ptr);
	char byte;
	ssize_t n;
	do n = read(callback.fd, &byte, 1); while (n < 0 && errno == EINTR);
	close(callback.fd);
	if (n == 1 && byte == 1) callback.function(callback.value);
	return 0;
}

int __fbx_mq_notify(mqd_t mqd, const struct sigevent *sev)
{
	if (!sev) return mq_result(mq_host_notify(mqd, -1, 0, 0));
	if (sev->sigev_notify != SIGEV_THREAD) {
		uint64_t value = 0;
		int signo = 0;
		if (sev->sigev_notify == SIGEV_SIGNAL) {
			memcpy(&value, &sev->sigev_value, sizeof sev->sigev_value);
			signo = sev->sigev_signo;
		}
		return mq_result(mq_host_notify(mqd, sev->sigev_notify, signo, value));
	}

	if (mq_result(mq_host_attr(mqd, -1, 0))) return -1;
	if (!sev->sigev_notify_function) {
		errno = EINVAL;
		return -1;
	}
	struct mq_callback *callback = malloc(sizeof *callback);
	if (!callback) return -1;
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) < 0) {
		free(callback);
		return -1;
	}
	*callback = (struct mq_callback){fds[0], sev->sigev_notify_function, sev->sigev_value};
	pthread_attr_t attr;
	if (sev->sigev_notify_attributes) attr = *sev->sigev_notify_attributes;
	else pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_t thread;
	int error = __pthread_create(&thread, &attr, run_callback, callback);
	pthread_attr_destroy(&attr);
	if (error) {
		close(fds[0]);
		close(fds[1]);
		free(callback);
		errno = error == ENOSYS ? ENOSYS : EAGAIN;
		return -1;
	}
	/* The host holds the only writer after close. On registration failure
	 * there is no host writer, so the worker exits without a callback. */
	int32_t result = mq_host_notify(mqd, SIGEV_THREAD, fds[1], 0);
	close(fds[1]);
	return mq_result(result);
}
#endif
