#include <signal.h>
#include <errno.h>

int sigismember(const sigset_t *set, int sig)
{
	unsigned s = sig-1;
	/* firebox#XCJ — an invalid signal number is an ERROR, not "not a member".
	 * POSIX (and Open POSIX sigismember/5-1): sigismember() shall return -1 and
	 * set errno to [EINVAL] for an out-of-range signal number. The upstream-musl
	 * body returned 0 ("not a member") for the out-of-range case, which is wrong
	 * for the error contract the conformance suite checks (sigismember(set, -1)
	 * with `set` full must report the error, not a false "absent"). The negative
	 * case wraps to a huge unsigned via `sig-1`, so the single unsigned compare
	 * folds both the negative and the too-large signo. (sig == 0 → s = (unsigned)-1
	 * is also rejected: 0 is not a valid member-query signal.) */
	if (s >= _NSIG-1) {
		errno = EINVAL;
		return -1;
	}
	return !!(set->__bits[s/8/sizeof *set->__bits] & 1UL<<(s&8*sizeof *set->__bits-1));
}
