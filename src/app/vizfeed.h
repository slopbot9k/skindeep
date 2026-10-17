/* The visualization's core side (decision 0015), as app/viz.js has it in
 * the main process: 30 times a second a numeric frame of every tab with
 * a pane, from its bus, as one message for the UI thread, which hands
 * it to the window (vizwin.h) and to the tab strip's bands (tabviz.h).
 * Every tab's events are drained here alone, whether the window is open
 * or not, so none piles up and the pane and the band see the same ones.
 *
 * The feed owns the modes: each pane's, which the space bar and the
 * menu set for the active tab's pane and a pane's own drop-down for that
 * pane, and the input, the sessions, the sound output or both. Each pick
 * is recorded as the last skin is (decision 0022), one file per window
 * beside last-skins, the newest read at launch. The window's open state
 * is the feed's too: the UI opens and closes the window as the frames
 * say.
 *
 * The sound output is recorded only while the window is open on an
 * input that takes it: parec, else pw-record, run as a child on the
 * loop and read through a pipe, its newest 1024 samples sent in the
 * frame. On macOS no recorder is known and the sound inputs are listed
 * as unavailable (decision 0036: speaker capture is Linux-only at
 * first).
 *
 * Every call is made on the core thread, the loop's, save the
 * sk_vizfeed_post_ ones, which any thread may make. */
#ifndef SK_APP_VIZFEED_H
#define SK_APP_VIZFEED_H

#include "../base/paths.h"
#include "../core/loop.h"
#include "../viz/synth.h"

#include <sys/types.h>

/* The menu's Visualization input, in its order: the index is the input
 * (SK_VIZ_SRC_SESSION, SK_VIZ_SRC_SOUND, SK_VIZ_SRC_BOTH). */
#define SK_VIZ_INPUTS 3
typedef struct {
    const char *id;    /* what the menu and the record use */
    const char *label; /* what the menu shows */
} sk_viz_input_info;
extern const sk_viz_input_info sk_viz_inputs[SK_VIZ_INPUTS];

/* A recorder of the sound output: the command, found on PATH, and its
 * arguments after the program, NULL-terminated. It writes mono 16-bit
 * little-endian samples at 44.1 kHz to its stdout. */
typedef struct {
    const char *cmd;
    const char *const *args;
} sk_viz_recorder;
/* PulseAudio's parec on the default sink's monitor, which PipeWire's
 * pulse server answers too, then PipeWire's pw-record: Linux's. */
#define SK_VIZ_RECORDERS_LINUX 2
extern const sk_viz_recorder sk_viz_recorders_linux[SK_VIZ_RECORDERS_LINUX];

/* The sound output's state, as viz.js's Sound has it; the frame sends
 * none yet for off and starting (synth.h's SK_VIZ_SOUND_). */
enum { SK_VIZFEED_SOUND_OFF, SK_VIZFEED_SOUND_STARTING, SK_VIZFEED_SOUND_LIVE, SK_VIZFEED_SOUND_FAILED };

/* A tab of the list, as the caller gives it. */
typedef struct {
    int id;
    /* Its bus, read and drained on the core thread; NULL for a tab with
     * none. It must stay valid until the next sk_vizfeed_tabs. */
    sk_bus *bus;
    bool active; /* the active tab: the current window's active tab */
    bool pane;   /* drawn by a pane: a CLI or monitor tab, not the dashboard */
} sk_vizfeed_tab;

/* A frame, posted 30 times a second. The callee owns it and frees it
 * with sk_vizfeed_msg_free, on any thread. */
typedef struct {
    /* Every tab with a pane, in the list's order, each with its numbers
     * and the events drained this frame: the window's frame while it is
     * open. While it is closed a pane's mode is -1 and none is open,
     * and the samples are none. */
    sk_viz_frame frame;
    /* For the tab strip, per pane: whether the tab has no busy state
     * (u of feed()), and its quiet seconds since its terminal's last
     * output or its last event, whichever is nearer (q). */
    const bool *no_busy;
    const double *quiet;
    /* Whether the window is to be open, and how many times the feed has
     * opened it: a window the user closed is not opened again by the
     * frames that crossed its close. */
    bool open;
    unsigned opened;
} sk_vizfeed_msg;

void sk_vizfeed_msg_free(sk_vizfeed_msg *m);

typedef struct {
    sk_loop *loop;
    /* Where the picks are recorded, as sk_vizfeed_mode_dir and
     * sk_vizfeed_input_dir give them; NULL for no record. */
    const char *mode_dir, *input_dir;
    /* The pid the records are written as; 0 for this process's. */
    pid_t pid;
    /* The sound output's recorders, tried in order; NULL for the
     * system's: sk_viz_recorders_linux on Linux, none on macOS. With
     * none, the sound inputs are unavailable. */
    const sk_viz_recorder *recorders;
    size_t n_recorders;
    /* Each frame, on the core thread; the callee owns msg. NULL drops
     * the frames. */
    void (*frame)(void *ud, sk_vizfeed_msg *msg);
    /* The app's log; NULL for none. */
    void (*log)(void *ud, const char *msg);
    void *ud;
    /* No timer of its own: the caller calls sk_vizfeed_tick (tests). */
    bool manual;
} sk_vizfeed_config;

