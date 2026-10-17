/* The window layer over X11 (platform/x11/xui.c) against the fake server
 * on any system: windows and their properties, frames presented in
 * chunks and read back, resizes, keys, the mouse, closing, the clipboard
 * both ways with INCR, files dropped by XDND, timers, tasks from other
 * threads, and a lost connection. */
#include "../../src/base/log.h"
#include "../../src/core/loop.h"
#include "../../src/platform/x11/x11.h"
#include "../../src/platform/x11/xui.h"
#include "../test.h"
#include "x11_fake.h"

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_EVENTS 128

static struct {
    sk_event ev[MAX_EVENTS];
    size_t n;
    char *clips[8]; /* what each clipboard read gave */
    int clip_calls;
    char *dropped; /* the last drop's paths, a line each, which last only the call */
} rec;

static void forget(void)
{
    for (size_t i = 0; i < SK_LEN(rec.clips); i++)
        free(rec.clips[i]);
    free(rec.dropped);
    memset(&rec, 0, sizeof rec);
}

static void on_event(void *ud, const sk_event *e)
{
    (void)ud;
    if (rec.n >= MAX_EVENTS)
        return;
    rec.ev[rec.n] = *e;
    if (e->type == SK_EV_DROP) {
        sk_buf b = {0};
        for (size_t i = 0; i < e->drop.n; i++)
            sk_buf_printf(&b, "%s%s", i ? "\n" : "", e->drop.paths[i]);
        free(rec.dropped);
        rec.dropped = b.ptr;
        rec.ev[rec.n].drop.paths = NULL;
    }
    rec.n++;
}

static sk_xfake *start(const sk_xfake_opts *o)
{
    forget();
    sk_xfake *f = sk_xfake_start(o);
    char *err = NULL;
    if (!sk_xui_open(sk_xfake_display(f), "skinned-test", on_event, NULL, &err)) {
        sk_test_fail(__FILE__, __LINE__, "open: %s", err ? err : "?");
        free(err);
        sk_xfake_stop(f);
        return NULL;
    }
    return f;
}

static void finish(sk_xfake *f)
{
    sk_xui_close();
    sk_xfake_stop(f);
    forget();
}

static sk_window *window(int w, int h, bool hidden)
{
    sk_window_opts o = {"t", w, h, 0, 0, NULL, hidden, false, false, 0, 0};
    char *err = NULL;
    sk_window *win = sk_xui_window_new(&o, &err);
    if (!win) {
        sk_test_fail(__FILE__, __LINE__, "window: %s", err ? err : "?");
        free(err);
    }
    return win;
}

static size_t count_of(sk_event_type type)
{
    size_t n = 0;
    for (size_t i = 0; i < rec.n; i++)
        n += rec.ev[i].type == type;
    return n;
}

static const sk_event *nth_of(sk_event_type type, size_t nth)
{
    for (size_t i = 0; i < rec.n; i++)
        if (rec.ev[i].type == type && nth-- == 0)
            return &rec.ev[i];
    return NULL;
}

/* Runs the UI loop until n events of type came, or ms passed. */
static const sk_event *wait_for(sk_event_type type, size_t n, int ms)
{
    uint64_t end = sk_now_ms() + (uint64_t)ms;
    while (count_of(type) < n && sk_now_ms() < end)
        sk_xui_once(5);
    return nth_of(type, n - 1);
}

static void run_ms(int ms)
{
    uint64_t end = sk_now_ms() + (uint64_t)ms;
    while (sk_now_ms() < end)
        sk_xui_once(5);
}

static bool wait_request(sk_xfake *f, int major, size_t n)
{
    uint64_t end = sk_now_ms() + 2000;
    while (sk_xfake_count(f, major, -1) < n && sk_now_ms() < end)
        sk_xui_once(5);
    return sk_xfake_count(f, major, -1) >= n;
}

static void pattern(sk_image *im, uint32_t seed)
{
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            sk_image_row(im, y)[x] = 0xff000000u | ((uint32_t)(x * 3 + seed) & 255) << 16 |
                                     ((uint32_t)(y * 5) & 255) << 8 | ((uint32_t)(x + y) & 255);
}

static bool same(const sk_image *a, const sk_image *b)
{
    if (a->w != b->w || a->h != b->h)
        return false;
    for (int y = 0; y < a->h; y++)
        if (memcmp(sk_image_row(a, y), sk_image_row(b, y), (size_t)a->w * 4))
            return false;
    return true;
}

static void key_event(sk_xfake *f, uint8_t type, uint32_t win, uint8_t code, uint32_t time,
                      uint16_t state)
{
    uint8_t ev[32] = {type, code};
    sk_x11_set32(ev + 4, time);
    sk_x11_set32(ev + 8, SK_XFAKE_ROOT);
    sk_x11_set32(ev + 12, win);
    sk_x11_set16(ev + 28, state);
    ev[30] = 1;
    sk_xfake_event(f, ev);
}

static void pointer_event(sk_xfake *f, uint8_t type, uint32_t win, uint8_t button, int x, int y,
                          uint32_t time, uint16_t state)
{
    uint8_t ev[32] = {type, button};
    sk_x11_set32(ev + 4, time);
    sk_x11_set32(ev + 8, SK_XFAKE_ROOT);
    sk_x11_set32(ev + 12, win);
    sk_x11_set16(ev + 24, (uint32_t)x);
    sk_x11_set16(ev + 26, (uint32_t)y);
    sk_x11_set16(ev + 28, state);
    /* Enter and Leave's mode, Normal; the others' same-screen. */
    ev[30] = type == SK_X11_ENTER_NOTIFY || type == SK_X11_LEAVE_NOTIFY ? 0 : 1;
    sk_xfake_event(f, ev);
}

static uint32_t created_id(sk_xfake *f, int major, size_t nth)
{
    size_t len;
    uint8_t *r = sk_xfake_request(f, major, -1, nth, &len);
    uint32_t id = r && len >= 8 ? sk_x11_get32(r + 4) : 0;
    free(r);
    return id;
}

