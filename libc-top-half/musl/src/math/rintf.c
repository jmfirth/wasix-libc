#include <float.h>
#include <math.h>
#include <stdint.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise FE_INEXACT explicitly on wasm */

#if FLT_EVAL_METHOD==0
#define EPS FLT_EPSILON
#elif FLT_EVAL_METHOD==1
#define EPS DBL_EPSILON
#elif FLT_EVAL_METHOD==2
#define EPS LDBL_EPSILON
#endif
static const float_t toint = 1/EPS;

float rintf(float x)
{
	union {float f; uint32_t i;} u = {x};
	int e = u.i>>23 & 0xff;
	int s = u.i>>31;
	float_t y;

	if (e >= 0x7f+23)
		return x;		/* already an integer (or inf/nan): exact */
	if (s)
		y = x - toint + toint;
	else
		y = x + toint - toint;
	if (y == 0)
		y = s ? -0.0f : 0.0f;
	/* firebox #RNS: raise FE_INEXACT iff rounding changed x (see rint.c). */
	if (y != x)
		feraiseexcept(FE_INEXACT);
	return y;
}
