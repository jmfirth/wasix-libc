
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int check_process_shared(void)
{
    pthread_condattr_t cond;
    pthread_mutexattr_t mutex;
    pthread_barrierattr_t barrier;
    pthread_rwlockattr_t rwlock;
    pthread_spinlock_t spin;
    long feature = sysconf(_SC_THREAD_PROCESS_SHARED);
    int expected = feature < 0 ? ENOTSUP : 0;

    if (pthread_condattr_init(&cond) || pthread_mutexattr_init(&mutex)
        || pthread_barrierattr_init(&barrier) || pthread_rwlockattr_init(&rwlock))
        return 1;

    const int shared[] = {
        pthread_condattr_setpshared(&cond, PTHREAD_PROCESS_SHARED),
        pthread_mutexattr_setpshared(&mutex, PTHREAD_PROCESS_SHARED),
        pthread_barrierattr_setpshared(&barrier, PTHREAD_PROCESS_SHARED),
        pthread_rwlockattr_setpshared(&rwlock, PTHREAD_PROCESS_SHARED),
        pthread_spin_init(&spin, PTHREAD_PROCESS_SHARED),
    };
    printf("process-shared header=%ld sysconf=%ld cond=%d mutex=%d barrier=%d rwlock=%d spin=%d\n",
           (long)_POSIX_THREAD_PROCESS_SHARED, feature,
           shared[0], shared[1], shared[2], shared[3], shared[4]);
    if (shared[4] == 0)
        pthread_spin_destroy(&spin);
    for (size_t i = 0; i < sizeof(shared) / sizeof(shared[0]); ++i)
        if (shared[i] != expected)
            return 1;
#if defined(__wasi__) && !defined(__wasilibc_unmodified_upstream)
    if (_POSIX_THREAD_PROCESS_SHARED != 0)
        return 1;
#endif
    if (pthread_condattr_setpshared(&cond, PTHREAD_PROCESS_PRIVATE)
        || pthread_mutexattr_setpshared(&mutex, PTHREAD_PROCESS_PRIVATE)
        || pthread_barrierattr_setpshared(&barrier, PTHREAD_PROCESS_PRIVATE)
        || pthread_rwlockattr_setpshared(&rwlock, PTHREAD_PROCESS_PRIVATE)
        || pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE))
        return 1;
    pthread_spin_destroy(&spin);
    pthread_condattr_destroy(&cond);
    pthread_mutexattr_destroy(&mutex);
    pthread_barrierattr_destroy(&barrier);
    pthread_rwlockattr_destroy(&rwlock);
    return 0;
}

void *thread(void *ptr)
{
    long type = (long)ptr;
    fprintf(stdout,"thread %ld\n", type);
    return  ptr;
}

int main(int argc, char **argv)
{
    int pshared = check_process_shared();
    if (pshared || (argc == 2 && !strcmp(argv[1], "--check-process-shared")))
        return pshared;

    pthread_t thread1, thread2;
    int thr = 1;
    int thr2 = 2;
    pthread_create(&thread1, NULL, *thread, (void*)thr);
    pthread_create(&thread2, NULL, *thread, (void*)thr2);

    pthread_join(thread1,NULL);
    pthread_join(thread2,NULL);

    fprintf(stdout, "threads joined\n");

    return 0;
}