static void test_open_and_a_window_s_properties(void)
{
    sk_xfake_opts o = {.resources = "Xft.dpi:\t192\n", .big_requests = true};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    CHECK_INT(sk_xfake_count(f, 98, -1), 2);
    CHECK_INT(sk_xfake_count(f, 130, -1), 1);
    CHECK_INT(sk_xfake_count(f, 101, -1), 1);
    CHECK_INT(sk_xfake_count(f, 119, -1), 1);
    CHECK_INT(sk_xfake_count(f, 16, -1), 24);
    sk_window_opts wo = {"h\xc3\xa9llo", 40, 30, 20, 10, &rec, true, false, false, 0, 0};
    char *err = NULL;
    sk_window *w = sk_xui_window_new(&wo, &err);
    CHECK(w != NULL);
    if (!w) {
        free(err);
        finish(f);
        return;
    }
    CHECK(sk_xui_window_ud(w) == &rec);
    CHECK(sk_xui_window_scale(w) == 2.0);
    int pw, ph;
    sk_xui_window_size(w, &pw, &ph);
    CHECK_INT(pw, 40);
    CHECK_INT(ph, 30);
    CHECK_INT(sk_xui_window_frame(w)->w, 80);
    CHECK_INT(sk_xui_window_frame(w)->h, 60);

    uint32_t id = sk_xui_window_id(w);
    size_t len;
    uint8_t *r = sk_xfake_request(f, 1, -1, 1, &len); /* the first is the selections' window */
    CHECK(r && sk_x11_get32(r + 4) == id);
    CHECK(r && r[1] == 24 && sk_x11_get16(r + 16) == 80 && sk_x11_get16(r + 18) == 60);
    CHECK(r && sk_x11_get32(r + 24) == SK_XFAKE_VISUAL);
    free(r);

    uint32_t type = 0;
    char *v = (char *)sk_xfake_property(f, id, sk_xfake_atom(f, "_NET_WM_NAME"), &len, &type);
    CHECK(v && len == 6 && !memcmp(v, "h\xc3\xa9llo", 6));
    CHECK_INT(type, sk_xfake_atom(f, "UTF8_STRING"));
    free(v);
    v = (char *)sk_xfake_property(f, id, SK_X11_WM_NAME, &len, &type);
    CHECK(v && len == 5 && !memcmp(v, "h\xe9llo", 5));
    CHECK_INT(type, SK_X11_STRING);
    free(v);
    v = (char *)sk_xfake_property(f, id, SK_X11_WM_CLASS, &len, &type);
    CHECK(v && len == 26 && !memcmp(v, "skinned-test\0skinned-test\0", 26));
    free(v);
    uint8_t *hints = sk_xfake_property(f, id, SK_X11_WM_NORMAL_HINTS, &len, &type);
    CHECK(hints && len == 72);
    CHECK(hints && sk_x11_get32(hints) == 16 && sk_x11_get32(hints + 20) == 40 &&
          sk_x11_get32(hints + 24) == 20);
    free(hints);
    uint8_t *pid = sk_xfake_property(f, id, sk_xfake_atom(f, "_NET_WM_PID"), &len, &type);
    CHECK(pid && len == 4 && sk_x11_get32(pid) == (uint32_t)getpid());
    free(pid);
    uint8_t *protocols = sk_xfake_property(f, id, sk_xfake_atom(f, "WM_PROTOCOLS"), &len, &type);
    CHECK(protocols && len == 8 &&
          sk_x11_get32(protocols) == sk_xfake_atom(f, "WM_DELETE_WINDOW") &&
          sk_x11_get32(protocols + 4) == sk_xfake_atom(f, "_NET_WM_PING"));
    free(protocols);
    /* XDND's version 5: files may be dropped on it. */
    uint8_t *aware = sk_xfake_property(f, id, sk_xfake_atom(f, "XdndAware"), &len, &type);
    CHECK(aware && len == 4 && sk_x11_get32(aware) == 5);
    CHECK_INT(type, SK_X11_ATOM);
    free(aware);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 8, -1), 0); /* hidden: never mapped */
    CHECK_INT(sk_xfake_count(f, 53, -1), 1);

    size_t props = sk_xfake_count(f, 18, -1);
    sk_xui_window_set_title(w, "next");
    CHECK(wait_request(f, 18, props + 2));
    v = (char *)sk_xfake_property(f, id, SK_X11_WM_NAME, &len, &type);
    CHECK(v && len == 4 && !memcmp(v, "next", 4));
    free(v);
    sk_xui_window_free(w);
    CHECK(wait_request(f, 4, 1));
    finish(f);
}

static void test_present_and_readback_in_chunks(void)
{
    sk_xfake_opts o = {.max_request = 4096};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    CHECK(!sk_xui_shm());
    sk_window *w = window(80, 60, true);
    sk_image *im = sk_xui_window_frame(w);
    CHECK_INT(im->w, 80);
    pattern(im, 0);
    sk_xui_window_present(w, NULL, 0);
    CHECK(sk_xui_sync());
    /* 19200 bytes under a maximum of 16384: two requests. */
    CHECK_INT(sk_xfake_count(f, 72, -1), 2);
    sk_image back;
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);
    int fw = 0, fh = 0;
    uint32_t *px = sk_xfake_pixels(f, created_id(f, 53, 0), &fw, &fh);
    CHECK(px && fw == 80 && fh == 60 && !memcmp(px, im->px, 80 * 60 * 4));
    free(px);

    /* One dirty rectangle, one request of its size; one past the edge is
     * clipped, an empty one sends nothing. */
    for (int y = 7; y < 17; y++)
        for (int x = 5; x < 15; x++)
            sk_image_row(im, y)[x] = 0xff123456;
    sk_rect dirty[] = {sk_rect_make(5, 7, 10, 10), sk_rect_make(70, 50, 30, 30),
                       sk_rect_make(0, 0, 0, 5)};
    sk_xui_window_present(w, dirty, SK_LEN(dirty));
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 72, -1), 4);
    size_t len;
    uint8_t *r = sk_xfake_request(f, 72, -1, 2, &len);
    CHECK(r && sk_x11_get16(r + 12) == 10 && sk_x11_get16(r + 14) == 10 &&
          sk_x11_get16(r + 16) == 5 && sk_x11_get16(r + 18) == 7);
    free(r);
    r = sk_xfake_request(f, 72, -1, 3, &len);
    CHECK(r && sk_x11_get16(r + 12) == 10 && sk_x11_get16(r + 14) == 10 &&
          sk_x11_get16(r + 16) == 70 && sk_x11_get16(r + 18) == 50);
    free(r);
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);
    finish(f);
}

static void test_big_frames_go_in_big_requests(void)
{
    sk_xfake_opts o = {.big_requests = true};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    sk_window *w = window(600, 500, true);
    sk_image *im = sk_xui_window_frame(w);
    pattern(im, 7);
    sk_xui_window_present(w, NULL, 0);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 72, -1), 2);
    CHECK(sk_xfake_big_count(f) >= 1);
    sk_image back;
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);
    finish(f);
}

