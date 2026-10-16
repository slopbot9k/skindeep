/* The element tree, built as app/renderer/skin.js and widgets.js build
 * the DOM: the host, the two stylesheet links, the stage, the panels in
 * the manifest's order, then each widget with its parts, a list's
 * toggle right after its list. The style attributes the JavaScript sets
 * become the app's own declarations (-sk-rect() for a rect's key,
 * -sk-value() for what a widget's value moves), and SVG's presentation
 * attributes declarations of their own layer. A limits widget holds its
 * meters once for each count it can show, 1 to SK_PACK_MAX_LIMITS; a
 * list holds one row, which stands for every row. */
#include "front.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

typedef struct {
    sk_front *f;
    sk_buf inline_css, hint_css;
    SK_VEC(sk_el *) inline_of, hint_of;
} builder;

static sk_el *el_new(builder *b, uint8_t kind, const char *tag, uint8_t role)
{
    sk_el *e = sk_arena_zalloc(&b->f->arena, sizeof *e);
    e->index = (uint32_t)b->f->els.len;
    e->kind = kind;
    e->tag = tag;
    e->role = role;
    e->widget = -1;
    e->vars = 1u << SK_VAR_HOVER;
    e->svg.kind = SK_SVG_NONE;
    e->svg.image = SK_PACK_NONE;
    sk_vec_push(&b->f->els, e);
    return e;
}

static void append(sk_el *parent, sk_el *child)
{
    child->parent = parent;
    child->prev = parent->last;
    if (parent->last)
        parent->last->next = child;
    else
        parent->first = child;
    parent->last = child;
    if (child->group == 0)
        child->nth = ++parent->nkids;
    if (parent->limits)
        child->limits = parent->limits;
}

/* Every element's count of siblings, once the tree is whole. */
static void count_siblings(sk_el *e)
{
    for (sk_el *c = e->first; c; c = c->next) {
        if (!c->group)
            c->siblings = e->nkids;
        count_siblings(c);
    }
}

/* Classes from a space-separated list, after those given. */
static void classes(builder *b, sk_el *e, int n, ...)
{
    SK_VEC(const char *) v = {0};
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        const char *list = va_arg(ap, const char *);
        if (!list)
            continue;
        const char *p = list;
        while (*p) {
            while (*p == ' ')
                p++;
            const char *q = p;
            while (*q && *q != ' ')
                q++;
            if (q > p)
                sk_vec_push(&v, sk_arena_strndup(&b->f->arena, p, (size_t)(q - p)));
            p = q;
        }
    }
    va_end(ap);
    e->nclasses = (uint16_t)v.len;
    e->classes = v.len ? sk_arena_array(&b->f->arena, v.len, sizeof *e->classes) : NULL;
    if (v.len)
        memcpy(e->classes, v.ptr, v.len * sizeof *e->classes);
    sk_vec_free(&v);
}

static sk_el *div_el(builder *b, sk_el *parent, const char *klass, uint8_t role)
{
    sk_el *e = el_new(b, SK_EL_DIV, "div", role);
    classes(b, e, 1, klass);
    append(parent, e);
    return e;
}

static sk_el *svg_el(builder *b, sk_el *parent, uint8_t kind, const char *tag, const char *klass, uint8_t role)
{
    sk_el *e = el_new(b, kind, tag, role);
    classes(b, e, 1, klass);
    append(parent, e);
    return e;
}

static void text(sk_el *e, uint8_t source, const char *t)
{
    e->text_source = source;
    e->text = t;
}

/* The app's own declarations for an element. */
static void inline_css(builder *b, sk_el *e, const char *fmt, ...) SK_PRINTF(3, 4);

static void inline_css(builder *b, sk_el *e, const char *fmt, ...)
{
    sk_buf_printf(&b->inline_css, "e%u{", (unsigned)e->index);
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b->inline_css, fmt, ap);
    va_end(ap);
    sk_buf_append_c(&b->inline_css, "}\n");
    sk_vec_push(&b->inline_of, e);
}

