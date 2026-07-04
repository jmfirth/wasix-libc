#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

// Firebox synthesizes sandbox-sane filesystem statistics for statvfs/fstatvfs.
//
// WHY synthesis (not a real backing-store query): the Firebox guest filesystem
// is an in-memory / host-overlay VFS with no fixed, guest-visible block or inode
// accounting to report — "real free space" is host RAM/disk and is ill-defined
// for a sandbox volume. Upstream wasix-libc left both calls as a hard `ENOTSUP`
// stub, which breaks every Linux program that calls statvfs() to learn the block
// size, the filename limit, or merely that the volume exists with room to write
// (musl libc-test regression/statvfs is the witness). The faithful answer for a
// sandbox is a coherent synthetic volume — the same posture as the synthetic
// /proc/meminfo (firebox-vfs src/proc.rs: a minimal VM with round, half-free
// numbers). We model a modest single-volume sandbox FS: 1 GiB at 4 KiB blocks,
// ~half free, 64 Ki inodes ~half free, NAME_MAX = 255. The values are static and
// internally consistent (f_blocks >= f_bfree >= f_bavail, f_files >= f_ffree >=
// f_favail); programs needing byte-exact live `df` accounting do not get it
// (neither does tmpfs) — that would require a new host fs-statistics import and
// is out of scope here.
//
// The PER-OBJECT resolution stays REAL (Linux-faithful errno semantics): statvfs
// first stat()s the path, so a nonexistent path returns ENOENT/ENOTDIR; fstatvfs
// first fstat()s the descriptor, so a bad fd returns EBADF. Only the
// filesystem-CAPACITY fields are synthetic. struct statvfs layout and both
// symbol signatures are unchanged, so this remains ABI-compatible with upstream
// wasmer.io artifacts.
static void __fbx_fill_statvfs(struct statvfs *buf) {
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = 4096;       // block size
    buf->f_frsize = 4096;      // fragment size
    buf->f_blocks = 262144;    // total blocks: 1 GiB at 4 KiB
    buf->f_bfree = 131072;     // free blocks: ~512 MiB
    buf->f_bavail = 131072;    // blocks available to unprivileged
    buf->f_files = 65536;      // total inodes
    buf->f_ffree = 32768;      // free inodes
    buf->f_favail = 32768;     // inodes available to unprivileged
    buf->f_fsid = 0;
    buf->f_flag = 0;
    buf->f_namemax = 255;      // NAME_MAX (8 <= v <= 1<<16)
}

int statvfs(const char *restrict path, struct statvfs *restrict buf)
{
    // Faithful path resolution: ENOENT/ENOTDIR/EACCES propagate from stat().
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;  // errno set by stat()
    __fbx_fill_statvfs(buf);
    return 0;
}

int fstatvfs(int fd, struct statvfs *buf)
{
    // Faithful fd validation: EBADF propagates from fstat().
    struct stat st;
    if (fstat(fd, &st) != 0)
        return -1;  // errno set by fstat()
    __fbx_fill_statvfs(buf);
    return 0;
}
