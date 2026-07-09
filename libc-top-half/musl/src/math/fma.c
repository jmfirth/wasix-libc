#include <stdint.h>
#include <float.h>
#include <math.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise FE_INVALID for invalid fma ops on wasm */
#include "atomic.h"

#define ASUINT64(x) ((union {double f; uint64_t i;}){x}).i
#define ZEROINFNAN (0x7ff-0x3ff-52-1)

struct num { uint64_t m; int e; int sign; };

static struct num normalize(double x)
{
	uint64_t ix = ASUINT64(x);
	int e = ix>>52;
	int sign = e & 0x800;
	e &= 0x7ff;
	if (!e) {
		ix = ASUINT64(x*0x1p63);
		e = ix>>52 & 0x7ff;
		e = e ? e-63 : 0x800;
	}
	ix &= (1ull<<52)-1;
	ix |= 1ull<<52;
	ix <<= 1;
	e -= 0x3ff + 52 + 1;
	return (struct num){ix,e,sign};
}

static void mul(uint64_t *hi, uint64_t *lo, uint64_t x, uint64_t y)
{
	uint64_t t1,t2,t3;
	uint64_t xlo = (uint32_t)x, xhi = x>>32;
	uint64_t ylo = (uint32_t)y, yhi = y>>32;

	t1 = xlo*ylo;
	t2 = xlo*yhi + xhi*ylo;
	t3 = xhi*yhi;
	*lo = t1 + (t2<<32);
	*hi = t3 + (t2>>32) + (t1 > *lo);
}

double fma(double x, double y, double z)
{
	#pragma STDC FENV_ACCESS ON

	/* firebox #RNS: the invalid fma operations produce a quiet nan through
	   wasm's silent f64 mul/add, which never sets a status flag. Raise
	   FE_INVALID explicitly for 0*inf (either order) and (+-inf)+(-+inf). A
	   quiet-NaN operand alone does NOT raise INVALID. The inf-inf test uses a
	   TRUE infinity (an inf operand), not isinf(x*y): a large finite product
	   like 1e300*1e300 overflows the double x*y to inf but is finite inside fma,
	   so 1e300*1e300 + (-inf) = -inf is NOT invalid. */
	if ((x == 0 && isinf(y)) || (isinf(x) && y == 0)) {
		feraiseexcept(FE_INVALID);
	} else if (isinf(z) && (isinf(x) || isinf(y)) && !isnan(x) && !isnan(y)
	           && x != 0 && y != 0
	           && (signbit(x) ^ signbit(y)) != signbit(z)) {
		feraiseexcept(FE_INVALID);
	}

	/* normalize so top 10bits and last bit are 0 */
	struct num nx, ny, nz;
	nx = normalize(x);
	ny = normalize(y);
	nz = normalize(z);

	if (nx.e >= ZEROINFNAN || ny.e >= ZEROINFNAN)
		return x*y + z;
	if (nz.e >= ZEROINFNAN) {
		if (nz.e > ZEROINFNAN) /* z==0 */
			return x*y + z;
		return z;
	}

	/* mul: r = x*y */
	uint64_t rhi, rlo, zhi, zlo;
	mul(&rhi, &rlo, nx.m, ny.m);
	/* either top 20 or 21 bits of rhi and last 2 bits of rlo are 0 */

	/* align exponents */
	int e = nx.e + ny.e;
	int d = nz.e - e;
	/* shift bits z<<=kz, r>>=kr, so kz+kr == d, set e = e+kr (== ez-kz) */
	if (d > 0) {
		if (d < 64) {
			zlo = nz.m<<d;
			zhi = nz.m>>64-d;
		} else {
			zlo = 0;
			zhi = nz.m;
			e = nz.e - 64;
			d -= 64;
			if (d == 0) {
			} else if (d < 64) {
				rlo = rhi<<64-d | rlo>>d | !!(rlo<<64-d);
				rhi = rhi>>d;
			} else {
				rlo = 1;
				rhi = 0;
			}
		}
	} else {
		zhi = 0;
		d = -d;
		if (d == 0) {
			zlo = nz.m;
		} else if (d < 64) {
			zlo = nz.m>>d | !!(nz.m<<64-d);
		} else {
			zlo = 1;
		}
	}

	/* add */
	int sign = nx.sign^ny.sign;
	int samesign = !(sign^nz.sign);
	int nonzero = 1;
	if (samesign) {
		/* r += z */
		rlo += zlo;
		rhi += zhi + (rlo < zlo);
	} else {
		/* r -= z */
		uint64_t t = rlo;
		rlo -= zlo;
		rhi = rhi - zhi - (t < rlo);
		if (rhi>>63) {
			rlo = -rlo;
			rhi = -rhi-!!rlo;
			sign = !sign;
		}
		nonzero = !!rhi;
	}

	/* set rhi to top 63bit of the result (last bit is sticky) */
	if (nonzero) {
		e += 64;
		d = a_clz_64(rhi)-1;
		/* note: d > 0 */
		rhi = rhi<<d | rlo>>64-d | !!(rlo<<d);
	} else if (rlo) {
		d = a_clz_64(rlo)-1;
		if (d < 0)
			rhi = rlo>>1 | (rlo&1);
		else
			rhi = rlo<<d;
	} else {
		/* exact +-0 */
		return x*y + z;
	}
	e -= d;

	/* convert to double */
	int64_t i = rhi; /* i is in [1<<62,(1<<63)-1] */
	if (sign)
		i = -i;
	double r = i; /* |r| is in [0x1p62,0x1p63] */

	if (e < -1022-62) {
		/* result is subnormal before rounding */
		if (e == -1022-63) {
			double c = 0x1p63;
			if (sign)
				c = -c;
			if (r == c) {
				/* min normal after rounding, underflow depends
				   on arch behaviour which can be imitated by
				   a double to float conversion */
				float fltmin = 0x0.ffffff8p-63*FLT_MIN * r;
				return DBL_MIN/FLT_MIN * fltmin;
			}
			/* one bit is lost when scaled, add another top bit to
			   only round once at conversion if it is inexact */
			if (rhi << 53) {
				i = rhi>>1 | (rhi&1) | 1ull<<62;
				if (sign)
					i = -i;
				r = i;
				r = 2*r - c; /* remove top bit */

				/* firebox #FPH (RC3): this subnormal result lost bits (rhi<<53
				   != 0) -> inexact. musl's "raise underflow portably" trick
				   (tiny*tiny)*(r-r) relies on tiny*tiny underflow-trapping on a
				   real FPU; it is INERT on wasm's silent f64 mul. Raise
				   UNDERFLOW|INEXACT explicitly. */
				feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
			}
		} else {
			/* only round once when scaled */
			d = 10;
			/* firebox #FPH (RC3): deeper subnormal; the sticky bit
			   rhi<<(64-d) != 0 means bits were rounded away -> inexact
			   subnormal -> UNDERFLOW|INEXACT (wasm silent ops raise nothing). */
			if (rhi << 64-d)
				feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
			i = ( rhi>>d | !!(rhi<<64-d) ) << d;
			if (sign)
				i = -i;
			r = i;
		}
	}
	return scalbn(r, e);
}
