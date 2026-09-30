#include <common/cancel.h>
#include <errno.h>
#include <common/net.h>

#include <sys/socket.h>
#include <__struct_msghdr.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

// The wasix header names roflags bit 0 `HIDDEN`; it is preview1's
// `recv_data_truncated`, and the host sets it for a truncated datagram
// (`__WASI_SOCK_RECV_OUTPUT_DATA_TRUNCATED` in wasi-types).
#ifndef __WASI_ROFLAGS_RECV_DATA_TRUNCATED
#define __WASI_ROFLAGS_RECV_DATA_TRUNCATED ((__wasi_roflags_t)(1 << 0))
#endif

ssize_t recvmsg(int socket, struct msghdr *restrict msg, int flags) {
  __wasi_iovec_t *ri_data = (__wasi_iovec_t *)msg->msg_iov;
  size_t ri_data_len = msg->msg_iovlen;
  __wasi_riflags_t ri_flags = 0;

  if ((flags & MSG_PEEK) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_PEEK; }
  if ((flags & MSG_WAITALL) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_WAITALL; }
  if ((flags & MSG_TRUNC) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_DATA_TRUNCATED; }
  if ((flags & MSG_DONTWAIT) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_DONT_WAIT; }

  __wasi_size_t ro_datalen;
  __wasi_roflags_t ro_flags;
  __wasi_errno_t error;
  int guest_error = 0;
  if (msg->msg_name == NULL) {
    // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
    // already-pending cancel BEFORE parking in the host await.
    __cloudlibc_testcancel();

    error = __wasi_sock_recv(socket,
								ri_data, ri_data_len, ri_flags,
								&ro_datalen,
								&ro_flags);
  } else {
    __wasi_addr_port_t peer_addr;
    error = __wasi_sock_recv_from(socket,
								ri_data, ri_data_len, ri_flags,
								&ro_datalen,
								&ro_flags,
								&peer_addr);
    if (error != 0) {
      // firebox#TWX — a cancel that arrived while we were parked. Keyed on
      // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
      // call never discards what it already consumed. Never returns if a cancel
      // is pending and enabled.
      __cloudlibc_testcancel_if_intr(error);
      errno = __wasilibc_errno_from_wasi(error);
      return -1;
    }
    
    struct sockaddr *addr = (struct sockaddr *)msg->msg_name;
    socklen_t *addrlen = &msg->msg_namelen;
    // firebox#EK0 — ONE VARIABLE CANNOT INHABIT TWO NUMBERING SPACES.
    // `error` carries the host's code from __wasi_sock_recv{,_from} above.
    // wasi_to_sockaddr makes a purely guest-side diagnosis and returns a guest
    // `E*`; letting it land in `error` funnelled it into the shared
    // __wasilibc_errno_from_wasi below, which translates a guest constant a
    // second time -- invisible today, wrong after #87F step 4. Same split as
    // pread/pwrite.
    if (wasi_addr_is_unnamed_unix(&peer_addr)) {
      // firebox#15W — see common/net.h: no address, a zero length.
      *addrlen = 0;
    } else {
      guest_error = wasi_to_sockaddr(&peer_addr, addr, addrlen);
    }
  }
  // firebox#MX0 — `ro_flags` is the WASI roflags word, not a set of MSG_*
  // bits: the host reports a datagram longer than the buffer as
  // `__WASI_ROFLAGS_RECV_DATA_TRUNCATED`, which Linux spells MSG_TRUNC.
  // Copying the word raw left msg_flags 0 on a truncated read (MEASURED).
  msg->msg_flags = 0;
  if ((ro_flags & __WASI_ROFLAGS_RECV_DATA_TRUNCATED) != 0) { msg->msg_flags |= MSG_TRUNC; }

  if (guest_error != 0) {
    // No host call is outstanding on this path -- __wasi_sock_recv_from
    // already returned 0 -- so there is no parked call to test a cancel
    // against and nothing to translate.
    errno = guest_error;
    return -1;
  }

  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }
  return ro_datalen;
}
