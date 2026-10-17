/* skinc's layer pass. The pack is first written without layers and
 * loaded as the runtime loads it, and a view of it (src/skin/view.h)
 * lays it out: the layers are made by the runtime's own layout and
 * painter, so a layer is a cache of what the runtime would paint, never
 * a second rendering of the same CSS.
 *
 * Which subtrees, element by element in tree order:
 *   - None of it shows a value: no text, fill, arc, mark, turn, frame,
 *     row or meter, no length the runtime fills in. A knob's turning
 *     group may turn (a SPRITE) when it holds an image, which the
 *     painter resamples whenever it turns anyway; a needle of lines is
 *     drawn turned as paths, sharper than a turned picture, and left to
 *     the painter.
 *   - Its paint is a function of its root's style: every combination of
 *     the variables any element of it reads is tried, at most 4096, and
 *     each root style must give one picture. Up to 8 pictures; one for
 *     every state shown is STATIC, else each is a STATE layer of the
 *     styles it stands for. The root's opacity and blend are the
 *     compositor's, so they may differ between its states; a root whose
 *     animation moves it is a SPRITE, its transform the compositor's
 *     too. A descendant may not animate.
 *   - It keeps its geometry: laid out at the skin's size, at its least
 *     and at a larger one, each with the splits at their least and
 *     most, every box of it sits where it sat from the root, at the
 *     size it had; an SVG shape or group against its svg's box.
 *   - It paints at one place in the painting order: a stacking context
 *     of its own, or a positioned box whose positioned descendants all
 *     stack at z 0 and that has no outline, or a box in flow with none,
 *     nothing in it blending with what lies below it.
 *   - It is worth a picture: an image, a gradient, a blurred shadow, a
 *     filter, a clip-path, rounded corners, text, an SVG shape, or four
 *     boxes painted. A box of one colour is painted as fast as drawn.
 * A subtree with a layer for every state it shows holds its
 * descendants' paint, which get none of their own.
 *
 * NINE: a box no layer covers, of one style, with box shadows or a
 * border, its corners' radii not percentages: its shadows and border
 * painted for a box just large enough that its middle row and column
 * are the same at every size, sliced there; kept only when stretching
 * it to the box laid out at the skin's size gives what painting it
 * gives, within 2 levels.
 *
 * Each layer is painted at 1x and 2x, its top left on a whole device
 * pixel at the place the layout puts it at the skin's size, so where
 * the runtime draws the box on the same pixel grid its pixels land one
 * for one. What is left out: MASK, the clip a changing child is drawn
 * through, as a picture. The two the shipped skins have, coffee's lava
 * lamp's clip-path and phosphor's round radar sheet, the painter
 * rasterizes in 6 and 19 microseconds a frame at 2x, against frames of
 * 1.5 to 5 ms; their coverage is multiplied in either way. A mask would
 * add bytes and a decode for under one percent of a frame. */
#include "layers.h"

#include "../src/base/map.h"
#include "../src/gfx/image.h"
#include "../src/skin/packfmt.h"
#include "../src/skin/viewint.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NONE SK_PACK_NONE
/* The window sizes and split positions a subtree must keep its
 * geometry at; the first is the skin's own size, where it is painted. */
#define CONFIGS 5
#define MAX_COMBOS 4096u
#define MAX_IMAGES 8u
#define MAX_STYLES 16u
/* A layer's pixels at 2x, at most: past it a subtree is painted. */
#define MAX_LAYER_PIXELS (2048u * 2048u)
/* A nine-slice's middle may differ from a paint by this much. */
#define NINE_TOLERANCE 2

/* A state variable set: owner is an element, whose node it is. */
typedef struct {
    uint32_t owner;
    uint8_t var, value;
} lset;

typedef SK_VEC(lset) lsets;

/* A picture of one element: a SUBTREE layer, or its NINE. */
typedef struct {
    uint32_t el;
    uint8_t kind;
    bool every;              /* style none: every state it shows */
    SK_VEC(uint32_t) styles; /* the root styles it stands for */
    lsets state;             /* the variables that show it */
    uint64_t key;            /* its picture: the root's paint and its descendants' styles */
    uint64_t geom;           /* its geometry where its state was laid out */
    bool has_geom;
    bool fails;
    int round;
    bool chosen;
} ljob;

typedef SK_VEC(uint32_t) u32s;

typedef struct {
    const sk_pack *p;
    sk_skinpack *sp;
    sk_skinview *v;
    uint32_t n;
    uint8_t *dyn;     /* per element: shows a value */
    uint8_t *gotkey;  /* per style: keys made */
    uint64_t *rkey;   /* per style: its paint as a root's */
    uint64_t *dkey;   /* per style: its paint as a descendant's */
    SK_VEC(ljob) jobs;
    uint32_t *covered; /* per element: an ancestor's layer holds it in every state */
    int rounds;
    sk_wlayers *out;
    sk_layer_summary *summary;
} lpass;

/* ---- Hashing ---- */

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

#define FNV0 1469598103934665603ull

/* A float to 1/1024 px, so the same place found by another sum hashes
 * alike. */
static uint64_t fnv_f(uint64_t h, float f)
{
    long q = isfinite(f) ? lroundf(f * 1024) : 0x7fffffff;
    return fnv(h, &q, sizeof q);
}

/* ---- Styles ---- */

static uint32_t probe_str(void *data, const char *s)
{
    (void)data;
    return s ? (uint32_t)fnv(FNV0, s, strlen(s)) : NONE;
}

/* Whether a property is the compositor's for a layer's root: opacity,
 * blend, transform and what moves it, z-index, and what paints
 * nothing. */
static bool root_free(uint16_t id)
{
    switch (id) {
    case SK_PROP_OPACITY:
    case SK_PROP_MIX_BLEND_MODE:
    case SK_PROP_TRANSFORM:
    case SK_PROP_TRANSFORM_ORIGIN:
    case SK_PROP_Z_INDEX:
        return true;
    default:
        break;
    }
    return id >= SK_PROP_ANIMATION_NAME && id <= SK_PROP_TRANSITION_DELAY;
}

static bool paints_nothing(uint16_t id)
{
    return id == SK_PROP_CURSOR || id == SK_PROP_POINTER_EVENTS || id == SK_PROP_USER_SELECT ||
           (id >= SK_PROP_TRANSITION_PROPERTY && id <= SK_PROP_TRANSITION_DELAY);
}

