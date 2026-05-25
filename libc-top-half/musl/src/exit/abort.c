#include <stdlib.h>
#include <signal.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#include <stdint.h>
#endif
#include "pthread_impl.h"
#include "atomic.h"
#include "lock.h"
#include "ksigaction.h"

/* firebox#526 — abort() backtrace instrumentation.
 *
 * Predecessor: 4-cycle wedge-investigation arc on Edge.js cascade-9 (#455,
 * #505, #511, #516) converged on "the wedge actor calls wasi-libc's
 * abort() directly on the main thread". #516 confirmed via link-time
 * --wrap sentinels that:
 *   - it is NOT an assertion (no __assert_fail predecessor)
 *   - it is NOT a C++ throw (no __cxa_throw predecessor)
 *   - it is NOT std::terminate (no _ZSt9terminatev predecessor)
 *   - it IS a direct raw abort() — but which wasi-libc function?
 *
 * Link-time --wrap on pthread_mutex_lock / raise / pthread_self
 * DEADLOCKS startup (class lesson wasm_ld_wrap_startup_hazard — every
 * wrapper's write(2,...) emit re-enters wasi-libc through stdio mutex
 * acquisition). So we instrument INSIDE wasi-libc itself.
 *
 * --- Why no __builtin_return_address ---
 *
 * The natural first instinct for "abort backtrace" is to walk caller
 * frames via __builtin_return_address(N). On wasm32 this fails with a
 * clang front-end error:
 *
 *     error: Non-Emscripten WebAssembly hasn't implemented
 *            __builtin_return_address
 *
 * because wasm has no native frame-pointer / return-address-on-stack
 * convention (the call stack is the wasm value stack, opaque to guest
 * code). Emscripten implements the intrinsic by walking JS-side
 * stack-trace strings — that path isn't available in a pure-WASIX
 * build. So a true "frame walker" doesn't exist here.
 *
 * --- The breadcrumb mechanism instead ---
 *
 * We use a TLS-backed "last-set caller" string that suspect wasi-libc
 * primitives stamp on entry. On abort, we dump the most-recently-stamped
 * value. The set of stamping sites is intentionally small — only the
 * primitives most likely to be the wedge actor for #526:
 *
 *   - raise()           (signal-delivery; suspected #516 caller of abort)
 *   - __pthread_exit()  (thread teardown; sibling class lesson
 *                        thread_teardown_via_guest_asyncify_escape)
 *   - __lock()          (universal libc internal mutex)
 *
 * Other consumers (pthread_mutex_lock, futex helpers, atexit) bottom
 * out in __lock() so the breadcrumb captures them indirectly.
 *
 * Each stamp is a compile-time string literal (no malloc, no copy —
 * just the pointer) so stamping costs one TLS store. The breadcrumb
 * variable lives in this TU so that abort.o references it directly
 * and pulls it into the link without depending on weak symbols.
 *
 * --- Emission discipline ---
 *
 * Inside firebox_526_abort_trace:
 *   - Stack-allocated buffer (no malloc — heap may be wedged)
 *   - Raw __wasi_fd_write directly (no stdio — stdio acquires a mutex
 *     via the same pthread_mutex_lock that may BE the wedge actor)
 *   - Hand-rolled itoa (no snprintf — locale lock + reentrancy)
 *   - One syscall per emit (atomicity wrt other threads' stderr writes)
 *
 * --- Retirement ---
 *
 * This patch retires when #526's RCA closes the cascade-9 wedge at the
 * actual layer. Tracked in docs/reference/forks.md §5 retirement
 * registry. The breadcrumb mechanism (firebox_526_breadcrumb + its
 * stamping sites) is removed in lockstep.
 */

/* The breadcrumb. NULL until any instrumented primitive stamps it.
 * __thread so each thread has its own value — the wedge actor's
 * thread is what we care about, and main-thread abort dumps main's
 * breadcrumb specifically.
 *
 * `used` keeps the symbol live across LTO. `visibility("default")`
 * lets the breadcrumb-stamping sites in OTHER translation units
 * (raise.c, pthread_exit.c, __lock.c) reference it via `extern`. */
