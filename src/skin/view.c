/* The skin runtime's frame (view.h): the tree of nodes a pack's
 * elements make, a list's rows copied from its row and a limits
 * widget's meters of the count shown; each node's state variables and
 * the style its table gives them; the layout at the stage's size; what
 * changed since the last frame, as rectangles; the paint of those
 * rectangles; the splits (app/renderer/splits.js); hit tests in
 * painting order for the pointer's hover, cursor and presses; and what
 * the app draws above the skin, the splits' handles and the menu
 * button.
 *
 * Every step lays the whole tree out again: a skin's tree of a few
 * hundred to some thousand nodes lays out in a tenth of a millisecond.
 * What is painted again is only what changed: each node keeps a hash of
 * everything its own paint depends on (its style, text, value, turn,
 * geometry, box, transform and lines of text), and a node whose hash or
 * presence changed has the ink it had and the ink it has painted again,
 * grown by the reach of any filter above it. */
#include "viewint.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NONE SK_PACK_NONE
/* A frame's damage beyond this many rectangles becomes one. */
#define MAX_DAMAGE 24

/* ---- The pack's tree ---- */

static double wall_now(void *ud)
{
    (void)ud;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Element e's subtree in tree order into out, from its first child
 * down; at most cap. */
static void subtree(const sk_pack *p, uint32_t e, uint32_t *out, uint32_t *n, uint32_t cap)
{
    if (*n >= cap)
        return;
    out[(*n)++] = e;
    for (uint32_t c = p->els[e].first_child; c != NONE; c = p->els[c].next_sibling)
        subtree(p, c, out, n, cap);
}

static void lists_init(sk_skinview *v)
{
    const sk_pack *p = v->p;
    v->row_of = sk_xreallocarray(NULL, p->nels ? p->nels : 1, sizeof *v->row_of);
    v->sub_at = sk_xcalloc(p->nels ? p->nels : 1, sizeof *v->sub_at);
    for (uint32_t e = 0; e < p->nels; e++)
        v->row_of[e] = -1;
    uint32_t next = p->nels;
    for (uint32_t e = 0; e < p->nels; e++) {
        const sk_pack_el *el = &p->els[e];
        if (el->role != SK_ROLE_ITEM || el->widget == NONE || v->row_of[e] >= 0)
            continue;
        v->lists = sk_xreallocarray(v->lists, v->nlists + 1, sizeof *v->lists);
        sk_vlist *L = &v->lists[v->nlists];
        memset(L, 0, sizeof *L);
        L->widget = el->widget;
        L->row = e;
        L->items = el->parent;
        L->sub = sk_xreallocarray(NULL, p->nels, sizeof *L->sub);
        subtree(p, e, L->sub, &L->nsub, p->nels);
        for (uint32_t k = 0; k < L->nsub; k++) {
            v->row_of[L->sub[k]] = (int32_t)v->nlists;
            v->sub_at[L->sub[k]] = k;
        }
        L->base = next;
        next += (SK_VIEW_MAX_ROWS - 1) * L->nsub;
        v->nlists++;
    }
    v->nnodes = next;
}

uint32_t sk_view_node_of(const sk_skinview *v, uint32_t n, uint32_t e)
{
    if (e >= v->p->nels || v->row_of[e] < 0)
        return e;
    const sk_vlist *L = &v->lists[v->row_of[e]];
    uint32_t r = v->nodes[n].row;
    if (r == 0 || v->row_of[v->nodes[n].el] != v->row_of[e])
        return e;
    return L->base + (r - 1) * L->nsub + v->sub_at[e];
}

/* ---- Images and faces ---- */

static const sk_font *face_of(void *data, uint16_t index)
{
    const sk_skinview *v = data;
    if (index < SK_FONT_EMBEDDED_COUNT)
        return sk_font_embedded((sk_embedded_font)index);
    size_t k = (size_t)index - SK_FONT_EMBEDDED_COUNT;
    return k < v->p->nfonts ? v->sp->faces[k] : NULL;
}

static bool image_size(void *data, uint32_t source, float *w, float *h)
{
    const sk_skinview *v = data;
    if (source >= v->p->nimages)
        return false;
    *w = v->p->images[source].w;
    *h = v->p->images[source].h;
    return true;
}

/* The variant to draw an image w by h device pixels from: its own
 * pixels, or the smallest drawing at least that large, else the
 * largest. */
static int variant_for(const sk_pack_image *im, int w, int h)
{
    int best = -1;
    for (uint16_t i = 0; i < im->nvariants; i++) {
        const sk_pack_variant *x = &im->variants[i];
        if (x->scale == 0)
            return i;
        bool big = x->w >= w && x->h >= h;
        if (best < 0) {
            best = i;
            continue;
        }
        const sk_pack_variant *b = &im->variants[best];
        bool bbig = b->w >= w && b->h >= h;
        if (big && (!bbig || (uint32_t)x->w * x->h < (uint32_t)b->w * b->h))
            best = i;
        else if (!big && !bbig && (uint32_t)x->w * x->h > (uint32_t)b->w * b->h)
            best = i;
    }
    return best;
}

/* Images decode once each: the variant first asked for, which is the
 * one an image's every use asks for in the shipped skins. */
static const sk_image *image_pixels(void *data, uint32_t source, int w, int h)
{
    sk_skinview *v = data;
    if (source >= v->p->nimages)
        return NULL;
    sk_vimage *d = &v->decoded[source];
    if (!d->tried) {
        d->tried = 1;
        const sk_pack_image *im = &v->p->images[source];
        int k = variant_for(im, w, h);
        if (k >= 0) {
            const sk_pack_variant *x = &im->variants[k];
            if (sk_qoi_decode(v->p->blob + x->blob, x->len, SK_PACK_MAX_PIXELS, &d->im))
                d->tried = 2;
        }
    }
    return d->tried == 2 ? &d->im : NULL;
}

/* ---- Layers ---- */

static uint64_t fnv(uint64_t h, const void *p, size_t n);
static const sk_player *kept_px(sk_skinview *v, sk_vkept *k);

/* The anchor of element e's layers: the element, or the svg an SVG
 * shape or group is drawn in. */
static uint32_t anchor_of(const sk_pack *p, uint32_t e)
{
    uint32_t a = e;
    while (a != NONE && a < p->nels && p->els[a].kind != SK_EL_SVG && p->els[a].kind >= SK_EL_G &&
           p->els[a].kind <= SK_EL_IMAGE)
        a = p->els[a].parent;
    return a == NONE || a >= p->nels ? e : a;
}

/* Layer i of the pack as the painter draws it, decoded once. */
static const sk_player *layer_px(sk_skinview *v, uint32_t i)
{
    sk_vlayer *d = &v->vlayers[i];
    if (!d->tried) {
        d->tried = 1;
        const sk_pack_layer *y = &v->p->layers[i];
        if (sk_qoi_decode(v->p->blob + y->blob, y->len, SK_PACK_MAX_PIXELS, &d->im) && d->im.w == y->w &&
            d->im.h == y->h) {
            d->tried = 2;
            d->pl = (sk_player){&d->im, y->kind, y->scale, y->x, y->y, anchor_of(v->p, y->el), y->ox, y->oy,
                                {y->slice[0], y->slice[1], y->slice[2], y->slice[3]}};
        }
    }
    return d->tried == 2 ? &d->pl : NULL;
}

/* The painter's question: the layer node n draws as for part where it
 * is drawn at k, of those of its element for the style it shows; the
 * one at k's scale, else the one nearest above it. */
static const sk_player *find_layer(void *data, uint32_t n, uint8_t part, float k)
{
    sk_skinview *v = data;
    uint8_t want = part == SK_PART_NINE ? SK_VLAYER_NINE : SK_VLAYER_SUBTREE;
    if (n >= v->nnodes)
        return NULL;
    /* The view's own picture, painted at the frame's scale. */
    if (part == SK_PART_SUBTREE && !(v->layer_ok[n] & want) && (v->layer_ok[n] & SK_VLAYER_KEPT) &&
        v->kept_of[n] != NONE) {
        if (fabs(k - v->k * v->scale) > 1e-4)
            return NULL;
        return kept_px(v, &v->kept[v->kept_of[n]]);
    }
    if (!(v->layer_ok[n] & want))
        return NULL;
    const sk_pack_el *e = &v->p->els[v->nodes[n].el];
    uint32_t si = (uint32_t)(v->nodes[n].base - v->p->styles);
    uint8_t scale = k > 1 + 1e-3f ? 2 : 1;
    uint32_t best = NONE;
    for (uint32_t i = e->layer; i < e->layer + e->nlayers; i++) {
        const sk_pack_layer *y = &v->p->layers[i];
        bool nine = y->kind == SK_LAYER_NINE;
        if (nine != (part == SK_PART_NINE) || y->kind == SK_LAYER_MASK || (y->style != NONE && y->style != si))
            continue;
        if (best == NONE || y->scale == scale)
            best = i;
    }
    return best == NONE ? NULL : layer_px(v, best);
}

static bool len_value(const sk_css_len *l, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (l[i].kind == SK_LEN_VALUE)
            return true;
    return false;
}

/* Whether a style holds a length the runtime fills in. */
static bool style_has_value(const sk_css_style *s)
{
    if (len_value(s->inset, 4) || len_value(&s->width, 1) || len_value(&s->height, 1) ||
        len_value(&s->min_width, 1) || len_value(&s->min_height, 1) || len_value(&s->max_width, 1) ||
        len_value(&s->max_height, 1) || len_value(s->margin, 4) || len_value(s->padding, 4) ||
        len_value(&s->flex_basis, 1) || len_value(s->dasharray, s->ndasharray))
        return true;
    for (uint16_t i = 0; i < s->nbg_size; i++)
        if (len_value(&s->bg_size[i].w, 1) || len_value(&s->bg_size[i].h, 1))
            return true;
    for (uint16_t i = 0; i < s->nbg_position; i++)
        if (len_value(&s->bg_position[i].x, 1) || len_value(&s->bg_position[i].y, 1))
            return true;
    return false;
}

/* The entries of element e's state table. */
static uint32_t entries_of(const sk_pack_el *el)
{
    uint32_t n = 1;
    for (uint8_t i = 0; i < el->ninputs; i++)
        n *= el->inputs[i].radix;
    return n;
}

bool sk_view_shows_value(const sk_skinview *v, uint32_t e)
{
    const sk_pack *p = v->p;
    const sk_pack_el *el = &p->els[e];
    if (el->group || v->row_of[e] >= 0)
        return true;
    switch (el->role) {
    case SK_ROLE_VALUE:
    case SK_ROLE_RESETS_LEFT:
    case SK_ROLE_FILL:
    case SK_ROLE_ARC_FILL:
    case SK_ROLE_SHEET:
    case SK_ROLE_MARK:
    case SK_ROLE_DIAL_TEXT:
    case SK_ROLE_ITEMS:
    case SK_ROLE_ITEM:
    case SK_ROLE_ITEM_MARK:
    case SK_ROLE_ITEM_TEXT:
    case SK_ROLE_NEEDLE_GROUP:
    case SK_ROLE_KNOB_TURN:
        return true;
    default:
        break;
    }
    switch (el->text_source) {
    case SK_TEXT_VALUE:
    case SK_TEXT_RESETS:
    case SK_TEXT_ITEM_MARK:
    case SK_TEXT_ITEM_TEXT:
        return true;
    case SK_TEXT_LABEL:
        if (el->widget < p->nwidgets && p->widgets[el->widget].fallback_bind)
            return true;
        break;
    default:
        break;
    }
    uint32_t n = entries_of(el);
    for (uint32_t t = 0; t < n; t++)
        if (style_has_value(&p->styles[el->styles[t]]))
            return true;
    return false;
}

/* ---- Subtrees kept painted ---- */

/* At most this many bytes of pictures; a subtree past it is painted. */
#define KEPT_MAX_BYTES (96u << 20)

static bool table_animates(const sk_pack *p, const sk_pack_el *el)
{
    uint32_t n = entries_of(el);
    for (uint32_t t = 0; t < n; t++) {
        const sk_css_style *s = &p->styles[el->styles[t]];
        for (uint16_t i = 0; i < s->nanim_name; i++)
            if (s->anim_name[i] != NONE)
                return true;
    }
    return false;
}

/* Whether a style paints what takes longer to paint than to copy: an
 * image, a gradient, a shadow, a filter, a border image. */
static bool costly(const sk_css_style *s)
{
    for (uint16_t k = 0; k < s->nbg_image; k++)
        if (s->bg_image[k].kind != SK_IMAGE_NONE)
            return true;
    return s->nbox_shadow || s->ntext_shadow || s->nfilter || s->bi_source.kind != SK_IMAGE_NONE;
}

/* The subtrees kept painted: from the stage down, the first element
 * whose subtree shows no value and runs no animation, that is a box of
 * its own and paints something costly, and that skinc gave no layer of
 * the whole. Below the stage only: the stage holds the widgets. */
static void kept_find(sk_skinview *v, const uint8_t *settled, const uint8_t *dear, uint32_t e, int depth)
{
    const sk_pack *p = v->p;
    for (uint32_t c = p->els[e].first_child; c != NONE; c = p->els[c].next_sibling) {
        const sk_pack_el *el = &p->els[c];
        bool whole = false;
        for (uint32_t i = el->layer; i < el->layer + el->nlayers; i++)
            whole |= p->layers[i].kind == SK_LAYER_STATIC;
        if (settled[c] && dear[c] && !whole && el->kind >= SK_EL_DIV && el->kind <= SK_EL_SVG &&
            el->kind != SK_EL_SPAN) {
            v->kept = sk_xreallocarray(v->kept, v->nkept + 1, sizeof *v->kept);
            sk_vkept *k = &v->kept[v->nkept];
            memset(k, 0, sizeof *k);
            k->node = c;
            k->sub = sk_xreallocarray(NULL, p->nels, sizeof *k->sub);
            subtree(p, c, k->sub, &k->nsub, p->nels);
            v->kept_of[c] = v->nkept++;
        } else if (!settled[c] && depth < 64) {
            kept_find(v, settled, dear, c, depth + 1);
        }
    }
}

static void kept_init(sk_skinview *v)
{
    const sk_pack *p = v->p;
    v->kept_of = sk_xreallocarray(NULL, v->nnodes ? v->nnodes : 1, sizeof *v->kept_of);
    for (uint32_t i = 0; i < v->nnodes; i++)
        v->kept_of[i] = NONE;
    if (v->stage == NONE || !p->nels)
        return;
    /* Children come after their parents among the elements: from the
     * last, each element's children are known before it. */
    uint8_t *settled = sk_xcalloc(p->nels, 1), *dear = sk_xcalloc(p->nels, 1);
    for (uint32_t e = p->nels; e-- > 0;) {
        const sk_pack_el *el = &p->els[e];
        bool ok = !sk_view_shows_value(v, e) && !table_animates(p, el);
        bool d = costly(&p->styles[el->styles[0]]) || (el->text_source == SK_TEXT_STATIC && el->text);
        for (uint32_t c = el->first_child; c != NONE; c = p->els[c].next_sibling) {
            ok = ok && c > e && settled[c];
            d = d || (c > e && dear[c]);
        }
        settled[e] = ok;
        dear[e] = d;
    }
    kept_find(v, settled, dear, v->stage, 0);
    free(settled);
    free(dear);
}

static void kept_drop(sk_skinview *v, sk_vkept *k)
{
    if (k->valid)
        v->kept_bytes -= (size_t)k->im.w * (size_t)k->im.h * sizeof(sk_px);
    sk_image_free(&k->im);
    k->valid = false;
}


/* Kept subtree k's picture, painted anew when what it paints changed
 * (its nodes' signatures, the scale), on whole pixels of the frame. */
static const sk_player *kept_px(sk_skinview *v, sk_vkept *k)
{
    if (k->valid)
        return &k->pl;
    const sk_lbox *b = &v->layout.boxes[k->node];
    float s = (float)(v->k * v->scale);
    double x0 = floor((double)b->ink.x * s), y0 = floor((double)b->ink.y * s);
    double x1 = ceil((double)(b->ink.x + b->ink.w) * s), y1 = ceil((double)(b->ink.y + b->ink.h) * s);
    if (!(x1 > x0) || !(y1 > y0) || x1 - x0 > 8192 || y1 - y0 > 8192)
        return NULL;
    size_t bytes = (size_t)(x1 - x0) * (size_t)(y1 - y0) * sizeof(sk_px);
    if (v->kept_bytes + bytes > KEPT_MAX_BYTES)
        return NULL;
    sk_image_init(&k->im, (int)(x1 - x0), (int)(y1 - y0));
    sk_xform own = {s, 0, 0, s, (float)-x0, (float)-y0};
    sk_paint_env env = sk_view_env(v);
    sk_paint_layer(&k->im, &env, k->node, SK_PART_SUBTREE, &own);
    k->pl = (sk_player){&k->im,       SK_LAYER_STATIC, s, (float)(x0 / s - b->border.x), (float)(y0 / s - b->border.y),
                        k->node,      0,               0, {0, 0, 0, 0}};
    k->valid = true;
    v->kept_bytes += bytes;
    return &k->pl;
}

/* Each kept subtree's key this frame; a picture painted for another
 * is dropped. Kept while nothing of it eases and it paints at once,
 * from the second run its key stays the same: a resize, or a split
 * dragged, paints it straight into the frame, and the picture is
 * painted when it is next drawn from. */
static void kept_choose(sk_skinview *v)
{
    for (uint32_t i = 0; i < v->nkept; i++) {
        sk_vkept *k = &v->kept[i];
        uint32_t n = k->node;
        const sk_vnode *x = &v->nodes[n];
        bool ok = x->live && x->base && !v->busy[n] && !(v->layer_ok[n] & SK_VLAYER_SUBTREE) &&
                  v->layout.boxes[n].generated && sk_paint_at_once(&v->layout, k->sub, k->nsub);
        uint64_t key = 1469598103934665603ull;
        double s = v->k * v->scale;
        key = fnv(key, &s, sizeof s);
        for (uint32_t q = 0; ok && q < k->nsub; q++) {
            uint32_t d = k->sub[q];
            const sk_vnode *dx = &v->nodes[d];
            uint64_t sig = dx->live && v->layout.boxes[d].generated ? sk_view_signature(v, d, true) : 0;
            key = fnv(key, &sig, sizeof sig);
        }
        if (!ok || key != k->key)
            kept_drop(v, k);
        k->steady = ok && key == k->key;
        k->key = key;
        if (k->steady)
            v->layer_ok[n] |= SK_VLAYER_KEPT;
    }
}

/* Each node's easing, its own or below it. */
static bool mark_busy(sk_skinview *v, uint32_t n, int depth)
{
    bool b = sk_motion_easing(v->nodes[n].mo);
    if (depth < 256)
        for (uint32_t c = v->lnodes[n].first; c != NONE && c < v->nnodes; c = v->lnodes[c].next)
            b |= mark_busy(v, c, depth + 1);
    v->busy[n] = b;
    return b;
}

/* What each node may draw from its element's layers: an element's own
 * node, shown, in a style it has a layer for, nothing of it easing;
 * then the subtrees kept painted. */
static void choose_layers(sk_skinview *v)
{
    memset(v->layer_ok, 0, v->nnodes ? v->nnodes : 1);
    memset(v->busy, 0, v->nnodes ? v->nnodes : 1);
    if (v->stage == NONE)
        return;
    mark_busy(v, v->stage, 0);
    for (uint32_t n = 0; v->p->nlayers && n < v->p->nels; n++) {
        const sk_vnode *x = &v->nodes[n];
        const sk_pack_el *e = &v->p->els[n];
        if (!e->nlayers || !x->live || !x->base || x->hide || v->busy[n] || !v->layout.boxes[n].generated ||
            !v->lnodes[n].style)
            continue;
        uint32_t si = (uint32_t)(x->base - v->p->styles);
        uint8_t ok = 0;
        for (uint32_t i = e->layer; i < e->layer + e->nlayers; i++) {
            const sk_pack_layer *y = &v->p->layers[i];
            if (y->style != NONE && y->style != si)
                continue;
            if (y->kind == SK_LAYER_NINE)
                ok |= SK_VLAYER_NINE;
            else if (y->kind != SK_LAYER_MASK)
                ok |= SK_VLAYER_SUBTREE;
        }
        v->layer_ok[n] = ok;
    }
    kept_choose(v);
}

/* Whether a and b, edges in device pixels, are within a pixel. */
static bool edges_near(double a0, double a1, double b0, double b1)
{
    return fabs(a0 - b0) <= 1.001 && fabs(a1 - b1) <= 1.001;
}

bool sk_view_layers_fit(sk_skinview *v, char *err, size_t err_len)
{
    const sk_pack *p = v->p;
    if (!p->nlayers || v->stage == NONE)
        return true;
    sk_view_run(v);
    const sk_layout *l = &v->layout;
    for (uint32_t i = 0; i < p->nlayers; i++) {
        const sk_pack_layer *y = &p->layers[i];
        uint32_t e = y->el, a = anchor_of(p, e);
        const sk_vnode *x = &v->nodes[e];
        if (!x->live || !x->base || !l->boxes[e].generated || !l->boxes[a].generated || y->kind == SK_LAYER_MASK)
            continue;
        if (y->style != NONE && y->style != (uint32_t)(x->base - p->styles))
            continue;
        const sk_lbox *b = &l->boxes[e], *ab = &l->boxes[a];
        double s = y->scale;
        /* The layer's edges, device pixels of the anchor's space. */
        double lx0 = (ab->border.x + y->x) * s, ly0 = (ab->border.y + y->y) * s;
        double lx1 = lx0 + y->w, ly1 = ly0 + y->h;
        bool ok;
        if (y->kind == SK_LAYER_NINE) {
            double bx0 = b->border.x * s, by0 = b->border.y * s;
            ok = !b->svg_content && y->ox <= b->border.w + 1e-3f && y->oy <= b->border.h + 1e-3f &&
                 lx0 <= bx0 + 1e-3 && ly0 <= by0 + 1e-3 && lx1 >= bx0 + y->ox * s - 1e-3 &&
                 ly1 >= by0 + y->oy * s - 1e-3 && lx0 + y->slice[SK_LEFT] >= bx0 - 1e-3 &&
                 lx0 + y->slice[SK_LEFT] < bx0 + y->ox * s && ly0 + y->slice[SK_TOP] >= by0 - 1e-3 &&
                 ly0 + y->slice[SK_TOP] < by0 + y->oy * s;
        } else {
            /* What the element paints, in its anchor's space. */
            sk_frect ink = b->ink;
            if (b->svg_content)
                ink = sk_frect_map(&ab->viewbox, ink);
            ok = edges_near(lx0, lx1, floor(ink.x * s), ceil((ink.x + ink.w) * s)) &&
                 edges_near(ly0, ly1, floor(ink.y * s), ceil((ink.y + ink.h) * s));
        }
        if (!ok) {
            if (err && err_len)
                snprintf(err, err_len, "LAYR %u: %u x %u pixels at %g, %g do not fit element %u's box of %g x %g",
                         (unsigned)i, (unsigned)y->w, (unsigned)y->h, (double)y->x, (double)y->y, (unsigned)e,
                         (double)b->border.w, (double)b->border.h);
            return false;
        }
    }
    return true;
}

sk_paint_env sk_view_env(sk_skinview *v)
{
    sk_paint_env env = {&v->layout, &v->lfonts, &v->images, v->glyphs, v->pcache, &v->layer_hook};
    return env;
}

/* ---- The splits (splits.js) ---- */

static int edge_key(uint8_t edge)
{
    static const int KEY[] = {0, 1, 3, 4};
    return KEY[edge & 3];
}

static bool horizontal(uint8_t edge)
{
    return edge == SK_SPLIT_LEFT || edge == SK_SPLIT_RIGHT;
}

/* range(): a split's min, its max less the growth the layout's area
 * cannot hold, and its default. */
static void split_range(const sk_skinview *v, uint32_t i, double *min, double *max, double *def)
{
    const sk_pack_split *s = &v->p->splits[i];
    const sk_pack_meta *m = &v->p->meta;
    double d = m->terminal_rect.v[edge_key(s->edge)];
    double shortfall = horizontal(s->edge) ? m->width - v->lw : m->height - SK_VIEW_STRIP - v->lh;
    *def = d;
    *min = s->min;
    *max = fmax(d, fmin(s->max, s->max - shortfall));
}

/* sizes(), then rect() of every pack rect. */
static void place_rects(sk_skinview *v)
{
    const sk_pack *p = v->p;
    uint32_t ns = SK_MIN(p->nsplits, SK_VIEW_MAX_SPLITS);
    for (uint32_t i = 0; i < ns; i++) {
        double min, max, def;
        split_range(v, i, &min, &max, &def);
        double x = v->has_live[i] ? v->live[i] : v->has_saved[i] ? v->saved[i] : def;
        v->at[i] = fmin(max, fmax(min, x));
    }
    for (uint32_t r = 0; r < p->nrects; r++) {
        sk_pack_rect out = p->rects[r];
        for (int key = 0; key < 6; key++) {
            int s = p->rects[r].follow[key];
            if (s < 0 || (uint32_t)s >= ns || !(out.has & (1u << key)))
                continue;
            double def = p->meta.terminal_rect.v[edge_key(p->splits[s].edge)];
            out.v[key] = (float)(out.v[key] + (v->at[s] - def));
        }
        v->rects[r] = out;
    }
}

/* terminal(): its rect with the splits at their sizes, as left, top,
 * right, bottom of the layout's area. */
static void terminal_ltrb(const sk_skinview *v, double out[4])
{
    const sk_pack_rect *t = &v->p->meta.terminal_rect;
    double k[6];
    for (int i = 0; i < 6; i++)
        k[i] = t->has & (1u << i) ? t->v[i] : 0;
    for (uint32_t i = 0; i < SK_MIN(v->p->nsplits, SK_VIEW_MAX_SPLITS); i++)
        k[edge_key(v->p->splits[i].edge)] = v->at[i];
    out[0] = k[0];
    out[1] = k[3];
    out[2] = k[1];
    out[3] = k[4];
}

/* A rect placed in a w by h box as CSS's absolute positioning places
 * it, each key times k: x, y, width, height. */
static void place_box(const sk_pack_rect *r, double k, double w, double h, double out[4])
{
    double v[6];
    for (int i = 0; i < 6; i++)
        v[i] = r->v[i] * k;
    bool l = r->has & 1, rt = r->has & 2, wd = r->has & 4, t = r->has & 8, b = r->has & 16, ht = r->has & 32;
    out[2] = wd ? v[2] : w - (l ? v[0] : 0) - (rt ? v[1] : 0);
    out[0] = l ? v[0] : rt ? w - v[1] - out[2] : 0;
    out[3] = ht ? v[5] : h - (t ? v[3] : 0) - (b ? v[4] : 0);
    out[1] = t ? v[3] : b ? h - v[4] - out[3] : 0;
}

/* ---- The view ---- */

static void stage_geometry(sk_skinview *v)
{
    const sk_pack_meta *m = &v->p->meta;
    double minh = m->min_height - SK_VIEW_STRIP;
    double k = 1;
    if (m->min_width > 0)
        k = fmin(k, v->sw / m->min_width);
    if (minh > 0)
        k = fmin(k, v->sh / minh);
    if (!(k > 0))
        k = 1;
    v->k = k;
    v->lw = (float)(v->sw / k);
    v->lh = (float)(v->sh / k);
    place_rects(v);
}

sk_skinview *sk_skinview_new(sk_skinpack *sp, double now)
{
    sk_skinview *v = sk_xcalloc(1, sizeof *v);
    v->sp = sk_skinpack_ref(sp);
    v->p = &sp->pack;
    v->now = v->last = now;
    v->wall_fn = wall_now;
    v->wall = wall_now(NULL);
    v->stage = NONE;
    for (uint32_t e = 0; e < v->p->nels && v->stage == NONE; e++)
        if (v->p->els[e].role == SK_ROLE_STAGE)
            v->stage = e;
    lists_init(v);
    v->nodes = sk_xcalloc(v->nnodes ? v->nnodes : 1, sizeof *v->nodes);
    v->lnodes = sk_xcalloc(v->nnodes ? v->nnodes : 1, sizeof *v->lnodes);
    for (uint32_t e = 0; e < v->p->nels; e++)
        v->nodes[e].el = e;
    for (uint32_t l = 0; l < v->nlists; l++) {
        const sk_vlist *L = &v->lists[l];
        for (uint32_t r = 1; r < SK_VIEW_MAX_ROWS; r++)
            for (uint32_t k = 0; k < L->nsub; k++) {
                sk_vnode *x = &v->nodes[L->base + (r - 1) * L->nsub + k];
                x->el = L->sub[k];
                x->row = (uint16_t)r;
            }
    }
    v->rects = sk_xcalloc(v->p->nrects ? v->p->nrects : 1, sizeof *v->rects);
    sk_map_init(&v->metrics);
    sk_map_init(&v->dials);
    v->lfonts.face = face_of;
    v->lfonts.data = v;
    v->images.size = image_size;
    v->images.pixels = image_pixels;
    v->images.data = v;
    v->decoded = sk_xcalloc(v->p->nimages ? v->p->nimages : 1, sizeof *v->decoded);
    v->glyphs = sk_glyph_cache_new(16u << 20);
    v->pcache = sk_paint_cache_new(32u << 20);
    v->layer_hook.find = find_layer;
    v->layer_hook.data = v;
    v->vlayers = sk_xcalloc(v->p->nlayers ? v->p->nlayers : 1, sizeof *v->vlayers);
    v->layer_ok = sk_xcalloc(v->nnodes ? v->nnodes : 1, 1);
    v->busy = sk_xcalloc(v->nnodes ? v->nnodes : 1, 1);
    kept_init(v);
    v->hover = v->pressed = NONE;
    v->fade.node = NONE;
    v->handle_hover = v->drag_split = -1;
    v->next_due = -1;
    sk_widgets_init(v);
    v->dirty = v->all = v->metrics_new = true;
    const sk_pack_meta *m = &v->p->meta;
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, m->width, m->height - SK_VIEW_STRIP, 1);
    return v;
}