/* A hash of what style si paints, as a root's and as a descendant's. */
static void keys_of(lpass *L, uint32_t si)
{
    if (L->gotkey[si])
        return;
    L->gotkey[si] = 1;
    const sk_css_style *s = &L->p->styles[si];
    uint64_t r = FNV0, d = FNV0;
    sk_buf b = {0};
    for (uint16_t id = 1; id < SK_PROP_COUNT; id++) {
        sk_buf_clear(&b);
        if (!sk_pack_put_prop(&b, id, s, probe_str, NULL) || paints_nothing(id))
            continue;
        uint64_t h = fnv(fnv(FNV0, &id, sizeof id), b.ptr ? b.ptr : "", b.len);
        d = fnv(d, &h, sizeof h);
        if (!root_free(id))
            r = fnv(r, &h, sizeof h);
    }
    sk_buf_free(&b);
    L->rkey[si] = r;
    L->dkey[si] = d;
}

static bool animates(const sk_css_style *s)
{
    for (uint16_t i = 0; i < s->nanim_name; i++)
        if (s->anim_name[i] != NONE)
            return true;
    return false;
}

/* Whether a style's animations move it: a track that sets transform. */
static bool animates_transform(const sk_pack *p, const sk_css_style *s)
{
    for (uint16_t i = 0; i < s->nanim_name; i++) {
        uint32_t k = s->anim_name[i];
        if (k == NONE || k >= p->nkeyframes)
            continue;
        for (uint16_t j = 0; j < p->keyframes[k].nkeys; j++)
            if (p->keyframes[k].keys[j].has_transform)
                return true;
    }
    return false;
}

/* ---- The tree ---- */

static void subtree(const sk_pack *p, uint32_t e, u32s *out, int depth)
{
    sk_vec_push(out, e);
    if (depth > 200)
        return;
    for (uint32_t c = p->els[e].first_child; c != NONE; c = p->els[c].next_sibling)
        subtree(p, c, out, depth + 1);
}

static bool is_svg_content(const sk_pack *p, uint32_t e)
{
    return p->els[e].kind >= SK_EL_G && p->els[e].kind <= SK_EL_IMAGE;
}

static uint32_t anchor_of(const sk_pack *p, uint32_t e)
{
    uint32_t a = e;
    while (a != NONE && is_svg_content(p, a))
        a = p->els[a].parent;
    return a == NONE ? e : a;
}

/* A knob's turning group that holds an image or text: the painter
 * turns its picture whenever it turns, so a picture made once loses
 * nothing. */
static bool turns_a_picture(const sk_pack *p, uint32_t e)
{
    if (p->els[e].role != SK_ROLE_KNOB_TURN)
        return false;
    u32s sub = {0};
    subtree(p, e, &sub, 0);
    bool pic = false;
    for (size_t i = 0; i < sub.len; i++)
        pic |= p->els[sub.ptr[i]].kind == SK_EL_IMAGE || p->els[sub.ptr[i]].kind == SK_EL_TEXT;
    sk_vec_free(&sub);
    return pic;
}

/* ---- States ---- */

static uint8_t value_in(const lset *s, size_t n, uint32_t owner, uint8_t var)
{
    for (size_t i = 0; i < n; i++)
        if (s[i].owner == owner && s[i].var == var)
            return s[i].value;
    return 0;
}

static uint32_t style_in(const sk_pack *p, uint32_t e, const lset *s, size_t n)
{
    const sk_pack_el *el = &p->els[e];
    uint32_t at = 0, stride = 1;
    for (uint8_t i = 0; i < el->ninputs; i++) {
        uint8_t v = value_in(s, n, el->inputs[i].owner, el->inputs[i].var);
        if (v >= el->inputs[i].radix)
            v = (uint8_t)(el->inputs[i].radix - 1);
        at += v * stride;
        stride *= el->inputs[i].radix;
    }
    return el->styles[at];
}

/* The picture of e's subtree in state s: its root's paint and each
 * descendant's; *anim when a descendant animates. */
static uint64_t picture_of(lpass *L, const u32s *sub, const lset *s, size_t n, bool *anim)
{
    uint32_t root = sub->ptr[0];
    uint32_t si = style_in(L->p, root, s, n);
    keys_of(L, si);
    uint64_t h = fnv(FNV0, &L->rkey[si], sizeof L->rkey[si]);
    *anim = false;
    for (size_t i = 1; i < sub->len; i++) {
        uint32_t di = style_in(L->p, sub->ptr[i], s, n);
        keys_of(L, di);
        h = fnv(h, &L->dkey[di], sizeof L->dkey[di]);
        *anim |= animates(&L->p->styles[di]);
    }
    return h;
}

/* The subtree's inputs: each variable any of it reads, once, its radix
 * in value. */
static void inputs_of(const sk_pack *p, const u32s *sub, lsets *out)
{
    for (size_t i = 0; i < sub->len; i++) {
        const sk_pack_el *el = &p->els[sub->ptr[i]];
        for (uint8_t k = 0; k < el->ninputs; k++) {
            bool have = false;
            for (size_t j = 0; j < out->len && !have; j++)
                have = out->ptr[j].owner == el->inputs[k].owner && out->ptr[j].var == el->inputs[k].var;
            if (!have) {
                lset x = {el->inputs[k].owner, el->inputs[k].var, el->inputs[k].radix};
                sk_vec_push(out, x);
            }
        }
    }
}

static ljob *job_new(lpass *L, uint32_t e, uint8_t kind)
{
    ljob j;
    memset(&j, 0, sizeof j);
    j.el = e;
    j.kind = kind;
    j.round = -1;
    sk_vec_push(&L->jobs, j);
    return &L->jobs.ptr[L->jobs.len - 1];
}

static void job_free(ljob *j)
{
    sk_vec_free(&j->styles);
    sk_vec_free(&j->state);
}

