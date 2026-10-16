/* The process tree from /proc (decision 0014): the walk of
 * app/metrics/proc.js through /proc/<pid>/task/<tid>/children, and the
 * CPU time and resident memory app/metrics/generic.js reads from stat
 * and statm, with the system's tick rate and page size in place of its
 * fixed 100 and 4096. */
#include "../proctree.h"

#include "../../base/str.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* As proc.js caps the walk. */
#define MAX_TREE 4096

typedef SK_VEC(pid_t) pid_vec;

/* The whole file, or false. */
static bool read_file(const char *path, sk_buf *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    char chunk[4096];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof chunk)) > 0)
        sk_buf_append(out, chunk, (size_t)n);
    close(fd);
    return n == 0;
}

static void push_children(pid_t pid, pid_vec *stack)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/task", (long)pid);
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *e;
    sk_buf kids = {0};
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        sk_buf p = {0};
        sk_buf_printf(&p, "/proc/%ld/task/%s/children", (long)pid, e->d_name);
        sk_buf_clear(&kids);
        if (read_file(p.ptr, &kids)) {
            sk_str rest = sk_buf_str(&kids);
            while ((rest = sk_str_trim(rest)).len) {
                size_t sp = sk_str_find(rest, ' ');
                uint64_t k;
                if (sk_str_to_u64(sk_str_sub(rest, 0, sp), &k) && k > 0)
                    sk_vec_push(stack, (pid_t)k);
                rest = sp == SIZE_MAX ? sk_str_n(NULL, 0) : sk_str_sub(rest, sp + 1, SIZE_MAX);
            }
        }
        sk_buf_free(&p);
    }
    sk_buf_free(&kids);
    closedir(d);
}

size_t sk_proc_tree(pid_t pid, pid_t *out, size_t max)
{
    char path[64];
    struct stat st;
    snprintf(path, sizeof path, "/proc/%ld", (long)pid);
    if (pid <= 0 || !max || stat(path, &st) != 0)
        return 0;
    pid_vec stack = {0};
    sk_vec_push(&stack, pid);
    size_t n = 0;
    while (stack.len && n < max) {
        pid_t p = stack.ptr[--stack.len];
        out[n++] = p;
        push_children(p, &stack);
    }
    sk_vec_free(&stack);
    return n;
}

/* Field i of the whitespace-separated s, counting from 1. */
static bool field(sk_str s, int i, uint64_t *out)
{
    for (int f = 1; s.len; f++) {
        s = sk_str_trim(s);
        size_t sp = sk_str_find(s, ' ');
        if (f == i)
            return sk_str_to_u64(sk_str_sub(s, 0, sp), out);
        if (sp == SIZE_MAX)
            break;
        s = sk_str_sub(s, sp + 1, SIZE_MAX);
    }
    return false;
}

/* utime plus stime, fields 14 and 15 of stat, in ticks. */
static uint64_t cpu_ticks(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    sk_buf b = {0};
    uint64_t utime = 0, stime = 0;
    if (read_file(path, &b) && b.len) {
        /* The command name may hold spaces and parentheses, so the fields
         * start after the last ')', at field 3. */
        const char *paren = strrchr(b.ptr, ')');
        if (paren) {
            sk_str rest = sk_str_c(paren + 1);
            if (!field(rest, 14 - 2, &utime) || !field(rest, 15 - 2, &stime))
                utime = stime = 0;
        }
    }
    sk_buf_free(&b);
    return utime + stime;
}

/* The resident pages, field 2 of statm. */
static uint64_t rss_pages(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/statm", (long)pid);
    sk_buf b = {0};
    uint64_t pages = 0;
    if (!read_file(path, &b) || !field(sk_buf_str(&b), 2, &pages))
        pages = 0;
    sk_buf_free(&b);
    return pages;
}

bool sk_proc_tree_usage(pid_t pid, uint64_t *cpu_ms, uint64_t *rss_bytes)
{
    *cpu_ms = *rss_bytes = 0;
    pid_t *pids = sk_xmalloc(MAX_TREE * sizeof *pids);
    size_t n = sk_proc_tree(pid, pids, MAX_TREE);
    uint64_t ticks = 0, pages = 0;
    for (size_t i = 0; i < n; i++) {
        ticks += cpu_ticks(pids[i]);
        pages += rss_pages(pids[i]);
    }
    free(pids);
    if (!n)
        return false;
    long hz = sysconf(_SC_CLK_TCK), page = sysconf(_SC_PAGESIZE);
    *cpu_ms = hz > 0 ? ticks * 1000 / (uint64_t)hz : 0;
    *rss_bytes = pages * (uint64_t)(page > 0 ? page : 4096);
    return true;
}
