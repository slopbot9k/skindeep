#include "internal.h"

#include "../base/paths.h"
#include "../base/utf8.h"

#include <string.h>

/* Faces a stack holds per style, the embedded one included. */
#define CHAIN_MAX 16
/* The lookup cache starts over rather than grow past this. */
#define CACHE_MAX ((size_t)1 << 18)

typedef struct {
    uint32_t key; /* (cp << 2 | style) + 1; 0 for an empty slot */
    sk_glyph_ref ref;
} slot;

struct sk_fontset {
    sk_fonts *fs;
    bool mono;
    const sk_font *chain[4][CHAIN_MAX];
    int len[4];
    /* The system's faces in the order fallback tries them, made on the
     * first character the stack lacks. */
    size_t *order, order_len;
    bool ordered;
    slot *slots;
    size_t cap, used;
};

/* Faces fallback tries first, before any other with the glyph: each
 * system's faces for symbols, CJK and mathematics. */
static const char *const mac_prefs[] = {
    "Menlo", "Apple Symbols", "PingFang SC", "Hiragino Sans", "Apple SD Gothic Neo", "STIX Two Math", NULL,
};
static const char *const linux_prefs[] = {
    "DejaVu Sans",          "Noto Sans Mono",   "Noto Sans Mono CJK SC", "Noto Sans CJK SC",
    "Noto Sans CJK JP",     "Noto Sans CJK KR", "Noto Sans CJK TC",      "Noto Sans Symbols",
    "Noto Sans Symbols 2",  "Noto Sans Math",   NULL,
};

static const sk_font *embedded_for(bool mono, int style)
{
    static const sk_embedded_font mono_faces[4] = {SK_FONT_MONO, SK_FONT_MONO_BOLD, SK_FONT_MONO_OBLIQUE,
                                                   SK_FONT_MONO_BOLD_OBLIQUE};
    static const sk_embedded_font sans_faces[4] = {SK_FONT_SANS, SK_FONT_SANS_BOLD, SK_FONT_SANS,
                                                   SK_FONT_SANS_BOLD};
    return sk_font_embedded((mono ? mono_faces : sans_faces)[style & 3]);
}

/* The names the embedded faces answer to, CSS's generic families among
 * them: 1 for DejaVu Sans Mono, 2 for DejaVu Sans, 0 for neither. */
static int embedded_family(const char *name)
{
    static const char *const mono[] = {"DejaVu Sans Mono", "monospace", "ui-monospace"};
    static const char *const sans[] = {"DejaVu Sans",   "sans-serif",    "system-ui",
                                       "ui-sans-serif", "-apple-system", "BlinkMacSystemFont"};
    for (size_t i = 0; i < SK_LEN(mono); i++)
        if (sk_text_name_eq(name, mono[i]))
            return 1;
    for (size_t i = 0; i < SK_LEN(sans); i++)
        if (sk_text_name_eq(name, sans[i]))
            return 2;
    return 0;
}

static void push(sk_fontset *s, int style, const sk_font *f, int limit)
{
    if (!f || s->len[style] >= limit)
        return;
    for (int i = 0; i < s->len[style]; i++)
        if (s->chain[style][i] == f)
            return;
    s->chain[style][s->len[style]++] = f;
}

sk_fontset *sk_fontset_new(sk_fonts *fs, const char *const *families, bool mono)
{
    return sk_fontset_new_face(fs, NULL, families, mono);
}

sk_fontset *sk_fontset_new_face(sk_fonts *fs, const sk_font *face, const char *const *families, bool mono)
{
    sk_fontset *s = sk_xcalloc(1, sizeof *s);
    s->fs = fs;
    s->mono = mono;
    for (int style = 0; style < 4; style++) {
        push(s, style, face, CHAIN_MAX - 1);
        for (const char *const *name = families; name && *name; name++) {
            if (!**name)
                continue;
            int e = embedded_family(*name);
            push(s, style, e ? embedded_for(e == 1, style) : sk_fonts_find(fs, *name, style), CHAIN_MAX - 1);
        }
        push(s, style, embedded_for(mono, style), CHAIN_MAX);
    }
    return s;
}

void sk_fontset_free(sk_fontset *set)
{
    if (!set)
        return;
    free(set->order);
    free(set->slots);
    free(set);
}

const sk_font *sk_fontset_primary(sk_fontset *set, int style)
{
    return set->chain[style & 3][0];
}

/* Characters drawn as nothing: spaces and the default-ignorable ones,
 * whose glyphs are empty in every face. */
static bool blank(uint32_t cp)
{
    return cp == 0x20 || cp == 0xA0 || cp == 0xAD || cp == 0x34F || cp == 0x61C || cp == 0x115F || cp == 0x1160 ||
           cp == 0x1680 || (cp >= 0x17B4 && cp <= 0x17B5) || (cp >= 0x180B && cp <= 0x180F) ||
           (cp >= 0x2000 && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202F) || (cp >= 0x205F && cp <= 0x206F) ||
           cp == 0x3000 || cp == 0x3164 || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF || cp == 0xFFA0 ||
           (cp >= 0xFFF0 && cp <= 0xFFF8) || (cp >= 0xE0000 && cp <= 0xE0FFF);
}

/* A face of the system's draws cp only with an outline for it: some
 * map characters they cannot draw to empty glyphs. */
