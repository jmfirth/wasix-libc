#include <aio.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/auxv.h>
#include "syscall.h"
#else
/* firebox#5DB — faithful POSIX AIO on the wasix substrate.
 *
 * musl's aio is a thread-backed implementation: each submitted request runs on
 * its own detached pthread doing an ordinary pread/pwrite/read/write/fsync, and
 * synchronization is pure pthread/futex. Firebox has real pthreads, futexes and
 * (as of #93A/#5RE) faithful async pthread_cancel unwind, so the model is
 * enabled AS-IS. The only two Linux-syscall dependencies are routed to the
 * substrate here:
 *   - AT_MINSIGSTKSZ auxv probe (io_thread_stack_size): no auxv on wasm, so the
 *     MINSIGSTKSZ floor applies (see __aio_get_queue).
 *   - SIGEV_SIGNAL completion delivery (rt_sigqueueinfo): routed through the
 *     fork's RT-signal FIFO + a process-directed host nudge (__aio_notify_signal
 *     below). */
#include <wasi/api.h>
/* firebox RT-signal FIFO producer (defined in signal/sigaction.c); the queued
 * si_value + SI_ASYNCIO record is drained by __wasm_signal's SA_SIGINFO
 * dispatch so a completion handler observes the real payload. No-op for a
 * non-RT signal. */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
#endif
#include "atomic.h"
#include "pthread_impl.h"
#include "aio_impl.h"

#define malloc __libc_malloc
#define calloc __libc_calloc
#define realloc __libc_realloc
#define free __libc_free

#ifndef __wasilibc_unmodified_upstream
/* firebox#5DB — see aio_impl.h. Deliver the aio/lio completion signal `signo`
 * to the PROCESS carrying `value` + SI_ASYNCIO.
 *
 * WHY process-directed (not raise()/thread-directed): every aio worker thread —
 * and lio_listio's wait_thread — runs with ALL signals blocked (submit() /
 * lio_listio() sigfillset before pthread_create), and the caller here is that
 * worker, about to exit. A thread-directed nudge would pend on the dying worker
 * and the application's handler would never run. A process-directed
 * __wasi_proc_signal is delivered by the host on an unblocked thread — the
 * application thread that installed the handler (mirrors Linux, where
 * rt_sigqueueinfo(getpid(), ...) targets the process).
 *
 * WHY the FIFO push: for a realtime completion signal the SA_SIGINFO handler
 * expects si_value (Open POSIX lio_listio/3-1, /10-1 read info->si_value). The
 * value cannot ride the (pid, signo)-only proc_signal seam, so it is enqueued
 * into the guest RT-signal ring (shared across this process's threads in one
 * linear memory); __wasm_signal's SA_SIGINFO drain pops it back with SI_ASYNCIO.
 * A non-RT signal carries no queued value on Linux either, so the bare
 * proc_signal delivery is faithful (__fbx_rtsigq_push no-ops for it). */
void __aio_notify_signal(int signo, union sigval value)
{
	/* firebox#FY5 — the null signal (signo 0) is never delivered on Linux:
	 * rt_sigqueueinfo(pid, 0, ...) does only perms/existence checks and queues
	 * nothing. A zero-initialised struct aiocb has sigev_notify == SIGEV_SIGNAL
	 * (0) with sigev_signo == 0, so a completed request would route a spurious
	 * signal 0 through __wasi_proc_signal and interrupt an unrelated blocking
	 * syscall in the target (Open POSIX aio_write/2-1: EINTR on the
	 * post-completion read()). The kernel drops sig 0; our libc must reproduce
	 * that here because __wasi_proc_signal does not treat 0 as the null signal. */
	if (signo == 0)
		return;
	(void)__fbx_rtsigq_push(signo, value, SI_ASYNCIO, getpid(), getuid());
	/* Best-effort delivery: like Linux, where a failed rt_sigqueueinfo does not
	 * un-complete the I/O that already finished, a nonzero host errno here is
	 * not propagated back into the (already-done) aio request. */
	__wasi_errno_t __e = __wasi_proc_signal((__wasi_pid_t)getpid(),
	                                        (__wasi_signal_t)signo);
	(void)__e;
}
#endif