static void hint_css(builder *b, sk_el *e, const char *fmt, ...) SK_PRINTF(3, 4);

static void hint_css(builder *b, sk_el *e, const char *fmt, ...)
{
    sk_buf_printf(&b->hint_css, "e%u{", (unsigned)e->index);
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b->hint_css, fmt, ap);
    va_end(ap);
    sk_buf_append_c(&b->hint_css, "}\n");
    sk_vec_push(&b->hint_of, e);
}

/* s as the text of a CSS string, quoted: any quote, backslash or
 * control character escaped. */
static void css_string(sk_buf *out, const char *s)
{
    sk_buf_putc(out, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\' || c < 0x20 || c == 0x7f)
            sk_buf_printf(out, "\\%x ", (unsigned)c);
        else
            sk_buf_putc(out, (char)c);
    }
    sk_buf_putc(out, '"');
}

/* skin.js's place(): the rect's keys as px, the rest unset. */
static void place(builder *b, sk_el *e, const sk_pack_rect *r)
{
    static const char *const KEYS[] = {"left", "right", "width", "top", "bottom", "height"};
    sk_buf css = {0};
    for (int i = 0; i < 6; i++)
        if (r->has & (1 << i))
            sk_buf_printf(&css, "%s: -sk-rect(%.9g); ", KEYS[i], (double)r->v[i]);
    inline_css(b, e, "%s", sk_buf_cstr(&css));
    sk_buf_free(&css);
}

static void polar(float cx, float cy, float r, float deg, float *x, float *y)
{
    double rad = deg * M_PI / 180;
    *x = (float)(cx + r * sin(rad));
    *y = (float)(cy - r * cos(rad));
}

static void svg_line(sk_el *e, float x1, float y1, float x2, float y2)
{
    e->svg.kind = SK_SVG_LINE;
    e->svg.v[0] = x1;
    e->svg.v[1] = y1;
    e->svg.v[2] = x2;
    e->svg.v[3] = y2;
}

static void svg_arc(sk_el *e, float cx, float cy, float r, float a0, float a1, bool path_length)
{
    e->svg.kind = SK_SVG_ARC;
    e->svg.flag = path_length;
    e->svg.v[0] = cx;
    e->svg.v[1] = cy;
    e->svg.v[2] = r;
    e->svg.v[3] = a0;
    e->svg.v[4] = a1;
}

static void svg_box(sk_el *e, uint8_t kind, float x, float y, float w, float h, uint8_t flag)
{
    e->svg.kind = kind;
    e->svg.flag = flag;
    e->svg.v[0] = x;
    e->svg.v[1] = y;
    e->svg.v[2] = w;
    e->svg.v[3] = h;
}

/* labelled(): a .label when the spec has one. */
static sk_el *label(builder *b, sk_el *root, const sk_skin_widget *w, uint8_t source)
{
    if (!w->w.label)
        return NULL;
    sk_el *l = div_el(b, root, "label", SK_ROLE_LABEL);
    text(l, source, w->w.label);
    return l;
}

/* resetsOf(): .resets, .word and .left, when the spec names a reset. */
static void resets(builder *b, sk_el *root, const sk_skin_widget *w, bool meter)
{
    if (!w->w.resets && !w->w.fallback_resets && !meter)
        return;
    sk_el *r = div_el(b, root, "resets", SK_ROLE_RESETS);
    r->vars |= 1u << SK_VAR_NODATA;
    sk_el *word = el_new(b, SK_EL_SPAN, "span", SK_ROLE_RESETS_WORD);
    classes(b, word, 1, "word");
    text(word, SK_TEXT_STATIC, "resets ");
    append(r, word);
    sk_el *left = el_new(b, SK_EL_SPAN, "span", SK_ROLE_RESETS_LEFT);
    classes(b, left, 1, "left");
    text(left, SK_TEXT_RESETS, "--");
    append(r, left);
}

