/* The sessions dashboard's pane (decisions 0028, 0029 and 0033), as
 * app/renderer/dashboard.js and the .dashboard rules of app.css draw
 * it: fmt(), a row's flexbox at several widths and scales, the head line
 * and the charts' titles wrapped, hover and click, scrolling, the empty
 * view, ended rows, the dials' columns, a board in each state, state()
 * for the live checks, and a typical view drawn at 1x and 2x against
 * golden images. */
#include "../../src/app/dashview.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

#define FG SK_RGB(0xe6, 0xe6, 0xe6)
#define BG SK_RGB(0x0c, 0x0e, 0x12)
#define WHITE SK_RGB(0xff, 0xff, 0xff)
#define BLACK SK_RGB(0, 0, 0)
#define BLUE SK_RGB(0x40, 0x80, 0xff)
/* A label whose glyphs cover whole pixels: full blocks. */
#define BLOCKS "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88"

typedef struct {
    sk_fontset *fonts;
    sk_glyph_cache *cache;
    sk_dashview *v;
    sk_dashview_style st;
    sk_image im;
} pane;

static void pane_open(pane *p, double scale, int w, int h)
{
    memset(p, 0, sizeof *p);
    p->fonts = sk_fontset_new(NULL, NULL, true);
    p->cache = sk_glyph_cache_new((size_t)4 << 20);
    p->v = sk_dashview_new();
    p->st = (sk_dashview_style){p->fonts, p->cache, scale, FG, BG};
    sk_image_init(&p->im, (int)lround(w * scale), (int)lround(h * scale));
}

static void pane_close(pane *p)
{
    sk_dashview_free(p->v);
    sk_image_free(&p->im);
    sk_glyph_cache_free(p->cache);
    sk_fontset_free(p->fonts);
}

static void pane_draw(pane *p)
{
    sk_fill_rect(&p->im, sk_image_bounds(&p->im), BLACK, SK_BLEND_SRC, NULL);
    sk_dashview_draw(p->v, &p->im, sk_image_bounds(&p->im), &p->st);
}

/* A session's row: its series as JSON members, its board as JSON. */
typedef struct {
    const char *key, *label, *folder, *state;
    bool ended;
    const char *series; /* NULL: the five meters, empty */
    const char *board;  /* NULL: null */
} row_spec;

/* A view of ticks ticks 5 s apart, as sk_history_view writes it with
 * the meta of sk_dashboard: across's members and the dials as JSON. */
static void view_json(sk_buf *b, int ticks, const char *across, const char *dials, const row_spec *rows, size_t n)
{
    sk_buf_append_c(b, "{\"ticks\":[");
    for (int i = 0; i < ticks; i++)
        sk_buf_printf(b, "%s%lld", i ? "," : "", 1700000000000LL + 5000LL * i);
    sk_buf_printf(b, "],\"intervalMs\":5000,\"across\":{%s},\"dials\":%s,\"rows\":[", across ? across : "",
                  dials ? dials : "[]");
    for (size_t i = 0; i < n; i++) {
        const row_spec *r = &rows[i];
        sk_buf_printf(b,
                      "%s{\"key\":\"%s\",\"ended\":%s,\"series\":{%s},\"label\":\"%s\",\"folder\":\"%s\","
                      "\"state\":\"%s\",\"where\":\"tab\",\"tab\":%zu,\"board\":%s}",
                      i ? "," : "", r->key, r->ended ? "true" : "false",
                      r->series ? r->series
                                : "\"tools\":[],\"tokens\":[],\"cost\":[],\"context\":[],\"compactions\":[]",
                      r->label, r->folder, r->state, i + 1, r->board ? r->board : "null");
    }
    sk_buf_append_c(b, "]}");
}

static void update(pane *p, int ticks, const char *dials, const row_spec *rows, size_t n)
{
    sk_buf b = {0};
    view_json(&b, ticks, NULL, dials, rows, n);
    CHECK(sk_dashview_update(p->v, b.ptr, b.len));
    sk_buf_free(&b);
}

static char *state_of(const sk_dashview *v)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    sk_dashview_state(v, &w);
    return b.ptr;
}

