/* Streams like the ones Claude Code and Codex write, kept as golden
 * screens: Ink's redraw of its status lines in place inside synchronized
 * output, a scroll-region history insert under an inline viewport, an
 * alternate-screen pager, truecolor and 256 colours, OSC 8 links, the
 * cursor hidden and shown, bracketed paste and focus toggled, wide CJK
 * and emoji. Each stream must also come out the same however it is cut
 * into feeds. The streams are written by hand from what those programs
 * send, not recorded from them. */
#include "../../src/term/screen.h"
#include "screen.h"

#include <pthread.h>

#define ORANGE "\x1b[38;2;215;119;87m"
#define GRAY "\x1b[38;2;153;153;153m"
#define DEF "\x1b[39m"

/* Claude Code: the welcome box, a prompt, a turn with a spinner redrawn
 * in place, a tool call with a linked path, a diff, and the prompt box
 * at the bottom with the cursor shown in it. */
static const char claude[] =
    "\x1b[?2004h\x1b[?1004h\x1b[?25l"
    "\x1b[?2026h"
    ORANGE "\xE2\x95\xAD" "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x95\xAE" DEF "\r\n"
    ORANGE "\xE2\x94\x82" DEF " " ORANGE "\xE2\x9C\xBB" DEF " Welcome to \x1b[1mClaude Code\x1b[22m!      " ORANGE "\xE2\x94\x82" DEF "\r\n"
    ORANGE "\xE2\x94\x82" DEF "                            " ORANGE "\xE2\x94\x82" DEF "\r\n"
    ORANGE "\xE2\x94\x82" DEF "   " GRAY "\x1b[3m/help for help\x1b[23m" DEF "           " ORANGE "\xE2\x94\x82" DEF "\r\n"
    ORANGE "\xE2\x95\xB0" "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x95\xAF" DEF "\r\n\r\n"
    "\x1b[?2026l"
    /* The user's prompt echoed, then the spinner, drawn three times. */
    GRAY "> " DEF "fix the build \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\r\n\r\n"
    "\x1b[?2026h" ORANGE "\xE2\x9C\xBB" DEF " Thinking\xE2\x80\xA6 \x1b[2m(1s \xC2\xB7 esc to interrupt)\x1b[22m\r\n"
    "\x1b[?2026l"
    "\x1b[?2026h\x1b[2K\x1b[1A\x1b[2K\x1b[G" ORANGE "\xE2\x9C\xBD" DEF " Thinking\xE2\x80\xA6 \x1b[2m(2s \xC2\xB7 esc to interrupt)\x1b[22m\r\n"
    "\x1b[?2026l"
    "\x1b[?2026h\x1b[2K\x1b[1A\x1b[2K\x1b[G" ORANGE "\xE2\x9C\xB6" DEF " Thinking\xE2\x80\xA6 \x1b[2m(3s \xC2\xB7 esc to interrupt)\x1b[22m\r\n"
    "\x1b[?2026l"
    /* The answer replaces the spinner. */
    "\x1b[?2026h\x1b[2K\x1b[1A\x1b[2K\x1b[G"
    "\x1b[38;2;255;255;255m\xE2\x8F\xBA" DEF " I'll read \x1b]8;;file:///home/u/proj/src/main.c\x07src/main.c\x1b]8;;\x07 first.\r\n\r\n"
    "\x1b[38;2;78;186;101m\xE2\x8F\xBA" DEF " \x1b[1mRead\x1b[22m(src/main.c)\r\n"
    "  " GRAY "\xE2\x8E\xBF" DEF "  Read \x1b[1m42\x1b[22m lines\r\n\r\n"
    "\x1b[38;2;78;186;101m\xE2\x8F\xBA" DEF " \x1b[1mUpdate\x1b[22m(src/main.c)\r\n"
    "  " GRAY "\xE2\x8E\xBF" DEF "  \x1b[48;2;61;1;0m\x1b[38;2;255;220;220m- int x = 1;\x1b[39m\x1b[49m\r\n"
    "     \x1b[48;2;2;40;0m\x1b[38;2;220;255;220m+ int x = 2;\x1b[39m\x1b[49m\r\n\r\n"
    "\x1b[38;2;255;255;255m\xE2\x8F\xBA" DEF " Done \xE2\x9C\x85 \xF0\x9F\x9A\x80 the \x1b[3mbuild\x1b[23m passes.\r\n\r\n"
    "\x1b[?2026l"
    /* The prompt box, and the cursor in it. */
    "\x1b[?2026h"
    GRAY "\xE2\x95\xAD\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x95\xAE" DEF "\r\n"
    GRAY "\xE2\x94\x82" DEF " > \x1b[7m \x1b[27m     " GRAY "\xE2\x94\x82" DEF "\r\n"
    GRAY "\xE2\x95\xB0\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
    "\xE2\x95\xAF" DEF "\r\n"
    "  " GRAY "? for shortcuts" DEF
    "\x1b[2A\x1b[3G"
    "\x1b[?2026l\x1b[?25h";

