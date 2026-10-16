/* New tab...'s dialog (decision 0025), as app/renderer/chooser.js and
 * the #chooser rules of app/renderer/app.css have it: its text fields'
 * every key, the select and its list, the radios, Tab's order, Enter,
 * Escape and the buttons, the picker's answers, Forget, the layout in
 * points, and the dialog drawn at 1x and 2x. The picker and the
 * clipboard are fakes; the state is made up, its home /home/user. */
#include "../../src/app/chooser.h"
#include "../golden.h"
#include "../test.h"
#include "fakes.h"

#include <math.h>
#include <stdlib.h>

/* What the fakes were asked, and what they answer. */
static struct {
    sk_strv clipboard, primary;
    int gets;
    bool get_primary;
    void (*get_done)(void *ud, const char *utf8);
    void *get_ud;
    int picks;
    sk_buf title, start;
    bool hidden;
    sk_picker_done pick_done;
    void *pick_ud;
    int changes;
} fk;

static void fake_set(const char *utf8)
{
    sk_strv_push(&fk.clipboard, utf8);
}

static void fake_primary(const char *utf8)
{
    sk_strv_push(&fk.primary, utf8);
}

static void fake_get(bool primary, void (*done)(void *ud, const char *utf8), void *ud)
{
    fk.gets++;
    fk.get_primary = primary;
    fk.get_done = done;
    fk.get_ud = ud;
}

static void fake_pick(sk_window *parent, sk_loop *loop, const sk_picker_opts *o, sk_picker_done done, void *ud)
{
    (void)parent, (void)loop;
    fk.picks++;
    sk_buf_clear(&fk.title);
    sk_buf_append_c(&fk.title, o->title ? o->title : "");
    sk_buf_clear(&fk.start);
    sk_buf_append_c(&fk.start, o->start ? o->start : "");
    fk.hidden = o->hidden;
    fk.pick_done = done;
    fk.pick_ud = ud;
}

static void on_changed(void *ud)
{
    (void)ud;
    fk.changes++;
}

/* The clipboard's answer to the last paste. */
static void clipboard_holds(const char *text)
{
    CHECK(fk.get_done != NULL);
    if (fk.get_done)
        fk.get_done(fk.get_ud, text);
    fk.get_done = NULL;
}

static void picker_says(const char *dir, const char *err)
{
    CHECK(fk.pick_done != NULL);
    if (fk.pick_done)
        fk.pick_done(fk.pick_ud, dir, err);
}

typedef struct {
    sk_chooser *c;
    sk_fontset *sans, *mono;
    sk_glyph_cache *cache;
    sk_image im;
    sk_chooser_style style;
    sk_rect area;
} rig;

static void profile(sk_newtab_agent *a, const char *dir, const char *shown, bool dflt, bool remembered)
{
    sk_newtab_profile p = {sk_xstrdup(dir), sk_xstrdup(shown), dflt, remembered};
    sk_vec_push(&a->profiles, p);
}

/* Two agents: Claude Code, the window's, with three profiles and an
 * option it does not carry, and Codex with two. */
static void sample(sk_newtab_state *st, const char *claude_profile)
{
    memset(st, 0, sizeof *st);
    sk_newtab_agent *a = &st->agents[SK_PROFILE_CLAUDE];
    a->agent = SK_PROFILE_CLAUDE;
    profile(a, "/home/user/.claude", "~/.claude", true, false);
    profile(a, "/home/user/.claude-work", "~/.claude-work", false, true);
    profile(a, "/home/user/accounts/lab/.claude", "~/accounts/lab/.claude", false, false);
    a->line = sk_xstrdup("claude --model opus");
    a->dropped = sk_xstrdup("--resume 7f3a");
    a->profile = sk_xstrdup(claude_profile);
    a = &st->agents[SK_PROFILE_CODEX];
    a->agent = SK_PROFILE_CODEX;
    profile(a, "/home/user/.codex", "~/.codex", true, false);
    profile(a, "/home/user/.codex-lab", "~/.codex-lab", false, true);
    a->line = sk_xstrdup("codex");
    a->dropped = sk_xstrdup("");
    a->profile = sk_xstrdup("/home/user/.codex");
    st->agent = SK_PROFILE_CLAUDE;
    st->folder = sk_xstrdup("~/src/app");
    st->home = sk_xstrdup("/home/user");
    st->base = sk_xstrdup("/home/user/src/app");
}

/* The window's content, w by h points at scale, with what lies under
 * the dialog: a strip, then a stage the overlay darkens. */
static void paint_under(rig *r)
{
    sk_fill_rect(&r->im, sk_image_bounds(&r->im), SK_RGB(0x5a, 0x6e, 0x82), SK_BLEND_SRC, NULL);
    sk_fill_rect(&r->im, sk_rect_make(0, 0, r->im.w, r->area.y), SK_RGB(0x10, 0x10, 0x10), SK_BLEND_SRC, NULL);
    for (int x = 0; x < r->im.w; x += (int)(40 * r->style.scale))
        sk_fill_rect(&r->im, sk_rect_make(x, r->area.y, (int)(20 * r->style.scale), r->area.h),
                     SK_RGB(0x82, 0x96, 0xaa), SK_BLEND_SRC, NULL);
}

static void draw(rig *r)
{
    paint_under(r);
    sk_chooser_draw(r->c, &r->im, r->area, &r->style);
}

static void rig_init(rig *r, double scale, int w, int h)
{
    memset(r, 0, sizeof *r);
    sk_strv_free(&fk.clipboard);
    sk_strv_free(&fk.primary);
    sk_buf_free(&fk.title);
    sk_buf_free(&fk.start);
    memset(&fk, 0, sizeof fk);
    r->sans = sk_fontset_new(NULL, NULL, false);
    r->mono = sk_fontset_new(NULL, NULL, true);
    r->cache = sk_glyph_cache_new((size_t)16 << 20);
    sk_image_init(&r->im, (int)(w * scale), (int)(h * scale));
    r->style = (sk_chooser_style){r->sans, r->mono, r->cache, scale};
    int strip = (int)lround(22 * scale);
    r->area = sk_rect_make(0, strip, r->im.w, r->im.h - strip);
    sk_chooser_config cfg = {NULL, NULL, on_changed, NULL, fake_pick, fake_set, fake_primary, fake_get};
    r->c = sk_chooser_new(&cfg);
}

