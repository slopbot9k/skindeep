/* The cascade over a skin's element tree: origin, importance,
 * specificity and order; the app's own declarations; inheritance,
 * custom properties and var(); em and currentcolor; every state a
 * selector can reach, the variables each element's style reads; and
 * counters in generated content. Each case compiles a skin of its own. */
#include "../test.h"
#include "util.h"

#include <string.h>

static const char *PANEL_PQ = "[{\"class\": \"p q\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 100, \"height\": 50}}]";

/* The first panel's style, every input 0. */
static const sk_css_style *panel_style(const sk_front *f)
{
    return sk_front_style(f, sk_front_panel(f, "p"), NULL, 0);
}

static void test_order_and_specificity(void)
{
    sk_front f;
    sk_diags d = {0};
    COMPILES(&f, &d, PANEL_PQ, NULL,
                      ".p { color: red } .q { color: blue }\n"
                      ".p.q { background-color: green } .q { background-color: blue }\n"
                      ".p { opacity: 0.5 !important } .p.q { opacity: 0.25 }\n"
                      ".p { width: 5px }\n"
                      ".p { height: 7px !important }\n");
    const sk_css_style *s = panel_style(&f);
    /* Later wins at equal specificity, higher specificity wins, and
     * !important beats both. */
    CHECK_STR(sk_test_prop(s, SK_PROP_COLOR), "#0000ff");
    CHECK_STR(sk_test_prop(s, SK_PROP_BACKGROUND_COLOR), "#008000");
    CHECK_STR(sk_test_prop(s, SK_PROP_OPACITY), "0.5");
    /* The rect is the element's style attribute: it beats the author's
     * rules unless they are !important. */
    CHECK_STR(sk_test_prop(s, SK_PROP_WIDTH), "rect(100px)");
    CHECK_STR(sk_test_prop(s, SK_PROP_HEIGHT), "7px");
    CHECK_STR(sk_test_prop(s, SK_PROP_RIGHT), "auto");
    /* widgets.css's .panel rule, and the user agent's div. */
    CHECK_STR(sk_test_prop(s, SK_PROP_POSITION), "absolute");
    CHECK_STR(sk_test_prop(s, SK_PROP_DISPLAY), "block");
    sk_front_free(&f);
    sk_diags_free(&d);
}

/* var() as browsers bound it: a value that would expand past its size
 * or nest past the parser's depth is invalid, so a few lines of CSS
 * cannot take gigabytes (a review's six levels took 3.7 GB) or crash
 * skinc's stack; the fallback stands instead. */
static void test_custom_properties_are_bounded(void)
{
    sk_buf css = {0};
    sk_buf_append_c(&css, ".p { --v0: x x x x x x x x x x;\n");
    for (int l = 1; l <= 6; l++) {
        sk_buf_printf(&css, "  --v%d:", l);
        for (int k = 0; k < 10; k++)
            sk_buf_printf(&css, " var(--v%d)", l - 1);
        sk_buf_append_c(&css, ";\n");
    }
    /* Each wraps the one before in 25 parentheses: past 64 by the third. */
    sk_buf_append_c(&css, "  --d0: 1px;\n");
    for (int l = 1; l <= 40; l++) {
        sk_buf_printf(&css, "  --d%d: ", l);
        for (int k = 0; k < 25; k++)
            sk_buf_putc(&css, '(');
        sk_buf_printf(&css, "var(--d%d)", l - 1);
        for (int k = 0; k < 25; k++)
            sk_buf_putc(&css, ')');
        sk_buf_append_c(&css, ";\n");
    }
    sk_buf_append_c(&css, "  padding-top: var(--v6, 3px); padding-left: var(--d40, 4px) }\n");
    sk_front f;
    sk_diags d = {0};
    COMPILES(&f, &d, PANEL_PQ, NULL, sk_buf_cstr(&css));
    const sk_css_style *s = panel_style(&f);
    CHECK_STR(sk_test_prop(s, SK_PROP_PADDING_TOP), "3px");
    CHECK_STR(sk_test_prop(s, SK_PROP_PADDING_LEFT), "4px");
    sk_front_free(&f);
    sk_diags_free(&d);
    sk_buf_free(&css);
}

