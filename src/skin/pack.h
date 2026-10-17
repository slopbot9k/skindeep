/* A skin pack in memory (decision 0037): what skinc compiled a skin
 * into, as the runtime holds it once a pack has passed its bounds
 * checks. skinc's front end builds the same values, so the two read
 * one model: the element tree with its widget templates, a computed
 * style per element per state, the keyframe tracks, the images, the
 * layers skinc pre-rendered, the fonts and the sounds.
 *
 * Nothing here is CSS text. A style is CSS's computed value of every
 * property of the subset decision 0037 lists, with em, var(),
 * currentcolor, inherit and the font lookup already resolved; what is
 * left are percentages, which layout resolves, and the values the
 * runtime fills in: the rects a split moves, a bar's fill, a sprite's
 * frame. Lists (layers, shadows, transforms) are arrays the pack owns.
 *
 * The loader (src/skin/pack.c) reads the file format of decision 0037
 * into these structures and checks every count, index, offset, string
 * and float before anything uses it. skinc's writer (skinc/write.c)
 * makes the file; src/skin/packfmt.h holds what the two share. */
#ifndef SK_SKIN_PACK_H
#define SK_SKIN_PACK_H

#include "../base/base.h"

/* ---- The file ---- */

#define SK_PACK_MAGIC "SKINPACK" /* 8 bytes, no NUL */
#define SK_PACK_MAJOR 1          /* a loader refuses any other */
#define SK_PACK_MINOR 0          /* a loader reads any minor of its major */
#define SK_PACK_NONE 0xFFFFFFFFu /* an absent index */

/* Section ids, four ASCII bytes read as a little-endian uint32. */
#define SK_PACK_ID(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define SK_SECT_STRS SK_PACK_ID('S', 'T', 'R', 'S') /* string pool */
#define SK_SECT_META SK_PACK_ID('M', 'E', 'T', 'A') /* manifest: window, menu, terminal */
#define SK_SECT_SPLT SK_PACK_ID('S', 'P', 'L', 'T') /* splits */
#define SK_SECT_SPAN SK_PACK_ID('S', 'P', 'A', 'N') /* the span classes SK_VAR_SPAN tells apart */
#define SK_SECT_RECT SK_PACK_ID('R', 'E', 'C', 'T') /* rects and their follows */
#define SK_SECT_WIDG SK_PACK_ID('W', 'I', 'D', 'G') /* widgets */
#define SK_SECT_MARK SK_PACK_ID('M', 'A', 'R', 'K') /* gauges' marks */
#define SK_SECT_ELEM SK_PACK_ID('E', 'L', 'E', 'M') /* the element tree */
#define SK_SECT_SVGG SK_PACK_ID('S', 'V', 'G', 'G') /* SVG geometry */
#define SK_SECT_INPT SK_PACK_ID('I', 'N', 'P', 'T') /* state inputs */
#define SK_SECT_STAB SK_PACK_ID('S', 'T', 'A', 'B') /* state tables */
#define SK_SECT_STYL SK_PACK_ID('S', 'T', 'Y', 'L') /* styles */
#define SK_SECT_KEYF SK_PACK_ID('K', 'E', 'Y', 'F') /* keyframes */
#define SK_SECT_IMGS SK_PACK_ID('I', 'M', 'G', 'S') /* image sources */
#define SK_SECT_VARI SK_PACK_ID('V', 'A', 'R', 'I') /* their decoded variants */
#define SK_SECT_LAYR SK_PACK_ID('L', 'A', 'Y', 'R') /* pre-rendered layers */
#define SK_SECT_FONT SK_PACK_ID('F', 'O', 'N', 'T') /* fonts */
#define SK_SECT_SNDS SK_PACK_ID('S', 'N', 'D', 'S') /* sounds */
#define SK_SECT_BLOB SK_PACK_ID('B', 'L', 'O', 'B') /* QOI, TrueType and ADPCM bytes */

/* The loader's limits: a pack beyond any of them is refused. */
#define SK_PACK_MAX_BYTES (1u << 30)
#define SK_PACK_MAX_ELEMENTS 65535u
#define SK_PACK_MAX_INPUTS 10u     /* state inputs of one element */
#define SK_PACK_MAX_STATES 4096u   /* entries of one state table */
#define SK_PACK_MAX_TOTAL_STATES 65536u /* entries of every state table */
#define SK_PACK_MAX_LIST 256u      /* items of one value list */
#define SK_PACK_MAX_PIXELS (1u << 26)
#define SK_PACK_MAX_SIDE 16384u    /* an image's width or height */
#define SK_PACK_MAX_LIMITS 8u      /* meters of a limits widget */
/* A style's number, other than an iteration count, is at most this far
 * from 0: a length in px or percent, an angle in degrees, a time in
 * seconds, a factor. Layout and paint can then add and scale them in
 * float without overflow. */
#define SK_PACK_MAX_FLOAT 1e6f
/* The most sections, styles, keyframe tracks and images a pack holds:
 * no table could reach more styles, and the loader bounds its memory. */
#define SK_PACK_MAX_SECTIONS 64u
#define SK_PACK_MAX_STYLES SK_PACK_MAX_TOTAL_STATES
#define SK_PACK_MAX_KEYFRAMES 65536u
#define SK_PACK_MAX_IMAGES 65536u
/* What the loader allocates for a pack, at most. */
#define SK_PACK_MAX_MEMORY (1u << 30)

/* ---- Values ---- */

/* A length as computed: px plus pct percent of what the property
 * refers to, which is how calc(50% - 20px) and 100% both stay. The
 * kinds besides SK_LEN_PX carry no number unless they say so. */
typedef enum {
    SK_LEN_PX,     /* px + pct% */
    SK_LEN_AUTO,
    SK_LEN_NONE,   /* max-width: none */
    SK_LEN_NORMAL, /* gap, letter-spacing */
    SK_LEN_RECT,   /* the element's rect key at run time; px its manifest value */
    SK_LEN_VALUE,  /* pct comes from the widget at run time: a bar's fill, a sprite's frame */
} sk_len_kind;

