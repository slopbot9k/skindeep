/* platform/ui.h on macOS (decision 0036): AppKit, QuartzCore and
 * CoreGraphics through the Objective-C runtime. The main thread runs
 * AppKit's events in sk_ui_run. Each window is an NSWindow whose
 * content view, of a class made here at run time, is also its delegate.
 *
 * A frame reaches the screen as a CGImage set as the view layer's
 * contents. The image is never made over the frame the app draws in:
 * each present copies what changed into one of a few front buffers that
 * no image on screen still uses, so the layer never shows a buffer being
 * drawn and the frame keeps what was drawn. A front buffer is freed by
 * whoever lets it go last, the window or CoreGraphics, which can release
 * an image on another thread.
 *
 * The view is an NSTextInputClient: keys the input method may want pass
 * through it, and come out as keys, text or a composition as ui.h has
 * them. */
#include "uimac.h"

#include "../../base/utf8.h"

#include <dispatch/dispatch.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define FRONTS 3

typedef struct {
    /* One for the window while the buffer is in its pool, one for each
     * CGImage over px. */
    atomic_int refs;
    sk_px *px;
    int w, h;
    sk_rect stale; /* where px is behind the frame */
} front;

struct sk_window {
    sk_window *next;
    unsigned serial;
    id win, view;
    id link; /* a CADisplayLink, or nil: pacer paces frames */
    CFRunLoopTimerRef pacer;
    void *ud;
    bool hidden;
    bool no_focus; /* an sk_nokey_window, which never becomes key */
    int w, h;      /* points */
    double scale, forced_scale;
    bool sized;
    /* In sk_window_new, whose caller reads the size when it returns. */
    bool creating;
    sk_image frame;
    front *fronts[FRONTS];
    int evict;
    CGImageRef shown;
    bool frame_asked;
    CFAbsoluteTime last_frame;
    sk_cursor cursor;
    bool inside;
    /* The input method's composition, an NSString, nil when none is
     * open, and the selection in it. */
    id marked;
    NSRange marked_sel;
    /* The text cursor, in points from the content's top left. */
    int ime_x, ime_y, ime_w, ime_h;
};

typedef struct {
    int id;
    CFRunLoopTimerRef t;
    sk_ui_task task;
    void *ud;
    bool repeat;
} ui_timer;

static struct {
    bool open;
    sk_ui_handler handler;
    void *ud;
    sk_window *windows;
    unsigned serial;
    SK_VEC(ui_timer *) timers;
    int timer_id;
    CGColorSpaceRef srgb;
    bool regular;
    bool cursor_hidden;
    /* Calls into the app's handler in progress: a window freed in one is
     * released after AppKit, which may still be using it, returns. */
    int depth;
} ui;

static atomic_bool quit_asked;
static Class view_class;
static ptrdiff_t win_ivar;
static id app_delegate;

