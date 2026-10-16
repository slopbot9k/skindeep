/* The terminal: any bytes, fed in any pieces into a small terminal that
 * is now and then resized, scrolled or selected in, keep the screen
 * whole: the cursor in bounds, the buffers' counts consistent, every
 * line as wide as the screen, every combined cell with its text, every
 * line referenced by the slots that hold it and freed with the last.
 *
 * The first two bytes size the terminal. The rest is fed to it, except
 * that 0xFE, which UTF-8 never holds, ends a feed and starts a command:
 * a byte saying what, and two bytes for it. */
#include "../../src/term/screen.h"
#include "fuzz.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "\x1b[", "\x1b]", "\x1bP", "\x1b\\", "\x07", "\r\n", "\x1b[?1049h", "\x1b[?1049l", "\x1b[?47h",
    "\x1b[?1047l", "\x1b[?25l", "\x1b[?2004h", "\x1b[?1000h", "\x1b[?1006h", "\x1b[?2026h", "\x1b[2J",
    "\x1b[3J", "\x1b[K", "\x1b[1;5r", "\x1b[r", "\x1b[?6h", "\x1b[?7l", "\x1b[4h", "\x1b[?45h", "\x1bM",
    "\x1b" "D", "\x1b" "E", "\x1b" "7", "\x1b" "8", "\x1b" "c", "\x1b[!p", "\x1b#8", "\x1b(0", "\x1b)0",
    "\x0e", "\x0f", "\x1b[38;2;1;2;3m", "\x1b[48;5;9m", "\x1b[4:3m", "\x1b[58;5;3m", "\x1b[59m",
    "\x1b]8;;http://x/\x07", "\x1b]8;id=a;http://y/\x1b\\", "\x1b]8;;\x07", "\x1b]4;1;?\x07",
    "\x1b]10;#123\x07", "\x1b]104\x07", "\x1bP$qm\x1b\\", "\x1b[6n", "\x1b[c", "\x1b[?25$p", "\x1b[5b",
    "\x1b[2L", "\x1b[3M", "\x1b[2@", "\x1b[4P", "\x1b[3X", "\x1b[2S", "\x1b[2T", "\x1b[2 @", "\x1b[2 A",
    "\x1b['}", "\x1b['~", "\x1b[1\"q", "\x1b[?2J", "\x1b[3 q", "\x1b[H", "\x1b[99;99H", "\x1b[g",
    "\x1bH", "\t", "\b", "\xE6\x97\xA5", "\xF0\x9F\x98\x80", "\xCC\x81", "\xEF\xB8\x8F", "\xE2\x94\x80",
    "\xC2\x9B", "\xC2\x84", "\xC2\x85", "\xC2\x9D", "\x1b_", "\x1b[2147483647b", "\xFE\x00",
    "\xFE\x01", "\xFE\x02", "\xFE\x03", "\xFE\x04", NULL,
};

/* The broken invariant, named before the abort the driver catches. */
static void need(bool ok, const char *what)
{
    if (ok)
        return;
    fprintf(stderr, "fuzz-term: %s\n", what);
    abort();
}

static void check(sk_term *t)
{
    sk_buffer *bufs[2] = {&t->normal, &t->alt};
    for (int k = 0; k < 2; k++) {
        sk_buffer *b = bufs[k];
        sk_lines *r = &b->lines;
        need(r->len >= 0 && r->len <= r->max, "ring length");
        need(b->cols == t->cols && b->rows == t->rows, "buffer size");
        need(b->scroll_top >= 0 && b->scroll_top <= b->scroll_bottom && b->scroll_bottom < t->rows, "scroll region");
        for (int i = 0; i < r->len; i++) {
            sk_line *l = sk_lines_get(r, i);
            need(l != NULL, "hole in the ring");
            /* The alternate buffer keeps cells past a narrowing. */
            need(b == &t->normal ? l->len == t->cols : l->len >= t->cols, "line width");
            for (int x = 0; x < l->len; x++) {
                uint32_t c = l->cells[x].content;
                bool combined = c & SK_CELL_COMBINED;
                need(combined == (l->comb && l->comb[x]), "combined text");
                need((c >> SK_CELL_WIDTH_SHIFT) <= 2, "cell width");
            }
        }
    }
    sk_buffer *b = t->active;
    need(b->lines.len >= t->rows, "fewer lines than rows");
    need(b->x >= 0 && b->x <= t->cols + 1 && b->y >= 0 && b->y < t->rows, "cursor");
    /* The screen may run past the lines only in a full ring, whose
     * indexes wrap: the alternate buffer keeps a larger ring when it
     * shrinks empty, as xterm.js's does, and outgrows it later. */
    need(b->ybase >= 0 && b->ydisp >= 0 && b->ydisp <= b->ybase, "viewport");
    need(b->ybase + t->rows <= b->lines.len || b->lines.len == b->lines.max, "screen past the lines");
    need(!t->alt.lines.len || t->active == &t->alt, "alternate buffer left full");
}