static void rig_open(rig *r, const char *claude_profile)
{
    sk_newtab_state st;
    sample(&st, claude_profile);
    sk_chooser_open(r->c, &st);
    CHECK(st.folder == NULL);
    draw(r);
}

static void rig_free(rig *r)
{
    sk_chooser_free(r->c);
    sk_image_free(&r->im);
    sk_glyph_cache_free(r->cache);
    sk_fontset_free(r->sans);
    sk_fontset_free(r->mono);
    sk_strv_free(&fk.clipboard);
    sk_strv_free(&fk.primary);
    sk_buf_free(&fk.title);
    sk_buf_free(&fk.start);
}

/* ---- Input ---- */

static sk_chooser_result key(rig *r, sk_key k, uint32_t mods)
{
    sk_key_event e;
    memset(&e, 0, sizeof e);
    e.key = k;
    e.mods = mods;
    return sk_chooser_key(r->c, &e);
}

/* A chord: a letter held with mods, which types nothing. */
static sk_chooser_result chord(rig *r, char letter, uint32_t mods)
{
    sk_key_event e;
    memset(&e, 0, sizeof e);
    e.mods = mods;
    e.base = (uint32_t)letter;
    return sk_chooser_key(r->c, &e);
}

/* Each character typed by its key. */
static void typed(rig *r, const char *s)
{
    for (size_t i = 0; s[i];) {
        size_t n = 1;
        while (s[i + n] && ((unsigned char)s[i + n] & 0xC0) == 0x80)
            n++;
        sk_key_event e;
        memset(&e, 0, sizeof e);
        memcpy(e.text, s + i, n);
        e.base = (unsigned char)s[i] < 0x80 ? (uint32_t)s[i] : 0;
        sk_chooser_result res = sk_chooser_key(r->c, &e);
        CHECK(res.redraw);
        i += n;
    }
}

static void select_all(rig *r)
{
    chord(r, 'a', SK_CHOOSER_CMD_MOD);
}

/* The JSON member key of the state, compact, or NULL. */
static char *member(rig *r, const char *k)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    sk_chooser_json(r->c, &w);
    const char *path[] = {k, NULL};
    char *v = json_at(b.ptr, path);
    sk_buf_free(&b);
    return v;
}

#define CHECK_MEMBER(r, k, want)                                             \
    do {                                                                     \
        char *v_ = member(r, k);                                             \
        CHECK_STR(v_, want);                                                 \
        free(v_);                                                            \
    } while (0)

#define CHECK_LINE(r, want) CHECK_MEMBER(r, "line", "\"" want "\"")
#define CHECK_FOCUS(r, want) CHECK_MEMBER(r, "focus", "\"" want "\"")

static const char *last(const sk_strv *v)
{
    return v->len ? v->ptr[v->len - 1] : NULL;
}

/* A point of name's box: its fractions across and down. */
static void point(rig *r, const char *name, double fx, double fy, double *x, double *y)
{
    double b[4] = {0, 0, 0, 0};
    CHECK(sk_chooser_box(r->c, name, b));
    *x = b[0] + fx * b[2];
    *y = b[1] + fy * b[3];
}

static sk_chooser_result mouse(rig *r, sk_event_type type, double x, double y, int button, int clicks,
                               uint32_t mods)
{
    sk_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.button = button;
    ev.mouse.clicks = clicks;
    ev.mouse.mods = mods;
    return sk_chooser_mouse(r->c, &ev);
}

/* A press and a release at (x, y): the release's result. */
static sk_chooser_result click_at(rig *r, double x, double y, int clicks, uint32_t mods)
{
    mouse(r, SK_EV_MOUSE_DOWN, x, y, 1, clicks, mods);
    sk_chooser_result res = mouse(r, SK_EV_MOUSE_UP, x, y, 1, clicks, mods);
    draw(r);
    return res;
}

static sk_chooser_result click(rig *r, const char *name)
{
    double x, y;
    point(r, name, 0.5, 0.5, &x, &y);
    return click_at(r, x, y, 1, 0);
}

/* Where character i of the line field starts, in points: the field's
 * text is DejaVu Sans Mono at 12, from its border and padding. */
static double char_x(rig *r, const char *name, double i)
{
    double b[4];
    CHECK(sk_chooser_box(r->c, name, b));
    return b[0] + 5 + i * sk_measure_text(r->mono, 0, 12, "x", 1);
}

/* ---- Text fields ---- */

static void test_typing_moves_and_deletes(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    /* The command line has the focus, its caret at the end. */
    CHECK_FOCUS(&r, "line");
    typed(&r, " -p");
    CHECK_LINE(&r, "claude --model opus -p");
    select_all(&r);
    typed(&r, "abc déf");
    CHECK_LINE(&r, "abc déf");
    key(&r, SK_KEY_HOME, 0);
    typed(&r, "^");
    key(&r, SK_KEY_END, 0);
    typed(&r, "$");
    CHECK_LINE(&r, "^abc déf$");
    key(&r, SK_KEY_BACKSPACE, 0);
    key(&r, SK_KEY_LEFT, 0);
    key(&r, SK_KEY_BACKSPACE, 0);
    CHECK_LINE(&r, "^abc df");
    key(&r, SK_KEY_HOME, 0);
    key(&r, SK_KEY_DELETE, 0);
    key(&r, SK_KEY_RIGHT, 0);
    key(&r, SK_KEY_RIGHT, 0);
    key(&r, SK_KEY_DELETE, 0);
    CHECK_LINE(&r, "ab df");
    /* Backspace at the start and Delete at the end change nothing. */
    key(&r, SK_KEY_HOME, 0);
    key(&r, SK_KEY_BACKSPACE, 0);
    key(&r, SK_KEY_END, 0);
    key(&r, SK_KEY_DELETE, 0);
    CHECK_LINE(&r, "ab df");
    /* A character past ASCII moves and deletes whole. */
    select_all(&r);
    typed(&r, "aé日b");
    key(&r, SK_KEY_LEFT, 0);
    key(&r, SK_KEY_LEFT, 0);
    key(&r, SK_KEY_BACKSPACE, 0);
    CHECK_LINE(&r, "a日b");
    key(&r, SK_KEY_DELETE, 0);
    CHECK_LINE(&r, "ab");
    rig_free(&r);
}

