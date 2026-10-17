/* platform/ui.h on a real X server (decision 0036): on Linux when
 * $DISPLAY names one, as xvfb-run does in CI, and skipped otherwise. A
 * second connection plays another client: it resizes the window, types
 * into it and asks for the clipboard. On macOS the suite holds no tests. */
#include "../test.h"

#ifdef __linux__
#include "../../src/core/loop.h"
#include "../../src/platform/ui.h"
#include "../../src/platform/x11/keys.h"
#include "../../src/platform/x11/x11.h"
#include "../../src/platform/x11/xui.h"

#include <math.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_EVENTS 128

static struct {
    sk_event ev[MAX_EVENTS];
    size_t n;
    char *clips[4];
    int clip_calls;
} rec;

static void forget(void)
{
    for (size_t i = 0; i < SK_LEN(rec.clips); i++)
        free(rec.clips[i]);
    memset(&rec, 0, sizeof rec);
}

static void on_event(void *ud, const sk_event *e)
{
    (void)ud;
    if (rec.n < MAX_EVENTS)
        rec.ev[rec.n++] = *e;
}

static bool open_display(void)
{
    static bool said;
    const char *d = getenv("DISPLAY");
    if (!d || !*d) {
        if (!said)
            printf("skip ui_x11: DISPLAY is not set\n");
        said = true;
        return false;
    }
    forget();
    char *err = NULL;
    if (!sk_ui_open("skinned-test", on_event, NULL, &err)) {
        sk_test_fail(__FILE__, __LINE__, "sk_ui_open: %s", err ? err : "?");
        free(err);
        return false;
    }
    return true;
}

static void close_display(void)
{
    sk_ui_close();
    forget();
}

