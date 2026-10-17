/* app/skins/validate.js's rules in skinc: test/skin-validate.test.js's
 * cases over the same fixtures, the same messages in the same order,
 * then rules no fixture shows, each message as validate.js writes it. */
#include "../../src/base/paths.h"
#include "../test.h"
#include "util.h"

#include <dirent.h>
#include <string.h>

#define F "tests/fixtures/skins"

static sk_diags run(const char *dir, bool *ok)
{
    sk_diags d = {0};
    sk_front f;
    *ok = sk_front_run(&f, dir, &d);
    sk_front_free(&f);
    return d;
}

static void test_all_widgets_is_valid(void)
{
    bool ok;
    sk_diags d = run(F "/all-widgets", &ok);
    CHECK(ok);
    CHECK_INT(d.items.len, 0);
    sk_skin s;
    sk_diags e = {0};
    CHECK(sk_skin_load(&s, F "/all-widgets", &e, NULL, NULL));
    CHECK_STR(s.name, "all-widgets");
    sk_skin_free(&s);
    sk_diags_free(&e);
    sk_diags_free(&d);
}

static void test_sounds_are_valid(void)
{
    sk_skin s;
    sk_diags d = {0};
    CHECK(sk_skin_load(&s, F "/sounds", &d, NULL, NULL));
    CHECK_INT(d.items.len, 0);
    CHECK_INT(s.nsounds, 2);
    CHECK_INT(s.sounds[0].mode, SK_SOUND_LOOP);
    CHECK_INT(s.sounds[1].mode, SK_SOUND_RANDOM);
    sk_skin_free(&s);
    sk_diags_free(&d);
}

static void test_every_shipped_skin_is_valid(void)
{
    DIR *dir = opendir("skins");
    CHECK(dir != NULL);
    int n = 0;
    struct dirent *e;
    while (dir && (e = readdir(dir))) {
        if (e->d_name[0] == '.')
            continue;
        char *path = sk_path_join("skins", e->d_name);
        sk_skin s;
        sk_diags d = {0};
        bool ok = sk_skin_load(&s, path, &d, NULL, NULL);
        if (!ok)
            sk_test_fail(__FILE__, __LINE__, "%s: %s", path, d.items.len ? d.items.ptr[0] : "?");
        /* A shipped skin's name is its directory's (decision 0008). */
        if (ok && strcmp(s.name, e->d_name) != 0)
            sk_test_fail(__FILE__, __LINE__, "%s: the manifest name must equal the directory name", e->d_name);
        sk_skin_free(&s);
        sk_diags_free(&d);
        free(path);
        n++;
    }
    if (dir)
        closedir(dir);
    CHECK_INT(n, 4);
}

typedef struct {
    const char *dir;
    const char *want[12];
} broken_case;

