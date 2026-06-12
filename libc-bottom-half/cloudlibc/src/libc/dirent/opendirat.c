// Copyright (c) 2015 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <wasi/libc.h>
#include <wasi/libc-nocwd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

DIR *__wasilibc_nocwd_opendirat(int dir, const char *dirname) {
  // Open directory. O_CLOEXEC matches musl/glibc opendir: a DIR*'s fd must
  // not leak into spawned children (firebox#ENM finding C6); openat routes
  // it atomically through path_open2's fdflagsext.
  int fd = __wasilibc_nocwd_openat_nomode(
      dir, dirname, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC);
  if (fd == -1)
    return NULL;

  // Create directory handle.
  DIR *result = fdopendir(fd);
  if (result == NULL)
    close(fd);
  return result;
}