/* Marks the subtree below e as part of widget index. */
static void own(sk_el *e, int index)
{
    for (sk_el *c = e->first; c; c = c->next) {
        c->widget = index;
        own(c, index);
    }
}

static void gauge(builder *b, sk_el *root, const sk_skin_widget *w)
{
    const sk_pack_widget *s = &w->w;
    float a0 = s->start_angle, a1 = s->end_angle, th = s->thickness;
    float r = 50 - th / 2 - 2;
    sk_el *dial = svg_el(b, root, SK_EL_SVG, "svg", "dial", SK_ROLE_DIAL);
    svg_box(dial, SK_SVG_VIEWBOX, 0, 0, 100, 100, 0);
    sk_el *track = svg_el(b, dial, SK_EL_PATH, "path", "track", SK_ROLE_ARC_TRACK);
    svg_arc(track, 50, 50, r, a0, a1, true);
    /* A negative width the browser ignores as an attribute: none set. */
    if (th >= 0)
        hint_css(b, track, "fill: none; stroke-width: %.9g", (double)th);
    else
        hint_css(b, track, "fill: none");
    sk_el *fill = svg_el(b, dial, SK_EL_PATH, "path", "fill", SK_ROLE_ARC_FILL);
    svg_arc(fill, 50, 50, r, a0, a1, true);
    if (th >= 0)
        hint_css(b, fill, "fill: none; stroke-width: %.9g; stroke-dasharray: -sk-value() 100", (double)th);
    else
        hint_css(b, fill, "fill: none; stroke-dasharray: -sk-value() 100");
    for (int i = 0; s->ticks && i <= s->ticks; i++) {
        float a = a0 + (a1 - a0) * (float)i / s->ticks, x0, y0, x1, y1;
        polar(50, 50, r - th / 2 - 1, a, &x0, &y0);
        polar(50, 50, r - th / 2 - 5, a, &x1, &y1);
        sk_el *t = svg_el(b, dial, SK_EL_LINE, "line", "tick", SK_ROLE_TICK);
        svg_line(t, x0, y0, x1, y1);
    }
    for (uint16_t i = 0; i < s->nmarks; i++) {
        sk_el *m = el_new(b, SK_EL_LINE, "line", SK_ROLE_MARK);
        classes(b, m, 2, "mark", s->marks[i].klass);
        append(dial, m);
        /* Placed by its value at run time: r + th/2 + 1 to r - th/2 - 1. */
        svg_line(m, 0, 0, 0, 0);
    }
    sk_el *v = svg_el(b, dial, SK_EL_TEXT, "text", "value", SK_ROLE_DIAL_TEXT);
    v->svg.kind = SK_SVG_TEXT;
    v->svg.v[0] = 50;
    v->svg.v[1] = 56;
    hint_css(b, v, "text-anchor: middle");
    text(v, SK_TEXT_VALUE, "--");
    label(b, root, w, SK_TEXT_STATIC);
}

