#include <fenv.h>

/* Software floating-point environment for WebAssembly (firebox #7CD).
 *
 * Wasm has no hardware floating-point exception status register, so the
 * classic musl fenv relied on the arithmetic itself setting HW flags — on
 * wasm those flags don't exist, and upstream wasi-libc therefore shipped a
 * no-op stub (feraiseexcept/fetestexcept always returned 0). That made every
 * math domain/range-error conformance test (libc-test math tests) fail: the test
 * clears exceptions, calls e.g. log(0), then asserts FE_DIVBYZERO is set.
 *
 * We implement a real *software* exception-status word instead. musl's math
 * routines flag a domain/range error by calling __math_invalid/__math_divzero/
 * __math_oflow/__math_uflow (and a few explicit raise sites); those helpers are
 * patched to call feraiseexcept() explicitly on wasm, and this word is what
 * fetestexcept() reads back. It is pure software libc — no host dependency — so
 * it works identically on BOTH the native and browser profiles (tier-1, not
 * native-only).
 *
 * The word is per-thread (POSIX requires the fp environment to be thread-local);
 * the -threads variants build with TLS so _Thread_local is available.
 *
 * SCOPE / honest residual: this closes the *explicit-raise* cases (INVALID,
 * DIVBYZERO, OVERFLOW, UNDERFLOW from domain/range errors). It does NOT track
 * the *automatic* FE_INEXACT flag that hardware sets on nearly every rounded
 * operation — that has no wasm equivalent and would require soft-float. Rounding
 * modes other than FE_TONEAREST likewise don't exist on wasm (the generic
 * bits/fenv.h defines only FE_TONEAREST), so fegetround() is constant. */

static _Thread_local int __fe_except;

int feclearexcept(int mask)
{
	__fe_except &= ~(mask & FE_ALL_EXCEPT);
	return 0;
}

int feraiseexcept(int mask)
{
	__fe_except |= (mask & FE_ALL_EXCEPT);
	return 0;
}

int fetestexcept(int mask)
{
	return __fe_except & mask & FE_ALL_EXCEPT;
}

int fegetround(void)
{
	return FE_TONEAREST;
}

int __fesetround(int r)
{
	/* FE_TONEAREST is the only rounding mode wasm supports; the arch-independent
	 * fesetround() wrapper has already rejected any other value. */
	return r == FE_TONEAREST ? 0 : -1;
}

int fegetenv(fenv_t *envp)
{
	/* The environment is exactly the exception-status word (rounding is constant). */
	envp->__cw = (unsigned long)__fe_except;
	return 0;
}

int fesetenv(const fenv_t *envp)
{
	/* FE_DFL_ENV ((const fenv_t *)-1) is the default environment: no exceptions set. */
	__fe_except = (envp == FE_DFL_ENV) ? 0 : (int)envp->__cw;
	return 0;
}
