#include "../../src/base/embed.h"
#include "../../src/text/internal.h"
#include "../test.h"

#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Where a table of a plain font file starts, read here without the code
 * under test; 0 when it has none. */
static size_t table_at(const uint8_t *data, const char *tag)
{
    for (uint32_t i = 0; i < u16(data + 4); i++)
        if (memcmp(data + 12 + 16 * i, tag, 4) == 0)
            return u32(data + 12 + 16 * i + 8);
    return 0;
}

static void check_info(sk_embedded_font which, const char *family, const char *style, int weight, bool italic,
                       bool mono)
{
    sk_font_info in;
    sk_font_info_of(sk_font_embedded(which), &in);
    CHECK_STR(in.family, family);
    CHECK_STR(in.style, style);
    CHECK_INT(in.weight, weight);
    CHECK_INT(in.italic, italic);
    CHECK_INT(in.mono, mono);
}

static void test_embedded_faces(void)
{
    CHECK_INT(sk_embedded_fonts_count, SK_FONT_EMBEDDED_COUNT);
    check_info(SK_FONT_MONO, "DejaVu Sans Mono", "Book", 400, false, true);
    check_info(SK_FONT_MONO_BOLD, "DejaVu Sans Mono", "Bold", 700, false, true);
    check_info(SK_FONT_MONO_OBLIQUE, "DejaVu Sans Mono", "Oblique", 400, true, true);
    check_info(SK_FONT_MONO_BOLD_OBLIQUE, "DejaVu Sans Mono", "Bold Oblique", 700, true, true);
    check_info(SK_FONT_SANS, "DejaVu Sans", "Book", 400, false, false);
    check_info(SK_FONT_SANS_BOLD, "DejaVu Sans", "Bold", 700, false, false);
}

static void *embedded_from_thread(void *arg)
{
    const sk_font **out = arg;
    for (int i = 0; i < SK_FONT_EMBEDDED_COUNT; i++)
        out[i] = sk_font_embedded((sk_embedded_font)i);
    return NULL;
}

static void test_embedded_shared_by_threads(void)
{
    const sk_font *seen[4][SK_FONT_EMBEDDED_COUNT];
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        CHECK(pthread_create(&t[i], NULL, embedded_from_thread, seen[i]) == 0);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < SK_FONT_EMBEDDED_COUNT; j++)
            CHECK(seen[i][j] == sk_font_embedded((sk_embedded_font)j));
}

static void test_mono_advances(void)
{
    const sk_font *mono = sk_font_embedded(SK_FONT_MONO);
    float want = sk_font_advance(mono, sk_font_glyph(mono, 'M'), 13);
    CHECK(want > 7.5f && want < 8.5f);
    for (int which = SK_FONT_MONO; which <= SK_FONT_MONO_BOLD_OBLIQUE; which++) {
        const sk_font *f = sk_font_embedded((sk_embedded_font)which);
        for (uint32_t cp = 0x20; cp < 0x7f; cp++) {
            uint32_t g = sk_font_glyph(f, cp);
            CHECK(g != 0);
            if (sk_font_advance(f, g, 13) != want)
                sk_test_fail(__FILE__, __LINE__, "face %d: U+%04X advances %g, not %g", which, (unsigned)cp,
                             (double)sk_font_advance(f, g, 13), (double)want);
        }
    }
}

/* The metrics as the font's own bytes give them: OS/2's typographic ones
 * only when USE_TYPO_METRICS is set, hhea's otherwise. */
static void test_dejavu_metrics(void)
{
    for (int i = 0; i < SK_FONT_EMBEDDED_COUNT; i++) {
        const uint8_t *data = sk_embedded_fonts[i].data;
        size_t head_at = table_at(data, "head"), hhea_at = table_at(data, "hhea"), os2_at = table_at(data, "OS/2");
        CHECK(head_at && hhea_at && os2_at);
        if (!head_at || !hhea_at || !os2_at)
            continue;
        const uint8_t *head = data + head_at, *hhea = data + hhea_at, *os2 = data + os2_at;
        CHECK_INT(u16(head + 18), 2048);
        int asc = (int16_t)u16(hhea + 4), desc = -(int16_t)u16(hhea + 6), gap = (int16_t)u16(hhea + 8);
        if (u16(os2 + 62) & 0x80) {
            asc = (int16_t)u16(os2 + 68);
            desc = -(int16_t)u16(os2 + 70);
            gap = (int16_t)u16(os2 + 72);
        }
        CHECK(asc > 1500 && desc > 300);
        const sk_font *f = sk_font_embedded((sk_embedded_font)i);
        sk_font_vmetrics v;
        sk_font_vmetrics_at(f, 2048, &v);
        CHECK(v.ascent == (float)asc);
        CHECK(v.descent == (float)desc);
        CHECK(v.line_gap == (float)SK_MAX(gap, 0));
        sk_font_vmetrics_at(f, 13, &v);
        CHECK(v.ascent == (float)asc * (13.0f / 2048.0f));
        uint32_t m = sk_font_glyph(f, 'M');
        uint32_t longs = u16(hhea + 34);
        int adv = u16(data + table_at(data, "hmtx") + 4 * (m < longs ? m : longs - 1));
        CHECK(sk_font_advance(f, m, 2048) == (float)adv);
    }
}

