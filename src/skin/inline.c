/* Inline content: a block container's text and inline boxes on lines.
 *
 * The content becomes atoms, one per character drawn, each with its
 * glyph, its advance (letter-spacing and the kerning with the next
 * glyph of its face included) and whether a line may break after it;
 * an atomic inline box is one atom, a float an atom of no width. White
 * space collapses as white-space says, across elements. Lines take
 * atoms greedily up to the last break that fits; spaces at a line's end
 * hang. A line's height is CSS 2.1's: every inline box on it, the
 * container's strut among them, aligned on the baseline with half the
 * leading above and half below, the font's ascent and descent rounded
 * to whole pixels as browsers round them. */
#include "internal.h"

#include "../base/str.h"
#include "../base/utf8.h"

#include <string.h>

enum { A_GLYPH, A_BOX, A_FLOAT, A_BREAK };

typedef struct {
    uint8_t kind;
    bool space; /* a collapsible space */
    bool brk;   /* a line may break after it */
    uint32_t node;
    sk_glyph_ref ref;
    float px, adv;
} atom;

typedef struct {
    SK_VEC(atom) a;
    bool last_space; /* the last atom collapses a space after it */
} atoms;

/* ---- Characters ---- */

static uint32_t upper(uint32_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return c - 32;
    if (c == 0xFF)
        return 0x178;
    return c;
}

static uint32_t lower(uint32_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 32;
    return c;
}

/* The glyph for cp: the style's face, then the embedded faces. */
static sk_glyph_ref lookup(const sk_font *face, uint8_t synth, uint32_t cp, bool mono)
{
    sk_glyph_ref r = {face, sk_font_glyph(face, cp), synth};
    if (r.glyph || cp < 0x20)
        return r;
    static const sk_embedded_font ORDER[2][2] = {{SK_FONT_SANS, SK_FONT_MONO}, {SK_FONT_MONO, SK_FONT_SANS}};
    for (int k = 0; k < 2; k++) {
        const sk_font *f = sk_font_embedded(ORDER[mono][k]);
        uint32_t g = f ? sk_font_glyph(f, cp) : 0;
        if (g) {
            sk_glyph_ref out = {f, g, synth};
            return out;
        }
    }
    return r;
}

/* Whether a character the style's face lacks comes from DejaVu Sans
 * Mono first: its face is the mono one, or its stack names monospace,
 * as a browser falls back along the stack to its monospace face. */
static bool prefers_mono(const sk_css_style *s)
{
    if (s->font < 4)
        return true;
    for (uint16_t i = 0; i < s->nfont_family; i++) {
        const char *f = s->font_family[i];
        if (f && (!strcmp(f, "monospace") || !strcmp(f, "ui-monospace")))
            return true;
    }
    return false;
}

void lay_line_metrics(const lay *L, uint32_t i, float *ascent, float *descent, float *line_height)
{
    const sk_css_style *s = lay_style(L, i);
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(sk_layout_face(L->fonts, s), s->font_size, &vm);
    float a = roundf(vm.ascent), d = roundf(vm.descent);
    *ascent = a;
    *descent = d;
    switch (s->line_height_kind) {
    case SK_LH_NUMBER:
        *line_height = s->line_height * s->font_size;
        break;
    case SK_LH_PX:
        *line_height = s->line_height;
        break;
    default:
        *line_height = a + d + roundf(vm.line_gap);
        break;
    }
}

