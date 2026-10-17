/* The four shipped skins through the front end: each compiles with no
 * message, and what each needs comes out as the browser computes it. */
#include "../test.h"
#include "util.h"

#include <string.h>

static bool compile(sk_front *f, const char *dir)
{
    sk_diags d = {0};
    bool ok = sk_front_run(f, dir, &d);
    for (size_t i = 0; i < d.items.len; i++)
        sk_test_fail(__FILE__, __LINE__, "%s", d.items.ptr[i]);
    sk_diags_free(&d);
    return ok;
}

static void test_deck(void)
{
    sk_front f;
    CHECK(compile(&f, "skins/deck"));
    /* The playlist's rows count themselves: pl-17 is the 17th row. */
    const sk_el *label = sk_front_find(&f, "pl-17", SK_ROLE_LABEL, 0);
    CHECK(label && label->before);
    if (label && label->before)
        CHECK_STR(sk_test_prop(sk_front_style(&f, label->before, NULL, 0), SK_PROP_CONTENT), "\"17\" \". \"");
    /* A row turns blue under the pointer, and dark green while open.
     * Its filter: none loses to widgets.css's .w.click:hover, which is
     * more specific, as it does in the browser: the row brightens. */
    const sk_el *row = sk_front_find(&f, "pl-0", SK_ROLE_WIDGET, 0);
    sk_assign hover = {row, SK_VAR_HOVER, 1}, open = {row, SK_VAR_OPEN, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, row, &hover, 1), SK_PROP_BACKGROUND_COLOR), "#0a2a8a");
    CHECK_STR(sk_test_prop(sk_front_style(&f, row, &hover, 1), SK_PROP_FILTER), "brightness(1.3)");
    CHECK_STR(sk_test_prop(sk_front_style(&f, row, &open, 1), SK_PROP_BACKGROUND_COLOR), "#06301a");
    /* Hidden while the task list shows. */
    const sk_el *stage = f.els.ptr[0]->last;
    sk_assign list = {stage, SK_VAR_LIST, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, row, &list, 1), SK_PROP_VISIBILITY), "hidden");
    /* The analyzer's ninth segment up goes yellow. */
    const sk_el *s8 = sk_front_find(&f, "an-out", SK_ROLE_SEG, 7), *s9 = sk_front_find(&f, "an-out", SK_ROLE_SEG, 8);
    sk_assign on8 = {s8, SK_VAR_ON, 1}, on9 = {s9, SK_VAR_ON, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, s8, &on8, 1), SK_PROP_BACKGROUND_COLOR), "#2ee82e");
    CHECK_STR(sk_test_prop(sk_front_style(&f, s9, &on9, 1), SK_PROP_BACKGROUND_COLOR), "#d8e030");
    /* The title bar's border image and filters. */
    const sk_css_style *t = sk_front_style(&f, sk_front_panel(&f, "titlebar"), NULL, 0);
    CHECK_STR(sk_test_prop(t, SK_PROP_FILTER), "brightness(0.42) contrast(1.3)");
    CHECK_STR(sk_test_prop(t, SK_PROP_BORDER_IMAGE_SLICE), "8 8 8 8 fill");
    CHECK_STR(sk_test_prop(t, SK_PROP_BORDER_TOP_WIDTH), "4px");
    sk_front_free(&f);
}

static void test_phosphor(void)
{
    sk_front f;
    CHECK(compile(&f, "skins/phosphor"));
    const sk_css_style *s = sk_front_style(&f, sk_front_panel(&f, "scanlines"), NULL, 0);
    CHECK_STR(sk_test_prop(s, SK_PROP_ANIMATION_TIMING_FUNCTION), "steps(40, jump-end)");
    CHECK_STR(sk_test_prop(s, SK_PROP_POINTER_EVENTS), "none");
    CHECK_STR(sk_test_prop(s, SK_PROP_Z_INDEX), "5");
    /* Every widget's text glows: .w's text-shadow, inherited. */
    const sk_css_style *v = sk_front_style(&f, sk_front_find(&f, "cell-0", SK_ROLE_VALUE, 0), NULL, 0);
    CHECK_STR(sk_test_prop(v, SK_PROP_TEXT_SHADOW), "0px 0px 5px rgba(58, 255, 126, 0.6)");
    CHECK_STR(sk_test_prop(v, SK_PROP_FONT_SIZE), "26px");
    /* The ::after of a heading floats right. */
    const sk_el *h = sk_front_panel(&f, "h-systems");
    CHECK(h && h->after);
    if (h && h->after)
        CHECK_STR(sk_test_prop(sk_front_style(&f, h->after, NULL, 0), SK_PROP_FLOAT), "right");
    sk_front_free(&f);
}