static const CGBitmapInfo px_format = (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;

static void emit(sk_event *ev)
{
    if (!ui.handler)
        return;
    ui.depth++;
    ui.handler(ui.ud, ev);
    ui.depth--;
}

/* The view's sk_window, in the ivar make_classes gave its class. */
static sk_window **win_slot(id view)
{
    return (sk_window **)(void *)((char *)(void *)view + win_ivar);
}

static sk_window *win_of(id view)
{
    return *win_slot(view);
}

/* ---- Frames ---- */

static void front_unref(front *f)
{
    if (atomic_fetch_sub(&f->refs, 1) == 1) {
        free(f->px);
        free(f);
    }
}

static void front_released(void *info, const void *data, size_t size)
{
    (void)data, (void)size;
    front_unref(info);
}

static void drop_fronts(sk_window *w)
{
    for (int i = 0; i < FRONTS; i++) {
        if (w->fronts[i])
            front_unref(w->fronts[i]);
        w->fronts[i] = NULL;
    }
}

/* A front buffer no image uses, made when every one is in use; a full
 * pool gives up its buffers in turn, which are freed once let go. */
static front *take_front(sk_window *w)
{
    int slot = -1;
    for (int i = 0; i < FRONTS; i++) {
        if (w->fronts[i] && atomic_load(&w->fronts[i]->refs) == 1)
            return w->fronts[i];
        if (!w->fronts[i] && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        slot = w->evict;
        w->evict = (w->evict + 1) % FRONTS;
        front_unref(w->fronts[slot]);
    }
    front *f = sk_xcalloc(1, sizeof *f);
    atomic_init(&f->refs, 1);
    f->w = w->frame.w;
    f->h = w->frame.h;
    f->px = sk_xcalloc((size_t)f->w * (size_t)f->h, sizeof *f->px);
    f->stale = sk_image_bounds(&w->frame);
    w->fronts[slot] = f;
    return f;
}

static void copy_rect(front *f, const sk_image *src, sk_rect r)
{
    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(f->px + (size_t)y * (size_t)f->w + r.x, sk_image_row(src, y) + r.x, (size_t)r.w * sizeof(sk_px));
}

/* The layer's contents gravity that keeps an image at the top left while
 * the window grows ahead of its next frame. Gravity names the layer's
 * own axes, so it depends on whether its geometry ends up flipped. */
static id top_left(id layer)
{
    return sk_msg_bool(layer, "contentsAreFlipped") ? kCAGravityBottomLeft : kCAGravityTopLeft;
}

static void show(sk_window *w, CGImageRef img)
{
    id ca = sk_cls("CATransaction");
    id layer = sk_msg(w->view, "layer");
    sk_msg_void(ca, "begin");
    sk_msg_void_bool(ca, "setDisableActions:", YES);
    sk_msg_void_id(layer, "setContentsGravity:", top_left(layer));
    sk_msg_void_double(layer, "setContentsScale:", w->scale);
    sk_msg_void_id(layer, "setContents:", (id)(void *)img);
    sk_msg_void(ca, "commit");
    if (w->shown)
        CGImageRelease(w->shown);
    w->shown = img;
}

sk_image *sk_window_frame(sk_window *w)
{
    return &w->frame;
}

void sk_window_present(sk_window *w, const sk_rect *dirty, size_t n)
{
    sk_rect all = sk_image_bounds(&w->frame), d = n ? sk_rect_make(0, 0, 0, 0) : all;
    for (size_t i = 0; i < n; i++)
        d = sk_rect_union(d, sk_rect_intersect(dirty[i], all));
    if (sk_rect_empty(all) || (sk_rect_empty(d) && w->shown))
        return;
    for (int i = 0; i < FRONTS; i++)
        if (w->fronts[i])
            w->fronts[i]->stale = sk_rect_union(w->fronts[i]->stale, d);
    front *f = take_front(w);
    if (!sk_rect_empty(f->stale))
        copy_rect(f, &w->frame, f->stale);
    f->stale = sk_rect_make(0, 0, 0, 0);

    size_t row = (size_t)f->w * sizeof(sk_px);
    atomic_fetch_add(&f->refs, 1);
    CGDataProviderRef dp = CGDataProviderCreateWithData(f, f->px, row * (size_t)f->h, front_released);
    if (!dp) {
        front_unref(f);
        return;
    }
    /* Releasing the provider lets f go, once the image (if made) has. */
    CGImageRef img = CGImageCreate((size_t)f->w, (size_t)f->h, 8, 32, row, ui.srgb, px_format, dp, NULL, false,
                                   kCGRenderingIntentDefault);
    CGDataProviderRelease(dp);
    if (img) {
        void *pool = objc_autoreleasePoolPush();
        show(w, img);
        objc_autoreleasePoolPop(pool);
    }
}

/* img into out, as sk_window_readback has it. */
static bool read_image(CGImageRef img, sk_image *out)
{
    size_t iw = CGImageGetWidth(img), ih = CGImageGetHeight(img);
    if (!iw || !ih || iw > 1u << 15 || ih > 1u << 15)
        return false;
    sk_image_init(out, (int)iw, (int)ih);
    /* An image in the frame's own format is copied as it is; anything
     * else is drawn into one. */
    if (CGImageGetBitsPerComponent(img) == 8 && CGImageGetBitsPerPixel(img) == 32 &&
        CGImageGetBitmapInfo(img) == px_format && CGImageGetBytesPerRow(img) >= iw * 4) {
        CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(img));
        size_t row = CGImageGetBytesPerRow(img);
        if (!data || (size_t)CFDataGetLength(data) < row * (ih - 1) + iw * 4) {
            if (data)
                CFRelease(data);
            sk_image_free(out);
            return false;
        }
        const uint8_t *p = CFDataGetBytePtr(data);
        for (size_t y = 0; y < ih; y++)
            memcpy(sk_image_row(out, (int)y), p + y * row, iw * 4);
        CFRelease(data);
        return true;
    }
    CGContextRef cx = CGBitmapContextCreate(out->px, iw, ih, 8, iw * 4, ui.srgb, px_format);
    if (!cx) {
        sk_image_free(out);
        return false;
    }
    CGContextSetBlendMode(cx, kCGBlendModeCopy);
    CGContextDrawImage(cx, CGRectMake(0, 0, (CGFloat)iw, (CGFloat)ih), img);
    CGContextRelease(cx);
    return true;
}

bool sk_window_readback(sk_window *w, sk_image *out)
{
    sk_image_init(out, 0, 0);
    void *pool = objc_autoreleasePoolPush();
    CFTypeRef c = (CFTypeRef)(void *)sk_msg(sk_msg(w->view, "layer"), "contents");
    if (c)
        CFRetain(c);
    objc_autoreleasePoolPop(pool);
    if (!c)
        return false;
    bool ok = CFGetTypeID(c) == CGImageGetTypeID() && read_image((CGImageRef)c, out);
    CFRelease(c);
    return ok;
}

/* ---- Size and scale ---- */

static double backing_scale(sk_window *w)
{
    if (w->forced_scale > 0)
        return w->forced_scale;
    double s = sk_msg_double(w->win, "backingScaleFactor");
    return s > 0 ? s : 1;
}

/* Reads the view's size and the backing scale. When either changed, the
 * frame is a new one, cleared, and with tell the app hears of it. */
static void update_size(sk_window *w, bool tell)
{
    CGRect b = sk_msg_rect(w->view, "bounds");
    int pw = (int)lround(b.size.width), ph = (int)lround(b.size.height);
    double s = backing_scale(w);
    if (w->sized && pw == w->w && ph == w->h && s == w->scale)
        return;
    w->sized = true;
    w->w = pw;
    w->h = ph;
    w->scale = s;
    sk_image_free(&w->frame);
    sk_image_init(&w->frame, (int)lround(pw * s), (int)lround(ph * s));
    drop_fronts(w);
    if (tell && !w->creating) {
        sk_event ev = {.type = SK_EV_RESIZE, .win = w};
        ev.size.w = pw;
        ev.size.h = ph;
        ev.size.scale = s;
        emit(&ev);
    }
}

void sk_window_size(const sk_window *w, int *pw, int *ph)
{
    *pw = w->w;
    *ph = w->h;
}

double sk_window_scale(const sk_window *w)
{
    return w->scale;
}

void sk_macos_force_scale(sk_window *w, double scale)
{
    w->forced_scale = scale;
    update_size(w, true);
}

id sk_macos_nswindow(sk_window *w)
{
    return w->win;
}

/* ---- Frame pacing ---- */

static void frame_now(sk_window *w)
{
    if (!w->frame_asked)
        return;
    w->frame_asked = false;
    w->last_frame = CFAbsoluteTimeGetCurrent();
    if (w->link)
        sk_msg_void_bool(w->link, "setPaused:", YES);
    void *pool = objc_autoreleasePoolPush();
    sk_event ev = {.type = SK_EV_FRAME, .win = w};
    emit(&ev);
    objc_autoreleasePoolPop(pool);
}

static void pacer_fired(CFRunLoopTimerRef t, void *info)
{
    (void)t;
    frame_now(info);
}

static double refresh_interval(sk_window *w)
{
    id screen = sk_msg(w->win, "screen");
    if (!screen)
        screen = sk_msg(sk_cls("NSScreen"), "mainScreen");
    long fps = screen && sk_responds(screen, "maximumFramesPerSecond") ? sk_msg_long(screen, "maximumFramesPerSecond")
                                                                        : 60;
    return 1.0 / (double)(fps > 0 ? fps : 60);
}

void sk_window_request_frame(sk_window *w)
{
    if (w->frame_asked)
        return;
    w->frame_asked = true;
    if (w->link) {
        sk_msg_void_bool(w->link, "setPaused:", NO);
        return;
    }
    void *pool = objc_autoreleasePoolPush();
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent(), due = w->last_frame + refresh_interval(w);
    objc_autoreleasePoolPop(pool);
    CFRunLoopTimerSetNextFireDate(w->pacer, due > now ? due : now);
}

/* ---- Cursors ---- */

static id cursor_of(sk_cursor c)
{
    id cls = sk_cls("NSCursor");
    switch (c) {
    case SK_CURSOR_IBEAM:
        return sk_msg(cls, "IBeamCursor");
    case SK_CURSOR_POINTER:
        return sk_msg(cls, "pointingHandCursor");
    case SK_CURSOR_RESIZE_EW:
        return sk_msg(cls, sk_responds(cls, "columnResizeCursor") ? "columnResizeCursor" : "resizeLeftRightCursor");
    case SK_CURSOR_RESIZE_NS:
        return sk_msg(cls, sk_responds(cls, "rowResizeCursor") ? "rowResizeCursor" : "resizeUpDownCursor");
    default:
        return sk_msg(cls, "arrowCursor");
    }
}

static void hide_cursor(bool hide)
{
    if (hide == ui.cursor_hidden)
        return;
    ui.cursor_hidden = hide;
    sk_msg_void(sk_cls("NSCursor"), hide ? "hide" : "unhide");
}

/* The window's cursor, while the pointer is over a window on screen. */
static void apply_cursor(sk_window *w)
{
    if (!w->inside || w->hidden)
        return;
    hide_cursor(w->cursor == SK_CURSOR_HIDDEN);
    if (w->cursor != SK_CURSOR_HIDDEN)
        sk_msg_void(cursor_of(w->cursor), "set");
}

void sk_window_set_cursor(sk_window *w, sk_cursor c)
{
    w->cursor = c;
    void *pool = objc_autoreleasePoolPush();
    apply_cursor(w);
    objc_autoreleasePoolPop(pool);
}

/* ---- The view ---- */

static BOOL view_yes(id self, SEL cmd)
{
    (void)self, (void)cmd;
    return YES;
}

static BOOL view_no(id self, SEL cmd)
{
    (void)self, (void)cmd;
    return NO;
}

/* A window that never becomes key takes its first click as a click, as
 * nothing else would: every other keeps NSView's answer, no. */
static BOOL view_accepts_first_mouse(id self, SEL cmd, id ev)
{
    (void)cmd, (void)ev;
    sk_window *w = win_of(self);
    return w && w->no_focus ? YES : NO;
}

static void view_update_layer(id self, SEL cmd)
{
    (void)cmd;
    sk_window *w = win_of(self);
    if (w && w->shown)
        sk_msg_void_id(sk_msg(self, "layer"), "setContents:", (id)(void *)w->shown);
}

/* ---- Keys and the input method ---- */

/* The key keyDown: has given the input method, while it interprets it,
 * and whether the key's SK_EV_KEY_DOWN has gone out. */
typedef struct {
    id view;
    sk_key_event key;
    bool sent;
} key_in_flight;

static key_in_flight keying;
static void (*input_method)(id view, id ev);

void sk_macos_set_input_method(void (*im)(id view, id ev))
{
    input_method = im;
}

static void emit_key(sk_window *w, sk_event_type type, const sk_key_event *k)
{
    sk_event e = {.type = type, .win = w};
    e.key = *k;
    emit(&e);
}

static void key(id self, id ev, sk_event_type type)
{
    sk_window *w = win_of(self);
    if (!w)
        return;
    sk_key_event k;
    sk_macos_key_event(ev, &k);
    emit_key(w, type, &k);
}

/* SK_EV_TEXT or SK_EV_PREEDIT, while the view still has its window. */
static void emit_text(id view, sk_event_type type, const char *utf8)
{
    sk_window *w = win_of(view);
    if (!w)
        return;
    sk_event e = {.type = type, .win = w};
    e.text = utf8 ? utf8 : "";
    emit(&e);
}

/* The key in interpretation goes on to the app as its SK_EV_KEY_DOWN,
 * once: the input method passed it on, or typed the key's own text. */
static void pass_key(id view)
{
    sk_window *w = win_of(view);
    if (!w || keying.view != view || keying.sent)
        return;
    keying.sent = true;
    emit_key(w, SK_EV_KEY_DOWN, &keying.key);
}

/* A key goes to the app as it is unless the input method may want it:
 * Cmd chords never reach the input method, nor, outside a composition,
 * Ctrl chords and named keys, whose encoding is the terminal's. The
 * others go through interpretKeyEvents:, which answers with the
 * NSTextInputClient methods below; a key it answers with none of them
 * is the input method's. */
static void view_key_down(id self, SEL cmd, id ev)
{
    (void)cmd;
    sk_window *w = win_of(self);
    if (!w)
        return;
    sk_key_event k;
    sk_macos_key_event(ev, &k);
    if ((k.mods & SK_MOD_SUPER) || (!w->marked && ((k.mods & SK_MOD_CTRL) || k.key != SK_KEY_NONE))) {
        emit_key(w, SK_EV_KEY_DOWN, &k);
        return;
    }
    key_in_flight outer = keying;
    keying = (key_in_flight){.view = self, .key = k};
    if (input_method)
        input_method(self, ev);
    else
        sk_msg_void_id(self, "interpretKeyEvents:",
                       SK_MSG(id (*)(id, SEL, id))(sk_cls("NSArray"), sk_sel("arrayWithObject:"), ev));
    keying = outer;
}

static void view_key_up(id self, SEL cmd, id ev)
{
    (void)cmd;
    key(self, ev, SK_EV_KEY_UP);
}

/* AppKit keeps Ctrl+Tab for moving between controls: it is the app's. */
static BOOL view_key_equivalent(id self, SEL cmd, id ev)
{
    (void)cmd;
    if (sk_msg_ulong(ev, "type") != NSEventTypeKeyDown)
        return NO;
    unsigned long flags = sk_msg_ulong(ev, "modifierFlags");
    unsigned short code = SK_MSG(unsigned short (*)(id, SEL))(ev, sk_sel("keyCode"));
    if (code == 0x30 && (flags & NSEventModifierFlagControl) && !(flags & NSEventModifierFlagCommand)) {
        key(self, ev, SK_EV_KEY_DOWN);
        return YES;
    }
    return NO;
}

/* The string of an NSString or an NSAttributedString, which an input
 * method may pass either of. */
static id plain_string(id s)
{
    if (s && SK_MSG(BOOL (*)(id, SEL, Class))(s, sk_sel("isKindOfClass:"), (Class)sk_cls("NSAttributedString")))
        return sk_msg(s, "string");
    return s;
}

/* Whether s holds text, not only controls and function keys. */
static bool has_text(const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, NULL);
        if (sk_macos_is_text(cp))
            return true;
    }
    return false;
}

