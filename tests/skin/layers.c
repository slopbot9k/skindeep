/* The layers skinc pre-renders (decision 0037, skinc/layers.h) and the
 * runtime composites (src/skin/paint.c, view.c): which elements of each
 * shipped skin get which kind; a layer's pixels against its subtree
 * painted where the runtime lays it out; a whole frame drawn from its
 * layers against the same frame painted; the pictures the view keeps
 * of the panels that stretch; and the subtrees small skins show must
 * be painted. */
#include "../../skinc/front.h"
#include "../../skinc/write.h"
#include "../../src/skin/compile.h"
#include "../../src/skin/shipped.h"
#include "../../src/skin/viewint.h"
#include "../skinc/util.h"
#include "../test.h"

#include <math.h>
#include <string.h>

#define WALL 1700000000.0

static double fixed_wall(void *ud)
{
    (void)ud;
    return WALL;
}

static sk_skinpack *shipped(const char *name)
{
    const sk_embedded_file *e = sk_shipped_pack(name);
    char err[300];
    sk_skinpack *p = e ? sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err) : NULL;
    if (!p)
        sk_test_fail(__FILE__, __LINE__, "%s: %s", name, e ? err : "not shipped");
    return p;
}

/* The kinds of element e's layers, a bit (1 << sk_layer_kind) each. */
static unsigned kinds(const sk_pack *p, uint32_t e)
{
    unsigned k = 0;
    for (uint32_t i = p->els[e].layer; i < p->els[e].layer + p->els[e].nlayers; i++)
        k |= 1u << p->layers[i].kind;
    return k;
}

#define K(kind) (1u << SK_LAYER_##kind)

/* Element el's layers are of the kinds want. */
#define CHECK_KINDS(p, el, want, what) check_kinds(__FILE__, __LINE__, (p), (el), (want), (what))

static void check_kinds(const char *file, int line, const sk_pack *p, const sk_el *el, unsigned want,
                        const char *what)
{
    if (!el) {
        sk_test_fail(file, line, "%s: no such element", what);
        return;
    }
    unsigned got = kinds(p, el->index);
    if (got != want)
        sk_test_fail(file, line, "%s: layers of kinds 0x%x, wanted 0x%x", what, got, want);
}

/* A panel's ::before or ::after, or a widget part's. */
static const sk_el *pseudo(const sk_el *e, bool after)
{
    return e ? (after ? e->after : e->before) : NULL;
}

typedef struct {
    sk_front f;
    sk_skinpack *sp;
    const sk_pack *p;
} skin;

static bool skin_open(skin *s, const char *name)
{
    char dir[64];
    snprintf(dir, sizeof dir, "skins/%s", name);
    sk_diags d = {0};
    bool ok = sk_front_run(&s->f, dir, &d);
    sk_diags_free(&d);
    s->sp = ok ? shipped(name) : NULL;
    s->p = s->sp ? &s->sp->pack : NULL;
    if (!s->sp) {
        sk_front_free(&s->f);
        return false;
    }
    return true;
}

static void skin_close(skin *s)
{
    sk_skinpack_unref(s->sp);
    sk_front_free(&s->f);
}

/* No element showing what only the runtime knows has a layer, but a
 * knob's turning group, turned as a SPRITE. */
static void values_bare(const sk_pack *p, sk_skinpack *sp)
{
    sk_skinview *v = sk_skinview_new(sp, 0);
    for (uint32_t e = 0; e < p->nels; e++) {
        unsigned k = kinds(p, e);
        if (!k || !sk_view_shows_value(v, e))
            continue;
        if (p->els[e].role == SK_ROLE_KNOB_TURN && k == K(SPRITE))
            continue;
        sk_test_fail(__FILE__, __LINE__, "%s: element %u shows a value and has layers 0x%x", p->meta.name,
                     (unsigned)e, k);
    }
    sk_skinview_free(v);
}

