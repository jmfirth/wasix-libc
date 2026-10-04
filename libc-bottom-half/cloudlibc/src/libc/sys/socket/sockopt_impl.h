// firebox#VA3 -- the one table setsockopt() and getsockopt() are driven by.
//
// Every (level, option) pair a program can name is either in this table or
// unknown. A row says which WASIX socket option carries it and how its value
// is laid out; setsockopt.c and getsockopt.c hold one encoder and one decoder
// per layout and nothing per option.
//
// What the table replaces: setsockopt() answered ENOSYS for every level but
// SOL_SOCKET (two options were remapped by hand in front of that check), and
// read every boolean as `*((int *)&option_value) > 0` -- the address of the
// pointer, not the value -- so any flag could be switched on and none off.
//
// The runtime owns the rest of the answer: which sockets an option applies
// to, which values it accepts, and what it reads as before anyone set it.
// This file only has to get the request there intact.

#ifndef SOCKOPT_IMPL_H
#define SOCKOPT_IMPL_H

#include <sys/socket.h>
#include <sys/time.h>

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/libc.h>

// How an option's value is laid out in the caller's buffer.
enum sockopt_kind {
  SOCKOPT_FLAG,      // int, zero or not            -> sock_{set,get}_opt_flag
  SOCKOPT_INT,       // int                         -> sock_{set,get}_opt_size
  SOCKOPT_BYTE_FLAG, // int, or one byte (IPPROTO_IP accepts both)
  SOCKOPT_BYTE_INT,  // int, or one byte
  SOCKOPT_INADDR,    // struct in_addr / ip_mreq / ip_mreqn -> opt_size
  SOCKOPT_LINGER,    // struct linger               -> sock_{set,get}_opt_time
  SOCKOPT_TIMEVAL,   // struct timeval              -> sock_{set,get}_opt_time
  SOCKOPT_ERROR,     // int errno, read-only        -> sock_get_opt_size
  SOCKOPT_TYPE,      // int SOCK_*, read-only       -> fd_fdstat_get
  SOCKOPT_JOIN4,     // struct ip_mreq, write-only  -> sock_join_multicast_v4
  SOCKOPT_LEAVE4,
  SOCKOPT_JOIN6,     // struct ipv6_mreq, write-only
  SOCKOPT_LEAVE6,
};

enum sockopt_access { SOCKOPT_RW, SOCKOPT_RO, SOCKOPT_WO };

struct sockopt_row {
  int level;
  int name;
  __wasi_sock_option_t option;
  uint8_t kind;
  uint8_t access;
};