/* Codex: a cursor query, the kitty keyboard push xterm.js ignores, an
 * inline viewport at the bottom, history inserted above it through a
 * scroll region, and a redrawn composer. */
static const char codex[] =
    "\x1b[?2004h\x1b[>7u\x1b[?1004h\x1b[6n"
    "\x1b[?25l"
    /* The viewport: rows 9 and 10 of 10. */
    "\x1b[9;1H\x1b[K\x1b[2m\xE2\xA0\x8B\x1b[0m Working (0s \xE2\x80\xA2 Esc to interrupt)"
    "\x1b[10;1H\x1b[K\x1b[7m\xE2\x96\x8C\x1b[0m \x1b[38;5;245mAsk Codex to do anything\x1b[0m"
    /* History above it: a region of rows 1-8, scrolled at its bottom. */
    "\x1b[1;8r\x1b[8;1H\r\n\x1b[1m\xE2\x80\xA2 \x1b[0m\x1b[1mRan\x1b[0m cargo build\r\n"
    "  \xE2\x94\x94 \x1b[32m\xE2\x9C\x94\x1b[0m \x1b[2mfinished in 3.2s\x1b[0m"
    "\x1b[r"
    "\x1b[1;8r\x1b[8;1H\r\n\r\n\x1b[1m\xE2\x80\xA2 \x1b[0mEdited \x1b[36msrc/lib.rs\x1b[0m (\x1b[32m+1\x1b[0m \x1b[31m-1\x1b[0m)"
    "\r\n    \x1b[31m-    let n = 1;\x1b[0m\r\n    \x1b[32m+    let n = \xE4\xBA\x8C;\x1b[0m"
    "\x1b[r"
    /* The spinner and composer redrawn in place. */
    "\x1b[9;1H\x1b[K\x1b[2m\xE2\xA0\x99\x1b[0m Working (1s \xE2\x80\xA2 Esc to interrupt)"
    "\x1b[10;1H\x1b[K\x1b[7m\xE2\x96\x8C\x1b[0m make it \xF0\x9F\x94\xA5 faster"
    "\x1b[10;20H\x1b[?25h";

/* A pager in the alternate screen: its own region, reverse index,
 * insert line, a reverse-video status line; then back to the shell. */
static const char pager[] =
    "$ less notes.txt\r\n"
    "\x1b[?1049h\x1b[22;0;0t\x1b[?1h\x1b=\x1b[H\x1b[2J"
    "line one\r\nline two\r\nline three\r\n\x1b[34m~\x1b[39m\r\n\x1b[34m~\x1b[39m\r\n"
    "\x1b[6;1H\x1b[7mnotes.txt (END)\x1b[27m"
    "\x1b[1;5r\x1b[H\x1bMline zero\x1b[r"
    "\x1b[3;1H\x1b[Linserted\x1b[6;1H\x1b[K\x1b[7m:\x1b[27m"
    "\x1b[6;1H\x1b[K\x1b[?1l\x1b>\x1b[?1049l\x1b[23;0;0t"
    "$ ";