static void test_shift_selects_and_the_clipboard_cuts_copies_and_pastes(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    select_all(&r);
    CHECK_STR(last(&fk.primary), "claude --model opus");
    typed(&r, "one two three");
    key(&r, SK_KEY_HOME, 0);
    for (int i = 0; i < 3; i++)
        key(&r, SK_KEY_RIGHT, SK_MOD_SHIFT);
    /* What is selected is X11's PRIMARY. */
    CHECK_STR(last(&fk.primary), "one");
    chord(&r, 'c', SK_CHOOSER_CMD_MOD);
    CHECK_STR(last(&fk.clipboard), "one");
    /* Left takes the caret to the selection's start, Right to its end. */
    key(&r, SK_KEY_RIGHT, 0);
    typed(&r, "!");
    CHECK_LINE(&r, "one! two three");
    key(&r, SK_KEY_END, 0);
    key(&r, SK_KEY_LEFT, SK_MOD_SHIFT);
    key(&r, SK_KEY_LEFT, SK_MOD_SHIFT);
    key(&r, SK_KEY_LEFT, 0);
    typed(&r, "<");
    CHECK_LINE(&r, "one! two thr<ee");
    /* Shift+Home and Shift+End select to either end. */
    key(&r, SK_KEY_HOME, SK_MOD_SHIFT);
    chord(&r, 'x', SK_CHOOSER_CMD_MOD);
    CHECK_STR(last(&fk.clipboard), "one! two thr<");
    CHECK_LINE(&r, "ee");
    /* A paste replaces the selection when the clipboard answers, its
     * line breaks dropped. */
    key(&r, SK_KEY_END, SK_MOD_SHIFT);
    chord(&r, 'v', SK_CHOOSER_CMD_MOD);
    CHECK_INT(fk.gets, 1);
    CHECK(!fk.get_primary);
    clipboard_holds("pasted\r\nline");
    CHECK_LINE(&r, "pastedline");
    CHECK_INT(fk.changes, 1);
    /* Typing replaces a selection; Backspace and Delete remove it. */
    key(&r, SK_KEY_LEFT, SK_MOD_SHIFT);
    typed(&r, "E");
    key(&r, SK_KEY_HOME, SK_MOD_SHIFT);
    key(&r, SK_KEY_BACKSPACE, 0);
    CHECK_LINE(&r, "");
    typed(&r, "xyz");
    key(&r, SK_KEY_LEFT, SK_MOD_SHIFT);
    key(&r, SK_KEY_DELETE, 0);
    CHECK_LINE(&r, "xy");
    /* Copy with nothing selected copies nothing; an empty paste pastes
     * nothing. */
    size_t n = fk.clipboard.len;
    chord(&r, 'c', SK_CHOOSER_CMD_MOD);
    CHECK_INT(fk.clipboard.len, n);
    chord(&r, 'v', SK_CHOOSER_CMD_MOD);
    clipboard_holds(NULL);
    CHECK_LINE(&r, "xy");
#ifndef __APPLE__
    /* Linux's own: Shift+Insert pastes, Ctrl+Insert copies, Shift+Delete
     * cuts. */
    select_all(&r);
    key(&r, SK_KEY_INSERT, SK_MOD_CTRL);
    CHECK_STR(last(&fk.clipboard), "xy");
    key(&r, SK_KEY_DELETE, SK_MOD_SHIFT);
    CHECK_LINE(&r, "");
    key(&r, SK_KEY_INSERT, SK_MOD_SHIFT);
    clipboard_holds("q");
    CHECK_LINE(&r, "q");
#endif
    rig_free(&r);
}

static void test_word_moves_and_deletes(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    const uint32_t word = SK_CHOOSER_WORD_MOD;
    select_all(&r);
    typed(&r, "git -C ~/src/app status");
    key(&r, SK_KEY_HOME, 0);
    key(&r, SK_KEY_RIGHT, word);
    typed(&r, "1");
    key(&r, SK_KEY_RIGHT, word);
    typed(&r, "2");
    CHECK_LINE(&r, "git1 -C2 ~/src/app status");
    key(&r, SK_KEY_RIGHT, word);
    key(&r, SK_KEY_LEFT, word | SK_MOD_SHIFT);
    chord(&r, 'c', SK_CHOOSER_CMD_MOD);
    CHECK_STR(last(&fk.clipboard), "src");
    key(&r, SK_KEY_END, 0);
    key(&r, SK_KEY_BACKSPACE, word);
    CHECK_LINE(&r, "git1 -C2 ~/src/app ");
    key(&r, SK_KEY_BACKSPACE, word);
    CHECK_LINE(&r, "git1 -C2 ~/src/");
    key(&r, SK_KEY_HOME, 0);
    key(&r, SK_KEY_DELETE, word);
    CHECK_LINE(&r, " -C2 ~/src/");
    key(&r, SK_KEY_DELETE, word);
    CHECK_LINE(&r, " ~/src/");
    key(&r, SK_KEY_LEFT, word);
    key(&r, SK_KEY_END, 0);
    key(&r, SK_KEY_LEFT, word);
    typed(&r, "|");
    CHECK_LINE(&r, " ~/|src/");
#ifdef __APPLE__
    /* Cmd+Left and Right to either end, Cmd+Backspace to the start, and
     * Cocoa's Ctrl+A and Ctrl+E. */
    key(&r, SK_KEY_LEFT, SK_MOD_SUPER);
    typed(&r, "[");
    key(&r, SK_KEY_RIGHT, SK_MOD_SUPER);
    typed(&r, "]");
    CHECK_LINE(&r, "[ ~/|src/]");
    key(&r, SK_KEY_LEFT, 0);
    key(&r, SK_KEY_BACKSPACE, SK_MOD_SUPER);
    CHECK_LINE(&r, "]");
    chord(&r, 'a', SK_MOD_CTRL);
    typed(&r, "<");
    chord(&r, 'e', SK_MOD_CTRL);
    typed(&r, ">");
    CHECK_LINE(&r, "<]>");
    key(&r, SK_KEY_UP, 0);
    typed(&r, "^");
    key(&r, SK_KEY_DOWN, 0);
    typed(&r, "v");
    CHECK_LINE(&r, "^<]>v");
#endif
    rig_free(&r);
}

