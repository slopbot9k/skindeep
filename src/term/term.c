/* The screen: xterm.js 6.0.0's InputHandler with the services it drives
 * (BufferService, BufferSet, CoreService, CharsetService, OscLinkService,
 * the mouse protocol state and the theme's colours), ported from
 * xterm.js (MIT, third_party/xterm.js). Every sequence xterm.js handles
 * is handled the same way, quirks included; what it ignores is ignored.
 * Where a JavaScript handler would throw on a bad index, here nothing
 * happens; where a count is so large xterm.js would run out of memory,
 * the work is cut to what changes the screen. */
#include "screen.h"

#include "../app/js.h"
#include "../base/utf8.h"
#include "width.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MINIMUM_COLS 2
#define MINIMUM_ROWS 1
/* xterm.js's ColorIndex beyond the 256: OSC 10, 11 and 12. */
enum { SPECIAL_FG = 256, SPECIAL_BG, SPECIAL_CURSOR };

static const sk_cell_data default_attr = {SK_CELL_NULL, 0, 0, {0, 0}, NULL};

/* Charsets (data/Charsets.ts): a designation maps ASCII to these. */
static const uint32_t cs_dec[128] = {
    ['`'] = 0x25c6, ['a'] = 0x2592, ['b'] = 0x2409, ['c'] = 0x240c, ['d'] = 0x240d, ['e'] = 0x240a,
    ['f'] = 0x00b0, ['g'] = 0x00b1, ['h'] = 0x2424, ['i'] = 0x240b, ['j'] = 0x2518, ['k'] = 0x2510,
    ['l'] = 0x250c, ['m'] = 0x2514, ['n'] = 0x253c, ['o'] = 0x23ba, ['p'] = 0x23bb, ['q'] = 0x2500,
    ['r'] = 0x23bc, ['s'] = 0x23bd, ['t'] = 0x251c, ['u'] = 0x2524, ['v'] = 0x2534, ['w'] = 0x252c,
    ['x'] = 0x2502, ['y'] = 0x2264, ['z'] = 0x2265, ['{'] = 0x03c0, ['|'] = 0x2260, ['}'] = 0x00a3,
    ['~'] = 0x00b7,
};
static const uint32_t cs_uk[128] = {['#'] = 0xa3};
/* Dutch '[' is "ij", of which xterm.js takes the first code unit. */
static const uint32_t cs_dutch[128] = {
    ['#'] = 0xa3, ['@'] = 0xbe, ['['] = 'i', ['\\'] = 0xbd, [']'] = '|', ['{'] = 0xa8, ['|'] = 'f',
    ['}'] = 0xbc, ['~'] = 0xb4,
};
static const uint32_t cs_finnish[128] = {
    ['['] = 0xc4, ['\\'] = 0xd6, [']'] = 0xc5, ['^'] = 0xdc, ['`'] = 0xe9,
    ['{'] = 0xe4, ['|'] = 0xf6, ['}'] = 0xe5, ['~'] = 0xfc,
};
static const uint32_t cs_french[128] = {
    ['#'] = 0xa3, ['@'] = 0xe0, ['['] = 0xb0, ['\\'] = 0xe7, [']'] = 0xa7,
    ['{'] = 0xe9, ['|'] = 0xf9, ['}'] = 0xe8, ['~'] = 0xa8,
};
static const uint32_t cs_french_ca[128] = {
    ['@'] = 0xe0, ['['] = 0xe2, ['\\'] = 0xe7, [']'] = 0xea, ['^'] = 0xee,
    ['`'] = 0xf4, ['{'] = 0xe9, ['|'] = 0xf9, ['}'] = 0xe8, ['~'] = 0xfb,
};
static const uint32_t cs_german[128] = {
    ['@'] = 0xa7, ['['] = 0xc4, ['\\'] = 0xd6, [']'] = 0xdc, ['{'] = 0xe4, ['|'] = 0xf6, ['}'] = 0xfc, ['~'] = 0xdf,
};
static const uint32_t cs_italian[128] = {
    ['#'] = 0xa3, ['@'] = 0xa7, ['['] = 0xb0, ['\\'] = 0xe7, [']'] = 0xe9,
    ['`'] = 0xf9, ['{'] = 0xe0, ['|'] = 0xf2, ['}'] = 0xe8, ['~'] = 0xec,
};
static const uint32_t cs_nordan[128] = {
    ['@'] = 0xc4, ['['] = 0xc6, ['\\'] = 0xd8, [']'] = 0xc5, ['^'] = 0xdc,
    ['`'] = 0xe4, ['{'] = 0xe6, ['|'] = 0xf8, ['}'] = 0xe5, ['~'] = 0xfc,
};
static const uint32_t cs_spanish[128] = {
    ['#'] = 0xa3, ['@'] = 0xa7, ['['] = 0xa1, ['\\'] = 0xd1, [']'] = 0xbf, ['{'] = 0xb0, ['|'] = 0xf1, ['}'] = 0xe7,
};
static const uint32_t cs_swedish[128] = {
    ['@'] = 0xc9, ['['] = 0xc4, ['\\'] = 0xd6, [']'] = 0xc5, ['^'] = 0xdc,
    ['`'] = 0xe9, ['{'] = 0xe4, ['|'] = 0xf6, ['}'] = 0xe5, ['~'] = 0xfc,
};
static const uint32_t cs_swiss[128] = {
    ['#'] = 0xf9, ['@'] = 0xe0, ['['] = 0xe9, ['\\'] = 0xe7, [']'] = 0xea, ['^'] = 0xee,
    ['_'] = 0xe8, ['`'] = 0xf4, ['{'] = 0xe4, ['|'] = 0xf6, ['}'] = 0xfc, ['~'] = 0xfb,
};

/* The flags xterm.js registers; *out is NULL for US ASCII. */
static bool charset_of(uint32_t flag, const uint32_t **out)
{
    switch (flag) {
    case '0': *out = cs_dec; return true;
    case 'A': *out = cs_uk; return true;
    case 'B': *out = NULL; return true;
    case '4': *out = cs_dutch; return true;
    case 'C': case '5': *out = cs_finnish; return true;
    case 'R': *out = cs_french; return true;
    case 'Q': *out = cs_french_ca; return true;
    case 'K': *out = cs_german; return true;
    case 'Y': *out = cs_italian; return true;
    case 'E': case '6': *out = cs_nordan; return true;
    case 'Z': *out = cs_spanish; return true;
    case 'H': case '7': *out = cs_swedish; return true;
    case '=': *out = cs_swiss; return true;
    }
    return false;
}

static void set_glevel(sk_term *t, int g)
{
    t->glevel = g;
    t->charset = t->charsets[g];
}

static void set_gcharset(sk_term *t, int g, const uint32_t *cs)
{
    t->charsets[g] = cs;
    if (t->glevel == g)
        t->charset = cs;
}

static void charsets_reset(sk_term *t)
{
    t->charset = NULL;
    memset(t->charsets, 0, sizeof t->charsets);
    t->glevel = 0;
}

/* Replies. */

static void reply(sk_term *t, const char *s, size_t n)
{
    if (t->cb.reply)
        t->cb.reply(t->cb.ud, s, n);
}

static void SK_PRINTF(2, 3) replyf(sk_term *t, const char *fmt, ...)
{
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    reply(t, b.ptr, b.len);
    sk_buf_free(&b);
}

/* Attributes. */

static void update_extended(sk_cell_data *a)
{
    if (!a->x.url && !(a->x.ext & SK_EXT_UNDERLINE_STYLE))
        a->bg &= ~SK_BG_HAS_EXTENDED;
    else
        a->bg |= SK_BG_HAS_EXTENDED;
}

/* back_color_erase: the current background, nothing else. */
static const sk_cell_data *erase_attr(sk_term *t)
{
    t->erase.bg &= ~(SK_ATTR_CM_MASK | 0xFFFFFFu);
    t->erase.bg |= t->cur.bg & ~0xFC000000u;
    return &t->erase;
}

/* getNullCell(attr): an empty cell in attr's colours. */
static sk_cell_data null_of(const sk_cell_data *a)
{
    return (sk_cell_data){SK_CELL_NULL, a->fg, a->bg, a->x, NULL};
}

static sk_line *blank_line(sk_term *t, const sk_cell_data *attr, bool wrapped)
{
    sk_cell_data n = null_of(attr);
    return sk_line_new(t->cols, &n, wrapped);
}

/* lines.get(i) past the length: the ring wraps when full; otherwise the
 * slot holds nothing alive. */
sk_line *sk_screen_line_at(sk_buffer *b, int i)
{
    if (i < 0)
        return NULL;
    if (i < b->lines.len)
        return sk_screen_line(b, i);
    if (b->lines.len == b->lines.max)
        return sk_screen_line(b, i % b->lines.max);
    return NULL;
}

static sk_line *cur_line(sk_term *t)
{
    sk_buffer *b = t->active;
    return sk_screen_line(b, b->ybase + b->y);
}

/* Links (OscLinkService) and their markers. */

static sk_link *link_find(sk_term *t, uint32_t id)
{
    size_t lo = 0, hi = t->links.len;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (t->links.ptr[mid].id < id)
            lo = mid + 1;
        else if (t->links.ptr[mid].id > id)
            hi = mid;
        else
            return &t->links.ptr[mid];
    }
    return NULL;
}