/* test/skin-validate.test.js's BROKEN, entry for entry. */
static const broken_case BROKEN[] = {
    {"broken-json", {"skin.json: (file): not valid JSON"}},
    {"broken-metric", {"skin.json: widgets[0].bind: unknown metric \"foo.bar\""}},
    {"broken-type", {"skin.json: widgets[0].type: unknown widget type \"slider\""}},
    {"broken-list",
     {"skin.json: widgets[0].bind: \"tools.total\": a list widget shows checklist.items",
      "skin.json: widgets[0].rows: must be a number", "skin.json: widgets[0].toggle.labels: must be two strings",
      "skin.json: widgets[0].toggle.rect: overlaps the list at the minimum window",
      "skin.json: widgets[0].toggle.rect: overlaps the terminal at the minimum window",
      "skin.json: widgets[1].bind: \"checklist.items\": a list-valued metric needs a list widget",
      "skin.json: widgets[2].bind: \"checklist.items\": a limits widget shows limits.items",
      "skin.json: widgets[2].meter: must be bar or needle", "skin.json: widgets[2].direction: must be row or column"}},
    {"broken-fallback",
     {"skin.json: widgets[0].resets: unknown metric \"foo.when\"", "skin.json: widgets[0].fallback.color: unknown field",
      "skin.json: widgets[0].fallback.bind: unknown metric \"foo.spend\"",
      "skin.json: widgets[0].fallback.label: must be a string"}},
    {"broken-image", {"skin.json: widgets[1].image: \"images/nope.png\": file not found"}},
    {"broken-sprite", {"skin.json: widgets[1].sheet: is 512 x 32 px"}},
    {"broken-contrast", {"skin.json: terminal.theme.foreground: contrast with background is 1.26"}},
    {"broken-small", {"skin.json: terminal.rect: leaves about"}},
    {"broken-escape", {"skin.json: widgets[1].image: \"../all-widgets/images/knob.png\": outside the skin directory"}},
    {"broken-css", {"skin.css: line 2: @import is not allowed", "skin.css: line 3: url(https://example.org/x.png)"}},
    {"broken-script", {"evil.js: (file): file type not allowed in a skin"}},
    {"broken-rect", {"skin.json: widgets[0].rect: needs exactly two of left, right, width"}},
    {"broken-menu", {"skin.json: menu: missing"}},
    {"broken-sound",
     {"skin.json: sounds[0].file: \"sounds/nope.ogg\": file not found",
      "skin.json: sounds[1].file: \"menu.svg\": file type not allowed here",
      "skin.json: sounds[1].interval: needs 1 <= min <= max <= 3600 seconds", "skin.json: sounds[2].loud: unknown field",
      "skin.json: sounds[2].file: \"../sounds/sounds/beep.ogg\": outside the skin directory",
      "skin.json: sounds[2].mode: must be loop or random", "skin.json: sounds[3].volume: must be a number from 0 to 1",
      "skin.json: sounds[3].interval: only a random sound has an interval"}},
    {"broken-split",
     {"skin.json: splits[1].id: duplicate id \"rt\"", "skin.json: splits[2].edge: must be left, right, top or bottom",
      "skin.json: splits[3].edge: a second split on the terminal's right edge", "skin.json: splits[4].grip: unknown field",
      "skin.json: splits[4].min: must be a number of pixels, at least 0",
      "skin.json: splits[5]: the terminal's top of 60 must lie from min to max",
      "skin.json: panels[0].follow: must be an object of rect keys and split ids",
      "skin.json: widgets[0].follow.left: the right split \"rt\" moves right, or width on a rect with right",
      "skin.json: widgets[1].follow.width: the right split \"rt\" moves right",
      "skin.json: widgets[2].follow.width: unknown split \"nope\"", "skin.json: widgets[2].follow.colour: not a rect key"}},
    {"broken-split-room",
     {"skin.json: widgets[0].rect: overlaps the terminal with the splits at their min at the minimum window",
      "skin.json: widgets[2].rect: has no size with the splits at their min at the minimum window",
      "skin.json: terminal.rect: leaves about 27 x 36 cells with the splits at their max at the initial window, needs "
      "40 x 10",
      "skin.json: widgets[1].rect: meets widgets[3] with the splits at their max at the initial window"}},
    {"broken-menu-rect",
     {"skin.json: menu.image: \"nope.svg\": file not found", "skin.json: menu.rect: is 20 x 12 px at the minimum window",
      "skin.json: menu.rect: lies outside the skin area at the minimum window",
      "skin.json: menu.rect: overlaps the terminal at the minimum window"}},
};