/* Element e's pictures, by the states of what its subtree reads. */
static void consider(lpass *L, uint32_t e)
{
    const sk_pack *p = L->p;
    /* A shape a needle or knob turns is drawn turned as a path. */
    for (uint32_t a = p->els[e].parent; a != NONE && is_svg_content(p, a); a = p->els[a].parent)
        if (p->els[a].role == SK_ROLE_NEEDLE_GROUP || p->els[a].role == SK_ROLE_KNOB_TURN)
            return;
    u32s sub = {0};
    subtree(p, e, &sub, 0);
    bool turn = turns_a_picture(p, e);
    bool ok = true;
    for (size_t i = 0; i < sub.len && ok; i++)
        ok = !L->dyn[sub.ptr[i]] || (i == 0 && turn);
    lsets ins = {0};
    if (ok)
        inputs_of(p, &sub, &ins);
    uint64_t combos = 1;
    for (size_t i = 0; ok && i < ins.len; i++) {
        combos *= ins.ptr[i].value;
        ok = combos <= MAX_COMBOS;
    }
    size_t first = L->jobs.len;
    /* Each root style's picture, and whether it may be one. */
    SK_VEC(uint32_t) seen = {0};
    SK_VEC(uint64_t) seen_key = {0};
    bool all_one = true;
    lset *s = ins.len ? sk_xcalloc(ins.len, sizeof *s) : NULL;
    for (uint64_t c = 0; ok && c < combos; c++) {
        uint64_t rest = c;
        for (size_t i = 0; i < ins.len; i++) {
            s[i] = (lset){ins.ptr[i].owner, ins.ptr[i].var, (uint8_t)(rest % ins.ptr[i].value)};
            rest /= ins.ptr[i].value;
        }
        uint32_t si = style_in(p, e, s, ins.len);
        const sk_css_style *rs = &p->styles[si];
        if (rs->display == SK_DISPLAY_NONE)
            continue;
        bool anim = false;
        uint64_t key = picture_of(L, &sub, s, ins.len, &anim);
        bool sprite = turn || animates_transform(p, rs);
        if (anim || (rs->ntransform && !sprite))
            key = 0; /* none for this style */
        size_t k = 0;
        while (k < seen.len && seen.ptr[k] != si)
            k++;
        if (k < seen.len) {
            ok = seen_key.ptr[k] == key;
            continue;
        }
        sk_vec_push(&seen, si);
        sk_vec_push(&seen_key, key);
        if (!key) {
            all_one = false;
            continue;
        }
        ljob *j = NULL;
        for (size_t q = first; q < L->jobs.len && !j; q++)
            if (L->jobs.ptr[q].key == key)
                j = &L->jobs.ptr[q];
        if (!j) {
            j = job_new(L, e, sprite ? SK_LAYER_SPRITE : SK_LAYER_STATE);
            j->key = key;
            for (size_t i = 0; i < ins.len; i++)
                sk_vec_push(&j->state, s[i]);
        }
        sk_vec_push(&j->styles, si);
    }
    size_t made = L->jobs.len - first;
    if (!ok || made > MAX_IMAGES || seen.len > MAX_STYLES) {
        for (size_t q = first; q < L->jobs.len; q++)
            job_free(&L->jobs.ptr[q]);
        L->jobs.len = first;
    } else if (made == 1 && all_one) {
        ljob *j = &L->jobs.ptr[first];
        j->every = true;
        if (j->kind == SK_LAYER_STATE)
            j->kind = SK_LAYER_STATIC;
    }
    free(s);
    sk_vec_free(&seen);
    sk_vec_free(&seen_key);
    sk_vec_free(&ins);
    sk_vec_free(&sub);
}

/* ---- Layout ---- */

/* The view in state s at configuration c: the window's size and the
 * splits' positions, every motion started anew. */
static void lay_out(lpass *L, const lset *s, size_t n, int c)
{
    sk_skinview *v = L->v;
    const sk_pack_meta *m = &L->p->meta;
    for (uint32_t i = 0; i < v->nnodes; i++) {
        sk_vnode *x = &v->nodes[i];
        memset(x->var, 0, sizeof x->var);
        sk_motion_free(x->mo);
        x->mo = NULL;
        x->base = NULL;
    }
    for (size_t i = 0; i < n; i++)
        sk_view_set_var(v, s[i].owner, s[i].var, s[i].value);
    double w = m->width, h = m->height;
    if (c == 1 || c == 2) {
        w = m->min_width > 0 ? m->min_width : w;
        h = m->min_height > 0 ? m->min_height : h;
    } else if (c == 3 || c == 4) {
        w += 320;
        h += 240;
    }
    const char *ids[SK_VIEW_MAX_SPLITS];
    double sizes[SK_VIEW_MAX_SPLITS];
    size_t ns = SK_MIN(L->p->nsplits, (uint32_t)SK_VIEW_MAX_SPLITS);
    for (size_t i = 0; i < ns; i++) {
        ids[i] = L->p->splits[i].id;
        sizes[i] = c == 1 || c == 4 ? L->p->splits[i].min : c == 2 || c == 3 ? L->p->splits[i].max : NAN;
    }
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w, h - SK_VIEW_STRIP, 1);
    sk_skinview_set_panels(v, ids, sizes, c == 0 ? 0 : ns);
    sk_view_run(v);
}

static sk_xform xf_mul(sk_xform a, sk_xform b)
{
    return sk_xform_mul(&a, &b);
}

/* A box's geometry from its subtree's anchor at (ax, ay), its own
 * transform left out for the root. */
static uint64_t box_geom(uint64_t h, const sk_layout *l, uint32_t d, bool root, float ax, float ay)
{
    const sk_lbox *b = &l->boxes[d];
    uint8_t g = b->generated;
    h = fnv(h, &g, 1);
    if (!g)
        return h;
    if (b->svg_content) {
        if (!root) {
            const float m[6] = {b->local.a, b->local.b, b->local.c, b->local.d, b->local.e, b->local.f};
            for (int k = 0; k < 6; k++)
                h = fnv_f(h, m[k]);
        }
        const float f[4] = {b->ink.x, b->ink.y, b->ink.w, b->ink.h};
        for (int k = 0; k < 4; k++)
            h = fnv_f(h, f[k]);
    } else {
        const sk_frect r[4] = {b->border, b->padding, b->content, b->ink};
        for (int k = 0; k < 4; k++) {
            h = fnv_f(h, r[k].x - ax);
            h = fnv_f(h, r[k].y - ay);
            h = fnv_f(h, r[k].w);
            h = fnv_f(h, r[k].h);
        }
        if (!root && b->has_xform) {
            /* Its transform about the anchor: T(-a) local T(a). */
            sk_xform t0 = {1, 0, 0, 1, -ax, -ay}, t1 = {1, 0, 0, 1, ax, ay};
            sk_xform m = xf_mul(xf_mul(t0, b->local), t1);
            const float f[6] = {m.a, m.b, m.c, m.d, m.e, m.f};
            for (int k = 0; k < 6; k++)
                h = fnv_f(h, f[k]);
        }
        if (l->nodes[d].kind == SK_EL_SVG) {
            const float f[6] = {b->viewbox.a, b->viewbox.b, b->viewbox.c, b->viewbox.d, b->viewbox.e - ax,
                                b->viewbox.f - ay};
            for (int k = 0; k < 6; k++)
                h = fnv_f(h, f[k]);
        }
    }
    for (uint32_t k = 0; k < b->nruns; k++) {
        const sk_lrun *r = &l->runs[b->runs + k];
        float ox = b->svg_content ? 0 : ax, oy = b->svg_content ? 0 : ay;
        h = fnv_f(fnv_f(fnv_f(fnv_f(h, r->x - ox), r->y - oy), r->px), r->width);
        h = fnv(h, &r->node, sizeof r->node);
        h = fnv(h, &r->n, sizeof r->n);
        for (uint32_t q = 0; q < r->n; q++)
            h = fnv(h, &l->glyphs[r->first + q].glyph, sizeof(uint32_t));
    }
    return h;
}