typedef struct {
    float px, pct;
    uint8_t kind; /* sk_len_kind */
} sk_css_len;

/* sRGB, not premultiplied, each channel 0 to 1. currentcolor is
 * resolved before a colour reaches a style. */
typedef struct {
    float r, g, b, a;
} sk_css_color;

typedef enum { SK_TIMING_LINEAR, SK_TIMING_CUBIC, SK_TIMING_STEPS } sk_timing_kind;
typedef enum { SK_STEP_JUMP_END, SK_STEP_JUMP_START, SK_STEP_JUMP_NONE, SK_STEP_JUMP_BOTH } sk_step_pos;

/* An easing function: ease is CUBIC .25 .1 .25 1, steps(40) STEPS 40
 * JUMP_END. */
typedef struct {
    uint8_t kind, steps_pos;
    uint16_t steps;
    float x1, y1, x2, y2;
} sk_css_timing;

typedef struct {
    sk_css_color color;
    sk_css_len pos; /* SK_LEN_AUTO where the stop gave none */
} sk_css_stop;

typedef enum { SK_GRAD_LINEAR, SK_GRAD_RADIAL } sk_grad_kind;
typedef enum {
    SK_EXTENT_FARTHEST_CORNER,
    SK_EXTENT_CLOSEST_SIDE,
    SK_EXTENT_CLOSEST_CORNER,
    SK_EXTENT_FARTHEST_SIDE,
    SK_EXTENT_EXPLICIT,
} sk_extent;
/* to_corner: 0, or the corner a "to top right" names, bit 1 right,
 * bit 2 bottom, bit 4 set; its angle depends on the box. */
typedef struct {
    uint8_t kind, repeating, circle, extent, to_corner;
    float angle;        /* linear: degrees, 180 is to bottom */
    sk_css_len rx, ry;  /* radial, SK_EXTENT_EXPLICIT: the radii */
    sk_css_len cx, cy;  /* radial: the centre, from the top left */
    const sk_css_stop *stops;
    uint16_t nstops;
} sk_css_gradient;

typedef enum { SK_IMAGE_NONE, SK_IMAGE_URL, SK_IMAGE_GRADIENT } sk_image_kind;
typedef struct {
    uint8_t kind;
    uint32_t source;                  /* SK_IMAGE_URL: index into the pack's image sources */
    const sk_css_gradient *gradient; /* SK_IMAGE_GRADIENT */
} sk_css_image;

/* A background position: an offset from the edge named, so "right 10px"
 * and 100% both stay exact. */
typedef enum { SK_EDGE_START, SK_EDGE_END } sk_edge;
typedef struct {
    sk_css_len x, y;
    uint8_t x_edge, y_edge; /* sk_edge: left or right, top or bottom */
} sk_css_position;

typedef enum { SK_BGSIZE_EXPLICIT, SK_BGSIZE_COVER, SK_BGSIZE_CONTAIN } sk_bgsize_kind;
typedef struct {
    uint8_t kind;
    sk_css_len w, h; /* SK_BGSIZE_EXPLICIT; either may be SK_LEN_AUTO */
} sk_css_bgsize;

typedef enum { SK_REPEAT, SK_NO_REPEAT, SK_REPEAT_SPACE, SK_REPEAT_ROUND } sk_repeat;
typedef enum { SK_BOX_BORDER, SK_BOX_PADDING, SK_BOX_CONTENT } sk_box;
/* The blend modes src/gfx/draw.h composites. */
typedef enum {
    SK_MIX_NORMAL,
    SK_MIX_MULTIPLY,
    SK_MIX_SCREEN,
    SK_MIX_OVERLAY,
    SK_MIX_DARKEN,
    SK_MIX_LIGHTEN,
    SK_MIX_PLUS_LIGHTER,
} sk_mix;

typedef struct {
    float x, y, blur, spread; /* px */
    sk_css_color color;
    bool inset;
} sk_css_shadow;

typedef enum { SK_XF_TRANSLATE, SK_XF_ROTATE, SK_XF_SCALE, SK_XF_SKEW, SK_XF_MATRIX } sk_xf_kind;
/* One transform function. translate keeps its lengths, whose
 * percentages are of the element's own border box. rotate: v[0]
 * degrees. scale: v[0], v[1]. skew: v[0], v[1] degrees. matrix: v[0..5]
 * as CSS's matrix(a, b, c, d, e, f), e and f in px. */
typedef struct {
    uint8_t kind;
    sk_css_len tx, ty;
    float v[6];
} sk_css_xf;

typedef enum {
    SK_FX_BLUR,
    SK_FX_BRIGHTNESS,
    SK_FX_CONTRAST,
    SK_FX_SATURATE,
    SK_FX_GRAYSCALE,
    SK_FX_SEPIA,
    SK_FX_INVERT,
    SK_FX_OPACITY,
    SK_FX_HUE_ROTATE,
    SK_FX_DROP_SHADOW,
} sk_fx_kind;
/* One filter function: amount is blur's radius in px, hue-rotate's
 * degrees, else the factor (1.3 for 130%). drop-shadow uses shadow. */
typedef struct {
    uint8_t kind;
    float amount;
    sk_css_shadow shadow;
} sk_css_filter;

/* Generated content: text, or the ordinal of a list's row, base +
 * step * row (from 0), for counter() inside a list's rows. COUNTER,
 * counter(text), is skinc's until it resolves it: a pack never holds
 * one. */
typedef enum { SK_CONTENT_TEXT, SK_CONTENT_ORDINAL, SK_CONTENT_COUNTER } sk_content_kind;
typedef struct {
    uint8_t kind;
    const char *text;
    int32_t base, step;
} sk_css_content;

/* counter-reset and counter-increment: skinc resolves counters into
 * content, so a pack keeps them only for completeness. */
typedef struct {
    const char *name;
    int32_t value;
} sk_css_counter;

