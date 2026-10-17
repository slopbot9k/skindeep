/* Port of parse, adoptUserFd and tell of app/cli.js, with the check of
 * the app's options that bin/skinned made before Electron started
 * (decision 0003). resolveCommand and the exit codes are
 * src/core/command.h's. */
#include "options.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool ours(const char *a)
{
    return !strcmp(a, "--skin") || !strncmp(a, "--skin=", 7) ||
           !strcmp(a, "--statusline=always") || !strcmp(a, "--statusline=chain-only") ||
           !strcmp(a, "--no-viz") || !strcmp(a, "--mute");
}

bool sk_options_parse(sk_options *o, char *const *argv, const sk_envp *env, sk_buf *err)
{
    const char *skin = env ? sk_envp_get(env, "SKINNED_SKIN") : NULL;
    const char *mute = env ? sk_envp_get(env, "SKINNED_MUTE") : NULL;
    *o = (sk_options){skin, SK_STATUSLINE_ALWAYS, true, mute && !strcmp(mute, "1"), NULL};
    size_t sep = argv[0] ? 1 : 0;
    for (; argv[sep] && strcmp(argv[sep], "--"); sep++) {
        if (argv[sep][0] == '-' && !ours(argv[sep])) {
            sk_buf_printf(err, "unknown option %s\n%s", argv[sep], SK_USAGE);
            return false;
        }
    }
    if (!argv[sep] || !argv[sep + 1]) {
        sk_buf_append_c(err, SK_USAGE);
        return false;
    }
    for (size_t i = 1; i < sep; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--skin") && i + 1 < sep)
            o->skin = argv[++i];
        else if (!strncmp(a, "--skin=", 7))
            o->skin = a + 7;
        else if (!strcmp(a, "--statusline=always"))
            o->statusline = SK_STATUSLINE_ALWAYS;
        else if (!strcmp(a, "--statusline=chain-only"))
            o->statusline = SK_STATUSLINE_CHAIN_ONLY;
        else if (!strcmp(a, "--no-viz"))
            o->viz = false;
        else if (!strcmp(a, "--mute"))
            o->mute = true;
    }
    /* An empty name is no skin, as the JavaScript's truthiness had it. */
    if (o->skin && !*o->skin)
        o->skin = NULL;
    o->cmd = argv + sep + 1;
    return true;
}

static atomic_int user_fd = 2;

int sk_tell_adopt(int fd)
{
    struct stat st;
    int to = 2;
    if (fd > 2 && fstat(fd, &st) == 0) {
        int copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (copy >= 0) {
            to = copy;
            close(fd);
        }
    }
    int old = atomic_exchange(&user_fd, to);
    if (old > 2 && old != to)
        close(old);
    return to;
}

void sk_tell_now(const char *a, const char *b)
{
    char line[640];
    int n = snprintf(line, sizeof line, "skinned: %s%s\n", a, b);
    if (n <= 0)
        return;
    size_t len = SK_MIN((size_t)n, sizeof line - 1);
    line[len - 1] = '\n';
    int fd = atomic_load(&user_fd);
    for (size_t done = 0; done < len;) {
        ssize_t w = write(fd, line + done, len - done);
        if (w <= 0)
            break;
        done += (size_t)w;
    }
}

void sk_tell(const char *fmt, ...)
{
    sk_buf line = {0};
    sk_buf_append_c(&line, "skinned: ");
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&line, fmt, ap);
    va_end(ap);
    sk_buf_putc(&line, '\n');
    int fd = atomic_load(&user_fd);
    const char *p = line.ptr;
    size_t left = line.len;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w <= 0)
            break;
        p += w;
        left -= (size_t)w;
    }
    sk_buf_free(&line);
}
