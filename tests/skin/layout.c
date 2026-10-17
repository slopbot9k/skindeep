/* The layout of decision 0037, a feature at a time: rects and box
 * sizing, absolute and relative boxes, block flow, lines of text and
 * their heights, ellipsis, floats, flex and grid, an svg's viewBox,
 * transforms, the clip a box is under. Each case compiles a skin of
 * its own and reads the boxes laid out. */
#include "../../skinc/props.h"
#include "../test.h"
#include "util.h"

#include <math.h>
#include <string.h>

#define RECT(cls, l, t, w, h)                                                                                         \
    "{\"class\": \"" cls "\", \"rect\": {\"left\": " #l ", \"top\": " #t ", \"width\": " #w ", \"height\": " #h "}}"

static const sk_font *mono(void)
{
    return sk_font_embedded(SK_FONT_MONO);
}

/* DejaVu Sans Mono's advance at px. */
static float adv(float px)
{
    return sk_font_advance(mono(), sk_font_glyph(mono(), 'a'), px);
}

/* A face's ascent and descent at px, rounded as the layout rounds them. */
static void metrics(const sk_font *f, float px, float *a, float *d, float *gap)
{
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(f, px, &vm);
    *a = roundf(vm.ascent);
    *d = roundf(vm.descent);
    *gap = roundf(vm.line_gap);
}

static void test_rects_and_box_sizing(void)
{
    sk_tskin s;
    TSKIN(&s,
          "[" RECT("a", 10, 20, 100, 50) ", {\"class\": \"b\", \"rect\": {\"right\": 30, \"bottom\": 40, \"width\": 60, "
          "\"height\": 70}}, {\"class\": \"c\", \"rect\": {\"left\": 5, \"right\": 5, \"top\": 300, \"bottom\": 8}}]",
          NULL, ".a { border: 2px solid red; padding: 3px 4px }\n.b { box-sizing: content-box; padding: 5px }\n", NULL);
    const sk_lbox *a = sk_tskin_panel(&s, "a");
    CHECK_RECT(a->border, 10, 20, 100, 50);
    CHECK_RECT(a->padding, 12, 22, 96, 46);
    CHECK_RECT(a->content, 16, 25, 88, 40);
    /* A content-box width grows by its padding, from the right edge. */
    CHECK_RECT(sk_tskin_panel(&s, "b")->border, 900, 558, 70, 80);
    CHECK_RECT(sk_tskin_panel(&s, "c")->border, 5, 300, 990, 370);
    sk_tskin_close(&s);
}

static void test_absolute_insets_and_translate(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("p", 0, 0, 200, 100) "]", NULL,
          ".p::after { content: \"\"; position: absolute; left: 10px; right: 20px; top: 5px; bottom: 15px }\n"
          ".p::before { content: \"\"; position: absolute; left: 50%; top: 50%; width: 10px; height: 4px;\n"
          "  transform: translate(-50%, -50%) }\n",
          NULL);
    CHECK_RECT(sk_tskin_pseudo(&s, "p", true)->border, 10, 5, 170, 80);
    const sk_lbox *b = sk_tskin_pseudo(&s, "p", false);
    CHECK_RECT(b->border, 100, 50, 10, 4);
    /* Its percentages are of its own box. */
    CHECK_RECT(sk_frect_map(&b->xform, b->border), 95, 48, 10, 4);
    sk_tskin_close(&s);
}

static void test_shrink_to_fit_and_limits(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("p", 0, 0, 200, 100) "]", NULL,
          ".p::after { content: \"abc\"; position: absolute; font: 10px \"DejaVu Sans Mono\" }\n"
          ".p::before { content: \"\"; position: absolute; width: 500px; max-width: 50%; min-height: 30px;\n"
          "  padding-left: 10% }\n",
          NULL);
    /* An absolute box with no width is as wide as its text. */
    CHECK_NEAR(sk_tskin_pseudo(&s, "p", true)->border.w, 3 * adv(10));
    const sk_lbox *b = sk_tskin_pseudo(&s, "p", false);
    /* max-width of the containing block, padding a percentage of its width. */
    CHECK_NEAR(b->border.w, 120);
    CHECK_NEAR(b->pad[SK_LEFT], 20);
    CHECK_NEAR(b->border.h, 30);
    sk_tskin_close(&s);
}

