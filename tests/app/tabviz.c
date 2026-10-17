/* Each tab's band in the tab strip (decision 0015, 2026-10-15), as
 * app/renderer/tabviz.js has it: a column every 125 ms, an event's
 * column lit in its colour, the done state and the flat line, a done
 * band that settles and is drawn no more, and a golden of three bands. */
#include "../../src/app/tabviz.h"
#include "../../src/gfx/draw.h"
#include "../../src/viz/script.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>

/* One tab's frame: its numbers, its events, u and q. */
typedef struct {
    int tab;
    double out_bps, out_rate, busy;
    bool no_busy;
    double quiet;
    int events[8];
    size_t n_events;
} tab_nums;

static void feed(sk_tabviz *t, const tab_nums *n, size_t count)
{
    sk_viz_pane_frame p[4];
    bool no_busy[4];
    double quiet[4];
    memset(p, 0, sizeof p);
    for (size_t i = 0; i < count; i++) {
        p[i].tab = n[i].tab;
        p[i].n[SK_VIZ_OUT_BPS] = n[i].out_bps;
        p[i].n[SK_VIZ_OUT_RATE] = n[i].out_rate;
        p[i].n[SK_VIZ_BUSY] = n[i].busy;
        p[i].events = n[i].events;
        p[i].n_events = n[i].n_events;
        no_busy[i] = n[i].no_busy;
        quiet[i] = n[i].quiet;
    }
    sk_vizfeed_msg m;
    memset(&m, 0, sizeof m);
    m.frame.panes = p;
    m.frame.n_panes = count;
    m.no_busy = no_busy;
    m.quiet = quiet;
    sk_tabviz_feed(t, &m);
}

/* Steps of 1/30 s for secs; how many of them asked for a redraw. */
static int run(sk_tabviz *t, double secs)
{
    int redraws = 0;
    for (int i = 0; i < (int)lround(secs * 30); i++)
        redraws += sk_tabviz_step(t, 1.0 / 30);
    return redraws;
}

static void test_columns_scroll_and_events_light_them(void)
{
    sk_tabviz *t = sk_tabviz_new(7);
    int ids[] = {1, 2};
    sk_tabviz_keep(t, ids, 2);
    tab_nums busy = {1, 2000, 60, 1, false, 0, {SK_EV_TOOL, 1}, 2};
    feed(t, &busy, 1);
    /* No column before 125 ms; then one, lit in an edit's green. */
    CHECK(!sk_tabviz_step(t, 0.1));
    CHECK(sk_tabviz_step(t, 0.03));
    double amp, hue;
    bool lit;
    CHECK(sk_tabviz_column(t, 1, 43, &amp, &hue, &lit));
    CHECK(lit);
    CHECK(hue == 120);
    CHECK(amp > 0.5 && amp <= 1);
    CHECK(sk_tabviz_column(t, 1, 42, &amp, &hue, &lit));
    CHECK(!lit && amp == 0);
    /* The next column is unlit, its hue from the treble and the busy
     * state; the lit one scrolls left. */
    busy.n_events = 0;
    feed(t, &busy, 1);
    run(t, 0.125);
    CHECK(sk_tabviz_column(t, 1, 42, &amp, &hue, &lit));
    CHECK(lit && hue == 120);
    CHECK(sk_tabviz_column(t, 1, 43, &amp, &hue, &lit));
    CHECK(!lit);
    CHECK(hue > 100 && hue < 190);
    /* A prompt's colour, a stop's, and a class that is none. */
    tab_nums ev = {1, 0, 0, 1, false, 0, {SK_EV_PROMPT, 0, SK_EV_STOP, 0, SK_EV_TOOL, 9}, 6};
    feed(t, &ev, 1);
    run(t, 0.125);
    CHECK(sk_tabviz_column(t, 1, 43, &amp, &hue, &lit));
    CHECK(lit && hue == 200);
    /* 44 columns later the band has scrolled the event out. */
    tab_nums quiet = {1, 0, 0, 1, false, 0, {0}, 0};
    feed(t, &quiet, 1);
    run(t, 44 * 0.125 + 0.05);
    for (int x = 0; x < SK_TABVIZ_W; x++) {
        CHECK(sk_tabviz_column(t, 1, x, &amp, &hue, &lit));
        CHECK(!lit);
    }
    /* A tab not kept has no band; the band of one dropped goes. */
    CHECK(!sk_tabviz_column(t, 3, 0, &amp, &hue, &lit));
    CHECK(sk_tabviz_done(t, 2) == 0);
    sk_tabviz_keep(t, ids, 1);
    CHECK(sk_tabviz_done(t, 2) == -1);
    sk_tabviz_free(t);
}

