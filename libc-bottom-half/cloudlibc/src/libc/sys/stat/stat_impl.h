// Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef SYS_STAT_STAT_IMPL_H
#define SYS_STAT_STAT_IMPL_H

#include <common/time.h>

#include <sys/stat.h>

#include <assert.h>
#include <wasi/api.h>
#include <stdbool.h>

static_assert(S_ISBLK(S_IFBLK), "Value mismatch");
static_assert(S_ISCHR(S_IFCHR), "Value mismatch");
static_assert(S_ISDIR(S_IFDIR), "Value mismatch");
static_assert(S_ISFIFO(S_IFIFO), "Value mismatch");
static_assert(S_ISLNK(S_IFLNK), "Value mismatch");
static_assert(S_ISREG(S_IFREG), "Value mismatch");
static_assert(S_ISSOCK(S_IFSOCK), "Value mismatch");

static inline void to_public_stat(const __wasi_filestat_t *in,
                                  struct stat *out) {
  // Ensure that we don't truncate any values.
  static_assert(sizeof(in->dev) == sizeof(out->st_dev), "Size mismatch");
  static_assert(sizeof(in->ino) == sizeof(out->st_ino), "Size mismatch");
  /*
   * The non-standard __st_filetype field appears to only be used for shared
   * memory, which we don't currently support.
   */
  /* nlink_t is 64-bit on wasm32, following the x32 ABI. */
  static_assert(sizeof(in->nlink) <= sizeof(out->st_nlink), "Size shortfall");
  static_assert(sizeof(in->size) == sizeof(out->st_size), "Size mismatch");

  *out = (struct stat){
      .st_dev = in->dev,
      .st_ino = in->ino,
      .st_nlink = in->nlink,
      .st_size = in->size,
      .st_atim = timestamp_to_timespec(in->atim),
      .st_mtim = timestamp_to_timespec(in->mtim),
      .st_ctim = timestamp_to_timespec(in->ctim),
  };

  // Convert file type to legacy types encoded in st_mode.
  switch (in->filetype) {
    case __WASI_FILETYPE_BLOCK_DEVICE:
      out->st_mode |= S_IFBLK;
      break;
    case __WASI_FILETYPE_CHARACTER_DEVICE:
      out->st_mode |= S_IFCHR;
      break;
    case __WASI_FILETYPE_DIRECTORY:
      out->st_mode |= S_IFDIR;
      break;
    case __WASI_FILETYPE_REGULAR_FILE:
      out->st_mode |= S_IFREG;
      break;
    case __WASI_FILETYPE_SOCKET_DGRAM:
    case __WASI_FILETYPE_SOCKET_STREAM:
    case __WASI_FILETYPE_SOCKET_SEQPACKET:
    case __WASI_FILETYPE_SOCKET_RAW:
      out->st_mode |= S_IFSOCK;
      break;
    case __WASI_FILETYPE_SYMBOLIC_LINK:
      out->st_mode |= S_IFLNK;
      break;
    case __WASI_FILETYPE_FIFO:
      out->st_mode |= S_IFIFO;
      break;
  }

  // Extract POSIX permission bits from the dev field.
  //
  // WASI preview1's filestat has no mode field. Firebox's runtime encodes
  // Unix permission bits (0o7777 -- owner/group/other rwx plus setuid,
  // setgid, sticky) in the low 12 bits of the dev (device ID) field.
  // A dev value of 0 means the runtime did not provide mode information;
  // in that case fall back to sensible defaults (0755 for directories,
  // 0644 for regular files) so that ls -l always shows something useful.
  {
    mode_t perm = (mode_t)(in->dev & 07777);
    if (perm != 0) {
      out->st_mode |= perm;
    } else {
      // Default permissions when the runtime provides none.
      if (S_ISDIR(out->st_mode)) {
        out->st_mode |= 0755;
      } else if (S_ISREG(out->st_mode)) {
        out->st_mode |= 0644;
      } else if (S_ISLNK(out->st_mode)) {
        out->st_mode |= 0777;
      } else {
        out->st_mode |= 0644;
      }
    }
  }

  // firebox#WJK — HAND THE dev FIELD BACK once the mode has been taken out.
  //
  // The block above is the ONLY legitimate reader of the WASIX mode-in-dev
  // channel. Everything downstream sees `st_dev` as POSIX defines it: "device
  // ID of the device containing this file". Leaving the runtime's encoded mode
  // there made st_dev VARY BY PERMISSION, so two files in one directory with
  // modes 0644 and 0755 reported different devices and looked like they were on
  // different filesystems -- and `chmod` CHANGED a file's st_dev, which cannot
  // happen on any real system.
  //
  // That is not theoretical: five consumers in this very libc compare st_dev.
  //   misc/nftw.c:51           FTW_MOUNT -- the `find -xdev` mechanism, PRUNES
  //   misc/nftw.c:81           (dev,ino) directory-cycle detection
  //   misc/get_current_dir_name.c:12   getcwd identity check
  //   unistd/ttyname_r.c:30    tty identity check
  //   ipc/ftok.c:9             IPC key derivation
  // Each is an EQUALITY test between two stats, so a single constant makes all
  // five correct for a single-filesystem view, where today they are correct
  // only when the two files happen to share a mode.
  //
  // The constant is a WAYPOINT, not the destination: firebox composes genuinely
  // distinct backing filesystems (virtual-fs mount_fs/overlay_fs -- host_fs,
  // mem_fs, webc_volume_fs), so the honest end state is a per-mount device id.
  // Until that exists, claiming "one filesystem" is strictly more truthful than
  // claiming "one filesystem per permission bit pattern". Tracked as #WJK (C).
  //
  // Note this runs AFTER the extraction above and reads `in->dev`, never
  // `out->st_dev`, so the ordering is not fragile.
  out->st_dev = 1;
}