static void metrics_clear(sk_skinview *v)
{
    size_t i = 0;
    sk_str k;
    void *x;
    while (sk_map_next(&v->metrics, &i, &k, &x)) {
        sk_val *val = x;
        free(val->text);
        free(val);
    }
    sk_map_free(&v->metrics);
    sk_map_init(&v->metrics);
}

void sk_skinview_free(sk_skinview *v)
{
    if (!v)
        return;
    sk_widgets_free(v);
    for (uint32_t i = 0; i < v->nnodes; i++) {
        free(v->nodes[i].text);
        sk_motion_free(v->nodes[i].mo);
    }
    free(v->nodes);
    free(v->lnodes);
    free(v->rects);
    for (uint32_t l = 0; l < v->nlists; l++)
        free(v->lists[l].sub);
    free(v->lists);
    free(v->row_of);
    free(v->sub_at);
    metrics_clear(v);
    sk_map_free(&v->metrics);
    sk_map_free(&v->dials);
    if (v->laid)
        sk_layout_free(&v->layout);
    for (uint32_t i = 0; i < v->p->nimages; i++)
        sk_image_free(&v->decoded[i].im);
    free(v->decoded);
    for (uint32_t i = 0; i < v->p->nlayers; i++)
        sk_image_free(&v->vlayers[i].im);
    free(v->vlayers);
    free(v->layer_ok);
    free(v->busy);
    for (uint32_t i = 0; i < v->nkept; i++) {
        sk_image_free(&v->kept[i].im);
        free(v->kept[i].sub);
    }
    free(v->kept);
    free(v->kept_of);
    sk_glyph_cache_free(v->glyphs);
    sk_paint_cache_free(v->pcache);
    sk_fade_free(&v->fade);
    free(v->order);
    sk_vec_free(&v->damage);
    sk_vec_free(&v->hovered);
    sk_buf_free(&v->event_text);
    sk_skinpack_unref(v->sp);
    free(v);
}

