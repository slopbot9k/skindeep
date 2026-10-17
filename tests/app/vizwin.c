/* The visualization's window (decision 0015) as app/viz/index.html,
 * viz.js and layout.js have it: the panes the feed's frames give, in
 * their order, each with its mode; the presses that go to the feed and
 * the pointer that goes to the modes, held by the pane pressed; goldens
 * of two panes at 2x and of four with a drop-down open; and the window
 * itself, hidden, which never takes the focus and stays closed once the
 * user closed it. */
#include "../../src/app/vizwin.h"
#include "../../src/viz/script.h"
#include "../../src/viz/viz.h"
#include "../golden.h"
#include "../test.h"

#ifdef __APPLE__
#include "../../src/platform/macos/uimac.h"
#endif

#include <math.h>
#include <stdlib.h>

/* A pane of a hand-made frame. */
typedef struct {
    int tab, mode;
    bool active, open;
    double offset; /* where in the script the tab is */
} spec;

/* The feed's frame for panes from the script's (t0, t], as vizfeed
 * would post it. */
static sk_vizfeed_msg *frame_of(const spec *s, size_t n, double t0, double t, int src, int st, unsigned opened)
{
    sk_vizfeed_msg *m = sk_xcalloc(1, sizeof *m);
    sk_viz_pane_frame *p = sk_xcalloc(n ? n : 1, sizeof *p);
    bool *no_busy = sk_xcalloc(n ? n : 1, sizeof *no_busy);
    double *quiet = sk_xcalloc(n ? n : 1, sizeof *quiet);
    for (size_t i = 0; i < n; i++) {
        int buf[256];
        sk_viz_script(t0 + s[i].offset, t + s[i].offset, &p[i], buf, SK_LEN(buf));
        int *ev = sk_xcalloc(p[i].n_events ? p[i].n_events : 1, sizeof *ev);
        if (p[i].n_events)
            memcpy(ev, buf, p[i].n_events * sizeof *ev);
        p[i].events = ev;
        p[i].tab = s[i].tab;
        p[i].place = (int)i + 1;
        p[i].active = s[i].active;
        p[i].mode = s[i].mode;
        p[i].open = s[i].open;
    }
    m->frame.panes = p;
    m->frame.n_panes = n;
    m->frame.src = src;
    m->frame.st = st;
    m->no_busy = no_busy;
    m->quiet = quiet;
    m->open = true;
    m->opened = opened;
    return m;
}

static void give(sk_vizwin *v, sk_vizfeed_msg *m)
{
    sk_vizwin_frame(v, m);
    sk_vizfeed_msg_free(m);
}

typedef struct {
    sk_fontset *mono;
    sk_glyph_cache *cache;
} fonts;

static void fonts_open(fonts *f)
{
    static const char *const mono[] = {"DejaVu Sans Mono", NULL};
    f->mono = sk_fontset_new(NULL, mono, true);
    f->cache = sk_glyph_cache_new((size_t)8 << 20);
}

static void fonts_close(fonts *f)
{
    sk_glyph_cache_free(f->cache);
    sk_fontset_free(f->mono);
}

static sk_vizwin *offscreen(fonts *f, sk_vizfeed *feed)
{
    sk_vizwin_config c = {0};
    c.mono = f->mono;
    c.cache = f->cache;
    c.feed = feed;
    c.offscreen = true;
    return sk_vizwin_new(&c);
}

