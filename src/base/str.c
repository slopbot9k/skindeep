#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

sk_str sk_str_c(const char *s)
{
    return (sk_str){s, strlen(s)};
}

sk_str sk_str_n(const char *s, size_t n)
{
    return (sk_str){s, n};
}

bool sk_str_eq(sk_str a, sk_str b)
{
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

bool sk_str_eq_c(sk_str a, const char *s)
{
    return sk_str_eq(a, sk_str_c(s));
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

bool sk_str_eq_nocase(sk_str a, sk_str b)
{
    if (a.len != b.len)
        return false;
    for (size_t i = 0; i < a.len; i++)
        if (lower(a.ptr[i]) != lower(b.ptr[i]))
            return false;
    return true;
}

bool sk_str_prefix(sk_str s, sk_str prefix)
{
    return s.len >= prefix.len && sk_str_eq(sk_str_n(s.ptr, prefix.len), prefix);
}

bool sk_str_suffix(sk_str s, sk_str suffix)
{
    return s.len >= suffix.len &&
           sk_str_eq(sk_str_n(s.ptr + s.len - suffix.len, suffix.len), suffix);
}

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

sk_str sk_str_trim(sk_str s)
{
    while (s.len && space(s.ptr[0]))
        s.ptr++, s.len--;
    while (s.len && space(s.ptr[s.len - 1]))
        s.len--;
    return s;
}

sk_str sk_str_sub(sk_str s, size_t i, size_t n)
{
    if (i > s.len)
        i = s.len;
    if (n > s.len - i)
        n = s.len - i;
    return sk_str_n(s.ptr + i, n);
}

size_t sk_str_find(sk_str s, char c)
{
    const char *p = s.len ? memchr(s.ptr, c, s.len) : NULL;
    return p ? (size_t)(p - s.ptr) : SIZE_MAX;
}

bool sk_str_to_u64(sk_str s, uint64_t *out)
{
    if (s.len == 0)
        return false;
    uint64_t v = 0;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (c < '0' || c > '9')
            return false;
        unsigned d = (unsigned)(c - '0');
        if (v > (UINT64_MAX - d) / 10)
            return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

void sk_buf_reserve(sk_buf *b, size_t extra)
{
    if (extra > SIZE_MAX - b->len - 1)
        sk_die("buffer too large");
    size_t need = b->len + extra + 1;
    if (need <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need)
        cap = cap > SIZE_MAX / 2 ? need : cap * 2;
    b->ptr = sk_xrealloc(b->ptr, cap);
    b->cap = cap;
    b->ptr[b->len] = 0; /* a fresh buffer is terminated too */
}

void sk_buf_append(sk_buf *b, const void *p, size_t n)
{
    sk_buf_reserve(b, n);
    if (n)
        memcpy(b->ptr + b->len, p, n);
    b->len += n;
    b->ptr[b->len] = 0;
}

void sk_buf_append_str(sk_buf *b, sk_str s)
{
    sk_buf_append(b, s.ptr, s.len);
}

void sk_buf_append_c(sk_buf *b, const char *s)
{
    sk_buf_append(b, s, strlen(s));
}

void sk_buf_putc(sk_buf *b, char c)
{
    sk_buf_append(b, &c, 1);
}

void sk_buf_vprintf(sk_buf *b, const char *fmt, va_list ap)
{
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0)
        sk_die("bad format: %s", fmt);
    sk_buf_reserve(b, (size_t)n);
    vsnprintf(b->ptr + b->len, (size_t)n + 1, fmt, again);
    va_end(again);
    b->len += (size_t)n;
}

void sk_buf_printf(sk_buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(b, fmt, ap);
    va_end(ap);
}

const char *sk_buf_cstr(const sk_buf *b)
{
    return b->ptr ? b->ptr : "";
}

sk_str sk_buf_str(const sk_buf *b)
{
    return sk_str_n(sk_buf_cstr(b), b->len);
}

void sk_buf_consume(sk_buf *b, size_t n)
{
    if (n >= b->len) {
        sk_buf_clear(b);
        return;
    }
    memmove(b->ptr, b->ptr + n, b->len - n);
    b->len -= n;
    b->ptr[b->len] = 0;
}

void sk_buf_clear(sk_buf *b)
{
    b->len = 0;
    if (b->ptr)
        b->ptr[0] = 0;
}

void sk_buf_free(sk_buf *b)
{
    free(b->ptr);
    b->ptr = NULL;
    b->len = b->cap = 0;
}
