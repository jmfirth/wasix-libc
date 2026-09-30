#include <resolv.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include "lookup.h"

/* DNS wire transport uses ordinary WASIX sockets: the host address lookup
 * import cannot return arbitrary DNS records. Preserve the full wire length
 * even when the caller's buffer is short, and retry truncated UDP over TCP. */
/* The cleanup chain lives in pthread_create.o on WASIX. Weak hooks avoid
 * pulling thread-start machinery (and __wasm_init_tls) into an ordinary
 * unthreaded resolver client, while registering cleanup when threads exist. */
void __do_cleanup_push(struct __ptcb *) __attribute__((__weak__));
void __do_cleanup_pop(struct __ptcb *) __attribute__((__weak__));

struct transport { int fd; unsigned char *packet; };

static void cleanup(void *p)
{
	struct transport *t = p;
	int saved = errno;
	if (t->fd >= 0) close(t->fd);
	free(t->packet);
	errno = saved;
}

static long long now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int waitfd(int fd, short events, long long deadline)
{
	struct pollfd pfd = { .fd = fd, .events = events };
	for (;;) {
		long long left = deadline - now();
		if (left <= 0) { errno = ETIMEDOUT; return -1; }
		int r = poll(&pfd, 1, left);
		if (r > 0) return 0;
		if (!r) { errno = ETIMEDOUT; return -1; }
		if (errno != EINTR) return -1;
	}
}

static int transfer(int fd, unsigned char *buf, int len, int writing,
	long long deadline)
{
	while (len) {
		if (waitfd(fd, writing ? POLLOUT : POLLIN, deadline)) return -1;
		ssize_t n = writing ? send(fd, buf, len, MSG_NOSIGNAL) : recv(fd, buf, len, 0);
		if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
		if (n <= 0) { if (!n) errno = ECONNRESET; return -1; }
		buf += n;
		len -= n;
	}
	return 0;
}

/* Connected sockets check the server address. Compare decoded questions too,
 * without case bias, to reject unrelated replies even if their IDs collide. */
static int matches(const unsigned char *q, int qlen, const unsigned char *a, int alen)
{
	if (alen < 12 || memcmp(q, a, 2) || !(a[2] & 128)
	    || ((q[2] ^ a[2]) & 120)) return 0;
	/* A truncated UDP response may contain only the header. It triggers a
	 * TCP retry; only the complete TCP response can reach the caller. */
	if ((a[2] & 2) && !a[4] && !a[5]) return 1;
	if (memcmp(q+4, a+4, 2)) return 0;
	const unsigned char *qp = q+12, *ap = a+12;
	unsigned count = q[4]*256 + q[5];
	while (count--) {
		char qname[256], aname[256];
		int qn = dn_expand(q, q+qlen, qp, qname, sizeof qname);
		int an = dn_expand(a, a+alen, ap, aname, sizeof aname);
		if (qn < 0 || an < 0 || qp+qn+4 > q+qlen || ap+an+4 > a+alen
		    || strcasecmp(qname, aname) || memcmp(qp+qn, ap+an, 4)) return 0;
		qp += qn+4;
		ap += an+4;
	}
	return 1;
}

static int tcp(struct transport *t, const struct sockaddr *sa, socklen_t sl,
	const unsigned char *q, int qlen, long long deadline)
{
	if (t->fd >= 0) close(t->fd);
	t->fd = socket(sa->sa_family, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
	if (t->fd < 0) return -1;
	if (connect(t->fd, sa, sl) < 0) {
		if (errno != EINPROGRESS && errno != EAGAIN) return -1;
		if (waitfd(t->fd, POLLOUT, deadline)) return -1;
		int error;
		socklen_t size = sizeof error;
		if (getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &error, &size)) return -1;
		if (error) { errno = error; return -1; }
	}
	unsigned char prefix[2] = { qlen >> 8, qlen };
	if (transfer(t->fd, prefix, 2, 1, deadline)
	    || transfer(t->fd, (unsigned char *)q, qlen, 1, deadline)) return -1;
	for (;;) {
		if (transfer(t->fd, prefix, 2, 0, deadline)) return -1;
		int n = prefix[0]*256 + prefix[1];
		if (transfer(t->fd, t->packet, n, 0, deadline)) return -1;
		if (matches(q, qlen, t->packet, n)) return n;
	}
}