static void test_frames_through_mit_shm(void)
{
    sk_xfake_opts o = {.shm = true};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    CHECK(sk_xui_shm());
    sk_window *w = window(80, 60, true);
    CHECK(sk_xui_shm());
    CHECK_INT(sk_xfake_count(f, SK_XFAKE_SHM_OPCODE, 1), 1);
    sk_image *im = sk_xui_window_frame(w);
    CHECK(!im->owned);
    pattern(im, 5);
    sk_xui_window_present(w, NULL, 0);
    CHECK_INT(sk_xfake_count(f, SK_XFAKE_SHM_OPCODE, 3), 1);
    CHECK_INT(sk_xfake_count(f, 72, -1), 0);
    sk_image back;
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);

    /* Two rectangles: only the last asks for the completion. */
    sk_image_row(im, 3)[4] = 0xff010203;
    sk_image_row(im, 22)[21] = 0xff040506;
    sk_rect dirty[] = {sk_rect_make(0, 0, 10, 10), sk_rect_make(20, 20, 5, 5)};
    sk_xui_window_present(w, dirty, 2);
    size_t len;
    uint8_t *r = sk_xfake_request(f, SK_XFAKE_SHM_OPCODE, 3, 1, &len);
    CHECK(r && len == 40 && r[30] == 0 && sk_x11_get16(r + 20) == 10);
    free(r);
    r = sk_xfake_request(f, SK_XFAKE_SHM_OPCODE, 3, 2, &len);
    CHECK(r && r[30] == 1 && sk_x11_get16(r + 16) == 20 && sk_x11_get16(r + 22) == 5);
    free(r);
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);

    /* A resize lets the segment go and attaches another. */
    sk_xfake_configure(f, sk_xui_window_id(w), 40, 30);
    CHECK(wait_for(SK_EV_RESIZE, 1, 2000) != NULL);
    im = sk_xui_window_frame(w);
    CHECK(im->w == 40 && !im->owned);
    pattern(im, 6);
    sk_xui_window_present(w, NULL, 0);
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);
    CHECK_INT(sk_xfake_count(f, SK_XFAKE_SHM_OPCODE, 2), 1);
    CHECK_INT(sk_xfake_count(f, SK_XFAKE_SHM_OPCODE, 1), 2);
    sk_xui_window_free(w);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, SK_XFAKE_SHM_OPCODE, 2), 2);
    finish(f);
}

static void test_a_refused_segment_falls_back_to_put_image(void)
{
    sk_xfake_opts o = {.shm = true, .shm_refuse = true};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    CHECK(sk_xui_shm());
    sk_window *w = window(30, 20, true);
    CHECK(!sk_xui_shm());
    sk_image *im = sk_xui_window_frame(w);
    CHECK(im->owned);
    pattern(im, 8);
    sk_xui_window_present(w, NULL, 0);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 72, -1), 1);
    CHECK_INT(sk_xfake_count(f, SK_XFAKE_SHM_OPCODE, 3), 0);
    sk_image back;
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(im, &back));
    sk_image_free(&back);
    finish(f);
}

static void test_a_shown_window_maps_and_redraws_what_is_exposed(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(50, 40, false);
    pattern(sk_xui_window_frame(w), 3);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 8, -1), 1);
    CHECK(wait_request(f, 72, 1)); /* the Expose that the mapping brought */
    sk_image back;
    CHECK(sk_xui_window_readback(w, &back));
    CHECK(same(sk_xui_window_frame(w), &back));
    sk_image_free(&back);
    finish(f);
}

static void test_a_resize_brings_a_new_cleared_frame(void)
{
    sk_xfake_opts o = {.resources = "Xft.dpi: 192"};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    sk_window *w = window(40, 30, true);
    pattern(sk_xui_window_frame(w), 1);
    sk_xfake_configure(f, sk_xui_window_id(w), 100, 50);
    const sk_event *e = wait_for(SK_EV_RESIZE, 1, 2000);
    CHECK(e != NULL);
    if (e) {
        CHECK(e->win == w);
        CHECK_INT(e->size.w, 50);
        CHECK_INT(e->size.h, 25);
        CHECK(e->size.scale == 2.0);
    }
    sk_image *im = sk_xui_window_frame(w);
    CHECK_INT(im->w, 100);
    CHECK_INT(im->h, 50);
    bool clear = true;
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            clear = clear && sk_image_row(im, y)[x] == 0;
    CHECK(clear);
    int pw, ph;
    sk_xui_window_size(w, &pw, &ph);
    CHECK(pw == 50 && ph == 25);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 53, -1), 2);
    CHECK_INT(sk_xfake_count(f, 54, -1), 1);
    /* The same size again is no resize. */
    sk_xfake_configure(f, sk_xui_window_id(w), 100, 50);
    run_ms(50);
    CHECK_INT(count_of(SK_EV_RESIZE), 1);
    finish(f);
}

static void test_keys_and_autorepeat(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(20, 20, true);
    uint32_t id = sk_xui_window_id(w);
    key_event(f, SK_X11_KEY_PRESS, id, 38, 1000, 0);
    const sk_event *e = wait_for(SK_EV_KEY_DOWN, 1, 2000);
    CHECK(e && e->win == w && !strcmp(e->key.text, "a") && !e->key.repeat);
    key_event(f, SK_X11_KEY_PRESS, id, 38, 1010, 1 /* Shift */);
    e = wait_for(SK_EV_KEY_DOWN, 2, 2000);
    CHECK(e && !strcmp(e->key.text, "A") && e->key.mods == SK_MOD_SHIFT && e->key.base == 'a');
    /* The server's autorepeat: a release and a press at the same time. */
    key_event(f, SK_X11_KEY_RELEASE, id, 38, 1100, 0);
    key_event(f, SK_X11_KEY_PRESS, id, 38, 1100, 0);
    e = wait_for(SK_EV_KEY_DOWN, 3, 2000);
    CHECK(e && e->key.repeat);
    CHECK_INT(count_of(SK_EV_KEY_UP), 0);
    key_event(f, SK_X11_KEY_RELEASE, id, 38, 1200, 0);
    e = wait_for(SK_EV_KEY_UP, 1, 2000);
    CHECK(e && !strcmp(e->key.text, "a"));
    key_event(f, SK_X11_KEY_PRESS, id, 36, 1300, 4 /* Control */);
    e = wait_for(SK_EV_KEY_DOWN, 4, 2000);
    CHECK(e && e->key.key == SK_KEY_ENTER && e->key.mods == SK_MOD_CTRL && !e->key.repeat);
    finish(f);
}

