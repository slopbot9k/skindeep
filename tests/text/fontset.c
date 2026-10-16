#include "../../src/text/text.h"
#include "../test.h"

static void check_ref(sk_glyph_ref r, const sk_font *font, bool has_glyph, int synth)
{
    CHECK(r.font == font);
    CHECK((r.glyph != 0) == has_glyph);
    CHECK_INT(r.synth, synth);
}

static const sk_font *emb(sk_embedded_font w)
{
    return sk_font_embedded(w);
}

static void test_embedded_only(void)
{
    sk_fontset *mono = sk_fontset_new(NULL, NULL, true), *sans = sk_fontset_new(NULL, NULL, false);
    check_ref(sk_fontset_lookup(mono, 'A', SK_STYLE_REGULAR), emb(SK_FONT_MONO), true, 0);
    check_ref(sk_fontset_lookup(mono, 'A', SK_STYLE_BOLD), emb(SK_FONT_MONO_BOLD), true, 0);
    check_ref(sk_fontset_lookup(mono, 'A', SK_STYLE_ITALIC), emb(SK_FONT_MONO_OBLIQUE), true, 0);
    check_ref(sk_fontset_lookup(mono, 'A', SK_STYLE_BOLD | SK_STYLE_ITALIC), emb(SK_FONT_MONO_BOLD_OBLIQUE), true,
              0);
    check_ref(sk_fontset_lookup(sans, 'A', SK_STYLE_REGULAR), emb(SK_FONT_SANS), true, 0);
    check_ref(sk_fontset_lookup(sans, 'A', SK_STYLE_BOLD), emb(SK_FONT_SANS_BOLD), true, 0);
    /* DejaVu Sans has no oblique here: it is faked. */
    check_ref(sk_fontset_lookup(sans, 'A', SK_STYLE_ITALIC), emb(SK_FONT_SANS), true, SK_STYLE_ITALIC);
    check_ref(sk_fontset_lookup(sans, 'A', SK_STYLE_BOLD | SK_STYLE_ITALIC), emb(SK_FONT_SANS_BOLD), true,
              SK_STYLE_ITALIC);
    CHECK(sk_fontset_primary(mono, SK_STYLE_REGULAR) == emb(SK_FONT_MONO));
    CHECK(sk_fontset_primary(mono, SK_STYLE_BOLD) == emb(SK_FONT_MONO_BOLD));
    CHECK(sk_fontset_primary(sans, SK_STYLE_ITALIC) == emb(SK_FONT_SANS));
    /* Nothing has it: the embedded face's missing-glyph box. */
    check_ref(sk_fontset_lookup(mono, 0x10FFFD, SK_STYLE_REGULAR), emb(SK_FONT_MONO), false, 0);
    check_ref(sk_fontset_lookup(sans, 0x10FFFD, SK_STYLE_ITALIC), emb(SK_FONT_SANS), false, SK_STYLE_ITALIC);
    /* Past Unicode is U+FFFD. */
    sk_glyph_ref r = sk_fontset_lookup(mono, 0x7FFFFFFF, SK_STYLE_REGULAR);
    CHECK(r.glyph == sk_font_glyph(emb(SK_FONT_MONO), 0xFFFD) && r.glyph != 0);
    sk_fontset_free(mono);
    sk_fontset_free(sans);
    sk_fontset_free(NULL);
}

/* A glyph the oblique face lacks comes from the upright one, slanted. */
static void test_other_style_of_the_stack(void)
{
    const sk_font *up = emb(SK_FONT_MONO), *ob = emb(SK_FONT_MONO_OBLIQUE);
    uint32_t cp = 0;
    for (uint32_t c = 0x2000; c < 0x3000 && !cp; c++)
        if (sk_font_glyph(up, c) && !sk_font_glyph(ob, c))
            cp = c;
    CHECK(cp != 0);
    sk_fontset *set = sk_fontset_new(NULL, NULL, true);
    check_ref(sk_fontset_lookup(set, cp, SK_STYLE_ITALIC), up, true, SK_STYLE_ITALIC);
    sk_fontset_free(set);
}