int __res_send(const unsigned char *msg, int msglen, unsigned char *answer, int anslen)
{
	if (msglen < 12 || msglen > 65535 || anslen < 12) { errno = EINVAL; return -1; }
	struct resolvconf conf;
	if (__get_resolv_conf(&conf, 0, 0)) return -1;
	struct __res_state state = _res;
	/* Applications conventionally override IPv4 servers through _res. Keep
	 * their port as well as their address; resolv.conf servers use port 53. */
	if (state.nscount > 0 && state.nscount <= MAXNS) {
		conf.nns = state.nscount;
		if (state.retry > 0) conf.attempts = state.retry > 10 ? 10 : state.retry;
		if (state.retrans > 0) conf.timeout = state.retrans > 60 ? 60 : state.retrans;
		for (unsigned i = 0; i < conf.nns; i++) {
			conf.ns[i].family = AF_INET;
			memcpy(conf.ns[i].addr, &state.nsaddr_list[i].sin_addr, 4);
		}
	}
	struct transport t = { .fd = -1 };
	int cs, result = -1, error = ETIMEDOUT;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	t.packet = malloc(65536);
	if (!t.packet) { pthread_setcancelstate(cs, 0); return -1; }
	struct __ptcb cb = { .__f = cleanup, .__x = &t };
	if (__do_cleanup_push) __do_cleanup_push(&cb);
	pthread_setcancelstate(cs, 0);
	/* Zero-valued resolv.conf options must still make an actual attempt. */
	unsigned attempts = conf.attempts ? conf.attempts : 1;
	unsigned timeout = conf.timeout ? conf.timeout : 1;
	for (unsigned attempt = 0; attempt < attempts; attempt++) {
		for (unsigned i = 0; i < conf.nns; i++) {
			union { struct sockaddr_in in; struct sockaddr_in6 in6; } sa = {0};
			socklen_t sl;
			if (conf.ns[i].family == AF_INET) {
				sa.in.sin_family = AF_INET;
				sa.in.sin_port = state.nscount > 0 && state.nscount <= MAXNS
					? state.nsaddr_list[i].sin_port : htons(53);
				memcpy(&sa.in.sin_addr, conf.ns[i].addr, 4);
				sl = sizeof sa.in;
			} else {
				sa.in6.sin6_family = AF_INET6;
				sa.in6.sin6_port = htons(53);
				sa.in6.sin6_scope_id = conf.ns[i].scopeid;
				memcpy(&sa.in6.sin6_addr, conf.ns[i].addr, 16);
				sl = sizeof sa.in6;
			}
			if (t.fd >= 0) close(t.fd);
			t.fd = -1;
			long long deadline = now() + 1000*timeout;
			int n = -1;
			if (msglen > 512 || (state.options & RES_USEVC)) n = tcp(&t, (void *)&sa, sl, msg, msglen, deadline);
			else {
				t.fd = socket(conf.ns[i].family, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
				if (t.fd < 0 || connect(t.fd, (void *)&sa, sl)) { error = errno; continue; }
				if (transfer(t.fd, (unsigned char *)msg, msglen, 1, deadline)) {
					error = errno;
					continue;
				}
				for (;;) {
					if (waitfd(t.fd, POLLIN, deadline)) { n = -1; break; }
					n = recv(t.fd, t.packet, 65536, 0);
					if (n < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
						break;
					}
					if (!matches(msg, msglen, t.packet, n)) continue;
					if ((t.packet[2] & 2) && !(state.options & RES_IGNTC)) n = tcp(&t, (void *)&sa, sl, msg, msglen, deadline);
					break;
				}
			}
			if (n < 12) { error = errno; continue; }
			unsigned rcode = t.packet[3] & 15;
			if ((rcode == 2 || rcode == 4 || rcode == 5) && i+1 < conf.nns) continue;
			memcpy(answer, t.packet, n < anslen ? n : anslen);
			if (n > anslen) answer[2] |= 2;
			result = n;
			goto out;
		}
	}
out:
	if (result < 0) errno = error;
	if (__do_cleanup_pop) __do_cleanup_pop(&cb);
	cleanup(&t);
	return result;
}

weak_alias(__res_send, res_send);
