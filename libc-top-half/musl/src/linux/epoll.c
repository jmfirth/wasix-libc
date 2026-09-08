#include <wasi/api.h>
#include <sys/epoll.h>
#include <signal.h>
#include <pthread.h>   /* firebox#B28 — pthread_sigmask, for epoll_pwait */
#include <errno.h>
#include <wasi/libc.h>

int epoll_create(int size)
{
    int ret_val = 0;
    int error = __wasi_epoll_create(&ret_val);
    if (error == 0)
    {
        return ret_val;
    }
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
}

int epoll_create1(int flags)
{
    return epoll_create(0);
}

/* firebox#XBX — epoll_ctl RETURNED A RAW WASI ERRNO, NOT -1 WITH errno SET.
 *
 * `__wasi_epoll_ctl` returns a `__wasi_errno_t`, and both `return` statements
 * below handed it straight back to the guest. Linux's epoll_ctl returns 0 or
 * -1 with errno set, so `if (epoll_ctl(...) < 0)` -- the way every caller
 * writes it -- saw SUCCESS on every failure, and the failure left errno
 * untouched, so even a caller that checked `!= 0` had nothing to report. A
 * fail-open where broken state is indistinguishable from working state;
 * invariant 3 admits no deferral for that.
 *
 * The translation is not decorative even though it is the identity today. The
 * naive `errno = err; return -1;` would be accidentally right until #87F step
 * 4 renumbers the guest side, and would then silently hand back a plausible
 * wrong errno with nothing to flag it -- a bad fd reporting the guest meaning
 * of WASI 8 rather than EBADF. Written with the translation, it is correct in
 * both numbering worlds.
 *
 * Checked against the whole file, since the class is the unit: epoll_create
 * (:8) and epoll_pwait (:57) already translate and return -1, and
 * epoll_create1/epoll_wait delegate to them, so epoll_ctl was the only carrier.
 */
static inline int __fbx_epoll_ctl_finish(__wasi_errno_t error)
{
    if (error != 0)
    {
        errno = __wasilibc_errno_from_wasi(error);
        return -1;
    }
    return 0;
}

int epoll_ctl(int fd, int op, int fd2, struct epoll_event *ev)
{
    if (ev)
    {
        struct __wasi_epoll_event_t ev2;

        ev2.events = 0;
        if ((ev->events & EPOLLIN) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLIN;
        if ((ev->events & EPOLLOUT) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLOUT;
        if ((ev->events & EPOLLRDHUP) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLRDHUP;
        if ((ev->events & EPOLLPRI) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLPRI;
        if ((ev->events & EPOLLERR) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLERR;
        if ((ev->events & EPOLLHUP) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLHUP;
        if ((ev->events & EPOLLET) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLET;
        if ((ev->events & EPOLLONESHOT) != 0)
            ev2.events |= __WASI_EPOLL_TYPE_EPOLLONESHOT;
        ev2.data.ptr = (__wasi_pointersize_t)ev->data.ptr;
        ev2.data.fd = fd2;
        ev2.data.data1 = ev->data.u32;
        ev2.data.data2 = ev->data.u64;
        return __fbx_epoll_ctl_finish(__wasi_epoll_ctl(fd, op, fd2, &ev2));
    }
    return __fbx_epoll_ctl_finish(__wasi_epoll_ctl(fd, op, fd2, NULL));
}

int epoll_pwait(int fd, struct epoll_event *ev, int cnt, int to, const sigset_t *sigs)
{
    /* firebox#B28 — APPLY THE CALLER'S SIGMASK AROUND THE WAIT.
     *
     * Second carrier of pselect's fail-open. `(void)sigs;` accepted the
     * argument and discarded it, so `epoll_pwait`'s entire reason to exist over
     * `epoll_wait` — atomically unblocking a signal for the duration of the
     * wait, so the canonical "block SIGCHLD, test a flag, then wait" pattern
     * has no race window — silently did nothing. There is no error and no
     * symptom at the call site: the broken state is indistinguishable from the
     * working one, which is why an explicit `(void)` cast survived here.
     * Invariant 3 admits no deferral for a fail-open; invariant 1 says fix the
     * class, and this is the same class as pselect.c.
     *
     * Same save/set/wait/restore shape as pselect.c — see that file for the
     * measurement and the pthread_sigmask drain semantics that make a signal
     * pending outside the wait get dispatched as soon as the mask is
     * installed. */
    sigset_t saved_mask;
    int mask_applied = 0;
    if (sigs != NULL) {
        if (pthread_sigmask(SIG_SETMASK, sigs, &saved_mask) == 0) {
            mask_applied = 1;
        }
    }
    __wasi_size_t ret_val;
    struct __wasi_epoll_event_t ev2[64];
    __wasi_timestamp_t timeout;
    if (to < 0) {
        timeout = 0xffffffffffffffff;
    } else {
        timeout = (__wasi_timestamp_t)to * 1000000;
    }
    if (cnt > 64)
    {
        cnt = 64;
    }
    int error = __wasi_epoll_wait(fd, &ev2[0], cnt, timeout, &ret_val);
    /* firebox#B28 — restore before touching errno below: pthread_sigmask's
     * SIG_SETMASK drain can dispatch a handler, and a handler may clobber
     * errno. Placed here so EVERY return path below is covered by one
     * restore. */
    if (mask_applied) {
        pthread_sigmask(SIG_SETMASK, &saved_mask, NULL);
    }
    if (error == 0)
    {
        cnt = ret_val;
        for (int c = 0; c < cnt; c++)
        {
            ev[c].events = 0;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLIN) != 0)
                ev[c].events |= EPOLLIN;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLOUT) != 0)
                ev[c].events |= EPOLLOUT;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLRDHUP) != 0)
                ev[c].events |= EPOLLRDHUP;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLPRI) != 0)
                ev[c].events |= EPOLLPRI;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLERR) != 0)
                ev[c].events |= EPOLLERR;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLHUP) != 0)
                ev[c].events |= EPOLLHUP;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLET) != 0)
                ev[c].events |= EPOLLET;
            if ((ev2[c].events & __WASI_EPOLL_TYPE_EPOLLONESHOT) != 0)
                ev[c].events |= EPOLLONESHOT;
            if (ev2[c].data.ptr)
            {
                ev[c].data.ptr = (void *)ev2[c].data.ptr;
            }
            else if (ev2[c].data.fd)
            {
                ev[c].data.fd = ev2[c].data.fd;
            }
            else if (ev2[c].data.data1)
            {
                ev[c].data.u32 = ev2[c].data.data1;
            }
            else
            {
                ev[c].data.u64 = ev2[c].data.data2;
            }
        }
        return (int)ret_val;
    }
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
}

int epoll_wait(int fd, struct epoll_event *ev, int cnt, int to)
{
    return epoll_pwait(fd, ev, cnt, to, 0);
}
