#include "json.h"

#include "map.h"
#include "utf8.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The reader */

static bool fail(sk_jr *r, const char *msg)
{
    if (!r->error) {
        r->error = msg;
        r->error_at = r->i;
    }
    return false;
}

void sk_jr_init(sk_jr *r, const char *s, size_t n)
{
    memset(r, 0, sizeof *r);
    r->s = s;
    r->n = n;
}

void sk_jr_free(sk_jr *r)
{
    sk_buf_free(&r->key);
    sk_buf_free(&r->str);
}

bool sk_jr_failed(const sk_jr *r)
{
    return r->error != NULL;
}

static void ws(sk_jr *r)
{
    while (r->i < r->n) {
        char c = r->s[r->i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        r->i++;
    }
}

sk_json_type sk_jr_peek(sk_jr *r)
{
    if (r->error)
        return SK_JSON_NONE;
    ws(r);
    if (r->i >= r->n)
        return SK_JSON_NONE;
    char c = r->s[r->i];
    switch (c) {
    case '{':
        return SK_JSON_OBJECT;
    case '[':
        return SK_JSON_ARRAY;
    case '"':
        return SK_JSON_STRING;
    case 't':
    case 'f':
        return SK_JSON_BOOL;
    case 'n':
        return SK_JSON_NULL;
    default:
        return c == '-' || (c >= '0' && c <= '9') ? SK_JSON_NUMBER : SK_JSON_NONE;
    }
}

static bool literal(sk_jr *r, const char *word)
{
    size_t len = strlen(word);
    if (r->n - r->i < len || memcmp(r->s + r->i, word, len) != 0)
        return fail(r, "invalid literal");
    r->i += len;
    return true;
}

bool sk_jr_null(sk_jr *r)
{
    if (sk_jr_peek(r) != SK_JSON_NULL)
        return fail(r, "expected null");
    return literal(r, "null");
}

bool sk_jr_bool(sk_jr *r, bool *out)
{
    if (sk_jr_peek(r) != SK_JSON_BOOL)
        return fail(r, "expected true or false");
    bool t = r->s[r->i] == 't';
    if (!literal(r, t ? "true" : "false"))
        return false;
    *out = t;
    return true;
}

static bool digits(sk_jr *r)
{
    size_t start = r->i;
    while (r->i < r->n && r->s[r->i] >= '0' && r->s[r->i] <= '9')
        r->i++;
    return r->i > start;
}

static bool at(const sk_jr *r, char c)
{
    return r->i < r->n && r->s[r->i] == c;
}

static bool scan_number(sk_jr *r)
{
    if (at(r, '-'))
        r->i++;
    if (at(r, '0'))
        r->i++;
    else if (!digits(r))
        return fail(r, "invalid number");
    if (at(r, '.')) {
        r->i++;
        if (!digits(r))
            return fail(r, "invalid number");
    }
    if (at(r, 'e') || at(r, 'E')) {
        r->i++;
        if (at(r, '+') || at(r, '-'))
            r->i++;
        if (!digits(r))
            return fail(r, "invalid number");
    }
    return true;
}

bool sk_jr_number(sk_jr *r, double *out)
{
    if (sk_jr_peek(r) != SK_JSON_NUMBER)
        return fail(r, "expected a number");
    size_t start = r->i;
    if (!scan_number(r))
        return false;
    size_t len = r->i - start;
    char small[64];
    char *text = len < sizeof small ? small : sk_xmalloc(len + 1);
    memcpy(text, r->s + start, len);
    text[len] = 0;
    *out = strtod(text, NULL);
    if (text != small)
        free(text);
    return true;
}

static int hex4(const char *p)
{
    int v = 0;
    for (int k = 0; k < 4; k++) {
        char c = p[k];
        int d = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (d < 0)
            return -1;
        v = v * 16 + d;
    }
    return v;
}

static bool plain(unsigned char c)
{
    return c >= 0x20 && c < 0x80 && c != '"' && c != '\\';
}

/* The string whose quote is at r->i. With buf NULL it only checks the
 * string. Else *res is its text: a view of the input when nothing needed
 * changing, or buf's contents. */
static bool read_string(sk_jr *r, sk_buf *buf, sk_str *res)
{
    size_t i = r->i + 1, start = i;
    /* Most strings need no decoding: no escape, valid UTF-8. */
    while (i < r->n) {
        unsigned char c = (unsigned char)r->s[i];
        if (plain(c)) {
            i++;
            continue;
        }
        if (c >= 0x80) {
            uint32_t cp;
            bool bad = false;
            size_t len = sk_utf8_decode(r->s + i, r->n - i, &cp, &bad);
            if (bad)
                break;
            i += len;
            continue;
        }
        break;
    }
    if (i < r->n && r->s[i] == '"') {
        if (res)
            *res = sk_str_n(r->s + start, i - start);
        r->i = i + 1;
        return true;
    }
    if (buf) {
        sk_buf_clear(buf);
        sk_buf_append(buf, r->s + start, i - start);
    }
    while (i < r->n) {
        unsigned char c = (unsigned char)r->s[i];
        if (plain(c)) {
            size_t j = i;
            while (j < r->n && plain((unsigned char)r->s[j]))
                j++;
            if (buf)
                sk_buf_append(buf, r->s + i, j - i);
            i = j;
        } else if (c == '"') {
            r->i = i + 1;
            if (res)
                *res = sk_buf_str(buf);
            return true;
        } else if (c >= 0x80) {
            uint32_t cp;
            bool bad = false;
            size_t len = sk_utf8_decode(r->s + i, r->n - i, &cp, &bad);
            if (buf) {
                if (bad)
                    sk_utf8_append(buf, SK_UTF8_REPLACEMENT);
                else
                    sk_buf_append(buf, r->s + i, len);
            }
            i += len;
        } else if (c == '\\') {
            r->i = i;
            if (i + 1 >= r->n)
                return fail(r, "unterminated string");
            char e = r->s[i + 1], one;
            switch (e) {
            case '"':
            case '\\':
            case '/':
                one = e;
                break;
            case 'b':
                one = '\b';
                break;
            case 'f':
                one = '\f';
                break;
            case 'n':
                one = '\n';
                break;
            case 'r':
                one = '\r';
                break;
            case 't':
                one = '\t';
                break;
            case 'u': {
                int h = r->n - i >= 6 ? hex4(r->s + i + 2) : -1;
                if (h < 0)
                    return fail(r, "invalid \\u escape");
                uint32_t cp = (uint32_t)h;
                i += 6;
                /* A pair of surrogates is one character. A lone one has
                 * no UTF-8 form: it becomes U+FFFD, as it does when Node
                 * writes such a string out. */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    int low = r->n - i >= 6 && r->s[i] == '\\' && r->s[i + 1] == 'u'
                                  ? hex4(r->s + i + 2)
                                  : -1;
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)low - 0xDC00);
                        i += 6;
                    } else {
                        cp = SK_UTF8_REPLACEMENT;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = SK_UTF8_REPLACEMENT;
                }
                if (buf)
                    sk_utf8_append(buf, cp);
                continue;
            }
            default:
                return fail(r, "invalid escape");
            }
            if (buf)
                sk_buf_putc(buf, one);
            i += 2;
        } else {
            r->i = i;
            return fail(r, "control character in a string");
        }
    }
    r->i = i;
    return fail(r, "unterminated string");
}

