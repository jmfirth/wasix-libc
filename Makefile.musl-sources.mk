# ---------------------------------------------------------------------------
# SHARED musl source manifest — included by BOTH `Makefile` (firebox's own
# sysroot variants) and `Makefile-eh` (the wasixcc flavour that edgejs, python
# and make link).  firebox#NW5 / firebox#F2D / firebox#EX6.
#
# WHY THIS FILE EXISTS.  These two manifests used to enumerate the same ~1025
# musl sources BY COPY.  A capability landed in one and not the other twice —
# #F2D (AIO / POSIX timers / mqueue) and #P47 (ctermid, cuserid, getlogin,
# getlogin_r, tmpnam, and the network identity set) — each time producing a
# silent invariant-4 divergence INSIDE one profile: declared in the shipped
# headers, undefined in the shipped archive.  firebox's `check-wasixcc-drift`
# arm D detects that, but it runs on firebox's commit path and these manifests
# live in THIS repository, so it can never gate the commit that drifts them.
# A detector in the wrong repository is not a guard.  This file is the guard:
# there is now exactly one list, so there is nothing to keep in sync.
#
# CONTRACT.  Neither manifest may add to `LIBC_TOP_HALF_MUSL_SOURCES` outside
# this fragment.  The ONE legitimate asymmetry — musl's dlopen surface, which
# `Makefile-eh` gates on PIC because a non-PIC module cannot dynamically link —
# is expressed as the `LIBC_TOP_HALF_LDSO_SOURCES` knob below, which each
# manifest sets BEFORE including this file.  `check-wasixcc-drift` arm D
# enforces that contract statically; adding a source to a manifest instead of
# to this file will fail it.
#
# REQUIRES (all defined by the including manifest, above the `include`):
#   LIBC_TOP_HALF_MUSL_SRC_DIR   THREAD_MODEL   EXTRA_CFLAGS   TARGET_ARCH
#   LIBC_TOP_HALF_LDSO_SOURCES   (musl-relative names, or empty)
# ---------------------------------------------------------------------------

