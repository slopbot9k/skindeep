/* zenity or kdialog asking which skin a launch opens (decision 0022), as
 * Electron's dialog.showMessageBox asks it on a Linux desktop. At launch,
 * before the core thread's loop runs, so the child is waited for here:
 * its stdout read to its end, then the child reaped by its pid. */
#include "ask.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* What the child may print before it is cut off: an index. */
#define OUT_CAP 4096

void sk_ask_argv(sk_picker_prog prog, const char *title, const char *message, const char *detail,
                 const char *const *choices, size_t n, sk_strv *argv)
{
    sk_buf text = {0};
    sk_buf_printf(&text, "%s\n\n%s", message, detail);
    if (prog == SK_PICKER_KDIALOG) {
        sk_strv_push(argv, "kdialog");
        sk_strv_push(argv, "--title");
        sk_strv_push(argv, title);
        sk_strv_push(argv, "--radiolist");
        sk_strv_push(argv, text.ptr);
        for (size_t i = 0; i < n; i++) {
            sk_strv_pushf(argv, "%zu", i);
            sk_strv_push(argv, choices[i]);
            sk_strv_push(argv, i ? "off" : "on");
        }
        sk_buf_free(&text);
        return;
    }
    sk_strv_push(argv, "zenity");
    sk_strv_push(argv, "--list");
    sk_strv_push(argv, "--radiolist");
    sk_strv_pushf(argv, "--title=%s", title);
    sk_strv_pushf(argv, "--text=%s", text.ptr);
    /* The radio button, the index printed, hidden, and the skin. */
    sk_strv_push(argv, "--column=");
    sk_strv_push(argv, "--column=");
    sk_strv_push(argv, "--column=Skin");
    sk_strv_push(argv, "--hide-column=2");
    sk_strv_push(argv, "--print-column=2");
    sk_strv_push(argv, "--hide-header");
    sk_strv_push(argv, "--width=460");
    sk_strv_push(argv, "--height=280");
    for (size_t i = 0; i < n; i++) {
        sk_strv_push(argv, i ? "FALSE" : "TRUE");
        sk_strv_pushf(argv, "%zu", i);
        sk_strv_push(argv, choices[i]);
    }
    sk_buf_free(&text);
}

int sk_ask_answer(const char *out, size_t n_out, int wstatus, size_t n)
{
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
        return 0;
    size_t i = 0, k = 0;
    for (; i < n_out && out[i] >= '0' && out[i] <= '9'; i++) {
        k = k * 10 + (size_t)(out[i] - '0');
        if (k >= n)
            return 0;
    }
    if (i == 0)
        return 0;
    while (i < n_out && (out[i] == '\n' || out[i] == '\r'))
        i++;
    return i == n_out ? (int)k : 0;
}

int sk_ask_run(const char *path, const char *title, const char *message, const char *detail,
               const char *const *choices, size_t n)
{
    char *file = NULL;
    sk_picker_prog prog = sk_picker_find(path, &file);
    if (prog == SK_PICKER_NONE)
        return -1;
    int p[2];
    if (pipe(p) != 0) {
        free(file);
        return -1;
    }
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    /* The app ignores SIGPIPE: the child gets the default. */
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, dflt;
    sigemptyset(&none);
    sigemptyset(&dflt);
    sigaddset(&dflt, SIGPIPE);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &dflt);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    sk_strv argv = {0};
    sk_ask_argv(prog, title, message, detail, choices, n, &argv);
    pid_t pid;
    int e = posix_spawnp(&pid, file, &fa, &at, sk_strv_argv(&argv), environ);
    sk_strv_free(&argv);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    free(file);
    if (e) {
        close(p[0]);
        return -1;
    }
    char out[OUT_CAP];
    size_t len = 0;
    for (;;) {
        char buf[512];
        ssize_t r = read(p[0], buf, sizeof buf);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        size_t take = SK_MIN((size_t)r, sizeof out - len);
        memcpy(out + len, buf, take);
        len += take;
    }
    close(p[0]);
    int ws;
    pid_t w;
    while ((w = waitpid(pid, &ws, 0)) < 0 && errno == EINTR)
        ;
    return w == pid ? sk_ask_answer(out, len, ws, n) : -1;
}
