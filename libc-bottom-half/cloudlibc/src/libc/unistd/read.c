// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <wasi/api.h>
#include <errno.h>
#include <unistd.h>

ssize_t read(int fildes, void *buf, size_t nbyte) {
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  __wasi_iovec_t iov = {.buf = buf, .buf_len = nbyte};
  __wasi_size_t bytes_read;
  __wasi_errno_t error = __wasi_fd_read(fildes, &iov, 1, &bytes_read);
  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's own `__syscall_cp_c` rule) so a COMPLETED call never
    // discards bytes it already consumed. Never returns if a cancel is
    // pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    errno = error == ENOTCAPABLE ? EBADF : error;
    return -1;
  }
  return bytes_read;
}
