/* platform/ui.h on macOS, on hidden windows: frames presented and read
 * back, sizes and scales, the key and mouse events AppKit would send
 * the window, input methods, frame pacing, tasks and timers from any
 * thread, and the clipboard, which is put back as the user had it.
 * Elsewhere the suite is empty. */
#include "../test.h"

#ifdef __APPLE__

#include "../../src/base/utf8.h"
#include "../../src/platform/macos/uimac.h"

#include <pthread.h>
#include <time.h>

#define MAX_EVENTS 256

static sk_event events[MAX_EVENTS];
static size_t n_events;
/* The text of each TEXT and PREEDIT, which lasts only the call. */
static char texts[MAX_EVENTS][64];
/* What a test does on an event, besides recording it. */
static void (*react)(const sk_event *ev);

static void record(void *ud, const sk_event *ev)
{
    (void)ud;
    if (n_events < MAX_EVENTS) {
        events[n_events] = *ev;
        if (ev->type == SK_EV_TEXT || ev->type == SK_EV_PREEDIT) {
            snprintf(texts[n_events], sizeof texts[n_events], "%s", ev->text);
            events[n_events].text = texts[n_events];
        }
        n_events++;
    }
    if (react)
        react(ev);
}

static size_t count(sk_event_type type)
{
    size_t n = 0;
    for (size_t i = 0; i < n_events; i++)
        n += events[i].type == type;
    return n;
}

/* The last event of type, or NULL. */
static const sk_event *last(sk_event_type type)
{
    for (size_t i = n_events; i-- > 0;)
        if (events[i].type == type)
            return &events[i];
    return NULL;
}

static void *pool;

/* Opens the UI for one test. A machine with no window server (a login
 * over ssh) skips the test rather than failing it. */
static bool open_ui(void)
{
    char *err = NULL;
    n_events = 0;
    react = NULL;
    if (!sk_ui_open("skinned-tests", record, NULL, &err)) {
        if (strstr(err, "no window server"))
            fprintf(stderr, "    skipped: %s\n", err);
        else
            sk_test_fail(__FILE__, __LINE__, "sk_ui_open: %s", err);
        free(err);
        return false;
    }
    pool = objc_autoreleasePoolPush();
    return true;
}

static void close_ui(void)
{
    sk_ui_close();
    objc_autoreleasePoolPop(pool);
    react = NULL;
    sk_macos_set_input_method(NULL);
}

static sk_window *hidden_window(int w, int h)
{
    char *err = NULL;
    sk_window_opts o = {.title = "test", .w = w, .h = h, .min_w = 50, .min_h = 40, .hidden = true};
    sk_window *win = sk_window_new(&o, &err);
    if (!win) {
        sk_test_fail(__FILE__, __LINE__, "sk_window_new: %s", err);
        free(err);
    }
    return win;
}

static id view_of(sk_window *w)
{
    return sk_msg(sk_macos_nswindow(w), "contentView");
}

static void quit_task(void *ud)
{
    (void)ud;
    sk_ui_quit();
}

/* Runs the UI loop for ms. */
static void run_for(uint64_t ms)
{
    sk_ui_timer(ms, 0, quit_task, NULL);
    CHECK(sk_ui_run());
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---- Frames ---- */

/* A pattern that differs from pixel to pixel, with some alpha. */
static void paint(sk_image *f, uint32_t seed)
{
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++)
            sk_image_row(f, y)[x] =
                sk_premul(SK_RGBA((x * 7 + seed) & 255, (y * 13 + seed) & 255, (x ^ y) & 255, (x + y) % 7 ? 255 : 128));
}

static void fill(sk_image *f, sk_rect r, sk_px px)
{
    r = sk_rect_intersect(r, sk_image_bounds(f));
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            sk_image_row(f, y)[x] = px;
}

static bool same(const sk_image *a, const sk_image *b)
{
    if (a->w != b->w || a->h != b->h)
        return false;
    for (int y = 0; y < a->h; y++)
        if (memcmp(sk_image_row(a, y), sk_image_row(b, y), (size_t)a->w * sizeof(sk_px)))
            return false;
    return true;
}

static bool shows_frame(sk_window *w)
{
    sk_image back;
    bool ok = sk_window_readback(w, &back) && same(&back, sk_window_frame(w));
    sk_image_free(&back);
    return ok;
}

static bool cleared(const sk_image *f)
{
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++)
            if (sk_image_row(f, y)[x])
                return false;
    return true;
}

static void test_a_presented_frame_reads_back_exactly(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(64, 48);
    int pw, ph;
    sk_window_size(w, &pw, &ph);
    CHECK_INT(pw, 64);
    CHECK_INT(ph, 48);
    double s = sk_window_scale(w);
    CHECK(s >= 1);
    sk_image *f = sk_window_frame(w);
    CHECK_INT(f->w, (int)(64 * s));
    CHECK_INT(f->h, (int)(48 * s));
    CHECK(cleared(f));

    sk_image none;
    CHECK(!sk_window_readback(w, &none));
    CHECK(none.px == NULL);

    paint(f, 1);
    sk_window_present(w, NULL, 0);
    CHECK(shows_frame(w));

    /* Damage in turns, some while the images shown before are still
     * held and some after the loop lets them go, so every front buffer
     * has to catch up with what it missed. */
    for (int i = 0; i < 12; i++) {
        sk_rect r = sk_rect_make((i * 11) % f->w, (i * 5) % f->h, 9 + i, 7);
        fill(f, r, sk_premul(SK_RGB(i * 20, 255 - i * 20, 99)));
        sk_window_present(w, &r, 1);
        CHECK(shows_frame(w));
        if (i % 3 == 2)
            run_for(5);
    }
    /* Two rectangles at once, one reaching past the frame. */
    sk_rect two[2] = {sk_rect_make(0, 0, 3, 3), sk_rect_make(f->w - 2, f->h - 2, 10, 10)};
    fill(f, two[0], 0xff0000ffu);
    fill(f, two[1], 0xff00ff00u);
    sk_window_present(w, two, 2);
    CHECK(shows_frame(w));
    CHECK(sk_msg_double(sk_msg(view_of(w), "layer"), "contentsScale") == s);

    sk_window_set_title(w, "a title");
    CHECK_STR(sk_utf8(sk_msg(sk_macos_nswindow(w), "title")), "a title");
    close_ui();
}

static void test_frames_at_scale_one_and_two(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(40, 30);
    for (int s = 1; s <= 2; s++) {
        double was = sk_window_scale(w);
        n_events = 0;
        sk_macos_force_scale(w, s);
        CHECK(sk_window_scale(w) == s);
        CHECK_INT(count(SK_EV_RESIZE), was != s);
        sk_image *f = sk_window_frame(w);
        CHECK_INT(f->w, 40 * s);
        CHECK_INT(f->h, 30 * s);
        paint(f, (uint32_t)s);
        sk_window_present(w, NULL, 0);
        CHECK(shows_frame(w));
        CHECK(sk_msg_double(sk_msg(view_of(w), "layer"), "contentsScale") == s);
    }
    /* From 2 back to 1: a resize that names the new scale, and a new
     * frame, cleared. */
    n_events = 0;
    sk_macos_force_scale(w, 1);
    const sk_event *e = last(SK_EV_RESIZE);
    CHECK(e != NULL);
    if (e) {
        CHECK(e->win == w);
        CHECK_INT(e->size.w, 40);
        CHECK_INT(e->size.h, 30);
        CHECK(e->size.scale == 1);
    }
    CHECK(cleared(sk_window_frame(w)));
    sk_macos_force_scale(w, 0);
    close_ui();
}

