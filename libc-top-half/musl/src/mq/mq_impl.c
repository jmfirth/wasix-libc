/*
 * firebox#KS9 — guest-side POSIX message-queue registry for wasix-libc.
 * See mq_impl.h for the rationale, scope, and cross-process caveat.
 *
 * Model:
 *   - A fixed table of named queues (process-global static state, shared across
 *     threads). Each queue holds a priority-ordered singly-linked message list
 *     (highest priority first, FIFO within equal priority — POSIX ordering).
 *   - A fixed table of open descriptors; mqd_t is an index into it.
 *   - The registry is protected by a single pthread_mutex. Blocking send/receive
 *     follow the classic futex condition-variable pattern: snapshot a per-queue
 *     sequence counter under the lock, drop the lock, then __timedwait on that
 *     counter. The counterpart operation bumps the counter (a_inc) under the
 *     lock and __wake()s it, so a wake that races the park is not lost (the value
 *     mismatch short-circuits __timedwait). __timedwait surfaces EINTR (signal)
 *     and ETIMEDOUT faithfully on Firebox (firebox#G13/#NSL), matching the Linux
 *     mq_timed* syscall contract.
 */

#ifndef __wasilibc_unmodified_upstream

#include "mq_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pthread_impl.h" /* __timedwait, __wake, a_inc */

#define FBX_MQ_MAX_QUEUES 128
#define FBX_MQ_MAX_DESC 256
#define FBX_MQ_NAME_MAX NAME_MAX        /* 255 — POSIX queue name (sans slash) */
#define FBX_MQ_DEFAULT_MAXMSG 10        /* Linux /proc/sys/fs/mqueue defaults */
#define FBX_MQ_DEFAULT_MSGSIZE 8192

struct fbx_mq_msg {
	struct fbx_mq_msg *next;
	unsigned prio;
	size_t len;
	char data[];
};

struct fbx_mq_queue {
	int used;
	int unlinked;                   /* name removed; existing descs still valid */
	int refcnt;                     /* number of open descriptors */
	char name[FBX_MQ_NAME_MAX + 1];
	long maxmsg;
	long msgsize;
	long curmsgs;
	struct fbx_mq_msg *head;        /* priority-ordered */
	volatile int send_seq;          /* bumped when a slot frees (wakes senders) */
	volatile int recv_seq;          /* bumped when a msg arrives (wakes receivers) */
	int blocked_receivers;          /* receivers currently parked (notify gate) */
	int notify_active;
	struct sigevent notify_sev;
};

struct fbx_mq_desc {
	int used;
	int flags;                      /* O_ACCMODE bits + O_NONBLOCK */
	struct fbx_mq_queue *q;
};

static struct fbx_mq_queue g_queues[FBX_MQ_MAX_QUEUES];
static struct fbx_mq_desc g_descs[FBX_MQ_MAX_DESC];
static pthread_mutex_t g_mq_lock = PTHREAD_MUTEX_INITIALIZER;

static void mq_lock(void) { pthread_mutex_lock(&g_mq_lock); }
static void mq_unlock(void) { pthread_mutex_unlock(&g_mq_lock); }

/* Strip a single leading slash (POSIX names are "/name"); reject over-length.
 * Mirrors musl's public wrappers, which already strip before the syscall, so
 * this is idempotent on an already-stripped name. */
static int mq_key(const char *name, const char **out)
{
	if (!name) return EINVAL;
	if (*name == '/') name++;
	if (strlen(name) > FBX_MQ_NAME_MAX) return ENAMETOOLONG;
	*out = name;
	return 0;
}

static struct fbx_mq_queue *mq_find(const char *key)
{
	for (int i = 0; i < FBX_MQ_MAX_QUEUES; i++)
		if (g_queues[i].used && !g_queues[i].unlinked
		    && !strcmp(g_queues[i].name, key))
			return &g_queues[i];
	return 0;
}

