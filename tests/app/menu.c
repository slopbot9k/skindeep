/* The app's drop-down (decision 0022) as menu.js builds it: where it
 * opens, which ask wins, what each press does, the keys, the flyouts,
 * the sessions' box and the message, and how it looks. */
#include "../../src/app/menu.h"
#include "../../src/base/arena.h"
#include "../../src/gfx/draw.h"
#include "../golden.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

#define WIN_W 1100.0
#define WIN_H 760.0
#define STAGE 22.0

typedef struct {
    sk_fontset *sans, *mono;
    sk_glyph_cache *cache;
    sk_menu_style st;
    sk_menu *m;
} rig;

static void rig_open(rig *r, double scale)
{
    static const char *const sans[] = {"DejaVu Sans", NULL};
    static const char *const mono[] = {"DejaVu Sans Mono", NULL};
    memset(r, 0, sizeof *r);
    r->sans = sk_fontset_new(NULL, sans, false);
    r->mono = sk_fontset_new(NULL, mono, true);
    r->cache = sk_glyph_cache_new((size_t)8 << 20);
    r->st = (sk_menu_style){r->sans, r->mono, r->cache, scale};
    r->m = sk_menu_new();
}

static void rig_close(rig *r)
{
    sk_menu_free(r->m);
    sk_glyph_cache_free(r->cache);
    sk_fontset_free(r->sans);
    sk_fontset_free(r->mono);
}

/* ---- Rows ---- */

typedef struct {
    sk_menu_row *rows;
    size_t n;
} rows;