static void needle_parts(builder *b, sk_el *root, const sk_skin_widget *w, bool meter)
{
    const sk_pack_widget *s = &w->w;
    float a0 = s->start_angle, a1 = s->end_angle;
    sk_el *dial = svg_el(b, root, SK_EL_SVG, "svg", "dial", SK_ROLE_DIAL);
    svg_box(dial, SK_SVG_VIEWBOX, 0, 0, 100, 75, 0);
    if (w->face) {
        sk_el *face = svg_el(b, dial, SK_EL_IMAGE, "image", "face", SK_ROLE_FACE);
        svg_box(face, SK_SVG_IMAGE, 0, 0, 100, 75, 1);
    } else {
        sk_el *track = svg_el(b, dial, SK_EL_PATH, "path", "track", SK_ROLE_ARC_TRACK);
        svg_arc(track, 50, 70, 55, a0, a1, false);
        hint_css(b, track, "fill: none");
    }
    for (int i = 0; s->ticks && i <= s->ticks; i++) {
        float a = a0 + (a1 - a0) * (float)i / s->ticks, x0, y0, x1, y1;
        polar(50, 70, 55, a, &x0, &y0);
        polar(50, 70, 49, a, &x1, &y1);
        sk_el *t = svg_el(b, dial, SK_EL_LINE, "line", "tick", SK_ROLE_TICK);
        svg_line(t, x0, y0, x1, y1);
    }
    sk_el *g = svg_el(b, dial, SK_EL_G, "g", "needle-g", SK_ROLE_NEEDLE_GROUP);
    g->svg.kind = SK_SVG_TURN;
    g->svg.v[0] = 50;
    g->svg.v[1] = 70;
    sk_el *n = svg_el(b, g, SK_EL_LINE, "line", "needle", SK_ROLE_NEEDLE);
    svg_line(n, 50, 70, 50, 14);
    sk_el *pivot = svg_el(b, dial, SK_EL_CIRCLE, "circle", "pivot", SK_ROLE_PIVOT);
    pivot->svg.kind = SK_SVG_CIRCLE;
    pivot->svg.v[0] = 50;
    pivot->svg.v[1] = 70;
    pivot->svg.v[2] = 4;
    sk_el *l = label(b, root, w, meter ? SK_TEXT_LABEL : SK_TEXT_STATIC);
    if (l && meter && s->suffix) {
        sk_el *sx = el_new(b, SK_EL_SPAN, "span", SK_ROLE_SUFFIX);
        classes(b, sx, 1, "suffix");
        text(sx, SK_TEXT_STATIC, s->suffix);
        append(l, sx);
    }
    sk_el *v = div_el(b, root, "value", SK_ROLE_VALUE);
    text(v, SK_TEXT_VALUE, "--");
    resets(b, root, w, meter);
}

static void knob(builder *b, sk_el *root, const sk_skin_widget *w)
{
    sk_el *box = svg_el(b, root, SK_EL_SVG, "svg", "knob-box", SK_ROLE_KNOB_BOX);
    svg_box(box, SK_SVG_VIEWBOX, 0, 0, 100, 100, 0);
    sk_el *turn = svg_el(b, box, SK_EL_G, "g", NULL, SK_ROLE_KNOB_TURN);
    turn->svg.kind = SK_SVG_TURN;
    turn->svg.v[0] = 50;
    turn->svg.v[1] = 50;
    if (w->image) {
        sk_el *im = svg_el(b, turn, SK_EL_IMAGE, "image", "knob-image", SK_ROLE_KNOB_IMAGE);
        svg_box(im, SK_SVG_IMAGE, 2, 2, 96, 96, 0);
    } else {
        sk_el *face = svg_el(b, turn, SK_EL_CIRCLE, "circle", "knob-face", SK_ROLE_KNOB_FACE);
        face->svg.kind = SK_SVG_CIRCLE;
        face->svg.v[0] = 50;
        face->svg.v[1] = 50;
        face->svg.v[2] = 46;
        sk_el *p = svg_el(b, turn, SK_EL_LINE, "line", "knob-pointer", SK_ROLE_KNOB_POINTER);
        svg_line(p, 50, 12, 50, 36);
    }
    label(b, root, w, SK_TEXT_STATIC);
    sk_el *v = div_el(b, root, "value", SK_ROLE_VALUE);
    text(v, SK_TEXT_VALUE, "--");
}

