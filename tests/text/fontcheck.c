/* The font checker of decision 0037: the skins' fonts and a font made
 * in code pass and draw; each way of breaking the made font is refused
 * with its reason. */
#include "../../src/text/fontcheck.h"
#include "../../src/text/internal.h"
#include "../test.h"
#include "tinyfont.h"

#include <stdlib.h>

static const char *const SKIN_FONTS[] = {
    "skins/coffee/fonts/AmaticSC/AmaticSC-Bold.ttf",
    "skins/coffee/fonts/CutiveMono/CutiveMono-Regular.ttf",
    "skins/coffee/fonts/Shrikhand/Shrikhand-Regular.ttf",
    "skins/deck/fonts/DSEG7Classic/DSEG7Classic-Bold.ttf",
    "skins/deck/fonts/DSEG7Classic/DSEG7Classic-Regular.ttf",
    "skins/deck/fonts/Silkscreen/Silkscreen-Bold.ttf",
    "skins/deck/fonts/Silkscreen/Silkscreen-Regular.ttf",
    "skins/library/fonts/PermanentMarker/PermanentMarker-Regular.ttf",
    "skins/library/fonts/SpecialElite/SpecialElite-Regular.ttf",
    "skins/phosphor/fonts/VT323/VT323-Regular.ttf",
};

/* Draws every glyph of a face that passed, upright and slanted, at
 * three sizes: stb_truetype reads the outlines and rasterizes them. */
static void draw_all(const sk_font *f)
{
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 22);
    for (int g = 0; g < f->info.numGlyphs; g++)
        for (int k = 0; k < 3; k++) {
            sk_glyph_ref r = {f, (uint32_t)g, k == 2 ? SK_STYLE_ITALIC : 0};
            const sk_glyph *gl = sk_glyph_get(c, r, k ? 37.5f : 9, k);
            CHECK(gl->mask.w >= 0 && gl->mask.h >= 0);
        }
    sk_glyph_cache_free(c);
}

static void test_skin_fonts(void)
{
    for (size_t i = 0; i < SK_LEN(SKIN_FONTS); i++) {
        size_t n = 0;
        uint8_t *data = (uint8_t *)sk_test_read(SKIN_FONTS[i], &n);
        CHECK(data != NULL);
        if (!data)
            continue;
        char err[200];
        if (!sk_font_check(data, n, err, sizeof err))
            sk_test_fail(__FILE__, __LINE__, "%s %s", SKIN_FONTS[i], err);
        CHECK_STR(err, "");
        sk_font *f = sk_font_open(data, n, 0);
        CHECK(f != NULL);
        if (f) {
            CHECK(sk_font_glyph(f, 'A') != 0 || sk_font_glyph(f, '0') != 0);
            draw_all(f);
        }
        sk_font_close(f);
        free(data);
    }
}