static void test_broken_fixtures_name_file_and_field(void)
{
    for (size_t c = 0; c < SK_LEN(BROKEN); c++) {
        const broken_case *b = &BROKEN[c];
        char dir[256];
        snprintf(dir, sizeof dir, F "/%s", b->dir);
        bool ok;
        sk_diags d = run(dir, &ok);
        size_t want = 0;
        while (want < SK_LEN(b->want) && b->want[want])
            want++;
        if (ok)
            sk_test_fail(__FILE__, __LINE__, "%s compiled", b->dir);
        if (d.items.len != want)
            sk_test_fail(__FILE__, __LINE__, "%s: %zu errors, want %zu", b->dir, d.items.len, want);
        for (size_t i = 0; i < want && i < d.items.len; i++) {
            char expect[512];
            snprintf(expect, sizeof expect, "%s/%s", dir, b->want[i]);
            if (!strstr(d.items.ptr[i], expect))
                sk_test_fail(__FILE__, __LINE__, "%s error %zu:\n  %s\n  wanted %s", b->dir, i, d.items.ptr[i], expect);
        }
        for (size_t i = want; i < d.items.len; i++)
            sk_test_fail(__FILE__, __LINE__, "%s extra: %s", b->dir, d.items.ptr[i]);
        sk_diags_free(&d);
    }
}

/* A manifest of the test skin with one change, and the errors it
 * gives. */
static sk_diags with(const char *json, const char *extra_file)
{
    const char *dir = sk_test_skin(NULL, NULL, "");
    sk_test_write(dir, "skin.json", json);
    if (extra_file)
        sk_test_write(dir, extra_file, "x");
    sk_skin s;
    sk_diags d = {0};
    sk_skin_load(&s, dir, &d, NULL, NULL);
    sk_skin_free(&s);
    return d;
}

#define BASE_WINDOW "\"window\": {\"width\": 1000, \"height\": 700, \"minWidth\": 800, \"minHeight\": 500}"
#define BASE_MENU "\"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 24, \"height\": 24}, \"image\": \"menu.svg\"}"
#define BASE_TERM                                                                                                      \
    "\"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, \"right\": 10, \"bottom\": 10}, \"theme\": "                \
    "{\"foreground\": \"#e0e0e0\", \"background\": \"#000000\"}}"

static void expect(const char *json, const char *extra, const char *want)
{
    sk_diags d = with(json, extra);
    bool found = sk_test_said(&d, want);
    if (!found) {
        sk_test_fail(__FILE__, __LINE__, "wanted \"%s\" among %zu errors:", want, d.items.len);
        for (size_t i = 0; i < d.items.len; i++)
            fprintf(stderr, "      %s\n", d.items.ptr[i]);
    }
    sk_diags_free(&d);
}

static void test_top_level_rules(void)
{
    expect("[]", NULL, "skin.json: (root): must be an object");
    expect("{\"name\": \"Bad Name\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM "}", NULL,
           "skin.json: name: must be lowercase letters, digits and dashes");
    expect("{\"name\": \"x\", \"colour\": 1, \"version\": 2, " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM "}", NULL,
           "skin.json: colour: unknown field");
    expect("{\"name\": \"x\", \"version\": 2, " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM "}", NULL,
           "skin.json: version: must be a string");
    expect("{\"name\": \"x\", " BASE_MENU ", " BASE_TERM "}", NULL, "skin.json: window: missing");
    expect("{\"name\": \"x\", \"window\": {\"width\": 900, \"height\": 700, \"minWidth\": 1000, \"minHeight\": 150}, "
           BASE_MENU ", " BASE_TERM "}",
           NULL, "skin.json: window.minWidth: larger than width");
    expect("{\"name\": \"x\", \"window\": {\"width\": 900, \"height\": 700, \"minWidth\": 800, \"minHeight\": 150}, "
           BASE_MENU ", " BASE_TERM "}",
           NULL, "skin.json: window.minHeight: must be a number of pixels, at least 200");
    expect("{\"name\": \"x\", \"window\": {\"width\": 20000, \"height\": 700, \"minWidth\": 800, \"minHeight\": 600}, "
           BASE_MENU ", " BASE_TERM "}",
           NULL, "skin.json: window.width: must be at most 16384 pixels");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU "}", NULL, "skin.json: terminal: missing");
}

