#include <common/cancel.h>
#include <errno.h>
#include <common/net.h>

#include <sys/socket.h>
#include <__struct_msghdr.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>

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
      errno = error;
      return -1;
    }
    
    struct sockaddr *addr = (struct sockaddr *)msg->msg_name;
    socklen_t *addrlen = &msg->msg_namelen;
    error = wasi_to_sockaddr(&peer_addr, addr, addrlen);
  }
  msg->msg_flags = ro_flags;
  
  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    errno = error;
    return -1;
  }
  return ro_datalen;
}
