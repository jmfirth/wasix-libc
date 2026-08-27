#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)

#ifdef _GNU_SOURCE
#define __ucontext ucontext
#endif

#define __NEED_size_t
#define __NEED_pid_t
#define __NEED_uid_t
#define __NEED_struct_timespec
#define __NEED_pthread_t
#define __NEED_pthread_attr_t
#define __NEED_time_t
#define __NEED_clock_t
#define __NEED_sigset_t

#include <bits/alltypes.h>

#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

#define SI_ASYNCNL (-60)
#define SI_TKILL (-6)
#define SI_SIGIO (-5)
#define SI_ASYNCIO (-4)
#define SI_MESGQ (-3)
#define SI_TIMER (-2)
#define SI_QUEUE (-1)
#define SI_USER 0
#define SI_KERNEL 128

typedef struct sigaltstack stack_t;

#endif

#include <bits/signal.h>

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)

#define SIG_HOLD ((void (*)(int)) 2)

#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define FPE_FLTDIV 3
#define FPE_FLTOVF 4
#define FPE_FLTUND 5
#define FPE_FLTRES 6
#define FPE_FLTINV 7
#define FPE_FLTSUB 8

#define ILL_ILLOPC 1
#define ILL_ILLOPN 2
#define ILL_ILLADR 3
#define ILL_ILLTRP 4
#define ILL_PRVOPC 5
#define ILL_PRVREG 6
#define ILL_COPROC 7
#define ILL_BADSTK 8

#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
#define SEGV_BNDERR 3
#define SEGV_PKUERR 4

#define BUS_ADRALN 1
#define BUS_ADRERR 2
#define BUS_OBJERR 3
#define BUS_MCEERR_AR 4
#define BUS_MCEERR_AO 5

#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_TRAPPED 4
#define CLD_STOPPED 5
#define CLD_CONTINUED 6

union sigval {
	int sival_int;
	void *sival_ptr;
};

typedef struct {
#ifdef __SI_SWAP_ERRNO_CODE
	int si_signo, si_code, si_errno;
#else
	int si_signo, si_errno, si_code;
#endif
	union {
		char __pad[128 - 2*sizeof(int) - sizeof(long)];
		struct {
			union {
				struct {
					pid_t si_pid;
					uid_t si_uid;
				} __piduid;
				struct {
					int si_timerid;
					int si_overrun;
				} __timer;
			} __first;
			union {
				union sigval si_value;
				struct {
					int si_status;
					clock_t si_utime, si_stime;
				} __sigchld;
			} __second;
		} __si_common;
		struct {
			void *si_addr;
			short si_addr_lsb;
			union {
				struct {
					void *si_lower;
					void *si_upper;
				} __addr_bnd;
				unsigned si_pkey;
			} __first;
		} __sigfault;
		struct {
			long si_band;
			int si_fd;
		} __sigpoll;
		struct {
			void *si_call_addr;
			int si_syscall;
			unsigned si_arch;
		} __sigsys;
	} __si_fields;
} siginfo_t;
#define si_pid     __si_fields.__si_common.__first.__piduid.si_pid
#define si_uid     __si_fields.__si_common.__first.__piduid.si_uid
#define si_status  __si_fields.__si_common.__second.__sigchld.si_status
#define si_utime   __si_fields.__si_common.__second.__sigchld.si_utime
#define si_stime   __si_fields.__si_common.__second.__sigchld.si_stime
#define si_value   __si_fields.__si_common.__second.si_value
#define si_addr    __si_fields.__sigfault.si_addr
#define si_addr_lsb __si_fields.__sigfault.si_addr_lsb
#define si_lower   __si_fields.__sigfault.__first.__addr_bnd.si_lower
#define si_upper   __si_fields.__sigfault.__first.__addr_bnd.si_upper
#define si_pkey    __si_fields.__sigfault.__first.si_pkey
#define si_band    __si_fields.__sigpoll.si_band
#define si_fd      __si_fields.__sigpoll.si_fd
#define si_timerid __si_fields.__si_common.__first.__timer.si_timerid
#define si_overrun __si_fields.__si_common.__first.__timer.si_overrun
#define si_ptr     si_value.sival_ptr
#define si_int     si_value.sival_int
#define si_call_addr __si_fields.__sigsys.si_call_addr
#define si_syscall __si_fields.__sigsys.si_syscall
#define si_arch    __si_fields.__sigsys.si_arch

struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
	} __sa_handler;
	sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
};
#define sa_handler   __sa_handler.sa_handler
#define sa_sigaction __sa_handler.sa_sigaction

struct sigevent {
	union sigval sigev_value;
	int sigev_signo;
	int sigev_notify;
	union {
		char __pad[64 - 2*sizeof(int) - sizeof(union sigval)];
		pid_t sigev_notify_thread_id;
		struct {
			void (*sigev_notify_function)(union sigval);
			pthread_attr_t *sigev_notify_attributes;
		} __sev_thread;
	} __sev_fields;
};

#define sigev_notify_thread_id __sev_fields.sigev_notify_thread_id
#define sigev_notify_function __sev_fields.__sev_thread.sigev_notify_function
#define sigev_notify_attributes __sev_fields.__sev_thread.sigev_notify_attributes

#define SIGEV_SIGNAL 0
#define SIGEV_NONE 1
#define SIGEV_THREAD 2
#define SIGEV_THREAD_ID 4

/* Realtime-signal range (#WJJ). Firebox ships the SIGRTMIN..SIGRTMAX
 * surface: __libc_current_sigrtmin/max are real, exported functions
 * (src/signal/sigrtmin.c / sigrtmax.c, present in defined-symbols.txt)
 * and _POSIX_REALTIME_SIGNALS / _SC_REALTIME_SIGNALS are advertised, so
 * sysconf(_SC_REALTIME_SIGNALS) > 0 and POSIX programs reasonably expect
 * the macros to exist. Upstream wasi-libc gated these out under
 * __wasilibc_unmodified_upstream because stock WASI has no signals at
 * all; that premise no longer holds for Firebox (W1 delivers signals).
 * Leaving the macros gated out is what BUILDFAILs the Open POSIX RT tests
 * (e.g. sigaction/29-1: "use of undeclared identifier 'SIGRTMAX'"). */
int __libc_current_sigrtmin(void);
int __libc_current_sigrtmax(void);

#define SIGRTMIN  (__libc_current_sigrtmin())
#define SIGRTMAX  (__libc_current_sigrtmax())

int kill(pid_t, int);

int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);

int sigprocmask(int, const sigset_t *__restrict, sigset_t *__restrict);
int sigsuspend(const sigset_t *);
int sigaction(int, const struct sigaction *__restrict, struct sigaction *__restrict);
int sigpending(sigset_t *);
int sigwait(const sigset_t *__restrict, int *__restrict);
int sigwaitinfo(const sigset_t *__restrict, siginfo_t *__restrict);
int sigtimedwait(const sigset_t *__restrict, siginfo_t *__restrict, const struct timespec *__restrict);
int sigqueue(pid_t, int, union sigval);

int pthread_sigmask(int, const sigset_t *__restrict, sigset_t *__restrict);
int pthread_kill(pthread_t, int);

/* firebox#H18: DEFINED in our libc.a, so the prototype must be visible.
 * Upstream hid psiginfo behind __wasilibc_unmodified_upstream because stock
 * wasi-libc has no implementation. Firebox DOES: MEASURED T psiginfo in libc.a.
 * A capability we define but do not DECLARE is unreachable -- the caller's only
 * recourse is to hand-declare it, and a hand-declaration in front of a real
 * implementation is a latent ABI mismatch (firebox#4ZY deleted packages/vi/shims
 * for exactly that). The block-mates left hidden are NOT defined (MEASURED 0). */
/* ⚠️ The guard's reason -- "WASI has no siginfo" -- is FALSE about Firebox: real signal
 * semantics are the moat, and siginfo_t is declared in this very header. */
void psiginfo(const siginfo_t *, const char *);
void psignal(int, const char *);

#endif

#if defined(_XOPEN_SOURCE) || defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
int killpg(pid_t, int);

