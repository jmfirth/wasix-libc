/* firebox#9PX FROZEN REGRESSION PROBE — SA_ONSTACK runs the handler on the
 * sigaltstack-registered alternate stack.
 *
 * Stronger than the Open POSIX sigaction/12-* witnesses: those only check that
 * sigaltstack(NULL,&ss) round-trips the registered ss_sp/ss_size. This probe
 * additionally asserts the handler's ACTUAL stack pointer (the address of a
 * handler local) lies INSIDE [ss_sp, ss_sp+ss_size) — i.e. the shadow stack
 * really switched. It also asserts:
 *   - SS_ONSTACK is reported in ss_flags while the handler runs;
 *   - sigaltstack() refuses to change the alt stack mid-handler (EPERM);
 *   - a handler WITHOUT SA_ONSTACK runs on the normal stack (no false switch).
 *
 * Exit 0 = all assertions hold (PASS). Non-zero = a specific failure (printed).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

static char       alt_buf[SIGSTKSZ];
static uintptr_t  alt_lo, alt_hi;
static volatile int onstack_ok, flags_ok, eperm_ok, ran_onstack, ran_plain;
static volatile uintptr_t plain_sp;

static void onstack_handler(int sig) {
    char local;                      /* address = current shadow-stack frame */
    uintptr_t sp = (uintptr_t)&local;
    ran_onstack = 1;
    onstack_ok = (sp >= alt_lo && sp < alt_hi);

    /* While on the alt stack, sigaltstack() must report SS_ONSTACK and refuse
     * to change the alt stack (EPERM). */
    stack_t q; memset(&q, 0, sizeof q);
    if (sigaltstack(NULL, &q) == 0 && (q.ss_flags & SS_ONSTACK)) flags_ok = 1;

    stack_t bad; memset(&bad, 0, sizeof bad);
    static char other[SIGSTKSZ];
    bad.ss_sp = other; bad.ss_size = SIGSTKSZ; bad.ss_flags = 0;
    errno = 0;
    if (sigaltstack(&bad, NULL) == -1 && errno == EPERM) eperm_ok = 1;
}

static void plain_handler(int sig) {
    char local;
    ran_plain = 1;
    plain_sp = (uintptr_t)&local;    /* must NOT be in the alt range */
}

int main(void) {
    alt_lo = (uintptr_t)alt_buf;
    alt_hi = alt_lo + sizeof alt_buf;

    stack_t ss; memset(&ss, 0, sizeof ss);
    ss.ss_sp = alt_buf; ss.ss_size = sizeof alt_buf; ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) { perror("sigaltstack install"); return 10; }

    /* Read it back (the sigaction/12-* assertion). */
    stack_t got; memset(&got, 0, sizeof got);
    if (sigaltstack(NULL, &got) == -1) { perror("sigaltstack query"); return 11; }
    if (got.ss_sp != ss.ss_sp || got.ss_size != ss.ss_size) {
        printf("FAIL: round-trip mismatch (got sp=%p size=%zu)\n", got.ss_sp, got.ss_size);
        return 12;
    }

    /* SA_ONSTACK handler must run on the alt stack. */
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = onstack_handler; sa.sa_flags = SA_ONSTACK; sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) { perror("sigaction onstack"); return 13; }
    raise(SIGUSR1);

    /* A plain handler must run on the normal stack. */
    struct sigaction sp2; memset(&sp2, 0, sizeof sp2);
    sp2.sa_handler = plain_handler; sp2.sa_flags = 0; sigemptyset(&sp2.sa_mask);
    if (sigaction(SIGUSR2, &sp2, NULL) == -1) { perror("sigaction plain"); return 14; }
    raise(SIGUSR2);

    if (!ran_onstack) { printf("FAIL: SA_ONSTACK handler never ran\n"); return 20; }
    if (!onstack_ok)  { printf("FAIL: handler SP not within alt stack [%#zx,%#zx)\n",
                               (size_t)alt_lo, (size_t)alt_hi); return 21; }
    if (!flags_ok)    { printf("FAIL: SS_ONSTACK not reported during handler\n"); return 22; }
    if (!eperm_ok)    { printf("FAIL: mid-handler sigaltstack change not rejected with EPERM\n"); return 23; }
    if (!ran_plain)   { printf("FAIL: plain handler never ran\n"); return 24; }
    if (plain_sp >= alt_lo && plain_sp < alt_hi) {
        printf("FAIL: plain (non-SA_ONSTACK) handler ran ON the alt stack\n"); return 25;
    }

    printf("PASS: SA_ONSTACK handler ran on alt stack (sp in range); SS_ONSTACK + EPERM honored; plain handler off alt stack\n");
    return 0;
}