/* n bytes of text in owner's style, as atoms. */
static void add_text(lay *L, atoms *A, uint32_t owner, const char *text, size_t n)
{
    const sk_css_style *s = lay_style(L, owner);
    const sk_font *face = sk_layout_face(L->fonts, s);
    bool mono = prefers_mono(s);
    uint8_t ws = s->white_space;
    float px = s->font_size, ls = s->letter_spacing;
    if (!isfinite(ls))
        ls = 0;
    bool word_start = true;
    size_t first = A->a.len;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(text + i, n - i, &cp, NULL);
        if (ws != SK_WS_PRE && (cp == '\n' || cp == '\t' || cp == '\r' || cp == '\f'))
            cp = ' ';
        atom a;
        memset(&a, 0, sizeof a);
        a.node = owner;
        a.px = px;
        if (cp == '\n') {
            a.kind = A_BREAK;
            sk_vec_push(&A->a, a);
            A->last_space = true;
            word_start = true;
            continue;
        }
        if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
            continue;
        if (cp == ' ' && ws != SK_WS_PRE) {
            if (A->last_space)
                continue;
            a.space = true;
            a.brk = ws == SK_WS_NORMAL;
        }
        A->last_space = a.space;
        switch (s->text_transform) {
        case SK_TT_UPPERCASE:
            cp = upper(cp);
            break;
        case SK_TT_LOWERCASE:
            cp = lower(cp);
            break;
        case SK_TT_CAPITALIZE:
            if (word_start)
                cp = upper(cp);
            break;
        default:
            break;
        }
        word_start = cp == ' ';
        a.kind = A_GLYPH;
        a.ref = lookup(face, s->font_synth, cp, mono);
        a.adv = sk_font_advance(a.ref.font, a.ref.glyph, px) + ls;
        if (A->a.len > first) {
            atom *p = &A->a.ptr[A->a.len - 1];
            if (p->kind == A_GLYPH && p->ref.font == a.ref.font)
                p->adv += sk_font_kern(a.ref.font, p->ref.glyph, a.ref.glyph, px);
        }
        sk_vec_push(&A->a, a);
    }
}

/* The text of a ::before or ::after: its content's parts. */
static void content_text(const lay *L, uint32_t i, sk_buf *out)
{
    const sk_css_style *s = lay_style(L, i);
    for (uint16_t k = 0; k < s->ncontent; k++) {
        const sk_css_content *c = &s->content[k];
        if (c->kind == SK_CONTENT_TEXT && c->text)
            sk_buf_append_c(out, c->text);
        else if (c->kind == SK_CONTENT_ORDINAL)
            sk_buf_printf(out, "%ld", (long)c->base + (long)c->step * (long)L->nodes[i].row);
    }
}

static void add_own_text(lay *L, atoms *A, uint32_t i)
{
    const sk_lnode *n = &L->nodes[i];
    if (n->text) {
        add_text(L, A, i, n->text, strlen(n->text));
    } else if (n->kind == SK_EL_BEFORE || n->kind == SK_EL_AFTER) {
        sk_buf b = {0};
        content_text(L, i, &b);
        if (b.len)
            add_text(L, A, i, b.ptr, b.len);
        sk_buf_free(&b);
    }
}

static bool white_space_wraps(const lay *L, uint32_t i)
{
    return lay_style(L, i)->white_space == SK_WS_NORMAL;
}

static void add_item(lay *L, atoms *A, uint32_t b, uint32_t c);

/* An inline box: its ::before, its text, its children, its ::after. */
static void add_inline(lay *L, atoms *A, uint32_t c)
{
    uint32_t k = L->nodes[c].first;
    if (k != SK_PACK_NONE && L->nodes[k].kind == SK_EL_BEFORE) {
        add_item(L, A, c, k);
        k = L->nodes[k].next;
    }
    add_own_text(L, A, c);
    for (; k != SK_PACK_NONE; k = L->nodes[k].next)
        add_item(L, A, c, k);
}

