/* Decision 0037's properties: each longhand's grammar, the fields of
 * sk_css_style it fills, its initial value and whether it inherits;
 * and the shorthands, which set their longhands. */
#include "props.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- The table ---- */

static const sk_propdef PROPS[] = {
    {"display", SK_PROP_DISPLAY, false},
    {"position", SK_PROP_POSITION, false},
    {"float", SK_PROP_FLOAT, false},
    {"box-sizing", SK_PROP_BOX_SIZING, false},
    {"top", SK_PROP_TOP, false},
    {"right", SK_PROP_RIGHT, false},
    {"bottom", SK_PROP_BOTTOM, false},
    {"left", SK_PROP_LEFT, false},
    {"width", SK_PROP_WIDTH, false},
    {"height", SK_PROP_HEIGHT, false},
    {"min-width", SK_PROP_MIN_WIDTH, false},
    {"min-height", SK_PROP_MIN_HEIGHT, false},
    {"max-width", SK_PROP_MAX_WIDTH, false},
    {"max-height", SK_PROP_MAX_HEIGHT, false},
    {"margin-top", SK_PROP_MARGIN_TOP, false},
    {"margin-right", SK_PROP_MARGIN_RIGHT, false},
    {"margin-bottom", SK_PROP_MARGIN_BOTTOM, false},
    {"margin-left", SK_PROP_MARGIN_LEFT, false},
    {"padding-top", SK_PROP_PADDING_TOP, false},
    {"padding-right", SK_PROP_PADDING_RIGHT, false},
    {"padding-bottom", SK_PROP_PADDING_BOTTOM, false},
    {"padding-left", SK_PROP_PADDING_LEFT, false},
    {"border-top-width", SK_PROP_BORDER_TOP_WIDTH, false},
    {"border-right-width", SK_PROP_BORDER_RIGHT_WIDTH, false},
    {"border-bottom-width", SK_PROP_BORDER_BOTTOM_WIDTH, false},
    {"border-left-width", SK_PROP_BORDER_LEFT_WIDTH, false},
    {"border-top-style", SK_PROP_BORDER_TOP_STYLE, false},
    {"border-right-style", SK_PROP_BORDER_RIGHT_STYLE, false},
    {"border-bottom-style", SK_PROP_BORDER_BOTTOM_STYLE, false},
    {"border-left-style", SK_PROP_BORDER_LEFT_STYLE, false},
    {"border-top-color", SK_PROP_BORDER_TOP_COLOR, false},
    {"border-right-color", SK_PROP_BORDER_RIGHT_COLOR, false},
    {"border-bottom-color", SK_PROP_BORDER_BOTTOM_COLOR, false},
    {"border-left-color", SK_PROP_BORDER_LEFT_COLOR, false},
    {"border-top-left-radius", SK_PROP_BORDER_TOP_LEFT_RADIUS, false},
    {"border-top-right-radius", SK_PROP_BORDER_TOP_RIGHT_RADIUS, false},
    {"border-bottom-right-radius", SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS, false},
    {"border-bottom-left-radius", SK_PROP_BORDER_BOTTOM_LEFT_RADIUS, false},
    {"border-image-source", SK_PROP_BORDER_IMAGE_SOURCE, false},
    {"border-image-slice", SK_PROP_BORDER_IMAGE_SLICE, false},
    {"border-image-width", SK_PROP_BORDER_IMAGE_WIDTH, false},
    {"border-image-outset", SK_PROP_BORDER_IMAGE_OUTSET, false},
    {"border-image-repeat", SK_PROP_BORDER_IMAGE_REPEAT, false},
    {"outline-width", SK_PROP_OUTLINE_WIDTH, false},
    {"outline-style", SK_PROP_OUTLINE_STYLE, false},
    {"outline-color", SK_PROP_OUTLINE_COLOR, false},
    {"outline-offset", SK_PROP_OUTLINE_OFFSET, false},
    {"overflow-x", SK_PROP_OVERFLOW_X, false},
    {"overflow-y", SK_PROP_OVERFLOW_Y, false},
    {"z-index", SK_PROP_Z_INDEX, false},
    {"visibility", SK_PROP_VISIBILITY, true},
    {"opacity", SK_PROP_OPACITY, false},
    {"pointer-events", SK_PROP_POINTER_EVENTS, true},
    {"cursor", SK_PROP_CURSOR, true},
    {"mix-blend-mode", SK_PROP_MIX_BLEND_MODE, false},
    {"isolation", SK_PROP_ISOLATION, false},
    {"contain", SK_PROP_CONTAIN, false},
    {"flex-direction", SK_PROP_FLEX_DIRECTION, false},
    {"flex-wrap", SK_PROP_FLEX_WRAP, false},
    {"flex-grow", SK_PROP_FLEX_GROW, false},
    {"flex-shrink", SK_PROP_FLEX_SHRINK, false},
    {"flex-basis", SK_PROP_FLEX_BASIS, false},
    {"justify-content", SK_PROP_JUSTIFY_CONTENT, false},
    {"align-items", SK_PROP_ALIGN_ITEMS, false},
    {"align-self", SK_PROP_ALIGN_SELF, false},
    {"row-gap", SK_PROP_ROW_GAP, false},
    {"column-gap", SK_PROP_COLUMN_GAP, false},
    {"grid-template-columns", SK_PROP_GRID_TEMPLATE_COLUMNS, false},
    {"grid-template-rows", SK_PROP_GRID_TEMPLATE_ROWS, false},
    {"grid-auto-rows", SK_PROP_GRID_AUTO_ROWS, false},
    {"grid-auto-columns", SK_PROP_GRID_AUTO_COLUMNS, false},
    {"grid-column-start", SK_PROP_GRID_COLUMN_START, false},
    {"grid-column-end", SK_PROP_GRID_COLUMN_END, false},
    {"grid-row-start", SK_PROP_GRID_ROW_START, false},
    {"grid-row-end", SK_PROP_GRID_ROW_END, false},
    {"transform", SK_PROP_TRANSFORM, false},
    {"transform-origin", SK_PROP_TRANSFORM_ORIGIN, false},
    {"filter", SK_PROP_FILTER, false},
    {"clip-path", SK_PROP_CLIP_PATH, false},
    {"image-rendering", SK_PROP_IMAGE_RENDERING, true},
    {"background-color", SK_PROP_BACKGROUND_COLOR, false},
    {"background-image", SK_PROP_BACKGROUND_IMAGE, false},
    {"background-position", SK_PROP_BACKGROUND_POSITION, false},
    {"background-size", SK_PROP_BACKGROUND_SIZE, false},
    {"background-repeat", SK_PROP_BACKGROUND_REPEAT, false},
    {"background-blend-mode", SK_PROP_BACKGROUND_BLEND_MODE, false},
    {"background-origin", SK_PROP_BACKGROUND_ORIGIN, false},
    {"background-clip", SK_PROP_BACKGROUND_CLIP, false},
    {"box-shadow", SK_PROP_BOX_SHADOW, false},
    {"color", SK_PROP_COLOR, true},
    {"font-family", SK_PROP_FONT_FAMILY, true},
    {"font-size", SK_PROP_FONT_SIZE, true},
    {"font-weight", SK_PROP_FONT_WEIGHT, true},
    {"font-style", SK_PROP_FONT_STYLE, true},
    {"line-height", SK_PROP_LINE_HEIGHT, true},
    {"letter-spacing", SK_PROP_LETTER_SPACING, true},
    {"text-transform", SK_PROP_TEXT_TRANSFORM, true},
    {"text-align", SK_PROP_TEXT_ALIGN, true},
    {"white-space", SK_PROP_WHITE_SPACE, true},
    {"text-overflow", SK_PROP_TEXT_OVERFLOW, false},
    {"text-decoration-line", SK_PROP_TEXT_DECORATION_LINE, false},
    {"text-decoration-color", SK_PROP_TEXT_DECORATION_COLOR, false},
    {"text-shadow", SK_PROP_TEXT_SHADOW, true},
    {"text-anchor", SK_PROP_TEXT_ANCHOR, true},
    {"content", SK_PROP_CONTENT, false},
    {"counter-reset", SK_PROP_COUNTER_RESET, false},
    {"counter-increment", SK_PROP_COUNTER_INCREMENT, false},
    {"fill", SK_PROP_FILL, true},
    {"stroke", SK_PROP_STROKE, true},
    {"stroke-width", SK_PROP_STROKE_WIDTH, true},
    {"stroke-dasharray", SK_PROP_STROKE_DASHARRAY, true},
    {"stroke-linecap", SK_PROP_STROKE_LINECAP, true},
    {"stroke-linejoin", SK_PROP_STROKE_LINEJOIN, true},
    {"animation-name", SK_PROP_ANIMATION_NAME, false},
    {"animation-duration", SK_PROP_ANIMATION_DURATION, false},
    {"animation-timing-function", SK_PROP_ANIMATION_TIMING_FUNCTION, false},
    {"animation-delay", SK_PROP_ANIMATION_DELAY, false},
    {"animation-iteration-count", SK_PROP_ANIMATION_ITERATION_COUNT, false},
    {"animation-direction", SK_PROP_ANIMATION_DIRECTION, false},
    {"animation-fill-mode", SK_PROP_ANIMATION_FILL_MODE, false},
    {"animation-play-state", SK_PROP_ANIMATION_PLAY_STATE, false},
    {"transition-property", SK_PROP_TRANSITION_PROPERTY, false},
    {"transition-duration", SK_PROP_TRANSITION_DURATION, false},
    {"transition-timing-function", SK_PROP_TRANSITION_TIMING_FUNCTION, false},
    {"transition-delay", SK_PROP_TRANSITION_DELAY, false},
    {"user-select", SK_PROP_USER_SELECT, false},
};

#define L(...) ((const uint16_t[]){__VA_ARGS__})
#define SH(name, ...) {name, L(__VA_ARGS__), sizeof(L(__VA_ARGS__)) / sizeof(uint16_t)}

static const sk_shorthand SHORTHANDS[] = {
    SH("inset", SK_PROP_TOP, SK_PROP_RIGHT, SK_PROP_BOTTOM, SK_PROP_LEFT),
    SH("margin", SK_PROP_MARGIN_TOP, SK_PROP_MARGIN_RIGHT, SK_PROP_MARGIN_BOTTOM, SK_PROP_MARGIN_LEFT),
    SH("padding", SK_PROP_PADDING_TOP, SK_PROP_PADDING_RIGHT, SK_PROP_PADDING_BOTTOM, SK_PROP_PADDING_LEFT),
    SH("border-width", SK_PROP_BORDER_TOP_WIDTH, SK_PROP_BORDER_RIGHT_WIDTH, SK_PROP_BORDER_BOTTOM_WIDTH,
       SK_PROP_BORDER_LEFT_WIDTH),
    SH("border-style", SK_PROP_BORDER_TOP_STYLE, SK_PROP_BORDER_RIGHT_STYLE, SK_PROP_BORDER_BOTTOM_STYLE,
       SK_PROP_BORDER_LEFT_STYLE),
    SH("border-color", SK_PROP_BORDER_TOP_COLOR, SK_PROP_BORDER_RIGHT_COLOR, SK_PROP_BORDER_BOTTOM_COLOR,
       SK_PROP_BORDER_LEFT_COLOR),
    SH("border-top", SK_PROP_BORDER_TOP_WIDTH, SK_PROP_BORDER_TOP_STYLE, SK_PROP_BORDER_TOP_COLOR),
    SH("border-right", SK_PROP_BORDER_RIGHT_WIDTH, SK_PROP_BORDER_RIGHT_STYLE, SK_PROP_BORDER_RIGHT_COLOR),
    SH("border-bottom", SK_PROP_BORDER_BOTTOM_WIDTH, SK_PROP_BORDER_BOTTOM_STYLE, SK_PROP_BORDER_BOTTOM_COLOR),
    SH("border-left", SK_PROP_BORDER_LEFT_WIDTH, SK_PROP_BORDER_LEFT_STYLE, SK_PROP_BORDER_LEFT_COLOR),
    SH("border", SK_PROP_BORDER_TOP_WIDTH, SK_PROP_BORDER_RIGHT_WIDTH, SK_PROP_BORDER_BOTTOM_WIDTH,
       SK_PROP_BORDER_LEFT_WIDTH, SK_PROP_BORDER_TOP_STYLE, SK_PROP_BORDER_RIGHT_STYLE, SK_PROP_BORDER_BOTTOM_STYLE,
       SK_PROP_BORDER_LEFT_STYLE, SK_PROP_BORDER_TOP_COLOR, SK_PROP_BORDER_RIGHT_COLOR, SK_PROP_BORDER_BOTTOM_COLOR,
       SK_PROP_BORDER_LEFT_COLOR, SK_PROP_BORDER_IMAGE_SOURCE, SK_PROP_BORDER_IMAGE_SLICE,
       SK_PROP_BORDER_IMAGE_WIDTH, SK_PROP_BORDER_IMAGE_OUTSET, SK_PROP_BORDER_IMAGE_REPEAT),
    SH("border-radius", SK_PROP_BORDER_TOP_LEFT_RADIUS, SK_PROP_BORDER_TOP_RIGHT_RADIUS,
       SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS, SK_PROP_BORDER_BOTTOM_LEFT_RADIUS),
    SH("border-image", SK_PROP_BORDER_IMAGE_SOURCE, SK_PROP_BORDER_IMAGE_SLICE, SK_PROP_BORDER_IMAGE_WIDTH,
       SK_PROP_BORDER_IMAGE_OUTSET, SK_PROP_BORDER_IMAGE_REPEAT),
    SH("outline", SK_PROP_OUTLINE_WIDTH, SK_PROP_OUTLINE_STYLE, SK_PROP_OUTLINE_COLOR),
    SH("overflow", SK_PROP_OVERFLOW_X, SK_PROP_OVERFLOW_Y),
    SH("flex", SK_PROP_FLEX_GROW, SK_PROP_FLEX_SHRINK, SK_PROP_FLEX_BASIS),
    SH("flex-flow", SK_PROP_FLEX_DIRECTION, SK_PROP_FLEX_WRAP),
    SH("gap", SK_PROP_ROW_GAP, SK_PROP_COLUMN_GAP),
    SH("grid-column", SK_PROP_GRID_COLUMN_START, SK_PROP_GRID_COLUMN_END),
    SH("grid-row", SK_PROP_GRID_ROW_START, SK_PROP_GRID_ROW_END),
    SH("background", SK_PROP_BACKGROUND_COLOR, SK_PROP_BACKGROUND_IMAGE, SK_PROP_BACKGROUND_POSITION,
       SK_PROP_BACKGROUND_SIZE, SK_PROP_BACKGROUND_REPEAT, SK_PROP_BACKGROUND_ORIGIN, SK_PROP_BACKGROUND_CLIP),
    SH("font", SK_PROP_FONT_STYLE, SK_PROP_FONT_WEIGHT, SK_PROP_FONT_SIZE, SK_PROP_LINE_HEIGHT,
       SK_PROP_FONT_FAMILY),
    SH("text-decoration", SK_PROP_TEXT_DECORATION_LINE, SK_PROP_TEXT_DECORATION_COLOR),
    SH("animation", SK_PROP_ANIMATION_NAME, SK_PROP_ANIMATION_DURATION, SK_PROP_ANIMATION_TIMING_FUNCTION,
       SK_PROP_ANIMATION_DELAY, SK_PROP_ANIMATION_ITERATION_COUNT, SK_PROP_ANIMATION_DIRECTION,
       SK_PROP_ANIMATION_FILL_MODE, SK_PROP_ANIMATION_PLAY_STATE),
    SH("transition", SK_PROP_TRANSITION_PROPERTY, SK_PROP_TRANSITION_DURATION, SK_PROP_TRANSITION_TIMING_FUNCTION,
       SK_PROP_TRANSITION_DELAY),
};

const sk_propdef *sk_prop_find(sk_str name)
{
    for (size_t i = 0; i < SK_LEN(PROPS); i++)
        if (sk_ident_is(name, PROPS[i].name))
            return &PROPS[i];
    return NULL;
}

const sk_propdef *sk_prop_def(uint16_t id)
{
    for (size_t i = 0; i < SK_LEN(PROPS); i++)
        if (PROPS[i].id == id)
            return &PROPS[i];
    return NULL;
}

const sk_shorthand *sk_shorthand_find(sk_str name)
{
    for (size_t i = 0; i < SK_LEN(SHORTHANDS); i++)
        if (sk_ident_is(name, SHORTHANDS[i].name))
            return &SHORTHANDS[i];
    return NULL;
}

size_t sk_prop_count(void)
{
    return SK_LEN(PROPS);
}

/* ---- Fields ---- */

typedef struct {
    uint16_t off, len;
} span;

#define F(m) {(uint16_t)offsetof(sk_css_style, m), (uint16_t)sizeof(((sk_css_style *)0)->m)}

