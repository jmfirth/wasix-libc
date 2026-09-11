// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <wasi/libc.h>

int posix_fallocate(int fd, off_t offset, off_t len) {
  if (offset < 0 || len < 0)
    return EINVAL;
  /* firebox#87F: like posix_fadvise, this reports through the return value.
     The EINVAL above is guest space and stays; this is host space and converts. */
  return __wasilibc_errno_from_wasi(__wasi_fd_allocate(fd, offset, len));
}