typedef enum { SK_TRACK_PX, SK_TRACK_FR, SK_TRACK_AUTO } sk_track_kind;
typedef struct {
    uint8_t kind;
    sk_css_len len; /* SK_TRACK_PX */
    float fr;
} sk_css_track;

typedef enum { SK_LINE_AUTO, SK_LINE_NUMBER, SK_LINE_SPAN } sk_line_kind;
typedef struct {
    uint8_t kind;
    int16_t n;
} sk_css_line;

typedef enum {
    SK_DIR_NORMAL,
    SK_DIR_REVERSE,
    SK_DIR_ALTERNATE,
    SK_DIR_ALTERNATE_REVERSE,
} sk_anim_dir;
typedef enum { SK_FILL_MODE_NONE, SK_FILL_MODE_FORWARDS, SK_FILL_MODE_BACKWARDS, SK_FILL_MODE_BOTH } sk_fill_mode;

/* One animation, its longhands paired as CSS pairs them, the shorter
 * lists repeated (sk_style_anim). keyframes indexes the pack's
 * keyframes, SK_PACK_NONE for none. */
typedef struct {
    uint32_t keyframes;
    float duration, delay; /* seconds */
    float iterations;      /* INFINITY for infinite */
    sk_css_timing timing;
    uint8_t direction, fill, paused;
} sk_css_anim;

/* One transition: the property it eases (an sk_prop, or SK_PROP_ALL),
 * paired as animations are (sk_style_transition). */
typedef struct {
    uint16_t prop;
    float duration, delay;
    sk_css_timing timing;
} sk_css_transition;

/* ---- Keywords ---- */

typedef enum { SK_DISPLAY_NONE, SK_DISPLAY_BLOCK, SK_DISPLAY_INLINE, SK_DISPLAY_INLINE_BLOCK, SK_DISPLAY_FLEX, SK_DISPLAY_GRID } sk_display;
typedef enum { SK_POS_STATIC, SK_POS_RELATIVE, SK_POS_ABSOLUTE, SK_POS_FIXED } sk_position;
typedef enum { SK_FLOAT_NONE, SK_FLOAT_LEFT, SK_FLOAT_RIGHT } sk_float;
typedef enum { SK_OVERFLOW_VISIBLE, SK_OVERFLOW_HIDDEN, SK_OVERFLOW_CLIP } sk_overflow;
typedef enum { SK_VIS_VISIBLE, SK_VIS_HIDDEN, SK_VIS_COLLAPSE } sk_visibility;
typedef enum { SK_BORDER_NONE, SK_BORDER_HIDDEN, SK_BORDER_SOLID } sk_border_style;
typedef enum { SK_FLEX_ROW, SK_FLEX_ROW_REVERSE, SK_FLEX_COLUMN, SK_FLEX_COLUMN_REVERSE } sk_flex_dir;
/* justify-content, align-items and align-self share these. */
typedef enum {
    SK_ALIGN_NORMAL,
    SK_ALIGN_AUTO, /* align-self only */
    SK_ALIGN_STRETCH,
    SK_ALIGN_FLEX_START,
    SK_ALIGN_FLEX_END,
    SK_ALIGN_START,
    SK_ALIGN_END,
    SK_ALIGN_CENTER,
    SK_ALIGN_BASELINE,
    SK_ALIGN_SPACE_BETWEEN,
    SK_ALIGN_SPACE_AROUND,
    SK_ALIGN_SPACE_EVENLY,
} sk_align;
typedef enum { SK_TT_NONE, SK_TT_UPPERCASE, SK_TT_LOWERCASE, SK_TT_CAPITALIZE } sk_text_transform;
typedef enum { SK_TA_START, SK_TA_END, SK_TA_LEFT, SK_TA_RIGHT, SK_TA_CENTER } sk_text_align;
typedef enum { SK_WS_NORMAL, SK_WS_NOWRAP, SK_WS_PRE } sk_white_space;
typedef enum { SK_TO_CLIP, SK_TO_ELLIPSIS } sk_text_overflow;
enum { SK_DECO_UNDERLINE = 1, SK_DECO_OVERLINE = 2, SK_DECO_LINE_THROUGH = 4 };
typedef enum { SK_ANCHOR_START, SK_ANCHOR_MIDDLE, SK_ANCHOR_END } sk_text_anchor;
typedef enum { SK_FSTYLE_NORMAL, SK_FSTYLE_ITALIC, SK_FSTYLE_OBLIQUE } sk_font_style;
typedef enum { SK_LH_NORMAL, SK_LH_NUMBER, SK_LH_PX } sk_lh_kind;
typedef enum { SK_PE_AUTO, SK_PE_NONE } sk_pointer_events;
typedef enum { SK_CURSOR_AUTO, SK_CURSOR_DEFAULT, SK_CURSOR_POINTER, SK_CURSOR_TEXT } sk_cursor;
typedef enum { SK_RENDER_AUTO, SK_RENDER_SMOOTH, SK_RENDER_PIXELATED } sk_image_rendering;
typedef enum { SK_LINECAP_BUTT, SK_LINECAP_ROUND, SK_LINECAP_SQUARE } sk_linecap;
typedef enum { SK_LINEJOIN_MITER, SK_LINEJOIN_ROUND, SK_LINEJOIN_BEVEL } sk_linejoin;
typedef enum { SK_PAINT_NONE, SK_PAINT_COLOR } sk_paint_kind;
typedef enum { SK_BI_STRETCH, SK_BI_REPEAT, SK_BI_ROUND, SK_BI_SPACE } sk_bi_repeat;

/* Sides and corners, in CSS's order. */
enum { SK_TOP, SK_RIGHT, SK_BOTTOM, SK_LEFT };
enum { SK_TOP_LEFT, SK_TOP_RIGHT, SK_BOTTOM_RIGHT, SK_BOTTOM_LEFT };

/* ---- The style ---- */

/* The computed values of one element in one state. The comments name
 * the CSS properties each field holds. */