static void test_chords_type_nothing_and_named_keys_nothing_more(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    select_all(&r);
    typed(&r, "ab");
    sk_key_event e;
    memset(&e, 0, sizeof e);
    strcpy(e.text, "k");
    e.base = 'k';
    e.mods = SK_MOD_CTRL;
    sk_chooser_key(r.c, &e);
    e.mods = SK_MOD_SUPER;
    sk_chooser_key(r.c, &e);
    CHECK_LINE(&r, "ab");
    /* Option types its character on macOS; Alt types nothing on Linux. */
    strcpy(e.text, "å");
    e.base = 'a';
    e.mods = SK_MOD_ALT;
    sk_chooser_key(r.c, &e);
#ifdef __APPLE__
    CHECK_LINE(&r, "abå");
#else
    CHECK_LINE(&r, "ab");
#endif
    sk_chooser_result res = key(&r, SK_KEY_F1 + 4, 0);
    CHECK_INT(res.act, SK_CHOOSER_NONE);
    rig_free(&r);
}

static void test_the_pointer_places_the_caret_and_selects(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    double x, y;
    point(&r, "line", 0.5, 0.5, &x, &y);
    /* "claude --model opus": a press nearest the boundary after 3. */
    click_at(&r, char_x(&r, "line", 3.3), y, 1, 0);
    typed(&r, "|");
    CHECK_LINE(&r, "cla|ude --model opus");
    /* Shift extends from the caret. */
    click_at(&r, char_x(&r, "line", 6.9), y, 1, SK_MOD_SHIFT);
    chord(&r, 'c', SK_CHOOSER_CMD_MOD);
    CHECK_STR(last(&fk.clipboard), "ude");
    /* A drag selects, and the release sets PRIMARY. */
    mouse(&r, SK_EV_MOUSE_DOWN, char_x(&r, "line", 8), y, 1, 1, 0);
    CHECK_INT(sk_chooser_cursor(r.c), SK_CURSOR_IBEAM);
    mouse(&r, SK_EV_MOUSE_MOVE, char_x(&r, "line", 11), y + 30, 0, 0, 0);
    mouse(&r, SK_EV_MOUSE_MOVE, char_x(&r, "line", 15), y + 30, 0, 0, 0);
    mouse(&r, SK_EV_MOUSE_UP, char_x(&r, "line", 15), y + 30, 1, 1, 0);
    CHECK_STR(last(&fk.primary), "--model");
    /* Double: the word; triple: the whole. */
    click_at(&r, char_x(&r, "line", 10.5), y, 2, 0);
    chord(&r, 'c', SK_CHOOSER_CMD_MOD);
    CHECK_STR(last(&fk.clipboard), "model");
    click_at(&r, char_x(&r, "line", 1), y, 3, 0);
    chord(&r, 'c', SK_CHOOSER_CMD_MOD);
    CHECK_STR(last(&fk.clipboard), "cla|ude --model opus");
    /* Past the text's end, the end. */
    click_at(&r, char_x(&r, "line", 60), y, 1, 0);
    typed(&r, "!");
    CHECK_LINE(&r, "cla|ude --model opus!");
    /* A press in the folder field moves the focus there, its caret where
     * it landed. */
    point(&r, "folder", 0.5, 0.5, &x, &y);
    click_at(&r, char_x(&r, "folder", 2), y, 1, 0);
    CHECK_FOCUS(&r, "folder");
    typed(&r, "x/");
    CHECK_MEMBER(&r, "folder", "\"~/x/src/app\"");
    /* The middle button pastes PRIMARY where it lands. */
    mouse(&r, SK_EV_MOUSE_DOWN, char_x(&r, "folder", 0), y, 2, 1, 0);
    CHECK(fk.get_primary);
    clipboard_holds("/");
    CHECK_MEMBER(&r, "folder", "\"/~/x/src/app\"");
    rig_free(&r);
}

static void test_an_input_method_types_and_composes(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    select_all(&r);
    typed(&r, "ab");
    key(&r, SK_KEY_LEFT, 0);
    sk_chooser_result res = sk_chooser_preedit(r.c, "に");
    CHECK(res.redraw);
    draw(&r);
    CHECK_LINE(&r, "ab");
    sk_chooser_preedit(r.c, "");
    res = sk_chooser_text(r.c, "日本");
    CHECK(res.redraw);
    CHECK_LINE(&r, "a日本b");
    /* A composition takes the selection's place once committed. */
    key(&r, SK_KEY_LEFT, SK_MOD_SHIFT);
    sk_chooser_preedit(r.c, "ほん");
    draw(&r);
    sk_chooser_preedit(r.c, "");
    sk_chooser_text(r.c, "本当");
    CHECK_LINE(&r, "a日本当b");
    int x, y, w, h;
    CHECK(sk_chooser_ime_rect(r.c, &x, &y, &w, &h));
    double b[4];
    CHECK(sk_chooser_box(r.c, "line", b));
    CHECK_INT(y, lround(b[1] + 2));
    CHECK(x > b[0] + 5);
    /* With no field focused, text goes nowhere. */
    key(&r, SK_KEY_TAB, 0);
    CHECK(!sk_chooser_ime_rect(r.c, &x, &y, &w, &h));
    res = sk_chooser_text(r.c, "z");
    CHECK(!res.redraw);
    CHECK_LINE(&r, "a日本当b");
    rig_free(&r);
}

