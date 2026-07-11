#include <sys/resource.h>
#include <errno.h>
#include <stdint.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#include "libc.h"
#ifndef __wasilibc_unmodified_upstream
#include <__wasilibc_rlimit.h>
// firebox#1GJ: __wasix_resource_set_nofile + __WASI_ERRNO_* (api.h pulls in
// api_firebox.h) — the host wire that makes RLIMIT_NOFILE actually enforced.
#include <wasi/api.h>
#endif

#ifdef __wasilibc_unmodified_upstream
#define MIN(a, b) ((a)<(b) ? (a) : (b))
#define FIX(x) do{ if ((x)>=SYSCALL_RLIM_INFINITY) (x)=RLIM_INFINITY; }while(0)

struct ctx {
	unsigned long lim[2];
	int res;
	int err;
};

static void do_setrlimit(void *p)
{
	struct ctx *c = p;
	if (c->err>0) return;
	c->err = -__syscall(SYS_setrlimit, c->res, c->lim);
}
#endif

#ifndef __wasilibc_unmodified_upstream
// firebox#KZ1: WASI has no host setrlimit syscall, so the process rlimits live
// in this table. Only RLIMIT_DATA and RLIMIT_AS are *enforced* — via the sbrk
// malloc-arena ceiling (__wasilibc_sbrk_max) — because that is the one resource
// wasix-libc controls end to end (dlmalloc is MORECORE-only, HAVE_MMAP == 0, so
// every allocation flows through sbrk). Every other resource is stored so that
// getrlimit() round-trips the value a program set, but is otherwise advisory.
// (RLIMIT_NOFILE is separately enforced by the host fd table, firebox#ASY;
// wiring guest getrlimit(RLIMIT_NOFILE) through to that limit is deliberately
// left to the companion host-import change — this table only echoes it back.)
//
// The table is BSS-zeroed. A resource reports RLIM_INFINITY (its Linux default
// for an unconstrained process) until its bit is raised in `set_mask`; tracking
// "set" separately lets RLIM_INFINITY (all-ones, so != 0) stay the default with
// no runtime initializer and no GNU range-designated static init. RLIM_NLIMITS
// is 16, so a plain `unsigned` mask is wide enough.
//
// Thread-safety matches the sbrk() static-break contract (see sbrk.c): concurrent
// limit changes are not synchronized. Programs set their rlimits once at startup,
// and the conformance callers are single-threaded forked children.
static struct rlimit rlimits[RLIM_NLIMITS];
static unsigned rlimit_set_mask;

// The sbrk ceiling in effect before any rlimit was applied — normally
// UINTPTR_MAX, but an embedder (blink's wasm64 linear-mapping mode,
// firebox#796/#853) may have lowered __wasilibc_sbrk_max first. Captured once,
// on the first RLIMIT_DATA/RLIMIT_AS store, so that re-raising a memory rlimit
// to RLIM_INFINITY restores the embedder's ceiling instead of clobbering it to
// "unlimited".
static uintptr_t sbrk_ceiling_base;
static int sbrk_ceiling_base_captured;

// wasm-ld synthesizes &__heap_base at the start of the heap region; sbrk()
// starts its break there and grows up (see sbrk.c).
extern unsigned char __heap_base;

// RLIMIT_AS caps the *total* virtual address space. In the single-linear-memory
// wasm model the heap high-water (sbrk's break) is the top of used memory, so
// the limit is an absolute ceiling on the break. A limit at or above the
// addressable range imposes no ceiling.
static uintptr_t as_limit_to_ceiling(rlim_t lim) {
	if (lim >= (rlim_t)UINTPTR_MAX)
		return UINTPTR_MAX;
	return (uintptr_t)lim;
}

// RLIMIT_DATA caps the data segment — here the heap that sbrk manages. The heap
// begins at &__heap_base, so the break may advance rlim_cur bytes from there:
// ceiling = __heap_base + rlim_cur. (Static data below __heap_base is not
// counted — a small, more-permissive divergence from Linux that does not affect
// the ENOMEM-at-the-limit semantics this limit exists to provide.)
static uintptr_t data_limit_to_ceiling(rlim_t lim) {
	uintptr_t base = (uintptr_t)&__heap_base;
	if (lim >= (rlim_t)UINTPTR_MAX)
		return UINTPTR_MAX;
	uintptr_t l = (uintptr_t)lim;
	if (l > UINTPTR_MAX - base) // would overflow past the addressable range
		return UINTPTR_MAX;
	return base + l;
}