typedef struct {
    /* display, position, float, box-sizing, visibility, opacity,
     * z-index, pointer-events, cursor, overflow-x, overflow-y */
    uint8_t display, position, float_, box_sizing, visibility, pointer_events, cursor;
    uint8_t overflow_x, overflow_y;
    bool z_auto;
    int32_t z_index;
    float opacity;
    uint8_t mix_blend; /* mix-blend-mode, sk_mix */
    bool isolate;      /* isolation: isolate */

    /* top right bottom left; width height; min-*; max-* */
    sk_css_len inset[4];
    sk_css_len width, height, min_width, min_height, max_width, max_height;
    sk_css_len margin[4], padding[4];

    /* border-*-width (px), border-*-style, border-*-color */
    float border_width[4];
    uint8_t border_style[4];
    sk_css_color border_color[4];
    /* border-*-radius: [corner][0] horizontal, [corner][1] vertical */
    sk_css_len radius[4][2];
    /* border-image: source, slice (numbers in image pixels, or
     * percentages when slice_pct), fill, width (number = times the
     * border width when width_num), outset, repeat */
    sk_css_image bi_source;
    float bi_slice[4];
    uint8_t bi_slice_pct; /* bit i: slice[i] is a percentage */
    bool bi_fill;
    sk_css_len bi_width[4];
    uint8_t bi_width_num; /* bit i: width[i] is a number, a multiple of the border width */
    float bi_outset[4];
    uint8_t bi_repeat[2];
    /* outline-width (px), outline-style, outline-color, outline-offset */
    float outline_width, outline_offset;
    uint8_t outline_style;
    sk_css_color outline_color;

    /* flex-direction, flex-grow, flex-shrink, flex-basis,
     * justify-content, align-items, align-self, row-gap, column-gap */
    uint8_t flex_direction, justify_content, align_items, align_self;
    float flex_grow, flex_shrink;
    sk_css_len flex_basis; /* SK_LEN_AUTO for auto */
    sk_css_len row_gap, column_gap;
    /* grid-template-columns, grid-template-rows, grid-auto-rows,
     * grid-auto-columns, grid-column-start/-end, grid-row-start/-end */
    const sk_css_track *grid_columns, *grid_rows;
    uint16_t ngrid_columns, ngrid_rows;
    sk_css_track grid_auto_rows, grid_auto_columns;
    sk_css_line grid_column[2], grid_row[2];

    /* transform, transform-origin, filter, clip-path (a polygon of
     * x, y pairs, none when nclip is 0), image-rendering */
    const sk_css_xf *transform;
    uint16_t ntransform;
    sk_css_len origin[2];
    const sk_css_filter *filter;
    uint16_t nfilter;
    const sk_css_len *clip; /* 2 * nclip lengths */
    uint16_t nclip;
    uint8_t clip_evenodd;
    uint8_t image_rendering;

    /* background-color, and the layers: background-image,
     * -position, -size, -repeat (x, y), -blend-mode, -origin, -clip.
     * As in CSS the image list sets the count, the others repeat. */
    sk_css_color bg_color;
    const sk_css_image *bg_image;
    const sk_css_position *bg_position;
    const sk_css_bgsize *bg_size;
    const uint8_t *bg_repeat; /* 2 per layer */
    const uint8_t *bg_blend, *bg_origin, *bg_clip;
    uint16_t nbg_image, nbg_position, nbg_size, nbg_repeat, nbg_blend, nbg_origin, nbg_clip;
    /* box-shadow */
    const sk_css_shadow *box_shadow;
    uint16_t nbox_shadow;

    /* color, font-family (resolved: font indexes the pack's faces, the
     * embedded DejaVu first; synth bit 1 bold, bit 2 italic),
     * font-size (px), font-weight, font-style, line-height,
     * letter-spacing (px), text-transform, text-align, white-space,
     * text-overflow, text-decoration-line, -color, text-shadow,
     * text-anchor */
    sk_css_color color;
    const char *const *font_family;
    uint16_t nfont_family;
    uint16_t font;
    uint8_t font_synth;
    float font_size;
    uint16_t font_weight;
    uint8_t font_style;
    uint8_t line_height_kind; /* sk_lh_kind */
    float line_height;        /* a factor, or px */
    float letter_spacing;
    uint8_t text_transform, text_align, white_space, text_overflow, text_decoration, text_anchor;
    sk_css_color text_decoration_color;
    const sk_css_shadow *text_shadow;
    uint16_t ntext_shadow;
    /* content: none when ncontent is 0 and content_none; "" is one
     * empty text part */
    bool content_none;
    const sk_css_content *content;
    uint16_t ncontent;
    const sk_css_counter *counter_reset, *counter_increment;
    uint16_t ncounter_reset, ncounter_increment;

    /* SVG: fill, stroke, stroke-width, stroke-dasharray, stroke-linecap,
     * stroke-linejoin */
    uint8_t fill_kind, stroke_kind;
    sk_css_color fill, stroke;
    sk_css_len stroke_width;
    const sk_css_len *dasharray;
    uint16_t ndasharray;
    uint8_t linecap, linejoin;

    /* animation-name (keyframes indexes, SK_PACK_NONE for none),
     * -duration and -delay (seconds), -timing-function,
     * -iteration-count (INFINITY for infinite), -direction,
     * -fill-mode, -play-state (1 paused): CSS's longhand lists, each
     * of one item at least */
    const uint32_t *anim_name;
    const float *anim_duration, *anim_delay, *anim_iterations;
    const sk_css_timing *anim_timing;
    const uint8_t *anim_direction, *anim_fill, *anim_paused;
    uint16_t nanim_name, nanim_duration, nanim_delay, nanim_iterations, nanim_timing, nanim_direction, nanim_fill,
        nanim_paused;
    /* transition-property (sk_prop or SK_PROP_ALL), -duration, -delay,
     * -timing-function */
    const uint16_t *tr_property;
    const float *tr_duration, *tr_delay;
    const sk_css_timing *tr_timing;
    uint16_t ntr_property, ntr_duration, ntr_delay, ntr_timing;
} sk_css_style;

/* Every field at its initial value, currentcolor as current: what a
 * pack's style holds for each property its record leaves out, current
 * being the style's own colour. */
