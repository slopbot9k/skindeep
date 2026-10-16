/* The payloads of decision 0037's STYL and KEYF sections, both ways:
 * sk_pack_put_prop and sk_pack_put_key for skinc's writer, and the
 * readers the loader runs, which check every byte. One table says what
 * each property's payload is and which fields of sk_css_style it
 * fills, so the writer and the loader read it alike.
 *
 * A reader never reads back what it has written: in the loader's first
 * pass every list it decodes lands in the same scratch bytes. */
#include "packfmt.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* ---- The table ---- */

/* A payload's form. */
enum {
    PF_NONE,    /* never in a pack */
    PF_KW,      /* u8 below arg */
    PF_BOOL,    /* u8, 0 or 1 */
    PF_F32,     /* f32 in range arg */
    PF_WEIGHT,  /* u16, 1 to 1000 */
    PF_LEN,     /* a length */
    PF_LEN2,    /* two lengths: a corner's radius, transform-origin */
    PF_COLOR,   /* a colour */
    PF_IMAGE,   /* an image */
    PF_SLICE,   /* border-image-slice: four f32, u8 percent bits, u8 fill */
    PF_BIWIDTH, /* border-image-width: four lengths, u8 number bits */
    PF_F32X4,   /* border-image-outset: four f32 */
    PF_REPEAT2, /* border-image-repeat: two u8 */
    PF_ZINDEX,  /* u8 auto, i32 */
    PF_TRACK,   /* a grid track */
    PF_LINE,    /* a grid line */
    PF_LH,      /* line-height: u8 kind, f32 */
    PF_PAINT,   /* fill, stroke: u8 kind (off), a colour (noff) */
    PF_FAMILY,  /* font-family: a list of strings, u16 face, u8 synthesis */
    PF_CONTENT, /* content: u8 none, a list of parts */
    PF_CLIP,    /* clip-path: u8 evenodd, a list of n points, 2n lengths */
    PF_LIST,    /* u16 count, then items of form item */
};

/* A list item's form. */
enum {
    IT_LEN,
    IT_TRACK,
    IT_XF,
    IT_FILTER,
    IT_IMAGE,
    IT_POSITION,
    IT_BGSIZE,
    IT_REPEAT2, /* two u8 below 4 */
    IT_KW,      /* u8 below arg */
    IT_SHADOW,
    IT_KEYF, /* u32, a keyframe track or none */
    IT_F32,  /* f32 in range arg */
    IT_TIMING,
    IT_PROP, /* u16, a transition's property */
    IT_COUNT,
};

/* A number's range: SK_PACK_MAX_FLOAT either way, from 0, 0 to 1, or
 * from 0 with infinity too. */
enum { R_ANY, R_NONNEG, R_UNIT, R_ITER };

typedef struct {
    uint8_t form, item, arg;
    uint8_t min;        /* a list's fewest items */
    uint16_t off, noff; /* the field; a list's count, or a paint's colour */
} propfmt;

#define O(m) ((uint16_t)offsetof(sk_css_style, m))
#define KW(m, n) {PF_KW, 0, n, 0, O(m), 0}
#define BOOL(m) {PF_BOOL, 0, 0, 0, O(m), 0}
#define NUM(m, r) {PF_F32, 0, r, 0, O(m), 0}
#define ONE(form, m) {form, 0, 0, 0, O(m), 0}
#define LIST(m, nm, it, arg, min) {PF_LIST, it, arg, min, O(m), O(nm)}

