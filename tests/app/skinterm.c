/* A skin's terminal as the terminals take it (src/app/skinterm.h): the
 * theme's colours over the defaults, the selection and cursor colours,
 * the font size the setting overrides and the skin's face first. This
 * file includes what the window's code includes, the runtime's headers
 * beside platform/ui.h, so a clash between them fails the build. */
#include "../../src/app/skins.h"
#include "../../src/app/skinterm.h"
#include "../../src/platform/ui.h"
#include "../test.h"

static void test_style(void)
{
    sk_skins *r = sk_skins_new(NULL);
    sk_buf err = {0};
    CHECK(sk_skins_start(r, "coffee", &err));
    sk_skin_terminal t;
    sk_skinpack_terminal(sk_skins_current(r), &t);
    sk_term_palette pal;
    sk_termview_style st;
    sk_termview_default_style(&st);
    st.scale = 2;
    sk_skin_term_style(&t, 0, &pal, &st);
    CHECK_INT(pal.fg, 0xf3e6cc);
    CHECK_INT(pal.bg, 0x1d140f);
    CHECK_INT(pal.cursor, 0xe8a85a);
    CHECK_INT(pal.ansi[1], 0xe77d68);
    CHECK_INT(pal.ansi[15], 0xfff8ea);
    /* The 256 colours past the theme's 16 are xterm's. */
    CHECK_INT(pal.ansi[16], 0x000000);
    CHECK_INT(pal.ansi[231], 0xffffff);
    CHECK_INT(st.selection_bg, 0xff4a3424);
    CHECK_INT(st.selection_inactive_bg, 0xff4a3424);
    CHECK_INT(st.selection_fg, 0);
    CHECK_INT(st.cursor_accent, 0xff1d140f);
    CHECK(st.font_px == 14 && st.scale == 2);
    sk_skin_term_style(&t, 16, &pal, &st);
    CHECK(st.font_px == 16);
    sk_fontset *fs = sk_skin_term_fonts(&t, NULL);
    CHECK(sk_fontset_primary(fs, SK_STYLE_REGULAR) == sk_font_embedded(SK_FONT_MONO));
    sk_fontset_free(fs);
    /* phosphor's VT323, the pack's own face. */
    CHECK(sk_skins_select(r, "phosphor", &err));
    sk_skinpack_terminal(sk_skins_current(r), &t);
    fs = sk_skin_term_fonts(&t, NULL);
    CHECK(t.face && sk_fontset_primary(fs, SK_STYLE_REGULAR) == t.face);
    sk_fontset_free(fs);
    sk_skin_term_style(&t, 0, &pal, &st);
    CHECK(st.font_px == 19);
    sk_buf_free(&err);
    sk_skins_free(r);
}

/* What the window draws with: its size, the terminal's place, the
 * cursor's shape mapped to the window's. */
static void test_window(void)
{
    sk_skins *r = sk_skins_new(NULL);
    sk_buf err = {0};
    CHECK(sk_skins_start(r, NULL, &err));
    sk_skin_window w;
    sk_skinpack_window(sk_skins_current(r), &w);
    CHECK_INT(w.width, 1240);
    CHECK_INT(w.height, 820);
    CHECK(w.min_width < w.width && w.min_height < w.height);
    sk_skinview *v = sk_skinview_new(sk_skins_current(r), 0);
    sk_skinview_set_stage(v, 0, 22, w.width, w.height - 22, 2);
    sk_skinview_step(v, 16);
    sk_image frame;
    sk_image_init(&frame, w.width * 2, w.height * 2);
    sk_rect rects[8];
    CHECK(sk_skinview_draw(v, &frame, true, rects, 8) > 0);
    sk_rect term = sk_skinview_terminal_px(v);
    CHECK(term.w > 0 && term.h > 0 && term.y > 44);
    sk_skin_event ev = sk_skinview_mouse(v, SK_SKIN_MOUSE_MOVE, term.x / 2.0 + 20, term.y / 2.0 + 20, 0);
    CHECK_INT(ev.action, SK_SKIN_NOTHING);
    CHECK_INT(sk_skinview_cursor(v), SK_SKIN_CURSOR_NONE);
    sk_cursor c = SK_CURSOR_IBEAM;
    CHECK(c == SK_CURSOR_IBEAM);
    sk_image_free(&frame);
    sk_skinview_free(v);
    sk_buf_free(&err);
    sk_skins_free(r);
}

const sk_test sk_suite_skinterm[] = {
    T(test_style),
    T(test_window),
    {0},
};
