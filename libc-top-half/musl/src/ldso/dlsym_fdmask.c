/* firebox#D9H -- dlsym for code compiled against the bitmask <sys/select.h>.
 *
 * <dlfcn.h> redirects `dlsym` to this function. The host resolves names
 * against the exported link names, where `select` and `pselect` are the LEGACY
 * {count, list} decoders (select_legacy.c); the bitmask decoders live under
 * __select_fdmask and __pselect_fdmask (firebox#D7S). So a caller that was
 * compiled against the new headers and asks for "select" must be handed
 * __select_fdmask, or it would call a list decoder with a bitmask.
 *
 * Only those two names are mapped; every other name goes through unchanged.
 * A caller that asks for "__select_fdmask" by its link name is unaffected.
 *
 * This file must not include <dlfcn.h>: its redirect would rename the
 * declaration of `dlsym` below. It is its own translation unit so that a
 * program defining its own `dlsym` does not also pull in the real one.
 */
#include <string.h>

void *dlsym(void *restrict p, const char *restrict s);

void *__dlsym_fdmask(void *restrict p, const char *restrict s)
{
	if (s) {
		if (!strcmp(s, "select"))
			s = "__select_fdmask";
		else if (!strcmp(s, "pselect"))
			s = "__pselect_fdmask";
	}
	return dlsym(p, s);
}
