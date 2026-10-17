/* The skin runtime (src/skin/view.h), on small skins compiled for each
 * test: widgets.js's formats and spring; every widget type's state
 * variables, text, values and turns for given metrics; the stage's list
 * and the toggle; the limits widget's meters; transitions and
 * animations at given times; hover, the cursor and presses; the
 * splits dragged; the scale below the minimum; what a change paints
 * again; and what the window takes from a pack. */
#include "../../skinc/write.h"
#include "../../src/skin/compile.h"
#include "../../src/skin/shipped.h"
#include "../../src/skin/viewint.h"
#include "../skinc/mediawrite.h"
#include "../skinc/util.h"
#include "../test.h"

#include <math.h>
#include <string.h>

/* ---- Skins and views ---- */

/* The skin at dir compiled and written as skinc writes it, then loaded. */
static sk_skinpack *pack_dir(const char *dir)
{
    sk_front f;
    sk_diags d = {0};
    sk_buf out = {0};
    unsigned char hash[32];
    char err[300];
    bool ok = sk_front_run(&f, dir, &d);
    ok = ok && sk_skin_hash(dir, hash, err, sizeof err) && sk_pack_write(&f, hash, &out, &d);
    for (size_t i = 0; i < d.items.len; i++)
        sk_test_fail(__FILE__, __LINE__, "%s", d.items.ptr[i]);
    sk_diags_free(&d);
    sk_front_free(&f);
    if (!ok) {
        sk_buf_free(&out);
        return NULL;
    }
    sk_skinpack *p = sk_skinpack_new((uint8_t *)out.ptr, out.len, true, err, sizeof err);
    if (!p)
        sk_test_fail(__FILE__, __LINE__, "%s", err);
    return p;
}

static sk_skinpack *pack_of(const char *panels, const char *widgets, const char *css)
{
    return pack_dir(sk_test_skin(panels, widgets, css));
}

/* Every view's clock of Unix seconds: a fixed moment. */
#define WALL 1700000000.0

static double fixed_wall(void *ud)
{
    (void)ud;
    return WALL;
}

static sk_skinview *view_of(sk_skinpack *p)
{
    if (!p)
        return NULL;
    sk_skinview *v = sk_skinview_new(p, 0);
    v->wall_fn = fixed_wall;
    v->wall = WALL;
    sk_skinpack_unref(p);
    return v;
}

/* Metrics built up for one set. */
typedef struct {
    sk_skin_metric m[32];
    size_t n;
} metrics;

static void num(metrics *t, const char *name, double x)
{
    t->m[t->n++] = (sk_skin_metric){name, SK_NUM(x)};
}

static void text(metrics *t, const char *name, const char *s)
{
    t->m[t->n++] = (sk_skin_metric){name, {SK_VAL_STR, 0, (char *)s}};
}

static void list(metrics *t, const char *name, const char *json)
{
    t->m[t->n++] = (sk_skin_metric){name, {SK_VAL_LIST, 0, (char *)json}};
}

static void none(metrics *t, const char *name)
{
    t->m[t->n++] = (sk_skin_metric){name, SK_NULLV};
}

/* The metrics given, then a step at the view's clock plus ms. */
static void give(sk_skinview *v, const metrics *t, double ms)
{
    sk_skinview_set_metrics(v, t->m, t->n);
    sk_skinview_step(v, v->now + ms);
}

/* The element of a widget (by id) with role, the nth of them. */
static uint32_t part(const sk_skinview *v, const char *widget, uint8_t role, int nth)
{
    for (uint32_t e = 0; v && e < v->p->nels; e++) {
        const sk_pack_el *el = &v->p->els[e];
        if (el->role != role || el->widget >= v->p->nwidgets || strcmp(v->p->widgets[el->widget].id, widget))
            continue;
        if (nth-- == 0)
            return e;
    }
    sk_test_fail(__FILE__, __LINE__, "no part %u of %s", (unsigned)role, widget);
    return 0;
}

/* A limits widget's meter part: group (the count), meter (from 1). */
static uint32_t meter_part(const sk_skinview *v, uint8_t group, uint8_t meter, uint8_t role)
{
    for (uint32_t e = 0; v && e < v->p->nels; e++) {
        const sk_pack_el *el = &v->p->els[e];
        if (el->role == role && el->group == group && el->meter == meter)
            return e;
    }
    sk_test_fail(__FILE__, __LINE__, "no meter part %u of %u/%u", (unsigned)role, group, meter);
    return 0;
}

static uint32_t panel(const sk_skinview *v, int nth)
{
    for (uint32_t e = 0; v && e < v->p->nels; e++)
        if (v->p->els[e].role == SK_ROLE_PANEL && nth-- == 0)
            return e;
    return 0;
}

static uint8_t var(const sk_skinview *v, uint32_t n, uint8_t which)
{
    return v ? v->nodes[n].var[which] : 0;
}

static const char *text_of(const sk_skinview *v, uint32_t n)
{
    return v && v->lnodes[n].text ? v->lnodes[n].text : "(none)";
}