/* Element e's subtree's geometry from its anchor. */
static uint64_t geom_of(const lpass *L, uint32_t e)
{
    const sk_layout *l = &L->v->layout;
    uint32_t a = anchor_of(L->p, e);
    const sk_lbox *ab = &l->boxes[a];
    if (!ab->generated)
        return 0;
    uint64_t h = FNV0;
    float ax = ab->border.x, ay = ab->border.y;
    if (a != e) {
        /* The svg's box and mapping, from its own corner. */
        h = box_geom(h, l, a, true, ax, ay);
        const float f[4] = {ab->xform.a, ab->xform.b, ab->xform.c, ab->xform.d};
        for (int k = 0; k < 4; k++)
            h = fnv_f(h, f[k]);
    }
    u32s sub = {0};
    subtree(L->p, e, &sub, 0);
    for (size_t i = 0; i < sub.len; i++) {
        h = box_geom(h, l, sub.ptr[i], i == 0, ax, ay);
        uint32_t c = l->boxes[sub.ptr[i]].clipper;
        bool inside = false;
        for (size_t q = 0; q < sub.len && !inside; q++)
            inside = sub.ptr[q] == c;
        uint32_t cl = inside ? c : NONE;
        h = fnv(h, &cl, sizeof cl);
    }
    sk_vec_free(&sub);
    return h;
}

/* Whether e's subtree paints at one place in the painting order, as
 * laid out now. */
static bool paints_at_once(const lpass *L, uint32_t e)
{
    const sk_layout *l = &L->v->layout;
    const sk_lbox *b = &l->boxes[e];
    const sk_css_style *s = l->nodes[e].style;
    if (!b->generated || !s)
        return false;
    if (b->svg_content)
        return true;
    if (s->display == SK_DISPLAY_INLINE && l->nodes[e].kind != SK_EL_SVG)
        return false;
    bool positioned = s->position != SK_POS_STATIC;
    /* A context whatever its opacity does: what it holds paints inside
     * its group. */
    bool filtered = false;
    for (uint16_t i = 0; i < s->nfilter; i++)
        filtered |= s->filter[i].kind == SK_FX_BLUR ? s->filter[i].amount != 0 : true;
    if ((positioned && (!s->z_auto || s->position == SK_POS_FIXED)) || filtered || s->nclip ||
        s->mix_blend != SK_MIX_NORMAL || s->isolate || s->ntransform)
        return true;
    if (positioned && s->outline_style == SK_BORDER_SOLID && s->outline_width > 0)
        return false;
    u32s sub = {0};
    subtree(L->p, e, &sub, 0);
    bool ok = true;
    for (size_t i = 1; i < sub.len && ok; i++) {
        uint32_t d = sub.ptr[i];
        const sk_lbox *db = &l->boxes[d];
        const sk_css_style *ds = l->nodes[d].style;
        if (!db->generated || db->svg_content || !ds)
            continue;
        if (ds->mix_blend != SK_MIX_NORMAL)
            ok = false;
        bool inl = ds->display == SK_DISPLAY_INLINE && l->nodes[d].kind != SK_EL_SVG;
        bool ctx = sk_paint_makes_context(l, d, NONE);
        bool entry = ctx || (ds->position != SK_POS_STATIC && !inl);
        if (!entry)
            continue;
        int32_t z = ctx && !ds->z_auto ? ds->z_index : 0;
        if (!positioned || z != 0)
            ok = false;
    }
    sk_vec_free(&sub);
    return ok;
}

/* Whether e's subtree in its laid out styles paints enough to be worth
 * a picture. */
static bool worth(const lpass *L, uint32_t e)
{
    const sk_layout *l = &L->v->layout;
    u32s sub = {0};
    subtree(L->p, e, &sub, 0);
    bool yes = false;
    int painted = 0;
    for (size_t i = 0; i < sub.len && !yes; i++) {
        uint32_t d = sub.ptr[i];
        const sk_css_style *s = l->nodes[d].style;
        if (!l->boxes[d].generated || !s)
            continue;
        for (uint16_t k = 0; k < s->nbg_image; k++)
            yes |= s->bg_image[k].kind != SK_IMAGE_NONE;
        for (uint16_t k = 0; k < s->nbox_shadow; k++)
            yes |= s->box_shadow[k].color.a > 0 && (s->box_shadow[k].blur > 0 || s->box_shadow[k].spread != 0);
        yes |= s->ntext_shadow > 0 || s->nfilter > 0 || s->nclip > 0 || s->bi_source.kind != SK_IMAGE_NONE;
        for (int k = 0; k < 4; k++)
            yes |= s->radius[k][0].px > 0 || s->radius[k][0].pct > 0;
        yes |= l->boxes[d].nruns > 0;
        const sk_pack_svg *g = l->nodes[d].svg;
        yes |= g && (g->kind == SK_SVG_ARC || g->kind == SK_SVG_CIRCLE || g->kind == SK_SVG_LINE ||
                     g->kind == SK_SVG_IMAGE || g->kind == SK_SVG_TEXT);
        if (s->bg_color.a > 0 || s->border_width[0] > 0 || s->border_width[1] > 0 || s->border_width[2] > 0 ||
            s->border_width[3] > 0)
            painted++;
    }
    sk_vec_free(&sub);
    return yes || painted >= 4;
}

