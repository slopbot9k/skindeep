/* vizwin: the visualization's window (src/app/vizwin.h) on fake tabs fed
 * by the scripted session (src/viz/script.h), to look at. Each fake
 * tab's bus is written 30 times a second from the script, the tabs some
 * seconds apart in it, and the feed (src/app/vizfeed.h) runs on a core
 * thread as the app's does. Closing the window ends it.
 *
 *   vizwin [-n PANES] [-m MODE,...] [-t SECONDS]
 *   vizwin -b [-n PANES] [-m MODE,...] [-s WxH] [-x SCALE] [-f FRAMES] [-o OUT.qoi]
 *
 * PANES is 2 unless told; -m picks each pane's mode in turn, by id; -t
 * closes the window after so many seconds and prints how many frames it
 * drew. With
 * -b no window opens: the window is drawn offscreen at 60 frames a
 * second, 720x460 points at 1x unless told, and the median, the 95th
 * percentile and the worst of the frames' times are printed in
 * milliseconds; -o writes the last frame as a QOI image. */
#include "../src/app/vizwin.h"
#include "../src/viz/script.h"
#include "../src/viz/viz.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TABS 8

typedef struct {
    sk_loop *loop;
    sk_vizfeed *feed;
    sk_vizwin *win;
    int n;
    sk_bus *bus[MAX_TABS];
    sk_source *src[MAX_TABS];
    double t; /* the script's time of the last write */
    uint64_t start;
    int frames; /* drawn in the window */
} app;

static const char *const NAMES[SK_VIZ_NUMS] = {
    "pty.out_bps", "pty.in_kps", "pty.idle_s", "tokens.out_rate", "state.busy", "proc.cpu_pct", "context.used_pct",
};

/* Each tab's bus to the script's time t, tab i 7 s ahead of the one
 * before. */
static void write_buses(app *a, double t)
{
    for (int i = 0; i < a->n; i++) {
        sk_viz_pane_frame f;
        int buf[256];
        sk_viz_script(a->t + 7 * i, t + 7 * i, &f, buf, SK_LEN(buf));
        for (int k = 0; k < SK_VIZ_NUMS; k++)
            sk_source_set_num(a->src[i], NAMES[k], f.n[k]);
        for (size_t k = 0; k + 1 < f.n_events; k += 2)
            sk_source_event(a->src[i], f.events[k], f.events[k + 1], false);
    }
    a->t = t;
}

static void on_script(sk_loop *loop, void *ud)
{
    (void)loop;
    app *a = ud;
    write_buses(a, (double)(sk_now_ms() - a->start) / 1000.0);
}

static void ui_frame(void *ud)
{
    void **pair = ud;
    app *a = pair[0];
    sk_vizfeed_msg *m = pair[1];
    free(pair);
    sk_vizwin_frame(a->win, m);
    if (!m->open)
        sk_ui_quit();
    sk_vizfeed_msg_free(m);
}

/* The feed's frame, from the core thread to the window's. */
static void post_frame(void *ud, sk_vizfeed_msg *m)
{
    void **pair = sk_xmalloc(2 * sizeof *pair);
    pair[0] = ud;
    pair[1] = m;
    sk_ui_post(ui_frame, pair);
}

static void on_event(void *ud, const sk_event *ev)
{
    app *a = ud;
    if (a->win && sk_vizwin_event(a->win, ev) && ev->type == SK_EV_FRAME)
        a->frames++;
}

static void time_up(void *ud)
{
    (void)ud;
    sk_ui_quit();
}

static void on_log(void *ud, const char *msg)
{
    (void)ud;
    fprintf(stderr, "vizwin: %s\n", msg);
}

static void *core_main(void *ud)
{
    sk_loop_run(((app *)ud)->loop);
    return NULL;
}

static void stop_loop(void *ud)
{
    sk_loop_stop(ud);
}

