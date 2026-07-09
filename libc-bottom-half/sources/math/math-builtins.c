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
    float y = __builtin_sqrtf(x);
    /* firebox #FPH (RC1-sqrt): the wasm f32.sqrt builtin sets no software-fenv
       flag, and src/math/sqrtf.c is filter-out'd dead code (see the class lesson
       math-fix-dead-when-filtered-out-verify-llvm-nm), so we raise the IEEE
       exceptions here. Two cases the musl/Open-POSIX drivers check strictly:
         - x < 0 (INCLUDING -inf): sqrt is domain-invalid -> FE_INVALID (result
           is NaN). The check is `x < 0`, NOT `isfinite(x) && x < 0`: the
           special/sqrtf.h table probes sqrt(-inf) wanting INVALID, and an
           isfinite guard would miss it (all-or-nothing driver -> whole row
           FAILs). `x < 0` is true for -inf and finite negatives, and correctly
           FALSE for -0.0 (== 0, sqrt(-0)=-0 no exception) and for NaN (unordered
           compares false, sqrt(nan)=nan no exception).
         - x > 0 (FINITE): FE_INEXACT iff the exact root is not representable.
       Inexactness oracle by WIDENING: f32.sqrt is IEEE correctly-rounded, so
       y*y == x exactly iff x is a perfect square iff the root was representable.
       (double)y*(double)y computes that square EXACTLY — a float square is 48
       significant bits, well inside double's 53, with no rounding, no overflow
       (max float^2 ~1.2e77 << DBL_MAX) and no underflow — so the product raises
       no exception of its own (no fenv save/restore needed: f64.mul touches no
       software flag) and the compare to (double)x (also exact) is a true
       oracle. This SUPERSEDES the earlier fmaf(y,y,-x) residual probe, which
       UNDERFLOWED to 0 for tiny/subnormal x (the residual fell below the
       smallest float subnormal 2^-149) -> false-negative, e.g. it missed
       sqrtf(0x1p-149). A widened product cannot underflow. */
    if (x < 0)
        feraiseexcept(FE_INVALID);
    else if (isfinite(x) && x > 0 && (double)y * (double)y != (double)x)
        feraiseexcept(FE_INEXACT);
    return y;
}

double sqrt(double x) {
    double y = __builtin_sqrt(x);
    /* firebox #FPH (RC1-sqrt): see sqrtf (incl. the `x < 0` vs isfinite guard
       rationale — special/sqrt.h probes sqrt(-inf) wanting INVALID). Same
       widened-exact-check one tier up: (long double)y*(long double)y is EXACT —
       a double square is 106 bits, inside binary128's 113 (verified
       LDBL_MANT_DIG=113 in this libc), no rounding/overflow/underflow — so the
       quad product is a clean inexactness oracle with no fenv pollution and no
       underflow blind spot. The compiler-rt soft-quad multiply (__multf3) does
       not touch the software fenv word. */
    if (x < 0)
        feraiseexcept(FE_INVALID);
    else if (isfinite(x) && x > 0 && (long double)y * (long double)y != (long double)x)
        feraiseexcept(FE_INEXACT);
    return y;
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