static void test_mouse_clicks_and_the_wheel(void)
{
    sk_xfake_opts o = {.resources = "Xft.dpi: 192"};
    sk_xfake *f = start(&o);
    if (!f)
        return;
    sk_window *w = window(50, 50, true);
    uint32_t id = sk_xui_window_id(w);
    pointer_event(f, SK_X11_BUTTON_PRESS, id, 1, 10, 20, 100, 0);
    pointer_event(f, SK_X11_BUTTON_RELEASE, id, 1, 10, 20, 150, 0);
    pointer_event(f, SK_X11_BUTTON_PRESS, id, 1, 12, 21, 300, 1);
    pointer_event(f, SK_X11_BUTTON_PRESS, id, 1, 12, 21, 1000, 0);
    pointer_event(f, SK_X11_BUTTON_PRESS, id, 3, 12, 21, 1100, 0);
    const sk_event *e = wait_for(SK_EV_MOUSE_DOWN, 4, 2000);
    CHECK(e != NULL);
    e = nth_of(SK_EV_MOUSE_DOWN, 0);
    CHECK(e && e->mouse.x == 5 && e->mouse.y == 10 && e->mouse.button == 1 &&
          e->mouse.clicks == 1);
    e = nth_of(SK_EV_MOUSE_UP, 0);
    CHECK(e && e->mouse.button == 1 && e->mouse.clicks == 1);
    e = nth_of(SK_EV_MOUSE_DOWN, 1);
    CHECK(e && e->mouse.clicks == 2 && e->mouse.mods == SK_MOD_SHIFT);
    e = nth_of(SK_EV_MOUSE_DOWN, 2);
    CHECK(e && e->mouse.clicks == 1);
    e = nth_of(SK_EV_MOUSE_DOWN, 3);
    CHECK(e && e->mouse.button == 3 && e->mouse.clicks == 1);

    pointer_event(f, SK_X11_BUTTON_PRESS, id, 4, 0, 0, 1200, 0);
    pointer_event(f, SK_X11_BUTTON_RELEASE, id, 4, 0, 0, 1201, 0);
    pointer_event(f, SK_X11_BUTTON_PRESS, id, 5, 0, 0, 1202, 0);
    pointer_event(f, SK_X11_BUTTON_PRESS, id, 7, 0, 0, 1203, 0);
    wait_for(SK_EV_SCROLL, 3, 2000);
    CHECK_INT(count_of(SK_EV_SCROLL), 3);
    e = nth_of(SK_EV_SCROLL, 0);
    CHECK(e && e->scroll.dy == 1 && e->scroll.dx == 0 && !e->scroll.precise);
    e = nth_of(SK_EV_SCROLL, 1);
    CHECK(e && e->scroll.dy == -1);
    e = nth_of(SK_EV_SCROLL, 2);
    CHECK(e && e->scroll.dx == -1 && e->scroll.dy == 0);

    pointer_event(f, SK_X11_MOTION_NOTIFY, id, 0, 30, 40, 1300, 0);
    e = wait_for(SK_EV_MOUSE_MOVE, 1, 2000);
    CHECK(e && e->mouse.x == 15 && e->mouse.y == 20 && e->mouse.button == 0);
    pointer_event(f, SK_X11_LEAVE_NOTIFY, id, 0, 30, 40, 1400, 0);
    CHECK(wait_for(SK_EV_MOUSE_LEAVE, 1, 2000) != NULL);
    finish(f);
}

static void client_message(sk_xfake *f, uint32_t win, uint32_t type, uint32_t d0, uint32_t d1)
{
    uint8_t ev[32] = {SK_X11_CLIENT_MESSAGE, 32};
    sk_x11_set32(ev + 4, win);
    sk_x11_set32(ev + 8, type);
    sk_x11_set32(ev + 12, d0);
    sk_x11_set32(ev + 16, d1);
    sk_xfake_event(f, ev);
}

static void focus_event(sk_xfake *f, uint8_t type, uint32_t win, uint8_t mode)
{
    uint8_t ev[32] = {type, 3 /* NotifyNonlinear */};
    sk_x11_set32(ev + 4, win);
    ev[8] = mode;
    sk_xfake_event(f, ev);
}

static void test_close_ping_and_focus(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(20, 20, true);
    uint32_t id = sk_xui_window_id(w), protocols = sk_xfake_atom(f, "WM_PROTOCOLS");
    client_message(f, id, protocols, sk_xfake_atom(f, "WM_DELETE_WINDOW"), 0);
    const sk_event *e = wait_for(SK_EV_CLOSE, 1, 2000);
    CHECK(e && e->win == w);

    size_t sends = sk_xfake_count(f, 25, -1);
    client_message(f, id, protocols, sk_xfake_atom(f, "_NET_WM_PING"), 4242);
    CHECK(wait_request(f, 25, sends + 1));
    size_t len;
    uint8_t *r = sk_xfake_request(f, 25, -1, sends, &len);
    CHECK(r && sk_x11_get32(r + 4) == SK_XFAKE_ROOT);
    CHECK(r && sk_x11_get32(r + 12 + 4) == SK_XFAKE_ROOT && sk_x11_get32(r + 12 + 16) == 4242);
    free(r);

    focus_event(f, SK_X11_FOCUS_IN, id, 0);
    focus_event(f, SK_X11_FOCUS_IN, id, 0);
    focus_event(f, SK_X11_FOCUS_OUT, id, 1); /* a grab's */
    focus_event(f, SK_X11_FOCUS_OUT, id, 0);
    e = wait_for(SK_EV_FOCUS, 2, 2000);
    run_ms(20);
    CHECK_INT(count_of(SK_EV_FOCUS), 2);
    CHECK(nth_of(SK_EV_FOCUS, 0) && nth_of(SK_EV_FOCUS, 0)->focused);
    CHECK(e && !e->focused);

    size_t before = sk_xfake_count(f, 101, -1);
    uint8_t mapping[32] = {SK_X11_MAPPING_NOTIFY, 0, 0, 0, 1, 8, 248};
    sk_xfake_event(f, mapping);
    CHECK(wait_request(f, 101, before + 1));
    finish(f);
}

/* WM_HINTS's input field, or -1 when the property is not there. */
static int input_hint(sk_xfake *f, uint32_t id)
{
    size_t len;
    uint32_t type = 0;
    uint8_t *h = sk_xfake_property(f, id, SK_X11_WM_HINTS, &len, &type);
    int input = h && len == 36 && type == SK_X11_WM_HINTS && (sk_x11_get32(h) & 1) ? (int)sk_x11_get32(h + 4) : -1;
    free(h);
    return input;
}