/* The brightest channel in the box of points (x, y, w, h). */
static int brightest(const pane *p, double x, double y, double w, double h)
{
    double s = p->st.scale;
    int best = 0;
    for (int py = (int)(y * s); py < (int)((y + h) * s) && py < p->im.h; py++)
        for (int px = (int)(x * s); px < (int)((x + w) * s) && px < p->im.w; px++) {
            sk_color c = sk_unpremul(sk_image_row(&p->im, py)[px]);
            for (int k = 0; k < 24; k += 8)
                best = SK_MAX(best, (int)((c >> k) & 255));
        }
    return best;
}

/* How many pixels in the box have more red than red_over. */
static int reds(const pane *p, double x, double y, double w, double h, int red_over)
{
    double s = p->st.scale;
    int n = 0;
    for (int py = (int)(y * s); py < (int)((y + h) * s) && py < p->im.h; py++)
        for (int px = (int)(x * s); px < (int)((x + w) * s) && px < p->im.w; px++)
            n += (int)((sk_unpremul(sk_image_row(&p->im, py)[px]) >> 16) & 255) > red_over;
    return n;
}

static void check_format(const char *key, double v, const char *want)
{
    sk_buf b = {0};
    sk_dashview_format(&b, key, v);
    CHECK_STR(sk_buf_cstr(&b), want);
    sk_buf_free(&b);
}

/* fmt() of dashboard.js: toFixed takes a tie up, where printf takes the
 * even digit, and Math.round takes half up. */
static void test_format(void)
{
    check_format("tokens", NAN, "--");
    check_format("cost", NAN, "--");
    check_format("cost", 0, "$0.00");
    check_format("cost", -0.0, "$0.00");
    check_format("cost", 0.125, "$0.13");
    check_format("cost", 1.005, "$1.00");
    check_format("cost", 2.675, "$2.67");
    check_format("cost", 12.3456, "$12.35");
    check_format("cost", 1234567, "$1234567.00");
    check_format("context", 48.5, "49%");
    check_format("context", 49.5, "50%");
    check_format("context", 0.4, "0%");
    check_format("context", 99.99, "100%");
    check_format("tokens", 0, "0");
    check_format("tokens", 999, "999");
    check_format("tokens", 999.4, "999");
    check_format("tokens", 999.5, "1000");
    check_format("tokens", 1000, "1.0k");
    check_format("tokens", 1249, "1.2k");
    check_format("tokens", 1250, "1.3k");
    check_format("tokens", 1350, "1.4k");
    check_format("tokens", 999999, "1000.0k");
    check_format("tokens", 1e6, "1.00M");
    check_format("tokens", 2345678, "2.35M");
    check_format("tokens", 1.5e9, "1500.00M");
    check_format("tools", 2.5, "3");
    check_format("tools", -2.5, "-2");
    check_format("tools", -0.4, "0");
    check_format("rate.5h_pct", 0.5, "1");
}

static void check_column(pane *p, size_t i, double x, double w)
{
    double cx = -1, cw = -1;
    CHECK(sk_dashview_column(p->v, i, &cx, &cw));
    if (fabs(cx - x) > 1e-9 || fabs(cw - w) > 1e-9)
        sk_test_fail(__FILE__, __LINE__, "column %zu at %g, %g wide: want %g, %g", i, cx, cw, x, w);
}

static const row_spec one[] = {{"a", "claude", "~/projects/skindeep", "working", false, NULL, NULL}};

/* The row's flexbox: everything fixed but the folder, which takes what
 * is left and 60px at least, so a narrow pane's rows pass its edge; the
 * same in points at every scale, but where a border floors to a pixel. */