static void test_tiny_font(void)
{
    size_t n = 0;
    uint8_t *data = sk_test_tiny_font(&n);
    char err[200];
    if (!sk_font_check(data, n, err, sizeof err))
        sk_test_fail(__FILE__, __LINE__, "%s", err);
    sk_font *f = sk_font_open(data, n, 0);
    CHECK(f != NULL);
    if (!f) {
        free(data);
        return;
    }
    sk_font_info in;
    sk_font_info_of(f, &in);
    CHECK_STR(in.family, "Tiny");
    CHECK_STR(in.style, "Regular");
    CHECK_INT(in.weight, 400);
    CHECK_INT(sk_font_glyph(f, ' '), 1);
    CHECK_INT(sk_font_glyph(f, 'A'), 2);
    CHECK_INT(sk_font_glyph(f, 'B'), 3);
    CHECK_INT(sk_font_glyph(f, 'C'), 4);
    CHECK_INT(sk_font_glyph(f, 'D'), 0);
    CHECK(sk_font_advance(f, 2, 1000) == 500);
    CHECK(sk_font_advance(f, 4, 1000) == 500);
    /* GPOS's pair, then its classes; kern's pair stands behind them. */
    CHECK(sk_font_kern(f, 2, 3, 1000) == -40);
    CHECK(sk_font_kern(f, 3, 2, 1000) == -30);
    CHECK(sk_font_kern(f, 4, 2, 1000) == -30);
    CHECK(sk_font_kern(f, 2, 2, 1000) == 0);
    /* Its glyphs draw, the compounds where stb_truetype puts them. */
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    for (uint32_t g = 0; g < 5; g++) {
        const sk_glyph *gl = sk_glyph_get(c, (sk_glyph_ref){f, g, 0}, 100, 0);
        int ink = 0;
        for (int i = 0; i < gl->mask.w * gl->mask.h; i++)
            ink += gl->mask.a[i];
        if ((g == 1) != (ink == 0))
            sk_test_fail(__FILE__, __LINE__, "glyph %u has ink %d", g, ink);
    }
    sk_glyph_cache_free(c);
    draw_all(f);
    sk_font_close(f);
    free(data);
}

/* The made font's parts, for the breakages below. */
typedef struct {
    uint8_t *d;
    size_t n;
    uint32_t glyf, loca, head, maxp, hhea, cmap, kern, gpos, name, os2, post;
} font;

static font made(void)
{
    font f;
    f.d = sk_test_tiny_font(&f.n);
    f.glyf = sk_test_font_table(f.d, "glyf", NULL);
    f.loca = sk_test_font_table(f.d, "loca", NULL);
    f.head = sk_test_font_table(f.d, "head", NULL);
    f.maxp = sk_test_font_table(f.d, "maxp", NULL);
    f.hhea = sk_test_font_table(f.d, "hhea", NULL);
    f.cmap = sk_test_font_table(f.d, "cmap", NULL);
    f.kern = sk_test_font_table(f.d, "kern", NULL);
    f.gpos = sk_test_font_table(f.d, "GPOS", NULL);
    f.name = sk_test_font_table(f.d, "name", NULL);
    f.os2 = sk_test_font_table(f.d, "OS/2", NULL);
    f.post = sk_test_font_table(f.d, "post", NULL);
    return f;
}

/* Glyphs 0, 2, 3 and 4 in glyf; cmap's subtables. */
enum { G0 = 0, G2 = 26, G3 = 68, G4 = 94, F4 = 52, F12 = 98, F6 = 138, F0 = 154 };

static void refused(font *f, const char *want, int line)
{
    char err[200];
    if (sk_font_check(f->d, f->n, err, sizeof err))
        sk_test_fail(__FILE__, line, "passed, but should fail with \"%s\"", want);
    else if (!strstr(err, want))
        sk_test_fail(__FILE__, line, "\"%s\" for \"%s\"", err, want);
    free(f->d);
}

static void passes(font *f, int line)
{
    char err[200];
    if (!sk_font_check(f->d, f->n, err, sizeof err))
        sk_test_fail(__FILE__, line, "%s", err);
    free(f->d);
}

#define REFUSED(f, want) refused(&(f), want, __LINE__)
#define PASSES(f) passes(&(f), __LINE__)

static void rename_table(font *f, const char *from, const char *to)
{
    memcpy(f->d + sk_test_font_record(f->d, from), to, 4);
}

