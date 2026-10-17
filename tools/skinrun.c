/* skinrun: a shipped skin live in a window (src/skin/view.h), its
 * metrics a scripted session's, to look at, hover and click.
 *
 *   skinrun [-H] [-t SECONDS] [-o OUT.qoi] SKIN
 *   skinrun -b [SKIN...]
 *
 * The session is the visualizer's script (viz/script.h), a loop of 40 s
 * of two turns and an idle spell, written as the metrics a Claude
 * session's channels write: the context filling, tokens and cost
 * counting, tool calls and their events, a subagent, the limits and a
 * task list worked through each turn. The terminal's rectangle shows
 * the theme's colours in the skin's font. A click on a readout toggles
 * its dial's mark, the toggle switches what the list replaces, the
 * menu button shows pressed while "open", a split's handle drags.
 *
 * -o reads the window back after SECONDS of the script (default 4) and
 * writes it as QOI (build/<mode>/qoi2png makes it a PNG), then quits;
 * -H keeps that window hidden, as SKINNED_HIDDEN=1 does skinned's.
 * -b opens no window: it times frames at 2x, the script's turn and its
 * idle spell, and a whole repaint at a new size, and prints them. */
#include "../src/platform/ui.h"
#include "../src/skin/shipped.h"
#include "../src/skin/view.h"
#include "../src/viz/script.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STRIP 22
#define FPS 60

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000 + (double)ts.tv_nsec / 1e6;
}

static double wall_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec;
}

/* ---- The scripted session ---- */

typedef struct {
    double t; /* the script's time, s */
    double tools, outputs, prompts, stops, compacts, agents;
    double tok_in, tok_out;
    double agent_until;
    double wall0;
    char limits[512], list[512];
    sk_skin_metric m[48];
    size_t n;
} script;

static void put_num(script *s, const char *name, double v)
{
    s->m[s->n++] = (sk_skin_metric){name, SK_NUM(v)};
}

static void put_text(script *s, const char *name, sk_val_type type, const char *text)
{
    s->m[s->n++] = (sk_skin_metric){name, {type, 0, (char *)text}};
}

static void script_init(script *s, double start)
{
    memset(s, 0, sizeof *s);
    s->t = start;
    s->tok_in = 18000;
    s->tok_out = 900;
    s->wall0 = wall_s();
}

