#include "strv.h"

#include <stdarg.h>
#include <stdlib.h>

static void push_owned(sk_strv *v, char *s)
{
    v->ptr = sk_vec_grow_(v->ptr, &v->cap, v->len + 2, sizeof *v->ptr);
    v->ptr[v->len++] = s;
    v->ptr[v->len] = NULL;
}

void sk_strv_push(sk_strv *v, const char *s)
{
    push_owned(v, sk_xstrdup(s));
}

void sk_strv_push_n(sk_strv *v, const char *s, size_t n)
{
    push_owned(v, sk_xstrndup(s, n));
}

void sk_strv_pushf(sk_strv *v, const char *fmt, ...)
{
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    push_owned(v, b.ptr ? b.ptr : sk_xstrdup(""));
}

void sk_strv_extend(sk_strv *v, char *const *src)
{
    for (; src && *src; src++)
        sk_strv_push(v, *src);
}

char *const *sk_strv_argv(sk_strv *v)
{
    if (!v->ptr) {
        v->ptr = sk_vec_grow_(v->ptr, &v->cap, 1, sizeof *v->ptr);
        v->ptr[0] = NULL;
    }
    return v->ptr;
}

void sk_strv_free(sk_strv *v)
{
    for (size_t i = 0; i < v->len; i++)
        free(v->ptr[i]);
    free(v->ptr);
    v->ptr = NULL;
    v->len = v->cap = 0;
}