static void test_block_flow(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("q", 0, 0, 200, 100) "]", NULL,
          ".q { padding: 4px }\n"
          ".q::before { content: \"\"; display: block; height: 10px }\n"
          ".q::after { content: \"\"; display: block; height: 20px; margin-left: 5px; width: 50px }\n",
          NULL);
    /* Blocks stack; a block fills the width unless it says otherwise. */
    CHECK_RECT(sk_tskin_pseudo(&s, "q", false)->border, 4, 4, 192, 10);
    CHECK_RECT(sk_tskin_pseudo(&s, "q", true)->border, 9, 14, 50, 20);
    sk_tskin_close(&s);
}

static void test_lines_wrap_at_spaces(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("t", 0, 0, 100, 100) ", " RECT("u", 0, 200, 100, 100) ", " RECT("v", 0, 400, 100, 100) "]",
          NULL,
          ".t, .u, .v { font: 10px/12px \"DejaVu Sans Mono\" }\n"
          ".t::after, .u::after { content: \"aaaa bbbb cccc dddd\" }\n"
          ".u { white-space: nowrap }\n"
          ".v { text-align: center }\n.v::after { content: \"ab\" }\n",
          NULL);
    float a, d, gap;
    metrics(mono(), 10, &a, &d, &gap);
    float base = a + floorf((12 - a - d) / 2);
    const sk_lbox *t = sk_tskin_panel(&s, "t");
    /* Fourteen characters fit in 100px, nineteen do not: two lines. */
    CHECK_INT(t->nruns, 2);
    if (t->nruns == 2) {
        const sk_lrun *r = &s.l.runs[t->runs];
        CHECK_NEAR(r[0].x, 0);
        CHECK_NEAR(r[0].y, base);
        CHECK_NEAR(r[1].y, base + 12);
        CHECK_INT(r[0].n, 14);
        CHECK_INT(r[1].n, 4);
    }
    CHECK_INT(sk_tskin_panel(&s, "u")->nruns, 1);
    const sk_lbox *v = sk_tskin_panel(&s, "v");
    CHECK_INT(v->nruns, 1);
    if (v->nruns)
        CHECK_NEAR(s.l.runs[v->runs].x, (100 - 2 * adv(10)) / 2);
    sk_tskin_close(&s);
}

static void test_line_heights(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("h1", 0, 0, 100, 100) ", " RECT("h2", 0, 100, 100, 100) ", " RECT("h3", 0, 200, 100, 100) "]",
          NULL,
          ".h1 { font: 10px/30px \"DejaVu Sans\" }\n.h2 { font-size: 10px; line-height: 2 }\n"
          ".h3 { font: 10px \"DejaVu Sans\" }\n"
          ".h1::after, .h2::after, .h3::after { content: \"x\"; display: block }\n",
          NULL);
    float a, d, gap;
    metrics(sk_font_embedded(SK_FONT_SANS), 10, &a, &d, &gap);
    CHECK_NEAR(sk_tskin_pseudo(&s, "h1", true)->border.h, 30);
    CHECK_NEAR(sk_tskin_pseudo(&s, "h2", true)->border.h, 20);
    /* normal: the font's ascent, descent and gap. */
    CHECK_NEAR(sk_tskin_pseudo(&s, "h3", true)->border.h, a + d + gap);
    /* The baseline: half the leading above the ascent. */
    const sk_lbox *h1 = sk_tskin_pseudo(&s, "h1", true);
    CHECK_NEAR(h1->baseline, a + floorf((30 - a - d) / 2));
    sk_tskin_close(&s);
}

static void test_letter_spacing_and_transform(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("p", 0, 0, 200, 100) "]", NULL,
          ".p { font: 10px \"DejaVu Sans Mono\"; letter-spacing: 2px; text-transform: uppercase }\n"
          ".p::after { content: \"abc\" }\n",
          NULL);
    const sk_lbox *p = sk_tskin_panel(&s, "p");
    CHECK_INT(p->nruns, 1);
    if (p->nruns) {
        const sk_lrun *r = &s.l.runs[p->runs];
        /* Every character takes the spacing after it, the last too. */
        CHECK_NEAR(r->width, 3 * (adv(10) + 2));
        CHECK_INT(s.l.glyphs[r->first].glyph, sk_font_glyph(mono(), 'A'));
        CHECK_NEAR(s.l.glyphs[r->first + 1].x, adv(10) + 2);
    }
    sk_tskin_close(&s);
}

