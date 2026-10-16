/* Mouse reports as xterm.js 6.0.0's CoreMouseService made them, for each
 * protocol and encoding, with the cases of its CoreMouseService.test.ts,
 * then the listeners around it: cells from points, held buttons, drags
 * outside the grid, Shift's selection on Linux, and the wheel. */
#include "../../src/term/keys.h"
#include "../test.h"

#define L SK_TERM_BUTTON_LEFT
#define MID SK_TERM_BUTTON_MIDDLE
#define R SK_TERM_BUTTON_RIGHT
#define NB SK_TERM_BUTTON_NONE
#define W SK_TERM_BUTTON_WHEEL
#define UP SK_TERM_MOUSE_UP
#define DN SK_TERM_MOUSE_DOWN
#define MV SK_TERM_MOUSE_MOVE

/* CoreMouseService's test terminal: 300 columns, 100 rows. */
#define COLS 300
#define ROWS 100

static sk_term_mouse_event ev(sk_term_button b, sk_term_mouse_action a)
{
    return (sk_term_mouse_event){.button = b, .action = a};
}

/* A report of e under protocol p and encoding enc, from a fresh state:
 * whether it was taken, and the bytes. */
static bool report(sk_mouse_protocol p, sk_mouse_encoding enc, sk_term_mouse_event e, sk_buf *out)
{
    sk_term_mouse m;
    sk_term_mouse_init(&m);
    sk_term_input_modes modes = {.mouse = p, .mouse_encoding = enc};
    sk_buf_clear(out);
    return sk_term_mouse_report(&m, &modes, COLS, ROWS, e, out);
}

static void test_protocols_take(void)
{
    static const struct {
        sk_term_button b;
        sk_term_mouse_action a;
        bool none, x10, vt200, drag, any;
    } cases[] = {
        {L, DN, false, true, true, true, true},       {L, UP, false, false, true, true, true},
        {L, MV, false, false, false, true, true},     {MID, DN, false, true, true, true, true},
        {R, DN, false, true, true, true, true},       {W, UP, false, false, true, true, true},
        {W, DN, false, false, true, true, true},      {NB, MV, false, false, false, false, true},
        {W, MV, false, false, false, false, false},   {NB, DN, false, false, false, false, false},
        {NB, UP, false, false, false, false, false},
    };
    sk_buf out = {0};
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_term_mouse_event e = ev(cases[i].b, cases[i].a);
        CHECK_INT(report(SK_MOUSE_NONE, SK_MOUSE_ENC_DEFAULT, e, &out), cases[i].none);
        CHECK_INT(report(SK_MOUSE_X10, SK_MOUSE_ENC_DEFAULT, e, &out), cases[i].x10);
        CHECK_INT(report(SK_MOUSE_VT200, SK_MOUSE_ENC_DEFAULT, e, &out), cases[i].vt200);
        CHECK_INT(report(SK_MOUSE_DRAG, SK_MOUSE_ENC_DEFAULT, e, &out), cases[i].drag);
        CHECK_INT(report(SK_MOUSE_ANY, SK_MOUSE_ENC_DEFAULT, e, &out), cases[i].any);
    }
    sk_buf_free(&out);
}

static void test_out_of_grid(void)
{
    sk_buf out = {0};
    int bad[][2] = {{-1, 0}, {COLS, 0}, {500, 0}, {0, -1}, {0, ROWS}, {0, 500}};
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        sk_term_mouse_event e = ev(L, DN);
        e.col = bad[i][0];
        e.row = bad[i][1];
        CHECK(!report(SK_MOUSE_ANY, SK_MOUSE_ENC_SGR, e, &out));
        CHECK_INT(out.len, 0);
    }
    sk_buf_free(&out);
}

/* The default encoding: one byte each, so a column past 222 (from 0) is
 * taken but not reported. */
