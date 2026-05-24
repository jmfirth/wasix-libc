#include <pthread.h>
#include "libc.h"
#include "lock.h"
#ifndef __wasilibc_unmodified_upstream
#include "pthread_impl.h"  /* firebox#472: __firebox_lock_sweep_wake_one */
#endif

static struct atfork_funcs {
	void (*prepare)(void);
	void (*parent)(void);
	void (*child)(void);
	struct atfork_funcs *prev, *next;
} *funcs;

static volatile int lock[1];

void __fork_handler(int who)
{
	struct atfork_funcs *p;
	if (!funcs) return;
	if (who < 0) {
		LOCK(lock);
		for (p=funcs; p; p = p->next) {
			if (p->prepare) p->prepare();
			funcs = p;
		}
	} else {
		for (p=funcs; p; p = p->prev) {
			if (!who && p->parent) p->parent();
			else if (who && p->child) p->child();
			funcs = p;
		}
		UNLOCK(lock);
#ifndef __wasilibc_unmodified_upstream
		/* firebox#472: targeted sweep-wake at the parent/child phase's
		 * export boundary. If any handler invocation (parent or child
		 * callback) Binaryen-asyncify-rewound past the post-callback
		 * loop-iteration's implicit lock-state-restore — or past the
		 * final UNLOCK above — the atfork_lock is orphaned. A long-lived
		 * process that forks repeatedly (Ruby Process.fork, system(),
		 * any spawn) then walls on every subsequent __fork_handler(-1)
		 * blocking forever on the static atfork_lock.
		 *
		 * Sweep is restricted to the who>=0 branch because the who<0
		 * (prepare) branch INTENTIONALLY holds the lock across the
		 * fork syscall (released by the matching who>=0 call in both
		 * parent and child). Sweeping there would prematurely release
		 * the lock, allowing a concurrent pthread_atfork registration
		 * to race the in-flight fork. See work/tasks/472-*. */
		__firebox_lock_sweep_wake_one(lock);
#endif
	}
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
	struct atfork_funcs *new = malloc(sizeof *new);
	if (!new) return -1;

	LOCK(lock);
	new->next = funcs;
	new->prev = 0;
	new->prepare = prepare;
	new->parent = parent;
	new->child = child;
	if (funcs) funcs->prev = new;
	funcs = new;
	UNLOCK(lock);
	return 0;
}
