/* The terminal of one tab (phase 4 of decision 0036): what xterm.js
 * 6.0.0 with Unicode 11 widths did for the Electron app, from bytes in
 * to cells out. It follows xterm.js (MIT, third_party/xterm.js): its
 * parser, its handlers and the sequences they ignore, its buffers with
 * 5000 lines of scrollback and reflow, its selection.
 *
 * Threads: the core thread feeds bytes, the UI thread reads cells. Every
 * call takes the terminal's lock, which is recursive; the UI wraps the
 * reads of one frame in sk_term_lock and sk_term_unlock so they see one
 * state. The callbacks run on the feeding thread with the lock held.
 *
 * Coordinates: x is a column from 0. A row is a row of the viewport from
 * 0, the rows shown, which scroll back when the user scrolls; a line is
 * an index into the whole buffer, scrollback first. */
#ifndef SK_TERM_H
#define SK_TERM_H

#include "input.h"

#include <stddef.h>
#include <stdint.h>

#define SK_TERM_SCROLLBACK 5000

/* A cell's attributes are xterm.js's two words, fg and bg, and its
 * extended word when bg has SK_BG_HAS_EXTENDED (buffer/Constants.ts).
 * The colour in fg, bg or an underline colour: */
#define SK_ATTR_CM_MASK 0x3000000u
#define SK_ATTR_CM_DEFAULT 0u
#define SK_ATTR_CM_P16 0x1000000u
#define SK_ATTR_CM_P256 0x2000000u
#define SK_ATTR_CM_RGB 0x3000000u
#define SK_ATTR_PCOLOR_MASK 0xFFu
#define SK_ATTR_RGB_MASK 0xFFFFFFu
#define SK_FG_INVERSE 0x4000000u
#define SK_FG_BOLD 0x8000000u
#define SK_FG_UNDERLINE 0x10000000u
#define SK_FG_BLINK 0x20000000u
#define SK_FG_INVISIBLE 0x40000000u
#define SK_FG_STRIKETHROUGH 0x80000000u
#define SK_BG_ITALIC 0x4000000u
#define SK_BG_DIM 0x8000000u
#define SK_BG_HAS_EXTENDED 0x10000000u
#define SK_BG_PROTECTED 0x20000000u
#define SK_BG_OVERLINE 0x40000000u
/* The extended word: the underline colour in its low 26 bits, the style
 * above. After SGR 59 the colour bits are all set, as in xterm.js. */
#define SK_EXT_UNDERLINE_COLOR 0x3FFFFFFu
#define SK_EXT_UNDERLINE_STYLE 0x1C000000u
#define SK_EXT_UNDERLINE_SHIFT 26

typedef enum {
    SK_UNDERLINE_NONE,
    SK_UNDERLINE_SINGLE,
    SK_UNDERLINE_DOUBLE,
    SK_UNDERLINE_CURLY,
    SK_UNDERLINE_DOTTED,
    SK_UNDERLINE_DASHED,
} sk_underline;

typedef struct sk_term sk_term;

typedef struct {
    void *ud;
    /* What the terminal answers, for the pty: device attributes, status
     * and mode reports, colour reports, the focus report. */
    void (*reply)(void *ud, const char *data, size_t len);
    /* OSC 0 or 2, UTF-8. */
    void (*title)(void *ud, const char *title);
    void (*bell)(void *ud);
} sk_term_callbacks;

/* Colours as 0xRRGGBB. */
typedef struct {
    uint32_t ansi[256];
    uint32_t fg, bg, cursor;
} sk_term_palette;

/* xterm.js's defaults: its 16 colours, the 6x6x6 cube and the greys,
 * white on black. */
void sk_term_default_palette(sk_term_palette *out);

/* At least 2 columns and 1 row, as xterm.js. cb may be NULL. */
sk_term *sk_term_new(int cols, int rows, const sk_term_callbacks *cb);
void sk_term_free(sk_term *t);
void sk_term_lock(sk_term *t);
void sk_term_unlock(sk_term *t);

void sk_term_feed(sk_term *t, const char *data, size_t len);
void sk_term_resize(sk_term *t, int cols, int rows);
void sk_term_size(sk_term *t, int *cols, int *rows);

typedef struct {
    uint32_t cp;          /* 0 for an empty cell, a placeholder or a combined cell */
    const uint32_t *comb; /* a combined cell's codepoints, valid while locked */
    uint32_t ncomb;
    int width;            /* 2 for a wide character, 0 for the cell after it */
    uint32_t fg, bg, ext;
    uint32_t link;        /* an OSC 8 link, 0 for none */
} sk_term_cell;

