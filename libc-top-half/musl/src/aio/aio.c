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
	/* firebox#VBY — set by aio_cancel, under q->lock, on a worker it has
	 * completed IN PLACE because that worker was parked in the sequencing
	 * wait below and had therefore not started its I/O. The worker observes
	 * it on wake and skips straight to its cleanup. */
	volatile int cancelled;
	/* firebox#VBY — true exactly while this worker is inside the sequencing
	 * wait. aio_cancel reads it under q->lock to decide which of the two
	 * cancel paths a target needs. Safe to read there because a worker that
	 * is NOT in the wait holds q->lock, so aio_cancel cannot be running. */
	volatile int seq_waiting;
	int err, op;
	ssize_t ret;
};

struct aio_queue {
	int fd, seekable, append, ref, init;
	/* firebox#VBY — count of workers this queue has had MARKED cancelled by
	 * aio_cancel and that have not yet published their completion. It lives on
	 * the QUEUE, not on the request, for one reason: a canceller must wait for
	 * the settle WITHOUT holding q->lock (holding it is the deadlock), and the
	 * request's `at` is a stack local of io_thread_func that dies the moment
	 * the worker unwinds. The queue is refcounted, so a canceller can hold it
	 * alive across an unlocked wait; a request cannot be held alive at all. */
	volatile int cancel_pending;
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

	/* firebox#VBY v4 — PUBLISH THE COMPLETION BEFORE RELEASING THE CANCELLER.
	 * Upstream does these two in the opposite order, which means
	 * aio_cancel's __wait(&p->running,...) is released ONE LINE BEFORE
	 * cb->__err becomes visible — so even the ordinary pthread_cancel path
	 * can return AIO_CANCELED over a request still reading EINPROGRESS. That
	 * is not cosmetic: Open POSIX's shared cleanup_aio() issues a BLOCKING
	 * read() for every request still reading EINPROGRESS, so the application
	 * blocks forever on data that will never be written.
	 *
	 * Safe with respect to the aiocb handoff contract, and the analysis is
	 * the whole reason this is only a reorder: everything this function
	 * reads or writes through `cb` happens at or above this point (`sev` is
	 * copied at the top, cb->__ret just above), so once __err is published
	 * the application may free or reuse the aiocb and we never touch it
	 * again. Publishing EARLIER also NARROWS the pre-existing window in
	 * which the app could free cb before our own __err store. */
	cb->__ret = at->ret;
	if (a_swap(&cb->__err, at->err) != EINPROGRESS)
		__wake(&cb->__err, -1, 1);
	if (a_swap(&at->running, 0) < 0)
		__wake(&at->running, -1, 1);

	/* firebox#VBY — release aio_cancel's settle-wait, and do it HERE:
	 * strictly after the cb->__err publish above, so a canceller that observes
	 * the drain is guaranteed to have our ECANCELED visible to aio_error().
	 * That ordering is the entire point of the counter — without it the
	 * canceller returns AIO_CANCELED over a request still reading
	 * EINPROGRESS, which Open POSIX aio_cancel/2-1 and /6-1 detect. */
	if (at->cancelled) {
		a_dec(&q->cancel_pending);
		__wake(&q->cancel_pending, -1, 1);
	}
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
	at.cancelled = 0;
	at.seq_waiting = 0;
	at.ret = -1;
	/* NOTE (firebox#VBY): at.err is ECANCELED and at.ret is -1 from here
	 * until the I/O completes and overwrites them. That is what makes the
	 * cancelled path below correct WITHOUT setting anything: a worker that
	 * skips its I/O already carries exactly the result POSIX requires. */
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
		/* firebox#VBY — announce that we are parked HERE rather than in a
		 * syscall. aio_cancel cannot cancel us out of this wait: POSIX makes
		 * a thread cancelled out of pthread_cond_wait re-acquire the mutex
		 * before it may unwind, and the mutex it needs is q->lock, which
		 * aio_cancel is holding while it waits for us. That is a deadlock,
		 * and it is why this flag exists. */
		at.seq_waiting = 1;
		for (;;) {
			if (at.cancelled) break;
			/* NOTE (firebox#VBY): deliberately NOT skipping entries with
			 * `cancelled` set. It looked necessary and is not: a marked
			 * worker breaks out, and its cleanup() unlinks it and broadcasts
			 * this same cv, so anyone waiting behind it re-scans and
			 * proceeds. The only cost is one extra wake/rescan round-trip,
			 * and the alternative is a real semantic change to write
			 * ordering for no liveness gain. */
			for (p=at.next; p && p->op!=LIO_WRITE; p=p->next);
			if (!p) break;
			pthread_cond_wait(&q->cond, &q->lock);
		}
		at.seq_waiting = 0;
	}

	pthread_mutex_unlock(&q->lock);

	/* firebox#VBY — aio_cancel MARKED us (it does not publish anything; see
	 * the cancel path). Our I/O was never started, so skip straight to
	 * cleanup(), which publishes the -1/ECANCELED that at.ret/at.err already
	 * hold and then drains q->cancel_pending to release the canceller. */
	if (at.cancelled)
		goto done;

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