static unsigned long marked_length(sk_window *w)
{
    return w && w->marked ? sk_msg_ulong(w->marked, "length") : 0;
}

/* Closes the composition, if one is open, with SK_EV_PREEDIT ""; true
 * when one was. */
static bool end_composition(id view)
{
    sk_window *w = win_of(view);
    if (!w || !w->marked)
        return false;
    sk_release(w->marked);
    w->marked = nil;
    emit_text(view, SK_EV_PREEDIT, "");
    return true;
}

/* Text the input method inserts, in place of the composition when one
 * is open. Outside one, the key's own text, or nothing but controls
 * (AppKit's key bindings without a command for the key), lets the key
 * go on as it is, once; any other text is SK_EV_TEXT. Inside one,
 * controls alone cancel it. */
static void view_insert_text(id self, SEL cmd, id text, NSRange replace)
{
    (void)cmd, (void)replace;
    if (!win_of(self))
        return;
    const char *s = sk_utf8(plain_string(text));
    if (!s)
        s = "";
    bool composed = end_composition(self);
    if (!has_text(s)) {
        if (!composed)
            pass_key(self);
    } else if (!composed && keying.view == self && !strcmp(s, keying.key.text)) {
        pass_key(self);
    } else {
        emit_text(self, SK_EV_TEXT, s);
    }
}