static void test_coffee_kinds(void)
{
    skin s;
    if (!skin_open(&s, "coffee"))
        return;
    static const char *const STATIC[] = {"cork", "deck", "amp", "shelf", "plant", "typewriter", "doodles", "doodle",
                                         "board-title"};
    for (size_t i = 0; i < SK_LEN(STATIC); i++)
        CHECK_KINDS(s.p, sk_front_panel(&s.f, STATIC[i]), K(STATIC), STATIC[i]);
    static const char *const NINE[] = {"frame", "slate", "cabinet", "sash", "tabletop", "cloth"};
    for (size_t i = 0; i < SK_LEN(NINE); i++)
        CHECK_KINDS(s.p, sk_front_panel(&s.f, NINE[i]), K(NINE), NINE[i]);
    /* The room stretches and has no shadow: the view keeps it. */
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "room"), 0, "room");
    /* The record turns; the knob's image turns with its group. */
    CHECK_KINDS(s.p, sk_front_find(&s.f, "spin", SK_ROLE_LAMP, 0), K(SPRITE), "the record");
    CHECK_KINDS(s.p, sk_front_find(&s.f, "cpu", SK_ROLE_KNOB_TURN, 0), K(SPRITE), "the cpu knob");
    /* The lava lamp: its body still, its blobs rising when it is on,
     * its glow there only then. */
    const sk_el *lamp = sk_front_find(&s.f, "brewing", SK_ROLE_LAMP, 0);
    CHECK_KINDS(s.p, lamp, K(STATE), "the lava lamp");
    CHECK_KINDS(s.p, pseudo(lamp, false), K(STATE) | K(SPRITE), "a blob");
    CHECK_KINDS(s.p, pseudo(lamp, true), K(STATE) | K(SPRITE), "a blob");
    CHECK_KINDS(s.p, pseudo(sk_front_find(&s.f, "brewing", SK_ROLE_WIDGET, 0), false), K(STATE), "the lamp's glow");
    /* The VU meter's face is still, its needle a path the painter
     * turns. */
    CHECK_KINDS(s.p, sk_front_find(&s.f, "vu", SK_ROLE_FACE, 0), K(STATIC), "the VU face");
    CHECK_KINDS(s.p, sk_front_find(&s.f, "vu", SK_ROLE_NEEDLE, 0), 0, "the VU needle");
    CHECK_KINDS(s.p, sk_front_find(&s.f, "context", SK_ROLE_ARC_TRACK, 0), K(STATIC), "the platter's track");
    CHECK_KINDS(s.p, sk_front_find(&s.f, "context", SK_ROLE_ARC_FILL, 0), 0, "the platter's fill");
    values_bare(s.p, s.sp);
    skin_close(&s);
}

static void test_deck_kinds(void)
{
    skin s;
    if (!skin_open(&s, "deck"))
        return;
    const sk_el *lcd = sk_front_panel(&s.f, "lcd");
    CHECK_KINDS(s.p, lcd, K(NINE), "an lcd");
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "plate"), K(NINE), "a plate");
    /* The body stretches with a gradient and no shadow: the view keeps it. */
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "body"), 0, "the body");
    /* Every lamp lights in a picture of each state. */
    for (uint32_t e = 0; e < s.f.els.len; e++) {
        const sk_el *el = s.f.els.ptr[e];
        if (el->role == SK_ROLE_LAMP)
            CHECK_KINDS(s.p, el, K(STATE), "a lamp");
        if (el->role == SK_ROLE_OPTION_LIST || el->role == SK_ROLE_OPTION_OTHER)
            CHECK_KINDS(s.p, el, K(STATE), "a tab");
    }
    values_bare(s.p, s.sp);
    skin_close(&s);
}