static void test_families(void)
{
    const char *dirs[] = {"skins/phosphor/fonts", "skins/deck/fonts", NULL};
    sk_fonts *fs = sk_fonts_scan(dirs);
    const sk_font *vt = sk_fonts_find(fs, "VT323", SK_STYLE_REGULAR),
                  *silk_bold = sk_fonts_find(fs, "Silkscreen", SK_STYLE_BOLD);
    CHECK(vt && silk_bold);
    const char *stack[] = {"No Such Family", "VT323", "monospace", NULL};
    sk_fontset *set = sk_fontset_new(fs, stack, false);
    CHECK(sk_fontset_primary(set, SK_STYLE_REGULAR) == vt);
    check_ref(sk_fontset_lookup(set, 'A', SK_STYLE_REGULAR), vt, true, 0);
    /* VT323 has neither bold nor italic. */
    check_ref(sk_fontset_lookup(set, 'A', SK_STYLE_BOLD), vt, true, SK_STYLE_BOLD);
    check_ref(sk_fontset_lookup(set, 'A', SK_STYLE_BOLD | SK_STYLE_ITALIC), vt, true,
              SK_STYLE_BOLD | SK_STYLE_ITALIC);
    /* What VT323 lacks, "monospace" has, before the sans the set ends
     * with. */
    uint32_t cp = 0x2603;
    CHECK(sk_font_glyph(vt, cp) == 0);
    check_ref(sk_fontset_lookup(set, cp, SK_STYLE_REGULAR), emb(SK_FONT_MONO), true, 0);
    check_ref(sk_fontset_lookup(set, cp, SK_STYLE_BOLD), emb(SK_FONT_MONO_BOLD), true, 0);
    sk_fontset_free(set);

    const char *silk[] = {"silkscreen", NULL};
    set = sk_fontset_new(fs, silk, true);
    check_ref(sk_fontset_lookup(set, 'A', SK_STYLE_BOLD), silk_bold, true, 0);
    check_ref(sk_fontset_lookup(set, 'A', SK_STYLE_BOLD | SK_STYLE_ITALIC), silk_bold, true, SK_STYLE_ITALIC);
    CHECK(sk_fontset_primary(set, SK_STYLE_BOLD) == silk_bold);
    /* Silkscreen has no Cyrillic: DejaVu Sans Mono has. */
    CHECK(sk_fontset_lookup(set, 0x416, SK_STYLE_REGULAR).font == emb(SK_FONT_MONO));
    sk_fontset_free(set);
    sk_fonts_free(fs);
}

/* Characters no face of the stack has, from the faces of a directory:
 * Gujarati from Shrikhand alone; the manat sign from Amatic SC and from
 * VT323, which a mono set prefers for being monospaced. */
static void test_fallback(void)
{
    const char *dirs[] = {"skins/coffee/fonts", "skins/phosphor/fonts", NULL};
    sk_fonts *fs = sk_fonts_scan(dirs);
    const sk_font *shrikhand = sk_fonts_find(fs, "Shrikhand", 0), *amatic = sk_fonts_find(fs, "Amatic SC", 0),
                  *vt = sk_fonts_find(fs, "VT323", 0);
    CHECK(shrikhand && amatic && vt);
    sk_fontset *sans = sk_fontset_new(fs, NULL, false), *mono = sk_fontset_new(fs, NULL, true);
    for (int i = 0; i < 2; i++) {
        uint32_t gu = 0x0A85;
        CHECK(sk_font_glyph(emb(SK_FONT_SANS), gu) == 0);
        check_ref(sk_fontset_lookup(sans, gu, SK_STYLE_REGULAR), shrikhand, true, 0);
        check_ref(sk_fontset_lookup(mono, gu, SK_STYLE_BOLD), shrikhand, true, SK_STYLE_BOLD);
        check_ref(sk_fontset_lookup(sans, 0x20BC, SK_STYLE_REGULAR), amatic, true, 0);
        check_ref(sk_fontset_lookup(mono, 0x20BC, SK_STYLE_REGULAR), vt, true, 0);
        /* What the stack has never falls back. */
        check_ref(sk_fontset_lookup(mono, 'A', SK_STYLE_REGULAR), emb(SK_FONT_MONO), true, 0);
        /* Nothing has it. */
        check_ref(sk_fontset_lookup(mono, 0x1F5D1, SK_STYLE_REGULAR), emb(SK_FONT_MONO), false, 0);
    }
    sk_fontset_free(sans);
    sk_fontset_free(mono);
    sk_fonts_free(fs);
}

/* Many characters, so the lookup cache grows past its first size. */
static void test_many_lookups(void)
{
    sk_fontset *set = sk_fontset_new(NULL, NULL, true);
    const sk_font *f = emb(SK_FONT_MONO);
    for (int round = 0; round < 2; round++)
        for (uint32_t cp = 0x20; cp < 0x3000; cp++) {
            sk_glyph_ref r = sk_fontset_lookup(set, cp, (int)(cp & 3));
            if (cp < 0x7f && (cp & 3) == 0 && (r.font != f || r.glyph != sk_font_glyph(f, cp)))
                sk_test_fail(__FILE__, __LINE__, "U+%04X", (unsigned)cp);
        }
    sk_fontset_free(set);
}

const sk_test sk_suite_fontset[] = {
    T(test_embedded_only),
    T(test_other_style_of_the_stack),
    T(test_families),
    T(test_fallback),
    T(test_many_lookups),
    {0},
};
