/* Selection and text: xterm.js 6.0.0's SelectionService and
 * SelectionModel (src/browser/), ported from xterm.js (MIT, third_party/
 * xterm.js), and the app's screenText. Word selection walks the line as
 * a JavaScript string, in UTF-16 units, as xterm.js does, so a wide
 * character, a combined cell or one past U+FFFF moves the same. */
#include "screen.h"

#include "../app/js.h"
#include "../base/utf8.h"

#include <string.h>

/* xterm.js's default wordSeparator. */
static const char separators[] = " ()[]{}',\"`";

typedef SK_VEC(uint16_t) u16_vec;

static void dirty(sk_term *t)
{
    t->all_dirty = true;
}

void sk_select_reset(sk_term *t)
{
    sk_selection *s = &t->sel;
    s->has_start = s->has_end = false;
    s->all = false;
    s->start_len = 0;
    dirty(t);
}

void sk_select_trim(sk_term *t, int amount)
{
    sk_selection *s = &t->sel;
    if (s->has_start)
        s->sy -= amount;
    if (s->has_end)
        s->ey -= amount;
    if (s->has_end && s->ey < 0) {
        sk_select_reset(t);
        return;
    }
    if (s->has_start && s->sy < 0)
        s->sy = 0;
}

static bool reversed(const sk_selection *s)
{
    if (!s->has_start || !s->has_end)
        return false;
    return s->sy > s->ey || (s->sy == s->ey && s->sx > s->ex);
}

static bool final_start(sk_term *t, int *x, int *y)
{
    const sk_selection *s = &t->sel;
    if (s->all) {
        *x = *y = 0;
        return true;
    }
    if (!s->has_start)
        return false;
    if (s->has_end && reversed(s)) {
        *x = s->ex;
        *y = s->ey;
    } else {
        *x = s->sx;
        *y = s->sy;
    }
    return true;
}

static bool final_end(sk_term *t, int *x, int *y)
{
    const sk_selection *s = &t->sel;
    int cols = t->cols;
    if (s->all) {
        *x = cols;
        *y = t->active->ybase + t->rows - 1;
        return true;
    }
    if (!s->has_start)
        return false;
    if (!s->has_end || reversed(s)) {
        int spl = s->sx + s->start_len;
        if (spl > cols) {
            if (spl % cols == 0) {
                *x = cols;
                *y = s->sy + spl / cols - 1;
            } else {
                *x = spl % cols;
                *y = s->sy + spl / cols;
            }
        } else {
            *x = spl;
            *y = s->sy;
        }
        return true;
    }
    if (s->start_len && s->ey == s->sy) {
        int spl = s->sx + s->start_len;
        if (spl > cols) {
            *x = spl % cols;
            *y = s->sy + spl / cols;
        } else {
            *x = SK_MAX(spl, s->ex);
            *y = s->ey;
        }
        return true;
    }
    *x = s->ex;
    *y = s->ey;
    return true;
}

static sk_line *line_at(sk_term *t, int y)
{
    return sk_screen_line_at(t->active, y);
}

/* Cells as JavaScript sees them past the line: no width, no text. */
static int width_of(const sk_line *l, int i)
{
    return i >= 0 && i < l->len ? sk_line_width(l, i) : 0;
}

static int units_of(const sk_line *l, int i)
{
    if (i < 0 || i >= l->len)
        return 0;
    uint32_t one;
    const uint32_t *cp;
    int n = sk_line_chars(l, i, &one, &cp), u = 0;
    for (int k = 0; k < n; k++)
        u += cp[k] > 0xFFFF ? 2 : 1;
    return u;
}

static bool is_separator(const sk_line *l, int i)
{
    if (width_of(l, i) == 0)
        return false;
    uint32_t one;
    const uint32_t *cp;
    int n = sk_line_chars(l, i, &one, &cp);
    if (n == 0)
        return true; /* indexOf('') is 0 */
    if (n != 1 || cp[0] > 0x7F)
        return false;
    return strchr(separators, (int)cp[0]) != NULL;
}

static uint32_t codepoint_of(const sk_line *l, int i)
{
    return i >= 0 && i < l->len ? sk_line_codepoint(l, i) : 0;
}

static void utf16_push(u16_vec *v, uint32_t c)
{
    if (c > 0xFFFF) {
        c -= 0x10000;
        sk_vec_push(v, (uint16_t)(0xD800 + (c >> 10)));
        sk_vec_push(v, (uint16_t)(0xDC00 + (c & 0x3FF)));
    } else {
        sk_vec_push(v, (uint16_t)c);
    }
}

