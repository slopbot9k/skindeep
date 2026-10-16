/* Flex and grid containers.
 *
 * Flex is CSS Flexbox's algorithm for a single line (decision 0037
 * refuses wrapping): each item's base size from flex-basis, its main
 * size property or its content; the hypothetical size held by min and
 * max, the automatic minimum of section 4.5 among them; free space
 * shared by grow or by shrink weighted with the base size, items that
 * hit a limit frozen and the rest shared again (section 9.7); then the
 * cross sizes, stretch, the main axis's justify-content with auto
 * margins, and the cross axis's alignment with first baselines.
 *
 * Grid places items row by row into explicit columns, rows added from
 * grid-auto-rows, spans and line numbers kept; fr tracks share what
 * fixed tracks and gaps leave; items stretch over their area. */
#include "internal.h"

#include <string.h>

/* ---- Flex ---- */

typedef struct {
    uint32_t node;
    float base, hyp, min, max, main, cross;
    float m0, m1, c0, c1; /* margins: main start and end, cross start and end */
    bool auto0, auto1;    /* main margins that are auto */
    bool frozen;
    uint8_t align;
    bool cross_auto; /* its cross size is auto */
} item;

static float gap_of(const lay *L, uint32_t i, const sk_css_len *g, float base)
{
    float v = lay_len(L, i, g, KEY_NONE, base);
    return isnan(v) || v < 0 ? 0 : v;
}

static bool row_dir(uint8_t d)
{
    return d == SK_FLEX_ROW || d == SK_FLEX_ROW_REVERSE;
}

/* The width an item takes across a column when nothing stretches it:
 * its width, or as much of its content as fits. */
static float column_cross(lay *L, item *it, float avail)
{
    const sk_css_style *s = lay_style(L, it->node);
    float w = lay_size_w(L, it->node, &s->width, KEY_WIDTH, avail);
    if (!isnan(w))
        return lay_clamp_w(L, it->node, w, avail);
    if (it->align == SK_ALIGN_STRETCH && !isnan(avail) && s->margin[SK_LEFT].kind != SK_LEN_AUTO &&
        s->margin[SK_RIGHT].kind != SK_LEN_AUTO)
        return lay_clamp_w(L, it->node, avail - it->c0 - it->c1, avail);
    return lay_clamp_w(L, it->node, lay_fit_w(L, it->node, isnan(avail) ? NAN : avail - it->c0 - it->c1), avail);
}

/* The height an item's content takes at width w. */
static float content_h(lay *L, uint32_t c, float w)
{
    if (lay_is_svg(L, c))
        return (w - lay_bp_w(L, c)) / lay_svg_ratio(L, c) + lay_bp_h(L, c);
    return lay_contents(L, c, w, NAN);
}

static uint8_t item_align(const lay *L, uint32_t container, uint32_t c)
{
    uint8_t a = lay_style(L, c)->align_self;
    if (a == SK_ALIGN_AUTO)
        a = lay_style(L, container)->align_items;
    if (a == SK_ALIGN_NORMAL)
        a = SK_ALIGN_STRETCH;
    return a;
}

float lay_item_baseline(const lay *L, uint32_t i)
{
    float b = L->st[i].baseline;
    return isnan(b) ? L->st[i].h : b;
}

/* Notes how an absolute child of a flex or grid container would sit
 * alone in its content box. */
static void note_flex_static(lay *L, uint32_t i, uint32_t c, float cw, float ch, bool row, bool reverse,
                             uint8_t justify)
{
    const sk_lbox *b = &L->l->boxes[i];
    lstate *s = &L->st[c];
    s->flex_static = true;
    s->row = row;
    s->reverse = reverse;
    s->justify = justify;
    s->align = item_align(L, i, c);
    s->cx = b->bw[SK_LEFT] + b->pad[SK_LEFT];
    s->cy = b->bw[SK_TOP] + b->pad[SK_TOP];
    s->cw = cw;
    s->ch = isnan(ch) ? 0 : ch;
    s->sx = s->cx;
    s->sy = s->cy;
}

