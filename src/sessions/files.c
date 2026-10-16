/* Shared by the ports of app/profiles.js, app/sessions.js and
 * app/board.js (decisions 0025, 0027, 0029). */
#include "files.h"

#include "../platform/file.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

bool sk_files_list(const char *dir, sk_strv *names)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    size_t from = names->len;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            sk_strv_push(names, e->d_name);
    closedir(d);
    if (names->len > from)
        qsort(names->ptr + from, names->len - from, sizeof *names->ptr, by_name);
    return true;
}

bool sk_files_read(const char *path, size_t cap, sk_buf *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return false;
    }
    size_t from = out->len;
    for (;;) {
        sk_buf_reserve(out, 65536);
        ssize_t n = read(fd, out->ptr + out->len, out->cap - out->len - 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            out->ptr[out->len] = 0;
            return n == 0;
        }
        out->len += (size_t)n;
        if (out->len - from > cap) {
            close(fd);
            out->len = from;
            out->ptr[from] = 0;
            return false;
        }
    }
}

bool sk_files_mtime_ms(const char *path, double *ms)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    *ms = sk_stat_mtime_ms(&st);
    return true;
}

bool sk_files_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

char *sk_files_resolve(const char *path)
{
    sk_buf whole = {0};
    if (path[0] != '/') {
        char cwd[PATH_MAX];
        sk_buf_append_c(&whole, getcwd(cwd, sizeof cwd) ? cwd : "/");
        sk_buf_putc(&whole, '/');
    }
    sk_buf_append_c(&whole, path);
    /* Each part kept is its offset in out; .. drops the last one. */
    sk_buf out = {0};
    SK_VEC(size_t) at = {0};
    sk_str rest = sk_buf_str(&whole);
    while (rest.len) {
        size_t slash = sk_str_find(rest, '/');
        sk_str part = sk_str_sub(rest, 0, slash);
        rest = slash == SIZE_MAX ? sk_str_n(NULL, 0) : sk_str_sub(rest, slash + 1, SIZE_MAX);
        if (!part.len || sk_str_eq_c(part, "."))
            continue;
        if (sk_str_eq_c(part, "..")) {
            if (at.len)
                out.len = at.ptr[--at.len];
            continue;
        }
        sk_vec_push(&at, out.len);
        sk_buf_putc(&out, '/');
        sk_buf_append_str(&out, part);
    }
    if (!out.len)
        sk_buf_putc(&out, '/');
    out.ptr[out.len] = 0;
    sk_vec_free(&at);
    sk_buf_free(&whole);
    return out.ptr;
}
