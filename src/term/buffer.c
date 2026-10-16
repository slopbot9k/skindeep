/* xterm.js 6.0.0's BufferLine, CircularList, Buffer and BufferReflow,
 * ported from xterm.js (MIT, third_party/xterm.js). A combined cell's
 * text is copied wherever xterm.js shares the string, so a line owns
 * all it points to; a line dropped from a ring is freed where
 * JavaScript would let it go. */
#include "buffer.h"

#include "../base/utf8.h"
#include "term.h"

#include <stdatomic.h>
#include <string.h>

static _Atomic uint64_t gens;
static _Atomic long alive;

long sk_line_alive(void)
{
    return atomic_load_explicit(&alive, memory_order_relaxed);
}

void sk_line_touch(sk_line *l)
{
    l->gen = atomic_fetch_add_explicit(&gens, 1, memory_order_relaxed) + 1;
}

/* The room a combined text of n codepoints, after its count, is given: a
 * power of two, so marks added one at a time, as REP of one adds tens of
 * thousands, cost a copy only when the room fills. */
static size_t comb_room(uint32_t n)
{
    size_t room = 4;
    while (room < (size_t)n + 1)
        room *= 2;
    return room;
}

static uint32_t *comb_dup(const uint32_t *c)
{
    uint32_t n = c ? c[0] : 0;
    uint32_t *d = sk_xreallocarray(NULL, comb_room(n), sizeof *d);
    d[0] = n;
    if (n)
        memcpy(d + 1, c + 1, n * sizeof *d);
    return d;
}

static void reserve(sk_line *l, int cols)
{
    if (cols <= l->cap)
        return;
    l->cells = sk_xreallocarray(l->cells, (size_t)cols, sizeof *l->cells);
    if (l->ext)
        l->ext = sk_xreallocarray(l->ext, (size_t)cols, sizeof *l->ext);
    if (l->comb) {
        l->comb = sk_xreallocarray(l->comb, (size_t)cols, sizeof *l->comb);
        memset(l->comb + l->cap, 0, (size_t)(cols - l->cap) * sizeof *l->comb);
    }
    l->cap = cols;
}

static void drop_comb(sk_line *l, int i)
{
    if (l->comb && l->comb[i]) {
        free(l->comb[i]);
        l->comb[i] = NULL;
    }
}

sk_line *sk_line_new(int cols, const sk_cell_data *fill, bool wrapped)
{
    sk_line *l = sk_xcalloc(1, sizeof *l);
    l->refs = 1;
    atomic_fetch_add_explicit(&alive, 1, memory_order_relaxed);
    reserve(l, cols > 0 ? cols : 1);
    l->len = cols;
    l->wrapped = wrapped;
    for (int i = 0; i < cols; i++)
        sk_line_set(l, i, fill);
    sk_line_touch(l);
    return l;
}

void sk_line_free(sk_line *l)
{
    if (!l)
        return;
    atomic_fetch_sub_explicit(&alive, 1, memory_order_relaxed);
    if (l->comb)
        for (int i = 0; i < l->cap; i++)
            free(l->comb[i]);
    free(l->comb);
    free(l->ext);
    free(l->cells);
    free(l);
}

void sk_line_copy_from(sk_line *l, const sk_line *src)
{
    if (l->comb)
        for (int i = 0; i < l->cap; i++)
            drop_comb(l, i);
    reserve(l, src->len > 0 ? src->len : 1);
    l->len = src->len;
    if (src->len)
        memcpy(l->cells, src->cells, (size_t)src->len * sizeof *l->cells);
    if (src->ext) {
        if (!l->ext)
            l->ext = sk_xcalloc((size_t)l->cap, sizeof *l->ext);
        memcpy(l->ext, src->ext, (size_t)src->len * sizeof *l->ext);
    }
    if (src->comb) {
        if (!l->comb)
            l->comb = sk_xcalloc((size_t)l->cap, sizeof *l->comb);
        for (int i = 0; i < src->len; i++)
            if (src->comb[i])
                l->comb[i] = comb_dup(src->comb[i]);
    }
    l->wrapped = src->wrapped;
    sk_line_touch(l);
}

sk_line *sk_line_clone(const sk_line *src)
{
    sk_line *l = sk_xcalloc(1, sizeof *l);
    l->refs = 1;
    atomic_fetch_add_explicit(&alive, 1, memory_order_relaxed);
    sk_line_copy_from(l, src);
    return l;
}

void sk_line_hold(sk_line *l)
{
    l->refs++;
}

void sk_line_release(sk_line *l)
{
    if (l && --l->refs == 0)
        sk_line_free(l);
}

