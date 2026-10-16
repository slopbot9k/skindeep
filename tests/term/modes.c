/* Modes as xterm.js keeps them: the input modes the encoders read, the
 * alternate screens, the cursor's style and visibility, synchronized
 * output, focus reports, soft and full reset. */
#include "../../src/term/screen.h"
#include "screen.h"

static sk_term_input_modes modes(sk_term *t)
{
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    return m;
}

static void test_input_modes(void)
{
    tt x;
    tt_open(&x, 10, 3);
    sk_term_input_modes m = modes(x.t);
    CHECK(!m.app_cursor && !m.app_keypad && !m.bracketed_paste && !m.focus_events && !m.alt_screen);
    CHECK_INT(m.mouse, SK_MOUSE_NONE);
    CHECK_INT(m.mouse_encoding, SK_MOUSE_ENC_DEFAULT);
    tt_feed(&x, "\x1b[?1h\x1b=\x1b[?2004h\x1b[?1004h");
    m = modes(x.t);
    CHECK(m.app_cursor && m.app_keypad && m.bracketed_paste && m.focus_events);
    tt_feed(&x, "\x1b[?1l\x1b>\x1b[?2004l\x1b[?1004l\x1b[?66h");
    m = modes(x.t);
    CHECK(!m.app_cursor && m.app_keypad && !m.bracketed_paste && !m.focus_events);
    tt_feed(&x, "\x1b[?66l");
    CHECK(!modes(x.t).app_keypad);
    tt_close(&x);
}

static void test_mouse_modes(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?9h");
    CHECK_INT(modes(x.t).mouse, SK_MOUSE_X10);
    tt_feed(&x, "\x1b[?1000h");
    CHECK_INT(modes(x.t).mouse, SK_MOUSE_VT200);
    tt_feed(&x, "\x1b[?1002h");
    CHECK_INT(modes(x.t).mouse, SK_MOUSE_DRAG);
    tt_feed(&x, "\x1b[?1003h");
    CHECK_INT(modes(x.t).mouse, SK_MOUSE_ANY);
    /* Resetting any of the four turns reporting off. */
    tt_feed(&x, "\x1b[?9l");
    CHECK_INT(modes(x.t).mouse, SK_MOUSE_NONE);
    tt_feed(&x, "\x1b[?1006h");
    CHECK_INT(modes(x.t).mouse_encoding, SK_MOUSE_ENC_SGR);
    tt_feed(&x, "\x1b[?1016h");
    CHECK_INT(modes(x.t).mouse_encoding, SK_MOUSE_ENC_SGR_PIXELS);
    /* As does resetting either encoding. */
    tt_feed(&x, "\x1b[?1006l");
    CHECK_INT(modes(x.t).mouse_encoding, SK_MOUSE_ENC_DEFAULT);
    /* 1005 and 1015 are not supported. */
    tt_feed(&x, "\x1b[?1005h\x1b[?1015h");
    CHECK_INT(modes(x.t).mouse_encoding, SK_MOUSE_ENC_DEFAULT);
    tt_close(&x);
}

static void test_mouse_clears_selection(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "hello");
    sk_term_select_start(x.t, SK_SELECT_CHAR, 0, 0);
    sk_term_select_move(x.t, 3, 0);
    CHECK(sk_term_has_selection(x.t));
    tt_feed(&x, "\x1b[?1000h");
    CHECK(!sk_term_has_selection(x.t));
    tt_close(&x);
}

static void test_alt_1049(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "abc\x1b[?1049h");
    CHECK(modes(x.t).alt_screen);
    CHECK_ROWS(x.t, "\n\n");
    CHECK_CURSOR(x.t, 3, 0);
    tt_feed(&x, "xy\x1b[3;1Hz");
    CHECK_ROWS(x.t, "   xy\n\nz");
    tt_feed(&x, "\x1b[?1049l");
    CHECK(!modes(x.t).alt_screen);
    CHECK_ROWS(x.t, "abc\n\n");
    CHECK_CURSOR(x.t, 3, 0);
    /* The alternate screen starts blank each time. */
    tt_feed(&x, "\x1b[?1049h");
    CHECK_ROWS(x.t, "\n\n");
    tt_close(&x);
}

static void test_alt_47(void)
{
    /* Without the cursor save, the cursor carries both ways. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "abc\x1b[?47h\x1b[2;5H\x1b[?47l");
    CHECK_CURSOR(x.t, 4, 1);
    tt_feed(&x, "\x1b[?1047h\x1b[3;2H\x1b[?1047l");
    CHECK_CURSOR(x.t, 1, 2);
    tt_feed(&x, "\x1b[1;7H\x1b[?1048h\x1b[H\x1b[?1048l");
    CHECK_CURSOR(x.t, 6, 0);
    tt_close(&x);
}

static void test_alt_fill_and_scroll(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[44m\x1b[?1049h\x1b[0m");
    CHECK_INT(tt_cell(x.t, 5, 2).bg, SK_ATTR_CM_P16 | 4);
    /* No scrollback in the alternate buffer. */
    tt_feed(&x, "1\r\n2\r\n3\r\n4");
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(base, 0);
    CHECK_INT(lines, 3);
    CHECK_ROWS(x.t, "2\n3\n4");
    tt_close(&x);
}

