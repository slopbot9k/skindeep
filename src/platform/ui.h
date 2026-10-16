/* The windows (decision 0036): X11's wire protocol over a socket on
 * Linux, AppKit and CoreGraphics through the Objective-C runtime on
 * macOS. The app draws each window's frame in software into an sk_image
 * (gfx/image.h) and presents it; the layer brings the frame to the
 * screen and the user's input back as events.
 *
 * One UI thread owns every window: the thread that calls sk_ui_open and
 * sk_ui_run, which on macOS must be the main thread. Every function
 * here is called on it, except sk_ui_post and sk_ui_quit, which any
 * thread may call. Events reach the handler on the UI thread.
 *
 * Sizes and positions in events are logical points; a window's frame is
 * in device pixels, its size in points times its scale. */
#ifndef SK_PLATFORM_UI_H
#define SK_PLATFORM_UI_H

#include "../gfx/image.h"

typedef struct sk_window sk_window;

/* Modifier bits. SUPER is Cmd on macOS. */
enum {
    SK_MOD_SHIFT = 1 << 0,
    SK_MOD_CTRL = 1 << 1,
    SK_MOD_ALT = 1 << 2, /* Option on macOS */
    SK_MOD_SUPER = 1 << 3,
    SK_MOD_CAPS = 1 << 4,
};

/* The keys that are not characters. A key that types a character is
 * SK_KEY_NONE with its text. */
typedef enum {
    SK_KEY_NONE,
    SK_KEY_ENTER,
    SK_KEY_TAB,
    SK_KEY_BACKSPACE,
    SK_KEY_ESCAPE,
    SK_KEY_UP,
    SK_KEY_DOWN,
    SK_KEY_LEFT,
    SK_KEY_RIGHT,
    SK_KEY_HOME,
    SK_KEY_END,
    SK_KEY_PAGE_UP,
    SK_KEY_PAGE_DOWN,
    SK_KEY_INSERT,
    SK_KEY_DELETE,
    SK_KEY_F1, /* SK_KEY_F1 + n - 1 is Fn, to F24 */
    SK_KEY_F24 = SK_KEY_F1 + 23,
    SK_KEY_KP_0, /* the keypad's digits, SK_KEY_KP_0 + n */
    SK_KEY_KP_9 = SK_KEY_KP_0 + 9,
    SK_KEY_KP_DECIMAL,
    SK_KEY_KP_DIVIDE,
    SK_KEY_KP_MULTIPLY,
    SK_KEY_KP_SUBTRACT,
    SK_KEY_KP_ADD,
    SK_KEY_KP_ENTER,
    SK_KEY_KP_EQUAL,
    SK_KEY_MENU,
} sk_key;

typedef struct {
    sk_key key;
    uint32_t mods;
    /* The character the key types with the layout's Shift, AltGr and
     * Option applied, as UTF-8; empty for a named key, a dead key, and
     * any key held with Ctrl or Super whose layout gives nothing. */
    char text[16];
    /* The key's character on the layout with no modifier at all, a
     * letter in lower case: what a Ctrl or Cmd chord names. 0 for a
     * named key. */
    uint32_t base;
    bool repeat;
    /* The platform's own key code, for logs. */
    uint32_t code;
} sk_key_event;

typedef enum {
    SK_CURSOR_ARROW,
    SK_CURSOR_IBEAM,
    SK_CURSOR_POINTER,
    SK_CURSOR_RESIZE_EW,
    SK_CURSOR_RESIZE_NS,
    SK_CURSOR_HIDDEN,
} sk_cursor;

typedef enum {
    SK_EV_CLOSE,       /* the user asked to close the window */
    SK_EV_RESIZE,      /* size or scale changed; the frame now has the new size */
    SK_EV_FRAME,       /* time to draw, after sk_window_request_frame */
    SK_EV_FOCUS,       /* focus in (focused) or out */
    SK_EV_KEY_DOWN,
    SK_EV_KEY_UP,
    SK_EV_TEXT,        /* text from an input method (phase 4) */
    SK_EV_PREEDIT,     /* an input method's composition, "" when it ends (phase 4) */
    SK_EV_MOUSE_DOWN,
    SK_EV_MOUSE_UP,
    SK_EV_MOUSE_MOVE,
    SK_EV_MOUSE_LEAVE,
    SK_EV_SCROLL,
} sk_event_type;

