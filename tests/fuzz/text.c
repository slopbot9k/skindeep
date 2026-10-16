/* Runs of text: any bytes measure as they draw, to a finite advance, and
 * drawing touches nothing outside the clip. The first three bytes pick
 * the set, the style, the size and the pen; the rest is the text. Fonts
 * themselves are not fuzzed: only trusted ones are opened (decision
 * 0036). */
#include "../../src/text/text.h"
#include "fuzz.h"

#include <math.h>
#include <stdlib.h>

const char *const sk_fuzz_dict[] = {
    "AV",           "To",           "\t",           "\x7f",         "\xC2\x85",         "\xC3\xA9",
    "\xCC\x81",     "\xD0\x96",     "\xE2\x80\x8B", "\xE2\x94\xBC", "\xEF\xBF\xBD",     "\xE4\xB8\xAD",
    "\xF0\x9F\x98\x80", "\xF4\x8F\xBF\xBD", "\xED\xA0\x80", "\xE0\x80", "\xF4\x90", NULL,
};

#define W 64
#define H 24
#define CANARY 0x12345678u

static sk_fontset *sets[2];
static sk_glyph_cache *cache;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3)
        return 0;
    if (!cache) {
        sets[0] = sk_fontset_new(NULL, NULL, true);
        sets[1] = sk_fontset_new(NULL, NULL, false);
        /* Small, so glyphs come and go. */
        cache = sk_glyph_cache_new(32 * 1024);
    }
    sk_fontset *set = sets[data[0] & 1];
    int style = (data[0] >> 1) & 3;
    float px = 1.0f + (float)data[1] * 0.25f;
    float x = (float)(int8_t)data[2] * 0.75f, y = (float)(data[0] >> 3) * 1.5f;
    const char *s = (const char *)data + 3;
    size_t n = size - 3;

    float measured = sk_measure_text(set, style, px, s, n);
    if (!isfinite(measured))
        abort();

    static sk_px pixels[W * H];
    for (size_t i = 0; i < SK_LEN(pixels); i++)
        pixels[i] = CANARY;
    sk_image im = {pixels, W, H, W, false};
    sk_rect clip = sk_rect_make(8, 4, 40, 14);
    float drawn = sk_draw_text(&im, cache, set, style, px, x, y, s, n, SK_RGBA(200, 100, 50, 220), &clip);
    if (drawn != measured)
        abort();
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++)
            if ((i < clip.x || i >= clip.x + clip.w || j < clip.y || j >= clip.y + clip.h) &&
                pixels[j * W + i] != CANARY)
                abort();
    return 0;
}
