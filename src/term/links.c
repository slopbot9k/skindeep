/* The regex never backtracks past its scheme: at a start it takes the
 * longest run of path characters, then the last character of the run,
 * or the one just after it, that may end a URL.
 *
 * isUrl builds `${protocol}//${user}:${password}@${host}` from what
 * new URL() makes of the match and keeps the match only when it begins
 * with that, letter case aside. So a link survives only when its host,
 * port and credentials are already in the form the URL Standard writes
 * them. url_ok follows the Standard's parser for http and https as far
 * as that decides: the authority, the host as an IPv6 address, an IPv4
 * address or a domain, the port. A domain the Standard would rewrite
 * into Punycode, or decode from percent escapes, can never match, so
 * those fail at once. Ported from @xterm/addon-web-links 0.12.0, (c)
 * 2019 The xterm.js authors, MIT licence. */
#include "links.h"

#include "../base/str.h"
#include "../base/utf8.h"

#include <limits.h>
#include <string.h>

/* ---- The regex ---- */

/* JavaScript's \s: WhiteSpace and LineTerminator. */
static bool js_space(uint32_t c)
{
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

/* [^\s"'!*(){}|\\\^<>`] */
static bool path_char(uint32_t c)
{
    return !js_space(c) && !(c < 0x80 && strchr("\"'!*(){}|\\^<>`", (int)c) && c);
}

/* [^\s"':,.!?{}|\\\^~\[\]`()<>] */
static bool last_char(uint32_t c)
{
    return !js_space(c) && !(c < 0x80 && strchr("\"':,.!?{}|\\^~[]`()<>", (int)c) && c);
}

static bool word_at(const uint32_t *s, size_t n, size_t p, const char *w)
{
    for (; *w; w++, p++)
        if (p >= n || s[p] != (uint32_t)*w)
            return false;
    return true;
}

/* The match's length at p, 0 for none. */
static size_t match_at(const uint32_t *s, size_t n, size_t p)
{
    bool upper = word_at(s, n, p, "HTTP");
    if (!upper && !word_at(s, n, p, "http"))
        return 0;
    size_t i = p + 4;
    if (i < n && s[i] == (upper ? 'S' : 's'))
        i++;
    if (!word_at(s, n, i, "://"))
        return 0;
    i += 3;
    size_t run = 0;
    while (i + run < n && path_char(s[i + run]))
        run++;
    for (size_t k = run + 1; k-- > 0;)
        if (i + k < n && last_char(s[i + k]))
            return i + k + 1 - p;
    return 0;
}

/* ---- The URL check ---- */

static bool ascii_digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

static int hex_value(uint32_t c)
{
    if (ascii_digit(c))
        return (int)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (int)(c - 'A' + 10);
    return -1;
}

/* The Standard's IPv6 parser. */
static bool ipv6_parse(const uint32_t *s, size_t n, uint16_t a[8])
{
    memset(a, 0, 8 * sizeof a[0]);
    int piece = 0, compress = -1;
    size_t p = 0;
    if (p < n && s[p] == ':') {
        if (p + 1 >= n || s[p + 1] != ':')
            return false;
        p += 2;
        compress = ++piece;
    }
    while (p < n) {
        if (piece == 8)
            return false;
        if (s[p] == ':') {
            if (compress >= 0)
                return false;
            p++;
            compress = ++piece;
            continue;
        }
        unsigned value = 0, length = 0;
        while (length < 4 && p < n && hex_value(s[p]) >= 0) {
            value = value * 16 + (unsigned)hex_value(s[p]);
            p++;
            length++;
        }
        if (p < n && s[p] == '.') {
            /* An IPv4 address in the last two pieces. */
            if (length == 0 || piece > 6)
                return false;
            p -= length;
            int seen = 0;
            while (p < n) {
                int v4 = -1;
                if (seen > 0) {
                    if (s[p] != '.' || seen >= 4)
                        return false;
                    p++;
                }
                if (p >= n || !ascii_digit(s[p]))
                    return false;
                while (p < n && ascii_digit(s[p])) {
                    int d = (int)(s[p] - '0');
                    if (v4 < 0)
                        v4 = d;
                    else if (v4 == 0)
                        return false;
                    else
                        v4 = v4 * 10 + d;
                    if (v4 > 255)
                        return false;
                    p++;
                }
                a[piece] = (uint16_t)(a[piece] * 0x100 + v4);
                if (++seen == 2 || seen == 4)
                    piece++;
            }
            if (seen != 4)
                return false;
            break;
        }
        if (p < n && s[p] == ':') {
            if (++p >= n)
                return false;
        } else if (p < n) {
            return false;
        }
        a[piece++] = (uint16_t)value;
    }
    if (compress >= 0) {
        int swaps = piece - compress;
        for (piece = 7; piece != 0 && swaps > 0; piece--, swaps--) {
            uint16_t t = a[piece];
            a[piece] = a[compress + swaps - 1];
            a[compress + swaps - 1] = t;
        }
    } else if (piece != 8) {
        return false;
    }
    return true;
}

/* The Standard's IPv6 serializer: the first longest run of two or more
 * zero pieces as ::, hex in lower case. */
static void ipv6_write(const uint16_t a[8], sk_buf *out)
{
    int best = -1, best_len = 1;
    for (int i = 0; i < 8;) {
        int j = i;
        while (j < 8 && a[j] == 0)
            j++;
        if (j - i > best_len)
            best = i, best_len = j - i;
        i = j > i ? j : i + 1;
    }
    bool ignore0 = false;
    for (int i = 0; i < 8; i++) {
        if (ignore0 && a[i] == 0)
            continue;
        ignore0 = false;
        if (i == best) {
            sk_buf_append_c(out, i == 0 ? "::" : ":");
            ignore0 = true;
            continue;
        }
        sk_buf_printf(out, "%x", (unsigned)a[i]);
        if (i != 7)
            sk_buf_putc(out, ':');
    }
}

/* The IPv4 number parser: 0x for hex, a leading 0 for octal. A value
 * past 2^40 stays there, which fails as any past 2^32 does. */
static bool ipv4_number(const char *p, size_t n, uint64_t *v)
{
    if (!n)
        return false;
    int radix = 10;
    if (n >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2, n -= 2, radix = 16;
    else if (n >= 2 && p[0] == '0')
        p++, n--, radix = 8;
    uint64_t x = 0;
    for (size_t i = 0; i < n; i++) {
        int d = hex_value((unsigned char)p[i]);
        if (d < 0 || d >= radix)
            return false;
        if (x < (UINT64_C(1) << 40))
            x = x * (uint64_t)radix + (uint64_t)d;
    }
    *v = x;
    return true;
}

/* The ends-in-a-number checker: the last label, a trailing empty one
 * dropped, is decimal digits or 0x and hex digits. */
static bool ends_in_number(const char *d, size_t n)
{
    size_t e = n;
    if (e && d[e - 1] == '.')
        e--;
    size_t b = e;
    while (b && d[b - 1] != '.')
        b--;
    if (e == b)
        return false;
    bool digits = true;
    for (size_t i = b; i < e; i++)
        digits = digits && ascii_digit((unsigned char)d[i]);
    if (digits)
        return true;
    if (e - b < 2 || d[b] != '0' || (d[b + 1] != 'x' && d[b + 1] != 'X'))
        return false;
    for (size_t i = b + 2; i < e; i++)
        if (hex_value((unsigned char)d[i]) < 0)
            return false;
    return true;
}

/* The IPv4 parser, writing the address in dotted decimal. */
static bool ipv4_write(const char *d, size_t n, sk_buf *out)
{
    uint64_t num[4] = {0};
    size_t count = 0, b = 0;
    if (n && d[n - 1] == '.')
        n--; /* one trailing empty part goes */
    for (size_t i = 0; i <= n; i++) {
        if (i < n && d[i] != '.')
            continue;
        if (count == 4 || !ipv4_number(d + b, i - b, &num[count]))
            return false;
        count++;
        b = i + 1;
    }
    for (size_t i = 0; i + 1 < count; i++)
        if (num[i] > 255)
            return false;
    if (num[count - 1] >= (UINT64_C(1) << (8 * (5 - count))))
        return false;
    uint64_t v = num[count - 1];
    for (size_t i = 0; i + 1 < count; i++)
        v += num[i] << (8 * (3 - i));
    sk_buf_printf(out, "%u.%u.%u.%u", (unsigned)(v >> 24), (unsigned)(v >> 16) & 255, (unsigned)(v >> 8) & 255,
                  (unsigned)v & 255);
    return true;
}

static uint32_t adapt(uint32_t delta, uint32_t points, bool first)
{
    delta = first ? delta / 700 : delta / 2;
    delta += delta / points;
    uint32_t k = 0;
    while (delta > 35 * 26 / 2) {
        delta /= 35;
        k += 36;
    }
    return k + 36 * delta / (delta + 38);
}

/* An xn-- label's rest, lower case, by RFC 3492's decoder: true when it
 * decodes to scalar values, one past ASCII at least, and the result
 * does not itself begin xn--. UTS 46 also checks the result's
 * normalization, its characters' IDNA status and its bidi and joiner
 * rules; those are not here. */
static bool punycode_ok(const char *p, size_t n)
{
    size_t basic = 0;
    bool delim = false;
    for (size_t j = 0; j < n; j++)
        if (p[j] == '-')
            basic = j, delim = true;
    uint32_t *out = sk_xmalloc((n + 1) * sizeof *out);
    size_t len = 0;
    for (size_t j = 0; j < basic; j++)
        out[len++] = (unsigned char)p[j];
    uint32_t code = 128, i = 0, bias = 72;
    bool ok = true, wide = false;
    for (size_t in = delim ? basic + 1 : 0; ok && in < n;) {
        uint32_t old = i, w = 1;
        for (uint32_t k = 36;; k += 36) {
            uint32_t c = in < n ? (unsigned char)p[in++] : 0;
            int d = ascii_digit(c) ? (int)(c - '0') + 26 : c >= 'a' && c <= 'z' ? (int)(c - 'a') : -1;
            if (d < 0 || (uint32_t)d > (UINT32_MAX - i) / w) {
                ok = false;
                break;
            }
            i += (uint32_t)d * w;
            uint32_t t = k <= bias ? 1 : k >= bias + 26 ? 26 : k - bias;
            if ((uint32_t)d < t)
                break;
            if (w > UINT32_MAX / (36 - t)) {
                ok = false;
                break;
            }
            w *= 36 - t;
        }
        if (!ok)
            break;
        uint32_t points = (uint32_t)len + 1;
        bias = adapt(i - old, points, old == 0);
        if (i / points > UINT32_MAX - code) {
            ok = false;
            break;
        }
        code += i / points;
        i %= points;
        if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            ok = false;
            break;
        }
        memmove(out + i + 1, out + i, (len - i) * sizeof *out);
        out[i++] = code;
        len++;
        wide = true;
    }
    ok = ok && wide && !(len >= 4 && out[0] == 'x' && out[1] == 'n' && out[2] == '-' && out[3] == '-');
    free(out);
    return ok;
}

/* The forbidden domain code points of the ASCII range. */
static bool forbidden(unsigned char c)
{
    return c <= 0x20 || c == 0x7F || (c && strchr("#%/:<>?@[\\]^|", c));
}

/* Host parsing of s[0..n), n > 0, for a special scheme: the host as the
 * URL writes it, appended to out. Kelvin's K maps to k under IDNA and
 * lower-cases to k: the one character past ASCII a host can keep. */
static bool host_write(const uint32_t *s, size_t n, sk_buf *out)
{
    if (s[0] == '[') {
        uint16_t a[8];
        if (n < 2 || s[n - 1] != ']' || !ipv6_parse(s + 1, n - 2, a))
            return false;
        sk_buf_putc(out, '[');
        ipv6_write(a, out);
        sk_buf_putc(out, ']');
        return true;
    }
    char *d = sk_xmalloc(n + 1);
    bool ok = true;
    for (size_t i = 0; ok && i < n; i++) {
        uint32_t c = s[i] == 0x212A ? 'k' : s[i];
        ok = c < 0x80 && c != '%';
        d[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    for (size_t b = 0, i = 0; ok && i <= n; i++) {
        if (i < n && d[i] != '.')
            continue;
        if (i - b >= 4 && !memcmp(d + b, "xn--", 4))
            ok = punycode_ok(d + b + 4, i - b - 4);
        b = i + 1;
    }
    for (size_t i = 0; ok && i < n; i++)
        ok = !forbidden((unsigned char)d[i]);
    if (ok && ends_in_number(d, n))
        ok = ipv4_write(d, n, out);
    else if (ok)
        sk_buf_append(out, d, n);
    free(d);
    return ok;
}

/* The userinfo percent-encode set's escapes of s[0..n). */
static void userinfo_write(const uint32_t *s, size_t n, sk_buf *out)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c > 0x20 && c < 0x7F && !strchr("\"#<>?`{}/:;=@[\\]^|", (int)c)) {
            sk_buf_putc(out, (char)c);
            continue;
        }
        char u[4];
        size_t k = sk_utf8_encode(c, u);
        for (size_t j = 0; j < k; j++)
            sk_buf_printf(out, "%%%02X", (unsigned)(unsigned char)u[j]);
    }
}