/* The fake tabs, their buses and the feed, with modes as -m names them. */
static bool make_tabs(app *a, sk_vizfeed_config *fc, const char *modes)
{
    a->feed = sk_vizfeed_new(fc);
    sk_vizfeed_tab tabs[MAX_TABS];
    for (int i = 0; i < a->n; i++) {
        a->bus[i] = sk_bus_new(NULL, NULL);
        sk_source_spec spec = {NULL, 0, NULL, 0};
        a->src[i] = sk_bus_source(a->bus[i], &spec);
        sk_source_begin(a->src[i]);
        tabs[i] = (sk_vizfeed_tab){i + 1, a->bus[i], i == 0, true};
    }
    sk_vizfeed_tabs(a->feed, tabs, (size_t)a->n);
    char *list = sk_xstrdup(modes ? modes : "");
    int i = 0;
    for (char *s = list, *end; *s && i < a->n; s = end, i++) {
        end = s + strcspn(s, ",");
        if (*end)
            *end++ = 0;
        int m = sk_viz_mode_find(s);
        if (m < 0) {
            fprintf(stderr, "vizwin: no mode %s\n", s);
            free(list);
            return false;
        }
        sk_vizfeed_set_mode(a->feed, i + 1, m);
    }
    free(list);
    sk_vizfeed_open(a->feed);
    return true;
}

static void free_tabs(app *a)
{
    sk_vizfeed_free(a->feed);
    for (int i = 0; i < a->n; i++)
        sk_bus_free(a->bus[i]);
}

static int windowed(app *a, const char *modes, double seconds)
{
    char *err = NULL;
    if (!sk_ui_open("skinned-vizwin", on_event, a, &err)) {
        fprintf(stderr, "vizwin: %s\n", err ? err : "no display");
        free(err);
        return 1;
    }
    static const char *const mono[] = {"DejaVu Sans Mono", NULL};
    sk_fontset *set = sk_fontset_new(NULL, mono, true);
    sk_glyph_cache *cache = sk_glyph_cache_new((size_t)16 << 20);
    a->loop = sk_loop_new();
    sk_vizfeed_config fc = {0};
    fc.loop = a->loop;
    fc.frame = post_frame;
    fc.log = on_log;
    fc.ud = a;
    int status = 1;
    if (make_tabs(a, &fc, modes)) {
        sk_vizwin_config c = {0};
        c.mono = set;
        c.cache = cache;
        c.feed = a->feed;
        c.title = "visualization: vizwin";
        a->win = sk_vizwin_new(&c);
        a->start = sk_now_ms();
        sk_loop_timer(a->loop, 0, 33, on_script, a);
        if (seconds > 0)
            sk_ui_timer((uint64_t)(seconds * 1000), 0, time_up, NULL);
        pthread_t core;
        if (pthread_create(&core, NULL, core_main, a) == 0) {
            sk_ui_run();
            sk_loop_post(a->loop, stop_loop, a->loop);
            pthread_join(core, NULL);
            if (seconds > 0)
                printf("%d frames in %g s\n", a->frames, seconds);
            status = 0;
        }
        sk_vizwin_free(a->win);
        a->win = NULL;
    }
    free_tabs(a);
    sk_ui_close();
    sk_loop_free(a->loop);
    sk_glyph_cache_free(cache);
    sk_fontset_free(set);
    return status;
}

/* ---- -b ---- */