static void test_columns(void)
{
    static const double scales[] = {1, 2, 1.5};
    for (size_t k = 0; k < SK_LEN(scales); k++) {
        pane p;
        pane_open(&p, scales[k], 1000, 400);
        update(&p, 12, NULL, one, 1);
        pane_draw(&p);
        sk_dashview_layout l;
        CHECK(sk_dashview_layout_of(p.v, &l));
        CHECK_INT(l.columns, 9);
        check_column(&p, 0, 8, 110);
        check_column(&p, 1, 126, 168);
        check_column(&p, 2, 302, 58);
        for (size_t i = 0; i < 5; i++)
            check_column(&p, 3 + i, 368 + 104 * (double)i, 96);
        check_column(&p, 8, 888, 104);
        CHECK(!sk_dashview_column(p.v, 9, &l.height, &l.height));
        pane_close(&p);

        pane_open(&p, scales[k], 700, 300);
        update(&p, 12, NULL, one, 1);
        pane_draw(&p);
        check_column(&p, 1, 126, 60);
        check_column(&p, 2, 194, 58);
        check_column(&p, 7, 676, 96);
        check_column(&p, 8, 780, 104);
        CHECK(sk_dashview_layout_of(p.v, &l));
        if (scales[k] == 1.5) {
            /* A 1px border is one device pixel at 1.5x. */
            CHECK(fabs(l.header_y - (9 + 27 + 9 + (2 + 9 + 24.75 + 84) + 15) / 1.5) < 1e-9);
            CHECK(fabs(l.row_h - 37 / 1.5) < 1e-9);
        } else {
            CHECK(fabs(l.header_y - 120.5) < 1e-9);
            CHECK(fabs(l.rows_y - 142) < 1e-9);
            CHECK(fabs(l.row_h - 25) < 1e-9);
            CHECK(fabs(l.height - 173) < 1e-9);
        }
        CHECK_INT(l.head_lines, 1);
        CHECK(l.max_scroll == 0);
        pane_close(&p);
    }
}

/* Two dials: a column each, 118px, between the meters and the board. */
static void test_dials(void)
{
    static const row_spec rows[] = {
        {"a", BLOCKS, "~/a", "working", false,
         "\"tools\":[1,2],\"tokens\":[3,4],\"cost\":[1,2],\"context\":[5,6],\"compactions\":[0,0],"
         "\"rate.5h_pct\":[10,40,20],\"cpu\":[1,3]",
         NULL},
        {"b", BLOCKS, "~/b", "idle", false, NULL, NULL},
    };
    pane p;
    pane_open(&p, 1, 1200, 300);
    p.st.fg = BLUE;
    update(&p, 3, "[\"rate.5h_pct\",\"cpu\"]", rows, 2);
    pane_draw(&p);
    sk_dashview_layout l;
    CHECK(sk_dashview_layout_of(p.v, &l));
    CHECK_INT(l.columns, 11);
    check_column(&p, 1, 126, 116);
    check_column(&p, 2, 250, 58);
    check_column(&p, 7, 732, 96);
    check_column(&p, 8, 836, 118);
    check_column(&p, 9, 962, 118);
    check_column(&p, 10, 1088, 104);
    /* The dial's sparkline in its colour where the row has the series,
     * none where it has not; the text is blue, its red under 0x41. */
    CHECK(reds(&p, 836, l.rows_y + 2, 54, 20, 0x70) > 0);
    CHECK(reds(&p, 962, l.rows_y + 2, 54, 20, 0x70) > 0);
    CHECK_INT(reds(&p, 836, l.rows_y + l.row_h + 2, 54, 20, 0x70), 0);
    /* Its number beside it, "--" where there is none. */
    CHECK(brightest(&p, 894, l.rows_y + l.row_h + 2, 60, 20) > 0x30);
    pane_close(&p);
}

/* The head line and each chart's title wrap at their spaces in a narrow
 * pane, and the charts are as tall as the tallest. */
static void test_wrap(void)
{
    pane p;
    pane_open(&p, 1, 300, 400);
    update(&p, 3, NULL, one, 1);
    pane_draw(&p);
    sk_dashview_layout l;
    CHECK(sk_dashview_layout_of(p.v, &l));
    /* "Sessions dashboard: 1 live, 0 ended," then "the last minute,
     * sampled every 5 s", 284px wide; the titles 74px wide, "output",
     * "tokens /", "min" the tallest. */
    CHECK_INT(l.head_lines, 2);
    CHECK(fabs(l.header_y - (6 + 36 + 6 + (2 + 6 + 3 * 16.5 + 56) + 10)) < 1e-9);
    pane_close(&p);
}

static const row_spec four[] = {
    {"a", BLOCKS, "~/a", "working", false, NULL, NULL},
    {"b", BLOCKS, "~/b", "waiting", false, NULL, NULL},
    {"c", BLOCKS, "~/c", "idle", true, NULL, NULL},
    {"d", BLOCKS, "~/d", "idle", false, NULL, NULL},
};

