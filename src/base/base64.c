#include "base64.h"

static const char URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void sk_base64url_encode(sk_buf *out, const void *data, size_t n)
{
    const unsigned char *p = data;
    size_t i = 0;
    sk_buf_reserve(out, (n + 2) / 3 * 4);
    for (; i + 3 <= n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
        char q[4] = {URL[v >> 18], URL[(v >> 12) & 63], URL[(v >> 6) & 63], URL[v & 63]};
        sk_buf_append(out, q, 4);
    }
    if (n - i == 1) {
        uint32_t v = (uint32_t)p[i] << 16;
        char q[2] = {URL[v >> 18], URL[(v >> 12) & 63]};
        sk_buf_append(out, q, 2);
    } else if (n - i == 2) {
        uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8;
        char q[3] = {URL[v >> 18], URL[(v >> 12) & 63], URL[(v >> 6) & 63]};
        sk_buf_append(out, q, 3);
    }
}

static int value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-' || c == '+')
        return 62;
    if (c == '_' || c == '/')
        return 63;
    return -1;
}

bool sk_base64url_decode(sk_buf *out, sk_str text)
{
    size_t n = text.len;
    while (n && text.ptr[n - 1] == '=')
        n--;
    if (text.len - n > 2 || n % 4 == 1)
        return false;
    if (text.len != n && text.len % 4 != 0)
        return false;
    size_t start = out->len;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = value(text.ptr[i]);
        if (v < 0) {
            out->len = start;
            if (out->ptr)
                out->ptr[start] = 0;
            return false;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            sk_buf_putc(out, (char)(acc >> bits & 0xFF));
        }
    }
    return true;
}
