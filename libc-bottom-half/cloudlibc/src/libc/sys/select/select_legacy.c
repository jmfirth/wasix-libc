// firebox#D7S — the LEGACY select()/pselect() ABI, kept under the unversioned
// link names `select` and `pselect`.
//
// Until D7S, wasi-libc's fd_set was cloudlibc's {size_t __nfds; int __fds[]}
// list and the FD_* operations were static inlines over it, so every consumer
// compiled against that header baked the list layout into its own code and
// called the unversioned `select`/`pselect`. That includes every pre-D7S
// static object, every pre-D7S dynamic consumer (it imports env.select /
// env.pselect from its libc provider, and a provider successor is substituted
// for its generation only if it still exports those names with those types),
// and the Rust libc crate, whose wasi `fd_set` is this same list.
//
// <sys/select.h> now defines the POSIX bitmask and redirects `select` and
// `pselect` to `__select_fdmask`/`__pselect_fdmask`, so new code never binds
// here. Keeping these names on the list decoder, instead of reusing them for the
// bitmask, is what makes a mixed-vintage link correct rather than a silent
// reinterpretation of one layout as the other. This file therefore must NOT
// include <sys/select.h>: its redirect would rename the definitions below.
//
// Retirement: when no shipped consumer imports `select`/`pselect` from a
// provider and the Rust libc crate's wasi fd_set is the bitmask.

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wasi/api.h>

#include <__macro_FD_SETSIZE.h>
#include <__struct_timeval.h>

typedef struct {
  size_t __nfds;
  int __fds[FD_SETSIZE];
} legacy_fd_set;

#define FDMASK_BITS ((int)(8 * sizeof(unsigned long)))

int __pselect_fdmask(int, unsigned long *, unsigned long *, unsigned long *,
                     const struct timespec *, const sigset_t *);

// A list-layout set cannot hold more than FD_SETSIZE entries; anything larger
// was not built by the legacy FD_SET (it is a bitmask handed to the legacy
// name, e.g. through FFI or a hand-written prototype). Refuse it loudly rather
// than walking past the object. Negative fds cannot be represented by any
// bitmask and are never open: EBADF, as the host would have reported.
//
// Review F4: the bitmask handed on below spans every listed fd, so its size
// is chosen by the caller's VALUES, not by the at most FD_SETSIZE entries. One
// entry naming INT_MAX-1 with nfds == INT_MAX sized it at ~256 MiB per set,
// on the stack, before anything was validated. A listed fd at or above
// FD_SETSIZE is therefore proven open here, BEFORE anything is allocated: one
// that is not is EBADF, exactly what the host reports for it. The span is then
// bounded by a descriptor that exists, and the masks below never take an
// input-sized stack allocation.
static int legacy_check(const legacy_fd_set *set, int nfds, int *top) {
  if (set == NULL)
    return 0;
  if (set->__nfds > FD_SETSIZE) {
    errno = EINVAL;
    return -1;
  }
  for (size_t i = 0; i < set->__nfds; ++i) {
    int fd = set->__fds[i];
    if (fd >= nfds)
      continue;  // Outside the caller's range, as before: ignored.
    if (fd < 0) {
      errno = EBADF;
      return -1;
    }
    if (fd >= FD_SETSIZE) {
      __wasi_fdstat_t st;
      if (__wasi_fd_fdstat_get(fd, &st) != 0) {
        errno = EBADF;
        return -1;
      }
    }
    if (fd + 1 > *top)
      *top = fd + 1;
  }
  return 0;
}

static void legacy_to_mask(const legacy_fd_set *set, int top,
                           unsigned long *mask) {
  for (size_t i = 0; i < set->__nfds; ++i) {
    int fd = set->__fds[i];
    if (fd >= 0 && fd < top)
      mask[fd / FDMASK_BITS] |= 1UL << (fd % FDMASK_BITS);
  }
}

static void mask_to_legacy(const unsigned long *mask, int top,
                           legacy_fd_set *set) {
  set->__nfds = 0;
  for (int fd = 0; fd < top; ++fd) {
    if (mask[fd / FDMASK_BITS] & (1UL << (fd % FDMASK_BITS)))
      set->__fds[set->__nfds++] = fd;
  }
}

int pselect(int nfds, legacy_fd_set *restrict readfds,
            legacy_fd_set *restrict writefds, legacy_fd_set *restrict errorfds,
            const struct timespec *restrict timeout, const sigset_t *sigmask) {
  if (nfds < 0) {
    errno = EINVAL;
    return -1;
  }
  int top = 0;
  if (legacy_check(readfds, nfds, &top) != 0 ||
      legacy_check(writefds, nfds, &top) != 0)
    return -1;
  if (errorfds != NULL && errorfds->__nfds > FD_SETSIZE) {
    errno = EINVAL;
    return -1;
  }

  // `top` is at most the largest listed fd + 1, and legacy_check proved any
  // fd at or above FD_SETSIZE open. The common case fits the fixed FD_SETSIZE
  // masks; a span past them (a raised RLIMIT_NOFILE) is heap-allocated, sized
  // by a descriptor that exists.
  enum { SMALL_WORDS = FD_SETSIZE / FDMASK_BITS };
  unsigned long small[2][SMALL_WORDS];
  unsigned long *rmask = small[0], *wmask = small[1], *heap = NULL;
  size_t words = ((size_t)top + FDMASK_BITS - 1) / FDMASK_BITS;
  if (words > SMALL_WORDS) {
    heap = calloc(2 * words, sizeof(unsigned long));
    if (heap == NULL) {
      errno = ENOMEM;
      return -1;
    }
    rmask = heap;
    wmask = heap + words;
  } else {
    memset(small, 0, sizeof small);
  }
  if (readfds != NULL)
    legacy_to_mask(readfds, top, rmask);
  if (writefds != NULL)
    legacy_to_mask(writefds, top, wmask);

  int ready = __pselect_fdmask(top, readfds != NULL ? rmask : NULL,
                               writefds != NULL ? wmask : NULL, NULL, timeout,
                               sigmask);
  if (ready >= 0) {
    if (readfds != NULL)
      mask_to_legacy(rmask, top, readfds);
    if (writefds != NULL)
      mask_to_legacy(wmask, top, writefds);
    if (errorfds != NULL)
      errorfds->__nfds = 0;
  }
  if (heap != NULL) {
    int saved = errno;
    free(heap);
    errno = saved;
  }
  return ready;
}

int select(int nfds, legacy_fd_set *restrict readfds,
           legacy_fd_set *restrict writefds, legacy_fd_set *restrict errorfds,
           struct timeval *restrict timeout) {
  if (timeout != NULL) {
    if (timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) {
      errno = EINVAL;
      return -1;
    }
    struct timespec ts = {.tv_sec = timeout->tv_sec,
                          .tv_nsec = (long)timeout->tv_usec * 1000};
    return pselect(nfds, readfds, writefds, errorfds, &ts, NULL);
  }
  return pselect(nfds, readfds, writefds, errorfds, NULL, NULL);
}