typedef struct sk_vizfeed sk_vizfeed;

/* <state>/last-viz and <state>/last-viz-input, beside last-skins,
 * malloc'd; NULL without a home. */
char *sk_vizfeed_mode_dir(const sk_env *e);
char *sk_vizfeed_input_dir(const sk_env *e);

/* Reads and prunes the records, and starts the feed's timer unless
 * c->manual. The strings of c are copied. */
sk_vizfeed *sk_vizfeed_new(const sk_vizfeed_config *c);
/* Stops the recorder it started, and nothing else, and frees the feed.
 * Tasks posted to it must have run. */
void sk_vizfeed_free(sk_vizfeed *f);

/* Every tab of every window, in the order of the tabs' list (each
 * window's in its strip's order): on every change of the list, the
 * order or the active tab, and before a bus of the last list is freed.
 * A pane's place is its tab's place in this list, from 1. */
void sk_vizfeed_tabs(sk_vizfeed *f, const sk_vizfeed_tab *tabs, size_t n);
/* The active tab alone changed. */
void sk_vizfeed_active(sk_vizfeed *f, int id);

/* One frame at now, monotonic ms: the timer's, every 33 ms. */
void sk_vizfeed_tick(sk_vizfeed *f, uint64_t now);

bool sk_vizfeed_is_open(const sk_vizfeed *f);
/* The window opens, or closes, from the next frame on. The sound
 * output is recorded while it is open on an input that takes it. */
void sk_vizfeed_open(sk_vizfeed *f);
void sk_vizfeed_close(sk_vizfeed *f);
void sk_vizfeed_toggle(sk_vizfeed *f);

/* A row of the menu's flyouts. */
typedef struct {
    const char *id, *label;
    bool current;
    bool available; /* false: the row is shown disabled */
} sk_vizfeed_choice;

/* Every mode, in sk_viz_modes's order, the active tab's pane's
 * current: SK_VIZ_MODES of them into out. Returns how many. */
size_t sk_vizfeed_modes(sk_vizfeed *f, sk_vizfeed_choice *out);
/* Every input, the current one marked: SK_VIZ_INPUTS of them. */
size_t sk_vizfeed_inputs(const sk_vizfeed *f, sk_vizfeed_choice *out);

/* The menu's pick of mode, for the active tab's pane, recorded, the
 * window opened when it is closed. False for an id that is no mode. */
bool sk_vizfeed_select(sk_vizfeed *f, const char *id);
/* The menu's pick of input, recorded, the window opened. False with
 * *why (static) for an id that is no input, or one this system cannot
 * record. */
bool sk_vizfeed_select_input(sk_vizfeed *f, const char *id, const char **why);

/* The space bar: the active tab's pane steps to the next mode. */
void sk_vizfeed_step(sk_vizfeed *f);
/* A left press at (x, y) points in the window, w by h points, as the
 * window saw it: a pane's button opens its drop-down or closes it, a
 * row of the open drop-down shows that mode in that pane alone,
 * recorded, and any other press closes the drop-down. */
void sk_vizfeed_press(sk_vizfeed *f, double x, double y, int w, int h);
/* Tab tab's pane shows mode, recorded; false for no such pane or mode. */
bool sk_vizfeed_set_mode(sk_vizfeed *f, int tab, int mode);

/* The mode tab's pane shows, a new pane given its first; -1 for a tab
 * with no pane. */
int sk_vizfeed_mode_of(sk_vizfeed *f, int tab);
/* The tab whose pane's drop-down is open, or 0. */
int sk_vizfeed_menu_tab(const sk_vizfeed *f);
/* The input, an index of sk_viz_inputs. */
int sk_vizfeed_input(const sk_vizfeed *f);
/* The sound output's state, SK_VIZFEED_SOUND_, and when it failed the
 * reason, which every recorder tried adds to: valid until the next call
 * on the feed. */
int sk_vizfeed_sound(const sk_vizfeed *f, const char **reason);
/* The newest SK_SYNTH_N samples recorded, oldest first. */
void sk_vizfeed_samples(const sk_vizfeed *f, float *out);

/* From any thread, run on the feed's loop: the window's press, its
 * space bar, and its close by the user, opened being the msg's count
 * of the window that closed. */
void sk_vizfeed_post_press(sk_vizfeed *f, double x, double y, int w, int h);
void sk_vizfeed_post_step(sk_vizfeed *f);
void sk_vizfeed_post_closed(sk_vizfeed *f, unsigned opened);

#endif