bool sk_jr_string(sk_jr *r, sk_str *out)
{
    if (sk_jr_peek(r) != SK_JSON_STRING)
        return fail(r, "expected a string");
    sk_str s;
    if (!read_string(r, &r->str, &s))
        return false;
    if (out)
        *out = s;
    return true;
}

static bool push(sk_jr *r, unsigned char kind)
{
    if (r->depth >= SK_JSON_MAX_DEPTH)
        return fail(r, "nested too deeply");
    r->kind[r->depth++] = kind;
    r->first = true;
    r->i++;
    return true;
}

bool sk_jr_object(sk_jr *r)
{
    if (sk_jr_peek(r) != SK_JSON_OBJECT)
        return fail(r, "expected an object");
    return push(r, '{');
}

bool sk_jr_array(sk_jr *r)
{
    if (sk_jr_peek(r) != SK_JSON_ARRAY)
        return fail(r, "expected an array");
    return push(r, '[');
}

/* After a member of the open container: true when another one follows
 * (its comma consumed), false when the container closed or on an error. */
static bool next_member(sk_jr *r, unsigned char kind, char close)
{
    if (r->error)
        return false;
    if (!r->depth || r->kind[r->depth - 1] != kind)
        return fail(r, kind == '{' ? "not in an object" : "not in an array");
    ws(r);
    if (r->i >= r->n)
        return fail(r, kind == '{' ? "unterminated object" : "unterminated array");
    if (r->s[r->i] == close) {
        r->i++;
        r->depth--;
        r->first = false;
        return false;
    }
    if (!r->first) {
        if (r->s[r->i] != ',')
            return fail(r, kind == '{' ? "expected , or }" : "expected , or ]");
        r->i++;
        ws(r);
    }
    r->first = false;
    return true;
}

