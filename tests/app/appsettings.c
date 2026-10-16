/* The user's settings (decisions 0022, 0035), as test/settings.test.js
 * has them, with the file's bytes and key order beside. */
#include "../../src/app/appsettings.h"
#include "../test.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static char *file_in(const char *rel)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "%s/%s", sk_test_tmpdir(), rel);
    return b.ptr;
}

static void put(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd);
}

/* The values as compact JSON, to compare whole. */
static char *json_of(const sk_appsettings *v)
{
    sk_buf pretty = {0}, out = {0};
    sk_appsettings_write(v, &pretty);
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *j = sk_json_parse(&a, pretty.ptr, pretty.len, NULL);
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_jw_value(&w, j);
    sk_arena_free(&a);
    sk_buf_free(&pretty);
    return out.ptr;
}

#define CHECK_LOADED(file, want)                                             \
    do {                                                                     \
        sk_appsettings v_;                                                   \
        sk_appsettings_load(&v_, file);                                      \
        char *j_ = json_of(&v_);                                             \
        CHECK_STR(j_, want);                                                 \
        free(j_);                                                            \
        sk_appsettings_free(&v_);                                            \
    } while (0)

static void test_a_missing_or_broken_file_and_any_value_outside_a_settings_list_read_as_the_skin_default(void)
{
    char *f = file_in("settings.json");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    put(f, "{not json");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    put(f, "{\"terminal.fontSize\":19,\"other\":3}");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    put(f, "{\"terminal.fontSize\":\"14\"}");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    put(f, "[14]");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    put(f, "null");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    put(f, "{\"terminal.fontSize\":14.0}");
    CHECK_LOADED(f, "{\"terminal.fontSize\":14,\"panels\":{}}");
    free(f);
}

static void test_a_saved_setting_reads_back_and_the_menu_marks_it_beside_the_skins_own_value(void)
{
    char *f = file_in("deeper/settings.json");
    sk_appsettings v = {0};
    v.set[SK_SETTING_FONT_SIZE] = true;
    v.value[SK_SETTING_FONT_SIZE] = 20;
    CHECK_INT(sk_appsettings_save(f, &v), 0);
    sk_appsettings_free(&v);
    sk_appsettings_load(&v, f);
    char *j = json_of(&v);
    CHECK_STR(j, "{\"terminal.fontSize\":20,\"panels\":{}}");
    free(j);
    char *text = sk_test_read(f, NULL);
    CHECK_STR(text, "{\n \"terminal.fontSize\": 20,\n \"panels\": {}\n}\n");
    free(text);

    sk_arena a;
    sk_arena_init(&a, 0);
    const char *manifest = "{\"terminal\":{\"fontSize\":19}}";
    const sk_json *skin = sk_json_parse(&a, manifest, strlen(manifest), NULL);
    sk_setting_row rows[SK_SETTINGS_COUNT];
    sk_appsettings_menu(&v, skin, rows);
    CHECK_STR(rows[0].setting->id, "terminal.fontSize");
    CHECK_STR(rows[0].setting->label, "Terminal font size");
    CHECK_STR(rows[0].choices.ptr[0].label, "Skin default (19 px)");
    CHECK(rows[0].choices.ptr[0].is_default);
    CHECK(!rows[0].choices.ptr[0].current);
    CHECK_INT(rows[0].choices.len, 19);
    size_t current = 0;
    for (size_t i = 0; i < rows[0].choices.len; i++) {
        if (rows[0].choices.ptr[i].current) {
            current++;
            CHECK(rows[0].choices.ptr[i].value == 20);
            CHECK_STR(rows[0].choices.ptr[i].label, "20 px");
        }
    }
    CHECK_INT(current, 1);
    sk_appsettings_menu_free(rows);
    sk_arena_free(&a);

    /* Only settings.json is left in its folder: the temp file is gone. */
    char *dir = file_in("deeper");
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t n = 0;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        CHECK_STR(e->d_name, "settings.json");
        n++;
    }
    if (d)
        closedir(d);
    CHECK_INT(n, 1);
    free(dir);
    sk_appsettings_free(&v);
    free(f);
}

static void test_the_menu_offers_the_fallback_without_a_skin_value(void)
{
    sk_appsettings v = {0};
    sk_setting_row rows[SK_SETTINGS_COUNT];
    sk_appsettings_menu(&v, NULL, rows);
    CHECK_STR(rows[0].choices.ptr[0].label, "Skin default (14 px)");
    CHECK(rows[0].choices.ptr[0].current);
    CHECK_STR(rows[0].choices.ptr[1].label, "8 px");
    CHECK_STR(rows[0].choices.ptr[18].label, "32 px");
    sk_appsettings_menu_free(rows);
    sk_arena a;
    sk_arena_init(&a, 0);
    const char *zero = "{\"terminal\":{\"fontSize\":0}}";
    sk_appsettings_menu(&v, sk_json_parse(&a, zero, strlen(zero), NULL), rows);
    CHECK_STR(rows[0].choices.ptr[0].label, "Skin default (14 px)");
    sk_appsettings_menu_free(rows);
    sk_arena_free(&a);
}