sk_skinpack *sk_skinview_pack(const sk_skinview *v)
{
    return v->sp;
}

void sk_skinview_set_stage(sk_skinview *v, double x, double y, double w, double h, double scale)
{
    if (!(scale > 0))
        scale = 1;
    if (x == v->sx && y == v->sy && w == v->sw && h == v->sh && scale == v->scale)
        return;
    v->sx = x;
    v->sy = y;
    v->sw = fmax(0, w);
    v->sh = fmax(0, h);
    v->scale = scale;
    stage_geometry(v);
    sk_fade_free(&v->fade);
    v->dirty = v->all = true;
}

void sk_skinview_set_metrics(sk_skinview *v, const sk_skin_metric *m, size_t n)
{
    metrics_clear(v);
    for (size_t i = 0; i < n; i++) {
        if (!m[i].name)
            continue;
        void **slot = sk_map_put(&v->metrics, sk_str_c(m[i].name));
        sk_val *x = *slot;
        if (x)
            free(x->text);
        else
            x = *slot = sk_xmalloc(sizeof *x);
        *x = m[i].value;
        x->text = m[i].value.text ? sk_xstrdup(m[i].value.text) : NULL;
    }
    v->metrics_new = v->dirty = true;
}

const sk_val *sk_view_metric(const sk_skinview *v, const char *name)
{
    return sk_map_get(&v->metrics, sk_str_c(name));
}