typedef struct {
    sk_event_type type;
    sk_window *win;
    union {
        struct {
            int w, h; /* points */
            double scale;
        } size;
        bool focused;
        sk_key_event key;
        const char *text; /* TEXT, PREEDIT: UTF-8, valid during the call */
        struct {
            double x, y; /* points from the content's top-left */
            int button;  /* 1 left, 2 middle, 3 right; 0 for a move */
            int clicks;  /* 1, 2 for a double click, 3 for a triple */
            uint32_t mods;
        } mouse;
        struct {
            double x, y;
            double dx, dy; /* lines, or points when precise; dy > 0 scrolls up */
            bool precise;
            uint32_t mods;
        } scroll;
    };
} sk_event;

typedef void (*sk_ui_handler)(void *ud, const sk_event *ev);
typedef void (*sk_ui_task)(void *ud);

/* Connects to the display: $DISPLAY on Linux, the window server on
 * macOS. app_id names the app to the system (WM_CLASS, the bundle-less
 * process name). false with *err a malloc'd reason. */
bool sk_ui_open(const char *app_id, sk_ui_handler handler, void *ud, char **err);
/* Runs the UI thread's loop until sk_ui_quit; returns at once when the
 * display connection is lost, with false. */
bool sk_ui_run(void);
/* Thread-safe: sk_ui_run returns after the round in progress. */
void sk_ui_quit(void);
/* Closes every window and the connection. */
void sk_ui_close(void);

/* Thread-safe: runs task(ud) on the UI thread, in the order posted. */
void sk_ui_post(sk_ui_task task, void *ud);
/* A timer on the UI thread: task after delay_ms, then every repeat_ms
 * when it is not 0. Returns an id for sk_ui_cancel. */
int sk_ui_timer(uint64_t delay_ms, uint64_t repeat_ms, sk_ui_task task, void *ud);
void sk_ui_cancel(int id);

/* ---- The clipboard ---- */

/* The system clipboard (CLIPBOARD on X11) now holds text. */
void sk_clipboard_set(const char *utf8);
/* X11's PRIMARY selection, set by selecting; nothing on macOS. */
void sk_primary_set(const char *utf8);
/* The clipboard's text, delivered to done on the UI thread (X11 asks
 * the owner and waits for its answer, INCR included): NULL when there
 * is none. primary: X11's PRIMARY instead. */
void sk_clipboard_get(bool primary, void (*done)(void *ud, const char *utf8), void *ud);

/* ---- Windows ---- */

typedef struct {
    const char *title;
    int w, h;         /* points */
    int min_w, min_h; /* points; 0 for none */
    void *ud;         /* sk_window_ud */
    /* Not shown on screen, for tests: it has its frame, takes presents
     * and readbacks, and gets the events a test sends it. */
    bool hidden;
} sk_window_opts;

/* A window, shown unless o->hidden. NULL with *err a malloc'd reason. */
sk_window *sk_window_new(const sk_window_opts *o, char **err);
/* Closes it at once; no event names it after. */
void sk_window_free(sk_window *w);
void *sk_window_ud(const sk_window *w);
void sk_window_set_title(sk_window *w, const char *title);
void sk_window_size(const sk_window *w, int *pw, int *ph);
double sk_window_scale(const sk_window *w);
void sk_window_set_cursor(sk_window *w, sk_cursor c);
/* Raise and give it the keyboard. */
void sk_window_focus(sk_window *w);

/* The window's frame: an image of its size in pixels, which the app
 * draws into. What was drawn stays from frame to frame, until a resize
 * gives a new image, cleared. Valid until the next resize. */
sk_image *sk_window_frame(sk_window *w);
/* Brings the dirty rectangles of the frame (pixels) to the screen; n 0
 * for all of it. */
void sk_window_present(sk_window *w, const sk_rect *dirty, size_t n);
/* One SK_EV_FRAME at the next display refresh, however often asked. */
void sk_window_request_frame(sk_window *w);
/* For tests: what the system now shows of the window's content, read
 * back into out (sk_image_init'ed by the call). false when it cannot. */
bool sk_window_readback(sk_window *w, sk_image *out);

#endif