/* The fields a longhand fills: up to two spans of sk_css_style. */
static int fields(uint16_t id, span out[2])
{
    span a = {0, 0}, b = {0, 0};
    switch (id) {
#define ONE(p, m)                                                            \
    case p: {                                                                \
        span s_ = F(m);                                                      \
        a = s_;                                                              \
        break;                                                               \
    }
#define TWO(p, m1, m2)                                                       \
    case p: {                                                                \
        span s1_ = F(m1), s2_ = F(m2);                                       \
        a = s1_;                                                             \
        b = s2_;                                                             \
        break;                                                               \
    }
        ONE(SK_PROP_DISPLAY, display)
        ONE(SK_PROP_POSITION, position)
        ONE(SK_PROP_FLOAT, float_)
        ONE(SK_PROP_BOX_SIZING, box_sizing)
        ONE(SK_PROP_TOP, inset[SK_TOP])
        ONE(SK_PROP_RIGHT, inset[SK_RIGHT])
        ONE(SK_PROP_BOTTOM, inset[SK_BOTTOM])
        ONE(SK_PROP_LEFT, inset[SK_LEFT])
        ONE(SK_PROP_WIDTH, width)
        ONE(SK_PROP_HEIGHT, height)
        ONE(SK_PROP_MIN_WIDTH, min_width)
        ONE(SK_PROP_MIN_HEIGHT, min_height)
        ONE(SK_PROP_MAX_WIDTH, max_width)
        ONE(SK_PROP_MAX_HEIGHT, max_height)
        ONE(SK_PROP_MARGIN_TOP, margin[SK_TOP])
        ONE(SK_PROP_MARGIN_RIGHT, margin[SK_RIGHT])
        ONE(SK_PROP_MARGIN_BOTTOM, margin[SK_BOTTOM])
        ONE(SK_PROP_MARGIN_LEFT, margin[SK_LEFT])
        ONE(SK_PROP_PADDING_TOP, padding[SK_TOP])
        ONE(SK_PROP_PADDING_RIGHT, padding[SK_RIGHT])
        ONE(SK_PROP_PADDING_BOTTOM, padding[SK_BOTTOM])
        ONE(SK_PROP_PADDING_LEFT, padding[SK_LEFT])
        ONE(SK_PROP_BORDER_TOP_WIDTH, border_width[SK_TOP])
        ONE(SK_PROP_BORDER_RIGHT_WIDTH, border_width[SK_RIGHT])
        ONE(SK_PROP_BORDER_BOTTOM_WIDTH, border_width[SK_BOTTOM])
        ONE(SK_PROP_BORDER_LEFT_WIDTH, border_width[SK_LEFT])
        ONE(SK_PROP_BORDER_TOP_STYLE, border_style[SK_TOP])
        ONE(SK_PROP_BORDER_RIGHT_STYLE, border_style[SK_RIGHT])
        ONE(SK_PROP_BORDER_BOTTOM_STYLE, border_style[SK_BOTTOM])
        ONE(SK_PROP_BORDER_LEFT_STYLE, border_style[SK_LEFT])
        ONE(SK_PROP_BORDER_TOP_COLOR, border_color[SK_TOP])
        ONE(SK_PROP_BORDER_RIGHT_COLOR, border_color[SK_RIGHT])
        ONE(SK_PROP_BORDER_BOTTOM_COLOR, border_color[SK_BOTTOM])
        ONE(SK_PROP_BORDER_LEFT_COLOR, border_color[SK_LEFT])
        ONE(SK_PROP_BORDER_TOP_LEFT_RADIUS, radius[SK_TOP_LEFT])
        ONE(SK_PROP_BORDER_TOP_RIGHT_RADIUS, radius[SK_TOP_RIGHT])
        ONE(SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS, radius[SK_BOTTOM_RIGHT])
        ONE(SK_PROP_BORDER_BOTTOM_LEFT_RADIUS, radius[SK_BOTTOM_LEFT])
        ONE(SK_PROP_BORDER_IMAGE_SOURCE, bi_source)
        TWO(SK_PROP_BORDER_IMAGE_SLICE, bi_slice, bi_slice_pct)
        TWO(SK_PROP_BORDER_IMAGE_WIDTH, bi_width, bi_width_num)
        ONE(SK_PROP_BORDER_IMAGE_OUTSET, bi_outset)
        ONE(SK_PROP_BORDER_IMAGE_REPEAT, bi_repeat)
        ONE(SK_PROP_OUTLINE_WIDTH, outline_width)
        ONE(SK_PROP_OUTLINE_STYLE, outline_style)
        ONE(SK_PROP_OUTLINE_COLOR, outline_color)
        ONE(SK_PROP_OUTLINE_OFFSET, outline_offset)
        ONE(SK_PROP_OVERFLOW_X, overflow_x)
        ONE(SK_PROP_OVERFLOW_Y, overflow_y)
        TWO(SK_PROP_Z_INDEX, z_auto, z_index)
        ONE(SK_PROP_VISIBILITY, visibility)
        ONE(SK_PROP_OPACITY, opacity)
        ONE(SK_PROP_POINTER_EVENTS, pointer_events)
        ONE(SK_PROP_CURSOR, cursor)
        ONE(SK_PROP_MIX_BLEND_MODE, mix_blend)
        ONE(SK_PROP_ISOLATION, isolate)
        ONE(SK_PROP_FLEX_DIRECTION, flex_direction)
        ONE(SK_PROP_FLEX_GROW, flex_grow)
        ONE(SK_PROP_FLEX_SHRINK, flex_shrink)
        ONE(SK_PROP_FLEX_BASIS, flex_basis)
        ONE(SK_PROP_JUSTIFY_CONTENT, justify_content)
        ONE(SK_PROP_ALIGN_ITEMS, align_items)
        ONE(SK_PROP_ALIGN_SELF, align_self)
        ONE(SK_PROP_ROW_GAP, row_gap)
        ONE(SK_PROP_COLUMN_GAP, column_gap)
        TWO(SK_PROP_GRID_TEMPLATE_COLUMNS, grid_columns, ngrid_columns)
        TWO(SK_PROP_GRID_TEMPLATE_ROWS, grid_rows, ngrid_rows)
        ONE(SK_PROP_GRID_AUTO_ROWS, grid_auto_rows)
        ONE(SK_PROP_GRID_AUTO_COLUMNS, grid_auto_columns)
        ONE(SK_PROP_GRID_COLUMN_START, grid_column[0])
        ONE(SK_PROP_GRID_COLUMN_END, grid_column[1])
        ONE(SK_PROP_GRID_ROW_START, grid_row[0])
        ONE(SK_PROP_GRID_ROW_END, grid_row[1])
        TWO(SK_PROP_TRANSFORM, transform, ntransform)
        ONE(SK_PROP_TRANSFORM_ORIGIN, origin)
        TWO(SK_PROP_FILTER, filter, nfilter)
        ONE(SK_PROP_IMAGE_RENDERING, image_rendering)
        ONE(SK_PROP_BACKGROUND_COLOR, bg_color)
        TWO(SK_PROP_BACKGROUND_IMAGE, bg_image, nbg_image)
        TWO(SK_PROP_BACKGROUND_POSITION, bg_position, nbg_position)
        TWO(SK_PROP_BACKGROUND_SIZE, bg_size, nbg_size)
        TWO(SK_PROP_BACKGROUND_REPEAT, bg_repeat, nbg_repeat)
        TWO(SK_PROP_BACKGROUND_BLEND_MODE, bg_blend, nbg_blend)
        TWO(SK_PROP_BACKGROUND_ORIGIN, bg_origin, nbg_origin)
        TWO(SK_PROP_BACKGROUND_CLIP, bg_clip, nbg_clip)
        TWO(SK_PROP_BOX_SHADOW, box_shadow, nbox_shadow)
        ONE(SK_PROP_COLOR, color)
        TWO(SK_PROP_FONT_FAMILY, font_family, nfont_family)
        ONE(SK_PROP_FONT_SIZE, font_size)
        ONE(SK_PROP_FONT_WEIGHT, font_weight)
        ONE(SK_PROP_FONT_STYLE, font_style)
        TWO(SK_PROP_LINE_HEIGHT, line_height_kind, line_height)
        ONE(SK_PROP_LETTER_SPACING, letter_spacing)
        ONE(SK_PROP_TEXT_TRANSFORM, text_transform)
        ONE(SK_PROP_TEXT_ALIGN, text_align)
        ONE(SK_PROP_WHITE_SPACE, white_space)
        ONE(SK_PROP_TEXT_OVERFLOW, text_overflow)
        ONE(SK_PROP_TEXT_DECORATION_LINE, text_decoration)
        ONE(SK_PROP_TEXT_DECORATION_COLOR, text_decoration_color)
        TWO(SK_PROP_TEXT_SHADOW, text_shadow, ntext_shadow)
        ONE(SK_PROP_TEXT_ANCHOR, text_anchor)
        TWO(SK_PROP_COUNTER_RESET, counter_reset, ncounter_reset)
        TWO(SK_PROP_COUNTER_INCREMENT, counter_increment, ncounter_increment)
        ONE(SK_PROP_STROKE_WIDTH, stroke_width)
        TWO(SK_PROP_STROKE_DASHARRAY, dasharray, ndasharray)
        ONE(SK_PROP_STROKE_LINECAP, linecap)
        ONE(SK_PROP_STROKE_LINEJOIN, linejoin)
        TWO(SK_PROP_ANIMATION_NAME, anim_name, nanim_name)
        TWO(SK_PROP_ANIMATION_DURATION, anim_duration, nanim_duration)
        TWO(SK_PROP_ANIMATION_TIMING_FUNCTION, anim_timing, nanim_timing)
        TWO(SK_PROP_ANIMATION_DELAY, anim_delay, nanim_delay)
        TWO(SK_PROP_ANIMATION_ITERATION_COUNT, anim_iterations, nanim_iterations)
        TWO(SK_PROP_ANIMATION_DIRECTION, anim_direction, nanim_direction)
        TWO(SK_PROP_ANIMATION_FILL_MODE, anim_fill, nanim_fill)
        TWO(SK_PROP_ANIMATION_PLAY_STATE, anim_paused, nanim_paused)
        TWO(SK_PROP_TRANSITION_PROPERTY, tr_property, ntr_property)
        TWO(SK_PROP_TRANSITION_DURATION, tr_duration, ntr_duration)
        TWO(SK_PROP_TRANSITION_TIMING_FUNCTION, tr_timing, ntr_timing)
        TWO(SK_PROP_TRANSITION_DELAY, tr_delay, ntr_delay)
#undef ONE
#undef TWO
    case SK_PROP_FLEX_WRAP:
    case SK_PROP_USER_SELECT:
    case SK_PROP_CONTAIN:
        return 0;
    case SK_PROP_CLIP_PATH: {
        span s1 = F(clip), s2 = F(nclip);
        a = s1;
        b = s2;
        out[0] = a;
        out[1] = b;
        /* clip_evenodd travels with it. */
        return 3;
    }
    case SK_PROP_CONTENT: {
        span s1 = F(content), s2 = F(ncontent);
        out[0] = s1;
        out[1] = s2;
        return 4;
    }
    case SK_PROP_FILL: {
        span s1 = F(fill_kind), s2 = F(fill);
        a = s1;
        b = s2;
        break;
    }
    case SK_PROP_STROKE: {
        span s1 = F(stroke_kind), s2 = F(stroke);
        a = s1;
        b = s2;
        break;
    }
    default:
        return 0;
    }
    out[0] = a;
    out[1] = b;
    return b.len ? 2 : 1;
}

void sk_prop_copy(uint16_t id, sk_css_style *dst, const sk_css_style *src)
{
    span f[2];
    int n = fields(id, f);
    for (int i = 0; i < SK_MIN(n, 2); i++)
        memcpy((char *)dst + f[i].off, (const char *)src + f[i].off, f[i].len);
    if (n == 3)
        dst->clip_evenodd = src->clip_evenodd;
    if (n == 4)
        dst->content_none = src->content_none;
}

bool sk_prop_equal(uint16_t id, const sk_css_style *a, const sk_css_style *b)
{
    sk_buf x = {0}, y = {0};
    sk_prop_print(&x, id, a);
    sk_prop_print(&y, id, b);
    bool same = x.len == y.len && (x.len == 0 || memcmp(x.ptr, y.ptr, x.len) == 0);
    sk_buf_free(&x);
    sk_buf_free(&y);
    return same;
}

/* ---- Initial values ---- */

static const sk_css_image NO_IMAGE[1] = {{SK_IMAGE_NONE, 0, NULL}};
static const sk_css_position ZERO_POS[1] = {{{0, 0, SK_LEN_PX}, {0, 0, SK_LEN_PX}, 0, 0}};
static const sk_css_bgsize AUTO_SIZE[1] = {{SK_BGSIZE_EXPLICIT, {0, 0, SK_LEN_AUTO}, {0, 0, SK_LEN_AUTO}}};
static const uint8_t REPEAT_BOTH[2] = {SK_REPEAT, SK_REPEAT};
static const uint8_t ZERO_U8[1] = {0};
static const uint8_t PADDING_BOX[1] = {SK_BOX_PADDING};
static const uint8_t BORDER_BOX[1] = {SK_BOX_BORDER};
static const uint32_t NO_ANIM[1] = {SK_PACK_NONE};
static const float ZERO_F[1] = {0};
static const float ONE_F[1] = {1};
static const sk_css_timing EASE[1] = {{SK_TIMING_CUBIC, 0, 0, .25f, .1f, .25f, 1}};
static const uint16_t ALL_PROPS[1] = {SK_PROP_ALL};
static const char *const DEFAULT_FAMILY[1] = {"DejaVu Sans"};

void sk_style_initial(sk_css_style *s, sk_css_color current)
{
    memset(s, 0, sizeof *s);
    s->display = SK_DISPLAY_INLINE;
    s->z_auto = true;
    s->opacity = 1;
    sk_css_len zero = {0, 0, SK_LEN_PX}, autol = {0, 0, SK_LEN_AUTO};
    for (int i = 0; i < 4; i++) {
        s->inset[i] = autol;
        s->margin[i] = zero;
        s->padding[i] = zero;
        s->border_width[i] = 3;
        s->border_color[i] = current;
        s->radius[i][0] = s->radius[i][1] = zero;
        s->bi_slice[i] = 100;
        s->bi_width[i] = (sk_css_len){1, 0, SK_LEN_PX};
    }
    s->bi_slice_pct = 15;
    s->bi_width_num = 15;
    s->width = s->height = s->min_width = s->min_height = autol;
    s->max_width = s->max_height = (sk_css_len){0, 0, SK_LEN_NONE};
    s->outline_width = 3;
    s->outline_color = current;
    s->flex_shrink = 1;
    s->flex_basis = autol;
    s->align_self = SK_ALIGN_AUTO;
    s->row_gap = s->column_gap = (sk_css_len){0, 0, SK_LEN_NORMAL};
    s->grid_auto_rows.kind = s->grid_auto_columns.kind = SK_TRACK_AUTO;
    s->origin[0] = s->origin[1] = (sk_css_len){0, 50, SK_LEN_PX};
    s->bg_image = NO_IMAGE;
    s->nbg_image = 1;
    s->bg_position = ZERO_POS;
    s->nbg_position = 1;
    s->bg_size = AUTO_SIZE;
    s->nbg_size = 1;
    s->bg_repeat = REPEAT_BOTH;
    s->nbg_repeat = 1;
    s->bg_blend = ZERO_U8;
    s->nbg_blend = 1;
    s->bg_origin = PADDING_BOX;
    s->nbg_origin = 1;
    s->bg_clip = BORDER_BOX;
    s->nbg_clip = 1;
    s->color = (sk_css_color){0, 0, 0, 1};
    s->font_family = DEFAULT_FAMILY;
    s->nfont_family = 1;
    s->font_size = 16;
    s->font_weight = 400;
    s->letter_spacing = 0;
    s->text_decoration_color = current;
    s->fill_kind = SK_PAINT_COLOR;
    s->fill = (sk_css_color){0, 0, 0, 1};
    s->stroke_width = (sk_css_len){1, 0, SK_LEN_PX};
    s->anim_name = NO_ANIM;
    s->nanim_name = 1;
    s->anim_duration = ZERO_F;
    s->nanim_duration = 1;
    s->anim_delay = ZERO_F;
    s->nanim_delay = 1;
    s->anim_iterations = ONE_F;
    s->nanim_iterations = 1;
    s->anim_timing = EASE;
    s->nanim_timing = 1;
    s->anim_direction = ZERO_U8;
    s->nanim_direction = 1;
    s->anim_fill = ZERO_U8;
    s->nanim_fill = 1;
    s->anim_paused = ZERO_U8;
    s->nanim_paused = 1;
    s->tr_property = ALL_PROPS;
    s->ntr_property = 1;
    s->tr_duration = ZERO_F;
    s->ntr_duration = 1;
    s->tr_delay = ZERO_F;
    s->ntr_delay = 1;
    s->tr_timing = EASE;
    s->ntr_timing = 1;
}

void sk_style_finish(sk_css_style *s)
{
    for (int i = 0; i < 4; i++)
        if (s->border_style[i] == SK_BORDER_NONE || s->border_style[i] == SK_BORDER_HIDDEN)
            s->border_width[i] = 0;
    if (s->outline_style == SK_BORDER_NONE)
        s->outline_width = 0;
}

/* ---- Keywords ---- */

typedef struct {
    const char *name;
    uint8_t value;
} kw;

static const kw K_DISPLAY[] = {{"none", SK_DISPLAY_NONE},
                               {"block", SK_DISPLAY_BLOCK},
                               {"inline", SK_DISPLAY_INLINE},
                               {"inline-block", SK_DISPLAY_INLINE_BLOCK},
                               {"flex", SK_DISPLAY_FLEX},
                               {"grid", SK_DISPLAY_GRID},
                               {NULL, 0}};
static const kw K_POSITION[] = {{"static", SK_POS_STATIC},
                                {"relative", SK_POS_RELATIVE},
                                {"absolute", SK_POS_ABSOLUTE},
                                {"fixed", SK_POS_FIXED},
                                {NULL, 0}};
static const kw K_FLOAT[] = {{"none", SK_FLOAT_NONE}, {"left", SK_FLOAT_LEFT}, {"right", SK_FLOAT_RIGHT}, {NULL, 0}};
static const kw K_BOX_SIZING[] = {{"content-box", 0}, {"border-box", 1}, {NULL, 0}};
static const kw K_BORDER_STYLE[] = {{"none", SK_BORDER_NONE}, {"hidden", SK_BORDER_HIDDEN}, {"solid", SK_BORDER_SOLID},
                                    {NULL, 0}};
static const kw K_OUTLINE_STYLE[] = {{"none", SK_BORDER_NONE}, {"solid", SK_BORDER_SOLID}, {"auto", SK_BORDER_SOLID},
                                     {NULL, 0}};
static const kw K_OVERFLOW[] = {{"visible", SK_OVERFLOW_VISIBLE},
                                {"hidden", SK_OVERFLOW_HIDDEN},
                                {"clip", SK_OVERFLOW_CLIP},
                                {NULL, 0}};
static const kw K_VISIBILITY[] = {{"visible", SK_VIS_VISIBLE}, {"hidden", SK_VIS_HIDDEN}, {"collapse", SK_VIS_COLLAPSE},
                                  {NULL, 0}};
static const kw K_POINTER[] = {{"auto", SK_PE_AUTO}, {"none", SK_PE_NONE}, {NULL, 0}};
static const kw K_CURSOR[] = {{"auto", SK_CURSOR_AUTO},
                              {"default", SK_CURSOR_DEFAULT},
                              {"pointer", SK_CURSOR_POINTER},
                              {"text", SK_CURSOR_TEXT},
                              {NULL, 0}};
