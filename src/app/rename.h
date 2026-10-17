/* A tab's drop-down and its name edited in place (decision 0032), as
 * app/renderer/tabs.js has them and app.css draws #tab-menu and
 * #tabs .rename. A right click on a tab opens the drop-down at the
 * pointer, Rename alone; Rename puts a text field in the tab's place
 * holding its name, all of it selected; Enter commits, Escape cancels,
 * and what blurs the input commits: Tab, a press anywhere else, the
 * window losing the focus (the window's to say). The window places the
 * field and
 * sends the name; the clipboard is the window's, as field.h says.
 *
 * Points from the window's top-left, drawn at the window's scale. UI
 * thread only. */
#ifndef SK_APP_RENAME_H
#define SK_APP_RENAME_H

#include "field.h"
#include "menu.h"

typedef struct sk_rename sk_rename;

sk_rename *sk_rename_new(void);
void sk_rename_free(sk_rename *r);

/* ---- The drop-down ---- */

/* Opened for tab id at x, y, kept 120 points from the window's right
 * edge as tabs.js keeps it. */
void sk_rename_popup(sk_rename *r, int id, double x, double y, double win_w);
bool sk_rename_popup_open(const sk_rename *r);
void sk_rename_popup_close(sk_rename *r);
/* A press while it is open: false when it was not on the drop-down,
 * which then closed and the press goes on; true when it was, with
 * *rename true for a left press on Rename (the drop-down closed, the
 * caller starts the edit of *id). */
bool sk_rename_popup_press(sk_rename *r, double x, double y, int button, bool *rename, int *id);
/* The pointer at x, y; true when the drawing changed. */
bool sk_rename_popup_hover(sk_rename *r, double x, double y);
/* The rect it covers in pixels at scale, shadow included; empty when
 * closed. */
sk_rect sk_rename_popup_bounds(const sk_rename *r, double scale);
void sk_rename_popup_draw(sk_rename *r, sk_image *dst, const sk_menu_style *st);

/* ---- The field ---- */

/* Tab id's name being edited, starting as name, all of it selected. */
void sk_rename_edit(sk_rename *r, int id, const char *name);
bool sk_rename_editing(const sk_rename *r);
int sk_rename_tab(const sk_rename *r);
/* The text now, which the caller trims and sends on a commit. */
const char *sk_rename_text(const sk_rename *r);
/* The edit ended, committed or not. */
void sk_rename_end(sk_rename *r);
sk_field *sk_rename_field(sk_rename *r);

typedef enum {
    SK_RENAME_GOES_ON, /* still editing */
    SK_RENAME_COMMIT,  /* Enter, or a press elsewhere: send the text */
    SK_RENAME_CANCEL,  /* Escape */
} sk_rename_how;

/* A key while editing: Enter and Escape end it, the rest edit the
 * field, its clipboard's part in *clip (copy freed by the caller). */
sk_rename_how sk_rename_key(sk_rename *r, const sk_key_event *k, sk_field_key_result *clip);
/* The field's box, x, y, w and h in points, as the window last placed
 * it. A press outside it commits; inside, it places the caret (a double
 * click selects a word, a triple all) and a drag selects; *selected
 * says a selection was made, for X11's PRIMARY. On Linux a middle
 * click places the caret and asks for PRIMARY to be pasted there. */
void sk_rename_place(sk_rename *r, double x, double y, double w, double h);
sk_rename_how sk_rename_mouse(sk_rename *r, const sk_event *ev, const sk_menu_style *st, bool *selected,
                              bool *paste_primary);
/* Draws the field in its box: #000, a 1-point #6fa8dc border with
 * radius 2, 12-point DejaVu Sans Mono in #fff, the selection and the
 * caret, scrolled to keep the caret in view. */
void sk_rename_draw(sk_rename *r, sk_image *dst, const sk_menu_style *st);
/* The caret's box in points, for an input method's candidates. */
void sk_rename_caret(const sk_rename *r, const sk_menu_style *st, int *x, int *y, int *w, int *h);

#endif