static sk_menu_row *add(rows *b, sk_row_kind kind, const char *text)
{
    b->rows = realloc(b->rows, (b->n + 1) * sizeof *b->rows);
    sk_menu_row *r = &b->rows[b->n++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->text = text ? sk_xstrdup(text) : NULL;
    return r;
}

static sk_menu_row *item(rows *b, const char *text, int action, sk_row_then then)
{
    sk_menu_row *r = add(b, SK_ROW_ITEM, text);
    r->action = action;
    r->then = then;
    return r;
}

/* The File menu as menu.js fills it with nothing closed yet. */
static rows file_rows(void)
{
    rows b = {0};
    item(&b, "New tab", 1, SK_ROW_CLOSES)->keys = sk_xstrdup("Ctrl+Shift+T");
    item(&b, "New tab...", 2, SK_ROW_CLOSES);
    item(&b, "Close tab", 3, SK_ROW_CLOSES)->keys = sk_xstrdup("Ctrl+Shift+W");
    item(&b, "Reopen closed tab", 0, SK_ROW_CLOSES)->disabled = true;
    item(&b, "Duplicate tab", 5, SK_ROW_CLOSES);
    item(&b, "Next tab", 6, SK_ROW_CLOSES)->keys = sk_xstrdup("Ctrl+Tab");
    item(&b, "Previous tab", 7, SK_ROW_CLOSES)->keys = sk_xstrdup("Ctrl+Shift+Tab");
    add(&b, SK_ROW_RULE, NULL);
    add(&b, SK_ROW_HEADING, "Settings");
    sk_menu_row *f = add(&b, SK_ROW_FLYOUT, "Terminal font size");
    f->nsub = 3;
    f->sub = sk_xcalloc(3, sizeof *f->sub);
    const char *labels[] = {"* Skin default (14 px)", "  12 px", "  16 px"};
    for (int i = 0; i < 3; i++) {
        f->sub[i].kind = SK_ROW_ITEM;
        f->sub[i].text = sk_xstrdup(labels[i]);
        f->sub[i].current = i == 0;
        f->sub[i].action = 20 + i;
        f->sub[i].then = SK_ROW_WAITS;
    }
    item(&b, "Reset panel sizes", 0, SK_ROW_CLOSES)->disabled = true;
    add(&b, SK_ROW_RULE, NULL);
    add(&b, SK_ROW_HEADING, "Housekeeping");
    sk_menu_row *p = item(&b, "Reopen closed tab failed", 0, SK_ROW_SAYS);
    p->problem = true;
    p->arg = sk_xstrdup("cannot resume: no such session");
    item(&b, "Forget remembered skin", 8, SK_ROW_WAITS);
    item(&b, "Open log folder", 9, SK_ROW_CLOSES);
    item(&b, "Clear logs", 10, SK_ROW_WAITS);
    return b;
}

static sk_menu_session session(const char *key, const char *label, const char *folder, const char *state)
{
    sk_menu_session s = {sk_xstrdup(key),   sk_xstrdup(label), sk_xstrdup(folder), sk_xstrdup(state),
                         sk_xstrdup("12 m"), sk_xstrdup(""),    false};
    return s;
}

static rows agents_rows(size_t n)
{
    rows b = {0};
    add(&b, SK_ROW_HEADING, "Agent sessions");
    sk_menu_row *box = add(&b, SK_ROW_SESSIONS, NULL);
    box->nsessions = n;
    box->sessions = sk_xcalloc(n ? n : 1, sizeof *box->sessions);
    for (size_t i = 0; i < n; i++) {
        char key[32], label[32];
        snprintf(key, sizeof key, "%zu:1700000000", 100 + i);
        snprintf(label, sizeof label, "claude %zu", i);
        box->sessions[i] = session(key, label, "~/projects/skindeep", i % 2 ? "waiting" : "working");
    }
    item(&b, "Sessions dashboard", 11, SK_ROW_CLOSES);
    return b;
}

static bool show(rig *r, int menu, rows b, double bx, double by)
{
    unsigned a = sk_menu_ask(r->m, menu);
    return sk_menu_show(r->m, a, menu, b.rows, b.n, &r->st, bx, by, 30, 30, WIN_W, WIN_H, STAGE);
}

/* ---- The state, as the live checks read it ---- */

typedef struct {
    sk_arena a;
    sk_buf b;
    const sk_json *j;
} state;

static void state_of(rig *r, state *s)
{
    memset(s, 0, sizeof *s);
    sk_arena_init(&s->a, 0);
    sk_jw w;
    sk_jw_init(&w, &s->b, 0);
    sk_menu_state(r->m, &w);
    s->j = sk_json_parse(&s->a, s->b.ptr, s->b.len, NULL);
}

static void state_free(state *s)
{
    sk_arena_free(&s->a);
    sk_buf_free(&s->b);
}

static double numof(const sk_json *j, const char *key)
{
    double d = NAN;
    sk_json_as_number(sk_json_get(j, key), &d);
    return d;
}

static bool boolof(const sk_json *j, const char *key)
{
    bool b = false;
    sk_json_as_bool(sk_json_get(j, key), &b);
    return b;
}

static double row_y(rig *r, size_t i, double *h)
{
    state s;
    state_of(r, &s);
    const sk_json *row = sk_json_at(sk_json_get(s.j, "rows"), i);
    double y = numof(row, "y");
    if (h)
        *h = numof(row, "h");
    state_free(&s);
    return y;
}

static void box_of(rig *r, double *x, double *y, double *w, double *h)
{
    state s;
    state_of(r, &s);
    *x = numof(s.j, "x");
    *y = numof(s.j, "y");
    *w = numof(s.j, "w");
    *h = numof(s.j, "h");
    state_free(&s);
}

/* ---- Where it opens ---- */

static void test_opens_below_the_button(void)
{
    rig r;
    rig_open(&r, 1);
    CHECK(show(&r, SK_MENU_FILE, file_rows(), 100, 40));
    CHECK(sk_menu_open(r.m));
    CHECK_INT(sk_menu_shown(r.m), SK_MENU_FILE);
    double x, y, w, h;
    box_of(&r, &x, &y, &w, &h);
    CHECK(x == 100);
    CHECK(y == 72);
    /* min-width 180, the bar's four titles wider still, and the border. */
    CHECK(w > 182 && w <= 522);
    /* The border, the bar and its border, its margin, 7 items, a rule,
     * a heading, a flyout, an item, a rule, a heading, 4 items, the
     * padding and the border. */
    double rows_h = 13 * 20.8 + 2 * 9 + 2 * 17.6;
    CHECK(fabs(h - (1 + 22.8 + 1 + 4 + rows_h + 4 + 1)) < 1e-6);
    rig_close(&r);
}

static void test_kept_inside_the_window(void)
{
    rig r;
    rig_open(&r, 1);
    CHECK(show(&r, SK_MENU_FILE, file_rows(), 1080, 40));
    double x, y, w, h;
    box_of(&r, &x, &y, &w, &h);
    CHECK(fabs(x + w - (WIN_W - 4)) < 1e-6);
    /* No room below: above the button. */
    CHECK(show(&r, SK_MENU_FILE, file_rows(), 100, 600));
    box_of(&r, &x, &y, &w, &h);
    CHECK(fabs(y - (600 - h - 2)) < 1e-6);
    /* No room above either: at the stage's top. */
    CHECK(show(&r, SK_MENU_FILE, file_rows(), 100, 380));
    box_of(&r, &x, &y, &w, &h);
    CHECK(412 + h > WIN_H - 4 && 380 - h - 2 < STAGE);
    CHECK(y == STAGE);
    rig_close(&r);
}

static void test_the_newest_ask_wins(void)
{
    rig r;
    rig_open(&r, 1);
    unsigned a1 = sk_menu_ask(r.m, SK_MENU_FILE), a2 = sk_menu_ask(r.m, SK_MENU_AGENTS);
    rows b = file_rows();
    CHECK(!sk_menu_show(r.m, a1, SK_MENU_FILE, b.rows, b.n, &r.st, 10, 10, 30, 30, WIN_W, WIN_H, STAGE));
    CHECK(!sk_menu_open(r.m));
    b = agents_rows(1);
    CHECK(sk_menu_show(r.m, a2, SK_MENU_AGENTS, b.rows, b.n, &r.st, 10, 10, 30, 30, WIN_W, WIN_H, STAGE));
    CHECK_INT(sk_menu_shown(r.m), SK_MENU_AGENTS);
    /* A close cancels what was asked before it. */
    unsigned a3 = sk_menu_ask(r.m, SK_MENU_FILE);
    sk_menu_close(r.m);
    b = file_rows();
    CHECK(!sk_menu_show(r.m, a3, SK_MENU_FILE, b.rows, b.n, &r.st, 10, 10, 30, 30, WIN_W, WIN_H, STAGE));
    CHECK(!sk_menu_open(r.m));
    rig_close(&r);
}

/* ---- Presses ---- */

/* The middle of row i, a little in from the left. */
static sk_menu_press press_row(rig *r, size_t i)
{
    double x, y, w, h, rh;
    box_of(r, &x, &y, &w, &h);
    double ry = row_y(r, i, &rh);
    return sk_menu_mouse_down(r->m, x + 20, ry + rh / 2);
}

static void test_titles_show_their_menu(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    double x, y, w, h;
    box_of(&r, &x, &y, &w, &h);
    /* "File" is first, "Agents" next: a press past File's 14 + 26 + 14. */
    sk_menu_press p = sk_menu_mouse_down(r.m, x + 1 + 70, y + 10);
    CHECK_INT(p.kind, SK_MENU_PRESS_SHOW);
    CHECK_INT(p.menu, SK_MENU_AGENTS);
    p = sk_menu_mouse_down(r.m, x + 1 + 10, y + 10);
    CHECK_INT(p.kind, SK_MENU_PRESS_NOTHING);
    CHECK(sk_menu_open(r.m));
    rig_close(&r);
}

static void test_a_row_that_closes(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    sk_menu_press p = press_row(&r, 0);
    CHECK_INT(p.kind, SK_MENU_PRESS_ACTION);
    CHECK_INT(p.action, 1);
    CHECK(p.closes && !p.waits);
    CHECK(!sk_menu_open(r.m));
    rig_close(&r);
}

static void test_a_row_that_waits(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    sk_menu_press p = press_row(&r, 16); /* Clear logs */
    CHECK_INT(p.kind, SK_MENU_PRESS_ACTION);
    CHECK_INT(p.action, 10);
    CHECK(p.waits && !p.closes);
    CHECK(sk_menu_open(r.m));
    /* An answer to another press is not this one's. */
    sk_menu_answer(r.m, p.press + 1, NULL, NULL);
    CHECK(sk_menu_open(r.m));
    sk_menu_answer(r.m, p.press, NULL, "Removed 2 logs of ended windows.");
    state s;
    state_of(&r, &s);
    sk_str msg = {0};
    sk_json_as_string(sk_json_get(s.j, "message"), &msg);
    CHECK_MEM(msg, "Removed 2 logs of ended windows.");
    CHECK(!boolof(s.j, "error"));
    state_free(&s);
    /* The message makes the drop-down taller, not moved. */
    double x, y, w, h;
    box_of(&r, &x, &y, &w, &h);
    CHECK(y == 72);
    /* Nothing back closes it. */
    p = press_row(&r, 16);
    sk_menu_answer(r.m, p.press, NULL, NULL);
    CHECK(!sk_menu_open(r.m));
    rig_close(&r);
}

static void test_rows_that_do_nothing(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    CHECK_INT(press_row(&r, 3).kind, SK_MENU_PRESS_NOTHING); /* disabled */
    CHECK_INT(press_row(&r, 7).kind, SK_MENU_PRESS_NOTHING); /* the rule */
    CHECK_INT(press_row(&r, 8).kind, SK_MENU_PRESS_NOTHING); /* a heading */
    CHECK_INT(press_row(&r, 9).kind, SK_MENU_PRESS_NOTHING); /* a flyout's own row */
    CHECK(sk_menu_open(r.m));
    rig_close(&r);
}

static void test_a_problem_says_its_detail(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    CHECK_INT(press_row(&r, 13).kind, SK_MENU_PRESS_NOTHING);
    state s;
    state_of(&r, &s);
    sk_str msg = {0};
    sk_json_as_string(sk_json_get(s.j, "message"), &msg);
    CHECK_MEM(msg, "cannot resume: no such session");
    CHECK(boolof(s.j, "error"));
    state_free(&s);
    rig_close(&r);
}

static void test_a_press_outside_closes(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    CHECK_INT(sk_menu_mouse_down(r.m, 900, 700).kind, SK_MENU_PRESS_OUTSIDE);
    CHECK(!sk_menu_open(r.m));
    rig_close(&r);
}

/* ---- Keys ---- */

static void test_keys(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    sk_key_event k = {0};
    int to;
    k.key = SK_KEY_LEFT;
    CHECK(sk_menu_key(r.m, &k, &to));
    CHECK_INT(to, SK_MENU_VIZ);
    k.key = SK_KEY_RIGHT;
    CHECK(sk_menu_key(r.m, &k, &to));
    CHECK_INT(to, SK_MENU_AGENTS);
    /* Every other key is dropped while it is open. */
    memset(&k, 0, sizeof k);
    k.text[0] = 'q';
    k.base = 'q';
    CHECK(sk_menu_key(r.m, &k, &to));
    CHECK_INT(to, -1);
    CHECK(sk_menu_open(r.m));
    k.key = SK_KEY_ESCAPE;
    CHECK(sk_menu_key(r.m, &k, &to));
    CHECK(!sk_menu_open(r.m));
    /* Closed, it takes none. */
    CHECK(!sk_menu_key(r.m, &k, &to));
    rig_close(&r);
}

/* ---- Flyouts ---- */

static void test_flyouts(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_FILE, file_rows(), 100, 40);
    double x, y, w, h, rh;
    box_of(&r, &x, &y, &w, &h);
    double ry = row_y(&r, 9, &rh);
    CHECK(sk_menu_mouse_move(r.m, x + 20, ry + 5));
    state s;
    state_of(&r, &s);
    CHECK_INT(sk_json_get(s.j, "flyout")->type, SK_JSON_ARRAY);
    state_free(&s);
    /* To the right of the drop-down, 5 above its row: its first item
     * starts after the border and the padding. */
    double fy = ry - 5 + 1 + 4;
    CHECK(sk_menu_mouse_move(r.m, x + w + 20, fy + 20.8 + 5));
    sk_menu_press p = sk_menu_mouse_down(r.m, x + w + 20, fy + 20.8 + 5);
    CHECK_INT(p.kind, SK_MENU_PRESS_ACTION);
    CHECK_INT(p.action, 21);
    CHECK(p.waits);
    /* Off the row and the flyout, it closes. */
    CHECK(sk_menu_mouse_move(r.m, x + 20, row_y(&r, 0, NULL) + 5));
    state_of(&r, &s);
    CHECK_INT(sk_json_get(s.j, "flyout")->type, SK_JSON_NULL);
    state_free(&s);
    /* At the window's right edge it opens to the left. */
    show(&r, SK_MENU_FILE, file_rows(), 1080, 40);
    box_of(&r, &x, &y, &w, &h);
    ry = row_y(&r, 9, &rh);
    sk_menu_mouse_move(r.m, x + 20, ry + 5);
    fy = ry - 5 + 1 + 4;
    p = sk_menu_mouse_down(r.m, x - 20, fy + 5);
    CHECK_INT(p.kind, SK_MENU_PRESS_ACTION);
    CHECK_INT(p.action, 20);
    rig_close(&r);
}

