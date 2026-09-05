#ifndef	_DIRENT_H
#define	_DIRENT_H

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
#else
#include <__header_dirent.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_ino_t
#define __NEED_off_t
#if defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
#define __NEED_size_t
#endif

#include <bits/alltypes.h>

#include <bits/dirent.h>

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
typedef struct __dirstream DIR;
#else
#include <__typedef_DIR.h>
#endif

#define d_fileno d_ino

int            closedir(DIR *);
DIR           *fdopendir(int);
DIR           *opendir(const char *);
struct dirent *readdir(DIR *);
#ifdef __wasilibc_unmodified_upstream /* readdir_r is obsolete */
int            readdir_r(DIR *__restrict, struct dirent *__restrict, struct dirent **__restrict);
#endif
void           rewinddir(DIR *);
int            dirfd(DIR *);

int alphasort(const struct dirent **, const struct dirent **);
int scandir(const char *, struct dirent ***, int (*)(const struct dirent *), int (*)(const struct dirent **, const struct dirent **));

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
void           seekdir(DIR *, long);
long           telldir(DIR *);
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#define IFTODT(x) ((x)>>12 & 017)
#define DTTOIF(x) ((x)<<12)
#endif
#ifdef __wasilibc_unmodified_upstream /* getdents has no expressible contract here */
/* firebox#P47: `getdents` was DECLARED here and defined in no archive, so
   CMake's check_symbol_exists answered HAVE_getdents=1 while the link failed
   with `undefined symbol: getdents` -- a compile-time promise broken at link
   time, which is neither of invariant 0's acceptable outcomes.

   It is not merely unimplemented, it is UNEXPRESSIBLE in this ABI.  getdents
   returns a byte count over a packed stream of variable-length records, and
   the only defined way for a caller to advance is `d_reclen`.  The WASI
   `struct dirent` this sysroot ships is `{ ino_t d_ino; unsigned char d_type;
   char d_name[]; }` -- no `d_reclen`, no `d_off`, and a flexible array member,
   so the record length is not derivable from the type either.  A getdents
   returning THIS struct hands back bytes no caller can walk (a false success);
   a getdents returning the Linux `linux_dirent64` layout would require the
   caller to know a record format no header here declares, which is exactly the
   Firebox-specific user-visible knowledge invariants 0 and 4 forbid.

   Compiling it out is therefore not a descope of a capability -- it removes a
   promise that was never kept.  getdents is not POSIX: it has no sysconf key,
   no pathconf key and no feature-test macro of its own, so its standard
   discovery mechanism IS the configure-time compile-and-link test, and its
   standard fallback is readdir(), which this libc implements in full
   (libc-bottom-half/cloudlibc/src/libc/dirent/readdir.c, over
   __wasi_fd_readdir).  With the declaration gone both probe families agree the
   symbol is absent and every autoconf/cmake project takes that fallback.

   ⚠️ NOT closed as impossible.  Implementing getdents faithfully needs
   `struct dirent` to carry d_reclen/d_off, i.e. the musl/Linux layout -- an
   ABI change to the type readdir() returns and scandir() allocates, across
   every rendered shelf and prebuilt .webc.  That is the precondition, and it
   is tracked on firebox#P47, not settled here. */
int getdents(int, struct dirent *, size_t);
#endif
#endif

#ifdef _GNU_SOURCE
int versionsort(const struct dirent **, const struct dirent **);
#endif

#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
#define dirent64 dirent
#define readdir64 readdir
#ifdef __wasilibc_unmodified_upstream /* readdir_r is obsolete */
#define readdir64_r readdir_r
#endif
#define scandir64 scandir
#define alphasort64 alphasort
#define versionsort64 versionsort
#define off64_t off_t
#define ino64_t ino_t
/* firebox#P47: getdents64 aliased to the undefined getdents, so a program
   built with _GNU_SOURCE or _LARGEFILE64_SOURCE -- which AC_SYS_LARGEFILE sets
   for a very large share of autotools projects -- compiled clean and died at
   link with `undefined symbol: getdents`.  Gone with its target. */
#endif

#ifdef __cplusplus
}
#endif

#endif