static void test_the_panes_follow_the_frames(void)
{
    fonts f;
    fonts_open(&f);
    sk_vizwin *v = offscreen(&f, NULL);
    int scope = sk_viz_mode_find("scope"), go = sk_viz_mode_find("go");
    spec two[] = {{1, scope, true, false, 0}, {2, go, false, false, 3}};
    give(v, frame_of(two, 2, -0.01, 0.05, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 1));
    CHECK_INT(sk_vizwin_panes(v), 2);
    CHECK_INT(sk_vizwin_pane_tab(v, 0), 1);
    CHECK_INT(sk_vizwin_shown(v, 0), scope);
    CHECK_INT(sk_vizwin_shown(v, 1), go);
    /* The prompt at 0 s reached the first pane's synthesiser. */
    CHECK(sk_vizwin_synth(v, 0)->beats > 0);
    long beats = sk_vizwin_synth(v, 1)->beats;
    /* A new tab between, one gone, and a mode not yet known. */
    spec next[] = {{3, -1, false, false, 0}, {2, go, true, false, 3}};
    give(v, frame_of(next, 2, 0.05, 0.1, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 1));
    CHECK_INT(sk_vizwin_panes(v), 2);
    CHECK_INT(sk_vizwin_pane_tab(v, 0), 3);
    CHECK_INT(sk_vizwin_shown(v, 0), -1);
    CHECK_INT(sk_vizwin_pane_tab(v, 1), 2);
    CHECK_INT(sk_vizwin_shown(v, 1), go);
    CHECK(sk_vizwin_synth(v, 1)->beats >= beats);
    sk_image im;
    sk_image_init(&im, 320, 200);
    sk_vizwin_render(v, &im, 320, 200, 1, 1.0 / 60);
    /* The sound output alone feeds no pane the session's events. */
    spec sound[] = {{2, go, true, false, 0}};
    beats = sk_vizwin_synth(v, 1)->beats;
    give(v, frame_of(sound, 1, -0.01, 0.01, SK_VIZ_SRC_SOUND, SK_VIZ_SOUND_LIVE, 1));
    CHECK_INT(sk_vizwin_panes(v), 1);
    CHECK_INT(sk_vizwin_synth(v, 0)->beats, beats);
    sk_vizwin_render(v, &im, 320, 200, 1, 1.0 / 60);
    /* Closed: the page goes, and its panes with it. */
    sk_vizfeed_msg *m = frame_of(sound, 1, 0, 0.01, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 1);
    m->open = false;
    give(v, m);
    CHECK_INT(sk_vizwin_panes(v), 0);
    sk_image_free(&im);
    sk_vizwin_free(v);
    fonts_close(&f);
}

/* ---- Presses and the pointer ---- */

typedef struct {
    sk_vizfeed_msg *last;
} catcher;

static void catch_frame(void *ud, sk_vizfeed_msg *m)
{
    catcher *c = ud;
    sk_vizfeed_msg_free(c->last);
    c->last = m;
}

/* A frame from the feed handed to the window, and a frame drawn. */
static void feed_window(sk_vizfeed *feed, catcher *c, sk_vizwin *v, sk_image *im, uint64_t now)
{
    sk_vizfeed_tick(feed, now);
    if (c->last)
        sk_vizwin_frame(v, c->last);
    sk_vizwin_render(v, im, 720, 460, 1, 1.0 / 60);
}

static size_t touches(const sk_vizwin *v, size_t i)
{
    return sk_vizwin_synth(v, i)->touches.len;
}

