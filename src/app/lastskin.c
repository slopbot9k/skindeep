#include "lastskin.h"

#include "../base/arena.h"
#include "../base/json.h"
#include "../sessions/files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *sk_last_dir(const sk_env *e)
{
    char *state = sk_path_state(e);
    char *d = state ? sk_path_join(state, "last-skins") : NULL;
    free(state);
    return d;
}

int sk_last_write(const char *dir, pid_t pid, const char *spec, double now)
{
    if (sk_mkdirs(dir, 0777) != 0)
        return -1;
    sk_buf tmp = {0}, f = {0}, text = {0};
    sk_buf_printf(&tmp, "%s/.%ld.tmp", dir, (long)pid);
    sk_buf_printf(&f, "%s/%ld", dir, (long)pid);
    sk_jw w;
    sk_jw_init(&w, &text, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("t"));
    sk_jw_number(&w, now);
    sk_jw_key(&w, SK_STR("spec"));
    sk_jw_string(&w, sk_str_c(spec));
    sk_jw_object_end(&w);
    sk_buf_putc(&text, '\n');

    int fd = open(tmp.ptr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    bool ok = fd >= 0;
    for (size_t done = 0; ok && done < text.len;) {
        ssize_t n = write(fd, text.ptr + done, text.len - done);
        if (n > 0)
            done += (size_t)n;
        else if (n == 0 || errno != EINTR)
            ok = false;
    }
    if (fd >= 0 && close(fd) != 0)
        ok = false;
    if (ok && rename(tmp.ptr, f.ptr) != 0)
        ok = false;
    if (!ok && fd >= 0) {
        int saved = errno;
        unlink(tmp.ptr);
        errno = saved;
    }
    sk_buf_free(&text);
    sk_buf_free(&tmp);
    sk_buf_free(&f);
    return ok ? 0 : -1;
}

/* /^\d+$/ */
static bool record_name(const char *n)
{
    if (!*n)
        return false;
    for (; *n; n++)
        if (*n < '0' || *n > '9')
            return false;
    return true;
}

static bool read_record(const char *path, sk_last_record *out)
{
    sk_buf text = {0};
    bool ok = sk_files_read(path, 1 << 20, &text);
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *r = ok ? sk_json_parse(&arena, text.ptr, text.len, NULL) : NULL;
    const sk_json *t = sk_json_get(r, "t");
    sk_str spec;
    ok = t && t->type == SK_JSON_NUMBER && sk_json_as_string(sk_json_get(r, "spec"), &spec) && spec.len &&
         !memchr(spec.ptr, 0, spec.len);
    if (ok) {
        out->t = t->number;
        out->spec = sk_xstrndup(spec.ptr, spec.len);
    }
    sk_arena_free(&arena);
    sk_buf_free(&text);
    return ok;
}

void sk_last_read(const char *dir, sk_last_records *out)
{
    sk_strv names = {0};
    size_t from = out->len;
    if (sk_files_list(dir, &names)) {
        for (size_t i = 0; i < names.len; i++) {
            if (!record_name(names.ptr[i]))
                continue;
            char *path = sk_path_join(dir, names.ptr[i]);
            sk_last_record r;
            if (read_record(path, &r)) {
                r.pid = (pid_t)strtol(names.ptr[i], NULL, 10);
                sk_vec_push(out, r);
            }
            free(path);
        }
    }
    sk_strv_free(&names);
    /* Newest first, ties in the order read, as Array.prototype.sort is
     * stable. */
    for (size_t i = from + 1; i < out->len; i++) {
        sk_last_record r = out->ptr[i];
        size_t j = i;
        for (; j > from && out->ptr[j - 1].t < r.t; j--)
            out->ptr[j] = out->ptr[j - 1];
        out->ptr[j] = r;
    }
}

void sk_last_records_free(sk_last_records *r)
{
    for (size_t i = 0; i < r->len; i++)
        free(r->ptr[i].spec);
    sk_vec_free(r);
}

bool sk_last_pick(const sk_last_records *r, double race_ms, const char **spec, sk_strv *choices)
{
    *spec = NULL;
    if (!r->len)
        return false;
    for (size_t i = 0; i < r->len; i++) {
        if (r->ptr[0].t - r->ptr[i].t > race_ms)
            continue;
        bool seen = false;
        for (size_t k = 0; k < choices->len && !seen; k++)
            seen = !strcmp(choices->ptr[k], r->ptr[i].spec);
        if (!seen)
            sk_strv_push(choices, r->ptr[i].spec);
    }
    if (choices->len == 1) {
        *spec = r->ptr[0].spec;
        sk_strv_free(choices);
    }
    return true;
}

void sk_last_prune(const char *dir, const sk_last_records *r, double race_ms)
{
    for (size_t i = 0; i < r->len; i++) {
        if (r->ptr[0].t - r->ptr[i].t <= race_ms)
            continue;
        sk_buf f = {0};
        sk_buf_printf(&f, "%s/%ld", dir, (long)r->ptr[i].pid);
        unlink(f.ptr);
        sk_buf_free(&f);
    }
}

int sk_last_forget(const char *dir)
{
    sk_strv names = {0};
    int rc = 0, err = 0;
    if (sk_files_list(dir, &names)) {
        for (size_t i = 0; i < names.len; i++) {
            char *path = sk_path_join(dir, names.ptr[i]);
            if (unlink(path) != 0 && errno != ENOENT && !rc) {
                rc = -1;
                err = errno;
            }
            free(path);
        }
    }
    sk_strv_free(&names);
    errno = err;
    return rc;
}
