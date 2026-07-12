#include <wasi/api.h>
#include <sys/types.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>

int __wasilibc_futex_wait_wasix(volatile void *addr, int op, int expected, int64_t max_wait_ns) {
  if ((((intptr_t)addr) & 3) != 0) {
    return -EINVAL;
  }

  __wasi_bool_t woken = __WASI_BOOL_FALSE;
  
  /* firebox#M3T — the timed/untimed split is SIGNED: only a NEGATIVE
   * max_wait_ns (the -1 "no timespec" sentinel from __wait.c /
   * __timedwait.c's __futex4_cp) means wait-indefinitely. A ZERO relative
   * timeout is a legal, common value — musl's __timedwait_cp computes
   * `rel = abs - now` and produces rel=={0,0} whenever the deadline lands
   * on the CURRENT clock tick (pthread_cond_timedwait with abs==now: ruby
   * `sleep 0`, sem_timedwait(&ts=now), any timed wait whose remaining time
   * rounds to 0ns). On Linux futex(FUTEX_WAIT, ts={0,0}) returns ETIMEDOUT
   * immediately; the previous `> 0` test collapsed that case into the
   * INFINITE arm, turning a should-expire-now wait into a permanent park —
   * the #M3T/#HRF ruby `Thread.new{sleep 0}.join` wedge (~50% per run: the
   * flip is whether the clock ticked between the caller computing `abs`
   * and __timedwait_cp re-reading it — ticked → rel<0 → early ETIMEDOUT;
   * not ticked → rel==0 → this arm). The host handles Some(0) faithfully
   * (immediate TimedOut), so 0 belongs in the SOME arm. */
  __wasi_option_timestamp_t timeout;
  if (max_wait_ns >= 0) {
    timeout.tag = __WASI_OPTION_SOME;
    timeout.u.some = max_wait_ns;
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
    return -EWOULDBLOCK;
  }

  if (__wasi_futex_wait((uint32_t*)addr, expected, &timeout, &woken) != 0) {
    __builtin_trap();
  }

  if (woken == __WASI_BOOL_FALSE && *paddr == expected) {
    return -ETIMEDOUT;
  }
  return 0;
}

int __wasilibc_futex_wake_wasix(int* futex, int cnt) {
  __wasi_bool_t woken = __WASI_BOOL_FALSE;
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
  return 0;
}