/* ---- Rounds ---- */

/* Gives each job a round whose variables agree with its state: a round
 * sets every variable of the jobs in it, the rest 0. */
static void plan_rounds(lpass *L)
{
    SK_VEC(lsets) rounds = {0};
    for (size_t i = 0; i < L->jobs.len; i++) {
        ljob *j = &L->jobs.ptr[i];
        if (j->kind == SK_LAYER_NINE) {
            j->round = 0;
            continue;
        }
        int r = 0;
        for (;; r++) {
            if ((size_t)r == rounds.len) {
                lsets none = {0};
                sk_vec_push(&rounds, none);
            }
            lsets *R = &rounds.ptr[r];
            bool clash = false;
            for (size_t k = 0; k < j->state.len && !clash; k++)
                for (size_t q = 0; q < R->len && !clash; q++)
                    clash = R->ptr[q].owner == j->state.ptr[k].owner && R->ptr[q].var == j->state.ptr[k].var &&
                            R->ptr[q].value != j->state.ptr[k].value;
            if (!clash)
                break;
        }
        lsets *R = &rounds.ptr[r];
        for (size_t k = 0; k < j->state.len; k++) {
            bool have = false;
            for (size_t q = 0; q < R->len && !have; q++)
                have = R->ptr[q].owner == j->state.ptr[k].owner && R->ptr[q].var == j->state.ptr[k].var;
            if (!have)
                sk_vec_push(R, j->state.ptr[k]);
        }
        j->round = r;
    }
    L->rounds = (int)SK_MAX(rounds.len, 1);
    for (size_t r = 0; r < rounds.len; r++)
        sk_vec_free(&rounds.ptr[r]);
    sk_vec_free(&rounds);
}

/* Round r's variables: those of its jobs. */
static void round_state(const lpass *L, int r, lsets *out)
{
    out->len = 0;
    for (size_t i = 0; i < L->jobs.len; i++) {
        const ljob *j = &L->jobs.ptr[i];
        if (j->round != r || j->kind == SK_LAYER_NINE)
            continue;
        for (size_t k = 0; k < j->state.len; k++) {
            bool have = false;
            for (size_t q = 0; q < out->len && !have; q++)
                have = out->ptr[q].owner == j->state.ptr[k].owner && out->ptr[q].var == j->state.ptr[k].var;
            if (!have)
                sk_vec_push(out, j->state.ptr[k]);
        }
    }
}

/* Whether job j's picture is what its element shows as laid out now. */
static bool job_shown(lpass *L, const ljob *j)
{
    const sk_vnode *x = &L->v->nodes[j->el];
    if (!x->live || !x->base || !L->v->layout.boxes[j->el].generated)
        return false;
    uint32_t si = (uint32_t)(x->base - L->p->styles);
    bool has = false;
    for (size_t k = 0; k < j->styles.len && !has; k++)
        has = j->styles.ptr[k] == si;
    if (!has)
        return false;
    u32s sub = {0};
    subtree(L->p, j->el, &sub, 0);
    uint64_t h = fnv(FNV0, &L->rkey[si], sizeof L->rkey[si]);
    for (size_t i = 1; i < sub.len; i++) {
        const sk_vnode *dx = &L->v->nodes[sub.ptr[i]];
        uint32_t di = dx->base ? (uint32_t)(dx->base - L->p->styles) : 0;
        keys_of(L, di);
        h = fnv(h, &L->dkey[di], sizeof L->dkey[di]);
    }
    sk_vec_free(&sub);
    return h == j->key;
}

/* Every round at every configuration: what keeps its geometry wherever
 * its picture shows, paints at once and is worth it. */
static void check_jobs(lpass *L)
{
    lsets st = {0};
    for (int r = 0; r < L->rounds; r++) {
        round_state(L, r, &st);
        for (int c = CONFIGS - 1; c >= 0; c--) {
            lay_out(L, st.ptr, st.len, c);
            for (size_t i = 0; i < L->jobs.len; i++) {
                ljob *j = &L->jobs.ptr[i];
                if (j->fails || j->kind == SK_LAYER_NINE)
                    continue;
                bool mine = j->round == r;
                if (mine && (!job_shown(L, j) || (c == 0 && (!paints_at_once(L, j->el) || !worth(L, j->el))))) {
                    j->fails = true;
                    continue;
                }
                /* In its own round at every configuration, and at the
                 * skin's size in any other that shows it. */
                if (!mine && (c != 0 || !job_shown(L, j)))
                    continue;
                uint64_t g = geom_of(L, j->el);
                if (!j->has_geom) {
                    j->geom = g;
                    j->has_geom = true;
                } else if (j->geom != g) {
                    j->fails = true;
                }
            }
        }
    }
    sk_vec_free(&st);
}

/* Which jobs make layers: an element's when every picture of it held,
 * none under an element with a layer for every state it shows. */
static void choose(lpass *L)
{
    for (size_t i = 0; i < L->jobs.len;) {
        uint32_t e = L->jobs.ptr[i].el;
        size_t k = i;
        bool all = true, every = false;
        for (; k < L->jobs.len && L->jobs.ptr[k].el == e; k++) {
            all &= !L->jobs.ptr[k].fails;
            every |= L->jobs.ptr[k].every;
        }
        bool under = false;
        for (uint32_t a = L->p->els[e].parent; a != NONE && !under; a = L->p->els[a].parent)
            under = L->covered[a];
        for (size_t q = i; q < k; q++)
            L->jobs.ptr[q].chosen = !under && !L->jobs.ptr[q].fails;
        if (!under && all && every)
            L->covered[e] = 1;
        i = k;
    }
}

/* ---- Painting ---- */

static void put_layer(lpass *L, uint32_t el, uint32_t style, uint8_t kind, uint8_t scale, const sk_image *im, float x,
                      float y, float ox, float oy, const uint16_t slice[4])
{
    sk_wlayer w;
    memset(&w, 0, sizeof w);
    w.el = el;
    w.style = style;
    w.kind = kind;
    w.scale = scale;
    w.x = x;
    w.y = y;
    w.ox = ox;
    w.oy = oy;
    w.w = (uint16_t)im->w;
    w.h = (uint16_t)im->h;
    if (slice)
        memcpy(w.slice, slice, sizeof w.slice);
    w.qoi = sk_qoi_encode(im, &w.len);
    if (!w.qoi)
        return;
    sk_vec_push(L->out, w);
    if (L->summary && el < L->summary->n)
        L->summary->kinds[el] |= (uint8_t)(1u << kind);
}