int sk_line_chars(const sk_line *l, int i, uint32_t *one, const uint32_t **cp)
{
    uint32_t c = l->cells[i].content;
    if (c & SK_CELL_COMBINED) {
        *cp = l->comb[i] + 1;
        return (int)l->comb[i][0];
    }
    *one = c & SK_CELL_CP_MASK;
    *cp = one;
    return *one ? 1 : 0;
}

uint32_t sk_line_codepoint(const sk_line *l, int i)
{
    uint32_t c = l->cells[i].content;
    if (c & SK_CELL_COMBINED)
        return l->comb[i][0] ? l->comb[i][l->comb[i][0]] : 0;
    return c & SK_CELL_CP_MASK;
}

void sk_line_load(const sk_line *l, int i, sk_cell_data *out)
{
    const sk_cell *c = &l->cells[i];
    out->content = c->content;
    out->fg = c->fg;
    out->bg = c->bg;
    out->x = (c->bg & SK_BG_HAS_EXTENDED) && l->ext ? l->ext[i] : (sk_cell_ext){0, 0};
    out->comb = (c->content & SK_CELL_COMBINED) ? l->comb[i] : NULL;
}

void sk_line_set(sk_line *l, int i, const sk_cell_data *c)
{
    if (c->content & SK_CELL_COMBINED) {
        if (!l->comb)
            l->comb = sk_xcalloc((size_t)l->cap, sizeof *l->comb);
        if (l->comb[i] != c->comb) {
            uint32_t *d = comb_dup(c->comb);
            free(l->comb[i]);
            l->comb[i] = d;
        }
    } else {
        drop_comb(l, i);
    }
    if (c->bg & SK_BG_HAS_EXTENDED) {
        if (!l->ext)
            l->ext = sk_xcalloc((size_t)l->cap, sizeof *l->ext);
        l->ext[i] = c->x;
    }
    l->cells[i] = (sk_cell){c->content, c->fg, c->bg};
}

void sk_line_set_cp(sk_line *l, int i, uint32_t cp, int width, const sk_cell_data *attr)
{
    if (attr->bg & SK_BG_HAS_EXTENDED) {
        if (!l->ext)
            l->ext = sk_xcalloc((size_t)l->cap, sizeof *l->ext);
        l->ext[i] = attr->x;
    }
    drop_comb(l, i);
    l->cells[i] = (sk_cell){cp | (uint32_t)width << SK_CELL_WIDTH_SHIFT, attr->fg, attr->bg};
}

void sk_line_add_cp(sk_line *l, int i, uint32_t cp, int width)
{
    uint32_t content = l->cells[i].content;
    if (content & SK_CELL_COMBINED) {
        uint32_t n = l->comb[i][0];
        if ((size_t)n + 2 > comb_room(n))
            l->comb[i] = sk_xreallocarray(l->comb[i], comb_room(n + 1), sizeof **l->comb);
        l->comb[i][n + 1] = cp;
        l->comb[i][0] = n + 1;
    } else if (content & SK_CELL_CP_MASK) {
        if (!l->comb)
            l->comb = sk_xcalloc((size_t)l->cap, sizeof *l->comb);
        uint32_t *d = sk_xreallocarray(NULL, comb_room(2), sizeof *d);
        d[0] = 2;
        d[1] = content & SK_CELL_CP_MASK;
        d[2] = cp;
        l->comb[i] = d;
        content &= ~SK_CELL_CP_MASK;
        content |= SK_CELL_COMBINED;
    } else {
        content = cp | 1u << SK_CELL_WIDTH_SHIFT;
    }
    if (width) {
        content &= ~SK_CELL_WIDTH_MASK;
        content |= (uint32_t)width << SK_CELL_WIDTH_SHIFT;
    }
    l->cells[i].content = content;
}

static bool protected_at(const sk_line *l, int i)
{
    return l->cells[i].bg & SK_BG_PROTECTED;
}

void sk_line_insert_cells(sk_line *l, int pos, int n, const sk_cell_data *fill)
{
    pos %= l->len;
    if (pos && sk_line_width(l, pos - 1) == 2)
        sk_line_set_cp(l, pos - 1, 0, 1, fill);
    if (n < l->len - pos) {
        sk_cell_data c;
        for (int i = l->len - pos - n - 1; i >= 0; --i) {
            sk_line_load(l, pos + i, &c);
            sk_line_set(l, pos + n + i, &c);
        }
        for (int i = 0; i < n; ++i)
            sk_line_set(l, pos + i, fill);
    } else {
        for (int i = pos; i < l->len; ++i)
            sk_line_set(l, i, fill);
    }
    if (sk_line_width(l, l->len - 1) == 2)
        sk_line_set_cp(l, l->len - 1, 0, 1, fill);
    sk_line_touch(l);
}