done:
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
	int bcast = 0;
	int drain = 0;
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
			if (p->seq_waiting) {
				/* firebox#VBY — THE DEADLOCK CASE, AND WHY IT NEEDS ITS
				 * OWN PATH RATHER THAN A LONGER WAIT.
				 *
				 * This target is parked in the sequencing pthread_cond_wait
				 * above, holding nothing, waiting for an older write. It has
				 * NOT started its I/O. Cancelling it the ordinary way cannot
				 * work: a thread cancelled out of pthread_cond_wait must
				 * re-acquire the mutex before it may run its cleanup handlers
				 * (POSIX; musl does this unconditionally at the `relock`
				 * label in pthread_cond_timedwait.c), and that mutex is
				 * q->lock, which we are holding and will keep holding inside
				 * __wait. The cleanup that would release us can never run.
				 *
				 * So we neither cancel it nor wait for it. We only MARK it;
				 * the broadcast after the loop releases it, and it completes
				 * itself through the ordinary cleanup() path with the
				 * ECANCELED/-1 that at.err/at.ret already hold.
				 *
				 * ⛔ WE MUST NOT PUBLISH THE COMPLETION OURSELVES, AND THIS
				 * IS THE WHOLE REASON THIS BRANCH IS THREE LINES INSTEAD OF
				 * TEN. cleanup() is ordered so that everything it reads or
				 * writes through `cb` happens BEFORE it publishes
				 * cb->__err — because POSIX lets the application free or
				 * reuse the aiocb the moment aio_error() stops returning
				 * EINPROGRESS. A canceller that published cb->__err here
				 * would leave the worker to afterwards read cb->aio_sigevent
				 * (an indirect call through a pointer out of possibly-freed
				 * memory) and write cb->__ret and cb->__err again — landing
				 * on a RESUBMITTED request and reporting it complete. The
				 * worker is the only party that knows cb is still live.
				 *
				 * Consequence, and it matches the existing path rather than
				 * weakening it: aio_cancel may return before cb->__err is
				 * published. The pthread_cancel branch below already has
				 * that window, because __wait is released by cleanup()'s
				 * `running` store, which precedes its `__err` store. */
				/* ⛔ firebox#VBY v4 — THE INCREMENT IS GUARDED AND THE
				 * WAIT IS NOT. Do not "simplify" this by moving
				 * `drain = 1` inside the if.
				 *
				 * a_cas RETURNS THE OLD VALUE, not a success flag
				 * (`internal/atomic.h:20-27`), so the branch above is
				 * taken for old==1 (we won the transition) AND for
				 * old==-1 (another canceller already took it). Upstream
				 * can do that safely because the body it guards —
				 * pthread_cancel + __wait — is IDEMPOTENT. A counter
				 * increment is not: two cancellers would push twice and
				 * cleanup() pops once, so cancel_pending would never
				 * reach 0 and BOTH would wait forever, with all signals
				 * blocked, one of them inside close(). __aio_close
				 * supplies that second canceller for free.
				 *
				 * p->cancelled is only written here, under q->lock, so
				 * it is the exact "did I win" flag a_cas did not give
				 * us. The wait stays unconditional because a second
				 * canceller must still not return AIO_CANCELED over an
				 * unsettled request. */
				if (!p->cancelled) {
					p->cancelled = 1;
					a_inc(&q->cancel_pending);
					bcast = 1;
				}
				drain = 1;
				ret = AIO_CANCELED;
			} else {
				/* Parked in the I/O itself (or runnable). It holds no lock we
				 * need, and its cleanup() reaches the `running` store before
				 * it asks for q->lock, so this handshake completes. */
				pthread_cancel(p->td);
				__wait(&p->running, 0, -1, 1);
				if (p->err == ECANCELED) ret = AIO_CANCELED;
			}
		}
	}

	/* firebox#VBY — release the workers we marked. They cannot observe this
	 * until we drop q->lock below, which is the ordering that makes it safe
	 * to have written to their stack-resident `at`.
	 *
	 * ⚠️ THIS BROADCAST DOES TAKE A LOCK AND CAN BLOCK — do not describe it
	 * as lock-free. __private_cond_signal takes c->_c_lock and waits out any
	 * LEAVING-state waiter. It is nonetheless AS-safe here for two specific
	 * reasons: every holder of c->_c_lock runs with all signals blocked (the
	 * workers permanently, and we via the sigfillset above), so a handler
	 * cannot interrupt a holder on this thread; and cleanup() already
	 * broadcasts this same cv under the identical discipline, so this is not
	 * a new class. The non-obvious part is that it cannot deadlock against
	 * the q->lock we are holding: a LEAVING waiter decrements node.notify
	 * BEFORE the `relock` label, so the wait inside __private_cond_signal
	 * never needs q->lock.
	 *
	 * Guarded because __aio_close cancels on every close() of an fd with a
	 * live queue, and an unconditional broadcast makes that a wake storm. */
	if (bcast) pthread_cond_broadcast(&q->cond);

	/* firebox#VBY — hold the queue alive across the unlocked wait below.
	 * __aio_get_queue took NO reference; q->lock is the only thing keeping q
	 * from being freed under us, and we are about to drop it. This mirrors
	 * submit()'s q->ref++ before its own unlock. */
	if (drain) q->ref++;

	pthread_mutex_unlock(&q->lock);

	/* firebox#VBY — WAIT FOR THE MARKED WORKERS TO SETTLE, WITH NO LOCK HELD.
	 *
	 * Why this is not the deadlock we just fixed: the wait that deadlocks is a
	 * wait held OVER q->lock, because a marked worker needs that same lock to
	 * leave pthread_cond_wait and unwind. Here we have already released it, so
	 * every marked worker can run to completion without anything from us.
	 *
	 * Why we must wait at all: POSIX callers read aio_error() immediately after
	 * AIO_CANCELED and expect ECANCELED. Returning early is not merely a wrong
	 * value — Open POSIX's shared cleanup_aio() issues a BLOCKING read() on the
	 * peer socket for every request still reading EINPROGRESS, so an unsettled
	 * cancel makes the application block forever on data that will never be
	 * written (aio_cancel/4-1). Upstream has a hairline version of this window
	 * (cleanup() wakes `running` one line before it publishes cb->__err); the
	 * marked path widens it to a full wake-and-unwind, which is what makes it
	 * observable, so this closes it for the path we introduced.
	 *
	 * ⚠️ THE COUNTER IS A LEVEL, NOT A GENERATION, AND THIS WAIT IS THEREFORE
	 * NOT BOUNDED. An earlier version of this comment claimed the over-wait was
	 * "bounded" and "cannot hang"; that is FALSE and is corrected rather than
	 * deleted, because the next reader would otherwise re-derive it. We wait for
	 * the level to reach 0, and a concurrent canceller on the same fd can raise
	 * it again for a request we know nothing about, so a steady stream of
	 * submit+cancel on one fd can extend this wait indefinitely.
	 *
	 * Accepted deliberately, for a reason that is about the FUNCTION and not
	 * about this patch: aio_cancel is ALREADY an unbounded-wait call upstream —
	 * the pthread_cancel branch above __waits for an in-flight worker with no
	 * timeout, and that worker may be inside an arbitrarily slow read(). So this
	 * does not change aio_cancel's liveness class. Each individual marked worker
	 * is runnable and settles; it is only the AGGREGATE that can be kept
	 * nonzero. If this ever bites, the fix is a per-canceller epoch (wait on a
	 * count of the nodes THIS call marked), not a timeout. */
	if (drain) {
		int v;
		while ((v = q->cancel_pending))
			__wait(&q->cancel_pending, 0, v, 1);
		/* firebox#VBY v4 — the loop's load of cancel_pending is a PLAIN
		 * volatile read; volatile is not an acquire and orders nothing
		 * against a later read of cb->__err. Make the ordering real here
		 * rather than claiming it: aio_error() has its own a_barrier(),
		 * but a caller reading cb->__err directly has none. */
		a_barrier();
		pthread_mutex_lock(&q->lock);
		__aio_unref_queue(q);	/* releases q->lock; may free q */
	}
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