/* A click opens a live row's session; an ended row, the header, the
 * padding and the rest are no row. A row overflowing the pane reaches its
 * right edge; one that fits stops at the content's. */
static void test_hit(void)
{
    pane p;
    pane_open(&p, 1, 700, 300);
    CHECK(sk_dashview_hit(p.v, 20, 150) == NULL);
    update(&p, 3, NULL, four, 4);
    CHECK(sk_dashview_hit(p.v, 20, 150) == NULL); /* not laid out yet */
    pane_draw(&p);
    CHECK_STR(sk_dashview_hit(p.v, 20, 150), "a");
    CHECK_STR(sk_dashview_hit(p.v, 20, 142), "a");
    CHECK_STR(sk_dashview_hit(p.v, 20, 166.9), "a");
    CHECK_STR(sk_dashview_hit(p.v, 20, 167), "b");
    CHECK(sk_dashview_hit(p.v, 20, 200) == NULL);
    CHECK_STR(sk_dashview_hit(p.v, 20, 230), "d");
    CHECK(sk_dashview_hit(p.v, 20, 130) == NULL);
    CHECK(sk_dashview_hit(p.v, 4, 150) == NULL);
    CHECK_STR(sk_dashview_hit(p.v, 8, 150), "a");
    CHECK_STR(sk_dashview_hit(p.v, 699, 150), "a");
    CHECK(sk_dashview_hit(p.v, 700, 150) == NULL);
    CHECK(sk_dashview_hit(p.v, 20, 243) == NULL);
    CHECK(sk_dashview_hit(p.v, 20, -1) == NULL);
    CHECK(sk_dashview_hit(p.v, NAN, 150) == NULL);
    pane_close(&p);

    pane_open(&p, 2, 1200, 300);
    update(&p, 3, NULL, four, 4);
    pane_draw(&p);
    CHECK_STR(sk_dashview_hit(p.v, 1191, 150), "a");
    CHECK(sk_dashview_hit(p.v, 1193, 150) == NULL);
    pane_close(&p);
}

/* :hover lights a live row, #ffffff10 over its box; a move within it, or
 * onto an ended row from none, changes nothing drawn. */
static void test_hover(void)
{
    pane p;
    pane_open(&p, 1, 700, 300);
    p.st.bg = BLACK;
    update(&p, 3, NULL, four, 4);
    pane_draw(&p);
    /* Between the label and the folder, in the row's gap. */
    sk_px before = sk_image_row(&p.im, 150)[121];
    CHECK(sk_dashview_hover(p.v, 20, 150));
    CHECK(!sk_dashview_hover(p.v, 30, 155));
    pane_draw(&p);
    sk_px lit = sk_image_row(&p.im, 150)[121];
    CHECK(before == sk_premul(BLACK));
    CHECK(lit == sk_premul(SK_RGB(0x10, 0x10, 0x10)) || lit == sk_premul(SK_RGB(0x0f, 0x0f, 0x0f)));
    /* The rule under the row is #ffffff14 over it. */
    CHECK(sk_image_row(&p.im, 166)[121] != lit);
    CHECK(sk_dashview_hover(p.v, 20, 170));
    CHECK(sk_dashview_hover(p.v, 20, 200)); /* ended: lit no more */
    CHECK(!sk_dashview_hover(p.v, 20, 205));
    CHECK(!sk_dashview_hover(p.v, -1, -1));
    CHECK(sk_dashview_hover(p.v, 20, 150));
    CHECK(sk_dashview_hover(p.v, -1, -1));
    pane_draw(&p);
    CHECK(sk_image_row(&p.im, 150)[121] == before);
    pane_close(&p);
}

/* overflow: auto scrolls the pane within its rows, never past them; a
 * click lands on the row scrolled under it, and a view with fewer rows
 * brings the scroll back. */