/* Section 9.7: the items' main sizes in a line main_size long. */
static void resolve(item *it, size_t n, float main_size, float gaps, const lay *L)
{
    float sum = gaps;
    for (size_t k = 0; k < n; k++)
        sum += it[k].hyp + it[k].m0 + it[k].m1;
    bool grow = sum < main_size;
    for (size_t k = 0; k < n; k++) {
        const sk_css_style *s = lay_style(L, it[k].node);
        float f = grow ? s->flex_grow : s->flex_shrink;
        it[k].frozen = f == 0 || (grow && it[k].base > it[k].hyp) || (!grow && it[k].base < it[k].hyp);
        it[k].main = it[k].hyp;
    }
    float initial = main_size - sum;
    for (int round = 0; round < 64; round++) {
        float used = gaps, factors = 0, scaled = 0;
        bool any = false;
        for (size_t k = 0; k < n; k++) {
            used += it[k].m0 + it[k].m1;
            if (it[k].frozen) {
                used += it[k].main;
                continue;
            }
            any = true;
            used += it[k].base;
            const sk_css_style *s = lay_style(L, it[k].node);
            factors += grow ? s->flex_grow : s->flex_shrink;
            scaled += s->flex_shrink * it[k].base;
        }
        if (!any)
            break;
        float room = main_size - used;
        if (factors < 1 && fabsf(initial * factors) < fabsf(room))
            room = initial * factors;
        float violation = 0;
        for (size_t k = 0; k < n; k++) {
            if (it[k].frozen)
                continue;
            const sk_css_style *s = lay_style(L, it[k].node);
            float t = it[k].base;
            if (grow && factors > 0)
                t += room * s->flex_grow / factors;
            else if (!grow && scaled > 0)
                t += room * (s->flex_shrink * it[k].base) / scaled;
            float c = fminf(fmaxf(t, it[k].min), it[k].max);
            violation += c - t;
            it[k].main = c;
        }
        for (size_t k = 0; k < n; k++) {
            if (it[k].frozen)
                continue;
            float t = it[k].main;
            if (violation == 0 || (violation > 0 && t <= it[k].min) || (violation < 0 && t >= it[k].max))
                it[k].frozen = true;
        }
        if (violation == 0)
            break;
    }
}

/* Where justify-content puts the first item and what it adds between
 * items, room being the free space over n items. */
static void justify(uint8_t j, bool reverse, float room, size_t n, float *start, float *between)
{
    *start = 0;
    *between = 0;
    if (reverse) {
        if (j == SK_ALIGN_START)
            j = SK_ALIGN_FLEX_END;
        else if (j == SK_ALIGN_END)
            j = SK_ALIGN_FLEX_START;
    }
    switch (j) {
    case SK_ALIGN_FLEX_END:
    case SK_ALIGN_END:
        *start = room;
        break;
    case SK_ALIGN_CENTER:
        *start = room / 2;
        break;
    case SK_ALIGN_SPACE_BETWEEN:
        if (n > 1 && room > 0)
            *between = room / (float)(n - 1);
        break;
    case SK_ALIGN_SPACE_AROUND:
        if (room > 0) {
            *between = room / (float)n;
            *start = *between / 2;
        } else {
            *start = room / 2;
        }
        break;
    case SK_ALIGN_SPACE_EVENLY:
        if (room > 0) {
            *between = room / (float)(n + 1);
            *start = *between;
        } else {
            *start = room / 2;
        }
        break;
    default:
        break;
    }
}