/* The session from s->t to t: its counters moved, its metrics written. */
static void script_to(script *s, double t)
{
    sk_viz_pane_frame f;
    int ev[256];
    sk_viz_script(s->t, t, &f, ev, SK_LEN(ev));
    double dt = t - s->t;
    s->t = t;
    for (size_t i = 0; i + 1 < f.n_events; i += 2) {
        switch (ev[i]) {
        case SK_EV_TOOL:
            s->tools++;
            break;
        case SK_EV_OUTPUT:
            s->outputs++;
            break;
        case SK_EV_PROMPT:
            s->prompts++;
            break;
        case SK_EV_STOP:
            s->stops++;
            break;
        case SK_EV_COMPACT:
            s->compacts++;
            break;
        case SK_EV_AGENT:
            s->agents++;
            s->agent_until = t + 3;
            break;
        default:
            break;
        }
    }
    bool busy = f.n[SK_VIZ_BUSY] > 0;
    if (busy)
        s->tok_in += 900 * dt;
    s->tok_out += f.n[SK_VIZ_OUT_RATE] * dt;
    double u = fmod(t, SK_VIZ_SCRIPT_LOOP);
    s->n = 0;
    double ctx = f.n[SK_VIZ_CTX];
    put_num(s, "context.used_pct", ctx);
    put_num(s, "context.used_tokens", ctx * 2000);
    put_num(s, "context.window", 200000);
    put_num(s, "context.compact_at_pct", 80);
    put_num(s, "context.compact_threshold", 160000);
    put_num(s, "tokens.in", s->tok_in);
    put_num(s, "tokens.out", s->tok_out);
    put_num(s, "tokens.out_rate", f.n[SK_VIZ_OUT_RATE]);
    put_num(s, "cost.usd", s->tok_in * 3e-6 + s->tok_out * 15e-6);
    put_num(s, "cache.hit_ratio", 0.82 + 0.1 * sin(t / 7));
    put_num(s, "tools.total", s->tools);
    put_num(s, "tools.rate", busy ? 12 : 0);
    put_num(s, "agents.active", t < s->agent_until ? 1 : 0);
    put_num(s, "state.busy", busy ? 1 : 0);
    put_text(s, "model.name", SK_VAL_STR, "Opus 5.5");
    put_num(s, "session.clock_s", floor(t));
    put_num(s, "session.compactions", s->compacts);
    put_num(s, "pty.out_bps", f.n[SK_VIZ_OUT_BPS]);
    put_num(s, "pty.in_kps", f.n[SK_VIZ_IN_KPS]);
    put_num(s, "pty.idle_s", f.n[SK_VIZ_IDLE_S]);
    put_num(s, "proc.cpu_pct", f.n[SK_VIZ_CPU]);
    put_num(s, "proc.mem_mb", 420);
    put_num(s, "event.tool", s->tools);
    put_num(s, "event.output", s->outputs);
    put_num(s, "event.prompt", s->prompts);
    put_num(s, "event.stop", s->stops);
    put_num(s, "event.compact", s->compacts);
    put_num(s, "event.agent", s->agents);
    double five = 20 + 30 * u / SK_VIZ_SCRIPT_LOOP, week = 41;
    double r5 = s->wall0 + 3 * 3600 + 60, r7 = s->wall0 + 3 * 86400 + 23 * 3600 + 60;
    put_num(s, "limits.five_hour_pct", five);
    put_num(s, "limits.five_hour_resets_at", r5);
    put_num(s, "limits.seven_day_pct", week);
    put_num(s, "limits.seven_day_resets_at", r7);
    snprintf(s->limits, sizeof s->limits,
             "[{\"key\":\"five_hour\",\"label\":\"5h\",\"window\":\"5h\",\"model\":null,\"pct\":%.2f,\"resets_at\":%.0f},"
             "{\"key\":\"seven_day\",\"label\":\"7d\",\"window\":\"7d\",\"model\":null,\"pct\":%.2f,\"resets_at\":%.0f}]",
             five, r5, week, r7);
    put_text(s, "limits.items", SK_VAL_LIST, s->limits);
    /* The task list: each turn's three steps worked through, kept done
     * through the idle spell, cleared before the next turn. */
    static const char *const STEPS[2][3] = {
        {"Read the skin's manifest", "Lay the widgets out", "Paint what changed"},
        {"Run the tests", "Fix the failing one", "Commit"},
    };
    int turn = u < 13 ? 0 : u < 30 ? 1 : -1;
    if (turn >= 0) {
        double from = turn ? 14 : 0;
        int done = (int)floor((u - from) / 2.5);
        size_t at = 0;
        at += (size_t)snprintf(s->list + at, sizeof s->list - at, "[");
        for (int i = 0; i < 3; i++)
            at += (size_t)snprintf(s->list + at, sizeof s->list - at, "%s{\"text\":\"%s\",\"state\":\"%s\"}",
                                   i ? "," : "", STEPS[turn][i], i < done ? "done" : i == done ? "doing" : "todo");
        snprintf(s->list + at, sizeof s->list - at, "]");
        put_text(s, "checklist.items", SK_VAL_LIST, s->list);
        put_num(s, "checklist.count", 3);
        put_num(s, "checklist.done", SK_MIN(done, 3));
    }
}

/* ---- The window ---- */