static void link_free(sk_link *l)
{
    free(l->key);
    free(l->id_param);
    free(l->uri);
    sk_vec_free(&l->lines);
}

static void links_prune(sk_term *t)
{
    size_t w = 0;
    for (size_t i = 0; i < t->links.len; i++) {
        if (t->links.ptr[i].lines.len)
            t->links.ptr[w++] = t->links.ptr[i];
        else
            link_free(&t->links.ptr[i]);
    }
    t->links.len = w;
}

static void add_marker(sk_link *l, sk_buffer *b, int y)
{
    sk_vec_push(&l->lines, ((sk_marker){y, b->lines.id}));
}

static void add_line_to_link(sk_term *t, uint32_t id, int y)
{
    sk_link *l = link_find(t, id);
    if (!l)
        return;
    for (size_t i = 0; i < l->lines.len; i++)
        if (l->lines.ptr[i].line == y)
            return;
    add_marker(l, t->active, y);
}

static uint32_t register_link(sk_term *t, const char *id, const char *uri)
{
    sk_buffer *b = t->active;
    sk_buf key = {0};
    if (id) {
        sk_buf_printf(&key, "%s;;%s", id, uri);
        for (size_t i = 0; i < t->links.len; i++) {
            sk_link *l = &t->links.ptr[i];
            if (l->key && !strcmp(l->key, key.ptr)) {
                uint32_t found = l->id;
                add_line_to_link(t, found, b->ybase + b->y);
                sk_buf_free(&key);
                return found;
            }
        }
    }
    sk_link l = {0};
    l.id = t->next_link++;
    l.key = id ? sk_xstrdup(key.ptr) : NULL;
    l.id_param = id ? sk_xstrdup(id) : NULL;
    l.uri = sk_xstrdup(uri);
    add_marker(&l, b, b->ybase + b->y);
    sk_vec_push(&t->links, l);
    sk_buf_free(&key);
    return l.id;
}

/* What a buffer's lines do to the markers on them, and to the
 * selection when it is the active buffer. */
static void on_lines(void *ud, int buffer, sk_lines_event ev, int index, int amount)
{
    sk_term *t = ud;
    bool gone = false;
    for (size_t i = 0; i < t->links.len; i++) {
        sk_link *l = &t->links.ptr[i];
        size_t w = 0;
        for (size_t k = 0; k < l->lines.len; k++) {
            sk_marker m = l->lines.ptr[k];
            bool keep = true;
            if (m.buffer == buffer) {
                if (ev == SK_LINES_TRIM) {
                    m.line -= amount;
                    keep = m.line >= 0;
                } else if (ev == SK_LINES_INSERT) {
                    if (m.line >= index)
                        m.line += amount;
                } else {
                    if (m.line >= index && m.line < index + amount)
                        keep = false;
                    else if (m.line > index)
                        m.line -= amount;
                }
            }
            if (keep)
                l->lines.ptr[w++] = m;
        }
        l->lines.len = w;
        gone |= !w;
    }
    if (gone)
        links_prune(t);
    if (ev == SK_LINES_TRIM && t->active && buffer == t->active->lines.id)
        sk_select_trim(t, amount);
}

static void clear_markers(sk_term *t, int buffer, int y, bool all)
{
    bool gone = false;
    for (size_t i = 0; i < t->links.len; i++) {
        sk_link *l = &t->links.ptr[i];
        size_t w = 0;
        for (size_t k = 0; k < l->lines.len; k++) {
            sk_marker m = l->lines.ptr[k];
            if (m.buffer != buffer || (!all && m.line != y))
                l->lines.ptr[w++] = m;
        }
        l->lines.len = w;
        gone |= !w;
    }
    if (gone)
        links_prune(t);
}

/* Buffers (BufferService, BufferSet). */

static void buffers_init(sk_term *t)
{
    sk_buffer_init(&t->normal, ++t->buffer_ids, true, SK_TERM_SCROLLBACK, t->cols, t->rows, &t->listen);
    sk_buffer_fill_viewport(&t->normal, &default_attr);
    sk_buffer_init(&t->alt, ++t->buffer_ids, false, 0, t->cols, t->rows, &t->listen);
    t->active = &t->normal;
}

static void activate_alt(sk_term *t, const sk_cell_data *fill)
{
    if (t->active == &t->alt)
        return;
    sk_cell_data n = null_of(fill);
    sk_buffer_fill_viewport(&t->alt, &n);
    t->alt.x = t->normal.x;
    t->alt.y = t->normal.y;
    t->active = &t->alt;
    sk_select_reset(t);
}

static void activate_normal(sk_term *t)
{
    if (t->active == &t->normal)
        return;
    t->normal.x = t->alt.x;
    t->normal.y = t->alt.y;
    clear_markers(t, t->alt.lines.id, 0, true);
    sk_buffer_clear(&t->alt);
    t->active = &t->normal;
    sk_select_reset(t);
}

/* BufferService.scroll: one line up, a blank one in at the bottom of the
 * scroll region, into the scrollback when the region starts at the top. */
static void scroll(sk_term *t, const sk_cell_data *erase, bool wrapped)
{
    sk_buffer *b = t->active;
    sk_lines *r = &b->lines;
    int top = b->ybase + b->scroll_top, bottom = b->ybase + b->scroll_bottom;
    if (b->scroll_top == 0) {
        bool full = r->len == r->max;
        if (bottom == r->len - 1) {
            if (full) {
                /* recycle(): the first line is reused as the new last. */
                sk_line *l = sk_screen_line(b, 0);
                r->start = (r->start + 1) % r->max;
                sk_line *blank = blank_line(t, erase, wrapped);
                sk_line_copy_from(l, blank);
                sk_line_free(blank);
                r->listen->fn(r->listen->ud, r->id, SK_LINES_TRIM, 0, 1);
            } else {
                sk_lines_push(r, blank_line(t, erase, wrapped));
            }
        } else {
            sk_lines_insert(r, bottom + 1, blank_line(t, erase, wrapped));
        }
        if (!full) {
            b->ybase++;
            if (!t->user_scrolling)
                b->ydisp++;
        } else if (t->user_scrolling) {
            b->ydisp = SK_MAX(b->ydisp - 1, 0);
        }
    } else {
        sk_lines_shift_up(r, top, bottom - top + 1);
        sk_lines_set(r, bottom, blank_line(t, erase, wrapped));
    }
    if (!t->user_scrolling)
        b->ydisp = b->ybase;
}

/* Cursor. */

static void restrict_cursor(sk_term *t, int max_col)
{
    sk_buffer *b = t->active;
    b->x = SK_MIN(max_col, SK_MAX(0, b->x));
    b->y = t->origin ? SK_MIN(b->scroll_bottom, SK_MAX(b->scroll_top, b->y)) : SK_MIN(t->rows - 1, SK_MAX(0, b->y));
}

static int clamp_int(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int)v;
}

static void set_cursor(sk_term *t, int64_t x, int64_t y)
{
    sk_buffer *b = t->active;
    b->x = clamp_int(x);
    b->y = clamp_int(t->origin ? b->scroll_top + y : y);
    restrict_cursor(t, t->cols - 1);
}

static void move_cursor(sk_term *t, int64_t dx, int64_t dy)
{
    restrict_cursor(t, t->cols - 1);
    set_cursor(t, (int64_t)t->active->x + dx, (int64_t)t->active->y + dy);
}

static void save_cursor(sk_term *t)
{
    sk_buffer *b = t->active;
    b->saved_x = b->x;
    b->saved_y = b->ybase + b->y;
    b->saved_fg = t->cur.fg;
    b->saved_bg = t->cur.bg;
    b->saved_charset = t->charset;
}

static void restore_cursor(sk_term *t)
{
    sk_buffer *b = t->active;
    b->x = b->saved_x;
    b->y = SK_MAX(b->saved_y - b->ybase, 0);
    t->cur.fg = b->saved_fg;
    t->cur.bg = b->saved_bg;
    t->charset = b->saved_charset;
    restrict_cursor(t, t->cols - 1);
}

/* Printing. */

#define JOIN_WIDTH(v) ((int)(((v) >> 1) & 3))
#define JOIN_SHOULD(v) ((v) & 1)

/* UnicodeV11.charProperties: a zero-width character joins a preceding
 * one that has width, and takes its width. */
static uint32_t char_props(uint32_t cp, uint32_t preceding)
{
    int width = sk_term_wcwidth(cp);
    bool join = width == 0 && preceding != 0;
    if (join) {
        int old = JOIN_WIDTH(preceding);
        if (old == 0)
            join = false;
        else if (old > width)
            width = old;
    }
    return (uint32_t)(width & 3) << 1 | (join ? 1u : 0u);
}