float lay_flex(lay *L, uint32_t i, float cw, float ch)
{
    const sk_css_style *s = lay_style(L, i);
    bool row = row_dir(s->flex_direction);
    bool reverse = s->flex_direction == SK_FLEX_ROW_REVERSE || s->flex_direction == SK_FLEX_COLUMN_REVERSE;
    float main_avail = row ? cw : ch, cross_avail = row ? ch : cw;
    float gap = gap_of(L, i, row ? &s->column_gap : &s->row_gap, row ? cw : ch);
    size_t n = 0;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next)
        n++;
    item *it = sk_xcalloc(n ? n : 1, sizeof *it);
    n = 0;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        if (lay_display(L, c) == SK_DISPLAY_NONE)
            continue;
        if (lay_out_of_flow(L, c)) {
            note_flex_static(L, i, c, cw, ch, row, reverse, s->justify_content);
            continue;
        }
        item *t = &it[n++];
        t->node = c;
        t->align = item_align(L, i, c);
        const sk_css_style *cs = lay_style(L, c);
        lay_edges(L, c, cw);
        const sk_lbox *b = &L->l->boxes[c];
        t->m0 = row ? b->margin[SK_LEFT] : b->margin[SK_TOP];
        t->m1 = row ? b->margin[SK_RIGHT] : b->margin[SK_BOTTOM];
        t->c0 = row ? b->margin[SK_TOP] : b->margin[SK_LEFT];
        t->c1 = row ? b->margin[SK_BOTTOM] : b->margin[SK_RIGHT];
        t->auto0 = cs->margin[row ? SK_LEFT : SK_TOP].kind == SK_LEN_AUTO;
        t->auto1 = cs->margin[row ? SK_RIGHT : SK_BOTTOM].kind == SK_LEN_AUTO;
        const sk_css_len *size = row ? &cs->width : &cs->height;
        int key = row ? KEY_WIDTH : KEY_HEIGHT;
        /* The base size: flex-basis, the main size property, the content. */
        float base = NAN;
        if (cs->flex_basis.kind == SK_LEN_PX)
            base = row ? lay_size_w(L, c, &cs->flex_basis, KEY_NONE, main_avail)
                       : lay_size_h(L, c, &cs->flex_basis, KEY_NONE, main_avail);
        if (isnan(base))
            base = row ? lay_size_w(L, c, size, key, main_avail) : lay_size_h(L, c, size, key, main_avail);
        float content = NAN;
        float cross_w = NAN;
        if (!row)
            cross_w = column_cross(L, t, cross_avail);
        if (isnan(base)) {
            if (row) {
                float ht = lay_size_h(L, c, &cs->height, KEY_HEIGHT, cross_avail);
                if (lay_is_svg(L, c) && !isnan(ht))
                    content = (ht - lay_bp_h(L, c)) * lay_svg_ratio(L, c) + lay_bp_w(L, c);
                else if (lay_is_svg(L, c) && t->align == SK_ALIGN_STRETCH && !isnan(cross_avail))
                    content = (cross_avail - t->c0 - t->c1 - lay_bp_h(L, c)) * lay_svg_ratio(L, c) + lay_bp_w(L, c);
                else {
                    float lo, hi;
                    lay_intrinsic(L, c, &lo, &hi);
                    content = hi;
                }
            } else {
                content = content_h(L, c, cross_w);
            }
            base = content;
        }
        t->base = base;
        /* The minimum: min-*, or auto's content-based minimum where
         * the item shows its overflow. */
        const sk_css_len *minl = row ? &cs->min_width : &cs->min_height;
        float min = row ? lay_size_w(L, c, minl, KEY_NONE, main_avail) : lay_size_h(L, c, minl, KEY_NONE, main_avail);
        if (isnan(min)) {
            uint8_t ov = row ? cs->overflow_x : cs->overflow_y;
            if (minl->kind == SK_LEN_AUTO && ov == SK_OVERFLOW_VISIBLE) {
                float m;
                if (row) {
                    float lo, hi;
                    lay_intrinsic(L, c, &lo, &hi);
                    m = lay_is_svg(L, c) && !isnan(content) ? content : lo;
                } else {
                    m = isnan(content) ? content_h(L, c, cross_w) : content;
                }
                float spec = row ? lay_size_w(L, c, size, key, main_avail) : lay_size_h(L, c, size, key, main_avail);
                if (!isnan(spec))
                    m = fminf(m, spec);
                min = m;
            } else {
                min = 0;
            }
        }
        const sk_css_len *maxl = row ? &cs->max_width : &cs->max_height;
        float max = row ? lay_size_w(L, c, maxl, KEY_NONE, main_avail) : lay_size_h(L, c, maxl, KEY_NONE, main_avail);
        if (isnan(max))
            max = INFINITY;
        float bp = row ? lay_bp_w(L, c) : lay_bp_h(L, c);
        t->min = fmaxf(fminf(min, max), bp);
        t->max = fmaxf(max, t->min);
        t->hyp = fminf(fmaxf(base, t->min), t->max);
        t->cross = cross_w;
    }
    float gaps = n > 1 ? gap * (float)(n - 1) : 0;
    float main_size = main_avail;
    if (isnan(main_size)) {
        main_size = gaps;
        for (size_t k = 0; k < n; k++)
            main_size += it[k].hyp + it[k].m0 + it[k].m1;
        main_size = fmaxf(main_size, 0);
    }
    resolve(it, n, main_size, gaps, L);
    /* Cross sizes. */
    float line = 0;
    for (size_t k = 0; k < n; k++) {
        item *t = &it[k];
        uint32_t c = t->node;
        const sk_css_style *cs = lay_style(L, c);
        if (row) {
            float h = lay_size_h(L, c, &cs->height, KEY_HEIGHT, cross_avail);
            t->cross_auto = isnan(h);
            if (isnan(h)) {
                if (lay_is_svg(L, c))
                    h = (t->main - lay_bp_w(L, c)) / lay_svg_ratio(L, c) + lay_bp_h(L, c);
                else
                    h = lay_contents(L, c, t->main, NAN);
            }
            t->cross = lay_clamp_h(L, c, h, cross_avail);
        } else {
            t->cross_auto = isnan(lay_size_w(L, c, &cs->width, KEY_WIDTH, cross_avail));
        }
        line = fmaxf(line, t->cross + t->c0 + t->c1);
    }
    if (!isnan(cross_avail))
        line = cross_avail;
    /* Stretch, then the final layout of each item. */
    for (size_t k = 0; k < n; k++) {
        item *t = &it[k];
        uint32_t c = t->node;
        const sk_css_style *cs = lay_style(L, c);
        bool auto_cross = cs->margin[row ? SK_TOP : SK_LEFT].kind == SK_LEN_AUTO ||
                          cs->margin[row ? SK_BOTTOM : SK_RIGHT].kind == SK_LEN_AUTO;
        if (t->align == SK_ALIGN_STRETCH && t->cross_auto && !auto_cross)
            t->cross = row ? lay_clamp_h(L, c, line - t->c0 - t->c1, line) : lay_clamp_w(L, c, line - t->c0 - t->c1, line);
        if (row)
            lay_contents(L, c, t->main, t->cross);
        else
            lay_contents(L, c, t->cross, t->main);
    }
    /* The main axis: auto margins, then justify-content. */
    float used = gaps;
    size_t autos = 0;
    for (size_t k = 0; k < n; k++) {
        used += it[k].main + it[k].m0 + it[k].m1;
        autos += it[k].auto0 + it[k].auto1;
    }
    float room = main_size - used;
    if (room > 0 && autos) {
        float each = room / (float)autos;
        for (size_t k = 0; k < n; k++) {
            if (it[k].auto0)
                it[k].m0 += each;
            if (it[k].auto1)
                it[k].m1 += each;
        }
        room = 0;
    }
    float start, between;
    justify(s->justify_content, reverse, room, n, &start, &between);
    /* Baselines of the items that align by them. */
    float max_base = -INFINITY;
    for (size_t k = 0; k < n; k++)
        if (it[k].align == SK_ALIGN_BASELINE && row)
            max_base = fmaxf(max_base, lay_item_baseline(L, it[k].node) + it[k].c0);
    const sk_lbox *b = &L->l->boxes[i];
    float cx = b->bw[SK_LEFT] + b->pad[SK_LEFT], cy = b->bw[SK_TOP] + b->pad[SK_TOP];
    float pos = start;
    lstate *me = &L->st[i];
    me->baseline = NAN;
    float first_base = NAN;
    for (size_t k = 0; k < n; k++) {
        item *t = &it[k];
        uint32_t c = t->node;
        float m = pos + t->m0;
        pos = m + t->main + t->m1 + gap + between;
        float main_at = reverse ? main_size - m - t->main : m;
        float cross_at;
        switch (t->align) {
        case SK_ALIGN_FLEX_END:
        case SK_ALIGN_END:
            cross_at = line - t->cross - t->c1;
            break;
        case SK_ALIGN_CENTER:
            cross_at = (line - (t->cross + t->c0 + t->c1)) / 2 + t->c0;
            break;
        case SK_ALIGN_BASELINE:
            cross_at = row ? max_base - lay_item_baseline(L, c) : t->c0;
            break;
        default:
            cross_at = t->c0;
            break;
        }
        lstate *cs = &L->st[c];
        cs->x = cx + (row ? main_at : cross_at);
        cs->y = cy + (row ? cross_at : main_at);
        /* The container's first baseline: a baseline-aligned item's,
         * else the first item's. */
        float ib = cs->baseline;
        if (!isnan(ib)) {
            if (isnan(me->baseline) && t->align == SK_ALIGN_BASELINE)
                me->baseline = cs->y + ib;
            if (isnan(first_base))
                first_base = cs->y + ib;
        }
    }
    if (isnan(me->baseline))
        me->baseline = first_base;
    free(it);
    return row ? line : main_size;
}