static const sk_css_style *style_of(const sk_skinview *v, uint32_t n)
{
    return v ? v->lnodes[n].style : NULL;
}

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do {                                                                                                               \
        double a_ = (double)(a), b_ = (double)(b);                                                                     \
        if (!(fabs(a_ - b_) <= (eps)))                                                                                 \
            sk_test_fail(__FILE__, __LINE__, "%s == %s: %.6f != %.6f", #a, #b, a_, b_);                                \
    } while (0)

/* ---- Formats and the spring ---- */

static const char *fmt(double x, uint8_t format, uint8_t digits)
{
    static sk_buf b;
    sk_buf_clear(&b);
    sk_widget_format_num(&b, x, format, digits, 1000);
    return sk_buf_cstr(&b);
}

static void test_formats(void)
{
    CHECK_STR(fmt(0, SK_FORMAT_INT, 0), "0");
    CHECK_STR(fmt(-0.4, SK_FORMAT_INT, 0), "0");
    CHECK_STR(fmt(1234.5, SK_FORMAT_INT, 0), "1,235");
    CHECK_STR(fmt(1234567, SK_FORMAT_INT, 0), "1,234,567");
    CHECK_STR(fmt(-1500.5, SK_FORMAT_INT, 0), "-1,500");
    CHECK_STR(fmt(999.5, SK_FORMAT_INT, 0), "1,000");
    CHECK_STR(fmt(1e-12, SK_FORMAT_INT, 0), "0");
    CHECK_STR(fmt(1.005, SK_FORMAT_FIXED, 2), "1.00");
    CHECK_STR(fmt(2.5, SK_FORMAT_FIXED, 0), "3");
    CHECK_STR(fmt(-1.25, SK_FORMAT_FIXED, 1), "-1.3");
    CHECK_STR(fmt(42.25, SK_FORMAT_PCT, 0), "42.3%");
    CHECK_STR(fmt(1e-12, SK_FORMAT_PCT, 0), "0.0%");
    CHECK_STR(fmt(1.234, SK_FORMAT_USD, 0), "$1.23");
    CHECK_STR(fmt(12.34, SK_FORMAT_USD, 0), "$12.3");
    CHECK_STR(fmt(9.999, SK_FORMAT_USD, 0), "$10.00");
    CHECK_STR(fmt(999, SK_FORMAT_TOKENS, 0), "999");
    CHECK_STR(fmt(999.6, SK_FORMAT_TOKENS, 0), "1000");
    CHECK_STR(fmt(1500, SK_FORMAT_TOKENS, 0), "1.5k");
    CHECK_STR(fmt(123456, SK_FORMAT_TOKENS, 0), "123.5k");
    CHECK_STR(fmt(999999, SK_FORMAT_TOKENS, 0), "1000.0k");
    CHECK_STR(fmt(1234567, SK_FORMAT_TOKENS, 0), "1.23M");
    CHECK_STR(fmt(59, SK_FORMAT_DURATION, 0), "0:59");
    CHECK_STR(fmt(61.9, SK_FORMAT_DURATION, 0), "1:01");
    CHECK_STR(fmt(3661, SK_FORMAT_DURATION, 0), "1:01:01");
    CHECK_STR(fmt(-5, SK_FORMAT_DURATION, 0), "0:00");
    /* until counts from the clock given, 1000 s. */
    CHECK_STR(fmt(1000, SK_FORMAT_UNTIL, 0), "now");
    CHECK_STR(fmt(1030, SK_FORMAT_UNTIL, 0), "1m");
    CHECK_STR(fmt(1000 + 2 * 3600 + 5 * 60, SK_FORMAT_UNTIL, 0), "2h 5m");
    CHECK_STR(fmt(1000 + 3 * 86400 + 4 * 3600 + 59, SK_FORMAT_UNTIL, 0), "3d 4h");
    CHECK_STR(fmt(512, SK_FORMAT_BYTES, 0), "512 B");
    CHECK_STR(fmt(2048, SK_FORMAT_BYTES, 0), "2.0 KB");
    CHECK_STR(fmt(3 * 1048576.0, SK_FORMAT_BYTES, 0), "3.0 MB");
    CHECK_STR(fmt(3.5, SK_FORMAT_TEXT, 0), "3.5");
    CHECK_STR(fmt(INFINITY, SK_FORMAT_INT, 0), "--");
    CHECK_STR(fmt(NAN, SK_FORMAT_PCT, 0), "--");
    sk_buf b = {0};
    sk_val s = {SK_VAL_STR, 0, "Opus 5.5"};
    sk_widget_format(&b, &s, SK_FORMAT_PCT, 0, 0);
    CHECK_STR(sk_buf_cstr(&b), "Opus 5.5");
    sk_buf_clear(&b);
    sk_widget_format(&b, NULL, SK_FORMAT_PCT, 0, 0);
    CHECK_STR(sk_buf_cstr(&b), "--");
    sk_buf_free(&b);
}

static void test_spring(void)
{
    sk_spring s;
    double x;
    sk_spring_init(&s, 1, 3);
    CHECK(!sk_spring_step(&s, 0.016, &x));
    /* From nothing it jumps; then it eases, never past the target. */
    sk_spring_set(&s, true, 10);
    CHECK(sk_spring_step(&s, 0.016, &x));
    CHECK_NEAR(x, 10, 0);
    sk_spring_set(&s, true, 20);
    double last = 10;
    bool over = false;
    for (int i = 0; i < 120; i++) {
        sk_spring_step(&s, 1.0 / 60, &x);
        over |= x > 20 + 1e-9;
        CHECK(x >= last - 1e-9);
        last = x;
    }
    CHECK(!over);
    CHECK_NEAR(x, 20, 1e-3);
    /* At rest at last, held there. */
    for (int i = 0; i < 600; i++)
        sk_spring_step(&s, 1.0 / 60, &x);
    CHECK(sk_spring_rests(&s));
    CHECK_NEAR(x, 20, 0);
    /* Below critical damping it overshoots. */
    sk_spring_init(&s, 0.55, 2.5);
    sk_spring_set(&s, true, 0);
    sk_spring_set(&s, true, 100);
    double peak = 0;
    for (int i = 0; i < 120; i++) {
        sk_spring_step(&s, 1.0 / 60, &x);
        peak = fmax(peak, x);
    }
    CHECK(peak > 101);
    /* A slow frame counts as a tenth of a second at most. */
    sk_spring_init(&s, 1, 3);
    sk_spring_set(&s, true, 0);
    sk_spring_set(&s, true, 1);
    sk_spring_step(&s, 5, &x);
    sk_spring a;
    sk_spring_init(&a, 1, 3);
    sk_spring_set(&a, true, 0);
    sk_spring_set(&a, true, 1);
    double y;
    sk_spring_step(&a, 0.1, &y);
    CHECK_NEAR(x, y, 0);
    /* No target: no value. */
    sk_spring_set(&s, false, 0);
    CHECK(!sk_spring_step(&s, 0.016, &x));
}

/* ---- The widgets ---- */

static void test_gauge(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"g\", \"type\": \"gauge\", \"bind\": \"context.used_pct\", \"format\": \"pct\", \"max\": 100, "
        "\"marks\": [{\"bind\": \"context.compact_at_pct\"}, {\"value\": 50}], "
        "\"rect\": {\"left\": 0, \"top\": 0, \"width\": 100, \"height\": 100}}]",
        NULL));
    if (!v)
        return;
    metrics t = {0};
    give(v, &t, 16);
    uint32_t root = part(v, "g", SK_ROLE_WIDGET, 0), fill = part(v, "g", SK_ROLE_ARC_FILL, 0);
    uint32_t txt = part(v, "g", SK_ROLE_DIAL_TEXT, 0);
    uint32_t m0 = part(v, "g", SK_ROLE_MARK, 0), m1 = part(v, "g", SK_ROLE_MARK, 1);
    CHECK_INT(var(v, root, SK_VAR_NODATA), 1);
    CHECK_NEAR(v->lnodes[fill].value, 0, 0);
    CHECK_STR(text_of(v, txt), "--");
    /* The bound mark has no value: hidden; the fixed one at 50. */
    CHECK(style_of(v, m0) == NULL);
    CHECK(style_of(v, m1) != NULL);
    const sk_pack_svg *g = v->lnodes[m1].svg;
    /* r = 50 - 10/2 - 2 = 43, at 0 degrees: from 49 to 37 above the middle. */
    CHECK_NEAR(g->v[0], 50, 1e-4);
    CHECK_NEAR(g->v[1], 50 - 49, 1e-4);
    CHECK_NEAR(g->v[3], 50 - 37, 1e-4);
    /* A value from none shows at once. */
    num(&t, "context.used_pct", 40);
    num(&t, "context.compact_at_pct", 80);
    give(v, &t, 16);
    CHECK_INT(var(v, root, SK_VAR_NODATA), 0);
    CHECK_NEAR(v->lnodes[fill].value, 40, 0);
    CHECK_STR(text_of(v, txt), "40.0%");
    CHECK(style_of(v, m0) != NULL);
    double a = (-135 + 270 * 0.8) * M_PI / 180;
    CHECK_NEAR(v->lnodes[m0].svg->v[0], 50 + 49 * sin(a), 1e-3);
    CHECK_NEAR(v->lnodes[m0].svg->v[1], 50 - 49 * cos(a), 1e-3);
    /* Then it eases toward a new value. */
    t.m[0].value.num = 60;
    give(v, &t, 16);
    CHECK(v->lnodes[fill].value > 40 && v->lnodes[fill].value < 60);
    for (int i = 0; i < 200; i++)
        sk_skinview_step(v, v->now + 16);
    CHECK_NEAR(v->lnodes[fill].value, 60, 0);
    CHECK_STR(text_of(v, txt), "60.0%");
    /* At rest the view asks for no frame. */
    CHECK(sk_skinview_step(v, v->now + 16) < 0);
    sk_skinview_free(v);
}

static void test_needle_and_knob(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"n\", \"type\": \"needle\", \"bind\": \"tokens.out_rate\", \"max\": 150, \"resets\": "
        "\"limits.five_hour_resets_at\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 100, \"height\": 80}},\n"
        " {\"id\": \"k\", \"type\": \"knob\", \"bind\": \"proc.cpu_pct\", \"format\": \"pct\", "
        "\"rect\": {\"left\": 200, \"top\": 0, \"width\": 100, \"height\": 100}}]",
        NULL));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "tokens.out_rate", 75);
    num(&t, "proc.cpu_pct", 50);
    num(&t, "limits.five_hour_resets_at", WALL + 3600 + 120);
    give(v, &t, 16);
    uint32_t ng = part(v, "n", SK_ROLE_NEEDLE_GROUP, 0), kt = part(v, "k", SK_ROLE_KNOB_TURN, 0);
    CHECK_NEAR(v->lnodes[ng].turn, 0, 0);
    CHECK_STR(text_of(v, part(v, "n", SK_ROLE_VALUE, 0)), "75");
    CHECK_STR(text_of(v, part(v, "n", SK_ROLE_RESETS_LEFT, 0)), "1h 2m");
    CHECK_INT(var(v, part(v, "n", SK_ROLE_RESETS, 0), SK_VAR_NODATA), 0);
    CHECK_NEAR(v->lnodes[kt].turn, 0, 0);
    CHECK_STR(text_of(v, part(v, "k", SK_ROLE_VALUE, 0)), "50.0%");
    /* Past its max a needle stops 4% beyond; a knob at its end. */
    t.m[0].value.num = 400;
    t.m[1].value.num = 150;
    t.n = 2;
    give(v, &t, 16);
    for (int i = 0; i < 400; i++)
        sk_skinview_step(v, v->now + 16);
    CHECK_NEAR(v->lnodes[ng].turn, -60 + 120 * 1.04, 1e-3);
    CHECK_NEAR(v->lnodes[kt].turn, 135, 1e-3);
    CHECK_STR(text_of(v, part(v, "n", SK_ROLE_RESETS_LEFT, 0)), "--");
    CHECK_INT(var(v, part(v, "n", SK_ROLE_RESETS, 0), SK_VAR_NODATA), 1);
    sk_skinview_free(v);
}

