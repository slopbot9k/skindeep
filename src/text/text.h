/* Text (decision 0036): faces read by stb_truetype (TrueType and CFF
 * outlines, collections, kerning), DejaVu Sans Mono and DejaVu Sans
 * embedded in the program, the system's fonts discovered for fallback,
 * and a glyph cache of coverage masks. No shaping: one character, one
 * glyph, pair kerning within a face.
 *
 * Faces are immutable once open, so any thread may look glyphs up or
 * read metrics. A font set, its fallback cache and a glyph cache belong
 * to the thread that made them (the UI thread). stb_truetype trusts its
 * input: open only the embedded faces, the system's fonts, and skin
 * fonts skinc has checked. */
#ifndef SK_TEXT_H
#define SK_TEXT_H

#include "../gfx/draw.h"

/* ---- Faces ---- */

typedef struct sk_font sk_font;

/* How many faces data holds: 1 for a font file, n for a collection
 * (.ttc), 0 for anything else. */
int sk_font_faces(const uint8_t *data, size_t n);
/* Face index of data, which must outlive the face. NULL when it is not
 * a face stb_truetype can draw: no Unicode cmap, or no outlines. */
sk_font *sk_font_open(const uint8_t *data, size_t n, int index);
/* The same from a file, which the face reads (or maps) and owns. */
sk_font *sk_font_open_file(const char *path, int index);
void sk_font_close(sk_font *f);

/* From the name and OS/2 tables. weight is OS/2's 100..900. */
typedef struct {
    char family[64], style[64];
    int weight;
    bool italic, mono;
} sk_font_info;
void sk_font_info_of(const sk_font *f, sk_font_info *out);

/* The glyph for codepoint cp, 0 when the face has none. */
uint32_t sk_font_glyph(const sk_font *f, uint32_t cp);

/* At an em of px pixels: ascent above the baseline and descent below
 * it, both positive, and the gap between lines. */
typedef struct {
    float ascent, descent, line_gap;
} sk_font_vmetrics;
void sk_font_vmetrics_at(const sk_font *f, float px, sk_font_vmetrics *out);
float sk_font_advance(const sk_font *f, uint32_t glyph, float px);
/* The kerning between two glyphs of the face (kern or GPOS pairs). */
float sk_font_kern(const sk_font *f, uint32_t left, uint32_t right, float px);

/* The embedded faces, opened on first use and never closed. */
typedef enum {
    SK_FONT_MONO,
    SK_FONT_MONO_BOLD,
    SK_FONT_MONO_OBLIQUE,
    SK_FONT_MONO_BOLD_OBLIQUE,
    SK_FONT_SANS,
    SK_FONT_SANS_BOLD,
    SK_FONT_EMBEDDED_COUNT,
} sk_embedded_font;
const sk_font *sk_font_embedded(sk_embedded_font which);

/* ---- The system's fonts ---- */

/* Style bits. */
enum { SK_STYLE_REGULAR = 0, SK_STYLE_BOLD = 1, SK_STYLE_ITALIC = 2 };

/* Every face under the font directories, read once: family, style and
 * file, with no glyph data held. */
typedef struct sk_fonts sk_fonts;

/* dirs: NULL-terminated, searched recursively; NULL for the system's:
 * on Linux /usr/share/fonts, /usr/local/share/fonts,
 * ~/.local/share/fonts and ~/.fonts; on macOS /System/Library/Fonts,
 * /Library/Fonts and ~/Library/Fonts. Unreadable files are skipped. */
sk_fonts *sk_fonts_scan(const char *const *dirs);
void sk_fonts_free(sk_fonts *fs);
size_t sk_fonts_count(const sk_fonts *fs);
const sk_font_info *sk_fonts_info(const sk_fonts *fs, size_t i);
/* The face of family (ASCII case-insensitive) nearest to style, opened
 * and owned by fs; NULL when no face has that family. */
const sk_font *sk_fonts_find(sk_fonts *fs, const char *family, int style);

/* ---- Font sets ---- */

/* A stack as CSS's font-family names it: the families asked for, in
 * order; then the embedded DejaVu Sans Mono (mono) or DejaVu Sans; then
 * the system's faces, for a character none of those has. */
typedef struct sk_fontset sk_fontset;

/* fs may be NULL: the embedded faces alone. families: NULL-terminated,
 * may be NULL. fs outlives the set. */
sk_fontset *sk_fontset_new(sk_fonts *fs, const char *const *families, bool mono);
/* The same with face first in every style, ahead of families: a font
 * a skin pack carries, which fs does not list. Its bold and italic are
 * the glyph cache's. face may be NULL, and outlives the set. */
sk_fontset *sk_fontset_new_face(sk_fonts *fs, const sk_font *face, const char *const *families, bool mono);
void sk_fontset_free(sk_fontset *set);

/* What draws a character: the face and glyph, and the style the face
 * lacks, which the glyph cache fakes (SK_STYLE_BOLD by emboldening,
 * SK_STYLE_ITALIC by slanting). */
typedef struct {
    const sk_font *font;
    uint32_t glyph;
    int synth;
} sk_glyph_ref;

/* The first face of the stack with a glyph for cp, in style when the
 * family has that style. Never fails: the last resort is the embedded
 * face's missing-glyph box (glyph 0). Fallback lookups are cached. */
sk_glyph_ref sk_fontset_lookup(sk_fontset *set, uint32_t cp, int style);
/* The stack's first face in style: its metrics are the line's. */
const sk_font *sk_fontset_primary(sk_fontset *set, int style);

/* ---- The glyph cache ---- */

typedef struct sk_glyph_cache sk_glyph_cache;

/* A rasterized glyph: its mask's top-left corner is at (pen x + left,
 * baseline y + top), so top is negative for ink above the baseline. A
 * blank glyph has an empty mask. */
typedef struct {
    sk_mask mask;
    int left, top;
} sk_glyph;

/* At most about max_bytes of masks; the least recently used go first. */
sk_glyph_cache *sk_glyph_cache_new(size_t max_bytes);
void sk_glyph_cache_free(sk_glyph_cache *c);
/* ref's glyph at an em of px pixels, its pen subpixel/4 of a pixel right
 * of a whole pixel (0..3), synth applied. Valid until the next call on
 * the cache. */
const sk_glyph *sk_glyph_get(sk_glyph_cache *c, sk_glyph_ref ref, float px, int subpixel);

/* ---- Runs of text ---- */

/* n bytes of UTF-8 from the pen at (x, baseline y), each character from
 * set in style at px, kerned within a face, in c. Invalid UTF-8 draws
 * U+FFFD; control characters draw nothing and do not advance. Returns
 * the advance in pixels. */
float sk_draw_text(sk_image *dst, sk_glyph_cache *cache, sk_fontset *set, int style, float px, float x, float y,
                   const char *utf8, size_t n, sk_color c, const sk_rect *clip);
/* The advance sk_draw_text would return, drawing nothing. */
float sk_measure_text(sk_fontset *set, int style, float px, const char *utf8, size_t n);

#endif