/* The following is a threads-based implementation of AIO with minimal
 * dependence on implementation details. Most synchronization is
 * performed with pthread primitives, but atomics and futex operations
 * are used for notification in a couple places where the pthread
 * primitives would be inefficient or impractical.
 *
 * For each fd with outstanding aio operations, an aio_queue structure
 * is maintained. These are reference-counted and destroyed by the last
 * aio worker thread to exit. Accessing any member of the aio_queue
 * structure requires a lock on the aio_queue. Adding and removing aio
 * queues themselves requires a write lock on the global map object,
 * a 4-level table mapping file descriptor numbers to aio queues. A
 * read lock on the map is used to obtain locks on existing queues by
 * excluding destruction of the queue by a different thread while it is
 * being locked.
 *
 * Each aio queue has a list of active threads/operations. Presently there
 * is a one to one relationship between threads and operations. The only
 * members of the aio_thread structure which are accessed by other threads
 * are the linked list pointers, op (which is immutable), running (which
 * is updated atomically), and err (which is synchronized via running),
 * so no locking is necessary. Most of the other other members are used
 * for sharing data between the main flow of execution and cancellation
 * cleanup handler.
 *
 * Taking any aio locks requires having all signals blocked. This is
 * necessary because aio_cancel is needed by close, and close is required
 * to be async-signal safe. All aio worker threads run with all signals
 * blocked permanently.
 */

struct aio_thread {
	pthread_t td;
	struct aiocb *cb;
	struct aio_thread *next, *prev;
	struct aio_queue *q;
	volatile int running;
	int err, op;
	ssize_t ret;
};

struct aio_queue {
	int fd, seekable, append, ref, init;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct aio_thread *head;
};

struct aio_args {
	struct aiocb *cb;
	struct aio_queue *q;
	int op;
	sem_t sem;
};

static pthread_rwlock_t maplock = PTHREAD_RWLOCK_INITIALIZER;
static struct aio_queue *****map;
static volatile int aio_fd_cnt;
volatile int __aio_fut;

static size_t io_thread_stack_size;

#define MAX(a,b) ((a)>(b) ? (a) : (b))

static struct aio_queue *__aio_get_queue(int fd, int need)
{
	if (fd < 0) {
		errno = EBADF;
		return 0;
	}
	int a=fd>>24;
	unsigned char b=fd>>16, c=fd>>8, d=fd;
	struct aio_queue *q = 0;
	pthread_rwlock_rdlock(&maplock);
	if ((!map || !map[a] || !map[a][b] || !map[a][b][c] || !(q=map[a][b][c][d])) && need) {
		pthread_rwlock_unlock(&maplock);
		if (fcntl(fd, F_GETFD) < 0) return 0;
		pthread_rwlock_wrlock(&maplock);
		if (!io_thread_stack_size) {
#ifdef __wasilibc_unmodified_upstream
			unsigned long val = __getauxval(AT_MINSIGSTKSZ);
#else
			/* firebox#5DB — no auxv on wasm; the MINSIGSTKSZ floor applies. */
			unsigned long val = 0;
#endif
			io_thread_stack_size = MAX(MINSIGSTKSZ+2048, val+512);
		}
		if (!map) map = calloc(sizeof *map, (-1U/2+1)>>24);
		if (!map) goto out;
		if (!map[a]) map[a] = calloc(sizeof **map, 256);
		if (!map[a]) goto out;
		if (!map[a][b]) map[a][b] = calloc(sizeof ***map, 256);
		if (!map[a][b]) goto out;
		if (!map[a][b][c]) map[a][b][c] = calloc(sizeof ****map, 256);
		if (!map[a][b][c]) goto out;
		if (!(q = map[a][b][c][d])) {
			map[a][b][c][d] = q = calloc(sizeof *****map, 1);
			if (q) {
				q->fd = fd;
				pthread_mutex_init(&q->lock, 0);
				pthread_cond_init(&q->cond, 0);
				a_inc(&aio_fd_cnt);
			}
		}
	}
	if (q) pthread_mutex_lock(&q->lock);
out:
	pthread_rwlock_unlock(&maplock);
	return q;
}

