/* The skins a window can show (src/app/skins.h), as registry.js and
 * main.js's start keep them: the shipped skins in order, the default,
 * a name and a folder, a skin opened by path compiled by skinc and
 * replacing a shipped skin of its name until the shipped folder is
 * opened again, the spec and the last-skin record, a remembered skin
 * that no longer loads giving way to the default with a problem; and a
 * skin's sounds handed to decision 0024's player. */
#include "../../src/app/lastskin.h"
#include "../../src/app/skins.h"
#include "../../src/base/paths.h"
#include "../skinc/util.h"
#include "../test.h"

#include <string.h>
#include <unistd.h>

static const char *skinc(void)
{
    const char *b = getenv("SK_SKINC_BIN");
    if (!b || access(b, X_OK) != 0) {
        printf("    (skipped: SK_SKINC_BIN not set)\n");
        return NULL;
    }
    return b;
}

static sk_skins *registry(const char *bin)
{
    char *cache = sk_path_join(sk_test_tmpdir(), "cache");
    sk_skins_opts o = {bin, cache, "skins", "/home/someone"};
    sk_skins *r = sk_skins_new(&o);
    free(cache);
    return r;
}

/* A skin named name in a folder of its own under the test's directory. */
static char *skin_named(const char *name, const char *folder)
{
    const char *made = sk_test_skin(NULL, NULL, ".skin { background: #102030 }\n");
    char *dir = sk_path_join(sk_test_tmpdir(), folder);
    CHECK_INT(rename(made, dir), 0);
    char *json = sk_path_join(dir, "skin.json");
    char *text = sk_test_read(json, NULL);
    char *at = text ? strstr(text, "\"name\": \"test\"") : NULL;
    CHECK(at != NULL);
    if (at) {
        sk_buf b = {0};
        sk_buf_append(&b, text, (size_t)(at - text));
        sk_buf_printf(&b, "\"name\": \"%s\"", name);
        sk_buf_append_c(&b, at + strlen("\"name\": \"test\""));
        sk_test_write(dir, "skin.json", sk_buf_cstr(&b));
        sk_buf_free(&b);
    }
    free(text);
    free(json);
    return dir;
}

static void test_shipped_and_default(void)
{
    sk_skins *r = registry(NULL);
    static const char *const ids[] = {"coffee", "deck", "library", "phosphor"};
    CHECK_INT(sk_skins_count(r), 4);
    for (size_t i = 0; i < 4; i++) {
        sk_skins_entry e = sk_skins_at(r, i);
        CHECK_STR(e.id, ids[i]);
        CHECK(!e.location && !e.current);
    }
    CHECK(sk_skins_current(r) == NULL);
    CHECK(sk_skins_spec(r) == NULL);
    sk_buf err = {0};
    CHECK(sk_skins_start(r, NULL, &err));
    CHECK_STR(sk_skins_current_id(r), "deck");
    CHECK_STR(sk_skinpack_name(sk_skins_current(r)), "deck");
    CHECK_STR(sk_skins_spec(r), "deck");
    CHECK(sk_skins_at(r, 1).current && !sk_skins_at(r, 0).current);
    /* By name; a name not shipped refused, the current skin kept. */
    CHECK(sk_skins_start(r, "coffee", &err));
    CHECK_STR(sk_skins_current_id(r), "coffee");
    CHECK(!sk_skins_start(r, "nonesuch", &err));
    CHECK_STR(sk_buf_cstr(&err), "skins/nonesuch: skin: directory not found\n");
    CHECK_STR(sk_skins_current_id(r), "coffee");
    sk_buf_clear(&err);
    CHECK(sk_skins_select(r, "library", &err));
    CHECK_STR(sk_skins_current_id(r), "library");
    CHECK(!sk_skins_select(r, "nope", &err));
    CHECK_STR(sk_buf_cstr(&err), "no skin with id nope\n");
    /* A window keeps its pack when the registry moves on. */
    sk_skinpack *kept = sk_skinpack_ref(sk_skins_current(r));
    CHECK(sk_skins_select(r, "phosphor", &err));
    CHECK_STR(sk_skinpack_name(kept), "library");
    sk_skinpack_unref(kept);
    /* The shipped folder of a shipped skin is that skin, with or
     * without skinc. */
    CHECK(sk_skins_open(r, "skins/deck/../deck", &err));
    CHECK_STR(sk_skins_current_id(r), "deck");
    CHECK(sk_skins_at(r, 1).location == NULL);
    /* Without skinc another folder cannot compile, and says so. */
    char *mine = skin_named("mine", "mine");
    sk_buf_clear(&err);
    CHECK(!sk_skins_open(r, mine, &err));
    CHECK(strstr(sk_buf_cstr(&err), "skinc") != NULL);
    CHECK_STR(sk_skins_current_id(r), "deck");
    free(mine);
    sk_buf_free(&err);
    sk_skins_free(r);
}