/* translateToString(false) in UTF-16 units. */
static void line_utf16(const sk_line *l, u16_vec *out)
{
    out->len = 0;
    for (int x = 0; x < l->len;) {
        uint32_t one;
        const uint32_t *cp;
        int n = sk_line_chars(l, x, &one, &cp);
        if (!n)
            sk_vec_push(out, (uint16_t)' ');
        for (int k = 0; k < n; k++)
            utf16_push(out, cp[k]);
        int w = sk_line_width(l, x);
        x += w ? w : 1;
    }
}

static uint16_t char_at(const u16_vec *s, int i)
{
    return i >= 0 && (size_t)i < s->len ? s->ptr[i] : 0;
}

static int col_to_index(const sk_line *l, int x)
{
    int index = x;
    for (int i = 0; x >= i; i++) {
        int len = units_of(l, i);
        if (width_of(l, i) == 0)
            index--;
        else if (len > 1 && x != i)
            index += len - 1;
    }
    return index;
}

/* _getWordAt: the word's start column and length, which may reach into
 * the wrapped lines above and below. */
static bool word_at(sk_term *t, int cx, int cy, bool allow_space, bool above, bool below, int *out_start,
                    int *out_len)
{
    int cols = t->cols;
    if (cx >= cols)
        return false;
    sk_line *l = line_at(t, cy);
    if (!l)
        return false;
    u16_vec s = {0};
    line_utf16(l, &s);
    int si = col_to_index(l, cx), ei = si;
    int offset = cx - si;
    int left_wide = 0, right_wide = 0, left_long = 0, right_long = 0;
    if (char_at(&s, si) == ' ') {
        while (si > 0 && char_at(&s, si - 1) == ' ')
            si--;
        while ((size_t)ei < s.len && char_at(&s, ei + 1) == ' ')
            ei++;
    } else {
        int sc = cx, ec = cx;
        if (width_of(l, sc) == 0) {
            left_wide++;
            sc--;
        }
        if (width_of(l, ec) == 2) {
            right_wide++;
            ec++;
        }
        int len = units_of(l, ec);
        if (len > 1) {
            right_long += len - 1;
            ei += len - 1;
        }
        while (sc > 0 && si > 0 && !is_separator(l, sc - 1)) {
            len = units_of(l, sc - 1);
            if (width_of(l, sc - 1) == 0) {
                left_wide++;
                sc--;
            } else if (len > 1) {
                left_long += len - 1;
                si -= len - 1;
            }
            si--;
            sc--;
        }
        while (ec < l->len && (size_t)ei + 1 < s.len && !is_separator(l, ec + 1)) {
            len = units_of(l, ec + 1);
            if (width_of(l, ec + 1) == 2) {
                right_wide++;
                ec++;
            } else if (len > 1) {
                right_long += len - 1;
                ei += len - 1;
            }
            ei++;
            ec++;
        }
    }
    ei++;
    int start = si + offset - left_wide + left_long;
    int length = SK_MIN(cols, ei - si + left_wide + right_wide - left_long - right_long);
    if (!allow_space) {
        bool blank = true;
        for (int i = SK_MAX(si, 0); i < ei && (size_t)i < s.len; i++)
            if (!sk_js_space(s.ptr[i]))
                blank = false;
        if (blank) {
            sk_vec_free(&s);
            return false;
        }
    }
    sk_vec_free(&s);
    if (above && start == 0 && codepoint_of(l, 0) != ' ') {
        sk_line *prev = line_at(t, cy - 1);
        int ps, pl;
        if (prev && l->wrapped && codepoint_of(prev, cols - 1) != ' ' &&
            word_at(t, cols - 1, cy - 1, false, true, false, &ps, &pl)) {
            int off = cols - ps;
            start -= off;
            length += off;
        }
    }
    if (below && start + length == cols && codepoint_of(l, cols - 1) != ' ') {
        sk_line *next = line_at(t, cy + 1);
        int ns, nl;
        if (next && next->wrapped && codepoint_of(next, 0) != ' ' && word_at(t, 0, cy + 1, false, false, true, &ns, &nl))
            length += nl;
    }
    *out_start = start;
    *out_len = length;
    return true;
}

static void select_word(sk_term *t, int cx, int cy)
{
    int start, len;
    if (!word_at(t, cx, cy, true, true, true, &start, &len))
        return;
    while (start < 0) {
        start += t->cols;
        cy--;
    }
    t->sel.sx = start;
    t->sel.sy = cy;
    t->sel.has_start = true;
    t->sel.start_len = len;
}

static void select_to_word(sk_term *t, int cx, int cy)
{
    sk_selection *s = &t->sel;
    int start, len;
    if (!word_at(t, cx, cy, true, true, true, &start, &len))
        return;
    int row = cy;
    while (start < 0) {
        start += t->cols;
        row--;
    }
    if (!reversed(s)) {
        while (start + len > t->cols) {
            len -= t->cols;
            row++;
        }
    }
    s->ex = reversed(s) ? start : start + len;
    s->ey = row;
    s->has_end = true;
}