static struct fbx_mq_queue *mq_alloc_queue(void)
{
	for (int i = 0; i < FBX_MQ_MAX_QUEUES; i++)
		if (!g_queues[i].used)
			return &g_queues[i];
	return 0;
}

static struct fbx_mq_desc *mq_alloc_desc(void)
{
	for (int i = 0; i < FBX_MQ_MAX_DESC; i++)
		if (!g_descs[i].used)
			return &g_descs[i];
	return 0;
}

static struct fbx_mq_desc *mq_desc_check(mqd_t mqd)
{
	if (mqd < 0 || mqd >= FBX_MQ_MAX_DESC) return 0;
	struct fbx_mq_desc *d = &g_descs[mqd];
	return d->used ? d : 0;
}

static void mq_destroy_queue(struct fbx_mq_queue *q)
{
	struct fbx_mq_msg *m = q->head;
	while (m) {
		struct fbx_mq_msg *n = m->next;
		free(m);
		m = n;
	}
	memset(q, 0, sizeof *q);
}

/* Insert highest-priority-first, FIFO within equal priority (place after all
 * messages of priority >= m->prio). Receive pops the head. */
static void mq_insert(struct fbx_mq_queue *q, struct fbx_mq_msg *m)
{
	struct fbx_mq_msg **pp = &q->head;
	while (*pp && (*pp)->prio >= m->prio)
		pp = &(*pp)->next;
	m->next = *pp;
	*pp = m;
}

static void *mq_notify_thread(void *p)
{
	struct sigevent sev = *(struct sigevent *)p;
	free(p);
	void (*fn)(union sigval) = sev.sigev_notify_function;
	if (fn) fn(sev.sigev_value);
	return 0;
}

/* Deliver a triggered notification. Called with the registry lock NOT held so a
 * handler may re-enter the mq_* API. */
static void mq_deliver_notify(const struct sigevent *sev)
{
	switch (sev->sigev_notify) {
	case SIGEV_NONE:
		break;
	case SIGEV_SIGNAL:
		/* Process-directed, as POSIX specifies (single process here, so the
		 * registrant IS the caller). */
		raise(sev->sigev_signo);
		break;
	case SIGEV_THREAD: {
		struct sigevent *copy = malloc(sizeof *copy);
		if (!copy) break;
		*copy = *sev;
		pthread_attr_t attr;
		pthread_attr_t *ap = 0;
		if (sev->sigev_notify_attributes) {
			ap = sev->sigev_notify_attributes;
		} else {
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			ap = &attr;
		}
		pthread_t td;
		if (pthread_create(&td, ap, mq_notify_thread, copy)) {
			free(copy);
		} else if (ap == &attr) {
			pthread_attr_destroy(&attr);
		}
		break;
	}
	default:
		break;
	}
}

mqd_t __fbx_mq_open(const char *name, int flags, mode_t mode, struct mq_attr *attr)
{
	(void)mode; /* fidelity-not-security: default-root, no permission gating */
	const char *key;
	int e = mq_key(name, &key);
	if (e) {
		errno = e;
		return (mqd_t)-1;
	}

	long maxmsg = FBX_MQ_DEFAULT_MAXMSG;
	long msgsize = FBX_MQ_DEFAULT_MSGSIZE;
	if ((flags & O_CREAT) && attr) {
		if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0) {
			errno = EINVAL;
			return (mqd_t)-1;
		}
		maxmsg = attr->mq_maxmsg;
		msgsize = attr->mq_msgsize;
	}

	mq_lock();
	struct fbx_mq_queue *q = mq_find(key);
	int created = 0;
	if (q) {
		if ((flags & O_CREAT) && (flags & O_EXCL)) {
			mq_unlock();
			errno = EEXIST;
			return (mqd_t)-1;
		}
	} else {
		if (!(flags & O_CREAT)) {
			mq_unlock();
			errno = ENOENT;
			return (mqd_t)-1;
		}
		q = mq_alloc_queue();
		if (!q) {
			mq_unlock();
			errno = ENOSPC;
			return (mqd_t)-1;
		}
		memset(q, 0, sizeof *q);
		q->used = 1;
		strcpy(q->name, key);
		q->maxmsg = maxmsg;
		q->msgsize = msgsize;
		created = 1;
	}

	struct fbx_mq_desc *d = mq_alloc_desc();
	if (!d) {
		if (created)
			mq_destroy_queue(q);
		mq_unlock();
		errno = EMFILE;
		return (mqd_t)-1;
	}
	d->used = 1;
	d->flags = flags & (O_ACCMODE | O_NONBLOCK);
	d->q = q;
	q->refcnt++;
	mqd_t r = (mqd_t)(d - g_descs);
	mq_unlock();
	return r;
}

