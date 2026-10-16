/* What the terminal's modes say about input (phase 4 of decision 0036):
 * the state the screen keeps (term.h) and the key, mouse, focus and
 * paste encoders (keys.h) read, so neither needs the other's insides.
 * The modes and their effects are xterm.js 6.0.0's, the terminal the
 * Electron app used. */
#ifndef SK_TERM_INPUT_H
#define SK_TERM_INPUT_H

#include <stdbool.h>

/* DECSET 9, 1000, 1002 and 1003. */
typedef enum {
    SK_MOUSE_NONE,
    SK_MOUSE_X10,
    SK_MOUSE_VT200,
    SK_MOUSE_DRAG,
    SK_MOUSE_ANY,
} sk_mouse_protocol;

/* The default encoding, DECSET 1006 and 1016. */
typedef enum {
    SK_MOUSE_ENC_DEFAULT,
    SK_MOUSE_ENC_SGR,
    SK_MOUSE_ENC_SGR_PIXELS,
} sk_mouse_encoding;

typedef struct {
    bool app_cursor;      /* DECCKM: ESC O A for an arrow, not ESC [ A */
    bool app_keypad;      /* DECKPAM */
    bool bracketed_paste; /* DECSET 2004 */
    bool focus_events;    /* DECSET 1004 */
    bool alt_screen;      /* the alternate buffer is shown */
    sk_mouse_protocol mouse;
    sk_mouse_encoding mouse_encoding;
} sk_term_input_modes;

#endif
