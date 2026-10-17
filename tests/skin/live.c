/* The four shipped skins live (src/skin/view.h): each given one set of
 * metrics (a task list, two usage limits, every widget a value), a
 * dial open, and four seconds of frames at sixty a second, so springs
 * settle and animations stand where they stand at that time, then
 * drawn whole at 1x and in part at 2x against golden images, the
 * terminal's rectangle in its theme's background as the window shows
 * it. A blur, a glyph or a resampled image may round a little
 * differently on another machine, as tests/skin/skins.c allows. */
#include "../../src/skin/shipped.h"
#include "../../src/skin/viewint.h"
#include "../golden.h"
#include "../test.h"

#define WALL 1700000000.0

static double fixed_wall(void *ud)
{
    (void)ud;
    return WALL;
}

static const sk_skin_metric METRICS[] = {
    {"context.used_pct", SK_NUM(42.5)},
    {"context.used_tokens", SK_NUM(85000)},
    {"context.compact_at_pct", SK_NUM(80)},
    {"context.compact_threshold", SK_NUM(160000)},
    {"cost.usd", SK_NUM(1.234)},
    {"tools.total", SK_NUM(17)},
    {"tokens.in", SK_NUM(123456)},
    {"tokens.out", SK_NUM(4321)},
    {"tokens.out_rate", SK_NUM(60)},
    {"model.name", {SK_VAL_STR, 0, "Opus 5.5"}},
    {"session.clock_s", SK_NUM(1234)},
    {"state.busy", SK_NUM(1)},
    {"pty.out_bps", SK_NUM(400)},
    {"proc.cpu_pct", SK_NUM(35)},
    {"proc.mem_mb", SK_NUM(420)},
    {"cache.hit_ratio", SK_NUM(0.8)},
    {"agents.active", SK_NUM(2)},
    {"event.tool", SK_NUM(3)},
    {"limits.five_hour_pct", SK_NUM(30)},
    {"limits.five_hour_resets_at", SK_NUM(WALL + 3 * 3600 + 120)},
    {"limits.items",
     {SK_VAL_LIST, 0,
      "[{\"key\":\"five_hour\",\"label\":\"5h\",\"window\":\"5h\",\"model\":null,\"pct\":30,\"resets_at\":1700010920},"
      "{\"key\":\"seven_day\",\"label\":\"7d\",\"window\":\"7d\",\"model\":null,\"pct\":60,\"resets_at\":1700345000}]"}},
    {"checklist.items",
     {SK_VAL_LIST, 0,
      "[{\"text\":\"Read the code\",\"state\":\"done\"},{\"text\":\"Write the runtime\",\"state\":\"doing\"},"
      "{\"text\":\"Test it\",\"state\":\"todo\"}]"}},
};

/* The skin's window at scale, drawn as the app shows it. */
static void draw(sk_skinview *v, double scale, sk_image *out)
{
    sk_skin_window w;
    sk_skinpack_window(v->sp, &w);
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w.width, w.height - SK_VIEW_STRIP, scale);
    sk_skinview_step(v, v->now + 1000.0 / 60);
    sk_image_init(out, (int)(w.width * scale), (int)(w.height * scale));
    sk_fill_rect(out, sk_image_bounds(out), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    sk_rect r[8];
    sk_skinview_draw(v, out, true, r, 8);
    sk_skin_terminal t;
    sk_skinpack_terminal(v->sp, &t);
    sk_fill_rect(out, sk_skinview_terminal_px(v), t.colors[SK_THEME_BACKGROUND] | 0xff000000u, SK_BLEND_SRC, NULL);
}

static void live(const char *name, sk_rect part)
{
    const sk_embedded_file *e = sk_shipped_pack(name);
    char err[300];
    sk_skinpack *p = e ? sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err) : NULL;
    CHECK(p != NULL);
    if (!p)
        return;
    sk_skinview *v = sk_skinview_new(p, 0);
    sk_skinpack_unref(p);
    v->wall_fn = fixed_wall;
    sk_skinview_set_metrics(v, METRICS, SK_LEN(METRICS));
    const char *dials[] = {"cost.usd"};
    sk_skinview_set_dials(v, dials, 1);
    for (int i = 1; i <= 240; i++)
        sk_skinview_step(v, i * 1000.0 / 60);
    char golden[64];
    sk_image im;
    draw(v, 1, &im);
    snprintf(golden, sizeof golden, "live_%s_1x", name);
    CHECK_GOLDEN_NEAR(golden, &im, 24, 300);
    sk_image_free(&im);
    draw(v, 2, &im);
    sk_image crop;
    sk_image_init(&crop, part.w, part.h);
    sk_blit(&crop, 0, 0, &im, part, SK_BLEND_SRC, 255, NULL);
    snprintf(golden, sizeof golden, "live_%s_2x", name);
    CHECK_GOLDEN_NEAR(golden, &crop, 24, 300);
    sk_image_free(&crop);
    sk_image_free(&im);
    sk_skinview_free(v);
}

static void test_coffee(void)
{
    /* The chalkboard's list, the menu button, the turntable's gauge. */
    live("coffee", sk_rect_make(40, 40, 640, 720));
}

static void test_deck(void)
{
    /* The dials, the limits and the tasks. */
    live("deck", sk_rect_make(1160, 80, 1320, 480));
}

static void test_library(void)
{
    /* The chalk gauge and needle, the whiteboard's bars. */
    live("library", sk_rect_make(40, 340, 560, 900));
}

static void test_phosphor(void)
{
    /* The context gauge, the bars, the limits. */
    live("phosphor", sk_rect_make(40, 100, 640, 760));
}

const sk_test sk_suite_skin_live[] = {
    T(test_coffee),
    T(test_deck),
    T(test_library),
    T(test_phosphor),
    {0},
};