static const propfmt PROPS[SK_PROP_COUNT] = {
    [SK_PROP_DISPLAY] = KW(display, 6),
    [SK_PROP_POSITION] = KW(position, 4),
    [SK_PROP_FLOAT] = KW(float_, 3),
    [SK_PROP_BOX_SIZING] = KW(box_sizing, 2),
    [SK_PROP_TOP] = ONE(PF_LEN, inset[SK_TOP]),
    [SK_PROP_RIGHT] = ONE(PF_LEN, inset[SK_RIGHT]),
    [SK_PROP_BOTTOM] = ONE(PF_LEN, inset[SK_BOTTOM]),
    [SK_PROP_LEFT] = ONE(PF_LEN, inset[SK_LEFT]),
    [SK_PROP_WIDTH] = ONE(PF_LEN, width),
    [SK_PROP_HEIGHT] = ONE(PF_LEN, height),
    [SK_PROP_MIN_WIDTH] = ONE(PF_LEN, min_width),
    [SK_PROP_MIN_HEIGHT] = ONE(PF_LEN, min_height),
    [SK_PROP_MAX_WIDTH] = ONE(PF_LEN, max_width),
    [SK_PROP_MAX_HEIGHT] = ONE(PF_LEN, max_height),
    [SK_PROP_MARGIN_TOP] = ONE(PF_LEN, margin[SK_TOP]),
    [SK_PROP_MARGIN_RIGHT] = ONE(PF_LEN, margin[SK_RIGHT]),
    [SK_PROP_MARGIN_BOTTOM] = ONE(PF_LEN, margin[SK_BOTTOM]),
    [SK_PROP_MARGIN_LEFT] = ONE(PF_LEN, margin[SK_LEFT]),
    [SK_PROP_PADDING_TOP] = ONE(PF_LEN, padding[SK_TOP]),
    [SK_PROP_PADDING_RIGHT] = ONE(PF_LEN, padding[SK_RIGHT]),
    [SK_PROP_PADDING_BOTTOM] = ONE(PF_LEN, padding[SK_BOTTOM]),
    [SK_PROP_PADDING_LEFT] = ONE(PF_LEN, padding[SK_LEFT]),
    [SK_PROP_BORDER_TOP_WIDTH] = NUM(border_width[SK_TOP], R_NONNEG),
    [SK_PROP_BORDER_RIGHT_WIDTH] = NUM(border_width[SK_RIGHT], R_NONNEG),
    [SK_PROP_BORDER_BOTTOM_WIDTH] = NUM(border_width[SK_BOTTOM], R_NONNEG),
    [SK_PROP_BORDER_LEFT_WIDTH] = NUM(border_width[SK_LEFT], R_NONNEG),
    [SK_PROP_BORDER_TOP_STYLE] = KW(border_style[SK_TOP], 3),
    [SK_PROP_BORDER_RIGHT_STYLE] = KW(border_style[SK_RIGHT], 3),
    [SK_PROP_BORDER_BOTTOM_STYLE] = KW(border_style[SK_BOTTOM], 3),
    [SK_PROP_BORDER_LEFT_STYLE] = KW(border_style[SK_LEFT], 3),
    [SK_PROP_BORDER_TOP_COLOR] = ONE(PF_COLOR, border_color[SK_TOP]),
    [SK_PROP_BORDER_RIGHT_COLOR] = ONE(PF_COLOR, border_color[SK_RIGHT]),
    [SK_PROP_BORDER_BOTTOM_COLOR] = ONE(PF_COLOR, border_color[SK_BOTTOM]),
    [SK_PROP_BORDER_LEFT_COLOR] = ONE(PF_COLOR, border_color[SK_LEFT]),
    [SK_PROP_BORDER_TOP_LEFT_RADIUS] = ONE(PF_LEN2, radius[SK_TOP_LEFT]),
    [SK_PROP_BORDER_TOP_RIGHT_RADIUS] = ONE(PF_LEN2, radius[SK_TOP_RIGHT]),
    [SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS] = ONE(PF_LEN2, radius[SK_BOTTOM_RIGHT]),
    [SK_PROP_BORDER_BOTTOM_LEFT_RADIUS] = ONE(PF_LEN2, radius[SK_BOTTOM_LEFT]),
    [SK_PROP_BORDER_IMAGE_SOURCE] = ONE(PF_IMAGE, bi_source),
    [SK_PROP_BORDER_IMAGE_SLICE] = ONE(PF_SLICE, bi_slice),
    [SK_PROP_BORDER_IMAGE_WIDTH] = ONE(PF_BIWIDTH, bi_width),
    [SK_PROP_BORDER_IMAGE_OUTSET] = {PF_F32X4, 0, R_NONNEG, 0, O(bi_outset), 0},
    [SK_PROP_BORDER_IMAGE_REPEAT] = {PF_REPEAT2, 0, 4, 0, O(bi_repeat), 0},
    [SK_PROP_OUTLINE_WIDTH] = NUM(outline_width, R_NONNEG),
    [SK_PROP_OUTLINE_STYLE] = KW(outline_style, 3),
    [SK_PROP_OUTLINE_COLOR] = ONE(PF_COLOR, outline_color),
    [SK_PROP_OUTLINE_OFFSET] = NUM(outline_offset, R_ANY),
    [SK_PROP_OVERFLOW_X] = KW(overflow_x, 3),
    [SK_PROP_OVERFLOW_Y] = KW(overflow_y, 3),
    [SK_PROP_Z_INDEX] = ONE(PF_ZINDEX, z_auto),
    [SK_PROP_VISIBILITY] = KW(visibility, 3),
    [SK_PROP_OPACITY] = NUM(opacity, R_UNIT),
    [SK_PROP_POINTER_EVENTS] = KW(pointer_events, 2),
    [SK_PROP_CURSOR] = KW(cursor, 4),
    [SK_PROP_MIX_BLEND_MODE] = KW(mix_blend, 7),
    [SK_PROP_ISOLATION] = BOOL(isolate),
    [SK_PROP_FLEX_DIRECTION] = KW(flex_direction, 4),
    [SK_PROP_FLEX_GROW] = NUM(flex_grow, R_NONNEG),
    [SK_PROP_FLEX_SHRINK] = NUM(flex_shrink, R_NONNEG),
    [SK_PROP_FLEX_BASIS] = ONE(PF_LEN, flex_basis),
    [SK_PROP_JUSTIFY_CONTENT] = KW(justify_content, 12),
    [SK_PROP_ALIGN_ITEMS] = KW(align_items, 12),
    [SK_PROP_ALIGN_SELF] = KW(align_self, 12),
    [SK_PROP_ROW_GAP] = ONE(PF_LEN, row_gap),
    [SK_PROP_COLUMN_GAP] = ONE(PF_LEN, column_gap),
    [SK_PROP_GRID_TEMPLATE_COLUMNS] = LIST(grid_columns, ngrid_columns, IT_TRACK, 0, 0),
    [SK_PROP_GRID_TEMPLATE_ROWS] = LIST(grid_rows, ngrid_rows, IT_TRACK, 0, 0),
    [SK_PROP_GRID_AUTO_ROWS] = ONE(PF_TRACK, grid_auto_rows),
    [SK_PROP_GRID_AUTO_COLUMNS] = ONE(PF_TRACK, grid_auto_columns),
    [SK_PROP_GRID_COLUMN_START] = ONE(PF_LINE, grid_column[0]),
    [SK_PROP_GRID_COLUMN_END] = ONE(PF_LINE, grid_column[1]),
    [SK_PROP_GRID_ROW_START] = ONE(PF_LINE, grid_row[0]),
    [SK_PROP_GRID_ROW_END] = ONE(PF_LINE, grid_row[1]),
    [SK_PROP_TRANSFORM] = LIST(transform, ntransform, IT_XF, 0, 0),
    [SK_PROP_TRANSFORM_ORIGIN] = ONE(PF_LEN2, origin),
    [SK_PROP_FILTER] = LIST(filter, nfilter, IT_FILTER, 0, 0),
    [SK_PROP_CLIP_PATH] = ONE(PF_CLIP, clip),
    [SK_PROP_IMAGE_RENDERING] = KW(image_rendering, 3),
    [SK_PROP_BACKGROUND_COLOR] = ONE(PF_COLOR, bg_color),
    [SK_PROP_BACKGROUND_IMAGE] = LIST(bg_image, nbg_image, IT_IMAGE, 0, 1),
    [SK_PROP_BACKGROUND_POSITION] = LIST(bg_position, nbg_position, IT_POSITION, 0, 1),
    [SK_PROP_BACKGROUND_SIZE] = LIST(bg_size, nbg_size, IT_BGSIZE, 0, 1),
    [SK_PROP_BACKGROUND_REPEAT] = LIST(bg_repeat, nbg_repeat, IT_REPEAT2, 4, 1),
    [SK_PROP_BACKGROUND_BLEND_MODE] = LIST(bg_blend, nbg_blend, IT_KW, 7, 1),
    [SK_PROP_BACKGROUND_ORIGIN] = LIST(bg_origin, nbg_origin, IT_KW, 3, 1),
    [SK_PROP_BACKGROUND_CLIP] = LIST(bg_clip, nbg_clip, IT_KW, 3, 1),
    [SK_PROP_BOX_SHADOW] = LIST(box_shadow, nbox_shadow, IT_SHADOW, 0, 0),
    [SK_PROP_COLOR] = ONE(PF_COLOR, color),
    [SK_PROP_FONT_FAMILY] = ONE(PF_FAMILY, font_family),
    [SK_PROP_FONT_SIZE] = NUM(font_size, R_NONNEG),
    [SK_PROP_FONT_WEIGHT] = ONE(PF_WEIGHT, font_weight),
    [SK_PROP_FONT_STYLE] = KW(font_style, 3),
    [SK_PROP_LINE_HEIGHT] = ONE(PF_LH, line_height),
    [SK_PROP_LETTER_SPACING] = NUM(letter_spacing, R_ANY),
    [SK_PROP_TEXT_TRANSFORM] = KW(text_transform, 4),
    [SK_PROP_TEXT_ALIGN] = KW(text_align, 5),
    [SK_PROP_WHITE_SPACE] = KW(white_space, 3),
    [SK_PROP_TEXT_OVERFLOW] = KW(text_overflow, 2),
    [SK_PROP_TEXT_DECORATION_LINE] = KW(text_decoration, 8),
    [SK_PROP_TEXT_DECORATION_COLOR] = ONE(PF_COLOR, text_decoration_color),
    [SK_PROP_TEXT_SHADOW] = LIST(text_shadow, ntext_shadow, IT_SHADOW, 0, 0),
    [SK_PROP_TEXT_ANCHOR] = KW(text_anchor, 3),
    [SK_PROP_CONTENT] = ONE(PF_CONTENT, content),
    [SK_PROP_FILL] = {PF_PAINT, 0, 0, 0, O(fill_kind), O(fill)},
    [SK_PROP_STROKE] = {PF_PAINT, 0, 0, 0, O(stroke_kind), O(stroke)},
    [SK_PROP_STROKE_WIDTH] = ONE(PF_LEN, stroke_width),
    [SK_PROP_STROKE_DASHARRAY] = LIST(dasharray, ndasharray, IT_LEN, 0, 0),
    [SK_PROP_STROKE_LINECAP] = KW(linecap, 3),
    [SK_PROP_STROKE_LINEJOIN] = KW(linejoin, 3),
    [SK_PROP_ANIMATION_NAME] = LIST(anim_name, nanim_name, IT_KEYF, 0, 1),
    [SK_PROP_ANIMATION_DURATION] = LIST(anim_duration, nanim_duration, IT_F32, R_NONNEG, 1),
    [SK_PROP_ANIMATION_TIMING_FUNCTION] = LIST(anim_timing, nanim_timing, IT_TIMING, 0, 1),
    [SK_PROP_ANIMATION_DELAY] = LIST(anim_delay, nanim_delay, IT_F32, R_ANY, 1),
    [SK_PROP_ANIMATION_ITERATION_COUNT] = LIST(anim_iterations, nanim_iterations, IT_F32, R_ITER, 1),
    [SK_PROP_ANIMATION_DIRECTION] = LIST(anim_direction, nanim_direction, IT_KW, 4, 1),
    [SK_PROP_ANIMATION_FILL_MODE] = LIST(anim_fill, nanim_fill, IT_KW, 4, 1),
    [SK_PROP_ANIMATION_PLAY_STATE] = LIST(anim_paused, nanim_paused, IT_KW, 2, 1),
    [SK_PROP_TRANSITION_PROPERTY] = LIST(tr_property, ntr_property, IT_PROP, 0, 1),
    [SK_PROP_TRANSITION_DURATION] = LIST(tr_duration, ntr_duration, IT_F32, R_NONNEG, 1),
    [SK_PROP_TRANSITION_TIMING_FUNCTION] = LIST(tr_timing, ntr_timing, IT_TIMING, 0, 1),
    [SK_PROP_TRANSITION_DELAY] = LIST(tr_delay, ntr_delay, IT_F32, R_ANY, 1),
    /* counter-reset, counter-increment, contain, flex-wrap and
     * user-select: PF_NONE. skinc has turned the counters into
     * content; the others hold nothing. */
};