static void __aio_unref_queue(struct aio_queue *q)
{
	if (q->ref > 1) {
		q->ref--;
		pthread_mutex_unlock(&q->lock);
		return;
	}

	/* This is potentially the last reference, but a new reference
	 * may arrive since we cannot free the queue object without first
	 * taking the maplock, which requires releasing the queue lock. */
	pthread_mutex_unlock(&q->lock);
	pthread_rwlock_wrlock(&maplock);
	pthread_mutex_lock(&q->lock);
	if (q->ref == 1) {
		int fd=q->fd;
		int a=fd>>24;
		unsigned char b=fd>>16, c=fd>>8, d=fd;
		map[a][b][c][d] = 0;
		a_dec(&aio_fd_cnt);
		pthread_rwlock_unlock(&maplock);
		pthread_mutex_unlock(&q->lock);
		free(q);
	} else {
		q->ref--;
		pthread_rwlock_unlock(&maplock);
		pthread_mutex_unlock(&q->lock);
	}
}

static void cleanup(void *ctx)
{
	struct aio_thread *at = ctx;
	struct aio_queue *q = at->q;
	struct aiocb *cb = at->cb;
	struct sigevent sev = cb->aio_sigevent;

	/* There are four potential types of waiters we could need to wake:
	 *   1. Callers of aio_cancel/close.
	 *   2. Callers of aio_suspend with a single aiocb.
	 *   3. Callers of aio_suspend with a list.
	 *   4. AIO worker threads waiting for sequenced operations.
	 * Types 1-3 are notified via atomics/futexes, mainly for AS-safety
	 * considerations. Type 4 is notified later via a cond var. */

	cb->__ret = at->ret;
	if (a_swap(&at->running, 0) < 0)
		__wake(&at->running, -1, 1);
	if (a_swap(&cb->__err, at->err) != EINPROGRESS)
		__wake(&cb->__err, -1, 1);
	if (a_swap(&__aio_fut, 0))
		__wake(&__aio_fut, -1, 1);

	pthread_mutex_lock(&q->lock);

	if (at->next) at->next->prev = at->prev;
	if (at->prev) at->prev->next = at->next;
	else q->head = at->next;

	/* Signal aio worker threads waiting for sequenced operations. */
	pthread_cond_broadcast(&q->cond);

	__aio_unref_queue(q);

	if (sev.sigev_notify == SIGEV_SIGNAL) {
#ifdef __wasilibc_unmodified_upstream
		siginfo_t si = {
			.si_signo = sev.sigev_signo,
			.si_value = sev.sigev_value,
			.si_code = SI_ASYNCIO,
			.si_pid = getpid(),
			.si_uid = getuid()
		};
		__syscall(SYS_rt_sigqueueinfo, si.si_pid, si.si_signo, &si);
#else
		/* firebox#5DB — faithful completion signal via the RT-signal FIFO +
		 * process-directed nudge (see __aio_notify_signal). */
		__aio_notify_signal(sev.sigev_signo, sev.sigev_value);
#endif
	}
	if (sev.sigev_notify == SIGEV_THREAD) {
		a_store(&__pthread_self()->cancel, 0);
		sev.sigev_notify_function(sev.sigev_value);
	}
}

