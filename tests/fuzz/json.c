/* JSON: the tree parser and the skipping reader accept the same inputs,
 * and whatever parses survives the writer, compact and indented, as
 * valid UTF-8 that parses back to the same tree. */
#include "../../src/base/json.h"
#include "../../src/base/utf8.h"
#include "fuzz.h"

#include <math.h>
#include <stdlib.h>

const char *const sk_fuzz_dict[] = {
    "true", "false", "null", "\"", "\\u", "\\ud83d\\ude00", "\\ud800", "{}", "[]", "{\"a\":",
    "1e400", "-0", "0.5", "1E+2", "\xEF\xBF\xBD", "\xF0\x9F\x98\x80", NULL,
};

static bool same(const sk_json *a, const sk_json *b)
{
    if (a->type == SK_JSON_NUMBER && !isfinite(a->number))
        return b->type == SK_JSON_NULL;
    if (a->type != b->type)
        return false;
    switch (a->type) {
    case SK_JSON_BOOL:
        return a->boolean == b->boolean;
    case SK_JSON_NUMBER:
        return a->number == b->number;
    case SK_JSON_STRING:
        return sk_str_eq(a->string, b->string);
    case SK_JSON_ARRAY:
        if (a->array.len != b->array.len)
            return false;
        for (size_t i = 0; i < a->array.len; i++)
            if (!same(&a->array.items[i], &b->array.items[i]))
                return false;
        return true;
    case SK_JSON_OBJECT:
        if (a->object.len != b->object.len)
            return false;
        for (size_t i = 0; i < a->object.len; i++)
            if (!sk_str_eq(a->object.members[i].key, b->object.members[i].key) ||
                !same(&a->object.members[i].value, &b->object.members[i].value))
                return false;
        return true;
    default:
        return true;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const char *s = (const char *)data;
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_json *v = sk_json_parse(&a, s, size, NULL);

    sk_jr r;
    sk_jr_init(&r, s, size);
    bool skipped = sk_jr_skip(&r) && sk_jr_done(&r);
    sk_jr_free(&r);
    if (skipped != (v != NULL))
        abort();

    if (v) {
        for (int indent = 0; indent <= 1; indent++) {
            sk_buf out = {0};
            sk_jw w;
            sk_jw_init(&w, &out, indent);
            sk_jw_value(&w, v);
            if (!sk_utf8_valid(out.ptr, out.len))
                abort();
            sk_json *back = sk_json_parse(&a, out.ptr, out.len, NULL);
            if (!back || !same(v, back))
                abort();
            sk_buf_free(&out);
        }
    }
    sk_arena_free(&a);
    return 0;
}