/* ---- The agent sessions ---- */

static void sessions_of(rig *r, const char *want)
{
    state s;
    state_of(r, &s);
    sk_buf got = {0};
    const sk_json *l = sk_json_get(s.j, "sessions");
    for (size_t i = 0; l && i < l->array.len; i++) {
        sk_str label = {0};
        sk_json_as_string(sk_json_get(sk_json_at(l, i), "label"), &label);
        sk_buf_printf(&got, "%s%.*s%s", i ? " " : "", (int)label.len, label.ptr,
                      boolof(sk_json_at(l, i), "ended") ? "!" : "");
    }
    CHECK_STR(sk_buf_cstr(&got), want);
    sk_buf_free(&got);
    state_free(&s);
}

static void test_sessions_refresh_in_place(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_AGENTS, agents_rows(2), 100, 40);
    CHECK(sk_menu_has_sessions(r.m));
    sessions_of(&r, "claude 0 claude 1");
    /* 0 gone, 1 still there, a new one last. */
    sk_menu_session *l = sk_xcalloc(2, sizeof *l);
    l[0] = session("200:1700000000", "codex", "~/work", "idle");
    l[1] = session("101:1700000000", "claude 1b", "~/projects/skindeep", "working");
    sk_menu_sessions(r.m, l, 2);
    sessions_of(&r, "claude 0! claude 1b codex");
    /* One back from the dead is no longer ended. */
    l = sk_xcalloc(1, sizeof *l);
    l[0] = session("100:1700000000", "claude 0", "~/projects/skindeep", "idle");
    sk_menu_sessions(r.m, l, 1);
    sessions_of(&r, "claude 0 claude 1b! codex!");
    rig_close(&r);
}

