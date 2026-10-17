/* app/renderer/widgets.js in C: the seven display widgets of decision
 * 0007, the list of decision 0034 with its toggle, and the usage
 * limits of decision 0011, each turning the metrics into what its
 * elements show. update() runs when the metrics come (ten times a
 * second); frame() every step, moving each value toward its target with
 * widgets.js's damped spring. What a widget shows lands on its
 * elements' nodes: text, a fill's or a frame's value, a needle's or a
 * knob's turn, and the state variables of decision 0037 (nodata,
 * fallback, on, open, the stage's list, a row's item, a limits
 * widget's count and its meters' model and span). */
#include "viewint.h"

#include "../base/json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Numbers as JavaScript writes them ---- */

/* Number.prototype.toFixed: the exact value rounded half up, its digits
 * in full. */
static void js_fixed(sk_buf *out, double x, int digits)
{
    if (!isfinite(x) || fabs(x) >= 1e21) {
        sk_json_format_number(out, x);
        return;
    }
    char exact[512];
    snprintf(exact, sizeof exact, "%.60f", fabs(x));
    const char *dot = strchr(exact, '.');
    size_t ip = (size_t)(dot - exact);
    char digs[512];
    size_t n = 0;
    for (size_t i = 0; i < ip; i++)
        digs[n++] = exact[i];
    for (int i = 0; i < digits; i++)
        digs[n++] = dot[1 + i];
    bool up = dot[1 + digits] >= '5';
    for (size_t i = n; up && i-- > 0;) {
        if (digs[i] == '9') {
            digs[i] = '0';
        } else {
            digs[i]++;
            up = false;
        }
    }
    if (x < 0)
        sk_buf_putc(out, '-');
    if (up)
        sk_buf_putc(out, '1');
    sk_buf_append(out, digs, n - (size_t)digits);
    if (digits) {
        sk_buf_putc(out, '.');
        sk_buf_append(out, digs + n - (size_t)digits, (size_t)digits);
    }
}

/* Math.round: half toward positive infinity. */
static double js_round(double v)
{
    if (!isfinite(v) || fabs(v) >= 4503599627370496.0)
        return v;
    return floor(v + 0.5);
}

/* String(n) of a number. */
static void js_num(sk_buf *out, double v)
{
    sk_json_format_number(out, v == 0 ? 0 : v);
}

/* toLocaleString('en-US') of a whole number: thousands by commas. */
static void js_grouped(sk_buf *out, double r)
{
    char d[400];
    snprintf(d, sizeof d, "%.0f", fabs(r));
    if (r < 0)
        sk_buf_putc(out, '-');
    size_t n = strlen(d);
    for (size_t i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0)
            sk_buf_putc(out, ',');
        sk_buf_putc(out, d[i]);
    }
}

static void pad2(sk_buf *out, double v)
{
    sk_buf_printf(out, "%02.0f", v);
}

void sk_widget_format_num(sk_buf *out, double v, uint8_t format, uint8_t digits, double now)
{
    if (format == SK_FORMAT_TEXT) {
        js_num(out, v);
        return;
    }
    if (!isfinite(v)) {
        sk_buf_append_c(out, "--");
        return;
    }
    if (fabs(v) < 1e-9)
        v = 0;
    switch (format) {
    case SK_FORMAT_INT: {
        double r = js_round(v);
        if (r == 0)
            sk_buf_putc(out, '0');
        else
            js_grouped(out, r);
        return;
    }
    case SK_FORMAT_FIXED:
        js_fixed(out, v, digits);
        return;
    case SK_FORMAT_PCT:
        js_fixed(out, v, 1);
        sk_buf_putc(out, '%');
        return;
    case SK_FORMAT_USD:
        sk_buf_putc(out, '$');
        js_fixed(out, v, v < 10 ? 2 : 1);
        return;
    case SK_FORMAT_TOKENS:
        if (fabs(v) < 1000) {
            js_num(out, js_round(v));
        } else if (fabs(v) < 1e6) {
            js_fixed(out, v / 1e3, 1);
            sk_buf_putc(out, 'k');
        } else {
            js_fixed(out, v / 1e6, 2);
            sk_buf_putc(out, 'M');
        }
        return;
    case SK_FORMAT_DURATION: {
        double s = fmax(0, floor(v));
        double h = floor(s / 3600), m = fmod(floor(s / 60), 60), ss = fmod(s, 60);
        if (h) {
            sk_buf_printf(out, "%.0f:", h);
            pad2(out, m);
        } else {
            sk_buf_printf(out, "%.0f", m);
        }
        sk_buf_putc(out, ':');
        pad2(out, ss);
        return;
    }
    case SK_FORMAT_UNTIL: {
        /* Unix seconds to the time left until then. */
        double s = floor(v - now);
        if (s <= 0) {
            sk_buf_append_c(out, "now");
            return;
        }
        double d = floor(s / 86400), h = fmod(floor(s / 3600), 24), m = fmod(floor(s / 60), 60);
        if (d)
            sk_buf_printf(out, "%.0fd %.0fh", d, h);
        else if (h)
            sk_buf_printf(out, "%.0fh %.0fm", h, m);
        else
            sk_buf_printf(out, "%.0fm", fmax(1, m));
        return;
    }
    case SK_FORMAT_BYTES:
        if (v < 1024) {
            js_num(out, js_round(v));
            sk_buf_append_c(out, " B");
        } else if (v < 1048576) {
            js_fixed(out, v / 1024, 1);
            sk_buf_append_c(out, " KB");
        } else {
            js_fixed(out, v / 1048576, 1);
            sk_buf_append_c(out, " MB");
        }
        return;
    default:
        js_num(out, v);
        return;
    }
}