static sk_window *window(int w, int h, bool hidden)
{
    sk_window_opts o = {"skinned test", w, h, 0, 0, NULL, hidden, false, false, 0, 0};
    char *err = NULL;
    sk_window *win = sk_window_new(&o, &err);
    if (!win) {
        sk_test_fail(__FILE__, __LINE__, "sk_window_new: %s", err ? err : "?");
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

static const sk_event *wait_for(sk_event_type type, size_t n, int ms)
{
    uint64_t end = sk_now_ms() + (uint64_t)ms;
    while (count_of(type) < n && sk_now_ms() < end)
        sk_xui_once(5);
    return nth_of(type, n - 1);
}

static void pattern(sk_image *im, uint32_t seed)
{
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            sk_image_row(im, y)[x] = 0xff000000u | ((uint32_t)(x * 3 + seed) & 255) << 16 |
                                     ((uint32_t)(y * 5) & 255) << 8 | ((uint32_t)(x + y) & 255);
}

static bool shows(sk_window *w)
{
    sk_image back;
    bool ok = sk_window_readback(w, &back);
    const sk_image *im = sk_window_frame(w);
    ok = ok && back.w == im->w && back.h == im->h;
    for (int y = 0; ok && y < im->h; y++)
        ok = !memcmp(sk_image_row(im, y), sk_image_row(&back, y), (size_t)im->w * 4);
    sk_image_free(&back);
    return ok;
}

/* Another client, on its own connection. */
static sk_x11 *other(void)
{
    char *err = NULL;
    sk_x11 *c = sk_x11_open(getenv("DISPLAY"), &err);
    if (!c) {
        sk_test_fail(__FILE__, __LINE__, "the second connection: %s", err ? err : "?");
        free(err);
    }
    return c;
}

static void test_a_hidden_window_presents_and_reads_back(void)
{
    if (!open_display())
        return;
    sk_window *w = window(64, 48, true);
    if (w) {
        sk_image *im = sk_window_frame(w);
        CHECK(im->w == (int)lround(64 * sk_window_scale(w)));
        pattern(im, 0);
        sk_window_present(w, NULL, 0);
        CHECK(shows(w));
        for (int y = 4; y < 20; y++)
            for (int x = 8; x < 30; x++)
                sk_image_row(im, y)[x] = 0xff336699;
        sk_rect dirty = sk_rect_make(8, 4, 22, 16);
        sk_window_present(w, &dirty, 1);
        CHECK(shows(w));
        sk_window_free(w);
    }
    close_display();
}

static void test_a_mapped_window_presents_and_reads_back(void)
{
    if (!open_display())
        return;
    sk_window *w = window(80, 60, false);
    if (w) {
        pattern(sk_window_frame(w), 9);
        sk_window_present(w, NULL, 0);
        CHECK(shows(w));
        sk_window_free(w);
    }
    close_display();
}

static void test_a_resize_from_outside(void)
{
    if (!open_display())
        return;
    sk_window *w = window(64, 48, true);
    sk_x11 *c = other();
    if (w && c) {
        uint32_t size[2] = {100, 70};
        sk_x11_configure_window(c, sk_xui_window_id(w), 4 | 8 /* width, height */, size);
        CHECK(sk_x11_sync(c));
        const sk_event *e = wait_for(SK_EV_RESIZE, 1, 3000);
        CHECK(e != NULL);
        sk_image *im = sk_window_frame(w);
        CHECK_INT(im->w, 100);
        CHECK_INT(im->h, 70);
        double s = sk_window_scale(w);
        if (e)
            CHECK(e->size.w == (int)lround(100 / s) && e->size.h == (int)lround(70 / s));
        pattern(im, 4);
        sk_window_present(w, NULL, 0);
        CHECK(shows(w));
    }
    sk_x11_free(c);
    sk_window_free(w);
    close_display();
}

static void send_key(sk_x11 *c, uint32_t win, uint8_t code, uint16_t state)
{
    uint8_t ev[32] = {SK_X11_KEY_PRESS, code};
    sk_x11_set32(ev + 8, c->root);
    sk_x11_set32(ev + 12, win);
    sk_x11_set16(ev + 28, state);
    ev[30] = 1;
    sk_x11_send_event(c, false, win, SK_X11_KEY_PRESS_MASK, ev);
    sk_x11_flush(c);
}

static void test_a_sent_key_types_its_character(void)
{
    if (!open_display())
        return;
    sk_window *w = window(32, 32, true);
    sk_x11 *c = other();
    sk_x11_keymap m = {0};
    if (w && c && sk_x11_keymap_load(c, &m)) {
        uint8_t a = sk_x11_keymap_find(&m, 'a');
        CHECK(a != 0);
        send_key(c, sk_xui_window_id(w), a, 0);
        const sk_event *e = wait_for(SK_EV_KEY_DOWN, 1, 3000);
        CHECK(e && e->win == w && !strcmp(e->key.text, "a") && e->key.base == 'a');
        send_key(c, sk_xui_window_id(w), a, 1 /* Shift */);
        e = wait_for(SK_EV_KEY_DOWN, 2, 3000);
        CHECK(e && !strcmp(e->key.text, "A") && e->key.mods == SK_MOD_SHIFT);
    }
    sk_x11_keymap_free(&m);
    sk_x11_free(c);
    sk_window_free(w);
    close_display();
}

static void got(void *ud, const char *utf8)
{
    (void)ud;
    if (rec.clip_calls < (int)SK_LEN(rec.clips))
        rec.clips[rec.clip_calls] = utf8 ? sk_xstrdup(utf8) : NULL;
    rec.clip_calls++;
}

static bool wait_clip(int calls)
{
    uint64_t end = sk_now_ms() + 5000;
    while (rec.clip_calls < calls && sk_now_ms() < end)
        sk_xui_once(5);
    return rec.clip_calls >= calls;
}

static void test_the_clipboard_set_and_got_back(void)
{
    if (!open_display())
        return;
    const char *text = "h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93";
    sk_clipboard_set(text);
    sk_clipboard_get(false, got, NULL);
    CHECK(wait_clip(1));
    CHECK_STR(rec.clips[0], text);
    /* Past one request: INCR both ways. */
    size_t n = 600000;
    char *big = sk_xmalloc(n + 1);
    for (size_t i = 0; i < n; i++)
        big[i] = (char)('a' + i % 23);
    big[n] = 0;
    sk_clipboard_set(big);
    sk_primary_set("the selection");
    sk_clipboard_get(false, got, NULL);
    sk_clipboard_get(true, got, NULL);
    CHECK(wait_clip(3));
    CHECK(rec.clips[1] && !strcmp(rec.clips[1], big));
    CHECK_STR(rec.clips[2], "the selection");
    free(big);
    close_display();
}

static bool is_selection_notify(const uint8_t ev[32], void *ud)
{
    (void)ud;
    return (ev[0] & 0x7f) == SK_X11_SELECTION_NOTIFY;
}

static void test_another_client_gets_the_clipboard(void)
{
    if (!open_display())
        return;
    sk_x11 *c = other();
    const char *text = "for the other client";
    sk_clipboard_set(text);
    CHECK(sk_xui_sync()); /* the server knows the owner before the other client asks */
    const char *names[] = {"CLIPBOARD", "UTF8_STRING", "SKINNED_TEST"};
    uint32_t atoms[3];
    if (c && sk_x11_intern(c, names, 3, atoms)) {
        uint32_t win = sk_x11_id(c), mask = SK_X11_PROPERTY_CHANGE_MASK;
        sk_x11_create_window(c, 0, win, c->root, 0, 0, 1, 1, 2, 0, SK_X11_CW_EVENT_MASK, &mask);
        sk_x11_convert_selection(c, win, atoms[0], atoms[1], atoms[2], 0);
        sk_x11_flush(c);
        uint8_t ev[32];
        bool notified = false;
        uint64_t end = sk_now_ms() + 3000;
        while (!notified && sk_now_ms() < end) {
            sk_xui_once(5);
            sk_x11_read(c, 0);
            notified = sk_x11_wait_event(c, is_selection_notify, NULL, ev, 0);
        }
        CHECK(notified);
        CHECK(notified && sk_x11_get32(ev + 20) == atoms[2]);
        sk_x11_reply r;
        sk_x11_prop p;
        CHECK(sk_x11_prop_get(c, true, win, atoms[2], 0, 1 << 16, &r, &p));
        CHECK(p.type == atoms[1] && p.len == strlen(text) && !memcmp(p.value, text, p.len));
        sk_x11_reply_free(&r);
    }
    sk_x11_free(c);
    close_display();
}

static struct {
    int once, repeat, posted;
    char order[4];
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
        sk_ui_cancel(tk.repeat_id);
}

static void posted_task(void *ud)
{
    tk.order[tk.posted++] = *(const char *)ud;
    tk.on_ui = pthread_equal(pthread_self(), tk.ui) != 0;
}

static void *poster(void *arg)
{
    (void)arg;
    sk_test_sleep_ms(300);
    sk_ui_post(posted_task, "x");
    sk_ui_post(posted_task, "y");
    sk_ui_quit();
    return NULL;
}

static void test_tasks_timers_and_frames(void)
{
    if (!open_display())
        return;
    memset(&tk, 0, sizeof tk);
    tk.ui = pthread_self();
    sk_window *w = window(16, 16, true);
    sk_ui_timer(10, 0, once_task, NULL);
    tk.repeat_id = sk_ui_timer(5, 5, repeat_task, NULL);
    if (w) {
        sk_window_request_frame(w);
        sk_window_request_frame(w);
    }
    pthread_t th;
    CHECK(pthread_create(&th, NULL, poster, NULL) == 0);
    CHECK(sk_ui_run());
    pthread_join(th, NULL);
    CHECK_INT(tk.once, 1);
    CHECK_INT(tk.repeat, 3);
    CHECK_INT(tk.posted, 2);
    CHECK(!memcmp(tk.order, "xy", 2));
    CHECK(tk.on_ui);
    CHECK_INT(count_of(SK_EV_FRAME), w ? 1 : 0);
    sk_window_free(w);
    close_display();
}

static void test_title_cursor_and_focus(void)
{
    if (!open_display())
        return;
    sk_window *w = window(40, 30, false);
    sk_x11 *c = other();
    const char *names[] = {"_NET_WM_NAME"};
    uint32_t name = 0;
    if (w && c && sk_x11_intern(c, names, 1, &name)) {
        sk_window_set_title(w, "a title \xe2\x9c\x93");
        sk_window_set_cursor(w, SK_CURSOR_IBEAM);
        sk_window_set_cursor(w, SK_CURSOR_HIDDEN);
        sk_window_set_cursor(w, SK_CURSOR_ARROW);
        sk_xui_once(50);
        sk_window_focus(w);
        CHECK(sk_xui_sync());
        sk_x11_reply r;
        sk_x11_prop p;
        CHECK(sk_x11_prop_get(c, false, sk_xui_window_id(w), name, 0, 4096, &r, &p));
        CHECK(p.len == 11 && !memcmp(p.value, "a title \xe2\x9c\x93", 11));
        sk_x11_reply_free(&r);
        pattern(sk_window_frame(w), 2);
        sk_window_present(w, NULL, 0);
        CHECK(shows(w));
    }
    sk_x11_free(c);
    sk_window_free(w);
    close_display();
}
#endif

const sk_test sk_suite_ui_x11[] = {
#ifdef __linux__
    T(test_a_hidden_window_presents_and_reads_back),
    T(test_a_mapped_window_presents_and_reads_back),
    T(test_a_resize_from_outside),
    T(test_a_sent_key_types_its_character),
    T(test_the_clipboard_set_and_got_back),
    T(test_another_client_gets_the_clipboard),
    T(test_tasks_timers_and_frames),
    T(test_title_cursor_and_focus),
#endif
    {0},
};
