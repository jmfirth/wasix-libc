
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wasi/api.h>

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex;
static _Thread_local int cancel_at_unlock;
static _Thread_local int simulate_mutex_timeout;
static _Thread_local int timed_futex_calls;
static _Thread_local int expect_cancel_before_sleep;
static int cancel_published;
static int cleaned_up;
static int mutex_timeout_correct;
static int sleep_entered_host_poll;

static void require(int success)
{
    if (!success) abort();
}

int __real___pthread_mutex_unlock(pthread_mutex_t *);

int __wrap___pthread_mutex_unlock(pthread_mutex_t *m)
{
    int rc = __real___pthread_mutex_unlock(m);
    if (cancel_at_unlock && m == &mutex) {
        cancel_at_unlock = 0;
        require(rc == 0);
        /* cond_timedwait has passed its entry cancellation check and
         * released its mutex, but has yet to enter the futex wait. */
        require(pthread_cancel(pthread_self()) == 0);
        cancel_published = 1;
    }
    return rc;
}

static void cleanup_cond_wait(void *arg)
{
    /* The error-checking mutex verifies that cancellation reacquired it. */
    require(pthread_mutex_unlock(arg) == 0);
    cleaned_up = 1;
}

static void *pending_cancel_at_cond_wait(void *arg)
{
    (void)arg;
    require(pthread_mutex_lock(&mutex) == 0);
    struct timespec deadline;
    require(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec++;
    pthread_cleanup_push(cleanup_cond_wait, &mutex);
    cancel_at_unlock = 1;
    pthread_cond_timedwait(&cond, &mutex, &deadline);
    pthread_cleanup_pop(1);
    return NULL;
}

int __real___wasilibc_futex_wait_wasix(volatile void *, int, int, int64_t);
int __real___clock_gettime(clockid_t, struct timespec *);

int __wrap___clock_gettime(clockid_t clock, struct timespec *now)
{
    if (simulate_mutex_timeout) {
        require(clock == CLOCK_REALTIME);
        *now = (struct timespec){.tv_sec = timed_futex_calls >= 2 ? 2 : 1};
        return 0;
    }
    return __real___clock_gettime(clock, now);
}

int __wrap___wasilibc_futex_wait_wasix(volatile void *addr, int op, int val,
                                      int64_t max_wait_ns)
{
    if (simulate_mutex_timeout && max_wait_ns > 0) {
        /* The first expiry is early. The second reaches the deadline. */
        require(max_wait_ns == 1000000000);
        timed_futex_calls++;
        return -ETIMEDOUT;
    }
    return __real___wasilibc_futex_wait_wasix(addr, op, val, max_wait_ns);
}

static void *timed_mutex_wait(void *arg)
{
    pthread_mutex_t *held = arg;
    struct timespec deadline = {.tv_sec = 2};
    simulate_mutex_timeout = 1;
    int rc = pthread_mutex_timedlock(held, &deadline);
    simulate_mutex_timeout = 0;
    mutex_timeout_correct = rc == ETIMEDOUT && timed_futex_calls == 2;
    return NULL;
}

__wasi_errno_t __real___wasi_poll_oneoff(const __wasi_subscription_t *,
                                       __wasi_event_t *, __wasi_size_t, __wasi_size_t *);

__wasi_errno_t __wrap___wasi_poll_oneoff(const __wasi_subscription_t *in,
                                       __wasi_event_t *out, __wasi_size_t n,
                                       __wasi_size_t *nevents)
{
    if (expect_cancel_before_sleep) {
        sleep_entered_host_poll = 1;
        return __WASI_ERRNO_INTR;
    }
    return __real___wasi_poll_oneoff(in, out, n, nevents);
}

static void *pending_cancel_at_sleep(void *arg)
{
    (void)arg;
    const struct timespec delay = {.tv_sec = 120};
    require(pthread_cancel(pthread_self()) == 0);
    expect_cancel_before_sleep = 1;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
    return NULL;
}

void *thread(void *ptr)
{
    long type = (long)ptr;
    fprintf(stdout,"thread %ld\n", type);
    return  ptr;
}

int main(int argc, char **argv)
{
    pthread_t thread1, thread2;
    intptr_t thr = 1;
    intptr_t thr2 = 2;
    pthread_create(&thread1, NULL, *thread, (void*)thr);
    pthread_create(&thread2, NULL, *thread, (void*)thr2);

    pthread_join(thread1,NULL);
    pthread_join(thread2,NULL);

    pthread_mutexattr_t attr;
    require(pthread_mutexattr_init(&attr) == 0);
    require(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) == 0);
    require(pthread_mutex_init(&mutex, &attr) == 0);
    require(pthread_mutexattr_destroy(&attr) == 0);

    pthread_t waiter;
    void *result;
    require(pthread_create(&waiter, NULL, pending_cancel_at_cond_wait, NULL) == 0);
    require(pthread_join(waiter, &result) == 0);
    int cancel_correct = result == PTHREAD_CANCELED && cancel_published && cleaned_up;
    require(pthread_cond_destroy(&cond) == 0);

    require(pthread_mutex_lock(&mutex) == 0);
    require(pthread_create(&waiter, NULL, timed_mutex_wait, &mutex) == 0);
    require(pthread_join(waiter, NULL) == 0);
    require(pthread_mutex_unlock(&mutex) == 0);
    require(pthread_mutex_destroy(&mutex) == 0);

    require(pthread_create(&waiter, NULL, pending_cancel_at_sleep, NULL) == 0);
    require(pthread_join(waiter, &result) == 0);
    int sleep_correct = result == PTHREAD_CANCELED && !sleep_entered_host_poll;

    if (!cancel_correct)
        fputs("pending condvar cancellation did not unwind\n", stderr);
    if (!mutex_timeout_correct)
        fputs("timed mutex wait did not honor its absolute deadline\n", stderr);
    if (!sleep_correct)
        fputs("clock sleep parked with cancellation already pending\n", stderr);
    if (!cancel_correct || !mutex_timeout_correct || !sleep_correct)
        return 1;

    fprintf(stdout, "threads joined\n");

    return 0;
}
