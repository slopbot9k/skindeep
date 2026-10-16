/* windemo: phase 3's window, drawing and text together (decision 0036).
 * One window draws text through the glyph cache at the display's
 * scale, with fallback faces, and echoes the keys, clicks, scrolls and
 * resizes it gets; the corner shows how long the last frame took.
 *
 *   windemo [--frames N] [--shot FILE.qoi]
 *
 * --frames draws N frames as fast as the display allows, prints their
 * timings and quits; --shot then saves what the system shows of the
 * window, read back, as a QOI image. */
#include "../src/platform/ui.h"
#include "../src/text/text.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_LINES 14

typedef struct {
    sk_window *win;
    sk_fonts *fonts;
    sk_fontset *mono, *sans;
    sk_glyph_cache *cache;
    char log[LOG_LINES][128];
    int logged;
    double mx, my;
    int frames, frames_wanted;
    double draw_ms, worst_ms, total_ms;
    const char *shot;
    bool shot_done;
} demo;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000 + (double)ts.tv_nsec / 1e6;
}

static void say(demo *d, const char *fmt, ...) SK_PRINTF(2, 3);
static void say(demo *d, const char *fmt, ...)
{
    if (d->logged == LOG_LINES) {
        memmove(d->log[0], d->log[1], sizeof d->log[0] * (LOG_LINES - 1));
        d->logged--;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->log[d->logged++], sizeof d->log[0], fmt, ap);
    va_end(ap);
}

static const char *key_name(sk_key k)
{
    static char f[8];
    switch (k) {
    case SK_KEY_NONE: return "";
    case SK_KEY_ENTER: return "Enter";
    case SK_KEY_TAB: return "Tab";
    case SK_KEY_BACKSPACE: return "Backspace";
    case SK_KEY_ESCAPE: return "Escape";
    case SK_KEY_UP: return "Up";
    case SK_KEY_DOWN: return "Down";
    case SK_KEY_LEFT: return "Left";
    case SK_KEY_RIGHT: return "Right";
    case SK_KEY_HOME: return "Home";
    case SK_KEY_END: return "End";
    case SK_KEY_PAGE_UP: return "PageUp";
    case SK_KEY_PAGE_DOWN: return "PageDown";
    case SK_KEY_INSERT: return "Insert";
    case SK_KEY_DELETE: return "Delete";
    default:
        if (k >= SK_KEY_F1 && k <= SK_KEY_F24) {
            snprintf(f, sizeof f, "F%d", (int)(k - SK_KEY_F1) + 1);
            return f;
        }
        return "(key)";
    }
}

static void mods(char *out, size_t n, uint32_t m)
{
    snprintf(out, n, "%s%s%s%s", m & SK_MOD_CTRL ? "Ctrl+" : "", m & SK_MOD_ALT ? "Alt+" : "",
             m & SK_MOD_SUPER ? "Super+" : "", m & SK_MOD_SHIFT ? "Shift+" : "");
}

static void draw(demo *d)
{
    double t0 = now_ms();
    sk_image *im = sk_window_frame(d->win);
    float s = (float)sk_window_scale(d->win);
    sk_fill_rect(im, sk_image_bounds(im), SK_RGB(24, 26, 33), SK_BLEND_SRC, NULL);
    sk_fill_rect(im, sk_rect_make(0, 0, im->w, (int)(44 * s)), SK_RGB(36, 40, 52), SK_BLEND_SRC, NULL);
    float px = 14 * s, line = 20 * s, x = 16 * s, y = 28 * s;
    char text[256];
    int w, h;
    sk_window_size(d->win, &w, &h);
    snprintf(text, sizeof text, "skinned windemo  %dx%d points at %.2fx: %dx%d pixels", w, h, s, im->w, im->h);
    sk_draw_text(im, d->cache, d->sans, SK_STYLE_BOLD, 15 * s, x, y, text, strlen(text), SK_RGB(235, 238, 245), NULL);
    snprintf(text, sizeof text, "frame %d in %.2f ms", d->frames, d->draw_ms);
    float tw = sk_measure_text(d->mono, 0, px, text, strlen(text));
    sk_draw_text(im, d->cache, d->mono, 0, px, (float)im->w - tw - x, y, text, strlen(text), SK_RGB(150, 220, 150),
                 NULL);
    y = 76 * s;
    static const char *const samples[] = {
        "DejaVu Sans Mono: The quick brown fox jumps over the lazy dog. 0O 1lI {}[]()<>",
        "Fallback: \xe4\xb8\xad\xe6\x96\x87 \xed\x95\x9c\xea\xb5\xad\xec\x96\xb4 \xe2\x8c\x98 \xe2\x8e\x87 "
        "\xe2\x86\x92 \xe2\x9c\x93 \xe2\x94\x80\xe2\x94\x82\xe2\x94\x8c\xe2\x94\x90 \xe2\x96\x88\xe2\x96\x93\xe2\x96\x92",
    };
    for (size_t i = 0; i < SK_LEN(samples); i++, y += line)
        sk_draw_text(im, d->cache, d->mono, 0, px, x, y, samples[i], strlen(samples[i]), SK_RGB(220, 223, 230), NULL);
    static const struct {
        int style;
        const char *name;
    } styles[] = {{SK_STYLE_BOLD, "Bold"}, {SK_STYLE_ITALIC, "Oblique"}, {SK_STYLE_BOLD | SK_STYLE_ITALIC, "Bold Oblique"}};
    for (size_t i = 0; i < SK_LEN(styles); i++, y += line) {
        snprintf(text, sizeof text, "%s: sphinx of black quartz, judge my vow", styles[i].name);
        sk_draw_text(im, d->cache, d->mono, styles[i].style, px, x, y, text, strlen(text), SK_RGB(220, 223, 230), NULL);
    }
    const char *sans = "DejaVu Sans, kerned: AV To Wa Ye \xe2\x80\x94 resize, type, click and scroll.";
    sk_draw_text(im, d->cache, d->sans, 0, 17 * s, x, y + 6 * s, sans, strlen(sans), SK_RGB(240, 200, 120), NULL);
    y += 2 * line;
    snprintf(text, sizeof text, "mouse %.1f, %.1f", d->mx, d->my);
    sk_draw_text(im, d->cache, d->mono, 0, px, x, y, text, strlen(text), SK_RGB(140, 170, 255), NULL);
    y += line;
    for (int i = 0; i < d->logged; i++, y += line)
        sk_draw_text(im, d->cache, d->mono, 0, px, x, y, d->log[i], strlen(d->log[i]), SK_RGB(190, 195, 205), NULL);
    sk_window_present(d->win, NULL, 0);
    d->draw_ms = now_ms() - t0;
    d->total_ms += d->draw_ms;
    if (d->draw_ms > d->worst_ms)
        d->worst_ms = d->draw_ms;
    d->frames++;
}