static void test_bar(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"b\", \"type\": \"bar\", \"bind\": \"cache.hit_ratio\", \"max\": 1, \"orientation\": "
        "\"vertical\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 20, \"height\": 100}},\n"
        " {\"id\": \"s\", \"type\": \"bar\", \"bind\": \"context.used_pct\", \"segments\": 10, \"label\": \"ctx\", "
        "\"rect\": {\"left\": 30, \"top\": 0, \"width\": 200, \"height\": 20}},\n"
        " {\"id\": \"f\", \"type\": \"bar\", \"bind\": \"limits.five_hour_pct\", \"label\": \"5h\", "
        "\"resets\": \"limits.five_hour_resets_at\", "
        "\"fallback\": {\"bind\": \"limits.spend_pct\", \"label\": \"spend\", \"resets\": "
        "\"limits.spend_resets_at\"}, \"rect\": {\"left\": 30, \"top\": 40, \"width\": 200, \"height\": 20}}]",
        NULL));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "cache.hit_ratio", 0.25);
    num(&t, "context.used_pct", 33);
    num(&t, "limits.spend_pct", 70);
    num(&t, "limits.spend_resets_at", WALL + 2 * 86400 + 3 * 3600);
    give(v, &t, 16);
    CHECK_NEAR(v->lnodes[part(v, "b", SK_ROLE_FILL, 0)].value, 25, 0);
    /* The fill's height is its value's percentage of the track's. */
    const sk_lbox *track = &v->layout.boxes[part(v, "b", SK_ROLE_TRACK, 0)];
    const sk_lbox *fb = &v->layout.boxes[part(v, "b", SK_ROLE_FILL, 0)];
    CHECK_NEAR(fb->border.h, track->padding.h * 0.25, 0.01);
    for (int i = 0; i < 10; i++)
        CHECK_INT(var(v, part(v, "s", SK_ROLE_SEG, i), SK_VAR_ON), i < 3);
    /* The fallback in force: its label, its reset, its value. */
    uint32_t froot = part(v, "f", SK_ROLE_WIDGET, 0);
    CHECK_INT(var(v, froot, SK_VAR_FALLBACK), 1);
    CHECK_INT(var(v, froot, SK_VAR_NODATA), 0);
    CHECK_STR(text_of(v, part(v, "f", SK_ROLE_LABEL, 0)), "spend");
    CHECK_STR(text_of(v, part(v, "f", SK_ROLE_RESETS_LEFT, 0)), "2d 3h");
    CHECK_NEAR(v->lnodes[part(v, "f", SK_ROLE_FILL, 0)].value, 70, 0);
    /* Its own metric null is no data: the fallback stays. */
    none(&t, "limits.five_hour_pct");
    give(v, &t, 16);
    CHECK_INT(var(v, froot, SK_VAR_FALLBACK), 1);
    /* Its own metric back: its own label, no reset. */
    num(&t, "limits.five_hour_pct", 10);
    give(v, &t, 16);
    CHECK_INT(var(v, froot, SK_VAR_FALLBACK), 0);
    CHECK_STR(text_of(v, part(v, "f", SK_ROLE_LABEL, 0)), "5h");
    CHECK_STR(text_of(v, part(v, "f", SK_ROLE_RESETS_LEFT, 0)), "--");
    /* Nothing at all: nodata. */
    metrics empty = {0};
    give(v, &empty, 16);
    CHECK_INT(var(v, froot, SK_VAR_NODATA), 1);
    CHECK_INT(var(v, froot, SK_VAR_FALLBACK), 0);
    sk_skinview_free(v);
}

static void test_led(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"busy\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, \"label\": \"busy\", "
        "\"rect\": {\"left\": 0, \"top\": 0, \"width\": 60, \"height\": 20}},\n"
        " {\"id\": \"tool\", \"type\": \"led\", \"bind\": \"event.tool\", \"pulse\": true, \"pulseMs\": 300, "
        "\"rect\": {\"left\": 0, \"top\": 30, \"width\": 60, \"height\": 20}},\n"
        " {\"id\": \"blink\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, \"blink\": 500, "
        "\"rect\": {\"left\": 0, \"top\": 60, \"width\": 60, \"height\": 20}}]",
        NULL));
    if (!v)
        return;
    uint32_t busy = part(v, "busy", SK_ROLE_LAMP, 0), tool = part(v, "tool", SK_ROLE_LAMP, 0);
    uint32_t blink = part(v, "blink", SK_ROLE_LAMP, 0);
    metrics t = {0};
    give(v, &t, 16);
    CHECK_INT(var(v, part(v, "busy", SK_ROLE_WIDGET, 0), SK_VAR_NODATA), 1);
    CHECK_INT(var(v, busy, SK_VAR_ON), 0);
    num(&t, "state.busy", 1);
    num(&t, "event.tool", 4);
    give(v, &t, 16);
    CHECK_INT(var(v, busy, SK_VAR_ON), 1);
    /* An event counter's first reading is no pulse; a rise is. */
    CHECK_INT(var(v, tool, SK_VAR_ON), 0);
    t.m[1].value.num = 5;
    give(v, &t, 16);
    double at = v->now;
    CHECK_INT(var(v, tool, SK_VAR_ON), 1);
    /* The view asks to be stepped when the pulse ends. */
    double due = sk_skinview_step(v, at + 100);
    CHECK(due > 0 && due <= 200 + 1e-6);
    CHECK_INT(var(v, tool, SK_VAR_ON), 1);
    sk_skinview_step(v, at + 300);
    CHECK_INT(var(v, tool, SK_VAR_ON), 0);
    /* A blinking lamp is lit in the even halves of its period. */
    sk_skinview_step(v, 10250);
    CHECK_INT(var(v, blink, SK_VAR_ON), 1);
    sk_skinview_step(v, 10750);
    CHECK_INT(var(v, blink, SK_VAR_ON), 0);
    t.m[0].value.num = 0;
    give(v, &t, 16);
    CHECK_INT(var(v, busy, SK_VAR_ON), 0);
    CHECK_INT(var(v, blink, SK_VAR_ON), 0);
    sk_skinview_free(v);
}

static void test_readout(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"cost\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"format\": \"usd\", \"label\": "
        "\"cost\", \"prefix\": \"~\", \"suffix\": \" now\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 100, "
        "\"height\": 30}},\n"
        " {\"id\": \"model\", \"type\": \"readout\", \"bind\": \"model.name\", \"format\": \"text\", "
        "\"rect\": {\"left\": 0, \"top\": 40, \"width\": 100, \"height\": 30}},\n"
        " {\"id\": \"lim\", \"type\": \"readout\", \"bind\": \"limits.five_hour_pct\", \"format\": \"pct\", "
        "\"label\": \"5h\", \"fallback\": {\"bind\": \"limits.spend_pct\", \"label\": \"spend\"}, "
        "\"rect\": {\"left\": 0, \"top\": 80, \"width\": 100, \"height\": 30}}]",
        NULL));
    if (!v)
        return;
    metrics t = {0};
    give(v, &t, 16);
    uint32_t cost = part(v, "cost", SK_ROLE_VALUE, 0), model = part(v, "model", SK_ROLE_VALUE, 0);
    CHECK_STR(text_of(v, cost), "~-- now");
    CHECK_STR(text_of(v, model), "--");
    num(&t, "cost.usd", 1.234);
    text(&t, "model.name", "Opus 5.5");
    num(&t, "limits.spend_pct", 12.34);
    give(v, &t, 16);
    CHECK_STR(text_of(v, cost), "~$1.23 now");
    CHECK_STR(text_of(v, model), "Opus 5.5");
    CHECK_INT(var(v, part(v, "lim", SK_ROLE_WIDGET, 0), SK_VAR_FALLBACK), 1);
    CHECK_STR(text_of(v, part(v, "lim", SK_ROLE_LABEL, 0)), "spend");
    CHECK_STR(text_of(v, part(v, "lim", SK_ROLE_VALUE, 0)), "12.3%");
    /* A number readout is open while its dial is (decision 0033); a
     * text readout takes no click and is never open. */
    const char *dials[] = {"cost.usd", "model.name"};
    sk_skinview_set_dials(v, dials, 2);
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(var(v, part(v, "cost", SK_ROLE_WIDGET, 0), SK_VAR_OPEN), 1);
    CHECK_INT(var(v, part(v, "model", SK_ROLE_WIDGET, 0), SK_VAR_OPEN), 0);
    CHECK_INT(var(v, part(v, "lim", SK_ROLE_WIDGET, 0), SK_VAR_OPEN), 0);
    /* widgets.css: open outlined, click a pointer. */
    const sk_css_style *s = style_of(v, part(v, "cost", SK_ROLE_WIDGET, 0));
    CHECK(s && s->outline_style == SK_BORDER_SOLID && s->cursor == SK_CURSOR_POINTER);
    sk_skinview_set_dials(v, NULL, 0);
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(var(v, part(v, "cost", SK_ROLE_WIDGET, 0), SK_VAR_OPEN), 0);
    sk_skinview_free(v);
}

static void test_sprite(void)
{
    const char *dir = sk_test_skin(
        NULL,
        "[{\"id\": \"m\", \"type\": \"sprite\", \"bind\": \"agents.active\", \"max\": 4, \"sheet\": \"s.png\", "
        "\"frames\": 5, \"frameWidth\": 10, \"frameHeight\": 10, \"rect\": {\"left\": 0, \"top\": 0, \"width\": 10, "
        "\"height\": 10}},\n"
        " {\"id\": \"r\", \"type\": \"sprite\", \"bind\": \"pty.out_bps\", \"mode\": \"run\", \"fps\": 10, "
        "\"sheet\": \"r.png\", \"frames\": 4, \"frameWidth\": 10, \"frameHeight\": 10, \"rect\": {\"left\": 20, "
        "\"top\": 0, \"width\": 10, \"height\": 10}}]",
        NULL);
    /* Sheets of 5 and 4 frames, grey. */
    uint8_t rows[50 * 10];
    memset(rows, 128, sizeof rows);
    for (int w = 40; w <= 50; w += 10) {
        size_t np = 0;
        uint8_t *png = sk_test_png(w, 10, 0, 8, false, rows, (size_t)w, NULL, 0, NULL, 0, &np);
        sk_test_write_bytes(dir, w == 50 ? "s.png" : "r.png", png, np);
        free(png);
    }
    sk_skinview *v = view_of(pack_dir(dir));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "agents.active", 2);
    num(&t, "pty.out_bps", 0);
    /* A page's clock is long past its frames' first tenth of a second. */
    give(v, &t, 10000);
    uint32_t m = part(v, "m", SK_ROLE_SHEET, 0), r = part(v, "r", SK_ROLE_SHEET, 0);
    /* Frame 2 of 0 to 4: half way along the sheet. */
    CHECK_NEAR(v->lnodes[m].value, 50, 1e-4);
    CHECK_NEAR(v->lnodes[r].value, 0, 0);
    /* Running: a frame every tenth of a second while the metric is above 0. */
    t.m[1].value.num = 100;
    give(v, &t, 16);
    double at = v->now;
    CHECK_NEAR(v->lnodes[r].value, 100.0 / 3, 1e-4);
    sk_skinview_step(v, at + 50);
    CHECK_NEAR(v->lnodes[r].value, 100.0 / 3, 1e-4);
    sk_skinview_step(v, at + 101);
    CHECK_NEAR(v->lnodes[r].value, 200.0 / 3, 1e-4);
    t.m[1].value.num = 0;
    give(v, &t, 500);
    CHECK_NEAR(v->lnodes[r].value, 200.0 / 3, 1e-4);
    sk_skinview_free(v);
}