static void add_item(lay *L, atoms *A, uint32_t b, uint32_t c)
{
    if (c == LAY_OWN_TEXT) {
        add_own_text(L, A, b);
        return;
    }
    uint8_t d = lay_display(L, c);
    if (d == SK_DISPLAY_NONE)
        return;
    atom a;
    memset(&a, 0, sizeof a);
    a.node = c;
    if (lay_out_of_flow(L, c)) {
        /* Its static position: the content box's left, the line's top;
         * near enough for a box that sets its own insets. */
        const sk_lbox *bx = &L->l->boxes[b];
        L->st[c].sx = bx->bw[SK_LEFT] + bx->pad[SK_LEFT];
        L->st[c].sy = bx->bw[SK_TOP] + bx->pad[SK_TOP];
        L->st[c].flex_static = false;
        return;
    }
    if (lay_style(L, c)->float_ != SK_FLOAT_NONE) {
        a.kind = A_FLOAT;
        sk_vec_push(&A->a, a);
        return;
    }
    if (d == SK_DISPLAY_INLINE && !lay_is_svg(L, c)) {
        add_inline(L, A, c);
        return;
    }
    /* An atomic inline: breaks around it where the text wraps. */
    a.kind = A_BOX;
    a.brk = white_space_wraps(L, b);
    if (A->a.len && white_space_wraps(L, b))
        A->a.ptr[A->a.len - 1].brk = true;
    A->last_space = false;
    sk_vec_push(&A->a, a);
}

static void build(lay *L, atoms *A, uint32_t b, const uint32_t *items, size_t n)
{
    memset(A, 0, sizeof *A);
    A->last_space = true;
    for (size_t k = 0; k < n; k++)
        add_item(L, A, b, items[k]);
}

/* ---- Atomic and floating boxes ---- */

/* Sizes an atomic inline or a float laid out in a line cw wide. */
static void size_box(lay *L, uint32_t c, float cw)
{
    const sk_css_style *s = lay_style(L, c);
    lay_edges(L, c, cw);
    sk_lbox *b = &L->l->boxes[c];
    float w = lay_size_w(L, c, &s->width, KEY_WIDTH, cw);
    if (isnan(w))
        w = lay_fit_w(L, c, cw - b->margin[SK_LEFT] - b->margin[SK_RIGHT]);
    w = lay_clamp_w(L, c, w, cw);
    float h = lay_size_h(L, c, &s->height, KEY_HEIGHT, NAN);
    if (isnan(h) && lay_is_svg(L, c))
        h = (w - lay_bp_w(L, c)) / lay_svg_ratio(L, c) + lay_bp_h(L, c);
    if (!isnan(h))
        h = lay_clamp_h(L, c, h, NAN);
    lay_contents(L, c, w, h);
}

static float margin_w(const lay *L, uint32_t c)
{
    const sk_lbox *b = &L->l->boxes[c];
    return L->st[c].w + b->margin[SK_LEFT] + b->margin[SK_RIGHT];
}

/* ---- Lines ---- */

typedef struct {
    float above, below; /* the line's extent about its baseline */
} extent;

static void include(extent *e, float ascent, float descent, float line_height)
{
    float lead = line_height - (ascent + descent);
    float top = floorf(lead / 2);
    e->above = fmaxf(e->above, ascent + top);
    e->below = fmaxf(e->below, descent + (lead - top));
}

/* The inline boxes from an atom's element up to the container. */
static void include_boxes(lay *L, extent *e, uint32_t node, uint32_t b)
{
    for (uint32_t n = node; n != SK_PACK_NONE && n != b; n = L->nodes[n].parent) {
        float a, d, lh;
        lay_line_metrics(L, n, &a, &d, &lh);
        include(e, a, d, lh);
    }
}

/* The decorations crossing text of node in container b. */
static void decorations(const lay *L, uint32_t node, uint32_t b, uint8_t *deco, sk_css_color *color)
{
    *deco = 0;
    for (uint32_t n = node; n != SK_PACK_NONE; n = L->nodes[n].parent) {
        const sk_css_style *s = lay_style(L, n);
        if (s->text_decoration && !*deco)
            *color = s->text_decoration_color;
        *deco |= s->text_decoration;
        if (n == b)
            break;
    }
}

