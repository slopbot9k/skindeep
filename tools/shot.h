/* A shipped skin drawn whole, as tools/skinshot and the skin golden
 * tests draw it: compiled by skinc's front end, laid out at its
 * window's size in its default state (every variable 0), painted at a
 * scale with its images decoded by skinc's decoders. */
#ifndef SK_TOOLS_SHOT_H
#define SK_TOOLS_SHOT_H

#include "../skinc/render.h"

typedef struct {
    sk_front front;
    sk_render_tree tree;
    sk_render_fonts fonts;
    sk_lfonts lfonts;
    sk_layout layout;
    sk_pimages images;
    sk_image *decoded; /* per image, decoded on first use */
    uint8_t *tried;
    sk_glyph_cache *glyphs;
    float width, height; /* the skin area */
    sk_frect terminal;   /* the terminal's rect in it */
    uint32_t terminal_bg; /* the theme's background, 0xAARRGGBB */
} sk_shot;

/* The state drawn, beyond the default: each limits widget showing
 * count meters (0: none, the widget hidden), and each list its row
 * with the stage's .checklist, the row's text given. */
typedef struct {
    uint8_t count;
    const char *row;
} sk_shot_state;

/* Compiles and lays out the skin at dir in state st (NULL: the
 * default); false with the messages in d. */
bool sk_shot_load(sk_shot *s, const char *dir, const sk_shot_state *st, sk_diags *d);
/* The skin at scale into a new image (ceil(width * scale) by
 * ceil(height * scale)) over opaque black, and the terminal's
 * background over its rect when terminal is true. */
void sk_shot_paint(sk_shot *s, float scale, bool terminal, sk_image *out);
/* The same of the skin area's CSS rectangle area alone. */
void sk_shot_paint_area(sk_shot *s, sk_frect area, float scale, bool terminal, sk_image *out);
void sk_shot_free(sk_shot *s);

#endif