static void test_only_a_defined_setting_and_one_of_its_values_is_valid(void)
{
    sk_json eight = {.type = SK_JSON_NUMBER, .number = 8};
    sk_json max = {.type = SK_JSON_NUMBER, .number = 32};
    sk_json none = {.type = SK_JSON_NULL};
    sk_json over = {.type = SK_JSON_NUMBER, .number = 33};
    sk_json text = {.type = SK_JSON_STRING, .string = SK_STR("14")};
    CHECK(sk_appsettings_valid("terminal.fontSize", &eight));
    CHECK(sk_appsettings_valid("terminal.fontSize", &max));
    CHECK(sk_appsettings_valid("terminal.fontSize", &none));
    CHECK(!sk_appsettings_valid("terminal.fontSize", &over));
    CHECK(!sk_appsettings_valid("terminal.fontSize", &text));
    CHECK(!sk_appsettings_valid("skin.css", &none));
    CHECK(!sk_appsettings_valid("terminal.fontSize", NULL));
    sk_appsettings v = {0};
    CHECK(sk_appsettings_set(&v, "terminal.fontSize", &eight));
    CHECK(v.set[0] && v.value[0] == 8);
    CHECK(!sk_appsettings_set(&v, "terminal.fontSize", &over));
    CHECK(v.set[0] && v.value[0] == 8);
    CHECK(sk_appsettings_set(&v, "terminal.fontSize", &none));
    CHECK(!v.set[0]);
}

/* Panel sizes (decision 0035): kept by skin name and split id, a
 * malformed entry dropped, a skin's reset dropping only its own. */
static void test_panel_sizes_read_back_by_skin_and_split_and_a_reset_drops_one_skins(void)
{
    char *f = file_in("settings.json");
    sk_appsettings v;
    sk_appsettings_load(&v, f);
    sk_appsettings_set_panel(&v, "deck", "playlist", 412);
    sk_appsettings_set_panel(&v, "deck", "meters", 254);
    sk_appsettings_set_panel(&v, "coffee", "slate", 200);
    CHECK_INT(sk_appsettings_save(f, &v), 0);
    sk_appsettings_free(&v);
    sk_appsettings_load(&v, f);
    char *j = json_of(&v);
    CHECK_STR(j, "{\"terminal.fontSize\":null,\"panels\":{\"deck\":{\"playlist\":412,\"meters\":254},"
                 "\"coffee\":{\"slate\":200}}}");
    free(j);
    double size = 0;
    CHECK(sk_appsettings_panel(&v, "deck", "meters", &size) && size == 254);
    CHECK(!sk_appsettings_panel(&v, "deck", "nope", &size));
    sk_appsettings_set_panel(&v, "deck", NULL, 0);
    j = json_of(&v);
    CHECK_STR(j, "{\"terminal.fontSize\":null,\"panels\":{\"coffee\":{\"slate\":200}}}");
    free(j);
    sk_appsettings_free(&v);

    put(f, "{\"panels\":{\"deck\":{\"playlist\":\"412\",\"meters\":-1,\"ok\":300},\"Bad Name\":{\"a\":1},"
           "\"coffee\":[1],\"library\":{\"desk\":250}}}");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{\"deck\":{\"ok\":300},\"library\":{\"desk\":250}}}");
    put(f, "{\"panels\":\"wide\"}");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{}}");
    free(f);
}

/* Beyond the JavaScript tests: keys keep JavaScript's object order,
 * array indexes first and ascending, and an updated key its place. */
static void test_panels_keep_javascripts_key_order(void)
{
    sk_appsettings v = {0};
    sk_appsettings_set_panel(&v, "deck", "b", 1);
    sk_appsettings_set_panel(&v, "deck", "a", 2);
    sk_appsettings_set_panel(&v, "deck", "10", 3);
    sk_appsettings_set_panel(&v, "deck", "2", 4);
    sk_appsettings_set_panel(&v, "deck", "b", 5);
    sk_appsettings_set_panel(&v, "7", "x", 6);
    char *j = json_of(&v);
    CHECK_STR(j, "{\"terminal.fontSize\":null,\"panels\":{\"7\":{\"x\":6},"
                 "\"deck\":{\"2\":4,\"10\":3,\"b\":5,\"a\":2}}}");
    free(j);
    sk_appsettings_free(&v);
    char *f = file_in("ordered.json");
    put(f, "{\"panels\":{\"z\":{\"9\":1,\"1\":2},\"3\":{\"q\":0.5},\"z\":{\"k\":7}}}");
    CHECK_LOADED(f, "{\"terminal.fontSize\":null,\"panels\":{\"3\":{\"q\":0.5},\"z\":{\"k\":7}}}");
    free(f);
}

static void test_the_file_lives_in_the_state_dir(void)
{
    sk_env e = {SK_OS_LINUX, "/home/u", NULL, NULL, NULL, NULL, 1000};
    char *f = sk_appsettings_file(&e);
    CHECK_STR(f, "/home/u/.local/state/claude-skins/settings.json");
    free(f);
    e.xdg_state_home = "/s";
    f = sk_appsettings_file(&e);
    CHECK_STR(f, "/s/claude-skins/settings.json");
    free(f);
    CHECK(sk_appsettings_size_ok(0) && sk_appsettings_size_ok(412.5));
    CHECK(!sk_appsettings_size_ok(-1));
}

const sk_test sk_suite_appsettings[] = {
    T(test_a_missing_or_broken_file_and_any_value_outside_a_settings_list_read_as_the_skin_default),
    T(test_a_saved_setting_reads_back_and_the_menu_marks_it_beside_the_skins_own_value),
    T(test_the_menu_offers_the_fallback_without_a_skin_value),
    T(test_only_a_defined_setting_and_one_of_its_values_is_valid),
    T(test_panel_sizes_read_back_by_skin_and_split_and_a_reset_drops_one_skins),
    T(test_panels_keep_javascripts_key_order),
    T(test_the_file_lives_in_the_state_dir),
    {0},
};