static void test_default_coordinates(void)
{
    sk_buf out = {0};
    for (int i = 0; i < COLS; i++) {
        sk_term_mouse_event e = ev(L, DN);
        e.col = i;
        CHECK(report(SK_MOUSE_ANY, SK_MOUSE_ENC_DEFAULT, e, &out));
        if (i > 222) {
            CHECK_INT(out.len, 0);
        } else {
            CHECK_INT(out.len, 6);
            CHECK(memcmp(out.ptr, "\x1b[M ", 4) == 0 && out.ptr[5] == '!');
            CHECK_INT((unsigned char)out.ptr[4], i + 33);
        }
    }
    /* Rows likewise, on a grid tall enough. */
    sk_term_mouse m;
    sk_term_mouse_init(&m);
    sk_term_input_modes modes = {.mouse = SK_MOUSE_ANY};
    sk_term_mouse_event e = ev(L, DN);
    e.row = 222;
    sk_buf_clear(&out);
    CHECK(sk_term_mouse_report(&m, &modes, 300, 300, e, &out));
    CHECK_MEM(sk_buf_str(&out), "\x1b[M !\xff");
    e.row = 223;
    sk_buf_clear(&out);
    CHECK(sk_term_mouse_report(&m, &modes, 300, 300, e, &out));
    CHECK_INT(out.len, 0);
    /* The event code counts too: Ctrl+Alt+Shift on a drag is 32+28+32. */
    e = (sk_term_mouse_event){.button = NB, .action = MV, .ctrl = true, .alt = true, .shift = true};
    CHECK(report(SK_MOUSE_ANY, SK_MOUSE_ENC_DEFAULT, e, &out));
    CHECK_MEM(sk_buf_str(&out), "\x1b[M_!!");
    sk_buf_free(&out);
}

static void test_sgr_coordinates(void)
{
    sk_buf out = {0};
    char want[64];
    for (int i = 0; i < COLS; i++) {
        sk_term_mouse_event e = ev(L, DN);
        e.col = i;
        CHECK(report(SK_MOUSE_ANY, SK_MOUSE_ENC_SGR, e, &out));
        snprintf(want, sizeof want, "\x1b[<0;%d;1M", i + 1);
        CHECK_STR(sk_buf_cstr(&out), want);
    }
    for (int i = 0; i < 500; i++) {
        sk_term_mouse_event e = ev(L, DN);
        e.x = i;
        CHECK(report(SK_MOUSE_ANY, SK_MOUSE_ENC_SGR_PIXELS, e, &out));
        snprintf(want, sizeof want, "\x1b[<0;%d;0M", i);
        CHECK_STR(sk_buf_cstr(&out), want);
    }
    sk_buf_free(&out);
}

/* CoreMouseService.test.ts's event codes in the default encoding, and
 * the same events under SGR, which names the button released. */