void sk_widget_format(sk_buf *out, const sk_val *v, uint8_t format, uint8_t digits, double now)
{
    if (!v || v->type == SK_VAL_NULL)
        sk_buf_append_c(out, "--");
    else if (v->type == SK_VAL_STR)
        sk_buf_append_c(out, v->text ? v->text : "");
    else if (v->type == SK_VAL_NUM)
        sk_widget_format_num(out, v->num, format, digits, now);
    else if (format == SK_FORMAT_TEXT)
        sk_buf_append_c(out, v->text ? v->text : "");
    else
        sk_buf_append_c(out, "--");
}

/* ---- The spring ---- */

void sk_spring_init(sk_spring *s, double damping, double hz)
{
    memset(s, 0, sizeof *s);
    s->zeta = damping;
    s->w = 2 * M_PI * hz;
}

void sk_spring_set(sk_spring *s, bool has, double t)
{
    s->has_target = has;
    s->target = has ? t : 0;
    if (!s->has_v || !has) {
        s->has_v = has;
        s->v = s->target;
        s->vel = 0;
    }
}

/* Semi-implicit Euler in substeps of at most 1/240 s, so a slow frame
 * never makes the spring unstable. */
bool sk_spring_step(sk_spring *s, double dt, double *out)
{
    if (!s->has_v || !s->has_target) {
        *out = s->v;
        return s->has_v;
    }
    double total = fmin(fmax(dt, 0), 0.1);
    int n = (int)fmax(1, ceil(total * 240));
    double h = total / n;
    for (int i = 0; i < n; i++) {
        double a = s->w * s->w * (s->target - s->v) - 2 * s->zeta * s->w * s->vel;
        s->vel += a * h;
        s->v += s->vel * h;
    }
    /* Still at its target to far below what any format shows: held
     * there, so the view can rest. */
    double eps = 1e-7 * (1 + fabs(s->target));
    if (fabs(s->target - s->v) < eps && fabs(s->vel) < eps * 10) {
        s->v = s->target;
        s->vel = 0;
    }
    *out = s->v;
    return true;
}

bool sk_spring_rests(const sk_spring *s)
{
    return !s->has_v || !s->has_target || (s->v == s->target && s->vel == 0);
}

/* ---- Values ---- */

static void val_free(sk_val *v)
{
    free(v->text);
    *v = SK_NULLV;
}

static void val_copy(sk_val *dst, const sk_val *src)
{
    val_free(dst);
    if (!src)
        return;
    *dst = *src;
    dst->text = src->text ? sk_xstrdup(src->text) : NULL;
}

static bool val_num(const sk_val *v)
{
    return v->type == SK_VAL_NUM;
}

/* frac(): v in [min, max] as 0 to 1, 0 for none. */
static double frac_of(const sk_pack_widget *w, bool has, double v)
{
    if (!has)
        return 0;
    return fmax(0, fmin(1, (v - w->min) / (w->max - w->min)));
}

/* toFixed(2) as a number: what widgets.js writes into a style. */
static float fixed2(double v)
{
    return (float)(js_round(v * 100) / 100);
}

static void set_text(sk_skinview *v, uint32_t e, const char *t)
{
    if (e != SK_PACK_NONE)
        sk_view_set_text(v, e, t);
}

static void set_var(sk_skinview *v, uint32_t e, uint8_t var, uint8_t value)
{
    if (e != SK_PACK_NONE)
        sk_view_set_var(v, e, var, value);
}

static void set_value(sk_skinview *v, uint32_t e, float value)
{
    if (e == SK_PACK_NONE || v->nodes[e].value == value)
        return;
    v->nodes[e].value = value;
    v->dirty = true;
}

