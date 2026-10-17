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

/* ---- Painting what changed ---- */

/* Three seconds of a session that moves every kind of widget, drawn
 * frame by frame as the window draws, only what changed each time:
 * the frame at the end must be the frame drawn whole. */
static void incremental(const char *name, double scale)
{
    const sk_embedded_file *e = sk_shipped_pack(name);
    char err[300];
    sk_skinpack *p = e ? sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err) : NULL;
    CHECK(p != NULL);
    if (!p)
        return;
    sk_skin_window w;
    sk_skinpack_window(p, &w);
    sk_skinview *v = sk_skinview_new(p, 0);
    sk_skinpack_unref(p);
    v->wall_fn = fixed_wall;
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w.width, w.height - SK_VIEW_STRIP, scale);
    sk_image frame;
    sk_image_init(&frame, (int)(w.width * scale), (int)(w.height * scale));
    sk_skin_metric m[SK_LEN(METRICS)];
    memcpy(m, METRICS, sizeof m);
    static const char *const lists[] = {
        "[{\"text\":\"one\",\"state\":\"doing\"}]",
        "[{\"text\":\"one\",\"state\":\"done\"},{\"text\":\"two\",\"state\":\"doing\"}]",
        "[]",
    };
    static const char *const limits[] = {
        "[{\"key\":\"a\",\"label\":\"5h\",\"window\":\"5h\",\"pct\":20}]",
        ("[{\"key\":\"a\",\"label\":\"5h\",\"window\":\"5h\",\"pct\":70},{\"key\":\"b\",\"label\":\"7d\",\"window\":"
         "\"7d\",\"pct\":10},{\"key\":\"c\",\"label\":\"Spark 7d\",\"window\":\"7d\",\"model\":\"spark\",\"pct\":40}]"),
        "null",
    };
    sk_rect r[32];
    for (int i = 1; i <= 180; i++) {
        double t = i * 1000.0 / 60;
        if (i % 6 == 0) {
            int k = i / 6;
            m[0].value.num = 10 + (k * 7) % 80;   /* context */
            m[4].value.num = 0.5 + k * 0.37;      /* cost */
            m[5].value.num = k;                   /* tools */
            m[8].value.num = (k * 13) % 150;      /* tokens/s */
            m[11].value.num = (k / 5) % 2;        /* busy */
            m[12].value.num = (k % 3) ? 400 : 0;  /* output */
            m[13].value.num = (k * 11) % 100;     /* cpu */
            m[15].value.num = (k % 10) / 10.0;    /* cache */
            m[16].value.num = k % 4;              /* agents */
            m[17].value.num = 3 + k / 2;          /* tool events */
            m[20].value.type = (k / 8) % 3 == 2 ? SK_VAL_NULL : SK_VAL_LIST;
            m[20].value.text = (char *)limits[(k / 8) % 3];
            m[21].value.text = (char *)lists[(k / 9) % 3];
            sk_skinview_set_metrics(v, m, SK_LEN(m));
        }
        if (i == 90)
            sk_skinview_set_list_off(v, true);
        if (i == 120) {
            sk_skinview_set_dials(v, NULL, 0);
            sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 100, 120, 0);
        }
        sk_skinview_step(v, t);
        sk_skinview_draw(v, &frame, i == 1, r, 32);
    }
    /* phosphor's scanlines flicker over its screen: blended, not painted. */
    if (!strcmp(name, "phosphor"))
        CHECK(v->fade.node != SK_PACK_NONE && v->fade.valid);
    sk_image whole;
    sk_image_init(&whole, frame.w, frame.h);
    sk_skinview_draw(v, &whole, true, r, 32);
    /* A part painted alone may round a blur or a turned layer's samples
     * a level or two apart from the whole; a pixel left from an earlier
     * frame differs by far more. */
    sk_rect term = sk_skinview_terminal_px(v);
    long stale = 0;
    int first_x = -1, first_y = -1, worst = 0;
    for (int y = 0; y < frame.h; y++)
        for (int x = 0; x < frame.w; x++) {
            if (x >= term.x && x < term.x + term.w && y >= term.y && y < term.y + term.h)
                continue;
            sk_px a = sk_image_row(&frame, y)[x], b = sk_image_row(&whole, y)[x];
            int d = 0;
            for (int sh = 0; sh < 32; sh += 8)
                d = SK_MAX(d, abs((int)((a >> sh) & 255) - (int)((b >> sh) & 255)));
            worst = SK_MAX(worst, d);
            if (d > 6 && !stale++) {
                first_x = x;
                first_y = y;
            }
        }
    if (stale)
        sk_test_fail(__FILE__, __LINE__, "%s at %gx: %ld pixels differ from a whole paint, the first at %d, %d", name,
                     scale, stale, first_x, first_y);
    CHECK(worst <= 6);
    sk_image_free(&whole);
    sk_image_free(&frame);
    sk_skinview_free(v);
}

static void test_incremental(void)
{
    incremental("coffee", 1);
    incremental("deck", 1);
    incremental("library", 1);
    incremental("phosphor", 1);
    incremental("deck", 2);
    incremental("phosphor", 2);
}

const sk_test sk_suite_skin_live[] = {
    T(test_coffee),
    T(test_deck),
    T(test_library),
    T(test_phosphor),
    T(test_incremental),
    {0},
};