static const kw K_BLEND[] = {{"normal", SK_MIX_NORMAL},     {"multiply", SK_MIX_MULTIPLY},
                             {"screen", SK_MIX_SCREEN},     {"overlay", SK_MIX_OVERLAY},
                             {"darken", SK_MIX_DARKEN},     {"lighten", SK_MIX_LIGHTEN},
                             {"plus-lighter", SK_MIX_PLUS_LIGHTER}, {NULL, 0}};
static const kw K_FLEX_DIR[] = {{"row", SK_FLEX_ROW},
                                {"row-reverse", SK_FLEX_ROW_REVERSE},
                                {"column", SK_FLEX_COLUMN},
                                {"column-reverse", SK_FLEX_COLUMN_REVERSE},
                                {NULL, 0}};
static const kw K_JUSTIFY[] = {{"normal", SK_ALIGN_NORMAL},
                               {"flex-start", SK_ALIGN_FLEX_START},
                               {"flex-end", SK_ALIGN_FLEX_END},
                               {"start", SK_ALIGN_START},
                               {"end", SK_ALIGN_END},
                               {"center", SK_ALIGN_CENTER},
                               {"space-between", SK_ALIGN_SPACE_BETWEEN},
                               {"space-around", SK_ALIGN_SPACE_AROUND},
                               {"space-evenly", SK_ALIGN_SPACE_EVENLY},
                               {"stretch", SK_ALIGN_STRETCH},
                               {NULL, 0}};
static const kw K_ALIGN[] = {{"normal", SK_ALIGN_NORMAL},
                             {"stretch", SK_ALIGN_STRETCH},
                             {"flex-start", SK_ALIGN_FLEX_START},
                             {"flex-end", SK_ALIGN_FLEX_END},
                             {"start", SK_ALIGN_START},
                             {"end", SK_ALIGN_END},
                             {"center", SK_ALIGN_CENTER},
                             {"baseline", SK_ALIGN_BASELINE},
                             {NULL, 0}};
static const kw K_ALIGN_SELF[] = {{"auto", SK_ALIGN_AUTO},
                                  {"normal", SK_ALIGN_NORMAL},
                                  {"stretch", SK_ALIGN_STRETCH},
                                  {"flex-start", SK_ALIGN_FLEX_START},
                                  {"flex-end", SK_ALIGN_FLEX_END},
                                  {"start", SK_ALIGN_START},
                                  {"end", SK_ALIGN_END},
                                  {"center", SK_ALIGN_CENTER},
                                  {"baseline", SK_ALIGN_BASELINE},
                                  {NULL, 0}};
static const kw K_RENDERING[] = {{"auto", SK_RENDER_AUTO},
                                 {"smooth", SK_RENDER_SMOOTH},
                                 {"high-quality", SK_RENDER_SMOOTH},
                                 {"pixelated", SK_RENDER_PIXELATED},
                                 {"crisp-edges", SK_RENDER_PIXELATED},
                                 {NULL, 0}};
static const kw K_TRANSFORM_TEXT[] = {{"none", SK_TT_NONE},
                                      {"uppercase", SK_TT_UPPERCASE},
                                      {"lowercase", SK_TT_LOWERCASE},
                                      {"capitalize", SK_TT_CAPITALIZE},
                                      {NULL, 0}};
static const kw K_TEXT_ALIGN[] = {{"start", SK_TA_START}, {"end", SK_TA_END},       {"left", SK_TA_LEFT},
                                  {"right", SK_TA_RIGHT}, {"center", SK_TA_CENTER}, {NULL, 0}};
static const kw K_WHITE_SPACE[] = {{"normal", SK_WS_NORMAL}, {"nowrap", SK_WS_NOWRAP}, {"pre", SK_WS_PRE}, {NULL, 0}};
static const kw K_TEXT_OVERFLOW[] = {{"clip", SK_TO_CLIP}, {"ellipsis", SK_TO_ELLIPSIS}, {NULL, 0}};
static const kw K_ANCHOR[] = {{"start", SK_ANCHOR_START}, {"middle", SK_ANCHOR_MIDDLE}, {"end", SK_ANCHOR_END},
                              {NULL, 0}};
static const kw K_FONT_STYLE[] = {{"normal", SK_FSTYLE_NORMAL}, {"italic", SK_FSTYLE_ITALIC},
                                  {"oblique", SK_FSTYLE_OBLIQUE}, {NULL, 0}};
static const kw K_LINECAP[] = {{"butt", SK_LINECAP_BUTT}, {"round", SK_LINECAP_ROUND}, {"square", SK_LINECAP_SQUARE},
                               {NULL, 0}};
static const kw K_LINEJOIN[] = {{"miter", SK_LINEJOIN_MITER}, {"round", SK_LINEJOIN_ROUND},
                                {"bevel", SK_LINEJOIN_BEVEL}, {NULL, 0}};
static const kw K_BI_REPEAT[] = {{"stretch", SK_BI_STRETCH}, {"repeat", SK_BI_REPEAT}, {"round", SK_BI_ROUND},
                                 {"space", SK_BI_SPACE}, {NULL, 0}};
static const kw K_BOX[] = {{"border-box", SK_BOX_BORDER}, {"padding-box", SK_BOX_PADDING},
                           {"content-box", SK_BOX_CONTENT}, {NULL, 0}};
static const kw K_ANIM_DIR[] = {{"normal", SK_DIR_NORMAL},
                                {"reverse", SK_DIR_REVERSE},
                                {"alternate", SK_DIR_ALTERNATE},
                                {"alternate-reverse", SK_DIR_ALTERNATE_REVERSE},
                                {NULL, 0}};
static const kw K_ANIM_FILL[] = {{"none", SK_FILL_MODE_NONE},
                                 {"forwards", SK_FILL_MODE_FORWARDS},
                                 {"backwards", SK_FILL_MODE_BACKWARDS},
                                 {"both", SK_FILL_MODE_BOTH},
                                 {NULL, 0}};
static const kw K_PLAY[] = {{"running", 0}, {"paused", 1}, {NULL, 0}};
static const kw K_USER_SELECT[] = {{"auto", 0}, {"none", 0}, {"text", 0}, {"all", 0}, {"contain", 0}, {NULL, 0}};

static bool kw_of(const kw *table, const sk_cv *v, uint8_t *out)
{
    for (const kw *k = table; k->name; k++)
        if (sk_v_ident(v, k->name)) {
            *out = k->value;
            return true;
        }
    return false;
}

static const char *kw_name(const kw *table, uint8_t value)
{
    for (const kw *k = table; k->name; k++)
        if (k->value == value)
            return k->name;
    return "?";
}

/* One keyword and nothing after it. */
static bool one_kw(sk_vcur *v, const kw *table, uint8_t *out)
{
    return kw_of(table, sk_vcur_next(v), out) && sk_vcur_end(v);
}

/* ---- Helpers ---- */

static bool one_len(sk_vctx *c, sk_vcur *v, unsigned flags, sk_css_len *out)
{
    return sk_v_len(c, sk_vcur_next(v), flags, out) && sk_vcur_end(v);
}

static void *keep(sk_vctx *c, const void *p, size_t count, size_t size)
{
    if (!count)
        return NULL;
    void *q = sk_arena_array(c->arena, count, size);
    memcpy(q, p, count * size);
    return q;
}

/* thin, medium, thick or a length: a line's width. */
static bool line_width(sk_vctx *c, const sk_cv *x, float *out)
{
    if (sk_v_ident(x, "thin"))
        *out = 1;
    else if (sk_v_ident(x, "medium"))
        *out = 3;
    else if (sk_v_ident(x, "thick"))
        *out = 5;
    else {
        sk_css_len l;
        if (!sk_v_len(c, x, 0, &l))
            return false;
        *out = l.px;
    }
    return true;
}

static bool border_style(sk_vctx *c, const sk_cv *x, uint8_t *out)
{
    if (kw_of(K_BORDER_STYLE, x, out))
        return true;
    static const char *const OTHER[] = {"dotted", "dashed", "double", "groove", "ridge", "inset", "outset"};
    for (size_t i = 0; i < SK_LEN(OTHER); i++)
        if (sk_v_ident(x, OTHER[i]))
            c->why = "only solid lines are supported";
    return false;
}

/* Which of n values (1 to 4) side i takes: top, right, bottom, left. */
static int side_of(int i, int n)
{
    static const int MAP[4][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
    return MAP[n - 1][i];
}

/* 1 to 4 values for the four sides, as margin and padding take them. */
static bool sides(sk_vctx *c, sk_vcur *v, sk_css_len out[4], unsigned flags)
{
    sk_css_len l[4];
    int n = 0;
    while (!sk_vcur_end(v)) {
        if (n == 4 || !sk_v_len(c, sk_vcur_next(v), flags, &l[n]))
            return false;
        n++;
    }
    if (!n)
        return false;
    out[SK_TOP] = l[0];
    out[SK_RIGHT] = n > 1 ? l[1] : l[0];
    out[SK_BOTTOM] = n > 2 ? l[2] : l[0];
    out[SK_LEFT] = n > 3 ? l[3] : out[SK_RIGHT];
    return true;
}

/* A list of items separated by commas, each parsed by one(), into an
 * array of size-byte items. */
typedef bool (*item_fn)(sk_vctx *c, sk_vcur *v, void *out);

static bool comma_list(sk_vctx *c, sk_vcur *v, item_fn one, size_t size, const void **out, uint16_t *n)
{
    SK_VEC(unsigned char) bytes = {0};
    size_t count = 0;
    unsigned char item[256];
    while (!sk_vcur_end(v)) {
        sk_vcur it = sk_vcur_item(v);
        if (sk_vcur_end(&it) || !one(c, &it, item) || !sk_vcur_end(&it) || count >= SK_PACK_MAX_LIST) {
            sk_vec_free(&bytes);
            return false;
        }
        for (size_t k = 0; k < size; k++)
            sk_vec_push(&bytes, item[k]);
        count++;
    }
    if (!count) {
        sk_vec_free(&bytes);
        return false;
    }
    *out = keep(c, bytes.ptr, count, size);
    *n = (uint16_t)count;
    sk_vec_free(&bytes);
    return true;
}

/* ---- List items ---- */

static bool item_image(sk_vctx *c, sk_vcur *v, void *out)
{
    return sk_v_image(c, sk_vcur_next(v), true, out);
}

static bool item_position(sk_vctx *c, sk_vcur *v, void *out)
{
    return sk_v_position(c, v, out);
}

static bool bgsize(sk_vctx *c, sk_vcur *v, sk_css_bgsize *out)
{
    memset(out, 0, sizeof *out);
    const sk_cv *x = sk_vcur_next(v);
    if (sk_v_ident(x, "cover") || sk_v_ident(x, "contain")) {
        out->kind = sk_v_ident(x, "cover") ? SK_BGSIZE_COVER : SK_BGSIZE_CONTAIN;
        return true;
    }
    if (!sk_v_len(c, x, SK_L_PCT | SK_L_AUTO, &out->w))
        return false;
    out->h.kind = SK_LEN_AUTO;
    const sk_cv *y = sk_vcur_peek(v);
    sk_css_len h;
    if (y && sk_v_len(c, y, SK_L_PCT | SK_L_AUTO, &h)) {
        sk_vcur_next(v);
        out->h = h;
    }
    return true;
}

static bool item_size(sk_vctx *c, sk_vcur *v, void *out)
{
    return bgsize(c, v, out);
}

static bool repeat_style(sk_vcur *v, uint8_t out[2])
{
    const sk_cv *x = sk_vcur_peek(v);
    if (sk_v_ident(x, "repeat-x")) {
        sk_vcur_next(v);
        out[0] = SK_REPEAT;
        out[1] = SK_NO_REPEAT;
        return true;
    }
    if (sk_v_ident(x, "repeat-y")) {
        sk_vcur_next(v);
        out[0] = SK_NO_REPEAT;
        out[1] = SK_REPEAT;
        return true;
    }
    static const kw R[] = {{"repeat", SK_REPEAT},
                           {"no-repeat", SK_NO_REPEAT},
                           {"space", SK_REPEAT_SPACE},
                           {"round", SK_REPEAT_ROUND},
                           {NULL, 0}};
    if (!kw_of(R, x, &out[0]))
        return false;
    sk_vcur_next(v);
    out[1] = out[0];
    if (kw_of(R, sk_vcur_peek(v), &out[1]))
        sk_vcur_next(v);
    return true;
}

static bool item_repeat(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return repeat_style(v, out);
}

static bool item_blend(sk_vctx *c, sk_vcur *v, void *out)
{
    const sk_cv *x = sk_vcur_next(v);
    if (kw_of(K_BLEND, x, out))
        return true;
    if (x && x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_IDENT)
        c->why = "the blend modes are normal, multiply, screen, overlay, darken, lighten and plus-lighter";
    return false;
}

static bool item_box(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return kw_of(K_BOX, sk_vcur_next(v), out);
}

static bool item_shadow_box(sk_vctx *c, sk_vcur *v, void *out)
{
    return sk_v_shadow(c, v, true, out);
}

static bool item_shadow_text(sk_vctx *c, sk_vcur *v, void *out)
{
    return sk_v_shadow(c, v, false, out);
}

static bool item_time(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return sk_v_time(sk_vcur_next(v), out);
}

static bool item_duration(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return sk_v_time(sk_vcur_next(v), out) && *(float *)out >= 0;
}

static bool item_timing(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return sk_v_timing(sk_vcur_next(v), out);
}

static bool iterations(const sk_cv *x, float *out)
{
    if (sk_v_ident(x, "infinite")) {
        *out = INFINITY;
        return true;
    }
    return sk_v_number(x, out) && *out >= 0;
}

static bool item_iterations(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return iterations(sk_vcur_next(v), out);
}

static bool item_dir(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return kw_of(K_ANIM_DIR, sk_vcur_next(v), out);
}

static bool item_fill(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return kw_of(K_ANIM_FILL, sk_vcur_next(v), out);
}

static bool item_play(sk_vctx *c, sk_vcur *v, void *out)
{
    (void)c;
    return kw_of(K_PLAY, sk_vcur_next(v), out);
}

/* An animation's name: none, or an @keyframes rule's. */
static bool anim_name(sk_vctx *c, const sk_cv *x, uint32_t *out)
{
    if (sk_v_ident(x, "none")) {
        *out = SK_PACK_NONE;
        return true;
    }
    if (!x || x->kind != SK_CV_TOKEN || (x->tok.type != SK_TOK_IDENT && x->tok.type != SK_TOK_STRING))
        return false;
    if (!c->keyframes || !c->keyframes(c->data, x->tok.text, out)) {
        c->why = "no @keyframes has that name";
        return false;
    }
    return true;
}

static bool item_anim_name(sk_vctx *c, sk_vcur *v, void *out)
{
    return anim_name(c, sk_vcur_next(v), out);
}

static bool tr_prop(sk_vctx *c, const sk_cv *x, uint16_t *out)
{
    if (sk_v_ident(x, "all")) {
        *out = SK_PROP_ALL;
        return true;
    }
    if (sk_v_ident(x, "none")) {
        *out = 0;
        return true;
    }
    if (!x || x->kind != SK_CV_TOKEN || x->tok.type != SK_TOK_IDENT)
        return false;
    const sk_propdef *p = sk_prop_find(x->tok.text);
    if (!p) {
        c->why = "a transition names a longhand of the subset, or all";
        return false;
    }
    *out = p->id;
    return true;
}

static bool item_tr_prop(sk_vctx *c, sk_vcur *v, void *out)
{
    return tr_prop(c, sk_vcur_next(v), out);
}

static bool item_dash(sk_vctx *c, sk_vcur *v, void *out)
{
    return sk_v_len(c, sk_vcur_next(v), SK_L_PCT | SK_L_NUMBER, out) && ((sk_css_len *)out)->px >= 0 &&
           ((sk_css_len *)out)->pct >= 0;
}

/* ---- Longhands with grammars of their own ---- */

static bool filters(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    s->filter = NULL;
    s->nfilter = 0;
    if (sk_v_ident(sk_vcur_peek(v), "none")) {
        sk_vcur_next(v);
        return sk_vcur_end(v);
    }
    SK_VEC(sk_css_filter) list = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        const sk_cv *f = sk_vcur_next(v);
        sk_css_filter x;
        memset(&x, 0, sizeof x);
        if (!f || f->kind != SK_CV_FUNCTION || list.len >= SK_PACK_MAX_LIST) {
            ok = false;
            break;
        }
        sk_vcur in;
        sk_vcur_init(&in, f->kids, f->n);
        sk_str name = f->tok.text;
        static const struct {
            const char *name;
            uint8_t kind;
        } FACTOR[] = {{"brightness", SK_FX_BRIGHTNESS}, {"contrast", SK_FX_CONTRAST}, {"saturate", SK_FX_SATURATE},
                      {"grayscale", SK_FX_GRAYSCALE},   {"sepia", SK_FX_SEPIA},       {"invert", SK_FX_INVERT},
                      {"opacity", SK_FX_OPACITY}};
        bool known = false;
        for (size_t i = 0; i < SK_LEN(FACTOR); i++)
            if (sk_ident_is(name, FACTOR[i].name)) {
                known = true;
                x.kind = FACTOR[i].kind;
                ok = sk_v_factor(sk_vcur_next(&in), &x.amount) && x.amount >= 0 && sk_vcur_end(&in);
            }
        if (!known) {
            if (sk_ident_is(name, "blur")) {
                sk_css_len l;
                x.kind = SK_FX_BLUR;
                ok = sk_v_len(c, sk_vcur_next(&in), 0, &l) && sk_vcur_end(&in);
                x.amount = l.px;
            } else if (sk_ident_is(name, "hue-rotate")) {
                x.kind = SK_FX_HUE_ROTATE;
                ok = sk_v_angle(sk_vcur_next(&in), true, &x.amount) && sk_vcur_end(&in);
            } else if (sk_ident_is(name, "drop-shadow")) {
                x.kind = SK_FX_DROP_SHADOW;
                ok = sk_v_shadow(c, &in, false, &x.shadow);
            } else {
                c->why = "the filters are blur, brightness, contrast, saturate, grayscale, sepia, invert, opacity, "
                         "hue-rotate and drop-shadow";
                ok = false;
            }
        }
        if (ok)
            sk_vec_push(&list, x);
    }
    if (ok && list.len) {
        s->nfilter = (uint16_t)list.len;
        s->filter = keep(c, list.ptr, list.len, sizeof *list.ptr);
    }
    sk_vec_free(&list);
    return ok && s->nfilter;
}