static void test_refuses_files(void)
{
    font f = made();
    f.n = 11;
    REFUSED(f, "too short to be a font");
    f = made();
    memcpy(f.d, "ABCD", 4);
    REFUSED(f, "is not a TrueType font");
    f = made();
    memcpy(f.d, "OTTO", 4);
    REFUSED(f, "has CFF outlines");
    f = made();
    memcpy(f.d, "ttcf", 4);
    REFUSED(f, "is a font collection");
    f = made();
    rename_table(&f, "kern", "CFF ");
    REFUSED(f, "has CFF outlines");
    f = made();
    rename_table(&f, "kern", "sbix");
    REFUSED(f, "has bitmap glyphs (sbix)");
    f = made();
    sk_test_put32(f.d + sk_test_font_record(f.d, "glyf") + 12, (uint32_t)f.n);
    REFUSED(f, "has a glyf table that runs past the end of the file");
    f = made();
    sk_test_put16(f.d + 4, 200);
    REFUSED(f, "table directory that runs past");
    f = made();
    rename_table(&f, "kern", "name");
    REFUSED(f, "has two name tables");
    f = made();
    rename_table(&f, "glyf", "glyp");
    REFUSED(f, "has no glyf table");
    f = made();
    rename_table(&f, "hmtx", "hmtz");
    REFUSED(f, "has no hmtx table");
}

static void test_refuses_metrics(void)
{
    font f = made();
    sk_test_put32(f.d + f.head + 12, 0);
    REFUSED(f, "without its magic number");
    f = made();
    sk_test_put16(f.d + f.head + 18, 8);
    REFUSED(f, "has 8 units to the em");
    f = made();
    sk_test_put16(f.d + f.head + 50, 2);
    REFUSED(f, "loca format of 2");
    f = made();
    sk_test_put32(f.d + sk_test_font_record(f.d, "maxp") + 12, 4);
    REFUSED(f, "maxp table too short");
    f = made();
    sk_test_put16(f.d + f.maxp + 4, 0);
    REFUSED(f, "has no glyphs");
    f = made();
    sk_test_put16(f.d + f.hhea + 34, 0);
    REFUSED(f, "no horizontal metrics");
    f = made();
    sk_test_put32(f.d + sk_test_font_record(f.d, "hmtx") + 12, 14);
    REFUSED(f, "hmtx table too short for its 5 glyphs");
    f = made();
    sk_test_put32(f.d + sk_test_font_record(f.d, "loca") + 12, 10);
    REFUSED(f, "loca table too short");
    f = made();
    sk_test_put16(f.d + f.loca + 6, 10); /* glyph 3 before glyph 2 starts */
    REFUSED(f, "goes back at glyph 3");
    f = made();
    sk_test_put16(f.d + f.loca + 10, 0x7000);
    REFUSED(f, "loca table pointing past its glyf table at entry 5");
}

static void test_refuses_glyphs(void)
{
    font f = made();
    sk_test_put16(f.d + f.loca + 2, 2); /* glyph 0 of 4 bytes */
    REFUSED(f, "glyph 0 shorter than its header");
    f = made();
    sk_test_put16(f.d + f.glyf + G0, 100);
    REFUSED(f, "glyph 0's contours running past its end");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 12, 1000);
    REFUSED(f, "glyph 0's instructions running past its end");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 10, 100);
    REFUSED(f, "glyph 0's points running past its end");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 19, 0x7FFF);
    REFUSED(f, "glyph 0 with a point past the coordinates' range");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 6, 400);
    REFUSED(f, "glyph 0 drawing from (50, 0) to (450, 700), outside its box, (50, 0) to (400, 700)");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 6, 449); /* a unit short: font tools round */
    PASSES(f);
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 2, 451);
    REFUSED(f, "glyph 0 with its box inside out");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 8, 5000);
    REFUSED(f, "glyph 0 reaching more than 4 ems");
    /* Its box tops out at the baseline: nothing may rise above it, not
     * a unit, where the rasterizer asserts. */
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 8, 0);
    sk_test_put16(f.d + f.glyf + G0 + 21, 1);
    sk_test_put16(f.d + f.glyf + G0 + 23, 0xFFFF);
    REFUSED(f, "glyph 0 drawing from (50, 0) to (450, 1), outside its box, (50, 0) to (450, 0)");
    f = made();
    sk_test_put16(f.d + f.glyf + G0 + 8, 1);
    sk_test_put16(f.d + f.glyf + G0 + 21, 1);
    sk_test_put16(f.d + f.glyf + G0 + 23, 0xFFFF);
    PASSES(f);
    /* The second contour cut to its first point, off the curve: stb
     * reads past its points for the next. */
    f = made();
    sk_test_put16(f.d + f.glyf + G2 + 12, 5);
    REFUSED(f, "glyph 2 ending in a contour of one point off the curve");
}

