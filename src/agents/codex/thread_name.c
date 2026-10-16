/* A Codex thread's name (decision 0032), as
 * app/metrics/codex/thread-name.js. Codex CLI 0.162.1 appends one line
 * {"id":THREAD,"thread_name":NAME,"updated_at":TIME} to
 * <CODEX_HOME>/session_index.jsonl each time a thread is named, by the
 * user or by Codex itself, and the last line of a thread wins. */
#include "codex.h"

#include "../../base/paths.h"
#include "../../core/agentdirs.h"
#include "../../platform/procfds.h"
#include "../../platform/file.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool has(const char *s, size_t n, const char *needle, size_t m)
{
    for (size_t i = 0; i + m <= n; i++)
        if (!memcmp(s + i, needle, m))
            return true;
    return false;
}

char *sk_codex_thread_name(sk_str text, const char *id)
{
    size_t idn = strlen(id);
    char *name = NULL;
    if (!text.len)
        return NULL;
    sk_arena a;
    sk_arena_init(&a, 0);
    for (size_t from = 0; from <= text.len;) {
        const char *nl = memchr(text.ptr + from, '\n', text.len - from);
        size_t end = nl ? (size_t)(nl - text.ptr) : text.len;
        const char *line = text.ptr + from;
        size_t n = end - from;
        from = end + 1;
        /* Lines that cannot name the thread are not parsed. */
        if (!has(line, n, id, idn))
            continue;
        const sk_json *obj = sk_json_parse(&a, line, n, NULL);
        sk_str oid, tn;
        if (obj && sk_json_as_string(sk_json_get(obj, "id"), &oid) && sk_str_eq_c(oid, id) &&
            sk_json_as_string(sk_json_get(obj, "thread_name"), &tn)) {
            sk_buf b = {0};
            sk_codex_slice16(&b, sk_codex_trim(tn), SK_CODEX_NAME_MAX);
            free(name);
            name = b.len ? b.ptr : NULL;
            if (!b.len)
                sk_buf_free(&b);
        }
        sk_arena_reset(&a);
    }
    sk_arena_free(&a);
    return name;
}

void sk_codex_names_init(sk_codex_names *n, const sk_envp *env)
{
    memset(n, 0, sizeof *n);
    char *home = sk_codex_home(env);
    if (home)
        n->file = sk_path_join(home, "session_index.jsonl");
    free(home);
}

static bool read_all(const char *path, sk_buf *out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    sk_buf_clear(out);
    char chunk[65536];
    for (;;) {
        ssize_t r = read(fd, chunk, sizeof chunk);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            close(fd);
            return r == 0;
        }
        sk_buf_append(out, chunk, (size_t)r);
    }
}

const char *sk_codex_names_of(sk_codex_names *n, const char *id)
{
    free(n->name);
    n->name = NULL;
    if (!id || !*id)
        return NULL;
    struct stat st;
    bool readable = n->file && stat(n->file, &st) == 0;
    if (readable) {
        double mtime = sk_stat_mtime_ms(&st);
        if (!n->stamped || (long long)st.st_size != n->size || mtime != n->mtime) {
            readable = read_all(n->file, &n->text);
            n->size = (long long)st.st_size;
            n->mtime = mtime;
        }
    }
    n->stamped = readable;
    if (!readable)
        sk_buf_clear(&n->text);
    n->name = sk_codex_thread_name(sk_buf_str(&n->text), id);
    return n->name;
}

void sk_codex_names_free(sk_codex_names *n)
{
    free(n->file);
    free(n->name);
    sk_buf_free(&n->text);
    memset(n, 0, sizeof *n);
}