static void test_terminal_rules(void)
{
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, "
           "\"bottom\": 10}, \"fontSize\": 40, \"theme\": {\"foreground\": \"#fff\", \"background\": \"#000\", "
           "\"cursor\": \"#12\", \"glow\": \"#fff\"}}}",
           NULL, "skin.json: terminal.rect: needs right: the terminal stretches with the window");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, "
           "\"right\": 10, \"bottom\": 10}, \"fontSize\": 40, \"theme\": {\"foreground\": \"#fff\", \"background\": "
           "\"#000\"}}}",
           NULL, "skin.json: terminal.fontSize: must be a number from 8 to 32");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, "
           "\"right\": 10, \"bottom\": 10}, \"theme\": {\"foreground\": \"#fff\", \"background\": \"#000\", "
           "\"cursor\": \"#12\"}}}",
           NULL, "skin.json: terminal.theme.cursor: \"#12\": must be a #rgb, #rrggbb or #rrggbbaa colour");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, "
           "\"right\": 10, \"bottom\": 10}, \"theme\": {\"foreground\": \"#fff\", \"background\": \"#000\", "
           "\"glow\": \"#fff\"}}}",
           NULL, "skin.json: terminal.theme.glow: unknown field");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, "
           "\"right\": 10, \"bottom\": 10}, \"theme\": {\"foreground\": \"#ffffff80\", \"background\": \"#000\"}}}",
           NULL, "skin.json: terminal.theme.foreground: required as an opaque #rgb or #rrggbb colour");
    /* toFixed(2), as validate.js prints the ratio. */
    /* #777 on black is 4.69: enough. */
    sk_diags ok = with("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, "
                       "\"top\": 300, \"right\": 10, \"bottom\": 10}, \"theme\": {\"foreground\": \"#777\", "
                       "\"background\": \"#000\"}}}",
                       NULL);
    CHECK_INT(ok.items.len, 0);
    sk_diags_free(&ok);
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, "
           "\"right\": 10, \"bottom\": 10}, \"theme\": {\"foreground\": \"#555\", \"background\": \"#000\"}}}",
           NULL, "skin.json: terminal.theme.foreground: contrast with background is 2.82, needs at least 4.5");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", \"terminal\": {\"rect\": {\"left\": 300, \"top\": 300, "
           "\"right\": 300, \"bottom\": 100}, \"theme\": {\"foreground\": \"#fff\", \"background\": \"#000\"}}}",
           NULL, "skin.json: terminal.rect: leaves about 23 x 4 cells at the minimum window, needs 40 x 10");
}