/* The visualization's window (decision 0015): it takes input as any
 * window does, but maps with _NET_WM_USER_TIME 0, so a window manager
 * gives it no keys then, and a focus asked of it only raises it. */
/* A window placed where it is asked (a tab dragged out, decision 0023):
 * made there, with USPosition so the window manager keeps it, and its
 * origin read back from the server; one not placed asks nothing. */
static void test_a_placed_window(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window_opts at = {"at", 40, 30, 0, 0, NULL, true, false, true, 30, -12};
    char *err = NULL;
    sk_window *w = sk_xui_window_new(&at, &err), *plain = window(40, 30, true);
    CHECK(w && plain);
    if (!w || !plain) {
        free(err);
        finish(f);
        return;
    }
    CHECK(sk_xui_sync());
    size_t len;
    /* The second CreateWindow: the first is the layer's own helper. */
    uint8_t *r = sk_xfake_request(f, 1, -1, 1, &len);
    CHECK(r && sk_x11_get32(r + 4) == sk_xui_window_id(w) && (int16_t)sk_x11_get16(r + 12) == 30 && (int16_t)sk_x11_get16(r + 14) == -12);
    free(r);
    uint32_t type;
    uint8_t *h = sk_xfake_property(f, sk_xui_window_id(w), SK_X11_WM_NORMAL_HINTS, &len, &type);
    CHECK(h && len == 72 && (sk_x11_get32(h) & 5) == 5 && sk_x11_get32(h + 4) == 30);
    free(h);
    h = sk_xfake_property(f, sk_xui_window_id(plain), SK_X11_WM_NORMAL_HINTS, &len, &type);
    CHECK(h && len == 72 && (sk_x11_get32(h) & 5) == 0);
    free(h);
    int x = 0, y = 0;
    CHECK(sk_xui_window_origin(w, &x, &y));
    CHECK_INT(x, 30);
    CHECK_INT(y, -12);
    finish(f);
}

static void test_an_inactive_window(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window_opts plain = {"plain", 40, 30, 0, 0, NULL, false, false, false, 0, 0};
    sk_window_opts viz = {"viz", 40, 30, 0, 0, NULL, false, true, false, 0, 0};
    char *err = NULL;
    sk_window *a = sk_xui_window_new(&plain, &err), *b = sk_xui_window_new(&viz, &err);
    CHECK(a && b);
    if (!a || !b) {
        free(err);
        finish(f);
        return;
    }
    CHECK(sk_xui_sync());
    CHECK_INT(input_hint(f, sk_xui_window_id(a)), 1);
    CHECK_INT(input_hint(f, sk_xui_window_id(b)), 1);
    size_t len;
    uint32_t type;
    uint32_t user_time = sk_xfake_atom(f, "_NET_WM_USER_TIME");
    uint8_t *p = sk_xfake_property(f, sk_xui_window_id(b), user_time, &len, &type);
    CHECK(p && len == 4 && sk_x11_get32(p) == 0);
    free(p);
    p = sk_xfake_property(f, sk_xui_window_id(a), user_time, &len, &type);
    CHECK(p == NULL);
    free(p);
    CHECK(wait_request(f, 72, 2)); /* both mapped and exposed */
    run_ms(20);

    size_t raises = sk_xfake_count(f, 12, -1), focuses = sk_xfake_count(f, 42, -1),
           sends = sk_xfake_count(f, 25, -1);
    sk_xui_window_focus(b);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 12, -1), raises + 1);
    CHECK_INT(sk_xfake_count(f, 42, -1), focuses);
    CHECK_INT(sk_xfake_count(f, 25, -1), sends);
    /* The other window still takes the focus and asks the window
     * manager for it. */
    sk_xui_window_focus(a);
    CHECK(sk_xui_sync());
    CHECK_INT(sk_xfake_count(f, 42, -1), focuses + 1);
    CHECK_INT(sk_xfake_count(f, 25, -1), sends + 1);
    finish(f);
}

static void test_cursors(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(20, 20, true);
    sk_xui_window_set_cursor(w, SK_CURSOR_IBEAM);
    CHECK(wait_request(f, 2, 1));
    CHECK_INT(sk_xfake_count(f, 45, -1), 1);
    size_t len;
    uint8_t *r = sk_xfake_request(f, 45, -1, 0, &len);
    CHECK(r && len == 20 && !memcmp(r + 12, "cursor", 6));
    free(r);
    r = sk_xfake_request(f, 94, -1, 0, &len);
    CHECK(r && sk_x11_get16(r + 16) == 152 && sk_x11_get16(r + 18) == 153);
    free(r);
    r = sk_xfake_request(f, 2, -1, 0, &len);
    CHECK(r && sk_x11_get32(r + 4) == sk_xui_window_id(w) &&
          sk_x11_get32(r + 8) == SK_X11_CW_CURSOR);
    free(r);
    sk_xui_window_set_cursor(w, SK_CURSOR_HIDDEN);
    sk_xui_window_set_cursor(w, SK_CURSOR_IBEAM);
    CHECK(wait_request(f, 2, 3));
    CHECK_INT(sk_xfake_count(f, 93, -1), 1);
    CHECK_INT(sk_xfake_count(f, 94, -1), 1);
    CHECK_INT(sk_xfake_count(f, 45, -1), 1);
    finish(f);
}

static void got(void *ud, const char *utf8)
{
    (void)ud;
    if (rec.clip_calls < (int)SK_LEN(rec.clips))
        rec.clips[rec.clip_calls] = utf8 ? sk_xstrdup(utf8) : NULL;
    rec.clip_calls++;
}

static bool wait_clip(int calls, int ms)
{
    uint64_t end = sk_now_ms() + (uint64_t)ms;
    while (rec.clip_calls < calls && sk_now_ms() < end)
        sk_xui_once(5);
    return rec.clip_calls >= calls;
}

/* Runs the UI loop until the other client has its answer. */
static bool answer(sk_xfake *f, char **data, size_t *len, uint32_t *type)
{
    uint64_t end = sk_now_ms() + 5000;
    while (!sk_xfake_answer(f, data, len, type)) {
        if (sk_now_ms() > end)
            return false;
        sk_xui_once(5);
    }
    return true;
}