static void test_library_kinds(void)
{
    skin s;
    if (!skin_open(&s, "library"))
        return;
    static const char *const STATIC[] = {"catalog", "drawer", "pull", "plaque", "shelf-end"};
    for (size_t i = 0; i < SK_LEN(STATIC); i++)
        CHECK_KINDS(s.p, sk_front_panel(&s.f, STATIC[i]), K(STATIC), STATIC[i]);
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "frame"), K(NINE), "a frame");
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "whiteboard"), K(NINE), "the whiteboard");
    /* The brass knob turns its image. */
    uint32_t turns = 0;
    for (uint32_t e = 0; e < s.f.els.len; e++) {
        const sk_el *el = s.f.els.ptr[e];
        if (el->role == SK_ROLE_KNOB_TURN) {
            CHECK_KINDS(s.p, el, K(SPRITE), "a knob");
            turns++;
        }
        if (el->role == SK_ROLE_TICK)
            CHECK_KINDS(s.p, el, K(STATIC), "a tick");
    }
    CHECK(turns > 0);
    values_bare(s.p, s.sp);
    skin_close(&s);
}

static void test_phosphor_kinds(void)
{
    skin s;
    if (!skin_open(&s, "phosphor"))
        return;
    static const char *const NINE[] = {"casing", "screen", "bezel", "header"};
    for (size_t i = 0; i < SK_LEN(NINE); i++)
        CHECK_KINDS(s.p, sk_front_panel(&s.f, NINE[i]), K(NINE), NINE[i]);
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "heading"), K(STATIC), "a heading");
    /* The scanlines stretch over the screen and flicker: neither a
     * layer nor a picture the view keeps, but the group it blends. */
    CHECK_KINDS(s.p, sk_front_panel(&s.f, "scanlines"), 0, "the scanlines");
    for (uint32_t e = 0; e < s.f.els.len; e++) {
        const sk_el *el = s.f.els.ptr[e];
        /* A bar's segments; a limit's meter, shown by count, has none. */
        if (el->role == SK_ROLE_SEG && !el->limits)
            CHECK_KINDS(s.p, el, K(STATE), "a segment");
        if (el->role == SK_ROLE_SEG && el->limits)
            CHECK_KINDS(s.p, el, 0, "a limit's segment");
        if (el->role == SK_ROLE_LAMP)
            CHECK_KINDS(s.p, el, K(STATE), "a lamp");
    }
    values_bare(s.p, s.sp);
    skin_close(&s);
}

/* ---- Pixels ---- */

/* The view of a shipped skin at the skin's size at scale, every
 * variable 0, as skinc laid it out. */
static sk_skinview *bare_view(sk_skinpack *sp, double scale)
{
    sk_skin_window w;
    sk_skinpack_window(sp, &w);
    sk_skinview *v = sk_skinview_new(sp, 0);
    v->wall_fn = fixed_wall;
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w.width, w.height - SK_VIEW_STRIP, scale);
    sk_view_run(v);
    return v;
}

static int px_diff(sk_px a, sk_px b)
{
    int d = 0;
    for (int sh = 0; sh < 32; sh += 8)
        d = SK_MAX(d, abs((int)((a >> sh) & 255) - (int)((b >> sh) & 255)));
    return d;
}

/* Each layer of each scale of a skin whose element shows in its style
 * at the skin's size: its pixels the subtree painted there, or for a
 * nine-slice its box's shadows and border painted at the box's size,
 * the slice stretched to it. */
