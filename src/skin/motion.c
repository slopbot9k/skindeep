/* What moves in a style (decision 0037): CSS transitions and
 * animations, run from the view's clock.
 *
 * A transition starts when an element's style changes, for each
 * property the new style's transition names whose value changes and
 * can be eased: colours (in premultiplied sRGB), numbers, lengths of
 * one kind, shadow lists (the shorter padded with transparent
 * shadows), transform lists of matching functions (none matching any
 * list as its identity functions) and filter lists alike. It eases from
 * the value shown when the style changed, so a transition cut short
 * turns back from where it was. The rest switch at once.
 *
 * An animation starts when its name appears in the element's style,
 * keeps its clock while its play state is paused, and stops when the
 * name goes. Its keyframes animate transform and opacity: the frames
 * that set a property make its track, a 0% or 100% that none sets
 * being the element's own value, each stretch between two frames eased
 * by the first's timing function. Animations apply first, transitions
 * over them, as CSS's cascade orders the two. */
#include "viewint.h"

#include <math.h>
#include <string.h>

/* What a property's value is, to ease it. */
enum { PK_NONE, PK_COLOR, PK_PAINT, PK_FLOAT, PK_LEN, PK_SHADOWS, PK_XF, PK_FILTER };

/* Lists longer than this are switched, not eased. */
#define MAX_LIST 32

typedef struct {
    uint8_t kind;
    bool paint; /* PK_PAINT: a colour, not none */
    sk_css_color c;
    float f;
    sk_css_len l;
    sk_css_shadow sh[MAX_LIST];
    sk_css_xf xf[MAX_LIST];
    sk_css_filter fx[MAX_LIST];
    uint16_t n;
} pval;

typedef struct {
    uint16_t prop;
    double start, delay, duration; /* the clock, ms */
    sk_css_timing timing;
    pval *from, *to;
} trans;

typedef struct {
    uint32_t keyframes;
    uint16_t slot;   /* its place in the style's animation lists */
    double clock;    /* seconds it has run */
} anim;

struct sk_motion {
    sk_css_style shown;
    const sk_css_style *base;
    double at; /* the view's clock when its animations last ran */
    trans *tr;
    size_t ntr;
    anim *an;
    size_t nan;
    /* What shown's eased lists point to. */
    sk_css_xf xf[MAX_LIST];
    sk_css_shadow tsh[MAX_LIST], bsh[MAX_LIST];
    sk_css_filter fx[MAX_LIST];
    uint64_t sig, sig_but_opacity;
    bool opacity_only; /* its animations animate opacity alone */
};

/* ---- Easing ---- */

static double bezier(double a1, double a2, double t)
{
    /* B(t) of a curve from 0 to 1 through a1, a2. */
    double u = 1 - t;
    return 3 * u * u * t * a1 + 3 * u * t * t * a2 + t * t * t;
}

static double bezier_slope(double a1, double a2, double t)
{
    double u = 1 - t;
    return 3 * u * u * a1 + 6 * u * t * (a2 - a1) + 3 * t * t * (1 - a2);
}

double sk_timing_eval(const sk_css_timing *t, double x)
{
    if (!(x > 0))
        x = x < 0 ? x : 0;
    switch (t->kind) {
    case SK_TIMING_CUBIC: {
        if (x <= 0 || x >= 1)
            return x <= 0 ? 0 : 1;
        /* The curve's parameter for x: Newton, then halving. */
        double s = x;
        for (int i = 0; i < 8; i++) {
            double e = bezier(t->x1, t->x2, s) - x, d = bezier_slope(t->x1, t->x2, s);
            if (fabs(e) < 1e-7)
                return bezier(t->y1, t->y2, s);
            if (fabs(d) < 1e-6)
                break;
            s -= e / d;
        }
        double lo = 0, hi = 1;
        s = x;
        for (int i = 0; i < 60; i++) {
            double e = bezier(t->x1, t->x2, s);
            if (fabs(e - x) < 1e-9)
                break;
            if (e < x)
                lo = s;
            else
                hi = s;
            s = (lo + hi) / 2;
        }
        return bezier(t->y1, t->y2, s);
    }
    case SK_TIMING_STEPS: {
        int n = t->steps ? t->steps : 1;
        int jumps = t->steps_pos == SK_STEP_JUMP_BOTH   ? n + 1
                    : t->steps_pos == SK_STEP_JUMP_NONE ? (n > 1 ? n - 1 : 1)
                                                        : n;
        double cur = floor(x * n);
        if (t->steps_pos == SK_STEP_JUMP_START || t->steps_pos == SK_STEP_JUMP_BOTH)
            cur += 1;
        if (x >= 0 && cur < 0)
            cur = 0;
        if (x <= 1 && cur > jumps)
            cur = jumps;
        return cur / jumps;
    }
    default:
        return x;
    }
}

