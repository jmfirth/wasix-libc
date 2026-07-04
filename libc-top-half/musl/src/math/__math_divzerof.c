#include <fenv.h>
#include "libm.h"

float __math_divzerof(uint32_t sign)
{
	/* firebox #7CD: raise FE_DIVBYZERO explicitly (see __math_divzero.c). */
	feraiseexcept(FE_DIVBYZERO);
	return fp_barrierf(sign ? -1.0f : 1.0f) / 0.0f;
}