static void test_widget_rules(void)
{
#define WIDGETS(w) "{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM ", \"widgets\": [" w "]}"
#define R "\"rect\": {\"left\": 0, \"top\": 60, \"width\": 100, \"height\": 30}"
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"led\", \"bind\": \"state.busy\", " R "}"), NULL,
           "skin.json: widgets[0]: a led needs on (a threshold) or pulse: true");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"led\", \"bind\": \"state.busy\", \"pulse\": 1, " R "}"), NULL,
           "skin.json: widgets[0].pulse: must be true or false");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"readout\", \"bind\": \"state.busy\", \"format\": \"fixed:9\", " R "}"),
           NULL, "skin.json: widgets[0].format: \"fixed:9\": one of int, fixed:N, pct, usd, tokens, duration, until, "
                 "bytes, text");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"readout\", \"bind\": \"state.busy\", \"min\": 5, \"max\": 5, " R "}"),
           NULL, "skin.json: widgets[0].max: must be larger than min");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"gauge\", \"bind\": \"state.busy\", \"marks\": [{\"bind\": "
                   "\"state.busy\", \"value\": 1}, {\"bind\": 5}, 3], " R "}"),
           NULL, "skin.json: widgets[0].marks[0]: needs exactly one of bind, value");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"gauge\", \"bind\": \"state.busy\", \"marks\": [{\"bind\": 5}], " R "}"),
           NULL, "skin.json: widgets[0].marks[0].bind: unknown metric \"5\"");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"gauge\", \"bind\": \"state.busy\", \"marks\": [3], " R "}"), NULL,
           "skin.json: widgets[0].marks[0]: must be an object");
    expect(WIDGETS("{\"id\": \"A\", \"type\": \"bar\", \"bind\": \"state.busy\", \"orientation\": \"up\", " R "}"),
           NULL, "skin.json: widgets[0].orientation: must be horizontal or vertical");
    expect(WIDGETS("{\"id\": \"A\", \"type\": \"bar\", \"bind\": \"state.busy\", " R "}"), NULL,
           "skin.json: widgets[0].id: must be lowercase letters, digits and dashes");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"bar\", \"bind\": \"state.busy\", " R "}, {\"id\": \"a\", \"type\": "
                   "\"bar\", \"bind\": \"state.busy\", " R "}"),
           NULL, "skin.json: widgets[1].id: duplicate id \"a\"");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"bar\", \"bind\": \"state.busy\", \"face\": \"x.png\", " R "}"), NULL,
           "skin.json: widgets[0].face: unknown field");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"sprite\", \"bind\": \"state.busy\", \"sheet\": \"s.png\", \"frames\": "
                   "2, \"frameWidth\": 1, \"frameHeight\": 1, " R "}"),
           "s.png", "skin.json: widgets[0].sheet: not a PNG file");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"sprite\", \"bind\": \"state.busy\", \"frames\": 0, " R "}"), NULL,
           "skin.json: widgets[0].frames: required, a positive number");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"sprite\", \"bind\": \"state.busy\", \"frames\": 1, \"frameWidth\": 1, "
                   "\"frameHeight\": 1, " R "}"),
           NULL, "skin.json: widgets[0].sheet: must be a relative file path");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"knob\", \"bind\": \"state.busy\", \"image\": \"/etc/x.png\", " R "}"),
           NULL, "skin.json: widgets[0].image: \"/etc/x.png\": must be a path relative to the skin");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"knob\", \"bind\": \"state.busy\", \"image\": \"x.txt\", " R "}"), NULL,
           "skin.json: widgets[0].image: \"x.txt\": file type not allowed here (.png .jpg .jpeg .gif .webp .svg)");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"bar\", \"bind\": \"state.busy\", \"fallback\": [], " R "}"), NULL,
           "skin.json: widgets[0].fallback: must be an object of bind, label and resets");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"bar\", \"bind\": \"state.busy\", \"fallback\": {}, " R "}"), NULL,
           "skin.json: widgets[0].fallback.bind: missing metric name");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"bar\", \"bind\": \"state.busy\", \"rect\": {\"left\": \"0\", "
                   "\"top\": 0, \"width\": 1, \"height\": 1}}"),
           NULL, "skin.json: widgets[0].rect.left: must be a number of pixels");
    expect(WIDGETS("{\"id\": \"a\", \"type\": \"bar\", \"bind\": \"state.busy\"}"), NULL,
           "skin.json: widgets[0].rect: missing rect");
    expect(WIDGETS("3"), NULL, "skin.json: widgets[0]: must be an object");
#undef WIDGETS
#undef R
}

static void test_menu_fonts_files(void)
{
    expect("{\"name\": \"x\", " BASE_WINDOW ", \"menu\": [], " BASE_TERM "}", NULL,
           "skin.json: menu: missing: every skin places the menu button");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM
           ", \"fonts\": [{\"family\": \"A*\", \"file\": \"f.ttf\", \"style\": \"oblique\"}]}",
           "f.ttf", "skin.json: fonts[0].family: must be letters, digits, spaces, dashes");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM
           ", \"fonts\": [{\"family\": \"A\", \"file\": \"f.ttf\", \"style\": \"oblique\"}]}",
           "f.ttf", "skin.json: fonts[0].style: must be normal or italic");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM ", \"fonts\": {}}", NULL,
           "skin.json: fonts: must be an array");
    /* A plain license file is allowed, a script never. */
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM "}", "tool.py",
           "tool.py: (file): file type not allowed in a skin");
    sk_diags d = with("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM "}", "OFL-1.1");
    CHECK_INT(d.items.len, 0);
    sk_diags_free(&d);
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM ", \"sounds\": 1}", NULL,
           "skin.json: sounds: must be an array");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM ", \"splits\": {}}", NULL,
           "skin.json: splits: must be an array");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM ", \"panels\": [{\"class\": \"A\", \"rect\": "
           "{\"left\": 0, \"top\": 0, \"width\": 1, \"height\": 1}}]}",
           NULL, "skin.json: panels[0].class: must be lowercase class names");
}

