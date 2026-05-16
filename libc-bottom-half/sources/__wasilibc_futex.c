#include <wasi/api.h>
#include <sys/types.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>

/* Firebox #399 G.3: chokepoint trace.
 *
 * __wasilibc_futex_wait_wasix is the SOLE bottom-half entry point that
 * every wasm-side futex_wait must traverse before reaching the host
 * syscall (__wasi_futex_wait). Whether the caller is musl __wait, musl
 * __timedwait, dlmalloc's internal mutex, a wasix-libc bottom-half
 * helper, or LLVM-emitted intrinsic dispatch — it ALL flows through
 * here.
 *
 * G.2 (#398) instrumented the musl __wait path with caller __FILE__:
 * __LINE__ capture and observed ZERO wait_caller events on bash's hang.
 * That falsifies the musl-shim hypothesis but leaves open: dlmalloc,
 * compiler-rt, LLVM-emitted memory.atomic.wait32, or some other
 * non-musl path.
 *
 * G.3 closes that loop: every futex_wait emits a wasix_futex_wait_pre
 * event with {addr, val, op, timeout_ns}. Cross-referencing the
 * trace against G.2's wait_caller events:
 *   - If a wasix_futex_wait_pre event is PRECEDED by a matching
 *     wait_caller event (same pid, same addr, same val): the caller
 *     is a musl primitive (already pinned by G.2).
 *   - If a wasix_futex_wait_pre event is NOT preceded by wait_caller:
 *     the caller is non-musl. Cross-reference `addr` with bash.wasm's
 *     symbol table / static-data / heap layout to identify the
 *     specific dlmalloc / compiler-rt / wasix-libc helper.
 *
 * Caller __FILE__:__LINE__ capture at THIS site is not useful: we'd
 * only ever see this file's own line. Real caller-PC would require
 * __builtin_return_address(0), which clang refuses to lower for
 * non-Emscripten wasm32 (documented in dlmalloc.c:311 + G.2 report
 * §B.1). The addr+val pair, combined with bash's static layout, is
 * sufficient pin material per #386 + #394.
 */
#ifdef __FIREBOX_TRACE_MUSL_LOCK__
#include "firebox_lock_trace.h"
#endif

int __wasilibc_futex_wait_wasix(volatile void *addr, int op, int expected, int64_t max_wait_ns) {
  if ((((intptr_t)addr) & 3) != 0) {
    return -EINVAL;
  }

  __wasi_bool_t woken = __WASI_BOOL_FALSE;

  __wasi_option_timestamp_t timeout;
  if (max_wait_ns > 0) {
    timeout.tag = __WASI_OPTION_SOME;
    timeout.u.none = max_wait_ns;
  } else {
    timeout.tag = __WASI_OPTION_NONE;
    timeout.u.none = 0;
  }

  // int ret = __builtin_wasm_memory_atomic_wait32((int *)addr, val, max_wait_ns);
  // memory.atomic.wait32 returns:
  //   0 => "ok", woken by another agent.
  //   1 => "not-equal", loaded value != expected value
  //   2 => "timed-out", the timeout expired
  volatile int *paddr = (volatile int *)addr;
  if (*paddr != expected) {
#ifdef __FIREBOX_TRACE_MUSL_LOCK__
    /* Fast-path exit; record so we can distinguish from a real wait. */
    FIREBOX_LOCK_TRACE("wasix_futex_wait_skip",
                       "__wasilibc_futex_wait_wasix_not_equal",
                       addr, expected);
#endif
    return -EWOULDBLOCK;
  }

#ifdef __FIREBOX_TRACE_MUSL_LOCK__
  /* Trace BEFORE the syscall — guarantees the event is observable in
   * stderr before the wait blocks. The op/timeout fields are folded
   * into the site tag to keep the existing format string stable. */
  FIREBOX_LOCK_TRACE("wasix_futex_wait_pre",
                     (max_wait_ns > 0) ? "__wasilibc_futex_wait_wasix_timed"
                                       : "__wasilibc_futex_wait_wasix_indef",
                     addr, expected);
#endif

  if (__wasi_futex_wait((uint32_t*)addr, expected, &timeout, &woken) != 0) {
    __builtin_trap();
  }

#ifdef __FIREBOX_TRACE_MUSL_LOCK__
  /* Trace AFTER the syscall returns. If the wait woke up legitimately
   * we see this event; if the process hangs in the host syscall we
   * see only the pre event and the trace stops. */
  FIREBOX_LOCK_TRACE("wasix_futex_wait_post",
                     (woken == __WASI_BOOL_TRUE) ? "__wasilibc_futex_wait_wasix_woken"
                                                 : "__wasilibc_futex_wait_wasix_unwoken",
                     addr, *paddr);
#endif

  if (woken == __WASI_BOOL_FALSE && *paddr == expected) {
    return -ETIMEDOUT;
  }
  return 0;
}

int __wasilibc_futex_wake_wasix(int* futex, int cnt) {
  __wasi_bool_t woken = __WASI_BOOL_FALSE;
#ifdef __FIREBOX_TRACE_MUSL_LOCK__
  FIREBOX_LOCK_TRACE("wasix_futex_wake_pre",
                     (cnt == INT_MAX) ? "__wasilibc_futex_wake_all"
                                      : "__wasilibc_futex_wake_one",
                     futex, cnt);
#endif
  if (cnt == INT_MAX) {
    int ret = __wasi_futex_wake_all((uint32_t*)futex, &woken);
    if (ret != 0) {
      return -ret;
    }
  } else {
    for (int n = 0; n < cnt; n++) {
      int ret = __wasi_futex_wake((uint32_t*)futex, &woken);
      if (ret != 0) {
        return -ret;
      }
    }
  }
#ifdef __FIREBOX_TRACE_MUSL_LOCK__
  FIREBOX_LOCK_TRACE("wasix_futex_wake_post",
                     (woken == __WASI_BOOL_TRUE) ? "__wasilibc_futex_wake_woke"
                                                 : "__wasilibc_futex_wake_noop",
                     futex, cnt);
#endif
  return 0;
}