void sk_skinview_set_dials(sk_skinview *v, const char *const *names, size_t n)
{
    sk_map_free(&v->dials);
    sk_map_init(&v->dials);
    for (size_t i = 0; i < n; i++)
        if (names[i])
            *sk_map_put(&v->dials, sk_str_c(names[i])) = v;
    sk_widgets_marks(v);
    v->dirty = true;
}

void sk_skinview_set_list_off(sk_skinview *v, bool off)
{
    v->list_off = off;
    sk_widgets_marks(v);
    v->dirty = true;
}

void sk_skinview_set_panels(sk_skinview *v, const char *const *ids, const double *sizes, size_t n)
{
    for (uint32_t i = 0; i < SK_MIN(v->p->nsplits, SK_VIEW_MAX_SPLITS); i++) {
        v->has_saved[i] = false;
        for (size_t k = 0; k < n; k++)
            if (ids[k] && v->p->splits[i].id && !strcmp(ids[k], v->p->splits[i].id) && isfinite(sizes[k])) {
                v->has_saved[i] = true;
                v->saved[i] = sizes[k];
            }
    }
    place_rects(v);
    v->dirty = v->all = true;
}

/* Damage of the overlays, in stage pixels. */
static void damage_px(sk_skinview *v, sk_rect r);

void sk_skinview_set_menu(sk_skinview *v, bool open, bool unseen)
{
    if (open == v->menu_open && unseen == v->unseen)
        return;
    v->menu_open = open;
    v->unseen = unseen;
    damage_px(v, v->menu_px);
    v->dirty = true;
}

double sk_skinview_scale(const sk_skinview *v)
{
    return v->k;
}

void sk_skinview_terminal_rect(const sk_skinview *v, double out[4])
{
    double t[4];
    terminal_ltrb(v, t);
    double x0 = v->sx + t[0] * v->k, y0 = v->sy + t[1] * v->k;
    double x1 = v->sx + v->sw - t[2] * v->k, y1 = v->sy + v->sh - t[3] * v->k;
    out[0] = x0;
    out[1] = y0;
    out[2] = fmax(0, x1 - x0);
    out[3] = fmax(0, y1 - y0);
}

sk_rect sk_skinview_terminal_px(const sk_skinview *v)
{
    double t[4];
    sk_skinview_terminal_rect(v, t);
    long x0 = lround(t[0] * v->scale), y0 = lround(t[1] * v->scale);
    long x1 = lround((t[0] + t[2]) * v->scale), y1 = lround((t[1] + t[3]) * v->scale);
    return sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0));
}

void sk_skinview_menu_rect(const sk_skinview *v, double out[4])
{
    place_box(&v->p->meta.menu_rect, v->k, v->sw, v->sh, out);
    out[0] += v->sx;
    out[1] += v->sy;
}

/* The stage in frame pixels. */
static sk_rect stage_px(const sk_skinview *v)
{
    long x0 = lround(v->sx * v->scale), y0 = lround(v->sy * v->scale);
    long x1 = lround((v->sx + v->sw) * v->scale), y1 = lround((v->sy + v->sh) * v->scale);
    return sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0));
}

/* ---- State ---- */

void sk_view_set_var(sk_skinview *v, uint32_t n, uint8_t var, uint8_t value)
{
    if (n >= v->nnodes || var >= SK_VAR_COUNT_OF_VARS || v->nodes[n].var[var] == value)
        return;
    v->nodes[n].var[var] = value;
    v->dirty = true;
}

void sk_view_set_text(sk_skinview *v, uint32_t n, const char *text)
{
    if (n >= v->nnodes)
        return;
    sk_vnode *x = &v->nodes[n];
    if (x->has_text && ((!x->text && !text) || (x->text && text && !strcmp(x->text, text))))
        return;
    free(x->text);
    x->text = text ? sk_xstrdup(text) : NULL;
    x->has_text = true;
    v->dirty = true;
}

/* ---- The tree laid out ---- */

static bool meter_hidden(const sk_skinview *v, const sk_pack_el *e)
{
    if (!e->group || e->widget >= v->nwidgets)
        return false;
    return v->widgets[e->widget].count != e->group;
}

static void link_child(sk_skinview *v, uint32_t parent, uint32_t child, uint32_t *last)
{
    sk_lnode *c = &v->lnodes[child];
    c->parent = parent;
    c->next = NONE;
    if (*last == NONE)
        v->lnodes[parent].first = child;
    else
        v->lnodes[*last].next = child;
    *last = child;
}

/* Node n, element e's in row row, and what it holds, into the tree. */
static void walk(sk_skinview *v, uint32_t n, uint32_t e, uint16_t row, int depth)
{
    const sk_pack *p = v->p;
    v->nodes[n].live = true;
    if (depth > 256)
        return;
    uint32_t last = NONE;
    for (uint32_t c = p->els[e].first_child; c != NONE; c = p->els[c].next_sibling) {
        const sk_pack_el *ce = &p->els[c];
        if (meter_hidden(v, ce))
            continue;
        if (ce->role == SK_ROLE_ITEM && v->row_of[c] >= 0 && v->lists[v->row_of[c]].row == c) {
            const sk_vlist *L = &v->lists[v->row_of[c]];
            for (uint16_t r = 0; r < L->rows; r++) {
                uint32_t k = r == 0 ? c : L->base + (uint32_t)(r - 1) * L->nsub;
                link_child(v, n, k, &last);
                walk(v, k, c, r, depth + 1);
            }
            continue;
        }
        uint32_t k = c;
        if (row > 0 && v->row_of[c] >= 0) {
            const sk_vlist *L = &v->lists[v->row_of[c]];
            k = L->base + (uint32_t)(row - 1) * L->nsub + v->sub_at[c];
        }
        link_child(v, n, k, &last);
        walk(v, k, c, row, depth + 1);
    }
}

