/* sk_proc_open_files from the links of /proc/<pid>/fd, as
 * app/metrics/codex/discovery.js reads them. */
#include "../procfds.h"

#include <dirent.h>
#include <string.h>
#include <unistd.h>

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The link's target, which can be longer than any fixed buffer. */
static bool read_link(const char *link, sk_buf *out)
{
    for (size_t size = 256; size <= (size_t)1 << 20; size *= 2) {
        sk_buf_clear(out);
        sk_buf_reserve(out, size);
        ssize_t n = readlink(link, out->ptr, size);
        if (n < 0)
            return false;
        if ((size_t)n < size) {
            out->len = (size_t)n;
            out->ptr[n] = 0;
            return n > 0;
        }
    }
    return false;
}

void sk_proc_open_files(pid_t pid, sk_strv *paths)
{
    sk_buf dir = {0};
    sk_buf_printf(&dir, "/proc/%ld/fd", (long)pid);
    DIR *d = opendir(dir.ptr);
    if (!d) {
        sk_buf_free(&dir);
        return;
    }
    sk_strv fds = {0};
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            sk_strv_push(&fds, e->d_name);
    closedir(d);
    if (fds.len)
        qsort(fds.ptr, fds.len, sizeof *fds.ptr, by_name);
    sk_buf link = {0}, target = {0};
    for (size_t i = 0; i < fds.len; i++) {
        sk_buf_clear(&link);
        sk_buf_printf(&link, "%s/%s", dir.ptr, fds.ptr[i]);
        if (read_link(link.ptr, &target))
            sk_strv_push(paths, target.ptr);
    }
    sk_buf_free(&link);
    sk_buf_free(&target);
    sk_strv_free(&fds);
    sk_buf_free(&dir);
}