static const char *const LIST_WIDGETS =
    "[{\"id\": \"todo\", \"type\": \"list\", \"bind\": \"checklist.items\", \"rows\": 3, "
    "\"rect\": {\"left\": 0, \"top\": 0, \"width\": 300, \"height\": 100},\n"
    "  \"toggle\": {\"rect\": {\"left\": 0, \"top\": 110, \"width\": 100, \"height\": 20}, \"labels\": [\"tasks\", "
    "\"stats\"]}},\n"
    " {\"id\": \"r\", \"type\": \"readout\", \"bind\": \"tools.total\", \"rect\": {\"left\": 0, \"top\": 0, "
    "\"width\": 300, \"height\": 30}}]";

static void test_list(void)
{
    sk_skinview *v = view_of(pack_of(NULL, LIST_WIDGETS,
                                     ".skin.checklist .w-readout { display: none }\n"
                                     ".item.done .text { color: #0f0 }\n"));
    if (!v)
        return;
    metrics t = {0};
    give(v, &t, 16);
    uint32_t root = part(v, "todo", SK_ROLE_WIDGET, 0), row = part(v, "todo", SK_ROLE_ITEM, 0);
    uint32_t readout = part(v, "r", SK_ROLE_WIDGET, 0);
    CHECK_INT(var(v, root, SK_VAR_NODATA), 1);
    CHECK_INT(var(v, v->stage, SK_VAR_LIST), 0);
    CHECK(!v->layout.boxes[root].generated);
    CHECK(v->layout.boxes[readout].generated);
    list(&t, "checklist.items",
         "[{\"text\": \"one\", \"state\": \"done\"}, {\"text\": \"two\", \"state\": \"doing\"}, "
         "{\"text\": \"three\", \"state\": \"todo\"}, {\"text\": \"four\"}]");
    give(v, &t, 16);
    CHECK_INT(var(v, root, SK_VAR_NODATA), 0);
    CHECK_INT(var(v, v->stage, SK_VAR_LIST), 1);
    CHECK(v->layout.boxes[root].generated);
    CHECK(!v->layout.boxes[readout].generated);
    /* rows 3: three rows of four items, each its mark, text and state. */
    const sk_vlist *L = &v->lists[0];
    CHECK_INT(L->rows, 3);
    static const char *const want[3][2] = {{"[x]", "one"}, {"[>]", "two"}, {"[ ]", "three"}};
    float last_y = -1;
    for (uint16_t r = 0; r < 3; r++) {
        uint32_t base = r == 0 ? row : L->base + (uint32_t)(r - 1) * L->nsub;
        CHECK_INT(var(v, base, SK_VAR_ITEM), r == 0 ? 2 : r == 1 ? 1 : 0);
        for (uint32_t k = 0; k < L->nsub; k++) {
            uint32_t n = r == 0 ? L->sub[k] : L->base + (uint32_t)(r - 1) * L->nsub + k;
            if (v->p->els[L->sub[k]].role == SK_ROLE_ITEM_MARK)
                CHECK_STR(text_of(v, n), want[r][0]);
            if (v->p->els[L->sub[k]].role == SK_ROLE_ITEM_TEXT) {
                CHECK_STR(text_of(v, n), want[r][1]);
                /* A done row's text in its own colour: its row's state. */
                CHECK(style_of(v, n) && (style_of(v, n)->color.g == 1) == (r == 0));
            }
        }
        /* The rows stack down the list. */
        CHECK(v->layout.boxes[base].generated && v->layout.boxes[base].border.y > last_y);
        last_y = v->layout.boxes[base].border.y;
    }
    /* The toggle: shown with the list, the list's option on. */
    uint32_t toggle = part(v, "todo", SK_ROLE_TOGGLE, 0);
    CHECK(v->layout.boxes[toggle].generated);
    CHECK_INT(var(v, part(v, "todo", SK_ROLE_OPTION_LIST, 0), SK_VAR_ON), 1);
    CHECK_INT(var(v, part(v, "todo", SK_ROLE_OPTION_OTHER, 0), SK_VAR_ON), 0);
    /* The tab shows what the list replaces: checklist-off, the list
     * hidden, the readout back, the other option on. */
    sk_skinview_set_list_off(v, true);
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(var(v, v->stage, SK_VAR_LIST), 2);
    CHECK(!v->layout.boxes[root].generated);
    CHECK(v->layout.boxes[readout].generated);
    CHECK_INT(var(v, part(v, "todo", SK_ROLE_OPTION_OTHER, 0), SK_VAR_ON), 1);
    /* Cleared: no list, the toggle hidden. */
    list(&t, "checklist.items", "[]");
    give(v, &t, 16);
    CHECK_INT(var(v, v->stage, SK_VAR_LIST), 0);
    CHECK(!v->layout.boxes[toggle].generated);
    CHECK_INT(v->lists[0].rows, 0);
    sk_skinview_free(v);
}

static void test_limits(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"l\", \"type\": \"limits\", \"bind\": \"limits.items\", \"meter\": \"bar\", \"suffix\": \" use\", "
        "\"rect\": {\"left\": 0, \"top\": 0, \"width\": 300, \"height\": 100}}]",
        ".span-5h .fill { background: #f00 }\n.l.n-3 { gap: 9px }\n"));
    if (!v)
        return;
    metrics t = {0};
    give(v, &t, 16);
    uint32_t root = part(v, "l", SK_ROLE_WIDGET, 0);
    CHECK_INT(var(v, root, SK_VAR_COUNT), 0);
    CHECK(!v->layout.boxes[root].generated);
    /* An item whose reset passed, or with no reading, is no meter. */
    list(&t, "limits.items",
         "[{\"key\": \"a\", \"label\": \"5h\", \"window\": \"5h\", \"model\": null, \"pct\": 30, \"resets_at\": "
         "1700003600},"
         "{\"key\": \"gone\", \"label\": \"old\", \"window\": \"5h\", \"pct\": 30, \"resets_at\": 1699999999},"
         "{\"key\": \"none\", \"label\": \"x\", \"window\": \"5h\", \"pct\": null},"
         "{\"key\": \"b\", \"label\": \"Spark 7d\", \"window\": \"7d\", \"model\": \"spark\", \"pct\": 60},"
         "{\"key\": \"c\", \"label\": \"spend\", \"window\": null, \"model\": null, \"pct\": 90}]");
    give(v, &t, 16);
    CHECK_INT(var(v, root, SK_VAR_COUNT), 3);
    CHECK(v->layout.boxes[root].generated);
    /* The meters of count 3 shown, those of 2 not. */
    uint32_t m1 = meter_part(v, 3, 1, SK_ROLE_METER), m2 = meter_part(v, 3, 2, SK_ROLE_METER);
    uint32_t m3 = meter_part(v, 3, 3, SK_ROLE_METER);
    CHECK(v->layout.boxes[m1].generated && v->layout.boxes[m3].generated);
    CHECK(!v->layout.boxes[meter_part(v, 2, 1, SK_ROLE_METER)].generated);
    CHECK_STR(text_of(v, meter_part(v, 3, 1, SK_ROLE_LABEL)), "5h");
    CHECK_STR(text_of(v, meter_part(v, 3, 2, SK_ROLE_LABEL)), "Spark 7d");
    CHECK_STR(text_of(v, meter_part(v, 3, 1, SK_ROLE_SUFFIX)), " use");
    CHECK_STR(text_of(v, meter_part(v, 3, 1, SK_ROLE_RESETS_LEFT)), "1h 0m");
    CHECK_STR(text_of(v, meter_part(v, 3, 2, SK_ROLE_RESETS_LEFT)), "--");
    CHECK_INT(var(v, m1, SK_VAR_MODEL), 0);
    CHECK_INT(var(v, m2, SK_VAR_MODEL), 1);
    /* span-5h is a class the sheet names; 7d is not: any other. */
    CHECK_INT(var(v, m1, SK_VAR_SPAN), 1);
    CHECK_INT(var(v, m2, SK_VAR_SPAN), (int)v->p->nspans + 1);
    CHECK_INT(var(v, m3, SK_VAR_SPAN), 0);
    CHECK_NEAR(v->lnodes[meter_part(v, 3, 3, SK_ROLE_FILL)].value, 90, 0);
    CHECK(style_of(v, meter_part(v, 3, 1, SK_ROLE_FILL))->bg_color.r == 1);
    /* The meters share the rect, side by side down the column. */
    CHECK(v->layout.boxes[m2].border.y > v->layout.boxes[m1].border.y);
    /* A new reading eases; new items make new meters. */
    list(&t, "limits.items",
         "[{\"key\": \"a\", \"label\": \"5h\", \"window\": \"5h\", \"pct\": 40}]");
    give(v, &t, 16);
    CHECK_INT(var(v, root, SK_VAR_COUNT), 1);
    CHECK_NEAR(v->lnodes[meter_part(v, 1, 1, SK_ROLE_FILL)].value, 40, 0);
    /* Past eight limits, the first eight. */
    sk_buf many = {0};
    sk_buf_putc(&many, '[');
    for (int i = 0; i < 10; i++)
        sk_buf_printf(&many, "%s{\"key\": \"k%d\", \"label\": \"L%d\", \"window\": \"5h\", \"pct\": %d}", i ? "," : "",
                      i, i, i * 10);
    sk_buf_putc(&many, ']');
    list(&t, "limits.items", sk_buf_cstr(&many));
    give(v, &t, 16);
    CHECK_INT(var(v, root, SK_VAR_COUNT), 8);
    CHECK_STR(text_of(v, meter_part(v, 8, 8, SK_ROLE_LABEL)), "L7");
    sk_buf_free(&many);
    sk_skinview_free(v);
}