static bool clip_path(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    s->clip = NULL;
    s->nclip = 0;
    s->clip_evenodd = 0;
    const sk_cv *f = sk_vcur_next(v);
    if (sk_v_ident(f, "none"))
        return sk_vcur_end(v);
    if (!f || f->kind != SK_CV_FUNCTION || !sk_ident_is(f->tok.text, "polygon") || !sk_vcur_end(v)) {
        c->why = "clip-path takes none or polygon()";
        return false;
    }
    sk_vcur in;
    sk_vcur_init(&in, f->kids, f->n);
    const sk_cv *rule = sk_vcur_peek(&in);
    if (sk_v_ident(rule, "evenodd") || sk_v_ident(rule, "nonzero")) {
        s->clip_evenodd = sk_v_ident(rule, "evenodd");
        sk_vcur_next(&in);
        if (!sk_vcur_comma(&in))
            return false;
    }
    SK_VEC(sk_css_len) pts = {0};
    while (!sk_vcur_end(&in)) {
        sk_vcur it = sk_vcur_item(&in);
        sk_css_len x, y;
        if (!sk_v_len(c, sk_vcur_next(&it), SK_L_PCT | SK_L_NEG, &x) ||
            !sk_v_len(c, sk_vcur_next(&it), SK_L_PCT | SK_L_NEG, &y) || !sk_vcur_end(&it) ||
            pts.len >= 2 * SK_PACK_MAX_LIST) {
            sk_vec_free(&pts);
            return false;
        }
        sk_vec_push(&pts, x);
        sk_vec_push(&pts, y);
    }
    if (pts.len < 6) {
        sk_vec_free(&pts);
        return false;
    }
    s->nclip = (uint16_t)(pts.len / 2);
    s->clip = keep(c, pts.ptr, pts.len, sizeof *pts.ptr);
    sk_vec_free(&pts);
    return true;
}

static bool track(sk_vctx *c, const sk_cv *x, sk_css_track *out)
{
    memset(out, 0, sizeof *out);
    if (sk_v_ident(x, "auto")) {
        out->kind = SK_TRACK_AUTO;
        return true;
    }
    if (x && x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_DIMENSION && sk_ident_is(x->tok.unit, "fr")) {
        if (!(x->tok.num >= 0 && x->tok.num < 1e6))
            return false;
        out->kind = SK_TRACK_FR;
        out->fr = (float)x->tok.num;
        return true;
    }
    out->kind = SK_TRACK_PX;
    return sk_v_len(c, x, SK_L_PCT, &out->len);
}

static bool track_list(sk_vctx *c, sk_vcur *v, const sk_css_track **out, uint16_t *n)
{
    *out = NULL;
    *n = 0;
    if (sk_v_ident(sk_vcur_peek(v), "none")) {
        sk_vcur_next(v);
        return sk_vcur_end(v);
    }
    SK_VEC(sk_css_track) list = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_next(v);
        sk_css_track t;
        if (x->kind == SK_CV_FUNCTION && sk_ident_is(x->tok.text, "repeat")) {
            sk_vcur in;
            sk_vcur_init(&in, x->kids, x->n);
            int32_t times;
            ok = sk_v_integer(sk_vcur_next(&in), &times) && times >= 1 && times <= 64 && sk_vcur_comma(&in);
            SK_VEC(sk_css_track) inner = {0};
            while (ok && !sk_vcur_end(&in)) {
                ok = track(c, sk_vcur_next(&in), &t);
                if (ok)
                    sk_vec_push(&inner, t);
            }
            ok = ok && inner.len;
            for (int32_t k = 0; ok && k < times; k++)
                for (size_t j = 0; j < inner.len; j++)
                    sk_vec_push(&list, inner.ptr[j]);
            sk_vec_free(&inner);
        } else {
            ok = track(c, x, &t);
            if (ok)
                sk_vec_push(&list, t);
        }
        if (list.len > SK_PACK_MAX_LIST)
            ok = false;
    }
    if (ok && list.len) {
        *n = (uint16_t)list.len;
        *out = keep(c, list.ptr, list.len, sizeof *list.ptr);
    }
    sk_vec_free(&list);
    return ok && *n;
}

static bool grid_line(sk_vcur *v, sk_css_line *out)
{
    memset(out, 0, sizeof *out);
    const sk_cv *x = sk_vcur_next(v);
    int32_t n;
    if (sk_v_ident(x, "auto")) {
        out->kind = SK_LINE_AUTO;
    } else if (sk_v_ident(x, "span")) {
        if (!sk_v_integer(sk_vcur_next(v), &n) || n < 1 || n > 1000)
            return false;
        out->kind = SK_LINE_SPAN;
        out->n = (int16_t)n;
    } else if (sk_v_integer(x, &n) && n != 0 && n >= -1000 && n <= 1000) {
        out->kind = SK_LINE_NUMBER;
        out->n = (int16_t)n;
        if (sk_v_ident(sk_vcur_peek(v), "span"))
            return false;
    } else {
        return false;
    }
    return true;
}

static bool origin(sk_vctx *c, sk_vcur *v, sk_css_len out[2])
{
    /* Like a position of one or two values, then an optional zero z. */
    sk_css_len vals[2];
    int kwv[2] = {0, 0}; /* 1 x keyword, 2 y keyword, 3 center */
    int n = 0;
    while (n < 2 && !sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        if (sk_v_ident(x, "left") || sk_v_ident(x, "right")) {
            vals[n] = (sk_css_len){0, sk_v_ident(x, "left") ? 0.0f : 100.0f, SK_LEN_PX};
            kwv[n] = 1;
        } else if (sk_v_ident(x, "top") || sk_v_ident(x, "bottom")) {
            vals[n] = (sk_css_len){0, sk_v_ident(x, "top") ? 0.0f : 100.0f, SK_LEN_PX};
            kwv[n] = 2;
        } else if (sk_v_ident(x, "center")) {
            vals[n] = (sk_css_len){0, 50, SK_LEN_PX};
            kwv[n] = 3;
        } else if (!sk_v_len(c, x, SK_L_PCT | SK_L_NEG, &vals[n])) {
            break;
        }
        sk_vcur_next(v);
        n++;
    }
    if (!n)
        return false;
    out[0] = out[1] = (sk_css_len){0, 50, SK_LEN_PX};
    if (n == 1) {
        if (kwv[0] == 2)
            out[1] = vals[0];
        else
            out[0] = vals[0];
    } else {
        bool swap = kwv[0] == 2 || kwv[1] == 1;
        if (swap && (kwv[0] == 0 || kwv[1] == 0))
            return false;
        out[0] = vals[swap ? 1 : 0];
        out[1] = vals[swap ? 0 : 1];
        if (kwv[swap ? 1 : 0] == 2 || kwv[swap ? 0 : 1] == 1)
            return false;
    }
    if (!sk_vcur_end(v)) {
        sk_css_len z;
        if (!sk_v_len(c, sk_vcur_next(v), SK_L_NEG, &z) || z.px != 0) {
            c->why = "transform-origin's z must be 0: transforms are 2D";
            return false;
        }
    }
    return sk_vcur_end(v);
}

static bool family_list(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    SK_VEC(const char *) names = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        sk_vcur it = sk_vcur_item(v);
        const sk_cv *x = sk_vcur_next(&it);
        sk_buf name = {0};
        if (x && x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_STRING) {
            sk_buf_append_str(&name, x->tok.text);
            ok = sk_vcur_end(&it);
        } else {
            while (ok && x) {
                if (x->kind != SK_CV_TOKEN || x->tok.type != SK_TOK_IDENT) {
                    ok = false;
                    break;
                }
                if (sk_ident_is(x->tok.text, "inherit") || sk_ident_is(x->tok.text, "initial") ||
                    sk_ident_is(x->tok.text, "unset") || sk_ident_is(x->tok.text, "default"))
                    ok = false;
                if (name.len)
                    sk_buf_putc(&name, ' ');
                sk_buf_append_str(&name, x->tok.text);
                x = sk_vcur_next(&it);
            }
        }
        if (ok && (!name.len || names.len >= 64))
            ok = false;
        if (ok)
            sk_vec_push(&names, sk_arena_strndup(c->arena, sk_buf_cstr(&name), name.len));
        sk_buf_free(&name);
    }
    if (ok && names.len) {
        s->nfont_family = (uint16_t)names.len;
        s->font_family = keep(c, names.ptr, names.len, sizeof *names.ptr);
    }
    bool any = names.len > 0;
    sk_vec_free(&names);
    return ok && any;
}

static bool font_size(sk_vctx *c, const sk_cv *x, float *out)
{
    static const struct {
        const char *name;
        float px;
    } ABS[] = {{"xx-small", 9},  {"x-small", 10},  {"small", 13},     {"medium", 16},     {"large", 18},
               {"x-large", 24}, {"xx-large", 32}, {"xxx-large", 48}};
    for (size_t i = 0; i < SK_LEN(ABS); i++)
        if (sk_v_ident(x, ABS[i].name)) {
            *out = ABS[i].px;
            return true;
        }
    if (sk_v_ident(x, "smaller")) {
        *out = c->parent_font_size / 1.2f;
        return true;
    }
    if (sk_v_ident(x, "larger")) {
        *out = c->parent_font_size * 1.2f;
        return true;
    }
    /* em and % of the parent's size. */
    float em = c->font_size;
    c->font_size = c->parent_font_size;
    sk_css_len l;
    bool ok = sk_v_len(c, x, SK_L_PCT, &l);
    c->font_size = em;
    if (!ok || l.kind != SK_LEN_PX)
        return false;
    *out = l.px + l.pct / 100 * c->parent_font_size;
    return *out >= 0;
}

static uint16_t parent_weight(sk_vctx *c)
{
    return c->parent ? c->parent->font_weight : 400;
}

static bool font_weight(sk_vctx *c, const sk_cv *x, uint16_t *out)
{
    float n;
    if (sk_v_ident(x, "normal"))
        *out = 400;
    else if (sk_v_ident(x, "bold"))
        *out = 700;
    else if (sk_v_ident(x, "bolder")) {
        uint16_t p = parent_weight(c);
        *out = p < 350 ? 400 : p < 550 ? 700 : p < 900 ? 900 : p;
    } else if (sk_v_ident(x, "lighter")) {
        uint16_t p = parent_weight(c);
        *out = p < 100 ? p : p < 550 ? 100 : p < 750 ? 400 : 700;
    } else if (sk_v_number(x, &n) && n >= 1 && n <= 1000)
        *out = (uint16_t)lroundf(n);
    else
        return false;
    return true;
}

static bool line_height(sk_vctx *c, const sk_cv *x, sk_css_style *s)
{
    float n;
    if (sk_v_ident(x, "normal")) {
        s->line_height_kind = SK_LH_NORMAL;
        s->line_height = 0;
        return true;
    }
    if (sk_v_number(x, &n)) {
        if (n < 0)
            return false;
        s->line_height_kind = SK_LH_NUMBER;
        s->line_height = n;
        return true;
    }
    sk_css_len l;
    if (!sk_v_len(c, x, SK_L_PCT, &l) || l.kind != SK_LEN_PX)
        return false;
    s->line_height_kind = SK_LH_PX;
    s->line_height = l.px + l.pct / 100 * c->font_size;
    return true;
}

static bool content(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    s->content = NULL;
    s->ncontent = 0;
    s->content_none = false;
    const sk_cv *x = sk_vcur_peek(v);
    if (sk_v_ident(x, "none") || sk_v_ident(x, "normal")) {
        sk_vcur_next(v);
        s->content_none = true;
        return sk_vcur_end(v);
    }
    SK_VEC(sk_css_content) parts = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        x = sk_vcur_next(v);
        sk_css_content p;
        memset(&p, 0, sizeof p);
        if (x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_STRING) {
            p.kind = SK_CONTENT_TEXT;
            p.text = sk_arena_strndup(c->arena, x->tok.text.ptr ? x->tok.text.ptr : "", x->tok.text.len);
        } else if (x->kind == SK_CV_FUNCTION && sk_ident_is(x->tok.text, "counter")) {
            sk_vcur in;
            sk_vcur_init(&in, x->kids, x->n);
            const sk_cv *name = sk_vcur_next(&in);
            ok = name && name->kind == SK_CV_TOKEN && name->tok.type == SK_TOK_IDENT && !sk_ident_is(name->tok.text, "none");
            if (ok && sk_vcur_comma(&in)) {
                ok = sk_v_ident(sk_vcur_next(&in), "decimal");
                if (!ok)
                    c->why = "counters are drawn as decimal numbers";
            }
            ok = ok && sk_vcur_end(&in);
            if (ok) {
                p.kind = SK_CONTENT_COUNTER;
                p.text = sk_arena_strndup(c->arena, name->tok.text.ptr, name->tok.text.len);
            }
        } else {
            c->why = "content takes strings and counter()";
            ok = false;
        }
        if (ok && parts.len >= SK_PACK_MAX_LIST)
            ok = false;
        if (ok)
            sk_vec_push(&parts, p);
    }
    if (ok && parts.len) {
        s->ncontent = (uint16_t)parts.len;
        s->content = keep(c, parts.ptr, parts.len, sizeof *parts.ptr);
    }
    sk_vec_free(&parts);
    return ok && s->ncontent;
}

static bool counters(sk_vctx *c, sk_vcur *v, int32_t dflt, const sk_css_counter **out, uint16_t *n)
{
    *out = NULL;
    *n = 0;
    if (sk_v_ident(sk_vcur_peek(v), "none")) {
        sk_vcur_next(v);
        return sk_vcur_end(v);
    }
    SK_VEC(sk_css_counter) list = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_next(v);
        if (!x || x->kind != SK_CV_TOKEN || x->tok.type != SK_TOK_IDENT || sk_ident_is(x->tok.text, "none") ||
            list.len >= SK_PACK_MAX_LIST) {
            ok = false;
            break;
        }
        sk_css_counter k = {sk_arena_strndup(c->arena, x->tok.text.ptr, x->tok.text.len), dflt};
        int32_t val;
        if (sk_v_integer(sk_vcur_peek(v), &val)) {
            sk_vcur_next(v);
            k.value = val;
        }
        sk_vec_push(&list, k);
    }
    if (ok && list.len) {
        *n = (uint16_t)list.len;
        *out = keep(c, list.ptr, list.len, sizeof *list.ptr);
    }
    sk_vec_free(&list);
    return ok && *n;
}

static bool paint(sk_vctx *c, sk_vcur *v, uint8_t *kind, sk_css_color *color)
{
    const sk_cv *x = sk_vcur_next(v);
    if (sk_v_ident(x, "none")) {
        *kind = SK_PAINT_NONE;
        *color = (sk_css_color){0, 0, 0, 0};
        return sk_vcur_end(v);
    }
    *kind = SK_PAINT_COLOR;
    return sk_v_color(c, x, color) && sk_vcur_end(v);
}

static bool border_slice(sk_vcur *v, sk_css_style *s)
{
    float val[4];
    bool pct[4];
    int n = 0;
    bool fill = false;
    while (!sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        if (!fill && sk_v_ident(x, "fill")) {
            fill = true;
            sk_vcur_next(v);
            continue;
        }
        if (n == 4 || fill)
            break;
        if (x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_PERCENTAGE && x->tok.num >= 0 && x->tok.num < 1e6) {
            val[n] = (float)x->tok.num;
            pct[n] = true;
        } else if (sk_v_number(x, &val[n]) && val[n] >= 0) {
            pct[n] = false;
        } else {
            break;
        }
        sk_vcur_next(v);
        n++;
    }
    if (!n)
        return false;
    int idx[4] = {0, n > 1 ? 1 : 0, n > 2 ? 2 : 0, n > 3 ? 3 : (n > 1 ? 1 : 0)};
    s->bi_slice_pct = 0;
    for (int i = 0; i < 4; i++) {
        s->bi_slice[i] = val[idx[i]];
        if (pct[idx[i]])
            s->bi_slice_pct |= (uint8_t)(1 << i);
    }
    s->bi_fill = fill;
    return true;
}

static bool border_widths(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    sk_css_len val[4];
    bool num[4];
    int n = 0;
    while (n < 4 && !sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        float f;
        if (sk_v_number(x, &f) && f >= 0) {
            val[n] = (sk_css_len){f, 0, SK_LEN_PX};
            num[n] = true;
        } else if (sk_v_len(c, x, SK_L_PCT | SK_L_AUTO, &val[n])) {
            num[n] = false;
        } else {
            break;
        }
        sk_vcur_next(v);
        n++;
    }
    if (!n)
        return false;
    int idx[4] = {0, n > 1 ? 1 : 0, n > 2 ? 2 : 0, n > 3 ? 3 : (n > 1 ? 1 : 0)};
    s->bi_width_num = 0;
    for (int i = 0; i < 4; i++) {
        s->bi_width[i] = val[idx[i]];
        if (num[idx[i]])
            s->bi_width_num |= (uint8_t)(1 << i);
    }
    return true;
}

static bool border_outset(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    float val[4];
    int n = 0;
    while (n < 4 && !sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        float f;
        sk_css_len l;
        if (sk_v_number(x, &f) && f == 0) {
            val[n] = 0;
        } else if (sk_v_len(c, x, 0, &l)) {
            val[n] = l.px;
        } else {
            break;
        }
        sk_vcur_next(v);
        n++;
    }
    if (!n)
        return false;
    for (int i = 0; i < 4; i++)
        s->bi_outset[i] = val[side_of(i, n)];
    return true;
}

static bool border_repeat(sk_vcur *v, sk_css_style *s)
{
    if (!kw_of(K_BI_REPEAT, sk_vcur_peek(v), &s->bi_repeat[0]))
        return false;
    sk_vcur_next(v);
    s->bi_repeat[1] = s->bi_repeat[0];
    if (kw_of(K_BI_REPEAT, sk_vcur_peek(v), &s->bi_repeat[1]))
        sk_vcur_next(v);
    return true;
}

static bool radius(sk_vctx *c, sk_vcur *v, sk_css_len out[2])
{
    if (!sk_v_len(c, sk_vcur_next(v), SK_L_PCT, &out[0]))
        return false;
    out[1] = out[0];
    if (!sk_vcur_end(v) && !sk_v_len(c, sk_vcur_next(v), SK_L_PCT, &out[1]))
        return false;
    return sk_vcur_end(v);
}

/* ---- Longhands ---- */

