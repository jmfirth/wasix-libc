// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <assert.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

static_assert(O_APPEND == __WASI_FDFLAGS_APPEND, "Value mismatch");
static_assert(O_DSYNC == __WASI_FDFLAGS_DSYNC, "Value mismatch");
static_assert(O_NONBLOCK == __WASI_FDFLAGS_NONBLOCK, "Value mismatch");
static_assert(O_RSYNC == __WASI_FDFLAGS_RSYNC, "Value mismatch");
static_assert(O_SYNC == __WASI_FDFLAGS_SYNC, "Value mismatch");

static_assert(O_CREAT >> 12 == __WASI_OFLAGS_CREAT, "Value mismatch");
static_assert(O_DIRECTORY >> 12 == __WASI_OFLAGS_DIRECTORY, "Value mismatch");
static_assert(O_EXCL >> 12 == __WASI_OFLAGS_EXCL, "Value mismatch");
static_assert(O_TRUNC >> 12 == __WASI_OFLAGS_TRUNC, "Value mismatch");

static_assert(O_CLOEXEC >> 30 == __WASI_FDFLAGSEXT_CLOEXEC, "Value mismatch");

int __wasilibc_nocwd_openat_nomode(int fd, const char *path, int oflag) {
  // Compute rights corresponding with the access modes provided.
  // Attempt to obtain all rights, except the ones that contradict the
  // access mode provided to openat().
  __wasi_rights_t max =
      ~(__WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_READ |
        __WASI_RIGHTS_FD_WRITE | __WASI_RIGHTS_FD_ALLOCATE |
        __WASI_RIGHTS_FD_READDIR | __WASI_RIGHTS_FD_FILESTAT_SET_SIZE);
  //
  // firebox#87F — the access mode is a VALUE, not a set of bits.
  //
  // POSIX and Linux specify O_RDONLY/O_WRONLY/O_RDWR as three enumerated
  // values selected by the O_ACCMODE field, not as independent flags; on Linux
  // O_RDONLY is 0, so a `(oflag & O_RDONLY) != 0` test can never fire. Testing
  // them as bits happens to work only while our O_RDONLY/O_WRONLY carry the
  // WASI-invented values 0x04000000/0x10000000, where they really are disjoint
  // bits above the wire field.
  //
  // Why that matters here rather than in a header: this function IS the wire
  // encoder. It ships `oflag & 0xfff` as __wasi_fdflags_t below. Linux's
  // O_WRONLY(1)/O_RDWR(2) land exactly on __WASI_FDFLAGS_APPEND(1) and
  // __WASI_FDFLAGS_DSYNC(2), so once these constants move onto Linux numbering
  // a bit-testing encoder turns every write-open into a silent O_APPEND — a
  // fail-open, indistinguishable from a correct open, not an errno. The
  // renumber therefore has to be preceded by this rewrite, and the value
  // semantics below are what stays correct on both sides of it.
  //
  // This form is a behaviour-preserving no-op against today's constants: the
  // switch already discriminated on `oflag & O_ACCMODE`, so reaching the RDWR
  // arm already implied both bits were set.
  switch (oflag & O_ACCMODE) {
    case O_RDONLY:
      max |= __WASI_RIGHTS_FD_READ | __WASI_RIGHTS_FD_READDIR;
      break;
    case O_WRONLY:
      max |= __WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_WRITE |
             __WASI_RIGHTS_FD_ALLOCATE | __WASI_RIGHTS_FD_FILESTAT_SET_SIZE;
      break;
    case O_RDWR:
      max |= __WASI_RIGHTS_FD_READ | __WASI_RIGHTS_FD_READDIR |
             __WASI_RIGHTS_FD_DATASYNC | __WASI_RIGHTS_FD_WRITE |
             __WASI_RIGHTS_FD_ALLOCATE | __WASI_RIGHTS_FD_FILESTAT_SET_SIZE;
      break;
    case O_EXEC:
      break;
    case O_SEARCH:
      break;
    default:
      errno = EINVAL;
      return -1;
  }

  // Ensure that we can actually obtain the minimal rights needed.
  __wasi_fdstat_t fsb_cur;
  __wasi_errno_t error = __wasi_fd_fdstat_get(fd, &fsb_cur);
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  // Path lookup properties.
  __wasi_lookupflags_t lookup_flags = 0;
  if ((oflag & O_NOFOLLOW) == 0)
    lookup_flags |= __WASI_LOOKUPFLAGS_SYMLINK_FOLLOW;

  // Open file with appropriate rights.
  __wasi_fdflags_t fs_flags = oflag & 0xfff;
  __wasi_fdflagsext_t fd_flags = (oflag >> 30) & 0x03;
  __wasi_rights_t fs_rights_base = max & fsb_cur.fs_rights_inheriting;
  __wasi_rights_t fs_rights_inheriting = fsb_cur.fs_rights_inheriting;
  __wasi_fd_t newfd;
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  error = __wasi_path_open2(fd, lookup_flags, path,
                                 (oflag >> 12) & 0xfff,
                                 fs_rights_base, fs_rights_inheriting, fs_flags,
                                 fd_flags, &newfd);
  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }
  return newfd;
}