/* Colours and links: a run of the 256 palette, a truecolor ramp, every
 * underline style with a colour, links with and without ids. */
static const char colors[] =
    "\x1b[38;5;16m#\x1b[38;5;52m#\x1b[38;5;88m#\x1b[38;5;124m#\x1b[38;5;160m#\x1b[38;5;196m#"
    "\x1b[48;5;232m \x1b[48;5;244m \x1b[48;5;255m \x1b[0m\r\n"
    "\x1b[38;2;255;0;0m#\x1b[38;2;255;128;0m#\x1b[38;2;255;255;0m#\x1b[38;2;0;255;0m#\x1b[0m\r\n"
    "\x1b[4mone\x1b[4:2mtwo\x1b[4:3;58;2;255;0;255mcurly\x1b[4:4mdot\x1b[4:5;59mdash\x1b[24m\x1b[0m\r\n"
    "\x1b[1;2;3;9;53mmix\x1b[0m \x1b[7;8mhidden\x1b[0m \x1b[5mblink\x1b[0m\r\n"
    "\x1b]8;id=a;https://example.com/a\x1b\\A1\x1b]8;;\x1b\\ \x1b]8;;https://example.com/b\x07" "B\x1b]8;;\x07"
    " \x1b]8;id=a;https://example.com/a\x1b\\A2\x1b]8;;\x1b\\";

static void feed_all(tt *x, const char *s, size_t n)
{
    tt_feedn(x, s, n);
}

/* The same stream fed in pieces of every size up to 7, and by one byte. */
static void check_cuts(const char *s, size_t n, int cols, int rows)
{
    tt whole;
    tt_open(&whole, cols, rows);
    feed_all(&whole, s, n);
    char *want = tt_dump(whole.t);
    char *want_replies = tt_take(&whole);
    for (size_t step = 1; step <= 7; step++) {
        tt x;
        tt_open(&x, cols, rows);
        for (size_t i = 0; i < n; i += step)
            tt_feedn(&x, s + i, SK_MIN(step, n - i));
        char *got = tt_dump(x.t);
        CHECK_STR(got, want);
        char *replies = tt_take(&x);
        CHECK_STR(replies, want_replies);
        free(replies);
        free(got);
        tt_close(&x);
    }
    free(want);
    free(want_replies);
    tt_close(&whole);
}

static void test_claude(void)
{
    tt x;
    tt_open(&x, 40, 14);
    tt_feedn(&x, claude, sizeof claude - 1);
    CHECK_GOLDEN_SCREEN("claude_session", x.t);
    /* DECSET 1004 answered with the focus it found. */
    CHECK_REPLY(&x, "\x1b[O");
    sk_term_input_modes m;
    sk_term_input_modes_of(x.t, &m);
    CHECK(m.bracketed_paste && m.focus_events && !m.alt_screen);
    CHECK(!sk_term_synchronized(x.t));
    char *s = sk_term_screen_text(x.t);
    CHECK(strstr(s, "Done \xE2\x9C\x85 \xF0\x9F\x9A\x80 the build passes.") != NULL);
    CHECK(strstr(s, "Thinking") == NULL);
    free(s);
    check_cuts(claude, sizeof claude - 1, 40, 14);
    tt_close(&x);
}

static void test_codex(void)
{
    tt x;
    tt_open(&x, 40, 10);
    tt_feedn(&x, codex, sizeof codex - 1);
    CHECK_GOLDEN_SCREEN("codex_session", x.t);
    CHECK_REPLY(&x, "\x1b[O\x1b[1;1R");
    check_cuts(codex, sizeof codex - 1, 40, 10);
    tt_close(&x);
}