int __fbx_mq_close(mqd_t mqd)
{
	mq_lock();
	struct fbx_mq_desc *d = mq_desc_check(mqd);
	if (!d) {
		mq_unlock();
		errno = EBADF;
		return -1;
	}
	struct fbx_mq_queue *q = d->q;
	d->used = 0;
	d->flags = 0;
	d->q = 0;
	if (--q->refcnt == 0 && q->unlinked)
		mq_destroy_queue(q);
	mq_unlock();
	return 0;
}

int __fbx_mq_unlink(const char *name)
{
	const char *key;
	int e = mq_key(name, &key);
	if (e) {
		errno = e;
		return -1;
	}
	mq_lock();
	struct fbx_mq_queue *q = mq_find(key);
	if (!q) {
		mq_unlock();
		errno = ENOENT;
		return -1;
	}
	q->unlinked = 1;
	if (q->refcnt == 0)
		mq_destroy_queue(q);
	mq_unlock();
	return 0;
}

/* Backs both mq_getattr (neu==NULL) and mq_setattr. Only the O_NONBLOCK flag is
 * mutable per POSIX; maxmsg/msgsize are fixed at creation. */
int __fbx_mq_getsetattr(mqd_t mqd, const struct mq_attr *neu, struct mq_attr *old)
{
	mq_lock();
	struct fbx_mq_desc *d = mq_desc_check(mqd);
	if (!d) {
		mq_unlock();
		errno = EBADF;
		return -1;
	}
	struct fbx_mq_queue *q = d->q;
	if (old) {
		memset(old, 0, sizeof *old);
		old->mq_flags = (d->flags & O_NONBLOCK) ? O_NONBLOCK : 0;
		old->mq_maxmsg = q->maxmsg;
		old->mq_msgsize = q->msgsize;
		old->mq_curmsgs = q->curmsgs;
	}
	if (neu) {
		if (neu->mq_flags & O_NONBLOCK)
			d->flags |= O_NONBLOCK;
		else
			d->flags &= ~O_NONBLOCK;
	}
	mq_unlock();
	return 0;
}

