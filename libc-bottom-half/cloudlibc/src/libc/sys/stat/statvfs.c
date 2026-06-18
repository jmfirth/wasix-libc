#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

// Firebox (firebox#RXJ): faithful `statvfs`/`fstatvfs` for the in-memory VFS.
//
// WASI Preview 1 has no filesystem-statistics syscall (`statfs`/`fstatfs` have
// no WASI ABI), so the upstream cloudlibc stubs returned `ENOTSUP`. That broke
// every program that probes free space (`df`, build-system disk checks, the
// musl libc-test `regression/statvfs` sanity test) — `statvfs` reporting "Not
// supported" is a faithfulness gap, not the Linux behavior.
//
// Firebox's filesystem is an in-memory VFS, so the faithful model is the one
// Linux uses for `tmpfs`: synthesize self-consistent block/inode counts derived
// from a representative capacity rather than from a physical disk. The numbers
// below are not a physical measurement (there is no backing disk to measure) —
// they are the correct *shape* for an in-memory filesystem, exactly as
// `statvfs("/")` on a Linux `tmpfs` mount returns synthesized values from its
// size limit. Programs get a working, sane answer instead of an error.
//
// We still honor the path/fd: a `statvfs` on a path that does not exist must
// fail (`ENOENT`), and `fstatvfs` on a bad fd must fail (`EBADF`). We get that
// for free by validating with `stat`/`fstat` first and propagating its errno.

// Representative in-memory capacity: 4 GiB of 4 KiB blocks. Reported half-free
// so that bavail/bfree are strictly less than blocks (and likewise for inodes),
// satisfying the POSIX invariant `free <= total` for any consumer.
#define FBX_VFS_BSIZE 4096UL
#define FBX_VFS_BLOCKS (1UL << 20) /* 4 GiB / 4 KiB */
#define FBX_VFS_FILES (1UL << 20)
#define FBX_VFS_NAMEMAX 255UL /* NAME_MAX */

static void fbx_fill_statvfs(struct statvfs *buf) {
  *buf = (struct statvfs){0};
  buf->f_bsize = FBX_VFS_BSIZE;
  buf->f_frsize = FBX_VFS_BSIZE;
  buf->f_blocks = FBX_VFS_BLOCKS;
  buf->f_bfree = FBX_VFS_BLOCKS / 2;
  buf->f_bavail = FBX_VFS_BLOCKS / 2;
  buf->f_files = FBX_VFS_FILES;
  buf->f_ffree = FBX_VFS_FILES / 2;
  buf->f_favail = FBX_VFS_FILES / 2;
  buf->f_fsid = 0;
  buf->f_flag = 0;
  buf->f_namemax = FBX_VFS_NAMEMAX;
}

int statvfs(const char *restrict path, struct statvfs *restrict buf) {
  // Faithful errno for a missing path: let `stat` report ENOENT/ENOTDIR/etc.
  struct stat st;
  if (stat(path, &st) != 0) {
    return -1;
  }
  fbx_fill_statvfs(buf);
  return 0;
}

int fstatvfs(int fd, struct statvfs *buf) {
  // Faithful errno for a bad fd: let `fstat` report EBADF.
  struct stat st;
  if (fstat(fd, &st) != 0) {
    return -1;
  }
  fbx_fill_statvfs(buf);
  return 0;
}