static void test_cursor_style(void)
{
    tt x;
    tt_open(&x, 10, 3);
    sk_term_cursor c;
    sk_term_cursor_of(x.t, &c);
    CHECK(!c.hidden && c.style == SK_CURSOR_BLOCK && !c.blink && !c.initialized);
    tt_feed(&x, "\x1b[?25l\x1b[3 q");
    sk_term_cursor_of(x.t, &c);
    CHECK(c.hidden && c.style == SK_CURSOR_UNDERLINE && c.blink);
    tt_feed(&x, "\x1b[?25h\x1b[6 q");
    sk_term_cursor_of(x.t, &c);
    CHECK(!c.hidden && c.style == SK_CURSOR_BAR && !c.blink);
    /* An unknown style keeps the shape and takes the blink. */
    tt_feed(&x, "\x1b[7 q");
    sk_term_cursor_of(x.t, &c);
    CHECK(c.style == SK_CURSOR_BAR && c.blink);
    /* No parameter is 0: back to the options. */
    tt_feed(&x, "\x1b[ q");
    sk_term_cursor_of(x.t, &c);
    CHECK(c.style == SK_CURSOR_BLOCK && !c.blink);
    /* DECSET 12 is the blink option. */
    tt_feed(&x, "\x1b[?12h");
    sk_term_cursor_of(x.t, &c);
    CHECK(c.blink);
    tt_feed(&x, "\x1b[2 q");
    sk_term_cursor_of(x.t, &c);
    CHECK(!c.blink);
    /* A buffer switch makes the cursor drawable. */
    tt_feed(&x, "\x1b[?1049h\x1b[?1049l");
    sk_term_cursor_of(x.t, &c);
    CHECK(c.initialized);
    tt_close(&x);
}

static void test_sync_output(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?2026h");
    CHECK(sk_term_synchronized(x.t));
    tt_feed(&x, "\x1b[?2026l");
    CHECK(!sk_term_synchronized(x.t));
    tt_feed(&x, "\x1b[?2026h");
    sk_term_end_sync(x.t);
    CHECK(!sk_term_synchronized(x.t));
    tt_close(&x);
}

static void test_focus_report(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?1004h");
    CHECK_REPLY(&x, "\x1b[O");
    sk_term_set_focus(x.t, true);
    tt_feed(&x, "\x1b[?1004h");
    CHECK_REPLY(&x, "\x1b[I");
    tt_feed(&x, "\x1b[?1004l");
    CHECK_REPLY(&x, "");
    tt_close(&x);
}

static void test_soft_reset(void)
{
    tt x;
    tt_open(&x, 10, 4);
    tt_feed(&x, "\x1b[?25l\x1b[?1h\x1b[?2004h\x1b[4h\x1b[?7l\x1b[2;3r\x1b[?6h\x1b[31m\x1b(0\x1b[?1000h");
    tt_feed(&x, "\x1b[!p");
    sk_term_cursor c;
    sk_term_cursor_of(x.t, &c);
    CHECK(!c.hidden);
    sk_term_input_modes m = modes(x.t);
    CHECK(!m.app_cursor && !m.bracketed_paste);
    /* The mouse is not the core's: it stays. */
    CHECK_INT(m.mouse, SK_MOUSE_VT200);
    CHECK(x.t->wraparound && !x.t->insert_mode && !x.t->origin);
    CHECK(x.t->active->scroll_top == 0 && x.t->active->scroll_bottom == 3);
    tt_feed(&x, "q");
    sk_term_cell cell = tt_cell(x.t, 0, 1);
    CHECK_INT(cell.cp, 'q');
    CHECK_INT(cell.fg, 0);
    tt_close(&x);
}

static void test_full_reset(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "1\r\n2\r\n3\r\n4\x1b[?25l\x1b[?2004h\x1b[20h\x1b[?1000h\x1b[31m\x1b" "c");
    CHECK_ROWS(x.t, "\n\n");
    CHECK_CURSOR(x.t, 0, 0);
    int top, base, lines;
    sk_term_viewport(x.t, &top, &base, &lines);
    CHECK_INT(lines, 3);
    sk_term_input_modes m = modes(x.t);
    CHECK(!m.bracketed_paste);
    CHECK_INT(m.mouse, SK_MOUSE_NONE);
    /* The cursor stays hidden, and LNM, an option, stays set. */
    sk_term_cursor c;
    sk_term_cursor_of(x.t, &c);
    CHECK(c.hidden);
    tt_feed(&x, "a\nb");
    CHECK_ROWS(x.t, "a\nb\n");
    CHECK_INT(tt_cell(x.t, 0, 0).fg, 0);
    tt_close(&x);
}

static void test_full_reset_in_alt(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "abc\x1b[?1049hxyz\x1b" "cq");
    CHECK(!modes(x.t).alt_screen);
    CHECK_ROWS(x.t, "q\n\n");
    tt_close(&x);
}

const sk_test sk_suite_term_modes[] = {
    T(test_input_modes),
    T(test_mouse_modes),
    T(test_mouse_clears_selection),
    T(test_alt_1049),
    T(test_alt_47),
    T(test_alt_fill_and_scroll),
    T(test_cursor_style),
    T(test_sync_output),
    T(test_focus_report),
    T(test_soft_reset),
    T(test_full_reset),
    T(test_full_reset_in_alt),
    {0},
};
