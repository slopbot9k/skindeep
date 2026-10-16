/* New tab... (decision 0025), the dialog: app/renderer/chooser.js with
 * the #chooser rules of app/renderer/app.css, drawn by hand. The agent
 * as radios, its profile in a select with Other profile... and Forget,
 * the variable the tab runs with, the folder with Choose..., the command
 * line, what the window's options do not carry, the hint, the error, and
 * Cancel and Open. Enter opens, Escape closes.
 *
 * Its controls work as Chromium's do: text fields that edit, with a
 * caret, a selection by drag, double click or Shift, word moves on Alt
 * (macOS) or Ctrl (Linux), the clipboard and an input method's text; a
 * select whose list opens and picks with the pointer or the keys; Tab
 * and Shift+Tab through the controls; hover and disabled states.
 *
 * Everything here runs on the UI thread. What needs the tabs goes to
 * the core thread as an action: the window's handler acts on it and
 * answers (sk_newtab_open, then sk_chooser_fail or sk_chooser_close;
 * sk_newtab_forget and sk_newtab_offer, then sk_chooser_forgot). The
 * folder picker and the clipboard it runs itself, through the config's
 * hooks; their answers come later, and config.changed says the dialog
 * needs drawing again. */
#ifndef SK_APP_CHOOSER_H
#define SK_APP_CHOOSER_H

#include "../base/json.h"
#include "../platform/picker.h"
#include "../platform/ui.h"
#include "../text/text.h"
#include "field.h"
#include "newtab.h"

/* The modifier of word moves, and of the clipboard's and select all's
 * chords, on this system: field.c's. */
#define SK_CHOOSER_WORD_MOD SK_FIELD_WORD_MOD
#define SK_CHOOSER_CMD_MOD SK_FIELD_CMD_MOD

typedef struct {
    /* The window it is drawn in, the picker's parent, and the core
     * thread's loop, which runs Linux's picker child. */
    sk_window *win;
    sk_loop *loop;
    /* An answer that came later, from the picker or the clipboard,
     * changed the dialog. Optional. */
    void (*changed)(void *ud);
    void *ud;
    /* NULL: the platform's (platform/picker.h, platform/ui.h). Tests
     * give their own. */
    void (*pick_folder)(sk_window *parent, sk_loop *loop, const sk_picker_opts *o, sk_picker_done done, void *ud);
    void (*clipboard_set)(const char *utf8);
    void (*primary_set)(const char *utf8);
    void (*clipboard_get)(bool primary, void (*done)(void *ud, const char *utf8), void *ud);
} sk_chooser_config;

typedef struct {
    sk_fontset *sans, *mono; /* DejaVu Sans and DejaVu Sans Mono first */
    sk_glyph_cache *cache;
    double scale;            /* the window's */
} sk_chooser_style;

typedef enum {
    SK_CHOOSER_NONE,
    /* Open sk_chooser_pick's pick: sk_newtab_open on the core thread,
     * then sk_chooser_close, or sk_chooser_fail with its error. */
    SK_CHOOSER_OPEN,
    /* Forget sk_chooser_pick's profile of its agent: sk_newtab_forget
     * and sk_newtab_offer(..., false, ...), then sk_chooser_forgot. */
    SK_CHOOSER_FORGET,
    /* Cancel or Escape closed it. */
    SK_CHOOSER_CLOSED,
} sk_chooser_act;

typedef struct {
    sk_chooser_act act;
    bool redraw; /* the dialog changed: draw it again */
} sk_chooser_result;

typedef struct sk_chooser sk_chooser;

/* The config is copied; its pointers outlive the dialog. */
sk_chooser *sk_chooser_new(const sk_chooser_config *cfg);
void sk_chooser_free(sk_chooser *c);

/* Opens on st, whose contents it takes, leaving it empty: its agent and
 * that agent's profile chosen, its folder, the agent's command line,
 * which has the focus. An open dialog stays as it is, and st is freed. */
void sk_chooser_open(sk_chooser *c, sk_newtab_state *st);
void sk_chooser_close(sk_chooser *c);
bool sk_chooser_is_open(const sk_chooser *c);

/* What SK_CHOOSER_OPEN and SK_CHOOSER_FORGET act on, into out, which
 * the caller frees (sk_newtab_pick_free). */
void sk_chooser_pick(const sk_chooser *c, sk_newtab_pick *out);
/* The pick was refused: err shown, the dialog open. */
void sk_chooser_fail(sk_chooser *c, const char *err);
/* After Forget: st's offer, whose contents it takes, with the command
 * line and the folder kept as typed. */
void sk_chooser_forgot(sk_chooser *c, sk_newtab_state *st);

/* Draws the dialog into area of dst, the #chooser box: the window below
 * the identity strip, in pixels. It darkens what is there, so the caller
 * draws the window under it first, each time. Mouse events are taken in
 * the points of the window's content, the area's / style->scale. The
 * style's pointers outlive the dialog. */
void sk_chooser_draw(sk_chooser *c, sk_image *dst, sk_rect area, const sk_chooser_style *style);

/* The user's input while the dialog is open. mouse takes
 * SK_EV_MOUSE_DOWN, _UP, _MOVE and _LEAVE, and SK_EV_SCROLL; text an
 * input method's SK_EV_TEXT; preedit its SK_EV_PREEDIT. Nothing is hit
 * before the first draw. */
sk_chooser_result sk_chooser_mouse(sk_chooser *c, const sk_event *ev);
sk_chooser_result sk_chooser_key(sk_chooser *c, const sk_key_event *k);
sk_chooser_result sk_chooser_text(sk_chooser *c, const char *utf8);
sk_chooser_result sk_chooser_preedit(sk_chooser *c, const char *utf8);

/* The pointer's shape where it last moved. */
sk_cursor sk_chooser_cursor(const sk_chooser *c);
/* The caret of the field with the focus, in points, for an input
 * method's candidates; false when no field has it. */
bool sk_chooser_ime_rect(const sk_chooser *c, int *x, int *y, int *w, int *h);

/* For the live checks: { open, agent, profile, profiles, env, forget,
 * folder, line, dropped, error, focus, list }. */
void sk_chooser_json(const sk_chooser *c, sk_jw *w);
/* For the live checks and the tests: where the last draw put name, in
 * points: x, y, w and h. name is "dialog", "agent0", "radio0" (and 1),
 * "profile", "other", "forget", "env", "folder", "choose", "line",
 * "dropped", "hint", "error", "cancel", "open" or "list". False for one
 * not shown. */
bool sk_chooser_box(const sk_chooser *c, const char *name, double out[4]);

#endif