static void test_a_resize_gives_a_new_frame(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(100, 80);
    double s = sk_window_scale(w);
    paint(sk_window_frame(w), 3);
    sk_window_present(w, NULL, 0);
    n_events = 0;
    SK_MSG(void (*)(id, SEL, CGSize))(sk_macos_nswindow(w), sk_sel("setContentSize:"), CGSizeMake(150, 90));
    CHECK_INT(count(SK_EV_RESIZE), 1);
    const sk_event *e = last(SK_EV_RESIZE);
    if (e) {
        CHECK(e->win == w);
        CHECK_INT(e->size.w, 150);
        CHECK_INT(e->size.h, 90);
        CHECK(e->size.scale == s);
    }
    int pw, ph;
    sk_window_size(w, &pw, &ph);
    CHECK_INT(pw, 150);
    CHECK_INT(ph, 90);
    sk_image *f = sk_window_frame(w);
    CHECK_INT(f->w, (int)(150 * s));
    CHECK_INT(f->h, (int)(90 * s));
    CHECK(cleared(f));
    paint(f, 4);
    sk_window_present(w, NULL, 0);
    CHECK(shows_frame(w));

    /* The minimum size the user can drag the window to. */
    CGSize min = SK_MSG(CGSize (*)(id, SEL))(sk_macos_nswindow(w), sk_sel("contentMinSize"));
    CHECK(min.width == 50 && min.height == 40);
    close_ui();
}

/* ---- Keys ---- */

static id key_event(sk_window *w, unsigned long type, unsigned code, unsigned long flags, const char *chars,
                    const char *plain, bool repeat)
{
    long num = sk_msg_long(sk_macos_nswindow(w), "windowNumber");
    return SK_MSG(id (*)(id, SEL, unsigned long, CGPoint, unsigned long, double, long, id, id, id, BOOL,
                         unsigned short))(
        sk_cls("NSEvent"),
        sk_sel("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:"
               "charactersIgnoringModifiers:isARepeat:keyCode:"),
        type, CGPointZero, flags, 0.0, num, nil, sk_nsstring(chars), sk_nsstring(plain), repeat,
        (unsigned short)code);
}

/* The key event the window reports for a key AppKit sends it, which
 * types no text but the key's own. */
static sk_key_event press(sk_window *w, unsigned code, unsigned long flags, const char *chars, const char *plain)
{
    n_events = 0;
    id ev = key_event(w, NSEventTypeKeyDown, code, flags, chars, plain, false);
    sk_msg_void_id(sk_macos_nswindow(w), "sendEvent:", ev);
    sk_key_event k = {0};
    CHECK_INT(count(SK_EV_KEY_DOWN), 1);
    CHECK_INT(count(SK_EV_TEXT), 0);
    CHECK_INT(count(SK_EV_PREEDIT), 0);
    if (last(SK_EV_KEY_DOWN))
        k = last(SK_EV_KEY_DOWN)->key;
    return k;
}

static uint32_t cp(const char *s)
{
    uint32_t c = 0;
    if (*s)
        sk_utf8_decode(s, strlen(s), &c, NULL);
    return c;
}

static void test_keys_come_back_as_the_window_got_them(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    /* The base a layout gives is its own; these checks know QWERTY's,
     * where the key at kVK_ANSI_A types a. */
    sk_key_event a = press(w, 0x00, 0, "a", "a");
    bool qwerty = a.base == 'a';
    CHECK_INT(a.key, SK_KEY_NONE);
    CHECK_STR(a.text, "a");
    CHECK_INT(a.mods, 0);
    CHECK(!a.repeat);
    CHECK_INT(a.code, 0);
    CHECK(a.base != 0);

    sk_key_event shifted = press(w, 0x00, NSEventModifierFlagShift, "A", "A");
    CHECK_STR(shifted.text, "A");
    CHECK_INT(shifted.mods, SK_MOD_SHIFT);
    CHECK_INT(shifted.base, a.base);

    /* Option composes; the base stays the key's own. */
    sk_key_event sharp = press(w, 0x01, NSEventModifierFlagOption, "\xc3\x9f", "s");
    CHECK_STR(sharp.text, "\xc3\x9f");
    CHECK_INT(sharp.mods, SK_MOD_ALT);
    if (qwerty)
        CHECK_INT(sharp.base, 's');

    /* Cmd chords reach the app as keys, with their character. */
    sk_key_event cmd = press(w, 0x28, NSEventModifierFlagCommand, "k", "k");
    CHECK_INT(cmd.key, SK_KEY_NONE);
    CHECK_INT(cmd.mods, SK_MOD_SUPER);
    if (qwerty) {
        CHECK_STR(cmd.text, "k");
        CHECK_INT(cmd.base, 'k');
    }
    sk_key_event cmd_shift = press(w, 0x21, NSEventModifierFlagCommand | NSEventModifierFlagShift, "{", "{");
    CHECK_INT(cmd_shift.mods, SK_MOD_SUPER | SK_MOD_SHIFT);
    if (qwerty) {
        CHECK_STR(cmd_shift.text, "{");
        CHECK_INT(cmd_shift.base, '[');
    }

    /* Ctrl's control character is not text; the letter is. */
    sk_key_event ctrl = press(w, 0x08, NSEventModifierFlagControl, "\x03", "c");
    CHECK_INT(ctrl.mods, SK_MOD_CTRL);
    if (qwerty) {
        CHECK_STR(ctrl.text, "c");
        CHECK_INT(ctrl.base, 'c');
    }
    CHECK(cp(ctrl.text) >= 0x20);

    sk_key_event caps = press(w, 0x00, NSEventModifierFlagCapsLock, "A", "a");
    CHECK_INT(caps.mods, SK_MOD_CAPS);
    CHECK_STR(caps.text, "A");
    CHECK_INT(caps.base, a.base);

    /* Named keys: no text, no base. AppKit marks the arrows and the
     * function keys with Fn, the keypad with NumericPad. */
    enum { FN = NSEventModifierFlagFunction, KP = NSEventModifierFlagNumericPad };
    static const struct {
        unsigned code;
        unsigned long flags;
        const char *chars;
        sk_key key;
    } named[] = {
        {0x7E, FN, "\xef\x9c\x80", SK_KEY_UP},
        {0x7D, FN, "\xef\x9c\x81", SK_KEY_DOWN},
        {0x7B, FN, "\xef\x9c\x82", SK_KEY_LEFT},
        {0x7C, FN, "\xef\x9c\x83", SK_KEY_RIGHT},
        {0x24, 0, "\r", SK_KEY_ENTER},
        {0x30, 0, "\t", SK_KEY_TAB},
        {0x33, 0, "\x7f", SK_KEY_BACKSPACE},
        {0x35, 0, "\x1b", SK_KEY_ESCAPE},
        {0x72, FN, "\xef\x9d\x86", SK_KEY_INSERT},
        {0x75, FN, "\xef\x9c\xa8", SK_KEY_DELETE},
        {0x73, FN, "\xef\x9c\xa9", SK_KEY_HOME},
        {0x77, FN, "\xef\x9c\xab", SK_KEY_END},
        {0x74, FN, "\xef\x9c\xac", SK_KEY_PAGE_UP},
        {0x79, FN, "\xef\x9c\xad", SK_KEY_PAGE_DOWN},
        {0x7A, FN, "\xef\x9c\x84", SK_KEY_F1},
        {0x6F, FN, "\xef\x9c\x8f", SK_KEY_F1 + 11},
        {0x5A, FN, "\xef\x9c\x97", SK_KEY_F1 + 19},
        {0x6E, 0, "\x10", SK_KEY_MENU},
        {0x52, KP, "0", SK_KEY_KP_0},
        {0x57, KP, "5", SK_KEY_KP_0 + 5},
        {0x5C, KP, "9", SK_KEY_KP_9},
        {0x41, KP, ".", SK_KEY_KP_DECIMAL},
        {0x4B, KP, "/", SK_KEY_KP_DIVIDE},
        {0x43, KP, "*", SK_KEY_KP_MULTIPLY},
        {0x4E, KP, "-", SK_KEY_KP_SUBTRACT},
        {0x45, KP, "+", SK_KEY_KP_ADD},
        {0x4C, KP, "\x03", SK_KEY_KP_ENTER},
        {0x51, KP, "=", SK_KEY_KP_EQUAL},
    };
    for (size_t i = 0; i < SK_LEN(named); i++) {
        sk_key_event k = press(w, named[i].code, named[i].flags, named[i].chars, named[i].chars);
        if (k.key != named[i].key)
            sk_test_fail(__FILE__, __LINE__, "key code 0x%x gave %d, not %d", named[i].code, k.key, named[i].key);
        /* The keypad's keys type their character too, as the key
         * encoder reads it (ui.h). */
        bool keypad = k.key >= SK_KEY_KP_0 && k.key <= SK_KEY_KP_EQUAL && k.key != SK_KEY_KP_ENTER;
        CHECK_STR(k.text, keypad ? named[i].chars : "");
        CHECK_INT(k.base, keypad ? (unsigned char)named[i].chars[0] : 0);
        CHECK_INT(k.mods, 0);
    }
    unsigned long fn_shift_cmd = NSEventModifierFlagFunction | NSEventModifierFlagShift | NSEventModifierFlagCommand;
    sk_key_event up = press(w, 0x7E, fn_shift_cmd, "\xef\x9c\x80", "\xef\x9c\x80");
    CHECK_INT(up.key, SK_KEY_UP);
    CHECK_INT(up.mods, SK_MOD_SHIFT | SK_MOD_SUPER);

    /* A held key repeats; letting go is a key up. */
    n_events = 0;
    id nswin = sk_macos_nswindow(w);
    sk_msg_void_id(nswin, "sendEvent:", key_event(w, NSEventTypeKeyDown, 0x00, 0, "a", "a", true));
    sk_msg_void_id(nswin, "sendEvent:", key_event(w, NSEventTypeKeyUp, 0x00, 0, "a", "a", false));
    CHECK_INT(count(SK_EV_KEY_DOWN), 1);
    CHECK_INT(count(SK_EV_KEY_UP), 1);
    if (n_events == 2) {
        CHECK(events[0].key.repeat);
        CHECK_STR(events[1].key.text, "a");
        CHECK(!events[1].key.repeat);
    }
    /* Ctrl+Tab, which AppKit offers as a key equivalent first. */
    n_events = 0;
    id ctrl_tab = key_event(w, NSEventTypeKeyDown, 0x30, NSEventModifierFlagControl, "\t", "\t", false);
    CHECK(SK_MSG(BOOL (*)(id, SEL, id))(view_of(w), sk_sel("performKeyEquivalent:"), ctrl_tab));
    CHECK_INT(count(SK_EV_KEY_DOWN), 1);
    if (last(SK_EV_KEY_DOWN)) {
        CHECK_INT(last(SK_EV_KEY_DOWN)->key.key, SK_KEY_TAB);
        CHECK_INT(last(SK_EV_KEY_DOWN)->key.mods, SK_MOD_CTRL);
    }
    close_ui();
}

