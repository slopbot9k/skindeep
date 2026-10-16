/* What every file of the C tree shares (decision 0036): sizes, the
 * allocators that never return NULL, and the one way to die. */
#ifndef SK_BASE_H
#define SK_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define SK_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define SK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define SK_MAX(a, b) ((a) > (b) ? (a) : (b))

#if defined(__GNUC__) || defined(__clang__)
#define SK_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define SK_PRINTF(f, a)
#endif

/* Prints "skinned: <message>" to stderr and aborts. For a broken
 * invariant or memory exhausted, never for bad input. */
_Noreturn void sk_die(const char *fmt, ...) SK_PRINTF(1, 2);

/* malloc and friends that die instead of returning NULL. */
void *sk_xmalloc(size_t n);
void *sk_xcalloc(size_t count, size_t size);
void *sk_xrealloc(void *p, size_t n);
/* realloc of count elements of size bytes, checking the product. */
void *sk_xreallocarray(void *p, size_t count, size_t size);
char *sk_xstrdup(const char *s);
char *sk_xstrndup(const char *s, size_t n);

/* Growable arrays: SK_VEC(int) v = {0}; sk_vec_push(&v, 3); */
#define SK_VEC(T) struct { T *ptr; size_t len, cap; }
#define sk_vec_push(v, x)                                                    \
    ((v)->ptr = sk_vec_grow_((v)->ptr, &(v)->cap, (v)->len + 1,              \
                             sizeof *(v)->ptr),                              \
     (v)->ptr[(v)->len++] = (x))
#define sk_vec_free(v) (free((v)->ptr), (v)->ptr = NULL, (v)->len = (v)->cap = 0)
void *sk_vec_grow_(void *ptr, size_t *cap, size_t need, size_t elem);

#endif