static void test_the_clipboard_answers_another_client(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    uint32_t clipboard = sk_xfake_atom(f, "CLIPBOARD"), utf8 = sk_xfake_atom(f, "UTF8_STRING");
    sk_xui_clipboard_set("h\xc3\xa9llo");
    CHECK(wait_request(f, 22, 1));
    char *data = NULL;
    size_t len = 0;
    uint32_t type = 0;
    sk_xfake_ask(f, clipboard, utf8);
    CHECK(answer(f, &data, &len, &type));
    CHECK(data && len == 6 && !memcmp(data, "h\xc3\xa9llo", 6));
    CHECK_INT(type, utf8);
    free(data);
    sk_xfake_ask(f, clipboard, SK_X11_STRING);
    CHECK(answer(f, &data, &len, &type));
    CHECK_STR(data, "h\xe9llo");
    CHECK_INT(type, SK_X11_STRING);
    free(data);
    sk_xfake_ask(f, clipboard, sk_xfake_atom(f, "TEXT"));
    CHECK(answer(f, &data, &len, &type));
    CHECK_STR(data, "h\xc3\xa9llo");
    free(data);
    sk_xfake_ask(f, clipboard, sk_xfake_atom(f, "TARGETS"));
    CHECK(answer(f, &data, &len, &type));
    CHECK(data && len == 16 && sk_x11_get32((uint8_t *)data + 4) == utf8);
    CHECK_INT(type, SK_X11_ATOM);
    free(data);
    sk_xfake_ask(f, clipboard, sk_xfake_atom(f, "image/png"));
    CHECK(answer(f, &data, &len, &type));
    CHECK(data == NULL);
    /* PRIMARY is its own. */
    sk_xfake_ask(f, SK_X11_PRIMARY, utf8);
    CHECK(answer(f, &data, &len, &type));
    CHECK(data == NULL);
    sk_xui_primary_set("selected");
    CHECK(wait_request(f, 22, 2));
    sk_xfake_ask(f, SK_X11_PRIMARY, utf8);
    CHECK(answer(f, &data, &len, &type));
    CHECK_STR(data, "selected");
    free(data);
    /* Another owner takes it, and is asked. */
    sk_xfake_own(f, SK_X11_PRIMARY, "theirs", 0);
    sk_xui_clipboard_get(true, got, NULL);
    CHECK(wait_clip(1, 3000));
    CHECK_STR(rec.clips[0], "theirs");
    finish(f);
}

static char *letters(size_t n)
{
    char *s = sk_xmalloc(n + 1);
    for (size_t i = 0; i < n; i++)
        s[i] = (char)('a' + (i * 7 + i / 26) % 26);
    s[n] = 0;
    return s;
}

static void test_the_clipboard_goes_by_incr_to_another_client(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    char *text = letters(300000);
    sk_xui_clipboard_set(text);
    CHECK(sk_xui_sync());
    sk_xfake_ask(f, sk_xfake_atom(f, "CLIPBOARD"), sk_xfake_atom(f, "UTF8_STRING"));
    char *data = NULL;
    size_t len = 0;
    uint32_t type = 0;
    CHECK(answer(f, &data, &len, &type));
    CHECK(data && len == 300000 && !memcmp(data, text, len));
    free(data);
    /* The requestor's window was watched for the transfer, then let go.
     * The release follows the requestor's last PropertyNotify, which xui
     * may not have handled when the answer is whole: rounds of its loop
     * until it has, three seconds at most. */
    bool watched = false, released = false;
    for (uint64_t until = sk_now_ms() + 3000; !released && sk_now_ms() < until;) {
        for (size_t i = 0; i < sk_xfake_count(f, 2, -1); i++) {
            uint8_t *r = sk_xfake_request(f, 2, -1, i, &len);
            if (r && sk_x11_get32(r + 4) == SK_XFAKE_REQUESTOR) {
                watched = watched || sk_x11_get32(r + 12) == SK_X11_PROPERTY_CHANGE_MASK;
                released = released || sk_x11_get32(r + 12) == 0;
            }
            free(r);
        }
        if (!released) {
            sk_xui_once(20);
            sk_xui_sync();
        }
    }
    CHECK(watched);
    CHECK(released);
    free(text);
    finish(f);
}

static void test_the_clipboard_from_another_client(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    uint32_t clipboard = sk_xfake_atom(f, "CLIPBOARD");
    sk_xfake_own(f, clipboard, "from elsewhere", 0);
    sk_xui_clipboard_get(false, got, NULL);
    CHECK(wait_clip(1, 3000));
    CHECK_STR(rec.clips[0], "from elsewhere");
    char *text = letters(100000);
    sk_xfake_own(f, clipboard, text, 4096);
    sk_xui_clipboard_get(false, got, NULL);
    CHECK(wait_clip(2, 5000));
    CHECK(rec.clips[1] && !strcmp(rec.clips[1], text));
    free(text);
    /* No owner: nothing, after UTF8_STRING and then STRING were asked. */
    size_t converts = sk_xfake_count(f, 24, -1);
    sk_xui_clipboard_get(true, got, NULL);
    CHECK(wait_clip(3, 3000));
    CHECK(rec.clips[2] == NULL);
    CHECK_INT(sk_xfake_count(f, 24, -1), converts + 2);
    finish(f);
}

static void test_the_clipboard_round_trip_through_the_server(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_xui_clipboard_set("mine \xe2\x9c\x93");
    sk_xui_clipboard_get(false, got, NULL);
    CHECK(wait_clip(1, 3000));
    CHECK_STR(rec.clips[0], "mine \xe2\x9c\x93");
    char *text = letters(300000);
    sk_xui_clipboard_set(text);
    sk_xui_primary_set("other");
    sk_xui_clipboard_get(false, got, NULL);
    sk_xui_clipboard_get(true, got, NULL);
    CHECK(wait_clip(2, 5000));
    CHECK(rec.clips[1] && !strcmp(rec.clips[1], text));
    CHECK(wait_clip(3, 3000));
    CHECK_STR(rec.clips[2], "other");
    free(text);
    finish(f);
}

static void test_the_clipboard_gives_up_on_a_silent_owner(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_xfake_own(f, sk_xfake_atom(f, "CLIPBOARD"), NULL, 0);
    uint64_t t0 = sk_now_ms();
    sk_xui_clipboard_get(false, got, NULL);
    CHECK(wait_clip(1, 5000));
    uint64_t took = sk_now_ms() - t0;
    CHECK(rec.clips[0] == NULL);
    CHECK(took >= 1900 && took < 4000);
    finish(f);
}

/* ---- Files dropped (XDND) ---- */

/* An XDND message from the drag's source, the other client's window, to
 * win: l[0] is the source, l[1] to l[4] as given. */