void sk_style_initial(sk_css_style *s, sk_css_color current);

/* Animation i of a style, below nanim_name, its other longhands
 * repeated as CSS repeats the shorter lists. */
static inline sk_css_anim sk_style_anim(const sk_css_style *s, uint16_t i)
{
    sk_css_anim a;
    a.keyframes = s->anim_name[i];
    a.duration = s->anim_duration[i % s->nanim_duration];
    a.delay = s->anim_delay[i % s->nanim_delay];
    a.iterations = s->anim_iterations[i % s->nanim_iterations];
    a.timing = s->anim_timing[i % s->nanim_timing];
    a.direction = s->anim_direction[i % s->nanim_direction];
    a.fill = s->anim_fill[i % s->nanim_fill];
    a.paused = s->anim_paused[i % s->nanim_paused];
    return a;
}

/* Transition i, below ntr_property, likewise. */
static inline sk_css_transition sk_style_transition(const sk_css_style *s, uint16_t i)
{
    sk_css_transition t;
    t.prop = s->tr_property[i];
    t.duration = s->tr_duration[i % s->ntr_duration];
    t.delay = s->tr_delay[i % s->ntr_delay];
    t.timing = s->tr_timing[i % s->ntr_timing];
    return t;
}

/* ---- Properties ---- */

/* Every longhand a pack can carry, with the id the STYL section
 * names it by. The numbers are the format's: a new property takes the
 * next number and a minor version. */
typedef enum {
    SK_PROP_DISPLAY = 1,
    SK_PROP_POSITION,
    SK_PROP_FLOAT,
    SK_PROP_BOX_SIZING,
    SK_PROP_TOP,
    SK_PROP_RIGHT,
    SK_PROP_BOTTOM,
    SK_PROP_LEFT,
    SK_PROP_WIDTH,
    SK_PROP_HEIGHT,
    SK_PROP_MIN_WIDTH,
    SK_PROP_MIN_HEIGHT,
    SK_PROP_MAX_WIDTH,
    SK_PROP_MAX_HEIGHT,
    SK_PROP_MARGIN_TOP,
    SK_PROP_MARGIN_RIGHT,
    SK_PROP_MARGIN_BOTTOM,
    SK_PROP_MARGIN_LEFT,
    SK_PROP_PADDING_TOP,
    SK_PROP_PADDING_RIGHT,
    SK_PROP_PADDING_BOTTOM,
    SK_PROP_PADDING_LEFT,
    SK_PROP_BORDER_TOP_WIDTH,
    SK_PROP_BORDER_RIGHT_WIDTH,
    SK_PROP_BORDER_BOTTOM_WIDTH,
    SK_PROP_BORDER_LEFT_WIDTH,
    SK_PROP_BORDER_TOP_STYLE,
    SK_PROP_BORDER_RIGHT_STYLE,
    SK_PROP_BORDER_BOTTOM_STYLE,
    SK_PROP_BORDER_LEFT_STYLE,
    SK_PROP_BORDER_TOP_COLOR,
    SK_PROP_BORDER_RIGHT_COLOR,
    SK_PROP_BORDER_BOTTOM_COLOR,
    SK_PROP_BORDER_LEFT_COLOR,
    SK_PROP_BORDER_TOP_LEFT_RADIUS,
    SK_PROP_BORDER_TOP_RIGHT_RADIUS,
    SK_PROP_BORDER_BOTTOM_RIGHT_RADIUS,
    SK_PROP_BORDER_BOTTOM_LEFT_RADIUS,
    SK_PROP_BORDER_IMAGE_SOURCE,
    SK_PROP_BORDER_IMAGE_SLICE,
    SK_PROP_BORDER_IMAGE_WIDTH,
    SK_PROP_BORDER_IMAGE_OUTSET,
    SK_PROP_BORDER_IMAGE_REPEAT,
    SK_PROP_OUTLINE_WIDTH,
    SK_PROP_OUTLINE_STYLE,
    SK_PROP_OUTLINE_COLOR,
    SK_PROP_OUTLINE_OFFSET,
    SK_PROP_OVERFLOW_X,
    SK_PROP_OVERFLOW_Y,
    SK_PROP_Z_INDEX,
    SK_PROP_VISIBILITY,
    SK_PROP_OPACITY,
    SK_PROP_POINTER_EVENTS,
    SK_PROP_CURSOR,
    SK_PROP_MIX_BLEND_MODE,
    SK_PROP_ISOLATION,
    SK_PROP_CONTAIN, /* none alone: what containment would change is not supported */
    SK_PROP_FLEX_DIRECTION,
    SK_PROP_FLEX_WRAP,
    SK_PROP_FLEX_GROW,
    SK_PROP_FLEX_SHRINK,
    SK_PROP_FLEX_BASIS,
    SK_PROP_JUSTIFY_CONTENT,
    SK_PROP_ALIGN_ITEMS,
    SK_PROP_ALIGN_SELF,
    SK_PROP_ROW_GAP,
    SK_PROP_COLUMN_GAP,
    SK_PROP_GRID_TEMPLATE_COLUMNS,
    SK_PROP_GRID_TEMPLATE_ROWS,
    SK_PROP_GRID_AUTO_ROWS,
    SK_PROP_GRID_AUTO_COLUMNS,
    SK_PROP_GRID_COLUMN_START,
    SK_PROP_GRID_COLUMN_END,
    SK_PROP_GRID_ROW_START,
    SK_PROP_GRID_ROW_END,
    SK_PROP_TRANSFORM,
    SK_PROP_TRANSFORM_ORIGIN,
    SK_PROP_FILTER,
    SK_PROP_CLIP_PATH,
    SK_PROP_IMAGE_RENDERING,
    SK_PROP_BACKGROUND_COLOR,
    SK_PROP_BACKGROUND_IMAGE,
    SK_PROP_BACKGROUND_POSITION,
    SK_PROP_BACKGROUND_SIZE,
    SK_PROP_BACKGROUND_REPEAT,
    SK_PROP_BACKGROUND_BLEND_MODE,
    SK_PROP_BACKGROUND_ORIGIN,
    SK_PROP_BACKGROUND_CLIP,
    SK_PROP_BOX_SHADOW,
    SK_PROP_COLOR,
    SK_PROP_FONT_FAMILY,
    SK_PROP_FONT_SIZE,
    SK_PROP_FONT_WEIGHT,
    SK_PROP_FONT_STYLE,
    SK_PROP_LINE_HEIGHT,
    SK_PROP_LETTER_SPACING,
    SK_PROP_TEXT_TRANSFORM,
    SK_PROP_TEXT_ALIGN,
    SK_PROP_WHITE_SPACE,
    SK_PROP_TEXT_OVERFLOW,
    SK_PROP_TEXT_DECORATION_LINE,
    SK_PROP_TEXT_DECORATION_COLOR,
    SK_PROP_TEXT_SHADOW,
    SK_PROP_TEXT_ANCHOR,
    SK_PROP_CONTENT,
    SK_PROP_COUNTER_RESET,
    SK_PROP_COUNTER_INCREMENT,
    SK_PROP_FILL,
    SK_PROP_STROKE,
    SK_PROP_STROKE_WIDTH,
    SK_PROP_STROKE_DASHARRAY,
    SK_PROP_STROKE_LINECAP,
    SK_PROP_STROKE_LINEJOIN,
    SK_PROP_ANIMATION_NAME,
    SK_PROP_ANIMATION_DURATION,
    SK_PROP_ANIMATION_TIMING_FUNCTION,
    SK_PROP_ANIMATION_DELAY,
    SK_PROP_ANIMATION_ITERATION_COUNT,
    SK_PROP_ANIMATION_DIRECTION,
    SK_PROP_ANIMATION_FILL_MODE,
    SK_PROP_ANIMATION_PLAY_STATE,
    SK_PROP_TRANSITION_PROPERTY,
    SK_PROP_TRANSITION_DURATION,
    SK_PROP_TRANSITION_TIMING_FUNCTION,
    SK_PROP_TRANSITION_DELAY,
    SK_PROP_USER_SELECT, /* accepted, nothing to do: a skin has no selection */
    SK_PROP_COUNT,
    SK_PROP_ALL = 0xFFFF, /* transition-property: all */
} sk_prop;