static void bar_parts(builder *b, sk_el *root, const sk_skin_widget *w, bool meter)
{
    const sk_pack_widget *s = &w->w;
    sk_el *l = label(b, root, w, meter || s->fallback_label ? SK_TEXT_LABEL : SK_TEXT_STATIC);
    if (l && meter && s->suffix) {
        sk_el *sx = el_new(b, SK_EL_SPAN, "span", SK_ROLE_SUFFIX);
        classes(b, sx, 1, "suffix");
        text(sx, SK_TEXT_STATIC, s->suffix);
        append(l, sx);
    }
    resets(b, root, w, meter);
    sk_el *track = div_el(b, root, "track", SK_ROLE_TRACK);
    if (s->segments) {
        /* A negative gap the browser ignores: none set. */
        if (s->gap >= 0)
            inline_css(b, track, "display: flex; flex-direction: %s; gap: %.9gpx", s->vertical ? "column-reverse" : "row",
                       (double)s->gap);
        else
            inline_css(b, track, "display: flex; flex-direction: %s", s->vertical ? "column-reverse" : "row");
        for (uint16_t i = 0; i < s->segments; i++) {
            sk_el *seg = div_el(b, track, "seg", SK_ROLE_SEG);
            seg->vars |= 1u << SK_VAR_ON;
        }
    } else {
        sk_el *fill = div_el(b, track, "fill", SK_ROLE_FILL);
        inline_css(b, fill, "%s: -sk-value()", s->vertical ? "height" : "width");
    }
}

static void led(builder *b, sk_el *root, const sk_skin_widget *w)
{
    sk_el *lamp = div_el(b, root, "lamp", SK_ROLE_LAMP);
    lamp->vars |= 1u << SK_VAR_ON;
    label(b, root, w, SK_TEXT_STATIC);
}

static void readout(builder *b, sk_el *root, const sk_skin_widget *w)
{
    label(b, root, w, w->w.fallback_label ? SK_TEXT_LABEL : SK_TEXT_STATIC);
    sk_el *v = div_el(b, root, "value", SK_ROLE_VALUE);
    text(v, SK_TEXT_VALUE, "--");
    resets(b, root, w, false);
}

static void sprite(builder *b, sk_el *root, const sk_skin_widget *w)
{
    const sk_pack_widget *s = &w->w;
    sk_el *sheet = div_el(b, root, "sheet", SK_ROLE_SHEET);
    /* The sheet's url is the skin's, not the stylesheet's: url_base
     * of the app's own declarations is the skin's root. */
    sk_buf url = {0};
    css_string(&url, w->sheet);
    if (s->column)
        inline_css(b, sheet, "background-image: url(%s); background-size: 100%% %.9g%%; background-position: 0 -sk-value()",
                   sk_buf_cstr(&url), (double)s->frames * 100);
    else
        inline_css(b, sheet, "background-image: url(%s); background-size: %.9g%% 100%%; background-position: -sk-value() 0",
                   sk_buf_cstr(&url), (double)s->frames * 100);
    sk_buf_free(&url);
    label(b, root, w, SK_TEXT_STATIC);
}

static void list(builder *b, sk_el *root, const sk_skin_widget *w)
{
    label(b, root, w, SK_TEXT_STATIC);
    sk_el *items = div_el(b, root, "items", SK_ROLE_ITEMS);
    sk_el *item = div_el(b, items, "item", SK_ROLE_ITEM);
    item->repeated = true;
    item->vars |= 1u << SK_VAR_ITEM;
    sk_el *mark = div_el(b, item, "mark", SK_ROLE_ITEM_MARK);
    text(mark, SK_TEXT_ITEM_MARK, "[ ]");
    sk_el *t = div_el(b, item, "text", SK_ROLE_ITEM_TEXT);
    text(t, SK_TEXT_ITEM_TEXT, "");
}