static void test_event_codes(void)
{
    static const struct {
        sk_term_button b;
        sk_term_mouse_action a;
        bool ctrl, alt, shift;
        const char *dflt, *sgr;
    } cases[] = {
        {L, DN, 0, 0, 0, "\x1b[M !!", "\x1b[<0;1;1M"},
        {MID, DN, 0, 0, 0, "\x1b[M!!!", "\x1b[<1;1;1M"},
        {R, DN, 0, 0, 0, "\x1b[M\"!!", "\x1b[<2;1;1M"},
        {W, DN, 0, 0, 0, "\x1b[Ma!!", "\x1b[<65;1;1M"},
        {L, UP, 0, 0, 0, "\x1b[M#!!", "\x1b[<0;1;1m"},
        {MID, UP, 0, 0, 0, "\x1b[M#!!", "\x1b[<1;1;1m"},
        {R, UP, 0, 0, 0, "\x1b[M#!!", "\x1b[<2;1;1m"},
        {W, UP, 0, 0, 0, "\x1b[M`!!", "\x1b[<64;1;1M"},
        {L, MV, 0, 0, 0, "\x1b[M@!!", "\x1b[<32;1;1M"},
        {MID, MV, 0, 0, 0, "\x1b[MA!!", "\x1b[<33;1;1M"},
        {R, MV, 0, 0, 0, "\x1b[MB!!", "\x1b[<34;1;1M"},
        {NB, MV, 0, 0, 0, "\x1b[MC!!", "\x1b[<35;1;1M"},
        {NB, MV, 1, 0, 0, "\x1b[MS!!", "\x1b[<51;1;1M"},
        {NB, MV, 0, 1, 0, "\x1b[MK!!", "\x1b[<43;1;1M"},
        {NB, MV, 0, 0, 1, "\x1b[MG!!", "\x1b[<39;1;1M"},
        {NB, MV, 1, 1, 0, "\x1b[M[!!", "\x1b[<59;1;1M"},
        {NB, MV, 0, 1, 1, "\x1b[MO!!", "\x1b[<47;1;1M"},
        {NB, MV, 1, 1, 1, "\x1b[M_!!", "\x1b[<63;1;1M"},
        {L, DN, 1, 0, 0, "\x1b[M0!!", "\x1b[<16;1;1M"},
        {L, DN, 0, 1, 1, "\x1b[M,!!", "\x1b[<12;1;1M"},
        {W, UP, 1, 0, 0, "\x1b[Mp!!", "\x1b[<80;1;1M"},
        {W, DN, 0, 1, 0, "\x1b[Mi!!", "\x1b[<73;1;1M"},
        {R, UP, 0, 0, 1, "\x1b[M'!!", "\x1b[<6;1;1m"},
    };
    sk_buf out = {0};
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_term_mouse_event e = {.button = cases[i].b, .action = cases[i].a, .ctrl = cases[i].ctrl,
                                 .alt = cases[i].alt, .shift = cases[i].shift};
        CHECK(report(SK_MOUSE_ANY, SK_MOUSE_ENC_DEFAULT, e, &out));
        CHECK_STR(sk_buf_cstr(&out), cases[i].dflt);
        CHECK(report(SK_MOUSE_ANY, SK_MOUSE_ENC_SGR, e, &out));
        CHECK_STR(sk_buf_cstr(&out), cases[i].sgr);
    }
    sk_buf_free(&out);
}

/* X10 reports presses only, without modifiers. */
static void test_x10_drops_modifiers(void)
{
    sk_buf out = {0};
    sk_term_mouse_event e = {.col = 4, .row = 2, .button = R, .action = DN, .ctrl = true, .alt = true, .shift = true};
    CHECK(report(SK_MOUSE_X10, SK_MOUSE_ENC_DEFAULT, e, &out));
    CHECK_MEM(sk_buf_str(&out), "\x1b[M\"%#");
    CHECK(report(SK_MOUSE_X10, SK_MOUSE_ENC_SGR, e, &out));
    CHECK_STR(sk_buf_cstr(&out), "\x1b[<2;5;3M");
    sk_buf_free(&out);
}

/* A move that repeats the last report is dropped: by cell, or by point
 * under SGR pixels. */