static void print(void *ud, const uint32_t *data, size_t n)
{
    sk_term *t = ud;
    sk_buffer *b = t->active;
    int cols = t->cols;
    const uint32_t *charset = t->charset;
    sk_cell_data *cur = &t->cur;
    sk_line *row = cur_line(t);
    if (b->x && n > 0 && sk_line_width(row, b->x - 1) == 2)
        sk_line_set_cp(row, b->x - 1, 0, 1, cur);
    uint32_t join = t->vt.join;
    for (size_t pos = 0; pos < n; ++pos) {
        uint32_t code = data[pos];
        if (code < 127 && charset && charset[code])
            code = charset[code];
        uint32_t info = char_props(code, join);
        int width = JOIN_WIDTH(info);
        bool should_join = JOIN_SHOULD(info);
        int old_width = should_join ? JOIN_WIDTH(join) : 0;
        join = info;
        if (cur->x.url)
            add_line_to_link(t, cur->x.url, b->ybase + b->y);
        if (b->x + width - old_width > cols) {
            if (t->wraparound) {
                sk_line *old_row = row;
                int old_col = b->x - old_width;
                b->x = old_width;
                b->y++;
                if (b->y == b->scroll_bottom + 1) {
                    b->y--;
                    scroll(t, erase_attr(t), true);
                } else {
                    if (b->y >= t->rows)
                        b->y = t->rows - 1;
                    cur_line(t)->wrapped = true;
                }
                row = cur_line(t);
                if (old_width > 0)
                    sk_line_copy_cells(row, old_row, old_col, 0, old_width, false);
                while (old_col < cols)
                    sk_line_set_cp(old_row, old_col++, 0, 1, cur);
                sk_line_touch(old_row);
            } else {
                b->x = cols - 1;
                if (width == 2)
                    continue;
            }
        }
        if (should_join && b->x) {
            int offset = sk_line_width(row, b->x - 1) ? 1 : 2;
            sk_line_add_cp(row, b->x - offset, code, width);
            for (int delta = width - old_width; --delta >= 0;)
                sk_line_set_cp(row, b->x++, 0, 0, cur);
            continue;
        }
        if (t->insert_mode) {
            sk_cell_data fill = null_of(cur);
            sk_line_insert_cells(row, b->x, width - old_width, &fill);
            if (sk_line_width(row, cols - 1) == 2)
                sk_line_set_cp(row, cols - 1, 0, 1, cur);
        }
        sk_line_set_cp(row, b->x++, code, width, cur);
        if (width > 0)
            while (--width)
                sk_line_set_cp(row, b->x++, 0, 0, cur);
    }
    t->vt.join = join;
    if (b->x < cols && n > 0 && sk_line_width(row, b->x) == 0 && !sk_line_has_content(row, b->x))
        sk_line_set_cp(row, b->x, 0, 1, cur);
    sk_line_touch(row);
}

/* REP: the preceding character again, as print would; past the copies
 * that fill the whole ring the screen only cycles, so the count is cut
 * to the same point of the cycle. */
static void repeat(sk_term *t, int64_t count)
{
    uint32_t join = t->vt.join;
    if (!join)
        return;
    sk_buffer *b = t->active;
    int width = JOIN_WIDTH(join);
    int x = b->x - width;
    sk_line *row = cur_line(t);
    if (x < 0 || x >= row->len)
        return;
    uint32_t one;
    const uint32_t *cp;
    int k = sk_line_chars(row, x, &one, &cp);
    if (!k)
        return;
    uint32_t text[64];
    if (k > (int)SK_LEN(text))
        k = (int)SK_LEN(text);
    memcpy(text, cp, (size_t)k * sizeof *text);
    int64_t per_line = width ? t->cols / width : 0;
    if (per_line) {
        int64_t settled = (int64_t)(b->lines.max + t->rows + 2) * per_line;
        if (count > settled + per_line)
            count = settled + (count - settled) % per_line;
    } else if (count > 65536) {
        count = 65536;
    }
    uint32_t buf[1024];
    size_t fill = 0;
    for (int64_t i = 0; i < count; i++) {
        if (fill + (size_t)k > SK_LEN(buf)) {
            print(t, buf, fill);
            fill = 0;
        }
        memcpy(buf + fill, text, (size_t)k * sizeof *buf);
        fill += (size_t)k;
    }
    if (fill)
        print(t, buf, fill);
}

/* Executes. */

static void line_feed(sk_term *t)
{
    sk_buffer *b = t->active;
    if (t->convert_eol)
        b->x = 0;
    b->y++;
    if (b->y == b->scroll_bottom + 1) {
        b->y--;
        scroll(t, erase_attr(t), false);
    } else if (b->y >= t->rows) {
        b->y = t->rows - 1;
    } else {
        cur_line(t)->wrapped = false;
    }
    if (b->x >= t->cols)
        b->x--;
}

static void backspace(sk_term *t)
{
    sk_buffer *b = t->active;
    if (!t->reverse_wrap) {
        restrict_cursor(t, t->cols - 1);
        if (b->x > 0)
            b->x--;
        return;
    }
    restrict_cursor(t, t->cols);
    if (b->x > 0) {
        b->x--;
    } else if (b->x == 0 && b->y > b->scroll_top && b->y <= b->scroll_bottom && cur_line(t)->wrapped) {
        cur_line(t)->wrapped = false;
        b->y--;
        b->x = t->cols - 1;
        sk_line *l = cur_line(t);
        if ((l->cells[b->x].content & SK_CELL_WIDTH_MASK) && !sk_line_has_content(l, b->x))
            b->x--;
    }
    restrict_cursor(t, t->cols - 1);
}

static void tab(sk_term *t)
{
    sk_buffer *b = t->active;
    if (b->x >= t->cols)
        return;
    b->x = sk_buffer_next_stop(b, b->x);
}

static void index_down(sk_term *t)
{
    sk_buffer *b = t->active;
    restrict_cursor(t, t->cols - 1);
    b->y++;
    if (b->y == b->scroll_bottom + 1) {
        b->y--;
        scroll(t, erase_attr(t), false);
    } else if (b->y >= t->rows) {
        b->y = t->rows - 1;
    }
    restrict_cursor(t, t->cols - 1);
}

static void reverse_index(sk_term *t)
{
    sk_buffer *b = t->active;
    restrict_cursor(t, t->cols - 1);
    if (b->y == b->scroll_top) {
        int height = b->scroll_bottom - b->scroll_top;
        sk_lines_shift_down(&b->lines, b->ybase + b->y, height);
        sk_lines_set(&b->lines, b->ybase + b->y, blank_line(t, erase_attr(t), false));
    } else {
        b->y--;
        restrict_cursor(t, t->cols - 1);
    }
}

static void tab_set(sk_term *t)
{
    sk_buffer_set_tab(t->active, t->active->x, true);
}

static void execute(void *ud, uint32_t code)
{
    sk_term *t = ud;
    switch (code) {
    case 0x07:
        if (t->cb.bell)
            t->cb.bell(t->cb.ud);
        break;
    case 0x0a:
    case 0x0b:
    case 0x0c:
        line_feed(t);
        break;
    case 0x0d:
        t->active->x = 0;
        break;
    case 0x08:
        backspace(t);
        break;
    case 0x09:
        tab(t);
        break;
    case 0x0e:
        set_glevel(t, 1);
        break;
    case 0x0f:
        set_glevel(t, 0);
        break;
    case 0x84:
        index_down(t);
        break;
    case 0x85:
        t->active->x = 0;
        index_down(t);
        break;
    case 0x88:
        tab_set(t);
        break;
    }
}

/* Erasing. */

static void erase_in_line(sk_term *t, int y, int start, int end, bool clear_wrap, bool protect)
{
    sk_buffer *b = t->active;
    sk_line *l = sk_screen_line(b, b->ybase + y);
    sk_cell_data n = null_of(erase_attr(t));
    sk_line_replace_cells(l, start, end, &n, protect);
    if (clear_wrap)
        l->wrapped = false;
}

static void reset_line(sk_term *t, int y, bool protect)
{
    sk_buffer *b = t->active;
    sk_line *l = sk_screen_line_at(b, b->ybase + y);
    if (!l)
        return;
    sk_cell_data n = null_of(erase_attr(t));
    sk_line_fill(l, &n, protect);
    clear_markers(t, b->lines.id, b->ybase + y, false);
    l->wrapped = false;
}

static void erase_in_display(sk_term *t, int mode, bool protect)
{
    sk_buffer *b = t->active;
    restrict_cursor(t, t->cols);
    int j;
    switch (mode) {
    case 0:
        j = b->y;
        erase_in_line(t, j++, b->x, t->cols, b->x == 0, protect);
        for (; j < t->rows; j++)
            reset_line(t, j, protect);
        break;
    case 1: {
        j = b->y;
        erase_in_line(t, j, 0, b->x + 1, true, protect);
        if (b->x + 1 >= t->cols) {
            /* xterm.js indexes the buffer, not the viewport, here. */
            sk_line *l = sk_screen_line_at(b, j + 1);
            if (l)
                l->wrapped = false;
        }
        while (j--)
            reset_line(t, j, protect);
        break;
    }
    case 2:
        for (j = t->rows; j--;)
            reset_line(t, j, protect);
        break;
    case 3: {
        int n = b->lines.len - t->rows;
        if (n > 0) {
            sk_lines_trim_start(&b->lines, n);
            b->ybase = SK_MAX(b->ybase - n, 0);
            b->ydisp = SK_MAX(b->ydisp - n, 0);
        }
        break;
    }
    }
}

