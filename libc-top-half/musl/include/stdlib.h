#ifndef _STDLIB_H
#define _STDLIB_H

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
#else
#include <__functions_malloc.h>
#include <__header_stdlib.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

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

#define __NEED_size_t
#define __NEED_wchar_t

#include <bits/alltypes.h>

int atoi (const char *);
long atol (const char *);
long long atoll (const char *);
double atof (const char *);

float strtof (const char *__restrict, char **__restrict);
double strtod (const char *__restrict, char **__restrict);
long double strtold (const char *__restrict, char **__restrict);

long strtol (const char *__restrict, char **__restrict, int);
unsigned long strtoul (const char *__restrict, char **__restrict, int);
long long strtoll (const char *__restrict, char **__restrict, int);
unsigned long long strtoull (const char *__restrict, char **__restrict, int);

int rand (void);
void srand (unsigned);

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
void *malloc (size_t);
void *calloc (size_t, size_t);
void *realloc (void *, size_t);
void free (void *);
#endif
void *aligned_alloc(size_t, size_t);

_Noreturn void abort (void);
int atexit (void (*) (void));
_Noreturn void exit (int);
_Noreturn void _Exit (int);
int at_quick_exit (void (*) (void));
_Noreturn void quick_exit (int);

char *getenv (const char *);

int system (const char *);

void *bsearch (const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
void qsort (void *, size_t, size_t, int (*)(const void *, const void *));

int abs (int);
long labs (long);
long long llabs (long long);

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

div_t div (int, int);
ldiv_t ldiv (long, long);
lldiv_t lldiv (long long, long long);

int mblen (const char *, size_t);
int mbtowc (wchar_t *__restrict, const char *__restrict, size_t);
int wctomb (char *, wchar_t);
size_t mbstowcs (wchar_t *__restrict, const char *__restrict, size_t);
size_t wcstombs (char *__restrict, const wchar_t *__restrict, size_t);

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

size_t __ctype_get_mb_cur_max(void);
#define MB_CUR_MAX (__ctype_get_mb_cur_max())

#define RAND_MAX (0x7fffffff)


#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)

#define WNOHANG    1
#define WUNTRACED  2

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WSTOPSIG(s) WEXITSTATUS(s)
#define WIFEXITED(s) (!WTERMSIG(s))
#define WIFSTOPPED(s) ((short)((((s)&0xffff)*0x10001)>>8) > 0x7f00)
#define WIFSIGNALED(s) (((s)&0xffff)-1U < 0xffu)

/* Macros for constructing status values.  */
#define __W_EXITCODE(ret, sig)        ((ret) << 8 | (sig))
#define __W_STOPCODE(sig)        ((sig) << 8 | 0x7f)
#define __W_CONTINUED                0xffff
#define W_EXITCODE(ret, sig)	__W_EXITCODE (ret, sig)
#define W_STOPCODE(sig)	__W_STOPCODE (sig)

int posix_memalign (void **, size_t, size_t);
int setenv (const char *, const char *, int);
int unsetenv (const char *);
int mkstemp (char *);
int mkostemp (char *, int);
char *mkdtemp (char *);
int getsubopt (char **, char *const *, char **);
int rand_r (unsigned *);

#endif


#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
char *realpath (const char *__restrict, char *__restrict);
long int random (void);
void srandom (unsigned int);
char *initstate (unsigned int, char *, size_t);
char *setstate (char *);
int putenv (char *);
#ifdef __wasilibc_unmodified_upstream /* WASI has no pseudo-terminals */
int unlockpt (int);
char *ptsname (int);
/* firebox#P47: posix_openpt and grantpt were OUTSIDE this guard while their three
   siblings (unlockpt, ptsname, ptsname_r) were inside it -- so the two that OPEN
   and PREPARE a pty were declared and the three that USE one were not. Nothing
   defines them in any archive on any shelf, so they COMPILED CLEAN and failed at
   LINK: MEASURED compile EXIT=0 / link EXIT=1 under -D_XOPEN_SOURCE=700, against
   unlockpt's honest compile EXIT=1 in the same header under the same macro.
   That is a capability lie (invariant 0) and it is invisible to any probe that
   only COMPILES -- CMake check_symbol_exists, meson has_header_symbol, a bare
   #ifdef. (Autoconf AC_CHECK_FUNCS links, which is why Python's pyconfig.h
   already reads /* #undef HAVE_GRANTPT */ and no shipped package was misled.)
   Declaring absence honestly is the faithful answer here, and it is the same
   rule firebox#H18 applied in the opposite direction on this very file: a
   declaration must match a definition. NOT defining them to return ENOSYS:
   musl's grantpt body is `return 0;` unconditionally, so shipping the pair
   would report SUCCESS for preparing a pty that cannot exist -- a false
   success, which invariant 0 forbids outright. */