// firebox(#QQZ/#NGG): `noinline` is load-bearing, NOT a style choice.
//
// WHY: when this helper is inlined into an out-of-line caller (futimens.c's
// `futimens`, utimensat.c's `__wasilibc_nocwd_utimensat`), LLVM 22
// miscompiles the two-consecutive-signed-`switch (times[i].tv_nsec)` pattern:
// it emits only the `times[0]`/atim `br_table`, DROPS the entire
// `times[1]`/mtim switch, pre-seeds `flags` with `__WASI_FSTFLAGS_MTIM (4)`,
// and validates `times[1].tv_nsec` unconditionally against NSEC_PER_SEC-1
// (999999999). Since `UTIME_NOW (-1)` / `UTIME_OMIT (-2)` are `> 999999999`
// when read unsigned, ANY special value in the mtim slot returns EINVAL(28)
// — breaking `cp -p`, `tar -x`, `install`, GNU `touch`'s fd path, and `make`.
// Verified at the object level: pre-fix futimens.o and utimensat.o each carry
// exactly ONE `br_table` and no `MTIM_NOW(8)` path. Forcing the helper
// out-of-line compiles it in a neutral context, restoring both switches.
// `inline` is kept so unused includers (fstat.c/fstatat.c) don't trip
// `-Wunused-function`; `noinline` forces the emitted out-of-line copy where
// it IS called. Retire when upstream LLVM fixes the two-consecutive-signed-
// `switch` inlining miscompile (docs/reference/forks.md §5).
static inline __attribute__((noinline)) bool utimens_get_timestamps(
                                          const struct timespec *times,
                                          __wasi_timestamp_t *st_atim,
                                          __wasi_timestamp_t *st_mtim,
                                          __wasi_fstflags_t *flags) {
  if (times == NULL) {
    // Update both timestamps.
    *flags = __WASI_FSTFLAGS_ATIM_NOW | __WASI_FSTFLAGS_MTIM_NOW;
  } else {
    // Set individual timestamps.
    *flags = 0;
    switch (times[0].tv_nsec) {
      case UTIME_NOW:
        *flags |= __WASI_FSTFLAGS_ATIM_NOW;
        break;
      case UTIME_OMIT:
        break;
      default:
        *flags |= __WASI_FSTFLAGS_ATIM;
        if (!timespec_to_timestamp_exact(&times[0], st_atim))
          return false;
        break;
    }

    switch (times[1].tv_nsec) {
      case UTIME_NOW:
        *flags |= __WASI_FSTFLAGS_MTIM_NOW;
        break;
      case UTIME_OMIT:
        break;
      default:
        *flags |= __WASI_FSTFLAGS_MTIM;
        if (!timespec_to_timestamp_exact(&times[1], st_mtim))
          return false;
        break;
    }
  }
  return true;
}

#endif