static void erase_line_mode(sk_term *t, int mode, bool protect)
{
    sk_buffer *b = t->active;
    restrict_cursor(t, t->cols);
    switch (mode) {
    case 0:
        erase_in_line(t, b->y, b->x, t->cols, b->x == 0, protect);
        break;
    case 1:
        erase_in_line(t, b->y, 0, b->x + 1, false, protect);
        break;
    case 2:
        erase_in_line(t, b->y, 0, t->cols, true, protect);
        break;
    }
}

/* Lines and columns. Repeating past the region's height changes nothing
 * more, so the counts stop there. */

static void insert_lines(sk_term *t, int64_t n)
{
    sk_buffer *b = t->active;
    restrict_cursor(t, t->cols - 1);
    if (b->y > b->scroll_bottom || b->y < b->scroll_top)
        return;
    int row = b->ybase + b->y;
    int bottom = b->ybase + b->scroll_bottom + 1;
    n = SK_MIN(n, (int64_t)(b->scroll_bottom - b->y + 1));
    while (n-- > 0) {
        sk_lines_remove(&b->lines, bottom - 1);
        sk_lines_insert(&b->lines, row, blank_line(t, erase_attr(t), false));
    }
    b->x = 0;
}

static void delete_lines(sk_term *t, int64_t n)
{
    sk_buffer *b = t->active;
    restrict_cursor(t, t->cols - 1);
    if (b->y > b->scroll_bottom || b->y < b->scroll_top)
        return;
    int row = b->ybase + b->y;
    int j = b->ybase + b->scroll_bottom;
    n = SK_MIN(n, (int64_t)(b->scroll_bottom - b->y + 1));
    while (n-- > 0) {
        sk_lines_remove(&b->lines, row);
        sk_lines_insert(&b->lines, j, blank_line(t, erase_attr(t), false));
    }
    b->x = 0;
}

static void scroll_up(sk_term *t, int64_t n)
{
    sk_buffer *b = t->active;
    n = SK_MIN(n, (int64_t)(b->scroll_bottom - b->scroll_top + 1));
    while (n-- > 0) {
        sk_lines_remove(&b->lines, b->ybase + b->scroll_top);
        sk_lines_insert(&b->lines, b->ybase + b->scroll_bottom, blank_line(t, erase_attr(t), false));
    }
}

static void scroll_down(sk_term *t, int64_t n)
{
    sk_buffer *b = t->active;
    n = SK_MIN(n, (int64_t)(b->scroll_bottom - b->scroll_top + 1));
    while (n-- > 0) {
        sk_lines_remove(&b->lines, b->ybase + b->scroll_bottom);
        sk_lines_insert(&b->lines, b->ybase + b->scroll_top, blank_line(t, &default_attr, false));
    }
}

static int count_of(int64_t n, int cols)
{
    return (int)SK_MIN(n, (int64_t)cols + 1);
}

/* SL, SR, DECIC and DECDC: every line of the scroll region. */
static void shift_columns(sk_term *t, int at, int64_t n, bool insert)
{
    sk_buffer *b = t->active;
    if (b->y > b->scroll_bottom || b->y < b->scroll_top)
        return;
    sk_cell_data fill = null_of(erase_attr(t));
    for (int y = b->scroll_top; y <= b->scroll_bottom; ++y) {
        sk_line *l = sk_screen_line(b, b->ybase + y);
        if (insert)
            sk_line_insert_cells(l, at, count_of(n, t->cols), &fill);
        else
            sk_line_delete_cells(l, at, count_of(n, t->cols), &fill);
        l->wrapped = false;
    }
}

/* SGR. */

static uint32_t attr_color(uint32_t color, int mode, int32_t c1, int32_t c2, int32_t c3)
{
    if (mode == 2) {
        color |= SK_ATTR_CM_RGB;
        color &= ~SK_ATTR_RGB_MASK;
        color |= ((uint32_t)c1 & 255) << 16 | ((uint32_t)c2 & 255) << 8 | ((uint32_t)c3 & 255);
    } else if (mode == 5) {
        color &= ~(SK_ATTR_CM_MASK | SK_ATTR_PCOLOR_MASK);
        color |= SK_ATTR_CM_P256 | ((uint32_t)c1 & 0xff);
    }
    return color;
}

/* _extractColor: 38, 48 or 58 with its colour by ; or by :. Returns how
 * many parameters it took past the first. */
static int extract_color(const sk_vt_params *p, int pos, sk_cell_data *a)
{
    int32_t accu[8] = {0, 0, -1, 0, 0, 0, 0, 0};
    int len = 6;
    int cspace = 0, advance = 0;
    do {
        accu[advance + cspace] = p->v[pos + advance];
        if (sk_vt_has_sub(p, pos + advance)) {
            const int32_t *sub;
            int ns = sk_vt_subs(p, pos + advance, &sub);
            int i = 0;
            do {
                if (accu[1] == 5)
                    cspace = 1;
                int at = advance + i + 1 + cspace;
                accu[at] = sub[i];
                if (at >= len)
                    len = at + 1;
            } while (++i < ns && i + advance + 1 + cspace < len);
            break;
        }
        if ((accu[1] == 5 && advance + cspace >= 2) || (accu[1] == 2 && advance + cspace >= 5))
            break;
        if (accu[1])
            cspace = 1;
    } while (++advance + pos < p->n && advance + cspace < len);
    for (int i = 2; i < 6; ++i)
        if (accu[i] == -1)
            accu[i] = 0;
    switch (accu[0]) {
    case 38:
        a->fg = attr_color(a->fg, accu[1], accu[3], accu[4], accu[5]);
        break;
    case 48:
        a->bg = attr_color(a->bg, accu[1], accu[3], accu[4], accu[5]);
        break;
    case 58: {
        uint32_t ul = attr_color(a->x.ext & SK_EXT_UNDERLINE_COLOR, accu[1], accu[3], accu[4], accu[5]);
        a->x.ext = (a->x.ext & ~SK_EXT_UNDERLINE_COLOR) | (ul & SK_EXT_UNDERLINE_COLOR);
        break;
    }
    }
    return advance;
}

static void set_underline(sk_cell_data *a, int32_t style)
{
    if (style == -1 || style > 5)
        style = 1;
    a->x.ext = (a->x.ext & ~SK_EXT_UNDERLINE_STYLE) | (((uint32_t)style << SK_EXT_UNDERLINE_SHIFT) & SK_EXT_UNDERLINE_STYLE);
    a->fg |= SK_FG_UNDERLINE;
    if (style == 0)
        a->fg &= ~SK_FG_UNDERLINE;
    update_extended(a);
}

static void sgr0(sk_cell_data *a)
{
    a->fg = 0;
    a->bg = 0;
    a->x.ext &= ~(SK_EXT_UNDERLINE_STYLE | SK_EXT_UNDERLINE_COLOR);
    update_extended(a);
}

static void char_attributes(sk_term *t, const sk_vt_params *p)
{
    sk_cell_data *a = &t->cur;
    if (p->n == 1 && p->v[0] == 0) {
        sgr0(a);
        return;
    }
    for (int i = 0; i < p->n; i++) {
        int32_t v = p->v[i];
        if (v >= 30 && v <= 37) {
            a->fg &= ~(SK_ATTR_CM_MASK | SK_ATTR_PCOLOR_MASK);
            a->fg |= SK_ATTR_CM_P16 | (uint32_t)(v - 30);
        } else if (v >= 40 && v <= 47) {
            a->bg &= ~(SK_ATTR_CM_MASK | SK_ATTR_PCOLOR_MASK);
            a->bg |= SK_ATTR_CM_P16 | (uint32_t)(v - 40);
        } else if (v >= 90 && v <= 97) {
            a->fg &= ~(SK_ATTR_CM_MASK | SK_ATTR_PCOLOR_MASK);
            a->fg |= SK_ATTR_CM_P16 | (uint32_t)(v - 90) | 8;
        } else if (v >= 100 && v <= 107) {
            a->bg &= ~(SK_ATTR_CM_MASK | SK_ATTR_PCOLOR_MASK);
            a->bg |= SK_ATTR_CM_P16 | (uint32_t)(v - 100) | 8;
        } else if (v == 0) {
            sgr0(a);
        } else if (v == 1) {
            a->fg |= SK_FG_BOLD;
        } else if (v == 3) {
            a->bg |= SK_BG_ITALIC;
        } else if (v == 4) {
            a->fg |= SK_FG_UNDERLINE;
            const int32_t *sub;
            set_underline(a, sk_vt_subs(p, i, &sub) ? sub[0] : SK_UNDERLINE_SINGLE);
        } else if (v == 5) {
            a->fg |= SK_FG_BLINK;
        } else if (v == 7) {
            a->fg |= SK_FG_INVERSE;
        } else if (v == 8) {
            a->fg |= SK_FG_INVISIBLE;
        } else if (v == 9) {
            a->fg |= SK_FG_STRIKETHROUGH;
        } else if (v == 2) {
            a->bg |= SK_BG_DIM;
        } else if (v == 21) {
            set_underline(a, SK_UNDERLINE_DOUBLE);
        } else if (v == 22) {
            a->fg &= ~SK_FG_BOLD;
            a->bg &= ~SK_BG_DIM;
        } else if (v == 23) {
            a->bg &= ~SK_BG_ITALIC;
        } else if (v == 24) {
            a->fg &= ~SK_FG_UNDERLINE;
            set_underline(a, SK_UNDERLINE_NONE);
        } else if (v == 25) {
            a->fg &= ~SK_FG_BLINK;
        } else if (v == 27) {
            a->fg &= ~SK_FG_INVERSE;
        } else if (v == 28) {
            a->fg &= ~SK_FG_INVISIBLE;
        } else if (v == 29) {
            a->fg &= ~SK_FG_STRIKETHROUGH;
        } else if (v == 39) {
            a->fg &= ~(SK_ATTR_CM_MASK | SK_ATTR_RGB_MASK);
        } else if (v == 49) {
            a->bg &= ~(SK_ATTR_CM_MASK | SK_ATTR_RGB_MASK);
        } else if (v == 38 || v == 48 || v == 58) {
            i += extract_color(p, i, a);
        } else if (v == 53) {
            a->bg |= SK_BG_OVERLINE;
        } else if (v == 55) {
            a->bg &= ~SK_BG_OVERLINE;
        } else if (v == 59) {
            a->x.ext |= SK_EXT_UNDERLINE_COLOR;
            update_extended(a);
        }
    }
}