/* ---- Values ---- */

static uint8_t kind_of(uint16_t prop)
{
    switch (prop) {
    case SK_PROP_COLOR:
    case SK_PROP_BACKGROUND_COLOR:
    case SK_PROP_BORDER_TOP_COLOR:
    case SK_PROP_BORDER_RIGHT_COLOR:
    case SK_PROP_BORDER_BOTTOM_COLOR:
    case SK_PROP_BORDER_LEFT_COLOR:
    case SK_PROP_OUTLINE_COLOR:
    case SK_PROP_TEXT_DECORATION_COLOR:
        return PK_COLOR;
    case SK_PROP_FILL:
    case SK_PROP_STROKE:
        return PK_PAINT;
    case SK_PROP_OPACITY:
    case SK_PROP_FONT_SIZE:
    case SK_PROP_LETTER_SPACING:
    case SK_PROP_BORDER_TOP_WIDTH:
    case SK_PROP_BORDER_RIGHT_WIDTH:
    case SK_PROP_BORDER_BOTTOM_WIDTH:
    case SK_PROP_BORDER_LEFT_WIDTH:
    case SK_PROP_OUTLINE_WIDTH:
    case SK_PROP_OUTLINE_OFFSET:
    case SK_PROP_FLEX_GROW:
    case SK_PROP_FLEX_SHRINK:
        return PK_FLOAT;
    case SK_PROP_TOP:
    case SK_PROP_RIGHT:
    case SK_PROP_BOTTOM:
    case SK_PROP_LEFT:
    case SK_PROP_WIDTH:
    case SK_PROP_HEIGHT:
    case SK_PROP_MIN_WIDTH:
    case SK_PROP_MIN_HEIGHT:
    case SK_PROP_MAX_WIDTH:
    case SK_PROP_MAX_HEIGHT:
    case SK_PROP_MARGIN_TOP:
    case SK_PROP_MARGIN_RIGHT:
    case SK_PROP_MARGIN_BOTTOM:
    case SK_PROP_MARGIN_LEFT:
    case SK_PROP_PADDING_TOP:
    case SK_PROP_PADDING_RIGHT:
    case SK_PROP_PADDING_BOTTOM:
    case SK_PROP_PADDING_LEFT:
    case SK_PROP_STROKE_WIDTH:
    case SK_PROP_ROW_GAP:
    case SK_PROP_COLUMN_GAP:
    case SK_PROP_FLEX_BASIS:
        return PK_LEN;
    case SK_PROP_TEXT_SHADOW:
    case SK_PROP_BOX_SHADOW:
        return PK_SHADOWS;
    case SK_PROP_TRANSFORM:
        return PK_XF;
    case SK_PROP_FILTER:
        return PK_FILTER;
    default:
        return PK_NONE;
    }
}

static sk_css_color *color_field(sk_css_style *s, uint16_t prop)
{
    switch (prop) {
    case SK_PROP_COLOR:
        return &s->color;
    case SK_PROP_BACKGROUND_COLOR:
        return &s->bg_color;
    case SK_PROP_OUTLINE_COLOR:
        return &s->outline_color;
    case SK_PROP_TEXT_DECORATION_COLOR:
        return &s->text_decoration_color;
    case SK_PROP_FILL:
        return &s->fill;
    case SK_PROP_STROKE:
        return &s->stroke;
    default:
        return &s->border_color[prop - SK_PROP_BORDER_TOP_COLOR];
    }
}

static float *float_field(sk_css_style *s, uint16_t prop)
{
    switch (prop) {
    case SK_PROP_OPACITY:
        return &s->opacity;
    case SK_PROP_FONT_SIZE:
        return &s->font_size;
    case SK_PROP_LETTER_SPACING:
        return &s->letter_spacing;
    case SK_PROP_OUTLINE_WIDTH:
        return &s->outline_width;
    case SK_PROP_OUTLINE_OFFSET:
        return &s->outline_offset;
    case SK_PROP_FLEX_GROW:
        return &s->flex_grow;
    case SK_PROP_FLEX_SHRINK:
        return &s->flex_shrink;
    default:
        return &s->border_width[prop - SK_PROP_BORDER_TOP_WIDTH];
    }
}

