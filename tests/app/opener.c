/* The links the system's opener may be given (opener.h): http, https
 * and file URLs, a file URL's path decoded, and never a program. */
#include "../../src/app/opener.h"
#include "../../src/base/paths.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static void check_path(const char *uri, const char *want)
{
    sk_buf b = {0};
    bool ok = sk_opener_file_path(uri, &b);
    if (want) {
        CHECK(ok);
        CHECK_STR(sk_buf_cstr(&b), want);
    } else {
        CHECK(!ok);
    }
    sk_buf_free(&b);
}

static void test_file_paths(void)
{
    check_path("file:///tmp/a", "/tmp/a");
    check_path("FILE://localhost/tmp/a%20b", "/tmp/a b");
    check_path("file:///tmp/caf%C3%A9", "/tmp/caf\xc3\xa9");
    check_path("file://other/tmp/a", NULL);
    check_path("file:/tmp/a", NULL);
    check_path("file:///tmp/a%2", NULL);
    check_path("file:///tmp/a%zz", NULL);
    check_path("file:///tmp/a%00b", NULL);
    check_path("file:///tmp/a#top", NULL);
    check_path("http://x/", NULL);
}

static char *file_at(const char *name, mode_t mode)
{
    char *p = sk_path_join(sk_test_tmpdir(), name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("x\n", f);
        fclose(f);
    }
    chmod(p, mode);
    return p;
}

static bool allows(const char *uri)
{
    const char *why = NULL;
    bool ok = sk_opener_allows(uri, &why);
    CHECK(ok == (why == NULL));
    return ok;
}

static void test_what_opens(void)
{
    CHECK(allows("https://example.org/a"));
    CHECK(allows("HTTP://example.org/a"));
    CHECK(!allows("ftp://example.org/a"));
    CHECK(!allows("javascript:alert(1)"));
    CHECK(!allows("file://other/etc/passwd"));
    /* A document, a folder, a file not there yet: opened. */
    char *doc = file_at("notes.txt", 0644), *prog = file_at("run.sh", 0755);
    char *launcher = file_at("x.desktop", 0644), *settings = file_at("x.terminal", 0644);
    char *app = sk_path_join(sk_test_tmpdir(), "Some.app");
    CHECK_INT(sk_mkdirs(app, 0755), 0);
    sk_buf uri = {0};
    sk_buf_printf(&uri, "file://%s", doc);
    CHECK(allows(uri.ptr));
    sk_buf_clear(&uri);
    sk_buf_printf(&uri, "file://%s/", sk_test_tmpdir());
    CHECK(allows(uri.ptr));
    sk_buf_clear(&uri);
    sk_buf_printf(&uri, "file://%s/later.txt", sk_test_tmpdir());
    CHECK(allows(uri.ptr));
    /* A program, an application, a launcher: never. */
    const char *const no[] = {prog, app, launcher, settings};
    for (size_t i = 0; i < SK_LEN(no); i++) {
        sk_buf_clear(&uri);
        sk_buf_printf(&uri, "file://%s", no[i]);
        CHECK(!allows(uri.ptr));
        sk_buf_append_c(&uri, "/");
        CHECK(!allows(uri.ptr));
    }
    sk_buf_free(&uri);
    free(doc);
    free(prog);
    free(launcher);
    free(settings);
    free(app);
}

const sk_test sk_suite_opener[] = {
    T(test_file_paths),
    T(test_what_opens),
    {0},
};