void lay_flex_widths(lay *L, uint32_t i, float *min_w, float *max_w)
{
    const sk_css_style *s = lay_style(L, i);
    bool row = row_dir(s->flex_direction);
    float gap = row ? gap_of(L, i, &s->column_gap, NAN) : 0;
    float lo = 0, hi = 0;
    size_t n = 0;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        if (lay_display(L, c) == SK_DISPLAY_NONE || lay_out_of_flow(L, c))
            continue;
        float a, z;
        lay_intrinsic(L, c, &a, &z);
        float m = lay_margins_w(L, c);
        if (row) {
            lo += a + m;
            hi += z + m;
        } else {
            lo = fmaxf(lo, a + m);
            hi = fmaxf(hi, z + m);
        }
        n++;
    }
    if (row && n > 1) {
        lo += gap * (float)(n - 1);
        hi += gap * (float)(n - 1);
    }
    *min_w = lo;
    *max_w = hi;
}

/* ---- Grid ---- */

typedef struct {
    uint32_t node;
    int col, row, cspan, rspan;
} cell;

/* A track's line: n from 1, negative from the end; 0 for none. */
static int line_of(const sk_css_line *l, int count)
{
    if (l->kind != SK_LINE_NUMBER || l->n == 0)
        return 0;
    return l->n > 0 ? l->n : count + 2 + l->n;
}