int posix_openpt (int);
int grantpt (int);
#endif
char *l64a (long);
long a64l (const char *);
void setkey (const char *);
double drand48 (void);
double erand48 (unsigned short [3]);
long int lrand48 (void);
long int nrand48 (unsigned short [3]);
long mrand48 (void);
long jrand48 (unsigned short [3]);
void srand48 (long);
unsigned short *seed48 (unsigned short [3]);
void lcong48 (unsigned short [7]);
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#include <alloca.h>
char *mktemp (char *);
int mkstemps (char *, int);
int mkostemps (char *, int, int);
#ifdef __wasilibc_unmodified_upstream /* WASI libc doesn't build the legacy functions */
void *valloc (size_t);
void *memalign(size_t, size_t);
int getloadavg(double *, int);
#endif
int clearenv(void);
#ifdef __wasilibc_unmodified_upstream /* WASI has no wait */
#define WCOREDUMP(s) ((s) & 0x80)
#define WIFCONTINUED(s) ((s) == 0xffff)
#endif
/* firebox#H18: DEFINED in our libc.a, so the prototype must be visible.
 * Upstream hid reallocarray behind __wasilibc_unmodified_upstream because stock
 * wasi-libc has no implementation. Firebox DOES: MEASURED W reallocarray in libc.a.
 * A capability we define but do not DECLARE is unreachable -- the caller's only
 * recourse is to hand-declare it, and a hand-declaration in front of a real
 * implementation is a latent ABI mismatch (firebox#4ZY deleted packages/vi/shims
 * for exactly that). The block-mates left hidden are NOT defined (MEASURED 0). */
/* ⚠️ AND IT WAS NEVER A WAIT FUNCTION. reallocarray is a malloc-family call that had
 * been swept into a block whose stated reason is "WASI has no wait", beside WCOREDUMP
 * and WIFCONTINUED. The guard's own comment did not describe it, which is why no
 * reader questioned it. */
void *reallocarray (void *, size_t, size_t);
#endif

#ifdef _GNU_SOURCE
#ifdef __wasilibc_unmodified_upstream /* WASI has no pseudo-terminals */
int ptsname_r(int, char *, size_t);
#endif
char *ecvt(double, int, int *, int *);
char *fcvt(double, int, int *, int *);
char *gcvt(double, int, char *);
char *secure_getenv(const char *);
struct __locale_struct;
float strtof_l(const char *__restrict, char **__restrict, struct __locale_struct *);
double strtod_l(const char *__restrict, char **__restrict, struct __locale_struct *);
long double strtold_l(const char *__restrict, char **__restrict, struct __locale_struct *);
#endif

#ifdef __wasilibc_unmodified_upstream /* WASI has no temp directories */
#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
#define mkstemp64 mkstemp
#define mkostemp64 mkostemp
#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define mkstemps64 mkstemps
#define mkostemps64 mkostemps
#endif
#endif
#endif

#ifdef __wasilibc_unmodified_upstream /* Declare arc4random functions */
#else
#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#include <stdint.h>
uint32_t arc4random(void);
void arc4random_buf(void *, size_t);
uint32_t arc4random_uniform(uint32_t);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
