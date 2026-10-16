/* JavaScript's String and Node's path, as the app's JavaScript modules
 * used them (decision 0036: the C app gives what the JavaScript app
 * gave). The path functions follow lib/path.js of Node 22, posix. */
#include "js.h"

#include "../base/utf8.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

bool sk_js_space(uint32_t c)
{
    switch (c) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
    case 0xA0: case 0x1680: case 0x2028: case 0x2029: case 0x202F:
    case 0x205F: case 0x3000: case 0xFEFF:
        return true;
    default:
        return c >= 0x2000 && c <= 0x200A;
    }
}

sk_str sk_js_trim(sk_str s)
{
    size_t i = 0, start = 0, end = 0;
    bool seen = false;
    while (i < s.len) {
        uint32_t cp;
        size_t n = sk_utf8_decode(s.ptr + i, s.len - i, &cp, NULL);
        if (!sk_js_space(cp)) {
            if (!seen)
                start = i;
            seen = true;
            end = i + n;
        }
        i += n;
    }
    return seen ? sk_str_n(s.ptr + start, end - start) : sk_str_n("", 0);
}

size_t sk_js_units(sk_str s)
{
    size_t units = 0;
    for (size_t i = 0; i < s.len;) {
        uint32_t cp;
        i += sk_utf8_decode(s.ptr + i, s.len - i, &cp, NULL);
        units += cp > 0xFFFF ? 2 : 1;
    }
    return units;
}

void sk_js_cut(sk_buf *out, sk_str s, size_t units)
{
    size_t used = 0;
    for (size_t i = 0; i < s.len && used < units;) {
        uint32_t cp;
        i += sk_utf8_decode(s.ptr + i, s.len - i, &cp, NULL);
        size_t u = cp > 0xFFFF ? 2 : 1;
        if (used + u > units)
            cp = SK_UTF8_REPLACEMENT;
        sk_utf8_append(out, cp);
        used += u;
    }
}

sk_str sk_js_basename(sk_str p)
{
    size_t start = 0, end = 0;
    bool matched_slash = true, have_end = false;
    for (size_t i = p.len; i-- > 0;) {
        if (p.ptr[i] == '/') {
            if (!matched_slash) {
                start = i + 1;
                break;
            }
        } else if (!have_end) {
            matched_slash = false;
            have_end = true;
            end = i + 1;
        }
    }
    return have_end ? sk_str_n(p.ptr + start, end - start) : sk_str_n("", 0);
}

sk_str sk_js_dirname(sk_str p)
{
    if (p.len == 0)
        return SK_STR(".");
    bool has_root = p.ptr[0] == '/', matched_slash = true;
    size_t end = 0;
    for (size_t i = p.len - 1; i >= 1; i--) {
        if (p.ptr[i] == '/') {
            if (!matched_slash) {
                end = i;
                break;
            }
        } else {
            matched_slash = false;
        }
    }
    if (end == 0)
        return has_root ? SK_STR("/") : SK_STR(".");
    if (has_root && end == 1)
        return SK_STR("//");
    return sk_str_n(p.ptr, end);
}

/* normalizeString: the segments without empty ones and dots, each ..
 * taking back the segment before it, or kept at the front when
 * above_root allows it. */
static void normalize_segments(sk_buf *out, sk_str p, bool above_root)
{
    SK_VEC(sk_str) segs = {0};
    for (size_t i = 0; i < p.len;) {
        size_t j = i;
        while (j < p.len && p.ptr[j] != '/')
            j++;
        sk_str s = sk_str_n(p.ptr + i, j - i);
        i = j + 1;
        if (s.len == 0 || sk_str_eq_c(s, "."))
            continue;
        if (sk_str_eq_c(s, "..")) {
            if (segs.len && !sk_str_eq_c(segs.ptr[segs.len - 1], ".."))
                segs.len--;
            else if (above_root)
                sk_vec_push(&segs, s);
            continue;
        }
        sk_vec_push(&segs, s);
    }
    for (size_t k = 0; k < segs.len; k++) {
        if (k)
            sk_buf_putc(out, '/');
        sk_buf_append_str(out, segs.ptr[k]);
    }
    sk_vec_free(&segs);
}

void sk_js_normalize(sk_buf *out, sk_str p)
{
    if (p.len == 0) {
        sk_buf_putc(out, '.');
        return;
    }
    bool absolute = p.ptr[0] == '/', trailing = p.ptr[p.len - 1] == '/';
    sk_buf res = {0};
    normalize_segments(&res, p, !absolute);
    if (absolute)
        sk_buf_putc(out, '/');
    if (res.len == 0 && !absolute)
        sk_buf_append_c(out, trailing ? "./" : ".");
    sk_buf_append_str(out, sk_buf_str(&res));
    if (res.len && trailing)
        sk_buf_putc(out, '/');
    sk_buf_free(&res);
}

void sk_js_join(sk_buf *out, sk_str a, sk_str b)
{
    sk_buf joined = {0};
    sk_buf_append_str(&joined, a);
    if (a.len && b.len)
        sk_buf_putc(&joined, '/');
    sk_buf_append_str(&joined, b);
    sk_js_normalize(out, sk_buf_str(&joined));
    sk_buf_free(&joined);
}

static void cwd(sk_buf *out)
{
    size_t size = 256;
    for (;;) {
        char *buf = sk_xmalloc(size);
        if (getcwd(buf, size)) {
            sk_buf_append_c(out, buf);
            free(buf);
            return;
        }
        free(buf);
        if (errno != ERANGE)
            return;
        size *= 2;
    }
}

void sk_js_resolve(sk_buf *out, sk_str p)
{
    sk_buf all = {0};
    if (!(p.len && p.ptr[0] == '/')) {
        cwd(&all);
        sk_buf_putc(&all, '/');
    }
    sk_buf_append_str(&all, p);
    sk_buf_putc(out, '/');
    normalize_segments(out, sk_buf_str(&all), false);
    sk_buf_free(&all);
}