static void test_kerning(void)
{
    const sk_font *sans = sk_font_embedded(SK_FONT_SANS);
    uint32_t a = sk_font_glyph(sans, 'A'), v = sk_font_glyph(sans, 'V'), t = sk_font_glyph(sans, 'T'),
             o = sk_font_glyph(sans, 'o'), h = sk_font_glyph(sans, 'H');
    CHECK(sk_font_kern(sans, a, v, 2048) < 0);
    CHECK(sk_font_kern(sans, v, a, 2048) < 0);
    CHECK(sk_font_kern(sans, t, o, 2048) < 0);
    CHECK(sk_font_kern(sans, h, h, 2048) == 0);
    /* Scaled with the em. */
    CHECK(sk_font_kern(sans, a, v, 1024) == sk_font_kern(sans, a, v, 2048) / 2);
    CHECK(sk_font_kern(sans, a, 1u << 30, 2048) == 0);
}

static void test_glyphs(void)
{
    const sk_font *f = sk_font_embedded(SK_FONT_SANS);
    CHECK(sk_font_glyph(f, 'A') != 0);
    CHECK(sk_font_glyph(f, 0x416) != 0); /* Ж */
    CHECK(sk_font_glyph(f, 0xFFFD) != 0);
    CHECK_INT(sk_font_glyph(f, 0x10FFFF), 0);
    CHECK_INT(sk_font_glyph(f, 0x110000), 0);
    CHECK_INT(sk_font_glyph(f, 0xFFFFFFFF), 0);
    CHECK(sk_font_advance(f, 1u << 30, 13) == sk_font_advance(f, 0, 13));
}

static void test_not_fonts(void)
{
    const sk_embedded_file *mono = &sk_embedded_fonts[SK_FONT_MONO];
    const char text[] = "this is not a font, though it is long enough to look like one";
    CHECK_INT(sk_font_faces((const uint8_t *)text, sizeof text), 0);
    CHECK(sk_font_open((const uint8_t *)text, sizeof text, 0) == NULL);
    CHECK_INT(sk_font_faces(mono->data, 8), 0);
    CHECK_INT(sk_font_faces(NULL, 0), 0);
    CHECK_INT(sk_font_faces(mono->data, mono->size), 1);
    CHECK(sk_font_open(mono->data, mono->size, 1) == NULL);
    CHECK(sk_font_open(mono->data, mono->size, -1) == NULL);
    /* Cut short, its tables run past the end. */
    CHECK(sk_font_open(mono->data, 4096, 0) == NULL);
    /* A collection header claiming more faces than it has room for. */
    uint8_t ttc[16] = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 9};
    CHECK_INT(sk_font_faces(ttc, sizeof ttc), 0);
}

static void test_open_file(void)
{
    sk_font *f = sk_font_open_file("third_party/dejavu/DejaVuSans-Bold.ttf", 0);
    CHECK(f != NULL);
    if (f) {
        sk_font_info in;
        sk_font_info_of(f, &in);
        CHECK_STR(in.family, "DejaVu Sans");
        CHECK_INT(in.weight, 700);
        CHECK(sk_font_glyph(f, 'A') == sk_font_glyph(sk_font_embedded(SK_FONT_SANS_BOLD), 'A'));
        sk_font_close(f);
    }
    CHECK(sk_font_open_file("third_party/dejavu/DejaVuSans-Bold.ttf", 1) == NULL);
    CHECK(sk_font_open_file("third_party/dejavu/no-such-font.ttf", 0) == NULL);
    CHECK(sk_font_open_file("third_party/dejavu", 0) == NULL);
    CHECK(sk_font_open_file("third_party/dejavu/LICENSE", 0) == NULL);
    sk_font_close(NULL);
}

