#include "../../src/base/json.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

static sk_json *parse(sk_arena *a, const char *s)
{
    return sk_json_parse(a, s, strlen(s), NULL);
}

static void test_scalars(void)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_json *v = parse(&a, " null ");
    CHECK(v && v->type == SK_JSON_NULL);
    v = parse(&a, "true");
    CHECK(v && v->type == SK_JSON_BOOL && v->boolean);
    v = parse(&a, "false");
    CHECK(v && v->type == SK_JSON_BOOL && !v->boolean);
    struct {
        const char *text;
        double value;
    } nums[] = {{"0", 0}, {"-0", -0.0}, {"1.5e3", 1500}, {"1E-2", 0.01}, {"-12.25", -12.25},
                {"123456789012", 123456789012.0}};
    for (size_t i = 0; i < SK_LEN(nums); i++) {
        v = parse(&a, nums[i].text);
        CHECK(v && v->type == SK_JSON_NUMBER && v->number == nums[i].value);
    }
    v = parse(&a, "1e400");
    double d = 0;
    CHECK(v && v->type == SK_JSON_NUMBER && isinf(v->number));
    CHECK(!sk_json_as_number(v, &d)); /* not finite */
    sk_arena_free(&a);
}

static void test_strings(void)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    struct {
        const char *text, *value;
    } cases[] = {
        {"\"plain\"", "plain"},
        {"\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"", "a\"b\\c/d\b\f\n\r\t"},
        {"\"\\u00e9\\u20AC\"", "\xC3\xA9\xE2\x82\xAC"},
        {"\"\\ud83d\\ude00\"", "\xF0\x9F\x98\x80"},           /* a surrogate pair */
        {"\"\\ud800x\"", "\xEF\xBF\xBDx"},                     /* a lone high surrogate */
        {"\"\\udc00\"", "\xEF\xBF\xBD"},                       /* a lone low surrogate */
        {"\"\\ud800\\u0041\"", "\xEF\xBF\xBD" "A"},           /* high, then not low */
        {"\"caf\xC3\xA9\"", "caf\xC3\xA9"},                   /* raw UTF-8 */
        {"\"a\xFF" "b\"", "a\xEF\xBF\xBD" "b"},               /* invalid bytes, as Node reads them */
        {"\"\x7F\"", "\x7F"},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_json *v = parse(&a, cases[i].text);
        CHECK(v && v->type == SK_JSON_STRING);
        if (v)
            CHECK_MEM(v->string, cases[i].value);
    }
    sk_arena_free(&a);
}

static void test_rejects(void)
{
    const char *bad[] = {
        "", " ", "01", "1.", ".5", "+1", "-", "1e", "0x10", "[1,]", "{\"a\":1,}", "{a:1}",
        "\"\\x\"", "\"a", "\"a\nb\"", "\"\\u12\"", "\"\\uZZZZ\"", "tru", "nulls", "[1 2]",
        "{\"a\" 1}", "{\"a\":}", "[", "]", "{", "}", "1 2", "\xEF\xBB\xBF{}", "NaN", "Infinity",
        "[1,,2]", "{,}", "\"\t\"",
    };
    sk_arena a;
    sk_arena_init(&a, 0);
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        sk_json_error err = {0};
        sk_json *v = sk_json_parse(&a, bad[i], strlen(bad[i]), &err);
        if (v)
            sk_test_fail(__FILE__, __LINE__, "accepted: %s", bad[i]);
        CHECK(err.message != NULL);
    }
    sk_arena_free(&a);
}

static void test_depth_cap(void)
{
    sk_buf s = {0};
    for (int i = 0; i < SK_JSON_MAX_DEPTH; i++)
        sk_buf_putc(&s, '[');
    for (int i = 0; i < SK_JSON_MAX_DEPTH; i++)
        sk_buf_putc(&s, ']');
    sk_arena a;
    sk_arena_init(&a, 0);
    CHECK(sk_json_parse(&a, s.ptr, s.len, NULL) != NULL);
    sk_buf_clear(&s);
    for (int i = 0; i <= SK_JSON_MAX_DEPTH; i++)
        sk_buf_putc(&s, '[');
    for (int i = 0; i <= SK_JSON_MAX_DEPTH; i++)
        sk_buf_putc(&s, ']');
    sk_json_error err;
    CHECK(sk_json_parse(&a, s.ptr, s.len, &err) == NULL);
    CHECK_STR(err.message, "nested too deeply");
    sk_arena_free(&a);
    sk_buf_free(&s);
}