static void finish(demo *d)
{
    printf("windemo: %d frames, mean %.2f ms, worst %.2f ms\n", d->frames, d->total_ms / d->frames, d->worst_ms);
    if (d->shot && !d->shot_done) {
        d->shot_done = true;
        sk_image im;
        if (!sk_window_readback(d->win, &im)) {
            fprintf(stderr, "windemo: cannot read the window back\n");
        } else {
            size_t n = 0;
            uint8_t *q = sk_qoi_encode(&im, &n);
            FILE *f = fopen(d->shot, "wb");
            if (!q || !f || fwrite(q, 1, n, f) != n)
                fprintf(stderr, "windemo: cannot write %s\n", d->shot);
            if (f)
                fclose(f);
            free(q);
            sk_image_free(&im);
        }
    }
    sk_ui_quit();
}

static void on_event(void *ud, const sk_event *ev)
{
    demo *d = ud;
    char m[64];
    switch (ev->type) {
    case SK_EV_CLOSE:
        sk_ui_quit();
        return;
    case SK_EV_FRAME:
        draw(d);
        if (d->frames_wanted && d->frames >= d->frames_wanted)
            finish(d);
        else if (d->frames_wanted)
            sk_window_request_frame(d->win);
        return;
    case SK_EV_RESIZE:
        say(d, "resize %dx%d at %.2fx", ev->size.w, ev->size.h, ev->size.scale);
        break;
    case SK_EV_FOCUS:
        say(d, "focus %s", ev->focused ? "in" : "out");
        break;
    case SK_EV_KEY_DOWN:
        mods(m, sizeof m, ev->key.mods);
        say(d, "key %s%s text \"%s\" base %u%s", m, ev->key.key ? key_name(ev->key.key) : "", ev->key.text,
            (unsigned)ev->key.base, ev->key.repeat ? " repeat" : "");
        break;
    case SK_EV_MOUSE_DOWN:
        say(d, "button %d down at %.1f, %.1f, clicks %d", ev->mouse.button, ev->mouse.x, ev->mouse.y,
            ev->mouse.clicks);
        break;
    case SK_EV_MOUSE_MOVE:
        d->mx = ev->mouse.x;
        d->my = ev->mouse.y;
        break;
    case SK_EV_SCROLL:
        say(d, "scroll %.2f, %.2f %s", ev->scroll.dx, ev->scroll.dy, ev->scroll.precise ? "points" : "lines");
        break;
    default:
        return;
    }
    sk_window_request_frame(d->win);
}

int main(int argc, char **argv)
{
    demo d;
    memset(&d, 0, sizeof d);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            d.frames_wanted = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            d.shot = argv[++i];
        else {
            fprintf(stderr, "usage: windemo [--frames N] [--shot FILE.qoi]\n");
            return 2;
        }
    }
    if (d.shot && !d.frames_wanted)
        d.frames_wanted = 3;
    char *err = NULL;
    if (!sk_ui_open("skinned-windemo", on_event, &d, &err)) {
        fprintf(stderr, "windemo: %s\n", err);
        free(err);
        return 1;
    }
    double t0 = now_ms();
    d.fonts = sk_fonts_scan(NULL);
    printf("windemo: %zu system faces scanned in %.0f ms\n", sk_fonts_count(d.fonts), now_ms() - t0);
    d.mono = sk_fontset_new(d.fonts, NULL, true);
    d.sans = sk_fontset_new(d.fonts, NULL, false);
    d.cache = sk_glyph_cache_new((size_t)32 << 20);
    sk_window_opts o = {.title = "skinned windemo", .w = 900, .h = 560, .min_w = 320, .min_h = 200, .ud = &d};
    d.win = sk_window_new(&o, &err);
    if (!d.win) {
        fprintf(stderr, "windemo: %s\n", err);
        free(err);
        return 1;
    }
    sk_window_focus(d.win);
    sk_window_request_frame(d.win);
    bool ok = sk_ui_run();
    /* The cache keys glyphs by face, so it goes before the faces do. */
    sk_glyph_cache_free(d.cache);
    sk_fontset_free(d.mono);
    sk_fontset_free(d.sans);
    sk_window_free(d.win);
    sk_fonts_free(d.fonts);
    sk_ui_close();
    return ok ? 0 : 1;
}
