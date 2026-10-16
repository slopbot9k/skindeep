/* A terminal drawn (phase 4 of decision 0036): an sk_term's viewport
 * into an image, as xterm.js 6.0.0's WebGL renderer drew it for the
 * Electron app: its cell size from the font, its colours, bold in the
 * bright colours, dim at half, its underline styles, its cursors, the
 * selection, and its own box-drawing and block glyphs.
 *
 * A view belongs to the UI thread. It reads the terminal under the
 * terminal's lock, taken by the caller for the frame. */
#ifndef SK_UI_TERMVIEW_H
#define SK_UI_TERMVIEW_H

#include "../term/term.h"
#include "../text/text.h"

typedef struct sk_termview sk_termview;

/* What a view draws with. The font set and cache outlive the view. */
typedef struct {
    sk_fontset *fonts;     /* the terminal's stack, mono */
    sk_glyph_cache *cache;
    float font_px;         /* the font size in CSS pixels: 14 */
    double scale;          /* device pixels per CSS pixel */
    /* Over the terminal's palette (sk_term_palette_of): the selection's
     * background, and its foreground when not 0 (0xAARRGGBB). */
    sk_color selection_bg, selection_inactive_bg, selection_fg;
} sk_termview_style;

/* xterm.js's selection colours for its default theme. */
void sk_termview_default_style(sk_termview_style *out);

sk_termview *sk_termview_new(const sk_termview_style *s);
void sk_termview_free(sk_termview *v);
/* A new font size or scale: the cell size changes, and the next draw is
 * whole. */
void sk_termview_set_style(sk_termview *v, const sk_termview_style *s);

/* A cell in device pixels, as xterm.js's render dimensions give it. */
void sk_termview_cell(const sk_termview *v, int *w, int *h);
/* The columns and rows that fit w by h device pixels, as FitAddon's
 * proposeDimensions: at least 2 columns and 1 row. */
void sk_termview_fit(const sk_termview *v, int w, int h, int *cols, int *rows);

/* The cell under (x, y) device pixels from the view's top-left, clamped
 * to the grid: *col is the column, *edge the column boundary nearer the
 * point, as selection wants it. */
void sk_termview_hit(const sk_termview *v, const sk_term *t, int x, int y, int *col, int *row, int *edge);

/* What a draw shows besides the cells. */
typedef struct {
    bool focused;      /* else the cursor is an outline and the selection inactive */
    bool cursor_on;    /* blink phase: false hides a blinking cursor */
    uint32_t link;     /* an OSC 8 link under the pointer: its cells underlined, 0 for none */
    /* A detected (not OSC 8) link under the pointer: underlined, in
     * viewport rows and columns, end exclusive; row0 < 0 for none. */
    int link_row0, link_col0, link_row1, link_col1;
} sk_termview_state;

/* Draws t's viewport into dst at its origin (a view of the window's
 * frame), the rows the terminal marks dirty, or every row when all is
 * true or the size, style or state changed since the last draw. The
 * caller holds the terminal's lock. Returns the rectangle drawn, in
 * dst's pixels, empty when nothing was. */
sk_rect sk_termview_draw(sk_termview *v, sk_term *t, sk_image *dst, const sk_termview_state *st, bool all);

/* xterm.js's custom glyphs (box drawing, blocks, shades, Powerline
 * arrows): true when cp is one, drawn into cell r of dst in c. */
bool sk_termview_custom_glyph(sk_image *dst, sk_rect r, uint32_t cp, sk_color c, double scale);

#endif