static void test_presses_go_to_the_feed_and_the_pointer_to_the_modes(void)
{
    fonts f;
    fonts_open(&f);
    sk_loop *loop = sk_loop_new();
    catcher c = {0};
    sk_vizfeed_config fc = {0};
    fc.loop = loop;
    fc.manual = true;
    fc.frame = catch_frame;
    fc.ud = &c;
    sk_vizfeed *feed = sk_vizfeed_new(&fc);
    sk_vizfeed_tab tabs[] = {{1, NULL, true, true}, {2, NULL, false, true}};
    sk_vizfeed_tabs(feed, tabs, SK_LEN(tabs));
    sk_vizfeed_open(feed);
    sk_vizwin *v = offscreen(&f, feed);
    sk_image im;
    sk_image_init(&im, 720, 460);
    feed_window(feed, &c, v, &im, 0);
    CHECK_INT(sk_vizwin_panes(v), 2);
    /* Drift and Rings: MilkDrop's, which hear the pointer as a tone. */
    CHECK_INT(sk_vizwin_shown(v, 0), 0);
    CHECK_INT(sk_vizwin_shown(v, 1), 1);

    sk_rect p0 = sk_viz_pane_rect(0, 2, 720, 460), p1 = sk_viz_pane_rect(1, 2, 720, 460);
    sk_viz_menu m1;
    sk_viz_menu_of(p1, SK_VIZ_MODES, 720, 460, &m1);
    /* The second pane's button: the feed opens its list; the mode hears
     * nothing of it. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, m1.button.x + 4, m1.button.y + 4, 1, 1000);
    sk_vizwin_pointer(v, SK_EV_MOUSE_UP, m1.button.x + 4, m1.button.y + 4, 1, 1010);
    CHECK_INT(touches(v, 1), 0);
    CHECK_INT(sk_vizwin_held(v), 0);
    CHECK_INT(sk_vizfeed_menu_tab(feed), 0);
    sk_loop_once(loop, 0);
    CHECK_INT(sk_vizfeed_menu_tab(feed), 2);
    feed_window(feed, &c, v, &im, 33);
    /* While it is open no press reaches a mode, and a press on the
     * first pane closes it. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, p0.x + 200, p0.y + 300, 1, 1100);
    sk_vizwin_pointer(v, SK_EV_MOUSE_UP, p0.x + 200, p0.y + 300, 1, 1110);
    CHECK_INT(touches(v, 0), 0);
    sk_loop_once(loop, 0);
    CHECK_INT(sk_vizfeed_menu_tab(feed), 0);
    feed_window(feed, &c, v, &im, 66);
    /* Open again, a row picks the second pane's mode. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, m1.button.x + 4, m1.button.y + 4, 1, 1200);
    sk_vizwin_pointer(v, SK_EV_MOUSE_UP, m1.button.x + 4, m1.button.y + 4, 1, 1210);
    sk_loop_once(loop, 0);
    feed_window(feed, &c, v, &im, 99);
    int go = sk_viz_mode_find("go");
    sk_rect row = m1.items[go];
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, row.x + 10, row.y + 9, 1, 1300);
    sk_vizwin_pointer(v, SK_EV_MOUSE_UP, row.x + 10, row.y + 9, 1, 1310);
    sk_loop_once(loop, 0);
    CHECK_INT(sk_vizfeed_mode_of(feed, 2), go);
    feed_window(feed, &c, v, &im, 132);
    CHECK_INT(sk_vizwin_shown(v, 1), go);

    /* A press on the first pane's picture: a tone at once, held by the
     * pane through a drag into the second, again only every 60 ms. */
    double x = p0.x + 100, y = p0.y + 300;
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, x, y, 1, 2000);
    CHECK_INT(sk_vizwin_held(v), 1);
    CHECK_INT(touches(v, 0), 1);
    sk_vizwin_pointer(v, SK_EV_MOUSE_MOVE, x + 10, y, 0, 2030);
    CHECK_INT(touches(v, 0), 1);
    sk_vizwin_pointer(v, SK_EV_MOUSE_MOVE, p1.x + 50, y, 0, 2070);
    CHECK_INT(touches(v, 0), 2);
    CHECK_INT(touches(v, 1), 0);
    sk_vizwin_pointer(v, SK_EV_MOUSE_UP, p1.x + 50, y, 1, 2080);
    CHECK_INT(sk_vizwin_held(v), 0);
    /* Unpressed moves sound nothing. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_MOVE, x, y, 0, 3000);
    CHECK_INT(touches(v, 0), 2);
    /* Go answers the pointer itself: no tone. A right press is no
     * press. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, p1.x + 180, p1.y + 230, 1, 4000);
    CHECK_INT(sk_vizwin_held(v), 2);
    sk_vizwin_pointer(v, SK_EV_MOUSE_UP, p1.x + 180, p1.y + 230, 1, 4010);
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, p0.x + 180, p0.y + 230, 3, 4100);
    CHECK_INT(touches(v, 1), 0);
    CHECK_INT(touches(v, 0), 2);
    CHECK_INT(sk_vizwin_held(v), 0);
    /* Every left press went to the feed, which the feed's loop runs. */
    sk_loop_once(loop, 0);
    CHECK_INT(sk_vizfeed_menu_tab(feed), 0);

    /* The pane pressed and gone lets the pointer go. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_DOWN, x, y, 1, 5000);
    CHECK_INT(sk_vizwin_held(v), 1);
    sk_loop_once(loop, 0);
    sk_vizfeed_tab one[] = {{2, NULL, true, true}};
    sk_vizfeed_tabs(feed, one, 1);
    feed_window(feed, &c, v, &im, 165);
    CHECK_INT(sk_vizwin_held(v), 0);
    CHECK_INT(sk_vizwin_panes(v), 1);

    sk_image_free(&im);
    sk_vizwin_free(v);
    sk_vizfeed_msg_free(c.last);
    sk_vizfeed_free(feed);
    sk_loop_free(loop);
    fonts_close(&f);
}

/* ---- Goldens ---- */