void sk_line_delete_cells(sk_line *l, int pos, int n, const sk_cell_data *fill)
{
    pos %= l->len;
    if (n < l->len - pos) {
        sk_cell_data c;
        for (int i = 0; i < l->len - pos - n; ++i) {
            sk_line_load(l, pos + n + i, &c);
            sk_line_set(l, pos + i, &c);
        }
        for (int i = l->len - n; i < l->len; ++i)
            sk_line_set(l, i, fill);
    } else {
        for (int i = pos; i < l->len; ++i)
            sk_line_set(l, i, fill);
    }
    if (pos && sk_line_width(l, pos - 1) == 2)
        sk_line_set_cp(l, pos - 1, 0, 1, fill);
    if (sk_line_width(l, pos) == 0 && !sk_line_has_content(l, pos))
        sk_line_set_cp(l, pos, 0, 1, fill);
    sk_line_touch(l);
}

void sk_line_replace_cells(sk_line *l, int start, int end, const sk_cell_data *fill, bool protect)
{
    if (protect) {
        if (start && start <= l->len && sk_line_width(l, start - 1) == 2 && !protected_at(l, start - 1))
            sk_line_set_cp(l, start - 1, 0, 1, fill);
        if (end >= 1 && end < l->len && sk_line_width(l, end - 1) == 2 && !protected_at(l, end))
            sk_line_set_cp(l, end, 0, 1, fill);
        for (; start < end && start < l->len; start++)
            if (!protected_at(l, start))
                sk_line_set(l, start, fill);
        sk_line_touch(l);
        return;
    }
    if (start && start <= l->len && sk_line_width(l, start - 1) == 2)
        sk_line_set_cp(l, start - 1, 0, 1, fill);
    if (end >= 1 && end < l->len && sk_line_width(l, end - 1) == 2)
        sk_line_set_cp(l, end, 0, 1, fill);
    for (; start < end && start < l->len; start++)
        sk_line_set(l, start, fill);
    sk_line_touch(l);
}

void sk_line_fill(sk_line *l, const sk_cell_data *fill, bool protect)
{
    for (int i = 0; i < l->len; ++i)
        if (!protect || !protected_at(l, i))
            sk_line_set(l, i, fill);
    sk_line_touch(l);
}

void sk_line_resize(sk_line *l, int cols, const sk_cell_data *fill)
{
    if (cols == l->len)
        return;
    if (cols > l->len) {
        reserve(l, cols);
        int old = l->len;
        l->len = cols;
        for (int i = old; i < cols; ++i) {
            l->cells[i].content = 0;
            sk_line_set(l, i, fill);
        }
    } else {
        for (int i = cols; i < l->len; i++)
            drop_comb(l, i);
        l->len = cols;
    }
    sk_line_touch(l);
}

void sk_line_copy_cells(sk_line *dst, const sk_line *src, int src_col, int dst_col, int n, bool reverse)
{
    for (int k = 0; k < n; k++) {
        int cell = reverse ? n - 1 - k : k;
        int s = src_col + cell, d = dst_col + cell;
        if (dst == src && s == d)
            continue;
        sk_cell_data c;
        sk_line_load(src, s, &c);
        sk_line_set(dst, d, &c);
    }
    sk_line_touch(dst);
}

int sk_line_trimmed_length(const sk_line *l)
{
    for (int i = l->len - 1; i >= 0; --i)
        if (l->cells[i].content & SK_CELL_HAS_CONTENT)
            return i + (int)(l->cells[i].content >> SK_CELL_WIDTH_SHIFT);
    return 0;
}

void sk_line_text(const sk_line *l, sk_buf *out, bool trim_right, int start, int end)
{
    if (trim_right)
        end = SK_MIN(end, sk_line_trimmed_length(l));
    while (start < end) {
        uint32_t c = start >= 0 && start < l->len ? l->cells[start].content : 0;
        if (c & SK_CELL_COMBINED) {
            const uint32_t *s = l->comb[start];
            for (uint32_t k = 1; k <= s[0]; k++)
                sk_utf8_append(out, s[k]);
        } else if (c & SK_CELL_CP_MASK) {
            sk_utf8_append(out, c & SK_CELL_CP_MASK);
        } else {
            sk_buf_putc(out, ' ');
        }
        int w = (int)(c >> SK_CELL_WIDTH_SHIFT);
        start += w ? w : 1;
    }
}

/* The ring. */

static const sk_cell_data null_cell = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};

static void emit(sk_lines *r, sk_lines_event ev, int index, int amount)
{
    if (r->listen && r->listen->fn)
        r->listen->fn(r->listen->ud, r->id, ev, index, amount);
}

