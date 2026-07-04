#include <math.h>
#include <stdint.h>
#include <fenv.h>	/* firebox #C36: software fenv — raise range-error flags explicitly on wasm */

float hypotf(float x, float y)
{
	union {float f; uint32_t i;} ux = {x}, uy = {y}, ut;
	float_t z;

	ux.i &= -1U>>1;
	uy.i &= -1U>>1;
	if (ux.i < uy.i) {
		ut = ux;
		ux = uy;
		uy = ut;
	}

	x = ux.f;
	y = uy.f;
	if (uy.i == 0xff<<23)
		return y;
	if (ux.i >= 0xff<<23 || uy.i == 0 || ux.i - uy.i >= 25<<23)
		return x + y;

	z = 1;
	if (ux.i >= (0x7f+60)<<23) {
		z = 0x1p90f;
		x *= 0x1p-90f;
		y *= 0x1p-90f;
	} else if (uy.i < (0x7f-60)<<23) {
		z = 0x1p-90f;
		x *= 0x1p90f;
		y *= 0x1p90f;
	}
	/* firebox #C36: explicit range-error flags (see hypot.c). Operands here are
	   finite and non-zero, so inf → overflow, sub-normal → inexact underflow. */
	float r = z*sqrtf((double)x*x + (double)y*y);
	if (isinf(r))
		feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (r != 0 && fabsf(r) < 0x1p-126f)
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
	return r;
}