static sk_paint_env env_of(lpass *L, const sk_layout *l)
{
    sk_paint_env env = {l, &L->v->lfonts, &L->v->images, L->v->glyphs, NULL, NULL};
    return env;
}

/* The device rectangle of r at scale, on whole pixels; false when it
 * is empty or too large for a layer. */
static bool device_rect(sk_frect r, float scale, int *x0, int *y0, int *w, int *h)
{
    if (sk_frect_empty(r))
        return false;
    double a = floor((double)r.x * scale), b = floor((double)r.y * scale);
    double c = ceil((double)(r.x + r.w) * scale), d = ceil((double)(r.y + r.h) * scale);
    if (!(c > a) || !(d > b) || c - a > 4096 || d - b > 4096 || (c - a) * (d - b) > (double)MAX_LAYER_PIXELS)
        return false;
    *x0 = (int)a;
    *y0 = (int)b;
    *w = (int)(c - a);
    *h = (int)(d - b);
    return true;
}

/* Job j painted at 1x and 2x into its records: one for every state, or
 * one for each style it stands for, sharing its picture. */
static void paint_job(lpass *L, const ljob *j)
{
    const sk_layout *l = &L->v->layout;
    uint32_t e = j->el, a = anchor_of(L->p, e);
    const sk_lbox *b = &l->boxes[e], *ab = &l->boxes[a];
    /* The node's own space to its anchor's, before its own transform:
     * an SVG node's through its svg's viewBox alone, as the compositor
     * takes it back (paint.c's layer_map). */
    sk_xform rel = SK_XFORM_IDENTITY;
    if (a != e) {
        sk_xform ia, il;
        if (!sk_xform_invert(&ab->xform, &ia) || !sk_xform_invert(&b->local, &il))
            return;
        sk_xform t = xf_mul(xf_mul(ia, b->xform), il);
        const sk_xform *vb = &ab->viewbox;
        if (fabsf(t.a - vb->a) > 1e-4f || fabsf(t.b - vb->b) > 1e-4f || fabsf(t.c - vb->c) > 1e-4f ||
            fabsf(t.d - vb->d) > 1e-4f || fabsf(t.e - vb->e) > 1e-3f || fabsf(t.f - vb->f) > 1e-3f ||
            fabsf(vb->b) > 1e-6f || fabsf(vb->c) > 1e-6f)
            return;
        rel = *vb;
    }
    /* Where its transform turns it about, from the anchor. */
    float tx = 0, ty = 0;
    const sk_pack_el *el = &L->p->els[e];
    if (j->kind == SK_LAYER_SPRITE && el->svg != NONE && L->p->svgs[el->svg].kind == SK_SVG_TURN) {
        const sk_pack_svg *g = &L->p->svgs[el->svg];
        tx = rel.a * g->v[0] + rel.c * g->v[1] + rel.e;
        ty = rel.b * g->v[0] + rel.d * g->v[1] + rel.f;
    } else if (j->kind == SK_LAYER_SPRITE && a == e) {
        const sk_css_style *s = l->nodes[e].style;
        tx = b->border.x + sk_layout_len(&s->origin[0], b->border.w);
        ty = b->border.y + sk_layout_len(&s->origin[1], b->border.h);
    }
    sk_paint_env env = env_of(L, l);
    for (uint8_t scale = 1; scale <= 2; scale++) {
        sk_xform d0 = {scale, 0, 0, scale, 0, 0};
        d0 = xf_mul(d0, rel);
        int x0, y0, w, h;
        if (!device_rect(sk_frect_map(&d0, b->ink), 1, &x0, &y0, &w, &h))
            continue;
        sk_xform own = {1, 0, 0, 1, (float)-x0, (float)-y0};
        own = xf_mul(own, d0);
        sk_image im;
        sk_image_init(&im, w, h);
        sk_paint_layer(&im, &env, e, SK_PART_SUBTREE, &own);
        float x = (float)x0 / scale - ab->border.x, y = (float)y0 / scale - ab->border.y;
        float ox = tx - ab->border.x - x, oy = ty - ab->border.y - y;
        if (j->kind != SK_LAYER_SPRITE)
            ox = oy = 0;
        if (j->every)
            put_layer(L, e, NONE, j->kind, scale, &im, x, y, ox, oy, NULL);
        else
            for (size_t k = 0; k < j->styles.len; k++)
                put_layer(L, e, j->styles.ptr[k], j->kind, scale, &im, x, y, ox, oy, NULL);
        sk_image_free(&im);
    }
}

/* ---- Nine-slices ---- */

static float max_radius(const sk_css_style *s)
{
    float r = 0;
    for (int k = 0; k < 4; k++)
        r = fmaxf(r, fmaxf(s->radius[k][0].px, s->radius[k][1].px));
    return r;
}

/* Whether e is a box whose shadows and border a nine-slice can hold:
 * one style, shadows or a border, no radius or border-image width of a
 * percentage. */
static bool nine_box(const lpass *L, uint32_t e)
{
    const sk_pack *p = L->p;
    const sk_pack_el *el = &p->els[e];
    if (is_svg_content(p, e) || el->kind == SK_EL_HOST || el->role == SK_ROLE_STAGE)
        return false;
    uint32_t entries = 1;
    for (uint8_t i = 0; i < el->ninputs; i++)
        entries *= el->inputs[i].radix;
    for (uint32_t t = 1; t < entries; t++)
        if (el->styles[t] != el->styles[0])
            return false;
    const sk_css_style *s = &p->styles[el->styles[0]];
    if (s->display == SK_DISPLAY_NONE || s->display == SK_DISPLAY_INLINE || animates(s) || s->ntransform)
        return false;
    bool any = s->bi_source.kind != SK_IMAGE_NONE;
    for (uint16_t k = 0; k < s->nbox_shadow; k++)
        any |= s->box_shadow[k].color.a > 0 && (s->box_shadow[k].blur > 0 || s->box_shadow[k].spread != 0 ||
                                               s->box_shadow[k].x != 0 || s->box_shadow[k].y != 0);
    for (int k = 0; k < 4; k++) {
        if (s->radius[k][0].pct != 0 || s->radius[k][1].pct != 0)
            return false;
        if (s->border_style[k] == SK_BORDER_SOLID && s->border_width[k] > 0 && s->border_color[k].a > 0 &&
            max_radius(s) > 0)
            any = true;
        if (s->bi_source.kind != SK_IMAGE_NONE && !(s->bi_width_num & (1 << k)) && s->bi_width[k].pct != 0)
            return false;
    }
    return any;
}