static void push_run(lay *L, uint32_t b, const atom *a, size_t n, float x, float baseline)
{
    lstate *st = &L->st[b];
    sk_lrun r;
    memset(&r, 0, sizeof r);
    r.node = a[0].node;
    r.block = b;
    r.x = x;
    r.y = baseline;
    r.px = a[0].px;
    r.first = (uint32_t)st->glyphs.len;
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(a[0].ref.font, a[0].px, &vm);
    r.ascent = roundf(vm.ascent);
    r.descent = roundf(vm.descent);
    decorations(L, r.node, b, &r.deco, &r.deco_color);
    float pen = 0;
    for (size_t k = 0; k < n; k++) {
        sk_lglyph g = {a[k].ref.font, a[k].ref.glyph, (uint8_t)a[k].ref.synth, pen};
        sk_vec_push(&st->glyphs, g);
        pen += a[k].adv;
    }
    r.n = (uint32_t)n;
    r.width = pen;
    sk_vec_push(&st->runs, r);
}

/* The ellipsis in the container's style, as an atom. */
static atom ellipsis(lay *L, uint32_t b)
{
    const sk_css_style *s = lay_style(L, b);
    atom a;
    memset(&a, 0, sizeof a);
    a.kind = A_GLYPH;
    a.node = b;
    a.px = s->font_size;
    a.ref = lookup(sk_layout_face(L->fonts, s), s->font_synth, 0x2026, prefers_mono(s));
    a.adv = sk_font_advance(a.ref.font, a.ref.glyph, a.px) + (isfinite(s->letter_spacing) ? s->letter_spacing : 0);
    return a;
}

/* One line, atoms [s, e): its floats placed, its height found, its
 * runs and atomic boxes placed. The line's height. */
