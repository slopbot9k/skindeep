/* The manifest rule of decision 0017, read by the C JSON parser: every
 * entry names an existing file and an allowed licence, every file of
 * skins/ but a manifest and CSS is listed, and no shipped file names
 * another product, as test/assets.test.js had them. */
#include "../../src/base/json.h"
#include "../../src/base/strv.h"
#include "../test.h"

#include <ctype.h>
#include <dirent.h>
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

/* Every file under dir, as paths from the repository's root, into out. */
static void walk(const char *dir, sk_strv *out)
{
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    for (struct dirent *e; d && (e = readdir(d));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        sk_buf p = {0};
        sk_buf_printf(&p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p.ptr, &st) == 0 && S_ISDIR(st.st_mode))
            walk(p.ptr, out);
        else
            sk_strv_push(out, p.ptr);
        sk_buf_free(&p);
    }
    if (d)
        closedir(d);
}

static bool ends_with(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    return n >= m && !strcmp(s + n - m, end);
}

static void test_every_skin_file_is_listed(void)
{
    size_t len;
    char *text = sk_test_read("ASSETS.json", &len);
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_json *list = text ? sk_json_parse(&a, text, len, NULL) : NULL;
    CHECK(list && list->type == SK_JSON_ARRAY);
    sk_strv files = {0};
    walk("skins", &files);
    CHECK(files.len > 0);
    for (size_t i = 0; i < files.len; i++) {
        const char *f = files.ptr[i], *slash = strrchr(f, '/');
        if (!strcmp(slash ? slash + 1 : f, "skin.json") || ends_with(f, ".css"))
            continue;
        bool listed = false;
        for (size_t k = 0; list && list->type == SK_JSON_ARRAY && k < list->array.len && !listed; k++) {
            sk_str p = {0};
            listed = sk_json_as_string(sk_json_get(&list->array.items[k], "path"), &p) && sk_str_eq_c(p, f);
        }
        if (!listed)
            sk_test_fail(__FILE__, __LINE__, "%s: not listed in ASSETS.json", f);
    }
    sk_strv_free(&files);
    sk_arena_free(&a);
    free(text);
}

/* s, lowercased, holds one of the names, the hyphen in each optional. */
static bool names_another(const char *s, bool text)
{
    static const char *const PATH[] = {"winamp", "pipboy", "pip-boy", "fallout", "vault"};
    static const char *const TEXT[] = {"winamp", "pipboy", "pip-boy", "fallout", "vaulttec", "vault-tec"};
    char *low = sk_xstrdup(s);
    for (char *p = low; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    const char *const *names = text ? TEXT : PATH;
    size_t n = text ? SK_LEN(TEXT) : SK_LEN(PATH);
    bool hit = false;
    for (size_t i = 0; i < n && !hit; i++)
        hit = strstr(low, names[i]) != NULL;
    free(low);
    return hit;
}

static void test_no_shipped_file_names_another_product(void)
{
    sk_strv files = {0};
    walk("skins", &files);
    for (size_t i = 0; i < files.len; i++) {
        const char *f = files.ptr[i];
        if (names_another(f, false))
            sk_test_fail(__FILE__, __LINE__, "%s: its name names another product", f);
        if (!ends_with(f, ".json") && !ends_with(f, ".css") && !ends_with(f, ".txt") && !ends_with(f, ".md"))
            continue;
        char *text = sk_test_read(f, NULL);
        if (text && names_another(text, true))
            sk_test_fail(__FILE__, __LINE__, "%s names another product", f);
        free(text);
    }
    sk_strv_free(&files);
    CHECK(names_another("a Fallout Pip-Boy", true));
    CHECK(!names_another("deck of cards", true));
}

const sk_test sk_suite_assets[] = {
    T(test_manifest),
    T(test_every_skin_file_is_listed),
    T(test_no_shipped_file_names_another_product),
    {0},
};