/* ---- Motion ---- */

static void test_timing(void)
{
    sk_css_timing linear = {SK_TIMING_LINEAR, 0, 0, 0, 0, 0, 0};
    sk_css_timing ease = {SK_TIMING_CUBIC, 0, 0, .25f, .1f, .25f, 1};
    sk_css_timing steps = {SK_TIMING_STEPS, SK_STEP_JUMP_END, 4, 0, 0, 0, 0};
    sk_css_timing start = {SK_TIMING_STEPS, SK_STEP_JUMP_START, 4, 0, 0, 0, 0};
    CHECK_NEAR(sk_timing_eval(&linear, 0.3), 0.3, 1e-9);
    CHECK_NEAR(sk_timing_eval(&ease, 0.5), 0.8024, 1e-3);
    CHECK_NEAR(sk_timing_eval(&ease, 0), 0, 1e-9);
    CHECK_NEAR(sk_timing_eval(&ease, 1), 1, 1e-9);
    CHECK_NEAR(sk_timing_eval(&steps, 0.3), 0.25, 1e-9);
    CHECK_NEAR(sk_timing_eval(&steps, 1), 1, 1e-9);
    CHECK_NEAR(sk_timing_eval(&start, 0.3), 0.5, 1e-9);
}

static void test_transition(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"busy\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, \"label\": \"busy\", "
        "\"rect\": {\"left\": 0, \"top\": 0, \"width\": 60, \"height\": 20}}]",
        ".lamp { opacity: 0.2; transition: opacity 1s linear }\n.lamp.on { opacity: 1 }\n"
        ".label { color: #000; transition: color 1s linear, text-shadow 1s linear }\n"
        ".lamp.on + .label { color: #fff; text-shadow: 0 0 4px #f00 }\n"));
    if (!v)
        return;
    uint32_t lamp = part(v, "busy", SK_ROLE_LAMP, 0), label = part(v, "busy", SK_ROLE_LABEL, 0);
    metrics t = {0};
    num(&t, "state.busy", 0);
    give(v, &t, 16);
    CHECK_NEAR(style_of(v, lamp)->opacity, 0.2, 1e-6);
    t.m[0].value.num = 1;
    give(v, &t, 16);
    double at = v->now;
    CHECK_NEAR(style_of(v, lamp)->opacity, 0.2, 1e-6);
    CHECK(sk_skinview_step(v, at + 250) == 0);
    CHECK_NEAR(style_of(v, lamp)->opacity, 0.4, 1e-4);
    /* The label's colour eases premultiplied, its glow from none. */
    CHECK_NEAR(style_of(v, label)->color.r, 0.25, 1e-4);
    CHECK_INT(style_of(v, label)->ntext_shadow, 1);
    CHECK_NEAR(style_of(v, label)->text_shadow[0].color.a, 0.25, 1e-4);
    CHECK_NEAR(style_of(v, label)->text_shadow[0].color.r, 1, 1e-4);
    CHECK_NEAR(style_of(v, label)->text_shadow[0].blur, 1, 1e-4);
    /* Turned back half way: from where it was, the whole second again. */
    t.m[0].value.num = 0;
    give(v, &t, 250);
    at = v->now;
    CHECK_NEAR(style_of(v, lamp)->opacity, 0.6, 1e-4);
    sk_skinview_step(v, at + 500);
    CHECK_NEAR(style_of(v, lamp)->opacity, 0.4, 1e-4);
    sk_skinview_step(v, at + 1000);
    CHECK_NEAR(style_of(v, lamp)->opacity, 0.2, 1e-6);
    CHECK_INT(style_of(v, label)->ntext_shadow, 0);
    /* Done: at rest. */
    CHECK(sk_skinview_step(v, at + 1100) < 0);
    sk_skinview_free(v);
}

static void test_animation(void)
{
    sk_skinview *v = view_of(pack_of(
        "[{\"class\": \"a\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 40, \"height\": 40}},"
        " {\"class\": \"b\", \"rect\": {\"left\": 50, \"top\": 0, \"width\": 40, \"height\": 40}}]",
        "[{\"id\": \"busy\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, "
        "\"rect\": {\"left\": 100, \"top\": 0, \"width\": 60, \"height\": 20}}]",
        "@keyframes spin { to { transform: rotate(360deg) } }\n"
        "@keyframes blink { 0% { opacity: 0.2 } 50% { opacity: 1 } 100% { opacity: 0.2 } }\n"
        "@keyframes bob { 0% { transform: translateY(0) scale(1, 1) } 50% { transform: translateY(-10px) "
        "scale(2, 1) } }\n"
        ".a { animation: spin 2s linear infinite }\n"
        ".b { animation: blink 4s steps(4) infinite, bob 1s linear 1 forwards }\n"
        ".lamp { animation: spin 1s linear infinite; animation-play-state: paused }\n"
        ".lamp.on { animation-play-state: running }\n"));
    if (!v)
        return;
    uint32_t a = panel(v, 0), b = panel(v, 1), lamp = part(v, "busy", SK_ROLE_LAMP, 0);
    metrics t = {0};
    num(&t, "state.busy", 0);
    give(v, &t, 0);
    CHECK_NEAR(style_of(v, a)->transform[0].v[0], 0, 1e-4);
    sk_skinview_step(v, 500);
    CHECK_INT(style_of(v, a)->ntransform, 1);
    CHECK_NEAR(style_of(v, a)->transform[0].v[0], 90, 1e-3);
    /* steps(4) on each half: 0.2 to 1 in quarters. */
    CHECK_NEAR(style_of(v, b)->opacity, 0.2 + 0.8 * 0.25, 1e-5);
    /* bob: at half of its stretch to 50%, then held at its end (the
     * element's own transform, none: identity) after its one run. */
    CHECK_INT(style_of(v, b)->ntransform, 2);
    CHECK_NEAR(style_of(v, b)->transform[0].ty.px, -10, 1e-4);
    CHECK_NEAR(style_of(v, b)->transform[1].v[0], 2, 1e-4);
    sk_skinview_step(v, 750);
    CHECK_NEAR(style_of(v, b)->transform[0].ty.px, -5, 1e-4);
    sk_skinview_step(v, 1500);
    CHECK_NEAR(style_of(v, b)->transform[0].ty.px, 0, 1e-4);
    CHECK_NEAR(style_of(v, b)->transform[1].v[0], 1, 1e-4);
    CHECK_NEAR(style_of(v, a)->transform[0].v[0], 270, 1e-3);
    /* The turned panel: its box's transform turns about its middle. */
    const sk_lbox *box = &v->layout.boxes[a];
    CHECK(box->has_xform);
    CHECK_NEAR(box->local.a, 0, 1e-4);
    /* Paused, the lamp's clock holds; running, it runs. */
    CHECK_NEAR(style_of(v, lamp)->transform[0].v[0], 0, 1e-4);
    t.m[0].value.num = 1;
    give(v, &t, 0);
    sk_skinview_step(v, 1750);
    CHECK_NEAR(style_of(v, lamp)->transform[0].v[0], 90, 1e-3);
    t.m[0].value.num = 0;
    give(v, &t, 0);
    sk_skinview_step(v, 2500);
    CHECK_NEAR(style_of(v, lamp)->transform[0].v[0], 90, 1e-3);
    sk_skinview_free(v);
}

/* display none stops an animation; shown again it starts over. */
static void test_animation_hidden(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"busy\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, "
        "\"rect\": {\"left\": 100, \"top\": 0, \"width\": 60, \"height\": 20}}]",
        "@keyframes spin { to { transform: rotate(360deg) } }\n"
        ".lamp { animation: spin 1s linear infinite }\n.lamp.on { display: none }\n"));
    if (!v)
        return;
    uint32_t lamp = part(v, "busy", SK_ROLE_LAMP, 0);
    metrics t = {0};
    num(&t, "state.busy", 0);
    give(v, &t, 0);
    sk_skinview_step(v, 250);
    CHECK_NEAR(style_of(v, lamp)->transform[0].v[0], 90, 1e-3);
    t.m[0].value.num = 1;
    give(v, &t, 100);
    CHECK(!v->layout.boxes[lamp].generated);
    CHECK(v->nodes[lamp].mo == NULL);
    t.m[0].value.num = 0;
    sk_skinview_set_metrics(v, t.m, t.n);
    sk_skinview_step(v, 600);
    sk_skinview_step(v, 850);
    CHECK_NEAR(style_of(v, lamp)->transform[0].v[0], 90, 1e-3);
    sk_skinview_free(v);
}