static void layers_paint_alike(const char *name)
{
    sk_skinpack *sp = shipped(name);
    if (!sp)
        return;
    const sk_pack *p = &sp->pack;
    sk_skinview *v = bare_view(sp, 1);
    sk_glyph_cache *gc = sk_glyph_cache_new(8u << 20);
    sk_paint_env env = {&v->layout, &v->lfonts, &v->images, gc, NULL, NULL};
    int checked = 0;
    for (uint32_t i = 0; i < p->nlayers; i++) {
        const sk_pack_layer *y = &p->layers[i];
        const sk_vnode *x = &v->nodes[y->el];
        const sk_lbox *b = &v->layout.boxes[y->el];
        if (!x->live || !x->base || !b->generated || (y->style != SK_PACK_NONE && y->style != (uint32_t)(x->base - p->styles)))
            continue;
        sk_image want, got;
        if (!sk_qoi_decode(p->blob + y->blob, y->len, SK_PACK_MAX_PIXELS, &got))
            continue;
        sk_image_init(&want, y->w, y->h);
        float s = y->scale;
        if (y->kind == SK_LAYER_NINE) {
            /* The box at the size it was sliced for. */
            sk_layout lr = v->layout;
            lr.boxes = sk_xreallocarray(NULL, lr.n, sizeof *lr.boxes);
            memcpy(lr.boxes, v->layout.boxes, lr.n * sizeof *lr.boxes);
            sk_lbox *rb = &lr.boxes[y->el];
            float dw = rb->border.w - y->ox, dh = rb->border.h - y->oy;
            rb->border.w -= dw;
            rb->border.h -= dh;
            rb->padding.w -= dw;
            rb->padding.h -= dh;
            rb->content.w -= dw;
            rb->content.h -= dh;
            sk_paint_env re = env;
            re.layout = &lr;
            sk_xform own = {s, 0, 0, s, -(b->border.x + y->x) * s, -(b->border.y + y->y) * s};
            sk_paint_layer(&want, &re, y->el, SK_PART_NINE, &own);
            free(lr.boxes);
        } else {
            /* The layer's top left, from its anchor's box. */
            uint32_t a = y->el;
            while (v->layout.boxes[a].svg_content)
                a = v->lnodes[a].parent;
            const sk_lbox *ab = &v->layout.boxes[a];
            sk_xform d0 = {s, 0, 0, s, -(ab->border.x + y->x) * s, -(ab->border.y + y->y) * s};
            if (a != y->el)
                d0 = sk_xform_mul(&d0, &ab->viewbox);
            sk_paint_layer(&want, &env, y->el, SK_PART_SUBTREE, &d0);
        }
        int worst = 0;
        long off = 0;
        for (int r = 0; r < want.h; r++)
            for (int c = 0; c < want.w; c++) {
                int d = px_diff(sk_image_row(&want, r)[c], sk_image_row(&got, r)[c]);
                worst = SK_MAX(worst, d);
                off += d > 1;
            }
        if (off)
            sk_test_fail(__FILE__, __LINE__, "%s: layer %u (element %u, kind %u, %ux): %ld pixels apart, worst %d", name,
                         (unsigned)i, (unsigned)y->el, (unsigned)y->kind, (unsigned)y->scale, off, worst);
        checked++;
        sk_image_free(&want);
        sk_image_free(&got);
    }
    CHECK(checked > 0);
    sk_glyph_cache_free(gc);
    sk_skinview_free(v);
    sk_skinpack_unref(sp);
}

static void test_layer_pixels(void)
{
    layers_paint_alike("coffee");
    layers_paint_alike("deck");
    layers_paint_alike("library");
    layers_paint_alike("phosphor");
}

/* A busy session's frame drawn from the layers and the pictures the
 * view keeps, against the same frame painted node by node: a blur or a
 * resampled picture may round a little apart, as the live goldens
 * allow, no more. At a scale between 1x and 2x the 2x layers are
 * resampled, and an edge the painter would put on a whole pixel lands
 * between two: a thin line or a glyph's stem may then differ by much,
 * in up to 3 pixels in a thousand. */
/* How many pixels in a million of sp's frame at scale, the terminal's
 * left out, lie more than 24 apart drawn with and without layers; the
 * worst difference in *worst. */