static void test_a_done_tab_greys_and_settles(void)
{
    sk_tabviz *t = sk_tabviz_new(7);
    int ids[] = {1, 2, 3};
    sk_tabviz_keep(t, ids, 3);
    /* Busy, then idle but heard from 1 s ago, then quiet 2 s. */
    tab_nums n[] = {
        {1, 1500, 80, 1, false, 0, {0}, 0},
        {2, 0, 0, 0, false, 1, {0}, 0},
        {3, 0, 0, 0, false, 2.5, {0}, 0},
    };
    feed(t, n, 3);
    run(t, 1.5);
    CHECK(sk_tabviz_done(t, 1) == 0);
    CHECK(sk_tabviz_done(t, 2) == 0);
    CHECK(sk_tabviz_done(t, 3) > 0.99);
    /* With no busy state, a token rate still counts as work. */
    tab_nums transcript[] = {{1, 0, 30, 0, true, 5, {0}, 0}, {2, 0, 0, 0, true, 5, {0}, 0}};
    feed(t, transcript, 2);
    run(t, 1.5);
    CHECK(sk_tabviz_done(t, 1) < 0.01);
    CHECK(sk_tabviz_done(t, 2) > 0.99);
    /* All done and every column quiet: the strip is drawn no more. */
    tab_nums all[] = {{1, 0, 0, 0, true, 5, {0}, 0}, {2, 0, 0, 0, true, 5, {0}, 0}, {3, 0, 0, 0, false, 9, {0}, 0}};
    feed(t, all, 3);
    run(t, 6.5);
    CHECK_INT(run(t, 2), 0);
    double amp, hue;
    bool lit;
    for (int x = 0; x < SK_TABVIZ_W; x++) {
        CHECK(sk_tabviz_column(t, 1, x, &amp, &hue, &lit));
        CHECK(amp < 0.15 && !lit);
    }
    /* An event wakes it. */
    tab_nums wake = {2, 0, 0, 0, true, 0, {SK_EV_OUTPUT, 0}, 2};
    feed(t, &wake, 1);
    CHECK(run(t, 0.2) > 0);
    int lit_at = -1;
    for (int x = 40; x < SK_TABVIZ_W; x++)
        if (sk_tabviz_column(t, 2, x, &amp, &hue, &lit) && lit && hue == 160)
            lit_at = x;
    CHECK(lit_at >= 42);
    sk_tabviz_free(t);
}

/* Three bands as a strip holds them: one at work on the scripted
 * session, one done, one crowded to 30 points, on the active tab's
 * #1e1e1e, at 2x. */
static void test_bands_golden(void)
{
    sk_tabviz *t = sk_tabviz_new(11);
    int ids[] = {1, 2, 3};
    sk_tabviz_keep(t, ids, 3);
    double now = 2.0;
    for (int k = 0; k < 120; k++) {
        tab_nums n[3];
        memset(n, 0, sizeof n);
        sk_viz_pane_frame f;
        int buf[16];
        for (int i = 0; i < 3; i++) {
            double off = i == 1 ? 25 : i * 6.0;
            sk_viz_script(now + off - 1.0 / 30, now + off, &f, buf, SK_LEN(buf));
            n[i].tab = ids[i];
            n[i].out_bps = f.n[SK_VIZ_OUT_BPS];
            n[i].out_rate = f.n[SK_VIZ_OUT_RATE];
            n[i].busy = f.n[SK_VIZ_BUSY];
            n[i].quiet = f.n[SK_VIZ_IDLE_S];
            n[i].n_events = SK_MIN(f.n_events, SK_LEN(n[i].events));
            memcpy(n[i].events, buf, n[i].n_events * sizeof *buf);
        }
        feed(t, n, 3);
        sk_tabviz_step(t, 1.0 / 30);
        now += 1.0 / 30;
    }
    const double s = 2;
    sk_image im;
    sk_image_init(&im, (int)(140 * s), (int)(24 * s));
    sk_fill_rect(&im, sk_image_bounds(&im), SK_RGB(0x1e, 0x1e, 0x1e), SK_BLEND_SRC, NULL);
    sk_tabviz_draw(t, 1, &im, sk_rect_make((int)(4 * s), (int)(4 * s), (int)(44 * s), (int)(16 * s)), s, NULL);
    sk_tabviz_draw(t, 2, &im, sk_rect_make((int)(54 * s), (int)(4 * s), (int)(44 * s), (int)(16 * s)), s, NULL);
    sk_tabviz_draw(t, 3, &im, sk_rect_make((int)(104 * s), (int)(4 * s), (int)(30 * s), (int)(16 * s)), s, NULL);
    /* A tab with no band draws nothing. */
    sk_tabviz_draw(t, 9, &im, sk_rect_make(0, 0, 10, 10), s, NULL);
    CHECK(sk_tabviz_done(t, 2) > 0.99);
    CHECK(sk_tabviz_done(t, 1) < 0.01);
    CHECK_GOLDEN("tabviz_bands_2x", &im, 2);
    sk_image_free(&im);
    sk_tabviz_free(t);
}

const sk_test sk_suite_tabviz[] = {
    T(test_columns_scroll_and_events_light_them),
    T(test_a_done_tab_greys_and_settles),
    T(test_bands_golden),
    {0},
};