static void set_turn(sk_skinview *v, uint32_t e, float turn)
{
    if (e == SK_PACK_NONE || v->nodes[e].turn == turn)
        return;
    v->nodes[e].turn = turn;
    v->dirty = true;
}

/* A value of the snapshot a widget reads: the view's metrics, or a
 * limit's own { pct, resets }. */
typedef struct {
    const sk_skinview *v;
    const sk_val *pct, *resets; /* a meter's */
} snap;

static const sk_val *snap_get(const snap *s, const char *name)
{
    if (!name)
        return NULL;
    if (!s->v)
        return !strcmp(name, "pct") ? s->pct : !strcmp(name, "resets") ? s->resets : NULL;
    return sk_view_metric(s->v, name);
}

static bool present(const sk_val *x)
{
    return x && x->type != SK_VAL_NULL;
}

/* ---- The parts ---- */

static void parts_init(sk_wparts *p)
{
    memset(p, 0, sizeof *p);
    uint32_t *f = &p->root;
    for (uint32_t *x = f; x <= &p->opt_other; x++)
        *x = SK_PACK_NONE;
}

static void add_part(uint32_t **list, uint16_t *n, uint32_t e)
{
    *list = sk_xreallocarray(*list, (size_t)*n + 1, sizeof **list);
    (*list)[(*n)++] = e;
}

static void part(sk_wparts *p, const sk_pack_el *el, uint32_t e)
{
    switch (el->role) {
    case SK_ROLE_WIDGET:
    case SK_ROLE_METER:
        p->root = e;
        break;
    case SK_ROLE_LABEL:
        if (p->label == SK_PACK_NONE)
            p->label = e;
        break;
    case SK_ROLE_VALUE:
        p->value = e;
        break;
    case SK_ROLE_RESETS:
        p->resets = e;
        break;
    case SK_ROLE_RESETS_LEFT:
        p->resets_left = e;
        break;
    case SK_ROLE_TRACK:
        p->track = e;
        break;
    case SK_ROLE_FILL:
        p->fill = e;
        break;
    case SK_ROLE_SEG:
        add_part(&p->segs, &p->nsegs, e);
        break;
    case SK_ROLE_LAMP:
        p->lamp = e;
        break;
    case SK_ROLE_SHEET:
        p->sheet = e;
        break;
    case SK_ROLE_ARC_FILL:
        p->arc_fill = e;
        break;
    case SK_ROLE_MARK:
        add_part(&p->marks, &p->nmarks, e);
        break;
    case SK_ROLE_DIAL_TEXT:
        p->dial_text = e;
        break;
    case SK_ROLE_NEEDLE_GROUP:
    case SK_ROLE_KNOB_TURN:
        p->turn = e;
        break;
    case SK_ROLE_ITEMS:
        p->items = e;
        break;
    case SK_ROLE_ITEM:
        p->row = e;
        break;
    case SK_ROLE_TOGGLE:
        p->toggle = e;
        break;
    case SK_ROLE_OPTION_LIST:
        p->opt_list = e;
        break;
    case SK_ROLE_OPTION_OTHER:
        p->opt_other = e;
        break;
    default:
        break;
    }
}

static void spring_of(sk_wstate *s)
{
    const sk_pack_widget *w = s->w;
    double hz = s->type == SK_WIDGET_NEEDLE ? 2.5 : s->type == SK_WIDGET_READOUT ? 4 : 3;
    sk_spring_init(&s->sp, w->damping, hz);
}

static void state_init(sk_wstate *s, const sk_pack_widget *w, uint8_t type, bool meter)
{
    memset(s, 0, sizeof *s);
    s->w = w;
    s->type = type;
    s->meter = meter;
    s->pulse_at = -1e9;
    s->list = -1;
    parts_init(&s->parts);
    spring_of(s);
}

