// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef DIRENT_DIRENT_IMPL_H
#define DIRENT_DIRENT_IMPL_H

#include <wasi/api.h>
#include <dirent.h>
#include <stddef.h>

struct dirent;

// firebox#1DX: translate a raw `__wasi_filetype_t` off `fd_readdir` into this
// libc's `DT_*` space. Callers used to assign `entry.d_type` to `d_type`
// directly, which is only correct while every `DT_*` happens to be spelled as
// the numerically equal WASI filetype -- and it never was. When this was
// written `DT_SOCK` was 255, outside the filetype space entirely, and no
// `DT_*` spelled `__WASI_FILETYPE_FIFO`, `_SOCKET_RAW` or `_SOCKET_SEQPACKET`
// at all. Since firebox#4TY the two spaces are unrelated by construction:
// `DT_*` holds Linux's values, so this function is the whole translation
// rather than a mostly-identity one.
//
// The raw path is unreachable today only because wasmer's `fd_readdir` returns
// `d_ino == 0` for every entry, which sends every caller down the `fstatat`
// fallback where `__wasilibc_iftodt` produces the answer instead. Populating
// that inode -- a pure performance change -- would have turned a socket into
// `DT_FIFO` (6 aliases `__WASI_FILETYPE_SOCKET_STREAM`) and a FIFO into a value
// with no `DT_*` spelling. Measured, four arms, in firebox#1DX's bank.
//
// Deliberately symbolic on both sides: it stays correct wherever firebox#4TY
// moves the `DT_*` constants, and it is the only place a filetype crosses into
// `DT_*` space, so there is one write per entry rather than two.
static __inline unsigned char __wasilibc_filetype_to_dt(__wasi_filetype_t ft) {
  switch (ft) {
    case __WASI_FILETYPE_BLOCK_DEVICE:     return DT_BLK;
    case __WASI_FILETYPE_CHARACTER_DEVICE: return DT_CHR;
    case __WASI_FILETYPE_DIRECTORY:        return DT_DIR;
    case __WASI_FILETYPE_REGULAR_FILE:     return DT_REG;
    case __WASI_FILETYPE_SYMBOLIC_LINK:    return DT_LNK;
    case __WASI_FILETYPE_FIFO:             return DT_FIFO;
    case __WASI_FILETYPE_SOCKET_DGRAM:
    case __WASI_FILETYPE_SOCKET_RAW:
    case __WASI_FILETYPE_SOCKET_SEQPACKET:
    case __WASI_FILETYPE_SOCKET_STREAM:    return DT_SOCK;
    // An unrecognised filetype is exactly what DT_UNKNOWN is for. Do not
    // forward the raw value: a caller reading it would land on whichever DT_*
    // happens to collide with it.
    case __WASI_FILETYPE_UNKNOWN:
    default:                               return DT_UNKNOWN;
  }
}

#define DIRENT_DEFAULT_BUFFER_SIZE 4096

struct _DIR {
  // Directory file descriptor and cookie.
  int fd;
  __wasi_dircookie_t cookie;

  // Read buffer.
  char *buffer;
  size_t buffer_processed;
  size_t buffer_size;
  size_t buffer_used;

  // Object returned by readdir().
  struct dirent *dirent;
  size_t dirent_size;
};

#endif
