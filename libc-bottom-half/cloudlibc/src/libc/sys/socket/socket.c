#include <sys/socket.h>
#include <__header_netinet_in.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int socket(int domain, int ty, int protocol) {
  int fd;

  // Linux accepts SOCK_CLOEXEC/SOCK_NONBLOCK OR'd into the type (since
  // 2.6.27), but the runtime's sock_open parses the raw type value and
  // returns ENOTSUP for anything that isn't a bare SOCK_* type — so before
  // this masking, socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0) hard-failed
  // and every ported daemon using the flags broke at socket() (firebox#ENM
  // finding C4). The flags also defeated the protocol-defaulting switch
  // below, which compares the full value. Mask them out and apply each via
  // fcntl after creation, like pipe2 does.
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
  __wasi_errno_t error = __wasi_sock_open(domain, ty, protocol, &fd);
  if (error != 0) {
    errno = error;
    return -1;
  }

  if (flags & SOCK_CLOEXEC) {
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
      int saved_errno = errno;
      close(fd);
      errno = saved_errno;
      return -1;
    }
  }
  if (flags & SOCK_NONBLOCK) {
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) {
      int saved_errno = errno;
      close(fd);
      errno = saved_errno;
      return -1;
    }
  }

  return fd;
}