/* ---- State ---- */

/* What changes an element's classes while the app runs: the runtime
 * sets these, and a state table maps them to a style. A variable
 * belongs to one element, its owner; the classes it gives are those of
 * decision 0037. */
typedef enum {
    SK_VAR_HOVER,    /* 2: under the pointer, itself or a descendant */
    SK_VAR_NODATA,   /* 2: .nodata: a widget, a .resets box */
    SK_VAR_OPEN,     /* 2: .open: a readout whose dial is open */
    SK_VAR_FALLBACK, /* 2: .fallback: a bar or readout showing its fallback */
    SK_VAR_ON,       /* 2: .on: a segment, a lamp, a toggle's option */
    SK_VAR_LIST,     /* 3: the stage: none, .checklist, .checklist-off */
    SK_VAR_ITEM,     /* 3: a list's row: .todo, .doing, .done */
    SK_VAR_COUNT,    /* 1 + SK_PACK_MAX_LIMITS: a limits widget's .n-<count>, 0 also .nodata */
    SK_VAR_MODEL,    /* 2: a limit's meter: .account, .model */
    SK_VAR_SPAN,     /* the span classes + 2: .spend, each .span-<x> the CSS names, any other */
    SK_VAR_COUNT_OF_VARS,
} sk_var;

/* One input of an element's state table: a variable of an element. */
typedef struct {
    uint32_t owner;
    uint8_t var, radix;
} sk_pack_input;

/* ---- Elements ---- */

typedef enum {
    SK_EL_HOST, /* the root, matched by :host alone */
    SK_EL_LINK, /* the two stylesheet links before the stage, display none */
    SK_EL_DIV,
    SK_EL_SPAN,
    SK_EL_SVG,
    SK_EL_G,
    SK_EL_PATH,
    SK_EL_LINE,
    SK_EL_CIRCLE,
    SK_EL_TEXT,
    SK_EL_IMAGE,
    SK_EL_BEFORE, /* ::before of its parent */
    SK_EL_AFTER,  /* ::after of its parent */
} sk_el_kind;

/* What an element is to the runtime: which value it shows, which
 * geometry it takes. Parts are named as widgets.js names their
 * classes. */
typedef enum {
    SK_ROLE_NONE,
    SK_ROLE_STAGE,
    SK_ROLE_PANEL,
    SK_ROLE_WIDGET,       /* a widget's root */
    SK_ROLE_METER,        /* a limits widget's meter: the root of a bar or needle */
    SK_ROLE_TOGGLE,       /* a list's toggle */
    SK_ROLE_LABEL,
    SK_ROLE_SUFFIX,       /* a meter's label suffix */
    SK_ROLE_VALUE,        /* a readout's, needle's or knob's .value */
    SK_ROLE_RESETS,
    SK_ROLE_RESETS_WORD,
    SK_ROLE_RESETS_LEFT,
    SK_ROLE_TRACK,        /* a bar's track */
    SK_ROLE_FILL,         /* a bar's continuous fill */
    SK_ROLE_SEG,          /* a bar's segment */
    SK_ROLE_LAMP,
    SK_ROLE_SHEET,
    SK_ROLE_DIAL,         /* a gauge's or needle's svg */
    SK_ROLE_ARC_TRACK,    /* a gauge's or needle's track path */
    SK_ROLE_ARC_FILL,     /* a gauge's fill path */
    SK_ROLE_TICK,
    SK_ROLE_MARK,
    SK_ROLE_DIAL_TEXT,    /* a gauge's text.value */
    SK_ROLE_FACE,
    SK_ROLE_NEEDLE_GROUP, /* turned by the value */
    SK_ROLE_NEEDLE,
    SK_ROLE_PIVOT,
    SK_ROLE_KNOB_BOX,
    SK_ROLE_KNOB_TURN,    /* turned by the value */
    SK_ROLE_KNOB_IMAGE,
    SK_ROLE_KNOB_FACE,
    SK_ROLE_KNOB_POINTER,
    SK_ROLE_ITEMS,
    SK_ROLE_ITEM,         /* a list's row, repeated for each item */
    SK_ROLE_ITEM_MARK,
    SK_ROLE_ITEM_TEXT,
    SK_ROLE_OPTION_LIST,
    SK_ROLE_OPTION_OTHER,
} sk_role;