static const uint16_t ITEM_SIZE[IT_COUNT] = {
    [IT_LEN] = sizeof(sk_css_len),
    [IT_TRACK] = sizeof(sk_css_track),
    [IT_XF] = sizeof(sk_css_xf),
    [IT_FILTER] = sizeof(sk_css_filter),
    [IT_IMAGE] = sizeof(sk_css_image),
    [IT_POSITION] = sizeof(sk_css_position),
    [IT_BGSIZE] = sizeof(sk_css_bgsize),
    [IT_REPEAT2] = 2,
    [IT_KW] = 1,
    [IT_SHADOW] = sizeof(sk_css_shadow),
    [IT_KEYF] = sizeof(uint32_t),
    [IT_F32] = sizeof(float),
    [IT_TIMING] = sizeof(sk_css_timing),
    [IT_PROP] = sizeof(uint16_t),
};

/* A list of the most items, of the largest, fits the scratch. */
_Static_assert(sizeof(sk_css_xf) <= 64 && sizeof(sk_css_filter) <= 64 && sizeof(sk_css_stop) <= 64 &&
                   sizeof(sk_css_content) <= 64 && sizeof(sk_pack_key) <= 64 && 2 * sizeof(sk_css_len) <= 64 &&
                   sizeof(sk_css_gradient) <= SK_PACK_SCRATCH,
               "a list item outgrows the loader's scratch");

/* ---- Writing ---- */

static void put_len(sk_buf *b, sk_css_len l)
{
    sk_put8(b, l.kind);
    sk_putf32(b, l.px);
    sk_putf32(b, l.pct);
}

static void put_color(sk_buf *b, sk_css_color c)
{
    sk_putf32(b, c.r);
    sk_putf32(b, c.g);
    sk_putf32(b, c.b);
    sk_putf32(b, c.a);
}

static void put_timing(sk_buf *b, const sk_css_timing *t)
{
    sk_put8(b, t->kind);
    sk_put8(b, t->steps_pos);
    sk_put16(b, t->steps);
    sk_putf32(b, t->x1);
    sk_putf32(b, t->y1);
    sk_putf32(b, t->x2);
    sk_putf32(b, t->y2);
}