/* ---- The pointer ---- */

static void test_hover_and_clicks(void)
{
    sk_skinview *v = view_of(pack_of(NULL, LIST_WIDGETS, ".w-readout { top: 200px !important }\n"));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "tools.total", 3);
    list(&t, "checklist.items", "[{\"text\": \"one\", \"state\": \"todo\"}]");
    give(v, &t, 16);
    /* The stage starts at y 22 in the window. */
    uint32_t r = part(v, "r", SK_ROLE_WIDGET, 0);
    sk_skin_event ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 10, 22 + 210, 0);
    CHECK_INT(ev.action, SK_SKIN_NOTHING);
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(var(v, r, SK_VAR_HOVER), 1);
    CHECK_INT(var(v, v->stage, SK_VAR_HOVER), 1);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_POINTER);
    /* widgets.css: a readout that takes a click brightens under the pointer. */
    CHECK(style_of(v, r)->nfilter == 1 && style_of(v, r)->filter[0].kind == SK_FX_BRIGHTNESS);
    /* A click: down and up on it toggles its dial. */
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, 10, 22 + 210, 1);
    CHECK_INT(ev.action, SK_SKIN_TAKEN);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, 12, 22 + 212, 1);
    CHECK_INT(ev.action, SK_SKIN_DIAL);
    CHECK_STR(ev.metric, "tools.total");
    /* Down on it, up elsewhere: no click. */
    sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, 10, 22 + 210, 1);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, 900, 22 + 100, 1);
    CHECK_INT(ev.action, SK_SKIN_TAKEN);
    /* The toggle's second option: the readouts. */
    sk_skinview_step(v, v->now + 16);
    uint32_t other = part(v, "todo", SK_ROLE_OPTION_OTHER, 0);
    const sk_lbox *ob = &v->layout.boxes[other];
    double ox = ob->border.x + ob->border.w / 2, oy = 22 + ob->border.y + ob->border.h / 2;
    sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, ox, oy, 1);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, ox, oy, 1);
    CHECK_INT(ev.action, SK_SKIN_TOGGLE);
    CHECK(ev.off);
    /* The terminal is the window's: nothing there. */
    CHECK(!sk_skinview_covers(v, 500, 22 + 500));
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, 500, 22 + 500, 1);
    CHECK_INT(ev.action, SK_SKIN_NOTHING);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_NONE);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, 500, 22 + 500, 1);
    /* The menu button, at the top right: pressed, it opens the menu. */
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 1000 - 8 - 12, 22 + 8 + 12, 0);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_POINTER);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, 1000 - 8 - 12, 22 + 8 + 12, 1);
    CHECK_INT(ev.action, SK_SKIN_MENU);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, 1000 - 8 - 12, 22 + 8 + 12, 1);
    CHECK_INT(ev.action, SK_SKIN_TAKEN);
    double mr[4];
    sk_skinview_menu_rect(v, mr);
    CHECK_NEAR(mr[0], 1000 - 8 - 24, 1e-9);
    CHECK_NEAR(mr[1], 22 + 8, 1e-9);
    CHECK_NEAR(mr[2], 24, 1e-9);
    /* Leaving: nothing under the pointer. */
    sk_skinview_mouse(v, SK_SKIN_MOUSE_LEAVE, 0, 0, 0);
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(var(v, v->stage, SK_VAR_HOVER), 0);
    sk_skinview_free(v);
}

/* A pointer that stays still while what is under it goes: hover
 * follows the layout. */
static void test_hover_follows_layout(void)
{
    sk_skinview *v = view_of(pack_of(NULL, LIST_WIDGETS,
                                     ".w-readout { top: 200px !important }\n"
                                     ".skin.checklist .w-readout { display: none }\n"));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "tools.total", 3);
    give(v, &t, 16);
    uint32_t r = part(v, "r", SK_ROLE_WIDGET, 0);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 10, 22 + 210, 0);
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(var(v, r, SK_VAR_HOVER), 1);
    list(&t, "checklist.items", "[{\"text\": \"one\", \"state\": \"todo\"}]");
    give(v, &t, 16);
    CHECK(!v->layout.boxes[r].generated);
    CHECK_INT(var(v, r, SK_VAR_HOVER), 0);
    CHECK_INT(var(v, v->stage, SK_VAR_HOVER), 1);
    /* The next step restyles what hover moved; then the view rests. */
    sk_skinview_step(v, v->now + 16);
    CHECK(sk_skinview_step(v, v->now + 16) < 0);
    sk_skinview_free(v);
}

/* A skin with a split on the terminal's right edge, from min to max,
 * and a panel that stretches with it. */
static sk_skinpack *split_pack_of(int min, int max)
{
    const char *dir = sk_test_skin(NULL, NULL, NULL);
    sk_buf b = {0};
    sk_buf_printf(&b,
                  "{\"name\": \"split\", \"window\": {\"width\": 1000, \"height\": 700, \"minWidth\": 800, "
                  "\"minHeight\": 500},\n"
                  " \"menu\": {\"rect\": {\"left\": 8, \"top\": 8, \"width\": 24, \"height\": 24}, \"image\": "
                  "\"menu.svg\"},\n"
                  " \"terminal\": {\"rect\": {\"left\": 10, \"top\": 40, \"right\": 300, \"bottom\": 10},\n"
                  "   \"theme\": {\"foreground\": \"#e0e0e0\", \"background\": \"#000000\"}},\n"
                  " \"splits\": [{\"id\": \"side\", \"edge\": \"right\", \"min\": %d, \"max\": %d}],\n"
                  " \"stylesheet\": \"skin.css\",\n"
                  " \"panels\": [{\"class\": \"side\", \"follow\": {\"width\": \"side\"}, "
                  "\"rect\": {\"right\": 10, \"top\": 40, \"width\": 280, \"bottom\": 10}}]}\n",
                  min, max);
    sk_test_write(dir, "skin.json", sk_buf_cstr(&b));
    sk_buf_free(&b);
    return pack_dir(dir);
}

static sk_skinpack *split_pack(void)
{
    return split_pack_of(250, 400);
}

static void test_splits(void)
{
    sk_skinview *v = view_of(split_pack());
    if (!v)
        return;
    sk_skinview_step(v, 16);
    uint32_t side = panel(v, 0);
    double tr[4];
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[0], 10, 1e-9);
    CHECK_NEAR(tr[1], 22 + 40, 1e-9);
    CHECK_NEAR(tr[2], 1000 - 10 - 300, 1e-9);
    CHECK_NEAR(v->layout.boxes[side].border.w, 280, 1e-4);
    /* The handle: 8 points just right of the terminal. Dragged 50
     * points left, the split grows by 50, the panel with it. */
    double hx = 1000 - 300 + 4, hy = 22 + 300;
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, hx, hy, 0);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_RESIZE_EW);
    sk_skin_event ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, hx, hy, 1);
    CHECK_INT(ev.action, SK_SKIN_TAKEN);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, hx - 50, hy + 30, 0);
    CHECK_INT(ev.action, SK_SKIN_SPLIT_MOVE);
    CHECK_STR(ev.split, "side");
    sk_skinview_step(v, 32);
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[2], 1000 - 10 - 350, 1e-9);
    CHECK_NEAR(v->layout.boxes[side].border.w, 330, 1e-4);
    /* Held within its range. */
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, hx - 500, hy, 0);
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[2], 1000 - 10 - 400, 1e-9);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, hx - 60.4, hy, 1);
    CHECK_INT(ev.action, SK_SKIN_SPLIT_DONE);
    CHECK_STR(ev.split, "side");
    CHECK_NEAR(ev.size, 360, 1e-9);
    /* The kept sizes, as settings give them: clamped to the range. */
    const char *ids[] = {"side", "other"};
    double sizes[] = {100, 5};
    sk_skinview_set_panels(v, ids, sizes, 2);
    sk_skinview_step(v, 48);
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[2], 1000 - 10 - 250, 1e-9);
    /* A narrower window gives back the growth: at 900 wide the split
     * grows to 300 at most, and at 800 it can move no further than its
     * default. */
    sizes[0] = 400;
    sk_skinview_set_panels(v, ids, sizes, 1);
    sk_skinview_set_stage(v, 0, 22, 900, 678, 1);
    sk_skinview_step(v, 64);
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[2], 900 - 10 - 300, 1e-9);
    sk_skinview_set_stage(v, 0, 22, 800, 678, 1);
    sk_skinview_step(v, 80);
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[2], 800 - 10 - 300, 1e-9);
    sk_skinview_set_panels(v, NULL, NULL, 0);
    sk_skinview_step(v, 96);
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[2], 800 - 10 - 300, 1e-9);
    sk_skinview_free(v);
}