static void test_a_long_line_scrolls_to_its_caret(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    select_all(&r);
    for (int i = 0; i < 12; i++)
        typed(&r, "0123456789");
    draw(&r);
    /* The caret at the end is in view: a press at the field's right
     * edge lands near the end. */
    double b[4];
    CHECK(sk_chooser_box(r.c, "line", b));
    click_at(&r, b[0] + b[2] - 6, b[1] + b[3] / 2, 1, 0);
    typed(&r, "!");
    sk_newtab_pick p;
    sk_chooser_pick(r.c, &p);
    CHECK(strlen(p.line) == 121 && p.line[120] == '!');
    sk_newtab_pick_free(&p);
    /* Home scrolls back to the start. */
    key(&r, SK_KEY_HOME, 0);
    draw(&r);
    click_at(&r, char_x(&r, "line", 1), b[1] + b[3] / 2, 1, 0);
    typed(&r, "<");
    sk_chooser_pick(r.c, &p);
    CHECK(!strncmp(p.line, "0<12", 4));
    sk_newtab_pick_free(&p);
    rig_free(&r);
}

/* ---- The select, the radios, the focus ---- */

static void test_the_select_opens_and_picks_with_the_pointer(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    CHECK_MEMBER(&r, "env", "\"CLAUDE_CONFIG_DIR unset, the default profile\"");
    CHECK_MEMBER(&r, "forget", "false");
    CHECK_MEMBER(&r, "profiles", "[\"~/.claude (default)\",\"~/.claude-work\",\"~/accounts/lab/.claude\"]");
    click(&r, "profile");
    CHECK_MEMBER(&r, "list", "true");
    CHECK_FOCUS(&r, "profile");
    double s[4], l[4];
    CHECK(sk_chooser_box(r.c, "profile", s));
    CHECK(sk_chooser_box(r.c, "list", l));
    CHECK(fabs(l[0] - s[0]) < 1e-9 && fabs(l[1] - (s[1] + s[3])) < 1e-9 && fabs(l[2] - s[2]) < 1e-9);
    CHECK(fabs(l[3] - (3 * 20 + 2)) < 1e-9);
    /* The pointer moves the highlight; a press on a row picks it. */
    double x, y;
    point(&r, "list", 0.5, 0.5, &x, &y);
    sk_chooser_result res = mouse(&r, SK_EV_MOUSE_MOVE, x, y, 0, 0, 0);
    CHECK(res.redraw);
    click_at(&r, x, y, 1, 0);
    CHECK_MEMBER(&r, "list", "false");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude-work\"");
    CHECK_MEMBER(&r, "env", "\"CLAUDE_CONFIG_DIR=~/.claude-work\"");
    CHECK_MEMBER(&r, "forget", "true");
    /* A press outside closes it and picks nothing. */
    click(&r, "profile");
    CHECK_MEMBER(&r, "list", "true");
    click(&r, "hint");
    CHECK_MEMBER(&r, "list", "false");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude-work\"");
    /* Pressing the select again closes it too. */
    click(&r, "profile");
    click(&r, "profile");
    CHECK_MEMBER(&r, "list", "false");
    rig_free(&r);
}

static void test_the_select_with_keys(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    /* From the command line: Cancel, Open, the agents, the profile. */
    for (int i = 0; i < 4; i++)
        key(&r, SK_KEY_TAB, 0);
    CHECK_FOCUS(&r, "profile");
    key(&r, SK_KEY_DOWN, 0);
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude-work\"");
    key(&r, SK_KEY_DOWN, 0);
    key(&r, SK_KEY_DOWN, 0);
    CHECK_MEMBER(&r, "profile", "\"/home/user/accounts/lab/.claude\"");
    key(&r, SK_KEY_UP, 0);
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude-work\"");
    key(&r, SK_KEY_HOME, 0);
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude\"");
    key(&r, SK_KEY_END, 0);
    CHECK_MEMBER(&r, "profile", "\"/home/user/accounts/lab/.claude\"");
    /* Alt+Down opens the list; its keys move the highlight; Enter picks
     * and does not open the tab. */
    key(&r, SK_KEY_DOWN, SK_MOD_ALT);
    CHECK_MEMBER(&r, "list", "true");
    key(&r, SK_KEY_UP, 0);
    key(&r, SK_KEY_UP, 0);
    sk_chooser_result res = key(&r, SK_KEY_ENTER, 0);
    CHECK_INT(res.act, SK_CHOOSER_NONE);
    CHECK_MEMBER(&r, "list", "false");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude\"");
    /* Escape closes the list alone, picking nothing. */
    sk_key_event space;
    memset(&space, 0, sizeof space);
    strcpy(space.text, " ");
    sk_chooser_key(r.c, &space);
    CHECK_MEMBER(&r, "list", "true");
    key(&r, SK_KEY_END, 0);
    res = key(&r, SK_KEY_ESCAPE, 0);
    CHECK_INT(res.act, SK_CHOOSER_NONE);
    CHECK(sk_chooser_is_open(r.c));
    CHECK_MEMBER(&r, "list", "false");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude\"");
    /* F4 opens it; Tab picks and goes on. */
    key(&r, SK_KEY_F1 + 3, 0);
    CHECK_MEMBER(&r, "list", "true");
    key(&r, SK_KEY_PAGE_DOWN, 0);
    key(&r, SK_KEY_TAB, 0);
    CHECK_MEMBER(&r, "list", "false");
    CHECK_MEMBER(&r, "profile", "\"/home/user/accounts/lab/.claude\"");
    CHECK_FOCUS(&r, "other");
    rig_free(&r);
}

static void test_the_agents_by_the_pointer_and_the_arrows(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude-work");
    select_all(&r);
    typed(&r, "edited");
    sk_chooser_fail(r.c, "no such folder: ~/x");
    /* pickAgent: its profile, its line, what it does not carry, no
     * error; the folder stays. */
    click(&r, "agent1");
    CHECK_MEMBER(&r, "agent", "\"codex\"");
    CHECK_LINE(&r, "codex");
    CHECK_MEMBER(&r, "dropped", "\"\"");
    CHECK_MEMBER(&r, "error", "null");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.codex\"");
    CHECK_MEMBER(&r, "env", "\"CODEX_HOME unset, the default profile\"");
    CHECK_MEMBER(&r, "folder", "\"~/src/app\"");
    CHECK_FOCUS(&r, "agent");
    double b[4];
    CHECK(!sk_chooser_box(r.c, "dropped", b));
    /* The arrows move through the radios, wrapping. */
    key(&r, SK_KEY_RIGHT, 0);
    CHECK_MEMBER(&r, "agent", "\"claude\"");
    CHECK_LINE(&r, "claude --model opus");
    CHECK_MEMBER(&r, "dropped", "\"--resume 7f3a\"");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude-work\"");
    key(&r, SK_KEY_UP, 0);
    CHECK_MEMBER(&r, "agent", "\"codex\"");
    /* A press on the checked radio's name changes nothing. */
    typed(&r, " ");
    click(&r, "agent1");
    CHECK_MEMBER(&r, "agent", "\"codex\"");
    rig_free(&r);
}

