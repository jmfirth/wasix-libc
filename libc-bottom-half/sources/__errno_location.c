#include <errno.h>

/* firebox#7GZ: fail CLOSED on the one build configuration that would make this
 * function call itself. Since #7GZ, <__errno.h>'s PIC-consumer branch defines
 * `errno` as `(*__errno_location())`; if that branch were ever reached while
 * compiling libc itself, `return &errno;` below would expand to
 * `return &(*__errno_location());` -- unbounded recursion, and a stack
 * exhaustion at runtime is indistinguishable from a hang. Every firebox libc
 * build passes -D__WASILIBC_BUILDING_LIBC (scripts/wasix-libc/build.sh:208,
 * 245, 260, 272, 279), which vetoes that branch, so this never fires today; it
 * exists because the failure it guards is silent and this is the only place
 * that can see it. */
#if defined(__PIC__) && !defined(__PIE__) && !defined(__WASILIBC_BUILDING_LIBC)
#error "__errno_location.c must be compiled with -D__WASILIBC_BUILDING_LIBC: without it <errno.h> defines errno as (*__errno_location()) and this function recurses forever (firebox#7GZ)"
#endif

int *__errno_location(void) {
    return &errno;
}