static const struct sockopt_row sockopt_table[] = {
    // SOL_SOCKET. The SO_* constants are the WASIX option numbers.
    {SOL_SOCKET, SO_REUSEADDR, __WASI_SOCK_OPTION_REUSE_ADDR, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_REUSEPORT, __WASI_SOCK_OPTION_REUSE_PORT, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_KEEPALIVE, __WASI_SOCK_OPTION_KEEP_ALIVE, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_BROADCAST, __WASI_SOCK_OPTION_BROADCAST, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_DONTROUTE, __WASI_SOCK_OPTION_DONT_ROUTE, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_OOBINLINE, __WASI_SOCK_OPTION_OOB_INLINE, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_RCVBUF, __WASI_SOCK_OPTION_RECV_BUF_SIZE, SOCKOPT_INT, SOCKOPT_RW},
    {SOL_SOCKET, SO_SNDBUF, __WASI_SOCK_OPTION_SEND_BUF_SIZE, SOCKOPT_INT, SOCKOPT_RW},
    {SOL_SOCKET, SO_LINGER, __WASI_SOCK_OPTION_LINGER, SOCKOPT_LINGER, SOCKOPT_RW},
    {SOL_SOCKET, SO_RCVTIMEO, __WASI_SOCK_OPTION_RECV_TIMEOUT, SOCKOPT_TIMEVAL, SOCKOPT_RW},
    {SOL_SOCKET, SO_SNDTIMEO, __WASI_SOCK_OPTION_SEND_TIMEOUT, SOCKOPT_TIMEVAL, SOCKOPT_RW},
    {SOL_SOCKET, SO_ERROR, __WASI_SOCK_OPTION_LAST_ERROR, SOCKOPT_ERROR, SOCKOPT_RO},
    {SOL_SOCKET, SO_TYPE, __WASI_SOCK_OPTION_TYPE, SOCKOPT_TYPE, SOCKOPT_RO},
    {SOL_SOCKET, SO_ACCEPTCONN, __WASI_SOCK_OPTION_LISTENING, SOCKOPT_FLAG, SOCKOPT_RO},
    {SOL_SOCKET, SO_PROTOCOL, __WASI_SOCK_OPTION_PROTO, SOCKOPT_INT, SOCKOPT_RO},
    {SOL_SOCKET, SO_DOMAIN, __WASI_SOCK_OPTION_DOMAIN, SOCKOPT_INT, SOCKOPT_RO},
    // WASIX's own SOL_SOCKET spellings of options Linux keeps at other levels.
    {SOL_SOCKET, SO_NODELAY, __WASI_SOCK_OPTION_NO_DELAY, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_ONLYV6, __WASI_SOCK_OPTION_ONLY_V6, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_MCASTLOOPV4, __WASI_SOCK_OPTION_MULTICAST_LOOP_V4, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_MCASTLOOPV6, __WASI_SOCK_OPTION_MULTICAST_LOOP_V6, SOCKOPT_FLAG, SOCKOPT_RW},
    {SOL_SOCKET, SO_TTL, __WASI_SOCK_OPTION_TTL, SOCKOPT_INT, SOCKOPT_RW},
    {SOL_SOCKET, SO_MCASTTTLV4, __WASI_SOCK_OPTION_MULTICAST_TTL_V4, SOCKOPT_INT, SOCKOPT_RW},
    {SOL_SOCKET, SO_CONNTIMEO, __WASI_SOCK_OPTION_CONNECT_TIMEOUT, SOCKOPT_TIMEVAL, SOCKOPT_RW},
    {SOL_SOCKET, SO_ACCPTIMEO, __WASI_SOCK_OPTION_ACCEPT_TIMEOUT, SOCKOPT_TIMEVAL, SOCKOPT_RW},

    {IPPROTO_TCP, TCP_NODELAY, __WASI_SOCK_OPTION_NO_DELAY, SOCKOPT_FLAG, SOCKOPT_RW},
    {IPPROTO_TCP, TCP_KEEPIDLE, __WASI_SOCK_OPTION_KEEP_IDLE, SOCKOPT_INT, SOCKOPT_RW},
    {IPPROTO_TCP, TCP_KEEPINTVL, __WASI_SOCK_OPTION_KEEP_INTERVAL, SOCKOPT_INT, SOCKOPT_RW},
    {IPPROTO_TCP, TCP_KEEPCNT, __WASI_SOCK_OPTION_KEEP_COUNT, SOCKOPT_INT, SOCKOPT_RW},
    {IPPROTO_TCP, TCP_CORK, __WASI_SOCK_OPTION_CORK, SOCKOPT_FLAG, SOCKOPT_RW},
    {IPPROTO_TCP, TCP_QUICKACK, __WASI_SOCK_OPTION_QUICK_ACK, SOCKOPT_FLAG, SOCKOPT_RW},
    {IPPROTO_TCP, TCP_USER_TIMEOUT, __WASI_SOCK_OPTION_USER_TIMEOUT, SOCKOPT_INT, SOCKOPT_RW},

    {IPPROTO_IP, IP_TTL, __WASI_SOCK_OPTION_TTL, SOCKOPT_BYTE_INT, SOCKOPT_RW},
    {IPPROTO_IP, IP_TOS, __WASI_SOCK_OPTION_TOS, SOCKOPT_BYTE_INT, SOCKOPT_RW},
    {IPPROTO_IP, IP_MULTICAST_TTL, __WASI_SOCK_OPTION_MULTICAST_TTL_V4, SOCKOPT_BYTE_INT, SOCKOPT_RW},
    {IPPROTO_IP, IP_MULTICAST_LOOP, __WASI_SOCK_OPTION_MULTICAST_LOOP_V4, SOCKOPT_BYTE_FLAG, SOCKOPT_RW},
    {IPPROTO_IP, IP_MULTICAST_IF, __WASI_SOCK_OPTION_MULTICAST_IF_V4, SOCKOPT_INADDR, SOCKOPT_RW},
    {IPPROTO_IP, IP_ADD_MEMBERSHIP, __WASI_SOCK_OPTION_NOOP, SOCKOPT_JOIN4, SOCKOPT_WO},
    {IPPROTO_IP, IP_DROP_MEMBERSHIP, __WASI_SOCK_OPTION_NOOP, SOCKOPT_LEAVE4, SOCKOPT_WO},

    {IPPROTO_IPV6, IPV6_V6ONLY, __WASI_SOCK_OPTION_ONLY_V6, SOCKOPT_FLAG, SOCKOPT_RW},
    {IPPROTO_IPV6, IPV6_UNICAST_HOPS, __WASI_SOCK_OPTION_UNICAST_HOPS_V6, SOCKOPT_INT, SOCKOPT_RW},
    {IPPROTO_IPV6, IPV6_MULTICAST_HOPS, __WASI_SOCK_OPTION_MULTICAST_HOPS_V6, SOCKOPT_INT, SOCKOPT_RW},
    {IPPROTO_IPV6, IPV6_MULTICAST_LOOP, __WASI_SOCK_OPTION_MULTICAST_LOOP_V6, SOCKOPT_FLAG, SOCKOPT_RW},
    {IPPROTO_IPV6, IPV6_MULTICAST_IF, __WASI_SOCK_OPTION_MULTICAST_IF_V6, SOCKOPT_INT, SOCKOPT_RW},
    {IPPROTO_IPV6, IPV6_JOIN_GROUP, __WASI_SOCK_OPTION_NOOP, SOCKOPT_JOIN6, SOCKOPT_WO},
    {IPPROTO_IPV6, IPV6_LEAVE_GROUP, __WASI_SOCK_OPTION_NOOP, SOCKOPT_LEAVE6, SOCKOPT_WO},
};

