#include "paths.h"

#include "str.h"

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

sk_os sk_os_current(void)
{
    struct utsname u;
    if (uname(&u) == 0 && strcmp(u.sysname, "Darwin") == 0)
        return SK_OS_MACOS;
    return SK_OS_LINUX;
}

static const char *env(const char *name)
{
    const char *v = getenv(name);
    return v && *v ? v : NULL;
}

void sk_env_current(sk_env *e)
{
    e->os = sk_os_current();
    e->home = env("HOME");
    if (!e->home) {
        struct passwd *pw = getpwuid(getuid());
        e->home = pw && pw->pw_dir && *pw->pw_dir ? pw->pw_dir : NULL;
    }
    e->xdg_state_home = env("XDG_STATE_HOME");
    e->xdg_cache_home = env("XDG_CACHE_HOME");
    e->xdg_runtime_dir = env("XDG_RUNTIME_DIR");
    e->tmpdir = env("TMPDIR");
    e->uid = (unsigned long)getuid();
}

static bool set(const char *v)
{
    return v && *v;
}

char *sk_path_join(const char *a, const char *b)
{
    sk_buf out = {0};
    sk_buf_append_c(&out, a);
    if (out.len && out.ptr[out.len - 1] != '/')
        sk_buf_putc(&out, '/');
    while (*b == '/')
        b++;
    sk_buf_append_c(&out, b);
    return out.ptr;
}

static char *under_home(const sk_env *e, const char *rel)
{
    if (!set(e->home))
        return NULL;
    char *dir = sk_path_join(e->home, rel);
    char *p = sk_path_join(dir, SK_APP_NAME);
    free(dir);
    return p;
}

char *sk_path_state(const sk_env *e)
{
    if (set(e->xdg_state_home))
        return sk_path_join(e->xdg_state_home, SK_APP_NAME);
    return under_home(e, e->os == SK_OS_MACOS ? "Library/Application Support" : ".local/state");
}

char *sk_path_cache(const sk_env *e)
{
    if (set(e->xdg_cache_home))
        return sk_path_join(e->xdg_cache_home, SK_APP_NAME);
    return under_home(e, e->os == SK_OS_MACOS ? "Library/Caches" : ".cache");
}

char *sk_path_runtime_base(const sk_env *e)
{
    if (set(e->xdg_runtime_dir))
        return sk_path_join(e->xdg_runtime_dir, SK_APP_NAME);
    sk_buf name = {0};
    sk_buf_printf(&name, "%s-%lu", SK_APP_NAME, e->uid);
    char *p = sk_path_join(set(e->tmpdir) ? e->tmpdir : "/tmp", name.ptr);
    sk_buf_free(&name);
    return p;
}

bool sk_path_private_dir(const char *path, bool tighten)
{
    struct stat st;
    if (!path || lstat(path, &st) != 0)
        return false;
    if (!S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
        errno = EPERM;
        return false;
    }
    if (!(st.st_mode & 077))
        return true;
    if (tighten && chmod(path, 0700) == 0 && lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && !(st.st_mode & 077))
        return true;
    errno = EPERM;
    return false;
}

int sk_mkdirs(const char *path, mode_t mode)
{
    if (!*path) {
        errno = ENOENT;
        return -1;
    }
    char *p = sk_xstrdup(path);
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        p[--n] = 0;
    for (char *s = p + 1;; s++) {
        bool end = *s == 0;
        if (*s == '/' || end) {
            *s = 0;
            if (mkdir(p, mode) != 0) {
                /* What exists already is fine as long as it is a
                 * directory, whatever mkdir said about it. */
                int saved = errno;
                struct stat st;
                if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    if (saved == EEXIST)
                        saved = ENOTDIR;
                    free(p);
                    errno = saved;
                    return -1;
                }
            }
            if (end)
                break;
            *s = '/';
        }
    }
    free(p);
    return 0;
}

bool sk_path_plain_name(const char *s, size_t n)
{
    if (!s || !n)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

bool sk_path_clean_absolute(const char *path)
{
    if (!path || path[0] != '/')
        return false;
    for (const char *p = path; *p;) {
        while (*p == '/')
            p++;
        size_t n = strcspn(p, "/");
        if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.'))
            return false;
        p += n;
    }
    return true;
}
