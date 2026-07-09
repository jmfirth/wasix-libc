#include <float.h>
#include <math.h>
#include <stdint.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise FE_INEXACT explicitly on wasm */

#if FLT_EVAL_METHOD==0 || FLT_EVAL_METHOD==1
#define EPS DBL_EPSILON
#elif FLT_EVAL_METHOD==2
#define EPS LDBL_EPSILON
#endif
static const double_t toint = 1/EPS;

double rint(double x)
{
	union {double f; uint64_t i;} u = {x};
	int e = u.i>>52 & 0x7ff;
	int s = u.i>>63;
	double_t y;

	if (e >= 0x3ff+52)
		return x;		/* already an integer (or inf/nan): exact */
	if (s)
		y = x - toint + toint;
	else
		y = x + toint - toint;
	if (y == 0)
		y = s ? -0.0 : 0;
	/* firebox #RNS: rint raises FE_INEXACT iff it changed x (x was not an
	   integer); wasm's add/sub don't set the HW flag. inf/nan/large-integer
	   returned above, so here x is finite and y!=x is a genuine inexact round
	   (no spurious flag on exact integers, and NaN can't reach here). */
	if (y != x)
		feraiseexcept(FE_INEXACT);
	return y;
}
