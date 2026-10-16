/* The user's input as the bytes a program reads (phase 4 of decision
 * 0036): keys, the mouse, focus and paste, as xterm.js 6.0.0 sent them
 * from the Electron app. That is evaluateKeyboardEvent and the keydown,
 * keypress and input handlers around it; CoreMouseService and the mouse
 * and wheel listeners; the focus reports; the paste. With the app's
 * options: macOptionIsMeta false, so Option composes characters on
 * macOS; scrollSensitivity 1 and fastScrollSensitivity 5. Ported from
 * xterm.js, (c) 2014-2019 The xterm.js authors and (c) 2012-2013
 * Christopher Jeffrey, MIT licence.
 *
 * The rules differ by platform where xterm.js asks isMac: Alt is an ESC
 * prefix on Linux, and Option types the layout's character on macOS.
 * The functions below follow the build's platform; the _as forms take
 * it, so the tests check both everywhere. */
#ifndef SK_TERM_KEYS_H
#define SK_TERM_KEYS_H

#include "../base/str.h"
#include "../platform/ui.h"
#include "input.h"

#ifdef __APPLE__
#define SK_TERM_MAC true
#else
#define SK_TERM_MAC false
#endif

/* ---- Keys ---- */

typedef enum {
    SK_TERM_KEY_NONE,      /* nothing reaches the program */
    SK_TERM_KEY_SEND,      /* out has the bytes */
    SK_TERM_KEY_PAGE_UP,   /* Shift+Page Up: the view scrolls up rows - 1 */
    SK_TERM_KEY_PAGE_DOWN, /* Shift+Page Down */
} sk_term_key_result;

/* A key down, appended to out as bytes. The event as ui.h makes it:
 *   mods  ALT is Alt (Mod1) or Option, never AltGr, whose character
 *         is in text. SUPER is Cmd on macOS, where every Cmd chord is
 *         the app's and gives nothing here.
 *   text  what the key types, as a browser's KeyboardEvent.key has it:
 *         Shift, AltGr, Option and Caps Lock applied, also under Ctrl
 *         ("_" for Ctrl+Shift+-); empty for a dead key.
 *   base  the key's unshifted character. a..z, 0..9, space and the US
 *         layout's punctuation stand for Chromium's keyCode, which the
 *         Ctrl and Alt rules read; any other base names no keyCode.
 * The app's own chords (Ctrl+Shift+C and V, Shift+Insert) are taken
 * before this. Text an input method commits (SK_EV_TEXT) goes to the
 * program as it is. */
sk_term_key_result sk_term_key(const sk_key_event *ev, const sk_term_input_modes *m, sk_buf *out);
sk_term_key_result sk_term_key_as(bool mac, const sk_key_event *ev, const sk_term_input_modes *m,
                                  sk_buf *out);

/* ---- Mouse ---- */

/* CoreMouseService's buttons and actions. A wheel's action is UP for a
 * scroll up, DOWN for a scroll down. */
typedef enum {
    SK_TERM_BUTTON_LEFT,
    SK_TERM_BUTTON_MIDDLE,
    SK_TERM_BUTTON_RIGHT,
    SK_TERM_BUTTON_NONE,
    SK_TERM_BUTTON_WHEEL,
} sk_term_button;

typedef enum {
    SK_TERM_MOUSE_UP = 0,
    SK_TERM_MOUSE_DOWN = 1,
    SK_TERM_MOUSE_MOVE = 32,
} sk_term_mouse_action;

typedef struct {
    int col, row; /* the cell, from 0 */
    int x, y;     /* points from the grid's top left, for SGR pixels */
    sk_term_button button;
    sk_term_mouse_action action;
    bool ctrl, alt, shift;
} sk_term_mouse_event;

/* The grid under the pointer: its size in cells and a cell's in
 * points. */
typedef struct {
    int cols, rows;
    double cell_w, cell_h;
} sk_term_grid;

/* What the mouse carries from one event to the next. */
typedef struct {
    bool mac;         /* the platform's rules: sk_term_mouse_init sets the build's */
    unsigned buttons; /* held, as MouseEvent.buttons: 1 left, 2 right, 4 middle */
    /* A press the terminal took: its release and its drags reach the
     * terminal wherever the pointer is, as listeners on the document. */
    bool up_listening, drag_listening;
    bool has_last;
    sk_term_mouse_event last; /* the last report, against repeated moves */
    double wheel_partial;     /* a wheel's lines short of one */
} sk_term_mouse;

void sk_term_mouse_init(sk_term_mouse *m);
/* What a full reset (RIS) does to CoreMouseService beyond the modes:
 * no last report, no partial wheel. */
void sk_term_mouse_reset(sk_term_mouse *m);

/* CoreMouseService.triggerMouseEvent: the report of e under the
 * program's protocol and encoding, appended to out. false when the
 * protocol takes no such event, or e repeats the last move; true when
 * it does, even when the default encoding cannot address the cell and
 * writes nothing. */
bool sk_term_mouse_report(sk_term_mouse *m, const sk_term_input_modes *modes, int cols, int rows,
                          sk_term_mouse_event e, sk_buf *out);

/* The pointer's events, x and y in points from the grid's top left
 * (outside it during a drag), button 1 left, 2 middle, 3 right, more for
 * the others. Each returns true when the terminal took the event, so the
 * app neither selects nor scrolls with it. A press is the terminal's
 * whenever the program reads the mouse, except with Shift on Linux,
 * which selects. */
bool sk_term_mouse_down(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                        double y, int button, uint32_t mods, sk_buf *out);
bool sk_term_mouse_up(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                      double y, int button, uint32_t mods, sk_buf *out);
/* over: the pointer is over the grid. */
bool sk_term_mouse_move(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                        double y, bool over, uint32_t mods, sk_buf *out);
/* SK_EV_SCROLL: dy > 0 up, in lines, or points when precise. A program
 * that reads the wheel gets one report for each event that adds up to a
 * line; on the alternate screen without one it gets an up or down arrow
 * instead. false: the view scrolls. */
bool sk_term_mouse_wheel(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                         double y, double dy, bool precise, uint32_t mods, sk_buf *out);

/* ---- Focus and paste ---- */

/* The window's focus changed: CSI I in, CSI O out, when the program
 * asked (DECSET 1004). When it asks, the screen sends the focus as it is
 * at once, as xterm.js does. true when it wrote. */
bool sk_term_focus(const sk_term_input_modes *m, bool focused, sk_buf *out);

/* Pasted text: CR LF and LF become CR, and the whole is bracketed when
 * the program asked (DECSET 2004). Invalid UTF-8 becomes U+FFFD, as the
 * browser read the clipboard. Nothing else is filtered: xterm.js strips
 * no escape, not even an ESC [ 201 ~ inside the text. */
void sk_term_paste(const sk_term_input_modes *m, const char *text, size_t n, sk_buf *out);

#endif