static void put_shadow(sk_buf *b, const sk_css_shadow *s)
{
    sk_putf32(b, s->x);
    sk_putf32(b, s->y);
    sk_putf32(b, s->blur);
    sk_putf32(b, s->spread);
    put_color(b, s->color);
    sk_put8(b, s->inset);
}

static void put_xf(sk_buf *b, const sk_css_xf *x)
{
    sk_put8(b, x->kind);
    put_len(b, x->tx);
    put_len(b, x->ty);
    for (int i = 0; i < 6; i++)
        sk_putf32(b, x->v[i]);
}

static void put_image(sk_buf *b, const sk_css_image *im)
{
    sk_put8(b, im->kind);
    if (im->kind == SK_IMAGE_URL) {
        sk_put32(b, im->source);
    } else if (im->kind == SK_IMAGE_GRADIENT) {
        const sk_css_gradient *g = im->gradient;
        sk_put8(b, g->kind);
        sk_put8(b, g->repeating);
        sk_put8(b, g->circle);
        sk_put8(b, g->extent);
        sk_put8(b, g->to_corner);
        sk_putf32(b, g->angle);
        put_len(b, g->rx);
        put_len(b, g->ry);
        put_len(b, g->cx);
        put_len(b, g->cy);
        sk_put16(b, g->nstops);
        for (uint16_t i = 0; i < g->nstops; i++) {
            put_color(b, g->stops[i].color);
            put_len(b, g->stops[i].pos);
        }
    }
}

static void put_track(sk_buf *b, const sk_css_track *t)
{
    sk_put8(b, t->kind);
    put_len(b, t->len);
    sk_putf32(b, t->fr);
}

static void put_item(sk_buf *b, uint8_t it, const void *p)
{
    switch (it) {
    case IT_LEN:
        put_len(b, *(const sk_css_len *)p);
        break;
    case IT_TRACK:
        put_track(b, p);
        break;
    case IT_XF:
        put_xf(b, p);
        break;
    case IT_FILTER: {
        const sk_css_filter *f = p;
        sk_put8(b, f->kind);
        sk_putf32(b, f->amount);
        put_shadow(b, &f->shadow);
        break;
    }
    case IT_IMAGE:
        put_image(b, p);
        break;
    case IT_POSITION: {
        const sk_css_position *q = p;
        put_len(b, q->x);
        put_len(b, q->y);
        sk_put8(b, q->x_edge);
        sk_put8(b, q->y_edge);
        break;
    }
    case IT_BGSIZE: {
        const sk_css_bgsize *z = p;
        sk_put8(b, z->kind);
        put_len(b, z->w);
        put_len(b, z->h);
        break;
    }
    case IT_REPEAT2:
        sk_put8(b, ((const uint8_t *)p)[0]);
        sk_put8(b, ((const uint8_t *)p)[1]);
        break;
    case IT_KW:
        sk_put8(b, *(const uint8_t *)p);
        break;
    case IT_SHADOW:
        put_shadow(b, p);
        break;
    case IT_KEYF:
        sk_put32(b, *(const uint32_t *)p);
        break;
    case IT_F32:
        sk_putf32(b, *(const float *)p);
        break;
    case IT_TIMING:
        put_timing(b, p);
        break;
    case IT_PROP:
        sk_put16(b, *(const uint16_t *)p);
        break;
    default:
        break;
    }
}

/* The pointer a style's list field holds. */
static const void *list_of(const sk_css_style *s, uint16_t off)
{
    const void *p;
    memcpy(&p, (const char *)s + off, sizeof p);
    return p;
}

bool sk_pack_put_prop(sk_buf *out, uint16_t id, const sk_css_style *s, sk_pack_intern intern, void *data)
{
    if (id == 0 || id >= SK_PROP_COUNT || PROPS[id].form == PF_NONE)
        return false;
    const propfmt *f = &PROPS[id];
    const char *field = (const char *)s + f->off;
    switch (f->form) {
    case PF_KW:
        sk_put8(out, *(const uint8_t *)field);
        break;
    case PF_BOOL:
        sk_put8(out, *(const bool *)field);
        break;
    case PF_F32:
        sk_putf32(out, *(const float *)field);
        break;
    case PF_WEIGHT:
        sk_put16(out, *(const uint16_t *)field);
        break;
    case PF_LEN:
        put_len(out, *(const sk_css_len *)field);
        break;
    case PF_LEN2:
        put_len(out, ((const sk_css_len *)field)[0]);
        put_len(out, ((const sk_css_len *)field)[1]);
        break;
    case PF_COLOR:
        put_color(out, *(const sk_css_color *)field);
        break;
    case PF_IMAGE:
        put_image(out, (const sk_css_image *)field);
        break;
    case PF_SLICE:
        for (int i = 0; i < 4; i++)
            sk_putf32(out, s->bi_slice[i]);
        sk_put8(out, s->bi_slice_pct);
        sk_put8(out, s->bi_fill);
        break;
    case PF_BIWIDTH:
        for (int i = 0; i < 4; i++)
            put_len(out, s->bi_width[i]);
        sk_put8(out, s->bi_width_num);
        break;
    case PF_F32X4:
        for (int i = 0; i < 4; i++)
            sk_putf32(out, ((const float *)field)[i]);
        break;
    case PF_REPEAT2:
        sk_put8(out, ((const uint8_t *)field)[0]);
        sk_put8(out, ((const uint8_t *)field)[1]);
        break;
    case PF_ZINDEX:
        sk_put8(out, s->z_auto);
        sk_put32(out, (uint32_t)s->z_index);
        break;
    case PF_TRACK:
        put_track(out, (const sk_css_track *)field);
        break;
    case PF_LINE:
        sk_put8(out, ((const sk_css_line *)field)->kind);
        sk_put16(out, (uint16_t)((const sk_css_line *)field)->n);
        break;
    case PF_LH:
        sk_put8(out, s->line_height_kind);
        sk_putf32(out, s->line_height);
        break;
    case PF_PAINT:
        sk_put8(out, *(const uint8_t *)field);
        put_color(out, *(const sk_css_color *)((const char *)s + f->noff));
        break;
    case PF_FAMILY:
        sk_put16(out, s->nfont_family);
        for (uint16_t i = 0; i < s->nfont_family; i++)
            sk_put32(out, intern(data, s->font_family[i]));
        sk_put16(out, s->font);
        sk_put8(out, s->font_synth);
        break;
    case PF_CONTENT:
        sk_put8(out, s->content_none);
        sk_put16(out, s->ncontent);
        for (uint16_t i = 0; i < s->ncontent; i++) {
            const sk_css_content *c = &s->content[i];
            sk_put8(out, c->kind);
            sk_put32(out, intern(data, c->text));
            sk_put32(out, (uint32_t)c->base);
            sk_put32(out, (uint32_t)c->step);
        }
        break;
    case PF_CLIP:
        sk_put8(out, s->clip_evenodd);
        sk_put16(out, s->nclip);
        for (uint32_t i = 0; i < 2u * s->nclip; i++)
            put_len(out, s->clip[i]);
        break;
    case PF_LIST: {
        uint16_t n;
        memcpy(&n, (const char *)s + f->noff, sizeof n);
        const char *items = list_of(s, f->off);
        sk_put16(out, n);
        for (uint16_t i = 0; i < n; i++)
            put_item(out, f->item, items + (size_t)i * ITEM_SIZE[f->item]);
        break;
    }
    default:
        return false;
    }
    return true;
}

