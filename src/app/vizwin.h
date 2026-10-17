/* The visualization's window (decision 0015), as app/viz/index.html,
 * viz.js and layout.js draw it: beside the app's windows and never
 * taking the focus from them, tiled by layout.h into one pane per tab
 * with a session, each pane a synthesiser and a renderer stepped and
 * drawn each display frame. A pane shows its place and its mode in a
 * caption, and a button at its top left naming its mode; with more
 * than one pane the active tab's is outlined. A pane's drop-down, open
 * as the feed says, lists every mode over the window.
 *
 * The feed (vizfeed.h) owns the modes and whether the window is open:
 * the window follows its frames, and sends back only a left press, the
 * space bar and the user's close, posted to the feed's thread. The
 * pointer is the window's own: a press, its moves and its release go to
 * the mode of the pane under the press, which holds the pointer until
 * the release; a mode with no answer of its own hears a tone in its
 * pane's signal instead. A press on a button, or any press while a
 * drop-down is open, reaches no mode.
 *
 * A pane's renderer is made anew at each change of its mode; the five
 * MilkDrop modes are the MilkDrop core's (viz/milk.h), one per pane,
 * which blends 1.5 s from one of them to the next.
 *
 * Every call is made on the UI thread. */
#ifndef SK_APP_VIZWIN_H
#define SK_APP_VIZWIN_H

#include "../platform/ui.h"
#include "../text/text.h"
#include "vizfeed.h"

/* viz.js's window, in points. */
#define SK_VIZWIN_W 720
#define SK_VIZWIN_H 460
#define SK_VIZWIN_MIN_W 320
#define SK_VIZWIN_MIN_H 200

typedef struct {
    /* DejaVu Sans Mono and a glyph cache, the UI thread's; they outlive
     * the window. */
    sk_fontset *mono;
    sk_glyph_cache *cache;
    /* Where the window's presses, its space bar and its close go;
     * NULL for nowhere. */
    sk_vizfeed *feed;
    const char *title;
    /* The window off the screen (tests). */
    bool hidden;
    /* No window at all: sk_vizwin_render draws it, and the pointer comes
     * through sk_vizwin_pointer (tests, tools). */
    bool offscreen;
} sk_vizwin_config;

typedef struct sk_vizwin sk_vizwin;

sk_vizwin *sk_vizwin_new(const sk_vizwin_config *c);
/* Closes the window, if open, and frees everything. */
void sk_vizwin_free(sk_vizwin *v);

/* A frame of the feed, which the caller still owns: the window opens or
 * closes as it says, each pane is made, dropped or moved to its place,
 * takes its numbers and events, and shows its mode. A window the user
 * closed stays closed for the frames of that opening. */
void sk_vizwin_frame(sk_vizwin *v, const sk_vizfeed_msg *m);

/* An event of the platform's: false when it is not this window's, for
 * gui.c's on_event to call first. Its own it takes whole: the close,
 * each display frame, a resize, the space bar and the pointer. */
bool sk_vizwin_event(sk_vizwin *v, const sk_event *ev);

void sk_vizwin_set_title(sk_vizwin *v, const char *title);
/* The window while it is open, else NULL. */
sk_window *sk_vizwin_window(const sk_vizwin *v);
bool sk_vizwin_is_open(const sk_vizwin *v);

/* Every pane stepped dt seconds and the window drawn into dst, w by h
 * points at scale (dst is their size in pixels): what each display frame
 * does. */
void sk_vizwin_render(sk_vizwin *v, sk_image *dst, int w, int h, double scale, double dt);

/* The pointer as the window gets it: a press, a release (button 1 is
 * the left) or a move at (x, y) points, at now ms; SK_EV_MOUSE_LEAVE
 * when it left. The window's size is its own, or the last render's. */
void sk_vizwin_pointer(sk_vizwin *v, sk_event_type type, double x, double y, int button, uint64_t now);

/* For tests: how many panes, the tab of pane i, the mode it shows (-1
 * for none yet), its synthesiser, and the tab holding the pointer (0
 * for none). */
size_t sk_vizwin_panes(const sk_vizwin *v);
int sk_vizwin_pane_tab(const sk_vizwin *v, size_t i);
int sk_vizwin_shown(const sk_vizwin *v, size_t i);
const sk_synth *sk_vizwin_synth(const sk_vizwin *v, size_t i);
int sk_vizwin_held(const sk_vizwin *v);

#endif