/* panes run for frames at 60 a second, a frame of the feed every
 * second one, then drawn w by h at scale. */
static void run(sk_vizwin *v, const spec *s, size_t n, int src, int st, double start, int frames, sk_image *im, int w,
                int h, double scale)
{
    double sent = start - 1.0 / 30;
    for (int k = 0; k < frames; k++) {
        double t = start + k / 60.0;
        if (k % 2 == 0) {
            give(v, frame_of(s, n, sent, t, src, st, 1));
            sent = t;
        }
        sk_vizwin_render(v, im, w, h, scale, 1.0 / 60);
    }
}

static void test_two_panes_at_2x(void)
{
    fonts f;
    fonts_open(&f);
    sk_vizwin *v = offscreen(&f, NULL);
    spec s[] = {
        {1, sk_viz_mode_find("analyzer"), false, false, 0},
        {2, sk_viz_mode_find("dotplane"), true, false, 14},
    };
    sk_image im;
    sk_image_init(&im, 960, 600);
    /* The pointer over the first pane's button. */
    sk_vizwin_pointer(v, SK_EV_MOUSE_MOVE, 40, 12, 0, 0);
    run(v, s, 2, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 1, 90, &im, 480, 300, 2);
    CHECK_GOLDEN("vizwin_two_2x", &im, 2);
    sk_image_free(&im);
    sk_vizwin_free(v);
    fonts_close(&f);
}

static void test_four_panes_and_a_drop_down(void)
{
    fonts f;
    fonts_open(&f);
    sk_vizwin *v = offscreen(&f, NULL);
    spec s[] = {
        {1, sk_viz_mode_find("drift"), false, true, 0},
        {2, sk_viz_mode_find("go"), false, false, 4},
        {4, sk_viz_mode_find("gravity"), true, false, 8},
        {6, sk_viz_mode_find("scope"), false, false, 15},
    };
    sk_image im;
    sk_image_init(&im, 720, 460);
    /* The pointer on the drop-down's row of Fern. */
    sk_viz_menu m;
    sk_viz_menu_of(sk_viz_pane_rect(0, 4, 720, 460), SK_VIZ_MODES, 720, 460, &m);
    sk_rect fern = m.items[sk_viz_mode_find("fern")];
    sk_vizwin_pointer(v, SK_EV_MOUSE_MOVE, fern.x + 20, fern.y + 5, 0, 0);
    run(v, s, 4, SK_VIZ_SRC_BOTH, SK_VIZ_SOUND_FAILED, 2, 60, &im, 720, 460, 1);
    /* MilkDrop's pane may round apart on another machine, as its own
     * goldens allow (milk_shared.c). */
    CHECK_GOLDEN_NEAR("vizwin_four", &im, 8, 1000);
    sk_image_free(&im);
    sk_vizwin_free(v);
    fonts_close(&f);
}

/* ---- The window ---- */

static int ui_events;

static void count_event(void *ud, const sk_event *ev)
{
    sk_vizwin *v = *(sk_vizwin **)ud;
    ui_events++;
    if (v)
        sk_vizwin_event(v, ev);
}