void sk_pack_put_key(sk_buf *out, const sk_pack_key *k)
{
    sk_putf32(out, k->offset);
    sk_put8(out, k->has_transform);
    sk_put8(out, k->has_opacity);
    sk_put8(out, k->has_timing);
    sk_put8(out, 0);
    sk_putf32(out, k->opacity);
    put_timing(out, &k->timing);
    sk_put16(out, k->ntransform);
    for (uint16_t i = 0; i < k->ntransform; i++)
        put_xf(out, &k->transform[i]);
}

/* ---- Reading ---- */

/* A cursor over one payload, or one track. */
typedef struct {
    sk_packfmt *c;
    const uint8_t *p;
    size_t n, at;
    unsigned id; /* the property, 0 in a track */
} cur;

static bool bad(cur *k, const char *fmt, ...) SK_PRINTF(2, 3);

static bool bad(cur *k, const char *fmt, ...)
{
    char what[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    if (k->id)
        return sk_packfmt_fail(k->c, "property %u: %s", k->id, what);
    return sk_packfmt_fail(k->c, "%s", what);
}

static const uint8_t *take(cur *k, size_t m)
{
    if (k->n - k->at < m) {
        bad(k, "runs past its %zu bytes", k->n);
        return NULL;
    }
    const uint8_t *r = k->p + k->at;
    k->at += m;
    return r;
}

static bool in_range(float v, int range)
{
    switch (range) {
    case R_UNIT:
        return v >= 0 && v <= 1;
    case R_NONNEG:
        return v >= 0 && v <= SK_PACK_MAX_FLOAT;
    case R_ITER:
        return v >= 0 && (v <= SK_PACK_MAX_FLOAT || v == INFINITY);
    default:
        return v >= -SK_PACK_MAX_FLOAT && v <= SK_PACK_MAX_FLOAT;
    }
}

/* The readers write their value whether or not they succeed, 0 when
 * the payload ends. */
static bool get_u8(cur *k, uint8_t *v)
{
    const uint8_t *p = take(k, 1);
    *v = p ? p[0] : 0;
    return p != NULL;
}

static bool get_u16(cur *k, uint16_t *v)
{
    const uint8_t *p = take(k, 2);
    *v = p ? sk_le16(p) : 0;
    return p != NULL;
}

static bool get_u32(cur *k, uint32_t *v)
{
    const uint8_t *p = take(k, 4);
    *v = p ? sk_le32(p) : 0;
    return p != NULL;
}

static bool get_kw(cur *k, unsigned below, const char *what, uint8_t *v)
{
    if (!get_u8(k, v))
        return false;
    return *v < below || bad(k, "%s %u is none of the %u", what, (unsigned)*v, below);
}

static bool get_bool(cur *k, bool *v)
{
    uint8_t u;
    if (!get_u8(k, &u))
        return false;
    *v = u != 0;
    return u <= 1 || bad(k, "a flag of %u, not 0 or 1", (unsigned)u);
}

static bool get_f32(cur *k, int range, const char *what, float *v)
{
    const uint8_t *p = take(k, 4);
    *v = p ? sk_lef32(p) : 0;
    if (!p)
        return false;
    return in_range(*v, range) || bad(k, "%s %g is out of its range", what, (double)*v);
}

static bool get_len(cur *k, sk_css_len *l)
{
    return get_kw(k, SK_LEN_VALUE + 1, "a length's kind", &l->kind) && get_f32(k, R_ANY, "a length's px", &l->px) &&
           get_f32(k, R_ANY, "a length's percentage", &l->pct);
}

static bool get_color(cur *k, sk_css_color *c)
{
    return get_f32(k, R_UNIT, "a colour's red", &c->r) && get_f32(k, R_UNIT, "a colour's green", &c->g) &&
           get_f32(k, R_UNIT, "a colour's blue", &c->b) && get_f32(k, R_UNIT, "a colour's alpha", &c->a);
}

static bool get_timing(cur *k, sk_css_timing *t)
{
    if (!get_kw(k, SK_TIMING_STEPS + 1, "an easing", &t->kind) ||
        !get_kw(k, SK_STEP_JUMP_BOTH + 1, "a step position", &t->steps_pos) || !get_u16(k, &t->steps) ||
        !get_f32(k, R_ANY, "an easing's x1", &t->x1) || !get_f32(k, R_ANY, "an easing's y1", &t->y1) ||
        !get_f32(k, R_ANY, "an easing's x2", &t->x2) || !get_f32(k, R_ANY, "an easing's y2", &t->y2))
        return false;
    if (t->kind == SK_TIMING_STEPS && (t->steps < 1 || (t->steps_pos == SK_STEP_JUMP_NONE && t->steps < 2)))
        return bad(k, "steps(%u) with that position takes no step", (unsigned)t->steps);
    if (t->kind == SK_TIMING_CUBIC && !(t->x1 >= 0 && t->x1 <= 1 && t->x2 >= 0 && t->x2 <= 1))
        return bad(k, "a cubic-bezier's x outside 0 to 1");
    return true;
}

static bool get_shadow(cur *k, sk_css_shadow *s)
{
    return get_f32(k, R_ANY, "a shadow's x", &s->x) && get_f32(k, R_ANY, "a shadow's y", &s->y) &&
           get_f32(k, R_NONNEG, "a shadow's blur", &s->blur) && get_f32(k, R_ANY, "a shadow's spread", &s->spread) &&
           get_color(k, &s->color) && get_bool(k, &s->inset);
}

static bool get_xf(cur *k, sk_css_xf *x)
{
    if (!get_kw(k, SK_XF_MATRIX + 1, "a transform function", &x->kind) || !get_len(k, &x->tx) ||
        !get_len(k, &x->ty))
        return false;
    for (int i = 0; i < 6; i++)
        if (!get_f32(k, R_ANY, "a transform's number", &x->v[i]))
            return false;
    return true;
}

/* A list's count: at most SK_PACK_MAX_LIST, at least min. */
static bool get_count(cur *k, unsigned min, uint16_t *n)
{
    if (!get_u16(k, n))
        return false;
    if (*n > SK_PACK_MAX_LIST)
        return bad(k, "a list of %u items, more than %u", (unsigned)*n, SK_PACK_MAX_LIST);
    return *n >= min || bad(k, "a list of %u items, fewer than %u", (unsigned)*n, min);
}

static bool get_gradient(cur *k, const sk_css_gradient **out)
{
    sk_css_gradient g;
    memset(&g, 0, sizeof g);
    uint16_t n;
    if (!get_kw(k, SK_GRAD_RADIAL + 1, "a gradient", &g.kind) || !get_u8(k, &g.repeating) || !get_u8(k, &g.circle) ||
        !get_kw(k, SK_EXTENT_EXPLICIT + 1, "a gradient's extent", &g.extent) || !get_u8(k, &g.to_corner) ||
        !get_f32(k, R_ANY, "a gradient's angle", &g.angle) || !get_len(k, &g.rx) || !get_len(k, &g.ry) ||
        !get_len(k, &g.cx) || !get_len(k, &g.cy) || !get_count(k, 2, &n))
        return false;
    if (g.repeating > 1 || g.circle > 1)
        return bad(k, "a gradient's flag past 1");
    if (g.to_corner != 0 && (g.to_corner & ~7u) != 0)
        return bad(k, "a gradient's corner %u is none", (unsigned)g.to_corner);
    if (g.to_corner != 0 && !(g.to_corner & 4))
        return bad(k, "a gradient's corner %u is none", (unsigned)g.to_corner);
    sk_css_stop *stops = sk_packfmt_list(k->c, n, sizeof *stops);
    for (uint16_t i = 0; i < n; i++) {
        sk_css_stop st;
        if (!get_color(k, &st.color) || !get_len(k, &st.pos))
            return false;
        stops[i] = st;
    }
    g.stops = stops;
    g.nstops = n;
    sk_css_gradient *kept = sk_packfmt_list(k->c, 1, sizeof *kept);
    *kept = g;
    *out = kept;
    return true;
}

static bool get_image(cur *k, sk_css_image *im)
{
    sk_css_image x = {0, 0, NULL};
    if (!get_kw(k, SK_IMAGE_GRADIENT + 1, "an image's kind", &x.kind))
        return false;
    if (x.kind == SK_IMAGE_URL) {
        if (!get_u32(k, &x.source))
            return false;
        if (x.source >= k->c->nimages)
            return bad(k, "image %u is past the %u images", (unsigned)x.source, (unsigned)k->c->nimages);
    } else if (x.kind == SK_IMAGE_GRADIENT && !get_gradient(k, &x.gradient)) {
        return false;
    }
    *im = x;
    return true;
}

static bool get_track(cur *k, sk_css_track *t)
{
    return get_kw(k, SK_TRACK_AUTO + 1, "a track's kind", &t->kind) && get_len(k, &t->len) &&
           get_f32(k, R_NONNEG, "a track's fr", &t->fr);
}

static bool get_line(cur *k, sk_css_line *l)
{
    uint16_t n;
    if (!get_kw(k, SK_LINE_SPAN + 1, "a grid line's kind", &l->kind) || !get_u16(k, &n))
        return false;
    l->n = (int16_t)n;
    if (l->kind == SK_LINE_NUMBER && l->n == 0)
        return bad(k, "grid line 0");
    if (l->kind == SK_LINE_SPAN && l->n < 1)
        return bad(k, "a span of %d", (int)l->n);
    return true;
}

static bool get_item(cur *k, uint8_t it, uint8_t arg, void *out)
{
    switch (it) {
    case IT_LEN:
        return get_len(k, out);
    case IT_TRACK:
        return get_track(k, out);
    case IT_XF:
        return get_xf(k, out);
    case IT_FILTER: {
        sk_css_filter *f = out;
        if (!get_kw(k, SK_FX_DROP_SHADOW + 1, "a filter", &f->kind) ||
            !get_f32(k, f->kind == SK_FX_HUE_ROTATE ? R_ANY : R_NONNEG, "a filter's amount", &f->amount))
            return false;
        return get_shadow(k, &f->shadow);
    }
    case IT_IMAGE:
        return get_image(k, out);
    case IT_POSITION: {
        sk_css_position *q = out;
        return get_len(k, &q->x) && get_len(k, &q->y) && get_kw(k, 2, "a position's edge", &q->x_edge) &&
               get_kw(k, 2, "a position's edge", &q->y_edge);
    }
    case IT_BGSIZE: {
        sk_css_bgsize *z = out;
        return get_kw(k, SK_BGSIZE_CONTAIN + 1, "a background size", &z->kind) && get_len(k, &z->w) &&
               get_len(k, &z->h);
    }
    case IT_REPEAT2:
        return get_kw(k, arg, "a repeat", &((uint8_t *)out)[0]) && get_kw(k, arg, "a repeat", &((uint8_t *)out)[1]);
    case IT_KW:
        return get_kw(k, arg, "a keyword", out);
    case IT_SHADOW:
        return get_shadow(k, out);
    case IT_KEYF: {
        uint32_t v;
        if (!get_u32(k, &v))
            return false;
        if (v != SK_PACK_NONE && v >= k->c->nkeyframes)
            return bad(k, "keyframes %u past the %u tracks", (unsigned)v, (unsigned)k->c->nkeyframes);
        memcpy(out, &v, sizeof v);
        return true;
    }
    case IT_F32: {
        float v;
        if (!get_f32(k, arg, "a number", &v))
            return false;
        memcpy(out, &v, sizeof v);
        return true;
    }
    case IT_TIMING:
        return get_timing(k, out);
    case IT_PROP: {
        uint16_t v;
        if (!get_u16(k, &v))
            return false;
        memcpy(out, &v, sizeof v);
        return true;
    }
    default:
        return false;
    }
}

/* Writes p into a style's list field. */
static void set_list(sk_css_style *s, uint16_t off, const void *p)
{
    memcpy((char *)s + off, &p, sizeof p);
}

static bool get_prop(cur *k, const propfmt *f, sk_css_style *s)
{
    char *field = (char *)s + f->off;
    switch (f->form) {
    case PF_KW:
        return get_kw(k, f->arg, "a keyword", (uint8_t *)field);
    case PF_BOOL:
        return get_bool(k, (bool *)field);
    case PF_F32:
        return get_f32(k, f->arg, "a number", (float *)field);
    case PF_WEIGHT: {
        uint16_t w;
        if (!get_u16(k, &w))
            return false;
        s->font_weight = w;
        return (w >= 1 && w <= 1000) || bad(k, "a weight of %u", (unsigned)w);
    }
    case PF_LEN:
        return get_len(k, (sk_css_len *)field);
    case PF_LEN2:
        return get_len(k, &((sk_css_len *)field)[0]) && get_len(k, &((sk_css_len *)field)[1]);
    case PF_COLOR:
        return get_color(k, (sk_css_color *)field);
    case PF_IMAGE:
        return get_image(k, (sk_css_image *)field);
    case PF_SLICE:
        for (int i = 0; i < 4; i++)
            if (!get_f32(k, R_NONNEG, "a slice", &s->bi_slice[i]))
                return false;
        if (!get_u8(k, &s->bi_slice_pct) || !get_bool(k, &s->bi_fill))
            return false;
        return s->bi_slice_pct < 16 || bad(k, "percent bits 0x%x", (unsigned)s->bi_slice_pct);
    case PF_BIWIDTH:
        for (int i = 0; i < 4; i++)
            if (!get_len(k, &s->bi_width[i]))
                return false;
        if (!get_u8(k, &s->bi_width_num))
            return false;
        return s->bi_width_num < 16 || bad(k, "number bits 0x%x", (unsigned)s->bi_width_num);
    case PF_F32X4:
        for (int i = 0; i < 4; i++)
            if (!get_f32(k, f->arg, "a number", &((float *)field)[i]))
                return false;
        return true;
    case PF_REPEAT2:
        return get_kw(k, f->arg, "a repeat", &((uint8_t *)field)[0]) &&
               get_kw(k, f->arg, "a repeat", &((uint8_t *)field)[1]);
    case PF_ZINDEX: {
        uint32_t z;
        if (!get_bool(k, &s->z_auto) || !get_u32(k, &z))
            return false;
        s->z_index = (int32_t)z;
        return true;
    }
    case PF_TRACK:
        return get_track(k, (sk_css_track *)field);
    case PF_LINE:
        return get_line(k, (sk_css_line *)field);
    case PF_LH:
        return get_kw(k, SK_LH_PX + 1, "a line height's kind", &s->line_height_kind) &&
               get_f32(k, R_NONNEG, "a line height", &s->line_height);
    case PF_PAINT:
        return get_kw(k, SK_PAINT_COLOR + 1, "a paint", (uint8_t *)field) &&
               get_color(k, (sk_css_color *)((char *)s + f->noff));
    case PF_FAMILY: {
        uint16_t n;
        if (!get_count(k, 1, &n))
            return false;
        const char **names = sk_packfmt_list(k->c, n, sizeof *names);
        for (uint16_t i = 0; i < n; i++) {
            uint32_t off;
            const char *name;
            if (!get_u32(k, &off) || !sk_packfmt_str(k->c, off, false, "a font family", &name))
                return false;
            names[i] = name;
        }
        s->font_family = names;
        s->nfont_family = n;
        if (!get_u16(k, &s->font) || !get_u8(k, &s->font_synth))
            return false;
        if (s->font >= k->c->nfaces)
            return bad(k, "face %u past the %u faces", (unsigned)s->font, (unsigned)k->c->nfaces);
        return s->font_synth <= 3 || bad(k, "synthesis bits 0x%x", (unsigned)s->font_synth);
    }
    case PF_CONTENT: {
        uint16_t n;
        if (!get_bool(k, &s->content_none) || !get_count(k, 0, &n))
            return false;
        if (s->content_none && n)
            return bad(k, "content none with %u parts", (unsigned)n);
        sk_css_content *parts = sk_packfmt_list(k->c, n, sizeof *parts);
        for (uint16_t i = 0; i < n; i++) {
            sk_css_content part;
            uint32_t off, base, step;
            if (!get_kw(k, SK_CONTENT_ORDINAL + 1, "a content part", &part.kind) || !get_u32(k, &off) ||
                !sk_packfmt_str(k->c, off, part.kind == SK_CONTENT_ORDINAL, "a content part's text", &part.text) ||
                !get_u32(k, &base) || !get_u32(k, &step))
                return false;
            part.base = (int32_t)base;
            part.step = (int32_t)step;
            parts[i] = part;
        }
        s->content = parts;
        s->ncontent = n;
        return true;
    }
    case PF_CLIP: {
        uint16_t n;
        if (!get_u8(k, &s->clip_evenodd) || !get_count(k, 0, &n))
            return false;
        if (s->clip_evenodd > 1)
            return bad(k, "a fill rule of %u", (unsigned)s->clip_evenodd);
        sk_css_len *pts = sk_packfmt_list(k->c, 2u * n, sizeof *pts);
        for (uint32_t i = 0; i < 2u * n; i++) {
            sk_css_len l;
            if (!get_len(k, &l))
                return false;
            pts[i] = l;
        }
        s->clip = pts;
        s->nclip = n;
        return true;
    }
    case PF_LIST: {
        uint16_t n;
        if (!get_count(k, f->min, &n))
            return false;
        char *items = sk_packfmt_list(k->c, n, ITEM_SIZE[f->item]);
        union {
            max_align_t align;
            unsigned char bytes[64];
        } one;
        for (uint16_t i = 0; i < n; i++) {
            memset(&one, 0, sizeof one);
            if (!get_item(k, f->item, f->arg, one.bytes))
                return false;
            memcpy(items + (size_t)i * ITEM_SIZE[f->item], one.bytes, ITEM_SIZE[f->item]);
        }
        set_list(s, f->off, items);
        memcpy((char *)s + f->noff, &n, sizeof n);
        return true;
    }
    default:
        return bad(k, "has no payload");
    }
}

/* The properties whose initial value is currentcolor, the style's own
 * colour: the four border colours, the outline's, the decoration's. */
static const uint16_t CURRENT[] = {SK_PROP_BORDER_TOP_COLOR,    SK_PROP_BORDER_RIGHT_COLOR,
                                   SK_PROP_BORDER_BOTTOM_COLOR, SK_PROP_BORDER_LEFT_COLOR,
                                   SK_PROP_OUTLINE_COLOR,       SK_PROP_TEXT_DECORATION_COLOR};

bool sk_packfmt_style(sk_packfmt *c, const uint8_t *p, size_t n, sk_css_style *out)
{
    sk_css_style s;
    sk_style_initial(&s, (sk_css_color){0, 0, 0, 1});
    if (n < 2)
        return sk_packfmt_fail(c, "%zu bytes, too few for a style", n);
    uint16_t count = sk_le16(p);
    size_t at = 2;
    unsigned last = 0, given = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (n - at < 4)
            return sk_packfmt_fail(c, "property %u of %u runs past the style", (unsigned)i, (unsigned)count);
        unsigned id = sk_le16(p + at), len = sk_le16(p + at + 2);
        at += 4;
        if (id == 0)
            return sk_packfmt_fail(c, "property 0 is none");
        if (id <= last)
            return sk_packfmt_fail(c, "property %u after %u: a style's properties rise", id, last);
        last = id;
        if (len > n - at)
            return sk_packfmt_fail(c, "property %u: %u bytes run past the style", id, len);
        if (id < SK_PROP_COUNT) {
            if (PROPS[id].form == PF_NONE)
                return sk_packfmt_fail(c, "property %u is one a pack never holds", id);
            cur k = {c, p + at, len, 0, id};
            if (!get_prop(&k, &PROPS[id], &s))
                return false;
            if (k.at != len)
                return sk_packfmt_fail(c, "property %u: %zu of its %u bytes unread", id, len - k.at, len);
            for (size_t j = 0; j < SK_LEN(CURRENT); j++)
                if (CURRENT[j] == id)
                    given |= 1u << j;
        }
        /* An id past those this loader knows is a newer minor's: its
         * length steps over it. */
        at += len;
    }
    if (at != n)
        return sk_packfmt_fail(c, "%zu bytes past its properties", n - at);
    sk_css_color *current[] = {&s.border_color[SK_TOP],    &s.border_color[SK_RIGHT], &s.border_color[SK_BOTTOM],
                               &s.border_color[SK_LEFT],   &s.outline_color,          &s.text_decoration_color};
    for (size_t j = 0; j < SK_LEN(CURRENT); j++)
        if (!(given & (1u << j)))
            *current[j] = s.color;
    *out = s;
    return true;
}