void sk_widgets_init(sk_skinview *v)
{
    const sk_pack *p = v->p;
    v->nwidgets = p->nwidgets;
    v->widgets = sk_xcalloc(p->nwidgets ? p->nwidgets : 1, sizeof *v->widgets);
    for (uint32_t i = 0; i < p->nwidgets; i++) {
        const sk_pack_widget *w = &p->widgets[i];
        sk_wstate *s = &v->widgets[i];
        state_init(s, w, w->type, false);
        if (w->type == SK_WIDGET_LIMITS) {
            uint8_t mt = w->needle_meters ? SK_WIDGET_NEEDLE : SK_WIDGET_BAR;
            for (int g = 0; g < 8; g++) {
                s->meters[g] = sk_xcalloc((size_t)g + 1, sizeof *s->meters[g]);
                for (int m = 0; m <= g; m++)
                    state_init(&s->meters[g][m], w, mt, true);
            }
        }
    }
    for (uint32_t e = 0; e < p->nels; e++) {
        const sk_pack_el *el = &p->els[e];
        if (el->widget == SK_PACK_NONE || el->widget >= p->nwidgets)
            continue;
        sk_wstate *s = &v->widgets[el->widget];
        if (el->group && el->meter && el->group <= 8 && el->meter <= el->group && s->meters[0])
            part(&s->meters[el->group - 1][el->meter - 1].parts, el, e);
        else
            part(&s->parts, el, e);
    }
    for (uint32_t l = 0; l < v->nlists; l++)
        v->widgets[v->lists[l].widget].list = (int)l;
}

static void state_free(sk_wstate *s)
{
    free(s->parts.segs);
    free(s->parts.marks);
    val_free(&s->raw);
    for (size_t i = 0; i < s->nitems; i++)
        free(s->item_text[i]);
    free(s->item_text);
    free(s->item_state);
    free(s->drawn);
    free(s->limits_key);
    for (int g = 0; g < 8; g++) {
        for (int m = 0; s->meters[g] && m <= g; m++)
            state_free(&s->meters[g][m]);
        free(s->meters[g]);
    }
}

void sk_widgets_free(sk_skinview *v)
{
    for (uint32_t i = 0; i < v->nwidgets; i++)
        state_free(&v->widgets[i]);
    free(v->widgets);
    v->widgets = NULL;
    v->nwidgets = 0;
}

/* ---- update() ---- */

/* resetsOf(): the time to a usage limit's reset, its word and its time
 * spans of their own. */
static void update_resets(sk_skinview *v, sk_wstate *s, const snap *sn, const char *name)
{
    if (s->parts.resets == SK_PACK_NONE)
        return;
    const sk_val *at = name ? snap_get(sn, name) : NULL;
    bool none = !present(at);
    set_var(v, s->parts.resets, SK_VAR_NODATA, none);
    sk_buf b = {0};
    if (none)
        sk_buf_append_c(&b, "--");
    else
        sk_widget_format(&b, at, SK_FORMAT_UNTIL, 0, v->wall);
    set_text(v, s->parts.resets_left, sk_buf_cstr(&b));
    sk_buf_free(&b);
}

/* fallbackOf(): the fallback in force while the widget's own metric is
 * absent and the fallback's present; its label then. */
static void update_fallback(sk_skinview *v, sk_wstate *s, const snap *sn)
{
    const sk_pack_widget *w = s->w;
    bool use = !s->meter && w->fallback_bind && !present(snap_get(sn, w->bind)) &&
               present(snap_get(sn, w->fallback_bind));
    s->fallback = use;
    if (s->meter || (w->type != SK_WIDGET_BAR && w->type != SK_WIDGET_READOUT))
        return;
    if (w->fallback_bind)
        set_var(v, s->parts.root, SK_VAR_FALLBACK, use);
    if (w->fallback_bind && s->parts.label != SK_PACK_NONE)
        set_text(v, s->parts.label, use && w->fallback_label ? w->fallback_label : w->label);
}

static const char *bind_of(const sk_wstate *s)
{
    if (s->meter)
        return "pct";
    return s->fallback ? s->w->fallback_bind : s->w->bind;
}

static const char *resets_of(const sk_wstate *s)
{
    if (s->meter)
        return "resets";
    return s->fallback && s->w->fallback_resets ? s->w->fallback_resets : s->w->resets;
}

/* A gauge's marks at their values, or hidden. */
static void update_marks(sk_skinview *v, sk_wstate *s, const snap *sn)
{
    const sk_pack_widget *w = s->w;
    float a0 = w->start_angle, a1 = w->end_angle, th = w->thickness;
    float r = 50 - th / 2 - 2;
    for (uint16_t i = 0; i < s->parts.nmarks && i < w->nmarks; i++) {
        uint32_t e = s->parts.marks[i];
        sk_vnode *n = &v->nodes[e];
        const sk_pack_mark *m = &w->marks[i];
        double mv = m->value;
        bool has = true;
        if (m->bind) {
            const sk_val *x = snap_get(sn, m->bind);
            has = x && val_num(x);
            mv = has ? x->num : 0;
        }
        sk_pack_svg g = n->svg;
        g.kind = SK_SVG_LINE;
        if (has) {
            double a = (a0 + (a1 - a0) * frac_of(w, true, mv)) * M_PI / 180;
            g.v[0] = (float)(50 + (r + th / 2 + 1) * sin(a));
            g.v[1] = (float)(50 - (r + th / 2 + 1) * cos(a));
            g.v[2] = (float)(50 + (r - th / 2 - 1) * sin(a));
            g.v[3] = (float)(50 - (r - th / 2 - 1) * cos(a));
        }
        if (n->hide != !has || memcmp(&g, &n->svg, sizeof g) || !n->own_svg) {
            n->hide = !has;
            n->svg = g;
            n->own_svg = true;
            v->dirty = true;
        }
    }
}