static void toggle(builder *b, sk_el *stage, const sk_skin_widget *w, int index)
{
    sk_el *e = el_new(b, SK_EL_DIV, "div", SK_ROLE_TOGGLE);
    classes(b, e, 3, "w", "w-toggle", w->toggle_class);
    e->widget = index;
    e->rect = 1000000 + index;
    append(stage, e);
    place(b, e, &w->toggle_rect);
    sk_el *a = div_el(b, e, "option list", SK_ROLE_OPTION_LIST);
    a->vars |= 1u << SK_VAR_ON;
    text(a, SK_TEXT_STATIC, w->toggle_labels[0]);
    sk_el *o = div_el(b, e, "option other", SK_ROLE_OPTION_OTHER);
    o->vars |= 1u << SK_VAR_ON;
    text(o, SK_TEXT_STATIC, w->toggle_labels[1]);
    own(e, index);
}

/* A limits widget's meters, for every count. */
static void limits(builder *b, sk_el *root, const sk_skin_widget *w, int index)
{
    root->limits = root;
    sk_skin_widget sub = *w;
    sub.w.label = "";
    for (uint8_t n = 1; n <= SK_PACK_MAX_LIMITS; n++) {
        for (uint8_t i = 1; i <= n; i++) {
            const char *type = w->w.needle_meters ? "needle" : "bar";
            char wt[32];
            snprintf(wt, sizeof wt, "w-%s", type);
            sk_el *m = el_new(b, SK_EL_DIV, "div", SK_ROLE_METER);
            m->group = n;
            m->meter = i;
            m->nth = i;
            m->siblings = n;
            m->limits = root;
            m->widget = index;
            classes(b, m, 4, "w", wt, "limit-item", w->item_class);
            m->vars |= 1u << SK_VAR_NODATA | 1u << SK_VAR_MODEL | 1u << SK_VAR_SPAN;
            if (!w->w.needle_meters && w->w.vertical) {
                /* bar(): a vertical meter carries .vertical. */
                const char **c = sk_arena_array(&b->f->arena, m->nclasses + 1u, sizeof *c);
                memcpy(c, m->classes, m->nclasses * sizeof *c);
                c[m->nclasses] = "vertical";
                m->classes = c;
                m->nclasses++;
            }
            append(root, m);
            if (w->w.needle_meters)
                needle_parts(b, m, &sub, true);
            else
                bar_parts(b, m, &sub, true);
        }
    }
    own(root, index);
}

static void widget(builder *b, sk_el *stage, int index)
{
    const sk_skin_widget *w = &b->f->skin.widgets[index];
    static const char *const TYPE[] = {"gauge", "needle", "knob", "bar", "led", "readout", "sprite", "list", "limits"};
    const char *type = TYPE[w->w.type];
    const char *extra = NULL;
    if (w->w.type == SK_WIDGET_BAR && w->w.vertical)
        extra = "vertical";
    if (w->w.type == SK_WIDGET_READOUT && w->w.format != SK_FORMAT_TEXT)
        extra = "click";
    if (w->w.type == SK_WIDGET_LIMITS)
        extra = w->w.across ? "across" : "down";
    sk_el *root = el_new(b, SK_EL_DIV, "div", SK_ROLE_WIDGET);
    char wt[32];
    snprintf(wt, sizeof wt, "w-%s", type);
    /* base() gives w, w-<type> and the spec's classes; bar() and
     * readout() and limits() add theirs after. */
    classes(b, root, 4, "w", wt, w->klass, extra);
    root->widget = index;
    root->rect = -(1 + index);
    append(stage, root);
    place(b, root, &w->rect);
    if (w->w.type == SK_WIDGET_LIMITS)
        root->vars |= 1u << SK_VAR_COUNT;
    else
        root->vars |= 1u << SK_VAR_NODATA;
    if (w->w.type == SK_WIDGET_READOUT && w->w.format != SK_FORMAT_TEXT)
        root->vars |= 1u << SK_VAR_OPEN;
    if ((w->w.type == SK_WIDGET_BAR || w->w.type == SK_WIDGET_READOUT) && w->w.fallback_bind)
        root->vars |= 1u << SK_VAR_FALLBACK;
    switch (w->w.type) {
    case SK_WIDGET_GAUGE:
        gauge(b, root, w);
        break;
    case SK_WIDGET_NEEDLE:
        needle_parts(b, root, w, false);
        break;
    case SK_WIDGET_KNOB:
        knob(b, root, w);
        break;
    case SK_WIDGET_BAR:
        bar_parts(b, root, w, false);
        break;
    case SK_WIDGET_LED:
        led(b, root, w);
        break;
    case SK_WIDGET_READOUT:
        readout(b, root, w);
        break;
    case SK_WIDGET_SPRITE:
        sprite(b, root, w);
        break;
    case SK_WIDGET_LIST:
        list(b, root, w);
        break;
    case SK_WIDGET_LIMITS:
        limits(b, root, w, index);
        break;
    }
    own(root, index);
    if (w->w.type == SK_WIDGET_LIST && w->has_toggle)
        toggle(b, stage, w, index);
}

