#include <semaphore.h>
#include <sys/mman.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>
#include "lock.h"
#include "fork_impl.h"

#define malloc __libc_malloc
#define calloc __libc_calloc
#define realloc undef
#define free undef

static struct {
	ino_t ino;
	sem_t *sem;
	int refcnt;
} *semtab;
static volatile int lock[1];
volatile int *const __sem_open_lockptr = lock;

// firebox#61X-regfix (conformance wave-1): named semaphores are SCOPED OUT of
// the #61X host shared-memory-window routing. #61X marked the sem object's
// inode here so its mmap(MAP_SHARED, fd) routed to the window for cross-process
// byte coherence — but the window cannot yet faithfully back a named semaphore,
// for THREE substrate reasons the guest libc cannot fix:
//   1. The window is a fresh anonymous segment, NOT initialized from the fd's
//      bytes, so the sem_init'd `value` musl write()s to the file is lost — the
//      window reads back zero. (Broke every nonzero-initialized named sem:
//      sem_wait/sem_post/sem_getvalue/sem_open single-process tests.)
//   2. A BLOCKING sem_wait/sem_timedwait does futex_wait on a window ADDRESS
//      (above the grow-capped heap, in the Cranelift Static-heap elided-bounds
//      region). Raw store/load alias there, but futex_wait TRAPS
//      ("RuntimeError: unreachable") — so any sem that actually blocks crashes
//      the process (sem_unlink/9-1, fork/21-1, the cross-process fork/1-1).
//      This is the Milestone-1c cross-process futex work; it is deeper than the
//      "no wake" the #61X message anticipated — the WAIT itself faults.
//   3. The window slot is keyed on st_ino; the firebox VFS reuses inode numbers
//      across unlink+recreate, so an unlinked-then-recreated name collides on
//      the prior slot (sem_unlink/6-1: a value-1 sem reads back its successor's
//      value 3). Distinct objects must get distinct backing — host-side slot
//      invalidation on shm_unlink (§7.4), absent today.
// Until the window is COMPLETED host-side (fd-content init + 1c window-address
// futex + unlink slot-invalidation), named sems stay on the legacy
// aligned_alloc+pread path, which faithfully preads `value`, blocks on a normal
// heap futex, and gives distinct objects distinct backing — restoring all 15
// single-process sem regressions + fork/21-1 with NO traps. The raw
// cross-process MAP_SHARED win (fork/16-1) is UNAFFECTED: it routes via
// shm_open.c (still marks its inodes) and only does raw store/load, never a
// blocking wait or a value-init or an unlink-recreate. The cost of this
// scope-down is the two cross-process named-sem cases the window happened to
// carry at b34541b — fork/14-1 (non-blocking trywait, passed only because its
// value-0 sem matched the zero-filled window) and the flaky fork/1-1 — which
// revert to their pre-#61X legacy FAIL; they (and 6-1/9-1/21-1) all return
// together once the window's futex/init/unlink semantics are completed.

#define FLAGS (O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK)