static uint32_t lower(uint32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c == 0x212A ? 'k' : c;
}

/* isUrl on a match: http or https, :// then anything. */
static bool url_ok(const uint32_t *s, size_t n)
{
    size_t m = n;
    while (m && s[m - 1] <= 0x20)
        m--; /* the parser trims C0 controls and spaces */
    bool https = m > 4 && (s[4] == 's' || s[4] == 'S');
    size_t i = https ? 6 : 5;
    while (i < m && (s[i] == '/' || s[i] == '\\'))
        i++;
    size_t end = i;
    while (end < m && s[end] != '/' && s[end] != '?' && s[end] != '#' && s[end] != '\\')
        end++;
    sk_buf base = {0};
    sk_buf_append_c(&base, https ? "https://" : "http://");
    size_t host = i;
    for (size_t k = i; k < end; k++)
        if (s[k] == '@')
            host = k + 1;
    if (host > i) {
        /* Credentials before the last @, split at the first colon; the
         * base names the user only, or neither when it is empty. */
        size_t at = host - 1, colon = i;
        while (colon < at && s[colon] != ':')
            colon++;
        if (colon > i) {
            userinfo_write(s + i, colon - i, &base);
            if (at > colon + 1) {
                sk_buf_putc(&base, ':');
                userinfo_write(s + colon + 1, at - colon - 1, &base);
            }
            sk_buf_putc(&base, '@');
        }
    }
    size_t port = SIZE_MAX;
    bool bracket = false;
    for (size_t k = host; k < end && port == SIZE_MAX; k++) {
        if (s[k] == ':' && !bracket)
            port = k;
        else if (s[k] == '[')
            bracket = true;
        else if (s[k] == ']')
            bracket = false;
    }
    size_t host_end = port == SIZE_MAX ? end : port;
    bool ok = host_end > host && host_write(s + host, host_end - host, &base);
    if (ok && port != SIZE_MAX && port + 1 < end) {
        uint32_t v = 0;
        for (size_t k = port + 1; ok && k < end; k++) {
            ok = ascii_digit(s[k]);
            if (ok && v <= 65535)
                v = v * 10 + (s[k] - '0');
        }
        ok = ok && v <= 65535;
        if (ok && v != (https ? 443u : 80u))
            sk_buf_printf(&base, ":%u", (unsigned)v);
    }
    for (size_t k = 0; ok && k < base.len; k++)
        ok = k < n && lower(s[k]) == lower((unsigned char)base.ptr[k]);
    sk_buf_free(&base);
    return ok;
}