/* update() of the one-value widgets: gauge, needle, knob, bar, readout,
 * sprite. */
static void update_value(sk_skinview *v, sk_wstate *s, const snap *sn)
{
    update_fallback(v, s, sn);
    const sk_val *raw = snap_get(sn, bind_of(s));
    val_copy(&s->raw, raw);
    bool num = val_num(&s->raw) && !(s->type == SK_WIDGET_READOUT && s->w->format == SK_FORMAT_TEXT);
    sk_spring_set(&s->sp, num, s->raw.num);
    set_var(v, s->parts.root, SK_VAR_NODATA, s->raw.type == SK_VAL_NULL);
    if (s->type == SK_WIDGET_GAUGE)
        update_marks(v, s, sn);
    if (s->type == SK_WIDGET_NEEDLE || s->type == SK_WIDGET_BAR || s->type == SK_WIDGET_READOUT)
        update_resets(v, s, sn, resets_of(s));
}

static void update_led(sk_skinview *v, sk_wstate *s, const snap *sn)
{
    const sk_val *x = snap_get(sn, s->w->bind);
    set_var(v, s->parts.root, SK_VAR_NODATA, !present(x));
    bool num = x && val_num(x);
    if (s->w->pulse) {
        if (num && s->has_last && x->num > s->last)
            s->pulse_at = v->now;
        if (num) {
            s->last = x->num;
            s->has_last = true;
        }
    } else {
        s->lit = num && s->w->has_on && x->num >= s->w->on;
    }
}

/* String(x) of a JSON value, as a row's text shows it. */
static void js_string(sk_buf *out, const sk_json *x)
{
    if (!x || x->type == SK_JSON_NONE)
        sk_buf_append_c(out, "undefined");
    else if (x->type == SK_JSON_NULL)
        sk_buf_append_c(out, "null");
    else if (x->type == SK_JSON_BOOL)
        sk_buf_append_c(out, x->boolean ? "true" : "false");
    else if (x->type == SK_JSON_NUMBER)
        js_num(out, x->number);
    else if (x->type == SK_JSON_STRING)
        sk_buf_append_str(out, x->string);
    else if (x->type == SK_JSON_ARRAY)
        for (size_t i = 0; i < x->array.len; i++) {
            if (i)
                sk_buf_putc(out, ',');
            const sk_json *y = &x->array.items[i];
            if (y->type != SK_JSON_NULL)
                js_string(out, y);
        }
    else
        sk_buf_append_c(out, "[object Object]");
}

/* The list (decision 0034): one row per item, at most rows, its mark
 * and text and the item's state; nodata, and hidden, while empty. */
