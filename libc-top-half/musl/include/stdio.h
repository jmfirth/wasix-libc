#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_FILE
#define __NEED___isoc_va_list
#define __NEED_size_t

#ifdef __wasilibc_unmodified_upstream /* WASI doesn't need to define FILE as a complete type */
#if __STDC_VERSION__ < 201112L
#define __NEED_struct__IO_FILE
#endif
#endif

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define __NEED_ssize_t
#define __NEED_off_t
#define __NEED_va_list
#endif

#include <bits/alltypes.h>

#ifdef __wasilibc_unmodified_upstream /* Use the compiler's definition of NULL */
#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif
#else
#define __need_NULL
#include <stddef.h>
#endif

#undef EOF
#define EOF (-1)

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#else
#include <__seek.h>
#endif

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define BUFSIZ 1024
#define FILENAME_MAX 4096
#define FOPEN_MAX 1000
/* C requires <stdio.h> to define both. They were compiled out on the premise
 * that "WASI has no temp directories" -- false for Firebox, MEASURED in-guest.
 * These are plain integer constants: hiding them does not make anything safer,
 * it just means `char buf[L_tmpnam];` fails to COMPILE, which is a conformance
 * gap a program cannot work around. (firebox #P47)
 */
#define TMP_MAX 10000
#define L_tmpnam 20

typedef union _G_fpos64_t {
	char __opaque[16];
	long long __lldata;
	double __align;
} fpos_t;

extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;

#define stdin  (stdin)
#define stdout (stdout)
#define stderr (stderr)

FILE *fopen(const char *__restrict, const char *__restrict);
FILE *freopen(const char *__restrict, const char *__restrict, FILE *__restrict);
int fclose(FILE *);

int remove(const char *);
int rename(const char *, const char *);

int feof(FILE *);
int ferror(FILE *);
int fflush(FILE *);
void clearerr(FILE *);

int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);

int fgetpos(FILE *__restrict, fpos_t *__restrict);
int fsetpos(FILE *, const fpos_t *);

size_t fread(void *__restrict, size_t, size_t, FILE *__restrict);
size_t fwrite(const void *__restrict, size_t, size_t, FILE *__restrict);

int fgetc(FILE *);
int getc(FILE *);
int getchar(void);
int ungetc(int, FILE *);

int fputc(int, FILE *);
int putc(int, FILE *);
int putchar(int);

char *fgets(char *__restrict, int, FILE *__restrict);
#if __STDC_VERSION__ < 201112L
#ifdef __wasilibc_unmodified_upstream /* gets is obsolete */
char *gets(char *);
#else
char *gets(char *) __attribute__((__deprecated__("gets is not defined on WASI")));
#endif
#endif

int fputs(const char *__restrict, FILE *__restrict);
int puts(const char *);

int printf(const char *__restrict, ...);
int fprintf(FILE *__restrict, const char *__restrict, ...);
int sprintf(char *__restrict, const char *__restrict, ...);
int snprintf(char *__restrict, size_t, const char *__restrict, ...);

int vprintf(const char *__restrict, __isoc_va_list);
int vfprintf(FILE *__restrict, const char *__restrict, __isoc_va_list);
int vsprintf(char *__restrict, const char *__restrict, __isoc_va_list);
int vsnprintf(char *__restrict, size_t, const char *__restrict, __isoc_va_list);

int scanf(const char *__restrict, ...);
int fscanf(FILE *__restrict, const char *__restrict, ...);
int sscanf(const char *__restrict, const char *__restrict, ...);
int vscanf(const char *__restrict, __isoc_va_list);
int vfscanf(FILE *__restrict, const char *__restrict, __isoc_va_list);
int vsscanf(const char *__restrict, const char *__restrict, __isoc_va_list);

void perror(const char *);

int setvbuf(FILE *__restrict, char *__restrict, int, size_t);
void setbuf(FILE *__restrict, char *__restrict);

#ifdef __wasilibc_unmodified_upstream /* WASI has no temp directories */
char *tmpnam(char *);
FILE *tmpfile(void);
#else
/* Both ARE defined here. tmpfile has been implemented all along (MEASURED: a
 * write/rewind/read round-trip succeeds inside `firebox run`), and tmpnam is
 * implemented in src/stdio/tmpnam.c's wasi branch. The deprecation attributes
 * that used to sit here read "... is not defined on WASI" -- a false statement
 * about our own libc, and a user-reachable one: -Wdeprecated-declarations turns
 * it into a warning and any -Werror build calling tmpfile FAILED. (firebox #P47)
 */