/* Modes. */

static void set_mouse(sk_term *t, sk_mouse_protocol m)
{
    t->mouse = m;
    if (m != SK_MOUSE_NONE) {
        sk_select_reset(t);
        t->select_enabled = false;
    } else {
        t->select_enabled = true;
    }
}

static void report_focus(sk_term *t)
{
    reply(t, t->focused ? "\x1b[I" : "\x1b[O", 3);
}

static void modes_reset(sk_term *t)
{
    t->insert_mode = false;
    t->app_cursor = t->app_keypad = t->bracketed_paste = t->origin = false;
    t->reverse_wrap = t->send_focus = t->sync_output = false;
    t->wraparound = true;
    t->cursor_style = -1;
    t->cursor_blink = -1;
}

static void set_mode_private(sk_term *t, int32_t m, bool on)
{
    switch (m) {
    case 1:
        t->app_cursor = on;
        break;
    case 2:
        if (on)
            for (int g = 0; g < 4; g++)
                set_gcharset(t, g, NULL);
        break;
    case 6:
        t->origin = on;
        set_cursor(t, 0, 0);
        break;
    case 7:
        t->wraparound = on;
        break;
    case 12:
        t->opt_cursor_blink = on;
        break;
    case 45:
        t->reverse_wrap = on;
        break;
    case 66:
        t->app_keypad = on;
        break;
    case 9:
        set_mouse(t, on ? SK_MOUSE_X10 : SK_MOUSE_NONE);
        break;
    case 1000:
        set_mouse(t, on ? SK_MOUSE_VT200 : SK_MOUSE_NONE);
        break;
    case 1002:
        set_mouse(t, on ? SK_MOUSE_DRAG : SK_MOUSE_NONE);
        break;
    case 1003:
        set_mouse(t, on ? SK_MOUSE_ANY : SK_MOUSE_NONE);
        break;
    case 1004:
        t->send_focus = on;
        if (on)
            report_focus(t);
        break;
    case 1006:
        t->mouse_encoding = on ? SK_MOUSE_ENC_SGR : SK_MOUSE_ENC_DEFAULT;
        break;
    case 1016:
        t->mouse_encoding = on ? SK_MOUSE_ENC_SGR_PIXELS : SK_MOUSE_ENC_DEFAULT;
        break;
    case 25:
        t->cursor_hidden = !on;
        break;
    case 1048:
        if (on)
            save_cursor(t);
        else
            restore_cursor(t);
        break;
    case 1049:
    case 47:
    case 1047:
        if (on) {
            if (m == 1049)
                save_cursor(t);
            activate_alt(t, erase_attr(t));
        } else {
            activate_normal(t);
            if (m == 1049)
                restore_cursor(t);
        }
        t->cursor_initialized = true;
        break;
    case 2004:
        t->bracketed_paste = on;
        break;
    case 2026:
        t->sync_output = on;
        break;
    }
}

static void request_mode(sk_term *t, int32_t m, bool ansi)
{
    int v = 0;
    if (ansi) {
        v = m == 2 ? 4 : m == 4 ? (t->insert_mode ? 1 : 2) : m == 12 ? 3 : m == 20 ? (t->convert_eol ? 1 : 2) : 0;
    } else {
#define B2V(x) ((x) ? 1 : 2)
        switch (m) {
        case 1: v = B2V(t->app_cursor); break;
        case 3: v = 0; break;
        case 6: v = B2V(t->origin); break;
        case 7: v = B2V(t->wraparound); break;
        case 8: v = 3; break;
        case 9: v = B2V(t->mouse == SK_MOUSE_X10); break;
        case 12: v = B2V(t->opt_cursor_blink); break;
        case 25: v = B2V(!t->cursor_hidden); break;
        case 45: v = B2V(t->reverse_wrap); break;
        case 66: v = B2V(t->app_keypad); break;
        case 67: v = 4; break;
        case 1000: v = B2V(t->mouse == SK_MOUSE_VT200); break;
        case 1002: v = B2V(t->mouse == SK_MOUSE_DRAG); break;
        case 1003: v = B2V(t->mouse == SK_MOUSE_ANY); break;
        case 1004: v = B2V(t->send_focus); break;
        case 1005: v = 4; break;
        case 1006: v = B2V(t->mouse_encoding == SK_MOUSE_ENC_SGR); break;
        case 1015: v = 4; break;
        case 1016: v = B2V(t->mouse_encoding == SK_MOUSE_ENC_SGR_PIXELS); break;
        case 1048: v = 1; break;
        case 47:
        case 1047:
        case 1049: v = B2V(t->active == &t->alt); break;
        case 2004: v = B2V(t->bracketed_paste); break;
        case 2026: v = B2V(t->sync_output); break;
        }
#undef B2V
    }
    replyf(t, "\x1b[%s%d;%d$y", ansi ? "" : "?", (int)m, v);
}

static void soft_reset(sk_term *t)
{
    sk_buffer *b = t->active;
    t->cursor_hidden = false;
    b->scroll_top = 0;
    b->scroll_bottom = t->rows - 1;
    t->cur = default_attr;
    modes_reset(t);
    charsets_reset(t);
    b->saved_x = 0;
    b->saved_y = b->ybase;
    b->saved_fg = t->cur.fg;
    b->saved_bg = t->cur.bg;
    b->saved_charset = t->charset;
    t->origin = false;
}

static void full_reset(sk_term *t)
{
    sk_vt_reset(&t->vt);
    t->cur = default_attr;
    t->erase = default_attr;
    sk_buffer_free(&t->normal);
    sk_buffer_free(&t->alt);
    buffers_init(t);
    t->user_scrolling = false;
    charsets_reset(t);
    modes_reset(t);
    set_mouse(t, SK_MOUSE_NONE);
    t->mouse_encoding = SK_MOUSE_ENC_DEFAULT;
    sk_select_reset(t);
    t->all_dirty = true;
}

static void set_cursor_style(sk_term *t, const sk_vt_params *p)
{
    int32_t v = p->n == 0 ? 1 : p->v[0];
    if (v == 0) {
        t->cursor_style = -1;
        t->cursor_blink = -1;
        return;
    }
    if (v == 1 || v == 2)
        t->cursor_style = SK_CURSOR_BLOCK;
    else if (v == 3 || v == 4)
        t->cursor_style = SK_CURSOR_UNDERLINE;
    else if (v == 5 || v == 6)
        t->cursor_style = SK_CURSOR_BAR;
    t->cursor_blink = v % 2 == 1;
}

static void set_scroll_region(sk_term *t, const sk_vt_params *p)
{
    int64_t top = p->v[0] ? p->v[0] : 1;
    int64_t bottom;
    if (p->n < 2 || (bottom = p->v[1]) > t->rows || bottom == 0)
        bottom = t->rows;
    if (bottom > top) {
        t->active->scroll_top = (int)top - 1;
        t->active->scroll_bottom = (int)bottom - 1;
        set_cursor(t, 0, 0);
    }
}

#define ID(a, b) ((uint32_t)(a) << 8 | (uint32_t)(b))
#define ID3(a, b, c) ((uint32_t)(a) << 16 | (uint32_t)(b) << 8 | (uint32_t)(c))

