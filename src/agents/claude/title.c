/* A Claude Code session's custom title, as app/metrics/claude/title.js
 * has it (decision 0032). Claude Code 2.1.296 records a /rename twice:
 * a transcript line {"type":"custom-title","customTitle":NAME,
 * "sessionId":ID}, and <project>/<session id>/custom-title.json holding
 * {"customTitle":NAME}. title_of reads the line, record writes both as
 * the CLI does. */
#include "internal.h"

#include "../../base/paths.h"
#include "../../base/utf8.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* A line of another session is not this one's. */
bool sk_claude_title_of(const sk_json *obj, const char *session, sk_buf *out)
{
    sk_str type, title, id;
    if (!sk_json_as_string(sk_json_get(obj, "type"), &type) || !sk_str_eq_c(type, "custom-title") ||
        !sk_json_as_string(sk_json_get(obj, "customTitle"), &title))
        return false;
    if (sk_json_as_string(sk_json_get(obj, "sessionId"), &id) && (!session || !sk_str_eq_c(id, session)))
        return false;
    size_t start = out->len;
    sk_claude_slice(out, sk_claude_trim(title), SK_CLAUDE_TITLE_MAX);
    return out->len > start;
}

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* JSON.stringify of an object of string members, the names and values
 * given in pairs. */
static void object_of(sk_buf *out, const char *const *kv, size_t n)
{
    sk_jw w;
    sk_jw_init(&w, out, 0);
    sk_jw_object(&w);
    for (size_t i = 0; i + 1 < n; i += 2) {
        sk_buf v = {0};
        sk_utf8_sanitize(&v, kv[i + 1], strlen(kv[i + 1]));
        sk_jw_key(&w, sk_str_c(kv[i]));
        sk_jw_string(&w, sk_buf_str(&v));
        sk_buf_free(&v);
    }
    sk_jw_object_end(&w);
}

/* Claude Code holds no fd on the transcript between its writes
 * (measured 2026-10-14), so one appended line cannot tear one of its
 * own. The json file is written whole through a temp file. */
int sk_claude_title_record(const char *transcript, const char *session, const char *name)
{
    const char *line_kv[] = {"type", "custom-title", "customTitle", name, "sessionId", session};
    sk_buf line = {0};
    object_of(&line, line_kv, SK_LEN(line_kv));
    sk_buf_putc(&line, '\n');
    int fd = open(transcript, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0666);
    int rc = fd < 0 ? -1 : write_all(fd, line.ptr, line.len);
    int saved = errno;
    if (fd >= 0)
        close(fd);
    sk_buf_free(&line);
    if (rc != 0) {
        errno = saved;
        return -1;
    }

    const char *slash = strrchr(transcript, '/');
    sk_buf dir = {0};
    if (slash)
        sk_buf_append(&dir, transcript, (size_t)(slash - transcript));
    else
        sk_buf_putc(&dir, '.');
    sk_buf_printf(&dir, "/%s", session);
    sk_buf file = {0}, tmp = {0}, text = {0};
    sk_buf_printf(&file, "%s/custom-title.json", dir.ptr);
    sk_buf_printf(&tmp, "%s.%ld.tmp", file.ptr, (long)getpid());
    const char *title_kv[] = {"customTitle", name};
    object_of(&text, title_kv, SK_LEN(title_kv));
    rc = -1;
    if (sk_mkdirs(dir.ptr, 0777) == 0) {
        fd = open(tmp.ptr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd >= 0) {
            rc = write_all(fd, text.ptr, text.len);
            saved = errno;
            close(fd);
            errno = saved;
            if (rc == 0 && rename(tmp.ptr, file.ptr) != 0)
                rc = -1;
            if (rc != 0) {
                saved = errno;
                unlink(tmp.ptr);
                errno = saved;
            }
        }
    }
    saved = errno;
    sk_buf_free(&dir);
    sk_buf_free(&file);
    sk_buf_free(&tmp);
    sk_buf_free(&text);
    errno = saved;
    return rc;
}