/* The focus after each Tab, from the command line round to it. */
static char *tab_round(rig *r, uint32_t mods)
{
    sk_buf b = {0};
    for (int i = 0; i < 12; i++) {
        key(r, SK_KEY_TAB, mods);
        char *f = member(r, "focus");
        sk_buf_printf(&b, "%s%s", i ? " " : "", f);
        bool back = f && !strcmp(f, "\"line\"");
        free(f);
        if (back)
            break;
    }
    return b.ptr;
}

static void test_tab_and_shift_tab_take_the_controls_in_order(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    /* A disabled Forget is left out. */
    char *got = tab_round(&r, 0);
    CHECK_STR(got, "\"cancel\" \"open\" \"agent\" \"profile\" \"other\" \"folder\" \"choose\" \"line\"");
    free(got);
    got = tab_round(&r, SK_MOD_SHIFT);
    CHECK_STR(got, "\"choose\" \"folder\" \"other\" \"profile\" \"agent\" \"open\" \"cancel\" \"line\"");
    free(got);
    sk_chooser_close(r.c);
    rig_open(&r, "/home/user/.claude-work");
    got = tab_round(&r, 0);
    CHECK_STR(got, "\"cancel\" \"open\" \"agent\" \"profile\" \"other\" \"forget\" \"folder\" \"choose\" \"line\"");
    free(got);
    /* Tab into a field selects all of it. */
    key(&r, SK_KEY_TAB, SK_MOD_SHIFT);
    key(&r, SK_KEY_TAB, SK_MOD_SHIFT);
    CHECK_FOCUS(&r, "folder");
    typed(&r, "/tmp");
    CHECK_MEMBER(&r, "folder", "\"/tmp\"");
    rig_free(&r);
}

/* ---- Open, close, Forget, the picker ---- */

static void test_enter_opens_and_escape_closes(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude-work");
    typed(&r, " -c");
    sk_chooser_result res = key(&r, SK_KEY_ENTER, 0);
    CHECK_INT(res.act, SK_CHOOSER_OPEN);
    CHECK(sk_chooser_is_open(r.c));
    sk_newtab_pick p;
    sk_chooser_pick(r.c, &p);
    CHECK_STR(p.agent, "claude");
    CHECK_STR(p.profile, "/home/user/.claude-work");
    CHECK_STR(p.folder, "~/src/app");
    CHECK_STR(p.line, "claude --model opus -c");
    sk_newtab_pick_free(&p);
    /* The core refused it: the error shows, below the hint, and the
     * buttons move down. */
    double before[4], after[4], hint[4], err[4];
    CHECK(sk_chooser_box(r.c, "open", before));
    CHECK(!sk_chooser_box(r.c, "error", err));
    sk_chooser_fail(r.c, "no such folder: ~/src/app");
    draw(&r);
    CHECK_MEMBER(&r, "error", "\"no such folder: ~/src/app\"");
    CHECK(sk_chooser_box(r.c, "error", err));
    CHECK(sk_chooser_box(r.c, "hint", hint));
    CHECK(sk_chooser_box(r.c, "open", after));
    CHECK(fabs(err[1] - (hint[1] + hint[3] + 6)) < 1e-9);
    CHECK(fabs(err[3] - (12 + 11 * 1.4)) < 1e-9);
    CHECK(fabs(after[1] - (before[1] + err[3] + 6)) < 1e-9);
    /* Enter on the folder field, and on the radios, submits too. */
    click(&r, "folder");
    CHECK_INT(key(&r, SK_KEY_KP_ENTER, 0).act, SK_CHOOSER_OPEN);
    /* Enter or Space on a button presses it. */
    click(&r, "line");
    key(&r, SK_KEY_TAB, 0);
    CHECK_FOCUS(&r, "cancel");
    res = key(&r, SK_KEY_ENTER, 0);
    CHECK_INT(res.act, SK_CHOOSER_CLOSED);
    CHECK(!sk_chooser_is_open(r.c));
    CHECK_MEMBER(&r, "open", "false");
    CHECK(member(&r, "line") == NULL);
    rig_open(&r, "/home/user/.claude");
    res = key(&r, SK_KEY_ESCAPE, 0);
    CHECK_INT(res.act, SK_CHOOSER_CLOSED);
    CHECK(!sk_chooser_is_open(r.c));
    /* The buttons by the pointer. */
    rig_open(&r, "/home/user/.claude");
    CHECK_INT(click(&r, "open").act, SK_CHOOSER_OPEN);
    /* A press that leaves the button before its release presses
     * nothing. */
    double x, y, ox, oy;
    point(&r, "cancel", 0.5, 0.5, &x, &y);
    point(&r, "open", 0.5, 0.5, &ox, &oy);
    mouse(&r, SK_EV_MOUSE_DOWN, x, y, 1, 1, 0);
    CHECK_INT(mouse(&r, SK_EV_MOUSE_UP, ox, oy, 1, 1, 0).act, SK_CHOOSER_NONE);
    CHECK_INT(click(&r, "cancel").act, SK_CHOOSER_CLOSED);
    /* Input to a closed dialog does nothing. */
    CHECK_INT(key(&r, SK_KEY_ENTER, 0).act, SK_CHOOSER_NONE);
    rig_free(&r);
}

/* The sample offer with the work profile forgotten. */
static void forgotten(sk_newtab_state *st)
{
    sample(st, "/home/user/.claude");
    sk_newtab_agent *a = &st->agents[SK_PROFILE_CLAUDE];
    free(a->profiles.ptr[1].dir);
    free(a->profiles.ptr[1].shown);
    a->profiles.ptr[1] = a->profiles.ptr[2];
    a->profiles.len = 2;
}