static void test_pager(void)
{
    tt x;
    tt_open(&x, 30, 6);
    /* Up to the pager's exit, in the alternate screen; then the rest. */
    size_t mid = (size_t)(strstr(pager, "\x1b[6;1H\x1b[K\x1b[?1l") - pager);
    tt_feedn(&x, pager, mid);
    CHECK_GOLDEN_SCREEN("pager_alt", x.t);
    tt_feedn(&x, pager + mid, sizeof pager - 1 - mid);
    CHECK_GOLDEN_SCREEN("pager_done", x.t);
    sk_term_input_modes m;
    sk_term_input_modes_of(x.t, &m);
    CHECK(!m.alt_screen && !m.app_cursor && !m.app_keypad);
    check_cuts(pager, sizeof pager - 1, 30, 6);
    tt_close(&x);
}

static void test_colors(void)
{
    tt x;
    tt_open(&x, 30, 6);
    tt_feedn(&x, colors, sizeof colors - 1);
    CHECK_GOLDEN_SCREEN("colors_links", x.t);
    check_cuts(colors, sizeof colors - 1, 30, 6);
    tt_close(&x);
}

static void test_wrap_and_resize(void)
{
    tt x;
    tt_open(&x, 20, 6);
    tt_feed(&x, "$ echo \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E and a long line that wraps twice over\r\n"
                "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E and a long line that wraps twice over\r\n$ ");
    CHECK_GOLDEN_SCREEN("wrap_20", x.t);
    sk_term_resize(x.t, 13, 6);
    CHECK_GOLDEN_SCREEN("wrap_13", x.t);
    sk_term_resize(x.t, 31, 8);
    CHECK_GOLDEN_SCREEN("wrap_31", x.t);
    tt_close(&x);
}

static void test_dirty(void)
{
    tt x;
    tt_open(&x, 10, 3);
    bool rows[3];
    CHECK(sk_term_take_dirty(x.t, rows));
    CHECK(rows[0] && rows[1] && rows[2]);
    CHECK(!sk_term_take_dirty(x.t, rows));
    tt_feed(&x, "\x1b[2;1Hab");
    CHECK(sk_term_take_dirty(x.t, rows));
    CHECK(!rows[0] && rows[1] && !rows[2]);
    tt_feed(&x, "\x1b[3;1H\n");
    CHECK(sk_term_take_dirty(x.t, rows));
    CHECK(rows[0] && rows[1] && rows[2]);
    sk_term_scroll(x.t, -1);
    CHECK(sk_term_take_dirty(x.t, rows));
    CHECK(rows[0]);
    sk_term_select_all(x.t);
    CHECK(sk_term_take_dirty(x.t, rows));
    CHECK(rows[0] && rows[1] && rows[2]);
    tt_close(&x);
}

typedef struct {
    sk_term *t;
    const char *s;
    size_t n;
} feeder;

static void *feed_thread(void *ud)
{
    feeder *f = ud;
    for (int round = 0; round < 50; round++)
        for (size_t i = 0; i < f->n; i += 13)
            sk_term_feed(f->t, f->s + i, SK_MIN((size_t)13, f->n - i));
    return NULL;
}

static void test_threads(void)
{
    /* The UI reads whole frames under the lock while the core feeds. */
    tt x;
    tt_open(&x, 40, 14);
    feeder f = {x.t, claude, sizeof claude - 1};
    pthread_t th;
    CHECK(pthread_create(&th, NULL, feed_thread, &f) == 0);
    sk_term_cell row[40];
    bool dirty[14];
    for (int frame = 0; frame < 200; frame++) {
        sk_term_lock(x.t);
        sk_term_take_dirty(x.t, dirty);
        for (int y = 0; y < 14; y++)
            sk_term_row(x.t, y, row);
        sk_term_cursor c;
        sk_term_cursor_of(x.t, &c);
        sk_term_unlock(x.t);
    }
    pthread_join(th, NULL);
    char *s = sk_term_screen_text(x.t);
    CHECK(strstr(s, "? for shortcuts") != NULL);
    free(s);
    tt_close(&x);
}

const sk_test sk_suite_term_streams[] = {
    T(test_claude),
    T(test_codex),
    T(test_pager),
    T(test_colors),
    T(test_wrap_and_resize),
    T(test_dirty),
    T(test_threads),
    {0},
};