bool sk_packfmt_track(sk_packfmt *c, const uint8_t *p, size_t n, sk_pack_keyframes *out)
{
    cur k = {c, p, n, 0, 0};
    uint32_t name_at;
    uint16_t nkeys;
    sk_pack_keyframes t = {NULL, NULL, 0};
    if (!get_u32(&k, &name_at) || !sk_packfmt_str(c, name_at, false, "a track's name", &t.name) ||
        !get_count(&k, 0, &nkeys))
        return false;
    sk_pack_key *keys = sk_packfmt_list(c, nkeys, sizeof *keys);
    float before = -1;
    for (uint16_t i = 0; i < nkeys; i++) {
        sk_pack_key key;
        memset(&key, 0, sizeof key);
        uint8_t pad;
        uint16_t nxf;
        if (!get_f32(&k, R_UNIT, "a key's offset", &key.offset) || !get_bool(&k, &key.has_transform) ||
            !get_bool(&k, &key.has_opacity) || !get_bool(&k, &key.has_timing) || !get_u8(&k, &pad) ||
            !get_f32(&k, R_UNIT, "a key's opacity", &key.opacity) || !get_timing(&k, &key.timing) ||
            !get_count(&k, 0, &nxf))
            return false;
        if (!(key.offset > before))
            return bad(&k, "key %u at %g after %g: a track's keys rise", (unsigned)i, (double)key.offset,
                       (double)before);
        before = key.offset;
        sk_css_xf *xf = sk_packfmt_list(c, nxf, sizeof *xf);
        for (uint16_t j = 0; j < nxf; j++) {
            sk_css_xf x;
            if (!get_xf(&k, &x))
                return false;
            xf[j] = x;
        }
        key.transform = xf;
        key.ntransform = nxf;
        keys[i] = key;
    }
    if (k.at != n)
        return bad(&k, "%zu bytes past its keys", n - k.at);
    t.keys = keys;
    t.nkeys = nkeys;
    *out = t;
    return true;
}