static sk_css_len *len_field(sk_css_style *s, uint16_t prop)
{
    if (prop >= SK_PROP_TOP && prop <= SK_PROP_LEFT)
        return &s->inset[prop - SK_PROP_TOP];
    if (prop >= SK_PROP_MARGIN_TOP && prop <= SK_PROP_MARGIN_LEFT)
        return &s->margin[prop - SK_PROP_MARGIN_TOP];
    if (prop >= SK_PROP_PADDING_TOP && prop <= SK_PROP_PADDING_LEFT)
        return &s->padding[prop - SK_PROP_PADDING_TOP];
    switch (prop) {
    case SK_PROP_WIDTH:
        return &s->width;
    case SK_PROP_HEIGHT:
        return &s->height;
    case SK_PROP_MIN_WIDTH:
        return &s->min_width;
    case SK_PROP_MIN_HEIGHT:
        return &s->min_height;
    case SK_PROP_MAX_WIDTH:
        return &s->max_width;
    case SK_PROP_MAX_HEIGHT:
        return &s->max_height;
    case SK_PROP_STROKE_WIDTH:
        return &s->stroke_width;
    case SK_PROP_ROW_GAP:
        return &s->row_gap;
    case SK_PROP_COLUMN_GAP:
        return &s->column_gap;
    default:
        return &s->flex_basis;
    }
}

/* A property's value in a style; false for one that is not eased or a
 * list too long to. */
static bool get(const sk_css_style *cs, uint16_t prop, pval *out)
{
    sk_css_style *s = (sk_css_style *)cs;
    out->kind = kind_of(prop);
    out->n = 0;
    switch (out->kind) {
    case PK_COLOR:
        out->c = *color_field(s, prop);
        return true;
    case PK_PAINT:
        out->paint = (prop == SK_PROP_FILL ? s->fill_kind : s->stroke_kind) == SK_PAINT_COLOR;
        out->c = *color_field(s, prop);
        return true;
    case PK_FLOAT:
        out->f = *float_field(s, prop);
        return true;
    case PK_LEN:
        out->l = *len_field(s, prop);
        return true;
    case PK_SHADOWS: {
        const sk_css_shadow *sh = prop == SK_PROP_TEXT_SHADOW ? s->text_shadow : s->box_shadow;
        uint16_t n = prop == SK_PROP_TEXT_SHADOW ? s->ntext_shadow : s->nbox_shadow;
        if (n > MAX_LIST)
            return false;
        out->n = n;
        if (n)
            memcpy(out->sh, sh, n * sizeof *sh);
        return true;
    }
    case PK_XF:
        if (s->ntransform > MAX_LIST)
            return false;
        out->n = s->ntransform;
        if (out->n)
            memcpy(out->xf, s->transform, out->n * sizeof *out->xf);
        return true;
    case PK_FILTER:
        if (s->nfilter > MAX_LIST)
            return false;
        out->n = s->nfilter;
        if (out->n)
            memcpy(out->fx, s->filter, out->n * sizeof *out->fx);
        return true;
    default:
        return false;
    }
}

/* Writes a value into the motion's shown style, its lists into the
 * motion's own arrays. */
static void put(sk_motion *m, uint16_t prop, const pval *x)
{
    sk_css_style *s = &m->shown;
    switch (x->kind) {
    case PK_COLOR:
        *color_field(s, prop) = x->c;
        break;
    case PK_PAINT:
        *color_field(s, prop) = x->c;
        if (prop == SK_PROP_FILL)
            s->fill_kind = x->paint ? SK_PAINT_COLOR : SK_PAINT_NONE;
        else
            s->stroke_kind = x->paint ? SK_PAINT_COLOR : SK_PAINT_NONE;
        break;
    case PK_FLOAT:
        *float_field(s, prop) = x->f;
        break;
    case PK_LEN:
        *len_field(s, prop) = x->l;
        break;
    case PK_SHADOWS:
        if (prop == SK_PROP_TEXT_SHADOW) {
            memcpy(m->tsh, x->sh, x->n * sizeof *x->sh);
            s->text_shadow = m->tsh;
            s->ntext_shadow = x->n;
        } else {
            memcpy(m->bsh, x->sh, x->n * sizeof *x->sh);
            s->box_shadow = m->bsh;
            s->nbox_shadow = x->n;
        }
        break;
    case PK_XF:
        memcpy(m->xf, x->xf, x->n * sizeof *x->xf);
        s->transform = m->xf;
        s->ntransform = x->n;
        break;
    case PK_FILTER:
        memcpy(m->fx, x->fx, x->n * sizeof *x->fx);
        s->filter = m->fx;
        s->nfilter = x->n;
        break;
    default:
        break;
    }
}

