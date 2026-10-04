// firebox#VA3 -- setsockopt(), driven by the table in sockopt_impl.h.

#include "sockopt_impl.h"

// An `int` from a buffer Linux would read one from. `byte_ok` is the
// IPPROTO_IP rule: a buffer shorter than an int is read as one unsigned char.
// The length is judged before the pointer, as Linux judges them.
static int read_int(const void *value, socklen_t len, int byte_ok, int *out) {
  if (len < sizeof(int) && !(byte_ok && len >= 1)) return EINVAL;
  if (value == NULL) return EFAULT;
  if (len >= sizeof(int)) {
    memcpy(out, value, sizeof(int));
    return 0;
  }
  if (byte_ok && len >= 1) {
    *out = *(const unsigned char *)value;
    return 0;
  }
  return EINVAL;
}

// A group address as the runtime's join/leave imports read it: sixteen-bit
// segments in host order.
static void ip6_segments(const struct in6_addr *in, __wasi_addr_ip6_t *out) {
  const uint8_t *b = in->s6_addr;
  memset(out, 0, sizeof *out);
  out->n0 = (uint16_t)(b[0] << 8 | b[1]);
  out->n1 = (uint16_t)(b[2] << 8 | b[3]);
  out->n2 = (uint16_t)(b[4] << 8 | b[5]);
  out->n3 = (uint16_t)(b[6] << 8 | b[7]);
  out->h0 = (uint16_t)(b[8] << 8 | b[9]);
  out->h1 = (uint16_t)(b[10] << 8 | b[11]);
  out->h2 = (uint16_t)(b[12] << 8 | b[13]);
  out->h3 = (uint16_t)(b[14] << 8 | b[15]);
}

static int fail(int error) {
  errno = error;
  return -1;
}

static int done(__wasi_errno_t error) {
  return error == 0 ? 0 : fail(__wasilibc_errno_from_wasi(error));
}