static sk_line **slot(sk_lines *r, int i)
{
    return &r->v[(r->start + i) % r->max];
}

static void lines_init(sk_lines *r, int max)
{
    r->max = max;
    r->start = 0;
    r->len = 0;
    r->v = sk_xcalloc((size_t)max, sizeof *r->v);
}

/* this._array[cyclic(i)] = l: the slot takes a reference to l and lets
 * go of the line it held. */
static void put(sk_lines *r, int i, sk_line *l)
{
    sk_line **s = slot(r, i);
    sk_line *old = *s;
    if (l)
        l->refs++;
    *s = l;
    sk_line_release(old);
}

/* The same with a new line, whose maker's reference the slot takes. */
static void adopt(sk_lines *r, int i, sk_line *l)
{
    sk_line **s = slot(r, i);
    sk_line *old = *s;
    *s = l;
    sk_line_release(old);
}

static void lines_free(sk_lines *r)
{
    for (int i = 0; i < r->max; i++)
        sk_line_release(r->v[i]);
    free(r->v);
    r->v = NULL;
    r->len = 0;
}

/* The maxLength setter: keeps the first lines that fit. */
static void lines_set_max(sk_lines *r, int max)
{
    if (max == r->max)
        return;
    sk_line **v = sk_xcalloc((size_t)max, sizeof *v);
    int keep = SK_MIN(max, r->len);
    for (int i = 0; i < keep; i++)
        if ((v[i] = *slot(r, i)))
            v[i]->refs++;
    lines_free(r);
    r->v = v;
    r->max = max;
    r->start = 0;
    r->len = keep;
}

void sk_lines_set(sk_lines *r, int i, sk_line *l)
{
    if (*slot(r, i) != l)
        adopt(r, i, l);
}

void sk_lines_push(sk_lines *r, sk_line *l)
{
    adopt(r, r->len, l);
    if (r->len == r->max) {
        r->start = (r->start + 1) % r->max;
        emit(r, SK_LINES_TRIM, 0, 1);
    } else {
        r->len++;
    }
}

/* The last line, with its slot's reference. */
static sk_line *lines_pop(sk_lines *r)
{
    sk_line **s = slot(r, r->len - 1);
    sk_line *l = *s;
    *s = NULL;
    r->len--;
    return l;
}

void sk_lines_insert(sk_lines *r, int i, sk_line *l)
{
    /* CircularList.splice, move for move. Into a full ring the first move
     * up wraps onto the first line, and past the end nothing moves and
     * the line lands on the one i wraps to; then the trim moves the start
     * on. Inserted at 0, the last line so comes first and the new one
     * last; past the end, the first line comes last. */
    bool full = r->len == r->max;
    /* Past the end of a ring with room JavaScript would expose a slot
     * the list no longer holds; the line goes at the end instead. */
    int at = full ? i : SK_MIN(i, r->len);
    for (int k = r->len - 1; k >= at; k--)
        put(r, k + 1, *slot(r, k));
    adopt(r, at, l);
    emit(r, SK_LINES_INSERT, i, 1);
    if (full) {
        r->start = (r->start + 1) % r->max;
        emit(r, SK_LINES_TRIM, 0, 1);
    } else {
        r->len++;
    }
}

void sk_lines_remove(sk_lines *r, int i)
{
    if (!r->len)
        return;
    for (int k = i; k < r->len - 1; k++)
        put(r, k, *slot(r, k + 1));
    /* The slot left past the end lets its line go. */
    put(r, r->len - 1, NULL);
    r->len--;
    emit(r, SK_LINES_DELETE, i, 1);
}

void sk_lines_trim_start(sk_lines *r, int n)
{
    if (n > r->len)
        n = r->len;
    for (int k = 0; k < n; k++)
        put(r, k, NULL);
    r->start = (r->start + n) % r->max;
    r->len -= n;
    emit(r, SK_LINES_TRIM, 0, n);
}

bool sk_lines_shift(sk_lines *r, int start, int count, int offset)
{
    if (count <= 0)
        return true;
    if (start < 0 || start >= r->len || start + offset < 0)
        return false;
    if (offset > 0) {
        for (int k = count - 1; k >= 0; k--)
            put(r, start + k + offset, *slot(r, start + k));
        int expand = start + count + offset - r->len;
        if (expand > 0) {
            r->len += expand;
            while (r->len > r->max) {
                r->len--;
                r->start = (r->start + 1) % r->max;
                emit(r, SK_LINES_TRIM, 0, 1);
            }
        }
    } else {
        for (int k = 0; k < count; k++)
            put(r, start + k + offset, *slot(r, start + k));
    }
    return true;
}