static void test_skinc_refusals(void)
{
    /* What validate.js lets through and skinc cannot compile. */
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM
           ", \"fonts\": [{\"family\": \"A\", \"file\": \"f.woff2\"}]}",
           "f.woff2",
           "skin.json: fonts[0].file: \"f.woff2\": skinc cannot compile this file type: fonts are .ttf or .otf with "
           "TrueType outlines");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM
           ", \"fonts\": [{\"family\": \"A\", \"file\": \"f.ttf\", \"weight\": \"heavy\"}]}",
           "f.ttf", "skin.json: fonts[0].weight: must be normal, bold or a number from 1 to 1000");
    expect("{\"name\": \"x\", " BASE_WINDOW ", " BASE_MENU ", " BASE_TERM
           ", \"sounds\": [{\"file\": \"s.mp3\", \"mode\": \"loop\"}]}",
           "s.mp3",
           "skin.json: sounds[0].file: \"s.mp3\": skinc cannot compile this file type: sounds are Ogg Vorbis (.ogg) or "
           "PCM WAVE (.wav)");
    expect("{\"name\": \"x\", " BASE_WINDOW ", \"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 24, "
           "\"height\": 24}, \"image\": \"m.webp\"}, " BASE_TERM "}",
           "m.webp", "skin.json: menu.image: \"m.webp\": skinc cannot compile this file type: images are PNG, JPEG or SVG");
    /* GIF passes the JavaScript validator's list, but skinc decodes no GIF. */
    expect("{\"name\": \"x\", " BASE_WINDOW ", \"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 24, "
           "\"height\": 24}, \"image\": \"m.gif\"}, " BASE_TERM "}",
           "m.gif", "skin.json: menu.image: \"m.gif\": skinc cannot compile this file type: images are PNG, JPEG or SVG");
}

static void test_js_fixed(void)
{
    sk_buf b = {0};
    sk_js_fixed(&b, 1.255, 2);
    /* 1.255 is 1.25499999... as a double: down, as V8 rounds it. */
    CHECK_STR(sk_buf_cstr(&b), "1.25");
    sk_buf_clear(&b);
    sk_js_fixed(&b, 1.125, 2);
    /* An exact tie goes up. */
    CHECK_STR(sk_buf_cstr(&b), "1.13");
    sk_buf_clear(&b);
    sk_js_fixed(&b, 9.999, 2);
    CHECK_STR(sk_buf_cstr(&b), "10.00");
    sk_buf_clear(&b);
    sk_js_fixed(&b, 21, 2);
    CHECK_STR(sk_buf_cstr(&b), "21.00");
    sk_buf_clear(&b);
    sk_js_fixed(&b, -0.001, 2);
    /* The sign stays, as toFixed keeps it. */
    CHECK_STR(sk_buf_cstr(&b), "-0.00");
    sk_buf_clear(&b);
    sk_js_fixed(&b, -0.0, 2);
    CHECK_STR(sk_buf_cstr(&b), "0.00");
    sk_buf_free(&b);
    /* WCAG's ratio of black and white. */
    CHECK(sk_contrast("#fff", "#000") > 20.99 && sk_contrast("#000000", "#ffffff") < 21.01);
}

const sk_test sk_suite_skinc_validate[] = {
    T(test_all_widgets_is_valid),
    T(test_sounds_are_valid),
    T(test_every_shipped_skin_is_valid),
    T(test_broken_fixtures_name_file_and_field),
    T(test_top_level_rules),
    T(test_terminal_rules),
    T(test_widget_rules),
    T(test_menu_fonts_files),
    T(test_skinc_refusals),
    T(test_js_fixed),
    {0},
};
