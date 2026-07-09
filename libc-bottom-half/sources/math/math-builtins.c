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
    /* firebox #FPH (RC1-sqrt): sqrt must raise FE_INEXACT iff the exact square
       root is not representable, i.e. the residual fmaf(y,y,-x) != 0. The wasm
       f32.sqrt builtin sets no software-fenv flag, and src/math/sqrtf.c is
       filter-out'd dead code (see the class lesson
       math-fix-dead-when-filtered-out-verify-llvm-nm). Guard isfinite(x)&&x>0:
       x<=0 (incl -0), +inf and nan give an exact/NaN result with no INEXACT,
       and INVALID for x<0 is a separate concern the #FPH INEXACT row does not
       cover. The soft-float fmaf leaks its OWN INEXACT, so save the exception
       flags before the probe and restore them after (fesetexceptflag is a true
       restore: feclearexcept(~saved)+feraiseexcept(saved)), then raise INEXACT
       only on a nonzero residual. fmaf(y,y,-x)==0 iff y*y==x exactly iff
       sqrt(x) was representable. */
    if (isfinite(x) && x > 0) {
        fexcept_t saved;
        fegetexceptflag(&saved, FE_ALL_EXCEPT);
        float r = fmaf(y, y, -x);
        fesetexceptflag(&saved, FE_ALL_EXCEPT);
        if (r != 0)
            feraiseexcept(FE_INEXACT);
    }
    return y;
}

double sqrt(double x) {
    double y = __builtin_sqrt(x);
    /* firebox #FPH (RC1-sqrt): see sqrtf — raise FE_INEXACT iff the exact sqrt
       is not representable (residual fma(y,y,-x) != 0), with the soft-float
       fma's own flags saved/restored around the probe. */
    if (isfinite(x) && x > 0) {
        fexcept_t saved;
        fegetexceptflag(&saved, FE_ALL_EXCEPT);
        double r = fma(y, y, -x);
        fesetexceptflag(&saved, FE_ALL_EXCEPT);
        if (r != 0)
            feraiseexcept(FE_INEXACT);
    }
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
