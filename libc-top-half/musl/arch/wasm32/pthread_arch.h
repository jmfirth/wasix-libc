/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
extern __FBX_THREAD_LOCAL struct __pthread *__wasilibc_pthread_self;

static inline uintptr_t __get_tp() {
  return (uintptr_t)__wasilibc_pthread_self;
}

static inline void __set_tp(uintptr_t p) {
  __wasilibc_pthread_self = (struct __pthread *)p;
}