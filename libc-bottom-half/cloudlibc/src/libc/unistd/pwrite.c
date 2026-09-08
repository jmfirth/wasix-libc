// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <wasi/api.h>
#include <errno.h>
#include <unistd.h>
#include <wasi/libc.h>

ssize_t pwrite(int fildes, const void *buf, size_t nbyte, off_t offset) {
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }
  __wasi_ciovec_t iov = {.buf = buf, .buf_len = nbyte};
  __wasi_size_t bytes_written;
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  __wasi_errno_t error =
      __wasi_fd_pwrite(fildes, &iov, 1, offset, &bytes_written);
  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    // firebox#87F — ONE VARIABLE CANNOT INHABIT TWO NUMBERING SPACES.
    // `error` is the host's, and stays the host's. The diagnosis below does not
    // reinterpret it, it CHOOSES a guest errno, so that choice lands in a
    // separate guest-space variable and the host code is translated only on the
    // path where nothing was chosen. Assigning EBADF back into `error` and
    // translating once at the bottom -- which is what this did -- reads as
    // correct today only because the two spaces are still the same numbers; the
    // moment they are not, it translates a guest constant a second time and
    // hands the caller an unrelated errno with nothing to flag it.
    __wasi_fdstat_t fds;
    int guest_error = 0;
    if (error == __WASI_ERRNO_NOTCAPABLE && __wasi_fd_fdstat_get(fildes, &fds) == 0) {
      // Determine why the host refused the capability.
      if ((fds.fs_rights_base & __WASI_RIGHTS_FD_WRITE) == 0)
        guest_error = EBADF;
      else
        guest_error = ESPIPE;
    }
    errno = guest_error != 0 ? guest_error : __wasilibc_errno_from_wasi(error);
    return -1;
  }
  return bytes_written;
}
