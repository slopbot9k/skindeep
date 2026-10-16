/* The manifest rule of decision 0017, read by the C JSON parser: every
 * entry names an existing file and an allowed licence. The skins/ walk
 * stays with test/assets.test.js until the cutover. */
#include "../../src/base/json.h"
#include "../test.h"

#include <stdlib.h>
#include <sys/stat.h>

static const char *const ALLOWED[] = {
    "CC0-1.0",   "OFL-1.1",   "MIT",       "BSD-2-Clause", "BSD-3-Clause",  "Apache-2.0",
    "Zlib",      "Unlicense", "CC-BY-3.0", "CC-BY-4.0",    "public-domain", "project-original",
};

/* The Bitstream Vera license, for the embedded DejaVu alone (decision
 * 0017). */
static bool allowed(sk_str licence, sk_str path)
{
    for (size_t i = 0; i < SK_LEN(ALLOWED); i++)
        if (sk_str_eq_c(licence, ALLOWED[i]))
            return true;
    return sk_str_eq_c(licence, "Bitstream-Vera") && sk_str_prefix(path, SK_STR("third_party/dejavu/"));
}

static void test_manifest(void)
{
    size_t len;
    char *text = sk_test_read("ASSETS.json", &len);
    CHECK(text != NULL);
    if (!text)
        return;
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_json_error err;
    sk_json *list = sk_json_parse(&a, text, len, &err);
    if (!list)
        sk_test_fail(__FILE__, __LINE__, "ASSETS.json: %s at %zu", err.message, err.offset);
    CHECK(list && list->type == SK_JSON_ARRAY && list->array.len > 0);
    size_t third_party = 0;
    for (size_t i = 0; list && i < list->array.len; i++) {
        const sk_json *e = &list->array.items[i];
        sk_str path = {0}, s;
        CHECK(sk_json_as_string(sk_json_get(e, "path"), &path));
        const char *const needed[] = {"source", "author"};
        for (size_t k = 0; k < SK_LEN(needed); k++)
            if (!sk_json_as_string(sk_json_get(e, needed[k]), &s) || !s.len)
                sk_test_fail(__FILE__, __LINE__, "%s: %s missing", path.ptr, needed[k]);
        if (!sk_json_as_string(sk_json_get(e, "license"), &s) || !allowed(s, path))
            sk_test_fail(__FILE__, __LINE__, "%s: licence not allowed", path.ptr);
        struct stat st;
        if (path.ptr && stat(path.ptr, &st) != 0)
            sk_test_fail(__FILE__, __LINE__, "%s: listed but missing", path.ptr);
        if (path.ptr && sk_str_prefix(path, SK_STR("third_party/")))
            third_party++;
    }
    CHECK_INT(third_party, 18);
    sk_arena_free(&a);
    free(text);
}

const sk_test sk_suite_assets[] = {
    T(test_manifest),
    {0},
};