static void test_sessions_box_keeps_the_window(void)
{
    rig r;
    rig_open(&r, 1);
    /* 30 rows cannot fit below a button at 40 in 760: the box gives up
     * what the window lacks. */
    show(&r, SK_MENU_AGENTS, agents_rows(30), 100, 40);
    double x, y, w, h, bh;
    box_of(&r, &x, &y, &w, &h);
    row_y(&r, 1, &bh);
    CHECK(bh < 30 * 20.8);
    CHECK(fabs(y + h - (WIN_H - 4)) < 1e-6);
    /* Not below two rows, however little room. */
    show(&r, SK_MENU_AGENTS, agents_rows(30), 100, 680);
    row_y(&r, 1, &bh);
    CHECK(fabs(bh - 2 * 20.8) < 1e-6);
    /* A refresh keeps its height. */
    show(&r, SK_MENU_AGENTS, agents_rows(1), 100, 40);
    row_y(&r, 1, &bh);
    CHECK(fabs(bh - 20.8) < 1e-6);
    sk_menu_session *l = sk_xcalloc(3, sizeof *l);
    for (int i = 0; i < 3; i++)
        l[i] = session(i == 0 ? "100:1700000000" : i == 1 ? "7:1" : "8:1", "x", "~", "idle");
    sk_menu_sessions(r.m, l, 3);
    double bh2;
    row_y(&r, 1, &bh2);
    CHECK(bh2 == bh);
    /* The wheel over it scrolls it. */
    double ry = row_y(&r, 1, NULL);
    CHECK(sk_menu_scroll(r.m, x + 20, ry + 5, 1));
    rig_close(&r);
}