static void test_cmd_q_asks_every_window_to_close(void)
{
    if (!open_ui())
        return;
    sk_window *a = hidden_window(60, 50), *b = hidden_window(60, 50);
    /* Without the delegate's answer, terminate: would end the runner. */
    id delegate = sk_msg(NSApp, "delegate");
    CHECK(delegate && sk_responds(delegate, "applicationShouldTerminate:"));
    if (delegate && sk_responds(delegate, "applicationShouldTerminate:")) {
        n_events = 0;
        sk_msg_void_id(NSApp, "sendEvent:", key_event(a, NSEventTypeKeyDown, 0x0C, NSEventModifierFlagCommand, "q", "q",
                                                      false));
        CHECK_INT(count(SK_EV_KEY_DOWN), 0);
        CHECK_INT(count(SK_EV_CLOSE), 2);
        bool saw_a = false, saw_b = false;
        for (size_t i = 0; i < n_events; i++)
            if (events[i].type == SK_EV_CLOSE) {
                saw_a = saw_a || events[i].win == a;
                saw_b = saw_b || events[i].win == b;
            }
        CHECK(saw_a && saw_b);
    }
    close_ui();
}

/* ---- Input methods ---- */

static NSRange range(unsigned long location, unsigned long length)
{
    return (NSRange){location, length};
}

#define CHECK_RANGE(r, loc, len)                                                                                       \
    do {                                                                                                               \
        NSRange r_ = (r);                                                                                              \
        CHECK_INT(r_.location, loc);                                                                                   \
        CHECK_INT(r_.length, len);                                                                                     \
    } while (0)

/* The view's NSTextInputClient methods, as AppKit calls them. */
static void set_marked(id view, id text, unsigned long caret)
{
    SK_MSG(void (*)(id, SEL, id, NSRange, NSRange))(view, sk_sel("setMarkedText:selectedRange:replacementRange:"), text,
                                                    range(caret, 0), range(NSNotFound, 0));
}

static void insert(id view, const char *utf8)
{
    SK_MSG(void (*)(id, SEL, id, NSRange))(view, sk_sel("insertText:replacementRange:"), sk_nsstring(utf8),
                                           range(NSNotFound, 0));
}

static NSRange marked_range(id view)
{
    return SK_MSG(NSRange (*)(id, SEL))(view, sk_sel("markedRange"));
}

static NSRange selected_range(id view)
{
    return SK_MSG(NSRange (*)(id, SEL))(view, sk_sel("selectedRange"));
}

static id substring(id view, NSRange r, NSRange *actual)
{
    return SK_MSG(id (*)(id, SEL, NSRange, NSRange *))(view, sk_sel("attributedSubstringForProposedRange:actualRange:"),
                                                       r, actual);
}

static CGRect first_rect(id view, NSRange r, NSRange *actual)
{
    SEL sel = sk_sel("firstRectForCharacterRange:actualRange:");
    return SK_MSG_STRET(CGRect (*)(id, SEL, NSRange, NSRange *))(view, sel, r, actual);
}

/* The composition's text, "" when none is open. */
static const char *marked_text(id view)
{
    NSRange m = marked_range(view);
    id s = m.location == NSNotFound ? nil : substring(view, m, NULL);
    return s ? sk_utf8(sk_msg(s, "string")) : "";
}

/* An input method answering as macOS's would, which a hidden window's
 * synthetic keys never reach: the US layout's dead acute on Option+E,
 * which E completes; a kana composition that K opens, A turns into か,
 * Enter commits and Escape cancels; a full-width comma for ",". Any
 * other key goes to AppKit's key bindings, as it would with no input
 * method. */
static int im_keys;