static void link_tree(sk_skinview *v)
{
    for (uint32_t i = 0; i < v->nnodes; i++) {
        v->nodes[i].live = false;
        v->lnodes[i].parent = v->lnodes[i].first = v->lnodes[i].next = NONE;
    }
    if (v->p->nels)
        walk(v, 0, 0, 0, 0);
}

/* Each live node's style from its table, transitions and animations
 * started and stopped as it changes. */
static void restyle(sk_skinview *v)
{
    const sk_pack *p = v->p;
    for (uint32_t n = 0; n < v->nnodes; n++) {
        sk_vnode *x = &v->nodes[n];
        if (!x->live) {
            /* Gone from the tree: as a removed element, it starts over. */
            if (x->mo) {
                sk_motion_free(x->mo);
                x->mo = NULL;
            }
            x->base = NULL;
            continue;
        }
        const sk_pack_el *e = &p->els[x->el];
        uint8_t vals[SK_PACK_MAX_INPUTS];
        for (uint8_t i = 0; i < e->ninputs; i++) {
            const sk_pack_input *in = &e->inputs[i];
            uint32_t o = sk_view_node_of(v, n, in->owner);
            uint8_t val = o < v->nnodes && in->var < SK_VAR_COUNT_OF_VARS ? v->nodes[o].var[in->var] : 0;
            vals[i] = val < in->radix ? val : (uint8_t)(in->radix - 1);
        }
        const sk_css_style *base = sk_pack_style_of(p, e, vals);
        if (base == x->base)
            continue;
        /* display none stops its animations and transitions, and an
         * element displayed again starts with none of them, as CSS's. */
        bool was_shown = x->base && x->base->display != SK_DISPLAY_NONE;
        if (base->display == SK_DISPLAY_NONE) {
            sk_motion_free(x->mo);
            x->mo = NULL;
            x->base = base;
            continue;
        }
        const sk_css_style *old = !was_shown ? NULL
                                  : x->mo    ? sk_motion_style(v, n, &(bool){false}, &(double){-1})
                                             : x->base;
        sk_motion_restyle(v, n, old, base);
        x->base = base;
    }
}

/* The layout's input from the nodes, each in the style it shows now. */
static void build(sk_skinview *v)
{
    const sk_pack *p = v->p;
    for (uint32_t n = 0; n < v->nnodes; n++) {
        sk_vnode *x = &v->nodes[n];
        sk_lnode *l = &v->lnodes[n];
        const sk_pack_el *e = &p->els[x->el];
        l->kind = e->kind;
        l->role = e->role;
        l->row = x->row;
        l->value = x->value;
        l->turn = x->turn;
        l->rect = e->rect != NONE && e->rect < p->nrects ? &v->rects[e->rect] : NULL;
        l->svg = x->own_svg ? &x->svg : e->svg != NONE ? &p->svgs[e->svg] : NULL;
        if (e->kind == SK_EL_BEFORE || e->kind == SK_EL_AFTER)
            l->text = NULL;
        else if (x->has_text)
            l->text = x->text;
        else
            l->text = e->text_source != SK_TEXT_NONE ? e->text : NULL;
        if (!x->live || !x->base) {
            l->style = NULL;
            continue;
        }
        bool moving = false;
        double due = -1;
        l->style = x->hide ? NULL : sk_motion_style(v, n, &moving, &due);
        if (moving)
            v->moving = true;
        if (due >= 0 && (v->next_due < 0 || due < v->next_due))
            v->next_due = due;
    }
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static uint64_t fnv_rect(uint64_t h, sk_frect r)
{
    float f[4] = {r.x, r.y, r.w, r.h};
    return fnv(h, f, sizeof f);
}

uint64_t sk_view_signature(const sk_skinview *v, uint32_t n, bool opacity)
{
    const sk_vnode *x = &v->nodes[n];
    const sk_lnode *l = &v->lnodes[n];
    const sk_lbox *b = &v->layout.boxes[n];
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, &x->base, sizeof x->base);
    uint64_t ms = opacity ? sk_motion_sig(x->mo) : sk_motion_sig_but_opacity(x->mo);
    h = fnv(h, &ms, sizeof ms);
    if (l->text)
        h = fnv(h, l->text, strlen(l->text) + 1);
    h = fnv(h, &l->value, sizeof l->value);
    h = fnv(h, &l->turn, sizeof l->turn);
    if (l->svg) {
        h = fnv(h, &l->svg->kind, 1);
        h = fnv(h, l->svg->v, sizeof l->svg->v);
    }
    h = fnv_rect(h, b->border);
    float m[6] = {b->xform.a, b->xform.b, b->xform.c, b->xform.d, b->xform.e, b->xform.f};
    h = fnv(h, m, sizeof m);
    float vb[6] = {b->viewbox.a, b->viewbox.b, b->viewbox.c, b->viewbox.d, b->viewbox.e, b->viewbox.f};
    h = fnv(h, vb, sizeof vb);
    h = fnv(h, &b->clipper, sizeof b->clipper);
    /* Drawn from a layer of skinc's or painted: the two may round
     * apart. A picture the view keeps is its own paint, composited. */
    uint8_t lk = v->layer_ok[n] & (uint8_t)~SK_VLAYER_KEPT;
    h = fnv(h, &lk, 1);
    for (uint32_t k = 0; k < b->nruns; k++) {
        const sk_lrun *r = &v->layout.runs[b->runs + k];
        float f[4] = {r->x, r->y, r->px, r->width};
        h = fnv(h, f, sizeof f);
        h = fnv(h, &r->node, sizeof r->node);
        h = fnv(h, &r->n, sizeof r->n);
        for (uint32_t g = 0; g < r->n; g++)
            h = fnv(h, &v->layout.glyphs[r->first + g].glyph, sizeof(uint32_t));
    }
    return h;
}

static sk_rect rect_union(sk_rect a, sk_rect b)
{
    return sk_rect_union(a, b);
}

static bool near(sk_rect a, sk_rect b)
{
    return a.x <= b.x + b.w + 8 && b.x <= a.x + a.w + 8 && a.y <= b.y + b.h + 8 && b.y <= a.y + a.h + 8;
}

/* A stage-pixel rectangle to paint again, merged with any it touches. */
static void damage_px(sk_skinview *v, sk_rect r)
{
    sk_rect s = sk_rect_make(0, 0, stage_px(v).w, stage_px(v).h);
    r = sk_rect_intersect(r, s);
    if (sk_rect_empty(r))
        return;
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t i = 0; i < v->damage.len; i++)
            if (near(v->damage.ptr[i], r)) {
                r = rect_union(r, v->damage.ptr[i]);
                v->damage.ptr[i] = v->damage.ptr[--v->damage.len];
                merged = true;
                break;
            }
    }
    if (v->damage.len >= MAX_DAMAGE) {
        for (size_t i = 0; i < v->damage.len; i++)
            r = rect_union(r, v->damage.ptr[i]);
        v->damage.len = 0;
    }
    sk_vec_push(&v->damage, r);
}

/* A rectangle of the skin area under node n grown by the reach of its
 * ancestors' filters: a blur or a drop shadow of a group spreads what
 * changes inside it. Each reach in the ancestor's own units, scaled as
 * it is drawn. */
static sk_frect reach_of(const sk_skinview *v, uint32_t n, sk_frect r)
{
    if (sk_frect_empty(r))
        return r;
    for (uint32_t a = v->lnodes[n].parent; a != NONE && a < v->nnodes; a = v->lnodes[a].parent) {
        const sk_css_style *s = v->lnodes[a].style;
        if (!s || !s->nfilter)
            continue;
        sk_frect u = sk_layout_filter_reach(s, (sk_frect){0, 0, 1, 1});
        const sk_xform *m = &v->layout.boxes[a].xform;
        float d = -u.x * sqrtf(fabsf(m->a * m->d - m->b * m->c));
        if (d > 0)
            r = (sk_frect){r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d};
    }
    return r;
}

/* A rectangle of the skin area to paint again. */
static void damage_ink(sk_skinview *v, sk_frect r)
{
    if (sk_frect_empty(r))
        return;
    double f = v->k * v->scale;
    /* Clamped before they become integers: a hostile pack's ink may
     * reach past any. */
    long x0 = (long)fmax(-1e6, fmin(1e6, floor(r.x * f) - 2)), y0 = (long)fmax(-1e6, fmin(1e6, floor(r.y * f) - 2));
    long x1 = (long)fmax(-1e6, fmin(1e6, ceil((r.x + r.w) * f) + 2));
    long y1 = (long)fmax(-1e6, fmin(1e6, ceil((r.y + r.h) * f) + 2));
    damage_px(v, sk_rect_make((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0)));
}

/* The boxes in the order they paint (paint.c's Appendix E), for hit
 * tests: each box once, after what it paints over. */
static bool positioned(const sk_css_style *s)
{
    return s->position != SK_POS_STATIC;
}

static bool is_inline(const sk_skinview *v, uint32_t i)
{
    return v->lnodes[i].style->display == SK_DISPLAY_INLINE && v->lnodes[i].kind != SK_EL_SVG;
}

static bool makes_context(const sk_skinview *v, uint32_t i, uint32_t root)
{
    const sk_css_style *s = v->lnodes[i].style;
    if (i == root)
        return true;
    if (is_inline(v, i))
        return false;
    if (positioned(s) && (!s->z_auto || s->position == SK_POS_FIXED))
        return true;
    uint32_t parent = v->lnodes[i].parent;
    if (!s->z_auto && parent != NONE &&
        (v->lnodes[parent].style->display == SK_DISPLAY_FLEX || v->lnodes[parent].style->display == SK_DISPLAY_GRID))
        return true;
    return s->opacity < 1 || s->ntransform || s->nfilter || s->nclip || s->mix_blend != SK_MIX_NORMAL || s->isolate;
}