static long frame_apart(sk_skinpack *sp, double scale, int *worst_out)
{
    sk_skin_window w;
    sk_skinpack_window(sp, &w);
    sk_skinview *v = sk_skinview_new(sp, 0);
    v->wall_fn = fixed_wall;
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w.width, w.height - SK_VIEW_STRIP, scale);
    const sk_skin_metric m[] = {
        {"state.busy", {SK_VAL_NUM, 1, NULL}},     {"pty.out_bps", {SK_VAL_NUM, 400, NULL}},
        {"cost.usd", {SK_VAL_NUM, 1.5, NULL}},     {"proc.cpu_pct", {SK_VAL_NUM, 35, NULL}},
        {"tokens.out_rate", {SK_VAL_NUM, 60, NULL}}, {"context.used_pct", {SK_VAL_NUM, 42, NULL}},
    };
    sk_skinview_set_metrics(v, m, SK_LEN(m));
    sk_image a, b;
    sk_image_init(&a, (int)(w.width * scale), (int)(w.height * scale));
    sk_image_init(&b, a.w, a.h);
    sk_rect r[8];
    for (int i = 1; i <= 90; i++) {
        sk_skinview_step(v, i * 1000.0 / 60);
        sk_skinview_draw(v, &a, false, r, 8);
    }
    sk_skinview_draw(v, &a, true, r, 8);
    v->layer_hook.find = NULL;
    sk_skinview_draw(v, &b, true, r, 8);
    sk_rect term = sk_skinview_terminal_px(v);
    long off = 0;
    int worst = 0;
    for (int y = 0; y < a.h; y++)
        for (int x = 0; x < a.w; x++) {
            if (x >= term.x && x < term.x + term.w && y >= term.y && y < term.y + term.h)
                continue;
            int d = px_diff(sk_image_row(&a, y)[x], sk_image_row(&b, y)[x]);
            worst = SK_MAX(worst, d);
            off += d > 24;
        }
    long ppm = off * 1000000L / ((long)a.w * a.h);
    sk_image_free(&a);
    sk_image_free(&b);
    sk_skinview_free(v);
    *worst_out = worst;
    return ppm;
}

static void frame_alike(const char *name, double scale)
{
    sk_skinpack *sp = shipped(name);
    if (!sp)
        return;
    int worst = 0;
    long ppm = frame_apart(sp, scale, &worst);
    sk_skinpack_unref(sp);
    if (ppm > (scale == floor(scale) ? 300 : 3000))
        sk_test_fail(__FILE__, __LINE__, "%s at %gx: %ld pixels in a million more than 24 apart (worst %d)", name, scale,
                     ppm, worst);
}

/* The view draws no layer of a scale below the frame's, which would be
 * blurred up, and none its subtree no longer fits, as when a box grew
 * in a state no layer was drawn in: those subtrees paint live. deck at
 * 2x with its 2x layers made unusable, then at 1x with its 1x ones moved
 * 10 px off their boxes. */
static void test_layers_refused_where_drawn(void)
{
    for (int moved = 0; moved < 2; moved++) {
        sk_skinpack *sp = shipped("deck");
        if (!sp)
            return;
        sk_pack_layer *ls = (sk_pack_layer *)sp->pack.layers;
        for (uint32_t i = 0; i < sp->pack.nlayers; i++) {
            if (ls[i].scale == 2)
                ls[i].kind = SK_LAYER_MASK;
            else if (moved)
                ls[i].x += 10;
        }
        double scale = moved ? 1 : 2;
        int worst = 0;
        long ppm = frame_apart(sp, scale, &worst);
        if (ppm > 300)
            sk_test_fail(__FILE__, __LINE__, "deck at %gx: %ld pixels in a million more than 24 apart (worst %d)",
                         scale, ppm, worst);
        sk_skinpack_unref(sp);
    }
}

static void test_frame_alike(void)
{
    frame_alike("coffee", 1);
    frame_alike("coffee", 2);
    frame_alike("deck", 2);
    frame_alike("library", 2);
    frame_alike("phosphor", 1);
    frame_alike("phosphor", 2);
    /* A scale between, where layers are resampled. */
    frame_alike("library", 1.5);
}

