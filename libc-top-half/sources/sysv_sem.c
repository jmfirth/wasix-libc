#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/sem.h>
#include <time.h>
#include <wasi/libc.h>

#ifdef __wasm64__
#define WASIX_NAMESPACE "wasix_64v1"
#else
#define WASIX_NAMESPACE "wasix_32v1"
#endif

/* Fixed-width wire layout, independent of the musl header's padding. */
struct sem_metadata {
    int64_t otime, ctime;
    int32_t key;
    uint32_t count, uid, gid, cuid, cgid, mode, sequence;
};

_Static_assert(sizeof(struct sem_metadata) == 48, "WASIX semaphore metadata");
_Static_assert(sizeof(struct sembuf) == 6 && offsetof(struct sembuf, sem_flg) == 4,
    "WASIX semaphore operation");
_Static_assert(sizeof(struct seminfo) == 40, "WASIX semaphore limits");

static int fail(int error)
{
    errno = error;
    return -1;
}

int32_t __imported_sysv_sem_get(int32_t, int32_t, int32_t, int32_t *)
    __attribute__((__import_module__(WASIX_NAMESPACE), __import_name__("sysv_sem_get")));
int32_t __imported_sysv_sem_ctl(int32_t, int32_t, int32_t, int32_t, void *, int32_t *)
    __attribute__((__import_module__(WASIX_NAMESPACE), __import_name__("sysv_sem_ctl")));
int32_t __imported_sysv_sem_op(int32_t, const struct sembuf *, size_t, int64_t)
    __attribute__((__import_module__(WASIX_NAMESPACE), __import_name__("sysv_sem_op")));

int semget(key_t key, int count, int flags)
{
    int32_t result;
    int error = __imported_sysv_sem_get(key, count, flags, &result);
    return error ? fail(__wasilibc_errno_from_wasi(error)) : result;
}

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

int semctl(int id, int num, int cmd, ...)
{
    union semun arg = {0};
    va_list ap;
    switch (cmd) {
    case SETVAL: case GETALL: case SETALL:
    case IPC_SET: case IPC_STAT: case SEM_STAT: case SEM_STAT_ANY:
    case IPC_INFO: case SEM_INFO:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
    }

    struct sem_metadata metadata = {0};
    void *data = NULL;
    int value = 0;
    switch (cmd) {
    case SETVAL:
        value = arg.val;
        break;
    case GETALL: case SETALL:
        data = arg.array;
        break;
    case IPC_INFO: case SEM_INFO:
        data = arg.__buf;
        break;
    case IPC_SET:
        if (!arg.buf) return fail(EFAULT);
        metadata.uid = arg.buf->sem_perm.uid;
        metadata.gid = arg.buf->sem_perm.gid;
        metadata.mode = arg.buf->sem_perm.mode;
        data = &metadata;
        break;
    case IPC_STAT: case SEM_STAT: case SEM_STAT_ANY:
        if (!arg.buf) return fail(EFAULT);
        data = &metadata;
        break;
    }

    int32_t result;
    int error = __imported_sysv_sem_ctl(id, num, cmd, value, data, &result);
    if (error) return fail(__wasilibc_errno_from_wasi(error));
    switch (cmd) {
    case IPC_STAT: case SEM_STAT: case SEM_STAT_ANY:
        *arg.buf = (struct semid_ds) {
            .sem_perm = {
                .__ipc_perm_key = metadata.key,
                .uid = metadata.uid, .gid = metadata.gid,
                .cuid = metadata.cuid, .cgid = metadata.cgid,
                .mode = metadata.mode,
                .__ipc_perm_seq = metadata.sequence,
            },
            .sem_otime = metadata.otime, .sem_ctime = metadata.ctime,
            .sem_nsems = metadata.count,
        };
        break;
    }
    return result;
}

int semop(int id, struct sembuf *ops, size_t count)
{
    int error = __imported_sysv_sem_op(id, ops, count, -1);
    return error ? fail(__wasilibc_errno_from_wasi(error)) : 0;
}

int semtimedop(int id, struct sembuf *ops, size_t count, const struct timespec *timeout)
{
    if (!timeout) return semop(id, ops, count);
    const int64_t billion = 1000000000;
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= billion)
        return fail(EINVAL);

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
    int64_t start = now.tv_sec * billion + now.tv_nsec;
    int64_t deadline = INT64_MAX;
    if (timeout->tv_sec <= (INT64_MAX - start - timeout->tv_nsec) / billion)
        deadline = start + timeout->tv_sec * billion + timeout->tv_nsec;

    int error = __imported_sysv_sem_op(id, ops, count, deadline);
    return error ? fail(__wasilibc_errno_from_wasi(error)) : 0;
}