static void select_line(sk_term *t, int y)
{
    sk_buffer *b = t->active;
    int first = y, last = y;
    while (first > 0 && first < b->lines.len && sk_screen_line(b, first)->wrapped)
        first--;
    while (last + 1 < b->lines.len && sk_screen_line(b, last + 1)->wrapped)
        last++;
    t->sel.sx = 0;
    t->sel.sy = first;
    t->sel.has_start = true;
    t->sel.has_end = false;
    t->sel.start_len = t->cols * (last - first) + t->cols;
}

/* A pointer at a viewport row, as a buffer position. */
static void at(sk_term *t, int col, int row, int *x, int *y)
{
    *x = SK_MIN(SK_MAX(col, 0), t->cols);
    *y = SK_MIN(SK_MAX(row, 0), t->rows - 1) + t->active->ydisp;
}

void sk_term_select_start(sk_term *t, sk_select_kind kind, int col, int row)
{
    sk_term_lock(t);
    sk_selection *s = &t->sel;
    int x, y;
    at(t, col, row, &x, &y);
    switch (kind) {
    case SK_SELECT_CHAR:
    case SK_SELECT_COLUMN: {
        s->start_len = 0;
        s->all = false;
        s->mode = kind == SK_SELECT_COLUMN ? SK_SEL_COLUMN : SK_SEL_NORMAL;
        s->sx = x;
        s->sy = y;
        s->has_start = true;
        s->has_end = false;
        sk_line *l = line_at(t, y);
        if (l && l->len != x && x < l->len && !(l->cells[x].content & SK_CELL_WIDTH_MASK))
            s->sx++;
        break;
    }
    case SK_SELECT_WORD:
        select_word(t, x, y);
        s->has_end = false;
        s->mode = SK_SEL_WORD;
        break;
    case SK_SELECT_LINE:
        s->mode = SK_SEL_LINE;
        select_line(t, y);
        break;
    }
    dirty(t);
    sk_term_unlock(t);
}

void sk_term_select_move(sk_term *t, int col, int row)
{
    sk_term_lock(t);
    sk_selection *s = &t->sel;
    if (!s->has_start) {
        sk_term_unlock(t);
        return;
    }
    int x, y;
    at(t, col, row, &x, &y);
    s->ex = x;
    s->ey = y;
    s->has_end = true;
    if (s->mode == SK_SEL_LINE)
        s->ex = s->ey < s->sy ? 0 : t->cols;
    else if (s->mode == SK_SEL_WORD)
        select_to_word(t, s->ex, s->ey);
    if (s->mode != SK_SEL_COLUMN) {
        if (row >= t->rows)
            s->ex = t->cols;
        else if (row < 0)
            s->ex = 0;
    }
    sk_buffer *b = t->active;
    if (s->ey < b->lines.len) {
        sk_line *l = line_at(t, s->ey);
        if (l && s->ex >= 0 && s->ex < l->len && !(l->cells[s->ex].content & SK_CELL_WIDTH_MASK) && s->ex < t->cols)
            s->ex++;
    }
    dirty(t);
    sk_term_unlock(t);
}

void sk_term_select_extend(sk_term *t, int col, int row)
{
    sk_term_lock(t);
    if (t->sel.has_start) {
        at(t, col, row, &t->sel.ex, &t->sel.ey);
        t->sel.has_end = true;
        dirty(t);
    }
    sk_term_unlock(t);
}

void sk_term_select_drag_scroll(sk_term *t, int amount)
{
    sk_term_lock(t);
    sk_selection *s = &t->sel;
    if (s->has_start && s->has_end && amount) {
        sk_term_scroll(t, amount);
        sk_buffer *b = t->active;
        if (amount > 0) {
            if (s->mode != SK_SEL_COLUMN)
                s->ex = t->cols;
            s->ey = SK_MIN(b->ydisp + t->rows, b->lines.len - 1);
        } else {
            if (s->mode != SK_SEL_COLUMN)
                s->ex = 0;
            s->ey = b->ydisp;
        }
        dirty(t);
    }
    sk_term_unlock(t);
}

void sk_term_select_all(sk_term *t)
{
    sk_term_lock(t);
    t->sel.all = true;
    dirty(t);
    sk_term_unlock(t);
}

void sk_term_select_clear(sk_term *t)
{
    sk_term_lock(t);
    sk_select_reset(t);
    sk_term_unlock(t);
}

bool sk_term_has_selection(sk_term *t)
{
    sk_term_lock(t);
    int sx, sy, ex, ey;
    bool has = final_start(t, &sx, &sy) && final_end(t, &ex, &ey) && (sx != ex || sy != ey);
    sk_term_unlock(t);
    return has;
}