static float lay_line(lay *L, uint32_t b, atom *a, size_t s, size_t e, float cw, float y, float *left_floats,
                      float *right_floats)
{
    const sk_lbox *bx = &L->l->boxes[b];
    const sk_css_style *bs = lay_style(L, b);
    float cx = bx->bw[SK_LEFT] + bx->pad[SK_LEFT], cy = bx->bw[SK_TOP] + bx->pad[SK_TOP];
    /* Floats sit at the line's top, at their edge. */
    bool content = false;
    for (size_t k = s; k < e; k++) {
        if (a[k].kind == A_FLOAT) {
            uint32_t c = a[k].node;
            size_box(L, c, cw);
            const sk_lbox *fb = &L->l->boxes[c];
            float mw = margin_w(L, c);
            if (lay_style(L, c)->float_ == SK_FLOAT_LEFT) {
                L->st[c].x = cx + *left_floats + fb->margin[SK_LEFT];
                *left_floats += mw;
            } else {
                L->st[c].x = cx + cw - *right_floats - mw + fb->margin[SK_LEFT];
                *right_floats += mw;
            }
            L->st[c].y = cy + y + fb->margin[SK_TOP];
        } else if (a[k].kind != A_BREAK) {
            content = true;
        }
    }
    if (!content)
        return 0;
    /* Trailing spaces hang. */
    size_t last = e;
    while (last > s && (a[last - 1].space || a[last - 1].kind == A_FLOAT || a[last - 1].kind == A_BREAK))
        last--;
    float lw = 0;
    for (size_t k = s; k < last; k++)
        lw += a[k].kind == A_BOX ? margin_w(L, a[k].node) : a[k].kind == A_GLYPH ? a[k].adv : 0;
    /* An overflowing line ends in an ellipsis where the container says so. */
    atom ell;
    bool cut = false;
    if (lw > cw + 0.01f && bs->text_overflow == SK_TO_ELLIPSIS && bs->overflow_x != SK_OVERFLOW_VISIBLE) {
        ell = ellipsis(L, b);
        float room = cw - ell.adv, pen = 0;
        size_t k = s;
        for (; k < last; k++) {
            float adv = a[k].kind == A_BOX ? margin_w(L, a[k].node) : a[k].kind == A_GLYPH ? a[k].adv : 0;
            if (pen + adv > room + 0.01f)
                break;
            pen += adv;
        }
        last = k;
        lw = pen + ell.adv;
        cut = true;
    }
    /* The line's height: the strut, every inline box on it, atomic
     * boxes standing on the baseline. */
    extent ext = {-INFINITY, -INFINITY};
    float sa, sd, slh;
    lay_line_metrics(L, b, &sa, &sd, &slh);
    include(&ext, sa, sd, slh);
    uint32_t seen = SK_PACK_NONE;
    for (size_t k = s; k < last; k++) {
        if (a[k].kind == A_GLYPH && a[k].node != seen) {
            include_boxes(L, &ext, a[k].node, b);
            seen = a[k].node;
        } else if (a[k].kind == A_BOX) {
            uint32_t c = a[k].node;
            const sk_lbox *ab = &L->l->boxes[c];
            float h = L->st[c].h + ab->margin[SK_TOP] + ab->margin[SK_BOTTOM];
            float base = lay_is_svg(L, c) || isnan(L->st[c].baseline) ? h : L->st[c].baseline + ab->margin[SK_TOP];
            ext.above = fmaxf(ext.above, base);
            ext.below = fmaxf(ext.below, h - base);
        }
    }
    float baseline = y + ext.above;
    /* Where the line starts: text-align, an overflowing line at the start. */
    float off = 0;
    if (lw < cw) {
        switch (bs->text_align) {
        case SK_TA_CENTER:
            off = (cw - lw) / 2;
            break;
        case SK_TA_END:
        case SK_TA_RIGHT:
            off = cw - lw;
            break;
        default:
            break;
        }
    }
    float pen = cx + off;
    size_t k = s;
    while (k < last) {
        if (a[k].kind == A_BOX) {
            uint32_t c = a[k].node;
            const sk_lbox *ab = &L->l->boxes[c];
            float h = L->st[c].h + ab->margin[SK_TOP] + ab->margin[SK_BOTTOM];
            float base = lay_is_svg(L, c) || isnan(L->st[c].baseline) ? h : L->st[c].baseline + ab->margin[SK_TOP];
            L->st[c].x = pen + ab->margin[SK_LEFT];
            L->st[c].y = cy + baseline - base + ab->margin[SK_TOP];
            pen += margin_w(L, c);
            k++;
            continue;
        }
        if (a[k].kind != A_GLYPH) {
            k++;
            continue;
        }
        size_t j = k + 1;
        while (j < last && a[j].kind == A_GLYPH && a[j].node == a[k].node && a[j].ref.font == a[k].ref.font)
            j++;
        /* Hanging spaces draw nothing, but keep their place. */
        push_run(L, b, a + k, j - k, pen, cy + baseline);
        for (size_t q = k; q < j; q++)
            pen += a[q].adv;
        k = j;
    }
    if (cut)
        push_run(L, b, &ell, 1, pen, cy + baseline);
    if (isnan(L->st[b].baseline))
        L->st[b].baseline = cy + baseline;
    return ext.above + ext.below;
}