static bool drawable(const sk_font *f, uint32_t glyph, uint32_t cp)
{
    return blank(cp) || !stbtt_IsGlyphEmpty(&f->info, (int)glyph);
}

static int by_key(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* The preferred families' faces in the list's order, then every other
 * face, then the hidden ones whose family begins with a dot (macOS's
 * own UI faces); in a mono set the monospaced faces of each group come
 * first. Ties keep the scan's order. */
static void build_order(sk_fontset *s)
{
    s->ordered = true;
    size_t n = sk_fonts_count(s->fs);
    if (!n)
        return;
    const char *const *prefs = sk_os_current() == SK_OS_MACOS ? mac_prefs : linux_prefs;
    uint64_t *keys = sk_xreallocarray(NULL, n, sizeof *keys);
    for (size_t i = 0; i < n; i++) {
        const sk_font_info *in = sk_fonts_info(s->fs, i);
        uint64_t pref = 0;
        while (prefs[pref] && !sk_text_fonts_is_family(s->fs, i, prefs[pref]))
            pref++;
        uint64_t group = prefs[pref] ? 0 : in->family[0] == '.' ? 2 : 1;
        uint64_t prop = s->mono && !in->mono;
        keys[i] = group << 60 | prop << 56 | pref << 40 | (uint64_t)i;
    }
    qsort(keys, n, sizeof *keys, by_key);
    s->order = sk_xreallocarray(NULL, n, sizeof *s->order);
    for (size_t i = 0; i < n; i++)
        s->order[i] = (size_t)(keys[i] & (((uint64_t)1 << 40) - 1));
    s->order_len = n;
    free(keys);
}

/* The first system face with an outline for cp, then its family's face
 * nearest to style when that one has the outline too. */
static bool fallback(sk_fontset *s, uint32_t cp, int style, sk_glyph_ref *out)
{
    if (!s->fs)
        return false;
    if (!s->ordered)
        build_order(s);
    for (size_t k = 0; k < s->order_len; k++) {
        size_t i = s->order[k];
        const sk_font *f = sk_text_fonts_face(s->fs, i);
        uint32_t g = f ? sk_font_glyph(f, cp) : 0;
        if (!g || !drawable(f, g, cp))
            continue;
        const sk_font *styled = sk_fonts_find(s->fs, sk_fonts_info(s->fs, i)->family, style);
        uint32_t sg = styled && styled != f ? sk_font_glyph(styled, cp) : 0;
        if (sg && drawable(styled, sg, cp)) {
            f = styled;
            g = sg;
        }
        out->font = f;
        out->glyph = g;
        out->synth = sk_text_synth(f, style);
        return true;
    }
    return false;
}

/* The stack in style, then the stack's faces in the styles nearest it,
 * faked into style: DejaVu Sans Mono Oblique lacks glyphs its upright
 * face has. Then the system's faces. */
static sk_glyph_ref resolve(sk_fontset *s, uint32_t cp, int style)
{
    sk_glyph_ref ref;
    const int near[4] = {style, style & SK_STYLE_BOLD, style & SK_STYLE_ITALIC, SK_STYLE_REGULAR};
    unsigned walked = 0;
    for (int k = 0; k < 4; k++) {
        if (walked & 1u << near[k])
            continue;
        walked |= 1u << near[k];
        for (int i = 0; i < s->len[near[k]]; i++) {
            const sk_font *f = s->chain[near[k]][i];
            uint32_t g = sk_font_glyph(f, cp);
            if (g) {
                ref.font = f;
                ref.glyph = g;
                ref.synth = sk_text_synth(f, style);
                return ref;
            }
        }
    }
    if (fallback(s, cp, style, &ref))
        return ref;
    /* The missing-glyph box of the stack's own embedded face. */
    ref.font = s->chain[style][s->len[style] - 1];
    ref.glyph = 0;
    ref.synth = sk_text_synth(ref.font, style);
    return ref;
}

static slot *find_slot(slot *slots, size_t cap, uint32_t key)
{
    size_t i = (key * 2654435761u) & (cap - 1);
    while (slots[i].key && slots[i].key != key)
        i = (i + 1) & (cap - 1);
    return &slots[i];
}

static void grow(sk_fontset *s)
{
    size_t cap = s->cap ? s->cap * 2 : 256;
    if (cap > CACHE_MAX) {
        memset(s->slots, 0, s->cap * sizeof *s->slots);
        s->used = 0;
        return;
    }
    slot *slots = sk_xcalloc(cap, sizeof *slots);
    for (size_t i = 0; i < s->cap; i++)
        if (s->slots[i].key)
            *find_slot(slots, cap, s->slots[i].key) = s->slots[i];
    free(s->slots);
    s->slots = slots;
    s->cap = cap;
}

sk_glyph_ref sk_fontset_lookup(sk_fontset *set, uint32_t cp, int style)
{
    style &= 3;
    if (cp > 0x10FFFF)
        cp = SK_UTF8_REPLACEMENT;
    uint32_t key = (cp << 2 | (uint32_t)style) + 1;
    if (set->cap) {
        slot *hit = find_slot(set->slots, set->cap, key);
        if (hit->key)
            return hit->ref;
    }
    sk_glyph_ref ref = resolve(set, cp, style);
    if ((set->used + 1) * 4 > set->cap * 3)
        grow(set);
    slot *sl = find_slot(set->slots, set->cap, key);
    sl->key = key;
    sl->ref = ref;
    set->used++;
    return ref;
}
