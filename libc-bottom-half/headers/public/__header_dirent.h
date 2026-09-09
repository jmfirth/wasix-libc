#ifndef __wasilibc___header_dirent_h
#define __wasilibc___header_dirent_h

#include <wasi/api.h>

// firebox#4TY: these are Linux's `DT_*` values, verbatim from
// `linux/include/uapi/linux/fs.h` (and identically musl's `include/dirent.h`).
//
// They used to be spelled as `__WASI_FILETYPE_*`, which made `d_type` a WASI
// filetype wearing a `DT_*` name: `DT_DIR` was 3 where Linux says 4, `DT_REG`
// 4 where Linux says 8, and `DT_FIFO` was *aliased onto*
// `__WASI_FILETYPE_SOCKET_STREAM` (6), so a FIFO and a stream socket shared
// one `d_type` byte while `__WASI_FILETYPE_FIFO` (10) had no `DT_*` spelling
// at all. Six of the eight diverged; only `DT_UNKNOWN` and `DT_CHR` happened
// to land on Linux's value. That is the same reasoning error firebox#NJ4 corrected in
// `__mode_t.h` -- a constant set to something *distinct* rather than to Linux's
// value -- and both halves came from the same upstream commit, 6426235.
//
// Invariant 2: `d_type` is an ABI-visible byte that guest programs compare
// against literals, serialise, and carry between separately-compiled
// components. There is no version of "the same way you would on Linux" in
// which `DT_DIR` is 3.
//
// Nothing is lost by no longer aliasing WASI's space. The single crossing
// point from `__wasi_filetype_t` into `DT_*` is `__wasilibc_filetype_to_dt`
// (firebox#1DX, `dirent_impl.h`), which is a symbolic switch and therefore
// already correct at whatever values this table holds. `__wasilibc_iftodt` and
// `__wasilibc_dttoif` in `libc-bottom-half/sources/__wasilibc_dt.c` are the
// other two, likewise symbolic on both sides.
//
// With this table and firebox#NJ4's `S_IF*`, the pair satisfies Linux's
// algebraic identity `DTTOIF(x) == (x) << 12` / `IFTODT(x) == (x) >> 12 & 017`
// for every defined type -- which it could not before, and which is the
// property portable code actually relies on.
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define IFTODT(x) (__wasilibc_iftodt(x))
#define DTTOIF(x) (__wasilibc_dttoif(x))

#include <__struct_dirent.h>
#include <__typedef_DIR.h>

#ifdef __cplusplus
extern "C" {
#endif

int __wasilibc_iftodt(int x);
int __wasilibc_dttoif(int x);

int closedir(DIR *);
DIR *opendir(const char *);
DIR *fdopendir(int);
int fdclosedir(DIR *);
struct dirent *readdir(DIR *);
void rewinddir(DIR *);
void seekdir(DIR *, long);
long telldir(DIR *);
DIR *opendirat(int, const char *);
void rewinddir(DIR *);
int scandirat(int, const char *, struct dirent ***,
              int (*)(const struct dirent *),
              int (*)(const struct dirent **, const struct dirent **));

#ifdef __cplusplus
}
#endif

#endif