static bool len_eq(const sk_css_len *a, const sk_css_len *b)
{
    return a->kind == b->kind && a->px == b->px && a->pct == b->pct;
}

static bool color_eq(const sk_css_color *a, const sk_css_color *b)
{
    return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

static bool shadow_eq(const sk_css_shadow *a, const sk_css_shadow *b)
{
    return a->x == b->x && a->y == b->y && a->blur == b->blur && a->spread == b->spread && a->inset == b->inset &&
           color_eq(&a->color, &b->color);
}

static bool pval_eq(const pval *a, const pval *b)
{
    if (a->kind != b->kind || a->n != b->n)
        return false;
    switch (a->kind) {
    case PK_COLOR:
        return color_eq(&a->c, &b->c);
    case PK_PAINT:
        return a->paint == b->paint && (!a->paint || color_eq(&a->c, &b->c));
    case PK_FLOAT:
        return a->f == b->f;
    case PK_LEN:
        return len_eq(&a->l, &b->l);
    case PK_SHADOWS:
        for (uint16_t i = 0; i < a->n; i++)
            if (!shadow_eq(&a->sh[i], &b->sh[i]))
                return false;
        return true;
    case PK_XF:
        for (uint16_t i = 0; i < a->n; i++)
            if (a->xf[i].kind != b->xf[i].kind || !len_eq(&a->xf[i].tx, &b->xf[i].tx) ||
                !len_eq(&a->xf[i].ty, &b->xf[i].ty) || memcmp(a->xf[i].v, b->xf[i].v, sizeof a->xf[i].v))
                return false;
        return true;
    case PK_FILTER:
        for (uint16_t i = 0; i < a->n; i++)
            if (a->fx[i].kind != b->fx[i].kind || a->fx[i].amount != b->fx[i].amount ||
                !shadow_eq(&a->fx[i].shadow, &b->fx[i].shadow))
                return false;
        return true;
    default:
        return true;
    }
}

static float lerpf(float a, float b, double t)
{
    return (float)(a + (b - a) * t);
}

/* Colours ease premultiplied, as CSS's do. */
static sk_css_color lerp_color(sk_css_color a, sk_css_color b, double t)
{
    float al = lerpf(a.a, b.a, t);
    sk_css_color c = {0, 0, 0, al};
    if (al > 0) {
        c.r = lerpf(a.r * a.a, b.r * b.a, t) / al;
        c.g = lerpf(a.g * a.a, b.g * b.a, t) / al;
        c.b = lerpf(a.b * a.a, b.b * b.a, t) / al;
    }
    return c;
}

static sk_css_len lerp_len(sk_css_len a, sk_css_len b, double t)
{
    sk_css_len l = a;
    l.px = lerpf(a.px, b.px, t);
    l.pct = lerpf(a.pct, b.pct, t);
    return l;
}

/* A transform function that changes nothing, of f's kind. */
static sk_css_xf xf_identity(const sk_css_xf *f)
{
    sk_css_xf x;
    memset(&x, 0, sizeof x);
    x.kind = f->kind;
    x.tx = x.ty = (sk_css_len){0, 0, SK_LEN_PX};
    if (f->kind == SK_XF_SCALE)
        x.v[0] = x.v[1] = 1;
    if (f->kind == SK_XF_MATRIX)
        x.v[0] = x.v[3] = 1;
    return x;
}

static sk_css_filter fx_identity(const sk_css_filter *f)
{
    sk_css_filter x;
    memset(&x, 0, sizeof x);
    x.kind = f->kind;
    bool one = f->kind == SK_FX_BRIGHTNESS || f->kind == SK_FX_CONTRAST || f->kind == SK_FX_SATURATE ||
               f->kind == SK_FX_OPACITY;
    x.amount = one ? 1 : 0;
    x.shadow.color = (sk_css_color){0, 0, 0, 0};
    return x;
}

static sk_css_shadow shadow_none(bool inset)
{
    sk_css_shadow s;
    memset(&s, 0, sizeof s);
    s.inset = inset;
    return s;
}

static sk_css_shadow lerp_shadow(const sk_css_shadow *a, const sk_css_shadow *b, double t)
{
    sk_css_shadow s;
    s.x = lerpf(a->x, b->x, t);
    s.y = lerpf(a->y, b->y, t);
    s.blur = fmaxf(0, lerpf(a->blur, b->blur, t));
    s.spread = lerpf(a->spread, b->spread, t);
    s.color = lerp_color(a->color, b->color, t);
    s.inset = b->inset;
    return s;
}

/* Whether a and b ease into each other. */
static bool easable(const pval *a, const pval *b)
{
    if (a->kind != b->kind || a->kind == PK_NONE)
        return false;
    switch (a->kind) {
    case PK_PAINT:
        return a->paint && b->paint;
    case PK_LEN:
        return a->l.kind == SK_LEN_PX && b->l.kind == SK_LEN_PX;
    case PK_SHADOWS:
        for (uint16_t i = 0; i < a->n && i < b->n; i++)
            if (a->sh[i].inset != b->sh[i].inset)
                return false;
        return true;
    case PK_XF:
        if (a->n && b->n && a->n != b->n)
            return false;
        for (uint16_t i = 0; a->n && b->n && i < a->n; i++)
            if (a->xf[i].kind != b->xf[i].kind || a->xf[i].tx.kind != SK_LEN_PX || b->xf[i].tx.kind != SK_LEN_PX ||
                a->xf[i].ty.kind != SK_LEN_PX || b->xf[i].ty.kind != SK_LEN_PX)
                return false;
        return true;
    case PK_FILTER:
        if (a->n && b->n && a->n != b->n)
            return false;
        for (uint16_t i = 0; a->n && b->n && i < a->n; i++)
            if (a->fx[i].kind != b->fx[i].kind)
                return false;
        return true;
    default:
        return true;
    }
}

/* a eased to b at t into out; a value that cannot ease switches half
 * way, as a keyframe's discrete value does. */
static void lerp(const pval *a, const pval *b, double t, pval *out)
{
    if (!easable(a, b)) {
        *out = t < 0.5 ? *a : *b;
        return;
    }
    out->kind = a->kind;
    out->n = 0;
    switch (a->kind) {
    case PK_COLOR:
    case PK_PAINT:
        out->paint = true;
        out->c = lerp_color(a->c, b->c, t);
        break;
    case PK_FLOAT:
        out->f = lerpf(a->f, b->f, t);
        break;
    case PK_LEN:
        out->l = lerp_len(a->l, b->l, t);
        break;
    case PK_SHADOWS:
        out->n = SK_MAX(a->n, b->n);
        for (uint16_t i = 0; i < out->n; i++) {
            sk_css_shadow x = i < a->n ? a->sh[i] : shadow_none(b->sh[i].inset);
            sk_css_shadow y = i < b->n ? b->sh[i] : shadow_none(a->sh[i].inset);
            out->sh[i] = lerp_shadow(&x, &y, t);
        }
        break;
    case PK_XF:
        out->n = SK_MAX(a->n, b->n);
        for (uint16_t i = 0; i < out->n; i++) {
            sk_css_xf x = i < a->n ? a->xf[i] : xf_identity(&b->xf[i]);
            sk_css_xf y = i < b->n ? b->xf[i] : xf_identity(&a->xf[i]);
            sk_css_xf r = x;
            r.tx = lerp_len(x.tx, y.tx, t);
            r.ty = lerp_len(x.ty, y.ty, t);
            for (int k = 0; k < 6; k++)
                r.v[k] = lerpf(x.v[k], y.v[k], t);
            out->xf[i] = r;
        }
        break;
    case PK_FILTER:
        out->n = SK_MAX(a->n, b->n);
        for (uint16_t i = 0; i < out->n; i++) {
            sk_css_filter x = i < a->n ? a->fx[i] : fx_identity(&b->fx[i]);
            sk_css_filter y = i < b->n ? b->fx[i] : fx_identity(&a->fx[i]);
            sk_css_filter r = x;
            r.amount = lerpf(x.amount, y.amount, t);
            r.shadow = lerp_shadow(&x.shadow, &y.shadow, t);
            out->fx[i] = r;
        }
        break;
    default:
        break;
    }
}

/* ---- The motion of a node ---- */

static sk_motion *motion_of(sk_skinview *v, uint32_t n)
{
    sk_vnode *x = &v->nodes[n];
    if (!x->mo) {
        x->mo = sk_xcalloc(1, sizeof *x->mo);
        x->mo->at = v->now;
    }
    return x->mo;
}

static void trans_drop(sk_motion *m, size_t i)
{
    free(m->tr[i].from);
    free(m->tr[i].to);
    m->tr[i] = m->tr[--m->ntr];
}

void sk_motion_free(sk_motion *m)
{
    if (!m)
        return;
    while (m->ntr)
        trans_drop(m, m->ntr - 1);
    free(m->tr);
    free(m->an);
    free(m);
}

/* The entry of base's transition list that names prop, the last that
 * does; -1 for none. */
static int transition_for(const sk_css_style *s, uint16_t prop)
{
    int found = -1;
    for (uint16_t i = 0; i < s->ntr_property; i++)
        if (s->tr_property[i] == prop || s->tr_property[i] == SK_PROP_ALL)
            found = i;
    return found;
}

static bool any_transition(const sk_css_style *s)
{
    for (uint16_t i = 0; i < s->ntr_property; i++) {
        sk_css_transition t = sk_style_transition(s, i);
        if (t.duration > 0)
            return true;
    }
    return false;
}

static bool any_animation(const sk_css_style *s)
{
    for (uint16_t i = 0; i < s->nanim_name; i++)
        if (s->anim_name[i] != SK_PACK_NONE)
            return true;
    return false;
}

void sk_motion_restyle(sk_skinview *v, uint32_t n, const sk_css_style *old, const sk_css_style *base)
{
    sk_vnode *x = &v->nodes[n];
    if (!x->mo && !(old && any_transition(base)) && !any_animation(base))
        return;
    sk_motion *m = motion_of(v, n);
    /* Transitions: each eased property's, from what showed. */
    for (uint16_t prop = 1; old && prop < SK_PROP_COUNT; prop++) {
        if (kind_of(prop) == PK_NONE)
            continue;
        int i = transition_for(base, prop);
        sk_css_transition t = i >= 0 ? sk_style_transition(base, (uint16_t)i) : (sk_css_transition){0};
        size_t running = m->ntr;
        for (size_t k = 0; k < m->ntr; k++)
            if (m->tr[k].prop == prop)
                running = k;
        if ((i < 0 || !(t.duration > 0)) && running == m->ntr)
            continue;
        pval *from = sk_xmalloc(sizeof *from), *to = sk_xmalloc(sizeof *to);
        bool ok = i >= 0 && t.duration > 0 && get(old, prop, from) && get(base, prop, to) && easable(from, to) &&
                  !pval_eq(from, to);
        if (running < m->ntr && (!ok || !pval_eq(m->tr[running].to, to)))
            trans_drop(m, running);
        else if (running < m->ntr)
            ok = false; /* already easing to this value */
        if (!ok) {
            free(from);
            free(to);
            continue;
        }
        m->tr = sk_xreallocarray(m->tr, m->ntr + 1, sizeof *m->tr);
        m->tr[m->ntr++] = (trans){prop, v->now, t.delay * 1000.0, t.duration * 1000.0, t.timing, from, to};
    }
    /* Animations: by name, the clock of one that stays kept. */
    anim *was = m->an;
    size_t nwas = m->nan;
    m->an = NULL;
    m->nan = 0;
    for (uint16_t i = 0; i < base->nanim_name; i++) {
        uint32_t k = base->anim_name[i];
        if (k == SK_PACK_NONE || k >= v->p->nkeyframes)
            continue;
        anim a = {k, i, 0};
        for (size_t j = 0; j < nwas; j++)
            if (was[j].keyframes == k) {
                a.clock = was[j].clock;
                was[j].keyframes = SK_PACK_NONE;
                break;
            }
        m->an = sk_xreallocarray(m->an, m->nan + 1, sizeof *m->an);
        m->an[m->nan++] = a;
    }
    free(was);
    m->base = base;
}

/* An animation's iteration progress at its clock, directed; false
 * while it has no effect. */
static bool progress(const sk_css_anim *a, double clock, double *out, bool *done)
{
    double t = clock - a->delay, d = a->duration, n = a->iterations;
    *done = false;
    if (!(n >= 0))
        n = 1;
    double iter, p;
    double active = d > 0 ? d * n : 0;
    if (t < 0) {
        if (a->fill != SK_FILL_MODE_BACKWARDS && a->fill != SK_FILL_MODE_BOTH)
            return false;
        iter = 0;
        p = 0;
    } else if (isfinite(active) && t >= active) {
        *done = true;
        if (a->fill != SK_FILL_MODE_FORWARDS && a->fill != SK_FILL_MODE_BOTH)
            return false;
        double whole = floor(n);
        if (n == whole && n > 0) {
            iter = n - 1;
            p = 1;
        } else {
            iter = whole;
            p = n - whole;
        }
    } else {
        iter = floor(t / d);
        p = (t - iter * d) / d;
    }
    bool odd = fmod(iter, 2) != 0;
    switch (a->direction) {
    case SK_DIR_REVERSE:
        p = 1 - p;
        break;
    case SK_DIR_ALTERNATE:
        if (odd)
            p = 1 - p;
        break;
    case SK_DIR_ALTERNATE_REVERSE:
        if (!odd)
            p = 1 - p;
        break;
    default:
        break;
    }
    *out = p;
    return true;
}

/* A keyframe track's value of prop (transform or opacity) at progress
 * p, own the element's value where no frame sets it. */
static void track(const sk_pack_keyframes *kf, const sk_css_anim *a, uint16_t prop, const sk_css_style *own, double p,
                  pval *out)
{
    typedef struct {
        float offset;
        const sk_pack_key *key; /* NULL: the element's own value */
    } stop;
    stop st[SK_PACK_MAX_LIST + 2];
    size_t n = 0;
    bool xf = prop == SK_PROP_TRANSFORM;
    for (uint16_t i = 0; i < kf->nkeys && n < SK_PACK_MAX_LIST; i++) {
        const sk_pack_key *k = &kf->keys[i];
        if (!(xf ? k->has_transform : k->has_opacity))
            continue;
        /* In order of offset, a later frame at one offset replacing an
         * earlier. */
        size_t at = n;
        while (at > 0 && st[at - 1].offset > k->offset)
            at--;
        if (at > 0 && st[at - 1].offset == k->offset) {
            st[at - 1].key = k;
            continue;
        }
        memmove(&st[at + 1], &st[at], (n - at) * sizeof *st);
        st[at] = (stop){k->offset, k};
        n++;
    }
    if (!n || st[0].offset > 0) {
        memmove(&st[1], &st[0], n * sizeof *st);
        st[0] = (stop){0, NULL};
        n++;
    }
    if (st[n - 1].offset < 1)
        st[n++] = (stop){1, NULL};
    size_t i = 0;
    while (i + 2 < n && st[i + 1].offset <= p)
        i++;
    pval *x = sk_xmalloc(sizeof *x), *y = sk_xmalloc(sizeof *y);
    for (int side = 0; side < 2; side++) {
        const sk_pack_key *k = st[i + (size_t)side].key;
        pval *val = side ? y : x;
        if (!k) {
            get(own, prop, val);
        } else if (xf) {
            val->kind = PK_XF;
            val->n = SK_MIN(k->ntransform, MAX_LIST);
            if (val->n)
                memcpy(val->xf, k->transform, val->n * sizeof *val->xf);
        } else {
            val->kind = PK_FLOAT;
            val->n = 0;
            val->f = k->opacity;
        }
    }
    float span = st[i + 1].offset - st[i].offset;
    double local = span > 0 ? (p - st[i].offset) / span : 1;
    local = fmax(0, fmin(1, local));
    const sk_css_timing *tm = st[i].key && st[i].key->has_timing ? &st[i].key->timing : &a->timing;
    lerp(x, y, sk_timing_eval(tm, local), out);
    free(x);
    free(y);
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static uint64_t fnv_len(uint64_t h, const sk_css_len *l)
{
    h = fnv(h, &l->px, sizeof l->px);
    h = fnv(h, &l->pct, sizeof l->pct);
    return fnv(h, &l->kind, 1);
}

static uint64_t fnv_shadow(uint64_t h, const sk_css_shadow *s)
{
    float f[8] = {s->x, s->y, s->blur, s->spread, s->color.r, s->color.g, s->color.b, s->color.a};
    return fnv(h, f, sizeof f);
}

/* A hash of what a motion may change of the style. */
static uint64_t sig_of(const sk_css_style *s)
{
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, &s->opacity, sizeof s->opacity);
    h = fnv(h, &s->color, sizeof s->color);
    h = fnv(h, &s->bg_color, sizeof s->bg_color);
    h = fnv(h, s->border_color, sizeof s->border_color);
    h = fnv(h, s->border_width, sizeof s->border_width);
    h = fnv(h, &s->outline_color, sizeof s->outline_color);
    h = fnv(h, &s->outline_width, sizeof s->outline_width);
    h = fnv(h, &s->outline_offset, sizeof s->outline_offset);
    h = fnv(h, &s->text_decoration_color, sizeof s->text_decoration_color);
    h = fnv(h, &s->fill, sizeof s->fill);
    h = fnv(h, &s->stroke, sizeof s->stroke);
    h = fnv(h, &s->fill_kind, 1);
    h = fnv(h, &s->stroke_kind, 1);
    h = fnv(h, &s->font_size, sizeof s->font_size);
    h = fnv(h, &s->letter_spacing, sizeof s->letter_spacing);
    for (int i = 0; i < 4; i++) {
        h = fnv_len(h, &s->inset[i]);
        h = fnv_len(h, &s->margin[i]);
        h = fnv_len(h, &s->padding[i]);
    }
    h = fnv_len(h, &s->width);
    h = fnv_len(h, &s->height);
    h = fnv_len(h, &s->stroke_width);
    h = fnv(h, &s->ntransform, sizeof s->ntransform);
    for (uint16_t i = 0; i < s->ntransform; i++) {
        h = fnv(h, &s->transform[i].kind, 1);
        h = fnv_len(h, &s->transform[i].tx);
        h = fnv_len(h, &s->transform[i].ty);
        h = fnv(h, s->transform[i].v, sizeof s->transform[i].v);
    }
    h = fnv(h, &s->ntext_shadow, sizeof s->ntext_shadow);
    for (uint16_t i = 0; i < s->ntext_shadow; i++)
        h = fnv_shadow(h, &s->text_shadow[i]);
    h = fnv(h, &s->nbox_shadow, sizeof s->nbox_shadow);
    for (uint16_t i = 0; i < s->nbox_shadow; i++)
        h = fnv_shadow(h, &s->box_shadow[i]);
    h = fnv(h, &s->nfilter, sizeof s->nfilter);
    for (uint16_t i = 0; i < s->nfilter; i++) {
        h = fnv(h, &s->filter[i].kind, 1);
        h = fnv(h, &s->filter[i].amount, sizeof s->filter[i].amount);
        h = fnv_shadow(h, &s->filter[i].shadow);
    }
    return h;
}

uint64_t sk_motion_sig(const sk_motion *m)
{
    return m ? m->sig : 0;
}

uint64_t sk_motion_sig_but_opacity(const sk_motion *m)
{
    return m ? m->sig_but_opacity : 0;
}

bool sk_motion_fades(const sk_motion *m)
{
    return m && m->nan && m->opacity_only && !m->ntr;
}

const sk_css_style *sk_motion_style(sk_skinview *v, uint32_t n, bool *moving, double *due)
{
    sk_vnode *x = &v->nodes[n];
    sk_motion *m = x->mo;
    if (!m)
        return x->base;
    m->shown = *x->base;
    double dt = fmax(0, (v->now - m->at) / 1000.0);
    m->at = v->now;
    pval *val = sk_xmalloc(sizeof *val);
    m->opacity_only = true;
    for (size_t i = 0; i < m->nan; i++) {
        anim *an = &m->an[i];
        if (an->slot >= x->base->nanim_name)
            continue;
        sk_css_anim a = sk_style_anim(x->base, an->slot);
        if (!a.paused)
            an->clock += dt;
        double p;
        bool done;
        bool on = progress(&a, an->clock, &p, &done);
        if (!a.paused && !done)
            *moving = true;
        const sk_pack_keyframes *kf = &v->p->keyframes[an->keyframes];
        bool has_xf = false, has_op = false;
        for (uint16_t k = 0; k < kf->nkeys; k++) {
            has_xf |= kf->keys[k].has_transform;
            has_op |= kf->keys[k].has_opacity;
        }
        if (has_xf)
            m->opacity_only = false;
        if (!on)
            continue;
        if (has_xf) {
            track(kf, &a, SK_PROP_TRANSFORM, x->base, p, val);
            put(m, SK_PROP_TRANSFORM, val);
        }
        if (has_op) {
            track(kf, &a, SK_PROP_OPACITY, x->base, p, val);
            put(m, SK_PROP_OPACITY, val);
        }
    }
    for (size_t i = 0; i < m->ntr;) {
        trans *t = &m->tr[i];
        double el = v->now - t->start - t->delay;
        if (el >= t->duration) {
            trans_drop(m, i);
            continue;
        }
        if (el < 0) {
            put(m, t->prop, t->from);
            if (*due < 0 || t->start + t->delay < *due)
                *due = t->start + t->delay;
        } else {
            lerp(t->from, t->to, sk_timing_eval(&t->timing, el / t->duration), val);
            put(m, t->prop, val);
            *moving = true;
        }
        i++;
    }
    free(val);
    m->sig = sig_of(&m->shown);
    float o = m->shown.opacity;
    m->shown.opacity = 1;
    m->sig_but_opacity = sig_of(&m->shown);
    m->shown.opacity = o;
    return &m->shown;
}