/* Fills out[0..cols) from viewport row y. */
void sk_term_row(sk_term *t, int y, sk_term_cell *out);
/* Fills out[0..cols) from buffer line `line`, counted from the first of
 * the scrollback (viewport row y is line top + y, top as
 * sk_term_viewport gives it), sets *cols and whether the line is wrapped
 * on from the one before, as LinkComputer reads lines.get(i).isWrapped.
 * False past the lines, which a full ring wraps as lines.get does. */
bool sk_term_line(sk_term *t, int line, sk_term_cell *out, int *cols, bool *wrapped);
/* As xterm.js's getUnderlineStyle: none unless SK_FG_UNDERLINE, then the
 * extended style, dashed for a link. */
sk_underline sk_term_cell_underline(const sk_term_cell *c);

typedef enum { SK_CURSOR_BLOCK, SK_CURSOR_UNDERLINE, SK_CURSOR_BAR } sk_cursor_style;

typedef struct {
    /* x may be cols, or cols + 1, past the last column, as in xterm.js;
     * y is a viewport row, outside 0..rows-1 when scrolled away. */
    int x, y;
    bool hidden; /* DECTCEM */
    bool initialized; /* xterm.js draws no cursor before focus or a buffer switch */
    sk_cursor_style style;
    bool blink;
} sk_term_cursor;

void sk_term_cursor_of(sk_term *t, sk_term_cursor *out);
void sk_term_input_modes_of(sk_term *t, sk_term_input_modes *out);
/* DECSET 2026: the UI holds its drawing while set. xterm.js ends it after
 * 1 s of held drawing, which the UI does with sk_term_end_sync. */
bool sk_term_synchronized(sk_term *t);
void sk_term_end_sync(sk_term *t);
/* For the focus report DECSET 1004 asks for. */
void sk_term_set_focus(sk_term *t, bool focused);
/* An OSC 8 link's URI while any line holds it, valid while locked. */
const char *sk_term_link_uri(sk_term *t, uint32_t link);
/* The OSC 8 id= of a link, NULL for none. */
const char *sk_term_link_id(sk_term *t, uint32_t link);

/* The skin's theme: sets the colours and what OSC 104 and 110 to 112
 * restore. */
void sk_term_set_palette(sk_term *t, const sk_term_palette *p);
/* The colours now, after any OSC 4, 10, 11 or 12. */
void sk_term_palette_of(sk_term *t, sk_term_palette *out);

/* The viewport: its first line, the first line of the screen, the lines
 * in the buffer. Output keeps a viewport scrolled up where it is; the UI
 * scrolls to the bottom on user input, as xterm.js's scrollOnUserInput
 * does. */
void sk_term_viewport(sk_term *t, int *top, int *base, int *lines);
void sk_term_scroll(sk_term *t, int lines);
void sk_term_scroll_to_bottom(sk_term *t);

/* Selection, as xterm.js's mouse selection. col is a boundary between
 * columns, 0 to cols: the UI rounds a pointer to the nearer edge of its
 * cell, as xterm.js does. The UI clears the selection on user input. */
typedef enum { SK_SELECT_CHAR, SK_SELECT_WORD, SK_SELECT_LINE, SK_SELECT_COLUMN } sk_select_kind;

/* A press: one click, a double click (word), a triple click (line), or
 * an Alt click (column). */
void sk_term_select_start(sk_term *t, sk_select_kind kind, int col, int row);
/* The pointer dragged to col, row. */
void sk_term_select_move(sk_term *t, int col, int row);
/* A Shift click, ending the selection there. */
void sk_term_select_extend(sk_term *t, int col, int row);
/* A drag held past the top or bottom: scrolls by amount rows and takes
 * the selection to the viewport's edge. */
void sk_term_select_drag_scroll(sk_term *t, int amount);
void sk_term_select_all(sk_term *t);
void sk_term_select_clear(sk_term *t);
bool sk_term_has_selection(sk_term *t);
/* The selection in lines and columns, end exclusive. */
bool sk_term_selection_range(sk_term *t, int *x0, int *line0, int *x1, int *line1, bool *column);
bool sk_term_selected(sk_term *t, int x, int row);
/* The selected text as getSelection gives it, malloc'd UTF-8. */
char *sk_term_selection_text(sk_term *t);

/* The screen's rows joined by \n, blank rows at either end dropped, as
 * renderer.js's screenText; malloc'd. */
char *sk_term_screen_text(sk_term *t);
/* A line of the buffer, trailing blanks trimmed; malloc'd. */
char *sk_term_line_text(sk_term *t, int line);

/* Which viewport rows to redraw since the last call: those whose cells
 * changed or now show another line, and those the cursor left and
 * reached. rows[0..rows) is set to true for each; returns whether any. */
bool sk_term_take_dirty(sk_term *t, bool *rows);

#endif