bool sk_jr_key(sk_jr *r, sk_str *key)
{
    if (!next_member(r, '{', '}'))
        return false;
    if (!at(r, '"'))
        return fail(r, "expected a key");
    sk_str k;
    if (!read_string(r, &r->key, &k))
        return false;
    ws(r);
    if (!at(r, ':'))
        return fail(r, "expected :");
    r->i++;
    if (key)
        *key = k;
    return true;
}

bool sk_jr_item(sk_jr *r)
{
    return next_member(r, '[', ']');
}

/* sk_jr_key for sk_jr_skip: checks the key without decoding it, so the
 * caller's current key stays as it was. */
static bool skip_key(sk_jr *r)
{
    if (!next_member(r, '{', '}'))
        return false;
    if (!at(r, '"'))
        return fail(r, "expected a key");
    if (!read_string(r, NULL, NULL))
        return false;
    ws(r);
    if (!at(r, ':'))
        return fail(r, "expected :");
    r->i++;
    return true;
}

bool sk_jr_skip(sk_jr *r)
{
    unsigned base = r->depth;
    for (;;) {
        switch (sk_jr_peek(r)) {
        case SK_JSON_OBJECT:
            sk_jr_object(r);
            break;
        case SK_JSON_ARRAY:
            sk_jr_array(r);
            break;
        case SK_JSON_STRING:
            read_string(r, NULL, NULL);
            break;
        case SK_JSON_NUMBER:
            scan_number(r);
            break;
        case SK_JSON_BOOL:
            literal(r, r->s[r->i] == 't' ? "true" : "false");
            break;
        case SK_JSON_NULL:
            literal(r, "null");
            break;
        case SK_JSON_NONE:
            return fail(r, "expected a value");
        }
        /* Up to the next value still inside the one being skipped. */
        for (;;) {
            if (r->error)
                return false;
            if (r->depth == base)
                return true;
            bool more = r->kind[r->depth - 1] == '{' ? skip_key(r) : sk_jr_item(r);
            if (more)
                break;
        }
    }
}

bool sk_jr_done(sk_jr *r)
{
    if (r->error)
        return false;
    ws(r);
    return r->i == r->n && r->depth == 0;
}

/* The tree */

/* A key JavaScript treats as an array index: "0", or digits with no
 * leading zero, at most 2^32 - 2. */
static bool array_index(sk_str k, uint64_t *out)
{
    if (!k.len || k.len > 10 || (k.len > 1 && k.ptr[0] == '0'))
        return false;
    uint64_t v = 0;
    for (size_t i = 0; i < k.len; i++) {
        if (k.ptr[i] < '0' || k.ptr[i] > '9')
            return false;
        v = v * 10 + (uint64_t)(k.ptr[i] - '0');
    }
    if (v > 4294967294u)
        return false;
    *out = v;
    return true;
}

typedef struct {
    uint64_t index;
    const sk_json_member *m;
} indexed;