/* A name record's text, found in a copy of the font to change. */
typedef struct {
    uint8_t *rec, *text;
} name_rec;

static int name_records(uint8_t *data, unsigned id, name_rec *out, int max)
{
    size_t at = table_at(data, "name");
    uint8_t *name = data + at;
    int n = 0;
    for (uint32_t i = 0; at && i < u16(name + 2) && n < max; i++) {
        uint8_t *r = name + 6 + 12 * i;
        if (u16(r + 6) == id) {
            out[n].rec = r;
            out[n].text = name + u16(name + 4) + u16(r + 10);
            n++;
        }
    }
    return n;
}

/* Windows names are UTF-16BE, surrogate pairs included; without them the
 * Mac's Roman name is read. */
static void test_name_encodings(void)
{
    const sk_embedded_file *e = &sk_embedded_fonts[SK_FONT_MONO];
    uint8_t *copy = sk_xmalloc(e->size);
    memcpy(copy, e->data, e->size);
    name_rec recs[16];
    int n = name_records(copy, 1, recs, 16), windows = 0, mac = 0;
    for (int i = 0; i < n; i++) {
        unsigned plat = u16(recs[i].rec);
        if (plat == 3) {
            /* "De" becomes U+1F600. */
            memcpy(recs[i].text, "\xD8\x3D\xDE\x00", 4);
            windows++;
        } else if (plat == 1) {
            recs[i].text[u16(recs[i].rec + 8) - 1] = 0x8E; /* é */
            mac++;
        }
    }
    CHECK(windows > 0 && mac > 0);
    sk_font *f = sk_font_open(copy, e->size, 0);
    CHECK(f != NULL);
    sk_font_info in;
    if (f) {
        sk_font_info_of(f, &in);
        CHECK_STR(in.family, "\xF0\x9F\x98\x80jaVu Sans Mono");
        sk_font_close(f);
    }
    for (int i = 0; i < n; i++)
        if (u16(recs[i].rec) == 3)
            recs[i].rec[6] = recs[i].rec[7] = 0xff; /* no longer name 1 */
    f = sk_font_open(copy, e->size, 0);
    CHECK(f != NULL);
    if (f) {
        sk_font_info_of(f, &in);
        CHECK_STR(in.family, "DejaVu Sans Mon\xC3\xA9");
        sk_font_close(f);
    }
    free(copy);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The first file under dir, in name order, that wants accepts. */
static char *find_file(const char *dir, int depth, bool (*wants)(const char *path))
{
    DIR *d = opendir(dir);
    if (!d)
        return NULL;
    SK_VEC(char *) names = {0};
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') {
            char *name = sk_xstrdup(e->d_name);
            sk_vec_push(&names, name);
        }
    closedir(d);
    if (names.len)
        qsort(names.ptr, names.len, sizeof *names.ptr, by_name);
    char *found = NULL;
    for (size_t i = 0; i < names.len; i++) {
        sk_buf p = {0};
        sk_buf_printf(&p, "%s/%s", dir, names.ptr[i]);
        struct stat st;
        if (!found && lstat(p.ptr, &st) == 0) {
            if (S_ISDIR(st.st_mode) && depth > 0)
                found = find_file(p.ptr, depth - 1, wants);
            else if (S_ISREG(st.st_mode) && wants(p.ptr))
                found = sk_xstrdup(p.ptr);
        }
        sk_buf_free(&p);
        free(names.ptr[i]);
    }
    sk_vec_free(&names);
    return found;
}

static char *find_system_font(bool (*wants)(const char *path))
{
    static const char *const dirs[] = {"/System/Library/Fonts", "/usr/share/fonts"};
    for (size_t i = 0; i < SK_LEN(dirs); i++) {
        char *p = find_file(dirs[i], 4, wants);
        if (p)
            return p;
    }
    return NULL;
}

static bool has_suffix(const char *path, const char *suffix)
{
    size_t n = strlen(path), k = strlen(suffix);
    return n >= k && sk_text_name_eq(path + n - k, suffix);
}

/* An outline for 'A', drawn. */
static bool draws_a(const sk_font *f)
{
    uint32_t g = sk_font_glyph(f, 'A');
    if (!g)
        return false;
    sk_glyph_cache *c = sk_glyph_cache_new(1 << 20);
    sk_glyph_ref ref = {f, g, 0};
    const sk_glyph *gl = sk_glyph_get(c, ref, 32, 0);
    bool ink = false;
    for (int i = 0; i < gl->mask.w * gl->mask.h; i++)
        ink |= gl->mask.a[i] > 200;
    bool tall = gl->mask.h >= 18 && gl->mask.h <= 30 && gl->top < -15;
    ref.synth = SK_STYLE_BOLD | SK_STYLE_ITALIC;
    gl = sk_glyph_get(c, ref, 32, 1);
    bool synth = gl->mask.w > 0;
    sk_glyph_cache_free(c);
    return ink && tall && synth;
}