static void test_a_session_press_waits(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_AGENTS, agents_rows(2), 100, 40);
    double x, y, w, h;
    box_of(&r, &x, &y, &w, &h);
    double ry = row_y(&r, 1, NULL);
    sk_menu_press p = sk_menu_mouse_down(r.m, x + 30, ry + 20.8 + 5);
    CHECK_INT(p.kind, SK_MENU_PRESS_SESSION);
    CHECK_STR(p.arg, "101:1700000000");
    CHECK(p.waits);
    sk_menu_answer(r.m, p.press, "That session has ended.", NULL);
    state s;
    state_of(&r, &s);
    CHECK(boolof(s.j, "error"));
    state_free(&s);
    rig_close(&r);
}

static void test_a_long_message_wraps(void)
{
    rig r;
    rig_open(&r, 1);
    show(&r, SK_MENU_SKINS, (rows){0}, 100, 40);
    rows b = {0};
    item(&b, "Open skin...", 1, SK_ROW_WAITS);
    show(&r, SK_MENU_SKINS, b, 100, 40);
    sk_menu_press p = press_row(&r, 0);
    sk_buf e = {0};
    for (int i = 0; i < 12; i++)
        sk_buf_printf(&e, "skins/x/skin.css: line %d: \"cursor\": \"wait\" is not a valid value: cursor takes "
                          "auto, default, pointer or text\n",
                      i + 1);
    sk_menu_answer(r.m, p.press, sk_buf_cstr(&e), NULL);
    double x, y, w, h;
    box_of(&r, &x, &y, &w, &h);
    CHECK(w == 522);
    /* max-height 240 of the error, its padding and margins, below the
     * bar and the row. */
    CHECK(fabs(h - (1 + 22.8 + 1 + 4 + 20.8 + 4 + 12 + 240 + 4 + 4 + 1)) < 1e-6);
    sk_buf_free(&e);
    rig_close(&r);
}

