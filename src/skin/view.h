/* The skin runtime (decision 0037, phase 6 of decision 0036): a skin
 * pack shown in a window's stage and made live, as skin.js, widgets.js
 * and splits.js of app/renderer made the Electron app's skin.
 *
 * A view keeps every element's state variables as decision 0037's
 * table says them (hover from the pointer, nodata and fallback from the
 * metrics, on from the widgets, open from the dials, the stage's list,
 * each row's item, a limits widget's count and each meter's model and
 * span), looks each element's style up in its state table, runs the
 * animations and transitions the styles name, lays the skin out at the
 * stage's size over decision 0006's scale k, and paints again only
 * what changed. Above the skin it draws what the app owns: the splits'
 * handles (decision 0035) and the menu button with its dot (decision
 * 0022, app.css's #menu-button). The terminal, the strips and the
 * drop-down are the window's (gui.c): a view never paints inside the
 * terminal's rectangle.
 *
 * Nothing here includes the pack's own types, so the window's code,
 * which includes platform/ui.h, can include this header.
 *
 * Threads. An sk_skinpack is immutable once made: any thread may make,
 * ref and unref one, so the core thread can load or compile a skin and
 * post it to the UI thread. A view, and every call that takes one,
 * belongs to the UI thread.
 *
 * The calls of a window, in order:
 *
 *   at a new skin       v = sk_skinview_new(pack, now)
 *                       sk_skinview_set_stage(v, ...)   the stage's place
 *                       sk_skinview_set_panels(v, ...)  the kept splits
 *                       then the metrics, dials, list choice, menu
 *   on a resize or      sk_skinview_set_stage(v, ...)
 *   a tab strip shown
 *   10 times a second   sk_skinview_set_metrics(v, ...) the active tab's
 *   as the app's state  sk_skinview_set_dials, _set_list_off,
 *   changes             _set_panels, _set_menu
 *   on pointer events   sk_skinview_mouse(v, ...)       its event says
 *                                                       what to do
 *   every SK_EV_FRAME   d = sk_skinview_step(v, now)    0: ask for the
 *                                                       next frame; > 0:
 *                                                       a timer of d ms
 *                       n = sk_skinview_draw(v, frame, all, rects, cap)
 *                       then the terminal and the strips over it, and
 *                       the rects presented
 *
 * After any set or mouse call, ask for a frame: the step finds out
 * whether anything changed, and returns at once when nothing did. */
#ifndef SK_SKIN_VIEW_H
#define SK_SKIN_VIEW_H

#include "../audio/sounds.h"
#include "../core/bus.h"
#include "../text/text.h"

/* ---- A pack ---- */

typedef struct sk_skinpack sk_skinpack;

/* A pack file's n bytes, through the loader's checks. own: the pack
 * frees bytes (malloc'd) when it goes; else they outlive it. NULL with
 * why in err (err_len bytes), bytes freed when owned. One reference. */
sk_skinpack *sk_skinpack_new(uint8_t *bytes, size_t n, bool own, char *err, size_t err_len);
sk_skinpack *sk_skinpack_ref(sk_skinpack *p);
/* NULL does nothing. */
void sk_skinpack_unref(sk_skinpack *p);

/* The manifest's name, the skin's id (decision 0008). */
const char *sk_skinpack_name(const sk_skinpack *p);
/* The SHA-256 of the skin's files the pack was compiled from. */
const unsigned char *sk_skinpack_hash(const sk_skinpack *p);

/* The window of decision 0006, in points, the identity strip in it:
 * the first size, and scale.js's floor, to which the window adds the
 * tab strip while it shows. */
typedef struct {
    int width, height;
    int min_width, min_height;
} sk_skin_window;

void sk_skinpack_window(const sk_skinpack *p, sk_skin_window *out);

/* The terminal theme's colours, in validate.js's THEME_KEYS order. */
enum {
    SK_THEME_FOREGROUND,
    SK_THEME_BACKGROUND,
    SK_THEME_CURSOR,
    SK_THEME_CURSOR_ACCENT,
    SK_THEME_SELECTION_BACKGROUND,
    SK_THEME_SELECTION_FOREGROUND,
    SK_THEME_SELECTION_INACTIVE_BACKGROUND,
    SK_THEME_BLACK, /* SK_THEME_BLACK + i is ANSI colour i, to bright white */
    SK_THEME_BRIGHT_WHITE = SK_THEME_BLACK + 15,
    SK_THEME_COUNT,
};

/* What the skin gives its terminals (skin.js's styleTerminal). */
typedef struct {
    uint32_t colors[SK_THEME_COUNT]; /* 0xAARRGGBB */
    uint32_t has;                    /* bit i: colors[i] was given */
    const sk_font *face;             /* the skin's font, the pack's; NULL: DejaVu Sans Mono */
    float font_px;                   /* the skin's size, 14 when it gives none */
} sk_skin_terminal;

void sk_skinpack_terminal(const sk_skinpack *p, sk_skin_terminal *out);
/* The terminals' font size: the user's setting (0 when unset) over
 * the skin's, as prefs['terminal.fontSize'] || t.fontSize || 14. */
float sk_skin_font_px(const sk_skin_terminal *t, double setting);

/* The pack's sounds (decision 0024) for a window's player: each
 * decoded, with a reference the caller drops with
 * sk_skin_sounds_release after sk_sounds_set. At most cap entries. */
size_t sk_skinpack_sounds(const sk_skinpack *p, sk_sounds_entry *out, size_t cap);
void sk_skin_sounds_release(sk_sounds_entry *e, size_t n);

/* ---- The metrics ---- */