int setsockopt(int socket, int level, int option_name, const void *restrict option_value,
               socklen_t option_len) {
  struct sockopt_fd fd;
  int error = sockopt_classify(socket, &fd);
  if (error != 0) return fail(error);

  const struct sockopt_row *row = sockopt_find(level, option_name);
  if (row == NULL || row->access == SOCKOPT_RO || sockopt_legacy(&fd, row))
    return fail(sockopt_unknown(&fd, level, 0));

  int value;
  switch (row->kind) {
    case SOCKOPT_FLAG:
    case SOCKOPT_BYTE_FLAG:
      error = read_int(option_value, option_len, row->kind == SOCKOPT_BYTE_FLAG, &value);
      if (error != 0) return fail(error);
      return done(__wasi_sock_set_opt_flag(socket, sockopt_flag_wire(&fd, row),
                                           value != 0 ? __WASI_BOOL_TRUE : __WASI_BOOL_FALSE));

    case SOCKOPT_INT:
    case SOCKOPT_BYTE_INT:
      error = read_int(option_value, option_len, row->kind == SOCKOPT_BYTE_INT, &value);
      if (error != 0) return fail(error);
      // Sign-extended, so that -1 ("restore the default") arrives as -1.
      return done(__wasi_sock_set_opt_size(socket, row->option, (__wasi_filesize_t)(int64_t)value));

    case SOCKOPT_INADDR: {
      // IP_MULTICAST_IF takes an in_addr, an ip_mreq or an ip_mreqn; in the
      // two request forms the interface address is the second member. The
      // value crosses as the address in host byte order, with the interface
      // index of an ip_mreqn above it: Linux goes by the index when there is
      // one, and reads the option back as the address.
      struct in_addr addr;
      uint64_t index = 0;
      if (option_len < sizeof(struct in_addr)) return fail(EINVAL);
      if (option_value == NULL) return fail(EFAULT);
      if (option_len >= sizeof(struct ip_mreqn)) {
        struct ip_mreqn mreqn;
        memcpy(&mreqn, option_value, sizeof mreqn);
        addr = mreqn.imr_address;
        // No interface has a negative index or one past 31 bits.
        if (mreqn.imr_ifindex < 0) return fail(EADDRNOTAVAIL);
        index = (uint64_t)mreqn.imr_ifindex;
      } else if (option_len >= sizeof(struct ip_mreq)) {
        memcpy(&addr, (const char *)option_value + offsetof(struct ip_mreq, imr_interface),
               sizeof addr);
      } else {
        memcpy(&addr, option_value, sizeof addr);
      }
      return done(__wasi_sock_set_opt_size(socket, row->option, index << 32 | ntohl(addr.s_addr)));
    }

    case SOCKOPT_LINGER: {
      struct linger linger;
      if (option_len < sizeof linger) return fail(EINVAL);
      if (option_value == NULL) return fail(EFAULT);
      memcpy(&linger, option_value, sizeof linger);
      // The tag is l_onoff; l_linger travels either way, and the runtime
      // decides what it means with lingering off.
      __wasi_option_timestamp_t tm;
      tm.tag = linger.l_onoff != 0 ? __WASI_OPTION_SOME : __WASI_OPTION_NONE;
      tm.u.some = (__wasi_timestamp_t)(linger.l_linger < 0 ? 0 : linger.l_linger) * 1000000000ULL;
      return done(__wasi_sock_set_opt_time(socket, row->option, &tm));
    }

    case SOCKOPT_TIMEVAL: {
      struct timeval tv;
      if (option_len < sizeof tv) return fail(EINVAL);
      if (option_value == NULL) return fail(EFAULT);
      memcpy(&tv, option_value, sizeof tv);
      if (tv.tv_usec < 0 || tv.tv_usec >= 1000000) return fail(EDOM);
      __wasi_option_timestamp_t tm;
      tm.tag = tv.tv_sec > 0 || tv.tv_usec > 0 ? __WASI_OPTION_SOME : __WASI_OPTION_NONE;
      tm.u.some = tv.tv_sec < 0 ? 0 : (tv.tv_sec * 1000000000ULL) + (tv.tv_usec * 1000ULL);
      return done(__wasi_sock_set_opt_time(socket, row->option, &tm));
    }

    case SOCKOPT_JOIN4:
    case SOCKOPT_LEAVE4: {
      // An ip_mreqn starts with the same two addresses as an ip_mreq and
      // ends with an interface index, which decides when it is not zero.
      // The imports carry an interface address and nothing else, so the
      // index crosses as the address 0.x.y.z -- the BSD convention for an
      // index where only an address fits; no interface has such an address.
      struct ip_mreq mreq;
      if (option_len < sizeof mreq) return fail(EINVAL);
      if (option_value == NULL) return fail(EFAULT);
      memcpy(&mreq, option_value, sizeof mreq);
      if (option_len >= sizeof(struct ip_mreqn)) {
        struct ip_mreqn mreqn;
        memcpy(&mreqn, option_value, sizeof mreqn);
        if (mreqn.imr_ifindex != 0) {
          // More than 24 bits is more than any interface has.
          if (mreqn.imr_ifindex < 0 || mreqn.imr_ifindex > 0x00ffffff) return fail(ENODEV);
          mreq.imr_interface.s_addr = htonl((uint32_t)mreqn.imr_ifindex);
        }
      }
      __wasi_addr_ip4_t group, iface;
      memcpy(&group, &mreq.imr_multiaddr, sizeof group);
      memcpy(&iface, &mreq.imr_interface, sizeof iface);
      return done(row->kind == SOCKOPT_JOIN4
                      ? __wasi_sock_join_multicast_v4(socket, &group, &iface)
                      : __wasi_sock_leave_multicast_v4(socket, &group, &iface));
    }

    case SOCKOPT_JOIN6:
    case SOCKOPT_LEAVE6: {
      struct ipv6_mreq mreq;
      if (option_len < sizeof mreq) return fail(EINVAL);
      if (option_value == NULL) return fail(EFAULT);
      memcpy(&mreq, option_value, sizeof mreq);
      __wasi_addr_ip6_t group;
      ip6_segments(&mreq.ipv6mr_multiaddr, &group);
      return done(row->kind == SOCKOPT_JOIN6
                      ? __wasi_sock_join_multicast_v6(socket, &group, mreq.ipv6mr_interface)
                      : __wasi_sock_leave_multicast_v6(socket, &group, mreq.ipv6mr_interface));
    }
  }
  return fail(sockopt_unknown(&fd, level, 0));
}
