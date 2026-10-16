#include "internal.h"

#include "../base/utf8.h"

#include <math.h>

/* Beyond this a position no longer fits an int: such glyphs are laid out
 * and not drawn. */
#define FAR 1e9f

/* C0, DEL and C1. */
static bool control(uint32_t cp)
{
    return cp < 0x20 || (cp >= 0x7F && cp < 0xA0);
}

/* The run laid out from pen 0, each glyph drawn at x + pen, its pen
 * rounded to a quarter pixel, when dst is not NULL. */
static float run(sk_image *dst, sk_glyph_cache *cache, sk_fontset *set, int style, float px, float x, float y,
                 const char *s, size_t n, sk_color c, const sk_rect *clip)
{
    if (!s || !(px > 0) || !isfinite(px))
        return 0;
    bool draw = dst && isfinite(x) && fabsf(y) < FAR;
    int baseline = draw ? (int)floorf(y + 0.5f) : 0;
    float pen = 0;
    const sk_font *prev = NULL;
    uint32_t prev_glyph = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, NULL);
        if (control(cp))
            continue;
        sk_glyph_ref ref = sk_fontset_lookup(set, cp, style);
        if (ref.font == prev)
            pen += sk_font_kern(ref.font, prev_glyph, ref.glyph, px);
        float quarters = floorf((x + pen) * 4.0f + 0.5f);
        if (draw && fabsf(quarters) < FAR) {
            float whole = floorf(quarters / 4.0f);
            const sk_glyph *g = sk_glyph_get(cache, ref, px, (int)(quarters - whole * 4.0f));
            if (g->mask.w > 0)
                sk_fill_mask(dst, (int)whole + g->left, baseline + g->top, &g->mask, c, clip);
        }
        pen += sk_font_advance(ref.font, ref.glyph, px);
        prev = ref.font;
        prev_glyph = ref.glyph;
    }
    return pen;
}

float sk_draw_text(sk_image *dst, sk_glyph_cache *cache, sk_fontset *set, int style, float px, float x, float y,
                   const char *utf8, size_t n, sk_color c, const sk_rect *clip)
{
    return run(dst, cache, set, style, px, x, y, utf8, n, c, clip);
}

float sk_measure_text(sk_fontset *set, int style, float px, const char *utf8, size_t n)
{
    return run(NULL, NULL, set, style, px, 0, 0, utf8, n, 0, NULL);
}