static inline const struct sockopt_row *sockopt_find(int level, int name) {
  for (size_t i = 0; i < sizeof sockopt_table / sizeof sockopt_table[0]; i++) {
    if (sockopt_table[i].level == level && sockopt_table[i].name == name)
      return &sockopt_table[i];
  }
  return NULL;
}

// What the descriptor is, asked before anything else is decided: Linux checks
// the descriptor first, so a bad one is EBADF or ENOTSOCK whatever the option
// (a bad length on a pipe is ENOTSOCK, not EINVAL).
struct sockopt_fd {
  int family; // AF_*, or -1 when the runtime does not report it
  int type;   // SOCK_*
};

// Returns 0 and fills `out`, or returns the errno.
static inline int sockopt_classify(int fd, struct sockopt_fd *out) {
  __wasi_fdstat_t fsb;
  __wasi_errno_t error = __wasi_fd_fdstat_get(fd, &fsb);
  if (error != 0) return EBADF;
  switch (fsb.fs_filetype) {
    case __WASI_FILETYPE_SOCKET_DGRAM: out->type = SOCK_DGRAM; break;
    case __WASI_FILETYPE_SOCKET_STREAM: out->type = SOCK_STREAM; break;
    case __WASI_FILETYPE_SOCKET_SEQPACKET: out->type = SOCK_SEQPACKET; break;
    case __WASI_FILETYPE_SOCKET_RAW: out->type = SOCK_RAW; break;
    default: return ENOTSOCK;
  }
  __wasi_filesize_t family = 0;
  error = __wasi_sock_get_opt_size(fd, __WASI_SOCK_OPTION_DOMAIN, &family);
  out->family = error == 0 ? (int)family : -1;
  return 0;
}

// The errno for an option the table does not carry, which is what Linux
// answers for one its protocol does not define. Setting is ENOPROTOOPT.
// Reading is ENOPROTOOPT at a level the socket has and EOPNOTSUPP at one it
// does not -- except that an AF_INET6 socket answers ENOPROTOOPT for every
// level, and an AF_UNIX socket has no level but SOL_SOCKET in either
// direction.
static inline int sockopt_unknown(const struct sockopt_fd *fd, int level, int reading) {
  if (level == SOL_SOCKET) return ENOPROTOOPT;
  if (fd->family == AF_UNIX) return EOPNOTSUPP;
  if (!reading || fd->family == AF_INET6) return ENOPROTOOPT;
  if (level == IPPROTO_IP) return ENOPROTOOPT;
  if (level == IPPROTO_TCP && fd->type == SOCK_STREAM) return ENOPROTOOPT;
  if (level == IPPROTO_UDP && fd->type == SOCK_DGRAM) return ENOPROTOOPT;
  return EOPNOTSUPP;
}

#endif