static void csi(void *ud, uint32_t ident, const sk_vt_params *p)
{
    sk_term *t = ud;
    sk_buffer *b = t->active;
    int64_t n1 = p->v[0] ? p->v[0] : 1;
    switch (ident) {
    case '@':
        restrict_cursor(t, t->cols - 1);
        {
            sk_cell_data fill = null_of(erase_attr(t));
            sk_line_insert_cells(cur_line(t), b->x, count_of(n1, t->cols), &fill);
        }
        break;
    case ID(' ', '@'):
        shift_columns(t, 0, n1, false);
        break;
    case 'A': {
        int diff = b->y - b->scroll_top;
        move_cursor(t, 0, diff >= 0 ? -SK_MIN((int64_t)diff, n1) : -n1);
        break;
    }
    case ID(' ', 'A'):
        shift_columns(t, 0, n1, true);
        break;
    case 'B': {
        int diff = b->scroll_bottom - b->y;
        move_cursor(t, 0, diff >= 0 ? SK_MIN((int64_t)diff, n1) : n1);
        break;
    }
    case 'C':
    case 'a':
        move_cursor(t, n1, 0);
        break;
    case 'D':
        move_cursor(t, -n1, 0);
        break;
    case 'E': {
        int diff = b->scroll_bottom - b->y;
        move_cursor(t, 0, diff >= 0 ? SK_MIN((int64_t)diff, n1) : n1);
        t->active->x = 0;
        break;
    }
    case 'F': {
        int diff = b->y - b->scroll_top;
        move_cursor(t, 0, diff >= 0 ? -SK_MIN((int64_t)diff, n1) : -n1);
        t->active->x = 0;
        break;
    }
    case 'G':
    case '`':
        set_cursor(t, n1 - 1, b->y);
        break;
    case 'H':
    case 'f':
        set_cursor(t, p->n >= 2 ? (p->v[1] ? p->v[1] : 1) - 1 : 0, n1 - 1);
        break;
    case 'I':
        if (b->x < t->cols)
            for (int64_t k = SK_MIN(n1, (int64_t)t->cols); k > 0; k--)
                b->x = sk_buffer_next_stop(b, b->x);
        break;
    case 'J':
        erase_in_display(t, p->v[0], false);
        break;
    case ID('?', 'J'):
        erase_in_display(t, p->v[0], true);
        break;
    case 'K':
        erase_line_mode(t, p->v[0], false);
        break;
    case ID('?', 'K'):
        erase_line_mode(t, p->v[0], true);
        break;
    case 'L':
        insert_lines(t, n1);
        break;
    case 'M':
        delete_lines(t, n1);
        break;
    case 'P':
        restrict_cursor(t, t->cols - 1);
        {
            sk_cell_data fill = null_of(erase_attr(t));
            sk_line_delete_cells(cur_line(t), b->x, count_of(n1, t->cols), &fill);
        }
        break;
    case 'S':
        scroll_up(t, n1);
        break;
    case 'T':
        scroll_down(t, n1);
        break;
    case 'X':
        restrict_cursor(t, t->cols - 1);
        {
            sk_cell_data fill = null_of(erase_attr(t));
            sk_line_replace_cells(cur_line(t), b->x, (int)SK_MIN((int64_t)b->x + n1, (int64_t)t->cols + 1), &fill,
                                  false);
        }
        break;
    case 'Z':
        if (b->x < t->cols)
            for (int64_t k = SK_MIN(n1, (int64_t)t->cols); k > 0; k--)
                b->x = sk_buffer_prev_stop(b, b->x);
        break;
    case 'b':
        repeat(t, n1);
        break;
    case 'c':
        if (p->v[0] <= 0)
            reply(t, "\x1b[?1;2c", 7);
        break;
    case ID('>', 'c'):
        if (p->v[0] <= 0)
            reply(t, "\x1b[>0;276;0c", 11);
        break;
    case 'd':
        set_cursor(t, b->x, n1 - 1);
        break;
    case 'e':
        move_cursor(t, 0, n1);
        break;
    case 'g':
        if (p->v[0] == 0)
            sk_buffer_set_tab(b, b->x, false);
        else if (p->v[0] == 3)
            memset(b->tabs, 0, (size_t)b->tabs_cap);
        break;
    case 'h':
    case 'l':
        for (int i = 0; i < p->n; i++) {
            if (p->v[i] == 4)
                t->insert_mode = ident == 'h';
            else if (p->v[i] == 20)
                t->convert_eol = ident == 'h';
        }
        break;
    case ID('?', 'h'):
    case ID('?', 'l'):
        for (int i = 0; i < p->n; i++)
            set_mode_private(t, p->v[i], (ident & 0xFF) == 'h');
        break;
    case 'm':
        char_attributes(t, p);
        break;
    case 'n':
        if (p->v[0] == 5)
            reply(t, "\x1b[0n", 4);
        else if (p->v[0] == 6)
            replyf(t, "\x1b[%d;%dR", b->y + 1, b->x + 1);
        break;
    case ID('?', 'n'):
        if (p->v[0] == 6)
            replyf(t, "\x1b[?%d;%dR", b->y + 1, b->x + 1);
        break;
    case ID('!', 'p'):
        soft_reset(t);
        break;
    case ID(' ', 'q'):
        set_cursor_style(t, p);
        break;
    case 'r':
        set_scroll_region(t, p);
        break;
    case 's':
        save_cursor(t);
        break;
    case 'u':
        restore_cursor(t);
        break;
    case ID('\'', '}'):
        shift_columns(t, b->x, n1, true);
        break;
    case ID('\'', '~'):
        shift_columns(t, b->x, n1, false);
        break;
    case ID('"', 'q'):
        if (p->v[0] == 1)
            t->cur.bg |= SK_BG_PROTECTED;
        if (p->v[0] == 2 || p->v[0] == 0)
            t->cur.bg &= ~SK_BG_PROTECTED;
        break;
    case ID('$', 'p'):
        request_mode(t, p->v[0], true);
        break;
    case ID3('?', '$', 'p'):
        request_mode(t, p->v[0], false);
        break;
    }
}

static void esc(void *ud, uint32_t ident)
{
    sk_term *t = ud;
    switch (ident) {
    case '7':
        save_cursor(t);
        return;
    case '8':
        restore_cursor(t);
        return;
    case 'D':
        index_down(t);
        return;
    case 'E':
        t->active->x = 0;
        index_down(t);
        return;
    case 'H':
        tab_set(t);
        return;
    case 'M':
        reverse_index(t);
        return;
    case '=':
        t->app_keypad = true;
        return;
    case '>':
        t->app_keypad = false;
        return;
    case 'c':
        full_reset(t);
        return;
    case 'n':
    case '}':
        set_glevel(t, 2);
        return;
    case 'o':
    case '|':
        set_glevel(t, 3);
        return;
    case '~':
        set_glevel(t, 1);
        return;
    case ID('%', '@'):
    case ID('%', 'G'):
        set_glevel(t, 0);
        set_gcharset(t, 0, NULL);
        return;
    case ID('#', '8'): {
        sk_buffer *b = t->active;
        sk_cell_data e = {'E' | 1u << SK_CELL_WIDTH_SHIFT, t->cur.fg, t->cur.bg, {0, 0}, NULL};
        set_cursor(t, 0, 0);
        for (int k = 0; k < t->rows; ++k) {
            sk_line *l = sk_screen_line_at(b, b->ybase + b->y + k);
            if (l) {
                sk_line_fill(l, &e, false);
                l->wrapped = false;
            }
        }
        set_cursor(t, 0, 0);
        return;
    }
    }
    uint32_t inter = ident >> 8, flag = ident & 0xFF;
    const uint32_t *cs;
    if (inter > 0xFF || !charset_of(flag, &cs))
        return;
    switch (inter) {
    case '(':
        set_gcharset(t, 0, cs);
        break;
    case ')':
    case '-':
        set_gcharset(t, 1, cs);
        break;
    case '*':
    case '.':
        set_gcharset(t, 2, cs);
        break;
    case '+':
        set_gcharset(t, 3, cs);
        break;
    }
}

/* OSC. */

/* XParseColor's rgb: and # forms; false for anything else. */
static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool all_hex(sk_str s)
{
    for (size_t i = 0; i < s.len; i++)
        if (hexv(s.ptr[i]) < 0)
            return false;
    return s.len > 0;
}

static long hex_of(sk_str s)
{
    long v = 0;
    for (size_t i = 0; i < s.len; i++)
        v = v * 16 + hexv(s.ptr[i]);
    return v;
}

static bool parse_color(sk_str s, uint32_t *out)
{
    if (!s.len)
        return false;
    if (s.len >= 4 && sk_str_eq_nocase(sk_str_sub(s, 0, 4), SK_STR("rgb:"))) {
        sk_str rest = sk_str_sub(s, 4, SIZE_MAX);
        sk_str part[3];
        size_t at = 0;
        for (int i = 0; i < 3; i++) {
            size_t end = at;
            while (end < rest.len && rest.ptr[end] != '/')
                end++;
            if (i < 2 && end >= rest.len)
                return false;
            part[i] = sk_str_sub(rest, at, end - at);
            at = end + 1;
        }
        if (at <= rest.len)
            return false;
        size_t w = part[0].len;
        if (w < 1 || w > 4 || part[1].len != w || part[2].len != w)
            return false;
        for (int i = 0; i < 3; i++)
            if (!all_hex(part[i]))
                return false;
        double base = w == 1 ? 15 : w == 2 ? 255 : w == 3 ? 4095 : 65535;
        uint32_t v = 0;
        for (int i = 0; i < 3; i++) {
            double c = (double)hex_of(part[i]) / base * 255;
            v = v << 8 | ((uint32_t)(c + 0.5) & 255);
        }
        *out = v;
        return true;
    }
    if (s.ptr[0] == '#') {
        sk_str rest = sk_str_sub(s, 1, SIZE_MAX);
        if (!all_hex(rest) || (rest.len != 3 && rest.len != 6 && rest.len != 9 && rest.len != 12))
            return false;
        size_t adv = rest.len / 3;
        uint32_t v = 0;
        for (size_t i = 0; i < 3; i++) {
            long c = hex_of(sk_str_sub(rest, adv * i, adv));
            long r = adv == 1 ? c << 4 : adv == 2 ? c : adv == 3 ? c >> 4 : c >> 8;
            v = v << 8 | ((uint32_t)r & 255);
        }
        *out = v;
        return true;
    }
    return false;
}

