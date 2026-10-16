#include "internal.h"

#include "../base/paths.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* How far below each directory the scan goes. */
#define MAX_DEPTH 8

typedef struct {
    sk_font_info info;
    char legacy_family[64];
    int width;
    char *path;
    int index;
    sk_font *font; /* opened on first use */
    bool dead;     /* it would not open again */
} face;

struct sk_fonts {
    SK_VEC(face) faces;
};

/* Every directory and file the scan has reached, so a symbolic link
 * back up the tree, or to a file seen already, counts once. */
typedef struct {
    dev_t dev;
    ino_t ino;
} file_id;

typedef SK_VEC(file_id) seen_set;

static bool seen(seen_set *s, const struct stat *st)
{
    for (size_t i = 0; i < s->len; i++)
        if (s->ptr[i].dev == st->st_dev && s->ptr[i].ino == st->st_ino)
            return true;
    file_id id = {st->st_dev, st->st_ino};
    sk_vec_push(s, id);
    return false;
}

bool sk_text_name_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return false;
        if (!x)
            return true;
    }
}

/* .ttf, .otf, .ttc or .otc, in any case. */
static bool font_file(const char *name)
{
    size_t n = strlen(name);
    if (n < 5 || name[n - 4] != '.')
        return false;
    static const char *const exts[] = {".ttf", ".otf", ".ttc", ".otc"};
    for (size_t i = 0; i < SK_LEN(exts); i++)
        if (sk_text_name_eq(name + n - 4, exts[i]))
            return true;
    return false;
}

/* Every face of the file, named and then let go: nothing stays open. */
static void add_file(sk_fonts *fs, const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return;
    struct stat st;
    void *map = MAP_FAILED;
    size_t n = 0;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 12 && st.st_size <= INT_MAX) {
        n = (size_t)st.st_size;
        map = mmap(NULL, n, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    close(fd);
    if (map == MAP_FAILED)
        return;
    int count = sk_font_faces(map, n);
    for (int i = 0; i < count; i++) {
        sk_font *f = sk_font_open(map, n, i);
        if (!f)
            continue;
        face e = {0};
        e.info = f->meta;
        memcpy(e.legacy_family, f->legacy_family, sizeof e.legacy_family);
        e.width = f->width;
        e.path = sk_xstrdup(path);
        e.index = i;
        sk_vec_push(&fs->faces, e);
        sk_font_close(f);
    }
    munmap(map, n);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* dir's entries in name order, so a scan finds the same faces in the
 * same order every time. */
static void walk(sk_fonts *fs, seen_set *ids, const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    SK_VEC(char *) names = {0};
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            char *name = sk_xstrdup(e->d_name);
            sk_vec_push(&names, name);
        }
    closedir(d);
    if (names.len)
        qsort(names.ptr, names.len, sizeof *names.ptr, by_name);
    for (size_t i = 0; i < names.len; i++) {
        char *path = sk_path_join(dir, names.ptr[i]);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode) && depth < MAX_DEPTH && !seen(ids, &st))
                walk(fs, ids, path, depth + 1);
            else if (S_ISREG(st.st_mode) && font_file(names.ptr[i]) && !seen(ids, &st))
                add_file(fs, path);
        }
        free(path);
        free(names.ptr[i]);
    }
    sk_vec_free(&names);
}

static void scan_root(sk_fonts *fs, seen_set *ids, const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode) && !seen(ids, &st))
        walk(fs, ids, dir, 0);
}

sk_fonts *sk_fonts_scan(const char *const *dirs)
{
    sk_fonts *fs = sk_xcalloc(1, sizeof *fs);
    seen_set ids = {0};
    if (dirs) {
        for (; *dirs; dirs++)
            scan_root(fs, &ids, *dirs);
    } else {
        sk_env env;
        sk_env_current(&env);
        bool mac = env.os == SK_OS_MACOS;
        static const char *const mac_dirs[] = {"/System/Library/Fonts", "/Library/Fonts"};
        static const char *const mac_home[] = {"Library/Fonts"};
        static const char *const linux_dirs[] = {"/usr/share/fonts", "/usr/local/share/fonts"};
        static const char *const linux_home[] = {".local/share/fonts", ".fonts"};
        const char *const *sys = mac ? mac_dirs : linux_dirs;
        const char *const *home = mac ? mac_home : linux_home;
        size_t nsys = mac ? SK_LEN(mac_dirs) : SK_LEN(linux_dirs), nhome = mac ? SK_LEN(mac_home) : SK_LEN(linux_home);
        for (size_t i = 0; i < nsys; i++)
            scan_root(fs, &ids, sys[i]);
        for (size_t i = 0; env.home && i < nhome; i++) {
            char *dir = sk_path_join(env.home, home[i]);
            scan_root(fs, &ids, dir);
            free(dir);
        }
    }
    sk_vec_free(&ids);
    return fs;
}

void sk_fonts_free(sk_fonts *fs)
{
    if (!fs)
        return;
    for (size_t i = 0; i < fs->faces.len; i++) {
        free(fs->faces.ptr[i].path);
        sk_font_close(fs->faces.ptr[i].font);
    }
    sk_vec_free(&fs->faces);
    free(fs);
}

size_t sk_fonts_count(const sk_fonts *fs)
{
    return fs ? fs->faces.len : 0;
}

const sk_font_info *sk_fonts_info(const sk_fonts *fs, size_t i)
{
    return fs && i < fs->faces.len ? &fs->faces.ptr[i].info : NULL;
}

const sk_font *sk_text_fonts_face(sk_fonts *fs, size_t i)
{
    face *e = &fs->faces.ptr[i];
    if (!e->font && !e->dead) {
        e->font = sk_font_open_file(e->path, e->index);
        e->dead = !e->font;
    }
    return e->font;
}

bool sk_text_fonts_is_family(const sk_fonts *fs, size_t i, const char *family)
{
    const face *e = &fs->faces.ptr[i];
    return sk_text_name_eq(e->info.family, family) ||
           (e->legacy_family[0] && sk_text_name_eq(e->legacy_family, family));
}

/* CSS's order for weights: for 400, 400 to 500 upwards, then lighter,
 * then heavier; for 700, heavier upwards, then lighter. */
static long weight_distance(int w, int want)
{
    if (want <= 500) {
        if (w >= want && w <= 500)
            return w - want;
        return w < want ? 1000 + (want - w) : 2000 + (w - 500);
    }
    return w >= want ? w - want : 1000 + (want - w);
}

/* How far a face is from style: width first, as CSS matches, then the
 * slant, then the weight. */
static long style_distance(const face *e, int style)
{
    bool italic = style & SK_STYLE_ITALIC;
    long d = weight_distance(e->info.weight, style & SK_STYLE_BOLD ? 700 : 400);
    if (e->info.italic != italic)
        d += 10000;
    return d + 100000L * labs((long)e->width - 5);
}

const sk_font *sk_fonts_find(sk_fonts *fs, const char *family, int style)
{
    if (!fs || !family || !*family)
        return NULL;
    for (;;) {
        size_t best = SIZE_MAX;
        long best_d = LONG_MAX;
        for (size_t i = 0; i < fs->faces.len; i++) {
            if (fs->faces.ptr[i].dead || !sk_text_fonts_is_family(fs, i, family))
                continue;
            long d = style_distance(&fs->faces.ptr[i], style);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best == SIZE_MAX)
            return NULL;
        /* A face that no longer opens is marked dead: try the next. */
        const sk_font *f = sk_text_fonts_face(fs, best);
        if (f)
            return f;
    }
}