/* The buffer. */

static int correct_length(const sk_buffer *b, int rows)
{
    return b->has_scrollback ? rows + b->scrollback : rows;
}

void sk_buffer_init(sk_buffer *b, int id, bool scrollback_on, int scrollback, int cols, int rows,
                    const sk_lines_listener *listen)
{
    memset(b, 0, sizeof *b);
    b->has_scrollback = scrollback_on;
    b->scrollback = scrollback;
    b->cols = cols;
    b->rows = rows;
    lines_init(&b->lines, correct_length(b, rows));
    b->lines.id = id;
    b->lines.listen = listen;
    b->scroll_top = 0;
    b->scroll_bottom = rows - 1;
    sk_buffer_setup_tabs(b, -1);
}

void sk_buffer_free(sk_buffer *b)
{
    lines_free(&b->lines);
    free(b->tabs);
    b->tabs = NULL;
}

sk_line *sk_buffer_blank(const sk_buffer *b, const sk_cell_data *fill, bool wrapped)
{
    return sk_line_new(b->cols, fill, wrapped);
}

void sk_buffer_fill_viewport(sk_buffer *b, const sk_cell_data *fill)
{
    if (b->lines.len)
        return;
    for (int i = 0; i < b->rows; i++)
        sk_lines_push(&b->lines, sk_buffer_blank(b, fill ? fill : &null_cell, false));
}

void sk_buffer_clear(sk_buffer *b)
{
    b->ydisp = b->ybase = b->y = b->x = 0;
    const sk_lines_listener *listen = b->lines.listen;
    int id = b->lines.id;
    lines_free(&b->lines);
    lines_init(&b->lines, correct_length(b, b->rows));
    b->lines.id = id;
    b->lines.listen = listen;
    b->scroll_top = 0;
    b->scroll_bottom = b->rows - 1;
    sk_buffer_setup_tabs(b, -1);
}

/* Tab stops: xterm.js keeps them in an object, so a stop may lie past
 * the columns and survive a narrowing. */

bool sk_buffer_tab(const sk_buffer *b, int x)
{
    return x >= 0 && x < b->tabs_cap && b->tabs[x];
}

void sk_buffer_set_tab(sk_buffer *b, int x, bool on)
{
    if (x < 0)
        return;
    if (x >= b->tabs_cap) {
        if (!on)
            return;
        int cap = SK_MAX(x + 1, b->tabs_cap * 2);
        b->tabs = sk_xrealloc(b->tabs, (size_t)cap);
        memset(b->tabs + b->tabs_cap, 0, (size_t)(cap - b->tabs_cap));
        b->tabs_cap = cap;
    }
    b->tabs[x] = on;
}

int sk_buffer_prev_stop(const sk_buffer *b, int x)
{
    while (!sk_buffer_tab(b, --x) && x > 0)
        ;
    return x >= b->cols ? b->cols - 1 : x < 0 ? 0 : x;
}

int sk_buffer_next_stop(const sk_buffer *b, int x)
{
    while (!sk_buffer_tab(b, ++x) && x < b->cols)
        ;
    return x >= b->cols ? b->cols - 1 : x < 0 ? 0 : x;
}

void sk_buffer_setup_tabs(sk_buffer *b, int from)
{
    int i = from;
    if (from >= 0) {
        if (!sk_buffer_tab(b, i))
            i = sk_buffer_prev_stop(b, i);
    } else {
        if (b->tabs)
            memset(b->tabs, 0, (size_t)b->tabs_cap);
        i = 0;
    }
    for (; i < b->cols; i += 8)
        sk_buffer_set_tab(b, i, true);
}

/* Reflow, as BufferReflow.ts. */

static int width_at(const sk_line *l, int i)
{
    return i >= 0 && i < l->len ? sk_line_width(l, i) : 0;
}

static int wrapped_trimmed(sk_line **lines, int n, int i, int cols)
{
    if (i == n - 1)
        return sk_line_trimmed_length(lines[i]);
    bool ends_in_null = !sk_line_has_content(lines[i], cols - 1) && sk_line_width(lines[i], cols - 1) == 1;
    bool next_wide = width_at(lines[i + 1], 0) == 2;
    return ends_in_null && next_wide ? cols - 1 : cols;
}

typedef SK_VEC(int) int_vec;