static void test_scroll(void)
{
    static row_spec ten[10];
    static const char *keys[] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9"};
    for (size_t i = 0; i < 10; i++)
        ten[i] = (row_spec){keys[i], "claude", "~/x", "working", false, NULL, NULL};
    for (int s = 1; s <= 2; s++) {
        pane p;
        pane_open(&p, s, 700, 300);
        update(&p, 3, NULL, ten, 10);
        CHECK(!sk_dashview_scroll(p.v, 50)); /* nothing laid out */
        pane_draw(&p);
        sk_image top;
        sk_image_init(&top, p.im.w, p.im.h);
        sk_blit(&top, 0, 0, &p.im, sk_image_bounds(&p.im), SK_BLEND_SRC, 255, NULL);
        sk_dashview_layout l;
        CHECK(sk_dashview_layout_of(p.v, &l));
        CHECK(fabs(l.height - 398) < 1e-9);
        CHECK(fabs(l.max_scroll - 98) < 1e-9);
        CHECK(sk_dashview_scroll(p.v, 50));
        CHECK(sk_dashview_layout_of(p.v, &l) && l.scroll == 50);
        CHECK(sk_dashview_scroll(p.v, 100));
        CHECK(sk_dashview_layout_of(p.v, &l) && l.scroll == 98);
        CHECK(!sk_dashview_scroll(p.v, 10));
        CHECK(!sk_dashview_scroll(p.v, NAN));
        CHECK_STR(sk_dashview_hit(p.v, 20, 150), "r4");
        CHECK_STR(sk_dashview_hit(p.v, 20, 290), "r9");
        CHECK(sk_dashview_hit(p.v, 20, 295) == NULL);
        /* What showed 98 points down now shows at the top: rows r1 to r5
         * whole, the same pixels. */
        pane_draw(&p);
        int moved = 0;
        for (int y = 167 * s; y < 292 * s; y++)
            moved += memcmp(sk_image_row(&top, y), sk_image_row(&p.im, y - 98 * s), (size_t)p.im.w * sizeof(sk_px)) != 0;
        CHECK_INT(moved, 0);
        sk_image_free(&top);
        CHECK(sk_dashview_scroll(p.v, -1000));
        CHECK(sk_dashview_layout_of(p.v, &l) && l.scroll == 0);
        CHECK(!sk_dashview_scroll(p.v, -1));
        pane_draw(&p);
        CHECK(brightest(&p, 8, 6, 200, 18) > 0xc0);
        CHECK(sk_dashview_scroll(p.v, 98));
        update(&p, 3, NULL, ten, 4);
        CHECK(sk_dashview_layout_of(p.v, &l) && l.scroll == 0 && l.max_scroll == 0);
        pane_close(&p);
    }
}

/* No rows: the note under the header; before any view, the head line's
 * title alone. A view that is not an object is refused. */
static void test_empty(void)
{
    pane p;
    pane_open(&p, 1, 700, 300);
    pane_draw(&p);
    CHECK(brightest(&p, 8, 6, 150, 18) > 0xc0);
    CHECK(brightest(&p, 0, 30, 700, 270) <= 0x12);
    char *st = state_of(p.v);
    CHECK_STR(st, "null");
    free(st);
    CHECK(!sk_dashview_update(p.v, "[1]", 3));
    CHECK(!sk_dashview_update(p.v, "{", 1));
    CHECK(!sk_dashview_update(p.v, NULL, 0));
    update(&p, 3, NULL, NULL, 0);
    pane_draw(&p);
    sk_dashview_layout l;
    CHECK(sk_dashview_layout_of(p.v, &l));
    CHECK(fabs(l.height - (142 + 12 + 18 + 6)) < 1e-9);
    CHECK(brightest(&p, 8, 148, 210, 18) > 0x80);
    CHECK(sk_dashview_hit(p.v, 20, 150) == NULL);
    st = state_of(p.v);
    CHECK_STR(st, "{\"ticks\":3,\"dials\":[],\"rows\":[],\"shownRows\":[]}");
    free(st);
    CHECK(!sk_dashview_update(p.v, "\"x\"", 3));
    st = state_of(p.v);
    CHECK_STR(st, "{\"ticks\":3,\"dials\":[],\"rows\":[],\"shownRows\":[]}");
    free(st);
    pane_close(&p);
}

/* An ended row at opacity 0.4, the header at 0.6: as one layer each,
 * full blocks of white over black come out at 102 and 153. */