bool sk_term_url_next(const uint32_t *s, size_t n, size_t *from, size_t *start, size_t *end)
{
    for (size_t p = *from; p < n;) {
        size_t len = match_at(s, n, p);
        if (!len) {
            p++;
            continue;
        }
        *from = p + len;
        if (url_ok(s + p, len)) {
            *start = p;
            *end = p + len;
            return true;
        }
        p += len;
    }
    *from = n;
    return false;
}

/* ---- LinkComputer ---- */

typedef SK_VEC(uint32_t) cps;

static bool get_row(const sk_term_link_src *src, int y, int *cols, bool *wrapped)
{
    *cols = 0;
    *wrapped = false;
    return y >= 0 && y < INT_MAX && src->row(src->ud, y, cols, wrapped);
}

/* The cell's code points into buf, and its width. */
static int cell_text(const sk_term_link_src *src, int y, int x, cps *buf, int *width)
{
    if (buf->cap < 8)
        buf->ptr = sk_vec_grow_(buf->ptr, &buf->cap, 8, sizeof *buf->ptr);
    *width = 1;
    int n = src->cell(src->ud, y, x, buf->ptr, (int)buf->cap, width);
    if (n > (int)buf->cap) {
        buf->ptr = sk_vec_grow_(buf->ptr, &buf->cap, (size_t)n, sizeof *buf->ptr);
        n = src->cell(src->ud, y, x, buf->ptr, (int)buf->cap, width);
        n = n > (int)buf->cap ? (int)buf->cap : n;
    }
    buf->len = n > 0 ? (size_t)n : 0;
    return n > 0 ? n : 0;
}