static int span_of(const sk_css_line *start, const sk_css_line *end, int at)
{
    if (start->kind == SK_LINE_SPAN && start->n > 0)
        return start->n;
    if (end->kind == SK_LINE_SPAN && end->n > 0)
        return end->n;
    if (end->kind == SK_LINE_NUMBER && at > 0) {
        int e = end->n;
        if (e > at)
            return e - at;
    }
    return 1;
}

static const sk_css_track *track(const sk_css_track *list, uint16_t n, const sk_css_track *automatic, int k)
{
    return k < n ? &list[k] : automatic;
}

/* Places the items: definite lines kept, the rest row by row. */
static size_t place_items(lay *L, uint32_t i, int ncols, cell *cells, int *nrows)
{
    const sk_css_style *s = lay_style(L, i);
    size_t n = 0;
    int cap_rows = 1;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next)
        if (lay_display(L, c) != SK_DISPLAY_NONE && !lay_out_of_flow(L, c))
            cap_rows++;
    cap_rows = (cap_rows + s->ngrid_rows) * 2 + 2;
    uint8_t *used = sk_xcalloc((size_t)cap_rows * (size_t)ncols, 1);
    int cur_r = 0, cur_c = 0, rows = s->ngrid_rows;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        if (lay_display(L, c) == SK_DISPLAY_NONE || lay_out_of_flow(L, c))
            continue;
        const sk_css_style *cs = lay_style(L, c);
        int c0 = line_of(&cs->grid_column[0], ncols), r0 = line_of(&cs->grid_row[0], rows);
        int cspan = span_of(&cs->grid_column[0], &cs->grid_column[1], c0);
        int rspan = span_of(&cs->grid_row[0], &cs->grid_row[1], r0);
        cspan = SK_MAX(1, SK_MIN(cspan, ncols));
        rspan = SK_MAX(1, SK_MIN(rspan, cap_rows / 2));
        int col, rw;
        if (c0 > 0 && r0 > 0) {
            col = SK_MIN(c0 - 1, ncols - cspan);
            rw = r0 - 1;
        } else {
            /* The next place where it fits, in its column when it names one. */
            col = c0 > 0 ? SK_MIN(c0 - 1, ncols - cspan) : cur_c;
            rw = r0 > 0 ? r0 - 1 : cur_r;
            for (;; ) {
                if (c0 <= 0 && col + cspan > ncols) {
                    col = 0;
                    rw++;
                }
                if (rw + rspan > cap_rows)
                    break;
                bool fits = true;
                for (int y = rw; y < rw + rspan && fits; y++)
                    for (int x = col; x < col + cspan && fits; x++)
                        fits = !used[(size_t)y * (size_t)ncols + (size_t)x];
                if (fits)
                    break;
                if (c0 > 0)
                    rw++;
                else
                    col++;
            }
        }
        if (rw < 0)
            rw = 0;
        if (rw + rspan > cap_rows)
            rw = cap_rows - rspan;
        for (int y = rw; y < rw + rspan; y++)
            for (int x = col; x < col + cspan; x++)
                used[(size_t)y * (size_t)ncols + (size_t)x] = 1;
        if (c0 <= 0 && r0 <= 0) {
            cur_r = rw;
            cur_c = col + cspan;
        }
        cells[n++] = (cell){c, col, rw, cspan, rspan};
        rows = SK_MAX(rows, rw + rspan);
    }
    free(used);
    *nrows = rows;
    return n;
}

