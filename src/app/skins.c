#include "skins.h"

#include "../base/paths.h"
#include "../platform/file.h"
#include "../skin/compile.h"
#include "../skin/shipped.h"
#include "lastskin.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char *id;
    char *dir; /* its realpath */
} path_skin;

struct sk_skins {
    char *skinc, *cache, *shipped_dir, *home;
    SK_VEC(path_skin) paths;
    SK_VEC(char *) shipped; /* names, sorted */
    char *cur_id;
    char *cur_dir; /* the realpath of a current skin opened by path */
    sk_skinpack *cur;
    /* The watch: the current folder's files as compiled, and as last
     * refused. */
    uint64_t stamp, refused;
};

/* ---- The watch ---- */

#define STAMP_DEPTH 32

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

/* A sum over the files under dir of a hash of each one's path, size
 * and modification time, so the order a folder lists them in does not
 * count. A link to a file counts as the file, a link to a folder as
 * nothing, as the cache key has them. */
static uint64_t stamp_of(const char *dir, const char *rel, int depth)
{
    uint64_t sum = 0;
    DIR *d = depth < STAMP_DEPTH ? opendir(dir) : NULL;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char *full = sk_path_join(dir, e->d_name);
        char *name = *rel ? sk_path_join(rel, e->d_name) : sk_xstrdup(e->d_name);
        struct stat st;
        bool link = lstat(full, &st) == 0 && S_ISLNK(st.st_mode);
        if (stat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode) && !link) {
                sum += stamp_of(full, name, depth + 1);
            } else if (S_ISREG(st.st_mode)) {
                uint64_t h = fnv(1469598103934665603ull, name, strlen(name) + 1);
                uint64_t size = (uint64_t)st.st_size;
                double ms = sk_stat_mtime_ms(&st);
                h = fnv(h, &size, sizeof size);
                h = fnv(h, &ms, sizeof ms);
                sum += h;
            }
        }
        free(full);
        free(name);
    }
    if (d)
        closedir(d);
    return sum;
}

