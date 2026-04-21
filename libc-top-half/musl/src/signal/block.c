#include "pthread_impl.h"
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#include "atomic.h"
#endif
#include <signal.h>

#ifdef __wasilibc_unmodified_upstream
#else
/* Process-wide "signals blocked" flag set by __block_all_sigs and
 * cleared by __restore_sigs. Defined in sigaction.c. Consulted by
 * __wasm_signal before dispatching a handler. */
extern volatile int __wasm_signals_blocked;
/* Drain helper — re-raises queued signals after restore_sigs clears
 * the block. Defined in sigaction.c. */
void __wasm_drain_pending_sigs(void);
#endif

static const unsigned long all_mask[] = {
#if ULONG_MAX == 0xffffffff && _NSIG == 129
	-1UL, -1UL, -1UL, -1UL
#elif ULONG_MAX == 0xffffffff
	-1UL, -1UL
#else
	-1UL
#endif
};

static const unsigned long app_mask[] = {
#if ULONG_MAX == 0xffffffff
#if _NSIG == 65
	0x7fffffff, 0xfffffffc
#else
	0x7fffffff, 0xfffffffc, -1UL, -1UL
#endif
#else
#if _NSIG == 65
	0xfffffffc7fffffff
#else
	0xfffffffc7fffffff, -1UL
#endif
#endif
};

void __block_all_sigs(void *set)
{
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigprocmask, SIG_BLOCK, &all_mask, set, _NSIG/8);
#else
	/* Set the dedicated process-wide blocked flag. __wasm_signal will
	 * see this on its next entry and enqueue into __wasm_pending_sigs
	 * rather than dispatch. The callback_signal keeps the runtime's
	 * notification mechanism armed; the __wasm_signal_blocked export
	 * (new in this patch) is a no-op on the libc side. */
	a_store(&__wasm_signals_blocked, 1);
	__wasi_callback_signal("__wasm_signal_blocked");
#endif
}

void __block_app_sigs(void *set)
{
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigprocmask, SIG_BLOCK, &app_mask, set, _NSIG/8);
#else
	a_store(&__wasm_signals_blocked, 1);
	__wasi_callback_signal("__wasm_signal_blocked");
#endif
}

void __restore_sigs(void *set)
{
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigprocmask, SIG_SETMASK, set, 0, _NSIG/8);
#else
	/* Clear the block flag, re-arm the normal dispatcher, then drain
	 * any signals that queued during the block window. Drain must
	 * happen AFTER the flag clears so re-raised signals dispatch. */
	a_store(&__wasm_signals_blocked, 0);
	__wasi_callback_signal("__wasm_signal");
	__wasm_drain_pending_sigs();
#endif
}