static void toy_input_method(id view, id ev)
{
    im_keys++;
    unsigned code = SK_MSG(unsigned short (*)(id, SEL))(ev, sk_sel("keyCode"));
    bool option = sk_msg_ulong(ev, "modifierFlags") & NSEventModifierFlagOption;
    const char *m = marked_text(view);
    if (!*m && code == 0x0E && option)
        set_marked(view, sk_nsstring("\xc2\xb4"), 1);
    else if (!strcmp(m, "\xc2\xb4") && code == 0x0E && !option)
        insert(view, "\xc3\xa9");
    else if (!*m && code == 0x28)
        set_marked(view, sk_nsstring("k"), 1);
    else if (!strcmp(m, "k") && code == 0x00)
        set_marked(view, sk_nsstring("\xe3\x81\x8b"), 1);
    else if (*m && code == 0x24)
        insert(view, m);
    else if (*m && code == 0x35)
        set_marked(view, sk_nsstring(""), 0);
    else if (!*m && code == 0x2B)
        insert(view, "\xef\xbc\x8c");
    else
        sk_msg_void_id(view, "interpretKeyEvents:",
                       SK_MSG(id (*)(id, SEL, id))(sk_cls("NSArray"), sk_sel("arrayWithObject:"), ev));
}

/* A key pressed and let go, as AppKit sends them to the window. */
static void type_key(sk_window *w, unsigned code, unsigned long flags, const char *chars, const char *plain)
{
    id nswin = sk_macos_nswindow(w);
    sk_msg_void_id(nswin, "sendEvent:", key_event(w, NSEventTypeKeyDown, code, flags, chars, plain, false));
    sk_msg_void_id(nswin, "sendEvent:", key_event(w, NSEventTypeKeyUp, code, flags, chars, plain, false));
}

typedef struct {
    sk_event_type type;
    const char *text; /* a key down's key.text */
} want;

/* The events since n_events was 0 are these, but the key ups; a key
 * down named by its text. */
static void expect(int line, const want *ws, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; i < n_events; i++) {
        const sk_event *e = &events[i];
        if (e->type == SK_EV_KEY_UP)
            continue;
        const char *text = e->type == SK_EV_KEY_DOWN ? e->key.text : e->text;
        if (j >= n || e->type != ws[j].type || strcmp(text, ws[j].text)) {
            sk_test_fail(__FILE__, line, "event %zu is %d \"%s\", not %d \"%s\"", j, e->type, text,
                         j < n ? (int)ws[j].type : -1, j < n ? ws[j].text : "");
            return;
        }
        j++;
    }
    if (j != n)
        sk_test_fail(__FILE__, line, "%zu events, not %zu", j, n);
}

#define EXPECT(...) expect(__LINE__, (const want[]){__VA_ARGS__}, sizeof((const want[]){__VA_ARGS__}) / sizeof(want))
#define EXPECT_NOTHING() expect(__LINE__, NULL, 0)

static void test_the_view_takes_input_methods(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    id view = view_of(w);
    Protocol *client = objc_getProtocol("NSTextInputClient");
    CHECK(client && SK_MSG(BOOL (*)(id, SEL, Protocol *))(view, sk_sel("conformsToProtocol:"), client));
    CHECK(sk_msg(view, "inputContext") != nil);
    CHECK_INT(sk_msg_ulong(sk_msg(view, "validAttributesForMarkedText"), "count"), 0);
    unsigned long at = SK_MSG(unsigned long (*)(id, SEL, CGPoint))(view, sk_sel("characterIndexForPoint:"),
                                                                    CGPointMake(5, 5));
    CHECK_INT(at, NSNotFound);
    close_ui();
}

static void test_a_dead_key_composes_through_the_input_method(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    id view = view_of(w);
    sk_macos_set_input_method(toy_input_method);
    /* Option+E: the accent, marked; no key. */
    n_events = 0;
    type_key(w, 0x0E, NSEventModifierFlagOption, "", "e");
    EXPECT({SK_EV_PREEDIT, "\xc2\xb4"});
    CHECK_INT(count(SK_EV_KEY_UP), 1);
    CHECK(sk_msg_bool(view, "hasMarkedText"));
    CHECK_RANGE(marked_range(view), 0, 1);
    CHECK_RANGE(selected_range(view), 1, 0);
    /* E: the composition ends, then é is committed; no key. */
    n_events = 0;
    type_key(w, 0x0E, 0, "e", "e");
    EXPECT({SK_EV_PREEDIT, ""}, {SK_EV_TEXT, "\xc3\xa9"});
    CHECK_INT(count(SK_EV_KEY_UP), 1);
    CHECK(!sk_msg_bool(view, "hasMarkedText"));
    CHECK_RANGE(marked_range(view), NSNotFound, 0);

    /* Plain text the input method types as the key's own stays the
     * key's, once: so does an Option character outside a composition. */
    n_events = 0;
    type_key(w, 0x00, 0, "a", "a");
    type_key(w, 0x00, NSEventModifierFlagShift, "A", "A");
    type_key(w, 0x01, NSEventModifierFlagOption, "\xc3\x9f", "s");
    type_key(w, 0x31, 0, " ", " ");
    EXPECT({SK_EV_KEY_DOWN, "a"}, {SK_EV_KEY_DOWN, "A"}, {SK_EV_KEY_DOWN, "\xc3\x9f"}, {SK_EV_KEY_DOWN, " "});
    if (n_events == 8)
        CHECK_INT(events[4].key.mods, SK_MOD_ALT);

    /* With no input method composing it, as a hidden window's real one
     * does not, a dead key is a key with no text, as on Linux. */
    sk_macos_set_input_method(NULL);
    n_events = 0;
    type_key(w, 0x0E, NSEventModifierFlagOption, "", "e");
    EXPECT({SK_EV_KEY_DOWN, ""});
    close_ui();
}

static void test_compositions_take_the_keys_they_use(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    id view = view_of(w);
    sk_macos_set_input_method(toy_input_method);
    enum { FN = NSEventModifierFlagFunction, CTRL = NSEventModifierFlagControl, CMD = NSEventModifierFlagCommand };

    /* Outside a composition, named keys and Ctrl and Cmd chords never
     * reach the input method. */
    im_keys = 0;
    n_events = 0;
    type_key(w, 0x7B, FN, "\xef\x9c\x82", "\xef\x9c\x82");
    type_key(w, 0x24, 0, "\r", "\r");
    type_key(w, 0x08, CTRL, "\x03", "c");
    type_key(w, 0x28, CMD, "k", "k");
    CHECK_INT(im_keys, 0);
    EXPECT({SK_EV_KEY_DOWN, ""}, {SK_EV_KEY_DOWN, ""}, {SK_EV_KEY_DOWN, "c"}, {SK_EV_KEY_DOWN, "k"});
    if (n_events == 8) {
        CHECK_INT(events[0].key.key, SK_KEY_LEFT);
        CHECK_INT(events[2].key.key, SK_KEY_ENTER);
        CHECK_INT(events[4].key.mods, SK_MOD_CTRL);
        CHECK_INT(events[6].key.mods, SK_MOD_SUPER);
    }

    /* K, A: a composition, shown as it changes; Enter commits it and is
     * not a key. */
    n_events = 0;
    type_key(w, 0x28, 0, "k", "k");
    type_key(w, 0x00, 0, "a", "a");
    CHECK_STR(marked_text(view), "\xe3\x81\x8b");
    type_key(w, 0x24, 0, "\r", "\r");
    EXPECT({SK_EV_PREEDIT, "k"}, {SK_EV_PREEDIT, "\xe3\x81\x8b"}, {SK_EV_PREEDIT, ""}, {SK_EV_TEXT, "\xe3\x81\x8b"});
    CHECK_INT(count(SK_EV_KEY_UP), 3);
    CHECK_INT(im_keys, 3);

    /* While a composition is open, named keys and Ctrl chords go to the
     * input method, and those it passes on are keys as usual; Cmd
     * chords stay the app's. Escape cancels: nothing is committed. */
    type_key(w, 0x28, 0, "k", "k");
    im_keys = 0;
    n_events = 0;
    type_key(w, 0x7B, FN, "\xef\x9c\x82", "\xef\x9c\x82");
    type_key(w, 0x08, CTRL, "\x03", "c");
    type_key(w, 0x28, CMD, "k", "k");
    CHECK_INT(im_keys, 2);
    EXPECT({SK_EV_KEY_DOWN, ""}, {SK_EV_KEY_DOWN, "c"}, {SK_EV_KEY_DOWN, "k"});
    if (n_events == 6)
        CHECK_INT(events[0].key.key, SK_KEY_LEFT);
    CHECK_STR(marked_text(view), "k");
    n_events = 0;
    type_key(w, 0x35, 0, "\x1b", "\x1b");
    EXPECT({SK_EV_PREEDIT, ""});
    CHECK(!sk_msg_bool(view, "hasMarkedText"));

    /* Enter again, with no composition: a key. */
    n_events = 0;
    type_key(w, 0x24, 0, "\r", "\r");
    EXPECT({SK_EV_KEY_DOWN, ""});
    if (last(SK_EV_KEY_DOWN))
        CHECK_INT(last(SK_EV_KEY_DOWN)->key.key, SK_KEY_ENTER);

    /* Text in place of the key's own is text, and the key is not. */
    n_events = 0;
    type_key(w, 0x2B, 0, ",", ",");
    EXPECT({SK_EV_TEXT, "\xef\xbc\x8c"});

    /* Text with no key at all, as the emoji picker inserts it; a command
     * with no key in flight is nothing. */
    n_events = 0;
    insert(view, "\xf0\x9f\x98\x80");
    SK_MSG(void (*)(id, SEL, SEL))(view, sk_sel("doCommandBySelector:"), sk_sel("insertNewline:"));
    EXPECT({SK_EV_TEXT, "\xf0\x9f\x98\x80"});
    close_ui();
}