static void test_ended(void)
{
    for (int s = 1; s <= 2; s++) {
        pane p;
        pane_open(&p, s, 700, 300);
        p.st.fg = WHITE;
        p.st.bg = BLACK;
        update(&p, 3, NULL, four, 4);
        pane_draw(&p);
        CHECK_INT(brightest(&p, 8, 142, 110, 24), 255);
        int ended = brightest(&p, 8, 192, 110, 24);
        CHECK(ended >= 101 && ended <= 103);
        int header = brightest(&p, 8, 121, 110, 20);
        CHECK(header >= 100 && header <= 154);
        pane_close(&p);
    }
}

/* The board cell in each state: nothing for no project, "...", "no
 * access", or a sparkline of the open issues and "open / closed". */
static void test_board(void)
{
    static const row_spec rows[] = {
        {"a", "a", "~/a", "working", false, NULL, NULL},
        {"b", "b", "~/b", "working", false, NULL, "{\"state\":\"loading\"}"},
        {"c", "c", "~/c", "working", false, NULL, "{\"state\":\"no access\"}"},
        {"d", "d", "~/d", "working", false, NULL, "{\"state\":\"error\"}"},
        {"e", "e", "~/e", "working", false, NULL,
         "{\"state\":\"ok\",\"open\":[3,4,4,5,6,5,5,5,6,7,7,8,8,9],\"closed\":[0,0,1,0,0,1,0,0,0,1,0,0,1,0],"
         "\"openNow\":9,\"closed14\":4}"},
    };
    pane p;
    pane_open(&p, 1, 1000, 400);
    p.st.fg = BLUE;
    update(&p, 3, NULL, rows, 5);
    pane_draw(&p);
    sk_dashview_layout l;
    CHECK(sk_dashview_layout_of(p.v, &l));
    double bx = 888;
    for (size_t i = 0; i < 4; i++)
        CHECK_INT(reds(&p, bx, l.rows_y + (double)i * l.row_h, 104, l.row_h - 1, 0x70), 0);
    CHECK(reds(&p, bx, l.rows_y + 4 * l.row_h + 2, 46, 20, 0x70) > 0);
    CHECK(brightest(&p, bx, l.rows_y + 2, 104, 20) <= 0x12);
    for (size_t i = 1; i < 5; i++)
        CHECK(brightest(&p, bx + (i == 4 ? 50 : 0), l.rows_y + (double)i * l.row_h + 2, 40, 20) > 0x80);
    char *st = state_of(p.v);
    CHECK(strstr(st, "\"board\":null,\"boardText\":\"\"") != NULL);
    CHECK(strstr(st, "\"board\":{\"state\":\"loading\"},\"boardText\":\"...\"") != NULL);
    CHECK(strstr(st, "\"board\":{\"state\":\"no access\"},\"boardText\":\"no access\"") != NULL);
    CHECK(strstr(st, "\"board\":{\"state\":\"error\"},\"boardText\":\"no access\"") != NULL);
    CHECK(strstr(st, "\"openNow\":9,\"closed14\":4},\"boardText\":\"9 / 4\"") != NULL);
    free(st);
    pane_close(&p);
}

/* state() of dashboard.js, as JSON.stringify writes it. */
static void test_state(void)
{
    pane p;
    pane_open(&p, 1, 700, 300);
    const char *view =
        "{\"ticks\":[1,2,3],\"intervalMs\":5000,\"across\":{},\"dials\":[\"d1\"],\"rows\":["
        "{\"key\":\"k1\",\"ended\":false,\"series\":{\"tools\":[1,2,null],\"tokens\":[null,5,7],"
        "\"cost\":[0.5,0.75,1],\"context\":[10,20,30],\"compactions\":[0,0,1],\"d1\":[3,4,5]},"
        "\"label\":\"claude\",\"folder\":\"~/x\",\"state\":\"working\",\"where\":\"tab\",\"tab\":3,"
        "\"board\":{\"state\":\"ok\",\"open\":[1,2],\"closed\":[0,1],\"openNow\":2,\"closed14\":1}},"
        "{\"key\":\"k2\",\"ended\":true,\"series\":{\"tools\":[],\"tokens\":[],\"cost\":[],\"context\":[],"
        "\"compactions\":[]},\"label\":\"codex\",\"folder\":\"~/y\",\"state\":\"idle\",\"where\":\"\","
        "\"tab\":null,\"board\":null}]}";
    CHECK(sk_dashview_update(p.v, view, strlen(view)));
    char *st = state_of(p.v);
    CHECK_STR(st, "{\"ticks\":3,\"dials\":[\"d1\"],\"rows\":["
                  "{\"key\":\"k1\",\"label\":\"claude\",\"state\":\"working\",\"ended\":false,\"points\":2,"
                  "\"last\":{\"tools\":2,\"tokens\":7,\"cost\":1,\"context\":30,\"compactions\":1},"
                  "\"board\":{\"state\":\"ok\",\"open\":[1,2],\"closed\":[0,1],\"openNow\":2,\"closed14\":1},"
                  "\"boardText\":\"2 / 1\"},"
                  "{\"key\":\"k2\",\"label\":\"codex\",\"state\":\"idle\",\"ended\":true,\"points\":0,"
                  "\"last\":{\"tools\":null,\"tokens\":null,\"cost\":null,\"context\":null,\"compactions\":null},"
                  "\"board\":null,\"boardText\":\"\"}],"
                  "\"shownRows\":[{\"key\":\"k1\",\"ended\":false,\"opacity\":\"1\"},"
                  "{\"key\":\"k2\",\"ended\":true,\"opacity\":\"0.4\"}]}");
    free(st);
    pane_close(&p);
}