/* Parses the app's declarations, one rule per element, and hands each
 * element its own. */
static void attach(builder *b, sk_css_sheet *sheet, const sk_buf *css, sk_el **of, size_t n, bool hint)
{
    sk_diags d = {0};
    sk_css_parse(sheet, hint ? "(presentation attributes)" : "(style attributes)", sk_buf_cstr(css), css->len, &d);
    if (d.items.len || sheet->rules.len != n)
        sk_die("skinc: the app's own declarations did not parse: %s", d.items.len ? d.items.ptr[0] : "rule count");
    sk_diags_free(&d);
    for (size_t i = 0; i < n; i++) {
        sk_css_rule *r = &sheet->rules.ptr[i];
        sk_xdecl *x = r->ndecls ? sk_arena_array(&b->f->arena, r->ndecls, sizeof *x) : NULL;
        for (size_t k = 0; k < r->ndecls; k++)
            sk_xdecl_init(&x[k], &r->decls[k], sheet, true);
        if (hint) {
            of[i]->hint_decls = x;
            of[i]->nhint = r->ndecls;
        } else {
            of[i]->inline_decls = x;
            of[i]->ninline = r->ndecls;
        }
    }
}

void sk_tree_build(sk_front *f)
{
    builder b;
    memset(&b, 0, sizeof b);
    b.f = f;
    sk_el *host = el_new(&b, SK_EL_HOST, NULL, SK_ROLE_NONE);
    sk_el *link = el_new(&b, SK_EL_LINK, "link", SK_ROLE_NONE);
    append(host, link);
    if (f->skin.stylesheet) {
        sk_el *l2 = el_new(&b, SK_EL_LINK, "link", SK_ROLE_NONE);
        append(host, l2);
    }
    sk_el *stage = div_el(&b, host, "skin", SK_ROLE_STAGE);
    stage->vars |= 1u << SK_VAR_LIST;
    inline_css(&b, stage, "inset: auto; left: 0; top: 0; width: 100%%; height: 100%%; transform-origin: 0 0");
    for (size_t i = 0; i < f->skin.npanels; i++) {
        sk_el *p = el_new(&b, SK_EL_DIV, "div", SK_ROLE_PANEL);
        classes(&b, p, 2, "panel", f->skin.panels[i].klass);
        p->rect = 1 + (int)i;
        append(stage, p);
        place(&b, p, &f->skin.panels[i].rect);
    }
    for (size_t i = 0; i < f->skin.nwidgets; i++)
        widget(&b, stage, (int)i);
    count_siblings(host);
    attach(&b, &f->inline_sheet, &b.inline_css, b.inline_of.ptr, b.inline_of.len, false);
    attach(&b, &f->hint_sheet, &b.hint_css, b.hint_of.ptr, b.hint_of.len, true);
    sk_buf_free(&b.inline_css);
    sk_buf_free(&b.hint_css);
    sk_vec_free(&b.inline_of);
    sk_vec_free(&b.hint_of);
}
