/* firebox#25D / #9FB: getifaddrs over __wasix_port_if_list.
 *
 * History. firebox#YZN bound this definition to its POSIX name (it used to be
 * `getif_addrs` over a private `struct if_addrs`, so every caller died at link).
 * It then enumerated through __wasi_port_addr_list, which models ONE anonymous
 * interface: no name, no index, no flags, no netmask. Native `--net` never
 * implemented it (#9FB), so getifaddrs returned ENOTSUP where Linux lists `lo`
 * and the host's interfaces, and where it did answer (the loopback backend) every
 * node had ifa_name == NULL — which crashes nearly every consumer.
 *
 * Now one node per host record, in the host's getifaddrs order:
 *   - a LINK record becomes an AF_PACKET `struct sockaddr_ll` (Linux's shape)
 *     carrying the index, the host's hardware type (ARPHRD_*) and the whole
 *     hardware address (up to MAX_ADDR_LEN, 32); its link broadcast/peer
 *     address, when the host has one, becomes ifa_broadaddr, as on Linux;
 *   - an INET record becomes AF_INET/AF_INET6 with ifa_netmask built from the
 *     prefix length, and ifa_broadaddr / ifa_dstaddr from the record's aux
 *     address, which carries its own IPv6 scope id.
 * Names are the host's own bytes (lo0/en0 on a macOS host): guest sockets are
 * host sockets. ifa_data (Linux link statistics) is NULL: the host does not
 * supply them, and an absent pointer is a loud failure where invented counters
 * would be a quiet lie.
 *
 * Every member of a node is its own allocation, which is the contract
 * freeifaddrs.c frees by. Without an interface model (no --net; the browser)
 * the call fails with ENOTSUP, as before.
 *
 * musl's network/getifaddrs.c is NOT used: it enumerates over netlink, which
 * WASIX does not provide. */

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <_/cdefs.h>
#include <wasi/api.h>
#include <wasi/libc.h>

/* sockaddr_ll's sll_addr holds 8 bytes; a record may carry up to 32 (Linux
 * MAX_ADDR_LEN, e.g. 20-byte InfiniBand). musl's getifaddrs extends the array
 * the same way; callers still read it through struct sockaddr_ll. */
struct sockaddr_ll_wide {
  unsigned short sll_family, sll_protocol;
  int sll_ifindex;
  unsigned short sll_hatype;
  unsigned char sll_pkttype, sll_halen;
  unsigned char sll_addr[32];
};

static struct sockaddr *link_sockaddr(const struct __wasix_if_rec *r,
                                      const uint8_t *bytes, unsigned len) {
  struct sockaddr_ll_wide *ll = calloc(1, sizeof *ll);
  if (ll == NULL)
    return NULL;
  if (len > sizeof ll->sll_addr)
    len = sizeof ll->sll_addr; /* the host never sends more (EOVERFLOW) */
  ll->sll_family = AF_PACKET;
  ll->sll_ifindex = (int)r->index;
  ll->sll_hatype = r->hatype;
  ll->sll_halen = (unsigned char)len;
  memcpy(ll->sll_addr, bytes, len);
  return (struct sockaddr *)ll;
}

static struct sockaddr *inet_sockaddr(int family, const uint8_t *bytes,
                                      uint32_t scope_id) {
  if (family == AF_INET) {
    struct sockaddr_in *sin = calloc(1, sizeof *sin);
    if (sin == NULL)
      return NULL;
    sin->sin_family = AF_INET;
    memcpy(&sin->sin_addr, bytes, 4);
    return (struct sockaddr *)sin;
  }
  struct sockaddr_in6 *sin6 = calloc(1, sizeof *sin6);
  if (sin6 == NULL)
    return NULL;
  sin6->sin6_family = AF_INET6;
  memcpy(&sin6->sin6_addr, bytes, 16);
  sin6->sin6_scope_id = scope_id;
  return (struct sockaddr *)sin6;
}

static void mask_of(uint8_t *mask, unsigned width, unsigned prefix) {
  memset(mask, 0, 16);
  if (prefix > width * 8)
    prefix = width * 8;
  for (unsigned i = 0; i < prefix / 8; i++)
    mask[i] = 0xff;
  if (prefix % 8)
    mask[prefix / 8] = (uint8_t)(0xff << (8 - prefix % 8));
}

/* Fills `ifa` from `r`. Returns 0, or -1 on allocation failure (the node's
 * partial members are freed by the caller's freeifaddrs). */
static int fill(struct ifaddrs *ifa, const struct __wasix_if_rec *r) {
  ifa->ifa_name = strndup((const char *)r->name, sizeof r->name);
  if (ifa->ifa_name == NULL)
    return -1;
  ifa->ifa_flags = r->flags;

  if (r->kind == __WASIX_IF_KIND_LINK) {
    ifa->ifa_addr = link_sockaddr(r, r->addr, r->addr_len);
    if (ifa->ifa_addr == NULL)
      return -1;
    if (r->aux_len) {
      ifa->ifa_broadaddr = link_sockaddr(r, r->aux, r->aux_len);
      if (ifa->ifa_broadaddr == NULL)
        return -1;
    }
    return 0;
  }

  int family = r->kind == __WASIX_IF_KIND_INET4 ? AF_INET : AF_INET6;
  unsigned width = family == AF_INET ? 4 : 16;
  uint8_t mask[16];
  mask_of(mask, width, r->prefix_len);
  ifa->ifa_addr = inet_sockaddr(family, r->addr, r->scope_id);
  if (ifa->ifa_addr == NULL)
    return -1;
  ifa->ifa_netmask = inet_sockaddr(family, mask, 0);
  if (ifa->ifa_netmask == NULL)
    return -1;
  if (r->aux_len) {
    /* One allocation behind the ifa_broadaddr/ifa_dstaddr union. */
    ifa->ifa_broadaddr = inet_sockaddr(family, r->aux, r->aux_scope_id);
    if (ifa->ifa_broadaddr == NULL)
      return -1;
  }
  return 0;
}

int getifaddrs(struct ifaddrs **ifap) {
  struct __wasix_if_rec *recs;
  size_t n;
  __wasi_errno_t err = __wasix_if_list_fetch(&recs, &n);
  if (err != __WASI_ERRNO_SUCCESS) {
    errno = __wasilibc_errno_from_wasi(err);
    return -1;
  }

  struct ifaddrs *head = NULL, **tail = &head;
  for (size_t i = 0; i < n; i++) {
    if (recs[i].kind > __WASIX_IF_KIND_INET6)
      continue; /* a kind this libc does not know: skip, as for an unknown family */
    struct ifaddrs *ifa = calloc(1, sizeof *ifa);
    if (ifa == NULL || (*tail = ifa, fill(ifa, &recs[i]) != 0)) {
      freeifaddrs(head);
      free(recs);
      errno = ENOMEM;
      return -1;
    }
    tail = &ifa->ifa_next;
  }
  free(recs);
  *ifap = head;
  return 0;
}

// firebox#YZN: the pre-rename spelling stays EXPORTED as a strong alias of the
// same definition. Every provider before #YZN defined getif_addrs, so a
// consumer linked against one of them imports that name; dropping it would be
// a removal, which forks a new provider class instead of superseding the old
// one. The alias makes the rename a strict superset.
__strong_reference(getifaddrs, getif_addrs);
