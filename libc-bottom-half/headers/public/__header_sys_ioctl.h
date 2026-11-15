#ifndef __wasilibc___header_sys_ioctl_h
#define __wasilibc___header_sys_ioctl_h

#define FIONREAD 1
#define FIONBIO 2

#define TIOCGWINSZ 0x101
#define TIOCSWINSZ 0x102

/* Linux terminal-control requests forwarded by the WASIX fd_ioctl seam. */
#define TCSBRK 0x5409
#define TCXONC 0x540a
#define TCFLSH 0x540b

#ifdef __cplusplus
extern "C" {
#endif

/* MUSL includes these defines */
#define __NEED_struct_winsize
#include <bits/alltypes.h>
#include <bits/ioctl.h>
/* glibc also includes this one */
#include <sys/ttydefaults.h>

int ioctl(int, int, ...);

#ifdef __cplusplus
}
#endif

#endif
