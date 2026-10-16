/* The four shipped skins drawn whole at 1x and in part at 2x, in their
 * default state, against golden images: what tools/skinshot draws, so
 * a change to the layout or the paint shows in every skin. A blur, a
 * glyph or a resampled image may round a little differently on another
 * machine, so a few pixels in ten thousand may miss by more than 24. */
#include "../../tools/shot.h"
#include "../golden.h"
#include "../test.h"

static void shoot(const char *name, sk_frect part)
{
    char dir[64], golden[64];
    snprintf(dir, sizeof dir, "skins/%s", name);
    sk_diags d = {0};
    sk_shot s;
    if (!sk_shot_load(&s, dir, NULL, &d)) {
        for (size_t i = 0; i < d.items.len; i++)
            sk_test_fail(__FILE__, __LINE__, "%s", d.items.ptr[i]);
        sk_diags_free(&d);
        return;
    }
    sk_diags_free(&d);
    sk_image im;
    sk_shot_paint(&s, 1, true, &im);
    snprintf(golden, sizeof golden, "skin_%s_1x", name);
    CHECK_GOLDEN_NEAR(golden, &im, 24, 300);
    sk_image_free(&im);
    sk_shot_paint_area(&s, part, 2, true, &im);
    snprintf(golden, sizeof golden, "skin_%s_2x", name);
    CHECK_GOLDEN_NEAR(golden, &im, 24, 300);
    sk_image_free(&im);
    sk_shot_free(&s);
}

static void test_deck(void)
{
    /* The dials and the sliders. */
    shoot("deck", (sk_frect){570, 0, 400, 170});
}

static void test_phosphor(void)
{
    /* The context gauge, its glow and its readouts. */
    shoot("phosphor", (sk_frect){0, 0, 320, 400});
}

static void test_library(void)
{
    /* The lamp, the brass knob and the shelf's end. */
    shoot("library", (sk_frect){940, 0, 300, 140});
}

static void test_coffee(void)
{
    /* The corkboard, its handbill and the lava lamp. */
    shoot("coffee", (sk_frect){880, 0, 360, 180});
}

const sk_test sk_suite_skin_skins[] = {
    T(test_deck),
    T(test_phosphor),
    T(test_library),
    T(test_coffee),
    {0},
};
