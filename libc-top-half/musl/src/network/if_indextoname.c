#define _GNU_SOURCE
#ifdef __wasilibc_unmodified_upstream /* netlink / SIOCGIF* */
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include "syscall.h"

char *if_indextoname(unsigned index, char *name)
{
	struct ifreq ifr;
	int fd, r;

	if ((fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0)) < 0) return 0;
	ifr.ifr_ifindex = index;
	r = ioctl(fd, SIOCGIFNAME, &ifr);
	__syscall(SYS_close, fd);
	if (r < 0) {
		if (errno == ENODEV) errno = ENXIO;
		return 0;
	}
	return strncpy(name, ifr.ifr_name, IF_NAMESIZE);
}
#else
/* firebox#25D / #9FB: WASIX has no netlink and no SIOCGIF* ioctls, so musl's
 * body (kept above for upstream diffs) cannot run. This one derives from
 * __wasix_port_if_list, the same host list getifaddrs reads, so the four
 * interface functions cannot disagree. Without an interface model (no --net;
 * the browser) the list fails with ENOTSUP and so does this. */
#include <net/if.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/libc.h>

char *if_indextoname(unsigned index, char *name)
{
	struct __wasix_if_rec *recs;
	size_t n, i;
	char *ret = 0;
	__wasi_errno_t err = __wasix_if_list_fetch(&recs, &n);
	if (err) {
		errno = __wasilibc_errno_from_wasi(err);
		return 0;
	}
	for (i = 0; i < n; i++) {
		if (recs[i].index == index) {
			memcpy(name, recs[i].name, IF_NAMESIZE);
			name[IF_NAMESIZE-1] = 0;
			ret = name;
			break;
		}
	}
	free(recs);
	/* POSIX: no interface with this index is ENXIO. */
	if (!ret) errno = ENXIO;
	return ret;
}
#endif