static void test_ellipsis(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("e", 0, 0, 200, 100) "]", NULL,
          ".e::after { content: \"abcdefghijklmnop\"; display: block; width: 30px; overflow: hidden;\n"
          "  white-space: nowrap; text-overflow: ellipsis; font: 10px \"DejaVu Sans Mono\" }\n",
          NULL);
    const sk_lbox *e = sk_tskin_pseudo(&s, "e", true);
    CHECK_INT(e->nruns, 2);
    if (e->nruns == 2) {
        const sk_lrun *r = &s.l.runs[e->runs];
        /* Three characters and the ellipsis fit in 30px. */
        CHECK_INT(r[0].n, 3);
        CHECK_INT(s.l.glyphs[r[1].first].glyph, sk_font_glyph(mono(), 0x2026));
        CHECK(r[1].x + r[1].width <= 30.01f);
    }
    sk_tskin_close(&s);
}

static void test_float_right(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("f", 0, 0, 200, 100) "]", NULL,
          ".f { white-space: nowrap }\n"
          ".f::after { content: \"x\"; float: right; font: 10px \"DejaVu Sans Mono\" }\n",
          NULL);
    const sk_lbox *x = sk_tskin_pseudo(&s, "f", true);
    CHECK_NEAR(x->border.x + x->border.w, 200);
    CHECK_NEAR(x->border.y, 0);
    CHECK_NEAR(x->border.w, adv(10));
    sk_tskin_close(&s);
}

static void test_flex_grow_shrink_basis(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("f", 0, 0, 300, 50) ", " RECT("g", 0, 100, 300, 50) ", " RECT("m", 0, 200, 200, 50) ", " RECT(
                  "n", 0, 300, 200, 50) "]",
          NULL,
          ".f, .g, .m, .n { display: flex }\n.f { column-gap: 30px }\n"
          ".f::before { content: \"\"; flex: 1 } .f::after { content: \"\"; flex: 2 }\n"
          ".g::before { content: \"\"; flex: 0 0 50px } .g::after { content: \"\"; flex: 1 1 auto; width: 400px }\n"
          ".m::before, .n::before { content: \"aaaaaaaaaa\"; flex: 1 1 auto; font: 10px \"DejaVu Sans Mono\" }\n"
          ".m::after, .n::after { content: \"\"; flex: 0 0 180px }\n.n::before { overflow: hidden }\n",
          NULL);
    /* Free space by grow, the gap taken first. */
    CHECK_RECT(sk_tskin_pseudo(&s, "f", false)->border, 0, 0, 90, 50);
    CHECK_RECT(sk_tskin_pseudo(&s, "f", true)->border, 120, 0, 180, 50);
    /* An item shrinks by its basis; one that may not keeps it. */
    CHECK_NEAR(sk_tskin_pseudo(&s, "g", false)->border.w, 50);
    CHECK_NEAR(sk_tskin_pseudo(&s, "g", true)->border.w, 250);
    /* The automatic minimum: no narrower than its text, unless it
     * clips its overflow. */
    CHECK_NEAR(sk_tskin_pseudo(&s, "m", false)->border.w, 10 * adv(10));
    CHECK_NEAR(sk_tskin_pseudo(&s, "n", false)->border.w, 20);
    sk_tskin_close(&s);
}

static const char *READOUTS =
    "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"label\": \"cost\", \"rect\": {\"left\": 0, "
    "\"top\": 0, \"width\": 200, \"height\": 100}},\n"
    " {\"id\": \"s\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"label\": \"usd\", \"class\": \"small\", "
    "\"rect\": {\"left\": 0, \"top\": 200, \"width\": 200, \"height\": 100}},\n"
    " {\"id\": \"t\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"label\": \"usd\", \"class\": \"rev\", "
    "\"rect\": {\"left\": 0, \"top\": 400, \"width\": 200, \"height\": 100}}]";

