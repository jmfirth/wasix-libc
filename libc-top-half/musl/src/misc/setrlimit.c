#include <sys/resource.h>
#include <errno.h>
#include <stdint.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#include "libc.h"
#include <wasi/libc.h>
#ifndef __wasilibc_unmodified_upstream
#include <__wasilibc_rlimit.h>
#include "lock.h"
#ifdef _REENTRANT
#include <pthread.h>
#include "pthread_impl.h"
#include "atomic.h"
#endif
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
// (RLIMIT_NOFILE is not in this table at all: the host fd table enforces it,
// firebox#ASY, and setrlimit/getrlimit route to it, firebox#1GJ/#48H.)
//
// The table is BSS-zeroed. A resource reports RLIM_INFINITY (its Linux default
// for an unconstrained process) until its bit is raised in `set_mask`; tracking
// "set" separately lets RLIM_INFINITY (all-ones, so != 0) stay the default with
// no runtime initializer and no GNU range-designated static init. RLIM_NLIMITS
// is 16, so a plain `unsigned` mask is wide enough.
//
// One lock covers the table, set mask, hard-limit checks and memory ceilings.
//
// The lock is held across fork(). A lock some other thread holds at the instant
// of the fork stays held for ever in the child, whose only thread never owned
// it; and _Fork itself reads RLIMIT_NPROC from this table, so such a child
// could not even fork again. The forking thread therefore takes the lock in an
// atfork prepare handler and both sides release it afterwards. While it holds
// the lock that way, its own read inside _Fork must not take it a second time
// (the lock is not recursive): `rlimit_fork_owner` names the holder, and the
// holder reads the table directly -- nobody else can be writing it.
//
// The handlers are registered through pthread_atfork rather than fork.c's lock
// list because that list is made of named symbols, and the shared libc exports
// every symbol it defines: a new one would be a new provider generation.
#ifdef _REENTRANT
static volatile int rlimit_lock[1];
static volatile int rlimit_fork_owner;

static void rlimit_fork_prepare(void) {
	LOCK(rlimit_lock);
	a_store(&rlimit_fork_owner, __pthread_self()->tid);
}

static void rlimit_fork_release(void) {
	a_store(&rlimit_fork_owner, 0);
	UNLOCK(rlimit_lock);
}

__attribute__((constructor))
static void rlimit_fork_register(void) {
	pthread_atfork(rlimit_fork_prepare, rlimit_fork_release, rlimit_fork_release);
}

static int rlimit_held_for_fork_by_caller(void) {
	int owner = rlimit_fork_owner;
	return owner != 0 && owner == __pthread_self()->tid;
}
#endif
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
		sbrk_ceiling_base = __atomic_load_n(&__wasilibc_sbrk_max, __ATOMIC_RELAXED);
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
	__atomic_store_n(&__wasilibc_sbrk_max, ceiling, __ATOMIC_RELAXED);
}

static void get_stored_rlimit_locked(int resource, struct rlimit *rlim) {
	if (rlimit_set_mask & (1u << resource)) {
		*rlim = rlimits[resource];
	} else {
		rlim->rlim_cur = RLIM_INFINITY;
		rlim->rlim_max = RLIM_INFINITY;
	}
}

void __wasilibc_get_stored_rlimit(int resource, struct rlimit *rlim) {
#ifdef _REENTRANT
	if (rlimit_held_for_fork_by_caller()) {
		get_stored_rlimit_locked(resource, rlim);
		return;
	}
#endif
	LOCK(rlimit_lock);
	get_stored_rlimit_locked(resource, rlim);
	UNLOCK(rlimit_lock);
}

static int setrlimit_locked(int resource, const struct rlimit *rlim) {
	if (resource < 0 || resource >= RLIM_NLIMITS) {
		errno = EINVAL;
		return -1;
	}
	// POSIX: the soft limit may not exceed the hard limit.
	if (rlim->rlim_cur > rlim->rlim_max) {
		errno = EINVAL;
		return -1;
	}
	// firebox#1GJ/#48H: RLIMIT_NOFILE lives on the host fd table, which both
	// ENFORCES it (a newly allocated fd *number* >= the soft limit fails with
	// EMFILE) and REPORTS it (getrlimit reads it back through
	// __wasix_resource_get_nofile). The host applies Linux's unprivileged rules
	// itself — EINVAL for soft > hard, EPERM for a hard-limit raise past its
	// finite default (4096) — so NOFILE goes straight there and is never
	// recorded in this table: one source of truth, so what getrlimit reports is
	// what open() enforces. Other resources keep their table-only behavior
	// (RLIMIT_DATA/RLIMIT_AS drive the sbrk ceiling).
	if (resource == RLIMIT_NOFILE) {
		__wasi_errno_t e = __wasix_resource_set_nofile(rlim->rlim_cur, rlim->rlim_max);
		if (e != __WASI_ERRNO_SUCCESS) {
			errno = __wasilibc_errno_from_wasi((int)e);
			return -1;
		}
		return 0;
	}
	// Unprivileged processes may lower the hard limit but never raise it; the
	// sandbox grants no CAP_SYS_RESOURCE. The default hard limit is
	// RLIM_INFINITY, so the first setrlimit for a resource always succeeds.
	struct rlimit cur;
	get_stored_rlimit_locked(resource, &cur);
	if (rlim->rlim_max > cur.rlim_max) {
		errno = EPERM;
		return -1;
	}
	rlimits[resource] = *rlim;
	rlimit_set_mask |= (1u << resource);
	if (resource == RLIMIT_DATA || resource == RLIMIT_AS)
		apply_memory_rlimits();
	return 0;
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
	LOCK(rlimit_lock);
	int ret = setrlimit_locked(resource, rlim);
	UNLOCK(rlimit_lock);
	return ret;
#endif
}

weak_alias(setrlimit, setrlimit64);