/* Sizes tracks of a total length: px fixed, auto at the largest content
 * of its single-span items (given in need), fr sharing the rest. */
static void size_tracks(const sk_css_track *list, uint16_t nlist, const sk_css_track *automatic, int count,
                        float total, float gap, const float *need, float *out)
{
    float fixed = gap * (float)(count > 0 ? count - 1 : 0), frs = 0;
    for (int k = 0; k < count; k++) {
        const sk_css_track *t = track(list, nlist, automatic, k);
        if (t->kind == SK_TRACK_PX) {
            float v = sk_layout_len(&t->len, total);
            out[k] = isnan(v) ? 0 : fmaxf(0, v);
            fixed += out[k];
        } else if (t->kind == SK_TRACK_FR) {
            frs += t->fr;
            out[k] = 0;
        } else {
            out[k] = need[k];
            fixed += out[k];
        }
    }
    if (frs <= 0)
        return;
    float per;
    if (!isnan(total)) {
        per = fmaxf(0, total - fixed) / fmaxf(frs, 1);
    } else {
        /* An indefinite size: each fr as large as its items need. */
        per = 0;
        for (int k = 0; k < count; k++) {
            const sk_css_track *t = track(list, nlist, automatic, k);
            if (t->kind == SK_TRACK_FR && t->fr > 0)
                per = fmaxf(per, need[k] / t->fr);
        }
    }
    for (int k = 0; k < count; k++) {
        const sk_css_track *t = track(list, nlist, automatic, k);
        if (t->kind == SK_TRACK_FR)
            out[k] = per * t->fr;
    }
}