int __fbx_mq_timedsend(mqd_t mqd, const char *msg, size_t len, unsigned prio,
                       const struct timespec *at)
{
	if (prio >= MQ_PRIO_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (at && (at->tv_nsec < 0 || at->tv_nsec >= 1000000000L)) {
		errno = EINVAL;
		return -1;
	}

	for (;;) {
		mq_lock();
		struct fbx_mq_desc *d = mq_desc_check(mqd);
		if (!d) {
			mq_unlock();
			errno = EBADF;
			return -1;
		}
		if ((d->flags & O_ACCMODE) == O_RDONLY) {
			mq_unlock();
			errno = EBADF;
			return -1;
		}
		struct fbx_mq_queue *q = d->q;
		if (len > (size_t)q->msgsize) {
			mq_unlock();
			errno = EMSGSIZE;
			return -1;
		}

		if (q->curmsgs < q->maxmsg) {
			struct fbx_mq_msg *m = malloc(sizeof *m + len);
			if (!m) {
				mq_unlock();
				errno = ENOMEM;
				return -1;
			}
			m->prio = prio;
			m->len = len;
			if (len)
				memcpy(m->data, msg, len);
			int was_empty = (q->curmsgs == 0);
			mq_insert(q, m);
			q->curmsgs++;
			a_inc(&q->recv_seq);

			struct sigevent sev;
			int do_notify = 0;
			if (was_empty && q->notify_active && q->blocked_receivers == 0) {
				sev = q->notify_sev;
				q->notify_active = 0;
				do_notify = 1;
			}
			mq_unlock();
			__wake(&q->recv_seq, 1, 1);
			if (do_notify)
				mq_deliver_notify(&sev);
			return 0;
		}

		if (d->flags & O_NONBLOCK) {
			mq_unlock();
			errno = EAGAIN;
			return -1;
		}
		int seq = q->send_seq;
		mq_unlock();
		int r = __timedwait(&q->send_seq, seq, CLOCK_REALTIME, at, 1);
		if (r == ETIMEDOUT) {
			errno = ETIMEDOUT;
			return -1;
		}
		if (r == EINTR) {
			errno = EINTR;
			return -1;
		}
		/* woken / value-changed: re-evaluate */
	}
}

ssize_t __fbx_mq_timedreceive(mqd_t mqd, char *msg, size_t len, unsigned *prio,
                              const struct timespec *at)
{
	if (at && (at->tv_nsec < 0 || at->tv_nsec >= 1000000000L)) {
		errno = EINVAL;
		return -1;
	}

	for (;;) {
		mq_lock();
		struct fbx_mq_desc *d = mq_desc_check(mqd);
		if (!d) {
			mq_unlock();
			errno = EBADF;
			return -1;
		}
		if ((d->flags & O_ACCMODE) == O_WRONLY) {
			mq_unlock();
			errno = EBADF;
			return -1;
		}
		struct fbx_mq_queue *q = d->q;
		/* POSIX: the receive buffer must be at least mq_msgsize bytes. */
		if (len < (size_t)q->msgsize) {
			mq_unlock();
			errno = EMSGSIZE;
			return -1;
		}

		if (q->curmsgs > 0) {
			struct fbx_mq_msg *m = q->head;
			q->head = m->next;
			q->curmsgs--;
			a_inc(&q->send_seq);
			ssize_t rlen = (ssize_t)m->len;
			unsigned rprio = m->prio;
			if (m->len)
				memcpy(msg, m->data, m->len);
			free(m);
			mq_unlock();
			__wake(&q->send_seq, 1, 1);
			if (prio)
				*prio = rprio;
			return rlen;
		}

		if (d->flags & O_NONBLOCK) {
			mq_unlock();
			errno = EAGAIN;
			return -1;
		}
		int seq = q->recv_seq;
		q->blocked_receivers++;
		mq_unlock();
		int r = __timedwait(&q->recv_seq, seq, CLOCK_REALTIME, at, 1);
		mq_lock();
		q->blocked_receivers--;
		mq_unlock();
		if (r == ETIMEDOUT) {
			errno = ETIMEDOUT;
			return -1;
		}
		if (r == EINTR) {
			errno = EINTR;
			return -1;
		}
		/* woken / value-changed: re-evaluate */
	}
}

int __fbx_mq_notify(mqd_t mqd, const struct sigevent *sev)
{
	mq_lock();
	struct fbx_mq_desc *d = mq_desc_check(mqd);
	if (!d) {
		mq_unlock();
		errno = EBADF;
		return -1;
	}
	struct fbx_mq_queue *q = d->q;
	if (!sev) {
		q->notify_active = 0; /* deregister */
		mq_unlock();
		return 0;
	}
	if (q->notify_active) {
		mq_unlock();
		errno = EBUSY;
		return -1;
	}
	q->notify_sev = *sev;
	q->notify_active = 1;
	mq_unlock();
	return 0;
}

#endif /* !__wasilibc_unmodified_upstream */