/* ---- Pictures ---- */

static void draw_into(rig *r, sk_image *im, int w, int h)
{
    sk_image_init(im, (int)lround(w * r->st.scale), (int)lround(h * r->st.scale));
    sk_fill_rect(im, sk_image_bounds(im), SK_RGB(0x30, 0x40, 0x30), SK_BLEND_SRC, NULL);
    sk_menu_draw(r->m, im, &r->st);
}

static void test_golden_file(void)
{
    for (int s = 1; s <= 2; s++) {
        rig r;
        rig_open(&r, s);
        show(&r, SK_MENU_FILE, file_rows(), 20, 10);
        double x, y, w, h, rh;
        box_of(&r, &x, &y, &w, &h);
        double ry = row_y(&r, 9, &rh);
        sk_menu_mouse_move(r.m, x + 20, ry + 5);
        sk_menu_mouse_move(r.m, x + w + 20, ry - 5 + 1 + 4 + 20.8 + 5);
        sk_image im;
        draw_into(&r, &im, 500, 520);
        char name[32];
        snprintf(name, sizeof name, "menu_file_%dx", s);
        CHECK_GOLDEN(name, &im, 2);
        sk_image_free(&im);
        rig_close(&r);
    }
}

static void test_golden_agents(void)
{
    for (int s = 1; s <= 2; s++) {
        rig r;
        rig_open(&r, s);
        show(&r, SK_MENU_AGENTS, agents_rows(3), 20, 10);
        sk_menu_session *l = sk_xcalloc(2, sizeof *l);
        l[0] = session("101:1700000000", "claude 1", "~/projects/a/very/long/folder/name/skindeep", "waiting");
        l[1] = session("300:1700000000", "codex [work]", "~/work", "idle");
        sk_menu_sessions(r.m, l, 2);
        double x, y, w, h;
        box_of(&r, &x, &y, &w, &h);
        sk_menu_mouse_move(r.m, x + 30, row_y(&r, 1, NULL) + 20.8 + 5);
        sk_image im;
        draw_into(&r, &im, 560, 260);
        char name[32];
        snprintf(name, sizeof name, "menu_agents_%dx", s);
        CHECK_GOLDEN(name, &im, 2);
        sk_image_free(&im);
        rig_close(&r);
    }
}

static void test_shadow_is_the_gaussian(void)
{
    sk_image im;
    sk_image_init(&im, 160, 160);
    sk_fill_rect(&im, sk_image_bounds(&im), SK_RGB(0xff, 0xff, 0xff), SK_BLEND_SRC, NULL);
    sk_shadow_rect(&im, 30, 30, 100, 100, 16, SK_RGB(0, 0, 0), NULL);
    /* Deep inside, all of it; on an edge, a little over half (the
     * pixel's centre is half a pixel in); on a corner, that squared;
     * three deviations out, none. */
    CHECK_INT(sk_unpremul(im.px[80 * im.stride + 80]) & 0xff, 0);
    int edge = (int)(sk_unpremul(im.px[80 * im.stride + 30]) & 0xff);
    CHECK(edge >= 115 && edge <= 125);
    int corner = (int)(sk_unpremul(im.px[30 * im.stride + 30]) & 0xff);
    CHECK(corner >= 180 && corner <= 190);
    CHECK_INT(sk_unpremul(im.px[80 * im.stride + 2]) & 0xff, 0xff);
    sk_image_free(&im);
}

const sk_test sk_suite_menu[] = {
    T(test_opens_below_the_button),
    T(test_kept_inside_the_window),
    T(test_the_newest_ask_wins),
    T(test_titles_show_their_menu),
    T(test_a_row_that_closes),
    T(test_a_row_that_waits),
    T(test_rows_that_do_nothing),
    T(test_a_problem_says_its_detail),
    T(test_a_press_outside_closes),
    T(test_keys),
    T(test_flyouts),
    T(test_sessions_refresh_in_place),
    T(test_sessions_box_keeps_the_window),
    T(test_a_session_press_waits),
    T(test_a_long_message_wraps),
    T(test_golden_file),
    T(test_golden_agents),
    T(test_shadow_is_the_gaussian),
    {0},
};