typedef struct {
    uint32_t box;
    int32_t z;
    uint32_t order;
} zentry;

typedef SK_VEC(zentry) zentries;

static void collect(const sk_skinview *v, uint32_t root, uint32_t x, zentries *out)
{
    for (uint32_t c = v->lnodes[x].first; c != NONE; c = v->lnodes[c].next) {
        const sk_lbox *b = &v->layout.boxes[c];
        if (!b->generated || b->svg_content)
            continue;
        const sk_css_style *s = v->lnodes[c].style;
        if (makes_context(v, c, root)) {
            zentry e = {c, s->z_auto ? 0 : s->z_index, (uint32_t)out->len};
            sk_vec_push(out, e);
            continue;
        }
        if (positioned(s) && !is_inline(v, c)) {
            zentry e = {c, 0, (uint32_t)out->len};
            sk_vec_push(out, e);
        }
        if (v->lnodes[c].kind != SK_EL_SVG)
            collect(v, root, c, out);
    }
}

static int by_z(const void *a, const void *b)
{
    const zentry *x = a, *y = b;
    if (x->z != y->z)
        return x->z < y->z ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

static void emit(sk_skinview *v, uint32_t n)
{
    v->order[v->norder++] = n;
}

static void order_context(sk_skinview *v, uint32_t r);

static void order_flow(sk_skinview *v, uint32_t x, uint32_t root)
{
    if (v->lnodes[x].kind == SK_EL_SVG)
        return;
    for (uint32_t c = v->lnodes[x].first; c != NONE; c = v->lnodes[c].next) {
        const sk_lbox *b = &v->layout.boxes[c];
        if (!b->generated || b->svg_content)
            continue;
        if (makes_context(v, c, root) || (positioned(v->lnodes[c].style) && !is_inline(v, c)))
            continue;
        emit(v, c);
        order_flow(v, c, root);
    }
}

static void order_entry(sk_skinview *v, uint32_t root, uint32_t e)
{
    if (makes_context(v, e, root)) {
        order_context(v, e);
    } else {
        emit(v, e);
        order_flow(v, e, root);
    }
}

static void order_context(sk_skinview *v, uint32_t r)
{
    zentries list = {0};
    collect(v, r, r, &list);
    if (list.len > 1)
        qsort(list.ptr, list.len, sizeof *list.ptr, by_z);
    emit(v, r);
    size_t k = 0;
    for (; k < list.len && list.ptr[k].z < 0; k++)
        order_entry(v, r, list.ptr[k].box);
    order_flow(v, r, r);
    for (; k < list.len; k++)
        order_entry(v, r, list.ptr[k].box);
    sk_vec_free(&list);
}

static void paint_order(sk_skinview *v)
{
    free(v->order);
    v->order = sk_xreallocarray(NULL, v->nnodes ? v->nnodes : 1, sizeof *v->order);
    v->norder = 0;
    if (v->stage != NONE && v->layout.boxes[v->stage].generated)
        order_context(v, v->stage);
}

static void rehover(sk_skinview *v);

/* The passes of a step. */
void sk_view_run(sk_skinview *v)
{
    link_tree(v);
    restyle(v);
    build(v);
    sk_layout old = v->layout;
    bool had = v->laid;
    sk_layout_run(&v->layout, v->lnodes, v->nnodes, v->stage, v->lw, v->lh, &v->lfonts);
    v->laid = true;
    if (had)
        sk_layout_free(&old);
    choose_layers(v);
    /* What each node paints now, what it painted kept aside. */
    sk_vrecord *was = sk_xreallocarray(NULL, v->nnodes ? v->nnodes : 1, sizeof *was);
    for (uint32_t n = 0; n < v->nnodes; n++) {
        sk_vnode *x = &v->nodes[n];
        const sk_lbox *b = &v->layout.boxes[n];
        was[n] = (sk_vrecord){x->drawn, x->ink, x->sig};
        x->drawn = b->generated;
        x->ink = x->drawn ? reach_of(v, n, sk_frect_map(&b->xform, b->ink)) : (sk_frect){0, 0, 0, 0};
        x->sig = x->drawn ? sk_view_signature(v, n, true) : 0;
    }
    paint_order(v);
    sk_fade_pick(v);
    for (uint32_t n = 0; n < v->nnodes; n++) {
        const sk_vnode *x = &v->nodes[n];
        if (v->all || (x->drawn == was[n].drawn && x->sig == was[n].sig))
            continue;
        if (sk_fade_absorbs(v, n, was[n].drawn, was[n].ink, x->drawn, x->ink))
            continue;
        if (was[n].drawn)
            damage_ink(v, was[n].ink);
        if (x->drawn)
            damage_ink(v, x->ink);
    }
    free(was);
    rehover(v);
}

double sk_skinview_step(sk_skinview *v, double now)
{
    double dt = (now - v->last) / 1000.0;
    v->last = v->now = now;
    v->wall = v->wall_fn(v->wall_ud);
    bool due = v->next_due >= 0 && now >= v->next_due;
    if (!v->dirty && !v->moving && !due)
        return v->next_due < 0 ? -1 : v->next_due - now;
    /* After a rest, the springs start from a frame's time, as a page's
     * animation loop runs on through the rest. */
    if (!v->moving)
        dt = fmin(dt, 1.0 / 60);
    v->moving = false;
    v->next_due = -1;
    if (v->metrics_new) {
        v->metrics_new = false;
        sk_widgets_update(v);
    }
    sk_widgets_frame(v, dt);
    v->dirty = false;
    sk_view_run(v);
    /* A hover the new layout moved restyles at the next frame. */
    if (v->moving || v->dirty)
        return 0;
    return v->next_due < 0 ? -1 : fmax(0, v->next_due - now);
}

/* ---- Drawing ---- */

static sk_rect handle_rect(const sk_skinview *v, uint32_t i, bool *shown)
{
    const sk_pack_split *s = &v->p->splits[i];
    double t[4];
    terminal_ltrb(v, t);
    double k = v->k, W = v->sw, H = v->sh;
    double x0 = t[0] * k, x1 = W - t[2] * k, y0 = t[1] * k, y1 = H - t[3] * k;
    double r[4];
    if (horizontal(s->edge)) {
        r[0] = s->edge == SK_SPLIT_LEFT ? x0 - SK_VIEW_GRIP : x1;
        r[1] = y0;
        r[2] = SK_VIEW_GRIP;
        r[3] = y1 - y0;
    } else {
        r[0] = x0;
        r[1] = s->edge == SK_SPLIT_TOP ? y0 - SK_VIEW_GRIP : y1;
        r[2] = x1 - x0;
        r[3] = SK_VIEW_GRIP;
    }
    double min, max, def;
    split_range(v, i, &min, &max, &def);
    *shown = min < max;
    double f = v->scale;
    long a = lround(r[0] * f), b = lround(r[1] * f), c = lround((r[0] + r[2]) * f), d = lround((r[1] + r[3]) * f);
    return sk_rect_make((int)a, (int)b, (int)(c - a), (int)(d - b));
}

static sk_rect menu_rect_px(const sk_skinview *v)
{
    double r[4];
    place_box(&v->p->meta.menu_rect, v->k, v->sw, v->sh, r);
    double f = v->scale;
    long a = lround(r[0] * f), b = lround(r[1] * f), c = lround((r[0] + r[2]) * f), d = lround((r[1] + r[3]) * f);
    return sk_rect_make((int)a, (int)b, (int)(c - a), (int)(d - b));
}

/* A rounded rectangle filled, less the rounded hole inside it when
 * hole_w is not 0. */
static void fill_rrect(sk_image *dst, sk_frect r, float rad, sk_frect hole, float hole_rad, sk_color c,
                       const sk_rect *clip)
{
    sk_path *p = sk_path_new();
    sk_path_round_rect(p, r.x, r.y, r.w, r.h, rad);
    if (hole.w > 0)
        sk_path_round_rect(p, hole.x, hole.y, hole.w, hole.h, hole_rad);
    sk_xform id = SK_XFORM_IDENTITY;
    sk_fill_path(dst, p, &id, hole.w > 0 ? SK_FILL_EVENODD : SK_FILL_NONZERO, c, clip);
    sk_path_free(p);
}

/* app.css's .split-handle: lit under the pointer and while dragged, a
 * grip at its middle with a dark ring. */
static void draw_handle(sk_skinview *v, sk_image *f, sk_rect st, uint32_t i, const sk_rect *clip)
{
    bool shown;
    sk_rect r = handle_rect(v, i, &shown);
    if (!shown)
        return;
    r.x += st.x;
    r.y += st.y;
    bool lit = v->handle_hover == (int)i || v->drag_split == (int)i;
    if (lit)
        sk_fill_rect(f, r, SK_RGBA(255, 255, 255, 41), SK_BLEND_SRC_OVER, clip);
    float s = (float)v->scale;
    sk_frect g;
    if (horizontal(v->p->splits[i].edge))
        g = (sk_frect){r.x + 2 * s, r.y + r.h / 2.0f - 20 * s, (float)r.w - 4 * s, 40 * s};
    else
        g = (sk_frect){r.x + r.w / 2.0f - 20 * s, r.y + 2 * s, 40 * s, (float)r.h - 4 * s};
    if (!(g.w > 0) || !(g.h > 0))
        return;
    sk_frect ring = {g.x - s, g.y - s, g.w + 2 * s, g.h + 2 * s};
    fill_rrect(f, ring, 3 * s, g, 2 * s, SK_RGBA(0, 0, 0, 115), clip);
    fill_rrect(f, g, 2 * s, (sk_frect){0, 0, 0, 0}, 0, lit ? SK_RGBA(255, 255, 255, 204) : SK_RGBA(255, 255, 255, 89),
               clip);
}

/* An image of the pack drawn into r, stretched, from its drawing
 * nearest the size. */
static void draw_image(sk_skinview *v, sk_image *dst, uint32_t source, sk_rect r, float brightness)
{
    if (source >= v->p->nimages || sk_rect_empty(r))
        return;
    const sk_pack_image *im = &v->p->images[source];
    int k = variant_for(im, r.w, r.h);
    if (k < 0)
        return;
    sk_image px;
    const sk_pack_variant *x = &im->variants[k];
    if (!sk_qoi_decode(v->p->blob + x->blob, x->len, SK_PACK_MAX_PIXELS, &px))
        return;
    sk_image tmp;
    sk_image_init(&tmp, r.w, r.h);
    sk_blit_scaled(&tmp, sk_image_bounds(&tmp), &px, sk_image_bounds(&px), SK_FILTER_BILINEAR, SK_BLEND_SRC, 255, NULL);
    if (brightness != 1)
        for (int y = 0; y < tmp.h; y++) {
            sk_px *row = sk_image_row(&tmp, y);
            for (int i = 0; i < tmp.w; i++) {
                uint32_t a = row[i] >> 24, out = a << 24;
                for (int sh = 0; sh < 24; sh += 8) {
                    float c = (float)((row[i] >> sh) & 255) * brightness;
                    uint32_t q = (uint32_t)lroundf(fminf(c, (float)a));
                    out |= q << sh;
                }
                row[i] = out;
            }
        }
    sk_blit(dst, r.x, r.y, &tmp, sk_image_bounds(&tmp), SK_BLEND_SRC_OVER, 255, NULL);
    sk_image_free(&tmp);
    sk_image_free(&px);
}

/* app.css's #menu-button: the skin's image, its hover image under the
 * pointer and its pressed image while held or open; a state the skin
 * gives no image the app's effect, brightness 1.35 and an outline
 * under the pointer, 1 px down at brightness 0.8 pressed; and the dot
 * of an unseen problem at its top right. */
static void draw_menu(sk_skinview *v, sk_image *f, sk_rect st, const sk_rect *clip)
{
    const sk_pack_meta *m = &v->p->meta;
    sk_rect r = menu_rect_px(v);
    r.x += st.x;
    r.y += st.y;
    bool down = v->menu_down || v->menu_open, hover = v->menu_hover;
    uint32_t img = m->menu_image;
    float bright = 1;
    int dy = 0;
    bool outline = false;
    if (hover) {
        if (m->menu_hover != NONE)
            img = m->menu_hover;
        else {
            bright = 1.35f;
            outline = true;
        }
    }
    if (down) {
        img = m->menu_pressed != NONE ? m->menu_pressed : m->menu_image;
        if (m->menu_pressed == NONE) {
            bright = 0.8f;
            dy = (int)lround(v->scale);
        }
    }
    r.y += dy;
    sk_rect area = clip ? sk_rect_intersect(*clip, sk_image_bounds(f)) : sk_image_bounds(f);
    sk_image view = sk_image_view(f, area);
    sk_rect local = sk_rect_make(r.x - area.x, r.y - area.y, r.w, r.h);
    draw_image(v, &view, img, local, bright);
    if (outline) {
        int s = SK_MAX(1, (int)lround(v->scale));
        sk_color c = SK_RGBA(255, 255, 255, 140);
        sk_fill_rect(f, sk_rect_make(r.x - s, r.y - s, r.w + 2 * s, s), c, SK_BLEND_SRC_OVER, clip);
        sk_fill_rect(f, sk_rect_make(r.x - s, r.y + r.h, r.w + 2 * s, s), c, SK_BLEND_SRC_OVER, clip);
        sk_fill_rect(f, sk_rect_make(r.x - s, r.y, s, r.h), c, SK_BLEND_SRC_OVER, clip);
        sk_fill_rect(f, sk_rect_make(r.x + r.w, r.y, s, r.h), c, SK_BLEND_SRC_OVER, clip);
    }
    if (v->unseen) {
        float s = (float)v->scale;
        float x = (float)(r.x + r.w) + 3 * s - 8 * s, y = (float)r.y - 3 * s;
        sk_path *p = sk_path_new();
        sk_path_ellipse(p, x - s, y - s, 10 * s, 10 * s);
        sk_xform id = SK_XFORM_IDENTITY;
        sk_fill_path(f, p, &id, SK_FILL_NONZERO, SK_RGB(0, 0, 0), clip);
        sk_path_clear(p);
        sk_path_ellipse(p, x, y, 8 * s, 8 * s);
        sk_fill_path(f, p, &id, SK_FILL_NONZERO, SK_RGB(255, 59, 48), clip);
        sk_path_free(p);
    }
}

/* What the menu button and the dot cover, in stage pixels. */
static sk_rect menu_reach(const sk_skinview *v)
{
    sk_rect r = menu_rect_px(v);
    int s = (int)ceil(v->scale) * 4;
    return sk_rect_make(r.x - s, r.y - s, r.w + 2 * s, r.h + 2 * s);
}

void sk_view_paint_skin(sk_skinview *v, sk_image *frame, sk_rect st, sk_rect r)
{
    sk_image view = sk_image_view(frame, r);
    sk_fill_rect(&view, sk_image_bounds(&view), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    if (v->laid && v->stage != NONE) {
        sk_paint_env env = sk_view_env(v);
        sk_paint_px(&view, &env, v->stage, (float)(v->k * v->scale), r.x - st.x, r.y - st.y);
    }
}

/* What the app draws over the skin, over r. */
static void overlays(sk_skinview *v, sk_image *frame, sk_rect st, sk_rect r)
{
    for (uint32_t i = 0; i < SK_MIN(v->p->nsplits, SK_VIEW_MAX_SPLITS); i++)
        draw_handle(v, frame, st, i, &r);
    if (v->p->meta.menu_image != NONE)
        draw_menu(v, frame, st, &r);
}

static void paint_piece(sk_skinview *v, sk_image *frame, sk_rect st, sk_rect r)
{
    sk_fade_paint(v, frame, st, r);
    overlays(v, frame, st, r);
}

size_t sk_skinview_draw(sk_skinview *v, sk_image *frame, bool all, sk_rect *out, size_t cap)
{
    sk_rect st = stage_px(v);
    if (all || v->all) {
        v->damage.len = 0;
        damage_px(v, sk_rect_make(0, 0, st.w, st.h));
        v->all = false;
    }
    /* A fading group kept anew: what lies below it painted whole. */
    if (sk_fade_prepare(v))
        damage_px(v, v->fade.r);
    sk_rect term = sk_skinview_terminal_px(v);
    size_t n = 0;
    for (size_t i = 0; i < v->damage.len; i++) {
        sk_rect d = v->damage.ptr[i];
        d.x += st.x;
        d.y += st.y;
        d = sk_rect_intersect(d, sk_image_bounds(frame));
        if (sk_rect_empty(d))
            continue;
        /* Around the terminal: above, below, left, right of it. */
        sk_rect t = sk_rect_intersect(term, d);
        sk_rect pieces[4];
        int np = 0;
        if (sk_rect_empty(t)) {
            pieces[np++] = d;
        } else {
            pieces[np++] = sk_rect_make(d.x, d.y, d.w, t.y - d.y);
            pieces[np++] = sk_rect_make(d.x, t.y + t.h, d.w, d.y + d.h - t.y - t.h);
            pieces[np++] = sk_rect_make(d.x, t.y, t.x - d.x, t.h);
            pieces[np++] = sk_rect_make(t.x + t.w, t.y, d.x + d.w - t.x - t.w, t.h);
        }
        for (int k = 0; k < np; k++) {
            if (sk_rect_empty(pieces[k]))
                continue;
            paint_piece(v, frame, st, pieces[k]);
            if (n < cap)
                out[n++] = pieces[k];
            else if (cap)
                out[cap - 1] = sk_rect_union(out[cap - 1], pieces[k]);
        }
    }
    v->damage.len = 0;
    /* The fading group at its new opacity over what is kept below. */
    sk_rect blended[4];
    size_t nb = sk_fade_recompose(v, frame, st, term, blended, 4);
    for (size_t i = 0; i < nb; i++) {
        overlays(v, frame, st, blended[i]);
        if (n < cap)
            out[n++] = blended[i];
        else if (cap)
            out[cap - 1] = sk_rect_union(out[cap - 1], blended[i]);
    }
    for (uint32_t i = 0; i < SK_MIN(v->p->nsplits, SK_VIEW_MAX_SPLITS); i++)
        v->handle_px[i] = handle_rect(v, i, &v->handle_shown[i]);
    v->menu_px = menu_reach(v);
    return n;
}

/* ---- The pointer ---- */

static bool in_rect(sk_rect r, double x, double y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* The point of the skin area under window point (x, y). */
static void to_skin(const sk_skinview *v, double x, double y, float *sx, float *sy)
{
    *sx = (float)((x - v->sx) / v->k);
    *sy = (float)((y - v->sy) / v->k);
}

static bool box_has(const sk_lbox *b, const sk_frect *r, float x, float y)
{
    sk_xform inv;
    if (!sk_xform_invert(&b->xform, &inv))
        return false;
    float u = inv.a * x + inv.c * y + inv.e, w = inv.b * x + inv.d * y + inv.f;
    return u >= r->x && w >= r->y && u < r->x + r->w && w < r->y + r->h;
}

/* The topmost box under (x, y) of the skin area whose pointer-events
 * is not none, inside every box that clips it; NONE for none. */
static uint32_t hit(const sk_skinview *v, float x, float y)
{
    if (!v->laid)
        return NONE;
    for (uint32_t k = v->norder; k-- > 0;) {
        uint32_t n = v->order[k];
        const sk_lbox *b = &v->layout.boxes[n];
        const sk_css_style *s = v->lnodes[n].style;
        if (!b->generated || !s || s->pointer_events == SK_PE_NONE || s->visibility != SK_VIS_VISIBLE)
            continue;
        if (!box_has(b, &b->border, x, y))
            continue;
        bool inside = true;
        for (uint32_t c = b->clipper; c != NONE && inside; c = v->layout.boxes[c].clipper)
            inside = box_has(&v->layout.boxes[c], &v->layout.boxes[c].padding, x, y);
        if (inside)
            return n;
    }
    return NONE;
}

/* The pointer over node n: it and its ancestors hovered, those of the
 * last node no longer. */
static void set_hover(sk_skinview *v, uint32_t n)
{
    if (n == v->hover)
        return;
    for (size_t i = 0; i < v->hovered.len; i++)
        sk_view_set_var(v, v->hovered.ptr[i], SK_VAR_HOVER, 0);
    v->hovered.len = 0;
    v->hover = n;
    for (uint32_t a = n; a != NONE && a < v->nnodes && v->hovered.len <= v->nnodes; a = v->lnodes[a].parent) {
        sk_view_set_var(v, a, SK_VAR_HOVER, 1);
        sk_vec_push(&v->hovered, a);
    }
}

/* What lies under a pointer that stays still while the skin moves:
 * hover follows the boxes, as a browser's does after a layout. The
 * next step restyles what it changed. */
static void rehover(sk_skinview *v)
{
    if (!v->pointer_in || v->press_kind == 3 || v->handle_hover >= 0 || v->menu_hover)
        return;
    uint32_t n = NONE;
    if (sk_skinview_covers(v, v->px, v->py)) {
        float sx, sy;
        to_skin(v, v->px, v->py, &sx, &sy);
        n = hit(v, sx, sy);
    }
    set_hover(v, n);
}

bool sk_skinview_covers(const sk_skinview *v, double x, double y)
{
    bool in_stage = x >= v->sx && y >= v->sy && x < v->sx + v->sw && y < v->sy + v->sh;
    double t[4];
    sk_skinview_terminal_rect(v, t);
    bool in_term = x >= t[0] && y >= t[1] && x < t[0] + t[2] && y < t[1] + t[3];
    return in_stage && !in_term;
}

/* The handle under window point (x, y), or -1. */
static int handle_at(const sk_skinview *v, double x, double y)
{
    double px = (x - v->sx) * v->scale, py = (y - v->sy) * v->scale;
    for (uint32_t i = 0; i < SK_MIN(v->p->nsplits, SK_VIEW_MAX_SPLITS); i++) {
        bool shown;
        sk_rect r = handle_rect(v, i, &shown);
        if (shown && in_rect(r, px, py))
            return (int)i;
    }
    return -1;
}

static bool menu_at(const sk_skinview *v, double x, double y)
{
    if (v->p->meta.menu_image == NONE)
        return false;
    double px = (x - v->sx) * v->scale, py = (y - v->sy) * v->scale;
    return in_rect(menu_rect_px(v), px, py);
}

/* What a click on node n's element does: a readout's dial, a toggle's
 * option; NONE for nothing. */
static uint32_t clickable(const sk_skinview *v, uint32_t n, sk_skin_event *ev)
{
    for (; n != NONE && n < v->nnodes; n = v->lnodes[n].parent) {
        const sk_pack_el *e = &v->p->els[v->nodes[n].el];
        if (e->role == SK_ROLE_OPTION_LIST || e->role == SK_ROLE_OPTION_OTHER) {
            if (ev) {
                ev->action = SK_SKIN_TOGGLE;
                ev->off = e->role == SK_ROLE_OPTION_OTHER;
            }
            return n;
        }
        if (e->role == SK_ROLE_WIDGET && e->widget < v->p->nwidgets) {
            const sk_pack_widget *w = &v->p->widgets[e->widget];
            if (w->type == SK_WIDGET_READOUT && w->format != SK_FORMAT_TEXT && w->bind) {
                if (ev) {
                    ev->action = SK_SKIN_DIAL;
                    ev->metric = w->bind;
                }
                return n;
            }
        }
    }
    return NONE;
}

static void hover_at(sk_skinview *v, double x, double y)
{
    int h = v->pointer_in ? handle_at(v, x, y) : -1;
    if (h != v->handle_hover) {
        if (v->handle_hover >= 0)
            damage_px(v, v->handle_px[v->handle_hover]);
        if (h >= 0) {
            bool shown;
            damage_px(v, handle_rect(v, (uint32_t)h, &shown));
        }
        v->handle_hover = h;
        v->dirty = true;
    }
    bool m = v->pointer_in && h < 0 && menu_at(v, x, y);
    if (m != v->menu_hover) {
        v->menu_hover = m;
        damage_px(v, menu_reach(v));
        v->dirty = true;
    }
    uint32_t n = NONE;
    if (v->pointer_in && h < 0 && !m && sk_skinview_covers(v, x, y)) {
        float sx, sy;
        to_skin(v, x, y, &sx, &sy);
        n = hit(v, sx, sy);
    }
    set_hover(v, n);
}

sk_skin_event sk_skinview_mouse(sk_skinview *v, sk_skin_mouse what, double x, double y, int button)
{
    sk_skin_event ev = {SK_SKIN_NOTHING, NULL, false, NULL, 0};
    if (what == SK_SKIN_MOUSE_LEAVE) {
        v->pointer_in = false;
        if (v->press_kind != 3)
            hover_at(v, x, y);
        return ev;
    }
    v->pointer_in = true;
    v->px = x;
    v->py = y;
    if (v->press_kind == 3 && v->drag_split >= 0) {
        uint32_t i = (uint32_t)v->drag_split;
        const sk_pack_split *s = &v->p->splits[i];
        double pos = horizontal(s->edge) ? x : y;
        double sign = s->edge == SK_SPLIT_LEFT || s->edge == SK_SPLIT_TOP ? 1 : -1;
        ev.split = s->id;
        if (what == SK_SKIN_MOUSE_UP) {
            /* Let go: the size laid out, rounded, kept. */
            v->live[i] = v->drag_start + sign * (pos - v->drag_from) / v->k;
            v->has_live[i] = true;
            place_rects(v);
            v->saved[i] = round(v->at[i]);
            v->has_saved[i] = true;
            v->has_live[i] = false;
            v->drag_split = -1;
            v->press_kind = 0;
            place_rects(v);
            ev.action = SK_SKIN_SPLIT_DONE;
            ev.size = v->saved[i];
            hover_at(v, x, y);
        } else {
            v->live[i] = v->drag_start + sign * (pos - v->drag_from) / v->k;
            v->has_live[i] = true;
            place_rects(v);
            ev.action = SK_SKIN_SPLIT_MOVE;
        }
        v->dirty = v->all = true;
        return ev;
    }
    if (what == SK_SKIN_MOUSE_MOVE) {
        hover_at(v, x, y);
        if (v->press_kind || sk_skinview_covers(v, x, y))
            ev.action = SK_SKIN_TAKEN;
        return ev;
    }
    if (what == SK_SKIN_MOUSE_UP) {
        if (v->menu_down) {
            v->menu_down = false;
            damage_px(v, menu_reach(v));
            v->dirty = true;
        }
        int kind = v->press_kind;
        uint32_t was = v->pressed;
        v->press_kind = 0;
        v->pressed = NONE;
        if (kind == 2)
            ev.action = SK_SKIN_TAKEN;
        if (kind == 1) {
            ev.action = SK_SKIN_TAKEN;
            float sx, sy;
            to_skin(v, x, y, &sx, &sy);
            uint32_t under = sk_skinview_covers(v, x, y) ? hit(v, sx, sy) : NONE;
            sk_skin_event click = ev;
            uint32_t a = clickable(v, was, &click);
            if (a != NONE && clickable(v, under, NULL) == a && button == 1)
                ev = click;
        }
        hover_at(v, x, y);
        return ev;
    }
    /* A press. */
    hover_at(v, x, y);
    if (button == 1 && menu_at(v, x, y)) {
        v->menu_down = true;
        v->press_kind = 2;
        damage_px(v, menu_reach(v));
        v->dirty = true;
        ev.action = SK_SKIN_MENU;
        return ev;
    }
    int h = handle_at(v, x, y);
    if (h >= 0) {
        if (button == 1) {
            const sk_pack_split *s = &v->p->splits[h];
            v->press_kind = 3;
            v->drag_split = h;
            v->drag_from = horizontal(s->edge) ? x : y;
            v->drag_start = v->at[h];
            ev.split = s->id;
            v->dirty = true;
            damage_px(v, v->handle_px[h]);
        }
        ev.action = SK_SKIN_TAKEN;
        return ev;
    }
    if (!sk_skinview_covers(v, x, y))
        return ev;
    v->press_kind = 1;
    v->pressed = v->hover;
    ev.action = SK_SKIN_TAKEN;
    return ev;
}

sk_skin_cursor sk_skinview_cursor(const sk_skinview *v)
{
    int h = v->press_kind == 3 ? v->drag_split : v->handle_hover;
    if (h >= 0)
        return horizontal(v->p->splits[h].edge) ? SK_SKIN_CURSOR_RESIZE_EW : SK_SKIN_CURSOR_RESIZE_NS;
    if (v->menu_hover)
        return SK_SKIN_CURSOR_POINTER;
    if (!v->pointer_in || !sk_skinview_covers(v, v->px, v->py))
        return SK_SKIN_CURSOR_NONE;
    if (v->hover == NONE || !v->lnodes[v->hover].style)
        return SK_SKIN_CURSOR_ARROW;
    switch (v->lnodes[v->hover].style->cursor) {
    case SK_CURSOR_POINTER:
        return SK_SKIN_CURSOR_POINTER;
    case SK_CURSOR_TEXT:
        return SK_SKIN_CURSOR_TEXT;
    default:
        return SK_SKIN_CURSOR_ARROW;
    }
}