LIBC_TOP_HALF_MUSL_SOURCES = \
    $(addprefix $(LIBC_TOP_HALF_MUSL_SRC_DIR)/, \
        internal/syscall_ret.c \
        misc/a64l.c \
        misc/basename.c \
        misc/dirname.c \
        misc/ffs.c \
        misc/ffsl.c \
        misc/ffsll.c \
        misc/fmtmsg.c \
        misc/getdomainname.c \
        misc/gethostid.c \
        misc/getopt.c \
        misc/getopt_long.c \
        misc/getsubopt.c \
        misc/initgroups.c \
        misc/getrlimit.c \
        misc/setrlimit.c \
        misc/getrusage.c \
        misc/uname.c \
        misc/nftw.c \
	misc/realpath.c \
        misc/syslog.c \
        misc/mntent.c \
        misc/wordexp.c \
        mman/shm_open.c \
        errno/strerror.c \
        \
        network/services.c \
        network/getaddrinfo.c \
        network/getnameinfo.c \
        network/gethostbyname.c \
        network/gethostbyname_r.c \
        network/gethostbyname2.c \
        network/gethostbyname2_r.c \
        network/getservbyname.c \
        network/getservbyname_r.c \
        network/gethostbyaddr.c \
        network/gethostbyaddr_r.c \
        network/lookup_ipliteral.c \
        network/lookup_name.c \
        network/lookup_serv.c \
        network/freeaddrinfo.c \
        network/resolvconf.c \
        \
        network/gai_strerror.c \
        network/getservbyport.c \
        network/h_errno.c \
        network/hstrerror.c \
        network/htonl.c \
        network/htons.c \
        network/ntohl.c \
        network/ntohs.c \
        network/inet_ntop.c \
	    network/inet_ntoa.c \
        network/inet_pton.c \
        network/inet_aton.c \
        network/inet_addr.c \
        network/dn_expand.c \
        network/in6addr_any.c \
        network/in6addr_loopback.c \
        network/proto.c \
        \
        network/ent.c \
        network/serv.c \
        network/netname.c \
        network/herror.c \
        network/getservbyport_r.c \
        fenv/fenv.c \
        fenv/fesetround.c \
        fenv/feupdateenv.c \
        fenv/fesetexceptflag.c \
        fenv/fegetexceptflag.c \
        fenv/feholdexcept.c \
        exit/exit.c \
        exit/atexit.c \
        exit/assert.c \
        exit/quick_exit.c \
        exit/at_quick_exit.c \
        time/strftime.c \
        time/asctime.c \
        time/asctime_r.c \
        time/ctime.c \
        time/ctime_r.c \
        time/clock.c \
        time/wcsftime.c \
        time/strptime.c \
        time/difftime.c \
        time/timegm.c \
        time/ftime.c \
        time/times.c \
        time/gmtime.c \
        time/gmtime_r.c \
        time/timespec_get.c \
        time/getdate.c \
        time/localtime.c \
        time/localtime_r.c \
        time/mktime.c \
        time/gettimeofday.c \
        time/__tm_to_secs.c \
        time/__month_to_secs.c \
        time/__secs_to_tm.c \
        time/__year_to_secs.c \
        time/__tz.c \
        time/timer_create.c \
        fcntl/creat.c \
        dirent/alphasort.c \
        dirent/versionsort.c \
        env/__stack_chk_fail.c \
        env/clearenv.c \
        env/getenv.c \
        env/putenv.c \
        env/setenv.c \
        env/unsetenv.c \
        unistd/posix_close.c \
        unistd/tcgetpgrp.c \
        unistd/tcsetpgrp.c \
        unistd/getpgid.c \
        unistd/getpgrp.c \
        unistd/setpgid.c \
        unistd/setpgrp.c \
        unistd/getsid.c \
        unistd/setsid.c \
        unistd/gethostname.c \
        unistd/ctermid.c \
        unistd/getlogin.c \
        unistd/getlogin_r.c \
        unistd/alarm.c \
        unistd/ualarm.c \
        unistd/ttyname.c \
        unistd/ttyname_r.c \
        linux/wait3.c \
        linux/wait4.c \
        linux/epoll.c \
        linux/eventfd.c \
        linux/setgroups.c \
        $(LIBC_TOP_HALF_LDSO_SOURCES) \
        stat/futimesat.c \
        stat/mknodat.c \
        legacy/getpagesize.c \
        legacy/getpass.c \
        legacy/daemon.c \
        legacy/cuserid.c \
        thread/thrd_sleep.c \
        wasix/call_dynamic.c \
        wasix/closure_allocate.c \
        wasix/closure_free.c \
        wasix/closure_prepare.c \
        wasix/reflection.c \
        wasix/context.c \
        wasix/flock.c \
        aio/aio.c \
        aio/aio_suspend.c \
        aio/lio_listio.c \
    ) \
    $(filter-out %/procfdname.c %/syscall.c %/syscall_ret.c %/vdso.c %/version.c, \
                 $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/internal/*.c)) \
    $(filter-out %/rename.c \
                 %/tempnam.c \
                 %/remove.c \
                 %/gets.c, \
                 $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/stdio/*.c)) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/string/*.c) \
    $(filter-out %/bind_textdomain_codeset.c, \
                 $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/locale/*.c)) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/stdlib/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/setjmp/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/thread/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/signal/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/process/*.c) \
    $(filter-out %/sched_yield.c, \
                 $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/sched/*.c)) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/mq/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/env/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/exit/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/search/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/multibyte/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/regex/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/prng/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/conf/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/passwd/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/ctype/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/termios/*.c) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/temp/*.c) \
    $(filter-out %/__signbit.c %/__signbitf.c %/__signbitl.c \
                 %/__fpclassify.c %/__fpclassifyf.c %/__fpclassifyl.c \
                 %/ceilf.c %/ceil.c \
                 %/floorf.c %/floor.c \
                 %/truncf.c %/trunc.c \
                 %/rintf.c %/rint.c \
                 %/nearbyintf.c %/nearbyint.c \
                 %/sqrtf.c %/sqrt.c \
                 %/fabsf.c %/fabs.c \
                 %/copysignf.c %/copysign.c \
                 %/fminf.c %/fmaxf.c \
                 %/fmin.c %/fmax.c, \
                 $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/math/*.c)) \
    $(filter-out %/crealf.c %/creal.c %creall.c \
                 %/cimagf.c %/cimag.c %cimagl.c, \
                 $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/complex/*.c)) \
    $(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/crypt/*.c)

ifeq ($(THREAD_MODEL), posix)
LIBC_TOP_HALF_MUSL_SOURCES += \
    $(addprefix $(LIBC_TOP_HALF_MUSL_SRC_DIR)/, \
        env/__init_tls.c \
        stdio/__lockfile.c \
        stdio/flockfile.c \
        stdio/ftrylockfile.c \
        stdio/funlockfile.c \
        thread/__lock.c \
        thread/__wait.c \
        thread/__timedwait.c \
        thread/default_attr.c \
        thread/pthread_attr_destroy.c \
        thread/pthread_attr_get.c \
        thread/pthread_attr_init.c \
        thread/pthread_attr_setstack.c \
        thread/pthread_attr_setdetachstate.c \
        thread/pthread_attr_setstacksize.c \
        thread/pthread_barrier_destroy.c \
        thread/pthread_barrier_init.c \
        thread/pthread_barrier_wait.c \
        thread/pthread_cleanup_push.c \
        thread/pthread_cond_broadcast.c \
        thread/pthread_cond_destroy.c \
        thread/pthread_cond_init.c \
        thread/pthread_cond_signal.c \
        thread/pthread_cond_timedwait.c \
        thread/pthread_cond_wait.c \
        thread/pthread_condattr_destroy.c \
        thread/pthread_condattr_init.c \
        thread/pthread_condattr_setclock.c \
        thread/pthread_condattr_setpshared.c \
        thread/pthread_create.c \
        thread/pthread_detach.c \
        thread/pthread_equal.c \
        thread/pthread_getspecific.c \
        thread/pthread_join.c \
        thread/pthread_key_create.c \
        thread/pthread_mutex_consistent.c \
        thread/pthread_mutex_destroy.c \
        thread/pthread_mutex_init.c \
        thread/pthread_mutex_getprioceiling.c \
        thread/pthread_mutex_lock.c \
        thread/pthread_mutex_timedlock.c \
        thread/pthread_mutex_trylock.c \
        thread/pthread_mutex_unlock.c \
        thread/pthread_mutexattr_destroy.c \
        thread/pthread_mutexattr_init.c \
        thread/pthread_mutexattr_setprotocol.c \
        thread/pthread_mutexattr_setpshared.c \
        thread/pthread_mutexattr_setrobust.c \
        thread/pthread_mutexattr_settype.c \
        thread/pthread_once.c \
        thread/pthread_rwlock_destroy.c \
        thread/pthread_rwlock_init.c \
        thread/pthread_rwlock_rdlock.c \
        thread/pthread_rwlock_timedrdlock.c \
        thread/pthread_rwlock_timedwrlock.c \
        thread/pthread_rwlock_tryrdlock.c \
        thread/pthread_rwlock_trywrlock.c \
        thread/pthread_rwlock_unlock.c \
        thread/pthread_rwlock_wrlock.c \
        thread/pthread_rwlockattr_destroy.c \
        thread/pthread_rwlockattr_init.c \
        thread/pthread_rwlockattr_setpshared.c \
        thread/pthread_setcancelstate.c \
        thread/pthread_setspecific.c \
        thread/pthread_self.c \
        thread/pthread_spin_destroy.c \
        thread/pthread_spin_init.c \
        thread/pthread_spin_lock.c \
        thread/pthread_spin_trylock.c \
        thread/pthread_spin_unlock.c \
        thread/pthread_testcancel.c \
        thread/sem_destroy.c \
        thread/sem_getvalue.c \
        thread/sem_init.c \
        thread/sem_open.c \
        thread/sem_post.c \
        thread/sem_timedwait.c \
        thread/sem_trywait.c \
        thread/sem_unlink.c \
        thread/sem_wait.c \
        thread/$(TARGET_ARCH)/wasi_thread_start.s \
    )
endif

# firebox#5X0: the NOTHREADS variants deliberately build with THREAD_MODEL=posix
# (wasi-libc's stdio wildcard unconditionally pulls __lockfile.c/flockfile.c,
# which need _IO_FILE.lock — THREAD_MODEL=single does not compile upstream), so
# THREAD_MODEL cannot be the discriminator here. The variant states its own fact
# with -D__FIREBOX_NO_THREADS__ (see scripts/wasix-libc/build.sh, where it is
# DERIVED from the variant's own CFLAGS rather than hand-listed per variant), and
# this rule keys off that single marker so a future nothreads variant inherits it.
#
# wasi_thread_start.s is the thread-entry trampoline. It is ASSEMBLY, so it
# cannot gate itself with #ifdef the way pthread_create.c does, and it carries a
# hard `U __wasm_init_tls` — a global/function that wasm-ld synthesizes ONLY
# under --shared-memory. On a nothreads `-shared` libc.so link that reference
# survives as an unsatisfiable `env` import. Its only in-libc caller
# (__wasi_thread_start_C) is #ifdef'd out on the same marker, so dropping the
# object leaves nothing dangling.
ifneq ($(findstring __FIREBOX_NO_THREADS__,$(EXTRA_CFLAGS)),)
LIBC_TOP_HALF_MUSL_SOURCES := $(filter-out %/wasi_thread_start.s,$(LIBC_TOP_HALF_MUSL_SOURCES))
endif
