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

      /* firebox#KZ0 — F_GETLK now returns the CONFLICTING lock (real POSIX).
       * The prior code called fd_lock_range and hardcoded fl->l_type=F_UNLCK
       * because that flat ABI had no conflict-readback channel — so a child
       * never saw the parent's lock (Open POSIX fork/11-1: "found F_UNLCK,
       * should be F_WRLCK"). The additive fd_getlk import carries the conflict
       * back; F_SETLK/F_SETLKW keep the untouched fd_lock_range path. */
      if (cmd == F_GETLK) {
        uint64_t out[4] = {0, 0, 0, 0};
        __wasi_errno_t err = __wasix_fd_getlk(
            (__wasi_fd_t)fildes, l_type, whence,
            (int64_t)fl->l_start, (int64_t)fl->l_len, out);
        if (err != __WASI_ERRNO_SUCCESS) {
          errno = (int)err;
          return -1;
        }
        /* out = {l_type (0=RDLCK,1=WRLCK,2=UNLCK), l_pid, l_start, l_len}. */
        switch (out[0]) {
          case 0:  fl->l_type = F_RDLCK; break;
          case 1:  fl->l_type = F_WRLCK; break;
          default: fl->l_type = F_UNLCK; break;
        }
        if (out[0] != 2) {
          /* A real conflict: POSIX fills the conflicting lock's fields. The
           * host reports absolute offsets, so l_whence becomes SEEK_SET. */
          fl->l_pid    = (pid_t)out[1];
          fl->l_whence = SEEK_SET;
          fl->l_start  = (off_t)out[2];
          fl->l_len    = (off_t)out[3];
        }
        return 0;
      }

      uint32_t op = (cmd == F_SETLK) ? __FIREBOX_LOCK_OP_SETLK
                                     : __FIREBOX_LOCK_OP_SETLKW;
      __wasi_errno_t err = __wasix_fd_lock_range(
          (__wasi_fd_t)fildes, op, l_type, whence,
          (int64_t)fl->l_start, (int64_t)fl->l_len);
      if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
      }
      return 0;
    }
    default:
      errno = EINVAL;
      return -1;
  }
}