bool sk_pack_check_prop(uint16_t id, const uint8_t *payload, size_t n, const char *strs, size_t nstrs,
                        uint32_t nimages, uint32_t nkeyframes, uint32_t nfaces, char *err, size_t err_len)
{
    if (n > 0xffff) {
        if (err_len)
            snprintf(err, err_len, "%zu bytes, more than a property holds", n);
        return false;
    }
    sk_packfmt c;
    memset(&c, 0, sizeof c);
    c.strs = strs;
    c.nstrs = (uint32_t)nstrs;
    c.nimages = nimages;
    c.nkeyframes = nkeyframes;
    c.nfaces = nfaces;
    c.scratch = sk_xmalloc(SK_PACK_SCRATCH);
    c.err = err;
    c.err_len = err_len;
    sk_buf rec = {0};
    sk_put16(&rec, 1);
    sk_put16(&rec, id);
    sk_put16(&rec, (unsigned)n);
    if (n)
        sk_buf_append(&rec, payload, n);
    sk_css_style s;
    bool ok = sk_packfmt_style(&c, (const uint8_t *)rec.ptr, rec.len, &s);
    sk_buf_free(&rec);
    free(c.scratch);
    /* The caller names the property: "property 9: " goes. */
    const char *colon = !ok && err_len && !strncmp(err, "property ", 9) ? strstr(err, ": ") : NULL;
    if (colon)
        memmove(err, colon + 2, strlen(colon + 2) + 1);
    return ok;
}