/* ---- Pictures the view keeps ---- */

static void test_kept(void)
{
    sk_skinpack *sp = shipped("coffee");
    if (!sp)
        return;
    sk_skinview *v = sk_skinview_new(sp, 0);
    sk_skinpack_unref(sp);
    v->wall_fn = fixed_wall;
    sk_skin_window w;
    sk_skinpack_window(sp, &w);
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w.width, w.height - SK_VIEW_STRIP, 2);
    /* The room: the first panel, kept. */
    uint32_t room = SK_PACK_NONE;
    for (uint32_t e = 0; e < v->p->nels && room == SK_PACK_NONE; e++)
        if (v->p->els[e].role == SK_ROLE_PANEL)
            room = e;
    CHECK(room != SK_PACK_NONE && v->kept_of[room] != SK_PACK_NONE);
    if (room == SK_PACK_NONE || v->kept_of[room] == SK_PACK_NONE) {
        sk_skinview_free(v);
        return;
    }
    const sk_vkept *k = &v->kept[v->kept_of[room]];
    sk_image f;
    sk_image_init(&f, (int)(w.width * 2), (int)(w.height * 2));
    sk_rect r[8];
    /* The first run paints it straight; the next keeps it. */
    sk_skinview_step(v, 16);
    sk_skinview_draw(v, &f, true, r, 8);
    CHECK(!k->valid && !(v->layer_ok[room] & SK_VLAYER_KEPT));
    v->dirty = true;
    sk_skinview_step(v, 32);
    sk_skinview_draw(v, &f, true, r, 8);
    CHECK(k->valid && (v->layer_ok[room] & SK_VLAYER_KEPT));
    CHECK(k->im.w >= (int)(w.width * 2) - 1 && k->im.h >= (int)((w.height - SK_VIEW_STRIP) * 2) - 1);
    /* A resize: painted straight again, then kept at its new size. */
    sk_skinview_set_stage(v, 0, SK_VIEW_STRIP, w.width + 40, w.height - SK_VIEW_STRIP, 2);
    sk_skinview_step(v, 48);
    CHECK(!k->valid && !(v->layer_ok[room] & SK_VLAYER_KEPT));
    sk_skinview_draw(v, &f, true, r, 8);
    v->dirty = true;
    sk_skinview_step(v, 64);
    sk_image g;
    sk_image_init(&g, (int)((w.width + 40) * 2), (int)(w.height * 2));
    sk_skinview_draw(v, &g, true, r, 8);
    CHECK(k->valid && k->im.w >= (int)((w.width + 40) * 2) - 1);
    sk_image_free(&f);
    sk_image_free(&g);
    sk_skinview_free(v);
}

/* ---- What small skins must leave to the painter ---- */

/* A skin compiled as skinc writes it. */
static bool compiled(const char *panels, const char *widgets, const char *css, sk_buf *out, sk_front *f)
{
    const char *dir = sk_test_skin(panels, widgets, css);
    sk_diags d = {0};
    unsigned char hash[32];
    char err[300];
    bool ok = sk_front_run(f, dir, &d) && sk_skin_hash(dir, hash, err, sizeof err) && sk_pack_write(f, hash, out, &d);
    for (size_t i = 0; i < d.items.len; i++)
        sk_test_fail(__FILE__, __LINE__, "%s", d.items.ptr[i]);
    sk_diags_free(&d);
    return ok;
}

/* The kinds of the layers of panel klass in a skin of panels and css. */
static unsigned panel_kinds(const char *panels, const char *widgets, const char *css, const char *klass)
{
    sk_front f;
    sk_buf out = {0};
    unsigned k = ~0u;
    if (compiled(panels, widgets, css, &out, &f)) {
        sk_pack p;
        char err[300];
        const sk_el *el = sk_front_panel(&f, klass);
        if (el && sk_pack_load((const uint8_t *)out.ptr, out.len, &p, err, sizeof err)) {
            k = kinds(&p, el->index);
            sk_pack_free(&p);
        }
    }
    sk_front_free(&f);
    sk_buf_free(&out);
    return k;
}