typedef struct {
    sk_skinpack *pack;
    sk_skinview *view;
    sk_window *win;
    sk_fontset *mono, *term;
    sk_glyph_cache *cache;
    sk_skin_terminal theme;
    script sc;
    double t0;      /* the clock at the script's start */
    double start;   /* the script's start, s */
    double last_metrics;
    double timer_due;
    int timer;
    double shot_at; /* the script's time of the shot, or < 0 */
    const char *shot;
    bool done, all;
    SK_VEC(char *) dials;
    bool list_off, menu_open;
    int status;
} app;

static void request(app *a)
{
    sk_window_request_frame(a->win);
}

static void on_timer(void *ud)
{
    app *a = ud;
    a->timer = 0;
    request(a);
}

static sk_color theme(const app *a, int key, sk_color fallback)
{
    return a->theme.has & (1u << key) ? (sk_color)a->theme.colors[key] : fallback;
}

/* The identity strip, and the terminal's rectangle in the theme. */
static void draw_chrome(app *a, sk_image *f, bool strip)
{
    float s = (float)sk_window_scale(a->win);
    if (strip) {
        sk_rect r = sk_rect_make(0, 0, f->w, (int)lroundf(STRIP * s));
        sk_fill_rect(f, r, SK_RGB(0x10, 0x10, 0x10), SK_BLEND_SRC, NULL);
        sk_fill_rect(f, sk_rect_make(0, r.h - (int)lroundf(s), f->w, (int)lroundf(s)), SK_RGB(0x33, 0x33, 0x33),
                     SK_BLEND_SRC, NULL);
        char label[128];
        snprintf(label, sizeof label, "claude [%s]", sk_skinpack_name(a->pack));
        float x = 8 * s;
        x += sk_draw_text(f, a->cache, a->mono, SK_STYLE_BOLD, 12 * s, x, 15 * s, label, strlen(label),
                          SK_RGB(255, 255, 255), &r);
        const char *cmd = "skinrun: a scripted session";
        sk_draw_text(f, a->cache, a->mono, 0, 12 * s, x + 14 * s, 15 * s, cmd, strlen(cmd), SK_RGB(0x9f, 0xb3, 0xc8),
                     &r);
    }
    sk_rect t = sk_skinview_terminal_px(a->view);
    sk_fill_rect(f, t, theme(a, SK_THEME_BACKGROUND, SK_RGB(0, 0, 0)) | 0xff000000u, SK_BLEND_SRC, NULL);
    float px = sk_skin_font_px(&a->theme, 0) * s, line = px * 1.2f;
    static const char *const LINES[] = {
        "> Show the skin live, every widget from the session's metrics.",
        "",
        "  Read 1 file",
        "  - [x] Read the skin's manifest",
        "  - [>] Lay the widgets out",
        "  - [ ] Paint what changed",
    };
    float y = (float)t.y + 10 * s + px;
    for (size_t i = 0; i < SK_LEN(LINES); i++, y += line) {
        if (y > (float)(t.y + t.h))
            break;
        sk_color c = theme(a, i == 0 ? SK_THEME_FOREGROUND : SK_THEME_BLACK + (int)(i % 7) + 1, SK_RGB(220, 220, 220));
        sk_draw_text(f, a->cache, a->term, 0, px, (float)t.x + 10 * s, y, LINES[i], strlen(LINES[i]), c | 0xff000000u,
                     &t);
    }
}

static void write_qoi(const char *path, const sk_image *im)
{
    size_t n = 0;
    uint8_t *q = sk_qoi_encode(im, &n);
    FILE *f = q ? fopen(path, "wb") : NULL;
    if (!f || fwrite(q, 1, n, f) != n)
        fprintf(stderr, "skinrun: cannot write %s\n", path);
    if (f)
        fclose(f);
    free(q);
}

