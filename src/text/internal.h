/* What the files of src/text share: the face behind sk_font and the
 * system's fonts as the font sets walk them. */
#ifndef SK_TEXT_INTERNAL_H
#define SK_TEXT_INTERNAL_H

#include "text.h"

#include "../../third_party/stb/stb_truetype.h"

struct sk_font {
    stbtt_fontinfo info;
    /* info without its GPOS, so the kern table answers when GPOS has
     * no pair: stb_truetype reads one or the other. */
    stbtt_fontinfo kern;
    void *map; /* the file's mapping, or NULL when data is the caller's */
    size_t map_size;
    sk_font_info meta;
    char legacy_family[64]; /* name ID 1, which CSS matches as well */
    int width;              /* OS/2 usWidthClass: 5 is normal */
    int upem;
    int ascent, descent, line_gap; /* font units, descent positive */
};

/* Pixels per font unit at an em of px pixels. */
static inline float sk_text_scale(const sk_font *f, float px)
{
    return px / (float)f->upem;
}

/* Whether a face is bold or italic as style asks; what is not, the
 * glyph cache fakes. */
int sk_text_synth(const sk_font *f, int style);

/* Face i of fs, opened on first use; NULL when it no longer opens. */
const sk_font *sk_text_fonts_face(sk_fonts *fs, size_t i);
/* Whether face i of fs belongs to family, by either of its names. */
bool sk_text_fonts_is_family(const sk_fonts *fs, size_t i, const char *family);
bool sk_text_name_eq(const char *a, const char *b);

/* What a glyph cache holds, for the tests: the bytes of its masks and
 * entries, and how many entries. */
size_t sk_text_cache_bytes(const sk_glyph_cache *c);
size_t sk_text_cache_count(const sk_glyph_cache *c);

#endif
