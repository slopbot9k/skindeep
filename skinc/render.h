/* The front end's tree as the layout takes it (src/skin/layout.h): the
 * elements of one state of the variables, the stage at the root, each
 * rect where the window's size and the splits put it, the widgets'
 * images among the stylesheet's, and the skin's faces. What skinc lays
 * out and paints its layers from, and what tools/skinshot draws. */
#ifndef SKINC_RENDER_H
#define SKINC_RENDER_H

#include "../src/skin/paint.h"
#include "front.h"

/* The state drawn: the variables as sk_front_style takes them, whether
 * a list shows its row (once), and the splits' saved sizes (NaN or NULL
 * for their defaults). */
typedef struct {
    const sk_assign *assign;
    size_t nassign;
    bool rows;
    const double *saved;
} sk_render_state;

typedef struct {
    sk_lnode *nodes;
    uint32_t n, root; /* root: the stage */
    float width, height;
    sk_pack_rect *rects; /* per node, where it has one */
    sk_pack_svg *svgs;   /* per node */
    /* The images by index: the stylesheet's (sk_front's images), then
     * the widgets' faces and knob images; each a path or a data: url. */
    SK_VEC(const char *) images;
    sk_arena arena;
} sk_render_tree;

/* The tree of f in state st, laid out in a skin area w by h CSS px. */
void sk_render_tree_build(sk_render_tree *t, const sk_front *f, const sk_render_state *st, float w, float h);
void sk_render_tree_free(sk_render_tree *t);

/* The skin's faces as sk_lfonts gives them: the embedded DejaVu, then
 * the manifest's fonts opened from the skin's directory. */
typedef struct {
    sk_font **faces; /* the manifest's, NULL where one did not open */
    size_t n;
} sk_render_fonts;

void sk_render_fonts_open(sk_render_fonts *rf, const sk_front *f);
void sk_render_fonts_close(sk_render_fonts *rf);
const sk_font *sk_render_face(void *data, uint16_t index);

#endif