static void frame(app *a)
{
    double now = now_ms();
    double t = a->start + (now - a->t0) / 1000;
    /* The metrics ten times a second, as main.js sends them. */
    if (now - a->last_metrics >= 100) {
        a->last_metrics = now;
        script_to(&a->sc, t);
        sk_skinview_set_metrics(a->view, a->sc.m, a->sc.n);
    }
    double due = sk_skinview_step(a->view, now);
    sk_image *f = sk_window_frame(a->win);
    sk_rect rects[34];
    size_t n = sk_skinview_draw(a->view, f, a->all, rects, 32);
    if (a->all) {
        draw_chrome(a, f, true);
        sk_window_present(a->win, NULL, 0);
    } else if (n) {
        sk_window_present(a->win, rects, n);
    }
    a->all = false;
    if (a->shot && t >= a->shot_at && !a->done) {
        a->done = true;
        sk_image im;
        if (sk_window_readback(a->win, &im)) {
            write_qoi(a->shot, &im);
            sk_image_free(&im);
        } else {
            /* No readback here: the frame as drawn. */
            write_qoi(a->shot, f);
        }
        sk_ui_quit();
        return;
    }
    /* The next frame: at once while something moves, else at the next
     * timed change or the next metrics. */
    double wait = 100 - (now - a->last_metrics);
    if (due >= 0)
        wait = fmin(wait, due);
    if (due == 0 || wait <= 1000.0 / FPS) {
        request(a);
    } else if (!a->timer) {
        a->timer = sk_ui_timer((uint64_t)wait, 0, on_timer, a);
    }
}

static sk_cursor cursor_of(sk_skin_cursor c)
{
    switch (c) {
    case SK_SKIN_CURSOR_POINTER:
        return SK_CURSOR_POINTER;
    case SK_SKIN_CURSOR_TEXT:
    case SK_SKIN_CURSOR_NONE:
        return SK_CURSOR_IBEAM;
    case SK_SKIN_CURSOR_RESIZE_EW:
        return SK_CURSOR_RESIZE_EW;
    case SK_SKIN_CURSOR_RESIZE_NS:
        return SK_CURSOR_RESIZE_NS;
    default:
        return SK_CURSOR_ARROW;
    }
}

static void toggle_dial(app *a, const char *name)
{
    for (size_t i = 0; i < a->dials.len; i++)
        if (!strcmp(a->dials.ptr[i], name)) {
            free(a->dials.ptr[i]);
            a->dials.ptr[i] = a->dials.ptr[--a->dials.len];
            sk_skinview_set_dials(a->view, (const char *const *)a->dials.ptr, a->dials.len);
            printf("dial %s closed\n", name);
            return;
        }
    sk_vec_push(&a->dials, sk_xstrdup(name));
    sk_skinview_set_dials(a->view, (const char *const *)a->dials.ptr, a->dials.len);
    printf("dial %s open\n", name);
}

static void mouse(app *a, sk_skin_mouse what, const sk_event *ev)
{
    sk_skin_event e = sk_skinview_mouse(a->view, what, ev->mouse.x, ev->mouse.y, ev->mouse.button);
    switch (e.action) {
    case SK_SKIN_MENU:
        a->menu_open = !a->menu_open;
        sk_skinview_set_menu(a->view, a->menu_open, false);
        printf("menu %s\n", a->menu_open ? "open" : "closed");
        break;
    case SK_SKIN_DIAL:
        toggle_dial(a, e.metric);
        break;
    case SK_SKIN_TOGGLE:
        a->list_off = e.off;
        sk_skinview_set_list_off(a->view, e.off);
        printf("the list's toggle: %s\n", e.off ? "what it replaces" : "the list");
        break;
    case SK_SKIN_SPLIT_MOVE:
        a->all = true;
        break;
    case SK_SKIN_SPLIT_DONE:
        a->all = true;
        printf("split %s kept at %.0f\n", e.split, e.size);
        break;
    default:
        break;
    }
    sk_window_set_cursor(a->win, cursor_of(sk_skinview_cursor(a->view)));
    request(a);
}

