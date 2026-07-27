#include <aio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include "pthread_impl.h"
#ifndef __wasilibc_unmodified_upstream
#include "aio_impl.h"	/* firebox#5DB — __aio_notify_signal (shared with aio.c) */
#endif

struct lio_state {
	struct sigevent *sev;
	int cnt;
	struct aiocb *cbs[];
};

static int lio_wait(struct lio_state *st)
{
	int i, err, got_err = 0;
	int cnt = st->cnt;
	struct aiocb **cbs = st->cbs;

	for (;;) {
		for (i=0; i<cnt; i++) {
			if (!cbs[i]) continue;
			err = aio_error(cbs[i]);
			if (err==EINPROGRESS)
				break;
			if (err) got_err=1;
			cbs[i] = 0;
		}
		if (i==cnt) {
			if (got_err) {
				errno = EIO;
				return -1;
			}
			return 0;
		}
		if (aio_suspend((void *)cbs, cnt, 0))
			return -1;
	}
}

static void notify_signal(struct sigevent *sev)
{
#ifdef __wasilibc_unmodified_upstream
	siginfo_t si = {
		.si_signo = sev->sigev_signo,
		.si_value = sev->sigev_value,
		.si_code = SI_ASYNCIO,
		.si_pid = getpid(),
		.si_uid = getuid()
	};
	__syscall(SYS_rt_sigqueueinfo, si.si_pid, si.si_signo, &si);
#else
	/* firebox#5DB — faithful list-completion notification. wait_thread runs
	 * with all signals blocked (lio_listio sigfillsets before pthread_create),
	 * so delivery is routed process-directed to the application thread via the
	 * RT-signal FIFO (see __aio_notify_signal in aio.c). */
	__aio_notify_signal(sev->sigev_signo, sev->sigev_value);
#endif
}

static void *wait_thread(void *p)
{
	struct lio_state *st = p;
	struct sigevent *sev = st->sev;
	lio_wait(st);
	free(st);
	switch (sev->sigev_notify) {
	case SIGEV_SIGNAL:
		notify_signal(sev);
		break;
	case SIGEV_THREAD:
		sev->sigev_notify_function(sev->sigev_value);
		break;
	}
	return 0;
}

int lio_listio(int mode, struct aiocb *restrict const *restrict cbs, int cnt, struct sigevent *restrict sev)
{
	int i, ret;
	struct lio_state *st=0;

	if (cnt < 0) {
		errno = EINVAL;
		return -1;
	}

	/* POSIX mandates EINVAL for an improper mode ("[EINVAL] The mode argument is
	 * an improper value"). Upstream musl omits this check and falls through to the
	 * LIO_NOWAIT path for any non-LIO_WAIT value; validate explicitly so an invalid
	 * mode is rejected rather than silently treated as LIO_NOWAIT (lio_listio/18-1).
	 * (Invalid aio_lio_opcode remains musl-faithful `continue`/skip — POSIX leaves
	 * that unspecified, so it is a documented musl-vs-glibc divergence, not fixed here.) */
	if (mode != LIO_WAIT && mode != LIO_NOWAIT) {
		errno = EINVAL;
		return -1;
	}

	if (mode == LIO_WAIT || (sev && sev->sigev_notify != SIGEV_NONE)) {
		if (!(st = malloc(sizeof *st + cnt*sizeof *cbs))) {
			errno = EAGAIN;
			return -1;
		}
		st->cnt = cnt;
		st->sev = sev;
		memcpy(st->cbs, (void*) cbs, cnt*sizeof *cbs);
	}

	for (i=0; i<cnt; i++) {
		if (!cbs[i]) continue;
		switch (cbs[i]->aio_lio_opcode) {
		case LIO_READ:
			ret = aio_read(cbs[i]);
			break;
		case LIO_WRITE:
			ret = aio_write(cbs[i]);
			break;
		default:
			continue;
		}
		if (ret) {
			free(st);
			errno = EAGAIN;
			return -1;
		}
	}

	if (mode == LIO_WAIT) {
		ret = lio_wait(st);
		free(st);
		return ret;
	}

	if (st) {
		pthread_attr_t a;
		sigset_t set, set_old;
		pthread_t td;

		if (sev->sigev_notify == SIGEV_THREAD) {
			if (sev->sigev_notify_attributes)
				a = *sev->sigev_notify_attributes;
			else
				pthread_attr_init(&a);
		} else {
			pthread_attr_init(&a);
			pthread_attr_setstacksize(&a, PAGE_SIZE);
			pthread_attr_setguardsize(&a, 0);
		}
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
		sigfillset(&set);
		pthread_sigmask(SIG_BLOCK, &set, &set_old);
		if (__pthread_create(&td, &a, wait_thread, st)) {
			free(st);
			errno = EAGAIN;
			return -1;
		}
		pthread_sigmask(SIG_SETMASK, &set_old, 0);
	}

	return 0;
}

weak_alias(lio_listio, lio_listio64);
