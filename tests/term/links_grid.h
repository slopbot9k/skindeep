/* A buffer for the link tests and the links fuzz target, written as
 * xterm.js's InputHandler.print fills one: a wide character that does
 * not fit wraps early and leaves the last cell empty, a full row wraps
 * on the next character, a zero-width character joins the cell before.
 * CR goes to column 0, LF to a new row that does not wrap. */
#ifndef SK_TEST_LINKS_GRID_H
#define SK_TEST_LINKS_GRID_H

#include "../../src/base/utf8.h"
#include "../../src/term/links.h"
#include "../../src/term/width.h"

#include <string.h>

#define GRID_CELL_MAX 8

typedef struct {
    uint32_t cp[GRID_CELL_MAX];
    int n, width;
} grid_cell;

typedef struct {
    grid_cell *cells;
    bool wrapped;
} grid_row;

typedef struct {
    int cols, x;
    SK_VEC(grid_row) rows;
} grid;

static inline void grid_new_row(grid *g, bool wrapped)
{
    grid_row r = {sk_xcalloc((size_t)g->cols, sizeof(grid_cell)), wrapped};
    for (int i = 0; i < g->cols; i++)
        r.cells[i].width = 1;
    sk_vec_push(&g->rows, r);
}

static inline void grid_init(grid *g, int cols)
{
    *g = (grid){.cols = cols};
    grid_new_row(g, false);
}

static inline void grid_free(grid *g)
{
    for (size_t i = 0; i < g->rows.len; i++)
        free(g->rows.ptr[i].cells);
    sk_vec_free(&g->rows);
}

static inline void grid_print(grid *g, uint32_t cp)
{
    grid_row *r = &g->rows.ptr[g->rows.len - 1];
    if (cp == '\r') {
        g->x = 0;
        return;
    }
    if (cp == '\n') {
        grid_new_row(g, false);
        return;
    }
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
        return;
    int w = sk_term_wcwidth(cp);
    if (w == 0) {
        if (g->x == 0)
            return;
        int at = g->x - 1;
        if (at > 0 && r->cells[at].width == 0)
            at--;
        grid_cell *c = &r->cells[at];
        if (c->n && c->n < GRID_CELL_MAX)
            c->cp[c->n++] = cp;
        return;
    }
    if (w > g->cols)
        return;
    if (g->x + w > g->cols) {
        for (int i = g->x; i < g->cols; i++)
            r->cells[i] = (grid_cell){{0}, 0, 1};
        grid_new_row(g, true);
        r = &g->rows.ptr[g->rows.len - 1];
        g->x = 0;
    }
    r->cells[g->x] = (grid_cell){{cp}, 1, w};
    if (w == 2)
        r->cells[g->x + 1] = (grid_cell){{0}, 0, 0};
    g->x += w;
}

static inline void grid_write(grid *g, const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, NULL);
        grid_print(g, cp);
    }
}

static inline bool grid_row_fn(void *ud, int y, int *cols, bool *wrapped)
{
    grid *g = ud;
    if (y < 0 || (size_t)y >= g->rows.len)
        return false;
    *cols = g->cols;
    *wrapped = g->rows.ptr[y].wrapped;
    return true;
}

static inline int grid_cell_fn(void *ud, int y, int x, uint32_t *cp, int max, int *width)
{
    grid *g = ud;
    const grid_cell *c = &g->rows.ptr[y].cells[x];
    *width = c->width;
    for (int i = 0; i < c->n && i < max; i++)
        cp[i] = c->cp[i];
    return c->n;
}

static inline sk_term_link_src grid_src(grid *g)
{
    return (sk_term_link_src){g, grid_row_fn, grid_cell_fn};
}

#endif