static int by_index(const void *a, const void *b)
{
    uint64_t x = ((const indexed *)a)->index, y = ((const indexed *)b)->index;
    return x < y ? -1 : x > y;
}

/* The members of from in the order a JavaScript object keeps them, and
 * so JSON.stringify writes them: array-index keys first, ascending, then
 * the other keys in the order they came. */
static void js_order(sk_json_member *to, const sk_json_member *from, size_t n)
{
    SK_VEC(indexed) idx = {0};
    for (size_t i = 0; i < n; i++) {
        uint64_t k;
        if (array_index(from[i].key, &k)) {
            indexed e = {k, &from[i]};
            sk_vec_push(&idx, e);
        }
    }
    size_t out = 0;
    if (idx.len) {
        qsort(idx.ptr, idx.len, sizeof *idx.ptr, by_index);
        for (size_t i = 0; i < idx.len; i++)
            to[out++] = *idx.ptr[i].m;
    }
    for (size_t i = 0; i < n; i++) {
        uint64_t k;
        if (!array_index(from[i].key, &k))
            to[out++] = from[i];
    }
    sk_vec_free(&idx);
}

static bool build(sk_jr *r, sk_arena *a, sk_json *v)
{
    switch (sk_jr_peek(r)) {
    case SK_JSON_NULL:
        v->type = SK_JSON_NULL;
        return sk_jr_null(r);
    case SK_JSON_BOOL:
        v->type = SK_JSON_BOOL;
        return sk_jr_bool(r, &v->boolean);
    case SK_JSON_NUMBER:
        v->type = SK_JSON_NUMBER;
        return sk_jr_number(r, &v->number);
    case SK_JSON_STRING: {
        sk_str s;
        if (!sk_jr_string(r, &s))
            return false;
        v->type = SK_JSON_STRING;
        v->string = sk_str_n(sk_arena_strndup(a, s.ptr, s.len), s.len);
        return true;
    }
    case SK_JSON_ARRAY: {
        SK_VEC(sk_json) items = {0};
        sk_jr_array(r);
        while (sk_jr_item(r)) {
            sk_json item;
            if (!build(r, a, &item))
                break;
            sk_vec_push(&items, item);
        }
        if (sk_jr_failed(r)) {
            sk_vec_free(&items);
            return false;
        }
        v->type = SK_JSON_ARRAY;
        v->array.len = items.len;
        v->array.items = NULL;
        if (items.len) {
            v->array.items = sk_arena_array(a, items.len, sizeof *items.ptr);
            memcpy(v->array.items, items.ptr, items.len * sizeof *items.ptr);
        }
        sk_vec_free(&items);
        return true;
    }
    case SK_JSON_OBJECT: {
        SK_VEC(sk_json_member) members = {0};
        sk_map seen; /* key -> position + 1, once the object grows */
        bool mapped = false;
        sk_str key;
        sk_jr_object(r);
        while (sk_jr_key(r, &key)) {
            sk_json_member m;
            m.key = sk_str_n(sk_arena_strndup(a, key.ptr, key.len), key.len);
            if (!build(r, a, &m.value))
                break;
            /* A repeated key keeps its first place and takes its last
             * value, as JSON.parse leaves it. */
            size_t at_pos = SIZE_MAX;
            if (mapped) {
                uintptr_t p = (uintptr_t)sk_map_get(&seen, m.key);
                at_pos = p ? p - 1 : SIZE_MAX;
            } else {
                for (size_t i = 0; i < members.len && at_pos == SIZE_MAX; i++)
                    if (sk_str_eq(members.ptr[i].key, m.key))
                        at_pos = i;
            }
            if (at_pos != SIZE_MAX) {
                members.ptr[at_pos].value = m.value;
                continue;
            }
            sk_vec_push(&members, m);
            if (!mapped && members.len > 16) {
                sk_map_init(&seen);
                mapped = true;
                for (size_t i = 0; i < members.len; i++)
                    *sk_map_put(&seen, members.ptr[i].key) = (void *)(uintptr_t)(i + 1);
            } else if (mapped) {
                *sk_map_put(&seen, m.key) = (void *)(uintptr_t)members.len;
            }
        }
        if (mapped)
            sk_map_free(&seen);
        if (sk_jr_failed(r)) {
            sk_vec_free(&members);
            return false;
        }
        v->type = SK_JSON_OBJECT;
        v->object.len = members.len;
        v->object.members = NULL;
        if (members.len) {
            v->object.members = sk_arena_array(a, members.len, sizeof *members.ptr);
            js_order(v->object.members, members.ptr, members.len);
        }
        sk_vec_free(&members);
        return true;
    }
    case SK_JSON_NONE:
        break;
    }
    return fail(r, "expected a value");
}

