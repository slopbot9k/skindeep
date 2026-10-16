/* The font checker of decision 0037 in front of stb_truetype: any bytes
 * the checker passes open, and stb_truetype then reads everything the
 * app would ask of a skin's font: glyphs for codes of every plane,
 * advances, kerning by GPOS and kern, outlines, and bitmaps at three
 * sizes, upright, bold and slanted, at each subpixel offset. The input
 * is copied to a buffer of its own size, so a read past the font is
 * the sanitizer's to see, and an assertion of the rasterizer aborts.
 * What must hold beside: a font that fails says why, one that passes
 * opens, and its glyphs are below its count. */
#include "../../src/text/fontcheck.h"
#include "../../src/text/internal.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "glyf", "loca", "head", "hhea", "hmtx", "maxp", "cmap", "kern", "GPOS", "name", "OS/2", "post", "CFF ",
    "\x00\x01\x00\x00", "\x5f\x0f\x3c\xf5", "\x00\x03\x00\x01", "\x00\x03\x00\x0a", "\x00\x04", "\x00\x0c",
    "\x00\x06", "\xff\xff", "\x80\x00", "\x7f\xff", "\x00\x22", "\x00\x83", "\x40\x00", "\x00\x00\x00\x00", NULL,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *font = malloc(size ? size : 1);
    if (!font)
        abort();
    if (size)
        memcpy(font, data, size);
    char err[200];
    if (!sk_font_check(font, size, err, sizeof err)) {
        if (!err[0])
            abort();
        free(font);
        return 0;
    }
    sk_font *f = sk_font_open(font, size, 0);
    if (!f)
        abort();
    uint32_t glyphs = (uint32_t)f->info.numGlyphs;
    static const uint32_t far[] = {0xFFFD, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF, 0x110000};
    for (uint32_t cp = 0; cp < 0x300 + SK_LEN(far); cp++) {
        uint32_t g = sk_font_glyph(f, cp < 0x300 ? cp : far[cp - 0x300]);
        if (g >= glyphs)
            abort();
    }
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(f, 13, &vm);
    sk_font_info info;
    sk_font_info_of(f, &info);
    uint32_t some = glyphs < 64 ? glyphs : 64;
    for (uint32_t a = 0; a < some && a < 24; a++)
        for (uint32_t b = 0; b < some && b < 24; b++)
            sk_font_kern(f, a, b, 13);
    sk_glyph_cache *cache = sk_glyph_cache_new(1 << 22);
    static const float sizes[] = {1, 9.5f, 31};
    for (uint32_t g = 0; g < some; g++) {
        sk_font_advance(f, g, 13);
        stbtt_vertex *v = NULL;
        stbtt_GetGlyphShape(&f->info, (int)g, &v);
        stbtt_FreeShape(&f->info, v);
        for (size_t s = 0; s < SK_LEN(sizes); s++)
            for (int style = 0; style < 4; style++)
                sk_glyph_get(cache, (sk_glyph_ref){f, g, style}, sizes[s], (int)(g + s) & 3);
    }
    sk_glyph_cache_free(cache);
    sk_font_close(f);
    free(font);
    return 0;
}