static void test_marked_text_queries_agree(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    id view = view_of(w);
    /* With nothing marked: an empty text, the insertion point at 0. */
    CHECK(!sk_msg_bool(view, "hasMarkedText"));
    CHECK_RANGE(marked_range(view), NSNotFound, 0);
    CHECK_RANGE(selected_range(view), 0, 0);
    CHECK(substring(view, range(0, 1), NULL) == nil);

    /* An attributed string, as some input methods pass, with a clause
     * of it selected. */
    n_events = 0;
    id text = SK_MSG(id (*)(id, SEL, id))(sk_msg(sk_cls("NSAttributedString"), "alloc"), sk_sel("initWithString:"),
                                         sk_nsstring("\xe3\x81\x8b\xe3\x81\xaa\xf0\x9f\x98\x80"));
    SK_MSG(void (*)(id, SEL, id, NSRange, NSRange))(view, sk_sel("setMarkedText:selectedRange:replacementRange:"), text,
                                                    range(1, 9), range(NSNotFound, 0));
    sk_release(text);
    EXPECT({SK_EV_PREEDIT, "\xe3\x81\x8b\xe3\x81\xaa\xf0\x9f\x98\x80"});
    CHECK(sk_msg_bool(view, "hasMarkedText"));
    /* UTF-16 units: the emoji is two. A selection past the end is cut
     * to it. */
    CHECK_RANGE(marked_range(view), 0, 4);
    CHECK_RANGE(selected_range(view), 1, 3);
    NSRange actual = range(99, 99);
    id s = substring(view, range(0, 4), &actual);
    CHECK_STR(s ? sk_utf8(sk_msg(s, "string")) : NULL, "\xe3\x81\x8b\xe3\x81\xaa\xf0\x9f\x98\x80");
    CHECK_RANGE(actual, 0, 4);
    s = substring(view, range(1, 50), &actual);
    CHECK_STR(s ? sk_utf8(sk_msg(s, "string")) : NULL, "\xe3\x81\xaa\xf0\x9f\x98\x80");
    CHECK_RANGE(actual, 1, 3);
    CHECK(substring(view, range(4, 1), NULL) == nil);
    CHECK(substring(view, range(NSNotFound, 0), NULL) == nil);

    /* Changed, then accepted as it stands: committed. */
    n_events = 0;
    set_marked(view, sk_nsstring("\xe3\x81\x8b"), 1);
    CHECK_RANGE(marked_range(view), 0, 1);
    CHECK_RANGE(selected_range(view), 1, 0);
    sk_msg_void(view, "unmarkText");
    EXPECT({SK_EV_PREEDIT, "\xe3\x81\x8b"}, {SK_EV_PREEDIT, ""}, {SK_EV_TEXT, "\xe3\x81\x8b"});
    CHECK(!sk_msg_bool(view, "hasMarkedText"));
    CHECK_RANGE(marked_range(view), NSNotFound, 0);
    CHECK_RANGE(selected_range(view), 0, 0);

    /* With nothing open, ending it again is nothing. */
    n_events = 0;
    sk_msg_void(view, "unmarkText");
    set_marked(view, sk_nsstring(""), 0);
    EXPECT_NOTHING();

    /* A window freed with a composition open lets it go. */
    set_marked(view, sk_nsstring("k"), 1);
    sk_window_free(w);
    close_ui();
}

static void check_first_rect(sk_window *w, int x, int y, int width, int height)
{
    id nswin = sk_macos_nswindow(w);
    NSRange actual = range(99, 99);
    CGRect r = first_rect(view_of(w), range(0, 1), &actual);
    CGRect frame = sk_msg_rect(nswin, "frame");
    CGRect content = SK_MSG_STRET(CGRect (*)(id, SEL, CGRect))(nswin, sk_sel("contentRectForFrameRect:"), frame);
    CHECK(r.origin.x == content.origin.x + x);
    CHECK(r.origin.y == content.origin.y + content.size.height - y - height);
    CHECK(r.size.width == width);
    CHECK(r.size.height == height);
    CHECK(CGRectContainsRect(frame, r));
    CHECK_RANGE(actual, 0, 1);
}

static void test_candidates_go_by_the_text_cursor(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(200, 100);
    check_first_rect(w, 0, 0, 0, 0);
    sk_window_set_ime_rect(w, 30, 40, 9, 18);
    check_first_rect(w, 30, 40, 9, 18);
    /* Moved and grown, the window keeps it from its top left. */
    id nswin = sk_macos_nswindow(w);
    SK_MSG(void (*)(id, SEL, CGPoint))(nswin, sk_sel("setFrameOrigin:"), CGPointMake(140, 90));
    SK_MSG(void (*)(id, SEL, CGSize))(nswin, sk_sel("setContentSize:"), CGSizeMake(300, 160));
    check_first_rect(w, 30, 40, 9, 18);
    sk_window_set_ime_rect(w, 291, 142, 9, 18);
    check_first_rect(w, 291, 142, 9, 18);
    close_ui();
}

/* ---- The mouse ---- */

static id mouse_event(sk_window *w, unsigned long type, double x, double y, unsigned long flags, long clicks)
{
    long num = sk_msg_long(sk_macos_nswindow(w), "windowNumber");
    return SK_MSG(id (*)(id, SEL, unsigned long, CGPoint, unsigned long, double, long, id, long, long, float))(
        sk_cls("NSEvent"),
        sk_sel("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:"
               "pressure:"),
        type, CGPointMake(x, y), flags, 0.0, num, nil, 0, clicks, 1.0f);
}

/* Gives the view a mouse event at x, y from its content's top left, by
 * the method NSWindow would call. A window never shown hit-tests
 * nothing, so its sendEvent: drops mouse events. */
