#include <sys/socket.h>
#include <__header_netinet_in.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int socketpair(int domain, int ty, int protocol, int *restrict socket_vector) {
  int fd1, fd2;

  // Linux accepts SOCK_CLOEXEC/SOCK_NONBLOCK OR'd into the type (since
  // 2.6.27), but the runtime's sock_pair parses the raw type value and
  // returns ENOTSUP for anything that isn't a bare SOCK_* type. Mask the
  // flags out and apply each via fcntl after creation, like pipe2 does
  // (firebox#ENM finding C4; same fix as socket.c). The flags also defeated
  // the protocol-defaulting switch below.
  int flags = ty & (SOCK_CLOEXEC | SOCK_NONBLOCK);
  ty &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK);

  if(!protocol) {
    switch (ty)
    {
    case SOCK_STREAM:
      protocol = IPPROTO_TCP;
      break;
    case SOCK_DGRAM:
      protocol = IPPROTO_UDP;
      break;
    }
  }
  __wasi_errno_t error = __wasi_sock_pair(domain, ty, protocol, &fd1, &fd2);
  if (error != 0) {
    errno = error;
    return -1;
  }

  if (flags & SOCK_CLOEXEC) {
    if (fcntl(fd1, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd2, F_SETFD, FD_CLOEXEC) < 0) {
      int saved_errno = errno;
      close(fd1);
      close(fd2);
      errno = saved_errno;
      return -1;
    }
  }
  if (flags & SOCK_NONBLOCK) {
    int fl1 = fcntl(fd1, F_GETFL);
    int fl2 = fcntl(fd2, F_GETFL);
    if (fl1 < 0 || fl2 < 0 ||
        fcntl(fd1, F_SETFL, fl1 | O_NONBLOCK) < 0 ||
        fcntl(fd2, F_SETFL, fl2 | O_NONBLOCK) < 0) {
      int saved_errno = errno;
      close(fd1);
      close(fd2);
      errno = saved_errno;
      return -1;
    }
  }

  socket_vector[0] = fd1;
  socket_vector[1] = fd2;
  return 0;
}