static void *io_thread_func(void *ctx)
{
	struct aio_thread at, *p;

	struct aio_args *args = ctx;
	struct aiocb *cb = args->cb;
	int fd = cb->aio_fildes;
	int op = args->op;
	void *buf = (void *)cb->aio_buf;
	size_t len = cb->aio_nbytes;
	off_t off = cb->aio_offset;

	struct aio_queue *q = args->q;
	ssize_t ret;

	pthread_mutex_lock(&q->lock);
	sem_post(&args->sem);

	at.op = op;
	at.running = 1;
	at.ret = -1;
	at.err = ECANCELED;
	at.q = q;
	at.td = __pthread_self();
	at.cb = cb;
	at.prev = 0;
	if ((at.next = q->head)) at.next->prev = &at;
	q->head = &at;

	if (!q->init) {
		int seekable = lseek(fd, 0, SEEK_CUR) >= 0;
		q->seekable = seekable;
		q->append = !seekable || (fcntl(fd, F_GETFL) & O_APPEND);
		q->init = 1;
	}

	pthread_cleanup_push(cleanup, &at);

	/* Wait for sequenced operations. */
	if (op!=LIO_READ && (op!=LIO_WRITE || q->append)) {
		for (;;) {
			for (p=at.next; p && p->op!=LIO_WRITE; p=p->next);
			if (!p) break;
			pthread_cond_wait(&q->cond, &q->lock);
		}
	}

	pthread_mutex_unlock(&q->lock);

	switch (op) {
	case LIO_WRITE:
		ret = q->append ? write(fd, buf, len) : pwrite(fd, buf, len, off);
		break;
	case LIO_READ:
		ret = !q->seekable ? read(fd, buf, len) : pread(fd, buf, len, off);
		break;
	case O_SYNC:
		ret = fsync(fd);
		break;
	case O_DSYNC:
		ret = fdatasync(fd);
		break;
	}
	at.ret = ret;
	at.err = ret<0 ? errno : 0;
	
	pthread_cleanup_pop(1);

	return 0;
}

static int submit(struct aiocb *cb, int op)
{
	int ret = 0;
	pthread_attr_t a;
	sigset_t allmask, origmask;
	pthread_t td;
	struct aio_queue *q = __aio_get_queue(cb->aio_fildes, 1);
	struct aio_args args = { .cb = cb, .op = op, .q = q };
	sem_init(&args.sem, 0, 0);

	if (!q) {
		/* firebox#FY5 — glibc defers a bad-fd [EBADF] to aio_error rather than
		 * failing eagerly for the DATA-TRANSFER ops (aio_read/aio_write): POSIX
		 * permits either form there ("the call shall fail OR the error status of
		 * the operation shall be [EBADF]"), and the Open POSIX aio_read/10-1 +
		 * aio_write/8-1 assertions — and most Linux (=glibc) — require the
		 * deferred form. aio_fsync is DIFFERENT: POSIX requires it to validate
		 * the descriptor synchronously and FAIL EAGERLY with -1/EBADF (Open POSIX
		 * aio_fsync/12-1 asserts `aio_fsync(bad_fd) == -1 && errno == EBADF`), and
		 * aio_fsync routes through this same submit(). So gate the deferral on the
		 * data-transfer ops (LIO_READ/LIO_WRITE) only — for read/write, record the
		 * failure on the aiocb and return success (no worker thread; the fd is
		 * unusable, but aio_error()/aio_return() observe glibc's EBADF/-1
		 * contract). For aio_fsync (and any resource shortage), fall through to the
		 * eager -1: EBADF is preserved, other errnos become EAGAIN — the original
		 * musl behavior, which aio_fsync/12-1 needs. Deliberate glibc-alignment
		 * for the transfer ops (docs/reference/forks.md §5). */
		if (errno == EBADF && (op == LIO_READ || op == LIO_WRITE)) {
			cb->__ret = -1;
			cb->__err = EBADF;
			return 0;
		}
		if (errno != EBADF) errno = EAGAIN;
		cb->__ret = -1;
		cb->__err = errno;
		return -1;
	}
	q->ref++;
	pthread_mutex_unlock(&q->lock);

	if (cb->aio_sigevent.sigev_notify == SIGEV_THREAD) {
		if (cb->aio_sigevent.sigev_notify_attributes)
			a = *cb->aio_sigevent.sigev_notify_attributes;
		else
			pthread_attr_init(&a);
	} else {
		pthread_attr_init(&a);
		pthread_attr_setstacksize(&a, io_thread_stack_size);
		pthread_attr_setguardsize(&a, 0);
	}
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	sigfillset(&allmask);
	pthread_sigmask(SIG_BLOCK, &allmask, &origmask);
	cb->__err = EINPROGRESS;
	if (__pthread_create(&td, &a, io_thread_func, &args)) {
		pthread_mutex_lock(&q->lock);
		__aio_unref_queue(q);
		cb->__err = errno = EAGAIN;
		cb->__ret = ret = -1;
	}
	pthread_sigmask(SIG_SETMASK, &origmask, 0);

	if (!ret) {
		while (sem_wait(&args.sem));
	}

	return ret;
}

