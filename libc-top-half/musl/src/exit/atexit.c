#include <stdlib.h>
#include <stdint.h>
#include "libc.h"
#include "lock.h"
#include "fork_impl.h"
#ifndef __wasilibc_unmodified_upstream
#include "pthread_impl.h"  /* firebox#470: __firebox_lock_sweep_wake_one */
#endif

#define malloc __libc_malloc
#define calloc __libc_calloc
#define realloc undef
#define free undef

/* Ensure that at least 32 atexit handlers can be registered without malloc */
#define COUNT 32

static struct fl
{
	struct fl *next;
	void (*f[COUNT])(void *);
	void *a[COUNT];
} builtin, *head;

static int slot;

#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
static volatile int lock[1];
volatile int *const __atexit_lockptr = lock;
#endif

void __funcs_on_exit()
{
	void (*func)(void *), *arg;
	LOCK(lock);
	for (; head; head=head->next, slot=COUNT) while(slot-->0) {
		func = head->f[slot];
		arg = head->a[slot];
		UNLOCK(lock);
		func(arg);
		LOCK(lock);
	}
#if !defined(__wasilibc_unmodified_upstream) && defined(_REENTRANT)
	/* firebox#470: musl intentionally leaves `lock` held when the walk
	 * completes (process is about to _Exit, no further callers). But
	 * if a handler invocation rewound through Binaryen-asyncify past
	 * the UNLOCK/LOCK pair bracketing it, the held-locks accounting
	 * on this thread is out of sync with the lock-word reality. The
	 * targeted sweep clears the held-list slot and force-clears the
	 * lock word + wakes any waiters that may have arrived via
	 * concurrent __cxa_atexit calls on other threads (atypical at
	 * process-exit but possible). Safe at any point post-walk: lock
	 * is logically released either way. See work/tasks/470-* and
	 * class_lesson_thread_teardown_via_guest_asyncify_escape.
	 *
	 * firebox#552: gated on _REENTRANT to match the same guard on the
	 * `lock` variable definition (line 27-30). Without this gate the
	 * single-threaded (THREAD_MODEL=single, no _REENTRANT) ehpic-nothreads
	 * build fails with "use of undeclared identifier 'lock'" because
	 * the lock array only exists in reentrant builds. In single-threaded
	 * builds there is no sibling thread to wake, so the sweep is a
	 * no-op semantically — gating it out is correct, not a workaround. */
	__firebox_lock_sweep_wake_one(lock);
#endif
}

void __cxa_finalize(void *dso)
{
}

int __cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
	LOCK(lock);

	/* Defer initialization of head so it can be in BSS */
	if (!head) head = &builtin;

	/* If the current function list is full, add a new one */
	if (slot==COUNT) {
		struct fl *new_fl = calloc(sizeof(struct fl), 1);
		if (!new_fl) {
			UNLOCK(lock);
			return -1;
		}
		new_fl->next = head;
		head = new_fl;
		slot = 0;
	}

	/* Append function to the list. */
	head->f[slot] = func;
	head->a[slot] = arg;
	slot++;

	UNLOCK(lock);
	return 0;
}

static void call(void *p)
{
	((void (*)(void))(uintptr_t)p)();
}

int atexit(void (*func)(void))
{
	return __cxa_atexit(call, (void *)(uintptr_t)func, 0);
}
