/* A one-line text field as Chromium edits an <input type="text">: its
 * text, the caret and the selection's other end, byte offsets at
 * characters. The chooser's fields (decision 0025) and a tab's name
 * being renamed (decision 0032) are these. The clipboard is the
 * caller's: a key that copies, cuts or pastes says so, and so does a
 * selection made, which X11 keeps as PRIMARY. UI thread only. */
#ifndef SK_APP_FIELD_H
#define SK_APP_FIELD_H

#include "../base/str.h"
#include "../platform/ui.h"

/* The modifier of word moves, and of the clipboard's and select all's
 * chords, on this system. */
#ifdef __APPLE__
#define SK_FIELD_WORD_MOD SK_MOD_ALT
#define SK_FIELD_CMD_MOD SK_MOD_SUPER
#else
#define SK_FIELD_WORD_MOD SK_MOD_CTRL
#define SK_FIELD_CMD_MOD SK_MOD_CTRL
#endif

typedef struct {
    sk_buf text;
    size_t caret, anchor;
    double scroll;    /* points the text is scrolled left, the drawer's */
    size_t max_chars; /* maxLength, in characters; 0: none */
} sk_field;

void sk_field_init(sk_field *f);
void sk_field_free(sk_field *f);
/* As input.value = s: the caret after it. */
void sk_field_set(sk_field *f, const char *s);
/* As input.select(). */
void sk_field_select_all(sk_field *f);

size_t sk_field_sel_lo(const sk_field *f);
size_t sk_field_sel_hi(const sk_field *f);
bool sk_field_has_sel(const sk_field *f);
/* The selection, malloc'd. */
char *sk_field_selection(const sk_field *f);

/* [lo, hi) replaced with n bytes of s, the caret after them. */
void sk_field_replace(sk_field *f, size_t lo, size_t hi, const char *s, size_t n);
/* Typed or pasted text in place of the selection. A text input's value
 * holds no line break, so pasted ones are dropped, as Chromium drops
 * them; invalid UTF-8 becomes U+FFFD; past max_chars it is cut. */
void sk_field_insert(sk_field *f, const char *s);

/* The character before and after i, and the words around it, as
 * Alt+Left and Alt+Right move on macOS and Ctrl on Linux; a double
 * click at i selects [*lo, *hi): its word, or the run of others. */
size_t sk_field_prev(const sk_buf *t, size_t i);
size_t sk_field_next(const sk_buf *t, size_t i);
size_t sk_field_word_left(const sk_buf *t, size_t i);
size_t sk_field_word_right(const sk_buf *t, size_t i);
void sk_field_word_around(const sk_buf *t, size_t i, size_t *lo, size_t *hi);

/* What a key did besides editing. */
typedef struct {
    bool took;     /* it was the field's key */
    char *copy;    /* malloc'd text for the clipboard, or NULL */
    bool paste;    /* the clipboard is to be inserted, when it comes */
    bool selected; /* a selection was made: X11's PRIMARY takes it */
} sk_field_key_result;

/* A key in the field, as Chromium edits one: moves, word moves,
 * deletes, select all, cut, copy and paste, Cocoa's Ctrl+A and Ctrl+E,
 * and typed text. Enter, Escape and Tab are the caller's. */
sk_field_key_result sk_field_key(sk_field *f, const sk_key_event *k);

#endif