/* NSResponder's insertText:, which the key bindings send a responder
 * without an input context. */
static void view_insert_text_only(id self, SEL cmd, id text)
{
    view_insert_text(self, cmd, text, (NSRange){NSNotFound, 0});
}

/* The composition, opened or changed; empty, it ends with nothing
 * committed. */
static void view_set_marked_text(id self, SEL cmd, id text, NSRange sel, NSRange replace)
{
    (void)cmd, (void)replace;
    sk_window *w = win_of(self);
    if (!w)
        return;
    id s = plain_string(text);
    unsigned long len = s ? sk_msg_ulong(s, "length") : 0;
    if (!len) {
        end_composition(self);
        return;
    }
    sk_release(w->marked);
    w->marked = sk_msg(s, "copy");
    unsigned long at = sel.location < len ? sel.location : len;
    w->marked_sel = (NSRange){at, sel.length < len - at ? sel.length : len - at};
    /* The caller's string, which outlives a handler that frees w. */
    emit_text(self, SK_EV_PREEDIT, sk_utf8(s));
}

/* The composition is accepted as it stands, as NSTextInputClient has
 * it: committed. */
static void view_unmark_text(id self, SEL cmd)
{
    (void)cmd;
    sk_window *w = win_of(self);
    if (!w || !w->marked)
        return;
    id s = sk_retain(w->marked);
    end_composition(self);
    emit_text(self, SK_EV_TEXT, sk_utf8(s));
    sk_release(s);
}

static BOOL view_has_marked_text(id self, SEL cmd)
{
    (void)cmd;
    return marked_length(win_of(self)) ? YES : NO;
}

/* The view's text, for the input method, is the composition alone,
 * from 0; without one, an empty text with the insertion point at 0. */
static NSRange view_marked_range(id self, SEL cmd)
{
    (void)cmd;
    unsigned long len = marked_length(win_of(self));
    return len ? (NSRange){0, len} : (NSRange){NSNotFound, 0};
}

static NSRange view_selected_range(id self, SEL cmd)
{
    (void)cmd;
    sk_window *w = win_of(self);
    return marked_length(w) ? w->marked_sel : (NSRange){0, 0};
}

static id view_valid_attributes(id self, SEL cmd)
{
    (void)self, (void)cmd;
    return sk_msg(sk_cls("NSArray"), "array");
}

static id view_attributed_substring(id self, SEL cmd, NSRange range, NSRange *actual)
{
    (void)cmd;
    sk_window *w = win_of(self);
    unsigned long len = marked_length(w);
    if (range.location >= len)
        return nil;
    NSRange r = {range.location, range.length < len - range.location ? range.length : len - range.location};
    if (actual)
        *actual = r;
    id sub = SK_MSG(id (*)(id, SEL, NSRange))(w->marked, sk_sel("substringWithRange:"), r);
    id a = SK_MSG(id (*)(id, SEL, id))(sk_msg(sk_cls("NSAttributedString"), "alloc"), sk_sel("initWithString:"), sub);
    return sk_autorelease(a);
}

static unsigned long view_character_index(id self, SEL cmd, CGPoint p)
{
    (void)self, (void)cmd, (void)p;
    return NSNotFound;
}

/* Every character of the composition is at the text cursor, where the
 * app draws it, in screen coordinates. */
static CGRect view_first_rect(id self, SEL cmd, NSRange range, NSRange *actual)
{
    (void)cmd;
    if (actual)
        *actual = range;
    sk_window *w = win_of(self);
    if (!w)
        return CGRectZero;
    CGRect b = sk_msg_rect(self, "bounds");
    CGRect r = CGRectMake(w->ime_x, b.size.height - w->ime_y - w->ime_h, w->ime_w, w->ime_h);
    r = SK_MSG_STRET(CGRect (*)(id, SEL, CGRect, id))(self, sk_sel("convertRect:toView:"), r, nil);
    return SK_MSG_STRET(CGRect (*)(id, SEL, CGRect))(w->win, sk_sel("convertRectToScreen:"), r);
}

/* A command the key binds to, such as moveLeft: or insertNewline:: the
 * input method passed the key on. */
static void view_do_command(id self, SEL cmd, SEL command)
{
    (void)cmd, (void)command;
    pass_key(self);
}

void sk_window_set_ime_rect(sk_window *w, int x, int y, int width, int height)
{
    width = SK_MAX(width, 0);
    height = SK_MAX(height, 0);
    if (x == w->ime_x && y == w->ime_y && width == w->ime_w && height == w->ime_h)
        return;
    w->ime_x = x;
    w->ime_y = y;
    w->ime_w = width;
    w->ime_h = height;
    void *pool = objc_autoreleasePoolPush();
    id context = sk_msg(w->view, "inputContext");
    if (context)
        sk_msg_void(context, "invalidateCharacterCoordinates");
    objc_autoreleasePoolPop(pool);
}

/* ---- The mouse and the window's delegate ---- */

/* The event's position in points from the view's top left. */
static void position(sk_window *w, id ev, double *x, double *y)
{
    CGPoint p = sk_msg_point(ev, "locationInWindow");
    p = SK_MSG(CGPoint (*)(id, SEL, CGPoint, id))(w->view, sk_sel("convertPoint:fromView:"), p, nil);
    CGRect b = sk_msg_rect(w->view, "bounds");
    *x = p.x;
    *y = b.size.height - p.y;
}

/* The event's type names the left and right buttons. The others are
 * AppKit's 2 (middle), then 3 and on, which become X11's 8 (back), 9
 * (forward) and on. */
static int button_of(id ev)
{
    switch (sk_msg_ulong(ev, "type")) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
        return 1;
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
        return 3;
    default: {
        long n = sk_msg_long(ev, "buttonNumber");
        return n >= 3 ? (int)n + 5 : 2;
    }
    }
}

