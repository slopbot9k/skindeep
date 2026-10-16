/* The allocators of base.h and sk_die. */
#include "base.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Noreturn void sk_die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("skinned: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    abort();
}

void *sk_xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        sk_die("out of memory (%zu bytes)", n);
    return p;
}

void *sk_xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p)
        sk_die("out of memory (%zu by %zu bytes)", count, size);
    return p;
}

void *sk_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        sk_die("out of memory (%zu bytes)", n);
    return q;
}

void *sk_xreallocarray(void *p, size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        sk_die("allocation overflows (%zu by %zu bytes)", count, size);
    return sk_xrealloc(p, count * size);
}

char *sk_xstrdup(const char *s)
{
    return sk_xstrndup(s, strlen(s));
}

char *sk_xstrndup(const char *s, size_t n)
{
    char *d = sk_xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

void *sk_vec_grow_(void *ptr, size_t *cap, size_t need, size_t elem)
{
    if (need <= *cap)
        return ptr;
    size_t n = *cap ? *cap : 8;
    while (n < need) {
        if (n > SIZE_MAX / 2)
            sk_die("vector too large");
        n *= 2;
    }
    ptr = sk_xreallocarray(ptr, n, elem);
    *cap = n;
    return ptr;
}
