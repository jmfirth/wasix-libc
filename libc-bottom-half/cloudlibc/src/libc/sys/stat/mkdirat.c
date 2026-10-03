// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/stat.h>

#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

int __wasilibc_nocwd_mkdirat_nomode(int fd, const char *path) {
  __wasi_errno_t error = __wasi_path_create_directory(fd, path);
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }
  return 0;
}

// firebox#DNG/#J04 — mkdir(2)/mkdirat(2) with their mode argument: the runtime
// creates the directory at `mode & ~umask` in one step, instead of at a default
// that was then chmod-ed (a window in which it was visible wider than asked).
int __wasilibc_nocwd_mkdirat_mode(int fd, const char *path, mode_t mode) {
  __wasi_errno_t error =
      __wasix_path_create_directory_mode(fd, path, strlen(path), (uint32_t)mode);
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }
  return 0;
}
