/* Skin packs (decision 0037): each shipped skin written by skinc's
 * writer and read back by the loader equal to what the front end
 * computed, element by element, style by style, field by field; the
 * same skin giving the same bytes; the packs linked into the program
 * loading and being those bytes; and every check of the loader
 * refusing a small pack spoiled to fail it, with the message that
 * names the section and the record. A newer minor's pack, with larger
 * records, a section and a property this loader does not know, loads.
 *
 * SK_SEED_UPDATE=1 writes the fuzz target's seeds, tests/fuzz/corpus/
 * pack/, from the small packs these tests make. */
#include "../../skinc/media.h"
#include "../../skinc/write.h"
#include "../../src/audio/adpcm.h"
#include "../../src/base/paths.h"
#include "../../src/skin/compile.h"
#include "../../src/skin/packfmt.h"
#include "../../src/skin/shipped.h"
#include "../skinc/mediawrite.h"
#include "../skinc/util.h"
#include "../test.h"
#include "../text/tinyfont.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* ---- Styles compared field by field ---- */

static bool str_same(const char *a, const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a, b));
}

static bool len_same(const sk_css_len *a, const sk_css_len *b)
{
    return a->kind == b->kind && a->px == b->px && a->pct == b->pct;
}

static bool color_same(const sk_css_color *a, const sk_css_color *b)
{
    return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

static bool timing_same(const sk_css_timing *a, const sk_css_timing *b)
{
    return a->kind == b->kind && a->steps_pos == b->steps_pos && a->steps == b->steps && a->x1 == b->x1 &&
           a->y1 == b->y1 && a->x2 == b->x2 && a->y2 == b->y2;
}

static bool shadow_same(const sk_css_shadow *a, const sk_css_shadow *b)
{
    return a->x == b->x && a->y == b->y && a->blur == b->blur && a->spread == b->spread &&
           color_same(&a->color, &b->color) && a->inset == b->inset;
}

static bool xf_same(const sk_css_xf *a, const sk_css_xf *b)
{
    if (a->kind != b->kind || !len_same(&a->tx, &b->tx) || !len_same(&a->ty, &b->ty))
        return false;
    for (int i = 0; i < 6; i++)
        if (a->v[i] != b->v[i])
            return false;
    return true;
}

static bool filter_same(const sk_css_filter *a, const sk_css_filter *b)
{
    return a->kind == b->kind && a->amount == b->amount && shadow_same(&a->shadow, &b->shadow);
}

static bool image_same(const sk_css_image *a, const sk_css_image *b)
{
    if (a->kind != b->kind)
        return false;
    if (a->kind == SK_IMAGE_URL)
        return a->source == b->source;
    if (a->kind != SK_IMAGE_GRADIENT)
        return true;
    const sk_css_gradient *x = a->gradient, *y = b->gradient;
    if (x->kind != y->kind || x->repeating != y->repeating || x->circle != y->circle || x->extent != y->extent ||
        x->to_corner != y->to_corner || x->angle != y->angle || !len_same(&x->rx, &y->rx) ||
        !len_same(&x->ry, &y->ry) || !len_same(&x->cx, &y->cx) || !len_same(&x->cy, &y->cy) ||
        x->nstops != y->nstops)
        return false;
    for (uint16_t i = 0; i < x->nstops; i++)
        if (!color_same(&x->stops[i].color, &y->stops[i].color) || !len_same(&x->stops[i].pos, &y->stops[i].pos))
            return false;
    return true;
}

static bool position_same(const sk_css_position *a, const sk_css_position *b)
{
    return len_same(&a->x, &b->x) && len_same(&a->y, &b->y) && a->x_edge == b->x_edge && a->y_edge == b->y_edge;
}

static bool bgsize_same(const sk_css_bgsize *a, const sk_css_bgsize *b)
{
    return a->kind == b->kind && len_same(&a->w, &b->w) && len_same(&a->h, &b->h);
}

static bool track_same(const sk_css_track *a, const sk_css_track *b)
{
    return a->kind == b->kind && len_same(&a->len, &b->len) && a->fr == b->fr;
}

static bool line_same(const sk_css_line *a, const sk_css_line *b)
{
    return a->kind == b->kind && a->n == b->n;
}

static bool content_same(const sk_css_content *a, const sk_css_content *b)
{
    return a->kind == b->kind && str_same(a->text, b->text) && a->base == b->base && a->step == b->step;
}

static bool string_same(const char *const *a, const char *const *b)
{
    return str_same(*a, *b);
}

static bool u8_same(const uint8_t *a, const uint8_t *b)
{
    return *a == *b;
}

static bool u16_same(const uint16_t *a, const uint16_t *b)
{
    return *a == *b;
}

static bool u32_same(const uint32_t *a, const uint32_t *b)
{
    return *a == *b;
}

static bool f32_same(const float *a, const float *b)
{
    return *a == *b || (isinf(*a) && isinf(*b) && (*a > 0) == (*b > 0));
}

static bool differ(sk_buf *why, const char *field)
{
    sk_buf_append_c(why, field);
    return false;
}

#define SAME(f)                                                                                                        \
    do {                                                                                                               \
        if (!(a->f == b->f))                                                                                           \
            return differ(why, #f);                                                                                    \
    } while (0)
#define SAME_BY(eq, f)                                                                                                 \
    do {                                                                                                               \
        if (!eq(&a->f, &b->f))                                                                                         \
            return differ(why, #f);                                                                                    \
    } while (0)
/* A list of n items, n_ its count field. */
#define SAME_LIST(eq, f, nf, per)                                                                                      \
    do {                                                                                                               \
        if (a->nf != b->nf)                                                                                            \
            return differ(why, #nf);                                                                                   \
        for (size_t i_ = 0; i_ < (size_t)a->nf * (per); i_++)                                                          \
            if (!eq(&a->f[i_], &b->f[i_]))                                                                             \
                return differ(why, #f);                                                                                \
    } while (0)

/* Whether two computed styles hold the same values, field by field; the
 * first that differs named in why. counter-reset and counter-increment,
 * which a pack never holds, are not compared. */
static bool style_same(const sk_css_style *a, const sk_css_style *b, sk_buf *why)
{
    SAME(display);
    SAME(position);
    SAME(float_);
    SAME(box_sizing);
    SAME(visibility);
    SAME(pointer_events);
    SAME(cursor);
    SAME(overflow_x);
    SAME(overflow_y);
    SAME(z_auto);
    SAME(z_index);
    SAME(opacity);
    SAME(mix_blend);
    SAME(isolate);
    for (int i = 0; i < 4; i++) {
        SAME_BY(len_same, inset[i]);
        SAME_BY(len_same, margin[i]);
        SAME_BY(len_same, padding[i]);
        SAME(border_width[i]);
        SAME(border_style[i]);
        SAME_BY(color_same, border_color[i]);
        SAME_BY(len_same, radius[i][0]);
        SAME_BY(len_same, radius[i][1]);
        SAME(bi_slice[i]);
        SAME_BY(len_same, bi_width[i]);
        SAME(bi_outset[i]);
    }
    SAME_BY(len_same, width);
    SAME_BY(len_same, height);
    SAME_BY(len_same, min_width);
    SAME_BY(len_same, min_height);
    SAME_BY(len_same, max_width);
    SAME_BY(len_same, max_height);
    SAME_BY(image_same, bi_source);
    SAME(bi_slice_pct);
    SAME(bi_fill);
    SAME(bi_width_num);
    SAME(bi_repeat[0]);
    SAME(bi_repeat[1]);
    SAME(outline_width);
    SAME(outline_offset);
    SAME(outline_style);
    SAME_BY(color_same, outline_color);
    SAME(flex_direction);
    SAME(justify_content);
    SAME(align_items);
    SAME(align_self);
    SAME(flex_grow);
    SAME(flex_shrink);
    SAME_BY(len_same, flex_basis);
    SAME_BY(len_same, row_gap);
    SAME_BY(len_same, column_gap);
    SAME_LIST(track_same, grid_columns, ngrid_columns, 1);
    SAME_LIST(track_same, grid_rows, ngrid_rows, 1);
    SAME_BY(track_same, grid_auto_rows);
    SAME_BY(track_same, grid_auto_columns);
    SAME_BY(line_same, grid_column[0]);
    SAME_BY(line_same, grid_column[1]);
    SAME_BY(line_same, grid_row[0]);
    SAME_BY(line_same, grid_row[1]);
    SAME_LIST(xf_same, transform, ntransform, 1);
    SAME_BY(len_same, origin[0]);
    SAME_BY(len_same, origin[1]);
    SAME_LIST(filter_same, filter, nfilter, 1);
    SAME_LIST(len_same, clip, nclip, 2);
    SAME(clip_evenodd);
    SAME(image_rendering);
    SAME_BY(color_same, bg_color);
    SAME_LIST(image_same, bg_image, nbg_image, 1);
    SAME_LIST(position_same, bg_position, nbg_position, 1);
    SAME_LIST(bgsize_same, bg_size, nbg_size, 1);
    SAME_LIST(u8_same, bg_repeat, nbg_repeat, 2);
    SAME_LIST(u8_same, bg_blend, nbg_blend, 1);
    SAME_LIST(u8_same, bg_origin, nbg_origin, 1);
    SAME_LIST(u8_same, bg_clip, nbg_clip, 1);
    SAME_LIST(shadow_same, box_shadow, nbox_shadow, 1);
    SAME_BY(color_same, color);
    SAME_LIST(string_same, font_family, nfont_family, 1);
    SAME(font);
    SAME(font_synth);
    SAME(font_size);
    SAME(font_weight);
    SAME(font_style);
    SAME(line_height_kind);
    SAME(line_height);
    SAME(letter_spacing);
    SAME(text_transform);
    SAME(text_align);
    SAME(white_space);
    SAME(text_overflow);
    SAME(text_decoration);
    SAME(text_anchor);
    SAME_BY(color_same, text_decoration_color);
    SAME_LIST(shadow_same, text_shadow, ntext_shadow, 1);
    SAME(content_none);
    SAME_LIST(content_same, content, ncontent, 1);
    SAME(fill_kind);
    SAME(stroke_kind);
    SAME_BY(color_same, fill);
    SAME_BY(color_same, stroke);
    SAME_BY(len_same, stroke_width);
    SAME_LIST(len_same, dasharray, ndasharray, 1);
    SAME(linecap);
    SAME(linejoin);
    SAME_LIST(u32_same, anim_name, nanim_name, 1);
    SAME_LIST(f32_same, anim_duration, nanim_duration, 1);
    SAME_LIST(f32_same, anim_delay, nanim_delay, 1);
    SAME_LIST(f32_same, anim_iterations, nanim_iterations, 1);
    SAME_LIST(timing_same, anim_timing, nanim_timing, 1);
    SAME_LIST(u8_same, anim_direction, nanim_direction, 1);
    SAME_LIST(u8_same, anim_fill, nanim_fill, 1);
    SAME_LIST(u8_same, anim_paused, nanim_paused, 1);
    SAME_LIST(u16_same, tr_property, ntr_property, 1);
    SAME_LIST(f32_same, tr_duration, ntr_duration, 1);
    SAME_LIST(f32_same, tr_delay, ntr_delay, 1);
    SAME_LIST(timing_same, tr_timing, ntr_timing, 1);
    return true;
}

static void test_style_compare(void)
{
    /* The comparison sees a change in a list's item. */
    sk_css_style a, b;
    sk_style_initial(&a, (sk_css_color){0, 0, 0, 1});
    sk_style_initial(&b, (sk_css_color){0, 0, 0, 1});
    sk_buf why = {0};
    CHECK(style_same(&a, &b, &why));
    static const float two[1] = {2};
    b.anim_duration = two;
    CHECK(!style_same(&a, &b, &why));
    CHECK_STR(sk_buf_cstr(&why), "anim_duration");
    sk_buf_free(&why);
}

/* ---- Writing ---- */

/* The pack of the skin at dir, written as skinc writes it. */
static bool write_pack(const char *dir, sk_front *f, sk_buf *out)
{
    sk_diags d = {0};
    bool ok = sk_front_run(f, dir, &d);
    unsigned char hash[32];
    char err[256];
    if (ok && !sk_skin_hash(dir, hash, err, sizeof err)) {
        sk_test_fail(__FILE__, __LINE__, "%s", err);
        ok = false;
    }
    ok = ok && sk_pack_write(f, hash, out, &d);
    for (size_t i = 0; i < d.items.len; i++)
        sk_test_fail(__FILE__, __LINE__, "%s", d.items.ptr[i]);
    sk_diags_free(&d);
    return ok;
}

static bool load(const sk_buf *b, sk_pack *p)
{
    char err[300];
    bool ok = sk_pack_load((const uint8_t *)b->ptr, b->len, p, err, sizeof err);
    if (!ok)
        sk_test_fail(__FILE__, __LINE__, "the pack does not load: %s", err);
    return ok;
}

/* ---- The round trip ---- */

static bool rect_same(const sk_pack_rect *a, const sk_pack_rect *b)
{
    if (a->has != b->has)
        return false;
    for (int i = 0; i < 6; i++)
        if (a->v[i] != b->v[i] || a->follow[i] != b->follow[i])
            return false;
    return true;
}

/* The image a manifest path names: its index in the pack. */
static uint32_t image_named(const sk_pack *p, const char *path)
{
    for (uint32_t i = 0; path && i < p->nimages; i++)
        if (!strcmp(p->images[i].path, path))
            return i;
    return SK_PACK_NONE;
}

static const sk_pack_rect *front_rect(const sk_front *f, int rect)
{
    if (rect >= 1000000)
        return &f->skin.widgets[rect - 1000000].toggle_rect;
    if (rect < 0)
        return &f->skin.widgets[-rect - 1].rect;
    return &f->skin.panels[rect - 1].rect;
}

static const sk_el *meter_of(const sk_el *e)
{
    for (; e; e = e->parent)
        if (e->group)
            return e;
    return NULL;
}

/* The pack's element el against the front end's e. */
static void element_same(const sk_front *f, const sk_pack *p, uint32_t i)
{
    const sk_el *e = f->els.ptr[i];
    const sk_pack_el *x = &p->els[i];
    char at[32];
    snprintf(at, sizeof at, "element %u", (unsigned)i);
    if (x->kind != e->kind || x->role != e->role || x->text_source != e->text_source || !str_same(x->text, e->text))
        sk_test_fail(__FILE__, __LINE__, "%s: kind, role or text", at);
    if (x->parent != (e->parent ? e->parent->index : SK_PACK_NONE))
        sk_test_fail(__FILE__, __LINE__, "%s: parent", at);
    /* Its children: ::before, the elements, ::after. */
    uint32_t want[512], n = 0;
    if (e->before)
        want[n++] = e->before->index;
    for (const sk_el *c = e->first; c && n < 510; c = c->next)
        want[n++] = c->index;
    if (e->after)
        want[n++] = e->after->index;
    uint32_t k = 0;
    for (uint32_t c = x->first_child; c != SK_PACK_NONE && k <= n; c = p->els[c].next_sibling, k++)
        if (k == n || c != want[k])
            break;
    if (k != n)
        sk_test_fail(__FILE__, __LINE__, "%s: its children", at);
    if (x->widget != (e->widget >= 0 ? (uint32_t)e->widget : SK_PACK_NONE))
        sk_test_fail(__FILE__, __LINE__, "%s: widget", at);
    if ((x->rect == SK_PACK_NONE) != (e->rect == 0) ||
        (e->rect && !rect_same(&p->rects[x->rect], front_rect(f, e->rect))))
        sk_test_fail(__FILE__, __LINE__, "%s: rect", at);
    const sk_el *m = meter_of(e);
    if (x->group != (m ? m->group : 0) || x->meter != (m ? m->meter : 0))
        sk_test_fail(__FILE__, __LINE__, "%s: group", at);
    if ((x->svg == SK_PACK_NONE) != (e->svg.kind == SK_SVG_NONE))
        sk_test_fail(__FILE__, __LINE__, "%s: svg", at);
    if (x->svg != SK_PACK_NONE) {
        const sk_pack_svg *g = &p->svgs[x->svg];
        bool same = g->kind == e->svg.kind && g->flag == e->svg.flag;
        for (int v = 0; v < 5; v++)
            same = same && g->v[v] == e->svg.v[v];
        uint32_t image = e->svg.image;
        if (e->role == SK_ROLE_FACE)
            image = image_named(p, f->skin.widgets[e->widget].face);
        if (e->role == SK_ROLE_KNOB_IMAGE)
            image = image_named(p, f->skin.widgets[e->widget].image);
        if (!same || g->image != image)
            sk_test_fail(__FILE__, __LINE__, "%s: svg geometry", at);
    }
    if (x->ninputs != e->ninputs || x->nlayers != 0)
        sk_test_fail(__FILE__, __LINE__, "%s: inputs or layers", at);
    for (uint8_t k2 = 0; k2 < x->ninputs && k2 < e->ninputs; k2++)
        if (x->inputs[k2].owner != e->inputs[k2].owner->index || x->inputs[k2].var != e->inputs[k2].var ||
            x->inputs[k2].radix != e->inputs[k2].radix)
            sk_test_fail(__FILE__, __LINE__, "%s: input %u", at, (unsigned)k2);
    /* Every state's style, field by field. */
    for (size_t t = 0; t < e->ntable; t++) {
        sk_buf why = {0};
        if (!style_same(&p->styles[x->styles[t]], f->styles.ptr[e->table[t]], &why))
            sk_test_fail(__FILE__, __LINE__, "%s: state %zu: %s", at, t, sk_buf_cstr(&why));
        sk_buf_free(&why);
    }
}

static void widget_same(const sk_front *f, const sk_pack *p, uint32_t i)
{
    const sk_skin_widget *sw = &f->skin.widgets[i];
    const sk_pack_widget *a = &sw->w, *b = &p->widgets[i];
    bool same = a->type == b->type && a->format == b->format && a->digits == b->digits && str_same(a->id, b->id) &&
                str_same(a->bind, b->bind) && str_same(a->label, b->label) && str_same(a->prefix, b->prefix) &&
                str_same(a->suffix, b->suffix) && str_same(a->resets, b->resets) &&
                str_same(a->fallback_bind, b->fallback_bind) && str_same(a->fallback_label, b->fallback_label) &&
                str_same(a->fallback_resets, b->fallback_resets) && a->min == b->min && a->max == b->max &&
                a->damping == b->damping && a->start_angle == b->start_angle && a->end_angle == b->end_angle &&
                a->thickness == b->thickness && a->gap == b->gap && a->on == b->on && a->pulse_ms == b->pulse_ms &&
                a->blink == b->blink && a->fps == b->fps && a->ticks == b->ticks && a->segments == b->segments &&
                a->frames == b->frames && a->rows == b->rows && a->frame_w == b->frame_w &&
                a->frame_h == b->frame_h && a->has_on == b->has_on && a->pulse == b->pulse &&
                a->vertical == b->vertical && a->column == b->column && a->run == b->run &&
                a->needle_meters == b->needle_meters && a->across == b->across && a->nmarks == b->nmarks;
    for (uint16_t k = 0; same && k < a->nmarks; k++)
        same = str_same(a->marks[k].bind, b->marks[k].bind) && a->marks[k].value == b->marks[k].value &&
               str_same(a->marks[k].klass, b->marks[k].klass);
    if (!same)
        sk_test_fail(__FILE__, __LINE__, "widget %s: its spec", a->id);
    const sk_pack_el *root = &p->els[b->el];
    if (root->role != SK_ROLE_WIDGET || root->widget != i || !rect_same(&p->rects[root->rect], &sw->rect))
        sk_test_fail(__FILE__, __LINE__, "widget %s: its element", a->id);
    if ((b->toggle != SK_PACK_NONE) != sw->has_toggle ||
        (sw->has_toggle && !rect_same(&p->rects[p->els[b->toggle].rect], &sw->toggle_rect)))
        sk_test_fail(__FILE__, __LINE__, "widget %s: its toggle", a->id);
    if (b->face != image_named(p, sw->face) || b->image != image_named(p, sw->image) ||
        b->sheet != image_named(p, sw->sheet))
        sk_test_fail(__FILE__, __LINE__, "widget %s: its images", a->id);
}

/* What the pack holds of the files: images, fonts, sounds. */
static void media_same(const sk_front *f, const sk_pack *p)
{
    const sk_skin *s = &f->skin;
    CHECK(p->nimages >= f->images.len);
    for (size_t i = 0; i < p->nimages; i++) {
        const sk_pack_image *im = &p->images[i];
        if (i < f->images.len)
            CHECK_STR(im->path, f->images.ptr[i].path);
        if (!strncmp(im->path, "data:", 5))
            continue;
        size_t n;
        char *path = sk_path_join(s->dir, im->path);
        char *bytes = sk_test_read(path, &n);
        free(path);
        CHECK(bytes != NULL);
        if (!bytes)
            continue;
        if (sk_media_sniff((const uint8_t *)bytes, n) != SK_MEDIA_SVG) {
            /* A raster image: its own pixels, as skinc decodes them. */
            sk_image px;
            CHECK(sk_media_image((const uint8_t *)bytes, n, 0, &px, NULL));
            size_t qn = 0;
            uint8_t *qoi = sk_media_qoi(&px, &qn);
            CHECK_INT(im->nvariants, 1);
            if (im->nvariants == 1) {
                const sk_pack_variant *v = &im->variants[0];
                CHECK_INT(v->scale, 0);
                CHECK(v->w == px.w && v->h == px.h && im->w == px.w && im->h == px.h);
                CHECK(v->len == qn && !memcmp(p->blob + v->blob, qoi, qn));
            }
            free(qoi);
            sk_image_free(&px);
        } else {
            /* An SVG: each variant drawn at 1x and 2x of one size. */
            CHECK(im->nvariants % 2 == 0);
            for (uint16_t k = 0; k < im->nvariants; k++) {
                sk_image px;
                CHECK(im->variants[k].scale == 1 || im->variants[k].scale == 2);
                CHECK(sk_qoi_decode(p->blob + im->variants[k].blob, im->variants[k].len, SK_PACK_MAX_PIXELS, &px));
                CHECK(px.w == im->variants[k].w && px.h == im->variants[k].h);
                sk_image_free(&px);
            }
        }
        free(bytes);
    }
    CHECK_INT(p->nfonts, s->nfonts);
    for (uint32_t i = 0; i < p->nfonts && i < s->nfonts; i++) {
        CHECK_STR(p->fonts[i].family, s->fonts[i].family);
        CHECK_INT(p->fonts[i].weight, s->fonts[i].weight);
        CHECK_INT(p->fonts[i].style, s->fonts[i].style);
        size_t n;
        char *path = sk_path_join(s->dir, s->fonts[i].file);
        char *bytes = sk_test_read(path, &n);
        free(path);
        CHECK(bytes && n == p->fonts[i].len && !memcmp(bytes, p->fonts[i].data, n));
        free(bytes);
    }
    CHECK_INT(p->nsounds, s->nsounds);
    for (uint32_t i = 0; i < p->nsounds && i < s->nsounds; i++) {
        const sk_pack_sound *x = &p->sounds[i];
        CHECK_STR(x->path, s->sounds[i].file);
        CHECK_INT(x->mode, s->sounds[i].mode);
        CHECK(x->volume == s->sounds[i].volume);
        if (x->mode == SK_SOUND_RANDOM)
            CHECK(x->interval[0] == s->sounds[i].interval[0] && x->interval[1] == s->sounds[i].interval[1]);
        size_t n;
        char *path = sk_path_join(s->dir, s->sounds[i].file);
        char *bytes = sk_test_read(path, &n);
        free(path);
        sk_sound snd;
        CHECK(bytes && sk_media_sound((const uint8_t *)bytes, n, 0, &snd, NULL));
        free(bytes);
        size_t an = 0;
        uint8_t *adpcm = sk_media_adpcm(&snd, SK_ADPCM_BLOCK, &an);
        CHECK(x->rate == snd.rate && x->frames == snd.frames && x->block_bytes == SK_ADPCM_BLOCK);
        CHECK(x->len == an && adpcm && !memcmp(x->data, adpcm, an));
        free(adpcm);
        sk_sound_free(&snd);
    }
}

static void round_trip(const char *dir)
{
    sk_front f;
    sk_buf out = {0};
    if (!write_pack(dir, &f, &out)) {
        sk_front_free(&f);
        sk_buf_free(&out);
        return;
    }
    sk_pack p;
    if (!load(&out, &p)) {
        sk_front_free(&f);
        sk_buf_free(&out);
        return;
    }
    const sk_skin *s = &f.skin;
    unsigned char hash[32];
    CHECK(sk_skin_hash(dir, hash, NULL, 0));
    CHECK(!memcmp(p.source_hash, hash, 32));
    /* The manifest. */
    const sk_pack_meta *m = &p.meta;
    CHECK_STR(m->name, s->name);
    CHECK(str_same(m->version, s->version) && str_same(m->author, s->author) &&
          str_same(m->description, s->description) && str_same(m->license, s->license));
    CHECK(m->width == s->width && m->height == s->height && m->min_width == s->min_width &&
          m->min_height == s->min_height && m->terminal_font_size == s->terminal_font_size);
    CHECK(rect_same(&m->menu_rect, &s->menu_rect) && rect_same(&m->terminal_rect, &s->terminal_rect));
    CHECK(m->menu_image == image_named(&p, s->menu_image) && m->menu_hover == image_named(&p, s->menu_hover) &&
          m->menu_pressed == image_named(&p, s->menu_pressed));
    if (m->terminal_font != SK_PACK_NONE)
        CHECK(!strcasecmp(p.fonts[m->terminal_font].family, s->terminal_font));
    CHECK(!memcmp(m->theme, s->theme, sizeof m->theme) && m->has_theme == s->has_theme);
    CHECK_INT(p.nsplits, s->nsplits);
    for (uint32_t i = 0; i < p.nsplits && i < s->nsplits; i++)
        CHECK(str_same(p.splits[i].id, s->splits[i].id) && p.splits[i].edge == s->splits[i].edge &&
              p.splits[i].min == s->splits[i].min && p.splits[i].max == s->splits[i].max);
    CHECK_INT(p.nspans, f.spans.len);
    for (uint32_t i = 0; i < p.nspans && i < f.spans.len; i++)
        CHECK_STR(p.spans[i], f.spans.ptr[i]);
    /* The tree, its tables and styles. */
    CHECK_INT(p.nels, f.els.len);
    for (uint32_t i = 0; i < p.nels && i < f.els.len; i++)
        element_same(&f, &p, i);
    CHECK_INT(p.nwidgets, s->nwidgets);
    for (uint32_t i = 0; i < p.nwidgets && i < s->nwidgets; i++)
        widget_same(&f, &p, i);
    /* The keyframes. */
    CHECK_INT(p.nkeyframes, f.keyframes.len);
    for (uint32_t i = 0; i < p.nkeyframes && i < f.keyframes.len; i++) {
        const sk_pack_keyframes *a = &p.keyframes[i], *b = &f.keyframes.ptr[i];
        CHECK_STR(a->name, b->name);
        CHECK_INT(a->nkeys, b->nkeys);
        for (uint16_t k = 0; k < a->nkeys && k < b->nkeys; k++) {
            const sk_pack_key *x = &a->keys[k], *y = &b->keys[k];
            bool same = x->offset == y->offset && x->has_transform == y->has_transform &&
                        x->has_opacity == y->has_opacity && x->has_timing == y->has_timing &&
                        x->opacity == y->opacity && timing_same(&x->timing, &y->timing) &&
                        x->ntransform == y->ntransform;
            for (uint16_t t = 0; same && t < x->ntransform; t++)
                same = xf_same(&x->transform[t], &y->transform[t]);
            if (!same)
                sk_test_fail(__FILE__, __LINE__, "keyframes %s: key %u", a->name, (unsigned)k);
        }
    }
    media_same(&f, &p);
    CHECK_INT(p.nlayers, 0);
    sk_pack_free(&p);
    sk_front_free(&f);
    sk_buf_free(&out);
}

static void test_coffee(void)
{
    round_trip("skins/coffee");
}

static void test_deck(void)
{
    round_trip("skins/deck");
}

static void test_library(void)
{
    round_trip("skins/library");
}

static void test_phosphor(void)
{
    round_trip("skins/phosphor");
}

static void test_all_widgets(void)
{
    round_trip("test/fixtures/skins/all-widgets");
}

/* ---- The same bytes, and the shipped packs ---- */

static void test_same_bytes(void)
{
    sk_front f, g;
    sk_buf a = {0}, b = {0};
    CHECK(write_pack("skins/coffee", &f, &a));
    CHECK(write_pack("skins/coffee", &g, &b));
    CHECK(a.len == b.len && a.len && !memcmp(a.ptr, b.ptr, a.len));
    sk_front_free(&f);
    sk_front_free(&g);
    sk_buf_free(&a);
    sk_buf_free(&b);
}

static void test_shipped(void)
{
    static const char *const SKINS[] = {"coffee", "deck", "library", "phosphor"};
    CHECK_INT(sk_shipped_packs_count, SK_LEN(SKINS));
    for (size_t i = 0; i < SK_LEN(SKINS); i++) {
        const sk_embedded_file *e = sk_shipped_pack(SKINS[i]);
        CHECK(e != NULL);
        if (!e)
            continue;
        sk_pack p;
        char err[300];
        bool ok = sk_pack_load(e->data, e->size, &p, err, sizeof err);
        if (!ok)
            sk_test_fail(__FILE__, __LINE__, "%s: %s", e->name, err);
        else
            CHECK_STR(p.meta.name, SKINS[i]);
        sk_pack_free(&p);
        /* The build's pack is the one the writer writes now. */
        char dir[64];
        snprintf(dir, sizeof dir, "skins/%s", SKINS[i]);
        sk_front f;
        sk_buf now = {0};
        CHECK(write_pack(dir, &f, &now));
        CHECK(now.len == e->size && !memcmp(now.ptr, e->data, e->size));
        sk_front_free(&f);
        sk_buf_free(&now);
    }
    CHECK(sk_shipped_pack("nonesuch") == NULL);
}

/* ---- A small pack ---- */

enum { SMALL_FONT = 1, SMALL_SOUND = 2, SMALL_WIDGETS = 4 };

/* A skin of one panel, a picture and an SVG menu, written into a fresh
 * folder; with what says, the tiny font, a WAV and a gauge. */
static const char *small_skin(unsigned what)
{
    static unsigned n;
    static char dir[1024];
    snprintf(dir, sizeof dir, "%s/small%u", sk_test_tmpdir(), n++);
    bool font = what & SMALL_FONT;
    sk_buf m = {0};
    sk_buf_printf(&m,
                  "{\"name\": \"small\", \"version\": \"1.0.0\", \"description\": \"caf\xc3\xa9\",\n"
                  " \"window\": {\"width\": 1000, \"height\": 700, \"minWidth\": 800, \"minHeight\": 500},\n"
                  " \"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 16, \"height\": 16}, \"image\": \"m.svg\"},\n"
                  " \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, \"right\": 10, \"bottom\": 10},%s\n"
                  "   \"theme\": {\"foreground\": \"#e0e0e0\", \"background\": \"#000000\"}},\n"
                  " %s\"stylesheet\": \"skin.css\",\n"
                  " \"panels\": [{\"class\": \"a\", \"rect\": {\"left\": 0, \"top\": 0, \"width\": 90, \"height\": 90}}],\n"
                  " \"widgets\": [%s]%s}\n",
                  font ? " \"fontFamily\": \"Tiny\"," : "",
                  font ? "\"fonts\": [{\"family\": \"Tiny\", \"file\": \"t.ttf\"}],\n " : "",
                  what & SMALL_WIDGETS ? "{\"id\": \"g\", \"type\": \"gauge\", \"bind\": \"cost.usd\", \"label\": \"$\", "
                                         "\"rect\": {\"left\": 100, \"top\": 0, \"width\": 90, \"height\": 90}, "
                                         "\"marks\": [{\"value\": 5, \"class\": \"k\"}]}"
                                       : "",
                  what & SMALL_SOUND ? ",\n \"sounds\": [{\"file\": \"s.wav\", \"mode\": \"random\", \"volume\": 0.5, "
                                       "\"interval\": [1, 2]}]"
                                     : "");
    sk_test_write(dir, "skin.json", sk_buf_cstr(&m));
    sk_buf_free(&m);
    sk_test_write(dir, "m.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\"/>\n");
    sk_buf css = {0};
    sk_buf_printf(&css,
                  ".a { background: #123 url(p.png) no-repeat; color: red;%s\n"
                  "     animation: fade 2s infinite; }\n"
                  ".a:hover { opacity: .5; transition: opacity .2s; }\n"
                  ".a::before { content: \"x\"; }\n"
                  "@keyframes fade { from { opacity: 1 } to { opacity: 0 } }\n",
                  font ? " font-family: Tiny;" : "");
    sk_test_write(dir, "skin.css", sk_buf_cstr(&css));
    sk_buf_free(&css);
    if (font) {
        size_t fn = 0;
        uint8_t *tiny = sk_test_tiny_font(&fn);
        sk_test_write_bytes(dir, "t.ttf", tiny, fn);
        free(tiny);
    }
    uint8_t px[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 128, 255, 255, 255, 0};
    size_t pn = 0;
    uint8_t *png = sk_test_png(2, 2, 6, 8, false, px, 8, NULL, 0, NULL, 0, &pn);
    sk_test_write_bytes(dir, "p.png", png, pn);
    free(png);
    if (what & SMALL_SOUND) {
        int16_t pcm[64];
        for (int i = 0; i < 64; i++)
            pcm[i] = (int16_t)(i * 300 - 9000);
        size_t wn = 0;
        uint8_t *wav = sk_test_wav(1, 1, 8000, 16, pcm, sizeof pcm, &wn);
        sk_test_write_bytes(dir, "s.wav", wav, wn);
        free(wav);
    }
    return dir;
}

static bool small_pack(unsigned what, sk_buf *out)
{
    sk_front f;
    sk_buf_clear(out);
    bool ok = write_pack(small_skin(what), &f, out);
    sk_front_free(&f);
    return ok;
}

/* The small pack into b, or the test ends. */
#define SMALL(what, b)                                                                                                 \
    do {                                                                                                               \
        if (!small_pack(what, b)) {                                                                                    \
            sk_test_fail(__FILE__, __LINE__, "the small skin does not compile");                                       \
            sk_buf_free(b);                                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

/* The writer refuses dir, saying want, file and all. */
static void writer_refuses(const char *dir, const char *rel, const char *want)
{
    sk_front f;
    sk_diags d = {0};
    sk_buf out = {0};
    unsigned char hash[32] = {0};
    CHECK(sk_front_run(&f, dir, &d));
    CHECK(!sk_pack_write(&f, hash, &out, &d));
    sk_buf line = {0};
    sk_buf_printf(&line, "%s/%s: (file): %s", dir, rel, want);
    bool said = false;
    for (size_t i = 0; i < d.items.len; i++)
        said = said || !strncmp(d.items.ptr[i], line.ptr, line.len);
    if (!said)
        sk_test_fail(__FILE__, __LINE__, "no \"%s\" among %zu messages", line.ptr, d.items.len);
    sk_buf_free(&line);
    sk_buf_free(&out);
    sk_diags_free(&d);
    sk_front_free(&f);
}

static void test_writer_refusals(void)
{
    const char *dir = small_skin(SMALL_FONT | SMALL_SOUND);
    sk_test_write(dir, "t.ttf", "not a font at all");
    writer_refuses(dir, "t.ttf", "");
    dir = small_skin(SMALL_FONT);
    sk_test_write(dir, "p.png", "not a picture");
    writer_refuses(dir, "p.png", "is not a PNG or a JPEG");
    dir = small_skin(SMALL_FONT | SMALL_SOUND);
    sk_test_write(dir, "s.wav", "RIFF....WAVEnothing");
    writer_refuses(dir, "s.wav", "is a WAV file without its fmt and data chunks");
    dir = small_skin(SMALL_FONT);
    sk_test_write(dir, "m.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"0\" height=\"0\"/>");
    writer_refuses(dir, "m.svg", "is an SVG of 0 x 0");
}

/* ---- Packs taken apart and put together ---- */

typedef struct {
    uint32_t id, count;
    sk_buf b;
} esect;

typedef struct {
    uint8_t head[SK_PACK_HEADER_SIZE];
    esect s[SK_PACK_MAX_SECTIONS + 4];
    int n;
} epack;

static uint32_t id_of(const char *name)
{
    return SK_PACK_ID(name[0], name[1], name[2], name[3]);
}

static void ep_read(epack *e, const sk_buf *p)
{
    memset(e, 0, sizeof *e);
    memcpy(e->head, p->ptr, SK_PACK_HEADER_SIZE);
    const uint8_t *d = (const uint8_t *)p->ptr;
    uint32_t n = sk_le32(d + 24);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *t = d + SK_PACK_HEADER_SIZE + 16 * i;
        e->s[i].id = sk_le32(t);
        e->s[i].count = sk_le32(t + 12);
        sk_buf_append(&e->s[i].b, d + sk_le32(t + 4), sk_le32(t + 8));
    }
    e->n = (int)n;
}

static void ep_write(const epack *e, sk_buf *out)
{
    sk_buf_clear(out);
    uint32_t at = SK_PACK_HEADER_SIZE + 16 * (uint32_t)e->n, offs[SK_PACK_MAX_SECTIONS + 4];
    for (int i = 0; i < e->n; i++) {
        at = (at + 3) & ~3u;
        offs[i] = at;
        at += (uint32_t)e->s[i].b.len;
    }
    sk_buf_append(out, e->head, 16);
    sk_put64(out, at);
    sk_put32(out, (uint32_t)e->n);
    sk_buf_append(out, e->head + 28, SK_PACK_HEADER_SIZE - 28);
    for (int i = 0; i < e->n; i++) {
        sk_put32(out, e->s[i].id);
        sk_put32(out, offs[i]);
        sk_put32(out, (uint32_t)e->s[i].b.len);
        sk_put32(out, e->s[i].count);
    }
    for (int i = 0; i < e->n; i++) {
        while (out->len < offs[i])
            sk_put8(out, 0);
        if (e->s[i].b.len)
            sk_buf_append(out, e->s[i].b.ptr, e->s[i].b.len);
    }
}

static void ep_free(epack *e)
{
    for (int i = 0; i < e->n; i++)
        sk_buf_free(&e->s[i].b);
}

static esect *ep_get(epack *e, const char *name)
{
    for (int i = 0; i < e->n; i++)
        if (e->s[i].id == id_of(name))
            return &e->s[i];
    return NULL;
}

/* Record i of a fixed section, at its size in this pack. */
static uint8_t *ep_rec(epack *e, const char *name, uint32_t i)
{
    esect *s = ep_get(e, name);
    return (uint8_t *)s->b.ptr + (size_t)i * (s->b.len / s->count);
}

static void put32_at(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16_at(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void putf32_at(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    put32_at(p, u);
}

/* STYL's styles as records of their own, and back. */
typedef SK_VEC(sk_buf) records;

static void styl_split(esect *s, records *out)
{
    const uint8_t *p = (const uint8_t *)s->b.ptr;
    for (uint32_t i = 0; i < s->count; i++) {
        sk_buf r = {0};
        sk_buf_append(&r, p + sk_le32(p + 4 * i), sk_le32(p + 4 * i + 4) - sk_le32(p + 4 * i));
        sk_vec_push(out, r);
    }
}

static void styl_join(esect *s, records *r)
{
    sk_buf_clear(&s->b);
    uint32_t at = (uint32_t)(r->len + 1) * 4;
    for (size_t i = 0; i < r->len; i++) {
        sk_put32(&s->b, at);
        at += (uint32_t)r->ptr[i].len;
    }
    sk_put32(&s->b, at);
    for (size_t i = 0; i < r->len; i++) {
        sk_buf_append(&s->b, r->ptr[i].ptr, r->ptr[i].len);
        sk_buf_free(&r->ptr[i]);
    }
    s->count = (uint32_t)r->len;
    sk_vec_free(r);
}

/* Style 0 made of one property, id and its payload. */
static void style0_is(epack *e, uint16_t id, const void *payload, size_t n)
{
    records r = {0};
    esect *s = ep_get(e, "STYL");
    styl_split(s, &r);
    sk_buf_clear(&r.ptr[0]);
    sk_put16(&r.ptr[0], 1);
    sk_put16(&r.ptr[0], id);
    sk_put16(&r.ptr[0], (unsigned)n);
    sk_buf_append(&r.ptr[0], payload, n);
    styl_join(s, &r);
}

/* The pack's load fails, its message starting with want. */
#define REFUSED(buf, want) refused(__FILE__, __LINE__, (buf), (want))

static void refused(const char *file, int line, const sk_buf *b, const char *want)
{
    sk_pack p;
    char err[300];
    if (sk_pack_load((const uint8_t *)b->ptr, b->len, &p, err, sizeof err)) {
        sk_test_fail(file, line, "a pack that should fail loads: wanted \"%s\"", want);
        sk_pack_free(&p);
        return;
    }
    if (strncmp(err, want, strlen(want)) != 0)
        sk_test_fail(file, line, "refused with \"%s\", wanted \"%s\"", err, want);
    if (p.els || p.styles || p.mem_)
        sk_test_fail(file, line, "a refused pack is not empty");
}

/* ---- The loader's checks ---- */

static void test_small(void)
{
    sk_buf b = {0};
    SMALL(SMALL_FONT | SMALL_SOUND | SMALL_WIDGETS, &b);
    sk_pack p;
    if (load(&b, &p)) {
        CHECK_STR(p.meta.name, "small");
        CHECK_STR(p.meta.description, "caf\xc3\xa9");
        CHECK_INT(p.nfonts, 1);
        CHECK_INT(p.nsounds, 1);
        CHECK_INT(p.meta.terminal_font, 0);
        /* m.svg drawn at the menu's 16 x 16 at 1x and 2x, p.png at its own pixels. */
        CHECK_INT(p.nimages, 2);
        CHECK_STR(p.images[0].path, "p.png");
        CHECK_STR(p.images[1].path, "m.svg");
        CHECK(p.images[1].nvariants == 2 && p.images[1].variants[0].w == 16 && p.images[1].variants[1].w == 32);
        CHECK_INT(p.meta.menu_image, 1);
        CHECK_INT(p.nwidgets, 1);
        CHECK(p.nwidgets && p.widgets[0].nmarks == 1 && p.widgets[0].marks[0].value == 5);
        const sk_pack_el *panel = &p.els[4];
        CHECK_INT(panel->role, SK_ROLE_PANEL);
        CHECK_INT(panel->ninputs, 1);
        /* Its ::before comes first among its children. */
        CHECK(panel->first_child != SK_PACK_NONE && p.els[panel->first_child].kind == SK_EL_BEFORE);
        uint8_t hover = 1;
        CHECK(sk_pack_style_of(&p, panel, &hover)->opacity == .5f);
        sk_pack_free(&p);
    }
    sk_buf_free(&b);
}

static void test_header_checks(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT, &b);
    sk_pack p;
    char err[200];
    CHECK(!sk_pack_load((const uint8_t *)"", 0, &p, err, sizeof err));
    CHECK_STR(err, "0 bytes, fewer than a pack's header");
    /* No room for a message is no matter to a pack that loads. */
    CHECK(sk_pack_load((const uint8_t *)b.ptr, b.len, &p, NULL, 0));
    sk_pack_free(&p);
#define MUTATE(stmt, want)                                                                                             \
    do {                                                                                                               \
        sk_buf_clear(&x);                                                                                              \
        sk_buf_append(&x, b.ptr, b.len);                                                                               \
        uint8_t *d = (uint8_t *)x.ptr;                                                                                 \
        (void)d;                                                                                                       \
        stmt;                                                                                                          \
        REFUSED(&x, want);                                                                                             \
    } while (0)
    MUTATE(d[0] = 'X', "not a skin pack: it does not start SKINPACK");
    MUTATE(d[8] = 2, "format 2.0: this loader reads 1.x");
    char want[200];
    snprintf(want, sizeof want, "the header says %zu bytes, the pack has %zu", b.len + 1, b.len);
    MUTATE(put32_at(d + 16, (uint32_t)b.len + 1), want);
    MUTATE(d[12] = 60, "a header of 60 bytes");
    MUTATE(d[28] = 1, "flags 0x1: this loader knows none");
    MUTATE(d[24] = 65, "65 sections, more than 64");
    /* A pack that ends inside its section table. */
    sk_buf_clear(&x);
    sk_buf_append(&x, b.ptr, 100);
    put32_at((uint8_t *)x.ptr + 16, 100);
    REFUSED(&x, "its section table runs past its end");
    /* The table's entries: META is the second. */
    uint8_t *meta = (uint8_t *)b.ptr + SK_PACK_HEADER_SIZE + 16, *splt = meta + 16;
    size_t mo = (size_t)(meta - (uint8_t *)b.ptr), so = (size_t)(splt - (uint8_t *)b.ptr);
    snprintf(want, sizeof want, "section META at %u is not 4-byte aligned", (unsigned)sk_le32(meta + 4) + 2);
    MUTATE(put32_at(d + mo + 4, sk_le32(meta + 4) + 2), want);
    MUTATE(put32_at(d + mo + 8, 0x10000000), "section META, 268435456 bytes at");
    MUTATE(put32_at(d + mo + 4, SK_PACK_HEADER_SIZE), "section META, 216 bytes at 64, lies outside");
    MUTATE(put32_at(d + so + 4, sk_le32(meta + 4)), "sections META and SPLT overlap");
    MUTATE(memcpy(d + so, "META", 4), "section META twice");
    MUTATE(memcpy(d + so, "XXXX", 4), "SPLT: missing");
    sk_buf_free(&b);
    sk_buf_free(&x);
#undef MUTATE
}

static void test_section_checks(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT | SMALL_SOUND | SMALL_WIDGETS, &b);
    epack e;
    char want[200];
#define EDIT(stmt, want)                                                                                               \
    do {                                                                                                               \
        ep_read(&e, &b);                                                                                               \
        stmt;                                                                                                          \
        ep_write(&e, &x);                                                                                              \
        REFUSED(&x, want);                                                                                             \
        ep_free(&e);                                                                                                   \
    } while (0)
    ep_read(&e, &b);
    uint32_t nels = ep_get(&e, "ELEM")->count, nstrs = (uint32_t)ep_get(&e, "STRS")->b.len,
             nstab = ep_get(&e, "STAB")->count, nstyles = ep_get(&e, "STYL")->count;
    /* Where the description's é is, and the first input's variable. */
    uint32_t desc = sk_le32(ep_rec(&e, "META", 0) + 12);
    unsigned var0 = ep_rec(&e, "INPT", 0)[4], radix0 = ep_rec(&e, "INPT", 0)[5];
    ep_free(&e);
    /* Fixed records. */
    snprintf(want, sizeof want, "ELEM: %u records of at least 52 bytes do not fill its %u", nels + 1, nels * 52);
    EDIT(ep_get(&e, "ELEM")->count++, want);
    EDIT(ep_get(&e, "META")->count = 0, "META: no records in 216 bytes");
    /* Strings. */
    EDIT(ep_get(&e, "STRS")->b.ptr[nstrs - 1] = 'x', "STRS: its last string is not terminated");
    EDIT(ep_get(&e, "STRS")->b.ptr[desc + 3] = (char)0xff, "STRS: not valid UTF-8");
    EDIT(ep_get(&e, "STRS")->count++, "STRS: ");
    EDIT(put32_at(ep_rec(&e, "META", 0), SK_PACK_NONE), "META 0: the name is none");
    snprintf(want, sizeof want, "META 0: the name at %u, past STRS's %u bytes", nstrs, nstrs);
    EDIT(put32_at(ep_rec(&e, "META", 0), nstrs), want);
    snprintf(want, sizeof want, "META 0: the version at %u starts inside a character", desc + 4);
    EDIT(put32_at(ep_rec(&e, "META", 0) + 4, desc + 4), want);
    /* Indexes. */
    snprintf(want, sizeof want, "ELEM 1: parent 9999, past the %u elements", nels);
    EDIT(put32_at(ep_rec(&e, "ELEM", 1), 9999), want);
    EDIT(put32_at(ep_rec(&e, "ELEM", 4) + 16, 7), "ELEM 4: widget 7, past the 1 widgets");
    EDIT(ep_rec(&e, "RECT", 0)[25] = 5, "RECT 0: the rect follows split 5, past the 0 splits");
    EDIT(put32_at(ep_rec(&e, "META", 0) + 68, 9), "META 0: the menu's image 9, past the 2 images");
    EDIT(put32_at(ep_rec(&e, "WIDG", 0) + 108, nels), "WIDG 0: element");
    /* The tree: host 0, its links 1 and 2, the stage 3. */
    EDIT(put32_at(ep_rec(&e, "ELEM", 3) + 8, 1), "ELEM 1: reached twice");
    EDIT(put32_at(ep_rec(&e, "ELEM", 2), 3), "ELEM 2: its parent is 3, not 0, which reaches it");
    EDIT(put32_at(ep_rec(&e, "ELEM", 3) + 4, SK_PACK_NONE), "ELEM 4: not in the tree");
    EDIT(ep_rec(&e, "ELEM", 0)[12] = SK_EL_DIV, "ELEM 0: the root is not a host");
    EDIT(ep_rec(&e, "ELEM", 4)[15] = 1, "ELEM 4: meter 1 of a group of 0");
    /* Inputs and tables. */
    EDIT(ep_rec(&e, "ELEM", 4)[29] = 11, "ELEM 4: 11 inputs, more than 10");
    snprintf(want, sizeof want, "INPT 0: variable %u takes %u values, not 5", var0, radix0);
    EDIT(ep_rec(&e, "INPT", 0)[5] = 5, want);
    EDIT(ep_rec(&e, "INPT", 0)[4] = 99, "INPT 0: variable 99 is none");
    snprintf(want, sizeof want, "ELEM %u: its table %u to %u, past the %u STAB entries", nels - 1, nstab,
             nstab + 1, nstab);
    EDIT(put32_at(ep_rec(&e, "ELEM", nels - 1) + 40, nstab), want);
    snprintf(want, sizeof want, "STAB 0: style %u, past the %u styles", nstyles, nstyles);
    EDIT(put32_at(ep_rec(&e, "STAB", 0), nstyles), want);
    /* An element of ten inputs whose states number 9^10. */
    ep_read(&e, &b);
    esect *in = ep_get(&e, "INPT");
    uint32_t first = in->count;
    for (int k = 0; k < 10; k++) {
        uint8_t r[8] = {0, 0, 0, 0, SK_VAR_COUNT, SK_PACK_MAX_LIMITS + 1, 0, 0};
        sk_buf_append(&in->b, r, 8);
        in->count++;
    }
    put32_at(ep_rec(&e, "ELEM", 4) + 36, first);
    ep_rec(&e, "ELEM", 4)[29] = 10;
    ep_write(&e, &x);
    REFUSED(&x, "ELEM 4: more than 4096 states");
    /* Elements of 3888 states each, past 65536 in all. */
    for (int k = 0; k < 9; k++)
        ep_rec(&e, "INPT", first + (uint32_t)k)[4] = k < 4 ? SK_VAR_HOVER : SK_VAR_LIST;
    for (int k = 0; k < 9; k++)
        ep_rec(&e, "INPT", first + (uint32_t)k)[5] = k < 4 ? 2 : 3;
    ep_rec(&e, "ELEM", 4)[29] = 0;
    esect *st = ep_get(&e, "STAB");
    for (int k = 0; k < 3888; k++) {
        sk_put32(&st->b, 0);
        st->count++;
    }
    esect *el = ep_get(&e, "ELEM");
    uint8_t one[52];
    memset(one, 0xff, sizeof one);
    one[12] = SK_EL_DIV;
    one[13] = one[14] = one[15] = one[28] = 0;
    one[29] = 9;
    one[30] = one[31] = 0;
    put32_at(one + 36, first);
    put32_at(one + 40, 0);
    put32_at(one + 44, 0);
    put32_at(one + 48, 0);
    for (int k = 0; k < 17; k++) {
        sk_buf_append(&el->b, one, sizeof one);
        el->count++;
    }
    ep_write(&e, &x);
    REFUSED(&x, "ELEM ");
    {
        sk_pack p;
        char err[300];
        CHECK(!sk_pack_load((const uint8_t *)x.ptr, x.len, &p, err, sizeof err));
        CHECK(strstr(err, ": the tables reach more than 65536 states in all") != NULL);
    }
    ep_free(&e);
    /* Too many elements. */
    ep_read(&e, &b);
    el = ep_get(&e, "ELEM");
    sk_buf_clear(&el->b);
    uint8_t zero[52] = {0};
    for (uint32_t k = 0; k <= SK_PACK_MAX_ELEMENTS; k++)
        sk_buf_append(&el->b, zero, sizeof zero);
    el->count = SK_PACK_MAX_ELEMENTS + 1;
    ep_write(&e, &x);
    REFUSED(&x, "ELEM: 65536 elements: 1 to 65535");
    ep_free(&e);
    sk_buf_free(&b);
    sk_buf_free(&x);
#undef EDIT
}

static void test_style_checks(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT, &b);
    epack e;
    char want[200];
#define STYLE(id, bytes, want)                                                                                         \
    do {                                                                                                               \
        ep_read(&e, &b);                                                                                               \
        style0_is(&e, (id), (bytes), sizeof(bytes));                                                                   \
        ep_write(&e, &x);                                                                                              \
        REFUSED(&x, want);                                                                                             \
        ep_free(&e);                                                                                                   \
    } while (0)
    const uint8_t display9[] = {9};
    STYLE(SK_PROP_DISPLAY, display9, "STYL 0: property 1: a keyword 9 is none of the 6");
    const uint8_t display2[] = {1, 0};
    STYLE(SK_PROP_DISPLAY, display2, "STYL 0: property 1: 1 of its 2 bytes unread");
    uint8_t opacity[4];
    putf32_at(opacity, NAN);
    snprintf(want, sizeof want, "STYL 0: property %u: a number nan is out of its range", SK_PROP_OPACITY);
    STYLE(SK_PROP_OPACITY, opacity, want);
    putf32_at(opacity, 2);
    snprintf(want, sizeof want, "STYL 0: property %u: a number 2 is out of its range", SK_PROP_OPACITY);
    STYLE(SK_PROP_OPACITY, opacity, want);
    const uint8_t width[] = {SK_LEN_PX, 0, 0, 0x80, 0x7f, 0, 0, 0, 0};
    snprintf(want, sizeof want, "STYL 0: property %u: a length's px inf is out of its range", SK_PROP_WIDTH);
    STYLE(SK_PROP_WIDTH, width, want);
    const uint8_t transforms[] = {0x2c, 0x01};
    snprintf(want, sizeof want, "STYL 0: property %u: a list of 300 items, more than 256", SK_PROP_TRANSFORM);
    STYLE(SK_PROP_TRANSFORM, transforms, want);
    const uint8_t no_layers[] = {0, 0};
    snprintf(want, sizeof want, "STYL 0: property %u: a list of 0 items, fewer than 1", SK_PROP_BACKGROUND_IMAGE);
    STYLE(SK_PROP_BACKGROUND_IMAGE, no_layers, want);
    const uint8_t image9[] = {1, 0, SK_IMAGE_URL, 9, 0, 0, 0};
    snprintf(want, sizeof want, "STYL 0: property %u: image 9 is past the 2 images", SK_PROP_BACKGROUND_IMAGE);
    STYLE(SK_PROP_BACKGROUND_IMAGE, image9, want);
    const uint8_t anim[] = {1, 0, 7, 0, 0, 0};
    snprintf(want, sizeof want, "STYL 0: property %u: keyframes 7 past the 1 tracks", SK_PROP_ANIMATION_NAME);
    STYLE(SK_PROP_ANIMATION_NAME, anim, want);
    const uint8_t family[] = {1, 0, 0, 0, 0, 0, 9, 0, 0};
    snprintf(want, sizeof want, "STYL 0: property %u: face 9 past the 7 faces", SK_PROP_FONT_FAMILY);
    STYLE(SK_PROP_FONT_FAMILY, family, want);
    const uint8_t empty[1] = {0};
    snprintf(want, sizeof want, "STYL 0: property %u is one a pack never holds", SK_PROP_COUNTER_RESET);
    ep_read(&e, &b);
    style0_is(&e, SK_PROP_COUNTER_RESET, empty, 0);
    ep_write(&e, &x);
    REFUSED(&x, want);
    ep_free(&e);
    /* Properties out of order, and one past the style. */
    ep_read(&e, &b);
    records r = {0};
    esect *s = ep_get(&e, "STYL");
    styl_split(s, &r);
    sk_buf_clear(&r.ptr[0]);
    const uint8_t two[] = {2, 0, 2, 0, 1, 0, 0, 1, 0, 1, 0, 1};
    sk_buf_append(&r.ptr[0], two, sizeof two);
    styl_join(s, &r);
    ep_write(&e, &x);
    REFUSED(&x, "STYL 0: property 1 after 2: a style's properties rise");
    ep_free(&e);
    ep_read(&e, &b);
    s = ep_get(&e, "STYL");
    styl_split(s, &r);
    sk_buf_clear(&r.ptr[0]);
    const uint8_t past[] = {1, 0, 1, 0, 9, 0, 1};
    sk_buf_append(&r.ptr[0], past, sizeof past);
    styl_join(s, &r);
    ep_write(&e, &x);
    REFUSED(&x, "STYL 0: property 1: 9 bytes run past the style");
    ep_free(&e);
    /* The offsets. */
    ep_read(&e, &b);
    s = ep_get(&e, "STYL");
    put32_at((uint8_t *)s->b.ptr + 4, sk_le32((const uint8_t *)s->b.ptr));
    ep_write(&e, &x);
    snprintf(want, sizeof want, "STYL 0: from %u to %u: the offsets rise", (unsigned)sk_le32((uint8_t *)s->b.ptr),
             (unsigned)sk_le32((uint8_t *)s->b.ptr));
    REFUSED(&x, want);
    put32_at((uint8_t *)s->b.ptr, 0);
    ep_write(&e, &x);
    REFUSED(&x, "STYL: its first record at 0, not after its offsets");
    ep_free(&e);
    /* A track's keys: "fade" from 1 to 0, its second key at 0. */
    ep_read(&e, &b);
    esect *k = ep_get(&e, "KEYF");
    uint8_t *track = (uint8_t *)k->b.ptr + sk_le32((uint8_t *)k->b.ptr);
    putf32_at(track + 6 + 34, 0);
    ep_write(&e, &x);
    REFUSED(&x, "KEYF 0: key 1 at 0 after 0: a track's keys rise");
    ep_free(&e);
    sk_buf_free(&b);
    sk_buf_free(&x);
#undef STYLE
}

static void test_media_checks(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT | SMALL_SOUND, &b);
    epack e;
#define EDIT(stmt, want)                                                                                               \
    do {                                                                                                               \
        ep_read(&e, &b);                                                                                               \
        stmt;                                                                                                          \
        ep_write(&e, &x);                                                                                              \
        REFUSED(&x, want);                                                                                             \
        ep_free(&e);                                                                                                   \
    } while (0)
    /* VARI 0 is p.png's own 2 x 2 pixels. */
    EDIT(ep_rec(&e, "VARI", 0)[0] = 3, "VARI 0: its QOI image is not one of 3 x 2 pixels");
    EDIT(put16_at(ep_rec(&e, "VARI", 0), 20000), "VARI 0: 20000 x 2 pixels: at most 16384 a side and 67108864 in all");
    EDIT(ep_rec(&e, "VARI", 0)[4] = 3, "VARI 0: scale 3: 0, 1 or 2");
    EDIT(put32_at(ep_rec(&e, "VARI", 0) + 12, sk_le32(ep_rec(&e, "VARI", 0) + 12) - 1),
         "VARI 0: its QOI image is not one of 2 x 2 pixels");
    EDIT(put32_at(ep_rec(&e, "VARI", 0) + 8, 0x7fffffff), "VARI 0: blob 2147483647");
    EDIT(put16_at(ep_rec(&e, "IMGS", 0) + 4, 5), "IMGS 0: variant 0 is its own pixels at 2 x 2, not 5 x 2");
    /* The font: its sfnt version made CFF's. */
    EDIT(memcpy(ep_get(&e, "BLOB")->b.ptr + sk_le32(ep_rec(&e, "FONT", 0) + 8), "OTTO", 4), "FONT 0: the font ");
    EDIT(put16_at(ep_rec(&e, "FONT", 0) + 4, 0), "FONT 0: weight 0, style 0");
    /* The sound. */
    EDIT(put32_at(ep_rec(&e, "SNDS", 0) + 20, 7000), "SNDS 0: 7000 Hz: 8 to 48 kHz");
    EDIT(putf32_at(ep_rec(&e, "SNDS", 0) + 8, 2), "SNDS 0: volume 2: 0 to 1");
    EDIT(ep_rec(&e, "SNDS", 0)[4] = SK_SOUND_LOOP, "SNDS 0: an interval of 1 to 2 s for a looping sound");
    EDIT(putf32_at(ep_rec(&e, "SNDS", 0) + 16, 4000), "SNDS 0: an interval of 1 to 4000 s for a random sound");
    EDIT(put32_at(ep_rec(&e, "SNDS", 0) + 24, 100000), "SNDS 0: its 1024 bytes are not the blocks of 100000 frames");
    EDIT(ep_get(&e, "BLOB")->b.ptr[sk_le32(ep_rec(&e, "SNDS", 0) + 28) + 2] = 99,
         "SNDS 0: its 1024 bytes are not the blocks of 64 frames");
#undef EDIT
    sk_buf_free(&b);
    sk_buf_free(&x);
}

/* A layer drawn from p.png's pixels, kind and scale as given. */
static void add_layer(epack *e, uint8_t kind, uint8_t scale, uint16_t slice)
{
    esect *l = ep_get(e, "LAYR");
    uint8_t r[48] = {0};
    put32_at(r + 4, SK_PACK_NONE);
    r[8] = kind;
    r[9] = scale;
    put16_at(r + 28, 2);
    put16_at(r + 30, 2);
    put16_at(r + 32, slice);
    put16_at(r + 36, slice);
    memcpy(r + 40, ep_rec(e, "VARI", 0) + 8, 8);
    sk_buf_append(&l->b, r, sizeof r);
    l->count++;
    put32_at(ep_rec(e, "ELEM", 4) + 48, 1);
}

static void test_layer_checks(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT, &b);
    epack e;
    ep_read(&e, &b);
    add_layer(&e, SK_LAYER_STATIC, 2, 1);
    ep_write(&e, &x);
    sk_pack p;
    if (load(&x, &p)) {
        CHECK(p.nlayers == 1 && p.els[4].nlayers == 1 && p.layers[0].scale == 2 && p.layers[0].w == 2);
        sk_pack_free(&p);
    }
    ep_free(&e);
#define EDIT(stmt, want)                                                                                               \
    do {                                                                                                               \
        ep_read(&e, &b);                                                                                               \
        stmt;                                                                                                          \
        ep_write(&e, &x);                                                                                              \
        REFUSED(&x, want);                                                                                             \
        ep_free(&e);                                                                                                   \
    } while (0)
    EDIT(add_layer(&e, 9, 1, 0), "LAYR 0: kind 9 is none");
    EDIT(add_layer(&e, SK_LAYER_NINE, 3, 0), "LAYR 0: scale 3: 1 or 2");
    EDIT(add_layer(&e, SK_LAYER_NINE, 1, 2), "LAYR 0: slices wider than its 2 x 2");
    EDIT(put32_at(ep_rec(&e, "ELEM", 4) + 48, 1), "ELEM 4: layers 0 to 1, past the 0 layers");
#undef EDIT
    sk_buf_free(&b);
    sk_buf_free(&x);
}

/* A newer minor: larger records, a section and a property this loader
 * does not know. It loads as the pack it grew from. */
static void newer_minor(const sk_buf *b, sk_buf *out)
{
    epack e;
    ep_read(&e, b);
    e.head[10] = 1;
    esect *el = ep_get(&e, "ELEM");
    sk_buf grown = {0};
    for (uint32_t i = 0; i < el->count; i++) {
        sk_buf_append(&grown, el->b.ptr + 52 * i, 52);
        sk_put32(&grown, 0xdeadbeef);
    }
    sk_buf_free(&el->b);
    el->b = grown;
    esect *x = &e.s[e.n++];
    x->id = id_of("XTRA");
    x->count = 3;
    sk_buf_append_c(&x->b, "newer");
    records r = {0};
    esect *s = ep_get(&e, "STYL");
    styl_split(s, &r);
    uint8_t *rec = (uint8_t *)r.ptr[0].ptr;
    put16_at(rec, sk_le16(rec) + 1u);
    const uint8_t unknown[] = {0xf4, 0x01, 3, 0, 1, 2, 3};
    sk_buf_append(&r.ptr[0], unknown, sizeof unknown);
    styl_join(s, &r);
    ep_write(&e, out);
    ep_free(&e);
}

static void test_newer_minor(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT | SMALL_SOUND | SMALL_WIDGETS, &b);
    newer_minor(&b, &x);
    sk_pack p, q;
    if (load(&b, &p) && load(&x, &q)) {
        CHECK_INT(q.nels, p.nels);
        for (uint32_t i = 0; i < p.nels && i < q.nels; i++)
            CHECK(q.els[i].parent == p.els[i].parent && q.els[i].first_child == p.els[i].first_child &&
                  q.els[i].ninputs == p.els[i].ninputs && str_same(q.els[i].text, p.els[i].text));
        sk_buf why = {0};
        CHECK(style_same(&q.styles[0], &p.styles[0], &why));
        sk_buf_free(&why);
        sk_pack_free(&q);
    }
    sk_pack_free(&p);
    sk_buf_free(&b);
    sk_buf_free(&x);
}

/* The fuzz target's seeds: inputs of at most 4096 bytes, as its driver
 * takes them. */
static void seed(const char *name, const sk_buf *b)
{
    if (b->len > 4096)
        sk_test_fail(__FILE__, __LINE__, "the seed %s is %zu bytes, more than the fuzzer's 4096", name, b->len);
    sk_pack p;
    if (load(b, &p))
        sk_pack_free(&p);
    const char *update = getenv("SK_SEED_UPDATE");
    if (update && *update && strcmp(update, "0") != 0) {
        char *path = sk_path_join("tests/fuzz/corpus/pack", name);
        sk_test_write_bytes(".", path, b->ptr, b->len);
        free(path);
    }
}

static void test_seeds(void)
{
    sk_buf b = {0}, x = {0};
    SMALL(SMALL_FONT, &b);
    seed("small.pack", &b);
    newer_minor(&b, &x);
    seed("minor.pack", &x);
    epack e;
    ep_read(&e, &b);
    add_layer(&e, SK_LAYER_NINE, 1, 1);
    ep_write(&e, &x);
    ep_free(&e);
    seed("layer.pack", &x);
    SMALL(SMALL_SOUND, &b);
    seed("sound.pack", &b);
    /* The committed seeds load. */
    static const char *const SEEDS[] = {"small.pack", "minor.pack", "layer.pack", "sound.pack"};
    for (size_t i = 0; i < SK_LEN(SEEDS); i++) {
        char *path = sk_path_join("tests/fuzz/corpus/pack", SEEDS[i]);
        size_t n = 0;
        char *bytes = sk_test_read(path, &n);
        free(path);
        CHECK(bytes != NULL);
        sk_pack p;
        char err[300];
        if (bytes && !sk_pack_load((const uint8_t *)bytes, n, &p, err, sizeof err))
            sk_test_fail(__FILE__, __LINE__, "seed %s: %s", SEEDS[i], err);
        else if (bytes)
            sk_pack_free(&p);
        free(bytes);
    }
    sk_buf_free(&b);
    sk_buf_free(&x);
}

const sk_test sk_suite_skin_pack[] = {
    T(test_style_compare), T(test_coffee),         T(test_deck),          T(test_library),
    T(test_phosphor),      T(test_all_widgets),    T(test_same_bytes),    T(test_shipped),
    T(test_small),         T(test_header_checks),  T(test_section_checks), T(test_style_checks),
    T(test_media_checks),  T(test_layer_checks),   T(test_newer_minor),   T(test_seeds),
    T(test_writer_refusals),
    {0},
};
