// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <wasi/api.h>
#include <errno.h>
#include <unistd.h>

int close(int fildes) {
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  __wasi_errno_t error = __wasi_fd_close(fildes);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}