static void mouse(id self, id ev, sk_event_type type)
{
    sk_window *w = win_of(self);
    if (!w)
        return;
    sk_event e = {.type = type, .win = w};
    position(w, ev, &e.mouse.x, &e.mouse.y);
    e.mouse.mods = sk_macos_mods(sk_msg_ulong(ev, "modifierFlags"));
    if (type != SK_EV_MOUSE_MOVE) {
        e.mouse.button = button_of(ev);
        e.mouse.clicks = (int)sk_msg_long(ev, "clickCount");
    } else if (!w->inside) {
        w->inside = true;
        apply_cursor(w);
    }
    emit(&e);
}

static void view_mouse_down(id self, SEL cmd, id ev)
{
    (void)cmd;
    mouse(self, ev, SK_EV_MOUSE_DOWN);
}

static void view_mouse_up(id self, SEL cmd, id ev)
{
    (void)cmd;
    mouse(self, ev, SK_EV_MOUSE_UP);
}

static void view_mouse_moved(id self, SEL cmd, id ev)
{
    (void)cmd;
    mouse(self, ev, SK_EV_MOUSE_MOVE);
}

static void view_mouse_entered(id self, SEL cmd, id ev)
{
    (void)cmd, (void)ev;
    sk_window *w = win_of(self);
    if (w) {
        w->inside = true;
        apply_cursor(w);
    }
}

static void view_mouse_exited(id self, SEL cmd, id ev)
{
    (void)cmd, (void)ev;
    sk_window *w = win_of(self);
    if (!w)
        return;
    w->inside = false;
    if (!w->hidden)
        hide_cursor(false);
    sk_event e = {.type = SK_EV_MOUSE_LEAVE, .win = w};
    emit(&e);
}

static void view_cursor_update(id self, SEL cmd, id ev)
{
    (void)cmd, (void)ev;
    sk_window *w = win_of(self);
    if (w) {
        w->inside = true;
        apply_cursor(w);
    }
}

static void view_scroll(id self, SEL cmd, id ev)
{
    (void)cmd;
    sk_window *w = win_of(self);
    if (!w)
        return;
    sk_event e = {.type = SK_EV_SCROLL, .win = w};
    position(w, ev, &e.scroll.x, &e.scroll.y);
    /* A wheel's deltas are lines; a trackpad's, points. Both are
     * positive when the content should move down, showing what is
     * above. */
    e.scroll.precise = sk_msg_bool(ev, "hasPreciseScrollingDeltas");
    e.scroll.dx = sk_msg_double(ev, "scrollingDeltaX");
    e.scroll.dy = sk_msg_double(ev, "scrollingDeltaY");
    e.scroll.mods = sk_macos_mods(sk_msg_ulong(ev, "modifierFlags"));
    if (e.scroll.dx != 0 || e.scroll.dy != 0)
        emit(&e);
}

static void view_frame(id self, SEL cmd, id link)
{
    (void)cmd, (void)link;
    sk_window *w = win_of(self);
    if (w)
        frame_now(w);
}

static void view_backing_changed(id self, SEL cmd)
{
    (void)cmd;
    sk_window *w = win_of(self);
    if (w)
        update_size(w, true);
}

static BOOL win_should_close(id self, SEL cmd, id sender)
{
    (void)cmd, (void)sender;
    sk_window *w = win_of(self);
    if (w) {
        sk_event e = {.type = SK_EV_CLOSE, .win = w};
        emit(&e);
    }
    return NO;
}

static void win_resized(id self, SEL cmd, id note)
{
    (void)cmd, (void)note;
    sk_window *w = win_of(self);
    if (w)
        update_size(w, true);
}

static void focus(id self, bool focused)
{
    sk_window *w = win_of(self);
    if (!w)
        return;
    if (!focused && !w->hidden)
        hide_cursor(false);
    sk_event e = {.type = SK_EV_FOCUS, .win = w};
    e.focused = focused;
    emit(&e);
}

static void win_became_key(id self, SEL cmd, id note)
{
    (void)cmd, (void)note;
    focus(self, true);
}

static void win_resigned_key(id self, SEL cmd, id note)
{
    (void)cmd, (void)note;
    focus(self, false);
}

/* ---- The application ---- */

/* Every window is asked to close, Cmd+Q's and a logout's request alike,
 * and the app decides; with none, sk_ui_run returns. */
static unsigned long app_should_terminate(id self, SEL cmd, id sender)
{
    (void)self, (void)cmd, (void)sender;
    unsigned serials[64];
    size_t n = 0;
    for (sk_window *w = ui.windows; w && n < SK_LEN(serials); w = w->next)
        serials[n++] = w->serial;
    if (!n)
        sk_ui_quit();
    for (size_t i = 0; i < n; i++)
        for (sk_window *w = ui.windows; w; w = w->next)
            if (w->serial == serials[i]) {
                sk_event e = {.type = SK_EV_CLOSE, .win = w};
                emit(&e);
                break;
            }
    return NSTerminateCancel;
}

/* AppKit never sends a window the key-up of a Cmd chord; this sends it. */
static void app_send_event(id self, SEL cmd, id ev)
{
    if (sk_msg_ulong(ev, "type") == NSEventTypeKeyUp &&
        (sk_msg_ulong(ev, "modifierFlags") & NSEventModifierFlagCommand)) {
        id key_window = sk_msg(self, "keyWindow");
        if (key_window) {
            sk_msg_void_id(key_window, "sendEvent:", ev);
            return;
        }
    }
    struct objc_super super = {self, (Class)sk_cls("NSApplication")};
    SK_MSG_SUPER(void (*)(struct objc_super *, SEL, id))(&super, cmd, ev);
}

static void add(Class c, const char *sel, IMP imp, const char *types)
{
    if (!class_addMethod(c, sk_sel(sel), imp, types))
        sk_die("cannot add %s to %s", sel, class_getName(c));
}

