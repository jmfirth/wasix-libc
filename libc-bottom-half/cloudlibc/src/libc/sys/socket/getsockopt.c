// firebox#VA3 -- getsockopt(), driven by the table in sockopt_impl.h.

#include "sockopt_impl.h"

static int fail(int error) {
  errno = error;
  return -1;
}

// Hands `size` bytes back the way Linux does: as many as the caller has room
// for, and the count it wrote. The old code always reported the full size,
// whatever it had copied.
static int give(void *restrict option_value, socklen_t *restrict option_len, const void *value,
                size_t size) {
  // The length was read before the option was; the value is written last,
  // so a null buffer is found out here, as on Linux.
  if (option_value == NULL) return fail(EFAULT);
  socklen_t n = *option_len < size ? *option_len : (socklen_t)size;
  memcpy(option_value, value, n);
  *option_len = n;
  return 0;
}

int getsockopt(int socket, int level, int option_name, void *restrict option_value,
               socklen_t *restrict option_len) {
  struct sockopt_fd fd;
  int error = sockopt_classify(socket, &fd);
  if (error != 0) return fail(error);

  if (option_len == NULL) return fail(EFAULT);

  const struct sockopt_row *row = sockopt_find(level, option_name);
  if (row == NULL || row->access == SOCKOPT_WO || sockopt_legacy(&fd, row))
    return fail(sockopt_unknown(&fd, level, 1));

  switch (row->kind) {
    case SOCKOPT_FLAG:
    case SOCKOPT_BYTE_FLAG: {
      __wasi_bool_t on = 0;
      __wasi_errno_t wasi = __wasi_sock_get_opt_flag(socket, sockopt_flag_wire(&fd, row), &on);
      if (wasi != 0) return fail(__wasilibc_errno_from_wasi(wasi));
      int value = on == __WASI_BOOL_TRUE ? 1 : 0;
      return give(option_value, option_len, &value, sizeof value);
    }

    case SOCKOPT_INT:
    case SOCKOPT_BYTE_INT: {
      __wasi_filesize_t size = 0;
      __wasi_errno_t wasi = __wasi_sock_get_opt_size(socket, row->option, &size);
      if (wasi != 0) return fail(__wasilibc_errno_from_wasi(wasi));
      int value = (int)(int64_t)size;
      return give(option_value, option_len, &value, sizeof value);
    }

    case SOCKOPT_INADDR: {
      __wasi_filesize_t size = 0;
      __wasi_errno_t wasi = __wasi_sock_get_opt_size(socket, row->option, &size);
      if (wasi != 0) return fail(__wasilibc_errno_from_wasi(wasi));
      struct in_addr addr = {htonl((uint32_t)size)};
      return give(option_value, option_len, &addr, sizeof addr);
    }

    case SOCKOPT_ERROR: {
      // firebox#H8V: the pending error travels as the wasi errno number
      // (0 == none) and is consumed by this read, as Linux clears sk->sk_err.
      __wasi_filesize_t pending = 0;
      __wasi_errno_t wasi = __wasi_sock_get_opt_size(socket, row->option, &pending);
      if (wasi != 0) return fail(__wasilibc_errno_from_wasi(wasi));
      int value = pending == 0 ? 0 : __wasilibc_errno_from_wasi((__wasi_errno_t)pending);
      return give(option_value, option_len, &value, sizeof value);
    }

    case SOCKOPT_TYPE:
      // A socket whose type the runtime reports nowhere is not one this
      // file may invent a type for.
      if (fd.type < 0) return fail(ENOPROTOOPT);
      return give(option_value, option_len, &fd.type, sizeof fd.type);

    case SOCKOPT_LINGER: {
      __wasi_option_timestamp_t tm;
      __wasi_errno_t wasi = __wasi_sock_get_opt_time(socket, row->option, &tm);
      if (wasi != 0) return fail(__wasilibc_errno_from_wasi(wasi));
      struct linger linger;
      linger.l_onoff = tm.tag == __WASI_OPTION_SOME ? 1 : 0;
      linger.l_linger = (int)(tm.u.some / 1000000000ULL);
      return give(option_value, option_len, &linger, sizeof linger);
    }

    case SOCKOPT_TIMEVAL: {
      __wasi_option_timestamp_t tm;
      __wasi_errno_t wasi = __wasi_sock_get_opt_time(socket, row->option, &tm);
      if (wasi != 0) return fail(__wasilibc_errno_from_wasi(wasi));
      struct timeval tv;
      memset(&tv, 0, sizeof tv);
      if (tm.tag == __WASI_OPTION_SOME) {
        tv.tv_sec = tm.u.some / 1000000000ULL;
        tv.tv_usec = (tm.u.some % 1000000000ULL) / 1000ULL;
      }
      return give(option_value, option_len, &tv, sizeof tv);
    }
  }
  return fail(sockopt_unknown(&fd, level, 1));
}