static void test_refuses_compounds(void)
{
    font f = made();
    f.d[f.glyf + G3 + 11] = 0x20; /* no ARGS_ARE_XY_VALUES */
    REFUSED(f, "glyph 3 placing a component by matching points");
    f = made();
    sk_test_put16(f.d + f.glyf + G3 + 12, 9);
    REFUSED(f, "glyph 3 holding glyph 9, which the font does not have");
    f = made();
    sk_test_put16(f.d + f.glyf + G3 + 18, 3);
    REFUSED(f, "glyph 3 holding itself");
    f = made();
    sk_test_put16(f.d + f.glyf + G4 + 12, 4);
    REFUSED(f, "glyph 4 holding itself");
    f = made();
    f.d[f.glyf + G3 + 17] |= 0x40;
    REFUSED(f, "glyph 3 giving a component two scales");
    f = made();
    f.d[f.glyf + G3 + 17] |= 0x20; /* more after the last */
    REFUSED(f, "glyph 3's components running past its end");
    f = made();
    sk_test_put16(f.d + f.glyf + G3 + 24, 0x7FFF); /* scale 2, which stb doubles */
    sk_test_put16(f.d + f.glyf + G3 + 20, 30000);
    REFUSED(f, "glyph 3 moving a component past the coordinates' range");
    /* The half scale drawn where stb_truetype draws it, a quarter: put
     * where a font tool would, it would pass the box. */
    f = made();
    sk_test_put16(f.d + f.glyf + G3 + 6, 410);
    PASSES(f);
    f = made();
    sk_test_put16(f.d + f.glyf + G3 + 6, 400);
    REFUSED(f, "glyph 3 drawing from (110, 0) to (410, 525), outside its box, (110, 0) to (400, 600)");
    f = made();
    sk_test_put16(f.d + f.glyf + G4 + 8, 300);
    REFUSED(f, "glyph 4 drawing from (-525, 110) to (0, 410), outside its box, (-600, 110) to (0, 300)");
}

static void test_refuses_cmaps(void)
{
    font f = made();
    for (int i = 0; i < 6; i++)
        sk_test_put16(f.d + f.cmap + 4 + 8 * i, 1);
    REFUSED(f, "no Unicode cmap subtable");
    f = made();
    sk_test_put16(f.d + f.cmap + 2, 1000);
    REFUSED(f, "cmap table whose encodings run past its end");
    f = made();
    sk_test_put32(f.d + f.cmap + 4 + 4, 0x10000);
    REFUSED(f, "cmap subtable past the end of its table");
    f = made();
    sk_test_put16(f.d + f.cmap + F4 + 6, 2000);
    REFUSED(f, "format 4 subtable whose segments run past its table");
    f = made();
    sk_test_put16(f.d + f.cmap + F4 + 36, 0x7000);
    REFUSED(f, "format 4 subtable whose glyph array runs past its table");
    /* A search the header steers out of the subtable. */
    f = made();
    sk_test_put16(f.d + f.cmap + F4 + 12, 0x7000);
    REFUSED(f, "format 4 subtable whose search reads past its table");
    /* A header that is not the format's, but keeps the search inside. */
    f = made();
    sk_test_put16(f.d + f.cmap + F4 + 8, 2);
    sk_test_put16(f.d + f.cmap + F4 + 10, 0);
    sk_test_put16(f.d + f.cmap + F4 + 12, 4);
    PASSES(f);
    f = made();
    sk_test_put32(f.d + f.cmap + F12 + 12, 1000);
    REFUSED(f, "format 12 subtable past the end of its table");
    /* stb_truetype reads a group's numbers as an int shifted 24
     * places: one with its top bit set overflows (found by fuzzing). */
    f = made();
    sk_test_put32(f.d + f.cmap + F12 + 16 + 8, 0xE5000000);
    REFUSED(f, "format 12 group past Unicode's codes or the glyphs' 16 bits");
    f = made();
    sk_test_put32(f.d + f.cmap + F12 + 16 + 12 + 4, 0x110000);
    REFUSED(f, "format 12 group past Unicode's codes");
    f = made();
    sk_test_put16(f.d + f.cmap + F6 + 8, 1000);
    REFUSED(f, "format 6 subtable past the end of its table");
    f = made();
    sk_test_put16(f.d + f.cmap + F0 + 2, 5000);
    REFUSED(f, "format 0 subtable past the end of its table");
}

