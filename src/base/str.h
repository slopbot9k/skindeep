/* Byte strings: sk_str, a view that owns nothing, and sk_buf, a growable
 * buffer that is always NUL-terminated. Neither assumes valid UTF-8. */
#ifndef SK_STR_H
#define SK_STR_H

#include "base.h"

#include <stdarg.h>

typedef struct {
    const char *ptr;
    size_t len;
} sk_str;

#define SK_STR(lit) ((sk_str){"" lit, sizeof(lit) - 1})

sk_str sk_str_c(const char *s);
sk_str sk_str_n(const char *s, size_t n);
bool sk_str_eq(sk_str a, sk_str b);
bool sk_str_eq_c(sk_str a, const char *s);
/* ASCII letters only, as HTTP header names compare. */
bool sk_str_eq_nocase(sk_str a, sk_str b);
bool sk_str_prefix(sk_str s, sk_str prefix);
bool sk_str_suffix(sk_str s, sk_str suffix);
/* Without leading and trailing spaces, tabs, CR and LF. */
sk_str sk_str_trim(sk_str s);
/* The part from i, at most n bytes, clamped to s. */
sk_str sk_str_sub(sk_str s, size_t i, size_t n);
/* Index of the first byte c, or SIZE_MAX. */
size_t sk_str_find(sk_str s, char c);
/* Decimal digits only, no sign, no spaces; false on overflow or junk. */
bool sk_str_to_u64(sk_str s, uint64_t *out);

typedef struct {
    char *ptr; /* NUL-terminated whenever it is not NULL */
    size_t len, cap;
} sk_buf;

void sk_buf_reserve(sk_buf *b, size_t extra);
void sk_buf_append(sk_buf *b, const void *p, size_t n);
void sk_buf_append_str(sk_buf *b, sk_str s);
void sk_buf_append_c(sk_buf *b, const char *s);
void sk_buf_putc(sk_buf *b, char c);
void sk_buf_printf(sk_buf *b, const char *fmt, ...) SK_PRINTF(2, 3);
void sk_buf_vprintf(sk_buf *b, const char *fmt, va_list ap) SK_PRINTF(2, 0);
/* The contents as a C string: "" for an empty buffer, never NULL. */
const char *sk_buf_cstr(const sk_buf *b);
sk_str sk_buf_str(const sk_buf *b);
/* Drops the first n bytes. */
void sk_buf_consume(sk_buf *b, size_t n);
void sk_buf_clear(sk_buf *b);
void sk_buf_free(sk_buf *b);

#endif