static void offscreen_frame(void *ud, sk_vizfeed_msg *m)
{
    sk_vizwin_frame(((app *)ud)->win, m);
    sk_vizfeed_msg_free(m);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int by_value(const void *x, const void *y)
{
    double p = *(const double *)x, q = *(const double *)y;
    return (p > q) - (p < q);
}

static int bench(app *a, const char *modes, int w, int h, double scale, int frames, const char *out)
{
    static const char *const mono[] = {"DejaVu Sans Mono", NULL};
    sk_fontset *set = sk_fontset_new(NULL, mono, true);
    sk_glyph_cache *cache = sk_glyph_cache_new((size_t)16 << 20);
    a->loop = sk_loop_new();
    sk_vizfeed_config fc = {0};
    fc.loop = a->loop;
    fc.manual = true;
    fc.frame = offscreen_frame;
    fc.log = on_log;
    fc.ud = a;
    int status = 1;
    if (make_tabs(a, &fc, modes)) {
        sk_vizwin_config c = {0};
        c.mono = set;
        c.cache = cache;
        c.offscreen = true;
        a->win = sk_vizwin_new(&c);
        sk_image im;
        sk_image_init(&im, (int)(w * scale + 0.5), (int)(h * scale + 0.5));
        double *ms = sk_xcalloc((size_t)frames, sizeof *ms);
        for (int k = 0; k < frames; k++) {
            if (k % 2 == 0) {
                write_buses(a, k / 60.0);
                sk_vizfeed_tick(a->feed, (uint64_t)(k * 1000 / 60));
            }
            double t0 = now_ms();
            sk_vizwin_render(a->win, &im, w, h, scale, 1.0 / 60);
            ms[k] = now_ms() - t0;
        }
        qsort(ms, (size_t)frames, sizeof *ms, by_value);
        printf("%d panes, %dx%d at %gx:", a->n, w, h, scale);
        for (size_t i = 0; i < sk_vizwin_panes(a->win); i++)
            printf(" %s", sk_viz_modes[sk_vizwin_shown(a->win, i)].id);
        printf(": median %.2f ms, p95 %.2f, worst %.2f over %d frames\n", ms[frames / 2],
               ms[(int)(frames * 0.95)], ms[frames - 1], frames);
        free(ms);
        status = 0;
        if (out) {
            size_t n = 0;
            uint8_t *q = sk_qoi_encode(&im, &n);
            FILE *f = q ? fopen(out, "wb") : NULL;
            if (!f || fwrite(q, 1, n, f) != n) {
                fprintf(stderr, "vizwin: cannot write %s\n", out);
                status = 1;
            }
            if (f && fclose(f) != 0)
                status = 1;
            free(q);
        }
        sk_image_free(&im);
        sk_vizwin_free(a->win);
        a->win = NULL;
    }
    free_tabs(a);
    sk_loop_free(a->loop);
    sk_glyph_cache_free(cache);
    sk_fontset_free(set);
    return status;
}

static int usage(void)
{
    fprintf(stderr, "usage: vizwin [-n PANES] [-m MODE,...] [-t SECONDS]\n"
                    "       vizwin -b [-n PANES] [-m MODE,...] [-s WxH] [-x SCALE] [-f FRAMES] [-o OUT.qoi]\n");
    return 2;
}

int main(int argc, char **argv)
{
    app a;
    memset(&a, 0, sizeof a);
    a.n = 2;
    int w = SK_VIZWIN_W, h = SK_VIZWIN_H, frames = 300;
    double scale = 1, seconds = 0;
    bool timed = false;
    const char *modes = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        const char *o = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(o, "-b"))
            timed = true;
        else if (!strcmp(o, "-n") && more)
            a.n = atoi(argv[++i]);
        else if (!strcmp(o, "-m") && more)
            modes = argv[++i];
        else if (!strcmp(o, "-s") && more && sscanf(argv[i + 1], "%dx%d", &w, &h) == 2)
            i++;
        else if (!strcmp(o, "-x") && more)
            scale = atof(argv[++i]);
        else if (!strcmp(o, "-f") && more)
            frames = atoi(argv[++i]);
        else if (!strcmp(o, "-o") && more)
            out = argv[++i];
        else if (!strcmp(o, "-t") && more)
            seconds = atof(argv[++i]);
        else
            return usage();
    }
    if (a.n < 1 || a.n > MAX_TABS || w < 1 || h < 1 || w > 8192 || h > 8192 || !(scale > 0 && scale <= 4) ||
        frames < 1 || frames > 1000000)
        return usage();
    return timed ? bench(&a, modes, w, h, scale, frames, out) : windowed(&a, modes, seconds);
}