static void test_open_by_path(void)
{
    const char *bin = skinc();
    if (!bin)
        return;
    sk_skins *r = registry(bin);
    sk_buf err = {0};
    CHECK(sk_skins_start(r, NULL, &err));
    /* A skin of a new name: listed after the shipped, with its folder. */
    char *mine = skin_named("mine", "mine");
    char *real = realpath(mine, NULL);
    CHECK(sk_skins_open(r, mine, &err));
    CHECK_STR(sk_buf_cstr(&err), "");
    CHECK_INT(sk_skins_count(r), 5);
    sk_skins_entry e = sk_skins_at(r, 4);
    CHECK_STR(e.id, "mine");
    CHECK_STR(e.location, real);
    CHECK(e.current);
    CHECK_STR(sk_skins_spec(r), real);
    /* A start by its folder selects it again. */
    CHECK(sk_skins_start(r, "deck", &err));
    CHECK(sk_skins_start(r, real, &err));
    CHECK_STR(sk_skins_current_id(r), "mine");
    /* A skin named as a shipped one replaces it for the run. */
    char *copy = skin_named("coffee", "my-coffee");
    char *copy_real = realpath(copy, NULL);
    CHECK(sk_skins_open(r, copy, &err));
    CHECK_INT(sk_skins_count(r), 5);
    e = sk_skins_at(r, 0);
    CHECK_STR(e.id, "coffee");
    CHECK_STR(e.location, copy_real);
    CHECK(e.current);
    CHECK_STR(sk_skins_spec(r), copy_real);
    /* Selecting it by id compiles its folder again: the copy, not the
     * shipped skin (a 1000 x 700 window, not coffee's 1240 x 820). */
    CHECK(sk_skins_select(r, "coffee", &err));
    sk_skin_window w;
    sk_skinpack_window(sk_skins_current(r), &w);
    CHECK_INT(w.width, 1000);
    /* The shipped folder itself restores the shipped skin. */
    CHECK(sk_skins_open(r, "skins/coffee", &err));
    e = sk_skins_at(r, 0);
    CHECK(e.location == NULL && e.current);
    CHECK_STR(sk_skins_spec(r), "coffee");
    sk_skinpack_window(sk_skins_current(r), &w);
    CHECK_INT(w.width, 1240);
    /* A folder that does not compile: skinc's messages, the skin kept. */
    char *bad = skin_named("bad", "bad");
    sk_test_write(bad, "skin.css", ".x { colour: red }\n");
    sk_buf_clear(&err);
    CHECK(!sk_skins_open(r, bad, &err));
    CHECK(strstr(sk_buf_cstr(&err), "skin.css") != NULL);
    CHECK_STR(sk_skins_current_id(r), "coffee");
    CHECK(!sk_skins_open(r, "/nonexistent/skin", &err));
    sk_buf_free(&err);
    free(bad);
    free(copy);
    free(copy_real);
    free(mine);
    free(real);
    sk_skins_free(r);
}

static void test_records(void)
{
    sk_skins *r = registry(NULL);
    char *dir = sk_path_join(sk_test_tmpdir(), "last-skins");
    sk_buf err = {0};
    /* Nothing current: nothing to record. */
    CHECK_INT(sk_skins_remember(r, dir, 4242, 1000), -1);
    CHECK(sk_skins_start(r, "library", &err));
    CHECK_INT(sk_skins_remember(r, dir, 4242, 1000), 0);
    CHECK(sk_skins_select(r, "phosphor", &err));
    CHECK_INT(sk_skins_remember(r, dir, 4242, 2000), 0);
    CHECK_INT(sk_skins_remember(r, dir, 4343, 1500), 0);
    sk_last_records recs = {0};
    sk_last_read(dir, &recs);
    CHECK_INT(recs.len, 2);
    if (recs.len == 2) {
        CHECK_STR(recs.ptr[0].spec, "phosphor");
        CHECK_INT(recs.ptr[0].pid, 4242);
    }
    const char *spec = NULL;
    sk_strv choices = {0};
    CHECK(sk_last_pick(&recs, SK_LAST_RACE_MS, &spec, &choices));
    CHECK_STR(spec, "phosphor");
    sk_strv_free(&choices);
    sk_last_records_free(&recs);
    sk_buf_free(&err);
    free(dir);
    sk_skins_free(r);
}

