#define _GNU_SOURCE
#ifdef __wasilibc_unmodified_upstream /* netlink / SIOCGIF* */
#include <net/if.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "netlink.h"

#define IFADDRS_HASH_SIZE 64

struct ifnamemap {
	unsigned int hash_next;
	unsigned int index;
	unsigned char namelen;
	char name[IFNAMSIZ];
};

struct ifnameindexctx {
	unsigned int num, allocated, str_bytes;
	struct ifnamemap *list;
	unsigned int hash[IFADDRS_HASH_SIZE];
};

static int netlink_msg_to_nameindex(void *pctx, struct nlmsghdr *h)
{
	struct ifnameindexctx *ctx = pctx;
	struct ifnamemap *map;
	struct rtattr *rta;
	unsigned int i;
	int index, type, namelen, bucket;

	if (h->nlmsg_type == RTM_NEWLINK) {
		struct ifinfomsg *ifi = NLMSG_DATA(h);
		index = ifi->ifi_index;
		type = IFLA_IFNAME;
		rta = NLMSG_RTA(h, sizeof(*ifi));
	} else {
		struct ifaddrmsg *ifa = NLMSG_DATA(h);
		index = ifa->ifa_index;
		type = IFA_LABEL;
		rta = NLMSG_RTA(h, sizeof(*ifa));
	}
	for (; NLMSG_RTAOK(rta, h); rta = RTA_NEXT(rta)) {
		if (rta->rta_type != type) continue;

		namelen = RTA_DATALEN(rta) - 1;
		if (namelen > IFNAMSIZ) return 0;

		/* suppress duplicates */
		bucket = index % IFADDRS_HASH_SIZE;
		i = ctx->hash[bucket];
		while (i) {
			map = &ctx->list[i-1];
			if (map->index == index &&
			    map->namelen == namelen &&
			    memcmp(map->name, RTA_DATA(rta), namelen) == 0)
				return 0;
			i = map->hash_next;
		}

		if (ctx->num >= ctx->allocated) {
			size_t a = ctx->allocated ? ctx->allocated * 2 + 1 : 8;
			if (a > SIZE_MAX/sizeof *map) return -1;
			map = realloc(ctx->list, a * sizeof *map);
			if (!map) return -1;
			ctx->list = map;
			ctx->allocated = a;
		}
		map = &ctx->list[ctx->num];
		map->index = index;
		map->namelen = namelen;
		memcpy(map->name, RTA_DATA(rta), namelen);
		ctx->str_bytes += namelen + 1;
		ctx->num++;
		map->hash_next = ctx->hash[bucket];
		ctx->hash[bucket] = ctx->num;
		return 0;
	}
	return 0;
}

struct if_nameindex *if_nameindex()
{
	struct ifnameindexctx _ctx, *ctx = &_ctx;
	struct if_nameindex *ifs = 0, *d;
	struct ifnamemap *s;
	char *p;
	int i;
	int cs;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	memset(ctx, 0, sizeof(*ctx));
	if (__rtnetlink_enumerate(AF_UNSPEC, AF_INET, netlink_msg_to_nameindex, ctx) < 0) goto err;

	ifs = malloc(sizeof(struct if_nameindex[ctx->num+1]) + ctx->str_bytes);
	if (!ifs) goto err;

	p = (char*)(ifs + ctx->num + 1);
	for (i = ctx->num, d = ifs, s = ctx->list; i; i--, s++, d++) {
		d->if_index = s->index;
		d->if_name = p;
		memcpy(p, s->name, s->namelen);
		p += s->namelen;
		*p++ = 0;
	}
	d->if_index = 0;
	d->if_name = 0;
err:
	pthread_setcancelstate(cs, 0);
	free(ctx->list);
	errno = ENOBUFS;
	return ifs;
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

/* One allocation, as musl's: the array (terminated by a {0, NULL} entry) then
 * the names, so if_freenameindex stays a plain free(). One entry per distinct
 * index, in the order the host lists interfaces. */
struct if_nameindex *if_nameindex()
{
	struct __wasix_if_rec *recs;
	size_t n, i, j, num = 0;
	__wasi_errno_t err = __wasix_if_list_fetch(&recs, &n);
	if (err) {
		/* POSIX: an allocation failure is ENOBUFS, including the record
		 * buffer the fetch allocates. */
		errno = err == __WASI_ERRNO_NOMEM ? ENOBUFS
		                                  : __wasilibc_errno_from_wasi(err);
		return 0;
	}
	unsigned *seen = malloc((n ? n : 1) * sizeof *seen);
	size_t *pick = malloc((n ? n : 1) * sizeof *pick);
	if (!seen || !pick) {
		free(seen); free(pick); free(recs);
		errno = ENOBUFS;
		return 0;
	}
	for (i = 0; i < n; i++) {
		for (j = 0; j < num && seen[j] != recs[i].index; j++);
		if (j == num) {
			seen[num] = recs[i].index;
			pick[num++] = i;
		}
	}
	struct if_nameindex *ifs = malloc((num+1) * sizeof *ifs + num * IF_NAMESIZE);
	if (ifs) {
		char *p = (char *)(ifs + num + 1);
		for (j = 0; j < num; j++, p += IF_NAMESIZE) {
			memcpy(p, recs[pick[j]].name, IF_NAMESIZE);
			p[IF_NAMESIZE-1] = 0;
			ifs[j].if_index = seen[j];
			ifs[j].if_name = p;
		}
		ifs[num].if_index = 0;
		ifs[num].if_name = 0;
	} else {
		errno = ENOBUFS;
	}
	free(seen); free(pick); free(recs);
	return ifs;
}
#endif