sk_json *sk_json_parse(sk_arena *a, const char *s, size_t n, sk_json_error *err)
{
    sk_jr r;
    sk_jr_init(&r, s, n);
    sk_json *v = sk_arena_alloc(a, sizeof *v);
    bool ok = build(&r, a, v) && sk_jr_done(&r);
    if (!ok)
        fail(&r, "unexpected text after the value");
    if (err) {
        err->message = ok ? NULL : r.error;
        err->offset = ok ? 0 : r.error_at;
    }
    sk_jr_free(&r);
    return ok ? v : NULL;
}

const sk_json *sk_json_get(const sk_json *v, const char *key)
{
    if (!v || v->type != SK_JSON_OBJECT)
        return NULL;
    sk_str k = sk_str_c(key);
    for (size_t i = v->object.len; i-- > 0;)
        if (sk_str_eq(v->object.members[i].key, k))
            return &v->object.members[i].value;
    return NULL;
}

const sk_json *sk_json_dig(const sk_json *v, ...)
{
    va_list ap;
    va_start(ap, v);
    for (const char *k; v && (k = va_arg(ap, const char *));)
        v = sk_json_get(v, k);
    va_end(ap);
    return v;
}

const sk_json *sk_json_at(const sk_json *v, size_t i)
{
    if (!v || v->type != SK_JSON_ARRAY || i >= v->array.len)
        return NULL;
    return &v->array.items[i];
}

bool sk_json_as_number(const sk_json *v, double *out)
{
    if (!v || v->type != SK_JSON_NUMBER || !isfinite(v->number))
        return false;
    *out = v->number;
    return true;
}

bool sk_json_as_string(const sk_json *v, sk_str *out)
{
    if (!v || v->type != SK_JSON_STRING)
        return false;
    *out = v->string;
    return true;
}

bool sk_json_as_bool(const sk_json *v, bool *out)
{
    if (!v || v->type != SK_JSON_BOOL)
        return false;
    *out = v->boolean;
    return true;
}

/* The writer */

void sk_jw_init(sk_jw *w, sk_buf *out, int indent)
{
    w->out = out;
    w->indent = indent < 0 ? 0 : indent > 10 ? 10 : indent; /* as JSON.stringify */
    w->depth = 0;
    w->first = true;
    w->after_key = false;
}

static void newline(sk_jw *w, unsigned depth)
{
    sk_buf_putc(w->out, '\n');
    for (unsigned i = 0; i < depth * (unsigned)w->indent; i++)
        sk_buf_putc(w->out, ' ');
}

static void before_value(sk_jw *w)
{
    if (w->after_key) {
        w->after_key = false;
        return;
    }
    if (w->depth == 0)
        return;
    if (!w->first)
        sk_buf_putc(w->out, ',');
    if (w->indent)
        newline(w, w->depth);
    w->first = false;
}

static void open_container(sk_jw *w, char c)
{
    before_value(w);
    sk_buf_putc(w->out, c);
    w->depth++;
    w->first = true;
}

static void close_container(sk_jw *w, char c)
{
    if (w->depth == 0)
        sk_die("json writer: close without open");
    w->depth--;
    if (!w->first && w->indent)
        newline(w, w->depth);
    sk_buf_putc(w->out, c);
    w->first = false;
}

void sk_jw_object(sk_jw *w)
{
    open_container(w, '{');
}