static void report_color(sk_term *t, const char *ident, uint32_t rgb)
{
    unsigned r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
    replyf(t, "\x1b]%s;rgb:%02x%02x/%02x%02x/%02x%02x\x1b\\", ident, r, r, g, g, b, b);
}

static uint32_t *special_slot(sk_term_palette *p, int index)
{
    return index == SPECIAL_FG ? &p->fg : index == SPECIAL_BG ? &p->bg : &p->cursor;
}

static bool digits_only(sk_str s)
{
    if (!s.len)
        return false;
    for (size_t i = 0; i < s.len; i++)
        if (s.ptr[i] < '0' || s.ptr[i] > '9')
            return false;
    return true;
}

/* A /^\d+$/ index below 256, as parseInt reads it. */
static bool color_index(sk_str s, int *out)
{
    if (!digits_only(s))
        return false;
    size_t i = 0;
    while (i < s.len - 1 && s.ptr[i] == '0')
        i++;
    if (s.len - i > 3)
        return false;
    int v = 0;
    for (; i < s.len; i++)
        v = v * 10 + (s.ptr[i] - '0');
    if (v >= 256)
        return false;
    *out = v;
    return true;
}

/* data.split(sep): the next slot, and whether there was one. */
static bool split_by(sk_str *rest, bool *more, sk_str *slot, char sep)
{
    if (!*more)
        return false;
    size_t k = sk_str_find(*rest, sep);
    if (k == SIZE_MAX) {
        *slot = *rest;
        *more = false;
    } else {
        *slot = sk_str_sub(*rest, 0, k);
        *rest = sk_str_sub(*rest, k + 1, SIZE_MAX);
    }
    return true;
}

static bool split_next(sk_str *rest, bool *more, sk_str *slot)
{
    return split_by(rest, more, slot, ';');
}

static void osc_indexed(sk_term *t, sk_str data)
{
    sk_str slots[2], rest = data;
    bool more = true;
    /* Pairs; a lone last slot is ignored. */
    for (;;) {
        if (!split_next(&rest, &more, &slots[0]) || !split_next(&rest, &more, &slots[1]))
            break;
        int index;
        if (!color_index(slots[0], &index))
            continue;
        if (slots[1].len == 1 && slots[1].ptr[0] == '?') {
            char ident[16];
            snprintf(ident, sizeof ident, "4;%d", index);
            report_color(t, ident, t->pal.ansi[index]);
        } else {
            uint32_t rgb;
            if (parse_color(slots[1], &rgb)) {
                t->pal.ansi[index] = rgb;
                t->all_dirty = true;
            }
        }
    }
}

static void osc_special(sk_term *t, sk_str data, int offset)
{
    sk_str rest = data, slot;
    bool more = true;
    for (; split_next(&rest, &more, &slot); ++offset) {
        if (offset >= 3)
            break;
        int index = SPECIAL_FG + offset;
        if (slot.len == 1 && slot.ptr[0] == '?') {
            report_color(t, offset == 0 ? "10" : offset == 1 ? "11" : "12", *special_slot(&t->pal, index));
        } else {
            uint32_t rgb;
            if (parse_color(slot, &rgb)) {
                *special_slot(&t->pal, index) = rgb;
                t->all_dirty = true;
            }
        }
    }
}

static void osc_restore_indexed(sk_term *t, sk_str data)
{
    if (!data.len) {
        memcpy(t->pal.ansi, t->pal_restore.ansi, sizeof t->pal.ansi);
        t->all_dirty = true;
        return;
    }
    sk_str rest = data, slot;
    bool more = true;
    while (split_next(&rest, &more, &slot)) {
        int index;
        if (color_index(slot, &index)) {
            t->pal.ansi[index] = t->pal_restore.ansi[index];
            t->all_dirty = true;
        }
    }
}

static void finish_link(sk_term *t)
{
    t->cur.x.url = 0;
    update_extended(&t->cur);
}

static void osc_hyperlink(sk_term *t, sk_str data)
{
    size_t idx = sk_str_find(data, ';');
    if (idx == SIZE_MAX)
        return;
    sk_str params = sk_js_trim(sk_str_sub(data, 0, idx));
    sk_str uri = sk_str_sub(data, idx + 1, SIZE_MAX);
    if (!uri.len) {
        if (!params.len)
            finish_link(t);
        return;
    }
    if (t->cur.x.url)
        finish_link(t);
    char *id = NULL;
    sk_str rest = params, part;
    bool more = true;
    while (split_by(&rest, &more, &part, ':')) {
        if (part.len >= 3 && !memcmp(part.ptr, "id=", 3)) {
            if (part.len > 3)
                id = sk_xstrndup(part.ptr + 3, part.len - 3);
            break;
        }
    }
    char *u = sk_xstrndup(uri.ptr, uri.len);
    t->cur.x.url = register_link(t, id, u);
    update_extended(&t->cur);
    free(u);
    free(id);
}

static bool osc_wanted(void *ud, int64_t id)
{
    (void)ud;
    switch (id) {
    case 0: case 1: case 2: case 4: case 8: case 10: case 11: case 12:
    case 104: case 110: case 111: case 112:
        return true;
    }
    return false;
}

static void set_title(sk_term *t, const char *s)
{
    free(t->title);
    t->title = sk_xstrdup(s);
    if (t->cb.title)
        t->cb.title(t->cb.ud, s);
}

static void osc(void *ud, int64_t id, const char *data, size_t len)
{
    sk_term *t = ud;
    sk_str s = sk_str_n(data, len);
    switch (id) {
    case 0:
        set_title(t, data);
        free(t->icon);
        t->icon = sk_xstrdup(data);
        break;
    case 1:
        free(t->icon);
        t->icon = sk_xstrdup(data);
        break;
    case 2:
        set_title(t, data);
        break;
    case 4:
        osc_indexed(t, s);
        break;
    case 8:
        osc_hyperlink(t, s);
        break;
    case 10:
    case 11:
    case 12:
        osc_special(t, s, (int)id - 10);
        break;
    case 104:
        osc_restore_indexed(t, s);
        break;
    case 110:
    case 111:
    case 112:
        *special_slot(&t->pal, SPECIAL_FG + (int)id - 110) = *special_slot(&t->pal_restore, SPECIAL_FG + (int)id - 110);
        t->all_dirty = true;
        break;
    }
}

/* DCS: DECRQSS alone. */

static bool dcs_wanted(void *ud, uint32_t ident)
{
    (void)ud;
    return ident == ID('$', 'q');
}

static void dcs(void *ud, uint32_t ident, const sk_vt_params *p, const char *data, size_t len)
{
    sk_term *t = ud;
    (void)ident;
    (void)p;
    sk_str s = sk_str_n(data, len);
    sk_buffer *b = t->active;
    if (sk_str_eq_c(s, "\"q"))
        replyf(t, "\x1bP1$r%d\"q\x1b\\", (t->cur.bg & SK_BG_PROTECTED) ? 1 : 0);
    else if (sk_str_eq_c(s, "\"p"))
        replyf(t, "\x1bP1$r61;1\"p\x1b\\");
    else if (sk_str_eq_c(s, "r"))
        replyf(t, "\x1bP1$r%d;%dr\x1b\\", b->scroll_top + 1, b->scroll_bottom + 1);
    else if (sk_str_eq_c(s, "m"))
        replyf(t, "\x1bP1$r0m\x1b\\");
    else if (sk_str_eq_c(s, " q"))
        replyf(t, "\x1bP1$r%d q\x1b\\", 2 - (t->opt_cursor_blink ? 1 : 0));
    else
        replyf(t, "\x1bP0$r\x1b\\");
}

/* The public face. */

void sk_term_default_palette(sk_term_palette *out)
{
    static const uint32_t base[16] = {
        0x2e3436, 0xcc0000, 0x4e9a06, 0xc4a000, 0x3465a4, 0x75507b, 0x06989a, 0xd3d7cf,
        0x555753, 0xef2929, 0x8ae234, 0xfce94f, 0x729fcf, 0xad7fa8, 0x34e2e2, 0xeeeeec,
    };
    static const uint32_t v[6] = {0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
    memcpy(out->ansi, base, sizeof base);
    for (int i = 0; i < 216; i++)
        out->ansi[16 + i] = v[(i / 36) % 6] << 16 | v[(i / 6) % 6] << 8 | v[i % 6];
    for (int i = 0; i < 24; i++) {
        uint32_t c = (uint32_t)(8 + i * 10);
        out->ansi[232 + i] = c << 16 | c << 8 | c;
    }
    out->fg = 0xffffff;
    out->bg = 0x000000;
    out->cursor = 0xffffff;
}

sk_term *sk_term_new(int cols, int rows, const sk_term_callbacks *cb)
{
    sk_term *t = sk_xcalloc(1, sizeof *t);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&t->mu, &attr) != 0)
        sk_die("cannot make the terminal's lock");
    pthread_mutexattr_destroy(&attr);
    if (cb)
        t->cb = *cb;
    t->cols = SK_MAX(cols, MINIMUM_COLS);
    t->rows = SK_MAX(rows, MINIMUM_ROWS);
    t->listen = (sk_lines_listener){on_lines, t};
    t->cur = default_attr;
    t->erase = default_attr;
    t->next_link = 1;
    buffers_init(t);
    modes_reset(t);
    t->mouse = SK_MOUSE_NONE;
    t->select_enabled = true;
    sk_term_default_palette(&t->pal);
    t->pal_restore = t->pal;
    sk_vt_handler h = {
        .ud = t,
        .print = print,
        .execute = execute,
        .csi = csi,
        .esc = esc,
        .osc_wanted = osc_wanted,
        .dcs_wanted = dcs_wanted,
        .osc = osc,
        .dcs = dcs,
    };
    sk_vt_init(&t->vt, &h);
    t->all_dirty = true;
    return t;
}

