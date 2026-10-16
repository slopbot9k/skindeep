/* procinfo.h from /proc (decision 0036), as app/sessions.js reads it:
 * the listing for the table, /proc/<pid>/stat for the parent, start and
 * name, the owner of /proc/<pid>, /proc/<pid>/cmdline and the cwd link.
 * The start in ms counts from btime in /proc/stat. */
#include "../procinfo.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The whole of a /proc file, whose size stat does not tell. */
static bool slurp(const char *path, sk_buf *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    for (;;) {
        sk_buf_reserve(out, 4096);
        ssize_t n = read(fd, out->ptr + out->len, out->cap - out->len - 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            if (out->ptr)
                out->ptr[out->len] = 0;
            return n == 0;
        }
        out->len += (size_t)n;
    }
}

static uint64_t boot_ms;
static pthread_once_t boot_once = PTHREAD_ONCE_INIT;

static void read_boot(void)
{
    sk_buf b = {0};
    if (slurp("/proc/stat", &b)) {
        const char *line = sk_buf_cstr(&b);
        while (line && strncmp(line, "btime ", 6) != 0) {
            line = strchr(line, '\n');
            if (line)
                line++;
        }
        uint64_t s;
        if (line) {
            sk_str v = sk_str_c(line + 6);
            size_t nl = sk_str_find(v, '\n');
            if (sk_str_to_u64(sk_str_sub(v, 0, nl), &s))
                boot_ms = s * 1000u;
        }
    }
    sk_buf_free(&b);
}

/* The fields of /proc/<pid>/stat this file needs. The name in
 * parentheses may hold spaces and parentheses, so the fields after it
 * start after the last ')'. */
static bool read_stat(pid_t pid, sk_buf *comm, pid_t *ppid, uint64_t *start)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    sk_buf b = {0};
    bool ok = slurp(path, &b);
    const char *s = sk_buf_cstr(&b);
    const char *open_paren = ok ? strchr(s, '(') : NULL;
    const char *close_paren = ok ? strrchr(s, ')') : NULL;
    ok = open_paren && close_paren && close_paren > open_paren && close_paren[1] == ' ';
    uint64_t parent = 0, ticks = 0;
    if (ok) {
        if (comm)
            sk_buf_append(comm, open_paren + 1, (size_t)(close_paren - open_paren - 1));
        /* Fields 3 (state) onwards; ppid is field 4, starttime field 22. */
        sk_str rest = sk_str_c(close_paren + 2);
        int field = 3;
        for (; field <= 22 && rest.len; field++) {
            size_t sp = sk_str_find(rest, ' ');
            sk_str tok = sk_str_sub(rest, 0, sp);
            if ((field == 4 && !sk_str_to_u64(tok, &parent)) || (field == 22 && !sk_str_to_u64(tok, &ticks)))
                break;
            rest = sp == SIZE_MAX ? sk_str_n(NULL, 0) : sk_str_sub(rest, sp + 1, SIZE_MAX);
        }
        ok = field > 22;
    }
    sk_buf_free(&b);
    if (ppid)
        *ppid = (pid_t)parent;
    if (start)
        *start = ticks;
    return ok;
}

static bool owner(pid_t pid, uid_t *uid)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld", (long)pid);
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    *uid = st.st_uid;
    return true;
}

bool sk_procinfo_list(sk_procinfo_owners *out)
{
    DIR *d = opendir("/proc");
    if (!d)
        return false;
    struct dirent *e;
    while ((e = readdir(d))) {
        uint64_t pid;
        uid_t uid;
        if (sk_str_to_u64(sk_str_c(e->d_name), &pid) && pid > 0 && pid <= INT32_MAX &&
            owner((pid_t)pid, &uid))
            sk_vec_push(out, ((sk_procinfo_owner){(pid_t)pid, uid}));
    }
    closedir(d);
    return true;
}

bool sk_procinfo_read(pid_t pid, sk_procinfo *out)
{
    if (!read_stat(pid, NULL, &out->ppid, &out->start) || !owner(pid, &out->uid))
        return false;
    pthread_once(&boot_once, read_boot);
    long hz = sysconf(_SC_CLK_TCK);
    out->started_ms = boot_ms + out->start * 1000u / (uint64_t)(hz > 0 ? hz : 100);
    return true;
}

bool sk_procinfo_name(pid_t pid, sk_buf *out)
{
    return read_stat(pid, out, NULL, NULL);
}

bool sk_procinfo_argv(pid_t pid, sk_strv *out)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/cmdline", (long)pid);
    sk_buf b = {0};
    if (!slurp(path, &b)) {
        sk_buf_free(&b);
        return false;
    }
    /* NUL-separated, the last one ending in a NUL unless the process
     * rewrote its arguments: the empty piece after it is not an
     * argument, as split('\0') with the last empty piece dropped. */
    size_t at = 0;
    while (at < b.len) {
        const char *nul = memchr(b.ptr + at, 0, b.len - at);
        size_t n = nul ? (size_t)(nul - (b.ptr + at)) : b.len - at;
        sk_strv_push_n(out, b.ptr + at, n);
        at += n + 1;
    }
    sk_buf_free(&b);
    return true;
}

char *sk_procinfo_cwd(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/cwd", (long)pid);
    for (size_t cap = 256; cap <= 65536; cap *= 4) {
        char *buf = sk_xmalloc(cap);
        ssize_t n = readlink(path, buf, cap);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        if ((size_t)n < cap) {
            buf[n] = 0;
            return buf;
        }
        free(buf);
    }
    return NULL;
}