static void update_list(sk_skinview *v, sk_wstate *s)
{
    const sk_val *x = sk_view_metric(v, s->w->bind);
    const char *json = x && x->type == SK_VAL_LIST && x->text ? x->text : "";
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *arr = *json ? sk_json_parse(&a, json, strlen(json), NULL) : NULL;
    size_t n = arr && arr->type == SK_JSON_ARRAY ? arr->array.len : 0;
    if (s->w->rows && n > s->w->rows)
        n = s->w->rows;
    set_var(v, s->parts.root, SK_VAR_NODATA, n == 0);
    sk_buf key = {0};
    sk_buf_printf(&key, "%zu:", n);
    sk_buf_append_c(&key, json);
    if (s->drawn && !strcmp(s->drawn, sk_buf_cstr(&key))) {
        sk_buf_free(&key);
        sk_arena_free(&a);
        return;
    }
    free(s->drawn);
    s->drawn = sk_xstrdup(sk_buf_cstr(&key));
    sk_buf_free(&key);
    for (size_t i = 0; i < s->nitems; i++)
        free(s->item_text[i]);
    s->nitems = n;
    s->item_text = sk_xreallocarray(s->item_text, n ? n : 1, sizeof *s->item_text);
    s->item_state = sk_xreallocarray(s->item_state, n ? n : 1, 1);
    for (size_t i = 0; i < n; i++) {
        const sk_json *it = &arr->array.items[i];
        sk_str st = {0};
        bool has = it->type == SK_JSON_OBJECT && sk_json_as_string(sk_json_get(it, "state"), &st);
        s->item_state[i] = has && sk_str_eq_c(st, "doing") ? 1 : has && sk_str_eq_c(st, "done") ? 2 : 0;
        sk_buf t = {0};
        js_string(&t, it->type == SK_JSON_OBJECT ? sk_json_get(it, "text") : NULL);
        s->item_text[i] = sk_xstrdup(sk_buf_cstr(&t));
        sk_buf_free(&t);
    }
    sk_arena_free(&a);
    if (s->list < 0)
        return;
    sk_vlist *L = &v->lists[s->list];
    uint16_t rows = (uint16_t)SK_MIN(n, (size_t)SK_VIEW_MAX_ROWS);
    if (L->rows != rows) {
        L->rows = rows;
        v->dirty = true;
    }
    static const char *const MARK[3] = {"[ ]", "[>]", "[x]"};
    for (uint16_t r = 0; r < rows; r++) {
        for (uint32_t k = 0; k < L->nsub; k++) {
            uint32_t e = L->sub[k];
            uint32_t node = r == 0 ? e : L->base + (uint32_t)(r - 1) * L->nsub + k;
            uint8_t src = v->p->els[e].text_source;
            if (src == SK_TEXT_ITEM_MARK)
                sk_view_set_text(v, node, MARK[s->item_state[r]]);
            else if (src == SK_TEXT_ITEM_TEXT)
                sk_view_set_text(v, node, s->item_text[r]);
        }
        sk_view_set_var(v, r == 0 ? L->row : L->base + (uint32_t)(r - 1) * L->nsub, SK_VAR_ITEM,
                        s->item_state[r]);
    }
}

/* A limit's span class: span- and the lowercased letters and digits of
 * its window among the pack's span classes, spend with none. */
