// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/stat.h>

#include <wasi/api.h>
#include <errno.h>

#include "stat_impl.h"

int fstat(int fildes, struct stat *buf) {
  __wasi_filestat_t internal_stat;
  __wasi_errno_t error = __wasi_fd_filestat_get(fildes, &internal_stat);
  if (error != 0) {
    errno = error;
    return -1;
  }
  to_public_stat(&internal_stat, buf);
  // firebox#5T3 — WASI's __wasi_filestat_t carries no owner, so to_public_stat
  // left st_uid/st_gid at 0. Read the stored owner back through the Firebox
  // report channel so fstat reflects a prior fchown/chown (firebox#2E2). A
  // failure (older runtime, backing-less fd) leaves the fields at 0 (the
  // default root owner) — the stat itself already succeeded.
  uint32_t __fbx_owner[2];
  if (__wasix_fd_filestat_get_ext(fildes, __fbx_owner) == 0) {
    buf->st_uid = __fbx_owner[0];
    buf->st_gid = __fbx_owner[1];
  }
  return 0;
}
