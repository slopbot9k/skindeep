/* SGR as xterm.js's charAttributes and _extractColor read it: the
 * attribute bits, the colours by ; and by :, underline styles and their
 * colour, and the bits xterm.js leaves behind. */
#include "screen.h"

static sk_term_cell after(const char *sgr)
{
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, sgr);
    tt_feed(&x, "X");
    sk_term_cell c = tt_cell(x.t, 0, 0);
    tt_close(&x);
    return c;
}

#define P16(n) (SK_ATTR_CM_P16 | (n))
#define P256(n) (SK_ATTR_CM_P256 | (n))
#define RGB(n) (SK_ATTR_CM_RGB | (n))

static void test_flags(void)
{
    sk_term_cell c = after("\x1b[1;2;3;4;5;7;8;9;53m");
    CHECK_INT(c.fg, SK_FG_BOLD | SK_FG_UNDERLINE | SK_FG_BLINK | SK_FG_INVERSE | SK_FG_INVISIBLE | SK_FG_STRIKETHROUGH);
    CHECK_INT(c.bg, SK_BG_ITALIC | SK_BG_DIM | SK_BG_OVERLINE | SK_BG_HAS_EXTENDED);
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_SINGLE);
    c = after("\x1b[1;2;3;4;5;7;8;9;53m\x1b[22;23;24;25;27;28;29;55m");
    CHECK_INT(c.fg, 0);
    CHECK_INT(c.bg, 0);
    c = after("\x1b[1;31;44m\x1b[m");
    CHECK_INT(c.fg, 0);
    CHECK_INT(c.bg, 0);
}

static void test_palette_colors(void)
{
    CHECK_INT(after("\x1b[31m").fg, P16(1));
    CHECK_INT(after("\x1b[97m").fg, P16(15));
    CHECK_INT(after("\x1b[42m").bg, P16(2));
    CHECK_INT(after("\x1b[103m").bg, P16(11));
    CHECK_INT(after("\x1b[38;5;196m").fg, P256(196));
    CHECK_INT(after("\x1b[48;5;21m").bg, P256(21));
    CHECK_INT(after("\x1b[38:5:196m").fg, P256(196));
    /* An index past 255 wraps, as & 0xff does. */
    CHECK_INT(after("\x1b[38;5;300m").fg, P256(44));
    /* 39 and 49 drop the colour, not the flags. */
    sk_term_cell c = after("\x1b[1;31;44m\x1b[39;49m");
    CHECK_INT(c.fg, SK_FG_BOLD);
    CHECK_INT(c.bg, 0);
}

static void test_rgb(void)
{
    CHECK_INT(after("\x1b[38;2;255;128;0m").fg, RGB(0xff8000));
    CHECK_INT(after("\x1b[48;2;1;2;3m").bg, RGB(0x010203));
    CHECK_INT(after("\x1b[38:2::255:128:0m").fg, RGB(0xff8000));
    /* Without the colour space slot the first value is taken as it. */
    CHECK_INT(after("\x1b[38:2:255:128:0m").fg, RGB(0x800000));
    /* Short forms fill with 0. */
    CHECK_INT(after("\x1b[38;2;10;20m").fg, RGB(0x0a1400));
    CHECK_INT(after("\x1b[38;5m").fg, P256(0));
    /* What follows a colour is read on. */
    sk_term_cell c = after("\x1b[38;2;1;2;3;1;48;5;9m");
    CHECK_INT(c.fg, SK_FG_BOLD | RGB(0x010203));
    CHECK_INT(c.bg, P256(9));
}

static void test_stale_bits(void)
{
    /* A 256 colour after RGB keeps the RGB's upper bits, as xterm.js. */
    CHECK_INT(after("\x1b[38;2;10;20;30m\x1b[38;5;3m").fg, SK_ATTR_CM_P256 | 0x0a1403);
    CHECK_INT(after("\x1b[48;2;10;20;30m\x1b[41m").bg, SK_ATTR_CM_P16 | 0x0a1401);
}

static void test_underline(void)
{
    sk_term_cell c = after("\x1b[4:3m");
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_CURLY);
    c = after("\x1b[21m");
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_DOUBLE);
    c = after("\x1b[4:5m");
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_DASHED);
    /* Out of range, or empty, is single. */
    c = after("\x1b[4:9m");
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_SINGLE);
    c = after("\x1b[4:m");
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_SINGLE);
    c = after("\x1b[4:3m\x1b[4:0m");
    CHECK_INT(sk_term_cell_underline(&c), SK_UNDERLINE_NONE);
    CHECK_INT(c.bg & SK_BG_HAS_EXTENDED, 0);
    c = after("\x1b[4:3m\x1b[24m");
    CHECK_INT(c.fg & SK_FG_UNDERLINE, 0);
}

static void test_underline_color(void)
{
    sk_term_cell c = after("\x1b[4;58;2;0;255;0m");
    CHECK_INT(c.ext & SK_EXT_UNDERLINE_COLOR, RGB(0x00ff00));
    c = after("\x1b[58:5:3;4m");
    CHECK_INT(c.ext & SK_EXT_UNDERLINE_COLOR, P256(3));
    /* A colour alone sets no extended attributes. */
    c = after("\x1b[58;5;3m");
    CHECK_INT(c.bg & SK_BG_HAS_EXTENDED, 0);
    /* 59 sets every colour bit: xterm.js's -1 masked, read back as RGB
     * white. */
    c = after("\x1b[4;58;5;3;59m");
    CHECK_INT(c.ext & SK_EXT_UNDERLINE_COLOR, SK_EXT_UNDERLINE_COLOR);
    /* SGR 0 clears it. */
    c = after("\x1b[4;58;5;3m\x1b[0;4m");
    CHECK_INT(c.ext & SK_EXT_UNDERLINE_COLOR, 0);
}

static void test_restore_keeps_extended(void)
{
    /* DECRC restores fg and bg alone: the extended word stays as it is
     * now, here without a style, under a bg that claims one. */
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "\x1b[4:3m\x1b" "7\x1b[0m\x1b" "8X");
    sk_term_cell c = tt_cell(x.t, 0, 0);
    CHECK(c.bg & SK_BG_HAS_EXTENDED);
    CHECK_INT(c.ext & SK_EXT_UNDERLINE_STYLE, 0);
    tt_close(&x);
}

static void test_erase_colour(void)
{
    /* A scrolled-in line takes the background, nothing else. */
    tt x;
    tt_open(&x, 10, 2);
    tt_feed(&x, "\x1b[1;4;42m\r\n\r\n");
    sk_term_cell c = tt_cell(x.t, 5, 1);
    CHECK_INT(c.bg, P16(2));
    CHECK_INT(c.fg, 0);
    tt_close(&x);
}

const sk_test sk_suite_term_sgr[] = {
    T(test_flags),
    T(test_palette_colors),
    T(test_rgb),
    T(test_stale_bits),
    T(test_underline),
    T(test_underline_color),
    T(test_restore_keeps_extended),
    T(test_erase_colour),
    {0},
};
