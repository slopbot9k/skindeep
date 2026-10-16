/* What the clipboard's transfers share: INCR's chunks and STRING's
 * Latin-1. platform/x11/xui.c holds the transfers themselves. */
#include "x11.h"

#include "../../base/utf8.h"

/* ChangeProperty's own bytes before its data. */
#define PROPERTY_HEADER 24

size_t sk_x11_incr_threshold(const sk_x11 *c)
{
    size_t room = c->max_request > PROPERTY_HEADER ? c->max_request - PROPERTY_HEADER : 0;
    return SK_MAX(SK_MIN(room, (size_t)1 << 18), 4);
}

sk_str sk_x11_incr_next(sk_x11_incr *t, size_t max)
{
    size_t n = SK_MIN(t->len - t->off, max);
    sk_str s = sk_str_n(t->data + t->off, n);
    t->off += n;
    if (n == 0)
        t->done = true;
    return s;
}

void sk_x11_latin1_from_utf8(sk_buf *out, const char *s, size_t n)
{
    sk_buf_reserve(out, n);
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, NULL);
        sk_buf_putc(out, cp < 0x100 ? (char)cp : '?');
    }
}

void sk_x11_utf8_from_latin1(sk_buf *out, const uint8_t *s, size_t n)
{
    sk_buf_reserve(out, n);
    for (size_t i = 0; i < n; i++)
        sk_utf8_append(out, s[i]);
}