static void test_inheritance_em_currentcolor(void)
{
    sk_front f;
    sk_diags d = {0};
    COMPILES(&f, &d, PANEL_PQ, NULL,
                      ":host { font-size: 20px }\n"
                      ".p { color: #00ff00; font-size: 2em; border: 1px solid; letter-spacing: 0.1em;\n"
                      "  background-color: red; line-height: 1.5 }\n"
                      ".p::after { content: \"x\"; display: inline-block; background-color: inherit; width: 2em }\n");
    const sk_el *p = sk_front_panel(&f, "p");
    const sk_css_style *s = panel_style(&f);
    CHECK_STR(sk_test_prop(s, SK_PROP_FONT_SIZE), "40px");
    CHECK_STR(sk_test_prop(s, SK_PROP_LETTER_SPACING), "4px");
    CHECK_STR(sk_test_prop(s, SK_PROP_BORDER_TOP_COLOR), "#00ff00");
    CHECK_STR(sk_test_prop(s, SK_PROP_BORDER_LEFT_WIDTH), "1px");
    CHECK(p && p->after);
    const sk_css_style *a = sk_front_style(&f, p->after, NULL, 0);
    CHECK_STR(sk_test_prop(a, SK_PROP_COLOR), "#00ff00");
    CHECK_STR(sk_test_prop(a, SK_PROP_FONT_SIZE), "40px");
    CHECK_STR(sk_test_prop(a, SK_PROP_WIDTH), "80px");
    CHECK_STR(sk_test_prop(a, SK_PROP_LINE_HEIGHT), "1.5");
    /* inherit takes a property that does not inherit. */
    CHECK_STR(sk_test_prop(a, SK_PROP_BACKGROUND_COLOR), "#ff0000");
    CHECK_STR(sk_test_prop(a, SK_PROP_CONTENT), "\"x\"");
    /* The stage inherits :host's. */
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_custom_properties(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets = "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"label\": \"c\", "
                          "\"class\": \"k\", \"rect\": {\"left\": 0, \"top\": 60, \"width\": 100, \"height\": 30}}]";
    COMPILES(&f, &d, PANEL_PQ, widgets,
                      ".w { --text: #123456 }\n"
                      ".k { --a: var(--b); --b: var(--a); border-color: var(--a, blue) }\n"
                      ".k .label { outline-color: var(--glow); --x: 3px; padding-left: var(--x) }\n"
                      ".p { --c: var(--none, 2px) var(--none, solid); border-top: var(--c) }\n");
    const sk_el *root = sk_front_find(&f, "r", SK_ROLE_WIDGET, 0);
    const sk_css_style *s = sk_front_style(&f, root, NULL, 0);
    /* widgets.css's color: var(--text), the skin's --text. */
    CHECK_STR(sk_test_prop(s, SK_PROP_COLOR), "#123456");
    /* A cycle makes both invalid: the fallback stands. */
    CHECK_STR(sk_test_prop(s, SK_PROP_BORDER_TOP_COLOR), "#0000ff");
    const sk_css_style *l = sk_front_style(&f, sk_front_find(&f, "r", SK_ROLE_LABEL, 0), NULL, 0);
    /* --glow inherited from .w's widgets.css values. */
    CHECK_STR(sk_test_prop(l, SK_PROP_OUTLINE_COLOR), "rgba(0, 0, 0, 0)");
    CHECK_STR(sk_test_prop(l, SK_PROP_PADDING_LEFT), "3px");
    const sk_css_style *p = panel_style(&f);
    CHECK_STR(sk_test_prop(p, SK_PROP_BORDER_TOP_WIDTH), "2px");
    CHECK_STR(sk_test_prop(p, SK_PROP_BORDER_TOP_STYLE), "solid");
    sk_front_free(&f);
    sk_diags_free(&d);

    /* A var() that names nothing and has no fallback fails, at its line. */
    CHECK(!sk_test_compile(&f, &d, PANEL_PQ, NULL, "\n.p { color: var(--nope) }\n"));
    CHECK(sk_test_said(&d, "skin.css: line 2: \"color\": a var() names no custom property and has no fallback"));
    sk_front_free(&f);
    sk_diags_free(&d);
    CHECK(!sk_test_compile(&f, &d, PANEL_PQ, NULL, ".p { --c: 12; color: var(--c) }\n"));
    CHECK(sk_test_said(&d, "skin.css: line 1: \"color\": after var(), \"12\" is not a valid value"));
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_states_of_a_widget(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets = "[{\"id\": \"lamp\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, \"label\": "
                          "\"busy\", \"class\": \"x\", \"rect\": {\"left\": 0, \"top\": 60, \"width\": 100, "
                          "\"height\": 30}},\n"
                          " {\"id\": \"r\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"rect\": {\"left\": 0, "
                          "\"top\": 100, \"width\": 100, \"height\": 30}}]";
    COMPILES(&f, &d, NULL, widgets,
                      ".x .lamp.on { background-color: red }\n"
                      ".x:has(.lamp.on) { border-color: blue }\n"
                      ".x .lamp.on + .label { color: yellow }\n"
                      ".w-readout:hover .value { color: green }\n");
    const sk_el *root = sk_front_find(&f, "lamp", SK_ROLE_WIDGET, 0);
    const sk_el *lamp = sk_front_find(&f, "lamp", SK_ROLE_LAMP, 0);
    const sk_el *label = sk_front_find(&f, "lamp", SK_ROLE_LABEL, 0);
    CHECK(root && lamp && label);
    sk_assign on = {lamp, SK_VAR_ON, 1}, nodata = {root, SK_VAR_NODATA, 1};
    /* The lamp reads its own .on. */
    CHECK_INT(lamp->ninputs, 1);
    CHECK_STR(sk_test_prop(sk_front_style(&f, lamp, NULL, 0), SK_PROP_BACKGROUND_COLOR), "#20302a");
    CHECK_STR(sk_test_prop(sk_front_style(&f, lamp, &on, 1), SK_PROP_BACKGROUND_COLOR), "#ff0000");
    /* :has() makes the root read its lamp's; .nodata its own. */
    CHECK_STR(sk_test_prop(sk_front_style(&f, root, &on, 1), SK_PROP_BORDER_TOP_COLOR), "#0000ff");
    CHECK_STR(sk_test_prop(sk_front_style(&f, root, NULL, 0), SK_PROP_OPACITY), "1");
    CHECK_STR(sk_test_prop(sk_front_style(&f, root, &nodata, 1), SK_PROP_OPACITY), "0.55");
    /* + makes the label read its sibling's. */
    CHECK_STR(sk_test_prop(sk_front_style(&f, label, &on, 1), SK_PROP_COLOR), "#ffff00");
    /* :hover on an ancestor; a readout that takes a click. */
    const sk_el *r = sk_front_find(&f, "r", SK_ROLE_WIDGET, 0);
    const sk_el *v = sk_front_find(&f, "r", SK_ROLE_VALUE, 0);
    sk_assign hover = {r, SK_VAR_HOVER, 1}, open = {r, SK_VAR_OPEN, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, v, &hover, 1), SK_PROP_COLOR), "#008000");
    CHECK_STR(sk_test_prop(sk_front_style(&f, r, &hover, 1), SK_PROP_FILTER), "brightness(1.3)");
    CHECK_STR(sk_test_prop(sk_front_style(&f, r, &hover, 1), SK_PROP_CURSOR), "pointer");
    CHECK_STR(sk_test_prop(sk_front_style(&f, r, &open, 1), SK_PROP_OUTLINE_STYLE), "solid");
    CHECK_STR(sk_test_prop(sk_front_style(&f, r, NULL, 0), SK_PROP_OUTLINE_WIDTH), "0px");
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_stage_and_list(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets =
        "[{\"id\": \"a\", \"type\": \"readout\", \"bind\": \"tools.total\", \"class\": \"line\", \"rect\": {\"left\": "
        "0, \"top\": 60, \"width\": 100, \"height\": 30}},\n"
        " {\"id\": \"todo\", \"type\": \"list\", \"bind\": \"checklist.items\", \"class\": \"tl\", \"rect\": "
        "{\"left\": 200, \"top\": 60, \"width\": 100, \"height\": 200},\n"
        "  \"toggle\": {\"rect\": {\"left\": 400, \"top\": 60, \"width\": 100, \"height\": 20}}}]";
    COMPILES(&f, &d, NULL, widgets,
                      ".skin { counter-reset: none }\n"
                      ".skin.checklist .line { visibility: hidden }\n"
                      ".tl { counter-reset: t 10 }\n"
                      ".tl .item { counter-increment: t 2 }\n"
                      ".tl .item .text::before { content: counter(t) \". \" }\n"
                      ".tl .item.done .mark { color: red }\n");
    const sk_el *stage = f.els.ptr[0]->last;
    const sk_el *line = sk_front_find(&f, "a", SK_ROLE_WIDGET, 0);
    sk_assign checklist = {stage, SK_VAR_LIST, 1}, off = {stage, SK_VAR_LIST, 2};
    CHECK_STR(sk_test_prop(sk_front_style(&f, line, NULL, 0), SK_PROP_VISIBILITY), "visible");
    CHECK_STR(sk_test_prop(sk_front_style(&f, line, &checklist, 1), SK_PROP_VISIBILITY), "hidden");
    CHECK_STR(sk_test_prop(sk_front_style(&f, line, &off, 1), SK_PROP_VISIBILITY), "visible");
    /* The label inherits visibility: it reads the stage too. */
    const sk_el *value = sk_front_find(&f, "a", SK_ROLE_VALUE, 0);
    CHECK_STR(sk_test_prop(sk_front_style(&f, value, &checklist, 1), SK_PROP_VISIBILITY), "hidden");
    /* The toggle shows while the list has items, either way. */
    const sk_el *toggle = sk_front_find(&f, "todo", SK_ROLE_TOGGLE, 0);
    CHECK_STR(sk_test_prop(sk_front_style(&f, toggle, NULL, 0), SK_PROP_DISPLAY), "none");
    CHECK_STR(sk_test_prop(sk_front_style(&f, toggle, &checklist, 1), SK_PROP_DISPLAY), "flex");
    CHECK_STR(sk_test_prop(sk_front_style(&f, toggle, &off, 1), SK_PROP_DISPLAY), "flex");
    const sk_el *list = sk_front_find(&f, "todo", SK_ROLE_WIDGET, 0);
    CHECK_STR(sk_test_prop(sk_front_style(&f, list, &off, 1), SK_PROP_DISPLAY), "none");
    /* One row stands for all: its ordinal, its state. */
    const sk_el *item = sk_front_find(&f, "todo", SK_ROLE_ITEM, 0);
    const sk_el *mark = sk_front_find(&f, "todo", SK_ROLE_ITEM_MARK, 0);
    const sk_el *text = sk_front_find(&f, "todo", SK_ROLE_ITEM_TEXT, 0);
    CHECK(item && item->repeated && text && text->before);
    sk_assign done = {item, SK_VAR_ITEM, 2};
    CHECK_STR(sk_test_prop(sk_front_style(&f, mark, &done, 1), SK_PROP_COLOR), "#ff0000");
    CHECK_STR(sk_test_prop(sk_front_style(&f, text->before, NULL, 0), SK_PROP_CONTENT), "ordinal(12 + 2 * row) \". \"");
    sk_front_free(&f);
    sk_diags_free(&d);

    /* A row's place among the rows is unknown. */
    CHECK(!sk_test_compile(&f, &d, NULL, widgets, ".tl .item:first-child { color: red }\n"));
    CHECK(sk_test_said(&d, "a selector counts a list's rows or reaches into them by position"));
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_segments_and_limits(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets =
        "[{\"id\": \"b\", \"type\": \"bar\", \"bind\": \"cache.hit_ratio\", \"segments\": 4, \"class\": \"bb\", "
        "\"rect\": {\"left\": 0, \"top\": 60, \"width\": 100, \"height\": 30}},\n"
        " {\"id\": \"l\", \"type\": \"limits\", \"bind\": \"limits.items\", \"segments\": 2, \"itemClass\": \"m\", "
        "\"class\": \"ll\", \"rect\": {\"left\": 200, \"top\": 60, \"width\": 200, \"height\": 100}}]";
    COMPILES(&f, &d, NULL, widgets,
                      ".bb .seg:nth-child(n+3).on { background-color: red }\n"
                      ".ll.n-3 > :last-child { grid-column: span 2 }\n"
                      ".ll.n-3 { display: grid }\n"
                      ".ll .m.span-5h .seg.on { background-color: blue }\n"
                      ".ll .m.model { color: green }\n");
    const sk_el *s1 = sk_front_find(&f, "b", SK_ROLE_SEG, 0), *s3 = sk_front_find(&f, "b", SK_ROLE_SEG, 2);
    sk_assign on1 = {s1, SK_VAR_ON, 1}, on3 = {s3, SK_VAR_ON, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, s1, &on1, 1), SK_PROP_BACKGROUND_COLOR), "#4fd06a");
    CHECK_STR(sk_test_prop(sk_front_style(&f, s3, &on3, 1), SK_PROP_BACKGROUND_COLOR), "#ff0000");
    CHECK_STR(sk_test_prop(sk_front_style(&f, s3, NULL, 0), SK_PROP_BACKGROUND_COLOR), "#20302a");
    /* The limits widget's style per count; its meters per count too. */
    const sk_el *l = sk_front_find(&f, "l", SK_ROLE_WIDGET, 0);
    sk_assign c0 = {l, SK_VAR_COUNT, 0}, c2 = {l, SK_VAR_COUNT, 2}, c3 = {l, SK_VAR_COUNT, 3};
    CHECK_STR(sk_test_prop(sk_front_style(&f, l, &c0, 1), SK_PROP_DISPLAY), "none");
    CHECK_STR(sk_test_prop(sk_front_style(&f, l, &c2, 1), SK_PROP_DISPLAY), "flex");
    CHECK_STR(sk_test_prop(sk_front_style(&f, l, &c3, 1), SK_PROP_DISPLAY), "grid");
    /* Meters 1 (count 1), 2..3 (count 2), 4..6 (count 3). */
    const sk_el *m6 = sk_front_find(&f, "l", SK_ROLE_METER, 5), *m5 = sk_front_find(&f, "l", SK_ROLE_METER, 4);
    const sk_el *m1 = sk_front_find(&f, "l", SK_ROLE_METER, 0);
    CHECK(m6 && m6->group == 3 && m6->meter == 3 && m1->group == 1);
    CHECK_STR(sk_test_prop(sk_front_style(&f, m6, NULL, 0), SK_PROP_GRID_COLUMN_START), "span 2");
    CHECK_STR(sk_test_prop(sk_front_style(&f, m5, NULL, 0), SK_PROP_GRID_COLUMN_START), "auto");
    CHECK_STR(sk_test_prop(sk_front_style(&f, m1, NULL, 0), SK_PROP_GRID_COLUMN_START), "auto");
    /* The model and the window's span are a meter's state. */
    sk_assign model = {m1, SK_VAR_MODEL, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, m1, &model, 1), SK_PROP_COLOR), "#008000");
    CHECK_INT(f.spans.len, 1);
    const sk_el *seg = sk_front_find(&f, "l", SK_ROLE_SEG, 0);
    sk_assign span[2] = {{m1, SK_VAR_SPAN, 1}, {seg, SK_VAR_ON, 1}};
    CHECK_STR(sk_test_prop(sk_front_style(&f, seg, span, 2), SK_PROP_BACKGROUND_COLOR), "#0000ff");
    span[0].value = 0; /* .spend */
    CHECK_STR(sk_test_prop(sk_front_style(&f, seg, span, 2), SK_PROP_BACKGROUND_COLOR), "#4fd06a");
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_counters(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *panels = "[{\"class\": \"c\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 10, \"height\": 10}},"
                         " {\"class\": \"c\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 10, \"height\": 10}},"
                         " {\"class\": \"c z\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 10, \"height\": 10}}]";
    COMPILES(&f, &d, panels, NULL,
                      ".skin { counter-reset: n 4 }\n"
                      ".c { counter-increment: n }\n"
                      ".c::before { content: \"#\" counter(n) }\n"
                      ".z { display: none }\n");
    const sk_el *stage = f.els.ptr[0]->last;
    const sk_el *c1 = stage->first, *c2 = c1->next;
    CHECK_STR(sk_test_prop(sk_front_style(&f, c1->before, NULL, 0), SK_PROP_CONTENT), "\"#\" \"5\"");
    CHECK_STR(sk_test_prop(sk_front_style(&f, c2->before, NULL, 0), SK_PROP_CONTENT), "\"#\" \"6\"");
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_fonts_resolved(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *dir = sk_test_skin(PANEL_PQ, NULL,
                               ".p { font: bold 12px \"My Face\", monospace }\n"
                               ".q::after { content: \"\"; font: italic 12px nothing, monospace }\n"
                               ".q::before { content: \"\"; font-family: serif; font-weight: 900 }\n");
    sk_test_write(dir, "f.ttf", "not read by the front end");
    /* Rewrite the manifest with a font. */
    sk_test_write(dir, "skin.json",
              "{\"name\": \"test\", \"window\": {\"width\": 1000, \"height\": 700, \"minWidth\": 800, \"minHeight\": "
              "500},\n"
              " \"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 24, \"height\": 24}, \"image\": "
              "\"menu.svg\"},\n"
              " \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, \"right\": 10, \"bottom\": 10}, \"theme\": "
              "{\"foreground\": \"#e0e0e0\", \"background\": \"#000000\"}},\n"
              " \"fonts\": [{\"family\": \"My Face\", \"file\": \"f.ttf\"}],\n"
              " \"stylesheet\": \"skin.css\",\n"
              " \"panels\": [{\"class\": \"p q\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 100, \"height\": "
              "50}}]}\n");
    CHECK(sk_front_run(&f, dir, &d));
    const sk_el *p = sk_front_panel(&f, "p");
    const sk_css_style *s = sk_front_style(&f, p, NULL, 0);
    /* The skin's face, bold made up: the pack's faces follow the six
     * embedded DejaVu faces. */
    CHECK_INT(s->font, 6);
    CHECK_INT(s->font_synth, 1);
    /* No such family: monospace is DejaVu Sans Mono, Oblique for italic. */
    const sk_css_style *a = sk_front_style(&f, p->after, NULL, 0);
    CHECK_INT(a->font, 2);
    CHECK_INT(a->font_synth, 0);
    /* serif is DejaVu Sans; 900 takes its bold. */
    const sk_css_style *b = sk_front_style(&f, p->before, NULL, 0);
    CHECK_INT(b->font, 5);
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_errors_with_lines(void)
{
    sk_front f;
    sk_diags d = {0};
    CHECK(!sk_test_compile(&f, &d, PANEL_PQ, NULL,
                       ".p { colour: red }\n"
                       ".p { width: wide }\n"
                       ".p { animation: nowhere 1s }\n"
                       ".p { border-style: dashed }\n"
                       "@keyframes k { from { color: red } }\n"));
    CHECK(sk_test_said(&d, "skin.css: line 1: the property \"colour\" is not supported"));
    CHECK(sk_test_said(&d, "skin.css: line 2: \"width\": \"wide\" is not a valid value"));
    CHECK(sk_test_said(&d, "skin.css: line 3: \"animation\": \"nowhere 1s\" is not a valid value: no @keyframes has that name"));
    CHECK(sk_test_said(&d, "skin.css: line 4: \"border-style\": \"dashed\" is not a valid value: only solid lines are supported"));
    CHECK(sk_test_said(&d, "skin.css: line 5: a keyframe may set transform and opacity only, not \"color\""));
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_too_many_states(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets = "[{\"id\": \"b\", \"type\": \"bar\", \"bind\": \"cache.hit_ratio\", \"segments\": 12, "
                          "\"class\": \"bb\", \"rect\": {\"left\": 0, \"top\": 60, \"width\": 100, \"height\": 30}}]";
    CHECK(!sk_test_compile(&f, &d, NULL, widgets, ".bb:has(.seg.on) .label { color: red }\n.bb:has(.seg.on) { color: red }\n"));
    CHECK(sk_test_said(&d, "widget \"b\" can reach too many states: it depends on nodata on on on"));
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_keyframes_and_animation(void)
{
    sk_front f;
    sk_diags d = {0};
    COMPILES(&f, &d, PANEL_PQ, NULL,
                      "@keyframes rise { 0% { transform: translateY(0) } 45% { transform: translateY(-50px); opacity: "
                      "0.5 } 100%, 0% { opacity: 1 } }\n"
                      "@keyframes rise { to { transform: rotate(360deg) } }\n"
                      ".p { animation: rise 1.8s linear infinite; animation-play-state: paused }\n");
    /* The last rule of a name wins. */
    CHECK_INT(f.keyframes.len, 1);
    CHECK_INT(f.keyframes.ptr[0].nkeys, 1);
    CHECK(f.keyframes.ptr[0].keys[0].offset == 1 && f.keyframes.ptr[0].keys[0].has_transform);
    const sk_css_style *s = panel_style(&f);
    CHECK_STR(sk_test_prop(s, SK_PROP_ANIMATION_NAME), "keyframes(0)");
    CHECK_STR(sk_test_prop(s, SK_PROP_ANIMATION_PLAY_STATE), "paused");
    sk_css_anim a = sk_style_anim(s, 0);
    CHECK(a.duration > 1.79f && a.duration < 1.81f && a.paused);
    sk_front_free(&f);
    sk_diags_free(&d);

    /* Frames at one offset merge. */
    COMPILES(&f, &d, PANEL_PQ, NULL,
                      "@keyframes m { 0% { transform: scale(2) } 50% { opacity: 0 } from { opacity: 1 } }\n");
    CHECK_INT(f.keyframes.ptr[0].nkeys, 2);
    CHECK(f.keyframes.ptr[0].keys[0].has_transform && f.keyframes.ptr[0].keys[0].has_opacity);
    CHECK(f.keyframes.ptr[0].keys[1].offset == 0.5f);
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_blockified(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets = "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"label\": \"c\", "
                          "\"resets\": \"limits.five_hour_resets_at\", \"rect\": {\"left\": 0, \"top\": 60, \"width\": "
                          "100, \"height\": 30}}]";
    COMPILES(&f, &d, PANEL_PQ, widgets,
             ".p::after { content: \"\"; position: absolute; float: left }\n"
             ".p::before { content: \"\"; float: right }\n"
             ".w .label { display: inline }\n"
             ".resets .left { float: left }\n");
    const sk_el *p = sk_front_panel(&f, "p");
    const sk_css_style *a = sk_front_style(&f, p->after, NULL, 0), *b = sk_front_style(&f, p->before, NULL, 0);
    CHECK_STR(sk_test_prop(a, SK_PROP_DISPLAY), "block");
    CHECK_STR(sk_test_prop(a, SK_PROP_FLOAT), "none");
    CHECK_STR(sk_test_prop(b, SK_PROP_DISPLAY), "block");
    CHECK_STR(sk_test_prop(b, SK_PROP_FLOAT), "right");
    /* A flex container's child is a flex item, block-level. */
    CHECK_STR(sk_test_prop(sk_front_style(&f, sk_front_find(&f, "r", SK_ROLE_LABEL, 0), NULL, 0), SK_PROP_DISPLAY), "block");
    /* A span in a block stays inline, unless it floats. */
    CHECK_STR(sk_test_prop(sk_front_style(&f, sk_front_find(&f, "r", SK_ROLE_RESETS_WORD, 0), NULL, 0), SK_PROP_DISPLAY),
              "inline");
    CHECK_STR(sk_test_prop(sk_front_style(&f, sk_front_find(&f, "r", SK_ROLE_RESETS_LEFT, 0), NULL, 0), SK_PROP_DISPLAY),
              "block");
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_layout_refusals(void)
{
    sk_front f;
    sk_diags d = {0};
    const char *widgets = "[{\"id\": \"r\", \"type\": \"readout\", \"bind\": \"cost.usd\", \"label\": \"c\", \"rect\": "
                          "{\"left\": 0, \"top\": 60, \"width\": 100, \"height\": 30}}]";
    /* A flex item takes margins; an inline-block takes a box; auto
     * vertical margins are 0. */
    COMPILES(&f, &d, PANEL_PQ, widgets,
             ".w .label { margin-top: 2px }\n"
             ".p::before { content: \"x\"; display: inline-block; padding: 2px }\n"
             ".p::after { content: \"\"; display: block; margin: auto }\n");
    sk_front_free(&f);
    sk_diags_free(&d);
    CHECK(!sk_test_compile(&f, &d, PANEL_PQ, NULL, ".p::before { content: \"x\"; padding: 2px }\n"));
    CHECK(sk_test_said(&d, "skin.css: (layout): ::before of panel \"p q\": an inline box takes text properties only"));
    sk_front_free(&f);
    sk_diags_free(&d);
    CHECK(!sk_test_compile(&f, &d, PANEL_PQ, NULL, ".p::after { content: \"\"; display: block; margin-top: 3px }\n"));
    CHECK(sk_test_said(&d, "skin.css: (layout): ::after of panel \"p q\": a block in block flow takes no vertical margin"));
    sk_front_free(&f);
    sk_diags_free(&d);
}

static void test_app_declarations_from_the_manifest(void)
{
    /* A sheet's name goes into the app's own declarations quoted and
     * escaped; a negative gap is left out, as the browser drops it. */
    const char *widgets = "[{\"id\": \"s\", \"type\": \"sprite\", \"bind\": \"state.busy\", \"sheet\": \"a\\\"b.png\", "
                          "\"frames\": 2, \"frameWidth\": 1, \"frameHeight\": 1, \"rect\": {\"left\": 0, \"top\": 60, "
                          "\"width\": 10, \"height\": 10}},\n"
                          " {\"id\": \"b\", \"type\": \"bar\", \"bind\": \"cache.hit_ratio\", \"segments\": 2, \"gap\": "
                          "-3, \"rect\": {\"left\": 0, \"top\": 80, \"width\": 10, \"height\": 10}}]";
    const char *dir = sk_test_skin(NULL, widgets, "");
    /* A PNG's signature and IHDR, 2 x 1, all validate.js reads. */
    static const unsigned char png[24] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13,
                                          'I',  'H', 'D', 'R', 0,    0,    0,    2,    0, 0, 0, 1};
    sk_test_write_bytes(dir, "a\"b.png", png, sizeof png);
    sk_front f;
    sk_diags d = {0};
    bool ok = sk_front_run(&f, dir, &d);
    CHECK(ok);
    for (size_t i = 0; !ok && i < d.items.len; i++)
        fprintf(stderr, "      %s\n", d.items.ptr[i]);
    const sk_css_style *sheet = sk_front_style(&f, sk_front_find(&f, "s", SK_ROLE_SHEET, 0), NULL, 0);
    CHECK(sheet && sheet->nbg_image == 1 && sheet->bg_image[0].kind == SK_IMAGE_URL);
    if (sheet && sheet->nbg_image == 1)
        CHECK_STR(f.images.ptr[sheet->bg_image[0].source].path, "a\"b.png");
    CHECK_STR(sk_test_prop(sheet, SK_PROP_BACKGROUND_SIZE), "200% 100%");
    CHECK_STR(sk_test_prop(sheet, SK_PROP_BACKGROUND_POSITION), "left value() top 0px");
    const sk_css_style *track = sk_front_style(&f, sk_front_find(&f, "b", SK_ROLE_TRACK, 0), NULL, 0);
    CHECK_STR(sk_test_prop(track, SK_PROP_DISPLAY), "flex");
    CHECK_STR(sk_test_prop(track, SK_PROP_COLUMN_GAP), "normal");
    sk_front_free(&f);
    sk_diags_free(&d);
}

const sk_test sk_suite_skinc_cascade[] = {
    T(test_app_declarations_from_the_manifest),
    T(test_blockified),
    T(test_layout_refusals),
    T(test_order_and_specificity), T(test_inheritance_em_currentcolor), T(test_custom_properties), T(test_custom_properties_are_bounded),
    T(test_states_of_a_widget),    T(test_stage_and_list),              T(test_segments_and_limits),
    T(test_counters),              T(test_fonts_resolved),              T(test_errors_with_lines),
    T(test_too_many_states),       T(test_keyframes_and_animation),     {0},
};
