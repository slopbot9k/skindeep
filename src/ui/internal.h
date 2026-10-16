/* What the files of src/ui share: the 2D canvas xterm.js's texture
 * atlas draws a cell on, and the custom glyphs drawn onto it. */
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

#endif
