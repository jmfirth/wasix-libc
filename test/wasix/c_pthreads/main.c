
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
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

static void open_gate(int *gate);
static void wait_for_gate(int *gate);

/* A disabled-cancellation import seam: the import publishes `at_import` and
 * waits on `may_enter` before the real import, so the canceller can land the
 * request in the import's entry drain. `returns` counts real-import returns
 * and `first_returned` opens after the first, so the canceller makes data
 * available only after an interruption the libc did not restart. */
struct import_seam {
    int at_import, may_enter, first_returned;
    int calls, returns;
};
static _Thread_local struct import_seam *poll_seam;

__wasi_errno_t __wrap___wasi_poll_oneoff(const __wasi_subscription_t *in,
                                       __wasi_event_t *out, __wasi_size_t n,
                                       __wasi_size_t *nevents)
{
    if (expect_cancel_before_sleep) {
        sleep_entered_host_poll = 1;
        return __WASI_ERRNO_INTR;
    }
    struct import_seam *seam = poll_seam;
    if (seam && seam->calls++ == 0) {
        open_gate(&seam->at_import);
        wait_for_gate(&seam->may_enter);
    }
    __wasi_errno_t error = __real___wasi_poll_oneoff(in, out, n, nevents);
    if (seam && seam->returns++ == 0)
        open_gate(&seam->first_returned);
    return error;
}

__wasi_errno_t __real___wasi_fd_read(__wasi_fd_t, const __wasi_iovec_t *,
                                     size_t, __wasi_size_t *);
static _Thread_local struct import_seam *read_seam;