static void send_mouse(sk_window *w, unsigned long type, double x, double y, unsigned long flags, long clicks)
{
    static const struct {
        unsigned long type;
        const char *method;
    } methods[] = {
        {NSEventTypeLeftMouseDown, "mouseDown:"},
        {NSEventTypeLeftMouseUp, "mouseUp:"},
        {NSEventTypeLeftMouseDragged, "mouseDragged:"},
        {NSEventTypeRightMouseDown, "rightMouseDown:"},
        {NSEventTypeRightMouseUp, "rightMouseUp:"},
        {NSEventTypeOtherMouseDown, "otherMouseDown:"},
        {NSEventTypeOtherMouseUp, "otherMouseUp:"},
        {NSEventTypeMouseMoved, "mouseMoved:"},
    };
    int pw, ph;
    sk_window_size(w, &pw, &ph);
    for (size_t i = 0; i < SK_LEN(methods); i++)
        if (methods[i].type == type)
            sk_msg_void_id(view_of(w), methods[i].method, mouse_event(w, type, x, ph - y, flags, clicks));
}

static void check_mouse(size_t i, sk_event_type type, double x, double y, int button, int clicks, uint32_t mods)
{
    if (i >= n_events) {
        sk_test_fail(__FILE__, __LINE__, "no event %zu", i);
        return;
    }
    const sk_event *e = &events[i];
    CHECK_INT(e->type, type);
    CHECK(e->mouse.x == x);
    CHECK(e->mouse.y == y);
    CHECK_INT(e->mouse.button, button);
    CHECK_INT(e->mouse.clicks, clicks);
    CHECK_INT(e->mouse.mods, mods);
}

static void test_mouse_events_from_the_top_left(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(200, 100);
    n_events = 0;
    send_mouse(w, NSEventTypeLeftMouseDown, 10, 20, 0, 1);
    send_mouse(w, NSEventTypeLeftMouseDragged, 30.5, 25, 0, 0);
    send_mouse(w, NSEventTypeLeftMouseUp, 30.5, 25, 0, 1);
    send_mouse(w, NSEventTypeLeftMouseDown, 40, 50, NSEventModifierFlagShift, 2);
    send_mouse(w, NSEventTypeLeftMouseUp, 40, 50, NSEventModifierFlagShift, 2);
    send_mouse(w, NSEventTypeRightMouseDown, 5, 95, 0, 1);
    send_mouse(w, NSEventTypeRightMouseUp, 5, 95, 0, 1);
    send_mouse(w, NSEventTypeOtherMouseDown, 150, 1, NSEventModifierFlagCommand, 1);
    send_mouse(w, NSEventTypeOtherMouseUp, 150, 1, NSEventModifierFlagCommand, 1);
    send_mouse(w, NSEventTypeMouseMoved, 60, 70, 0, 0);
    CHECK_INT(n_events, 10);
    check_mouse(0, SK_EV_MOUSE_DOWN, 10, 20, 1, 1, 0);
    check_mouse(1, SK_EV_MOUSE_MOVE, 30.5, 25, 0, 0, 0);
    check_mouse(2, SK_EV_MOUSE_UP, 30.5, 25, 1, 1, 0);
    check_mouse(3, SK_EV_MOUSE_DOWN, 40, 50, 1, 2, SK_MOD_SHIFT);
    check_mouse(4, SK_EV_MOUSE_UP, 40, 50, 1, 2, SK_MOD_SHIFT);
    check_mouse(5, SK_EV_MOUSE_DOWN, 5, 95, 3, 1, 0);
    check_mouse(6, SK_EV_MOUSE_UP, 5, 95, 3, 1, 0);
    check_mouse(7, SK_EV_MOUSE_DOWN, 150, 1, 2, 1, SK_MOD_SUPER);
    check_mouse(8, SK_EV_MOUSE_UP, 150, 1, 2, 1, SK_MOD_SUPER);
    check_mouse(9, SK_EV_MOUSE_MOVE, 60, 70, 0, 0, 0);

    /* A mouse's back button, AppKit's button 3, is X11's 8. */
    CGEventRef cg = CGEventCreateMouseEvent(NULL, kCGEventOtherMouseDown, CGPointMake(0, 0), (CGMouseButton)3);
    n_events = 0;
    sk_msg_void_id(view_of(w), "otherMouseDown:",
                   SK_MSG(id (*)(id, SEL, CGEventRef))(sk_cls("NSEvent"), sk_sel("eventWithCGEvent:"), cg));
    CFRelease(cg);
    CHECK_INT(count(SK_EV_MOUSE_DOWN), 1);
    if (last(SK_EV_MOUSE_DOWN))
        CHECK_INT(last(SK_EV_MOUSE_DOWN)->mouse.button, 8);

    /* The pointer leaving the view. */
    n_events = 0;
    id exit = SK_MSG(id (*)(id, SEL, unsigned long, CGPoint, unsigned long, double, long, id, long, long, void *))(
        sk_cls("NSEvent"),
        sk_sel("enterExitEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:"
               "trackingNumber:userData:"),
        NSEventTypeMouseExited, CGPointMake(-5, -5), 0, 0.0, sk_msg_long(sk_macos_nswindow(w), "windowNumber"), nil, 0,
        0, NULL);
    sk_msg_void_id(view_of(w), "mouseExited:", exit);
    CHECK_INT(count(SK_EV_MOUSE_LEAVE), 1);
    if (last(SK_EV_MOUSE_LEAVE))
        CHECK(last(SK_EV_MOUSE_LEAVE)->win == w);
    close_ui();
}

static void test_scrolling_in_lines_and_points(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(200, 100);
    /* A trackpad's scroll, in points, toward the top: dy > 0. */
    CGEventRef cg = CGEventCreateScrollWheelEvent2(NULL, kCGScrollEventUnitPixel, 2, 12, -3, 0);
    CGEventSetIntegerValueField(cg, kCGScrollWheelEventIsContinuous, 1);
    n_events = 0;
    sk_msg_void_id(view_of(w), "scrollWheel:", SK_MSG(id (*)(id, SEL, CGEventRef))(sk_cls("NSEvent"),
                                                                                    sk_sel("eventWithCGEvent:"), cg));
    CFRelease(cg);
    const sk_event *e = last(SK_EV_SCROLL);
    CHECK(e != NULL);
    if (e) {
        CHECK(e->scroll.precise);
        CHECK(e->scroll.dy > 0);
        CHECK(e->scroll.dx < 0);
        CHECK(e->win == w);
    }
    /* A wheel's, in lines, toward the bottom. (Shift would turn it
     * sideways, as macOS does for a wheel.) */
    cg = CGEventCreateScrollWheelEvent2(NULL, kCGScrollEventUnitLine, 1, -2, 0, 0);
    CGEventSetFlags(cg, kCGEventFlagMaskAlternate);
    n_events = 0;
    sk_msg_void_id(view_of(w), "scrollWheel:", SK_MSG(id (*)(id, SEL, CGEventRef))(sk_cls("NSEvent"),
                                                                                    sk_sel("eventWithCGEvent:"), cg));
    CFRelease(cg);
    e = last(SK_EV_SCROLL);
    CHECK(e != NULL);
    if (e) {
        CHECK(!e->scroll.precise);
        CHECK(e->scroll.dy < 0);
        CHECK(e->scroll.dx == 0);
        CHECK_INT(e->scroll.mods, SK_MOD_ALT);
    }
    close_ui();
}

/* ---- Close and focus ---- */

static void free_on_close(const sk_event *ev)
{
    if (ev->type == SK_EV_CLOSE)
        sk_window_free(ev->win);
}