static void test_refuses_tables(void)
{
    font f = made();
    sk_test_put16(f.d + f.kern + 10, 1000);
    REFUSED(f, "kern table whose pairs run past its end");
    /* Its count of pairs, past a table of 11 bytes. */
    f = made();
    sk_test_put32(f.d + sk_test_font_record(f.d, "kern") + 12, 11);
    REFUSED(f, "kern table too short for its first subtable");
    /* Its search shifts the first glyph 16 places in an int. */
    f = made();
    sk_test_put16(f.d + f.kern + 18, 0x8000);
    REFUSED(f, "kern pair of glyph 32768");
    f = made();
    sk_test_put16(f.d + f.gpos + 10, 1000);
    REFUSED(f, "GPOS lookup list past the end of its table");
    f = made();
    sk_test_put16(f.d + f.gpos + 34, 0x7000); /* the pair set */
    REFUSED(f, "GPOS pair adjustment that reads past the end of its table");
    f = made();
    sk_test_put16(f.d + f.gpos + 42, 1000); /* its pairs */
    REFUSED(f, "GPOS pair adjustment");
    f = made();
    sk_test_put16(f.d + f.gpos + 68, 200); /* class 1 count */
    REFUSED(f, "GPOS pair adjustment");
    /* Another GPOS version is not read at all. */
    f = made();
    sk_test_put16(f.d + f.gpos + 2, 1);
    sk_test_put16(f.d + f.gpos + 10, 1000);
    PASSES(f);
    f = made();
    sk_test_put16(f.d + f.name + 14, 1000);
    REFUSED(f, "name whose text runs past the end of its table");
    f = made();
    sk_test_put16(f.d + f.os2, 5);
    REFUSED(f, "OS/2 table of version 5 shorter than its 100 bytes");
    f = made();
    sk_test_put16(f.d + f.post + 42, 300);
    REFUSED(f, "post table whose glyph names run past its end");
}

/* Every cut of the made font is refused or passes, and a pass opens. */
static void test_truncations(void)
{
    size_t n = 0;
    uint8_t *data = sk_test_tiny_font(&n);
    for (size_t k = 0; k < n; k++) {
        uint8_t *cut = sk_xmalloc(k ? k : 1);
        memcpy(cut, data, k);
        char err[200];
        if (sk_font_check(cut, k, err, sizeof err)) {
            sk_font *f = sk_font_open(cut, k, 0);
            CHECK(f != NULL);
            sk_font_close(f);
        } else {
            CHECK(err[0] != 0);
        }
        free(cut);
    }
    free(data);
}

const sk_test sk_suite_fontcheck[] = {
    T(test_skin_fonts),        T(test_tiny_font),       T(test_refuses_files), T(test_refuses_metrics),
    T(test_refuses_glyphs),    T(test_refuses_compounds), T(test_refuses_cmaps), T(test_refuses_tables),
    T(test_truncations),       {0},
};
