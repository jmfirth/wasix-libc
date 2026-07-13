// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/stat.h>

#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "stat_impl.h"

int __wasilibc_nocwd_fstatat(int fd, const char *restrict path, struct stat *restrict buf,
                             int flag) {
  // POSIX: reject unknown AT_* bits with EINVAL. This is the fstatat
  // callsite in the AT_* hygiene sweep from issue #24 patch J. The
  // only flag fstatat honors is AT_SYMLINK_NOFOLLOW.
  if ((flag & ~AT_SYMLINK_NOFOLLOW) != 0) {
    errno = EINVAL;
    return -1;
  }
  errno = 0;
  // Create lookup properties.
  __wasi_lookupflags_t lookup_flags = 0;
  if ((flag & AT_SYMLINK_NOFOLLOW) == 0)
    lookup_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

  // Perform system call.
  __wasi_filestat_t internal_stat;
  __wasi_errno_t error =
      __wasi_path_filestat_get(fd, lookup_flags, path, &internal_stat);
  if (error != 0) {
    errno = error;
    return -1;
  }
  to_public_stat(&internal_stat, buf);
  // firebox#5T3 — read the owner back through the Firebox report channel
  // (WASI's __wasi_filestat_t has no uid/gid) so stat/lstat/fstatat reflect a
  // prior chown/lchown (firebox#2E2). Reuses the SAME (fd, lookup_flags, path)
  // as the filestat_get above, so the host resolves the identical inode —
  // lookup_flags selects follow (stat) vs nofollow (lstat). A failure leaves
  // st_uid/st_gid at 0 (default root owner); the stat itself already succeeded.
  uint32_t __fbx_owner[2];
  if (__wasix_path_filestat_get_ext(fd, lookup_flags, path, strlen(path),
                                    __fbx_owner) == 0) {
    buf->st_uid = __fbx_owner[0];
    buf->st_gid = __fbx_owner[1];
  }
  return 0;
}
