// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <wasi/api.h>
#include <errno.h>

ssize_t preadv(int fildes, const struct iovec *iov, int iovcnt, off_t offset) {
  if (iovcnt < 0 || offset < 0) {
    errno = EINVAL;
    return -1;
  }
  __wasi_size_t bytes_read;
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  __wasi_errno_t error = __wasi_fd_pread(
      fildes, (const __wasi_iovec_t *)iov, iovcnt, offset, &bytes_read);
  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    errno = error;
    return -1;
  }
  return bytes_read;
}