static void test_flex_column_alignment(void)
{
    sk_tskin s;
    TSKIN(&s, NULL, READOUTS, "", NULL);
    /* widgets.css: a column centred on the main axis, a readout's
     * items at the start of the cross; 11px/1.2 and 18px/1.2. */
    const sk_lbox *label = sk_tskin_part(&s, "r", SK_ROLE_LABEL, 0);
    const sk_lbox *value = sk_tskin_part(&s, "r", SK_ROLE_VALUE, 0);
    CHECK_NEAR(label->border.x, 0);
    CHECK_NEAR(label->border.h, 13.2f);
    CHECK_NEAR(value->border.h, 21.6f);
    CHECK_NEAR(label->border.y, (100 - 34.8f) / 2);
    CHECK_NEAR(value->border.y, (100 - 34.8f) / 2 + 13.2f);
    sk_tskin_close(&s);
}

static void test_flex_baseline_and_reverse(void)
{
    sk_tskin s;
    TSKIN(&s, NULL, READOUTS,
          ".small { flex-direction: row; align-items: baseline; gap: 6px }\n"
          ".rev { flex-direction: row-reverse; justify-content: flex-start }\n",
          NULL);
    const sk_lbox *label = sk_tskin_part(&s, "s", SK_ROLE_LABEL, 0);
    const sk_lbox *value = sk_tskin_part(&s, "s", SK_ROLE_VALUE, 0);
    /* Different sizes, one baseline. */
    CHECK_NEAR(label->baseline, value->baseline);
    CHECK(label->border.h < value->border.h);
    CHECK_NEAR(value->border.x, label->border.x + label->border.w + 6);
    /* Reversed: the first item at the right, from flex-start. */
    const sk_lbox *rl = sk_tskin_part(&s, "t", SK_ROLE_LABEL, 0);
    const sk_lbox *rv = sk_tskin_part(&s, "t", SK_ROLE_VALUE, 0);
    CHECK_NEAR(rl->border.x + rl->border.w, 200);
    CHECK_NEAR(rv->border.x + rv->border.w, rl->border.x);
    sk_tskin_close(&s);
}

static const char *BARS =
    "[{\"id\": \"b\", \"type\": \"bar\", \"bind\": \"context.used_pct\", \"segments\": 4, \"gap\": 2, \"rect\": {\"left\": 0, "
    "\"top\": 0, \"width\": 100, \"height\": 20}},\n"
    " {\"id\": \"g\", \"type\": \"bar\", \"bind\": \"context.used_pct\", \"segments\": 5, \"class\": \"grid\", \"rect\": {\"left\": 0, "
    "\"top\": 100, \"width\": 100, \"height\": 48}},\n"
    " {\"id\": \"c\", \"type\": \"bar\", \"bind\": \"context.used_pct\", \"rect\": {\"left\": 0, \"top\": 200, \"width\": 100, "
    "\"height\": 10}}]";

static void test_flex_segments(void)
{
    sk_tskin s;
    TSKIN(&s, NULL, BARS, "", NULL);
    for (int k = 0; k < 4; k++)
        CHECK_RECT(sk_tskin_part(&s, "b", SK_ROLE_SEG, k)->border, 25.5f * (float)k, 0, 23.5f, 20);
    sk_tskin_close(&s);
}

static void test_grid_tracks_and_span(void)
{
    sk_tskin s;
    TSKIN(&s, NULL, BARS,
          ".grid .track { display: grid !important; grid-template-columns: 1fr 1fr; grid-auto-rows: 1fr;\n"
          "  gap: 4px !important }\n"
          ".grid .seg:last-child { grid-column: span 2 }\n",
          NULL);
    /* Two columns of 48, three rows of 40/3, the last item across both. */
    float rh = 40.0f / 3;
    CHECK_RECT(sk_tskin_part(&s, "g", SK_ROLE_SEG, 0)->border, 0, 100, 48, rh);
    CHECK_RECT(sk_tskin_part(&s, "g", SK_ROLE_SEG, 1)->border, 52, 100, 48, rh);
    CHECK_RECT(sk_tskin_part(&s, "g", SK_ROLE_SEG, 3)->border, 52, 100 + rh + 4, 48, rh);
    CHECK_RECT(sk_tskin_part(&s, "g", SK_ROLE_SEG, 4)->border, 0, 100 + 2 * (rh + 4), 100, rh);
    sk_tskin_close(&s);
}

