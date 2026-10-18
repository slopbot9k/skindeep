/* Port of app/runtime.js (decisions 0016, 0022, 0030). The directory
 * rules are src/base/paths.h's, for both systems (decision 0036). */
#include "runtime.h"

#include "../base/paths.h"
#include "../base/strv.h"
#include "options.h"
#include "js.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Digits as Number() reads them; UINT64_MAX when they overflow. */
static bool pid_digits(sk_str s, uint64_t *pid)
{
    if (s.len == 0)
        return false;
    for (size_t i = 0; i < s.len; i++)
        if (s.ptr[i] < '0' || s.ptr[i] > '9')
            return false;
    if (!sk_str_to_u64(s, pid))
        *pid = UINT64_MAX;
    return true;
}

/* kill(pid, 0) succeeds, or is refused for another user's process.
 * Node's process.kill threw on a pid beyond 32 bits, which counted as
 * dead, and so it does here. */
static bool alive(uint64_t pid)
{
    if (pid > INT32_MAX)
        return false;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

/* The names in dir, without . and .., that keep says to. */
static bool list(const char *dir, sk_strv *names, bool (*keep)(const char *name))
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && (!keep || keep(e->d_name)))
            sk_strv_push(names, e->d_name);
    }
    closedir(d);
    return true;
}

int sk_runtime_remove(const char *path)
{
    if (!path)
        return 0;
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode))
        return unlink(path) == 0 || errno == ENOENT ? 0 : -1;
    sk_strv names = {0};
    if (!list(path, &names, NULL))
        return errno == ENOENT ? 0 : -1;
    int rc = 0;
    for (size_t i = 0; i < names.len && rc == 0; i++) {
        char *p = sk_path_join(path, names.ptr[i]);
        rc = sk_runtime_remove(p);
        int saved = errno;
        free(p);
        errno = saved;
    }
    int saved = errno;
    sk_strv_free(&names);
    errno = saved;
    if (rc)
        return -1;
    return rmdir(path) == 0 || errno == ENOENT ? 0 : -1;
}

void sk_runtime_sweep_links(const char *root)
{
    char *dir = sk_path_join(root, "pids");
    sk_strv names = {0};
    if (list(dir, &names, NULL)) {
        for (size_t i = 0; i < names.len; i++) {
            char *p = sk_path_join(dir, names.ptr[i]);
            if (access(p, F_OK) != 0)
                unlink(p);
            free(p);
        }
    }
    sk_strv_free(&names);
    free(dir);
}

/* p normalized, with no slash at its end but the root's. */
static void canonical(sk_buf *out, sk_str p)
{
    sk_js_normalize(out, p);
    if (out->len > 1 && out->ptr[out->len - 1] == '/')
        out->ptr[--out->len] = 0;
}

/* given's dir is the root. Both are compared normalized: the launcher
 * builds given from $TMPDIR, which ends with a slash on macOS. */
static bool in_root(const char *given, const char *root)
{
    if (!given || !*given)
        return false;
    sk_buf a = {0}, b = {0};
    canonical(&a, sk_js_dirname(sk_str_c(given)));
    canonical(&b, sk_str_c(root));
    bool same = !strcmp(a.ptr, b.ptr);
    sk_buf_free(&a);
    sk_buf_free(&b);
    return same;
}

char *sk_runtime_create(const sk_env *e, const char *given)
{
    char *root = sk_path_runtime_base(e), *dir = NULL;
    sk_strv names = {0};
    uint64_t self = (uint64_t)getpid();
    int rc = -1;
    if (sk_mkdirs(root, 0700) != 0)
        goto out;
    /* Another user may have made it first, or a link to a folder of
     * this user's, whose old pids' dirs the sweep would remove. */
    if (!sk_path_private_dir(root, true)) {
        sk_tell("%s is not a folder of this user's alone, so it is not used: remove it, or set XDG_RUNTIME_DIR",
                root);
        errno = EPERM;
        goto out;
    }
    if (!list(root, &names, NULL))
        goto out;
    for (size_t i = 0; i < names.len; i++) {
        uint64_t pid = 0;
        if (!pid_digits(sk_str_c(names.ptr[i]), &pid) || pid == self || alive(pid))
            continue;
        char *p = sk_path_join(root, names.ptr[i]);
        int failed = sk_runtime_remove(p), saved = errno;
        free(p);
        errno = saved;
        if (failed)
            goto out;
    }
    sk_runtime_sweep_links(root);
    if (in_root(given, root)) {
        dir = sk_xstrdup(given);
    } else {
        sk_buf name = {0};
        sk_buf_printf(&name, "%llu", (unsigned long long)self);
        dir = sk_path_join(root, name.ptr);
        sk_buf_free(&name);
    }
    rc = sk_mkdirs(dir, 0700);
out:;
    int saved = errno;
    sk_strv_free(&names);
    free(root);
    if (rc != 0) {
        free(dir);
        dir = NULL;
    }
    errno = saved;
    return dir;
}

char *sk_runtime_log_dir(const sk_env *e)
{
    return sk_path_state(e);
}

char *sk_runtime_settings_dir(const sk_env *e)
{
    char *state = sk_path_state(e);
    char *dir = state ? sk_path_join(state, "settings") : NULL;
    free(state);
    return dir;
}

static bool is_log(const char *name)
{
    return sk_str_suffix(sk_str_c(name), SK_STR(".log"));
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* logNames: the .log files of the log dir, sorted. */
static void log_names(const char *dir, sk_strv *names)
{
    list(dir, names, is_log);
    if (names->len)
        qsort(names->ptr, names->len, sizeof *names->ptr, by_name);
}

/* rmSync(p, { force: true }): what is already gone is fine. */
static int remove_log(const char *dir, const char *name)
{
    char *p = sk_path_join(dir, name);
    int rc = unlink(p) == 0 || errno == ENOENT ? 0 : -1, saved = errno;
    free(p);
    errno = saved;
    return rc;
}

int sk_runtime_rotate_logs(const sk_env *e)
{
    char *dir = sk_runtime_log_dir(e);
    if (!dir)
        return 0;
    sk_strv names = {0};
    log_names(dir, &names);
    int rc = 0;
    for (size_t i = 0; rc == 0 && i + SK_RUNTIME_KEEP_LOGS < names.len; i++)
        rc = remove_log(dir, names.ptr[i]);
    int saved = errno;
    sk_strv_free(&names);
    free(dir);
    errno = saved;
    return rc;
}

/* The pid of a log named /-(\d+)\.log$/. */
static bool log_pid(const char *name, uint64_t *pid)
{
    sk_str s = sk_str_c(name);
    s.len -= 4;
    size_t i = s.len;
    while (i > 0 && s.ptr[i - 1] >= '0' && s.ptr[i - 1] <= '9')
        i--;
    if (i == s.len || i == 0 || s.ptr[i - 1] != '-')
        return false;
    return pid_digits(sk_str_sub(s, i, s.len), pid);
}

int sk_runtime_clear_logs(const sk_env *e)
{
    char *dir = sk_runtime_log_dir(e);
    if (!dir)
        return 0;
    sk_strv names = {0};
    log_names(dir, &names);
    int n = 0;
    for (size_t i = 0; i < names.len && n >= 0; i++) {
        uint64_t pid = 0;
        if (!log_pid(names.ptr[i], &pid) || alive(pid))
            continue;
        n = remove_log(dir, names.ptr[i]) == 0 ? n + 1 : -1;
    }
    int saved = errno;
    sk_strv_free(&names);
    free(dir);
    errno = saved;
    return n;
}