static char *dup_or_null(const char *s)
{
    return s ? sk_xstrdup(s) : NULL;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

sk_skins *sk_skins_new(const sk_skins_opts *o)
{
    sk_skins *r = sk_xcalloc(1, sizeof *r);
    r->skinc = dup_or_null(o ? o->skinc : NULL);
    r->cache = dup_or_null(o ? o->cache : NULL);
    r->shipped_dir = dup_or_null(o ? o->shipped_dir : NULL);
    r->home = dup_or_null(o ? o->home : NULL);
    for (size_t i = 0; i < sk_shipped_packs_count; i++) {
        const char *n = sk_shipped_packs[i].name;
        size_t len = strlen(n);
        if (len > 5 && !strcmp(n + len - 5, ".pack"))
            sk_vec_push(&r->shipped, sk_xstrndup(n, len - 5));
    }
    if (r->shipped.len > 1)
        qsort(r->shipped.ptr, r->shipped.len, sizeof *r->shipped.ptr, by_name);
    return r;
}

void sk_skins_free(sk_skins *r)
{
    if (!r)
        return;
    for (size_t i = 0; i < r->paths.len; i++) {
        free(r->paths.ptr[i].id);
        free(r->paths.ptr[i].dir);
    }
    sk_vec_free(&r->paths);
    for (size_t i = 0; i < r->shipped.len; i++)
        free(r->shipped.ptr[i]);
    sk_vec_free(&r->shipped);
    sk_skinpack_unref(r->cur);
    free(r->cur_id);
    free(r->cur_dir);
    free(r->skinc);
    free(r->cache);
    free(r->shipped_dir);
    free(r->home);
    free(r);
}

static bool is_shipped(const sk_skins *r, const char *id)
{
    for (size_t i = 0; i < r->shipped.len; i++)
        if (!strcmp(r->shipped.ptr[i], id))
            return true;
    return false;
}

static path_skin *path_of(const sk_skins *r, const char *id)
{
    for (size_t i = 0; i < r->paths.len; i++)
        if (!strcmp(r->paths.ptr[i].id, id))
            return &r->paths.ptr[i];
    return NULL;
}

/* The realpath of a shipped skin's own folder, malloc'd; NULL when
 * there is none. */
static char *shipped_real(const sk_skins *r, const char *id)
{
    if (!r->shipped_dir || !is_shipped(r, id))
        return NULL;
    char *d = sk_path_join(r->shipped_dir, id);
    char *real = realpath(d, NULL);
    free(d);
    return real;
}

/* The realpath of the folder a listed id stands for, malloc'd. */
static char *real_of_id(const sk_skins *r, const char *id)
{
    path_skin *p = path_of(r, id);
    return p ? sk_xstrdup(p->dir) : shipped_real(r, id);
}

static void make_current(sk_skins *r, const char *id, const char *dir, sk_skinpack *p)
{
    sk_skinpack_unref(r->cur);
    r->cur = p;
    char *nid = sk_xstrdup(id), *ndir = dup_or_null(dir);
    free(r->cur_id);
    free(r->cur_dir);
    r->cur_id = nid;
    r->cur_dir = ndir;
    r->stamp = ndir ? stamp_of(ndir, "", 0) : 0;
    r->refused = r->stamp;
}

/* A shipped skin's pack, through the loader. */
static sk_skinpack *load_shipped(const char *id, sk_buf *errors)
{
    const sk_embedded_file *e = sk_shipped_pack(id);
    if (!e) {
        sk_buf_printf(errors, "skins/%s: skin: directory not found\n", id);
        return NULL;
    }
    char err[300];
    sk_skinpack *p = sk_skinpack_new((uint8_t *)e->data, e->size, false, err, sizeof err);
    if (!p)
        sk_buf_printf(errors, "skins/%s: (pack): %s\n", id, err);
    return p;
}

/* A folder's pack, compiled by skinc or taken from the cache. */
static sk_skinpack *compile_dir(const sk_skins *r, const char *dir, sk_buf *errors)
{
    if (!r->skinc) {
        sk_buf_printf(errors, "%s: skin: skinc, which compiles a skin, is not installed beside skinned\n", dir);
        return NULL;
    }
    sk_skin_pack sp;
    size_t mark = errors->len;
    if (!sk_skin_compile(r->skinc, r->cache, dir, &sp, errors)) {
        if (errors->len == mark)
            sk_buf_printf(errors, "%s: skin: does not compile\n", dir);
        return NULL;
    }
    uint8_t *bytes = sp.bytes;
    size_t n = sp.len;
    sp.bytes = NULL;
    sk_skin_pack_free(&sp);
    char err[300];
    sk_skinpack *p = sk_skinpack_new(bytes, n, true, err, sizeof err);
    if (!p)
        sk_buf_printf(errors, "%s: (pack): %s\n", dir, err);
    return p;
}

bool sk_skins_select(sk_skins *r, const char *id, sk_buf *errors)
{
    path_skin *ps = path_of(r, id);
    if (!ps && !is_shipped(r, id)) {
        sk_buf_printf(errors, "no skin with id %s\n", id);
        return false;
    }
    char *dir = ps ? sk_xstrdup(ps->dir) : NULL;
    sk_skinpack *p = dir ? compile_dir(r, dir, errors) : load_shipped(id, errors);
    if (p)
        make_current(r, id, dir, p);
    free(dir);
    return p != NULL;
}

bool sk_skins_open(sk_skins *r, const char *dir, sk_buf *errors)
{
    char *real = realpath(dir, NULL);
    if (!real) {
        sk_buf_printf(errors, "%s: skin: directory not found\n", dir);
        return false;
    }
    /* A folder already listed is selected. */
    for (size_t i = 0; i < sk_skins_count(r); i++) {
        const char *id = sk_skins_at(r, i).id;
        char *x = real_of_id(r, id);
        bool same = x && !strcmp(x, real);
        free(x);
        if (same) {
            char *keep = sk_xstrdup(id);
            bool ok = sk_skins_select(r, keep, errors);
            free(keep);
            free(real);
            return ok;
        }
    }
    sk_skinpack *p = compile_dir(r, real, errors);
    if (!p) {
        free(real);
        return false;
    }
    char *id = sk_xstrdup(sk_skinpack_name(p));
    char *ship = shipped_real(r, id);
    path_skin *ps = path_of(r, id);
    if (ship && !strcmp(ship, real)) {
        /* The shipped folder itself, opened after a copy replaced it:
         * the shipped skin again. */
        if (ps) {
            free(ps->id);
            free(ps->dir);
            *ps = r->paths.ptr[--r->paths.len];
        }
        sk_skinpack_unref(p);
        sk_buf none = {0};
        p = load_shipped(id, &none);
        sk_buf_free(&none);
        if (p)
            make_current(r, id, NULL, p);
    } else {
        if (ps) {
            free(ps->dir);
            ps->dir = sk_xstrdup(real);
        } else {
            path_skin x = {sk_xstrdup(id), sk_xstrdup(real)};
            sk_vec_push(&r->paths, x);
        }
        make_current(r, id, real, p);
    }
    free(ship);
    free(id);
    free(real);
    return p != NULL;
}

bool sk_skins_start(sk_skins *r, const char *spec, sk_buf *errors)
{
    if (!spec) {
        if (!r->shipped.len) {
            sk_buf_printf(errors, "no shipped skins\n");
            return false;
        }
        const char *id = is_shipped(r, SK_SKINS_DEFAULT) ? SK_SKINS_DEFAULT : r->shipped.ptr[0];
        return sk_skins_select(r, id, errors);
    }
    if (strchr(spec, '/'))
        return sk_skins_open(r, spec, errors);
    if (!is_shipped(r, spec)) {
        /* A folder of the repository's skins/ that the build did not link
         * in, as registry.js took any folder there by name: opened as a
         * skin by path is, and compiled. */
        char *dir = r->shipped_dir && *spec != '.' ? sk_path_join(r->shipped_dir, spec) : NULL;
        bool ok = false;
        struct stat st;
        if (dir && stat(dir, &st) == 0 && S_ISDIR(st.st_mode))
            ok = sk_skins_open(r, dir, errors);
        else
            sk_buf_printf(errors, "skins/%s: skin: directory not found\n", spec);
        free(dir);
        return ok;
    }
    sk_skinpack *p = load_shipped(spec, errors);
    if (p)
        make_current(r, spec, NULL, p);
    return p != NULL;
}

/* main.js's shown(): a path under the home with ~. */
static char *shown(const sk_skins *r, const char *spec)
{
    size_t h = r->home ? strlen(r->home) : 0;
    sk_buf b = {0};
    if (h && !strncmp(spec, r->home, h) && spec[h] == '/')
        sk_buf_printf(&b, "~%s", spec + h);
    else
        sk_buf_append_c(&b, spec);
    char *s = sk_xstrdup(sk_buf_cstr(&b));
    sk_buf_free(&b);
    return s;
}

bool sk_skins_start_remembered(sk_skins *r, const char *spec, char **title, char **detail, sk_buf *errors)
{
    *title = *detail = NULL;
    if (spec) {
        sk_buf why = {0};
        if (sk_skins_start(r, spec, &why)) {
            sk_buf_free(&why);
            return true;
        }
        while (why.len && why.ptr[why.len - 1] == '\n')
            why.ptr[--why.len] = 0;
        char *s = shown(r, spec);
        sk_buf t = {0};
        sk_buf_printf(&t, "Remembered skin %s refused, using the default", s);
        free(s);
        *title = sk_xstrdup(sk_buf_cstr(&t));
        sk_buf_free(&t);
        *detail = sk_xstrdup(sk_buf_cstr(&why));
        sk_buf_free(&why);
    }
    return sk_skins_start(r, NULL, errors);
}

size_t sk_skins_count(const sk_skins *r)
{
    size_t n = r->shipped.len;
    for (size_t i = 0; i < r->paths.len; i++)
        if (!is_shipped(r, r->paths.ptr[i].id))
            n++;
    return n;
}

sk_skins_entry sk_skins_at(const sk_skins *r, size_t i)
{
    const char *id = NULL;
    if (i < r->shipped.len) {
        id = r->shipped.ptr[i];
    } else {
        size_t k = r->shipped.len;
        for (size_t j = 0; j < r->paths.len && !id; j++)
            if (!is_shipped(r, r->paths.ptr[j].id) && k++ == i)
                id = r->paths.ptr[j].id;
    }
    sk_skins_entry e = {id, NULL, false};
    if (!id)
        return e;
    const path_skin *p = path_of(r, id);
    e.location = p ? p->dir : NULL;
    e.current = r->cur_id && !strcmp(r->cur_id, id) &&
                (p ? r->cur_dir && !strcmp(r->cur_dir, p->dir) : !r->cur_dir);
    return e;
}

sk_skinpack *sk_skins_current(const sk_skins *r)
{
    return r->cur;
}

const char *sk_skins_current_id(const sk_skins *r)
{
    return r->cur_id;
}

const char *sk_skins_spec(const sk_skins *r)
{
    if (!r->cur_id)
        return NULL;
    const path_skin *p = path_of(r, r->cur_id);
    return p && r->cur_dir && !strcmp(p->dir, r->cur_dir) ? r->cur_dir : r->cur_id;
}

int sk_skins_remember(const sk_skins *r, const char *last_dir, pid_t pid, double now_ms)
{
    const char *spec = sk_skins_spec(r);
    if (!spec || !last_dir) {
        errno = EINVAL;
        return -1;
    }
    return sk_last_write(last_dir, pid, spec, now_ms);
}

sk_skins_change sk_skins_poll(sk_skins *r, sk_buf *errors)
{
    if (!r->cur_dir)
        return SK_SKINS_SAME;
    uint64_t s = stamp_of(r->cur_dir, "", 0);
    if (s == r->stamp || s == r->refused)
        return SK_SKINS_SAME;
    sk_skinpack *p = compile_dir(r, r->cur_dir, errors);
    if (!p) {
        r->refused = s;
        return SK_SKINS_REFUSED;
    }
    make_current(r, r->cur_id, r->cur_dir, p);
    return SK_SKINS_RELOADED;
}