int aio_read(struct aiocb *cb)
{
	return submit(cb, LIO_READ);
}

int aio_write(struct aiocb *cb)
{
	return submit(cb, LIO_WRITE);
}

int aio_fsync(int op, struct aiocb *cb)
{
	if (op != O_SYNC && op != O_DSYNC) {
		errno = EINVAL;
		return -1;
	}
	return submit(cb, op);
}

ssize_t aio_return(struct aiocb *cb)
{
	return cb->__ret;
}

int aio_error(const struct aiocb *cb)
{
	a_barrier();
	return cb->__err & 0x7fffffff;
}

int aio_cancel(int fd, struct aiocb *cb)
{
	sigset_t allmask, origmask;
	int ret = AIO_ALLDONE;
	struct aio_thread *p;
	struct aio_queue *q;

	/* Unspecified behavior case. Report an error. */
	if (cb && fd != cb->aio_fildes) {
		errno = EINVAL;
		return -1;
	}

	sigfillset(&allmask);
	pthread_sigmask(SIG_BLOCK, &allmask, &origmask);

	errno = ENOENT;
	if (!(q = __aio_get_queue(fd, 0))) {
		if (errno == EBADF) ret = -1;
		goto done;
	}

	for (p = q->head; p; p = p->next) {
		if (cb && cb != p->cb) continue;
		/* Transition target from running to running-with-waiters */
		if (a_cas(&p->running, 1, -1)) {
			pthread_cancel(p->td);
			__wait(&p->running, 0, -1, 1);
			if (p->err == ECANCELED) ret = AIO_CANCELED;
		}
	}

	pthread_mutex_unlock(&q->lock);
done:
	pthread_sigmask(SIG_SETMASK, &origmask, 0);
	return ret;
}

int __aio_close(int fd)
{
	a_barrier();
	if (aio_fd_cnt) aio_cancel(fd, 0);
	return fd;
}

void __aio_atfork(int who)
{
	if (who<0) {
		pthread_rwlock_rdlock(&maplock);
		return;
	}
	if (who>0 && map) for (int a=0; a<(-1U/2+1)>>24; a++)
		if (map[a]) for (int b=0; b<256; b++)
			if (map[a][b]) for (int c=0; c<256; c++)
				if (map[a][b][c]) for (int d=0; d<256; d++)
					map[a][b][c][d] = 0;
	pthread_rwlock_unlock(&maplock);
}

weak_alias(aio_cancel, aio_cancel64);
weak_alias(aio_error, aio_error64);
weak_alias(aio_fsync, aio_fsync64);
weak_alias(aio_read, aio_read64);
weak_alias(aio_write, aio_write64);
weak_alias(aio_return, aio_return64);
