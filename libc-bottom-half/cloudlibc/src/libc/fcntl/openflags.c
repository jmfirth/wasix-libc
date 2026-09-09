// firebox#87F — the ONLY translation between guest open(2) flags and the WASI
// wire fields, and the only one back.
//
// Before the renumber the guest O_* names WERE the wire encoding, so three
// call sites encoded with a shift and a mask (`oflag & 0xfff` for fdflags,
// `(oflag >> 12) & 0xfff` for oflags, `(oflag >> 30) & 0x03` for fdflagsext)
// and fcntl(F_GETFL) decoded by assigning `fds.fs_flags` straight into an int.
//
// ⛔ Why this is one function per direction and not four open-coded blocks:
// the failure mode of a MISSED encoder is fail-open, not an errno. Linux
// O_WRONLY is 1 and O_RDWR is 2, which are exactly __WASI_FDFLAGS_APPEND and
// __WASI_FDFLAGS_DSYNC, so a surviving `oflag & 0xfff` silently turns every
// write-open into an append — bytes land at the end of the file, the open
// succeeds, write() succeeds, close() succeeds, and nothing observable says
// anything went wrong. Centralising means "did I get every site?" is a
// question about who CALLS these, which the linker can answer, rather than a
// question about which arithmetic expressions still exist, which it cannot.

#include <wasi/api.h>
#include <wasi/libc.h>
#include <fcntl.h>

__wasi_fdflags_t __wasilibc_fdflags_to_wasi(int oflag) {
  __wasi_fdflags_t r = 0;
  if ((oflag & O_APPEND) != 0)
    r |= __WASI_FDFLAGS_APPEND;
  if ((oflag & O_NONBLOCK) != 0)
    r |= __WASI_FDFLAGS_NONBLOCK;
  if ((oflag & O_DSYNC) != 0)
    r |= __WASI_FDFLAGS_DSYNC;
  // O_SYNC is __O_SYNC|O_DSYNC on Linux, so the O_DSYNC arm above has already
  // fired for it; that is the correct containment (sync implies dsync).
  // O_RSYNC has the SAME VALUE as O_SYNC on Linux — the kernel gives
  // synchronised-read no separate encoding — so there is nothing here that can
  // ask for __WASI_FDFLAGS_RSYNC, and answering O_RSYNC with SYNC is what
  // Linux does. The reverse direction still recognises RSYNC from the host.
  if ((oflag & O_SYNC) == O_SYNC)
    r |= __WASI_FDFLAGS_SYNC;
  return r;
}

__wasi_oflags_t __wasilibc_oflags_to_wasi(int oflag) {
  __wasi_oflags_t r = 0;
  if ((oflag & O_CREAT) != 0)
    r |= __WASI_OFLAGS_CREAT;
  if ((oflag & O_DIRECTORY) != 0)
    r |= __WASI_OFLAGS_DIRECTORY;
  if ((oflag & O_EXCL) != 0)
    r |= __WASI_OFLAGS_EXCL;
  if ((oflag & O_TRUNC) != 0)
    r |= __WASI_OFLAGS_TRUNC;
  return r;
}

__wasi_fdflagsext_t __wasilibc_fdflagsext_to_wasi(int oflag) {
  __wasi_fdflagsext_t r = 0;
  if ((oflag & O_CLOEXEC) != 0)
    r |= __WASI_FDFLAGSEXT_CLOEXEC;
  return r;
}

int __wasilibc_fdflags_from_wasi(__wasi_fdflags_t fdflags) {
  int r = 0;
  if ((fdflags & __WASI_FDFLAGS_APPEND) != 0)
    r |= O_APPEND;
  if ((fdflags & __WASI_FDFLAGS_NONBLOCK) != 0)
    r |= O_NONBLOCK;
  if ((fdflags & __WASI_FDFLAGS_DSYNC) != 0)
    r |= O_DSYNC;
  if ((fdflags & __WASI_FDFLAGS_RSYNC) != 0)
    r |= O_RSYNC;
  if ((fdflags & __WASI_FDFLAGS_SYNC) != 0)
    r |= O_SYNC;
  return r;
}
