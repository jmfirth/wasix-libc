#ifndef FIREBOX_ALTSTACK_H
#define FIREBOX_ALTSTACK_H

/* firebox#9PX — per-thread signal alternate-stack state (sigaltstack(2) /
 * SA_ONSTACK).
 *
 * POSIX makes the alternate signal stack a PER-THREAD attribute. The state is
 * held in thread-local storage rather than in `struct pthread` ON PURPOSE:
 * growing `struct pthread` would change its size/ABI for EVERY translation
 * unit that allocates or walks it (crt1, __init_tp, pthread_create, …), so a
 * partial rebuild (only the signal TUs) would read fields past the end of a
 * pthread struct allocated by the un-rebuilt allocator — uninitialised/OOB
 * memory. TLS sidesteps that entirely: only the TUs that reference these
 * `__thread` symbols carry them, and the storage is part of the per-thread
 * TLS block, independent of the pthread-struct layout. (See the #338 partial-
 * rebuild trap in docs/reference/wasix-libc-dev.md.)
 *
 * Definitions live in src/signal/sigaltstack.c; src/signal/sigaction.c reads
 * them from the SA_ONSTACK dispatch path.
 *
 *   __fbx_altstack_sp     base of the registered alt stack, or 0 if none.
 *   __fbx_altstack_size   size in bytes of the registered alt stack.
 *   __fbx_altstack_flags  retained input ss_flags (SS_FLAG_BITS only).
 *   __fbx_altstack_depth  >0 while a handler is currently executing on the
 *                         alt stack. Makes sigaltstack(2) report SS_ONSTACK
 *                         and refuse a mid-handler swap (EPERM); a depth (not
 *                         a bool) so a nested SA_ONSTACK delivery restores the
 *                         flag correctly on unwind and does not re-switch.
 *
 * Zero-initialised TLS => "no alt stack registered, not on alt stack", the
 * correct POSIX initial disposition for every thread.
 */
#include <stddef.h>

extern __thread void  *__fbx_altstack_sp;
extern __thread size_t __fbx_altstack_size;
extern __thread int    __fbx_altstack_flags;
extern __thread int    __fbx_altstack_depth;

#endif
