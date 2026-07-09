// Each of the following math functions can be implemented with a single
// wasm instruction, so use that implementation rather than the portable
// one in libm.

#include <math.h>
#include <fenv.h>

float fabsf(float x) {
    return __builtin_fabsf(x);
}

double fabs(double x) {
    return __builtin_fabs(x);
}

float sqrtf(float x) {
    return __builtin_sqrtf(x);
}

double sqrt(double x) {
    return __builtin_sqrt(x);
}

float copysignf(float x, float y) {
    return __builtin_copysignf(x, y);
}

double copysign(double x, double y) {
    return __builtin_copysign(x, y);
}

float ceilf(float x) {
    return __builtin_ceilf(x);
}

double ceil(double x) {
    return __builtin_ceil(x);
}

float floorf(float x) {
    return __builtin_floorf(x);
}

double floor(double x) {
    return __builtin_floor(x);
}

float truncf(float x) {
    return __builtin_truncf(x);
}

double trunc(double x) {
    return __builtin_trunc(x);
}

float nearbyintf(float x) {
    return __builtin_nearbyintf(x);
}

double nearbyint(double x) {
    return __builtin_nearbyint(x);
}

float rintf(float x) {
    float y = __builtin_rintf(x);
    /* firebox #FPH: rint must raise FE_INEXACT iff it changed x (x was not
       already an integer). The wasm f32.nearest builtin cannot signal, so the
       fix lives HERE (math-builtins.c is what the Makefile links; the musl
       src/math/rintf.c is filter-out'd dead code — see the class lesson
       math-fix-dead-when-filtered-out-verify-llvm-nm). isfinite() guards inf
       (y==x) and nan (isfinite(nan) is false), so neither signals. */
    if (isfinite(x) && y != x)
        feraiseexcept(FE_INEXACT);
    return y;
}

double rint(double x) {
    double y = __builtin_rint(x);
    /* firebox #FPH: see rintf — raise FE_INEXACT iff rounding changed x. */
    if (isfinite(x) && y != x)
        feraiseexcept(FE_INEXACT);
    return y;
}