void sk_jw_object_end(sk_jw *w)
{
    close_container(w, '}');
}

void sk_jw_array(sk_jw *w)
{
    open_container(w, '[');
}

void sk_jw_array_end(sk_jw *w)
{
    close_container(w, ']');
}

static void write_string(sk_buf *b, sk_str s)
{
    static const char hex[] = "0123456789abcdef";
    sk_buf_putc(b, '"');
    size_t i = 0;
    while (i < s.len) {
        unsigned char c = (unsigned char)s.ptr[i];
        if (plain(c)) {
            size_t j = i;
            while (j < s.len && plain((unsigned char)s.ptr[j]))
                j++;
            sk_buf_append(b, s.ptr + i, j - i);
            i = j;
            continue;
        }
        if (c >= 0x80) {
            uint32_t cp;
            bool bad = false;
            size_t len = sk_utf8_decode(s.ptr + i, s.len - i, &cp, &bad);
            if (bad)
                sk_utf8_append(b, SK_UTF8_REPLACEMENT);
            else
                sk_buf_append(b, s.ptr + i, len);
            i += len;
            continue;
        }
        switch (c) {
        case '"':
            sk_buf_append_c(b, "\\\"");
            break;
        case '\\':
            sk_buf_append_c(b, "\\\\");
            break;
        case '\b':
            sk_buf_append_c(b, "\\b");
            break;
        case '\f':
            sk_buf_append_c(b, "\\f");
            break;
        case '\n':
            sk_buf_append_c(b, "\\n");
            break;
        case '\r':
            sk_buf_append_c(b, "\\r");
            break;
        case '\t':
            sk_buf_append_c(b, "\\t");
            break;
        default: {
            char e[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            sk_buf_append(b, e, sizeof e);
        }
        }
        i++;
    }
    sk_buf_putc(b, '"');
}

void sk_jw_key(sk_jw *w, sk_str key)
{
    if (w->depth == 0)
        sk_die("json writer: key outside an object");
    before_value(w);
    write_string(w->out, key);
    sk_buf_append_c(w->out, w->indent ? ": " : ":");
    w->after_key = true;
}

void sk_jw_string(sk_jw *w, sk_str s)
{
    before_value(w);
    write_string(w->out, s);
}

void sk_jw_number(sk_jw *w, double d)
{
    before_value(w);
    sk_json_format_number(w->out, d);
}

void sk_jw_int(sk_jw *w, int64_t i)
{
    before_value(w);
    sk_buf_printf(w->out, "%lld", (long long)i);
}

void sk_jw_bool(sk_jw *w, bool b)
{
    before_value(w);
    sk_buf_append_c(w->out, b ? "true" : "false");
}

void sk_jw_null(sk_jw *w)
{
    before_value(w);
    sk_buf_append_c(w->out, "null");
}

void sk_jw_raw(sk_jw *w, sk_str json)
{
    before_value(w);
    sk_buf_append_str(w->out, json);
}

void sk_jw_value(sk_jw *w, const sk_json *v)
{
    switch (v->type) {
    case SK_JSON_NONE:
    case SK_JSON_NULL:
        sk_jw_null(w);
        break;
    case SK_JSON_BOOL:
        sk_jw_bool(w, v->boolean);
        break;
    case SK_JSON_NUMBER:
        sk_jw_number(w, v->number);
        break;
    case SK_JSON_STRING:
        sk_jw_string(w, v->string);
        break;
    case SK_JSON_ARRAY:
        sk_jw_array(w);
        for (size_t i = 0; i < v->array.len; i++)
            sk_jw_value(w, &v->array.items[i]);
        sk_jw_array_end(w);
        break;
    case SK_JSON_OBJECT:
        sk_jw_object(w);
        for (size_t i = 0; i < v->object.len; i++) {
            sk_jw_key(w, v->object.members[i].key);
            sk_jw_value(w, &v->object.members[i].value);
        }
        sk_jw_object_end(w);
        break;
    }
}

/* e is d in %e form with p digits, which does not read back as d. Tries
 * the p-digit values one unit of the last digit above and below; when one
 * reads back, writes it to e and returns true. Beside a power of two a
 * double's interval is lopsided, so the correctly rounded p digits can
 * miss it while a neighbour lands inside (2^-24 is 5.960464477539063e-8
 * in JavaScript, not ...0625). */
static bool neighbour(char *e, size_t cap, double d, int p)
{
    const char *s = e;
    bool neg = *s == '-';
    if (neg)
        s++;
    char dig[24];
    int k = 0;
    for (; *s && *s != 'e'; s++)
        if (*s != '.' && k < (int)sizeof dig)
            dig[k++] = *s;
    if (*s != 'e' || k != p)
        return false;
    int exp = atoi(s + 1);
    for (int step = 1; step >= -1; step -= 2) {
        char c[24];
        int x = exp;
        memcpy(c, dig, (size_t)k);
        int i = k - 1;
        if (step > 0) {
            while (i >= 0 && c[i] == '9')
                c[i--] = '0';
            if (i < 0) {
                c[0] = '1'; /* 9.99 up is 10.0: one digit more to the left */
                x++;
            } else {
                c[i]++;
            }
        } else {
            while (i >= 0 && c[i] == '0')
                c[i--] = '9';
            if (i < 0)
                continue;
            c[i]--;
            if (c[0] == '0') { /* 1.00 down is 0.999: all nines, one place lower */
                memset(c, '9', (size_t)k);
                x--;
            }
        }
        char cand[48];
        int n = snprintf(cand, sizeof cand, "%s%c%s%.*se%d", neg ? "-" : "", c[0], k > 1 ? "." : "",
                         k - 1, c + 1, x);
        if (n > 0 && (size_t)n < cap && strtod(cand, NULL) == d) {
            memcpy(e, cand, (size_t)n + 1);
            return true;
        }
    }
    return false;
}

/* ECMAScript Number::toString: the fewest significant digits that read
 * back as the same double, then fixed or exponent form by the decimal
 * point's position. For each length the correctly rounded digits come
 * first, then their neighbours in the last digit, so the shortest form
 * that reads back is found even beside a power of two. */
void sk_json_format_number(sk_buf *b, double d)
{
    if (!isfinite(d)) {
        sk_buf_append_c(b, "null");
        return;
    }
    if (d == 0) {
        sk_buf_putc(b, '0'); /* -0 too */
        return;
    }
    char e[40];
    for (int p = 1; p <= 17; p++) {
        snprintf(e, sizeof e, "%.*e", p - 1, d);
        if (strtod(e, NULL) == d || (p < 17 && neighbour(e, sizeof e, d, p)))
            break;
    }
    const char *p = e;
    if (*p == '-') {
        sk_buf_putc(b, '-');
        p++;
    }
    char dig[24];
    int k = 0;
    for (; *p && *p != 'e'; p++)
        if (*p != '.' && k < (int)sizeof dig)
            dig[k++] = *p;
    int n = (*p == 'e' ? atoi(p + 1) : 0) + 1;
    while (k > 1 && dig[k - 1] == '0')
        k--;
    if (k <= n && n <= 21) {
        sk_buf_append(b, dig, (size_t)k);
        for (int i = k; i < n; i++)
            sk_buf_putc(b, '0');
    } else if (n > 0 && n <= 21) {
        sk_buf_append(b, dig, (size_t)n);
        sk_buf_putc(b, '.');
        sk_buf_append(b, dig + n, (size_t)(k - n));
    } else if (n > -6 && n <= 0) {
        sk_buf_append_c(b, "0.");
        for (int i = 0; i < -n; i++)
            sk_buf_putc(b, '0');
        sk_buf_append(b, dig, (size_t)k);
    } else {
        sk_buf_putc(b, dig[0]);
        if (k > 1) {
            sk_buf_putc(b, '.');
            sk_buf_append(b, dig + 1, (size_t)(k - 1));
        }
        sk_buf_printf(b, "e%c%d", n - 1 >= 0 ? '+' : '-', n - 1 >= 0 ? n - 1 : 1 - n);
    }
}