static void test_scale(void)
{
    sk_skinview *v = view_of(pack_of(
        "[{\"class\": \"p\", \"rect\": {\"left\": 0, \"right\": 0, \"top\": 0, \"height\": 100}}]", NULL, NULL));
    if (!v)
        return;
    /* 600 wide of a minimum 800, 478 high of 500 - 22: k = 0.75, laid
     * out at 800 by 637.33. */
    sk_skinview_set_stage(v, 0, 22, 600, 478, 2);
    sk_skinview_step(v, 16);
    CHECK_NEAR(sk_skinview_scale(v), 0.75, 1e-12);
    CHECK_NEAR(v->lw, 800, 1e-3);
    CHECK_NEAR(v->lh, 478 / 0.75, 1e-3);
    CHECK_NEAR(v->layout.boxes[panel(v, 0)].border.w, 800, 1e-3);
    /* The terminal and the menu button are scaled, outside the skin. */
    double tr[4], mr[4];
    sk_skinview_terminal_rect(v, tr);
    CHECK_NEAR(tr[0], 10 * 0.75, 1e-9);
    CHECK_NEAR(tr[1], 22 + 300 * 0.75, 1e-9);
    CHECK_NEAR(tr[2], 600 - 20 * 0.75, 1e-9);
    sk_skinview_menu_rect(v, mr);
    CHECK_NEAR(mr[2], 24 * 0.75, 1e-9);
    CHECK_NEAR(mr[0], 600 - (8 + 24) * 0.75, 1e-9);
    sk_rect px = sk_skinview_terminal_px(v);
    CHECK_INT(px.x, 15);
    CHECK_INT(px.y, 2 * 22 + 450);
    /* Taller than the minimum but narrow: the width decides. */
    sk_skinview_set_stage(v, 0, 22, 400, 900, 1);
    CHECK_NEAR(sk_skinview_scale(v), 0.5, 1e-12);
    sk_skinview_set_stage(v, 0, 22, 1200, 800, 1);
    CHECK_NEAR(sk_skinview_scale(v), 1, 1e-12);
    /* scale.js's floor: 40 columns of 14 px in 780 of 800, 10 rows in
     * 168 of 478. */
    sk_skin_window w;
    sk_skinpack_window(sk_skinview_pack(v), &w);
    CHECK_INT(w.width, 1000);
    CHECK_INT(w.height, 700);
    CHECK_INT(w.min_width, 400);
    CHECK_INT(w.min_height, 22 + 478);
    sk_skinview_free(v);
}

/* ---- Paint ---- */

static void test_damage(void)
{
    sk_skinview *v = view_of(pack_of(
        "[{\"class\": \"bg\", \"rect\": {\"left\": 0, \"right\": 0, \"top\": 0, \"bottom\": 0}}]",
        "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"tools.total\", \"rect\": {\"left\": 100, \"top\": 100, "
        "\"width\": 80, \"height\": 30}}]",
        ".bg { background: #123 }\n"));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "tools.total", 1);
    give(v, &t, 16);
    sk_image f;
    sk_image_init(&f, 1000, 700);
    sk_rect out[16];
    size_t n = sk_skinview_draw(v, &f, false, out, 16);
    /* The first draw is whole: around the terminal, four pieces. */
    CHECK_INT(n, 4);
    sk_rect term = sk_skinview_terminal_px(v);
    for (size_t i = 0; i < n; i++)
        CHECK(sk_rect_empty(sk_rect_intersect(out[i], term)));
    /* The skin's colour around it, the terminal left as it was. */
    CHECK_INT(sk_image_row(&f, 22 + 10)[10], sk_premul(SK_RGB(0x11, 0x22, 0x33)));
    CHECK_INT(sk_image_row(&f, term.y + 5)[term.x + 5], 0);
    /* Nothing changed: nothing to paint. */
    sk_skinview_step(v, v->now + 16);
    CHECK_INT(sk_skinview_draw(v, &f, false, out, 16), 0);
    /* A new value: the readout's box alone. */
    t.m[0].value.num = 22;
    give(v, &t, 16);
    for (int i = 0; i < 200; i++)
        sk_skinview_step(v, v->now + 16);
    n = sk_skinview_draw(v, &f, false, out, 16);
    CHECK_INT(n, 1);
    if (n) {
        CHECK(out[0].x >= 85 && out[0].x + out[0].w <= 190);
        CHECK(out[0].y >= 22 + 85 && out[0].y + out[0].h <= 22 + 145);
    }
    sk_image_free(&f);
    sk_skinview_free(v);
}

/* A change under a group's drop shadow paints the shadow's reach too. */
static void test_damage_under_filter(void)
{
    sk_skinview *v = view_of(pack_of(
        NULL,
        "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"tools.total\", \"rect\": {\"left\": 100, \"top\": 100, "
        "\"width\": 80, \"height\": 30}}]",
        ".w-readout { filter: drop-shadow(40px 0 0 #f00) }\n"));
    if (!v)
        return;
    metrics t = {0};
    num(&t, "tools.total", 1);
    give(v, &t, 16);
    sk_image f;
    sk_image_init(&f, 1000, 700);
    sk_rect out[16];
    sk_skinview_draw(v, &f, false, out, 16);
    t.m[0].value.num = 22;
    give(v, &t, 16);
    for (int i = 0; i < 200; i++)
        sk_skinview_step(v, v->now + 16);
    size_t n = sk_skinview_draw(v, &f, false, out, 16);
    CHECK_INT(n, 1);
    /* The text's ink, and 40 px right of it its shadow's. */
    if (n)
        CHECK(out[0].x + out[0].w >= 100 + 40 + 20);
    sk_image_free(&f);
    sk_skinview_free(v);
}

/* A group whose opacity alone flickers above what it covers keeps that
 * below it, and its flicker blends; one with anything above it does
 * not. Either way the frame is the one a whole paint gives. */
static void fade_case(const char *css, bool kept)
{
    sk_skinview *v = view_of(pack_of(
        "[{\"class\": \"bg\", \"rect\": {\"left\": 0, \"right\": 0, \"top\": 0, \"bottom\": 0}},"
        " {\"class\": \"veil\", \"rect\": {\"left\": 50, \"top\": 50, \"width\": 300, \"height\": 200}}]",
        "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"tools.total\", \"rect\": {\"left\": 100, \"top\": 100, "
        "\"width\": 80, \"height\": 30}}]",
        css));
    if (!v)
        return;
    sk_image f;
    sk_image_init(&f, 1000, 700);
    sk_rect out[16];
    metrics t = {0};
    num(&t, "tools.total", 1);
    for (int i = 1; i <= 90; i++) {
        if (i % 20 == 0) {
            t.m[0].value.num = i;
            sk_skinview_set_metrics(v, t.m, t.n);
        }
        sk_skinview_step(v, i * 1000.0 / 60);
        sk_skinview_draw(v, &f, i == 1, out, 16);
    }
    uint32_t veil = panel(v, 1);
    CHECK_INT(v->fade.node == veil && v->fade.valid, kept);
    sk_image whole;
    sk_image_init(&whole, 1000, 700);
    sk_skinview_draw(v, &whole, true, out, 16);
    int worst = 0;
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 400; x++) {
            sk_px a = sk_image_row(&f, y)[x], b = sk_image_row(&whole, y)[x];
            for (int sh = 0; sh < 32; sh += 8)
                worst = SK_MAX(worst, abs((int)((a >> sh) & 255) - (int)((b >> sh) & 255)));
        }
    CHECK(worst <= 2);
    sk_image_free(&whole);
    sk_image_free(&f);
    sk_skinview_free(v);
}

static void test_fade(void)
{
    static const char *const base = ".bg { background: #123 }\n.r { z-index: 1 }\n"
                                    ".veil { z-index: 5; background: repeating-linear-gradient(#000 0 2px, "
                                    "rgba(0, 0, 0, 0) 2px 4px); animation: flicker 1s steps(10) infinite }\n"
                                    "@keyframes flicker { 0% { opacity: 0.2 } 50% { opacity: 0.9 } }\n";
    fade_case(base, true);
    /* Something painted above the veil where it paints: no keeping. */
    sk_buf b = {0};
    sk_buf_printf(&b, "%s.w-readout { z-index: 9 }\n", base);
    fade_case(sk_buf_cstr(&b), false);
    sk_buf_clear(&b);
    /* A veil that also turns: painted as ever. */
    sk_buf_printf(&b, "%s@keyframes spin { to { transform: rotate(1deg) } }\n"
                      ".veil { animation: flicker 1s steps(10) infinite, spin 1s infinite }\n",
                  base);
    fade_case(sk_buf_cstr(&b), false);
    sk_buf_free(&b);
}

/* ---- What the window takes ---- */

/* The mean of each channel over r of im, unpremultiplied. */
static void mean(const sk_image *im, sk_rect r, double out[3])
{
    out[0] = out[1] = out[2] = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) {
            sk_color c = sk_unpremul(sk_image_row(im, y)[x]);
            out[0] += (c >> 16) & 255;
            out[1] += (c >> 8) & 255;
            out[2] += c & 255;
        }
    for (int k = 0; k < 3; k++)
        out[k] /= (double)r.w * r.h;
}

static sk_skinview *shipped_view(const char *name)
{
    char err[300];
    const sk_embedded_file *e = sk_shipped_pack(name);
    return view_of(e ? sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err) : NULL);
}

/* app.css's #menu-button: coffee gives one image, so hover brightens
 * and outlines it and a press darkens it and moves it a pixel down;
 * deck gives a pressed image of its own; the dot of an unseen problem
 * at the top right. */