// Recompute __wasilibc_sbrk_max as the tightest of the embedder/default base
// and whichever of RLIMIT_DATA / RLIMIT_AS are set. Called after a store to
// either of those two resources.
static void apply_memory_rlimits(void) {
	if (!sbrk_ceiling_base_captured) {
		sbrk_ceiling_base = __wasilibc_sbrk_max;
		sbrk_ceiling_base_captured = 1;
	}
	uintptr_t ceiling = sbrk_ceiling_base;
	if (rlimit_set_mask & (1u << RLIMIT_DATA)) {
		uintptr_t c = data_limit_to_ceiling(rlimits[RLIMIT_DATA].rlim_cur);
		if (c < ceiling) ceiling = c;
	}
	if (rlimit_set_mask & (1u << RLIMIT_AS)) {
		uintptr_t c = as_limit_to_ceiling(rlimits[RLIMIT_AS].rlim_cur);
		if (c < ceiling) ceiling = c;
	}
	__wasilibc_sbrk_max = ceiling;
}

void __wasilibc_get_stored_rlimit(int resource, struct rlimit *rlim) {
	if (rlimit_set_mask & (1u << resource)) {
		*rlim = rlimits[resource];
	} else {
		rlim->rlim_cur = RLIM_INFINITY;
		rlim->rlim_max = RLIM_INFINITY;
	}
}
#endif

int setrlimit(int resource, const struct rlimit *rlim)
{
#ifdef __wasilibc_unmodified_upstream
	struct rlimit tmp;
	if (SYSCALL_RLIM_INFINITY != RLIM_INFINITY) {
		tmp = *rlim;
		FIX(tmp.rlim_cur);
		FIX(tmp.rlim_max);
		rlim = &tmp;
	}
	int ret = __syscall(SYS_prlimit64, 0, resource, rlim, 0);
	if (ret != -ENOSYS) return __syscall_ret(ret);

	struct ctx c = {
		.lim[0] = MIN(rlim->rlim_cur, MIN(-1UL, SYSCALL_RLIM_INFINITY)),
		.lim[1] = MIN(rlim->rlim_max, MIN(-1UL, SYSCALL_RLIM_INFINITY)),
		.res = resource, .err = -1
	};
	__synccall(do_setrlimit, &c);
	if (c.err) {
		if (c.err>0) errno = c.err;
		return -1;
	}
	return 0;
#else
	if (resource < 0 || resource >= RLIM_NLIMITS) {
		errno = EINVAL;
		return -1;
	}
	// POSIX: the soft limit may not exceed the hard limit.
	if (rlim->rlim_cur > rlim->rlim_max) {
		errno = EINVAL;
		return -1;
	}
	// Unprivileged processes may lower the hard limit but never raise it; the
	// sandbox grants no CAP_SYS_RESOURCE. The default hard limit is
	// RLIM_INFINITY, so the first setrlimit for a resource always succeeds.
	struct rlimit cur;
	__wasilibc_get_stored_rlimit(resource, &cur);
	if (rlim->rlim_max > cur.rlim_max) {
		errno = EPERM;
		return -1;
	}
	// firebox#1GJ: RLIMIT_NOFILE is enforced by the host fd table (a newly
	// allocated fd *number* >= the soft limit fails with EMFILE), not by this
	// echo table. Route the change to the host BEFORE recording it, so a
	// host-rejected limit fails without desyncing the echo from what is actually
	// enforced. The host holds the authoritative NOFILE hard default (4096),
	// which this table does not (its default is RLIM_INFINITY), so a first-time
	// unprivileged hard-limit raise past the fd-table ceiling is caught here even
	// though the RLIM_INFINITY-based check above passed it. Other resources keep
	// their existing table-only behavior (RLIMIT_DATA/RLIMIT_AS sbrk ceiling).
	if (resource == RLIMIT_NOFILE) {
		__wasi_errno_t e = __wasix_resource_set_nofile(rlim->rlim_cur, rlim->rlim_max);
		if (e != __WASI_ERRNO_SUCCESS) {
			// The firebox sysroot aliases the POSIX errno macros to the
			// __WASI_ERRNO_* numbers, so the raw host errno matches <errno.h>
			// (sched_impl.h relies on the same aliasing).
			errno = (int)e;
			return -1;
		}
	}
	rlimits[resource] = *rlim;
	rlimit_set_mask |= (1u << resource);
	if (resource == RLIMIT_DATA || resource == RLIMIT_AS)
		apply_memory_rlimits();
	return 0;
#endif
}

weak_alias(setrlimit, setrlimit64);
