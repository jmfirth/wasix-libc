#ifndef FBX_MQ_IMPL_H
#define FBX_MQ_IMPL_H

#include <mqueue.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

/* firebox#2FX: choose the backend from the host capability. The KS9
 * registry preserves process-local queues on hosts without that capability. */
#define FBX_MQ_VISIBILITY __attribute__((__visibility__("hidden")))
FBX_MQ_VISIBILITY mqd_t __fbx_mq_open(const char *, int, mode_t, struct mq_attr *);
FBX_MQ_VISIBILITY int __fbx_mq_close(mqd_t);
FBX_MQ_VISIBILITY int __fbx_mq_getsetattr(mqd_t, const struct mq_attr *, struct mq_attr *);
FBX_MQ_VISIBILITY int __fbx_mq_timedsend(mqd_t, const char *, size_t, unsigned,
                                       const struct timespec *);
FBX_MQ_VISIBILITY ssize_t __fbx_mq_timedreceive(mqd_t, char *, size_t, unsigned *,
                                              const struct timespec *);
FBX_MQ_VISIBILITY int __fbx_mq_unlink(const char *);
FBX_MQ_VISIBILITY int __fbx_mq_notify(mqd_t, const struct sigevent *);

FBX_MQ_VISIBILITY mqd_t __fbx_mq_guest_open(const char *, int, mode_t, struct mq_attr *);
FBX_MQ_VISIBILITY int __fbx_mq_guest_close(mqd_t);
FBX_MQ_VISIBILITY int __fbx_mq_guest_getsetattr(mqd_t, const struct mq_attr *, struct mq_attr *);
FBX_MQ_VISIBILITY int __fbx_mq_guest_timedsend(mqd_t, const char *, size_t, unsigned,
                                             const struct timespec *);
FBX_MQ_VISIBILITY ssize_t __fbx_mq_guest_timedreceive(mqd_t, char *, size_t, unsigned *,
                                                    const struct timespec *);
FBX_MQ_VISIBILITY int __fbx_mq_guest_unlink(const char *);
FBX_MQ_VISIBILITY int __fbx_mq_guest_notify(mqd_t, const struct sigevent *);

#endif
