/* The initial values of decision 0037's properties: what skinc's
 * cascade starts each style from, and what the pack loader gives every
 * property a style's record leaves out. */
#include "pack.h"

#include <string.h>

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
    /* content: normal, which is none on ::before and ::after. */
    s->content_none = true;
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