/* ---- A typical view, drawn whole ---- */

#define TICKS 60

typedef double (*value_fn)(int i);

/* A series of TICKS values, null before from and from until on. */
static void series(sk_buf *b, const char *key, int from, int until, value_fn f)
{
    sk_buf_printf(b, "%s\"%s\":[", b->len && b->ptr[b->len - 1] != '{' ? "," : "", key);
    for (int i = 0; i < TICKS; i++) {
        if (i)
            sk_buf_putc(b, ',');
        if (i < from || i >= until || !f)
            sk_buf_append_c(b, "null");
        else
            sk_buf_printf(b, "%.4f", f(i));
    }
    sk_buf_putc(b, ']');
}

static double a_tokens(int i)
{
    return 1800 + 140 * i + 600 * (i / 7);
}
static double a_tools(int i)
{
    return 3 + i / 2 + 2 * (i / 9);
}
static double a_cost(int i)
{
    return 0.42 + 0.031 * i;
}
static double a_context(int i)
{
    return 20 + (i % 40) * 1.5;
}
static double a_compactions(int i)
{
    return i / 40;
}
static double a_dial(int i)
{
    return 10 + (i * 7 % 13);
}
static double b_tokens(int i)
{
    return 900 + 60 * (i - 20) + (i % 5 == 0 ? 200 : 0);
}
static double b_cost(int i)
{
    return 0.1 + 0.012 * (i - 20);
}
static double b_context(int i)
{
    return 35 + (i - 20) * 0.5;
}
static double c_tokens(int i)
{
    (void)i;
    return 52000;
}
static double c_tools(int i)
{
    (void)i;
    return 128;
}
static double c_cost(int i)
{
    (void)i;
    return 1.5;
}
static double c_context(int i)
{
    (void)i;
    return 64;
}
static double c_zero(int i)
{
    (void)i;
    return 0;
}
static double d_tokens(int i)
{
    return 1234567 + 1000 * i;
}
static double d_tools(int i)
{
    return 40 + i;
}
static double d_cost(int i)
{
    return 8.2 + 0.05 * i;
}
static double d_context(int i)
{
    return 70 + (i % 10);
}
static double sum_tokens(int i)
{
    return i < 12 ? i * 120 : 1400 + (i * 53 % 400);
}
static double sum_tools(int i)
{
    return (i * 7 % 11) + 2;
}
static double sum_cost(int i)
{
    return 0.6 + 0.04 * i;
}

/* Four sessions as a dashboard shows them five minutes in: one working
 * with its board read, one waiting whose history began late and whose
 * agent has no hooks, one idle with a long label and folder and no
 * access to its board, one ended; one dial open, the waiting row lit. */