static void test_the_window_never_takes_the_focus(void)
{
    static sk_vizwin *v;
    v = NULL;
    char *err = NULL;
    if (!sk_ui_open("skinned-tests", count_event, &v, &err)) {
        fprintf(stderr, "    skipped: %s\n", err ? err : "no display");
        free(err);
        return;
    }
    fonts f;
    fonts_open(&f);
    sk_loop *loop = sk_loop_new();
    sk_vizfeed_config fc = {0};
    fc.loop = loop;
    fc.manual = true;
    sk_vizfeed *feed = sk_vizfeed_new(&fc);
    sk_vizfeed_open(feed);
    sk_vizwin_config c = {0};
    c.mono = f.mono;
    c.cache = f.cache;
    c.feed = feed;
    c.hidden = true;
    c.title = "visualization: test";
    v = sk_vizwin_new(&c);
    spec s[] = {{1, sk_viz_mode_find("scope"), true, false, 0}};
    give(v, frame_of(s, 1, 0, 0.03, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 1));
    sk_window *w = sk_vizwin_window(v);
    CHECK(w != NULL);
    if (w) {
        CHECK(sk_window_ud(w) == v);
        int pw, ph;
        sk_window_size(w, &pw, &ph);
        CHECK_INT(pw, SK_VIZWIN_W);
        CHECK_INT(ph, SK_VIZWIN_H);
#ifdef __APPLE__
        id nw = sk_macos_nswindow(w);
        CHECK(!sk_msg_bool(nw, "canBecomeKeyWindow"));
        CHECK(!sk_msg_bool(nw, "canBecomeMainWindow"));
        sk_msg_void(nw, "makeKeyWindow");
        CHECK(!sk_msg_bool(nw, "isKeyWindow"));
#endif
        /* An event of another window is not its own. */
        sk_event other = {.type = SK_EV_FRAME};
        CHECK(!sk_vizwin_event(v, &other));
        /* A display frame draws the panes and presents them. */
        sk_event fr = {.type = SK_EV_FRAME, .win = w};
        CHECK(sk_vizwin_event(v, &fr));
        sk_image *frame = sk_window_frame(w);
        bool drawn = false;
        for (int y = 0; y < frame->h && !drawn; y++)
            for (int x = 0; x < frame->w && !drawn; x++)
                drawn = (sk_image_row(frame, y)[x] & 0xffffff) != 0;
        CHECK(drawn);
        /* The space bar steps the active pane, by the feed. */
        sk_event key = {.type = SK_EV_KEY_DOWN, .win = w};
        key.key.text[0] = ' ';
        key.key.base = ' ';
        CHECK(sk_vizwin_event(v, &key));
        sk_vizfeed_tab one[] = {{1, NULL, true, true}};
        sk_vizfeed_tabs(feed, one, 1);
        int before = sk_vizfeed_mode_of(feed, 1);
        sk_loop_once(loop, 0);
        CHECK_INT(sk_vizfeed_mode_of(feed, 1), (before + 1) % SK_VIZ_MODES);
        /* The user's close: gone at once, the feed told, and the frames
         * of that opening open nothing; the next opening opens it. */
        sk_event close = {.type = SK_EV_CLOSE, .win = w};
        CHECK(sk_vizwin_event(v, &close));
        CHECK(sk_vizwin_window(v) == NULL);
        give(v, frame_of(s, 1, 0.03, 0.06, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 1));
        CHECK(sk_vizwin_window(v) == NULL);
        CHECK(sk_vizfeed_is_open(feed));
        sk_loop_once(loop, 0);
        CHECK(!sk_vizfeed_is_open(feed));
        give(v, frame_of(s, 1, 0.06, 0.09, SK_VIZ_SRC_SESSION, SK_VIZ_SOUND_NONE, 2));
        CHECK(sk_vizwin_window(v) != NULL);
        sk_vizwin_set_title(v, "visualization: next");
    }
    sk_vizwin_free(v);
    v = NULL;
    sk_ui_close();
    sk_vizfeed_free(feed);
    sk_loop_free(loop);
    fonts_close(&f);
}

const sk_test sk_suite_vizwin[] = {
    T(test_the_panes_follow_the_frames),
    T(test_presses_go_to_the_feed_and_the_pointer_to_the_modes),
    T(test_two_panes_at_2x),
    T(test_four_panes_and_a_drop_down),
    T(test_the_window_never_takes_the_focus),
    {0},
};
