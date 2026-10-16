/* JavaScript's reading of values, for the ports of
 * app/metrics/claude/parse.js, compact.js and title.js: truthiness,
 * String(), trim() and slice(), which count UTF-16 code units. */
#include "internal.h"

#include "../../base/utf8.h"

#include <math.h>
#include <stdarg.h>

void sk_claude_logf(sk_log_fn fn, void *ud, const char *fmt, ...)
{
    if (!fn)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    fn(ud, sk_buf_cstr(&b));
    sk_buf_free(&b);
}

bool sk_claude_truthy_num(double x)
{
    return x != 0 && !isnan(x);
}

bool sk_claude_truthy(const sk_json *v)
{
    if (!v)
        return false;
    switch (v->type) {
    case SK_JSON_BOOL:
        return v->boolean;
    case SK_JSON_NUMBER:
        return sk_claude_truthy_num(v->number);
    case SK_JSON_STRING:
        return v->string.len > 0;
    case SK_JSON_ARRAY:
    case SK_JSON_OBJECT:
        return true;
    default:
        return false;
    }
}

void sk_claude_to_string(sk_buf *out, const sk_json *v)
{
    if (!v || v->type == SK_JSON_NONE) {
        sk_buf_append_c(out, "undefined");
        return;
    }
    switch (v->type) {
    case SK_JSON_NULL:
        sk_buf_append_c(out, "null");
        break;
    case SK_JSON_BOOL:
        sk_buf_append_c(out, v->boolean ? "true" : "false");
        break;
    case SK_JSON_NUMBER:
        sk_json_format_number(out, v->number);
        break;
    case SK_JSON_STRING:
        sk_buf_append_str(out, v->string);
        break;
    case SK_JSON_ARRAY:
        /* Array.prototype.join: null items are empty. */
        for (size_t i = 0; i < v->array.len; i++) {
            if (i)
                sk_buf_putc(out, ',');
            const sk_json *it = &v->array.items[i];
            if (it->type != SK_JSON_NULL && it->type != SK_JSON_NONE)
                sk_claude_to_string(out, it);
        }
        break;
    default:
        sk_buf_append_c(out, "[object Object]");
        break;
    }
}

bool sk_claude_space(uint32_t cp)
{
    switch (cp) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0:
    case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
    case 0xFEFF:
        return true;
    default:
        return cp >= 0x2000 && cp <= 0x200A;
    }
}

sk_str sk_claude_trim(sk_str s)
{
    size_t start = SIZE_MAX, end = 0;
    for (size_t i = 0; i < s.len;) {
        uint32_t cp;
        size_t n = sk_utf8_decode(s.ptr + i, s.len - i, &cp, NULL);
        if (!sk_claude_space(cp)) {
            if (start == SIZE_MAX)
                start = i;
            end = i + n;
        }
        i += n;
    }
    return start == SIZE_MAX ? sk_str_n(s.ptr, 0) : sk_str_n(s.ptr + start, end - start);
}

void sk_claude_slice(sk_buf *out, sk_str s, size_t units)
{
    for (size_t i = 0; i < s.len && units > 0;) {
        uint32_t cp;
        bool bad = false;
        size_t n = sk_utf8_decode(s.ptr + i, s.len - i, &cp, &bad);
        size_t need = cp > 0xFFFF ? 2 : 1;
        if (need > units) {
            sk_utf8_append(out, SK_UTF8_REPLACEMENT);
            return;
        }
        if (bad)
            sk_utf8_append(out, SK_UTF8_REPLACEMENT);
        else
            sk_buf_append(out, s.ptr + i, n);
        units -= need;
        i += n;
    }
}