static void on_event(void *ud, const sk_event *ev)
{
    app *a = ud;
    switch (ev->type) {
    case SK_EV_CLOSE:
        sk_ui_quit();
        break;
    case SK_EV_RESIZE:
        sk_skinview_set_stage(a->view, 0, STRIP, ev->size.w, ev->size.h - STRIP, ev->size.scale);
        a->all = true;
        request(a);
        break;
    case SK_EV_FRAME:
        frame(a);
        break;
    case SK_EV_MOUSE_MOVE:
        mouse(a, SK_SKIN_MOUSE_MOVE, ev);
        break;
    case SK_EV_MOUSE_DOWN:
        mouse(a, SK_SKIN_MOUSE_DOWN, ev);
        break;
    case SK_EV_MOUSE_UP:
        mouse(a, SK_SKIN_MOUSE_UP, ev);
        break;
    case SK_EV_MOUSE_LEAVE:
        sk_skinview_mouse(a->view, SK_SKIN_MOUSE_LEAVE, 0, 0, 0);
        request(a);
        break;
    case SK_EV_KEY_DOWN:
        if (ev->key.key == SK_KEY_ESCAPE)
            sk_ui_quit();
        break;
    default:
        break;
    }
}

static sk_skinpack *shipped(const char *name)
{
    const sk_embedded_file *e = sk_shipped_pack(name);
    if (!e) {
        fprintf(stderr, "skinrun: no shipped skin %s\n", name);
        return NULL;
    }
    char err[300];
    sk_skinpack *p = sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err);
    if (!p)
        fprintf(stderr, "skinrun: %s: %s\n", name, err);
    return p;
}

/* ---- Timings ---- */

typedef struct {
    double sum, worst;
    int n;
} timing;

static void add(timing *t, double ms)
{
    t->sum += ms;
    t->worst = fmax(t->worst, ms);
    t->n++;
}

/* A frame of the script at t: metrics when due, a step, a draw. */
static double bench_frame(sk_skinview *v, script *sc, sk_image *im, double t, double *last_metrics)
{
    double clock = t * 1000;
    double t0 = now_ms();
    if (clock - *last_metrics >= 100) {
        *last_metrics = clock;
        script_to(sc, t);
        sk_skinview_set_metrics(v, sc->m, sc->n);
    }
    sk_rect r[32];
    sk_skinview_step(v, clock);
    sk_skinview_draw(v, im, false, r, 32);
    return now_ms() - t0;
}

static int bench(const char *name)
{
    sk_skinpack *p = shipped(name);
    if (!p)
        return 1;
    sk_skin_window w;
    sk_skinpack_window(p, &w);
    sk_skinview *v = sk_skinview_new(p, 0);
    sk_skinpack_unref(p);
    const double scale = 2;
    sk_skinview_set_stage(v, 0, STRIP, w.width, w.height - STRIP, scale);
    sk_image im;
    sk_image_init(&im, (int)(w.width * scale), (int)(w.height * scale));
    script sc;
    script_init(&sc, 0);
    double last = -1e9;
    /* The first turn's start, to settle; then its busy frames, then the
     * idle spell's, where only values move. */
    for (double t = 0; t < 2; t += 1.0 / FPS)
        bench_frame(v, &sc, &im, t, &last);
    timing busy = {0}, idle = {0}, whole = {0};
    for (double t = 2; t < 8; t += 1.0 / FPS)
        add(&busy, bench_frame(v, &sc, &im, t, &last));
    for (double t = 8; t < 24; t += 1.0 / FPS)
        bench_frame(v, &sc, &im, t, &last);
    for (double t = 24; t < 30; t += 1.0 / FPS)
        add(&idle, bench_frame(v, &sc, &im, t, &last));
    /* A whole repaint at a new size, as a resize draws. */
    for (int i = 0; i < 10; i++) {
        double t0 = now_ms();
        sk_skinview_set_stage(v, 0, STRIP, w.width - (i & 1) * 8, w.height - STRIP - (i & 1) * 8, scale);
        sk_rect r[32];
        sk_skinview_step(v, (30 + i) * 1000.0);
        sk_skinview_draw(v, &im, true, r, 32);
        add(&whole, now_ms() - t0);
    }
    printf("%-9s 2x: turn frame %6.2f ms (worst %6.2f), idle frame %6.2f ms (worst %6.2f), resize %6.1f ms (worst "
           "%6.1f)\n",
           name, busy.sum / busy.n, busy.worst, idle.sum / idle.n, idle.worst, whole.sum / whole.n, whole.worst);
    sk_image_free(&im);
    sk_skinview_free(v);
    return 0;
}