#define LIST(fnc, field, nfield, T)                                                                                    \
    do {                                                                                                               \
        const void *p_;                                                                                                \
        uint16_t n_;                                                                                                   \
        if (!comma_list(c, &v, fnc, sizeof(T), &p_, &n_))                                                              \
            return false;                                                                                              \
        s->field = p_;                                                                                                 \
        s->nfield = n_;                                                                                                \
        return true;                                                                                                   \
    } while (0)

static bool longhand(uint16_t id, sk_vctx *c, sk_vcur v, sk_css_style *s)
{
    uint8_t k;
    switch (id) {
    case SK_PROP_DISPLAY:
        if (!one_kw(&v, K_DISPLAY, &s->display)) {
            c->why = "display takes none, block, inline, inline-block, flex or grid";
            return false;
        }
        return true;
    case SK_PROP_POSITION:
        return one_kw(&v, K_POSITION, &s->position);
    case SK_PROP_FLOAT:
        return one_kw(&v, K_FLOAT, &s->float_);
    case SK_PROP_BOX_SIZING:
        return one_kw(&v, K_BOX_SIZING, &s->box_sizing);
    case SK_PROP_TOP:
    case SK_PROP_RIGHT:
    case SK_PROP_BOTTOM:
    case SK_PROP_LEFT:
        return one_len(c, &v, SK_L_PCT | SK_L_NEG | SK_L_AUTO, &s->inset[id - SK_PROP_TOP]);
    case SK_PROP_WIDTH:
        return one_len(c, &v, SK_L_PCT | SK_L_AUTO, &s->width);
    case SK_PROP_HEIGHT:
        return one_len(c, &v, SK_L_PCT | SK_L_AUTO, &s->height);
    case SK_PROP_MIN_WIDTH:
        return one_len(c, &v, SK_L_PCT | SK_L_AUTO, &s->min_width);
    case SK_PROP_MIN_HEIGHT:
        return one_len(c, &v, SK_L_PCT | SK_L_AUTO, &s->min_height);
    case SK_PROP_MAX_WIDTH:
        return one_len(c, &v, SK_L_PCT | SK_L_NONE, &s->max_width);
    case SK_PROP_MAX_HEIGHT:
        return one_len(c, &v, SK_L_PCT | SK_L_NONE, &s->max_height);
    case SK_PROP_MARGIN_TOP:
    case SK_PROP_MARGIN_RIGHT:
    case SK_PROP_MARGIN_BOTTOM:
    case SK_PROP_MARGIN_LEFT:
        return one_len(c, &v, SK_L_PCT | SK_L_NEG | SK_L_AUTO, &s->margin[id - SK_PROP_MARGIN_TOP]);
    case SK_PROP_PADDING_TOP:
    case SK_PROP_PADDING_RIGHT:
    case SK_PROP_PADDING_BOTTOM:
    case SK_PROP_PADDING_LEFT:
        return one_len(c, &v, SK_L_PCT, &s->padding[id - SK_PROP_PADDING_TOP]);
    case SK_PROP_BORDER_TOP_WIDTH:
    case SK_PROP_BORDER_RIGHT_WIDTH:
    case SK_PROP_BORDER_BOTTOM_WIDTH:
    case SK_PROP_BORDER_LEFT_WIDTH:
        return line_width(c, sk_vcur_next(&v), &s->border_width[id - SK_PROP_BORDER_TOP_WIDTH]) && sk_vcur_end(&v);
    case SK_PROP_BORDER_TOP_STYLE:
    case SK_PROP_BORDER_RIGHT_STYLE:
    case SK_PROP_BORDER_BOTTOM_STYLE:
    case SK_PROP_BORDER_LEFT_STYLE:
        return border_style(c, sk_vcur_next(&v), &s->border_style[id - SK_PROP_BORDER_TOP_STYLE]) && sk_vcur_end(&v);
    case SK_PROP_BORDER_TOP_COLOR:
    case SK_PROP_BORDER_RIGHT_COLOR:
    case SK_PROP_BORDER_BOTTOM_COLOR:
    case SK_PROP_BORDER_LEFT_COLOR:
        return sk_v_color(c, sk_vcur_next(&v), &s->border_color[id - SK_PROP_BORDER_TOP_COLOR]) && sk_vcur_end(&v);
    case SK_PROP_BORDER_TOP_LEFT_RADIUS:
    case SK_PROP_BORDER_TOP_RIGHT_RADIUS:
    case SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS:
    case SK_PROP_BORDER_BOTTOM_LEFT_RADIUS:
        return radius(c, &v, s->radius[id - SK_PROP_BORDER_TOP_LEFT_RADIUS]);
    case SK_PROP_BORDER_IMAGE_SOURCE:
        return sk_v_image(c, sk_vcur_next(&v), true, &s->bi_source) && sk_vcur_end(&v);
    case SK_PROP_BORDER_IMAGE_SLICE:
        return border_slice(&v, s) && sk_vcur_end(&v);
    case SK_PROP_BORDER_IMAGE_WIDTH:
        return border_widths(c, &v, s) && sk_vcur_end(&v);
    case SK_PROP_BORDER_IMAGE_OUTSET:
        return border_outset(c, &v, s) && sk_vcur_end(&v);
    case SK_PROP_BORDER_IMAGE_REPEAT:
        return border_repeat(&v, s) && sk_vcur_end(&v);
    case SK_PROP_OUTLINE_WIDTH:
        return line_width(c, sk_vcur_next(&v), &s->outline_width) && sk_vcur_end(&v);
    case SK_PROP_OUTLINE_STYLE:
        return one_kw(&v, K_OUTLINE_STYLE, &s->outline_style);
    case SK_PROP_OUTLINE_COLOR:
        return sk_v_color(c, sk_vcur_next(&v), &s->outline_color) && sk_vcur_end(&v);
    case SK_PROP_OUTLINE_OFFSET: {
        sk_css_len l;
        if (!one_len(c, &v, SK_L_NEG, &l))
            return false;
        s->outline_offset = l.px;
        return true;
    }
    case SK_PROP_OVERFLOW_X:
    case SK_PROP_OVERFLOW_Y:
        if (!one_kw(&v, K_OVERFLOW, id == SK_PROP_OVERFLOW_X ? &s->overflow_x : &s->overflow_y)) {
            c->why = "overflow takes visible, hidden or clip: a skin does not scroll";
            return false;
        }
        return true;
    case SK_PROP_Z_INDEX: {
        const sk_cv *x = sk_vcur_next(&v);
        if (sk_v_ident(x, "auto")) {
            s->z_auto = true;
            s->z_index = 0;
            return sk_vcur_end(&v);
        }
        s->z_auto = false;
        return sk_v_integer(x, &s->z_index) && sk_vcur_end(&v);
    }
    case SK_PROP_VISIBILITY:
        return one_kw(&v, K_VISIBILITY, &s->visibility);
    case SK_PROP_OPACITY: {
        float f;
        if (!sk_v_factor(sk_vcur_next(&v), &f) || !sk_vcur_end(&v))
            return false;
        s->opacity = f < 0 ? 0 : f > 1 ? 1 : f;
        return true;
    }
    case SK_PROP_POINTER_EVENTS:
        return one_kw(&v, K_POINTER, &s->pointer_events);
    case SK_PROP_CURSOR:
        if (!one_kw(&v, K_CURSOR, &s->cursor)) {
            c->why = "cursor takes auto, default, pointer or text";
            return false;
        }
        return true;
    case SK_PROP_MIX_BLEND_MODE:
        return item_blend(c, &v, &s->mix_blend) && sk_vcur_end(&v);
    case SK_PROP_ISOLATION:
        if (sk_v_ident(sk_vcur_peek(&v), "isolate") || sk_v_ident(sk_vcur_peek(&v), "auto")) {
            s->isolate = sk_v_ident(sk_vcur_next(&v), "isolate");
            return sk_vcur_end(&v);
        }
        return false;
    case SK_PROP_CONTAIN:
        if (sk_v_ident(sk_vcur_next(&v), "none") && sk_vcur_end(&v))
            return true;
        c->why = "contain takes none: containment is not supported";
        return false;
    case SK_PROP_FLEX_DIRECTION:
        return one_kw(&v, K_FLEX_DIR, &s->flex_direction);
    case SK_PROP_FLEX_WRAP:
        if (sk_v_ident(sk_vcur_next(&v), "nowrap") && sk_vcur_end(&v))
            return true;
        c->why = "a flex container does not wrap: flex-wrap takes nowrap";
        return false;
    case SK_PROP_FLEX_GROW:
    case SK_PROP_FLEX_SHRINK: {
        float f;
        if (!sk_v_number(sk_vcur_next(&v), &f) || f < 0 || !sk_vcur_end(&v))
            return false;
        *(id == SK_PROP_FLEX_GROW ? &s->flex_grow : &s->flex_shrink) = f;
        return true;
    }
    case SK_PROP_FLEX_BASIS:
        return one_len(c, &v, SK_L_PCT | SK_L_AUTO, &s->flex_basis);
    case SK_PROP_JUSTIFY_CONTENT:
        return one_kw(&v, K_JUSTIFY, &s->justify_content);
    case SK_PROP_ALIGN_ITEMS:
        return one_kw(&v, K_ALIGN, &s->align_items);
    case SK_PROP_ALIGN_SELF:
        return one_kw(&v, K_ALIGN_SELF, &s->align_self);
    case SK_PROP_ROW_GAP:
        return one_len(c, &v, SK_L_PCT | SK_L_NORMAL, &s->row_gap);
    case SK_PROP_COLUMN_GAP:
        return one_len(c, &v, SK_L_PCT | SK_L_NORMAL, &s->column_gap);
    case SK_PROP_GRID_TEMPLATE_COLUMNS:
        return track_list(c, &v, &s->grid_columns, &s->ngrid_columns);
    case SK_PROP_GRID_TEMPLATE_ROWS:
        return track_list(c, &v, &s->grid_rows, &s->ngrid_rows);
    case SK_PROP_GRID_AUTO_ROWS:
        return track(c, sk_vcur_next(&v), &s->grid_auto_rows) && sk_vcur_end(&v);
    case SK_PROP_GRID_AUTO_COLUMNS:
        return track(c, sk_vcur_next(&v), &s->grid_auto_columns) && sk_vcur_end(&v);
    case SK_PROP_GRID_COLUMN_START:
    case SK_PROP_GRID_COLUMN_END:
        return grid_line(&v, &s->grid_column[id - SK_PROP_GRID_COLUMN_START]) && sk_vcur_end(&v);
    case SK_PROP_GRID_ROW_START:
    case SK_PROP_GRID_ROW_END:
        return grid_line(&v, &s->grid_row[id - SK_PROP_GRID_ROW_START]) && sk_vcur_end(&v);
    case SK_PROP_TRANSFORM: {
        sk_css_xf *x;
        if (!sk_v_transform(c, &v, &x, &s->ntransform))
            return false;
        s->transform = x;
        return true;
    }
    case SK_PROP_TRANSFORM_ORIGIN:
        return origin(c, &v, s->origin);
    case SK_PROP_FILTER:
        return filters(c, &v, s);
    case SK_PROP_CLIP_PATH:
        return clip_path(c, &v, s);
    case SK_PROP_IMAGE_RENDERING:
        return one_kw(&v, K_RENDERING, &s->image_rendering);
    case SK_PROP_BACKGROUND_COLOR:
        return sk_v_color(c, sk_vcur_next(&v), &s->bg_color) && sk_vcur_end(&v);
    case SK_PROP_BACKGROUND_IMAGE:
        LIST(item_image, bg_image, nbg_image, sk_css_image);
    case SK_PROP_BACKGROUND_POSITION:
        LIST(item_position, bg_position, nbg_position, sk_css_position);
    case SK_PROP_BACKGROUND_SIZE:
        LIST(item_size, bg_size, nbg_size, sk_css_bgsize);
    case SK_PROP_BACKGROUND_REPEAT:
        LIST(item_repeat, bg_repeat, nbg_repeat, uint8_t[2]);
    case SK_PROP_BACKGROUND_BLEND_MODE:
        LIST(item_blend, bg_blend, nbg_blend, uint8_t);
    case SK_PROP_BACKGROUND_ORIGIN:
        LIST(item_box, bg_origin, nbg_origin, uint8_t);
    case SK_PROP_BACKGROUND_CLIP:
        LIST(item_box, bg_clip, nbg_clip, uint8_t);
    case SK_PROP_BOX_SHADOW:
        if (sk_v_ident(sk_vcur_peek(&v), "none")) {
            sk_vcur_next(&v);
            s->box_shadow = NULL;
            s->nbox_shadow = 0;
            return sk_vcur_end(&v);
        }
        LIST(item_shadow_box, box_shadow, nbox_shadow, sk_css_shadow);
    case SK_PROP_COLOR: {
        /* currentcolor in color is the parent's. */
        sk_css_color own = c->color;
        c->color = c->parent_color;
        bool ok = sk_v_color(c, sk_vcur_next(&v), &s->color) && sk_vcur_end(&v);
        c->color = own;
        return ok;
    }
    case SK_PROP_FONT_FAMILY:
        return family_list(c, &v, s);
    case SK_PROP_FONT_SIZE:
        return font_size(c, sk_vcur_next(&v), &s->font_size) && sk_vcur_end(&v);
    case SK_PROP_FONT_WEIGHT:
        return font_weight(c, sk_vcur_next(&v), &s->font_weight) && sk_vcur_end(&v);
    case SK_PROP_FONT_STYLE:
        return one_kw(&v, K_FONT_STYLE, &s->font_style);
    case SK_PROP_LINE_HEIGHT:
        return line_height(c, sk_vcur_next(&v), s) && sk_vcur_end(&v);
    case SK_PROP_LETTER_SPACING: {
        sk_css_len l;
        if (!one_len(c, &v, SK_L_NEG | SK_L_NORMAL, &l))
            return false;
        s->letter_spacing = l.kind == SK_LEN_NORMAL ? 0 : l.px;
        return true;
    }
    case SK_PROP_TEXT_TRANSFORM:
        return one_kw(&v, K_TRANSFORM_TEXT, &s->text_transform);
    case SK_PROP_TEXT_ALIGN:
        if (!one_kw(&v, K_TEXT_ALIGN, &s->text_align)) {
            c->why = "text-align takes start, end, left, right or center";
            return false;
        }
        return true;
    case SK_PROP_WHITE_SPACE:
        if (!one_kw(&v, K_WHITE_SPACE, &s->white_space)) {
            c->why = "white-space takes normal, nowrap or pre";
            return false;
        }
        return true;
    case SK_PROP_TEXT_OVERFLOW:
        return one_kw(&v, K_TEXT_OVERFLOW, &s->text_overflow);
    case SK_PROP_TEXT_DECORATION_LINE: {
        s->text_decoration = 0;
        if (sk_v_ident(sk_vcur_peek(&v), "none")) {
            sk_vcur_next(&v);
            return sk_vcur_end(&v);
        }
        static const kw D[] = {{"underline", SK_DECO_UNDERLINE},
                               {"overline", SK_DECO_OVERLINE},
                               {"line-through", SK_DECO_LINE_THROUGH},
                               {NULL, 0}};
        while (!sk_vcur_end(&v)) {
            if (!kw_of(D, sk_vcur_next(&v), &k) || (s->text_decoration & k))
                return false;
            s->text_decoration |= k;
        }
        return s->text_decoration != 0;
    }
    case SK_PROP_TEXT_DECORATION_COLOR:
        return sk_v_color(c, sk_vcur_next(&v), &s->text_decoration_color) && sk_vcur_end(&v);
    case SK_PROP_TEXT_SHADOW:
        if (sk_v_ident(sk_vcur_peek(&v), "none")) {
            sk_vcur_next(&v);
            s->text_shadow = NULL;
            s->ntext_shadow = 0;
            return sk_vcur_end(&v);
        }
        LIST(item_shadow_text, text_shadow, ntext_shadow, sk_css_shadow);
    case SK_PROP_TEXT_ANCHOR:
        return one_kw(&v, K_ANCHOR, &s->text_anchor);
    case SK_PROP_CONTENT:
        return content(c, &v, s);
    case SK_PROP_COUNTER_RESET:
        return counters(c, &v, 0, &s->counter_reset, &s->ncounter_reset);
    case SK_PROP_COUNTER_INCREMENT:
        return counters(c, &v, 1, &s->counter_increment, &s->ncounter_increment);
    case SK_PROP_FILL:
        return paint(c, &v, &s->fill_kind, &s->fill);
    case SK_PROP_STROKE:
        return paint(c, &v, &s->stroke_kind, &s->stroke);
    case SK_PROP_STROKE_WIDTH:
        return one_len(c, &v, SK_L_PCT | SK_L_NUMBER, &s->stroke_width);
    case SK_PROP_STROKE_DASHARRAY: {
        s->dasharray = NULL;
        s->ndasharray = 0;
        if (sk_v_ident(sk_vcur_peek(&v), "none")) {
            sk_vcur_next(&v);
            return sk_vcur_end(&v);
        }
        /* Commas or spaces between the lengths. */
        SK_VEC(sk_css_len) list = {0};
        bool ok = true;
        while (ok && !sk_vcur_end(&v)) {
            sk_css_len l;
            ok = item_dash(c, &v, &l) && list.len < SK_PACK_MAX_LIST;
            if (ok)
                sk_vec_push(&list, l);
            sk_vcur_comma(&v);
        }
        if (ok && list.len) {
            s->ndasharray = (uint16_t)list.len;
            s->dasharray = keep(c, list.ptr, list.len, sizeof *list.ptr);
        }
        sk_vec_free(&list);
        return ok && s->ndasharray;
    }
    case SK_PROP_STROKE_LINECAP:
        return one_kw(&v, K_LINECAP, &s->linecap);
    case SK_PROP_STROKE_LINEJOIN:
        return one_kw(&v, K_LINEJOIN, &s->linejoin);
    case SK_PROP_ANIMATION_NAME:
        LIST(item_anim_name, anim_name, nanim_name, uint32_t);
    case SK_PROP_ANIMATION_DURATION:
        LIST(item_duration, anim_duration, nanim_duration, float);
    case SK_PROP_ANIMATION_TIMING_FUNCTION:
        LIST(item_timing, anim_timing, nanim_timing, sk_css_timing);
    case SK_PROP_ANIMATION_DELAY:
        LIST(item_time, anim_delay, nanim_delay, float);
    case SK_PROP_ANIMATION_ITERATION_COUNT:
        LIST(item_iterations, anim_iterations, nanim_iterations, float);
    case SK_PROP_ANIMATION_DIRECTION:
        LIST(item_dir, anim_direction, nanim_direction, uint8_t);
    case SK_PROP_ANIMATION_FILL_MODE:
        LIST(item_fill, anim_fill, nanim_fill, uint8_t);
    case SK_PROP_ANIMATION_PLAY_STATE:
        LIST(item_play, anim_paused, nanim_paused, uint8_t);
    case SK_PROP_TRANSITION_PROPERTY:
        LIST(item_tr_prop, tr_property, ntr_property, uint16_t);
    case SK_PROP_TRANSITION_DURATION:
        LIST(item_duration, tr_duration, ntr_duration, float);
    case SK_PROP_TRANSITION_TIMING_FUNCTION:
        LIST(item_timing, tr_timing, ntr_timing, sk_css_timing);
    case SK_PROP_TRANSITION_DELAY:
        LIST(item_time, tr_delay, ntr_delay, float);
    case SK_PROP_USER_SELECT:
        return one_kw(&v, K_USER_SELECT, &k);
    default:
        return false;
    }
}