__wasi_errno_t __wrap___wasi_fd_read(__wasi_fd_t fd, const __wasi_iovec_t *iovs,
                                     size_t count, __wasi_size_t *nread)
{
    struct import_seam *seam = read_seam;
    if (seam && seam->calls++ == 0) {
        open_gate(&seam->at_import);
        if (!seam->may_enter)
            wait_for_gate(&seam->may_enter);
    }
    __wasi_errno_t error = __real___wasi_fd_read(fd, iovs, count, nread);
    if (seam && seam->returns++ == 0)
        open_gate(&seam->first_returned);
    return error;
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

struct directed_signal_test {
    sigset_t set;
    int armed, enter_host, dispatched, other_checked;
    int accepted;
};

static void open_gate(int *gate)
{
    __atomic_store_n(gate, 1, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(gate, 1);
}

static void wait_for_gate(int *gate)
{
    /* Use a raw Wasm wait so this gate does not dispatch host signals. */
    __builtin_wasm_memory_atomic_wait32(gate, 0, -1);
    require(__atomic_load_n(gate, __ATOMIC_ACQUIRE) == 1);
}

static void *directed_signal_target(void *arg)
{
    struct directed_signal_test *test = arg;
    open_gate(&test->armed);
    wait_for_gate(&test->enter_host);
    /* Let the host dispatch while SIGUSR1 is blocked and before sigwait. */
    require(sched_yield() == 0);
    open_gate(&test->dispatched);
    wait_for_gate(&test->other_checked);
    test->accepted = sigtimedwait(&test->set, NULL, &(struct timespec){0});
    return NULL;
}

static int directed_signal_stays_with_target(void)
{
    struct directed_signal_test test = {0};
    sigset_t saved;
    pthread_t target;
    require(sigemptyset(&test.set) == 0);
    require(sigaddset(&test.set, SIGUSR1) == 0);
    require(pthread_sigmask(SIG_BLOCK, &test.set, &saved) == 0);
    require(pthread_create(&target, NULL, directed_signal_target, &test) == 0);
    wait_for_gate(&test.armed);
    require(pthread_kill(target, SIGUSR1) == 0);
    open_gate(&test.enter_host);
    wait_for_gate(&test.dispatched);
    int received = sigtimedwait(&test.set, NULL, &(struct timespec){0});
    int no_signal = received == -1 && errno == EAGAIN;
    open_gate(&test.other_checked);
    require(pthread_join(target, NULL) == 0);
    require(pthread_sigmask(SIG_SETMASK, &saved, NULL) == 0);
    return no_signal && test.accepted == SIGUSR1;
}

struct process_signal_test {
    sigset_t set;
    int armed, enter_kill;
    int handler_calls_after_kill;
};
static volatile sig_atomic_t process_signal_calls;

static void record_process_signal(int signal)
{
    if (signal == SIGUSR2)
        process_signal_calls++;
}

static void *eligible_signal_target(void *arg)
{
    struct process_signal_test *test = arg;
    require(pthread_sigmask(SIG_UNBLOCK, &test->set, NULL) == 0);
    open_gate(&test->armed);
    wait_for_gate(&test->enter_kill);
    /* The next host entry is inside pthread_kill's temporary signal mask. */
    require(pthread_kill(pthread_self(), 0) == 0);
    test->handler_calls_after_kill = process_signal_calls;
    return NULL;
}

static int restore_drains_process_signal(void)
{
    struct process_signal_test test = {0};
    struct sigaction action = {.sa_handler = record_process_signal};
    struct sigaction saved_action;
    sigset_t saved_mask;
    pthread_t target;
    require(sigemptyset(&action.sa_mask) == 0);
    require(sigaction(SIGUSR2, &action, &saved_action) == 0);
    require(sigemptyset(&test.set) == 0);
    require(sigaddset(&test.set, SIGUSR2) == 0);
    require(pthread_sigmask(SIG_BLOCK, &test.set, &saved_mask) == 0);
    require(pthread_create(&target, NULL, eligible_signal_target, &test) == 0);
    wait_for_gate(&test.armed);
    require(kill(getpid(), SIGUSR2) == 0);
    open_gate(&test.enter_kill);
    require(pthread_join(target, NULL) == 0);
    require(sigaction(SIGUSR2, &saved_action, NULL) == 0);
    require(pthread_sigmask(SIG_SETMASK, &saved_mask, NULL) == 0);
    return test.handler_calls_after_kill == 1;
}

/* musl's SIGCANCEL handler is SA_RESTART and returns while cancellation is
 * disabled, so a restart-list call resumes: the read below returns the byte
 * written after its first interruption, whether the request lands in the
 * import's entry drain (`hold`) or while it waits. A handler that really ran
 * without SA_RESTART still interrupts it with EINTR; one with SA_RESTART
 * does not. */
static volatile sig_atomic_t interrupting_handler_calls;

static void interrupting_handler(int signal)
{
    if (signal == SIGUSR1)
        interrupting_handler_calls++;
}

struct disabled_read_test {
    struct import_seam seam;
    int fds[2];
    ssize_t n;
    int error;
};

static void *disabled_reader(void *arg)
{
    struct disabled_read_test *test = arg;
    char byte;
    require(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
    read_seam = &test->seam;
    test->n = read(test->fds[0], &byte, 1);
    test->error = test->n < 0 ? errno : 0;
    read_seam = NULL;
    require(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) == 0);
    pthread_testcancel();
    return NULL;
}

static void disabled_read(int hold, int with_handler, struct disabled_read_test *test)
{
    struct sigaction action = {
        .sa_handler = interrupting_handler,
        .sa_flags = with_handler == 2 ? SA_RESTART : 0,
    };
    struct sigaction saved_action;
    pthread_t reader;
    void *result;
    *test = (struct disabled_read_test){0};
    test->seam.may_enter = !hold;
    interrupting_handler_calls = 0;
    require(pipe(test->fds) == 0);
    require(sigemptyset(&action.sa_mask) == 0);
    require(sigaction(SIGUSR1, &action, &saved_action) == 0);
    require(pthread_create(&reader, NULL, disabled_reader, test) == 0);
    wait_for_gate(&test->seam.at_import);
    require(pthread_cancel(reader) == 0);
    if (with_handler)
        require(pthread_kill(reader, SIGUSR1) == 0);
    if (hold)
        open_gate(&test->seam.may_enter);
    wait_for_gate(&test->seam.first_returned);
    require(write(test->fds[1], "x", 1) == 1);
    require(pthread_join(reader, &result) == 0);
    require(result == PTHREAD_CANCELED);
    require(sigaction(SIGUSR1, &saved_action, NULL) == 0);
    require(close(test->fds[0]) == 0 && close(test->fds[1]) == 0);
}

static int disabled_read_restarts(int hold)
{
    struct disabled_read_test test;
    disabled_read(hold, 0, &test);
    return test.n == 1 && test.seam.calls == 2;
}

static int handler_interrupts_disabled_read(void)
{
    struct disabled_read_test test;
    disabled_read(1, 1, &test);
    return test.n == -1 && test.error == EINTR && test.seam.calls == 1 &&
           interrupting_handler_calls == 1;
}

static int restart_handler_keeps_disabled_read_restarting(void)
{
    struct disabled_read_test test;
    disabled_read(1, 2, &test);
    return test.n == 1 && test.seam.calls == 2 && interrupting_handler_calls == 1;
}

/* nanosleep is never restarted (signal(7)); with cancellation disabled musl
 * returns EINTR for the nudge, and so must this libc. */
struct disabled_sleep_test {
    struct import_seam seam;
    int rc, error;
};

static void *disabled_sleeper(void *arg)
{
    struct disabled_sleep_test *test = arg;
    require(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
    poll_seam = &test->seam;
    test->rc = nanosleep(&(struct timespec){.tv_sec = 120}, NULL);
    test->error = test->rc < 0 ? errno : 0;
    poll_seam = NULL;
    require(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) == 0);
    pthread_testcancel();
    return NULL;
}

static int disabled_sleep_interrupts(void)
{
    struct disabled_sleep_test test = {0};
    pthread_t sleeper;
    void *result;
    require(pthread_create(&sleeper, NULL, disabled_sleeper, &test) == 0);
    wait_for_gate(&test.seam.at_import);
    require(pthread_cancel(sleeper) == 0);
    open_gate(&test.seam.may_enter);
    require(pthread_join(sleeper, &result) == 0);
    require(result == PTHREAD_CANCELED);
    return test.rc == -1 && test.error == EINTR && test.seam.calls == 1;
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
    int directed_correct = directed_signal_stays_with_target();
    int restore_correct = restore_drains_process_signal();
    int entry_restart_correct = disabled_read_restarts(1);
    int wait_restart_correct = disabled_read_restarts(0);
    int handler_eintr_correct = handler_interrupts_disabled_read();
    int restart_handler_correct = restart_handler_keeps_disabled_read_restarting();
    int sleep_eintr_correct = disabled_sleep_interrupts();

    if (!cancel_correct)
        fputs("pending condvar cancellation did not unwind\n", stderr);
    if (!mutex_timeout_correct)
        fputs("timed mutex wait did not honor its absolute deadline\n", stderr);
    if (!sleep_correct)
        fputs("clock sleep parked with cancellation already pending\n", stderr);
    if (!directed_correct)
        fputs("a different thread could consume a directed signal\n", stderr);
    if (!restore_correct)
        fputs("pthread_kill did not drain the signal when it restored the mask\n", stderr);
    if (!entry_restart_correct)
        fputs("disabled-cancellation read did not restart after an entry-drained nudge\n", stderr);
    if (!wait_restart_correct)
        fputs("disabled-cancellation read did not restart after a nudge during the wait\n", stderr);
    if (!handler_eintr_correct)
        fputs("a non-SA_RESTART handler did not interrupt a disabled-cancellation read\n", stderr);
    if (!restart_handler_correct)
        fputs("an SA_RESTART handler stopped a disabled-cancellation read from restarting\n", stderr);
    if (!sleep_eintr_correct)
        fputs("disabled-cancellation nanosleep did not return EINTR for the nudge\n", stderr);
    if (!cancel_correct || !mutex_timeout_correct || !sleep_correct ||
        !directed_correct || !restore_correct || !entry_restart_correct ||
        !wait_restart_correct || !handler_eintr_correct || !restart_handler_correct ||
        !sleep_eintr_correct)
        return 1;

    fprintf(stdout, "threads joined\n");

    return 0;
}
