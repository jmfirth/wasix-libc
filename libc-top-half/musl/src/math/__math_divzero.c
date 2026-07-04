#include <fenv.h>
#include "libm.h"

double __math_divzero(uint32_t sign)
{
	/* firebox #7CD: 1.0/0.0 always signals FE_DIVBYZERO on hardware; raise it
	 * explicitly on wasm. */
	feraiseexcept(FE_DIVBYZERO);
	return fp_barrier(sign ? -1.0 : 1.0) / 0.0;
}
