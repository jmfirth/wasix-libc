#ifndef __wasilibc___wasilibc_rlimit_h
#define __wasilibc___wasilibc_rlimit_h

#include <stdint.h>
#include <sys/resource.h>

// firebox#KZ1: wasix-internal glue between setrlimit()/getrlimit() and the
// sbrk() malloc-arena ceiling. This declares the two symbols the rlimit
// implementation shares across translation units so no source has to spell out
// a bare `extern` of its own (which would silently drift from the definition).

// The upper bound sbrk() will grow the malloc arena to, defined in
// libc-bottom-half/sources/sbrk.c. sbrk() returns -1/ENOMEM once the break
// would move past this address, so dlmalloc (MORECORE-only, HAVE_MMAP == 0)
// fails an allocation cleanly with ENOMEM at the ceiling instead of trapping on
// an over-grow. Default is UINTPTR_MAX (= no ceiling); embedders (blink's
// wasm64 linear-mapping mode, firebox#796/#853) and now setrlimit(RLIMIT_DATA/
// RLIMIT_AS) lower it. See sbrk.c for the ownership/growth model.
extern uintptr_t __wasilibc_sbrk_max;

// Fill *rlim with the limits last stored by setrlimit() for `resource`, or
// {RLIM_INFINITY, RLIM_INFINITY} if the process never set that resource.
// `resource` must already be range-checked (0 <= resource < RLIM_NLIMITS).
// Defined in setrlimit.c, which owns the process rlimit table.
void __wasilibc_get_stored_rlimit(int resource, struct rlimit *rlim);

#endif