/* translateToString(true): the row to its last cell with content, an
 * empty cell as a space, the right half of a wide character skipped. */
static void row_text(const sk_term_link_src *src, int y, int cols, cps *out, cps *tmp)
{
    int end = 0, w = 1;
    for (int x = cols - 1; x >= 0; x--) {
        if (cell_text(src, y, x, tmp, &w)) {
            end = x + w;
            break;
        }
    }
    end = end < cols ? end : cols;
    for (int x = 0; x < end; x += w > 0 ? w : 1) {
        if (cell_text(src, y, x, tmp, &w))
            for (size_t i = 0; i < tmp->len; i++)
                sk_vec_push(out, tmp->ptr[i]);
        else
            sk_vec_push(out, ' ');
    }
}

static size_t utf16_len(const cps *s)
{
    size_t n = s->len;
    for (size_t i = 0; i < s->len; i++)
        n += s->ptr[i] > 0xFFFF;
    return n;
}

static bool has_space(const cps *s)
{
    for (size_t i = 0; i < s->len; i++)
        if (s->ptr[i] == ' ')
            return true;
    return false;
}

/* _mapStrIdx: the cell k characters on from (start, line), or -1s
 * when that runs out of rows. An empty last cell before a wrapped row
 * that opens with a wide character is one the text lacks: the wide
 * character wrapped early and trimming dropped the cell. */