static void make_classes(void)
{
    if (view_class)
        return;
    Class v = objc_allocateClassPair((Class)sk_cls("NSView"), "sk_view", 0);
    if (!v)
        sk_die("cannot make the class sk_view");
    sk_add_ptr_ivar(v, "sk_win");
    add(v, "acceptsFirstResponder", (IMP)view_yes, SK_ENC_BOOL "@:");
    add(v, "wantsUpdateLayer", (IMP)view_yes, SK_ENC_BOOL "@:");
    add(v, "updateLayer", (IMP)view_update_layer, "v@:");
    add(v, "keyDown:", (IMP)view_key_down, "v@:@");
    add(v, "keyUp:", (IMP)view_key_up, "v@:@");
    add(v, "performKeyEquivalent:", (IMP)view_key_equivalent, SK_ENC_BOOL "@:@");
    /* NSView gives a view an input context when it conforms. */
    Protocol *client = objc_getProtocol("NSTextInputClient");
    if (!client || !class_addProtocol(v, client))
        sk_die("cannot make sk_view an NSTextInputClient");
    add(v, "insertText:replacementRange:", (IMP)view_insert_text, "v@:@" SK_ENC_RANGE);
    add(v, "insertText:", (IMP)view_insert_text_only, "v@:@");
    add(v, "setMarkedText:selectedRange:replacementRange:", (IMP)view_set_marked_text,
        "v@:@" SK_ENC_RANGE SK_ENC_RANGE);
    add(v, "unmarkText", (IMP)view_unmark_text, "v@:");
    add(v, "hasMarkedText", (IMP)view_has_marked_text, SK_ENC_BOOL "@:");
    add(v, "markedRange", (IMP)view_marked_range, SK_ENC_RANGE "@:");
    add(v, "selectedRange", (IMP)view_selected_range, SK_ENC_RANGE "@:");
    add(v, "validAttributesForMarkedText", (IMP)view_valid_attributes, "@@:");
    add(v, "attributedSubstringForProposedRange:actualRange:", (IMP)view_attributed_substring,
        "@@:" SK_ENC_RANGE "^" SK_ENC_RANGE);
    add(v, "characterIndexForPoint:", (IMP)view_character_index, "Q@:" SK_ENC_POINT);
    add(v, "firstRectForCharacterRange:actualRange:", (IMP)view_first_rect,
        SK_ENC_RECT "@:" SK_ENC_RANGE "^" SK_ENC_RANGE);
    add(v, "doCommandBySelector:", (IMP)view_do_command, "v@::");
    static const char *const downs[] = {"mouseDown:", "rightMouseDown:", "otherMouseDown:"};
    static const char *const ups[] = {"mouseUp:", "rightMouseUp:", "otherMouseUp:"};
    static const char *const moves[] = {"mouseMoved:", "mouseDragged:", "rightMouseDragged:", "otherMouseDragged:"};
    for (size_t i = 0; i < SK_LEN(downs); i++) {
        add(v, downs[i], (IMP)view_mouse_down, "v@:@");
        add(v, ups[i], (IMP)view_mouse_up, "v@:@");
    }
    for (size_t i = 0; i < SK_LEN(moves); i++)
        add(v, moves[i], (IMP)view_mouse_moved, "v@:@");
    add(v, "acceptsFirstMouse:", (IMP)view_accepts_first_mouse, SK_ENC_BOOL "@:@");
    add(v, "mouseEntered:", (IMP)view_mouse_entered, "v@:@");
    add(v, "mouseExited:", (IMP)view_mouse_exited, "v@:@");
    add(v, "cursorUpdate:", (IMP)view_cursor_update, "v@:@");
    add(v, "scrollWheel:", (IMP)view_scroll, "v@:@");
    add(v, "skFrame:", (IMP)view_frame, "v@:@");
    add(v, "viewDidChangeBackingProperties", (IMP)view_backing_changed, "v@:");
    add(v, "windowShouldClose:", (IMP)win_should_close, SK_ENC_BOOL "@:@");
    add(v, "windowDidResize:", (IMP)win_resized, "v@:@");
    add(v, "windowDidBecomeKey:", (IMP)win_became_key, "v@:@");
    add(v, "windowDidResignKey:", (IMP)win_resigned_key, "v@:@");
    objc_registerClassPair(v);
    win_ivar = ivar_getOffset(class_getInstanceVariable(v, "sk_win"));

    /* The window of sk_window_opts.no_focus: never key, never main. */
    Class k = objc_allocateClassPair((Class)sk_cls("NSWindow"), "sk_nokey_window", 0);
    if (!k)
        sk_die("cannot make the class sk_nokey_window");
    add(k, "canBecomeKeyWindow", (IMP)view_no, SK_ENC_BOOL "@:");
    add(k, "canBecomeMainWindow", (IMP)view_no, SK_ENC_BOOL "@:");
    objc_registerClassPair(k);

    Class d = objc_allocateClassPair((Class)sk_cls("NSObject"), "sk_app_delegate", 0);
    if (!d)
        sk_die("cannot make the class sk_app_delegate");
    add(d, "applicationShouldTerminate:", (IMP)app_should_terminate, "Q@:@");
    objc_registerClassPair(d);

    Class a = objc_allocateClassPair((Class)sk_cls("NSApplication"), "sk_application", 0);
    if (!a)
        sk_die("cannot make the class sk_application");
    add(a, "sendEvent:", (IMP)app_send_event, "v@:@");
    objc_registerClassPair(a);
    view_class = v;
}

/* The app menu, holding only Quit on Cmd+Q, so every other Cmd chord
 * reaches the app's window. */
static void make_menu(const char *app_id)
{
    id bar = sk_new("NSMenu"), item = sk_new("NSMenuItem"), menu = sk_new("NSMenu");
    char title[128];
    snprintf(title, sizeof title, "Quit %s", app_id && *app_id ? app_id : "skinned");
    id quit = SK_MSG(id (*)(id, SEL, id, SEL, id))(sk_msg(sk_cls("NSMenuItem"), "alloc"),
                                                    sk_sel("initWithTitle:action:keyEquivalent:"), sk_nsstring(title),
                                                    sk_sel("terminate:"), sk_nsstring("q"));
    sk_msg_void_id(menu, "addItem:", quit);
    sk_msg_void_id(item, "setSubmenu:", menu);
    sk_msg_void_id(bar, "addItem:", item);
    sk_msg_void_id(NSApp, "setMainMenu:", bar);
    sk_release(quit);
    sk_release(menu);
    sk_release(item);
    sk_release(bar);
}

