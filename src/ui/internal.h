/* What the files of src/ui share: the 2D canvas xterm.js's texture
 * atlas draws a cell on, the custom glyphs drawn onto it, and the atlas
 * of drawn cells the view copies from. */
#ifndef SK_UI_INTERNAL_H
#define SK_UI_INTERNAL_H

#include "termview.h"

/* ---- The canvas ---- */

/* A shade's repeating pattern: one bit a pixel, anchored at the
 * canvas's origin, as a CanvasPattern is. */
typedef struct {
    int w, h;
    const uint8_t *bits; /* w * h, 0 or 1 */
} sk_ui_pattern;

/* A fillStyle or strokeStyle: a colour (straight alpha), or that colour
 * where a shade's pattern has a bit. */
typedef struct {
    sk_color c;
    const sk_ui_pattern *pattern;
} sk_ui_paint;

/* What a CanvasRenderingContext2D keeps between calls, as much of it as
 * the atlas and the custom glyphs use. A clip is a rectangle, x0, y0,
 * x1, y1, anti-aliased as Chromium's is. */
typedef struct {
    sk_image im;
    sk_ui_paint fill;
    double line_width;
    bool clipped;
    double clip[4];
} sk_ui_canvas;

/* A w by h canvas filled with background, transparent when it is 0;
 * its fill is opaque black and its line width 1, as a new context's. */
void sk_ui_canvas_init(sk_ui_canvas *cv, int w, int h, sk_color background);
void sk_ui_canvas_free(sk_ui_canvas *cv);
/* Coverage cov, one byte a pixel of the canvas, painted source-over
 * within the canvas's clip and clip (x0, y0, x1, y1; NULL for none). */
void sk_ui_canvas_cover(sk_ui_canvas *cv, const uint8_t *cov, sk_ui_paint paint, const double *clip);
/* fillRect: x, y, w, h within the canvas's clip. */
void sk_ui_canvas_rect(sk_ui_canvas *cv, double x, double y, double w, double h, sk_ui_paint paint);
/* p filled (nonzero), or stroked width wide with butt caps and miter
 * joins of limit 10, a canvas's defaults. */
void sk_ui_canvas_fill(sk_ui_canvas *cv, const sk_path *p, sk_ui_paint paint, const double *clip);
void sk_ui_canvas_stroke(sk_ui_canvas *cv, const sk_path *p, double width, sk_ui_paint paint, const double *clip);

/* JavaScript's Math.round: halves go up. */
double sk_ui_js_round(double v);

/* ---- Custom glyphs ---- */

typedef enum {
    SK_UI_CUSTOM_NONE,
    SK_UI_CUSTOM_BLOCK,
    SK_UI_CUSTOM_PATTERN,
    SK_UI_CUSTOM_BOX,
    SK_UI_CUSTOM_POWERLINE,
} sk_ui_custom_kind;

sk_ui_custom_kind sk_ui_custom_kind_of(uint32_t cp);
/* cp drawn as tryDrawCustomChar draws it: the cell at (x, y), w by h
 * device pixels, in the canvas's fill. A shade leaves its pattern as the
 * fill, box drawing and Powerline leave their line width, and Powerline
 * leaves its clip to the cell, as on a canvas. false when cp has none. */
bool sk_ui_custom_draw(sk_ui_canvas *cv, uint32_t cp, double x, double y, int w, int h, double font_px,
                       double scale);

/* ---- The atlas ---- */

/* A cell drawn as the texture atlas draws it, its background taken out:
 * w by h pixels at (x, y) from the cell's corner, each opaque or 0. */
typedef struct {
    int x, y, w, h;
    sk_px *px;
} sk_ui_sprite;

typedef struct sk_ui_atlas sk_ui_atlas;

/* What the atlas draws with: the cell and character sizes in device
 * pixels, the font at font_px CSS pixels times scale, the theme. */
typedef struct {
    sk_fontset *fonts;
    sk_glyph_cache *glyphs;
    float font_px;
    double scale;
    int cell_w, cell_h, char_w, char_h;
    sk_term_palette pal;
} sk_ui_atlas_config;

sk_ui_atlas *sk_ui_atlas_new(void);
void sk_ui_atlas_free(sk_ui_atlas *a);
/* A new config forgets every sprite. */
void sk_ui_atlas_configure(sk_ui_atlas *a, const sk_ui_atlas_config *cfg);
/* Forgets every sprite when they hold more than the atlas's budget:
 * between frames, never while a frame holds sprites. */
void sk_ui_atlas_trim(sk_ui_atlas *a);
/* The sprite for cps[0..n) in a cell width cells wide with the resolved
 * attribute words fg, bg and ext (xterm.js's, the variant offset in
 * ext's top bits), drawn when first asked for; NULL when it draws
 * nothing. Valid until the next configure or trim. */
const sk_ui_sprite *sk_ui_atlas_get(sk_ui_atlas *a, const uint32_t *cps, int n, int width, uint32_t fg, uint32_t bg,
                                    uint32_t ext);
/* How many sprites and bytes the atlas holds, for the tests. */
size_t sk_ui_atlas_count(const sk_ui_atlas *a);
size_t sk_ui_atlas_bytes(const sk_ui_atlas *a);

#endif