sem_t *sem_open(const char *name, int flags, ...)
{
	va_list ap;
	mode_t mode;
	unsigned value;
	int fd, i, e, slot, first=1, cnt, cs;
	sem_t newsem;
	void *map;
	char tmp[64];
	struct timespec ts;
	struct stat st;
	char buf[NAME_MAX+10];

	if (!(name = __shm_mapname(name, buf)))
		return SEM_FAILED;

	LOCK(lock);
	/* Allocate table if we don't have one yet */
	if (!semtab && !(semtab = calloc(sizeof *semtab, SEM_NSEMS_MAX))) {
		UNLOCK(lock);
		return SEM_FAILED;
	}

	/* Reserve a slot in case this semaphore is not mapped yet;
	 * this is necessary because there is no way to handle
	 * failures after creation of the file. */
	slot = -1;
	for (cnt=i=0; i<SEM_NSEMS_MAX; i++) {
		cnt += semtab[i].refcnt;
		if (!semtab[i].sem && slot < 0) slot = i;
	}
	/* Avoid possibility of overflow later */
	if (cnt == INT_MAX || slot < 0) {
		errno = EMFILE;
		UNLOCK(lock);
		return SEM_FAILED;
	}
	/* Dummy pointer to make a reservation */
	semtab[slot].sem = (sem_t *)-1;
	UNLOCK(lock);

	flags &= (O_CREAT|O_EXCL);

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);

	/* Early failure check for exclusive open; otherwise the case
	 * where the semaphore already exists is expensive. */
	if (flags == (O_CREAT|O_EXCL) && access(name, F_OK) == 0) {
		errno = EEXIST;
		goto fail;
	}

	for (;;) {
		/* If exclusive mode is not requested, try opening an
		 * existing file first and fall back to creation. */
		if (flags != (O_CREAT|O_EXCL)) {
			fd = open(name, FLAGS);
			if (fd >= 0) {
				if (fstat(fd, &st) < 0 ||
				    (map = mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
					close(fd);
					goto fail;
				}
				close(fd);
				break;
			}
			if (errno != ENOENT)
				goto fail;
		}
		if (!(flags & O_CREAT))
			goto fail;
		if (first) {
			first = 0;
			va_start(ap, flags);
			mode = va_arg(ap, mode_t) & 0666;
			value = va_arg(ap, unsigned);
			va_end(ap);
			if (value > SEM_VALUE_MAX) {
				errno = EINVAL;
				goto fail;
			}
			sem_init(&newsem, 1, value);
		}
		/* Create a temp file with the new semaphore contents
		 * and attempt to atomically link it as the new name */
		clock_gettime(CLOCK_REALTIME, &ts);
		snprintf(tmp, sizeof(tmp), "/dev/shm/tmp-%d", (int)ts.tv_nsec);
		fd = open(tmp, O_CREAT|O_EXCL|FLAGS, mode);
		if (fd < 0) {
			if (errno == EEXIST) continue;
			goto fail;
		}
		if (write(fd, &newsem, sizeof newsem) != sizeof newsem || fstat(fd, &st) < 0 ||
		    (map = mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
			close(fd);
			unlink(tmp);
			goto fail;
		}
		close(fd);
		e = link(tmp, name) ? errno : 0;
		unlink(tmp);
		if (!e) break;
		munmap(map, sizeof(sem_t));
		/* Failure is only fatal when doing an exclusive open;
		 * otherwise, next iteration will try to open the
		 * existing file. */
		if (e != EEXIST || flags == (O_CREAT|O_EXCL))
			goto fail;
	}

	/* See if the newly mapped semaphore is already mapped. If
	 * so, unmap the new mapping and use the existing one. Otherwise,
	 * add it to the table of mapped semaphores. */
	LOCK(lock);
	for (i=0; i<SEM_NSEMS_MAX && semtab[i].ino != st.st_ino; i++);
	if (i<SEM_NSEMS_MAX) {
		munmap(map, sizeof(sem_t));
		semtab[slot].sem = 0;
		slot = i;
		map = semtab[i].sem;
	}
	semtab[slot].refcnt++;
	semtab[slot].sem = map;
	semtab[slot].ino = st.st_ino;
	UNLOCK(lock);
	pthread_setcancelstate(cs, 0);
	return map;

fail:
	pthread_setcancelstate(cs, 0);
	LOCK(lock);
	semtab[slot].sem = 0;
	UNLOCK(lock);
	return SEM_FAILED;
}

int sem_close(sem_t *sem)
{
	int i;
	LOCK(lock);
	for (i=0; i<SEM_NSEMS_MAX && semtab[i].sem != sem; i++);
	if (--semtab[i].refcnt) {
		UNLOCK(lock);
		return 0;
	}
	semtab[i].sem = 0;
	semtab[i].ino = 0;
	UNLOCK(lock);
	munmap(sem, sizeof *sem);
	return 0;
}