bool sk_ui_open(const char *app_id, sk_ui_handler handler, void *ud, char **err)
{
    if (ui.open) {
        *err = sk_xstrdup("the UI is already open");
        return false;
    }
    if (!pthread_main_np()) {
        *err = sk_xstrdup("AppKit runs only on the main thread");
        return false;
    }
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session) {
        *err = sk_xstrdup("no window server: not in a graphical login session");
        return false;
    }
    CFRelease(session);

    void *pool = objc_autoreleasePoolPush();
    static bool launched;
    make_classes();
    if (!launched) {
        if (!NSApp)
            sk_msg(sk_cls("sk_application"), "sharedApplication");
        if (app_id && *app_id)
            sk_msg_void_id(sk_msg(sk_cls("NSProcessInfo"), "processInfo"), "setProcessName:", sk_nsstring(app_id));
        /* No Dock icon nor menu bar until a window is shown, so hidden
         * windows (tests) leave the desktop as it was. */
        SK_MSG(BOOL (*)(id, SEL, long))(NSApp, sk_sel("setActivationPolicy:"), NSApplicationActivationPolicyAccessory);
        SK_MSG(void (*)(id, SEL, BOOL))(sk_cls("NSWindow"), sk_sel("setAllowsAutomaticWindowTabbing:"), NO);
        /* A held key repeats, as a terminal's should, rather than offer
         * its accents: press and hold is an input method, which keys now
         * reach. The user's own setting, if any, still wins. */
        id no = SK_MSG(id (*)(id, SEL, BOOL))(sk_cls("NSNumber"), sk_sel("numberWithBool:"), NO);
        id hold = SK_MSG(id (*)(id, SEL, id, id))(sk_cls("NSDictionary"), sk_sel("dictionaryWithObject:forKey:"), no,
                                                  sk_nsstring("ApplePressAndHoldEnabled"));
        sk_msg_void_id(sk_msg(sk_cls("NSUserDefaults"), "standardUserDefaults"), "registerDefaults:", hold);
        make_menu(app_id);
        app_delegate = sk_new("sk_app_delegate");
        sk_msg_void_id(NSApp, "setDelegate:", app_delegate);
        sk_msg_void(NSApp, "finishLaunching");
        launched = true;
    }
    objc_autoreleasePoolPop(pool);

    atomic_store(&quit_asked, false);
    ui.srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    ui.handler = handler;
    ui.ud = ud;
    ui.open = true;
    return true;
}

static void wake(void *unused)
{
    (void)unused;
    void *pool = objc_autoreleasePoolPush();
    id ev = SK_MSG(id (*)(id, SEL, unsigned long, CGPoint, unsigned long, double, long, id, short, long, long))(
        sk_cls("NSEvent"),
        sk_sel("otherEventWithType:location:modifierFlags:timestamp:windowNumber:context:subtype:data1:data2:"),
        NSEventTypeApplicationDefined, CGPointZero, 0, 0.0, 0, nil, 0, 0, 0);
    SK_MSG(void (*)(id, SEL, id, BOOL))(NSApp, sk_sel("postEvent:atStart:"), ev, YES);
    objc_autoreleasePoolPop(pool);
}

bool sk_ui_run(void)
{
    if (!ui.open)
        return false;
    id until = sk_msg(sk_cls("NSDate"), "distantFuture");
    SEL next = sk_sel("nextEventMatchingMask:untilDate:inMode:dequeue:");
    while (!atomic_exchange(&quit_asked, false)) {
        void *pool = objc_autoreleasePoolPush();
        id ev = SK_MSG(id (*)(id, SEL, unsigned long long, id, id, BOOL))(NSApp, next, NSEventMaskAny, until,
                                                                         (id)(void *)kCFRunLoopDefaultMode, YES);
        if (ev)
            sk_msg_void_id(NSApp, "sendEvent:", ev);
        objc_autoreleasePoolPop(pool);
    }
    return true;
}

void sk_ui_quit(void)
{
    atomic_store(&quit_asked, true);
    /* nextEventMatchingMask returns for an event, not for a task, so the
     * task posts one. */
    dispatch_async_f(dispatch_get_main_queue(), NULL, wake);
}

/* ---- Tasks and timers ---- */

typedef struct {
    sk_ui_task task;
    void *ud;
} posted;

static void run_posted(void *p)
{
    posted t = *(posted *)p;
    free(p);
    void *pool = objc_autoreleasePoolPush();
    t.task(t.ud);
    objc_autoreleasePoolPop(pool);
}

void sk_ui_post(sk_ui_task task, void *ud)
{
    posted *p = sk_xmalloc(sizeof *p);
    p->task = task;
    p->ud = ud;
    dispatch_async_f(dispatch_get_main_queue(), p, run_posted);
}

static void remove_timer(size_t i)
{
    CFRunLoopTimerRef cf = ui.timers.ptr[i]->t;
    ui.timers.ptr[i] = ui.timers.ptr[--ui.timers.len];
    /* Invalidating releases the timer's context, which frees its
     * ui_timer. */
    CFRunLoopTimerInvalidate(cf);
    CFRelease(cf);
}

static void timer_fired(CFRunLoopTimerRef cf, void *info)
{
    (void)cf;
    ui_timer *t = info;
    sk_ui_task task = t->task;
    void *ud = t->ud;
    if (!t->repeat)
        for (size_t i = 0; i < ui.timers.len; i++)
            if (ui.timers.ptr[i] == t) {
                remove_timer(i);
                break;
            }
    void *pool = objc_autoreleasePoolPush();
    task(ud);
    objc_autoreleasePoolPop(pool);
}

static void timer_info_free(const void *info)
{
    free((void *)info);
}

int sk_ui_timer(uint64_t delay_ms, uint64_t repeat_ms, sk_ui_task task, void *ud)
{
    ui_timer *t = sk_xcalloc(1, sizeof *t);
    t->id = ++ui.timer_id;
    t->task = task;
    t->ud = ud;
    t->repeat = repeat_ms != 0;
    CFRunLoopTimerContext ctx = {0, t, NULL, timer_info_free, NULL};
    t->t = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + (double)delay_ms / 1000.0,
                                (double)repeat_ms / 1000.0, 0, 0, timer_fired, &ctx);
    if (!t->t)
        sk_die("CFRunLoopTimerCreate failed");
    CFRunLoopAddTimer(CFRunLoopGetMain(), t->t, kCFRunLoopCommonModes);
    sk_vec_push(&ui.timers, t);
    return t->id;
}

void sk_ui_cancel(int id)
{
    for (size_t i = 0; i < ui.timers.len; i++)
        if (ui.timers.ptr[i]->id == id) {
            remove_timer(i);
            return;
        }
}

/* ---- Windows ---- */

static void activate(void)
{
    if (!ui.regular) {
        SK_MSG(BOOL (*)(id, SEL, long))(NSApp, sk_sel("setActivationPolicy:"), NSApplicationActivationPolicyRegular);
        ui.regular = true;
    }
    if (sk_responds(NSApp, "activate"))
        sk_msg_void(NSApp, "activate");
    else
        sk_msg_void_bool(NSApp, "activateIgnoringOtherApps:", YES);
}

