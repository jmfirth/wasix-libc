// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause
//
// Firebox extension (issue #243): F_SETLK / F_GETLK / F_SETLKW handling
// at the bottom of this file. The lock-table semantics live in the
// patched wasmer runtime; this layer just packs the supplied
// `struct flock` into flat WASIX-import arguments.

#include <wasi/api.h>
#include <wasi/api_wasix.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

/* Map our advisory-lock cmds onto fd_lock_range op codes that the
 * WASIX runtime understands. Keeping the libc-side constants close
 * here makes it obvious that the values that hit the runtime are
 * decoupled from whatever values the rest of the userland uses (we
 * could renumber F_SETLK at the libc level without touching the
 * runtime ABI). */
#define __FIREBOX_LOCK_OP_SETLK   0
#define __FIREBOX_LOCK_OP_SETLKW  1
#define __FIREBOX_LOCK_OP_GETLK   2

int fcntl(int fildes, int cmd, ...) {
  switch (cmd) {
    case F_GETFD: {
      __wasi_fdflagsext_t flags;
      __wasi_errno_t error = __wasi_fd_fdflags_get(fildes, &flags);
      if (error != 0) {
        errno = error;
        return -1;
      }
      return flags & __WASI_FDFLAGSEXT_CLOEXEC ? FD_CLOEXEC : 0;
    }
    case F_SETFD: {
      va_list ap;
      va_start(ap, cmd);
      int flags = va_arg(ap, int);
      va_end(ap);

      /* `(flags & FD_CLOEXEC)`, not `flags | FD_CLOEXEC`: the latter is
       * always nonzero (and `|` binds tighter than `?:`), which made
       * F_SETFD unable to CLEAR the CLOEXEC flag. */
      __wasi_fdflagsext_t fd_flags = (flags & FD_CLOEXEC) ? __WASI_FDFLAGSEXT_CLOEXEC : 0;
      __wasi_errno_t error =
          __wasi_fd_fdflags_set(fildes, fd_flags);
      if (error != 0) {
        errno = error;
        return -1;
      }
      return 0;
    }
    case F_GETFL: {
      // Obtain the flags and the rights of the descriptor.
      __wasi_fdstat_t fds;
      __wasi_errno_t error = __wasi_fd_fdstat_get(fildes, &fds);
      if (error != 0) {
        errno = error;
        return -1;
      }

      // Roughly approximate the access mode by converting the rights.
      int oflags = fds.fs_flags;
      if ((fds.fs_rights_base &
           (__WASI_RIGHTS_FD_READ | __WASI_RIGHTS_FD_READDIR)) != 0) {
        if ((fds.fs_rights_base & __WASI_RIGHTS_FD_WRITE) != 0)
          oflags |= O_RDWR;
        else
          oflags |= O_RDONLY;
      } else if ((fds.fs_rights_base & __WASI_RIGHTS_FD_WRITE) != 0) {
        oflags |= O_WRONLY;
      } else {
        oflags |= O_SEARCH;
      }
      return oflags;
    }
    case F_SETFL: {
      // Set new file descriptor flags.
      va_list ap;
      va_start(ap, cmd);
      int flags = va_arg(ap, int);
      va_end(ap);

      __wasi_fdflags_t fs_flags = flags & 0xfff;
      __wasi_errno_t error =
          __wasi_fd_fdstat_set_flags(fildes, fs_flags);
      if (error != 0) {
        errno = error;
        return -1;
      }
      return 0;
    }
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
      va_list ap;
      va_start(ap, cmd);
      int min_res_fd = va_arg(ap, int);
      va_end(ap);

      int fd;
      __wasi_bool_t cloexec = cmd == F_DUPFD_CLOEXEC;
      __wasi_errno_t error = __wasi_fd_dup2(fildes, min_res_fd, cloexec, &fd);
      if (error != 0) {
        errno = error;
        return -1;
      }
      return fd;
    }
    /* Firebox extension (issue #243): advisory record locks. */
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: {
      va_list ap;
      va_start(ap, cmd);
      struct flock *fl = va_arg(ap, struct flock *);
      va_end(ap);

      if (fl == NULL) {
        errno = EINVAL;
        return -1;
      }

      uint32_t op = (cmd == F_SETLK)  ? __FIREBOX_LOCK_OP_SETLK
                  : (cmd == F_SETLKW) ? __FIREBOX_LOCK_OP_SETLKW
                                      : __FIREBOX_LOCK_OP_GETLK;

      uint32_t l_type;
      switch (fl->l_type) {
        case F_RDLCK: l_type = 0; break;
        case F_WRLCK: l_type = 1; break;
        case F_UNLCK: l_type = 2; break;
        default:
          errno = EINVAL;
          return -1;
      }

      uint32_t whence;
      switch (fl->l_whence) {
        case SEEK_SET: whence = 0; break;
        case SEEK_CUR: whence = 1; break;
        case SEEK_END: whence = 2; break;
        default:
          errno = EINVAL;
          return -1;
      }

      __wasi_errno_t err = __wasix_fd_lock_range(
          (__wasi_fd_t)fildes, op, l_type, whence,
          (int64_t)fl->l_start, (int64_t)fl->l_len);
      if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
      }

      /* For F_GETLK, real POSIX writes the conflicting lock back into
       * `fl`. Our runtime currently does not return the conflict
       * structure (it only tells us success/conflict via errno). To
       * stay POSIX-correct for the no-conflict case, mark the lock
       * as F_UNLCK so callers detect "no conflict". This matches the
       * documented runtime limitation in the WASIX-side handler. */
      if (cmd == F_GETLK) {
        fl->l_type = F_UNLCK;
      }
      return 0;
    }
    default:
      errno = EINVAL;
      return -1;
  }
}