static void test_coffee(void)
{
    sk_front f;
    CHECK(compile(&f, "skins/coffee"));
    /* The record turns while the terminal prints, paused otherwise. */
    const sk_el *lamp = sk_front_find(&f, "spin", SK_ROLE_LAMP, 0);
    sk_assign on = {lamp, SK_VAR_ON, 1};
    CHECK_STR(sk_test_prop(sk_front_style(&f, lamp, NULL, 0), SK_PROP_ANIMATION_PLAY_STATE), "paused");
    CHECK_STR(sk_test_prop(sk_front_style(&f, lamp, &on, 1), SK_PROP_ANIMATION_PLAY_STATE), "running");
    /* The lava lamp's glow exists only while it is lit (:has()). */
    const sk_el *lava = sk_front_find(&f, "brewing", SK_ROLE_WIDGET, 0);
    const sk_el *lit = sk_front_find(&f, "brewing", SK_ROLE_LAMP, 0);
    sk_assign lon = {lit, SK_VAR_ON, 1};
    CHECK(lava && lava->before);
    if (lava && lava->before) {
        CHECK(sk_front_style(&f, lava->before, NULL, 0)->content_none);
        CHECK(!sk_front_style(&f, lava->before, &lon, 1)->content_none);
    }
    /* Four jars stand smaller. */
    const sk_el *jar = NULL;
    for (int i = 0; (jar = sk_front_find(&f, "lims", SK_ROLE_METER, i)); i++)
        if (jar->group == 4)
            break;
    CHECK(jar != NULL);
    if (jar)
        CHECK_STR(sk_test_prop(sk_front_style(&f, jar, NULL, 0), SK_PROP_TRANSFORM), "scale(0.75, 0.75)");
    /* The handbill's readout is turned. */
    CHECK_STR(sk_test_prop(sk_front_style(&f, sk_front_find(&f, "open", SK_ROLE_WIDGET, 0), NULL, 0), SK_PROP_TRANSFORM),
              "rotate(-3deg)");
    sk_front_free(&f);
}

static void test_library(void)
{
    sk_front f;
    CHECK(compile(&f, "skins/library"));
    /* A model's five-hour limit is orange, a week's green. */
    const sk_el *meter = sk_front_find(&f, "lims", SK_ROLE_METER, 0);
    const sk_el *seg = sk_front_find(&f, "lims", SK_ROLE_SEG, 0);
    uint8_t span5h = 0;
    for (size_t i = 0; i < f.spans.len; i++)
        if (!strcmp(f.spans.ptr[i], "span-5h"))
            span5h = (uint8_t)(i + 1);
    CHECK(span5h != 0);
    sk_assign a[3] = {{meter, SK_VAR_MODEL, 1}, {meter, SK_VAR_SPAN, span5h}, {seg, SK_VAR_ON, 1}};
    CHECK_STR(sk_test_prop(sk_front_style(&f, seg, a, 3), SK_PROP_BACKGROUND_COLOR), "#d0731c");
    a[0].value = 0;
    a[1].value = 0;
    CHECK_STR(sk_test_prop(sk_front_style(&f, seg, a, 3), SK_PROP_BACKGROUND_COLOR), "#2e8b4a");
    /* The gauge's chalk dashes. */
    const sk_css_style *track = sk_front_style(&f, sk_front_find(&f, "ctx", SK_ROLE_ARC_TRACK, 0), NULL, 0);
    CHECK_STR(sk_test_prop(track, SK_PROP_STROKE_DASHARRAY), "0.5px 1.1px");
    CHECK_STR(sk_test_prop(track, SK_PROP_STROKE_WIDTH), "9px");
    sk_front_free(&f);
}

const sk_test sk_suite_skinc_skins[] = {
    T(test_deck), T(test_phosphor), T(test_coffee), T(test_library), {0},
};