static void map_index(const sk_term_link_src *src, int line, int start, long k, int *oy, int *ox, cps *tmp)
{
    while (k) {
        int cols, next_cols, w;
        bool wrapped;
        if (!get_row(src, line, &cols, &wrapped)) {
            *oy = *ox = -1;
            return;
        }
        for (int i = start; i < cols; i++) {
            int n = cell_text(src, line, i, tmp, &w);
            if (w) {
                k -= n ? n : 1;
                if (i == cols - 1 && n == 0 && get_row(src, line + 1, &next_cols, &wrapped) && wrapped &&
                    next_cols > 0) {
                    cell_text(src, line + 1, 0, tmp, &w);
                    if (w == 2)
                        k += 1;
                }
            }
            if (k < 0) {
                *oy = line;
                *ox = i;
                return;
            }
        }
        line++;
        start = 0;
    }
    *oy = line;
    *ox = start;
}

void sk_term_links_find(const sk_term_link_src *src, int y, sk_term_links *out)
{
    int cols;
    bool wrapped;
    if (!get_row(src, y, &cols, &wrapped))
        return;
    cps text = {0}, cur = {0}, tmp = {0};
    SK_VEC(cps) above = {0};
    row_text(src, y, cols, &cur, &tmp);

    /* Up while rows wrap and hold no space, 2048 units at most. The
     * test comes after the step, so a stop at the limit leaves top a
     * row above the text, as in the addon. */
    int top = y;
    if (wrapped && (cur.len == 0 || cur.ptr[0] != ' ')) {
        size_t length = 0;
        int c;
        bool w;
        while (get_row(src, --top, &c, &w) && length < 2048) {
            cps r = {0};
            row_text(src, top, c, &r, &tmp);
            length += utf16_len(&r);
            sk_vec_push(&above, r);
            if (!w || has_space(&r))
                break;
        }
    }
    for (size_t i = above.len; i-- > 0;) {
        for (size_t j = 0; j < above.ptr[i].len; j++)
            sk_vec_push(&text, above.ptr[i].ptr[j]);
        sk_vec_free(&above.ptr[i]);
    }
    sk_vec_free(&above);
    for (size_t j = 0; j < cur.len; j++)
        sk_vec_push(&text, cur.ptr[j]);

    /* Down while rows wrap, to a row with a space. */
    size_t length = 0;
    int c;
    bool w;
    for (int b = y; b < INT_MAX - 1 && get_row(src, ++b, &c, &w) && w && length < 2048;) {
        cps r = {0};
        row_text(src, b, c, &r, &tmp);
        length += utf16_len(&r);
        for (size_t j = 0; j < r.len; j++)
            sk_vec_push(&text, r.ptr[j]);
        bool stop = has_space(&r);
        sk_vec_free(&r);
        if (stop)
            break;
    }

    size_t from = 0, s0, e0;
    while (sk_term_url_next(text.ptr, text.len, &from, &s0, &e0)) {
        sk_term_link l;
        map_index(src, top, 0, (long)s0, &l.y0, &l.x0, &tmp);
        if (l.y0 < 0 || l.x0 < 0)
            continue;
        map_index(src, l.y0, l.x0, (long)(e0 - s0), &l.y1, &l.x1, &tmp);
        if (l.y1 < 0 || l.x1 < 0)
            continue;
        sk_buf uri = {0};
        for (size_t i = s0; i < e0; i++)
            sk_utf8_append(&uri, text.ptr[i]);
        l.uri = sk_xstrdup(sk_buf_cstr(&uri));
        sk_buf_free(&uri);
        sk_vec_push(out, l);
    }
    sk_vec_free(&text);
    sk_vec_free(&cur);
    sk_vec_free(&tmp);
}

void sk_term_links_free(sk_term_links *l)
{
    for (size_t i = 0; i < l->len; i++)
        free(l->ptr[i].uri);
    sk_vec_free(l);
}