static void test_repeated_moves(void)
{
    sk_term_mouse m;
    sk_term_mouse_init(&m);
    sk_term_input_modes modes = {.mouse = SK_MOUSE_ANY, .mouse_encoding = SK_MOUSE_ENC_SGR};
    sk_buf out = {0};
    sk_term_mouse_event e = {.col = 3, .row = 4, .x = 30, .y = 40, .button = NB, .action = MV};
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    e.x = 31;
    CHECK(!sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    e.shift = true;
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    e.col = 4;
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    CHECK_STR(sk_buf_cstr(&out), "\x1b[<35;4;5M\x1b[<39;4;5M\x1b[<39;5;5M");
    /* A press between two moves to the same cell lets the second through. */
    e.action = DN;
    e.button = L;
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    e.action = MV;
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    CHECK(!sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    /* Pixels: the same cell at another point is news. */
    modes.mouse_encoding = SK_MOUSE_ENC_SGR_PIXELS;
    sk_buf_clear(&out);
    e.x = 32;
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    CHECK(!sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    CHECK_STR(sk_buf_cstr(&out), "\x1b[<36;32;40M");
    /* After a reset nothing is repeated. */
    sk_term_mouse_reset(&m);
    CHECK(sk_term_mouse_report(&m, &modes, 80, 24, e, &out));
    sk_buf_free(&out);
}

/* ---- The listeners ---- */

/* 80 by 24 cells of 8 by 17 points. */
static const sk_term_grid grid = {80, 24, 8, 17};

typedef struct {
    sk_term_mouse m;
    sk_term_input_modes modes;
    sk_buf out;
} rig;

static void rig_init(rig *r, bool mac, sk_mouse_protocol p, sk_mouse_encoding enc)
{
    sk_term_mouse_init(&r->m);
    r->m.mac = mac;
    r->modes = (sk_term_input_modes){.mouse = p, .mouse_encoding = enc};
    r->out = (sk_buf){0};
}

/* The bytes so far, cleared. */
static const char *take(rig *r, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s", sk_buf_cstr(&r->out));
    sk_buf_clear(&r->out);
    return buf;
}

static void test_cells_from_points(void)
{
    rig r;
    char b[256];
    rig_init(&r, false, SK_MOUSE_VT200, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 0, 0, 1, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 7.9, 16.9, 1, 0, &r.out));
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 8, 17, 1, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 639.5, 407.5, 1, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<0;1;1M\x1b[<0;1;1m\x1b[<0;2;2M\x1b[<0;80;24m");
    /* Outside the grid a point is clamped to its edge. */
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, -50, -3, 3, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 9000, 9000, 3, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<2;1;1M\x1b[<2;80;24m");
    /* Pixels are points, floored, clamped to the grid less one. */
    rig_init(&r, false, SK_MOUSE_VT200, SK_MOUSE_ENC_SGR_PIXELS);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 12.7, 33.2, 2, SK_MOD_CTRL, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 700, -1, 2, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<17;12;33M\x1b[<1;639;0m");
    sk_buf_free(&r.out);
}

static void test_protocol_none_takes_nothing(void)
{
    rig r;
    rig_init(&r, false, SK_MOUSE_NONE, SK_MOUSE_ENC_SGR);
    CHECK(!sk_term_mouse_down(&r.m, &r.modes, &grid, 10, 10, 1, 0, &r.out));
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 20, 10, true, 0, &r.out));
    CHECK(!sk_term_mouse_up(&r.m, &r.modes, &grid, 20, 10, 1, 0, &r.out));
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 30, 10, true, 0, &r.out));
    CHECK_INT(r.out.len, 0);
    sk_buf_free(&r.out);
}

/* X10: the press only; its release and drags are the app's. */
static void test_x10_press_only(void)
{
    rig r;
    char b[256];
    rig_init(&r, false, SK_MOUSE_X10, SK_MOUSE_ENC_DEFAULT);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 20, 20, 1, SK_MOD_CTRL, &r.out));
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 40, 20, true, 0, &r.out));
    CHECK(!sk_term_mouse_up(&r.m, &r.modes, &grid, 40, 20, 1, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[M #\"");
    sk_buf_free(&r.out);
}

/* Drags reach the program wherever the pointer goes, the held button
 * named left first, then middle, then right; plain moves only over the
 * grid and only under ANY. */
static void test_drags_and_moves(void)
{
    rig r;
    char b[512];
    rig_init(&r, false, SK_MOUSE_DRAG, SK_MOUSE_ENC_SGR);
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 4, 4, true, 0, &r.out));
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 3, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 12, 4, true, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 13, 4, true, 0, &r.out)); /* the same cell */
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 13, 4, 2, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 20, 4, false, SK_MOD_ALT, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 20, 4, 2, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, -20, 4, false, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, -20, 4, 3, 0, &r.out));
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 30, 4, true, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<2;1;1M\x1b[<34;2;1M\x1b[<1;2;1M\x1b[<41;3;1M\x1b[<1;3;1m"
                                     "\x1b[<34;1;1M\x1b[<2;1;1m");

    rig_init(&r, false, SK_MOUSE_ANY, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 4, 4, true, 0, &r.out));
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 900, 4, false, 0, &r.out));
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 1, 0, &r.out));
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 3, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 900, 4, false, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 900, 4, 1, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 4, 30, true, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 4, 30, 3, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 4, 60, true, SK_MOD_SHIFT, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<35;1;1M\x1b[<0;1;1M\x1b[<2;1;1M\x1b[<32;80;1M\x1b[<0;80;1m"
                                     "\x1b[<34;1;2M\x1b[<2;1;2m\x1b[<39;1;4M");
    sk_buf_free(&r.out);
}

/* A press the terminal did not take leaves its release and drags to the
 * app; a fourth button is taken but not reported. */
static void test_untaken_press(void)
{
    rig r;
    char b[256];
    rig_init(&r, false, SK_MOUSE_DRAG, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 4, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 12, 4, true, 0, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 12, 4, 4, 0, &r.out));
    CHECK_INT(r.out.len, 0);
    /* Under ANY a drag of that button is reported as a plain move. */
    rig_init(&r, false, SK_MOUSE_ANY, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 5, 0, &r.out));
    CHECK(sk_term_mouse_move(&r.m, &r.modes, &grid, 12, 4, true, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<35;2;1M");
    /* The protocol switched off mid-press drops the release. */
    rig_init(&r, false, SK_MOUSE_VT200, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 1, 0, &r.out));
    r.modes.mouse = SK_MOUSE_NONE;
    CHECK(!sk_term_mouse_up(&r.m, &r.modes, &grid, 4, 4, 1, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<0;1;1M");
    sk_buf_free(&r.out);
}

/* Shift+click selects on Linux, whatever the program asked; on macOS it
 * is reported with its modifier. */
static void test_shift_selects_on_linux(void)
{
    rig r;
    char b[256];
    rig_init(&r, false, SK_MOUSE_DRAG, SK_MOUSE_ENC_SGR);
    CHECK(!sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 1, SK_MOD_SHIFT, &r.out));
    CHECK(!sk_term_mouse_move(&r.m, &r.modes, &grid, 40, 4, true, SK_MOD_SHIFT, &r.out));
    CHECK(!sk_term_mouse_up(&r.m, &r.modes, &grid, 40, 4, 1, SK_MOD_SHIFT, &r.out));
    CHECK_INT(r.out.len, 0);
    rig_init(&r, true, SK_MOUSE_DRAG, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_down(&r.m, &r.modes, &grid, 4, 4, 1, SK_MOD_SHIFT, &r.out));
    CHECK(sk_term_mouse_up(&r.m, &r.modes, &grid, 4, 4, 1, SK_MOD_SHIFT | SK_MOD_ALT, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<4;1;1M\x1b[<12;1;1m");
    sk_buf_free(&r.out);
}

/* ---- The wheel ---- */

/* A mouse wheel's notch reports once; the line count only decides
 * whether there is a report. */
static void test_wheel_reports(void)
{
    rig r;
    char b[256];
    rig_init(&r, false, SK_MOUSE_VT200, SK_MOUSE_ENC_SGR);
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, 0, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, -1, false, 0, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 3, false, SK_MOD_CTRL, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, -1, false, SK_MOD_ALT, &r.out));
    /* Shift and a sideways scroll give nothing, and are still taken. */
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, SK_MOD_SHIFT, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 0, false, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[<64;1;1M\x1b[<65;1;1M\x1b[<80;1;1M\x1b[<73;1;1M");
    rig_init(&r, false, SK_MOUSE_DRAG, SK_MOUSE_ENC_DEFAULT);
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 9, 18, 1, false, 0, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 9, 18, -1, false, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[M`\"\"\x1b[Ma\"\"");
    sk_buf_free(&r.out);
}

/* A trackpad's small deltas add up: points / cell height, times 0.3
 * under 50 points. 10 points at 17 a cell is 0.176 of a line. */
static void test_wheel_trackpad(void)
{
    rig r;
    rig_init(&r, false, SK_MOUSE_ANY, SK_MOUSE_ENC_SGR);
    int reports = 0;
    for (int i = 1; i <= 12; i++) {
        size_t before = r.out.len;
        CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, -10, true, 0, &r.out));
        if (r.out.len > before) {
            reports++;
            CHECK(i == 6 || i == 12);
        }
    }
    CHECK_INT(reports, 2);
    CHECK_STR(sk_buf_cstr(&r.out), "\x1b[<65;1;1M\x1b[<65;1;1M");
    /* 60 points is past the trackpad guess: 3.5 lines, one report. */
    sk_buf_clear(&r.out);
    sk_term_mouse_reset(&r.m);
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 60, true, 0, &r.out));
    CHECK_STR(sk_buf_cstr(&r.out), "\x1b[<64;1;1M");
    sk_buf_free(&r.out);
}

/* A wheel that is not precise is 53 points a line on X11 and 40 on
 * macOS, where 40 counts as a trackpad: 0.706 of a line a notch. */
static void test_wheel_notches_by_platform(void)
{
    static const bool mac_reports[] = {false, true, true, false, true, true, false, true};
    rig r;
    rig_init(&r, true, SK_MOUSE_VT200, SK_MOUSE_ENC_SGR);
    for (size_t i = 0; i < SK_LEN(mac_reports); i++) {
        size_t before = r.out.len;
        sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, 0, &r.out);
        CHECK_INT(r.out.len > before, mac_reports[i]);
    }
    sk_buf_free(&r.out);
    rig_init(&r, false, SK_MOUSE_VT200, SK_MOUSE_ENC_SGR);
    for (int i = 0; i < 8; i++) {
        size_t before = r.out.len;
        sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, 0, &r.out);
        CHECK(r.out.len > before);
    }
    sk_buf_free(&r.out);
}

/* Without a wheel protocol the alternate screen gets an arrow for each
 * event that adds up to a line; the normal screen scrolls its view. */
static void test_wheel_without_reports(void)
{
    rig r;
    char b[256];
    rig_init(&r, false, SK_MOUSE_NONE, SK_MOUSE_ENC_DEFAULT);
    CHECK(!sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, 0, &r.out));
    r.modes.alt_screen = true;
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, 0, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, -2, false, SK_MOD_CTRL, &r.out));
    CHECK(!sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 0, false, 0, &r.out));
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, SK_MOD_SHIFT, &r.out));
    r.modes.app_cursor = true;
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 1, false, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1b[A\x1b[B\x1bOA");
    /* X10 takes no wheel: the same arrows. */
    r.modes.mouse = SK_MOUSE_X10;
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, -1, false, 0, &r.out));
    CHECK_STR(take(&r, b, sizeof b), "\x1bOB");
    r.modes.alt_screen = false;
    CHECK(!sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, -1, false, 0, &r.out));
    /* A trackpad's partial lines: nothing yet, still taken. */
    r.modes.alt_screen = true;
    CHECK(sk_term_mouse_wheel(&r.m, &r.modes, &grid, 4, 4, 2, true, 0, &r.out));
    CHECK_INT(r.out.len, 0);
    sk_buf_free(&r.out);
}

const sk_test sk_suite_keys_mouse[] = {
    T(test_protocols_take),
    T(test_out_of_grid),
    T(test_default_coordinates),
    T(test_sgr_coordinates),
    T(test_event_codes),
    T(test_x10_drops_modifiers),
    T(test_repeated_moves),
    T(test_cells_from_points),
    T(test_protocol_none_takes_nothing),
    T(test_x10_press_only),
    T(test_drags_and_moves),
    T(test_untaken_press),
    T(test_shift_selects_on_linux),
    T(test_wheel_reports),
    T(test_wheel_trackpad),
    T(test_wheel_notches_by_platform),
    T(test_wheel_without_reports),
    {0},
};