char *tmpnam(char *);
FILE *tmpfile(void);
#endif

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
FILE *fmemopen(void *__restrict, size_t, const char *__restrict);
FILE *open_memstream(char **, size_t *);
FILE *fdopen(int, const char *);
FILE *popen(const char *, const char *);
int pclose(FILE *);
int fileno(FILE *);
int fseeko(FILE *, off_t, int);
off_t ftello(FILE *);
int dprintf(int, const char *__restrict, ...);
int vdprintf(int, const char *__restrict, __isoc_va_list);
/* firebox#H18: DEFINED in our libc.a, so the prototype must be visible.
 * Upstream hid the stdio locking trio behind __wasilibc_unmodified_upstream because stock
 * wasi-libc has no implementation. Firebox DOES: MEASURED T flockfile / T ftrylockfile / T funlockfile in libc.a.
 * A capability we define but do not DECLARE is unreachable -- the caller's only
 * recourse is to hand-declare it, and a hand-declaration in front of a real
 * implementation is a latent ABI mismatch (firebox#4ZY deleted packages/vi/shims
 * for exactly that). The block-mates left hidden are NOT defined (MEASURED 0). */
/* POSIX declares these in <stdio.h> UNCONDITIONALLY; requiring _REENTRANT was a
 * divergence that broke ordinary single-threaded callers. */
void flockfile(FILE *);
int ftrylockfile(FILE *);
void funlockfile(FILE *);
int getc_unlocked(FILE *);
int getchar_unlocked(void);
int putc_unlocked(int, FILE *);
int putchar_unlocked(int);
ssize_t getdelim(char **__restrict, size_t *__restrict, int, FILE *__restrict);
ssize_t getline(char **__restrict, size_t *__restrict, FILE *__restrict);
int renameat(int, const char *, int, const char *);
char *ctermid(char *);
#define L_ctermid 20
#endif


/* P_tmpdir is a plain string constant, and /tmp is real here (MEASURED in-guest,
 * firebox #P47). Hiding it protected nothing: it means `check_writable_directory(
 * P_tmpdir)` fails to COMPILE, the same conformance gap #P47 closed for
 * TMP_MAX/L_tmpnam, and a caller cannot work around a macro that is not there.
 * XSI requires <stdio.h> to define it, so it is exposed under exactly the
 * feature-test postures upstream musl used. MEASURED: sole blocker for GNU nano
 * 8.7.1 (src/files.c:1473). (firebox #85Y)
 *
 * ⛔ tempnam() below deliberately STAYS in the dead branch: #P47 ruled that
 * nothing declares it and its absence is therefore already honest (MEASURED:
 * UNDEFINED in libc.a). A macro is not a declaration -- that is why the two
 * halves of this upstream block get different answers, and why the branch is
 * SPLIT rather than opened.
 */
#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define P_tmpdir "/tmp"
#endif

#ifdef __wasilibc_unmodified_upstream /* WASI has no temp directories */
#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
char *tempnam(const char *, const char *);
#endif
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define L_cuserid 20
char *cuserid(char *);
void setlinebuf(FILE *);
void setbuffer(FILE *, char *, size_t);
int fgetc_unlocked(FILE *);
int fputc_unlocked(int, FILE *);
int fflush_unlocked(FILE *);
size_t fread_unlocked(void *, size_t, size_t, FILE *);
size_t fwrite_unlocked(const void *, size_t, size_t, FILE *);
void clearerr_unlocked(FILE *);
int feof_unlocked(FILE *);
int ferror_unlocked(FILE *);
int fileno_unlocked(FILE *);
int getw(FILE *);
int putw(int, FILE *);
char *fgetln(FILE *, size_t *);
int asprintf(char **, const char *, ...);
int vasprintf(char **, const char *, __isoc_va_list);
#endif

#ifdef _GNU_SOURCE
char *fgets_unlocked(char *, int, FILE *);
int fputs_unlocked(const char *, FILE *);

typedef ssize_t (cookie_read_function_t)(void *, char *, size_t);
typedef ssize_t (cookie_write_function_t)(void *, const char *, size_t);
typedef int (cookie_seek_function_t)(void *, off_t *, int);
typedef int (cookie_close_function_t)(void *);

typedef struct _IO_cookie_io_functions_t {
	cookie_read_function_t *read;
	cookie_write_function_t *write;
	cookie_seek_function_t *seek;
	cookie_close_function_t *close;
} cookie_io_functions_t;

FILE *fopencookie(void *, const char *, cookie_io_functions_t);
#endif

#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
/* firebox#W0Q: tmpfile64 is a plain alias onto tmpfile, which is DEFINED in
 * libc.a and DECLARED above -- and the four aliases directly below it in this
 * same #if have always been unconditional. The "WASI has no temp directories"
 * guard is doubly misapplied here: its premise is false (#P47 MEASURED /tmp
 * working in-guest) and it is not an LFS question at all. Hiding it only means
 * a _GNU_SOURCE program naming tmpfile64 fails to COMPILE. */
#define tmpfile64 tmpfile
#define fopen64 fopen
#define freopen64 freopen
#define fseeko64 fseeko
#define ftello64 ftello
#define fgetpos64 fgetpos
#define fsetpos64 fsetpos
#define fpos64_t fpos_t
#define off64_t off_t
#endif

#ifdef __cplusplus
}
#endif

#endif