bool sk_term_selection_range(sk_term *t, int *x0, int *line0, int *x1, int *line1, bool *column)
{
    sk_term_lock(t);
    bool has = final_start(t, x0, line0) && final_end(t, x1, line1);
    if (column)
        *column = t->sel.mode == SK_SEL_COLUMN;
    sk_term_unlock(t);
    return has;
}

bool sk_term_selected(sk_term *t, int x, int row)
{
    sk_term_lock(t);
    int sx, sy, ex, ey;
    bool in = false;
    if (final_start(t, &sx, &sy) && final_end(t, &ex, &ey)) {
        int y = row + t->active->ydisp;
        if (t->sel.mode == SK_SEL_COLUMN) {
            int lo = SK_MIN(sx, ex), hi = SK_MAX(sx, ex);
            in = y >= sy && y <= ey && x >= lo && x < hi;
        } else {
            in = (y > sy && y < ey) || (sy == ey && y == sy && x >= sx && x < ex) ||
                 (sy < ey && y == ey && x < ex) || (sy < ey && y == sy && x >= sx);
        }
    }
    sk_term_unlock(t);
    return in;
}

/* translateBufferLineToString; end -1 for the line's end. */
static void line_string(sk_term *t, sk_buf *out, int y, bool trim, int start, int end)
{
    sk_buffer *b = t->active;
    if (y < 0 || y >= b->lines.len)
        return;
    sk_line *l = sk_screen_line(b, y);
    sk_line_text(l, out, trim, start, end < 0 ? l->len : end);
}

static void nbsp_to_space(sk_buf *b, size_t from)
{
    sk_buf out = {0};
    for (size_t i = from; i < b->len; i++) {
        if ((unsigned char)b->ptr[i] == 0xC2 && i + 1 < b->len && (unsigned char)b->ptr[i + 1] == 0xA0) {
            sk_buf_putc(&out, ' ');
            i++;
        } else {
            sk_buf_putc(&out, b->ptr[i]);
        }
    }
    b->len = from;
    sk_buf_append(b, out.ptr, out.len);
    sk_buf_free(&out);
}

char *sk_term_selection_text(sk_term *t)
{
    sk_term_lock(t);
    sk_buf out = {0};
    int sx, sy, ex, ey;
    if (final_start(t, &sx, &sy) && final_end(t, &ex, &ey)) {
        sk_buffer *b = t->active;
        if (t->sel.mode == SK_SEL_COLUMN) {
            if (sx != ex) {
                int lo = SK_MIN(sx, ex), hi = SK_MAX(sx, ex);
                for (int i = sy; i <= ey; i++) {
                    if (i > sy)
                        sk_buf_putc(&out, '\n');
                    line_string(t, &out, i, true, lo, hi);
                }
            }
        } else {
            line_string(t, &out, sy, true, sx, sy == ey ? ex : -1);
            for (int i = sy + 1; i <= ey - 1; i++) {
                sk_line *l = i >= 0 && i < b->lines.len ? sk_screen_line(b, i) : NULL;
                if (!(l && l->wrapped))
                    sk_buf_putc(&out, '\n');
                line_string(t, &out, i, true, 0, -1);
            }
            if (sy != ey) {
                sk_line *l = ey >= 0 && ey < b->lines.len ? sk_screen_line(b, ey) : NULL;
                if (!(l && l->wrapped))
                    sk_buf_putc(&out, '\n');
                line_string(t, &out, ey, true, 0, ex);
            }
        }
        nbsp_to_space(&out, 0);
    }
    sk_buf_reserve(&out, 0);
    sk_term_unlock(t);
    return out.ptr;
}

char *sk_term_screen_text(sk_term *t)
{
    sk_term_lock(t);
    sk_buffer *b = t->active;
    sk_buf *rows = sk_xcalloc((size_t)t->rows, sizeof *rows);
    for (int y = 0; y < t->rows; y++) {
        sk_line *l = sk_screen_line_at(b, b->ybase + y);
        if (l)
            sk_line_text(l, &rows[y], true, 0, l->len);
    }
    int first = 0, last = t->rows;
    while (last > first && !sk_js_trim(sk_buf_str(&rows[last - 1])).len)
        last--;
    while (first < last && !sk_js_trim(sk_buf_str(&rows[first])).len)
        first++;
    sk_buf out = {0};
    for (int y = first; y < last; y++) {
        if (y > first)
            sk_buf_putc(&out, '\n');
        sk_buf_append(&out, rows[y].ptr, rows[y].len);
    }
    for (int y = 0; y < t->rows; y++)
        sk_buf_free(&rows[y]);
    free(rows);
    sk_buf_reserve(&out, 0);
    sk_term_unlock(t);
    return out.ptr;
}
