#include "../../src/base/paths.h"
#include "../../src/text/text.h"
#include "../test.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* The repository's skin fonts, as the discovery tests copy them. */
static const char *const skin_fonts[] = {
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

static bool write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static void copy_to(const char *from, const char *dir, const char *name)
{
    size_t n = 0;
    char *data = sk_test_read(from, &n);
    CHECK(data != NULL);
    CHECK(sk_mkdirs(dir, 0755) == 0);
    char *to = sk_path_join(dir, name);
    CHECK(data && write_file(to, data, n));
    free(to);
    free(data);
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* Whether some face of fs has family and weight, and how many do. */
static int faces_of(const sk_fonts *fs, const char *family, int weight)
{
    int n = 0;
    for (size_t i = 0; i < sk_fonts_count(fs); i++) {
        const sk_font_info *in = sk_fonts_info(fs, i);
        if (strcmp(in->family, family) == 0 && in->weight == weight)
            n++;
    }
    return n;
}

/* The skin fonts copied a level or two down, with what a font directory
 * may also hold: a text file named .ttf, a font that cannot be read,
 * links that loop, an upper-case extension, and a link to a directory
 * seen already. */
static void test_scan_survives(void)
{
    const char *tmp = sk_test_tmpdir();
    char *root = sk_path_join(tmp, "fonts");
    for (size_t i = 0; i < SK_LEN(skin_fonts); i++) {
        sk_buf dir = {0};
        sk_buf_printf(&dir, "%s/%s/sub", root, i % 2 ? "odd" : "even");
        copy_to(skin_fonts[i], dir.ptr, base_name(skin_fonts[i]));
        sk_buf_free(&dir);
    }
    char *p = sk_path_join(root, "notes.ttf");
    CHECK(write_file(p, "not a font at all, only text\n", 29));
    free(p);
    copy_to("third_party/dejavu/DejaVuSans.ttf", root, "LOCKED.TTF");
    p = sk_path_join(root, "LOCKED.TTF");
    CHECK(chmod(p, 0) == 0);
    free(p);
    copy_to("third_party/dejavu/DejaVuSansMono-Bold.ttf", root, "UPPER.TTF");
    p = sk_path_join(root, "loop");
    CHECK(symlink(".", p) == 0);
    free(p);
    p = sk_path_join(root, "a.ttf");
    CHECK(symlink("b.ttf", p) == 0);
    free(p);
    p = sk_path_join(root, "b.ttf");
    CHECK(symlink("a.ttf", p) == 0);
    free(p);
    p = sk_path_join(root, "zz-again");
    CHECK(symlink("odd", p) == 0);
    free(p);
    p = sk_path_join(root, "empty.otf");
    CHECK(write_file(p, "", 0));
    free(p);

    const char *dirs[] = {root, NULL};
    sk_fonts *fs = sk_fonts_scan(dirs);
    /* Every skin font once, the upper-case one, and the locked one only
     * when the tests run as a user who can read it anyway (root). */
    size_t want = SK_LEN(skin_fonts) + 1 + (geteuid() == 0);
    CHECK_INT(sk_fonts_count(fs), want);
    CHECK_INT(faces_of(fs, "Silkscreen", 400), 1);
    CHECK_INT(faces_of(fs, "Silkscreen", 700), 1);
    CHECK_INT(faces_of(fs, "VT323", 400), 1);
    CHECK_INT(faces_of(fs, "Amatic SC", 700), 1);
    CHECK_INT(faces_of(fs, "DejaVu Sans Mono", 700), 1);
    for (size_t i = 0; i < SK_LEN(skin_fonts); i++) {
        sk_font *f = sk_font_open_file(skin_fonts[i], 0);
        CHECK(f != NULL);
        if (!f)
            continue;
        sk_font_info in;
        sk_font_info_of(f, &in);
        CHECK_INT(faces_of(fs, in.family, in.weight), 1);
        sk_font_close(f);
    }
    for (size_t i = 0; i < sk_fonts_count(fs); i++)
        CHECK(sk_fonts_info(fs, i)->family[0] != 0);
    CHECK(sk_fonts_info(fs, sk_fonts_count(fs)) == NULL);
    sk_fonts_free(fs);
    free(root);
}

static void test_scan_depth(void)
{
    const char *tmp = sk_test_tmpdir();
    sk_buf deep = {0}, shallow = {0};
    sk_buf_printf(&shallow, "%s/1/2/3", tmp);
    sk_buf_printf(&deep, "%s/1/2/3/4/5/6/7/8/9/10/11/12", tmp);
    copy_to("skins/phosphor/fonts/VT323/VT323-Regular.ttf", shallow.ptr, "VT323-Regular.ttf");
    copy_to("skins/deck/fonts/Silkscreen/Silkscreen-Regular.ttf", deep.ptr, "Silkscreen-Regular.ttf");
    const char *dirs[] = {tmp, "/no/such/directory", NULL};
    sk_fonts *fs = sk_fonts_scan(dirs);
    CHECK_INT(sk_fonts_count(fs), 1);
    CHECK_INT(faces_of(fs, "VT323", 400), 1);
    sk_fonts_free(fs);
    sk_buf_free(&deep);
    sk_buf_free(&shallow);
}

static void test_find(void)
{
    const char *dirs[] = {"skins/deck/fonts", "skins/phosphor/fonts", NULL};
    sk_fonts *fs = sk_fonts_scan(dirs);
    CHECK_INT(sk_fonts_count(fs), 5);
    sk_font_info in;
    const sk_font *f = sk_fonts_find(fs, "silkscreen", SK_STYLE_BOLD);
    CHECK(f != NULL);
    if (f) {
        sk_font_info_of(f, &in);
        CHECK_STR(in.family, "Silkscreen");
        CHECK_INT(in.weight, 700);
    }
    /* The same face each time, opened once. */
    CHECK(sk_fonts_find(fs, "SILKSCREEN", SK_STYLE_BOLD) == f);
    f = sk_fonts_find(fs, "Silkscreen", SK_STYLE_REGULAR);
    CHECK(f != NULL);
    if (f) {
        sk_font_info_of(f, &in);
        CHECK_INT(in.weight, 400);
    }
    /* No italic: the nearest is upright, at the weight asked for. */
    CHECK(sk_fonts_find(fs, "Silkscreen", SK_STYLE_ITALIC) == f);
    f = sk_fonts_find(fs, "Silkscreen", SK_STYLE_BOLD | SK_STYLE_ITALIC);
    if (f) {
        sk_font_info_of(f, &in);
        CHECK_INT(in.weight, 700);
    }
    /* VT323 has one face, for every style. */
    f = sk_fonts_find(fs, "VT323", SK_STYLE_REGULAR);
    CHECK(f != NULL);
    CHECK(sk_fonts_find(fs, "vt323", SK_STYLE_BOLD) == f);
    CHECK(sk_fonts_find(fs, "VT32", SK_STYLE_REGULAR) == NULL);
    CHECK(sk_fonts_find(fs, "", SK_STYLE_REGULAR) == NULL);
    CHECK(sk_fonts_find(NULL, "VT323", SK_STYLE_REGULAR) == NULL);
    sk_fonts_free(fs);
}

static void test_system_scan(void)
{
    sk_fonts *fs = sk_fonts_scan(NULL);
    for (size_t i = 0; i < sk_fonts_count(fs); i++) {
        const sk_font_info *in = sk_fonts_info(fs, i);
        CHECK(in->weight >= 1 && in->weight <= 1000);
        CHECK(strlen(in->family) < sizeof in->family && strlen(in->style) < sizeof in->style);
    }
    if (sk_os_current() == SK_OS_MACOS)
        CHECK(sk_fonts_find(fs, "Menlo", SK_STYLE_BOLD) != NULL);
    sk_fonts_free(fs);
    sk_fonts_free(NULL);
}

const sk_test sk_suite_fonts[] = {
    T(test_scan_survives),
    T(test_scan_depth),
    T(test_find),
    T(test_system_scan),
    {0},
};