static void test_tree_access(void)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_json *v = parse(&a, "{\"a\":1,\"cost\":{\"total_cost_usd\":0.25},\"a\":2,"
                           "\"list\":[true,\"x\",null],\"s\":\"t\"}");
    CHECK(v != NULL);
    double d = 0;
    CHECK(sk_json_as_number(sk_json_get(v, "a"), &d) && d == 2); /* the last one wins */
    CHECK(sk_json_as_number(sk_json_dig(v, "cost", "total_cost_usd", NULL), &d) && d == 0.25);
    CHECK(sk_json_dig(v, "cost", "nope", NULL) == NULL);
    CHECK(sk_json_dig(v, "list", "x", NULL) == NULL);
    const sk_json *list = sk_json_get(v, "list");
    CHECK(list && list->type == SK_JSON_ARRAY && list->array.len == 3);
    bool b = false;
    CHECK(sk_json_as_bool(sk_json_at(list, 0), &b) && b);
    sk_str s;
    CHECK(sk_json_as_string(sk_json_at(list, 1), &s));
    CHECK_MEM(s, "x");
    CHECK(sk_json_at(list, 3) == NULL);
    CHECK(sk_json_get(list, "a") == NULL);
    CHECK(!sk_json_as_string(sk_json_get(v, "a"), &s));
    CHECK(sk_json_as_string(sk_json_get(v, "s"), &s) && s.ptr[s.len] == 0);
    sk_arena_free(&a);
}

/* Reads only "model.display_name" from a line, skipping everything else,
 * the way the parsers of transcripts will. */
static void test_reader_picks_and_skips(void)
{
    const char *line = "{\"blob\":\"" "QUJD" "\",\"skip\":{\"deep\":[1,[2,{\"x\":\"\\u00e9\"}]]},"
                       "\"model\":{\"id\":\"m\",\"display_name\":\"Opus\"},\"n\":-1.5e2}";
    sk_jr r;
    sk_jr_init(&r, line, strlen(line));
    sk_str key, name = {0};
    double n = 0;
    CHECK(sk_jr_object(&r));
    while (sk_jr_key(&r, &key)) {
        if (sk_str_eq_c(key, "model")) {
            CHECK(sk_jr_object(&r));
            while (sk_jr_key(&r, &key)) {
                if (sk_str_eq_c(key, "display_name"))
                    CHECK(sk_jr_string(&r, &name));
                else
                    CHECK(sk_jr_skip(&r));
            }
        } else if (sk_str_eq_c(key, "n")) {
            CHECK(sk_jr_number(&r, &n));
        } else {
            CHECK(sk_jr_skip(&r));
        }
    }
    CHECK(!sk_jr_failed(&r));
    CHECK(sk_jr_done(&r));
    CHECK_MEM(name, "Opus");
    CHECK(n == -150);
    sk_jr_free(&r);
}

static void test_reader_errors_stick(void)
{
    sk_jr r;
    const char *s = "[1, oops, 3]";
    sk_jr_init(&r, s, strlen(s));
    double d;
    CHECK(sk_jr_array(&r));
    CHECK(sk_jr_item(&r) && sk_jr_number(&r, &d));
    CHECK(sk_jr_item(&r));
    CHECK(!sk_jr_number(&r, &d));
    CHECK(sk_jr_failed(&r));
    CHECK_INT(r.error_at, 4);
    CHECK(!sk_jr_item(&r)); /* nothing works after an error */
    CHECK(!sk_jr_done(&r));
    sk_jr_free(&r);
}

static const char *formatted(double d)
{
    static char out[64];
    sk_buf b = {0};
    sk_json_format_number(&b, d);
    snprintf(out, sizeof out, "%s", b.ptr);
    sk_buf_free(&b);
    return out;
}