bool sk_prop_parse(uint16_t id, sk_vctx *c, const sk_cv *cv, size_t n, sk_css_style *out)
{
    sk_vcur v;
    sk_vcur_init(&v, cv, n);
    if (sk_vcur_end(&v))
        return false;
    return longhand(id, c, v, out);
}

/* ---- Shorthands ---- */

static void reset(const sk_shorthand *sh, sk_vctx *c, sk_css_style *s)
{
    sk_css_style init;
    sk_style_initial(&init, c->color);
    for (size_t i = 0; i < sh->n; i++)
        sk_prop_copy(sh->longhands[i], s, &init);
}

/* width || style || color, for border, border-<side> and outline. */
static bool line(sk_vctx *c, sk_vcur *v, float *width, uint8_t *style, sk_css_color *color, const kw *styles)
{
    bool w = false, st = false, co = false;
    while (!sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_next(v);
        if (!w && line_width(c, x, width))
            w = true;
        else if (!st && (styles ? kw_of(styles, x, style) : border_style(c, x, style)))
            st = true;
        else if (!co && sk_v_color(c, x, color))
            co = true;
        else
            return false;
    }
    return w || st || co;
}

/* One layer of background, the last with its colour. */
static bool bg_layer(sk_vctx *c, sk_vcur *v, bool last, sk_css_image *image, sk_css_position *pos, sk_css_bgsize *size,
                     uint8_t repeat[2], uint8_t *origin_box, uint8_t *clip_box, sk_css_color *color, bool *has_color)
{
    bool im = false, po = false, re = false, at = false, co = false;
    int boxes = 0;
    while (!sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        uint8_t b;
        if (!im && sk_v_image(c, x, true, image)) {
            im = true;
            sk_vcur_next(v);
        } else if (!po && (sk_v_ident(x, "left") || sk_v_ident(x, "right") || sk_v_ident(x, "top") ||
                           sk_v_ident(x, "bottom") || sk_v_ident(x, "center") ||
                           (x->kind == SK_CV_TOKEN &&
                            (x->tok.type == SK_TOK_NUMBER || x->tok.type == SK_TOK_PERCENTAGE ||
                             x->tok.type == SK_TOK_DIMENSION)) ||
                           (x->kind == SK_CV_FUNCTION && sk_ident_is(x->tok.text, "calc")))) {
            if (!sk_v_position(c, v, pos))
                return false;
            po = true;
            const sk_cv *slash = sk_vcur_peek(v);
            if (slash && slash->kind == SK_CV_TOKEN && slash->tok.type == SK_TOK_DELIM && slash->tok.delim == '/') {
                sk_vcur_next(v);
                if (!bgsize(c, v, size))
                    return false;
            }
        } else if (!re && repeat_style(v, repeat)) {
            re = true;
        } else if (!at && sk_v_ident(x, "scroll")) {
            at = true;
            sk_vcur_next(v);
        } else if (boxes < 2 && kw_of(K_BOX, x, &b)) {
            if (boxes == 0)
                *origin_box = *clip_box = b;
            else
                *clip_box = b;
            boxes++;
            sk_vcur_next(v);
        } else if (last && !co && sk_v_color(c, x, color)) {
            co = true;
            *has_color = true;
            sk_vcur_next(v);
        } else {
            if (sk_v_ident(x, "fixed") || sk_v_ident(x, "local"))
                c->why = "background-attachment takes scroll: a skin does not scroll";
            return false;
        }
    }
    return im || po || re || at || co || boxes;
}

static bool background(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    SK_VEC(sk_css_image) images = {0};
    SK_VEC(sk_css_position) positions = {0};
    SK_VEC(sk_css_bgsize) sizes = {0};
    SK_VEC(uint8_t) repeats = {0}, origins = {0}, clips = {0};
    sk_css_color color = {0, 0, 0, 0};
    bool ok = true;
    size_t layers = 0;
    while (ok && !sk_vcur_end(v)) {
        sk_vcur it = sk_vcur_item(v);
        bool last = sk_vcur_end(v);
        sk_css_image image = {SK_IMAGE_NONE, 0, NULL};
        sk_css_position pos = {{0, 0, SK_LEN_PX}, {0, 0, SK_LEN_PX}, 0, 0};
        sk_css_bgsize size = {SK_BGSIZE_EXPLICIT, {0, 0, SK_LEN_AUTO}, {0, 0, SK_LEN_AUTO}};
        uint8_t repeat[2] = {SK_REPEAT, SK_REPEAT}, ob = SK_BOX_PADDING, cb = SK_BOX_BORDER;
        bool has_color = false;
        ok = bg_layer(c, &it, last, &image, &pos, &size, repeat, &ob, &cb, &color, &has_color) &&
             layers < SK_PACK_MAX_LIST;
        if (!ok)
            break;
        sk_vec_push(&images, image);
        sk_vec_push(&positions, pos);
        sk_vec_push(&sizes, size);
        sk_vec_push(&repeats, repeat[0]);
        sk_vec_push(&repeats, repeat[1]);
        sk_vec_push(&origins, ob);
        sk_vec_push(&clips, cb);
        layers++;
    }
    if (ok && layers) {
        s->bg_color = color;
        s->nbg_image = s->nbg_position = s->nbg_size = s->nbg_repeat = s->nbg_origin = s->nbg_clip = (uint16_t)layers;
        s->bg_image = keep(c, images.ptr, layers, sizeof *images.ptr);
        s->bg_position = keep(c, positions.ptr, layers, sizeof *positions.ptr);
        s->bg_size = keep(c, sizes.ptr, layers, sizeof *sizes.ptr);
        s->bg_repeat = keep(c, repeats.ptr, 2 * layers, 1);
        s->bg_origin = keep(c, origins.ptr, layers, 1);
        s->bg_clip = keep(c, clips.ptr, layers, 1);
    }
    sk_vec_free(&images);
    sk_vec_free(&positions);
    sk_vec_free(&sizes);
    sk_vec_free(&repeats);
    sk_vec_free(&origins);
    sk_vec_free(&clips);
    return ok && layers;
}

static bool font(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    bool st = false, we = false, va = false, str = false;
    int normals = 0;
    for (;;) {
        const sk_cv *x = sk_vcur_peek(v);
        if (!x)
            return false;
        uint8_t k;
        if (sk_v_ident(x, "normal")) {
            normals++;
        } else if (!st && kw_of(K_FONT_STYLE, x, &k)) {
            s->font_style = k;
            st = true;
        } else if (!we && (sk_v_ident(x, "bold") || sk_v_ident(x, "bolder") || sk_v_ident(x, "lighter") ||
                           (x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_NUMBER))) {
            if (!font_weight(c, x, &s->font_weight))
                return false;
            we = true;
        } else if (!va && sk_v_ident(x, "small-caps")) {
            c->why = "small-caps is not supported";
            return false;
        } else if (!str && (sk_v_ident(x, "condensed") || sk_v_ident(x, "expanded"))) {
            c->why = "font-stretch is not supported";
            return false;
        } else {
            break;
        }
        sk_vcur_next(v);
        if (normals > 4)
            return false;
    }
    (void)va;
    (void)str;
    if (!font_size(c, sk_vcur_next(v), &s->font_size))
        return false;
    const sk_cv *slash = sk_vcur_peek(v);
    if (slash && slash->kind == SK_CV_TOKEN && slash->tok.type == SK_TOK_DELIM && slash->tok.delim == '/') {
        sk_vcur_next(v);
        /* The line height's em is the font size just given. */
        float em = c->font_size;
        c->font_size = s->font_size;
        bool ok = line_height(c, sk_vcur_next(v), s);
        c->font_size = em;
        if (!ok)
            return false;
    }
    return family_list(c, v, s);
}

static bool flex(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    const sk_cv *x = sk_vcur_peek(v);
    if (sk_v_ident(x, "none") || sk_v_ident(x, "auto")) {
        bool none = sk_v_ident(x, "none");
        sk_vcur_next(v);
        s->flex_grow = s->flex_shrink = none ? 0.0f : 1.0f;
        s->flex_basis = (sk_css_len){0, 0, SK_LEN_AUTO};
        return sk_vcur_end(v);
    }
    /* [grow shrink?] || basis. A unitless 0 is a factor unless two
     * factors came before it. An omitted basis is 0%. */
    const sk_cv *items[3];
    int n = 0;
    while (!sk_vcur_end(v)) {
        if (n == 3)
            return false;
        items[n++] = sk_vcur_next(v);
    }
    float grow = 1, shrink = 1, f;
    sk_css_len basis = {0, 0, SK_LEN_PX};
    int i = 0;
    if (sk_v_number(items[0], &f)) {
        grow = f;
        i = 1;
        if (i < n && sk_v_number(items[i], &f)) {
            shrink = f;
            i++;
        }
        if (i < n) {
            if (i == 2 && sk_v_number(items[i], &f) && f == 0)
                basis = (sk_css_len){0, 0, SK_LEN_PX};
            else if (!sk_v_len(c, items[i], SK_L_PCT | SK_L_AUTO, &basis))
                return false;
            i++;
        }
    } else {
        if (!sk_v_len(c, items[0], SK_L_PCT | SK_L_AUTO, &basis))
            return false;
        i = 1;
        if (i < n && sk_v_number(items[i], &f)) {
            grow = f;
            i++;
            if (i < n && sk_v_number(items[i], &f)) {
                shrink = f;
                i++;
            }
        }
    }
    if (i != n || grow < 0 || shrink < 0)
        return false;
    s->flex_grow = grow;
    s->flex_shrink = shrink;
    s->flex_basis = basis;
    return true;
}

static bool animation(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    SK_VEC(uint32_t) names = {0};
    SK_VEC(float) dur = {0}, delay = {0}, iter = {0};
    SK_VEC(sk_css_timing) timing = {0};
    SK_VEC(uint8_t) dir = {0}, fill = {0}, play = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        sk_vcur it = sk_vcur_item(v);
        uint32_t name = SK_PACK_NONE;
        float d = 0, dl = 0, ic = 1;
        sk_css_timing tf = {SK_TIMING_CUBIC, 0, 0, .25f, .1f, .25f, 1};
        uint8_t di = 0, fi = 0, pl = 0;
        bool hn = false, hd = false, hdl = false, hic = false, htf = false, hdi = false, hfi = false, hpl = false;
        while (ok && !sk_vcur_end(&it)) {
            const sk_cv *x = sk_vcur_next(&it);
            float f;
            if (sk_v_time(x, &f)) {
                if (!hd) {
                    ok = f >= 0;
                    d = f;
                    hd = true;
                } else if (!hdl) {
                    dl = f;
                    hdl = true;
                } else {
                    ok = false;
                }
            } else if (!htf && sk_v_timing(x, &tf)) {
                htf = true;
            } else if (!hic && iterations(x, &ic)) {
                hic = true;
            } else if (!hdi && kw_of(K_ANIM_DIR, x, &di) && !sk_v_ident(x, "normal")) {
                hdi = true;
            } else if (!hfi && kw_of(K_ANIM_FILL, x, &fi) && !sk_v_ident(x, "none")) {
                hfi = true;
            } else if (!hpl && kw_of(K_PLAY, x, &pl)) {
                hpl = true;
            } else if (!hn) {
                if (sk_v_ident(x, "normal") && !hdi) {
                    hdi = true;
                } else {
                    ok = anim_name(c, x, &name);
                    hn = true;
                }
            } else {
                ok = false;
            }
        }
        if (ok && names.len >= SK_PACK_MAX_LIST)
            ok = false;
        if (!ok)
            break;
        sk_vec_push(&names, name);
        sk_vec_push(&dur, d);
        sk_vec_push(&delay, dl);
        sk_vec_push(&iter, ic);
        sk_vec_push(&timing, tf);
        sk_vec_push(&dir, di);
        sk_vec_push(&fill, fi);
        sk_vec_push(&play, pl);
    }
    size_t n = names.len;
    if (ok && n) {
        s->nanim_name = s->nanim_duration = s->nanim_delay = s->nanim_iterations = s->nanim_timing =
            s->nanim_direction = s->nanim_fill = s->nanim_paused = (uint16_t)n;
        s->anim_name = keep(c, names.ptr, n, sizeof(uint32_t));
        s->anim_duration = keep(c, dur.ptr, n, sizeof(float));
        s->anim_delay = keep(c, delay.ptr, n, sizeof(float));
        s->anim_iterations = keep(c, iter.ptr, n, sizeof(float));
        s->anim_timing = keep(c, timing.ptr, n, sizeof(sk_css_timing));
        s->anim_direction = keep(c, dir.ptr, n, 1);
        s->anim_fill = keep(c, fill.ptr, n, 1);
        s->anim_paused = keep(c, play.ptr, n, 1);
    }
    sk_vec_free(&names);
    sk_vec_free(&dur);
    sk_vec_free(&delay);
    sk_vec_free(&iter);
    sk_vec_free(&timing);
    sk_vec_free(&dir);
    sk_vec_free(&fill);
    sk_vec_free(&play);
    return ok && n;
}

static bool transition(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    SK_VEC(uint16_t) props = {0};
    SK_VEC(float) dur = {0}, delay = {0};
    SK_VEC(sk_css_timing) timing = {0};
    bool ok = true;
    while (ok && !sk_vcur_end(v)) {
        sk_vcur it = sk_vcur_item(v);
        uint16_t prop = SK_PROP_ALL;
        float d = 0, dl = 0;
        sk_css_timing tf = {SK_TIMING_CUBIC, 0, 0, .25f, .1f, .25f, 1};
        bool hp = false, hd = false, hdl = false, htf = false;
        while (ok && !sk_vcur_end(&it)) {
            const sk_cv *x = sk_vcur_next(&it);
            float f;
            if (sk_v_time(x, &f)) {
                if (!hd) {
                    ok = f >= 0;
                    d = f;
                    hd = true;
                } else if (!hdl) {
                    dl = f;
                    hdl = true;
                } else {
                    ok = false;
                }
            } else if (!htf && sk_v_timing(x, &tf)) {
                htf = true;
            } else if (!hp) {
                ok = tr_prop(c, x, &prop);
                hp = true;
            } else {
                ok = false;
            }
        }
        if (ok && props.len >= SK_PACK_MAX_LIST)
            ok = false;
        if (!ok)
            break;
        sk_vec_push(&props, prop);
        sk_vec_push(&dur, d);
        sk_vec_push(&delay, dl);
        sk_vec_push(&timing, tf);
    }
    size_t n = props.len;
    if (ok && n) {
        s->ntr_property = s->ntr_duration = s->ntr_delay = s->ntr_timing = (uint16_t)n;
        s->tr_property = keep(c, props.ptr, n, sizeof(uint16_t));
        s->tr_duration = keep(c, dur.ptr, n, sizeof(float));
        s->tr_delay = keep(c, delay.ptr, n, sizeof(float));
        s->tr_timing = keep(c, timing.ptr, n, sizeof(sk_css_timing));
    }
    sk_vec_free(&props);
    sk_vec_free(&dur);
    sk_vec_free(&delay);
    sk_vec_free(&timing);
    return ok && n;
}

static bool border_image(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    bool src = false, slice = false, rep = false;
    while (!sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        if (!src && sk_v_image(c, x, true, &s->bi_source)) {
            src = true;
            sk_vcur_next(v);
        } else if (!slice && border_slice(v, s)) {
            slice = true;
            const sk_cv *d = sk_vcur_peek(v);
            if (d && d->kind == SK_CV_TOKEN && d->tok.type == SK_TOK_DELIM && d->tok.delim == '/') {
                sk_vcur_next(v);
                const sk_cv *e = sk_vcur_peek(v);
                bool second = e && e->kind == SK_CV_TOKEN && e->tok.type == SK_TOK_DELIM && e->tok.delim == '/';
                if (!second && !border_widths(c, v, s))
                    return false;
                const sk_cv *f = sk_vcur_peek(v);
                if (f && f->kind == SK_CV_TOKEN && f->tok.type == SK_TOK_DELIM && f->tok.delim == '/') {
                    sk_vcur_next(v);
                    if (!border_outset(c, v, s))
                        return false;
                }
            }
        } else if (!rep && border_repeat(v, s)) {
            rep = true;
        } else {
            return false;
        }
    }
    return src || slice || rep;
}

static bool border_radius(sk_vctx *c, sk_vcur *v, sk_css_style *s)
{
    sk_css_len h[4], vv[4];
    int nh = 0, nv = 0;
    bool slash = false;
    while (!sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_next(v);
        if (x->kind == SK_CV_TOKEN && x->tok.type == SK_TOK_DELIM && x->tok.delim == '/') {
            if (slash || !nh)
                return false;
            slash = true;
            continue;
        }
        sk_css_len *dst = slash ? vv : h;
        int *n = slash ? &nv : &nh;
        if (*n == 4 || !sk_v_len(c, x, SK_L_PCT, &dst[*n]))
            return false;
        (*n)++;
    }
    if (!nh || (slash && !nv))
        return false;
    if (!slash) {
        memcpy(vv, h, sizeof h);
        nv = nh;
    }
    for (int i = 0; i < 4; i++) {
        s->radius[i][0] = h[side_of(i, nh)];
        s->radius[i][1] = vv[side_of(i, nv)];
    }
    return true;
}