static void test_clipper_and_escape(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("cl", 0, 300, 200, 100) "]", BARS,
          ".cl { overflow: hidden }\n"
          ".cl::before { content: \"\"; display: block; position: relative; top: 5px; left: 7px; height: 10px }\n",
          NULL);
    const sk_lbox *p = sk_tskin_panel(&s, "cl");
    const sk_lbox *b = sk_tskin_pseudo(&s, "cl", false);
    CHECK_RECT(b->border, 7, 305, 200, 10);
    CHECK_INT(b->clipper, p->node);
    /* A bar's fill: absolute in its track, which clips it. */
    const sk_lbox *track = sk_tskin_part(&s, "c", SK_ROLE_TRACK, 0);
    const sk_lbox *fill = sk_tskin_part(&s, "c", SK_ROLE_FILL, 0);
    CHECK_INT(fill->clipper, track->node);
    CHECK_INT(fill->cb, track->node);
    sk_tskin_close(&s);
    /* A static track no longer contains the fill: its clip does not
     * reach it. */
    TSKIN(&s, NULL, BARS, ".w-bar .track { position: static }\n", NULL);
    track = sk_tskin_part(&s, "c", SK_ROLE_TRACK, 0);
    fill = sk_tskin_part(&s, "c", SK_ROLE_FILL, 0);
    CHECK(fill->clipper != track->node);
    CHECK_INT(fill->cb, sk_tskin_part(&s, "c", SK_ROLE_WIDGET, 0)->node);
    sk_tskin_close(&s);
}

static void test_svg_viewbox(void)
{
    sk_tskin s;
    TSKIN(&s, NULL,
          "[{\"id\": \"gg\", \"type\": \"gauge\", \"bind\": \"context.used_pct\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 200, "
          "\"height\": 100}}]",
          "", NULL);
    const sk_lbox *dial = sk_tskin_part(&s, "gg", SK_ROLE_DIAL, 0);
    /* As wide as the widget, its height what the column leaves; the
     * viewBox of 100 met and centred. */
    CHECK_RECT(dial->border, 0, 0, 200, 100);
    CHECK_NEAR(dial->viewbox.a, 1);
    CHECK_NEAR(dial->viewbox.d, 1);
    CHECK_NEAR(dial->viewbox.e, 50);
    CHECK_NEAR(dial->viewbox.f, 0);
    /* The track's arc, in user units, maps through it. */
    const sk_lbox *track = sk_tskin_part(&s, "gg", SK_ROLE_ARC_TRACK, 0);
    CHECK(track->svg_content);
    CHECK_NEAR(track->xform.e, 50);
    sk_tskin_close(&s);
}

static void test_transform_about_origin(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("rt", 0, 0, 200, 100) "]", NULL,
          ".rt::after { content: \"\"; position: absolute; left: 0; top: 0; width: 40px; height: 20px;\n"
          "  transform: rotate(90deg) }\n"
          ".rt::before { content: \"\"; position: absolute; left: 100px; top: 0; width: 40px; height: 20px;\n"
          "  transform: scale(0.5); transform-origin: 0 100% }\n",
          NULL);
    const sk_lbox *a = sk_tskin_pseudo(&s, "rt", true);
    /* A quarter turn about the centre. */
    CHECK_RECT(sk_frect_map(&a->xform, a->border), 10, -10, 20, 40);
    const sk_lbox *b = sk_tskin_pseudo(&s, "rt", false);
    CHECK_RECT(sk_frect_map(&b->xform, b->border), 100, 10, 20, 10);
    sk_tskin_close(&s);
}

