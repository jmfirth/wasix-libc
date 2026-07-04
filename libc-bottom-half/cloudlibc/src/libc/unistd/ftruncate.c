// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <wasi/api.h>
#include <errno.h>
#include <unistd.h>

int ftruncate(int fildes, off_t length) {
  if (length < 0) {
    errno = EINVAL;
    return -1;
  }
  __wasi_filesize_t st_size = length;
  __wasi_errno_t error =
      __wasi_fd_filestat_set_size(fildes, st_size);
  if (error != 0) {
    // firebox#ZDN: faithful POSIX errno for "ftruncate on a fd not open for
    // writing". POSIX/Linux/glibc mandate EINVAL here (do_sys_ftruncate:
    // `if (!(f.file->f_mode & FMODE_WRITE)) return -EINVAL`), and EACCES is NOT
    // a valid ftruncate(2) errno on Linux at all. On WASI the write capability
    // is expressed as the descriptor right __WASI_RIGHTS_FD_FILESTAT_SET_SIZE,
    // which openat() deliberately WITHHOLDS from an O_RDONLY open (see
    // libc-bottom-half/.../fcntl/openat.c rights map). The host therefore
    // rejects the set_size with __WASI_ERRNO_ACCES / __WASI_ERRNO_NOTCAPABLE.
    // Translate that capability-withheld rejection into the POSIX-faithful
    // EINVAL — but ONLY when the fd genuinely lacks FD_WRITE, so any other
    // ACCES cause (or any other errno) surfaces unchanged. This is the same
    // "derive the POSIX errno from the descriptor's rights" discipline as the
    // firebox#TTB mmap(2) precondition checks in libc-bottom-half/mman/mman.c.
    // shm_open/13-1 and shm_open/20-1 assert it: an O_RDONLY shm object's
    // ftruncate must fail EINVAL, proving the object was not opened writable.
    if (error == __WASI_ERRNO_ACCES || error == __WASI_ERRNO_NOTCAPABLE) {
      __wasi_fdstat_t fds;
      if (__wasi_fd_fdstat_get((__wasi_fd_t)fildes, &fds) == 0 &&
          (fds.fs_rights_base & __WASI_RIGHTS_FD_WRITE) == 0) {
        errno = EINVAL;
        return -1;
      }
    }
    errno = error;
    return -1;
  }
  return 0;
}
