# ---------------------------------------------------------------------------
# SHARED musl omit-header manifest — included by BOTH `Makefile` and
# `Makefile-eh`.  Same contract, same reasons, as Makefile.musl-sources.mk
# (read the header there): one list, no copy to keep in sync.
#
# #F2D drifted this list too — the eh sysroot advertised headers the plain one
# declared absent.  `check-wasixcc-drift` arm D compares it at both PIC axes.
#
# THE ONE LEGITIMATE ASYMMETRY lives in `Makefile-eh`, after the include:
# `MUSL_OMIT_HEADERS += "dlfcn.h"` at PIC=no, the header half of the
# LIBC_TOP_HALF_LDSO_SOURCES gate.  Everything else belongs HERE.
#
# REQUIRES: THREAD_MODEL.
# ---------------------------------------------------------------------------

# Files from musl's include directory that we don't want to install in the
# sysroot's include directory.
MUSL_OMIT_HEADERS :=

# Remove files which aren't headers (we generate alltypes.h below).
MUSL_OMIT_HEADERS += \
    "bits/syscall.h.in" \
    "bits/alltypes.h.in" \
    "alltypes.h.in"

# Use the compiler's version of these headers.
MUSL_OMIT_HEADERS += \
    "stdarg.h" \
    "stddef.h"

# Use the WASI errno definitions.
MUSL_OMIT_HEADERS += \
    "bits/errno.h"

# Remove headers that aren't supported yet or that aren't relevant for WASI.
MUSL_OMIT_HEADERS += \
    "sys/procfs.h" \
    "sys/user.h" \
    "sys/kd.h" "sys/vt.h" "sys/soundcard.h" "sys/sem.h" \
    "sys/shm.h" "sys/msg.h" "sys/ipc.h" "sys/ptrace.h" \
    "sys/statfs.h" \
    "bits/kd.h" "bits/vt.h" "bits/soundcard.h" "bits/sem.h" \
    "bits/shm.h" "bits/msg.h" "bits/ipc.h" "bits/ptrace.h" \
    "bits/statfs.h" \
    "sys/vfs.h" \
    "sys/syslog.h" \
    "ucontext.h" "sys/ucontext.h" \
    "utmp.h" "utmpx.h" \
    "lastlog.h" \
    "sys/acct.h" \
    "sys/cachectl.h" \
    "sys/reboot.h" "sys/swap.h" \
    "sys/inotify.h" \
    "sys/quota.h" \
    "sys/klog.h" \
    "sys/fsuid.h" \
    "sys/io.h" \
    "sys/prctl.h" \
    "sys/mtio.h" \
    "sys/mount.h" \
    "sys/fanotify.h" \
    "sys/personality.h" \
    "link.h" "bits/link.h" \
    "scsi/scsi.h" "scsi/scsi_ioctl.h" "scsi/sg.h" \
    "sys/auxv.h" \
    "pty.h" \
    "ulimit.h" \
    "sys/xattr.h" \
    "sys/membarrier.h" \
    "sys/signalfd.h" \
    "sys/termios.h" \
    "net/if_arp.h" \
    "net/ethernet.h" \
    "net/route.h" \
    "netinet/if_ether.h" \
    "netinet/ether.h" \
    "sys/timerfd.h" \
    "sys/sysmacros.h"
# firebox#5DB — aio.h is NO LONGER omitted: POSIX AIO is now provided (musl's
# thread-backed src/aio/*.c, enabled above). The header ships so aio_read/
# aio_write/aio_error/aio_return/aio_suspend/aio_cancel/aio_fsync/lio_listio +
# struct aiocb are visible (closes the build:no-posix-aio conformance class).

ifeq ($(THREAD_MODEL), single)
# Remove headers not supported in single-threaded mode.
MUSL_OMIT_HEADERS += "pthread.h"
endif