#define BOX "[{\"class\": \"a\", \"rect\": {\"left\": 10, \"top\": 10, \"width\": 40, \"height\": 30}}]"
#define WIDE "[{\"class\": \"a\", \"rect\": {\"left\": 10, \"right\": 10, \"top\": 10, \"height\": 30}}]"

static void test_small_skins(void)
{
    /* Still, rounded: a STATIC layer. */
    CHECK_INT(panel_kinds(BOX, NULL, ".a { background: #345; border-radius: 4px; }", "a"), K(STATIC));
    /* Of one colour: as fast to paint as to copy. */
    CHECK_INT(panel_kinds(BOX, NULL, ".a { background: #345; }", "a"), 0);
    /* Lit by the pointer: a picture for each. */
    CHECK_INT(panel_kinds(BOX, NULL, ".a { background: #345; border-radius: 4px; } .a:hover { background: #567; }",
                          "a"),
              K(STATE));
    /* Its opacity and blend are the compositor's: one picture. */
    CHECK_INT(panel_kinds(BOX, NULL, ".a { background: #345; border-radius: 4px; } .a:hover { opacity: .5; }", "a"),
              K(STATIC));
    /* Turned by an animation: a SPRITE; turned for good: painted. */
    CHECK_INT(panel_kinds(BOX, NULL,
                          ".a { background: #345; border-radius: 4px; animation: t 1s infinite; }"
                          " @keyframes t { to { transform: rotate(90deg); } }",
                          "a"),
              K(SPRITE));
    CHECK_INT(panel_kinds(BOX, NULL, ".a { background: #345; border-radius: 4px; transform: rotate(3deg); }", "a"), 0);
    /* Something in it animates: painted. */
    CHECK_INT(panel_kinds(BOX, NULL,
                          ".a { background: #345; border-radius: 4px; }"
                          " .a::after { content: \"\"; position: absolute; inset: 4px; background: #fff;"
                          " animation: f 1s infinite; } @keyframes f { to { opacity: 0; } }",
                          "a"),
              0);
    /* Something in it blends with what lies below it: painted. */
    CHECK_INT(panel_kinds(BOX, NULL,
                          ".a { background: #345; border-radius: 4px; }"
                          " .a::after { content: \"\"; position: absolute; inset: 4px; background: #fff;"
                          " mix-blend-mode: multiply; }",
                          "a"),
              0);
    /* Something in it stacks above its neighbours: painted. */
    CHECK_INT(panel_kinds(BOX, NULL,
                          ".a { background: #345; border-radius: 4px; }"
                          " .a::after { content: \"\"; position: absolute; inset: 4px; background: #fff; z-index: 2; }",
                          "a"),
              0);
    /* It stretches: its shadow a nine-slice, the rest the runtime's. */
    CHECK_INT(panel_kinds(WIDE, NULL, ".a { background: #345; box-shadow: 0 2px 6px #000; }", "a"), K(NINE));
    CHECK_INT(panel_kinds(WIDE, NULL, ".a { background: #345; border-radius: 4px; }", "a"), 0);
    /* Its radius a percentage: a nine-slice cannot hold it. */
    CHECK_INT(panel_kinds(WIDE, NULL, ".a { background: #345; border-radius: 10%; box-shadow: 0 2px 6px #000; }", "a"),
              0);
}

const sk_test sk_suite_skin_layers[] = {
    T(test_coffee_kinds), T(test_deck_kinds),  T(test_library_kinds), T(test_phosphor_kinds), T(test_layer_pixels),
    T(test_frame_alike),  T(test_layers_refused_where_drawn), T(test_kept),        T(test_small_skins),
    {0},
};
