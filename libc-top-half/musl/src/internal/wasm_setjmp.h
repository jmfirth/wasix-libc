#ifndef WASM_SETJMP_H
#define WASM_SETJMP_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

/* Private EH capture state. Keep it inside the existing 24-byte __jb at
 * both pointer widths; neither the public buffer nor LLVM's helper ABI grows. */
struct __wasm_jmp_buf {
	void *func_invocation_id;
	unsigned int label;
	int handler_depth;
	uintptr_t mask_pending;
};

_Static_assert(sizeof(struct __wasm_jmp_buf) <=
               sizeof(((struct __jmp_buf_tag *)0)->__jb),
               "EH capture state must fit the existing __jb");
_Static_assert(offsetof(struct __jmp_buf_tag, __fl) >= sizeof(struct __wasm_jmp_buf),
               "EH capture state must not overlap the saved-mask flag");

static inline unsigned long __wasm_sigjmp_cookie(void *buf)
{
	return ((uintptr_t)buf ^ (uintptr_t)0x7b91d3a5U) | 1UL;
}

/* One capture consumes this marker. A subsequent plain setjmp on the same
 * buffer clears __fl rather than inheriting an older sigsetjmp's mask. */
static inline uintptr_t __wasm_sigjmp_pending(void *buf)
{
	return ((uintptr_t)buf ^ (uintptr_t)0xc6e85a39U) | 1UL;
}

#endif
