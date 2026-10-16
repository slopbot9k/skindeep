/* resolveCommand of app/cli.js (decision 0003). Node's path.join and
 * path.resolve normalize what they return, so this does the same: no
 * "." or empty segments, ".." applied without following links. */
#include "command.h"

#include "../base/str.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool is_exec(const char *p)
{
    struct stat st;
    return access(p, X_OK) == 0 && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool is_dotdot(const sk_buf *out, size_t start)
{
    const char *seg = out->ptr + start;
    if (*seg == '/')
        seg++;
    return !strcmp(seg, "..");
}

/* p as path.normalize gives it, without a trailing slash. */
static char *normalize(const char *p)
{
    bool abs = *p == '/';
    sk_buf out = {0};
    SK_VEC(size_t) starts = {0};
    for (const char *s = p; *s;) {
        if (*s == '/') {
            s++;
            continue;
        }
        size_t n = strcspn(s, "/");
        bool dot = n == 1 && s[0] == '.', dotdot = n == 2 && s[0] == '.' && s[1] == '.';
        if (dotdot && starts.len && !is_dotdot(&out, starts.ptr[starts.len - 1])) {
            out.len = starts.ptr[--starts.len];
            out.ptr[out.len] = 0;
        } else if (!dot && !(dotdot && abs)) {
            sk_vec_push(&starts, out.len);
            if (out.len || abs)
                sk_buf_putc(&out, '/');
            sk_buf_append(&out, s, n);
        }
        s += n;
    }
    sk_vec_free(&starts);
    if (!out.len)
        sk_buf_append_c(&out, abs ? "/" : ".");
    return out.ptr;
}

static char *join(const char *dir, size_t n, const char *name)
{
    sk_buf b = {0};
    sk_buf_append(&b, dir, n);
    sk_buf_putc(&b, '/');
    sk_buf_append_c(&b, name);
    char *p = normalize(b.ptr);
    sk_buf_free(&b);
    return p;
}

static char *cwd(void)
{
    for (size_t size = 256;; size *= 2) {
        char *buf = sk_xmalloc(size);
        if (getcwd(buf, size))
            return buf;
        free(buf);
        if (errno != ERANGE)
            return NULL;
    }
}

char *sk_resolve_command(const char *cmd, const char *path)
{
    if (!cmd || !*cmd)
        return NULL;
    if (strchr(cmd, '/')) {
        if (!is_exec(cmd))
            return NULL;
        if (*cmd == '/')
            return normalize(cmd);
        char *here = cwd();
        if (!here)
            return NULL;
        char *p = join(here, strlen(here), cmd);
        free(here);
        return p;
    }
    for (const char *s = path ? path : ""; *s;) {
        size_t n = strcspn(s, ":");
        if (n) {
            char *p = join(s, n, cmd);
            if (is_exec(p))
                return p;
            free(p);
        }
        s += n;
        if (*s == ':')
            s++;
    }
    return NULL;
}