__attribute__((used, visibility("default")))
__thread const char *firebox_526_breadcrumb = (const char *)0;

/* Helper exported with default visibility so stamp sites in OTHER
 * translation units (raise.c, sigaction.c, pthread_create.c) can use
 * it without each one needing to declare the TLS variable directly.
 * The literal-pointer protocol means stamping costs one TLS store. */
__attribute__((used, visibility("default")))
void firebox_526_stamp(const char *crumb) {
	firebox_526_breadcrumb = crumb;
}

static __attribute__((noinline,used)) void firebox_526_abort_trace(void) {
	char buf[256];
	size_t off = 0;
	static const char prefix[] = "FIREBOX_526_ABORT_FROM crumb=";
	for (size_t i = 0; i < sizeof(prefix) - 1; ++i) {
		buf[off++] = prefix[i];
	}

	/* Dump the breadcrumb's value. If no instrumented primitive
	 * stamped it before this thread reached abort(), emit "(unset)"
	 * — that itself is data (tells the next agent that the wedge
	 * actor is NOT one of the instrumented sites; we need to add
	 * more stamping). */
	const char *crumb = firebox_526_breadcrumb;
	if (crumb == (const char *)0) {
		static const char unset[] = "(unset)";
		for (size_t i = 0; i < sizeof(unset) - 1; ++i) {
			buf[off++] = unset[i];
		}
	} else {
		/* Bounded copy — never trust a pointer length without
		 * a cap, even when we own the source literals. */
		size_t cap = sizeof(buf) - off - 8; /* reserve for tail */
		size_t i = 0;
		while (i < cap && crumb[i] != 0) {
			buf[off++] = crumb[i++];
		}
	}

	buf[off++] = '\n';

	/* Raw __wasi_fd_write — bypass stdio mutex (which may BE the
	 * wedge actor). fd 2 = stderr. Best-effort: if write fails we
	 * have no recourse (we are inside abort()), but losing the
	 * sentinel is non-fatal. */
	__wasi_ciovec_t iov;
	iov.buf = (const uint8_t *)buf;
	iov.buf_len = off;
	__wasi_size_t nwritten;
	(void)__wasi_fd_write(2, &iov, 1, &nwritten);
}

_Noreturn void abort(void)
{
	/* firebox#526: emit caller breadcrumb BEFORE doing anything
	 * else. The downstream raise/_Exit path may be wedged itself;
	 * emitting first guarantees the sentinel reaches stderr even
	 * if the rest of abort() hangs. */
	firebox_526_abort_trace();

	raise(SIGABRT);

	/* If there was a SIGABRT handler installed and it returned, or if
	 * SIGABRT was blocked or ignored, take an AS-safe lock to prevent
	 * sigaction from installing a new SIGABRT handler, uninstall any
	 * handler that may be present, and re-raise the signal to generate
	 * the default action of abnormal termination. */
	__block_all_sigs(0);
	LOCK(__abort_lock);
#ifdef __wasilibc_unmodified_upstream
	__syscall(SYS_rt_sigaction, SIGABRT,
		&(struct k_sigaction){.handler = SIG_DFL}, 0, _NSIG/8);
	__syscall(SYS_tkill, __pthread_self()->tid, SIGABRT);
	__syscall(SYS_rt_sigprocmask, SIG_UNBLOCK,
		&(long[_NSIG/(8*sizeof(long))]){1UL<<(SIGABRT-1)}, 0, _NSIG/8);
#else
	int r;
	r = __wasi_thread_signal(__pthread_self()->tid, SIGABRT);
	_Exit(127);
#endif

	/* Beyond this point should be unreachable. */
	a_crash();
	raise(SIGKILL);
	_Exit(127);
}