bool sk_shorthand_parse(const sk_shorthand *sh, sk_vctx *c, const sk_cv *cv, size_t n, sk_css_style *s)
{
    sk_vcur v;
    sk_vcur_init(&v, cv, n);
    if (sk_vcur_end(&v))
        return false;
    reset(sh, c, s);
    const char *name = sh->name;
    if (!strcmp(name, "inset"))
        return sides(c, &v, s->inset, SK_L_PCT | SK_L_NEG | SK_L_AUTO);
    if (!strcmp(name, "margin"))
        return sides(c, &v, s->margin, SK_L_PCT | SK_L_NEG | SK_L_AUTO);
    if (!strcmp(name, "padding"))
        return sides(c, &v, s->padding, SK_L_PCT);
    if (!strcmp(name, "border-width") || !strcmp(name, "border-style") || !strcmp(name, "border-color")) {
        int which = name[7] == 'w' ? 0 : name[7] == 's' ? 1 : 2;
        const sk_cv *x[4];
        int k = 0;
        while (!sk_vcur_end(&v)) {
            if (k == 4)
                return false;
            x[k++] = sk_vcur_next(&v);
        }
        for (int i = 0; i < 4; i++) {
            const sk_cv *y = x[side_of(i, k)];
            bool ok = which == 0   ? line_width(c, y, &s->border_width[i])
                      : which == 1 ? border_style(c, y, &s->border_style[i])
                                   : sk_v_color(c, y, &s->border_color[i]);
            if (!ok)
                return false;
        }
        return true;
    }
    if (!strncmp(name, "border-", 7) && sh->n == 3) {
        int side = !strcmp(name, "border-top") ? SK_TOP : !strcmp(name, "border-right") ? SK_RIGHT
                                                    : !strcmp(name, "border-bottom") ? SK_BOTTOM
                                                                                      : SK_LEFT;
        return line(c, &v, &s->border_width[side], &s->border_style[side], &s->border_color[side], NULL);
    }
    if (!strcmp(name, "border")) {
        float w = 3;
        uint8_t st = SK_BORDER_NONE;
        sk_css_color co = c->color;
        if (!line(c, &v, &w, &st, &co, NULL))
            return false;
        for (int i = 0; i < 4; i++) {
            s->border_width[i] = w;
            s->border_style[i] = st;
            s->border_color[i] = co;
        }
        return true;
    }
    if (!strcmp(name, "border-radius"))
        return border_radius(c, &v, s);
    if (!strcmp(name, "border-image"))
        return border_image(c, &v, s);
    if (!strcmp(name, "outline"))
        return line(c, &v, &s->outline_width, &s->outline_style, &s->outline_color, K_OUTLINE_STYLE);
    if (!strcmp(name, "overflow")) {
        if (!kw_of(K_OVERFLOW, sk_vcur_next(&v), &s->overflow_x)) {
            c->why = "overflow takes visible, hidden or clip: a skin does not scroll";
            return false;
        }
        s->overflow_y = s->overflow_x;
        if (!sk_vcur_end(&v) && !kw_of(K_OVERFLOW, sk_vcur_next(&v), &s->overflow_y))
            return false;
        return sk_vcur_end(&v);
    }
    if (!strcmp(name, "flex"))
        return flex(c, &v, s);
    if (!strcmp(name, "flex-flow")) {
        bool d = false, w = false;
        while (!sk_vcur_end(&v)) {
            const sk_cv *x = sk_vcur_next(&v);
            if (!d && kw_of(K_FLEX_DIR, x, &s->flex_direction))
                d = true;
            else if (!w && sk_v_ident(x, "nowrap"))
                w = true;
            else
                return false;
        }
        return d || w;
    }
    if (!strcmp(name, "gap")) {
        if (!sk_v_len(c, sk_vcur_next(&v), SK_L_PCT | SK_L_NORMAL, &s->row_gap))
            return false;
        s->column_gap = s->row_gap;
        if (!sk_vcur_end(&v) && !sk_v_len(c, sk_vcur_next(&v), SK_L_PCT | SK_L_NORMAL, &s->column_gap))
            return false;
        return sk_vcur_end(&v);
    }
    if (!strcmp(name, "grid-column") || !strcmp(name, "grid-row")) {
        sk_css_line *lines = name[5] == 'c' ? s->grid_column : s->grid_row;
        if (!grid_line(&v, &lines[0]))
            return false;
        lines[1] = (sk_css_line){SK_LINE_AUTO, 0};
        const sk_cv *slash = sk_vcur_next(&v);
        if (!slash)
            return true;
        if (slash->kind != SK_CV_TOKEN || slash->tok.type != SK_TOK_DELIM || slash->tok.delim != '/')
            return false;
        return grid_line(&v, &lines[1]) && sk_vcur_end(&v);
    }
    if (!strcmp(name, "background"))
        return background(c, &v, s);
    if (!strcmp(name, "font"))
        return font(c, &v, s);
    if (!strcmp(name, "text-decoration")) {
        bool l = false, co = false, st = false;
        static const kw D[] = {{"underline", SK_DECO_UNDERLINE},
                               {"overline", SK_DECO_OVERLINE},
                               {"line-through", SK_DECO_LINE_THROUGH},
                               {NULL, 0}};
        while (!sk_vcur_end(&v)) {
            const sk_cv *x = sk_vcur_next(&v);
            uint8_t k;
            if (sk_v_ident(x, "none") && !l) {
                l = true;
            } else if (kw_of(D, x, &k) && !(s->text_decoration & k)) {
                s->text_decoration |= k;
                l = true;
            } else if (!st && sk_v_ident(x, "solid")) {
                st = true;
            } else if (!co && sk_v_color(c, x, &s->text_decoration_color)) {
                co = true;
            } else {
                return false;
            }
        }
        return l || co || st;
    }
    if (!strcmp(name, "animation"))
        return animation(c, &v, s);
    if (!strcmp(name, "transition"))
        return transition(c, &v, s);
    return false;
}

/* ---- Printing ---- */

static void num(sk_buf *b, double f)
{
    char t[32];
    snprintf(t, sizeof t, "%.4g", f);
    sk_buf_append_c(b, t);
}

static void len(sk_buf *b, sk_css_len l)
{
    switch (l.kind) {
    case SK_LEN_AUTO:
        sk_buf_append_c(b, "auto");
        return;
    case SK_LEN_NONE:
        sk_buf_append_c(b, "none");
        return;
    case SK_LEN_NORMAL:
        sk_buf_append_c(b, "normal");
        return;
    case SK_LEN_RECT:
        sk_buf_append_c(b, "rect(");
        num(b, l.px);
        sk_buf_append_c(b, "px)");
        return;
    case SK_LEN_VALUE:
        sk_buf_append_c(b, "value()");
        return;
    default:
        break;
    }
    if (l.pct != 0 && l.px != 0) {
        sk_buf_append_c(b, "calc(");
        num(b, l.pct);
        sk_buf_append_c(b, l.px < 0 ? "% - " : "% + ");
        num(b, fabs(l.px));
        sk_buf_append_c(b, "px)");
    } else if (l.pct != 0) {
        num(b, l.pct);
        sk_buf_putc(b, '%');
    } else {
        num(b, l.px);
        sk_buf_append_c(b, "px");
    }
}

static void color(sk_buf *b, sk_css_color c)
{
    int r = (int)lroundf(c.r * 255), g = (int)lroundf(c.g * 255), bl = (int)lroundf(c.b * 255);
    if (c.a >= 1)
        sk_buf_printf(b, "#%02x%02x%02x", r, g, bl);
    else
        sk_buf_printf(b, "rgba(%d, %d, %d, %.3g)", r, g, bl, (double)c.a);
}

static void shadow(sk_buf *b, const sk_css_shadow *s, bool box)
{
    if (s->inset)
        sk_buf_append_c(b, "inset ");
    num(b, s->x);
    sk_buf_append_c(b, "px ");
    num(b, s->y);
    sk_buf_append_c(b, "px ");
    num(b, s->blur);
    sk_buf_append_c(b, "px ");
    if (box) {
        num(b, s->spread);
        sk_buf_append_c(b, "px ");
    }
    color(b, s->color);
}

static void timing(sk_buf *b, const sk_css_timing *t)
{
    if (t->kind == SK_TIMING_LINEAR)
        sk_buf_append_c(b, "linear");
    else if (t->kind == SK_TIMING_STEPS)
        sk_buf_printf(b, "steps(%u, %s)", (unsigned)t->steps,
                      t->steps_pos == SK_STEP_JUMP_START  ? "jump-start"
                      : t->steps_pos == SK_STEP_JUMP_NONE ? "jump-none"
                      : t->steps_pos == SK_STEP_JUMP_BOTH ? "jump-both"
                                                          : "jump-end");
    else
        sk_buf_printf(b, "cubic-bezier(%.3g, %.3g, %.3g, %.3g)", (double)t->x1, (double)t->y1, (double)t->x2,
                      (double)t->y2);
}

static void image(sk_buf *b, const sk_css_image *im)
{
    if (im->kind == SK_IMAGE_NONE) {
        sk_buf_append_c(b, "none");
        return;
    }
    if (im->kind == SK_IMAGE_URL) {
        sk_buf_printf(b, "image(%u)", (unsigned)im->source);
        return;
    }
    const sk_css_gradient *g = im->gradient;
    sk_buf_append_c(b, g->repeating ? "repeating-" : "");
    if (g->kind == SK_GRAD_LINEAR) {
        sk_buf_append_c(b, "linear-gradient(");
        if (g->to_corner)
            sk_buf_printf(b, "to %s %s", g->to_corner & 2 ? "bottom" : "top", g->to_corner & 1 ? "right" : "left");
        else {
            num(b, g->angle);
            sk_buf_append_c(b, "deg");
        }
    } else {
        sk_buf_append_c(b, "radial-gradient(");
        sk_buf_append_c(b, g->circle ? "circle " : "ellipse ");
        if (g->extent == SK_EXTENT_EXPLICIT) {
            len(b, g->rx);
            sk_buf_putc(b, ' ');
            len(b, g->ry);
        } else {
            static const char *const EXT[] = {"farthest-corner", "closest-side", "closest-corner", "farthest-side"};
            sk_buf_append_c(b, EXT[g->extent]);
        }
        sk_buf_append_c(b, " at ");
        len(b, g->cx);
        sk_buf_putc(b, ' ');
        len(b, g->cy);
    }
    for (uint16_t i = 0; i < g->nstops; i++) {
        sk_buf_append_c(b, ", ");
        color(b, g->stops[i].color);
        if (g->stops[i].pos.kind != SK_LEN_AUTO) {
            sk_buf_putc(b, ' ');
            len(b, g->stops[i].pos);
        }
    }
    sk_buf_putc(b, ')');
}

static void xf(sk_buf *b, const sk_css_xf *x, uint16_t n)
{
    if (!n)
        sk_buf_append_c(b, "none");
    for (uint16_t i = 0; i < n; i++) {
        if (i)
            sk_buf_putc(b, ' ');
        switch (x[i].kind) {
        case SK_XF_TRANSLATE:
            sk_buf_append_c(b, "translate(");
            len(b, x[i].tx);
            sk_buf_append_c(b, ", ");
            len(b, x[i].ty);
            sk_buf_putc(b, ')');
            break;
        case SK_XF_ROTATE:
            sk_buf_append_c(b, "rotate(");
            num(b, x[i].v[0]);
            sk_buf_append_c(b, "deg)");
            break;
        case SK_XF_SCALE:
            sk_buf_append_c(b, "scale(");
            num(b, x[i].v[0]);
            sk_buf_append_c(b, ", ");
            num(b, x[i].v[1]);
            sk_buf_putc(b, ')');
            break;
        case SK_XF_SKEW:
            sk_buf_append_c(b, "skew(");
            num(b, x[i].v[0]);
            sk_buf_append_c(b, "deg, ");
            num(b, x[i].v[1]);
            sk_buf_append_c(b, "deg)");
            break;
        default:
            sk_buf_append_c(b, "matrix(");
            for (int k = 0; k < 6; k++) {
                if (k)
                    sk_buf_append_c(b, ", ");
                num(b, x[i].v[k]);
            }
            sk_buf_putc(b, ')');
            break;
        }
    }
}

static void track_print(sk_buf *b, const sk_css_track *t)
{
    if (t->kind == SK_TRACK_AUTO)
        sk_buf_append_c(b, "auto");
    else if (t->kind == SK_TRACK_FR) {
        num(b, t->fr);
        sk_buf_append_c(b, "fr");
    } else
        len(b, t->len);
}

static void line_print(sk_buf *b, sk_css_line l)
{
    if (l.kind == SK_LINE_AUTO)
        sk_buf_append_c(b, "auto");
    else if (l.kind == SK_LINE_SPAN)
        sk_buf_printf(b, "span %d", l.n);
    else
        sk_buf_printf(b, "%d", l.n);
}

