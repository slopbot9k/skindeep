#include "opener.h"

#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool sk_opener_file_path(const char *uri, sk_buf *out)
{
    sk_buf_clear(out);
    if (strncasecmp(uri, "file://", 7) != 0)
        return false;
    const char *rest = uri + 7, *slash = strchr(rest, '/');
    if (!slash)
        return false;
    size_t host = (size_t)(slash - rest);
    if (host && !(host == 9 && !strncasecmp(rest, "localhost", 9)))
        return false;
    if (strchr(slash, '#'))
        return false;
    for (const char *p = slash; *p; p++) {
        char c = *p;
        if (c == '%') {
            int hi = p[1] ? hex(p[1]) : -1, lo = hi >= 0 && p[2] ? hex(p[2]) : -1;
            if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) {
                sk_buf_clear(out);
                return false;
            }
            c = (char)(hi << 4 | lo);
            p += 2;
        }
        sk_buf_putc(out, c);
    }
    return true;
}

static bool ends_with(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    return n >= m && !strcasecmp(s + n - m, end);
}

bool sk_opener_allows(const char *uri, const char **why)
{
    *why = NULL;
    if (!strncasecmp(uri, "http://", 7) || !strncasecmp(uri, "https://", 8))
        return true;
    if (strncasecmp(uri, "file:", 5) != 0) {
        *why = "not an http, https or file link";
        return false;
    }
    sk_buf path = {0};
    bool ok = sk_opener_file_path(uri, &path);
    if (!ok) {
        *why = "not a file of this machine";
    } else {
        /* A trailing slash still names the bundle. */
        while (path.len > 1 && path.ptr[path.len - 1] == '/')
            path.ptr[--path.len] = 0;
        struct stat st;
        bool there = stat(sk_buf_cstr(&path), &st) == 0;
        if (there && S_ISREG(st.st_mode) && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            *why = "a program";
            ok = false;
        } else if (there && S_ISDIR(st.st_mode) && ends_with(sk_buf_cstr(&path), ".app")) {
            *why = "an application";
            ok = false;
        } else if (ends_with(sk_buf_cstr(&path), ".desktop") || ends_with(sk_buf_cstr(&path), ".terminal")) {
            /* A launcher a desktop runs, and Terminal's settings, which
             * run their commands, executable or not. */
            *why = "a launcher";
            ok = false;
        }
    }
    sk_buf_free(&path);
    return ok;
}