static void test_forget_keeps_what_was_typed(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude-work");
    typed(&r, " -x");
    click(&r, "folder");
    select_all(&r);
    typed(&r, "~/elsewhere");
    sk_chooser_result res = click(&r, "forget");
    CHECK_INT(res.act, SK_CHOOSER_FORGET);
    sk_newtab_pick p;
    sk_chooser_pick(r.c, &p);
    CHECK_STR(p.agent, "claude");
    CHECK_STR(p.profile, "/home/user/.claude-work");
    sk_newtab_pick_free(&p);
    sk_newtab_state st;
    forgotten(&st);
    sk_chooser_forgot(r.c, &st);
    draw(&r);
    CHECK_MEMBER(&r, "profiles", "[\"~/.claude (default)\",\"~/accounts/lab/.claude\"]");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude\"");
    CHECK_MEMBER(&r, "forget", "false");
    CHECK_LINE(&r, "claude --model opus -x");
    CHECK_MEMBER(&r, "folder", "\"~/elsewhere\"");
    /* The focus leaves the button that is now disabled. */
    CHECK_FOCUS(&r, "profile");
    /* A press on a disabled button does nothing. */
    res = click(&r, "forget");
    CHECK_INT(res.act, SK_CHOOSER_NONE);
    CHECK_FOCUS(&r, "profile");
    rig_free(&r);
}

static void test_choose_and_other_profile_through_the_picker(void)
{
    rig r;
    rig_init(&r, 1, 800, 600);
    rig_open(&r, "/home/user/.claude");
    /* Choose...: titled Folder, from the folder typed when it is one,
     * else the home; its answer written with ~. */
    click(&r, "folder");
    select_all(&r);
    typed(&r, "/definitely/not/here");
    sk_chooser_result res = click(&r, "choose");
    CHECK_INT(res.act, SK_CHOOSER_NONE);
    CHECK_INT(fk.picks, 1);
    CHECK_STR(fk.title.ptr, "Folder");
    CHECK_STR(fk.start.ptr, "/home/user");
    CHECK(!fk.hidden);
    picker_says("/home/user/projects/x", NULL);
    CHECK_MEMBER(&r, "folder", "\"~/projects/x\"");
    CHECK_INT(fk.changes, 1);
    click(&r, "folder");
    select_all(&r);
    typed(&r, "/");
    click(&r, "choose");
    CHECK_STR(fk.start.ptr, "/");
    /* A cancel changes nothing. */
    picker_says(NULL, NULL);
    CHECK_MEMBER(&r, "folder", "\"/\"");
    CHECK_INT(fk.changes, 1);
    /* Other profile...: hidden folders shown, from the home; a profile
     * not listed joins the list, chosen. */
    click(&r, "other");
    CHECK_INT(fk.picks, 3);
    CHECK_STR(fk.title.ptr, "Other profile");
    CHECK_STR(fk.start.ptr, "/home/user");
    CHECK(fk.hidden);
    picker_says("/home/user/.config/claude-x", NULL);
    CHECK_MEMBER(&r, "profiles",
                 "[\"~/.claude (default)\",\"~/.claude-work\",\"~/accounts/lab/.claude\",\"~/.config/claude-x\"]");
    CHECK_MEMBER(&r, "profile", "\"/home/user/.config/claude-x\"");
    CHECK_MEMBER(&r, "env", "\"CLAUDE_CONFIG_DIR=~/.config/claude-x\"");
    CHECK_MEMBER(&r, "forget", "false");
    /* One listed already is chosen, not listed twice. */
    click(&r, "other");
    picker_says("/home/user/.claude-work", NULL);
    CHECK_MEMBER(&r, "profile", "\"/home/user/.claude-work\"");
    CHECK_MEMBER(&r, "profiles",
                 "[\"~/.claude (default)\",\"~/.claude-work\",\"~/accounts/lab/.claude\",\"~/.config/claude-x\"]");
    /* No picker: its reason in the error box. */
    click(&r, "choose");
    picker_says(NULL, "no folder picker: install zenity or kdialog, or type the folder");
    CHECK_MEMBER(&r, "error", "\"no folder picker: install zenity or kdialog, or type the folder\"");
    /* An answer that comes after the dialog closed goes nowhere, even
     * when it opened again. */
    click(&r, "choose");
    sk_chooser_close(r.c);
    rig_open(&r, "/home/user/.claude");
    int changes = fk.changes;
    picker_says("/home/user/late", NULL);
    CHECK_MEMBER(&r, "folder", "\"~/src/app\"");
    CHECK_INT(fk.changes, changes);
    rig_free(&r);
}

/* ---- Layout and pictures ---- */

static void check_box(rig *r, const char *name, double x, double y, double w, double h)
{
    double b[4];
    CHECK(sk_chooser_box(r->c, name, b));
    if (fabs(b[0] - x) > 1e-6 || fabs(b[1] - y) > 1e-6 || fabs(b[2] - w) > 1e-6 || fabs(b[3] - h) > 1e-6)
        sk_test_fail(__FILE__, __LINE__, "%s at %g,%g %gx%g, want %g,%g %gx%g", name, b[0], b[1], b[2], b[3], x, y, w,
                     h);
}