void sk_prop_print(sk_buf *b, uint16_t id, const sk_css_style *s)
{
    switch (id) {
    case SK_PROP_DISPLAY:
        sk_buf_append_c(b, kw_name(K_DISPLAY, s->display));
        break;
    case SK_PROP_POSITION:
        sk_buf_append_c(b, kw_name(K_POSITION, s->position));
        break;
    case SK_PROP_FLOAT:
        sk_buf_append_c(b, kw_name(K_FLOAT, s->float_));
        break;
    case SK_PROP_BOX_SIZING:
        sk_buf_append_c(b, kw_name(K_BOX_SIZING, s->box_sizing));
        break;
    case SK_PROP_TOP:
    case SK_PROP_RIGHT:
    case SK_PROP_BOTTOM:
    case SK_PROP_LEFT:
        len(b, s->inset[id - SK_PROP_TOP]);
        break;
    case SK_PROP_WIDTH:
        len(b, s->width);
        break;
    case SK_PROP_HEIGHT:
        len(b, s->height);
        break;
    case SK_PROP_MIN_WIDTH:
        len(b, s->min_width);
        break;
    case SK_PROP_MIN_HEIGHT:
        len(b, s->min_height);
        break;
    case SK_PROP_MAX_WIDTH:
        len(b, s->max_width);
        break;
    case SK_PROP_MAX_HEIGHT:
        len(b, s->max_height);
        break;
    case SK_PROP_MARGIN_TOP:
    case SK_PROP_MARGIN_RIGHT:
    case SK_PROP_MARGIN_BOTTOM:
    case SK_PROP_MARGIN_LEFT:
        len(b, s->margin[id - SK_PROP_MARGIN_TOP]);
        break;
    case SK_PROP_PADDING_TOP:
    case SK_PROP_PADDING_RIGHT:
    case SK_PROP_PADDING_BOTTOM:
    case SK_PROP_PADDING_LEFT:
        len(b, s->padding[id - SK_PROP_PADDING_TOP]);
        break;
    case SK_PROP_BORDER_TOP_WIDTH:
    case SK_PROP_BORDER_RIGHT_WIDTH:
    case SK_PROP_BORDER_BOTTOM_WIDTH:
    case SK_PROP_BORDER_LEFT_WIDTH:
        num(b, s->border_width[id - SK_PROP_BORDER_TOP_WIDTH]);
        sk_buf_append_c(b, "px");
        break;
    case SK_PROP_BORDER_TOP_STYLE:
    case SK_PROP_BORDER_RIGHT_STYLE:
    case SK_PROP_BORDER_BOTTOM_STYLE:
    case SK_PROP_BORDER_LEFT_STYLE:
        sk_buf_append_c(b, kw_name(K_BORDER_STYLE, s->border_style[id - SK_PROP_BORDER_TOP_STYLE]));
        break;
    case SK_PROP_BORDER_TOP_COLOR:
    case SK_PROP_BORDER_RIGHT_COLOR:
    case SK_PROP_BORDER_BOTTOM_COLOR:
    case SK_PROP_BORDER_LEFT_COLOR:
        color(b, s->border_color[id - SK_PROP_BORDER_TOP_COLOR]);
        break;
    case SK_PROP_BORDER_TOP_LEFT_RADIUS:
    case SK_PROP_BORDER_TOP_RIGHT_RADIUS:
    case SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS:
    case SK_PROP_BORDER_BOTTOM_LEFT_RADIUS:
        len(b, s->radius[id - SK_PROP_BORDER_TOP_LEFT_RADIUS][0]);
        sk_buf_putc(b, ' ');
        len(b, s->radius[id - SK_PROP_BORDER_TOP_LEFT_RADIUS][1]);
        break;
    case SK_PROP_BORDER_IMAGE_SOURCE:
        image(b, &s->bi_source);
        break;
    case SK_PROP_BORDER_IMAGE_SLICE:
        for (int i = 0; i < 4; i++) {
            num(b, s->bi_slice[i]);
            sk_buf_append_c(b, s->bi_slice_pct & (1 << i) ? "% " : " ");
        }
        if (s->bi_fill)
            sk_buf_append_c(b, "fill");
        break;
    case SK_PROP_BORDER_IMAGE_WIDTH:
        for (int i = 0; i < 4; i++) {
            if (i)
                sk_buf_putc(b, ' ');
            if (s->bi_width_num & (1 << i))
                num(b, s->bi_width[i].px);
            else
                len(b, s->bi_width[i]);
        }
        break;
    case SK_PROP_BORDER_IMAGE_OUTSET:
        for (int i = 0; i < 4; i++) {
            if (i)
                sk_buf_putc(b, ' ');
            num(b, s->bi_outset[i]);
            sk_buf_append_c(b, "px");
        }
        break;
    case SK_PROP_BORDER_IMAGE_REPEAT:
        sk_buf_printf(b, "%s %s", kw_name(K_BI_REPEAT, s->bi_repeat[0]), kw_name(K_BI_REPEAT, s->bi_repeat[1]));
        break;
    case SK_PROP_OUTLINE_WIDTH:
        num(b, s->outline_width);
        sk_buf_append_c(b, "px");
        break;
    case SK_PROP_OUTLINE_STYLE:
        sk_buf_append_c(b, kw_name(K_BORDER_STYLE, s->outline_style));
        break;
    case SK_PROP_OUTLINE_COLOR:
        color(b, s->outline_color);
        break;
    case SK_PROP_OUTLINE_OFFSET:
        num(b, s->outline_offset);
        sk_buf_append_c(b, "px");
        break;
    case SK_PROP_OVERFLOW_X:
        sk_buf_append_c(b, kw_name(K_OVERFLOW, s->overflow_x));
        break;
    case SK_PROP_OVERFLOW_Y:
        sk_buf_append_c(b, kw_name(K_OVERFLOW, s->overflow_y));
        break;
    case SK_PROP_Z_INDEX:
        if (s->z_auto)
            sk_buf_append_c(b, "auto");
        else
            sk_buf_printf(b, "%d", (int)s->z_index);
        break;
    case SK_PROP_VISIBILITY:
        sk_buf_append_c(b, kw_name(K_VISIBILITY, s->visibility));
        break;
    case SK_PROP_OPACITY:
        num(b, s->opacity);
        break;
    case SK_PROP_POINTER_EVENTS:
        sk_buf_append_c(b, kw_name(K_POINTER, s->pointer_events));
        break;
    case SK_PROP_CURSOR:
        sk_buf_append_c(b, kw_name(K_CURSOR, s->cursor));
        break;
    case SK_PROP_MIX_BLEND_MODE:
        sk_buf_append_c(b, kw_name(K_BLEND, s->mix_blend));
        break;
    case SK_PROP_ISOLATION:
        sk_buf_append_c(b, s->isolate ? "isolate" : "auto");
        break;
    case SK_PROP_CONTAIN:
        sk_buf_append_c(b, "none");
        break;
    case SK_PROP_FLEX_DIRECTION:
        sk_buf_append_c(b, kw_name(K_FLEX_DIR, s->flex_direction));
        break;
    case SK_PROP_FLEX_WRAP:
        sk_buf_append_c(b, "nowrap");
        break;
    case SK_PROP_FLEX_GROW:
        num(b, s->flex_grow);
        break;
    case SK_PROP_FLEX_SHRINK:
        num(b, s->flex_shrink);
        break;
    case SK_PROP_FLEX_BASIS:
        len(b, s->flex_basis);
        break;
    case SK_PROP_JUSTIFY_CONTENT:
        sk_buf_append_c(b, kw_name(K_JUSTIFY, s->justify_content));
        break;
    case SK_PROP_ALIGN_ITEMS:
        sk_buf_append_c(b, kw_name(K_ALIGN, s->align_items));
        break;
    case SK_PROP_ALIGN_SELF:
        sk_buf_append_c(b, kw_name(K_ALIGN_SELF, s->align_self));
        break;
    case SK_PROP_ROW_GAP:
        len(b, s->row_gap);
        break;
    case SK_PROP_COLUMN_GAP:
        len(b, s->column_gap);
        break;
    case SK_PROP_GRID_TEMPLATE_COLUMNS:
    case SK_PROP_GRID_TEMPLATE_ROWS: {
        const sk_css_track *t = id == SK_PROP_GRID_TEMPLATE_COLUMNS ? s->grid_columns : s->grid_rows;
        uint16_t n = id == SK_PROP_GRID_TEMPLATE_COLUMNS ? s->ngrid_columns : s->ngrid_rows;
        if (!n)
            sk_buf_append_c(b, "none");
        for (uint16_t i = 0; i < n; i++) {
            if (i)
                sk_buf_putc(b, ' ');
            track_print(b, &t[i]);
        }
        break;
    }
    case SK_PROP_GRID_AUTO_ROWS:
        track_print(b, &s->grid_auto_rows);
        break;
    case SK_PROP_GRID_AUTO_COLUMNS:
        track_print(b, &s->grid_auto_columns);
        break;
    case SK_PROP_GRID_COLUMN_START:
    case SK_PROP_GRID_COLUMN_END:
        line_print(b, s->grid_column[id - SK_PROP_GRID_COLUMN_START]);
        break;
    case SK_PROP_GRID_ROW_START:
    case SK_PROP_GRID_ROW_END:
        line_print(b, s->grid_row[id - SK_PROP_GRID_ROW_START]);
        break;
    case SK_PROP_TRANSFORM:
        xf(b, s->transform, s->ntransform);
        break;
    case SK_PROP_TRANSFORM_ORIGIN:
        len(b, s->origin[0]);
        sk_buf_putc(b, ' ');
        len(b, s->origin[1]);
        break;
    case SK_PROP_FILTER:
        if (!s->nfilter)
            sk_buf_append_c(b, "none");
        for (uint16_t i = 0; i < s->nfilter; i++) {
            static const char *const FX[] = {"blur",   "brightness", "contrast", "saturate",   "grayscale",
                                             "sepia",  "invert",     "opacity",  "hue-rotate", "drop-shadow"};
            const sk_css_filter *f = &s->filter[i];
            sk_buf_printf(b, "%s%s(", i ? " " : "", FX[f->kind]);
            if (f->kind == SK_FX_DROP_SHADOW)
                shadow(b, &f->shadow, false);
            else
                num(b, f->amount);
            if (f->kind == SK_FX_BLUR)
                sk_buf_append_c(b, "px");
            else if (f->kind == SK_FX_HUE_ROTATE)
                sk_buf_append_c(b, "deg");
            sk_buf_putc(b, ')');
        }
        break;
    case SK_PROP_CLIP_PATH:
        if (!s->nclip) {
            sk_buf_append_c(b, "none");
            break;
        }
        sk_buf_append_c(b, s->clip_evenodd ? "polygon(evenodd" : "polygon(");
        for (uint16_t i = 0; i < s->nclip; i++) {
            sk_buf_append_c(b, i || s->clip_evenodd ? ", " : "");
            len(b, s->clip[2 * i]);
            sk_buf_putc(b, ' ');
            len(b, s->clip[2 * i + 1]);
        }
        sk_buf_putc(b, ')');
        break;
    case SK_PROP_IMAGE_RENDERING:
        sk_buf_append_c(b, kw_name(K_RENDERING, s->image_rendering));
        break;
    case SK_PROP_BACKGROUND_COLOR:
        color(b, s->bg_color);
        break;
    case SK_PROP_BACKGROUND_IMAGE:
        for (uint16_t i = 0; i < s->nbg_image; i++) {
            sk_buf_append_c(b, i ? ", " : "");
            image(b, &s->bg_image[i]);
        }
        break;
    case SK_PROP_BACKGROUND_POSITION:
        for (uint16_t i = 0; i < s->nbg_position; i++) {
            const sk_css_position *p = &s->bg_position[i];
            sk_buf_append_c(b, i ? ", " : "");
            sk_buf_append_c(b, p->x_edge ? "right " : "left ");
            len(b, p->x);
            sk_buf_append_c(b, p->y_edge ? " bottom " : " top ");
            len(b, p->y);
        }
        break;
    case SK_PROP_BACKGROUND_SIZE:
        for (uint16_t i = 0; i < s->nbg_size; i++) {
            const sk_css_bgsize *z = &s->bg_size[i];
            sk_buf_append_c(b, i ? ", " : "");
            if (z->kind == SK_BGSIZE_COVER)
                sk_buf_append_c(b, "cover");
            else if (z->kind == SK_BGSIZE_CONTAIN)
                sk_buf_append_c(b, "contain");
            else {
                len(b, z->w);
                sk_buf_putc(b, ' ');
                len(b, z->h);
            }
        }
        break;
    case SK_PROP_BACKGROUND_REPEAT:
        for (uint16_t i = 0; i < s->nbg_repeat; i++) {
            static const char *const R[] = {"repeat", "no-repeat", "space", "round"};
            sk_buf_printf(b, "%s%s %s", i ? ", " : "", R[s->bg_repeat[2 * i] & 3], R[s->bg_repeat[2 * i + 1] & 3]);
        }
        break;
    case SK_PROP_BACKGROUND_BLEND_MODE:
        for (uint16_t i = 0; i < s->nbg_blend; i++)
            sk_buf_printf(b, "%s%s", i ? ", " : "", kw_name(K_BLEND, s->bg_blend[i]));
        break;
    case SK_PROP_BACKGROUND_ORIGIN:
        for (uint16_t i = 0; i < s->nbg_origin; i++)
            sk_buf_printf(b, "%s%s", i ? ", " : "", kw_name(K_BOX, s->bg_origin[i]));
        break;
    case SK_PROP_BACKGROUND_CLIP:
        for (uint16_t i = 0; i < s->nbg_clip; i++)
            sk_buf_printf(b, "%s%s", i ? ", " : "", kw_name(K_BOX, s->bg_clip[i]));
        break;
    case SK_PROP_BOX_SHADOW:
    case SK_PROP_TEXT_SHADOW: {
        const sk_css_shadow *sh = id == SK_PROP_BOX_SHADOW ? s->box_shadow : s->text_shadow;
        uint16_t n = id == SK_PROP_BOX_SHADOW ? s->nbox_shadow : s->ntext_shadow;
        if (!n)
            sk_buf_append_c(b, "none");
        for (uint16_t i = 0; i < n; i++) {
            sk_buf_append_c(b, i ? ", " : "");
            shadow(b, &sh[i], id == SK_PROP_BOX_SHADOW);
        }
        break;
    }
    case SK_PROP_COLOR:
        color(b, s->color);
        break;
    case SK_PROP_FONT_FAMILY:
        for (uint16_t i = 0; i < s->nfont_family; i++)
            sk_buf_printf(b, "%s\"%s\"", i ? ", " : "", s->font_family[i]);
        sk_buf_printf(b, " = face %u", (unsigned)s->font);
        if (s->font_synth)
            sk_buf_printf(b, "%s%s", s->font_synth & 1 ? " +bold" : "", s->font_synth & 2 ? " +italic" : "");
        break;
    case SK_PROP_FONT_SIZE:
        num(b, s->font_size);
        sk_buf_append_c(b, "px");
        break;
    case SK_PROP_FONT_WEIGHT:
        sk_buf_printf(b, "%u", (unsigned)s->font_weight);
        break;
    case SK_PROP_FONT_STYLE:
        sk_buf_append_c(b, kw_name(K_FONT_STYLE, s->font_style));
        break;
    case SK_PROP_LINE_HEIGHT:
        if (s->line_height_kind == SK_LH_NORMAL)
            sk_buf_append_c(b, "normal");
        else {
            num(b, s->line_height);
            if (s->line_height_kind == SK_LH_PX)
                sk_buf_append_c(b, "px");
        }
        break;
    case SK_PROP_LETTER_SPACING:
        num(b, s->letter_spacing);
        sk_buf_append_c(b, "px");
        break;
    case SK_PROP_TEXT_TRANSFORM:
        sk_buf_append_c(b, kw_name(K_TRANSFORM_TEXT, s->text_transform));
        break;
    case SK_PROP_TEXT_ALIGN:
        sk_buf_append_c(b, kw_name(K_TEXT_ALIGN, s->text_align));
        break;
    case SK_PROP_WHITE_SPACE:
        sk_buf_append_c(b, kw_name(K_WHITE_SPACE, s->white_space));
        break;
    case SK_PROP_TEXT_OVERFLOW:
        sk_buf_append_c(b, kw_name(K_TEXT_OVERFLOW, s->text_overflow));
        break;
    case SK_PROP_TEXT_DECORATION_LINE:
        if (!s->text_decoration)
            sk_buf_append_c(b, "none");
        sk_buf_printf(b, "%s%s%s", s->text_decoration & SK_DECO_UNDERLINE ? "underline " : "",
                      s->text_decoration & SK_DECO_OVERLINE ? "overline " : "",
                      s->text_decoration & SK_DECO_LINE_THROUGH ? "line-through" : "");
        break;
    case SK_PROP_TEXT_DECORATION_COLOR:
        color(b, s->text_decoration_color);
        break;
    case SK_PROP_TEXT_ANCHOR:
        sk_buf_append_c(b, kw_name(K_ANCHOR, s->text_anchor));
        break;
    case SK_PROP_CONTENT:
        if (s->content_none || !s->ncontent)
            sk_buf_append_c(b, "none");
        for (uint16_t i = 0; i < s->ncontent; i++) {
            const sk_css_content *p = &s->content[i];
            sk_buf_append_c(b, i ? " " : "");
            if (p->kind == SK_CONTENT_TEXT)
                sk_buf_printf(b, "\"%s\"", p->text);
            else if (p->kind == SK_CONTENT_COUNTER)
                sk_buf_printf(b, "counter(%s)", p->text);
            else
                sk_buf_printf(b, "ordinal(%d + %d * row)", (int)p->base, (int)p->step);
        }
        break;
    case SK_PROP_COUNTER_RESET:
    case SK_PROP_COUNTER_INCREMENT: {
        const sk_css_counter *k = id == SK_PROP_COUNTER_RESET ? s->counter_reset : s->counter_increment;
        uint16_t n = id == SK_PROP_COUNTER_RESET ? s->ncounter_reset : s->ncounter_increment;
        if (!n)
            sk_buf_append_c(b, "none");
        for (uint16_t i = 0; i < n; i++)
            sk_buf_printf(b, "%s%s %d", i ? " " : "", k[i].name, (int)k[i].value);
        break;
    }
    case SK_PROP_FILL:
        if (s->fill_kind == SK_PAINT_NONE)
            sk_buf_append_c(b, "none");
        else
            color(b, s->fill);
        break;
    case SK_PROP_STROKE:
        if (s->stroke_kind == SK_PAINT_NONE)
            sk_buf_append_c(b, "none");
        else
            color(b, s->stroke);
        break;
    case SK_PROP_STROKE_WIDTH:
        len(b, s->stroke_width);
        break;
    case SK_PROP_STROKE_DASHARRAY:
        if (!s->ndasharray)
            sk_buf_append_c(b, "none");
        for (uint16_t i = 0; i < s->ndasharray; i++) {
            sk_buf_append_c(b, i ? " " : "");
            len(b, s->dasharray[i]);
        }
        break;
    case SK_PROP_STROKE_LINECAP:
        sk_buf_append_c(b, kw_name(K_LINECAP, s->linecap));
        break;
    case SK_PROP_STROKE_LINEJOIN:
        sk_buf_append_c(b, kw_name(K_LINEJOIN, s->linejoin));
        break;
    case SK_PROP_ANIMATION_NAME:
        for (uint16_t i = 0; i < s->nanim_name; i++) {
            if (s->anim_name[i] == SK_PACK_NONE)
                sk_buf_printf(b, "%snone", i ? ", " : "");
            else
                sk_buf_printf(b, "%skeyframes(%u)", i ? ", " : "", (unsigned)s->anim_name[i]);
        }
        break;
    case SK_PROP_ANIMATION_DURATION:
    case SK_PROP_ANIMATION_DELAY:
    case SK_PROP_ANIMATION_ITERATION_COUNT:
    case SK_PROP_TRANSITION_DURATION:
    case SK_PROP_TRANSITION_DELAY: {
        const float *f = id == SK_PROP_ANIMATION_DURATION           ? s->anim_duration
                         : id == SK_PROP_ANIMATION_DELAY            ? s->anim_delay
                         : id == SK_PROP_ANIMATION_ITERATION_COUNT  ? s->anim_iterations
                         : id == SK_PROP_TRANSITION_DURATION        ? s->tr_duration
                                                                    : s->tr_delay;
        uint16_t n = id == SK_PROP_ANIMATION_DURATION          ? s->nanim_duration
                     : id == SK_PROP_ANIMATION_DELAY           ? s->nanim_delay
                     : id == SK_PROP_ANIMATION_ITERATION_COUNT ? s->nanim_iterations
                     : id == SK_PROP_TRANSITION_DURATION       ? s->ntr_duration
                                                               : s->ntr_delay;
        bool count = id == SK_PROP_ANIMATION_ITERATION_COUNT;
        for (uint16_t i = 0; i < n; i++) {
            sk_buf_append_c(b, i ? ", " : "");
            if (count && isinf(f[i]))
                sk_buf_append_c(b, "infinite");
            else {
                num(b, f[i]);
                if (!count)
                    sk_buf_putc(b, 's');
            }
        }
        break;
    }
    case SK_PROP_ANIMATION_TIMING_FUNCTION:
        for (uint16_t i = 0; i < s->nanim_timing; i++) {
            sk_buf_append_c(b, i ? ", " : "");
            timing(b, &s->anim_timing[i]);
        }
        break;
    case SK_PROP_TRANSITION_TIMING_FUNCTION:
        for (uint16_t i = 0; i < s->ntr_timing; i++) {
            sk_buf_append_c(b, i ? ", " : "");
            timing(b, &s->tr_timing[i]);
        }
        break;
    case SK_PROP_ANIMATION_DIRECTION:
        for (uint16_t i = 0; i < s->nanim_direction; i++)
            sk_buf_printf(b, "%s%s", i ? ", " : "", kw_name(K_ANIM_DIR, s->anim_direction[i]));
        break;
    case SK_PROP_ANIMATION_FILL_MODE:
        for (uint16_t i = 0; i < s->nanim_fill; i++)
            sk_buf_printf(b, "%s%s", i ? ", " : "", kw_name(K_ANIM_FILL, s->anim_fill[i]));
        break;
    case SK_PROP_ANIMATION_PLAY_STATE:
        for (uint16_t i = 0; i < s->nanim_paused; i++)
            sk_buf_printf(b, "%s%s", i ? ", " : "", s->anim_paused[i] ? "paused" : "running");
        break;
    case SK_PROP_TRANSITION_PROPERTY:
        for (uint16_t i = 0; i < s->ntr_property; i++) {
            uint16_t p = s->tr_property[i];
            const sk_propdef *d = sk_prop_def(p);
            sk_buf_printf(b, "%s%s", i ? ", " : "", p == SK_PROP_ALL ? "all" : p == 0 ? "none" : d ? d->name : "?");
        }
        break;
    default:
        break;
    }
}

int sk_css_wide(const sk_cv *cv, size_t n)
{
    sk_vcur v;
    sk_vcur_init(&v, cv, n);
    const sk_cv *x = sk_vcur_next(&v);
    if (!x || !sk_vcur_end(&v))
        return 0;
    if (sk_v_ident(x, "inherit"))
        return SK_WIDE_INHERIT;
    if (sk_v_ident(x, "initial"))
        return SK_WIDE_INITIAL;
    if (sk_v_ident(x, "unset"))
        return SK_WIDE_UNSET;
    return 0;
}

bool sk_has_var(const sk_cv *cv, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (cv[i].kind == SK_CV_FUNCTION && sk_ident_is(cv[i].tok.text, "var"))
            return true;
        if (cv[i].kind != SK_CV_TOKEN && sk_has_var(cv[i].kids, cv[i].n))
            return true;
    }
    return false;
}