static void test_flex_justify_align_auto_margins(void)
{
    sk_tskin s;
    TSKIN(&s,
          "[" RECT("j1", 0, 0, 300, 50) ", " RECT("j2", 0, 100, 300, 50) ", " RECT("j3", 0, 200, 300, 50) ", " RECT(
              "j4", 0, 300, 300, 50) ", " RECT("sf", 400, 0, 100, 100) "]",
          NULL,
          ".j1, .j2, .j3, .j4 { display: flex }\n"
          ".j1::before, .j2::before, .j3::before, .j4::before { content: \"\"; width: 50px; height: 10px }\n"
          ".j1::after, .j2::after, .j3::after, .j4::after { content: \"\"; width: 50px; height: 20px }\n"
          ".j1 { justify-content: space-between; align-items: center }\n"
          ".j2 { justify-content: center; align-items: flex-end }\n"
          ".j3 { justify-content: space-evenly }\n.j4::after { margin-left: auto; align-self: flex-end }\n"
          ".sf { display: flex; justify-content: center; align-items: center }\n"
          ".sf::after { content: \"\"; position: absolute; width: 10px; height: 10px }\n",
          NULL);
    CHECK_RECT(sk_tskin_pseudo(&s, "j1", false)->border, 0, 20, 50, 10);
    CHECK_RECT(sk_tskin_pseudo(&s, "j1", true)->border, 250, 15, 50, 20);
    CHECK_RECT(sk_tskin_pseudo(&s, "j2", false)->border, 100, 140, 50, 10);
    CHECK_RECT(sk_tskin_pseudo(&s, "j2", true)->border, 150, 130, 50, 20);
    CHECK_NEAR(sk_tskin_pseudo(&s, "j3", false)->border.x, 200.0f / 3);
    CHECK_NEAR(sk_tskin_pseudo(&s, "j3", true)->border.x, 550.0f / 3);
    /* An auto margin takes the free space; stretch is the default. */
    CHECK_RECT(sk_tskin_pseudo(&s, "j4", true)->border, 250, 330, 50, 20);
    CHECK_RECT(sk_tskin_pseudo(&s, "j4", false)->border, 0, 300, 50, 10);
    /* An absolute child with no insets sits where the flex container
     * would put it alone: centred. */
    CHECK_RECT(sk_tskin_pseudo(&s, "sf", true)->border, 445, 45, 10, 10);
    sk_tskin_close(&s);
}

static void test_line_of_mixed_fonts_and_pre(void)
{
    sk_tskin s;
    TSKIN(&s, "[" RECT("m", 0, 0, 300, 100) ", " RECT("pre", 0, 200, 300, 100) "]", NULL,
          ".m { font: 10px/12px \"DejaVu Sans Mono\" }\n"
          ".m::before { content: \"a\"; font-size: 30px }\n.m::after { content: \"b\" }\n"
          ".pre { white-space: pre; font: 10px/10px \"DejaVu Sans Mono\" }\n.pre::after { content: \"a\\A  b\" }\n",
          NULL);
    /* The 30px box's half-leading, 12px tall, sets the line's top; the
     * strut its bottom; one baseline for both. */
    float a10, d10, g, a30, d30;
    metrics(mono(), 10, &a10, &d10, &g);
    metrics(mono(), 30, &a30, &d30, &g);
    float above = fmaxf(a10 + floorf((12 - a10 - d10) / 2), a30 + floorf((12 - a30 - d30) / 2));
    const sk_lbox *m = sk_tskin_panel(&s, "m");
    CHECK_INT(m->nruns, 2);
    if (m->nruns == 2) {
        CHECK_NEAR(s.l.runs[m->runs].y, above);
        CHECK_NEAR(s.l.runs[m->runs + 1].y, above);
        CHECK_NEAR(s.l.runs[m->runs + 1].x, adv(30));
    }
    /* pre: a line break where the text has one, its spaces kept (the
     * escape \A takes the one space after it). */
    const sk_lbox *p = sk_tskin_panel(&s, "pre");
    CHECK_INT(p->nruns, 2);
    if (p->nruns == 2) {
        CHECK_NEAR(s.l.runs[p->runs + 1].y - s.l.runs[p->runs].y, 10);
        CHECK_INT(s.l.runs[p->runs + 1].n, 2);
    }
    sk_tskin_close(&s);
}