static void typical(sk_buf *b)
{
    sk_buf_append_c(b, "{\"ticks\":[");
    for (int i = 0; i < TICKS; i++)
        sk_buf_printf(b, "%s%lld", i ? "," : "", 1700000000000LL + 5000LL * i);
    sk_buf_append_c(b, "],\"intervalMs\":5000,\"across\":{");
    series(b, "tokensPerMin", 0, TICKS, sum_tokens);
    series(b, "toolsPerMin", 0, TICKS, sum_tools);
    series(b, "cost", 0, TICKS, sum_cost);
    sk_buf_append_c(b, "},\"dials\":[\"rate.5h_pct\"],\"rows\":[");
    sk_buf_append_c(b, "{\"key\":\"4242:1000\",\"ended\":false,\"series\":{");
    series(b, "tools", 0, TICKS, a_tools);
    series(b, "tokens", 0, TICKS, a_tokens);
    series(b, "cost", 0, TICKS, a_cost);
    series(b, "context", 0, TICKS, a_context);
    series(b, "compactions", 0, TICKS, a_compactions);
    series(b, "rate.5h_pct", 30, TICKS, a_dial);
    sk_buf_append_c(b, "},\"label\":\"claude\",\"folder\":\"~/projects/skindeep\",\"state\":\"working\","
                       "\"where\":\"tab\",\"tab\":1,\"board\":{\"state\":\"ok\","
                       "\"open\":[5,5,6,6,6,7,7,7,8,8,7,7,8,9],\"closed\":[0,1,0,0,1,0,0,0,1,0,1,0,0,0],"
                       "\"openNow\":9,\"closed14\":4}},");
    sk_buf_append_c(b, "{\"key\":\"5151:1000\",\"ended\":false,\"series\":{");
    series(b, "tools", 0, 0, NULL);
    series(b, "tokens", 20, TICKS, b_tokens);
    series(b, "cost", 20, TICKS, b_cost);
    series(b, "context", 20, TICKS, b_context);
    series(b, "compactions", 0, 0, NULL);
    series(b, "rate.5h_pct", 0, 0, NULL);
    sk_buf_append_c(b, "},\"label\":\"codex\",\"folder\":\"~/src/api-server\",\"state\":\"waiting\","
                       "\"where\":\"\",\"tab\":null,\"board\":{\"state\":\"loading\"}},");
    sk_buf_append_c(b, "{\"key\":\"6060:1000\",\"ended\":false,\"series\":{");
    series(b, "tools", 0, TICKS, c_tools);
    series(b, "tokens", 0, TICKS, c_tokens);
    series(b, "cost", 0, TICKS, c_cost);
    series(b, "context", 0, TICKS, c_context);
    series(b, "compactions", 0, TICKS, c_zero);
    series(b, "rate.5h_pct", 0, 0, NULL);
    sk_buf_append_c(b, "},\"label\":\"fix-the-flaky-dashboard-tests\","
                       "\"folder\":\"~/work/clients/acme/very/long/path/to/the/project\",\"state\":\"idle\","
                       "\"where\":\"tab\",\"tab\":2,\"board\":{\"state\":\"no access\"}},");
    sk_buf_append_c(b, "{\"key\":\"7070:1000\",\"ended\":true,\"series\":{");
    series(b, "tools", 0, 30, d_tools);
    series(b, "tokens", 0, 30, d_tokens);
    series(b, "cost", 0, 30, d_cost);
    series(b, "context", 0, 30, d_context);
    series(b, "compactions", 0, 30, c_zero);
    series(b, "rate.5h_pct", 0, 0, NULL);
    sk_buf_append_c(b, "},\"label\":\"claude\",\"folder\":\"~/tmp\",\"state\":\"idle\",\"where\":\"\","
                       "\"tab\":null,\"board\":null}]}");
}

static void test_golden(void)
{
    sk_buf view = {0};
    typical(&view);
    for (int s = 1; s <= 2; s++) {
        pane p;
        pane_open(&p, s, 700, 300);
        CHECK(sk_dashview_update(p.v, view.ptr, view.len));
        pane_draw(&p);
        sk_dashview_hover(p.v, 300, 170);
        pane_draw(&p);
        char name[64];
        snprintf(name, sizeof name, "app/dashview_%dx", s);
        CHECK_GOLDEN(name, &p.im, 2);
        pane_close(&p);
    }
    sk_buf_free(&view);
}

const sk_test sk_suite_dashview[] = {
    T(test_format), T(test_columns), T(test_dials), T(test_wrap),  T(test_hit),    T(test_hover),
    T(test_scroll), T(test_empty),   T(test_ended), T(test_board), T(test_state), T(test_golden),
    {0},
};