/* The nine-slice of im, slices sl[] at its edges, stretched to w x h. */
static void stretch(const sk_image *im, const int sl[4], int w, int h, sk_image *out)
{
    sk_image_init(out, w, h);
    for (int y = 0; y < h; y++) {
        int sy = y < sl[SK_TOP] ? y : y >= h - sl[SK_BOTTOM] ? im->h - (h - y) : sl[SK_TOP];
        const sk_px *row = sk_image_row(im, sy);
        sk_px *o = sk_image_row(out, y);
        for (int x = 0; x < w; x++) {
            int sx = x < sl[SK_LEFT] ? x : x >= w - sl[SK_RIGHT] ? im->w - (w - x) : sl[SK_LEFT];
            o[x] = row[sx];
        }
    }
}

static int px_diff(sk_px a, sk_px b)
{
    int d = 0;
    for (int sh = 0; sh < 32; sh += 8)
        d = SK_MAX(d, abs((int)((a >> sh) & 255) - (int)((b >> sh) & 255)));
    return d;
}

/* Box e's nine-slice at 1x and 2x, from the skin's size laid out now.
 * lo and hi are the least and most width and height the box takes
 * anywhere: along an axis it never stretches the slice is the box's
 * own, elsewhere just long enough for its middle to repeat. */
static void paint_nine(lpass *L, uint32_t e, const float lo[2], const float hi[2])
{
    const sk_layout *l = &L->v->layout;
    const sk_lbox *b = &l->boxes[e];
    const sk_css_style *s = l->nodes[e].style;
    if (!b->generated || !s || b->xform.a != 1 || b->xform.d != 1 || b->xform.b != 0 || b->xform.c != 0)
        return;
    float bw = fmaxf(fmaxf(b->bw[0], b->bw[1]), fmaxf(b->bw[2], b->bw[3]));
    float reach[2] = {0, 0};
    for (uint16_t k = 0; k < s->nbox_shadow; k++) {
        const sk_css_shadow *sh = &s->box_shadow[k];
        float spread = fabsf(sh->spread) + 1.5f * fmaxf(0, sh->blur) + 1;
        reach[0] = fmaxf(reach[0], fabsf(sh->x) + spread);
        reach[1] = fmaxf(reach[1], fabsf(sh->y) + spread);
    }
    float out = sk_layout_shadow_reach(s->box_shadow, s->nbox_shadow, true);
    if (s->bi_source.kind != SK_IMAGE_NONE)
        for (int k = 0; k < 4; k++)
            out = fmaxf(out, s->bi_outset[k]);
    for (uint8_t scale = 1; scale <= 2; scale++) {
        /* Corners as wide as the painter's own blurred patches, the
         * middle eight pixels and more, as it stretches them too. */
        float size[2];
        bool fits = true;
        for (int k = 0; k < 2; k++) {
            float ext = max_radius(s) + reach[k] + bw + 4 + 6.0f / scale;
            size[k] = lo[k] == hi[k] ? lo[k] : ceilf(2 * ext + 1);
            fits &= size[k] <= lo[k] && size[k] == floorf(size[k]);
        }
        float ref_w = size[0], ref_h = size[1];
        if (!fits || ref_w > b->border.w || ref_h > b->border.h)
            return;
        /* The box at the reference size, the rest as laid out. */
        sk_layout lr = *l;
        lr.boxes = sk_xreallocarray(NULL, l->n, sizeof *lr.boxes);
        memcpy(lr.boxes, l->boxes, l->n * sizeof *lr.boxes);
        sk_lbox *rb = &lr.boxes[e];
        float dw = b->border.w - ref_w, dh = b->border.h - ref_h;
        rb->border.w -= dw;
        rb->border.h -= dh;
        rb->padding.w -= dw;
        rb->padding.h -= dh;
        rb->content.w -= dw;
        rb->content.h -= dh;
        sk_frect ink_ref = {rb->border.x - out, rb->border.y - out, rb->border.w + 2 * out, rb->border.h + 2 * out};
        sk_frect ink_real = {b->border.x - out, b->border.y - out, b->border.w + 2 * out, b->border.h + 2 * out};
        int x0, y0, w, h, rx0, ry0, rw, rh;
        bool ok = device_rect(ink_ref, scale, &x0, &y0, &w, &h) &&
                  device_rect(ink_real, scale, &rx0, &ry0, &rw, &rh) && rx0 == x0 && ry0 == y0;
        sk_image ref = {0}, real = {0}, made = {0};
        if (ok) {
            sk_paint_env env = env_of(L, &lr);
            sk_xform own = {scale, 0, 0, scale, (float)-x0, (float)-y0};
            sk_image_init(&ref, w, h);
            sk_paint_layer(&ref, &env, e, SK_PART_NINE, &own);
            env.layout = l;
            sk_image_init(&real, rw, rh);
            sk_paint_layer(&real, &env, e, SK_PART_NINE, &own);
            /* Sliced at the middle of the box. */
            float bx = b->border.x * scale, by = b->border.y * scale;
            int mx = (int)lroundf(bx) + (int)(ref_w * scale / 2), my = (int)lroundf(by) + (int)(ref_h * scale / 2);
            int sl[4];
            sl[SK_LEFT] = mx - x0;
            sl[SK_RIGHT] = x0 + w - (mx + 1);
            sl[SK_TOP] = my - y0;
            sl[SK_BOTTOM] = y0 + h - (my + 1);
            ok = sl[SK_LEFT] > 0 && sl[SK_RIGHT] > 0 && sl[SK_TOP] > 0 && sl[SK_BOTTOM] > 0 && rw >= w && rh >= h;
            if (ok) {
                stretch(&ref, sl, rw, rh, &made);
                for (int y = 0; y < rh && ok; y++)
                    for (int x = 0; x < rw && ok; x++)
                        ok = px_diff(sk_image_row(&made, y)[x], sk_image_row(&real, y)[x]) <= NINE_TOLERANCE;
            }
            if (ok) {
                uint16_t slice[4] = {(uint16_t)sl[0], (uint16_t)sl[1], (uint16_t)sl[2], (uint16_t)sl[3]};
                put_layer(L, e, NONE, SK_LAYER_NINE, scale, &ref, (float)x0 / scale - b->border.x,
                          (float)y0 / scale - b->border.y, ref_w, ref_h, slice);
            }
        }
        sk_image_free(&ref);
        sk_image_free(&real);
        sk_image_free(&made);
        free(lr.boxes);
        if (!ok)
            return;
    }
}

