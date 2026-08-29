// Copyright (c) 2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

_Static_assert(AT_EACCESS != 0,
               "AT_EACCESS must remain distinguishable from flags == 0");

int __wasilibc_nocwd_faccessat(int fd, const char *path, int amode, int flag) {
  if ((amode & ~(F_OK | R_OK | W_OK | X_OK)) != 0 ||
      (flag & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) != 0) {
    errno = EINVAL;
    return -1;
  }
  // Linux rejects an empty pathname unless AT_EMPTY_PATH is supplied. Firebox
  // does not expose that optional flag, so an empty path is always ENOENT.
  // Reject it here as well as in the host import so real dirfds cannot turn the
  // resolver's internal empty-component base case into a false success.
  if (path[0] == '\0') {
    errno = ENOENT;
    return -1;
  }

  __wasi_errno_t error =
      __wasix_path_access(fd, path, strlen(path), amode, flag);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}