static int by_address(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)*(sk_line *const *)a, y = (uintptr_t)*(sk_line *const *)b;
    return (x > y) - (x < y);
}

/* Each line's references are the slots that hold it, and the lines
 * alive beyond those there were before are the rings' lines. */
static void check_lines(sk_term *t, long before)
{
    sk_lines *rings[2] = {&t->normal.lines, &t->alt.lines};
    size_t n = 0, cap = (size_t)(rings[0]->max + rings[1]->max);
    sk_line **all = sk_xcalloc(cap, sizeof *all);
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < rings[k]->max; i++)
            if (rings[k]->v[i])
                all[n++] = rings[k]->v[i];
    qsort(all, n, sizeof *all, by_address);
    long distinct = 0;
    for (size_t i = 0; i < n;) {
        size_t run = 1;
        while (i + run < n && all[i + run] == all[i])
            run++;
        need(all[i]->refs == (int)run, "line references");
        distinct++;
        i += run;
    }
    free(all);
    need(sk_line_alive() - before == distinct, "lines leaked");
}

static void on_reply(void *ud, const char *data, size_t len)
{
    (void)ud;
    if (!len || !data)
        abort();
}

static void command(sk_term *t, uint8_t op, uint8_t a, uint8_t b)
{
    switch (op % 8) {
    case 0:
        sk_term_resize(t, a % 40, b % 16);
        break;
    case 1:
        sk_term_scroll(t, (int)(a % 21) - 10);
        break;
    case 2:
        sk_term_select_start(t, (sk_select_kind)(op / 8 % 4), a % 42, b % 16);
        break;
    case 3:
        sk_term_select_move(t, a % 42 - 1, (int)(b % 18) - 1);
        free(sk_term_selection_text(t));
        break;
    case 4:
        sk_term_select_drag_scroll(t, (int)(a % 7) - 3);
        free(sk_term_selection_text(t));
        break;
    case 5:
        sk_term_set_focus(t, a & 1);
        break;
    case 6:
        sk_term_select_all(t);
        free(sk_term_selection_text(t));
        break;
    default:
        sk_term_end_sync(t);
        break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;
    sk_term_callbacks cb = {NULL, on_reply, NULL, NULL};
    long before = sk_line_alive();
    sk_term *t = sk_term_new(2 + data[0] % 30, 1 + data[1] % 12, &cb);
    size_t i = 2;
    while (i < size) {
        const uint8_t *mark = memchr(data + i, 0xFE, size - i);
        size_t n = mark ? (size_t)(mark - (data + i)) : size - i;
        sk_term_feed(t, (const char *)data + i, n);
        i += n;
        check(t);
        if (mark) {
            uint8_t op = i + 1 < size ? data[i + 1] : 0;
            uint8_t a = i + 2 < size ? data[i + 2] : 0;
            uint8_t b = i + 3 < size ? data[i + 3] : 0;
            command(t, op, a, b);
            i = SK_MIN(i + 4, size);
            check(t);
        }
    }
    free(sk_term_screen_text(t));
    int cols, rows;
    sk_term_size(t, &cols, &rows);
    bool *dirty = sk_xcalloc((size_t)rows, sizeof *dirty);
    sk_term_take_dirty(t, dirty);
    free(dirty);
    sk_term_cell *row = sk_xcalloc((size_t)cols, sizeof *row);
    for (int y = 0; y < rows; y++)
        sk_term_row(t, y, row);
    free(row);
    check_lines(t, before);
    sk_term_free(t);
    need(sk_line_alive() == before, "lines left after the free");
    return 0;
}