static void reflow_larger(sk_buffer *b, int new_cols, int new_rows)
{
    sk_lines *r = &b->lines;
    int old_cols = b->cols;
    int abs_y = b->ybase + b->y;
    int_vec remove = {0};
    SK_VEC(sk_line *) wl = {0};
    for (int y = 0; y < r->len - 1; y++) {
        int i = y;
        sk_line *next = sk_lines_get(r, ++i);
        if (!next->wrapped)
            continue;
        wl.len = 0;
        sk_vec_push(&wl, sk_lines_get(r, y));
        while (i < r->len && next->wrapped) {
            sk_vec_push(&wl, next);
            ++i;
            next = i < r->len ? sk_lines_get(r, i) : NULL;
            if (!next)
                break;
        }
        int n = (int)wl.len;
        if (abs_y >= y && abs_y < i) {
            y += n - 1;
            continue;
        }
        int dest = 0, dest_col = wrapped_trimmed(wl.ptr, n, 0, old_cols);
        int src = 1, src_col = 0;
        while (src < n) {
            int src_trimmed = wrapped_trimmed(wl.ptr, n, src, old_cols);
            int take = SK_MIN(src_trimmed - src_col, new_cols - dest_col);
            sk_line_copy_cells(wl.ptr[dest], wl.ptr[src], src_col, dest_col, take, false);
            dest_col += take;
            if (dest_col == new_cols) {
                dest++;
                dest_col = 0;
            }
            src_col += take;
            if (src_col == src_trimmed) {
                src++;
                src_col = 0;
            }
            if (dest_col == 0 && dest != 0 && sk_line_width(wl.ptr[dest - 1], new_cols - 1) == 2) {
                sk_line_copy_cells(wl.ptr[dest], wl.ptr[dest - 1], new_cols - 1, dest_col++, 1, false);
                sk_line_set(wl.ptr[dest - 1], new_cols - 1, &null_cell);
                sk_line_touch(wl.ptr[dest - 1]);
            }
        }
        sk_line_replace_cells(wl.ptr[dest], dest_col, new_cols, &null_cell, false);
        int count = 0;
        for (int k = n - 1; k > 0; k--) {
            if (k > dest || sk_line_trimmed_length(wl.ptr[k]) == 0)
                count++;
            else
                break;
        }
        if (count > 0) {
            sk_vec_push(&remove, y + n - count);
            sk_vec_push(&remove, count);
        }
        y += n - 1;
    }
    sk_vec_free(&wl);
    if (!remove.len) {
        sk_vec_free(&remove);
        return;
    }
    /* The new layout, every line but the removed ones. */
    sk_line **kept = sk_xreallocarray(NULL, (size_t)r->len, sizeof *kept);
    int nk = 0, removed = 0;
    size_t next_idx = 0;
    for (int i = 0; i < r->len; i++) {
        if (next_idx < remove.len && remove.ptr[next_idx] == i) {
            int count = remove.ptr[next_idx + 1];
            emit(r, SK_LINES_DELETE, i - removed, count);
            for (int k = 0; k < count && i + k < r->len; k++)
                sk_line_release(sk_lines_get(r, i + k));
            i += count - 1;
            removed += count;
            next_idx += 2;
        } else {
            kept[nk++] = sk_lines_get(r, i);
        }
    }
    sk_vec_free(&remove);
    memset(r->v, 0, (size_t)r->max * sizeof *r->v);
    r->start = 0;
    for (int i = 0; i < nk; i++)
        r->v[i] = kept[i];
    r->len = nk;
    free(kept);
    /* The viewport. */
    for (int adj = removed; adj > 0; adj--) {
        if (b->ybase == 0) {
            if (b->y > 0)
                b->y--;
            if (r->len < new_rows)
                sk_lines_push(r, sk_line_new(new_cols, &null_cell, false));
        } else {
            if (b->ydisp == b->ybase)
                b->ydisp--;
            b->ybase--;
        }
    }
    b->saved_y = SK_MAX(b->saved_y - removed, 0);
}

static int_vec new_line_lengths(sk_line **wl, int n, int old_cols, int new_cols)
{
    int_vec out = {0};
    int needed = 0;
    for (int i = 0; i < n; i++)
        needed += wrapped_trimmed(wl, n, i, old_cols);
    int src_col = 0, src_line = 0, avail = 0;
    while (avail < needed) {
        if (needed - avail < new_cols) {
            sk_vec_push(&out, needed - avail);
            break;
        }
        src_col += new_cols;
        int old_trimmed = wrapped_trimmed(wl, n, src_line, old_cols);
        if (src_col > old_trimmed) {
            src_col -= old_trimmed;
            src_line++;
        }
        bool wide = src_line < n && width_at(wl[src_line], src_col - 1) == 2;
        if (wide)
            src_col--;
        int len = wide ? new_cols - 1 : new_cols;
        sk_vec_push(&out, len);
        avail += len;
    }
    return out;
}

typedef struct {
    int start;
    sk_line **lines;
    int n;
} insertion;

