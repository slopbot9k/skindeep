#include "utf8.h"

/* The lead byte's sequence length and the bounds of its first
 * continuation byte, per the WHATWG decoder; 0 for an invalid lead. */
static unsigned lead(unsigned char c, unsigned char *lo, unsigned char *hi, uint32_t *cp)
{
    *lo = 0x80, *hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        *cp = c & 0x1Fu;
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF) {
        if (c == 0xE0)
            *lo = 0xA0;
        if (c == 0xED)
            *hi = 0x9F;
        *cp = c & 0x0Fu;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (c == 0xF0)
            *lo = 0x90;
        if (c == 0xF4)
            *hi = 0x8F;
        *cp = c & 0x07u;
        return 4;
    }
    return 0;
}

size_t sk_utf8_decode(const char *s, size_t n, uint32_t *cp, bool *bad)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    unsigned char lo, hi;
    uint32_t v = 0;
    unsigned len = lead(u[0], &lo, &hi, &v);
    if (len == 0)
        goto invalid;
    for (unsigned i = 1; i < len; i++) {
        if (i >= n || u[i] < lo || u[i] > hi) {
            *cp = SK_UTF8_REPLACEMENT;
            if (bad)
                *bad = true;
            return i;
        }
        v = (v << 6) | (u[i] & 0x3Fu);
        lo = 0x80, hi = 0xBF;
    }
    *cp = v;
    return len;
invalid:
    *cp = SK_UTF8_REPLACEMENT;
    if (bad)
        *bad = true;
    return 1;
}

size_t sk_utf8_encode(uint32_t cp, char out[4])
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = SK_UTF8_REPLACEMENT;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

void sk_utf8_append(sk_buf *b, uint32_t cp)
{
    char tmp[4];
    sk_buf_append(b, tmp, sk_utf8_encode(cp, tmp));
}

bool sk_utf8_valid(const char *s, size_t n)
{
    bool bad = false;
    for (size_t i = 0; i < n && !bad;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, &bad);
    }
    return !bad;
}

size_t sk_utf8_whole(const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    /* A character is at most 4 bytes, so only the last 3 can begin one
     * still waiting for bytes. */
    size_t from = n > 3 ? n - 3 : 0;
    for (size_t i = n; i > from; i--) {
        unsigned char c = u[i - 1];
        if (c < 0x80)
            return n;
        if (c >= 0xC0) {
            unsigned char lo, hi;
            uint32_t v;
            unsigned len = lead(c, &lo, &hi, &v);
            if (len == 0 || n - (i - 1) >= len)
                return n;
            /* A prefix that more bytes could still complete is held back;
             * one that is already invalid is decoded now. */
            for (size_t k = i; k < n; k++) {
                if (u[k] < lo || u[k] > hi)
                    return n;
                lo = 0x80, hi = 0xBF;
            }
            return i - 1;
        }
    }
    return n;
}

void sk_utf8_sanitize(sk_buf *b, const char *s, size_t n)
{
    size_t i = 0;
    sk_buf_reserve(b, n);
    while (i < n) {
        size_t run = i;
        while (run < n && (unsigned char)s[run] < 0x80)
            run++;
        if (run > i) {
            sk_buf_append(b, s + i, run - i);
            i = run;
            continue;
        }
        uint32_t cp;
        bool bad = false;
        size_t len = sk_utf8_decode(s + i, n - i, &cp, &bad);
        if (bad)
            sk_utf8_append(b, SK_UTF8_REPLACEMENT);
        else
            sk_buf_append(b, s + i, len);
        i += len;
    }
}