static float span_size(const float *sizes, int at, int span, float gap)
{
    float v = gap * (float)(span - 1);
    for (int k = at; k < at + span; k++)
        v += sizes[k];
    return v;
}

float lay_grid(lay *L, uint32_t i, float cw, float ch)
{
    const sk_css_style *s = lay_style(L, i);
    int ncols = SK_MAX(1, (int)s->ngrid_columns);
    float cgap = gap_of(L, i, &s->column_gap, cw), rgap = gap_of(L, i, &s->row_gap, ch);
    size_t cap = 1;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next) {
        cap++;
        if (lay_display(L, c) != SK_DISPLAY_NONE && lay_out_of_flow(L, c))
            note_flex_static(L, i, c, cw, ch, true, false, SK_ALIGN_START);
    }
    cell *cells = sk_xcalloc(cap, sizeof *cells);
    int nrows = 0;
    size_t n = place_items(L, i, ncols, cells, &nrows);
    nrows = SK_MAX(nrows, 1);
    float *need = sk_xcalloc((size_t)SK_MAX(ncols, nrows), sizeof *need);
    float *cols = sk_xcalloc((size_t)ncols, sizeof *cols), *rows = sk_xcalloc((size_t)nrows, sizeof *rows);
    for (size_t k = 0; k < n; k++)
        if (cells[k].cspan == 1) {
            float lo, hi;
            lay_intrinsic(L, cells[k].node, &lo, &hi);
            lay_edges(L, cells[k].node, cw);
            const sk_lbox *b = &L->l->boxes[cells[k].node];
            need[cells[k].col] = fmaxf(need[cells[k].col], hi + b->margin[SK_LEFT] + b->margin[SK_RIGHT]);
        }
    size_tracks(s->grid_columns, s->ngrid_columns, &s->grid_auto_columns, ncols, cw, cgap, need, cols);
    /* Rows: what each item's content needs at its columns' width. */
    memset(need, 0, (size_t)SK_MAX(ncols, nrows) * sizeof *need);
    for (size_t k = 0; k < n; k++) {
        uint32_t c = cells[k].node;
        lay_edges(L, c, cw);
        const sk_lbox *b = &L->l->boxes[c];
        float w = span_size(cols, cells[k].col, cells[k].cspan, cgap) - b->margin[SK_LEFT] - b->margin[SK_RIGHT];
        const sk_css_style *cs = lay_style(L, c);
        float sw = lay_size_w(L, c, &cs->width, KEY_WIDTH, w);
        if (!isnan(sw))
            w = sw;
        float h = lay_size_h(L, c, &cs->height, KEY_HEIGHT, NAN);
        if (isnan(h))
            h = content_h(L, c, lay_clamp_w(L, c, w, cw));
        if (cells[k].rspan == 1)
            need[cells[k].row] = fmaxf(need[cells[k].row], h + b->margin[SK_TOP] + b->margin[SK_BOTTOM]);
    }
    size_tracks(s->grid_rows, s->ngrid_rows, &s->grid_auto_rows, nrows, ch, rgap, need, rows);
    const sk_lbox *b = &L->l->boxes[i];
    float cx = b->bw[SK_LEFT] + b->pad[SK_LEFT], cy = b->bw[SK_TOP] + b->pad[SK_TOP];
    L->st[i].baseline = NAN;
    for (size_t k = 0; k < n; k++) {
        uint32_t c = cells[k].node;
        const sk_lbox *cb = &L->l->boxes[c];
        const sk_css_style *cs = lay_style(L, c);
        float x = cx + span_size(cols, 0, cells[k].col, cgap) + (cells[k].col ? cgap : 0);
        float y = cy + span_size(rows, 0, cells[k].row, rgap) + (cells[k].row ? rgap : 0);
        float aw = span_size(cols, cells[k].col, cells[k].cspan, cgap);
        float ah = span_size(rows, cells[k].row, cells[k].rspan, rgap);
        float w = lay_size_w(L, c, &cs->width, KEY_WIDTH, aw);
        w = lay_clamp_w(L, c, isnan(w) ? aw - cb->margin[SK_LEFT] - cb->margin[SK_RIGHT] : w, aw);
        float h = lay_size_h(L, c, &cs->height, KEY_HEIGHT, ah);
        uint8_t al = item_align(L, i, c);
        if (isnan(h) && al == SK_ALIGN_STRETCH)
            h = ah - cb->margin[SK_TOP] - cb->margin[SK_BOTTOM];
        if (!isnan(h))
            h = lay_clamp_h(L, c, h, ah);
        h = lay_contents(L, c, w, h);
        float room = ah - h - cb->margin[SK_TOP] - cb->margin[SK_BOTTOM];
        float dy = al == SK_ALIGN_CENTER ? room / 2 : al == SK_ALIGN_END || al == SK_ALIGN_FLEX_END ? room : 0;
        L->st[c].x = x + cb->margin[SK_LEFT];
        L->st[c].y = y + cb->margin[SK_TOP] + dy;
        if (isnan(L->st[i].baseline) && !isnan(L->st[c].baseline))
            L->st[i].baseline = L->st[c].y + L->st[c].baseline;
    }
    float height = span_size(rows, 0, nrows, rgap);
    free(cells);
    free(need);
    free(cols);
    free(rows);
    return height;
}