static uint8_t span_of(const sk_pack *p, const sk_json *window)
{
    sk_str w = {0};
    if (!window || window->type == SK_JSON_NULL || window->type == SK_JSON_NONE ||
        (window->type == SK_JSON_STRING && window->string.len == 0) || (window->type == SK_JSON_BOOL && !window->boolean) ||
        (window->type == SK_JSON_NUMBER && (window->number == 0 || isnan(window->number))))
        return 0;
    sk_buf raw = {0}, cls = {0};
    if (window->type == SK_JSON_STRING)
        w = window->string;
    else {
        js_string(&raw, window);
        w = sk_buf_str(&raw);
    }
    sk_buf_append_c(&cls, "span-");
    for (size_t i = 0; i < w.len; i++) {
        char c = w.ptr[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            sk_buf_putc(&cls, c);
    }
    uint8_t out = (uint8_t)(p->nspans + 1);
    for (uint32_t i = 0; i < p->nspans; i++)
        if (!strcmp(p->spans[i], sk_buf_cstr(&cls)))
            out = (uint8_t)(i + 1);
    sk_buf_free(&raw);
    sk_buf_free(&cls);
    return out;
}

static void json_key(sk_buf *b, const sk_json *x)
{
    if (!x || x->type == SK_JSON_NONE) {
        sk_buf_append_c(b, "null");
        return;
    }
    sk_jw w;
    sk_jw_init(&w, b, 0);
    sk_jw_value(&w, x);
}

static void update_meter(sk_skinview *v, sk_wstate *m, const sk_val *pct, const sk_val *resets)
{
    snap sn = {NULL, pct, resets};
    update_value(v, m, &sn);
}

/* The usage limits (decision 0011): a meter per item with a reading
 * whose reset has not passed, the first SK_PACK_MAX_LIMITS of them; the
 * widget's count, its meters' labels, model and span; meters made anew
 * when the items' keys, labels, windows or models change. */
static void update_limits(sk_skinview *v, sk_wstate *s)
{
    const sk_val *x = sk_view_metric(v, s->w->bind);
    const char *json = x && x->type == SK_VAL_LIST && x->text ? x->text : "";
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *arr = *json ? sk_json_parse(&a, json, strlen(json), NULL) : NULL;
    const sk_json *items[SK_PACK_MAX_LIMITS];
    size_t n = 0;
    for (size_t i = 0; arr && arr->type == SK_JSON_ARRAY && i < arr->array.len && n < SK_PACK_MAX_LIMITS; i++) {
        const sk_json *it = &arr->array.items[i];
        if (it->type != SK_JSON_OBJECT)
            continue;
        const sk_json *pct = sk_json_get(it, "pct"), *at = sk_json_get(it, "resets_at");
        if (!pct || pct->type != SK_JSON_NUMBER)
            continue;
        if (at && at->type == SK_JSON_NUMBER && at->number <= v->wall)
            continue;
        items[n++] = it;
    }
    sk_buf key = {0};
    for (size_t i = 0; i < n; i++) {
        sk_buf_putc(&key, '[');
        json_key(&key, sk_json_get(items[i], "key"));
        sk_buf_putc(&key, ',');
        json_key(&key, sk_json_get(items[i], "label"));
        sk_buf_putc(&key, ',');
        json_key(&key, sk_json_get(items[i], "window"));
        sk_buf_putc(&key, ',');
        json_key(&key, sk_json_get(items[i], "model"));
        sk_buf_putc(&key, ']');
    }
    if (!s->limits_key || strcmp(s->limits_key, sk_buf_cstr(&key)) != 0) {
        free(s->limits_key);
        s->limits_key = sk_xstrdup(sk_buf_cstr(&key));
        s->count = (uint8_t)n;
        set_var(v, s->parts.root, SK_VAR_COUNT, (uint8_t)n);
        v->dirty = true;
        for (size_t i = 0; i < n; i++) {
            sk_wstate *m = &s->meters[n - 1][i];
            /* A meter made anew: its spring from nothing. */
            spring_of(m);
            val_free(&m->raw);
            const sk_json *model = sk_json_get(items[i], "model");
            bool is_model = model && model->type != SK_JSON_NULL &&
                            !(model->type == SK_JSON_STRING && model->string.len == 0) &&
                            !(model->type == SK_JSON_BOOL && !model->boolean) &&
                            !(model->type == SK_JSON_NUMBER && (model->number == 0 || isnan(model->number)));
            set_var(v, m->parts.root, SK_VAR_MODEL, is_model);
            set_var(v, m->parts.root, SK_VAR_SPAN, span_of(v->p, sk_json_get(items[i], "window")));
            /* The label's own text; a suffix is a span of its own. */
            sk_buf label = {0};
            js_string(&label, sk_json_get(items[i], "label"));
            set_text(v, m->parts.label, sk_buf_cstr(&label));
            sk_buf_free(&label);
        }
    }
    sk_buf_free(&key);
    for (size_t i = 0; i < n; i++) {
        const sk_json *pj = sk_json_get(items[i], "pct"), *rj = sk_json_get(items[i], "resets_at");
        sk_val pct = SK_NUM(pj->number), at = SK_NULLV;
        if (rj && rj->type == SK_JSON_NUMBER)
            at = SK_NUM(rj->number);
        update_meter(v, &s->meters[n - 1][i], &pct, rj && rj->type != SK_JSON_NULL ? &at : NULL);
    }
    sk_arena_free(&a);
}

/* The stage's list (decision 0034) and the toggles' options: the
 * stage carries checklist while the task list has items and shows,
 * checklist-off while it has items and the tab shows what it replaces,
 * which only a skin whose list has a toggle hides. */
void sk_widgets_marks(sk_skinview *v)
{
    const sk_val *items = sk_view_metric(v, "checklist.items");
    bool has = false;
    if (items && items->type == SK_VAL_LIST && items->text) {
        sk_arena a;
        sk_arena_init(&a, 0);
        const sk_json *arr = sk_json_parse(&a, items->text, strlen(items->text), NULL);
        has = arr && arr->type == SK_JSON_ARRAY && arr->array.len > 0;
        sk_arena_free(&a);
    }
    bool toggled = false;
    for (uint32_t i = 0; i < v->nwidgets; i++)
        toggled |= v->widgets[i].parts.toggle != SK_PACK_NONE;
    bool off = v->list_off && toggled;
    set_var(v, v->stage, SK_VAR_LIST, has ? (off ? 2 : 1) : 0);
    for (uint32_t i = 0; i < v->nwidgets; i++) {
        sk_wstate *s = &v->widgets[i];
        set_var(v, s->parts.opt_list, SK_VAR_ON, !off);
        set_var(v, s->parts.opt_other, SK_VAR_ON, off);
        /* A readout that takes a click is open while its dial is. */
        if (s->type == SK_WIDGET_READOUT && s->w->format != SK_FORMAT_TEXT && s->w->bind)
            set_var(v, s->parts.root, SK_VAR_OPEN, sk_map_has(&v->dials, sk_str_c(s->w->bind)));
    }
}

void sk_widgets_update(sk_skinview *v)
{
    snap sn = {v, NULL, NULL};
    for (uint32_t i = 0; i < v->nwidgets; i++) {
        sk_wstate *s = &v->widgets[i];
        switch (s->type) {
        case SK_WIDGET_LED:
            update_led(v, s, &sn);
            break;
        case SK_WIDGET_LIST:
            update_list(v, s);
            break;
        case SK_WIDGET_LIMITS:
            update_limits(v, s);
            break;
        default:
            update_value(v, s, &sn);
            break;
        }
    }
    sk_widgets_marks(v);
}

/* ---- frame() ---- */

static void show_value(sk_skinview *v, sk_wstate *s, uint32_t e, bool has, double x, bool affix)
{
    if (e == SK_PACK_NONE)
        return;
    sk_buf b = {0};
    if (affix && s->w->prefix)
        sk_buf_append_c(&b, s->w->prefix);
    if (has)
        sk_widget_format_num(&b, x, s->w->format, s->w->digits, v->wall);
    else
        sk_widget_format(&b, &s->raw, s->w->format, s->w->digits, v->wall);
    if (affix && s->w->suffix)
        sk_buf_append_c(&b, s->w->suffix);
    set_text(v, e, sk_buf_cstr(&b));
    sk_buf_free(&b);
}

/* The next clock time a held lamp or sheet changes, kept if sooner. */
static void due_at(sk_skinview *v, double t)
{
    if (t > v->now && (v->next_due < 0 || t < v->next_due))
        v->next_due = t;
}

static void frame_one(sk_skinview *v, sk_wstate *s, double dt)
{
    const sk_pack_widget *w = s->w;
    double x = 0;
    bool has = false;
    if (s->type != SK_WIDGET_LED && s->type != SK_WIDGET_LIST && s->type != SK_WIDGET_LIMITS) {
        has = sk_spring_step(&s->sp, dt, &x);
        if (!sk_spring_rests(&s->sp))
            v->moving = true;
    }
    float a0 = w->start_angle, a1 = w->end_angle;
    switch (s->type) {
    case SK_WIDGET_GAUGE:
        set_value(v, s->parts.arc_fill, fixed2(frac_of(w, has, x) * 100));
        show_value(v, s, s->parts.dial_text, has, x, false);
        break;
    case SK_WIDGET_NEEDLE: {
        double f = has ? (x - w->min) / (w->max - w->min) : 0;
        set_turn(v, s->parts.turn, fixed2(a0 + (a1 - a0) * fmax(-0.04, fmin(1.04, f))));
        show_value(v, s, s->parts.value, has, x, false);
        break;
    }
    case SK_WIDGET_KNOB:
        set_turn(v, s->parts.turn, fixed2(a0 + (a1 - a0) * frac_of(w, has, x)));
        show_value(v, s, s->parts.value, has, x, false);
        break;
    case SK_WIDGET_BAR: {
        double f = frac_of(w, has, x);
        if (s->parts.nsegs) {
            int lit = (int)js_round(f * s->parts.nsegs);
            for (uint16_t i = 0; i < s->parts.nsegs; i++)
                set_var(v, s->parts.segs[i], SK_VAR_ON, i < lit);
        } else {
            set_value(v, s->parts.fill, fixed2(f * 100));
        }
        break;
    }
    case SK_WIDGET_LED: {
        bool on = s->lit;
        if (w->pulse) {
            on = v->now - s->pulse_at < w->pulse_ms;
            if (on)
                due_at(v, s->pulse_at + w->pulse_ms);
        }
        if (on && w->blink > 0) {
            double phase = floor(v->now / w->blink);
            on = fmod(phase, 2) == 0;
            due_at(v, (phase + 1) * w->blink);
        }
        set_var(v, s->parts.lamp, SK_VAR_ON, on);
        break;
    }
    case SK_WIDGET_READOUT:
        show_value(v, s, s->parts.value, has, x, true);
        break;
    case SK_WIDGET_SPRITE: {
        int n = w->frames;
        int i = 0;
        if (w->run) {
            double every = 1000 / (w->fps > 0 ? w->fps : 12);
            bool runs = val_num(&s->raw) && s->raw.num > 0;
            if (runs && v->now - s->run_at > every) {
                s->run_frame = n > 0 ? (s->run_frame + 1) % n : 0;
                s->run_at = v->now;
            }
            if (runs)
                due_at(v, s->run_at + every + 1e-3);
            i = s->run_frame;
        } else {
            i = (int)js_round(frac_of(w, has, x) * (n - 1));
        }
        set_value(v, s->parts.sheet, n > 1 ? (float)((double)i / (n - 1) * 100) : 0);
        break;
    }
    case SK_WIDGET_LIMITS:
        for (int m = 0; s->count && m < s->count; m++)
            frame_one(v, &s->meters[s->count - 1][m], dt);
        break;
    default:
        break;
    }
}

void sk_widgets_frame(sk_skinview *v, double dt)
{
    for (uint32_t i = 0; i < v->nwidgets; i++)
        frame_one(v, &v->widgets[i], dt);
}