static void xdnd(sk_xfake *f, uint32_t win, const char *type, uint32_t l1, uint32_t l2, uint32_t l3,
                 uint32_t l4)
{
    uint8_t ev[32] = {SK_X11_CLIENT_MESSAGE | 0x80, 32};
    sk_x11_set32(ev + 4, win);
    sk_x11_set32(ev + 8, sk_xfake_atom(f, type));
    uint32_t l[5] = {SK_XFAKE_OWNER, l1, l2, l3, l4};
    for (int i = 0; i < 5; i++)
        sk_x11_set32(ev + 12 + 4 * i, l[i]);
    sk_xfake_event(f, ev);
}

/* Whether a SendEvent request sends the source a ClientMessage. */
static bool sent_to_source(const uint8_t *r, size_t len)
{
    return len >= 44 && sk_x11_get32(r + 4) == SK_XFAKE_OWNER && r[12] == SK_X11_CLIENT_MESSAGE &&
           r[13] == 32 && sk_x11_get32(r + 16) == SK_XFAKE_OWNER;
}

/* The nth message the client sent the source: its type, and its l[] in
 * l. Rounds of the UI loop until it came, two seconds at most. */
static bool to_source(sk_xfake *f, size_t nth, uint32_t *type, uint32_t l[5])
{
    for (uint64_t until = sk_now_ms() + 2000;; sk_xui_once(5)) {
        size_t seen = 0, len = 0;
        for (size_t i = 0; i < sk_xfake_count(f, 25, -1); i++) {
            uint8_t *r = sk_xfake_request(f, 25, -1, i, &len);
            bool found = r && sent_to_source(r, len) && seen++ == nth;
            if (found) {
                *type = sk_x11_get32(r + 20);
                for (int k = 0; k < 5; k++)
                    l[k] = sk_x11_get32(r + 24 + 4 * k);
            }
            free(r);
            if (found)
                return true;
        }
        if (sk_now_ms() > until)
            return false;
    }
}

/* A drag that offers text/uri-list among its three types is taken as a
 * copy; the drop asks the source for the list at the drop's time, its
 * local files reach the window, and the source hears it is done. */
static void test_files_dropped_on_a_window(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(40, 30, true);
    uint32_t id = sk_xui_window_id(w);
    uint32_t uris = sk_xfake_atom(f, "text/uri-list"), copy = sk_xfake_atom(f, "XdndActionCopy");
    uint32_t selection = sk_xfake_atom(f, "XdndSelection");
    sk_xfake_own_target(f, selection, uris,
                        "# from a file manager\r\n"
                        "file:///home/u/two%20words.txt\r\n"
                        "https://example.com/\r\n"
                        "file://localhost/srv/it's\r\n",
                        0);
    xdnd(f, id, "XdndEnter", 5u << 24, sk_xfake_atom(f, "text/plain"), uris, 0);
    xdnd(f, id, "XdndPosition", 0, 300u << 16 | 200, 1000, copy);
    uint32_t type = 0, l[5] = {0};
    CHECK(to_source(f, 0, &type, l));
    CHECK_INT(type, sk_xfake_atom(f, "XdndStatus"));
    CHECK_INT(l[0], id);
    CHECK_INT(l[1] & 1, 1);
    CHECK_INT(l[4], copy);

    xdnd(f, id, "XdndDrop", 0, 4321, 0, 0);
    const sk_event *e = wait_for(SK_EV_DROP, 1, 3000);
    CHECK(e && e->win == w && e->drop.n == 2);
    CHECK_STR(rec.dropped, "/home/u/two words.txt\n/srv/it's");
    size_t len = 0;
    uint8_t *r = sk_xfake_request(f, 24, -1, 0, &len);
    CHECK(r && sk_x11_get32(r + 8) == selection && sk_x11_get32(r + 12) == uris &&
          sk_x11_get32(r + 20) == 4321);
    free(r);
    CHECK(to_source(f, 1, &type, l));
    CHECK_INT(type, sk_xfake_atom(f, "XdndFinished"));
    CHECK_INT(l[0], id);
    CHECK_INT(l[1], 1);
    CHECK_INT(l[2], copy);
    finish(f);
}

/* A drag of anything but files is refused, and its drop is over at once;
 * so is a drag from a later version of XDND, and a list that names no
 * local file. */
static void test_other_drags_are_refused(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(40, 30, true);
    uint32_t id = sk_xui_window_id(w), uris = sk_xfake_atom(f, "text/uri-list");
    uint32_t status = sk_xfake_atom(f, "XdndStatus"), finished = sk_xfake_atom(f, "XdndFinished");
    uint32_t type = 0, l[5] = {0};
    xdnd(f, id, "XdndEnter", 5u << 24, sk_xfake_atom(f, "text/plain"),
         sk_xfake_atom(f, "UTF8_STRING"), 0);
    xdnd(f, id, "XdndPosition", 0, 10u << 16 | 10, 1000, sk_xfake_atom(f, "XdndActionCopy"));
    CHECK(to_source(f, 0, &type, l));
    CHECK_INT(type, status);
    CHECK_INT(l[0], id);
    CHECK_INT(l[1] & 1, 0);
    CHECK_INT(l[4], 0);
    xdnd(f, id, "XdndDrop", 0, 1001, 0, 0);
    CHECK(to_source(f, 1, &type, l));
    CHECK_INT(type, finished);
    CHECK_INT(l[0], id);
    CHECK_INT(l[1], 0);
    CHECK_INT(l[2], 0);

    xdnd(f, id, "XdndEnter", 6u << 24, uris, 0, 0);
    xdnd(f, id, "XdndPosition", 0, 10u << 16 | 10, 1002, 0);
    CHECK(to_source(f, 2, &type, l));
    CHECK_INT(type, status);
    CHECK_INT(l[1] & 1, 0);

    sk_xfake_own_target(f, sk_xfake_atom(f, "XdndSelection"), uris, "https://example.com/a\r\n", 0);
    xdnd(f, id, "XdndEnter", 5u << 24, uris, 0, 0);
    xdnd(f, id, "XdndDrop", 0, 1003, 0, 0);
    CHECK(to_source(f, 3, &type, l));
    CHECK_INT(type, finished);
    CHECK_INT(l[1], 0);
    CHECK_INT(sk_xfake_count(f, 24, -1), 1);
    run_ms(20);
    CHECK_INT(count_of(SK_EV_DROP), 0);
    finish(f);
}

/* A source of more than three types lists them in XdndTypeList; a drag
 * that leaves is forgotten; and a long list comes by INCR. */