static bool collection(const char *path)
{
    if (!has_suffix(path, ".ttc"))
        return false;
    sk_font *f = sk_font_open_file(path, 0);
    bool ok = f && sk_font_glyph(f, 'A');
    sk_font_close(f);
    return ok;
}

static void test_system_collection(void)
{
    char *path = find_system_font(collection);
    if (!path) {
        printf("    (skipped: no .ttc among the system's fonts)\n");
        return;
    }
    size_t n = 0;
    char *data = sk_test_read(path, &n);
    int faces = data ? sk_font_faces((const uint8_t *)data, n) : 0;
    free(data);
    if (faces < 2)
        sk_test_fail(__FILE__, __LINE__, "%s: %d faces", path, faces);
    int opened = 0;
    for (int i = 0; i < faces; i++) {
        sk_font *f = sk_font_open_file(path, i);
        if (!f)
            continue;
        opened++;
        sk_font_info in;
        sk_font_info_of(f, &in);
        CHECK(in.family[0] != 0);
        CHECK(in.weight >= 100 && in.weight <= 1000);
        if (i == 0)
            CHECK(draws_a(f));
        sk_font_close(f);
    }
    CHECK(opened >= 2);
    CHECK(sk_font_open_file(path, faces) == NULL);
    free(path);
}

static bool cff_with_a(const char *path)
{
    if (!has_suffix(path, ".otf"))
        return false;
    FILE *fp = fopen(path, "rb");
    char tag[4] = {0};
    bool cff = fp && fread(tag, 1, 4, fp) == 4 && memcmp(tag, "OTTO", 4) == 0;
    if (fp)
        fclose(fp);
    if (!cff)
        return false;
    sk_font *f = sk_font_open_file(path, 0);
    bool ok = f && f->info.cff.size > 0 && sk_font_glyph(f, 'A');
    sk_font_close(f);
    return ok;
}

static void test_system_cff(void)
{
    char *path = find_system_font(cff_with_a);
    if (!path) {
        printf("    (skipped: no CFF .otf with an A among the system's fonts)\n");
        return;
    }
    sk_font *f = sk_font_open_file(path, 0);
    CHECK(f != NULL);
    if (f) {
        sk_font_info in;
        sk_font_info_of(f, &in);
        CHECK(in.family[0] != 0);
        sk_font_vmetrics v;
        sk_font_vmetrics_at(f, 32, &v);
        CHECK(v.ascent > 10 && v.ascent < 64 && v.descent > 0);
        CHECK(sk_font_advance(f, sk_font_glyph(f, 'A'), 32) > 8);
        if (!draws_a(f))
            sk_test_fail(__FILE__, __LINE__, "%s draws no A", path);
        sk_font_close(f);
    }
    free(path);
}

/* Faces without outlines to draw are not opened: Apple's colour emoji
 * (sbix bitmaps over an empty glyf), Google's (CBDT), and the LastResort
 * font, whose only cmap maps ranges to one glyph. */
static void test_unusable_system_faces(void)
{
    static const char *const paths[] = {
        "/System/Library/Fonts/Apple Color Emoji.ttc",
        "/System/Library/Fonts/LastResort.otf",
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf",
    };
    int tried = 0;
    for (size_t i = 0; i < SK_LEN(paths); i++) {
        struct stat st;
        if (stat(paths[i], &st) != 0)
            continue;
        tried++;
        sk_font *f = sk_font_open_file(paths[i], 0);
        if (f)
            sk_test_fail(__FILE__, __LINE__, "%s opened", paths[i]);
        sk_font_close(f);
    }
    if (!tried)
        printf("    (skipped: no colour emoji or LastResort font on this system)\n");
}

const sk_test sk_suite_font[] = {
    T(test_embedded_faces),
    T(test_embedded_shared_by_threads),
    T(test_mono_advances),
    T(test_dejavu_metrics),
    T(test_kerning),
    T(test_glyphs),
    T(test_not_fonts),
    T(test_open_file),
    T(test_name_encodings),
    T(test_system_collection),
    T(test_system_cff),
    T(test_unusable_system_faces),
    {0},
};