static void test_grid_lines(void)
{
    sk_tskin s;
    TSKIN(&s, NULL,
          "[{\"id\": \"q\", \"type\": \"bar\", \"bind\": \"context.used_pct\", \"segments\": 4, \"rect\": {\"left\": "
          "0, \"top\": 0, \"width\": 90, \"height\": 40}}]",
          ".w-bar .track { display: grid !important; grid-template-columns: 30px 30px 30px; grid-auto-rows: 20px;\n"
          "  gap: 0 !important }\n"
          ".seg:first-child { grid-row: 2; grid-column: 1 / span 3 }\n",
          NULL);
    /* Its lines given, the first goes to the second row, across all
     * three; the rest fill the first row. */
    CHECK_RECT(sk_tskin_part(&s, "q", SK_ROLE_SEG, 0)->border, 0, 20, 90, 20);
    CHECK_RECT(sk_tskin_part(&s, "q", SK_ROLE_SEG, 1)->border, 0, 0, 30, 20);
    CHECK_RECT(sk_tskin_part(&s, "q", SK_ROLE_SEG, 3)->border, 60, 0, 30, 20);
    sk_tskin_close(&s);
}

/* A tree far deeper than any skin's, built by hand: laid out and
 * painted, its depth cut at 200, no pass recursing past it. */
static void test_deep_tree(void)
{
    enum { N = 5000 };
    sk_css_style st;
    sk_style_initial(&st, (sk_css_color){0, 0, 0, 1});
    st.display = SK_DISPLAY_BLOCK;
    st.padding[SK_LEFT] = (sk_css_len){1, 0, SK_LEN_PX};
    st.bg_color = (sk_css_color){1, 0, 0, 0.01f};
    sk_css_style root = st;
    root.position = SK_POS_ABSOLUTE;
    root.width = (sk_css_len){300, 0, SK_LEN_PX};
    root.height = (sk_css_len){100, 0, SK_LEN_PX};
    sk_lnode *nodes = sk_xcalloc(N, sizeof *nodes);
    for (uint32_t i = 0; i < N; i++) {
        nodes[i].parent = i ? i - 1 : SK_PACK_NONE;
        nodes[i].first = i + 1 < N ? i + 1 : SK_PACK_NONE;
        nodes[i].next = SK_PACK_NONE;
        nodes[i].kind = SK_EL_DIV;
        nodes[i].style = i ? &st : &root;
        nodes[i].text = i == N - 1 ? "deep" : NULL;
    }
    sk_layout l;
    sk_layout_run(&l, nodes, N, 0, 1000, 678, NULL);
    CHECK(l.boxes[0].generated);
    CHECK(l.boxes[200].generated);
    CHECK(!l.boxes[201].generated);
    CHECK_NEAR(l.boxes[200].border.x, 200);
    sk_image im;
    sk_image_init(&im, 300, 100);
    sk_glyph_cache *gc = sk_glyph_cache_new(1u << 20);
    sk_paint_env env = {&l, NULL, NULL, gc, NULL};
    sk_paint(&im, &env, 0, 1, 0, 0);
    CHECK(sk_image_row(&im, 50)[150] != 0);
    sk_glyph_cache_free(gc);
    sk_image_free(&im);
    sk_layout_free(&l);
    free(nodes);
}

const sk_test sk_suite_skin_layout[] = {
    T(test_deep_tree),
    T(test_rects_and_box_sizing),
    T(test_absolute_insets_and_translate),
    T(test_shrink_to_fit_and_limits),
    T(test_block_flow),
    T(test_lines_wrap_at_spaces),
    T(test_line_heights),
    T(test_letter_spacing_and_transform),
    T(test_ellipsis),
    T(test_float_right),
    T(test_flex_grow_shrink_basis),
    T(test_flex_column_alignment),
    T(test_flex_baseline_and_reverse),
    T(test_flex_justify_align_auto_margins),
    T(test_line_of_mixed_fonts_and_pre),
    T(test_grid_lines),
    T(test_flex_segments),
    T(test_grid_tracks_and_span),
    T(test_clipper_and_escape),
    T(test_svg_viewbox),
    T(test_transform_about_origin),
    {0},
};
