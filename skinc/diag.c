#include "diag.h"

#include <stdarg.h>
#include <stdio.h>

static void add(sk_diags *d, const char *file, const char *where, const char *fmt, va_list ap) SK_PRINTF(4, 0);

static void add(sk_diags *d, const char *file, const char *where, const char *fmt, va_list ap)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "%s: %s: ", file, where);
    sk_buf_vprintf(&b, fmt, ap);
    sk_vec_push(&d->items, b.ptr);
}

void sk_diag(sk_diags *d, const char *file, const char *where, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    add(d, file, where, fmt, ap);
    va_end(ap);
}

void sk_diag_line(sk_diags *d, const char *file, uint32_t line, const char *fmt, ...)
{
    char where[32];
    snprintf(where, sizeof where, "line %u", (unsigned)line);
    va_list ap;
    va_start(ap, fmt);
    add(d, file, where, fmt, ap);
    va_end(ap);
}

void sk_diags_free(sk_diags *d)
{
    for (size_t i = 0; i < d->items.len; i++)
        free(d->items.ptr[i]);
    sk_vec_free(&d->items);
}