static void test_close_asks_and_the_app_decides(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    id nswin = sk_macos_nswindow(w);
    n_events = 0;
    CHECK(!SK_MSG(BOOL (*)(id, SEL, id))(sk_msg(nswin, "delegate"), sk_sel("windowShouldClose:"), nswin));
    CHECK_INT(count(SK_EV_CLOSE), 1);
    if (last(SK_EV_CLOSE))
        CHECK(last(SK_EV_CLOSE)->win == w);

    /* The app frees the window in its handler, as AppKit returns. */
    react = free_on_close;
    sk_msg_void_id(nswin, "performClose:", nil);
    run_for(5);
    CHECK_INT(count(SK_EV_CLOSE), 2);
    close_ui();
}

static void test_focus_follows_the_key_window(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    id view = view_of(w);
    n_events = 0;
    sk_msg_void_id(view, "windowDidBecomeKey:", nil);
    sk_msg_void_id(view, "windowDidResignKey:", nil);
    CHECK_INT(n_events, 2);
    if (n_events == 2) {
        CHECK_INT(events[0].type, SK_EV_FOCUS);
        CHECK(events[0].focused);
        CHECK_INT(events[1].type, SK_EV_FOCUS);
        CHECK(!events[1].focused);
    }
    /* Hidden windows are not shown by focus nor do they take the cursor. */
    sk_window_focus(w);
    CHECK(!sk_msg_bool(sk_macos_nswindow(w), "isVisible"));
    sk_window_set_cursor(w, SK_CURSOR_IBEAM);
    sk_window_set_cursor(w, SK_CURSOR_ARROW);
    close_ui();
}

/* The visualization's window (decision 0015): it can never become the
 * key window, so the keyboard stays with the terminal, and its first
 * click is a click. */
static void test_a_window_that_takes_no_focus(void)
{
    if (!open_ui())
        return;
    char *err = NULL;
    sk_window_opts o = {.title = "viz", .w = 80, .h = 60, .hidden = true, .no_focus = true};
    sk_window *v = sk_window_new(&o, &err);
    sk_window *w = hidden_window(80, 60);
    CHECK(v != NULL);
    if (!v || !w) {
        free(err);
        close_ui();
        return;
    }
    id nv = sk_macos_nswindow(v), nw = sk_macos_nswindow(w);
    CHECK(!sk_msg_bool(nv, "canBecomeKeyWindow"));
    CHECK(!sk_msg_bool(nv, "canBecomeMainWindow"));
    CHECK(sk_msg_bool(nw, "canBecomeKeyWindow"));
    CHECK(SK_MSG(BOOL (*)(id, SEL, id))(view_of(v), sk_sel("acceptsFirstMouse:"), nil));
    CHECK(!SK_MSG(BOOL (*)(id, SEL, id))(view_of(w), sk_sel("acceptsFirstMouse:"), nil));
    n_events = 0;
    sk_msg_void(nv, "makeKeyWindow");
    sk_window_focus(v);
    run_for(5);
    CHECK(!sk_msg_bool(nv, "isKeyWindow"));
    CHECK(!sk_msg_bool(nv, "isVisible"));
    CHECK_INT(count(SK_EV_FOCUS), 0);
    /* Its pointer comes as any window's. */
    send_mouse(v, NSEventTypeLeftMouseDown, 10, 20, 0, 1);
    send_mouse(v, NSEventTypeLeftMouseUp, 10, 20, 0, 1);
    CHECK_INT(count(SK_EV_MOUSE_DOWN), 1);
    CHECK_INT(count(SK_EV_MOUSE_UP), 1);
    if (last(SK_EV_MOUSE_DOWN))
        CHECK(last(SK_EV_MOUSE_DOWN)->win == v);
    close_ui();
}

/* ---- Frame pacing ---- */

static double frame_times[64];
static size_t n_frames;
static bool keep_drawing;

static void on_frame(const sk_event *ev)
{
    if (ev->type != SK_EV_FRAME)
        return;
    if (n_frames < SK_LEN(frame_times))
        frame_times[n_frames++] = now_ms();
    if (keep_drawing)
        sk_window_request_frame(ev->win);
}

static void test_frames_come_once_per_request_at_the_refresh(void)
{
    if (!open_ui())
        return;
    sk_window *w = hidden_window(80, 60);
    react = on_frame;
    n_frames = 0;
    keep_drawing = false;
    sk_window_request_frame(w);
    sk_window_request_frame(w);
    sk_window_request_frame(w);
    run_for(80);
    CHECK_INT(count(SK_EV_FRAME), 1);
    if (last(SK_EV_FRAME))
        CHECK(last(SK_EV_FRAME)->win == w);

    /* Asked again at each frame: paced, never back to back. */
    n_frames = 0;
    keep_drawing = true;
    sk_window_request_frame(w);
    /* Three frames, however slow the machine: a loaded runner under
     * the sanitizers missed a fixed 150 ms. */
    for (int waited = 0; n_frames < 3 && waited < 2000; waited += 20)
        run_for(20);
    keep_drawing = false;
    CHECK(n_frames >= 3);
    for (size_t i = 1; i < n_frames; i++)
        if (frame_times[i] - frame_times[i - 1] < 4)
            sk_test_fail(__FILE__, __LINE__, "frames %zu and %zu %.2f ms apart", i - 1, i,
                         frame_times[i] - frame_times[i - 1]);

    /* A window freed with a frame asked for hears nothing more. */
    run_for(30);
    n_events = 0;
    sk_window_request_frame(w);
    sk_window_free(w);
    run_for(50);
    CHECK_INT(count(SK_EV_FRAME), 0);
    close_ui();
}

/* ---- Tasks, timers, quitting ---- */

static int order[100];
static size_t n_order;
static bool off_main;

static void note(void *ud)
{
    off_main = off_main || !pthread_main_np();
    if (n_order < SK_LEN(order))
        order[n_order++] = (int)(intptr_t)ud;
    if ((intptr_t)ud == 99)
        sk_ui_quit();
}

static void *poster(void *arg)
{
    (void)arg;
    for (intptr_t i = 0; i < 100; i++)
        sk_ui_post(note, (void *)i);
    return NULL;
}

static void test_posted_tasks_run_in_order_on_the_main_thread(void)
{
    if (!open_ui())
        return;
    n_order = 0;
    off_main = false;
    pthread_t t;
    CHECK(pthread_create(&t, NULL, poster, NULL) == 0);
    int guard = sk_ui_timer(5000, 0, quit_task, NULL);
    CHECK(sk_ui_run());
    sk_ui_cancel(guard);
    pthread_join(t, NULL);
    CHECK_INT(n_order, 100);
    for (size_t i = 0; i < n_order; i++)
        if (order[i] != (int)i) {
            sk_test_fail(__FILE__, __LINE__, "task %zu ran as %d", i, order[i]);
            break;
        }
    CHECK(!off_main);
    close_ui();
}

static int fired_once, fired_cancelled, fired_repeat, fired_self;
static int repeat_id, self_id;

static void once(void *ud)
{
    (void)ud;
    fired_once++;
}

static void cancelled(void *ud)
{
    (void)ud;
    fired_cancelled++;
}

static void repeating(void *ud)
{
    (void)ud;
    if (++fired_repeat == 3)
        sk_ui_cancel(repeat_id);
}

static void cancels_itself(void *ud)
{
    (void)ud;
    fired_self++;
    sk_ui_cancel(self_id);
}

static void test_timers_fire_repeat_and_cancel(void)
{
    if (!open_ui())
        return;
    fired_once = fired_cancelled = fired_repeat = fired_self = 0;
    sk_ui_timer(5, 0, once, NULL);
    int gone = sk_ui_timer(10, 0, cancelled, NULL);
    repeat_id = sk_ui_timer(5, 5, repeating, NULL);
    self_id = sk_ui_timer(1, 0, cancels_itself, NULL);
    sk_ui_cancel(gone);
    sk_ui_cancel(12345);
    double t0 = now_ms();
    run_for(80);
    CHECK(now_ms() - t0 >= 75);
    CHECK_INT(fired_once, 1);
    CHECK_INT(fired_cancelled, 0);
    CHECK_INT(fired_repeat, 3);
    CHECK_INT(fired_self, 1);
    /* Timers still due when the UI closes never fire. */
    sk_ui_timer(1, 0, once, NULL);
    close_ui();
    CHECK(open_ui());
    run_for(20);
    CHECK_INT(fired_once, 1);
    close_ui();
}