int main(int argc, char **argv)
{
    bool hidden = false, timings = false;
    double seconds = 4, start = 0;
    const char *shot = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-H"))
            hidden = true;
        else if (!strcmp(argv[i], "-b"))
            timings = true;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            start = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            shot = argv[++i];
        else
            break;
    }
    if (timings) {
        static const char *const ALL[] = {"coffee", "deck", "library", "phosphor"};
        int status = 0;
        if (i == argc)
            for (size_t k = 0; k < SK_LEN(ALL); k++)
                status |= bench(ALL[k]);
        for (; i < argc; i++)
            status |= bench(argv[i]);
        return status;
    }
    if (argc - i != 1) {
        fprintf(stderr, "usage: skinrun [-H] [-s START] [-t SECONDS] [-o OUT.qoi] SKIN\n       skinrun -b [SKIN...]\n");
        return 2;
    }
    app a;
    memset(&a, 0, sizeof a);
    a.pack = shipped(argv[i]);
    if (!a.pack)
        return 1;
    char *err = NULL;
    if (!sk_ui_open("skinned-skinrun", on_event, &a, &err)) {
        fprintf(stderr, "skinrun: %s\n", err);
        free(err);
        return 1;
    }
    sk_skin_window w;
    sk_skinpack_window(a.pack, &w);
    sk_skinpack_terminal(a.pack, &a.theme);
    a.mono = sk_fontset_new(NULL, NULL, true);
    a.term = sk_fontset_new_face(NULL, a.theme.face, NULL, true);
    a.cache = sk_glyph_cache_new((size_t)16 << 20);
    a.t0 = now_ms();
    a.start = start;
    a.last_metrics = -1e9;
    a.shot = shot;
    a.shot_at = start + seconds;
    a.all = true;
    a.view = sk_skinview_new(a.pack, a.t0);
    script_init(&a.sc, start);
    char title[128];
    snprintf(title, sizeof title, "skinrun: %s", sk_skinpack_name(a.pack));
    sk_window_opts o = {.title = title,
                        .w = w.width,
                        .h = w.height,
                        .min_w = w.min_width,
                        .min_h = w.min_height,
                        .ud = &a,
                        .hidden = hidden};
    a.win = sk_window_new(&o, &err);
    if (!a.win) {
        fprintf(stderr, "skinrun: %s\n", err);
        free(err);
        return 1;
    }
    int pw, ph;
    sk_window_size(a.win, &pw, &ph);
    sk_skinview_set_stage(a.view, 0, STRIP, pw, ph - STRIP, sk_window_scale(a.win));
    if (!hidden)
        sk_window_focus(a.win);
    request(&a);
    bool ok = sk_ui_run();
    sk_skinview_free(a.view);
    sk_skinpack_unref(a.pack);
    sk_window_free(a.win);
    sk_glyph_cache_free(a.cache);
    sk_fontset_free(a.mono);
    sk_fontset_free(a.term);
    for (size_t k = 0; k < a.dials.len; k++)
        free(a.dials.ptr[k]);
    sk_vec_free(&a.dials);
    sk_ui_close();
    return ok ? a.status : 1;
}
