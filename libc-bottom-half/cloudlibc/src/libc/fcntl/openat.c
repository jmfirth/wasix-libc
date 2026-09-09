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

// firebox#87F — the ten static_asserts that used to stand here asserted that
// each O_* name EQUALLED (or shifted onto) its WASI wire bit. The renumber
// falsifies every one of them by construction, so they are deleted in the
// commit that falsifies them rather than left to be "fixed" into something
// weaker. They are not replaced: an assert written from <fcntl.h> about the
// values <fcntl.h> defines is a tautology, and the encoders below no longer
// depend on any relationship between the two numbering spaces. The authority
// for these numbers is Linux uapi, and the oracle is a running guest.

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
  // The numbers have now moved: O_RDONLY is 0, so a bit test could not work
  // at all, and the flag word no longer contains the wire encoding — see
  // __wasilibc_*_to_wasi() below, which is the only thing that produces it.
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
    // O_EXEC and O_SEARCH are both O_PATH on musl, i.e. the same value, so
    // they are one label now. Two labels stopped compiling the moment the
    // numbers moved -- the one fail-CLOSED tripwire in this change.
    case O_EXEC:
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
  __wasi_fdflags_t fs_flags = __wasilibc_fdflags_to_wasi(oflag);
  __wasi_fdflagsext_t fd_flags = __wasilibc_fdflagsext_to_wasi(oflag);
  __wasi_rights_t fs_rights_base = max & fsb_cur.fs_rights_inheriting;
  __wasi_rights_t fs_rights_inheriting = fsb_cur.fs_rights_inheriting;
  __wasi_fd_t newfd;
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  error = __wasi_path_open2(fd, lookup_flags, path,
                                 __wasilibc_oflags_to_wasi(oflag),
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