static void *quitter(void *arg)
{
    (void)arg;
    sk_test_sleep_ms(30);
    sk_ui_quit();
    return NULL;
}

static void test_quit_from_another_thread_ends_the_run(void)
{
    if (!open_ui())
        return;
    int guard = sk_ui_timer(5000, 0, quit_task, NULL);
    pthread_t t;
    double t0 = now_ms();
    CHECK(pthread_create(&t, NULL, quitter, NULL) == 0);
    CHECK(sk_ui_run());
    double took = now_ms() - t0;
    pthread_join(t, NULL);
    CHECK(took >= 25 && took < 1000);
    /* Asked before the run, the run returns at once. */
    sk_ui_quit();
    t0 = now_ms();
    CHECK(sk_ui_run());
    CHECK(now_ms() - t0 < 1000);
    sk_ui_cancel(guard);
    close_ui();
}

static char *open_error;

static void *open_elsewhere(void *arg)
{
    (void)arg;
    if (sk_ui_open("x", record, NULL, &open_error))
        open_error = NULL;
    return NULL;
}

static void test_open_only_on_the_main_thread(void)
{
    pthread_t t;
    open_error = NULL;
    CHECK(pthread_create(&t, NULL, open_elsewhere, NULL) == 0);
    pthread_join(t, NULL);
    CHECK(open_error != NULL);
    if (open_error)
        CHECK(strstr(open_error, "main thread") != NULL);
    free(open_error);
}

/* ---- The clipboard ---- */

/* Every item on the general pasteboard, in every type it holds. */
static id save_pasteboard(id pb)
{
    id saved = sk_new("NSMutableArray");
    id items = sk_msg(pb, "pasteboardItems");
    for (unsigned long i = 0; items && i < sk_msg_ulong(items, "count"); i++) {
        id item = SK_MSG(id (*)(id, SEL, unsigned long))(items, sk_sel("objectAtIndex:"), i);
        id copy = sk_autorelease(sk_new("NSPasteboardItem"));
        id types = sk_msg(item, "types");
        for (unsigned long j = 0; j < sk_msg_ulong(types, "count"); j++) {
            id type = SK_MSG(id (*)(id, SEL, unsigned long))(types, sk_sel("objectAtIndex:"), j);
            id data = sk_msg_id(item, "dataForType:", type);
            if (data)
                SK_MSG(BOOL (*)(id, SEL, id, id))(copy, sk_sel("setData:forType:"), data, type);
        }
        sk_msg_void_id(saved, "addObject:", copy);
    }
    return saved;
}

static void restore_pasteboard(id pb, id saved)
{
    sk_msg_long(pb, "clearContents");
    if (sk_msg_ulong(saved, "count"))
        SK_MSG(BOOL (*)(id, SEL, id))(pb, sk_sel("writeObjects:"), saved);
    sk_release(saved);
}

static char *got_text;
static bool got_answer, got_on_main;

static void got(void *ud, const char *utf8)
{
    (void)ud;
    got_answer = true;
    got_on_main = pthread_main_np();
    free(got_text);
    got_text = utf8 ? sk_xstrdup(utf8) : NULL;
    sk_ui_quit();
}

static void test_the_clipboard_holds_text(void)
{
    if (!open_ui())
        return;
    id pb = sk_msg(sk_cls("NSPasteboard"), "generalPasteboard");
    id saved = save_pasteboard(pb);

    const char *text = "skinned \xe2\x9c\x93 cl\xc3\xa9 \xf0\x9f\x8e\xa8\nline two";
    sk_clipboard_set(text);
    got_answer = false;
    sk_clipboard_get(false, got, NULL);
    CHECK(!got_answer);
    int guard = sk_ui_timer(2000, 0, quit_task, NULL);
    CHECK(sk_ui_run());
    sk_ui_cancel(guard);
    CHECK(got_answer);
    CHECK(got_on_main);
    CHECK_STR(got_text, text);

    /* PRIMARY is X11's: setting it changes nothing, and it holds nothing. */
    long changes = sk_msg_long(pb, "changeCount");
    sk_primary_set("not this");
    CHECK_INT(sk_msg_long(pb, "changeCount"), changes);
    got_answer = false;
    sk_clipboard_get(true, got, NULL);
    guard = sk_ui_timer(2000, 0, quit_task, NULL);
    CHECK(sk_ui_run());
    sk_ui_cancel(guard);
    CHECK(got_answer);
    CHECK(got_text == NULL);

    restore_pasteboard(pb, saved);
    free(got_text);
    got_text = NULL;
    close_ui();
}

/* New tab...'s folder picker as it is made, never shown here: it takes
 * one folder and no file, hidden folders when asked, its title, and the
 * folder it starts in. */
static void test_the_folder_panel_chooses_one_folder(void)
{
    if (!open_ui())
        return;
    sk_picker_opts o = {"Other profile", "/usr", true};
    id panel = sk_macos_folder_panel(&o);
    CHECK(panel != nil);
    if (panel) {
        CHECK(sk_msg_bool(panel, "canChooseDirectories"));
        CHECK(!sk_msg_bool(panel, "canChooseFiles"));
        CHECK(!sk_msg_bool(panel, "allowsMultipleSelection"));
        CHECK(!sk_msg_bool(panel, "canCreateDirectories"));
        CHECK(sk_msg_bool(panel, "showsHiddenFiles"));
        CHECK_STR(sk_utf8(sk_msg(panel, "message")), "Other profile");
        id url = sk_msg(panel, "directoryURL");
        CHECK_STR(url ? sk_utf8(sk_msg(url, "path")) : NULL, "/usr");
    }
    sk_picker_opts plain = {"Folder", NULL, false};
    panel = sk_macos_folder_panel(&plain);
    CHECK(panel != nil && !sk_msg_bool(panel, "showsHiddenFiles"));
    close_ui();
}

const sk_test sk_suite_ui_macos[] = {
    T(test_a_presented_frame_reads_back_exactly),
    T(test_frames_at_scale_one_and_two),
    T(test_a_resize_gives_a_new_frame),
    T(test_keys_come_back_as_the_window_got_them),
    T(test_cmd_q_asks_every_window_to_close),
    T(test_the_view_takes_input_methods),
    T(test_a_dead_key_composes_through_the_input_method),
    T(test_compositions_take_the_keys_they_use),
    T(test_marked_text_queries_agree),
    T(test_candidates_go_by_the_text_cursor),
    T(test_mouse_events_from_the_top_left),
    T(test_scrolling_in_lines_and_points),
    T(test_close_asks_and_the_app_decides),
    T(test_focus_follows_the_key_window),
    T(test_a_window_that_takes_no_focus),
    T(test_frames_come_once_per_request_at_the_refresh),
    T(test_posted_tasks_run_in_order_on_the_main_thread),
    T(test_timers_fire_repeat_and_cancel),
    T(test_quit_from_another_thread_ends_the_run),
    T(test_open_only_on_the_main_thread),
    T(test_the_clipboard_holds_text),
    T(test_the_folder_panel_chooses_one_folder),
    {0},
};

#else

const sk_test sk_suite_ui_macos[] = {{0}};

#endif