static void test_the_layout_follows_app_css(void)
{
    for (int scale = 1; scale <= 2; scale++) {
        rig r;
        rig_init(&r, scale, 800, 600);
        rig_open(&r, "/home/user/.claude-work");
        /* .dialog: 640 wide, centred, 48 below the overlay's top, which is
         * 22 down; its content 15 in from each side. */
        double b[4];
        CHECK(sk_chooser_box(r.c, "dialog", b));
        CHECK(fabs(b[0] - 80) < 1e-9 && fabs(b[1] - 70) < 1e-9 && fabs(b[2] - 640) < 1e-9);
        double x0 = 95, cw = 610, ch = 12 * 1.6 + 4;
        /* Each row's control 80 in, past the 72 of its name and the gap
         * of 8; the buttons at the right; inputs and buttons 23.2 high. */
        double profile_y;
        point(&r, "profile", 0, 0, &x0, &profile_y);
        CHECK(fabs(x0 - 175) < 1e-9);
        double forget[4], other[4], prof[4];
        CHECK(sk_chooser_box(r.c, "forget", forget));
        CHECK(sk_chooser_box(r.c, "other", other));
        CHECK(sk_chooser_box(r.c, "profile", prof));
        CHECK(fabs(forget[0] + forget[2] - (95 + cw)) < 1e-9);
        CHECK(fabs(other[0] + other[2] + 8 - forget[0]) < 1e-9);
        CHECK(fabs(prof[0] + prof[2] + 8 - other[0]) < 1e-9);
        CHECK(fabs(prof[3] - ch) < 1e-9 && fabs(forget[3] - ch) < 1e-9);
        double env[4], folder[4], line[4], dropped[4], hint[4], open[4], cancel[4];
        CHECK(sk_chooser_box(r.c, "env", env));
        CHECK(sk_chooser_box(r.c, "folder", folder));
        CHECK(sk_chooser_box(r.c, "line", line));
        CHECK(sk_chooser_box(r.c, "dropped", dropped));
        CHECK(sk_chooser_box(r.c, "hint", hint));
        CHECK(sk_chooser_box(r.c, "open", open));
        CHECK(sk_chooser_box(r.c, "cancel", cancel));
        /* The hints 80 in and 11px at 1.6 a line, rows 6 apart. */
        check_box(&r, "env", 175, prof[1] + ch + 6, 530, 11 * 1.6);
        check_box(&r, "folder", 175, env[1] + env[3] + 6, folder[2], ch);
        check_box(&r, "line", 175, folder[1] + ch + 6, 530, ch);
        check_box(&r, "dropped", 175, line[1] + ch + 6, 530, 11 * 1.6);
        check_box(&r, "hint", 175, dropped[1] + dropped[3], 530, 11 * 1.6);
        CHECK(fabs(open[1] - (hint[1] + hint[3] + 10)) < 1e-9);
        CHECK(fabs(open[0] + open[2] - 705) < 1e-9);
        CHECK(fabs(cancel[0] + cancel[2] + 8 - open[0]) < 1e-9);
        CHECK(fabs(b[1] + b[3] - (open[1] + ch + 12 + 1)) < 1e-9);
        /* The title 20.8 high and its margin of 6 above the agents. */
        double agent[4], radio[4];
        CHECK(sk_chooser_box(r.c, "agent0", agent));
        CHECK(sk_chooser_box(r.c, "radio0", radio));
        CHECK(fabs(agent[1] - (70 + 1 + 10 + 13 * 1.6 + 6)) < 1e-9);
        CHECK(fabs(agent[0] - 175) < 1e-9);
        CHECK(fabs(radio[0] - 180) < 1e-9 && fabs(radio[2] - 13) < 1e-9);
        double agent1[4];
        CHECK(sk_chooser_box(r.c, "agent1", agent1));
        CHECK(fabs(agent1[0] - (agent[0] + agent[2] + 18)) < 1e-9);
        CHECK(fabs(prof[1] - (agent[1] + agent[3] + 6)) < 1e-9);
        rig_free(&r);
    }
    /* A narrow window: 100% less 32. */
    rig r;
    rig_init(&r, 1, 500, 400);
    rig_open(&r, "/home/user/.claude");
    double b[4];
    CHECK(sk_chooser_box(r.c, "dialog", b));
    CHECK(fabs(b[0] - 16) < 1e-9 && fabs(b[1] - 70) < 1e-9 && fabs(b[2] - 468) < 1e-9);
    rig_free(&r);
}

static void test_the_dialog_drawn_at_1x_and_2x(void)
{
    for (int scale = 1; scale <= 2; scale++) {
        rig r;
        rig_init(&r, scale, 760, 420);
        rig_open(&r, "/home/user/.claude-work");
        double x, y;
        point(&r, "choose", 0.5, 0.5, &x, &y);
        mouse(&r, SK_EV_MOUSE_MOVE, x, y, 0, 0, 0);
        draw(&r);
        CHECK_GOLDEN(scale == 1 ? "app/chooser_1x" : "app/chooser_2x", &r.im, 2);
        rig_free(&r);
    }
}

static void test_an_error_wraps_as_pre_wrap_and_overflow_wrap_take_it(void)
{
    rig r;
    rig_init(&r, 1, 760, 420);
    rig_open(&r, "/home/user/.claude");
    /* Its own break, a break at a space, and a path with none broken
     * where it must be: five lines in 518 points of 11px mono. */
    sk_chooser_fail(r.c, "no such profile folder: ~/accounts/lab/.claude\nsecond line of the error that is long enough "
                         "to wrap around the box at least once, surely: "
                         "/a/very/long/path/without/any/spaces/in/it/at/all/which/must/break/anywhere/to/fit");
    draw(&r);
    double b[4];
    CHECK(sk_chooser_box(r.c, "error", b));
    CHECK(fabs(b[3] - (12 + 5 * 11 * 1.4)) < 1e-9);
    rig_free(&r);
}

const sk_test sk_suite_chooser[] = {
    T(test_an_error_wraps_as_pre_wrap_and_overflow_wrap_take_it),
    T(test_typing_moves_and_deletes),
    T(test_shift_selects_and_the_clipboard_cuts_copies_and_pastes),
    T(test_word_moves_and_deletes),
    T(test_chords_type_nothing_and_named_keys_nothing_more),
    T(test_the_pointer_places_the_caret_and_selects),
    T(test_an_input_method_types_and_composes),
    T(test_a_long_line_scrolls_to_its_caret),
    T(test_the_select_opens_and_picks_with_the_pointer),
    T(test_the_select_with_keys),
    T(test_the_agents_by_the_pointer_and_the_arrows),
    T(test_tab_and_shift_tab_take_the_controls_in_order),
    T(test_enter_opens_and_escape_closes),
    T(test_forget_keeps_what_was_typed),
    T(test_choose_and_other_profile_through_the_picker),
    T(test_the_layout_follows_app_css),
    T(test_the_dialog_drawn_at_1x_and_2x),
    {0},
};