/* What String(d), and so JSON.stringify, gives in JavaScript. */
static void test_numbers_like_javascript(void)
{
    struct {
        double d;
        const char *js;
    } cases[] = {
        {0, "0"},
        {-0.0, "0"},
        {1, "1"},
        {-1, "-1"},
        {0.1, "0.1"},
        {1.5, "1.5"},
        {100, "100"},
        {2.5, "2.5"},
        {123456.789, "123456.789"},
        {0.000123, "0.000123"},
        {1e21, "1e+21"},
        {1e20, "100000000000000000000"},
        {1.2345678901234568e20, "123456789012345680000"},
        {1e-6, "0.000001"},
        {1e-7, "1e-7"},
        {1.5e-7, "1.5e-7"},
        {123e-20, "1.23e-18"},
        {0.30000000000000004, "0.30000000000000004"},
        {9007199254740992.0, "9007199254740992"},
        {1.0 / 3, "0.3333333333333333"},
        {5e-324, "5e-324"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
        {-2.5e-10, "-2.5e-10"},
        {NAN, "null"},
        {INFINITY, "null"},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++)
        CHECK_STR(formatted(cases[i].d), cases[i].js);
}

static void test_writer_like_stringify(void)
{
    sk_buf out = {0};
    sk_jw w;
    /* JSON.stringify(settings, null, 1), the text the settings files are
     * named for. */
    sk_jw_init(&w, &out, 1);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("hooks"));
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("Stop"));
    sk_jw_array(&w);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("hooks"));
    sk_jw_array(&w);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("type"));
    sk_jw_string(&w, SK_STR("command"));
    sk_jw_key(&w, SK_STR("command"));
    sk_jw_string(&w, SK_STR("'/usr/bin/x' 'a b'"));
    sk_jw_key(&w, SK_STR("timeout"));
    sk_jw_int(&w, 1);
    sk_jw_object_end(&w);
    sk_jw_array_end(&w);
    sk_jw_object_end(&w);
    sk_jw_array_end(&w);
    sk_jw_object_end(&w);
    sk_jw_key(&w, SK_STR("statusLine"));
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("padding"));
    sk_jw_number(&w, 0);
    sk_jw_key(&w, SK_STR("empty"));
    sk_jw_array(&w);
    sk_jw_array_end(&w);
    sk_jw_key(&w, SK_STR("none"));
    sk_jw_object(&w);
    sk_jw_object_end(&w);
    sk_jw_object_end(&w);
    sk_jw_object_end(&w);
    CHECK_STR(out.ptr, "{\n"
                       " \"hooks\": {\n"
                       "  \"Stop\": [\n"
                       "   {\n"
                       "    \"hooks\": [\n"
                       "     {\n"
                       "      \"type\": \"command\",\n"
                       "      \"command\": \"'/usr/bin/x' 'a b'\",\n"
                       "      \"timeout\": 1\n"
                       "     }\n"
                       "    ]\n"
                       "   }\n"
                       "  ]\n"
                       " },\n"
                       " \"statusLine\": {\n"
                       "  \"padding\": 0,\n"
                       "  \"empty\": [],\n"
                       "  \"none\": {}\n"
                       " }\n"
                       "}");
    sk_buf_free(&out);

    /* JSON.stringify("a\"b\\c\n\u0001\u007f€😀\ud800") */
    sk_jw_init(&w, &out, 0);
    sk_jw_string(&w, SK_STR("a\"b\\c\n\x01\x7F\xE2\x82\xAC\xF0\x9F\x98\x80\xFF"));
    CHECK_STR(out.ptr, "\"a\\\"b\\\\c\\n\\u0001\x7F\xE2\x82\xAC\xF0\x9F\x98\x80\xEF\xBF\xBD\"");
    sk_buf_free(&out);

    sk_jw_init(&w, &out, 0);
    sk_jw_array(&w);
    sk_jw_int(&w, 1);
    sk_jw_bool(&w, true);
    sk_jw_null(&w);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("k"));
    sk_jw_number(&w, 0.5);
    sk_jw_object_end(&w);
    sk_jw_array_end(&w);
    CHECK_STR(out.ptr, "[1,true,null,{\"k\":0.5}]");
    sk_buf_free(&out);
}

static bool same(const sk_json *a, const sk_json *b)
{
    if (a->type == SK_JSON_NUMBER && !isfinite(a->number))
        return b->type == SK_JSON_NULL; /* stringify writes null */
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

/* Every line of the recorded fixtures parses, and survives being written
 * out and read back, compact and indented. */
static void test_fixtures_round_trip(void)
{
    const char *files[] = {
        "test/fixtures/claude/hooks.jsonl",   "test/fixtures/claude/statusline.json",
        "test/fixtures/claude/transcript-read.jsonl", "test/fixtures/codex/rollout.jsonl",
        "test/fixtures/codex/limits.jsonl",
    };
    size_t lines = 0;
    for (size_t f = 0; f < SK_LEN(files); f++) {
        size_t len;
        char *text = sk_test_read(files[f], &len);
        CHECK(text != NULL);
        if (!text)
            continue;
        sk_str rest = sk_str_n(text, len);
        while (rest.len) {
            size_t nl = sk_str_find(rest, '\n');
            sk_str line = sk_str_sub(rest, 0, nl);
            rest = sk_str_sub(rest, nl == SIZE_MAX ? rest.len : nl + 1, SIZE_MAX);
            if (!sk_str_trim(line).len)
                continue;
            sk_arena a;
            sk_arena_init(&a, 0);
            sk_json_error err;
            sk_json *v = sk_json_parse(&a, line.ptr, line.len, &err);
            if (!v) {
                sk_test_fail(__FILE__, __LINE__, "%s line %zu: %s at %zu", files[f], lines,
                             err.message, err.offset);
            } else {
                for (int indent = 0; indent <= 2; indent++) {
                    sk_buf out = {0};
                    sk_jw w;
                    sk_jw_init(&w, &out, indent);
                    sk_jw_value(&w, v);
                    sk_json *back = sk_json_parse(&a, out.ptr, out.len, NULL);
                    CHECK(back && same(v, back));
                    sk_buf_free(&out);
                }
            }
            lines++;
            sk_arena_free(&a);
        }
        free(text);
    }
    CHECK(lines >= 470);
}

const sk_test sk_suite_json[] = {
    T(test_scalars),
    T(test_strings),
    T(test_rejects),
    T(test_depth_cap),
    T(test_tree_access),
    T(test_reader_picks_and_skips),
    T(test_reader_errors_stick),
    T(test_numbers_like_javascript),
    T(test_writer_like_stringify),
    T(test_fixtures_round_trip),
    {0},
};