static void test_remembered(void)
{
    sk_skins *r = registry(NULL);
    sk_buf err = {0};
    char *title, *detail;
    /* What loads opens, with no problem. */
    CHECK(sk_skins_start_remembered(r, "coffee", &title, &detail, &err));
    CHECK(!title && !detail);
    CHECK_STR(sk_skins_current_id(r), "coffee");
    /* What no longer loads gives way to the default, said once. */
    CHECK(sk_skins_start_remembered(r, "/home/someone/skins/gone", &title, &detail, &err));
    CHECK_STR(title, "Remembered skin ~/skins/gone refused, using the default");
    CHECK_STR(detail, "/home/someone/skins/gone: skin: directory not found");
    CHECK_STR(sk_skins_current_id(r), "deck");
    CHECK_STR(sk_buf_cstr(&err), "");
    free(title);
    free(detail);
    CHECK(sk_skins_start_remembered(r, "retired", &title, &detail, &err));
    CHECK_STR(title, "Remembered skin retired refused, using the default");
    CHECK_STR(detail, "skins/retired: skin: directory not found");
    free(title);
    free(detail);
    /* No record: the default. */
    CHECK(sk_skins_start_remembered(r, NULL, &title, &detail, &err));
    CHECK(!title && !detail);
    sk_buf_free(&err);
    sk_skins_free(r);
}

/* ---- Sounds ---- */

static uint64_t fake_now;

static uint64_t fake_clock(void *ud)
{
    (void)ud;
    return fake_now;
}

static void test_sounds(void)
{
    sk_skins *r = registry(NULL);
    sk_buf err = {0};
    CHECK(sk_skins_start(r, "coffee", &err));
    sk_sounds_entry e[SK_SOUNDS_MAX];
    size_t n = sk_skinpack_sounds(sk_skins_current(r), e, SK_SOUNDS_MAX);
    /* coffee's eleven: the room's murmur looped, the rest at random. */
    CHECK_INT(n, 11);
    CHECK(e[0].loop && e[0].pcm && e[0].volume == 0.5f);
    for (size_t i = 1; i < n; i++)
        CHECK(!e[i].loop && e[i].pcm && e[i].interval[0] >= 1 && e[i].interval[0] <= e[i].interval[1]);
    CHECK(e[1].interval[0] == 25 && e[1].interval[1] == 90);
    CHECK(sk_pcm_rate(e[0].pcm) >= SK_PCM_MIN_RATE && sk_pcm_frames(e[0].pcm) > 0);
    /* The player: muted until told otherwise, then the loop plays and
     * the random sounds wait their intervals. */
    fake_now = 1000;
    sk_mixer *m = sk_mixer_new(SK_MIXER_RATE);
    sk_sounds *s = sk_sounds_new(m, fake_clock, NULL, 7);
    sk_sounds_set(s, e, n);
    sk_skin_sounds_release(e, n);
    CHECK_INT(sk_mixer_live(m), 0);
    sk_sounds_mute(s, false);
    CHECK_INT(sk_sounds_count(s), 11);
    sk_sounds_state st = sk_sounds_get(s, 0);
    CHECK(st.loop && st.playing && !st.waiting);
    CHECK(st.volume == 0.25f);
    for (size_t i = 1; i < 11; i++) {
        st = sk_sounds_get(s, i);
        CHECK(!st.playing && st.waiting);
    }
    st = sk_sounds_get(s, 1);
    CHECK(st.due >= 1000 + 25000 && st.due <= 1000 + 90000);
    /* Muted: nothing plays, nothing waits. */
    sk_sounds_mute(s, true);
    for (size_t i = 0; i < 11; i++) {
        st = sk_sounds_get(s, i);
        CHECK(!st.playing && !st.waiting);
    }
    sk_sounds_free(s);
    sk_mixer_free(m);
    sk_buf_free(&err);
    sk_skins_free(r);
}

const sk_test sk_suite_skins[] = {
    T(test_shipped_and_default),
    T(test_open_by_path),
    T(test_records),
    T(test_remembered),
    T(test_sounds),
    {0},
};