void lay_grid_widths(lay *L, uint32_t i, float *min_w, float *max_w)
{
    const sk_css_style *s = lay_style(L, i);
    int ncols = SK_MAX(1, (int)s->ngrid_columns);
    float gap = gap_of(L, i, &s->column_gap, NAN);
    float *lo = sk_xcalloc((size_t)ncols, sizeof *lo), *hi = sk_xcalloc((size_t)ncols, sizeof *hi);
    size_t cap = 1;
    for (uint32_t c = L->nodes[i].first; c != SK_PACK_NONE; c = L->nodes[c].next)
        cap++;
    cell *cells = sk_xcalloc(cap, sizeof *cells);
    int nrows;
    size_t n = place_items(L, i, ncols, cells, &nrows);
    for (size_t k = 0; k < n; k++) {
        float a, z;
        lay_intrinsic(L, cells[k].node, &a, &z);
        int span = cells[k].cspan;
        for (int c = cells[k].col; c < cells[k].col + span; c++) {
            lo[c] = fmaxf(lo[c], a / (float)span);
            hi[c] = fmaxf(hi[c], z / (float)span);
        }
    }
    float a = gap * (float)(ncols - 1), z = a;
    for (int c = 0; c < ncols; c++) {
        const sk_css_track *t = track(s->grid_columns, s->ngrid_columns, &s->grid_auto_columns, c);
        if (t->kind == SK_TRACK_PX) {
            float v = sk_layout_len(&t->len, NAN);
            if (!isnan(v))
                lo[c] = hi[c] = v;
        }
        a += lo[c];
        z += hi[c];
    }
    free(lo);
    free(hi);
    free(cells);
    *min_w = a;
    *max_w = z;
}