static void test_menu_button(void)
{
    sk_skinview *v = shipped_view("coffee");
    if (!v)
        return;
    sk_skinview_step(v, 16);
    sk_image f;
    sk_image_init(&f, 1240, 820);
    sk_rect out[16];
    sk_skinview_draw(v, &f, true, out, 16);
    double mr[4];
    sk_skinview_menu_rect(v, mr);
    sk_rect r = sk_rect_make((int)mr[0] + 2, (int)mr[1] + 2, (int)mr[2] - 4, (int)mr[3] - 4);
    double plain[3], lit[3], down[3];
    mean(&f, r, plain);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, mr[0] + mr[2] / 2, mr[1] + mr[3] / 2, 0);
    sk_skinview_step(v, 32);
    CHECK(sk_skinview_draw(v, &f, false, out, 16) > 0);
    mean(&f, r, lit);
    /* The outline, a pixel outside the button, light. */
    sk_color edge = sk_unpremul(sk_image_row(&f, (int)(mr[1] + mr[3] / 2))[(int)mr[0] - 1]);
    CHECK(((edge >> 16) & 255) > 120);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, mr[0] + mr[2] / 2, mr[1] + mr[3] / 2, 1);
    sk_skinview_step(v, 48);
    sk_skinview_draw(v, &f, false, out, 16);
    mean(&f, r, down);
    CHECK(lit[0] + lit[1] + lit[2] > plain[0] + plain[1] + plain[2] + 10);
    CHECK(down[0] + down[1] + down[2] < plain[0] + plain[1] + plain[2]);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, mr[0] + mr[2] / 2, mr[1] + mr[3] / 2, 1);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_LEAVE, 0, 0, 0);
    /* An unseen problem: the red dot over the top right corner. */
    sk_skinview_set_menu(v, false, true);
    sk_skinview_step(v, 64);
    sk_skinview_draw(v, &f, false, out, 16);
    sk_color dot = sk_unpremul(sk_image_row(&f, (int)mr[1] + 1)[(int)(mr[0] + mr[2]) - 1]);
    CHECK_INT(dot, 0xffff3b30u);
    sk_skinview_set_menu(v, false, false);
    sk_skinview_step(v, 80);
    sk_skinview_draw(v, &f, false, out, 16);
    dot = sk_unpremul(sk_image_row(&f, (int)mr[1] + 1)[(int)(mr[0] + mr[2]) - 1]);
    CHECK(dot != 0xffff3b30u);
    sk_image_free(&f);
    sk_skinview_free(v);
    /* deck: its own pressed image while the drop-down is open. */
    v = shipped_view("deck");
    if (!v)
        return;
    sk_skinview_step(v, 16);
    sk_image_init(&f, 1240, 820);
    sk_skinview_draw(v, &f, true, out, 16);
    sk_skinview_menu_rect(v, mr);
    r = sk_rect_make((int)mr[0], (int)mr[1], (int)mr[2], (int)mr[3]);
    mean(&f, r, plain);
    sk_skinview_set_menu(v, true, false);
    sk_skinview_step(v, 32);
    sk_skinview_draw(v, &f, false, out, 16);
    mean(&f, r, down);
    CHECK(fabs(down[0] - plain[0]) + fabs(down[1] - plain[1]) + fabs(down[2] - plain[2]) > 3);
    sk_image_free(&f);
    sk_skinview_free(v);
}

/* A split's handle: shown where its split has room, lit under the
 * pointer, a grip at its middle. */
static void test_handle_drawn(void)
{
    sk_skinview *v = view_of(split_pack());
    if (!v)
        return;
    sk_skinview_step(v, 16);
    sk_image f;
    sk_image_init(&f, 1000, 700);
    sk_rect out[16];
    sk_skinview_draw(v, &f, true, out, 16);
    /* Right of the terminal, 8 points wide, at its middle the grip. */
    int gx = 1000 - 300 + 4, gy = 22 + 40 + (700 - 22 - 50) / 2;
    sk_color grip = sk_unpremul(sk_image_row(&f, gy)[gx]);
    sk_color strip = sk_unpremul(sk_image_row(&f, 22 + 45)[gx]);
    CHECK(((grip >> 16) & 255) > 60);
    CHECK_INT(strip, 0xff000000u);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, gx, 22 + 45, 0);
    sk_skinview_step(v, 32);
    CHECK(sk_skinview_draw(v, &f, false, out, 16) > 0);
    sk_color lit = sk_unpremul(sk_image_row(&f, 22 + 45)[gx]);
    CHECK(((lit >> 16) & 255) > 20);
    /* At 800 wide the split cannot grow, but it can still shrink: its
     * handle stays. */
    sk_skinview_set_stage(v, 0, 22, 800, 678, 1);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 800 - 300 + 4, 22 + 300, 0);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_RESIZE_EW);
    sk_image_free(&f);
    sk_skinview_free(v);
    /* A split that can grow only into room the window does not leave:
     * at the minimum window it cannot move, and has no handle. */
    v = view_of(split_pack_of(300, 400));
    if (!v)
        return;
    sk_skinview_set_stage(v, 0, 22, 800, 678, 1);
    sk_skinview_step(v, 16);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 800 - 300 + 4, 22 + 300, 0);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_ARROW);
    sk_skin_event ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_DOWN, 800 - 300 + 4, 22 + 300, 1);
    CHECK_INT(ev.action, SK_SKIN_TAKEN);
    ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 800 - 360, 22 + 300, 0);
    CHECK_INT(ev.action, SK_SKIN_TAKEN);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_UP, 800 - 360, 22 + 300, 1);
    /* At the first size it can grow: the handle is back. */
    sk_skinview_set_stage(v, 0, 22, 1000, 678, 1);
    sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, 1000 - 300 + 4, 22 + 300, 0);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_RESIZE_EW);
    sk_skinview_free(v);
}

static void test_pack_terminal(void)
{
    char err[300];
    const sk_embedded_file *e = sk_shipped_pack("phosphor");
    sk_skinpack *p = e ? sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err) : NULL;
    CHECK(p != NULL);
    if (!p)
        return;
    sk_skin_terminal t;
    sk_skinpack_terminal(p, &t);
    CHECK(t.face != NULL);
    CHECK_NEAR(t.font_px, 19, 0);
    CHECK_NEAR(sk_skin_font_px(&t, 0), 19, 0);
    CHECK_NEAR(sk_skin_font_px(&t, 16), 16, 0);
    CHECK(t.has & (1u << SK_THEME_FOREGROUND));
    CHECK(t.has & (1u << SK_THEME_BACKGROUND));
    CHECK_STR(sk_skinpack_name(p), "phosphor");
    sk_skin_window w;
    sk_skinpack_window(p, &w);
    CHECK_INT(w.width, 1260);
    CHECK_INT(w.height, 840);
    sk_skinpack_unref(p);
    /* deck names DejaVu Sans Mono, which is no pack font: none. */
    e = sk_shipped_pack("deck");
    p = e ? sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err) : NULL;
    if (p) {
        sk_skinpack_terminal(p, &t);
        CHECK(t.face == NULL);
        CHECK_NEAR(t.font_px, 14, 0);
        CHECK_INT(t.colors[SK_THEME_BACKGROUND] >> 24, 0xff);
        sk_skinpack_unref(p);
    }
    /* A pack the loader refuses. */
    uint8_t *junk = sk_xmalloc(16);
    memcpy(junk, "SKINPACKjunkjunk", 16);
    CHECK(sk_skinpack_new(junk, 16, true, err, sizeof err) == NULL);
    CHECK(err[0] != 0);
}

static void test_metrics_of_bus(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_metric_default d[] = {{"tools.total", SK_NUM(0)}};
    sk_source_spec spec = {d, 1, NULL, 0};
    sk_source *s = sk_bus_source(b, &spec);
    sk_source_begin(s);
    sk_source_set_num(s, "cost.usd", 1.5);
    sk_source_set_str(s, "model.name", SK_STR("Opus"));
    sk_skin_metrics m;
    sk_skin_metrics_of(b, &m);
    sk_bus_free(b);
    CHECK_INT(m.n, 3);
    bool cost = false, model = false;
    for (size_t i = 0; i < m.n; i++) {
        if (!strcmp(m.items[i].name, "cost.usd"))
            cost = m.items[i].value.type == SK_VAL_NUM && m.items[i].value.num == 1.5;
        if (!strcmp(m.items[i].name, "model.name"))
            model = m.items[i].value.type == SK_VAL_STR && !strcmp(m.items[i].value.text, "Opus");
    }
    CHECK(cost && model);
    sk_skin_metrics_free(&m);
    CHECK_INT(m.n, 0);
}

const sk_test sk_suite_skin_view[] = {
    T(test_formats),
    T(test_spring),
    T(test_gauge),
    T(test_needle_and_knob),
    T(test_bar),
    T(test_led),
    T(test_readout),
    T(test_sprite),
    T(test_list),
    T(test_limits),
    T(test_timing),
    T(test_transition),
    T(test_animation),
    T(test_animation_hidden),
    T(test_hover_and_clicks),
    T(test_hover_follows_layout),
    T(test_splits),
    T(test_scale),
    T(test_damage),
    T(test_damage_under_filter),
    T(test_fade),
    T(test_menu_button),
    T(test_handle_drawn),
    T(test_pack_terminal),
    T(test_metrics_of_bus),
    {0},
};
