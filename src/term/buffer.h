/* The terminal's lines and buffers: xterm.js 6.0.0's BufferLine,
 * CircularList, Buffer and BufferReflow (src/common/buffer/), ported
 * from xterm.js (MIT, third_party/xterm.js). A cell keeps xterm.js's
 * three words, so the attribute bits are xterm.js's own (term.h names
 * them). */
#ifndef SK_TERM_BUFFER_H
#define SK_TERM_BUFFER_H

#include "../base/str.h"

/* content: the codepoint (21 bits), the combined flag, the width. */
#define SK_CELL_CP_MASK 0x1FFFFFu
#define SK_CELL_COMBINED 0x200000u
#define SK_CELL_HAS_CONTENT 0x3FFFFFu
#define SK_CELL_WIDTH_SHIFT 22
#define SK_CELL_WIDTH_MASK 0xC00000u
/* An empty cell: no codepoint, width 1. */
#define SK_CELL_NULL (1u << SK_CELL_WIDTH_SHIFT)

typedef struct {
    uint32_t content, fg, bg;
} sk_cell;

/* xterm.js's ExtendedAttrs: the underline colour, style and variant in
 * ext, and the OSC 8 link id. */
typedef struct {
    uint32_t ext, url;
} sk_cell_ext;

/* What a cell is set from: xterm.js's CellData, or an attribute with a
 * codepoint. comb, when set, is a combined cell's text: comb[0] the
 * count, then the codepoints. */
typedef struct {
    uint32_t content, fg, bg;
    sk_cell_ext x;
    const uint32_t *comb;
} sk_cell_data;

typedef struct sk_line {
    sk_cell *cells;
    sk_cell_ext *ext; /* NULL, or valid where bg has SK_BG_HAS_EXTENDED */
    uint32_t **comb;  /* NULL, or set exactly where content is combined */
    int len, cap;
    bool wrapped;
    /* Changes with every change to the line, unique across lines, so a
     * reader can tell what to redraw. */
    uint64_t gen;
} sk_line;

sk_line *sk_line_new(int cols, const sk_cell_data *fill, bool wrapped);
sk_line *sk_line_clone(const sk_line *l);
void sk_line_free(sk_line *l);
void sk_line_touch(sk_line *l);

static inline int sk_line_width(const sk_line *l, int i)
{
    return (int)(l->cells[i].content >> SK_CELL_WIDTH_SHIFT);
}

static inline uint32_t sk_line_has_content(const sk_line *l, int i)
{
    return l->cells[i].content & SK_CELL_HAS_CONTENT;
}

/* The cell's codepoints, as getString: none for an empty cell. Returns
 * the count and points *cp at them; one is copied to *one. */
int sk_line_chars(const sk_line *l, int i, uint32_t *one, const uint32_t **cp);
/* The cell's codepoint, or a combined cell's last. */
uint32_t sk_line_codepoint(const sk_line *l, int i);
void sk_line_load(const sk_line *l, int i, sk_cell_data *out);
void sk_line_set(sk_line *l, int i, const sk_cell_data *c);
/* setCellFromCodepoint: the attribute's fg, bg and extended. */
void sk_line_set_cp(sk_line *l, int i, uint32_t cp, int width, const sk_cell_data *attr);
void sk_line_add_cp(sk_line *l, int i, uint32_t cp, int width);
void sk_line_insert_cells(sk_line *l, int pos, int n, const sk_cell_data *fill);
void sk_line_delete_cells(sk_line *l, int pos, int n, const sk_cell_data *fill);
void sk_line_replace_cells(sk_line *l, int start, int end, const sk_cell_data *fill, bool protect);
void sk_line_fill(sk_line *l, const sk_cell_data *fill, bool protect);
void sk_line_resize(sk_line *l, int cols, const sk_cell_data *fill);
void sk_line_copy_from(sk_line *l, const sk_line *src);
void sk_line_copy_cells(sk_line *dst, const sk_line *src, int src_col, int dst_col, int n, bool reverse);
int sk_line_trimmed_length(const sk_line *l);
/* translateToString as UTF-8: an empty cell is a space, a wide character
 * advances two columns. */
void sk_line_text(const sk_line *l, sk_buf *out, bool trim_right, int start, int end);

/* A buffer event the link markers follow, as CircularList fires them. */
typedef enum { SK_LINES_TRIM, SK_LINES_INSERT, SK_LINES_DELETE } sk_lines_event;

typedef struct {
    void (*fn)(void *ud, int buffer, sk_lines_event ev, int index, int amount);
    void *ud;
} sk_lines_listener;

/* The ring of lines: xterm.js's CircularList. A line dropped from it is
 * freed. */
typedef struct {
    sk_line **v;
    int max, start, len;
    int id; /* 0 the normal buffer, 1 the alternate */
    const sk_lines_listener *listen;
} sk_lines;

static inline sk_line *sk_lines_get(const sk_lines *r, int i)
{
    return r->v[(r->start + i) % r->max];
}

typedef struct {
    sk_lines lines;
    int ydisp, ybase, y, x;
    int scroll_top, scroll_bottom;
    bool *tabs;
    int tabs_cap;
    int saved_x, saved_y;
    uint32_t saved_fg, saved_bg;
    const uint32_t *saved_charset;
    bool has_scrollback;
    int scrollback;
    int cols, rows;
} sk_buffer;

void sk_buffer_init(sk_buffer *b, int id, bool scrollback_on, int scrollback, int cols, int rows,
                    const sk_lines_listener *listen);
void sk_buffer_free(sk_buffer *b);
/* Fills the viewport with blank lines when the buffer is empty. */
void sk_buffer_fill_viewport(sk_buffer *b, const sk_cell_data *fill);
/* Empties the buffer, as the alternate buffer is when it is left. */
void sk_buffer_clear(sk_buffer *b);
/* Resizes, reflowing the normal buffer's wrapped lines. */
void sk_buffer_resize(sk_buffer *b, int cols, int rows);
void sk_buffer_setup_tabs(sk_buffer *b, int from);
bool sk_buffer_tab(const sk_buffer *b, int x);
void sk_buffer_set_tab(sk_buffer *b, int x, bool on);
int sk_buffer_next_stop(const sk_buffer *b, int x);
int sk_buffer_prev_stop(const sk_buffer *b, int x);
sk_line *sk_buffer_blank(const sk_buffer *b, const sk_cell_data *fill, bool wrapped);

/* The ring operations the screen uses, with xterm.js's events. */
void sk_lines_set(sk_lines *r, int i, sk_line *l);
void sk_lines_push(sk_lines *r, sk_line *l);
void sk_lines_insert(sk_lines *r, int i, sk_line *l);
void sk_lines_remove(sk_lines *r, int i);
void sk_lines_trim_start(sk_lines *r, int n);
/* Moves the lines [i + 1, i + n) up one, freeing line i; line i + n - 1
 * is then the caller's to set. No event, as xterm.js fires none. */
void sk_lines_shift_up(sk_lines *r, int i, int n);
/* Moves the lines [i, i + n) down one, freeing line i + n; line i is then
 * the caller's to set. */
void sk_lines_shift_down(sk_lines *r, int i, int n);

#endif
