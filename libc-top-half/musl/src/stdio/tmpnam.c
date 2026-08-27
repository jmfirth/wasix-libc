#ifdef __wasilibc_unmodified_upstream
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include "syscall.h"
#include "kstat.h"

#define MAXTRIES 100

char *tmpnam(char *buf)
{
	static char internal[L_tmpnam];
	char s[] = "/tmp/tmpnam_XXXXXX";
	int try;
	int r;
	for (try=0; try<MAXTRIES; try++) {
		__randname(s+12);
#ifdef SYS_lstat
		r = __syscall(SYS_lstat, s, &(struct kstat){0});
#else
		r = __syscall(SYS_fstatat, AT_FDCWD, s,
			&(struct kstat){0}, AT_SYMLINK_NOFOLLOW);
#endif
		if (r == -ENOENT) return strcpy(buf ? buf : internal, s);
	}
	return 0;
}
#else
/* The upstream body needs raw __syscall(SYS_lstat) and `struct kstat`, and
 * musl ships kstat.h per-arch with no wasm32 variant -- which is why this file
 * was filter-out'd from the build. The SEMANTICS need none of that: pick a name
 * under /tmp that does not exist. Every piece is already in this libc
 * (__randname, stat), and `stat`-then-ENOENT is exactly the idiom
 * src/temp/mktemp.c uses here.
 *
 * ⛔ THE PREMISE IN <stdio.h> -- "WASI has no temp directories" -- IS FALSE FOR
 * FIREBOX. MEASURED in-guest: `mkdir -p /tmp && echo hi > /tmp/x && cat /tmp/x`
 * round-trips, and mkstemp/mkdtemp/mktemp/tmpfile are all defined and working.
 * A declaration without a definition is the false promise firebox#P47 exists to
 * close, so implement it rather than keep disclaiming it.
 *
 * tmpnam is still a race-prone API by design (the name can be taken between the
 * probe and the open) -- that is the C standard's problem, not a port gap, and
 * it is why mkstemp is the right thing to reach for. Being unwise is not the
 * same as being absent. (firebox #P47)
 */
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#define MAXTRIES 100

char *tmpnam(char *buf)
{
	static char internal[L_tmpnam];
	char s[] = "/tmp/tmpnam_XXXXXX";
	struct stat st;
	int try;

	for (try = 0; try < MAXTRIES; try++) {
		__randname(s + 12);
		if (stat(s, &st)) {
			if (errno != ENOENT) return 0;
			return strcpy(buf ? buf : internal, s);
		}
	}
	return 0;
}
#endif
