#include <wasi/api.h>
#include <wasi/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int dup3(int fd, int newfd, int flags) {
    // POSIX/Linux: unlike dup2, dup3(fd, fd, ...) is an error.
    if (fd == newfd) {
        errno = EINVAL;
        return -1;
    }
    // Linux rejects any flag other than O_CLOEXEC.
    if (flags & ~O_CLOEXEC) {
        errno = EINVAL;
        return -1;
    }
    __wasi_errno_t error = __wasi_fd_renumber(fd, newfd);
    if (error != 0) {
        errno = error;
        return -1;
    }
    // The flags parameter was previously ignored entirely:
    // dup3(fd, newfd, O_CLOEXEC) returned a non-CLOEXEC fd, because
    // fd_renumber clears the bit (correct dup2 semantics) and nothing set
    // it afterwards — the duplicate then leaked into every spawned child
    // (firebox#ENM finding C3).
    if (flags & O_CLOEXEC) {
        error = __wasi_fd_fdflags_set(newfd, __WASI_FDFLAGSEXT_CLOEXEC);
        if (error != 0) {
            errno = error;
            return -1;
        }
    }
    return newfd;
}
