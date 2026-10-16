/* The last skin used (decision 0022), as app/skins/last.js keeps it:
 * one record a window, read newest first, picked, pruned and forgotten. */
#include "../../src/app/lastskin.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char *dir_in(const char *name)
{
    return sk_path_join(sk_test_tmpdir(), name);
}

static void write_file(const char *dir, const char *name, const char *text)
{
    char *p = sk_path_join(dir, name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
    free(p);
}

static void test_record_and_read(void)
{
    char *d = dir_in("last-skins");
    CHECK_INT(sk_last_write(d, 101, "deck", 1000), 0);
    CHECK_INT(sk_last_write(d, 102, "/home/u/skins/mine", 3000), 0);
    CHECK_INT(sk_last_write(d, 103, "coffee", 2000), 0);
    /* A window's next record replaces its last. */
    CHECK_INT(sk_last_write(d, 101, "library", 4000), 0);
    char *f = sk_path_join(d, "101");
    char *text = sk_test_read(f, NULL);
    CHECK_STR(text, "{\"t\":4000,\"spec\":\"library\"}\n");
    free(text);
    free(f);
    /* What is not a record is skipped. */
    write_file(d, ".104.tmp", "{\"t\":9000,\"spec\":\"phosphor\"}\n");
    write_file(d, "105", "not json");
    write_file(d, "106", "{\"t\":\"late\",\"spec\":\"deck\"}");
    write_file(d, "107", "{\"t\":9000,\"spec\":\"\"}");
    sk_last_records r = {0};
    sk_last_read(d, &r);
    CHECK_INT(r.len, 3);
    if (r.len == 3) {
        CHECK_STR(r.ptr[0].spec, "library");
        CHECK_INT(r.ptr[0].pid, 101);
        CHECK_STR(r.ptr[1].spec, "/home/u/skins/mine");
        CHECK_STR(r.ptr[2].spec, "coffee");
    }
    sk_last_records_free(&r);
    free(d);
}

static void test_pick(void)
{
    sk_last_records r = {0};
    const char *spec = NULL;
    sk_strv choices = {0};
    CHECK(!sk_last_pick(&r, SK_LAST_RACE_MS, &spec, &choices));
    sk_last_record a = {1, 10000, sk_xstrdup("deck")}, b = {2, 9000, sk_xstrdup("deck")},
                   c = {3, 4000, sk_xstrdup("coffee")};
    sk_vec_push(&r, a);
    sk_vec_push(&r, b);
    sk_vec_push(&r, c);
    /* coffee is more than 5 s older than the newest: no race. */
    CHECK(sk_last_pick(&r, SK_LAST_RACE_MS, &spec, &choices));
    CHECK_STR(spec, "deck");
    CHECK_INT(choices.len, 0);
    /* Within 5 s, two skins: asked, the newest first, each once. */
    r.ptr[2].t = 5000;
    CHECK(sk_last_pick(&r, SK_LAST_RACE_MS, &spec, &choices));
    CHECK(spec == NULL);
    CHECK_INT(choices.len, 2);
    if (choices.len == 2) {
        CHECK_STR(choices.ptr[0], "deck");
        CHECK_STR(choices.ptr[1], "coffee");
    }
    sk_strv_free(&choices);
    sk_last_records_free(&r);
}

static void test_prune_and_forget(void)
{
    char *d = dir_in("last-skins");
    sk_last_write(d, 1, "deck", 20000);
    sk_last_write(d, 2, "coffee", 16000);
    sk_last_write(d, 3, "library", 1000);
    sk_last_records r = {0};
    sk_last_read(d, &r);
    sk_last_prune(d, &r, SK_LAST_RACE_MS);
    sk_last_records_free(&r);
    sk_last_read(d, &r);
    CHECK_INT(r.len, 2);
    sk_last_records_free(&r);
    CHECK_INT(sk_last_forget(d), 0);
    sk_last_read(d, &r);
    CHECK_INT(r.len, 0);
    sk_last_records_free(&r);
    /* A folder that is not there has nothing to forget. */
    char *none = dir_in("none");
    CHECK_INT(sk_last_forget(none), 0);
    free(none);
    free(d);
}

const sk_test sk_suite_lastskin[] = {
    T(test_record_and_read),
    T(test_pick),
    T(test_prune_and_forget),
    {0},
};