/* The boxes no layer covers that a nine-slice can hold, laid out in the
 * state of all variables 0 at every configuration for their least
 * size, then painted at the skin's. */
static void nines(lpass *L)
{
    u32s cand = {0};
    for (uint32_t e = 0; e < L->n; e++) {
        bool under = false;
        for (uint32_t a = e; a != NONE && !under; a = L->p->els[a].parent)
            under = L->covered[a];
        bool own = false;
        for (size_t i = 0; i < L->jobs.len && !own; i++)
            own = L->jobs.ptr[i].el == e && L->jobs.ptr[i].chosen;
        if (!under && !own && nine_box(L, e))
            sk_vec_push(&cand, e);
    }
    if (!cand.len) {
        sk_vec_free(&cand);
        return;
    }
    float(*lo)[2] = sk_xcalloc(cand.len, sizeof *lo), (*hi)[2] = sk_xcalloc(cand.len, sizeof *hi);
    for (size_t i = 0; i < cand.len; i++)
        lo[i][0] = lo[i][1] = INFINITY;
    for (int c = CONFIGS - 1; c >= 0; c--) {
        lay_out(L, NULL, 0, c);
        for (size_t i = 0; i < cand.len; i++) {
            const sk_lbox *b = &L->v->layout.boxes[cand.ptr[i]];
            float size[2] = {b->generated ? b->border.w : 0, b->generated ? b->border.h : 0};
            for (int k = 0; k < 2; k++) {
                lo[i][k] = fminf(lo[i][k], size[k]);
                hi[i][k] = fmaxf(hi[i][k], size[k]);
            }
        }
    }
    for (size_t i = 0; i < cand.len; i++)
        if (lo[i][0] > 0 && lo[i][1] > 0)
            paint_nine(L, cand.ptr[i], lo[i], hi[i]);
    free(lo);
    free(hi);
    sk_vec_free(&cand);
}

/* ---- The pass ---- */

static int by_record(const void *x, const void *y)
{
    const sk_wlayer *a = x, *b = y;
    if (a->el != b->el)
        return a->el < b->el ? -1 : 1;
    /* Style none first: SK_PACK_NONE + 1 wraps to 0. */
    uint32_t sa = a->style + 1, sb = b->style + 1;
    if (sa != sb)
        return sa < sb ? -1 : 1;
    if (a->kind != b->kind)
        return a->kind < b->kind ? -1 : 1;
    return a->scale < b->scale ? -1 : a->scale > b->scale;
}

bool sk_layers_make(const uint8_t *pack, size_t n, sk_wlayers *out, sk_layer_summary *summary, char *err,
                    size_t err_len)
{
    memset(out, 0, sizeof *out);
    if (summary)
        memset(summary, 0, sizeof *summary);
    lpass L;
    memset(&L, 0, sizeof L);
    L.sp = sk_skinpack_new((uint8_t *)pack, n, false, err, err_len);
    if (!L.sp)
        return false;
    L.p = &L.sp->pack;
    L.v = sk_skinview_new(L.sp, 0);
    L.n = L.p->nels;
    L.out = out;
    L.summary = summary;
    if (summary) {
        summary->n = L.n;
        summary->kinds = sk_xcalloc(L.n ? L.n : 1, 1);
    }
    L.dyn = sk_xcalloc(L.n ? L.n : 1, 1);
    L.covered = sk_xcalloc(L.n ? L.n : 1, sizeof *L.covered);
    L.gotkey = sk_xcalloc(L.p->nstyles ? L.p->nstyles : 1, 1);
    L.rkey = sk_xcalloc(L.p->nstyles ? L.p->nstyles : 1, sizeof *L.rkey);
    L.dkey = sk_xcalloc(L.p->nstyles ? L.p->nstyles : 1, sizeof *L.dkey);
    for (uint32_t e = 0; e < L.n; e++)
        L.dyn[e] = sk_view_shows_value(L.v, e);
    /* Candidates in tree order, the stage and what is above it never. */
    u32s order = {0};
    if (L.v->stage != NONE)
        for (uint32_t c = L.p->els[L.v->stage].first_child; c != NONE; c = L.p->els[c].next_sibling)
            subtree(L.p, c, &order, 0);
    for (size_t i = 0; i < order.len; i++)
        consider(&L, order.ptr[i]);
    plan_rounds(&L);
    check_jobs(&L);
    choose(&L);
    lsets st = {0};
    for (int r = 0; r < L.rounds; r++) {
        bool any = false;
        for (size_t i = 0; i < L.jobs.len && !any; i++)
            any = L.jobs.ptr[i].round == r && L.jobs.ptr[i].chosen;
        if (!any)
            continue;
        round_state(&L, r, &st);
        lay_out(&L, st.ptr, st.len, 0);
        for (size_t i = 0; i < L.jobs.len; i++)
            if (L.jobs.ptr[i].round == r && L.jobs.ptr[i].chosen && job_shown(&L, &L.jobs.ptr[i]))
                paint_job(&L, &L.jobs.ptr[i]);
    }
    sk_vec_free(&st);
    nines(&L);
    if (out->len > 1)
        qsort(out->ptr, out->len, sizeof *out->ptr, by_record);
    for (size_t i = 0; i < L.jobs.len; i++)
        job_free(&L.jobs.ptr[i]);
    sk_vec_free(&L.jobs);
    sk_vec_free(&order);
    free(L.dyn);
    free(L.covered);
    free(L.gotkey);
    free(L.rkey);
    free(L.dkey);
    sk_skinview_free(L.v);
    sk_skinpack_unref(L.sp);
    return true;
}

void sk_wlayers_free(sk_wlayers *l)
{
    for (size_t i = 0; i < l->len; i++)
        free(l->ptr[i].qoi);
    sk_vec_free(l);
}

void sk_layer_summary_free(sk_layer_summary *s)
{
    free(s->kinds);
    memset(s, 0, sizeof *s);
}