float lay_inline(lay *L, uint32_t b, const uint32_t *items, size_t n, float cw, float y0)
{
    atoms A;
    build(L, &A, b, items, n);
    atom *a = A.a.ptr;
    size_t na = A.a.len;
    /* Atomic boxes are sized before the lines are found. */
    for (size_t k = 0; k < na; k++)
        if (a[k].kind == A_BOX) {
            size_box(L, a[k].node, cw);
            a[k].adv = margin_w(L, a[k].node);
        }
    float y = y0, left_floats = 0, right_floats = 0;
    size_t s = 0;
    while (s < na) {
        /* A line drops the collapsible spaces it starts with. */
        while (s < na && a[s].space)
            s++;
        if (s >= na)
            break;
        float w = 0, w_ink = 0;
        size_t brk = SIZE_MAX, e = s;
        bool forced = false;
        for (; e < na; e++) {
            if (a[e].kind == A_BREAK) {
                forced = true;
                break;
            }
            float nw = w + (a[e].kind == A_FLOAT ? 0 : a[e].adv);
            float ink = a[e].space || a[e].kind == A_FLOAT ? w_ink : nw;
            if (ink > cw + 0.01f && brk != SIZE_MAX && !a[e].space)
                break;
            w = nw;
            w_ink = ink;
            if (a[e].brk && e + 1 < na)
                brk = e;
        }
        size_t end = e;
        if (!forced && e < na)
            end = brk + 1;
        float h = lay_line(L, b, a, s, end, cw, y, &left_floats, &right_floats);
        y += h;
        if (h > 0)
            left_floats = right_floats = 0;
        s = forced && end == e ? e + 1 : end;
    }
    sk_vec_free(&A.a);
    return y - y0;
}

void lay_inline_widths(lay *L, uint32_t b, const uint32_t *items, size_t n, float *min_w, float *max_w)
{
    atoms A;
    build(L, &A, b, items, n);
    float lo = 0, hi = 0, line = 0, line_ink = 0, piece = 0, floats = 0;
    for (size_t k = 0; k < A.a.len; k++) {
        const atom *a = &A.a.ptr[k];
        if (a->kind == A_BREAK) {
            hi = fmaxf(hi, line_ink);
            lo = fmaxf(lo, piece);
            line = line_ink = piece = 0;
            continue;
        }
        float adv = a->adv, least = a->adv;
        if (a->kind == A_BOX || a->kind == A_FLOAT) {
            float bl, bh;
            lay_intrinsic(L, a->node, &bl, &bh);
            float m = lay_margins_w(L, a->node);
            if (a->kind == A_FLOAT) {
                floats += bh + m;
                lo = fmaxf(lo, bl + m);
                continue;
            }
            adv = bh + m;
            least = bl + m;
        }
        /* A line drops the spaces it starts with. */
        if (a->space && line == 0)
            continue;
        line += adv;
        if (!a->space)
            line_ink = line;
        if (!a->space || !a->brk)
            piece += least;
        if (a->brk) {
            lo = fmaxf(lo, piece);
            piece = 0;
        }
    }
    hi = fmaxf(hi, line_ink) + floats;
    lo = fmaxf(lo, piece);
    sk_vec_free(&A.a);
    *min_w = lo;
    *max_w = fmaxf(hi, lo);
}

void lay_svg_text(lay *L, uint32_t i)
{
    lstate *st = &L->st[i];
    st->runs.len = 0;
    st->glyphs.len = 0;
    const sk_lnode *n = &L->nodes[i];
    if (!n->text || !n->text[0] || !n->svg)
        return;
    atoms A;
    memset(&A, 0, sizeof A);
    A.last_space = true;
    add_text(L, &A, i, n->text, strlen(n->text));
    size_t s = 0, e = A.a.len;
    while (s < e && A.a.ptr[s].space)
        s++;
    while (e > s && A.a.ptr[e - 1].space)
        e--;
    float w = 0;
    for (size_t k = s; k < e; k++)
        w += A.a.ptr[k].adv;
    float x = n->svg->v[0], y = n->svg->v[1];
    uint8_t anchor = lay_style(L, i)->text_anchor;
    if (anchor == SK_ANCHOR_MIDDLE)
        x -= w / 2;
    else if (anchor == SK_ANCHOR_END)
        x -= w;
    size_t k = s;
    while (k < e) {
        size_t j = k + 1;
        while (j < e && A.a.ptr[j].ref.font == A.a.ptr[k].ref.font)
            j++;
        push_run(L, i, A.a.ptr + k, j - k, x, y);
        for (size_t q = k; q < j; q++)
            x += A.a.ptr[q].adv;
        k = j;
    }
    sk_vec_free(&A.a);
}