sk_window *sk_window_new(const sk_window_opts *o, char **err)
{
    if (!ui.open) {
        *err = sk_xstrdup("sk_ui_open was not called");
        return NULL;
    }
    void *pool = objc_autoreleasePoolPush();
    CGRect r = CGRectMake(0, 0, o->w > 0 ? o->w : 800, o->h > 0 ? o->h : 600);
    unsigned long style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable;
    id nswin = SK_MSG(id (*)(id, SEL, CGRect, unsigned long, unsigned long, BOOL))(
        sk_msg(sk_cls(o->no_focus ? "sk_nokey_window" : "NSWindow"), "alloc"),
        sk_sel("initWithContentRect:styleMask:backing:defer:"), r, style, NSBackingStoreBuffered, NO);
    if (!nswin) {
        objc_autoreleasePoolPop(pool);
        *err = sk_xstrdup("cannot make an NSWindow");
        return NULL;
    }
    sk_window *w = sk_xcalloc(1, sizeof *w);
    w->serial = ++ui.serial;
    w->win = nswin;
    w->ud = o->ud;
    w->hidden = o->hidden;
    w->no_focus = o->no_focus;
    w->creating = true;
    w->cursor = SK_CURSOR_ARROW;
    sk_msg_void_bool(nswin, "setReleasedWhenClosed:", NO);
    sk_msg_void_bool(nswin, "setRestorable:", NO);
    sk_window_set_title(w, o->title ? o->title : "");
    if (o->min_w > 0 || o->min_h > 0)
        SK_MSG(void (*)(id, SEL, CGSize))(nswin, sk_sel("setContentMinSize:"),
                                          CGSizeMake(SK_MAX(o->min_w, 1), SK_MAX(o->min_h, 1)));

    id view = SK_MSG(id (*)(id, SEL, CGRect))(sk_msg(sk_cls("sk_view"), "alloc"), sk_sel("initWithFrame:"), r);
    *win_slot(view) = w;
    w->view = view;
    sk_msg_void_bool(view, "setWantsLayer:", YES);
    sk_msg_void_long(view, "setLayerContentsRedrawPolicy:", NSViewLayerContentsRedrawNever);
    /* The pointer is followed over an inactive window too, for hover,
     * as Chromium's are. The click that brings such a window forward
     * stays AppKit's: the view keeps NSView's acceptsFirstMouse:, as
     * Electron does. */
    unsigned long track = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingCursorUpdate |
                          NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingEnabledDuringMouseDrag;
    id area = SK_MSG(id (*)(id, SEL, CGRect, unsigned long, id, id))(
        sk_msg(sk_cls("NSTrackingArea"), "alloc"), sk_sel("initWithRect:options:owner:userInfo:"), CGRectZero, track,
        view, nil);
    sk_msg_void_id(view, "addTrackingArea:", area);
    sk_release(area);
    sk_msg_void_id(nswin, "setContentView:", view);
    sk_msg_void_id(nswin, "setDelegate:", view);
    SK_MSG(BOOL (*)(id, SEL, id))(nswin, sk_sel("makeFirstResponder:"), view);
    update_size(w, false);

    CFRunLoopTimerContext ctx = {0, w, NULL, NULL, NULL};
    w->pacer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1e9, 1e9, 0, 0, pacer_fired, &ctx);
    CFRunLoopAddTimer(CFRunLoopGetMain(), w->pacer, kCFRunLoopCommonModes);
    /* On screen, frames follow the display's own refresh (macOS 14 and
     * later); a hidden window, which no display shows, keeps the timer. */
    if (!w->hidden && sk_responds(view, "displayLinkWithTarget:selector:")) {
        w->link = sk_retain(SK_MSG(id (*)(id, SEL, id, SEL))(view, sk_sel("displayLinkWithTarget:selector:"), view,
                                                            sk_sel("skFrame:")));
        sk_msg_void_bool(w->link, "setPaused:", YES);
        SK_MSG(void (*)(id, SEL, id, id))(w->link, sk_sel("addToRunLoop:forMode:"),
                                          sk_msg(sk_cls("NSRunLoop"), "mainRunLoop"),
                                          (id)(void *)kCFRunLoopCommonModes);
    }

    w->next = ui.windows;
    ui.windows = w;
    if (!w->hidden) {
        sk_msg_void(nswin, "center");
        /* A window that takes no focus is shown in front without the
         * app being brought forward or any window losing the keys. */
        if (w->no_focus) {
            sk_msg_void(nswin, "orderFrontRegardless");
        } else {
            activate();
            sk_msg_void_id(nswin, "makeKeyAndOrderFront:", nil);
        }
    }
    update_size(w, false);
    w->creating = false;
    objc_autoreleasePoolPop(pool);
    return w;
}

void sk_window_free(sk_window *w)
{
    if (!w)
        return;
    for (sk_window **p = &ui.windows; *p; p = &(*p)->next)
        if (*p == w) {
            *p = w->next;
            break;
        }
    void *pool = objc_autoreleasePoolPush();
    *win_slot(w->view) = NULL;
    sk_msg_void_id(w->win, "setDelegate:", nil);
    if (w->link) {
        sk_msg_void(w->link, "invalidate");
        sk_release(w->link);
    }
    CFRunLoopTimerInvalidate(w->pacer);
    CFRelease(w->pacer);
    if (w->inside && !w->hidden)
        hide_cursor(false);
    sk_msg_void(w->win, "close");
    objc_autoreleasePoolPop(pool);
    /* Inside the app's handler, AppKit may be in the middle of a call on
     * this window: it goes with the round's pool. */
    if (ui.depth) {
        sk_autorelease(w->view);
        sk_autorelease(w->win);
    } else {
        sk_release(w->view);
        sk_release(w->win);
    }
    if (w->shown)
        CGImageRelease(w->shown);
    sk_release(w->marked);
    drop_fronts(w);
    sk_image_free(&w->frame);
    free(w);
}

void *sk_window_ud(const sk_window *w)
{
    return w->ud;
}

void sk_window_set_title(sk_window *w, const char *title)
{
    void *pool = objc_autoreleasePoolPush();
    sk_msg_void_id(w->win, "setTitle:", sk_nsstring(title));
    objc_autoreleasePoolPop(pool);
}

void sk_window_focus(sk_window *w)
{
    if (w->hidden)
        return;
    void *pool = objc_autoreleasePoolPush();
    if (w->no_focus) {
        sk_msg_void(w->win, "orderFrontRegardless");
    } else {
        activate();
        sk_msg_void_id(w->win, "makeKeyAndOrderFront:", nil);
    }
    objc_autoreleasePoolPop(pool);
}

void sk_ui_close(void)
{
    if (!ui.open)
        return;
    while (ui.windows)
        sk_window_free(ui.windows);
    while (ui.timers.len)
        remove_timer(0);
    sk_vec_free(&ui.timers);
    hide_cursor(false);
    CGColorSpaceRelease(ui.srgb);
    ui.srgb = NULL;
    ui.handler = NULL;
    ui.ud = NULL;
    ui.open = false;
}
