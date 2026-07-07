#ifndef AIO_IMPL_H
#define AIO_IMPL_H

extern hidden volatile int __aio_fut;

extern hidden int __aio_close(int);
extern hidden void __aio_atfork(int);

#ifndef __wasilibc_unmodified_upstream
/* firebox#5DB — faithful SIGEV_SIGNAL completion notification shared by aio.c
 * (per-operation completion) and lio_listio.c (list completion). Linux musl
 * delivers the completion signal with rt_sigqueueinfo(getpid(), signo, si);
 * on the wasix substrate there is no such syscall, so the delivery is routed
 * through the fork's RT-signal FIFO + a process-directed host nudge instead.
 * Defined in aio.c. See the definition there for the delivery rationale. */
#include <signal.h>
hidden void __aio_notify_signal(int signo, union sigval value);
#endif

#endif
