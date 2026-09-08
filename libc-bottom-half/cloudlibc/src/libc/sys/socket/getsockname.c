#include <errno.h>
#include <common/net.h>
#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

int getsockname(int socket, struct sockaddr *restrict addr, socklen_t *restrict addrlen) {
  __wasi_addr_port_t local_addr;
  __wasi_errno_t error = __wasi_sock_addr_local(socket, &local_addr);
  if (error == __WASI_ERRNO_NOTSOCK) {
    // in that case, return a AF_LOCAL type constant
    // firebox#9EJ — this arm writes through `addr` without going near
    // wasi_to_sockaddr, so it needs the same copy-out check the helper makes.
    // MEASURED before the fix: `getsockname(1, NULL, NULL)` wrote AF_LOCAL into
    // guest linear-memory offset 0 -- ordinary writable module memory, so no
    // fault -- and returned 0. The guest silently corrupted itself and was told
    // it succeeded. Linux reports EFAULT. Checked here rather than at the top of
    // the function because Linux resolves the descriptor first, so
    // EBADF/ENOTSOCK outranks EFAULT.
    if (addr == NULL || addrlen == NULL) {
      errno = EFAULT;
      return -1;
    }
    *addrlen = sizeof(addr->sa_family);
    addr->sa_family = AF_LOCAL;
    return 0;
  }
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  // firebox#9EJ — the conversion's errno was discarded. It is a guest `E*` and
  // must not pass through __wasilibc_errno_from_wasi; see common/net.h. Unlike
  // accept()/recvfrom(), getsockname() has no legal NULL address: Linux reports
  // EFAULT for one, which is exactly what the helper returns.
  int guest_error = wasi_to_sockaddr(&local_addr, addr, addrlen);
  if (guest_error != 0) {
    errno = guest_error;
    return -1;
  }
  return 0;
}
