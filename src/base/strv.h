/* A list of C strings it owns, always NULL-terminated, so ptr can be
 * passed as an argv or envp. */
#ifndef SK_STRV_H
#define SK_STRV_H

#include "str.h"

typedef struct {
    char **ptr; /* NULL-terminated once anything was pushed */
    size_t len, cap;
} sk_strv;

void sk_strv_push(sk_strv *v, const char *s);
void sk_strv_push_n(sk_strv *v, const char *s, size_t n);
void sk_strv_pushf(sk_strv *v, const char *fmt, ...) SK_PRINTF(2, 3);
/* Copies a NULL-terminated array; NULL copies nothing. */
void sk_strv_extend(sk_strv *v, char *const *src);
/* The strings as a NULL-terminated array, never NULL itself. */
char *const *sk_strv_argv(sk_strv *v);
void sk_strv_free(sk_strv *v);

#endif