static void test_a_drop_from_a_type_list_by_incr(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(40, 30, true);
    uint32_t id = sk_xui_window_id(w), uris = sk_xfake_atom(f, "text/uri-list");
    uint32_t type = 0, l[5] = {0};
    xdnd(f, id, "XdndEnter", 5u << 24, uris, 0, 0);
    xdnd(f, id, "XdndLeave", 0, 0, 0, 0);
    xdnd(f, id, "XdndPosition", 0, 10u << 16 | 10, 1000, 0);
    CHECK(to_source(f, 0, &type, l));
    CHECK_INT(l[1] & 1, 0);

    uint8_t list[16];
    sk_x11_set32(list, sk_xfake_atom(f, "text/plain"));
    sk_x11_set32(list + 4, sk_xfake_atom(f, "UTF8_STRING"));
    sk_x11_set32(list + 8, sk_xfake_atom(f, "x-special/gnome-copied-files"));
    sk_x11_set32(list + 12, uris);
    sk_xfake_set_property(f, SK_XFAKE_OWNER, sk_xfake_atom(f, "XdndTypeList"), SK_X11_ATOM, 32,
                          list, sizeof list);
    sk_buf text = {0}, want = {0};
    for (int i = 0; i < 40; i++) {
        sk_buf_printf(&text, "file:///srv/files/number%%20%d.txt\r\n", i);
        sk_buf_printf(&want, "%s/srv/files/number %d.txt", i ? "\n" : "", i);
    }
    sk_xfake_own_target(f, sk_xfake_atom(f, "XdndSelection"), uris, text.ptr, 64);
    xdnd(f, id, "XdndEnter", 5u << 24 | 1, 0, 0, 0);
    xdnd(f, id, "XdndPosition", 0, 10u << 16 | 10, 1001, 0);
    CHECK(to_source(f, 1, &type, l));
    CHECK_INT(l[1] & 1, 1);
    xdnd(f, id, "XdndDrop", 0, 1002, 0, 0);
    const sk_event *e = wait_for(SK_EV_DROP, 1, 5000);
    CHECK(e && e->drop.n == 40);
    CHECK_STR(rec.dropped, want.ptr);
    CHECK(to_source(f, 2, &type, l));
    CHECK_INT(type, sk_xfake_atom(f, "XdndFinished"));
    CHECK_INT(l[1], 1);
    sk_buf_free(&text);
    sk_buf_free(&want);
    finish(f);
}

static struct {
    int once, repeat, posted;
    char order[8];
    pthread_t ui;
    bool on_ui;
    int repeat_id;
} tk;

static void once_task(void *ud)
{
    (void)ud;
    tk.once++;
}

static void repeat_task(void *ud)
{
    (void)ud;
    if (++tk.repeat == 3)
        sk_xui_cancel(tk.repeat_id);
}

static void posted_task(void *ud)
{
    tk.order[tk.posted++] = *(const char *)ud;
    tk.on_ui = pthread_equal(pthread_self(), tk.ui) != 0;
}

static void *poster(void *arg)
{
    (void)arg;
    sk_test_sleep_ms(30);
    sk_xui_post(posted_task, "a");
    sk_xui_post(posted_task, "b");
    sk_xui_post(posted_task, "c");
    sk_xui_quit();
    return NULL;
}

static void test_timers_tasks_and_quit(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    memset(&tk, 0, sizeof tk);
    tk.ui = pthread_self();
    sk_xui_timer(10, 0, once_task, NULL);
    tk.repeat_id = sk_xui_timer(5, 5, repeat_task, NULL);
    int cancelled = sk_xui_timer(10, 0, once_task, NULL);
    sk_xui_cancel(cancelled);
    uint64_t end = sk_now_ms() + 2000;
    while ((tk.once < 1 || tk.repeat < 3) && sk_now_ms() < end)
        sk_xui_once(5);
    run_ms(20);
    CHECK_INT(tk.once, 1);
    CHECK_INT(tk.repeat, 3);
    pthread_t th;
    CHECK(pthread_create(&th, NULL, poster, NULL) == 0);
    CHECK(sk_xui_run());
    pthread_join(th, NULL);
    CHECK_INT(tk.posted, 3);
    CHECK(!memcmp(tk.order, "abc", 3));
    CHECK(tk.on_ui);
    finish(f);
}

static void test_frames_come_at_most_every_16_ms(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    sk_window *w = window(20, 20, true);
    sk_xui_window_request_frame(w);
    sk_xui_window_request_frame(w);
    CHECK(wait_for(SK_EV_FRAME, 1, 1000) != NULL);
    uint64_t first = sk_now_ms();
    run_ms(5);
    CHECK_INT(count_of(SK_EV_FRAME), 1);
    sk_xui_window_request_frame(w);
    CHECK(wait_for(SK_EV_FRAME, 2, 1000) != NULL);
    CHECK(sk_now_ms() - first >= 10);
    CHECK_INT(count_of(SK_EV_FRAME), 2);
    finish(f);
}

static void test_a_lost_connection_ends_the_run(void)
{
    sk_xfake *f = start(NULL);
    if (!f)
        return;
    window(20, 20, true);
    sk_xfake_stop(f);
    int quiet = open("/dev/null", O_WRONLY);
    sk_log_set_fd(quiet);
    uint64_t t0 = sk_now_ms();
    CHECK(!sk_xui_run());
    CHECK(sk_now_ms() - t0 < 2000);
    sk_log_set_fd(2);
    close(quiet);
    sk_xui_close();
    forget();
}

const sk_test sk_suite_x11_ui[] = {
    T(test_open_and_a_window_s_properties),
    T(test_present_and_readback_in_chunks),
    T(test_big_frames_go_in_big_requests),
    T(test_frames_through_mit_shm),
    T(test_a_refused_segment_falls_back_to_put_image),
    T(test_a_shown_window_maps_and_redraws_what_is_exposed),
    T(test_a_resize_brings_a_new_cleared_frame),
    T(test_keys_and_autorepeat),
    T(test_mouse_clicks_and_the_wheel),
    T(test_close_ping_and_focus),
    T(test_an_inactive_window),
    T(test_a_placed_window),
    T(test_cursors),
    T(test_the_clipboard_answers_another_client),
    T(test_the_clipboard_goes_by_incr_to_another_client),
    T(test_the_clipboard_from_another_client),
    T(test_the_clipboard_round_trip_through_the_server),
    T(test_the_clipboard_gives_up_on_a_silent_owner),
    T(test_files_dropped_on_a_window),
    T(test_other_drags_are_refused),
    T(test_a_drop_from_a_type_list_by_incr),
    T(test_timers_tasks_and_quit),
    T(test_frames_come_at_most_every_16_ms),
    T(test_a_lost_connection_ends_the_run),
    {0},
};