static void reflow_smaller(sk_buffer *b, int new_cols, int new_rows)
{
    sk_lines *r = &b->lines;
    int old_cols = b->cols;
    SK_VEC(insertion) ins = {0};
    int count_ins = 0;
    SK_VEC(sk_line *) wl = {0};
    for (int y = r->len - 1; y >= 0; y--) {
        sk_line *next = sk_lines_get(r, y);
        if (!next || (!next->wrapped && sk_line_trimmed_length(next) <= new_cols))
            continue;
        wl.len = 0;
        sk_vec_push(&wl, next);
        while (next->wrapped && y > 0) {
            next = sk_lines_get(r, --y);
            sk_vec_push(&wl, next);
        }
        /* Gathered bottom up; xterm.js unshifts. */
        for (size_t a = 0, z = wl.len - 1; a < z; a++, z--) {
            sk_line *t = wl.ptr[a];
            wl.ptr[a] = wl.ptr[z];
            wl.ptr[z] = t;
        }
        int n = (int)wl.len;
        int abs_y = b->ybase + b->y;
        if (abs_y >= y && abs_y < y + n)
            continue;
        int last_len = sk_line_trimmed_length(wl.ptr[n - 1]);
        int_vec dl = new_line_lengths(wl.ptr, n, old_cols, new_cols);
        /* Fewer new lines than old happens only for a lone wrapped line
         * with nothing in it at the top: nothing moves, but xterm.js
         * still shifts the saved cursor. */
        int to_add = (int)dl.len - n;
        int trimmed;
        if (b->ybase == 0 && b->y != r->len - 1)
            trimmed = SK_MAX(0, b->y - r->max + to_add);
        else
            trimmed = SK_MAX(0, r->len - r->max + to_add);
        if (to_add > 0) {
            insertion in = {y + n + count_ins, sk_xreallocarray(NULL, (size_t)to_add, sizeof(sk_line *)), to_add};
            for (int k = 0; k < to_add; k++) {
                in.lines[k] = sk_line_new(new_cols, &null_cell, true);
                sk_vec_push(&wl, in.lines[k]);
            }
            sk_vec_push(&ins, in);
            count_ins += to_add;
        }
        int total = (int)wl.len;
        int dest = (int)dl.len - 1;
        int dest_col = dest >= 0 ? dl.ptr[dest] : 0;
        if (dest >= 0 && dest_col == 0) {
            dest--;
            dest_col = dest >= 0 ? dl.ptr[dest] : 0;
        }
        int src = total - to_add - 1;
        int src_col = last_len;
        while (src >= 0) {
            if (dest < 0 || dest >= total || src >= total)
                break;
            int take = SK_MIN(src_col, dest_col);
            sk_line_copy_cells(wl.ptr[dest], wl.ptr[src], src_col - take, dest_col - take, take, true);
            dest_col -= take;
            if (dest_col == 0) {
                dest--;
                dest_col = dest >= 0 ? dl.ptr[dest] : 0;
            }
            src_col -= take;
            if (src_col == 0) {
                src--;
                src_col = wrapped_trimmed(wl.ptr, total, SK_MAX(src, 0), old_cols);
            }
        }
        for (int i = 0; i < total; i++)
            if (i < (int)dl.len && dl.ptr[i] < new_cols) {
                sk_line_set(wl.ptr[i], dl.ptr[i], &null_cell);
                sk_line_touch(wl.ptr[i]);
            }
        sk_vec_free(&dl);
        for (int adj = to_add - trimmed; adj > 0; adj--) {
            if (b->ybase == 0) {
                if (b->y < new_rows - 1) {
                    b->y++;
                    sk_line_release(lines_pop(r));
                } else {
                    b->ybase++;
                    b->ydisp++;
                }
            } else if (b->ybase < SK_MIN(r->max, r->len + count_ins) - new_rows) {
                if (b->ybase == b->ydisp)
                    b->ydisp++;
                b->ybase++;
            }
        }
        b->saved_y = SK_MIN(b->saved_y + to_add, b->ybase + new_rows - 1);
    }
    sk_vec_free(&wl);
    if (!ins.len) {
        sk_vec_free(&ins);
        return;
    }
    /* Merge the new lines in from the bottom, as xterm.js does, keeping
     * at most max lines. */
    int orig_len = r->len;
    sk_line **orig = sk_xreallocarray(NULL, (size_t)SK_MAX(orig_len, 1), sizeof *orig);
    for (int i = 0; i < orig_len; i++)
        orig[i] = sk_lines_get(r, i);
    int new_len = SK_MIN(r->max, orig_len + count_ins);
    sk_line **out = sk_xcalloc((size_t)SK_MAX(new_len, 1), sizeof *out);
    int_vec events = {0};
    int oli = orig_len - 1, inserted = 0;
    size_t ni = 0;
    for (int i = SK_MIN(r->max - 1, orig_len + count_ins - 1); i >= 0; i--) {
        if (ni < ins.len && ins.ptr[ni].start > oli + inserted) {
            insertion *in = &ins.ptr[ni];
            for (int k = in->n - 1; k >= 0; k--) {
                if (i >= 0) {
                    out[i] = in->lines[k];
                    in->lines[k] = NULL;
                }
                i--;
            }
            i++;
            sk_vec_push(&events, oli + 1);
            sk_vec_push(&events, in->n);
            inserted += in->n;
            ni++;
        } else {
            out[i] = orig[oli];
            orig[oli--] = NULL;
        }
    }
    /* What did not fit is dropped. */
    for (int i = 0; i < orig_len; i++)
        sk_line_release(orig[i]);
    for (size_t k = 0; k < ins.len; k++) {
        for (int j = 0; j < ins.ptr[k].n; j++)
            sk_line_release(ins.ptr[k].lines[j]);
        free(ins.ptr[k].lines);
    }
    sk_vec_free(&ins);
    free(orig);
    memset(r->v, 0, (size_t)r->max * sizeof *r->v);
    r->start = 0;
    for (int i = 0; i < new_len; i++)
        r->v[i] = out[i];
    r->len = new_len;
    free(out);
    int emitted = 0;
    for (size_t k = events.len; k >= 2; k -= 2) {
        emit(r, SK_LINES_INSERT, events.ptr[k - 2] + emitted, events.ptr[k - 1]);
        emitted += events.ptr[k - 1];
    }
    sk_vec_free(&events);
    int trim = SK_MAX(0, orig_len + count_ins - r->max);
    if (trim > 0)
        emit(r, SK_LINES_TRIM, 0, trim);
}