/* SVG geometry in the viewBox's units, as widgets.js draws it. */
typedef enum {
    SK_SVG_NONE,
    SK_SVG_VIEWBOX, /* v: x, y, w, h; flag: 1 preserveAspectRatio none, else xMidYMid meet */
    SK_SVG_ARC,     /* v: cx, cy, r, a0, a1 (degrees clockwise from 12); flag: pathLength 100 */
    SK_SVG_LINE,    /* v: x1, y1, x2, y2 */
    SK_SVG_CIRCLE,  /* v: cx, cy, r */
    SK_SVG_TEXT,    /* v: x, y */
    SK_SVG_IMAGE,   /* v: x, y, w, h; image: the source */
    SK_SVG_TURN,    /* v: cx, cy, the centre the value turns the group about */
} sk_svg_kind;

typedef struct {
    uint8_t kind, flag;
    float v[5];
    uint32_t image;
} sk_pack_svg;

/* Where a text comes from. */
typedef enum {
    SK_TEXT_NONE,
    SK_TEXT_STATIC,   /* text */
    SK_TEXT_VALUE,    /* the widget's value, formatted */
    SK_TEXT_LABEL,    /* the label: text, or the fallback's or the limit's at run time */
    SK_TEXT_RESETS,   /* the time to the reset */
    SK_TEXT_ITEM_MARK,
    SK_TEXT_ITEM_TEXT,
} sk_text_source;

/* An element. The elements are skinc's, in its order: the tree as
 * skin.js and widgets.js build it, then the pseudo-elements. Among a
 * parent's children its ::before comes first and its ::after last, as
 * their boxes do. */
typedef struct {
    uint32_t parent, first_child, next_sibling; /* SK_PACK_NONE when absent */
    uint8_t kind, role;
    uint32_t widget;  /* the widget it belongs to, or SK_PACK_NONE */
    uint32_t rect;    /* a panel's, widget's or toggle's rect, or SK_PACK_NONE */
    uint32_t svg;     /* SK_PACK_NONE, or its geometry */
    /* A limits widget's meter and everything in it is live only while
     * the widget has `group` meters; 0 for every other element. */
    uint8_t group, meter;
    uint8_t text_source;
    const char *text; /* SK_TEXT_STATIC, SK_TEXT_LABEL's default */
    /* The state table: ninputs inputs, the first least significant;
     * styles has the product of their radices entries. */
    const sk_pack_input *inputs;
    uint8_t ninputs;
    const uint32_t *styles;
    /* Layers skinc pre-rendered for this element, by state style. */
    uint32_t layer, nlayers;
} sk_pack_el;

/* ---- Widgets ---- */

typedef enum {
    SK_WIDGET_GAUGE,
    SK_WIDGET_NEEDLE,
    SK_WIDGET_KNOB,
    SK_WIDGET_BAR,
    SK_WIDGET_LED,
    SK_WIDGET_READOUT,
    SK_WIDGET_SPRITE,
    SK_WIDGET_LIST,
    SK_WIDGET_LIMITS,
} sk_widget_type;

typedef enum {
    SK_FORMAT_INT,
    SK_FORMAT_FIXED, /* digits 0 to 4 */
    SK_FORMAT_PCT,
    SK_FORMAT_USD,
    SK_FORMAT_TOKENS,
    SK_FORMAT_DURATION,
    SK_FORMAT_UNTIL,
    SK_FORMAT_BYTES,
    SK_FORMAT_TEXT,
} sk_format;

/* A rect of decision 0006: the keys in validate.js's order, left,
 * right, width, top, bottom, height; has bit i when key i is given;
 * follow[i] the split whose change key i takes, or -1. */
typedef struct {
    float v[6];
    uint8_t has;
    int8_t follow[6];
} sk_pack_rect;

typedef struct {
    const char *bind; /* NULL for a fixed value */
    float value;
    const char *klass;
} sk_pack_mark;

typedef struct {
    uint8_t type, format, digits;
    const char *id, *bind, *label, *prefix, *suffix, *resets;
    const char *fallback_bind, *fallback_label, *fallback_resets; /* fallback_bind NULL: none */
    float min, max, damping, start_angle, end_angle, thickness, gap;
    float on, pulse_ms, blink, fps;
    uint16_t ticks, segments, frames, rows;
    float frame_w, frame_h;
    bool has_on, pulse, vertical, column, run, needle_meters, across;
    const sk_pack_mark *marks;
    uint16_t nmarks;
    uint32_t el;                  /* the root element */
    uint32_t toggle;              /* a list's toggle root, or SK_PACK_NONE */
    uint32_t face, image, sheet;  /* image sources, or SK_PACK_NONE */
} sk_pack_widget;

typedef enum { SK_SPLIT_LEFT, SK_SPLIT_RIGHT, SK_SPLIT_TOP, SK_SPLIT_BOTTOM } sk_split_edge;
typedef struct {
    const char *id;
    uint8_t edge;
    float min, max;
} sk_pack_split;

/* ---- Keyframes ---- */

/* A keyframe sets transform, opacity or both; what it leaves out is
 * interpolated from the frames around it, or the element's own value. */
typedef struct {
    float offset; /* 0 to 1 */
    bool has_transform, has_opacity;
    const sk_css_xf *transform;
    uint16_t ntransform;
    float opacity;
    sk_css_timing timing; /* animation-timing-function inside the keyframe, or the animation's */
    bool has_timing;
} sk_pack_key;

