#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include "lock.h"

// firebox#V12 (Stage-1 1c gap #3): the shared-window UNLINK import (defined in
// libc-bottom-half/sources/__wasixlibc_firebox.c). shm_unlink()/sem_unlink() call
// it to drop the host's window slot + OS object for an inode after unlink(), so a
// recreated name that reuses the freed st_ino (the firebox VFS recycles inode
// numbers) gets fresh backing instead of colliding on the stale slot.
extern int32_t __wasilibc_shm_unmap(uint64_t ino);

// firebox#61X (Stage-1 1b-libc): a process-local set of the st_ino values of the
// /dev/shm objects this process has opened (shm_open / sem_open). The mman.c
// fork consults it (via __wasix_is_shm_inode) to route mmap(MAP_SHARED, fd) on a
// /dev/shm fd to the host shared-memory window (the __wasi_shm_map import)
// instead of the private aligned_alloc+pread emulation — which is what makes a
// raw cross-process MAP_SHARED store/load coherent (fork/16-1) and the named
// semaphores' byte region shared.
//
// Keyed on st_ino, NOT the fd. st_ino is a STABLE backing identity (#ADZ/#TPH:
// two opens of the same /dev/shm name resolve to the same inode), so:
//   * No fd-reuse hazard — an fd-keyed set would mis-route a regular file that
//     reused a closed shm fd's number, forcing close()/dup2()/dup3() hooks. An
//     inode is never recycled to a different live object, so no such hooks.
//   * A reopened /dev/shm name correctly still routes (same inode).
//   * The set rides proc_fork's private-memory copy, so a child that mmaps an
//     inherited /dev/shm fd routes correctly too.
// Thread-safe via musl's LOCK (a no-op in the non-threaded build; never pulls
// the wasi-thread/TLS dependency into non-threaded programs).
#define WASIX_SHM_INODE_MAX 256
static ino_t __wasix_shm_inodes[WASIX_SHM_INODE_MAX];
static int __wasix_shm_inode_count;
static volatile int __wasix_shm_inode_lock[1];

void __wasix_register_shm_inode(ino_t ino)
{
	LOCK(__wasix_shm_inode_lock);
	for (int i = 0; i < __wasix_shm_inode_count; i++) {
		if (__wasix_shm_inodes[i] == ino) { UNLOCK(__wasix_shm_inode_lock); return; }
	}
	// On overflow we silently skip: that object's MAP_SHARED falls back to the
	// legacy private path (graceful degradation — never a mis-shared mapping).
	if (__wasix_shm_inode_count < WASIX_SHM_INODE_MAX)
		__wasix_shm_inodes[__wasix_shm_inode_count++] = ino;
	UNLOCK(__wasix_shm_inode_lock);
}

int __wasix_is_shm_inode(ino_t ino)
{
	int found = 0;
	LOCK(__wasix_shm_inode_lock);
	for (int i = 0; i < __wasix_shm_inode_count; i++) {
		if (__wasix_shm_inodes[i] == ino) { found = 1; break; }
	}
	UNLOCK(__wasix_shm_inode_lock);
	return found;
}

char *__shm_mapname(const char *name, char *buf)
{
	char *p;
	while (*name == '/') name++;
	if (*(p = __strchrnul(name, '/')) || p==name ||
	    (p-name <= 2 && name[0]=='.' && p[-1]=='.')) {
		errno = EINVAL;
		return 0;
	}
	if (p-name > NAME_MAX) {
		errno = ENAMETOOLONG;
		return 0;
	}
	memcpy(buf, "/dev/shm/", 9);
	memcpy(buf+9, name, p-name+1);
	return buf;
}

int shm_open(const char *name, int flag, mode_t mode)
{
	int cs;
	char buf[NAME_MAX+10];
	if (!(name = __shm_mapname(name, buf))) return -1;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	int fd = open(name, flag|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK, mode);
	pthread_setcancelstate(cs, 0);
	// firebox#61X: record the /dev/shm object's inode so a later
	// mmap(MAP_SHARED, fd) on it routes to the host shared-memory window.
	if (fd >= 0) {
		struct stat st;
		if (fstat(fd, &st) == 0)
			__wasix_register_shm_inode(st.st_ino);
	}
	return fd;
}

int shm_unlink(const char *name)
{
	char buf[NAME_MAX+10];
	if (!(name = __shm_mapname(name, buf))) return -1;
	// firebox#V12 gap #3: capture the inode BEFORE unlink so the host can
	// invalidate the window slot keyed on it. The firebox VFS recycles st_ino
	// across unlink+recreate, so a stale host registry entry would make a
	// recreated name collide on the prior object's slot (sem_unlink/6-1, 9-1).
	// Only invalidate an object we actually window-routed (__wasix_is_shm_inode);
	// the host call is a benign no-op otherwise, but the gate avoids a needless
	// syscall for a legacy-path /dev/shm file. sem_unlink() reaches here too (it
	// is defined as shm_unlink), so named-semaphore unlink is covered as well.
	struct stat st;
	int have_ino = (stat(name, &st) == 0);
	int rc = unlink(name);
	if (rc == 0 && have_ino && __wasix_is_shm_inode(st.st_ino))
		__wasilibc_shm_unmap((uint64_t)st.st_ino);
	return rc;
}