void sk_buffer_resize(sk_buffer *b, int new_cols, int new_rows)
{
    sk_lines *r = &b->lines;
    int new_max = correct_length(b, new_rows);
    if (new_max > r->max)
        lines_set_max(r, new_max);
    if (r->len > 0) {
        if (b->cols < new_cols)
            for (int i = 0; i < r->len; i++)
                sk_line_resize(sk_lines_get(r, i), new_cols, &null_cell);
        int add_y = 0;
        if (b->rows < new_rows) {
            for (int y = b->rows; y < new_rows; y++) {
                if (r->len < new_rows + b->ybase) {
                    if (b->ybase > 0 && r->len <= b->ybase + b->y + add_y + 1) {
                        b->ybase--;
                        add_y++;
                        if (b->ydisp > 0)
                            b->ydisp--;
                    } else {
                        sk_lines_push(r, sk_line_new(new_cols, &null_cell, false));
                    }
                }
            }
        } else {
            for (int y = b->rows; y > new_rows; y--) {
                if (r->len > new_rows + b->ybase) {
                    if (r->len > b->ybase + b->y + 1) {
                        sk_line_release(lines_pop(r));
                    } else {
                        b->ybase++;
                        b->ydisp++;
                    }
                }
            }
        }
        if (new_max < r->max) {
            int trim = r->len - new_max;
            if (trim > 0) {
                sk_lines_trim_start(r, trim);
                b->ybase = SK_MAX(b->ybase - trim, 0);
                b->ydisp = SK_MAX(b->ydisp - trim, 0);
                b->saved_y = SK_MAX(b->saved_y - trim, 0);
            }
            lines_set_max(r, new_max);
        }
        b->x = SK_MIN(b->x, new_cols - 1);
        b->y = SK_MIN(b->y, new_rows - 1);
        if (add_y)
            b->y += add_y;
        b->saved_x = SK_MIN(b->saved_x, new_cols - 1);
        b->scroll_top = 0;
    }
    b->scroll_bottom = new_rows - 1;
    if (b->has_scrollback && b->cols != new_cols) {
        if (new_cols > b->cols)
            reflow_larger(b, new_cols, new_rows);
        else
            reflow_smaller(b, new_cols, new_rows);
        if (b->cols > new_cols)
            for (int i = 0; i < r->len; i++)
                sk_line_resize(sk_lines_get(r, i), new_cols, &null_cell);
    }
    b->cols = new_cols;
    b->rows = new_rows;
    /* Grown from a screen that ran past its lines, xterm.js's buffer can
     * be left with rows that have no line at all, and it throws at the
     * first write to one; blank lines fill them here. */
    while (r->len > 0 && r->len < r->max && r->len < b->ybase + new_rows)
        sk_lines_push(r, sk_line_new(new_cols, &null_cell, false));
}
