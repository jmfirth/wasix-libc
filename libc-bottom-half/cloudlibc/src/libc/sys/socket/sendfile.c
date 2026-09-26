#include <errno.h>
#include <common/net.h>

#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

// Linux sendfile(2). `__wasi_sock_send_file` copies from an EXPLICIT position
// of `in_fd` and leaves `in_fd`'s cursor at that position plus the count it
// copied; it cannot tell the two forms of the call apart, so this wrapper does.
//
// firebox#QGY — `ofs == NULL`: read from, and advance, `in_fd`'s own position.
//   That is exactly the syscall's cursor contract, so the current position is
//   passed in and nothing is restored.
//
// `ofs != NULL`: read from `*ofs`, update `*ofs` by the count, and leave
//   `in_fd`'s position UNTOUCHED. This form used to do neither: `*ofs` never
//   moved, so a `sendfile(out, in, &off, n)` loop re-sent its first chunk
//   forever, and `in_fd`'s cursor was silently moved to `*ofs + count`. The
//   position is saved before the call and restored after it, success or not.
//   ⚠️ Not atomic against another thread moving the same file description
//   during the call; Linux's is.
ssize_t sendfile(int socket, int in_fd, off_t *__ofs, size_t __count) {
  __wasi_errno_t error;
  uint64_t count = (uint64_t)__count;
  uint64_t ofs;
  uint64_t saved_pos = 0;

  if (__ofs != NULL) {
    // Linux checks the explicit position before anything else about it: a
    // non-seekable in_fd is ESPIPE (`do_sendfile`), a negative one EINVAL.
    error = __wasi_fd_tell(in_fd, &saved_pos);
    if (error != 0) {
      errno = __wasilibc_errno_from_wasi(error);
      return -1;
    }
    if (*__ofs < 0) {
      errno = EINVAL;
      return -1;
    }
    ofs = (uint64_t)*__ofs;
  } else {
    error = __wasi_fd_tell(in_fd, &ofs);
    if (error != 0) {
      // A stream in_fd (pipe, socket, tty) has no position. Linux answers the
      // NULL form of that with EINVAL — in_fd does not support mmap-like
      // operations — and reserves ESPIPE for the explicit-offset form.
      errno = error == __WASI_ERRNO_SPIPE ? EINVAL : __wasilibc_errno_from_wasi(error);
      return -1;
    }
  }

  // Perform system call.
  uint64_t so_datalen = 0;
  error = __wasi_sock_send_file(socket, in_fd, ofs, count, &so_datalen);

  if (__ofs != NULL) {
    __wasi_filesize_t restored;
    (void)__wasi_fd_seek(in_fd, (__wasi_filedelta_t)saved_pos, __WASI_WHENCE_SET, &restored);
  }

  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }
  if (__ofs != NULL) {
    *__ofs += (off_t)so_datalen;
  }
  return (ssize_t)so_datalen;
}
