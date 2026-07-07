#ifndef FBX_MQ_IMPL_H
#define FBX_MQ_IMPL_H

/*
 * firebox#KS9 — guest-side POSIX message-queue implementation for wasix-libc.
 *
 * WHY this file exists: upstream musl implements mq_* as thin wrappers over the
 * Linux `mq_*` syscalls, which drive the kernel `mqueue` filesystem (message
 * storage, blocking, priority ordering, and cross-process coherence all live in
 * the kernel). WASIX exposes none of those syscalls, so the whole family linked
 * as `undefined symbol: mq_open` and the Open POSIX mqueue conformance rows all
 * BUILDFAILed.
 *
 * Firebox's thesis (Invariant 0) is faithful Linux/POSIX semantics all the way
 * down, so the fix is not a stub — it is a real named-queue registry living in
 * guest libc: priority-ordered storage, blocking send/receive built on the
 * futex primitives (__timedwait/__wake) that already restore the Linux
 * futex==EINTR / ETIMEDOUT contract on Firebox (firebox#G13/#NSL), O_NONBLOCK →
 * EAGAIN, and mq_notify.
 *
 * SCOPE — single-process (incl. multi-threaded) faithfulness. The registry is
 * process-global static state shared across threads. It is NOT shared across a
 * `fork()` boundary: Firebox fork is a faithful copy-on-write of guest linear
 * memory, so a child gets its own COPY of the registry (correct for inheriting
 * already-enqueued messages, which is how a real kernel queue is inherited across
 * fork for the pre-fork contents). Tests where a child re-opens a queue BY NAME
 * to rendezvous with the parent, or where parent and child mutate the same queue
 * AFTER fork, need queue state that lives ABOVE the process (the kernel's role on
 * Linux) — that is a host mq surface and is deferred (see reports/). Those rows
 * link now (symbols present) and run, but their cross-process assertions are not
 * met by guest-only state; they are re-classified as a distinct known-gap, not a
 * regression.
 *
 * The public mq_*.c files delegate to the __fbx_mq_* entry points below under the
 * `#ifndef __wasilibc_unmodified_upstream` arm; the upstream arm keeps the syscall
 * bodies verbatim.
 */

#include <mqueue.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

#define FBX_MQ_VISIBILITY __attribute__((__visibility__("hidden")))

FBX_MQ_VISIBILITY mqd_t __fbx_mq_open(const char *name, int flags, mode_t mode,
                                      struct mq_attr *attr);
FBX_MQ_VISIBILITY int __fbx_mq_close(mqd_t mqd);
FBX_MQ_VISIBILITY int __fbx_mq_getsetattr(mqd_t mqd, const struct mq_attr *neu,
                                          struct mq_attr *old);
FBX_MQ_VISIBILITY int __fbx_mq_timedsend(mqd_t mqd, const char *msg, size_t len,
                                         unsigned prio, const struct timespec *at);
FBX_MQ_VISIBILITY ssize_t __fbx_mq_timedreceive(mqd_t mqd, char *msg, size_t len,
                                                unsigned *prio,
                                                const struct timespec *at);
FBX_MQ_VISIBILITY int __fbx_mq_unlink(const char *name);
FBX_MQ_VISIBILITY int __fbx_mq_notify(mqd_t mqd, const struct sigevent *sev);

#endif /* FBX_MQ_IMPL_H */