void sk_term_free(sk_term *t)
{
    if (!t)
        return;
    sk_vt_free(&t->vt);
    sk_buffer_free(&t->normal);
    sk_buffer_free(&t->alt);
    for (size_t i = 0; i < t->links.len; i++)
        link_free(&t->links.ptr[i]);
    sk_vec_free(&t->links);
    free(t->title);
    free(t->icon);
    free(t->seen);
    pthread_mutex_destroy(&t->mu);
    free(t);
}

void sk_term_lock(sk_term *t)
{
    pthread_mutex_lock(&t->mu);
}

void sk_term_unlock(sk_term *t)
{
    pthread_mutex_unlock(&t->mu);
}

void sk_term_feed(sk_term *t, const char *data, size_t len)
{
    sk_term_lock(t);
    sk_vt_feed(&t->vt, data, len);
    sk_term_unlock(t);
}

void sk_term_resize(sk_term *t, int cols, int rows)
{
    sk_term_lock(t);
    if (cols != t->cols || rows != t->rows) {
        cols = SK_MAX(cols, MINIMUM_COLS);
        rows = SK_MAX(rows, MINIMUM_ROWS);
        bool rows_changed = rows != t->rows;
        t->cols = cols;
        t->rows = rows;
        sk_buffer_resize(&t->normal, cols, rows);
        sk_buffer_resize(&t->alt, cols, rows);
        sk_buffer_setup_tabs(&t->normal, cols);
        sk_buffer_setup_tabs(&t->alt, cols);
        if (rows_changed)
            sk_select_reset(t);
        t->all_dirty = true;
    }
    sk_term_unlock(t);
}

void sk_term_size(sk_term *t, int *cols, int *rows)
{
    sk_term_lock(t);
    *cols = t->cols;
    *rows = t->rows;
    sk_term_unlock(t);
}

void sk_term_row(sk_term *t, int y, sk_term_cell *out)
{
    sk_term_lock(t);
    sk_buffer *b = t->active;
    sk_line *l = sk_screen_line_at(b, b->ydisp + y);
    for (int x = 0; x < t->cols; x++) {
        sk_term_cell *c = &out[x];
        memset(c, 0, sizeof *c);
        if (!l || x >= l->len) {
            c->width = 1;
            continue;
        }
        const sk_cell *s = &l->cells[x];
        c->width = (int)(s->content >> SK_CELL_WIDTH_SHIFT);
        c->fg = s->fg;
        c->bg = s->bg;
        if (s->content & SK_CELL_COMBINED) {
            c->comb = l->comb[x] + 1;
            c->ncomb = l->comb[x][0];
        } else {
            c->cp = s->content & SK_CELL_CP_MASK;
        }
        if ((s->bg & SK_BG_HAS_EXTENDED) && l->ext) {
            c->ext = l->ext[x].ext;
            c->link = l->ext[x].url;
        }
    }
    sk_term_unlock(t);
}

sk_underline sk_term_cell_underline(const sk_term_cell *c)
{
    if (!(c->fg & SK_FG_UNDERLINE))
        return SK_UNDERLINE_NONE;
    if (!(c->bg & SK_BG_HAS_EXTENDED))
        return SK_UNDERLINE_SINGLE;
    if (c->link)
        return SK_UNDERLINE_DASHED;
    return (sk_underline)((c->ext & SK_EXT_UNDERLINE_STYLE) >> SK_EXT_UNDERLINE_SHIFT);
}

void sk_term_cursor_of(sk_term *t, sk_term_cursor *out)
{
    sk_term_lock(t);
    sk_buffer *b = t->active;
    out->x = b->x;
    out->y = b->ybase + b->y - b->ydisp;
    out->hidden = t->cursor_hidden;
    out->initialized = t->cursor_initialized;
    out->style = t->cursor_style < 0 ? SK_CURSOR_BLOCK : (sk_cursor_style)t->cursor_style;
    out->blink = t->cursor_blink < 0 ? t->opt_cursor_blink : t->cursor_blink == 1;
    sk_term_unlock(t);
}

void sk_term_input_modes_of(sk_term *t, sk_term_input_modes *out)
{
    sk_term_lock(t);
    out->app_cursor = t->app_cursor;
    out->app_keypad = t->app_keypad;
    out->bracketed_paste = t->bracketed_paste;
    out->focus_events = t->send_focus;
    out->alt_screen = t->active == &t->alt;
    out->mouse = t->mouse;
    out->mouse_encoding = t->mouse_encoding;
    sk_term_unlock(t);
}

bool sk_term_synchronized(sk_term *t)
{
    sk_term_lock(t);
    bool on = t->sync_output;
    sk_term_unlock(t);
    return on;
}

void sk_term_end_sync(sk_term *t)
{
    sk_term_lock(t);
    t->sync_output = false;
    sk_term_unlock(t);
}

void sk_term_set_focus(sk_term *t, bool focused)
{
    sk_term_lock(t);
    t->focused = focused;
    if (focused)
        t->cursor_initialized = true;
    sk_term_unlock(t);
}

const char *sk_term_link_uri(sk_term *t, uint32_t link)
{
    sk_term_lock(t);
    sk_link *l = link_find(t, link);
    const char *uri = l ? l->uri : NULL;
    sk_term_unlock(t);
    return uri;
}

const char *sk_term_link_id(sk_term *t, uint32_t link)
{
    sk_term_lock(t);
    sk_link *l = link_find(t, link);
    const char *id = l ? l->id_param : NULL;
    sk_term_unlock(t);
    return id;
}

void sk_term_set_palette(sk_term *t, const sk_term_palette *p)
{
    sk_term_lock(t);
    t->pal = *p;
    t->pal_restore = *p;
    t->all_dirty = true;
    sk_term_unlock(t);
}

void sk_term_palette_of(sk_term *t, sk_term_palette *out)
{
    sk_term_lock(t);
    *out = t->pal;
    sk_term_unlock(t);
}

void sk_term_viewport(sk_term *t, int *top, int *base, int *lines)
{
    sk_term_lock(t);
    *top = t->active->ydisp;
    *base = t->active->ybase;
    *lines = t->active->lines.len;
    sk_term_unlock(t);
}

void sk_term_scroll(sk_term *t, int disp)
{
    sk_term_lock(t);
    sk_buffer *b = t->active;
    if (disp < 0) {
        if (b->ydisp == 0) {
            sk_term_unlock(t);
            return;
        }
        t->user_scrolling = true;
    } else if ((int64_t)disp + b->ydisp >= b->ybase) {
        t->user_scrolling = false;
    }
    b->ydisp = (int)SK_MAX(SK_MIN((int64_t)b->ydisp + disp, (int64_t)b->ybase), (int64_t)0);
    sk_term_unlock(t);
}

void sk_term_scroll_to_bottom(sk_term *t)
{
    sk_term_lock(t);
    sk_term_scroll(t, t->active->ybase - t->active->ydisp);
    sk_term_unlock(t);
}

char *sk_term_line_text(sk_term *t, int line)
{
    sk_term_lock(t);
    sk_buf out = {0};
    sk_buffer *b = t->active;
    if (line >= 0 && line < b->lines.len)
        sk_line_text(sk_screen_line(b, line), &out, true, 0, sk_screen_line(b, line)->len);
    sk_buf_reserve(&out, 0);
    sk_term_unlock(t);
    return out.ptr;
}

bool sk_term_take_dirty(sk_term *t, bool *rows)
{
    sk_term_lock(t);
    sk_buffer *b = t->active;
    if (t->seen_rows != t->rows) {
        free(t->seen);
        t->seen = sk_xcalloc((size_t)t->rows, sizeof *t->seen);
        t->seen_rows = t->rows;
        t->all_dirty = true;
    }
    bool any = false;
    for (int y = 0; y < t->rows; y++) {
        sk_line *l = sk_screen_line_at(b, b->ydisp + y);
        sk_seen now = {(uintptr_t)l, l ? l->gen : 0};
        rows[y] = t->all_dirty || now.line != t->seen[y].line || now.gen != t->seen[y].gen;
        any |= rows[y];
        t->seen[y] = now;
    }
    t->all_dirty = false;
    sk_term_unlock(t);
    return any;
}