/* XSI/System-V signal API (#43B / S5). Firebox ships REAL implementations
 * of these (sighold/sigrelse/sigignore/sigpause/sigset/siginterrupt in
 * src/signal/, each layered on the already-faithful sigprocmask/sigaction/
 * sigsuspend primitives, and present in defined-symbols.txt). Upstream
 * wasi-libc buried them inside the __wasilibc_unmodified_upstream "WASI has
 * no signals" block — the SAME stale premise the #WJJ SIGRTMIN ungate
 * (above) already overturned: Firebox delivers signals (W1 + the whole
 * S1-S4 signal-conformance wave). Leaving them gated out is what BUILDFAILs
 * the Open POSIX SysV-signal tests (sigpause, sigtimedwait 4-1, sigwaitinfo,
 * sigqueue 4-1..9-1: "call to undeclared function 'sighold' / 'sigpause'")
 * even though the functions link fine. sigaltstack is intentionally NOT
 * re-declared here — it already has an arch-level prototype in bits/signal.h
 * (the #9PX/#ZFF path) and the SS_ macros live in arch/wasm32/bits/signal.h;
 * re-adding them here would duplicate. The TRAP_ and POLL_ si_code constants
 * stay with the upstream block below (siginfo codes, not part of this
 * signal-API surface). */
int sighold(int);
int sigignore(int);
int siginterrupt(int, int);
int sigpause(int);
int sigrelse(int);
void (*sigset(int, void (*)(int)))(int);
#endif
#ifdef __wasilibc_unmodified_upstream /* WASI has no signals */
#if defined(_XOPEN_SOURCE) || defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
int sigaltstack(const stack_t *__restrict, stack_t *__restrict);
#define TRAP_BRKPT 1
#define TRAP_TRACE 2
#define TRAP_BRANCH 3
#define TRAP_HWBKPT 4
#define TRAP_UNK 5
#define POLL_IN 1
#define POLL_OUT 2
#define POLL_MSG 3
#define POLL_ERR 4
#define POLL_PRI 5
#define POLL_HUP 6
#define SS_ONSTACK    1
#define SS_DISABLE    2
#define SS_AUTODISARM (1U << 31)
#define SS_FLAG_BITS SS_AUTODISARM
#endif
#endif

#if defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
#define NSIG _NSIG
typedef void (*sig_t)(int);
#endif

#ifdef _GNU_SOURCE
typedef void (*sighandler_t)(int);
void (*bsd_signal(int, void (*)(int)))(int);
/* firebox#H18: DEFINED in our libc.a, so the prototype must be visible.
 * Upstream hid the GNU sigset operators behind __wasilibc_unmodified_upstream because stock
 * wasi-libc has no implementation. Firebox DOES: MEASURED T sigisemptyset / T sigorset / T sigandset in libc.a.
 * A capability we define but do not DECLARE is unreachable -- the caller's only
 * recourse is to hand-declare it, and a hand-declaration in front of a real
 * implementation is a latent ABI mismatch (firebox#4ZY deleted packages/vi/shims
 * for exactly that). The block-mates left hidden are NOT defined (MEASURED 0). */
/* ⚠️ "WASI has no signal sets" is FALSE about Firebox. These stay inside _GNU_SOURCE,
 * which is correct -- they are GNU extensions -- but they are no longer hidden from a
 * _GNU_SOURCE build that has every right to them. */
int sigisemptyset(const sigset_t *);
int sigorset (sigset_t *, const sigset_t *, const sigset_t *);
int sigandset(sigset_t *, const sigset_t *, const sigset_t *);

#ifdef __wasilibc_unmodified_upstream /* WASI has no signal sets */
#define SA_NOMASK SA_NODEFER
#define SA_ONESHOT SA_RESETHAND
#endif
#endif

#ifdef __wasilibc_unmodified_upstream /* 1 is a valid function pointer on wasm */
#define SIG_ERR  ((void (*)(int))-1)
#define SIG_DFL  ((void (*)(int)) 0)
#define SIG_IGN  ((void (*)(int)) 1)
#else
void __SIG_ERR(int);
void __SIG_IGN(int);
#define SIG_ERR  (__SIG_ERR)
#define SIG_DFL  ((void (*)(int)) 0)
#define SIG_IGN  (__SIG_IGN)
#endif

#ifdef __wasilibc_unmodified_upstream /* Make sig_atomic_t 64-bit on wasm64 */
typedef int sig_atomic_t;
#else
typedef long sig_atomic_t;
#endif

void (*signal(int, void (*)(int)))(int);
int raise(int);

#ifdef __wasilibc_unmodified_upstream /* WASI has no sigtimedwait */
#if _REDIR_TIME64
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
__REDIR(sigtimedwait, __sigtimedwait_time64);
#endif
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