/* One metric, as main.js's 'metrics' message carries it. */
typedef struct {
    const char *name;
    sk_val value;
} sk_skin_metric;

/* A copy of a bus's values, for the UI thread: made where the bus
 * lives (the core thread), posted, given to the view and freed. */
typedef struct {
    sk_skin_metric *items;
    size_t n;
} sk_skin_metrics;

void sk_skin_metrics_of(const sk_bus *b, sk_skin_metrics *out);
void sk_skin_metrics_free(sk_skin_metrics *m);

/* ---- The view ---- */

typedef struct sk_skinview sk_skinview;

/* A view of p, which it holds a reference to. now: the clock in ms,
 * which every later step continues. */
sk_skinview *sk_skinview_new(sk_skinpack *p, double now);
void sk_skinview_free(sk_skinview *v);
sk_skinpack *sk_skinview_pack(const sk_skinview *v);

/* Where the stage lies in the window (below the strips), in points,
 * and the window's scale, device pixels a point. The skin is laid out
 * at w / k by h / k and drawn at k (decision 0006). */
void sk_skinview_set_stage(sk_skinview *v, double x, double y, double w, double h, double scale);

/* The active tab's metrics, replacing the last (copied). */
void sk_skinview_set_metrics(sk_skinview *v, const sk_skin_metric *m, size_t n);
/* The dials open (decision 0033), by metric name (copied). */
void sk_skinview_set_dials(sk_skinview *v, const char *const *names, size_t n);
/* The active tab's choice of what its list replaces (decision 0034). */
void sk_skinview_set_list_off(sk_skinview *v, bool off);
/* The sizes the user keeps for this skin's splits, by id (settings.json's
 * panels of the skin, decision 0035); a split not named takes its own. */
void sk_skinview_set_panels(sk_skinview *v, const char *const *ids, const double *sizes, size_t n);
/* The drop-down open (the button shows pressed), and an unseen problem
 * of this launch (the button's dot). */
void sk_skinview_set_menu(sk_skinview *v, bool open, bool unseen);

/* Advances the widgets, animations and transitions to now (ms) and
 * lays out what changed. Returns the ms until the next step is due: 0
 * for the next display refresh, more for a timer (a lamp's pulse
 * ending, a blink), below 0 when nothing moves until the next call. */
double sk_skinview_step(sk_skinview *v, double now);

/* Paints into frame (the window's) what changed since the last draw,
 * or the whole stage when all, then the handles and the menu button
 * over it, never inside the terminal's rectangle. The rectangles
 * painted go to out, at most cap of them, in frame pixels; when there
 * are more, out[cap - 1] holds the rest. Returns how many. */
size_t sk_skinview_draw(sk_skinview *v, sk_image *frame, bool all, sk_rect *out, size_t cap);

/* The scale k the skin is drawn at. */
double sk_skinview_scale(const sk_skinview *v);
/* The terminal's rectangle with the splits at their sizes, in window
 * points, and in frame pixels as the view leaves it unpainted. */
void sk_skinview_terminal_rect(const sk_skinview *v, double out[4]);
sk_rect sk_skinview_terminal_px(const sk_skinview *v);
/* The menu button's rectangle in window points (x, y, w, h), for the
 * drop-down to open below. */
void sk_skinview_menu_rect(const sk_skinview *v, double out[4]);

/* ---- Pointer ---- */

typedef enum {
    SK_SKIN_MOUSE_MOVE,
    SK_SKIN_MOUSE_DOWN,
    SK_SKIN_MOUSE_UP,
    SK_SKIN_MOUSE_LEAVE,
} sk_skin_mouse;

/* The pointer's shape over the skin: NONE outside it (the terminal's
 * rectangle, outside the stage), where the window chooses. */
typedef enum {
    SK_SKIN_CURSOR_NONE,
    SK_SKIN_CURSOR_ARROW,
    SK_SKIN_CURSOR_POINTER,
    SK_SKIN_CURSOR_TEXT,
    SK_SKIN_CURSOR_RESIZE_EW,
    SK_SKIN_CURSOR_RESIZE_NS,
} sk_skin_cursor;

typedef enum {
    SK_SKIN_NOTHING,    /* not the skin's: the window handles it */
    SK_SKIN_TAKEN,      /* the skin's, nothing more to do */
    SK_SKIN_MENU,       /* the button pressed: open or close the drop-down */
    SK_SKIN_DIAL,       /* a readout clicked: toggle the dial of metric (dial:toggle) */
    SK_SKIN_TOGGLE,     /* a list's toggle clicked: the tab's choice is now off */
    SK_SKIN_SPLIT_MOVE, /* a split moved: the terminal's rectangle changed */
    SK_SKIN_SPLIT_DONE, /* a drag let go: keep split at size (panels:set) */
} sk_skin_action;

typedef struct {
    sk_skin_action action;
    const char *metric; /* DIAL, valid until the next call on the view */
    bool off;           /* TOGGLE */
    const char *split;  /* SPLIT_MOVE, SPLIT_DONE: the split's id */
    double size;        /* SPLIT_DONE: its size, rounded */
} sk_skin_event;

/* A pointer event at (x, y) window points; button 1 is the left. While
 * a drag or a press the skin took is held, every event is the skin's. */
sk_skin_event sk_skinview_mouse(sk_skinview *v, sk_skin_mouse what, double x, double y, int button);
sk_skin_cursor sk_skinview_cursor(const sk_skinview *v);
/* Whether (x, y) lies on the skin and not on the terminal. */
bool sk_skinview_covers(const sk_skinview *v, double x, double y);

#endif
