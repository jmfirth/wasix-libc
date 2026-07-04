#ifndef _LOCALE_IMPL_H
#define _LOCALE_IMPL_H

#include <locale.h>
#include <stdlib.h>
#include "libc.h"
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
#include "pthread_impl.h"
#endif

#define LOCALE_NAME_MAX 23

struct __locale_map {
	const void *map;
	size_t map_size;
	char name[LOCALE_NAME_MAX+1];
	const struct __locale_map *next;
};

#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
extern hidden volatile int __locale_lock[1];
#endif

extern hidden const struct __locale_map __c_dot_utf8;
extern hidden const struct __locale_struct __c_locale;
extern hidden const struct __locale_struct __c_dot_utf8_locale;

hidden const struct __locale_map *__get_locale(int, const char *);
hidden const char *__mo_lookup(const void *, size_t, const char *);
hidden const char *__lctrans(const char *, const struct __locale_map *);
hidden const char *__lctrans_cur(const char *);
hidden const char *__lctrans_impl(const char *, const struct __locale_map *);
hidden int __loc_is_allocated(locale_t);
hidden char *__gettextdomain(void);

#define LOC_MAP_FAILED ((const struct __locale_map *)-1)

#define LCTRANS(msg, lc, loc) __lctrans(msg, (loc)->cat[(lc)])
#define LCTRANS_CUR(msg) __lctrans_cur(msg)

#define C_LOCALE ((locale_t)&__c_locale)
#define UTF8_LOCALE ((locale_t)&__c_dot_utf8_locale)

#if defined(__wasilibc_unmodified_upstream) && defined(_REENTRANT)
#define CURRENT_LOCALE (__pthread_self()->locale)

#define CURRENT_UTF8 (!!__pthread_self()->locale->cat[LC_CTYPE])
#else
// If we haven't set up the current_local field yet, do so. Then return an
// lvalue for the current_locale field.
#define CURRENT_LOCALE \
    (*({ \
        if (!libc.current_locale) { \
            libc.current_locale = &libc.global_locale; \
        } \
        &libc.current_locale; \
    }))

// firebox#QWE: read the LC_CTYPE category through the settable current-locale
// pointer, not the global struct. In this fork's live (#else) branch
// CURRENT_LOCALE is the pointer libc.current_locale (uselocale()/iconv's
// `*ploc = UTF8_LOCALE` move it); the previous `libc.global_locale.cat[...]`
// read ignored that pointer, so MB_CUR_MAX stayed 1 (byte mode) for every
// uselocale()'d/iconv UTF-8 ctype — corrupting iconv's UTF-8 intermediate
// codec (iso-8859 roundtrip). Upstream's CURRENT_UTF8 (line 47) reads through
// the SAME indirection as CURRENT_LOCALE; this restores that invariant.
// setlocale() still works: current_locale defaults to &global_locale, so a
// setlocale(LC_CTYPE) that mutates global_locale.cat is still seen here.
#define CURRENT_UTF8 (!!CURRENT_LOCALE->cat[LC_CTYPE])
#endif

#undef MB_CUR_MAX
#define MB_CUR_MAX (CURRENT_UTF8 ? 4 : 1)

#endif