typedef struct {
    const char *name;
    const sk_pack_key *keys;
    uint16_t nkeys;
} sk_pack_keyframes;

/* ---- Images, layers, fonts, sounds ---- */

/* An image a style or widget names: a file of the skin, decoded by
 * skinc. Each variant is the image at one size: its own for a raster
 * file, and for an SVG each size it is drawn at, at 1x and 2x. */
typedef struct {
    uint16_t w, h;
    uint8_t scale; /* 0: the file's own pixels; 1 or 2: drawn at that scale */
    uint32_t blob; /* QOI bytes in BLOB */
    uint32_t len;
} sk_pack_variant;

typedef struct {
    const char *path; /* relative to the skin, for messages */
    uint16_t w, h;    /* the file's own size in CSS pixels */
    const sk_pack_variant *variants;
    uint16_t nvariants;
} sk_pack_image;

/* A pre-rendered layer: an element (and what it holds) painted by
 * skinc at one scale. x and y place its top left corner relative to
 * the element's border box (an SVG shape's or group's: its svg's), in
 * CSS pixels; w and h are device pixels.
 *
 * STATIC: the element and its subtree as painted in every state.
 * STATE: the same in the states whose table entry is style.
 * SPRITE: the subtree without the transform the runtime applies (a
 * knob's turning group, an animated element), turned about the
 * origin ox, oy (CSS pixels from x, y).
 * NINE: the element's box shadows and borders as a nine-slice whose
 * fixed edges are slice[] device pixels, for a box that stretches,
 * painted for a box of ox by oy CSS pixels.
 * MASK: an alpha mask, the clip-path or rounded corners a dynamic
 * child is clipped to. skinc writes none (skinc/layers.c says why);
 * the runtime passes them over.
 * The opacity and blend of a layer's element are applied as the
 * runtime composites it, never painted in. */
typedef enum { SK_LAYER_STATIC, SK_LAYER_STATE, SK_LAYER_SPRITE, SK_LAYER_NINE, SK_LAYER_MASK } sk_layer_kind;
typedef struct {
    uint32_t el, style;
    uint8_t kind, scale;
    float x, y, ox, oy;
    uint16_t w, h;
    uint16_t slice[4];
    uint32_t blob, len; /* QOI */
} sk_pack_layer;

/* A font of the manifest: TrueType bytes skinc's checker passed. */
typedef struct {
    const char *family;
    uint16_t weight;
    uint8_t style; /* sk_font_style */
    const uint8_t *data;
    uint32_t len;
} sk_pack_font;

/* A sound of the manifest (decision 0024), mono IMA ADPCM: blocks of
 * block_bytes, each a 4-byte header and 4-bit samples, as WAVE's
 * format 0x11 lays them out for one channel. */
typedef enum { SK_SOUND_LOOP, SK_SOUND_RANDOM } sk_sound_mode;
typedef struct {
    const char *path;
    uint8_t mode;
    float volume, interval[2];
    uint32_t rate, frames;
    uint16_t block_bytes;
    const uint8_t *data;
    uint32_t len;
} sk_pack_sound;

/* ---- The pack ---- */

/* The manifest's fields the runtime needs. theme holds the 23 colours
 * of validate.js's THEME_KEYS order, has_theme which of them were
 * given; font is the terminal's font (a pack font, or SK_PACK_NONE for
 * DejaVu Sans Mono). */
typedef struct {
    const char *name, *version, *author, *description, *license;
    float width, height, min_width, min_height;
    sk_pack_rect menu_rect;
    uint32_t menu_image, menu_hover, menu_pressed; /* image sources; SK_PACK_NONE */
    sk_pack_rect terminal_rect;
    uint32_t terminal_font;
    float terminal_font_size;
    uint32_t theme[23]; /* 0xAARRGGBB */
    uint32_t has_theme;
} sk_pack_meta;

typedef struct {
    sk_pack_meta meta;
    const sk_pack_split *splits;
    uint32_t nsplits;
    const sk_pack_rect *rects;
    uint32_t nrects;
    const sk_pack_widget *widgets;
    uint32_t nwidgets;
    const sk_pack_el *els; /* els[0] is the host, the stage its only displayed child */
    uint32_t nels;
    const sk_pack_svg *svgs;
    uint32_t nsvgs;
    const sk_css_style *styles;
    uint32_t nstyles;
    const char *const *spans; /* the span classes SK_VAR_SPAN tells apart */
    uint32_t nspans;
    const sk_pack_keyframes *keyframes;
    uint32_t nkeyframes;
    const sk_pack_image *images;
    uint32_t nimages;
    const sk_pack_layer *layers;
    uint32_t nlayers;
    const sk_pack_font *fonts;
    uint32_t nfonts;
    const sk_pack_sound *sounds;
    uint32_t nsounds;
    const uint8_t *blob; /* BLOB, which variants and layers index */
    uint32_t nblob;
    unsigned char source_hash[32]; /* SHA-256 of the skin's files, the cache key (skin/compile.h) */
    void *mem_;                    /* what the loader allocated */
} sk_pack;

/* Reads a pack of n bytes, which must outlive it: its strings, fonts,
 * sounds and BLOB are pointers into it. Everything else is in one
 * allocation, which sk_pack_free frees. False with why in err (err_len
 * bytes, which may be 0), naming the section and the record, for a pack
 * that fails any check of decision 0037; *out is then empty. */
bool sk_pack_load(const uint8_t *data, size_t n, sk_pack *out, char *err, size_t err_len);
void sk_pack_free(sk_pack *p);

/* The style element e takes when the variables of its inputs hold
 * values[i]: values has e->ninputs entries, each below its radix. */
static inline const sk_css_style *sk_pack_style_of(const sk_pack *p, const sk_pack_el *e, const uint8_t *values)
{
    uint32_t at = 0, stride = 1;
    for (uint8_t i = 0; i < e->ninputs; i++) {
        at += values[i] * stride;
        stride *= e->inputs[i].radix;
    }
    return &p->styles[e->styles[at]];
}

#endif
