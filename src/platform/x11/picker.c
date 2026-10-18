/* zenity or kdialog as New tab...'s folder picker (decision 0025), as
 * Electron's dialog.showOpenDialog runs on a Linux desktop: the child is
 * spawned on the core thread's loop, its stdout read there, and the loop
 * reaps it. Each prints the folder chosen and a newline, and exits 0;
 * 1 is a cancel. */
#include "picker.h"

#include "../../base/spawn.h"
#include "../../core/command.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* What a picker may print before it is cut off: a path or two. */
#define OUT_CAP (64 * 1024)

static const char *name_of(sk_picker_prog prog)
{
    return prog == SK_PICKER_KDIALOG ? "kdialog" : "zenity";
}

sk_picker_prog sk_picker_find(const char *path, char **file)
{
    if ((*file = sk_resolve_command("zenity", path)))
        return SK_PICKER_ZENITY;
    if ((*file = sk_resolve_command("kdialog", path)))
        return SK_PICKER_KDIALOG;
    return SK_PICKER_NONE;
}

void sk_picker_argv(sk_picker_prog prog, const sk_picker_opts *o, sk_strv *argv)
{
    sk_strv_push(argv, name_of(prog));
    if (prog == SK_PICKER_KDIALOG) {
        sk_strv_push(argv, "--getexistingdirectory");
        if (o->start && *o->start)
            sk_strv_push(argv, o->start);
        if (o->title) {
            sk_strv_push(argv, "--title");
            sk_strv_push(argv, o->title);
        }
        return;
    }
    sk_strv_push(argv, "--file-selection");
    sk_strv_push(argv, "--directory");
    if (o->title)
        sk_strv_pushf(argv, "--title=%s", o->title);
    /* With its slash, GTK opens the folder rather than select it in its
     * parent. */
    if (o->start && *o->start) {
        size_t n = strlen(o->start);
        sk_strv_pushf(argv, "--filename=%s%s", o->start, o->start[n - 1] == '/' ? "" : "/");
    }
}

void sk_picker_answer(sk_picker_prog prog, const char *out, size_t n, int wstatus, char **dir, char **err)
{
    *dir = *err = NULL;
    sk_buf b = {0};
    /* -1: the child was reaped elsewhere and its status is lost; what it
     * printed decides. */
    if (wstatus != -1 && WIFSIGNALED(wstatus)) {
        sk_buf_printf(&b, "%s was killed by signal %d", name_of(prog), WTERMSIG(wstatus));
        *err = b.ptr;
        return;
    }
    int code = wstatus == -1 || !WIFEXITED(wstatus) ? 0 : WEXITSTATUS(wstatus);
    if (code == 1)
        return;
    if (code != 0) {
        sk_buf_printf(&b, "%s failed with status %d", name_of(prog), code);
        *err = b.ptr;
        return;
    }
    /* The folder and its newline: a folder's name may hold one too. */
    if (n && out[n - 1] == '\n')
        n--;
    if (n && out[n - 1] == '\r')
        n--;
    if (!n)
        return;
    if (out[0] != '/') {
        sk_buf_printf(&b, "%s gave no folder: %.*s", name_of(prog), (int)SK_MIN(n, (size_t)200), out);
        *err = b.ptr;
        return;
    }
    *dir = sk_xstrndup(out, n);
}

typedef struct {
    sk_loop *loop;
    sk_picker_prog prog;
    int fd, watch;
    sk_buf out;
    bool eof, reaped;
    int wstatus;
    sk_picker_result done;
    void *ud;
} child;

/* Once its stdout has ended and it has been reaped. */
static void finish(child *c)
{
    if (!c->eof || !c->reaped)
        return;
    char *dir, *err;
    sk_picker_answer(c->prog, c->out.ptr ? c->out.ptr : "", c->out.len, c->wstatus, &dir, &err);
    sk_buf_free(&c->out);
    sk_picker_result done = c->done;
    void *ud = c->ud;
    free(c);
    done(ud, dir, err);
}

static void on_out(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)revents;
    child *c = ud;
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) {
            if (c->out.len < OUT_CAP)
                sk_buf_append(&c->out, buf, SK_MIN((size_t)n, OUT_CAP - c->out.len));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        break;
    }
    sk_loop_unwatch(loop, c->watch);
    close(fd);
    c->eof = true;
    finish(c);
}

static void on_reaped(sk_loop *loop, pid_t pid, int wstatus, void *ud)
{
    (void)loop, (void)pid;
    child *c = ud;
    c->wstatus = wstatus;
    c->reaped = true;
    finish(c);
}

/* A descriptor moved above stdio, closed on exec. */
static int high_cloexec(int fd)
{
    int r = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    close(fd);
    return r;
}

static void cannot(sk_picker_result done, void *ud, const char *name, int e)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "cannot run %s: %s", name, strerror(e));
    done(ud, NULL, b.ptr);
}

void sk_picker_run(sk_loop *loop, const sk_picker_opts *o, const char *path, sk_picker_result done, void *ud)
{
    char *file = NULL;
    sk_picker_prog prog = sk_picker_find(path, &file);
    if (prog == SK_PICKER_NONE) {
        done(ud, NULL, sk_xstrdup(SK_PICKER_MISSING));
        return;
    }
    int p[2];
    sk_spawn_lock();
    if (pipe(p) != 0) {
        int e = errno;
        sk_spawn_unlock();
        cannot(done, ud, name_of(prog), e);
        free(file);
        return;
    }
    int r = high_cloexec(p[0]), w = high_cloexec(p[1]);
    sk_spawn_unlock();
    if (r < 0 || w < 0) {
        int e = errno;
        if (r >= 0)
            close(r);
        if (w >= 0)
            close(w);
        cannot(done, ud, name_of(prog), e);
        free(file);
        return;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, w, 1);
    /* The core thread blocks the signals its loop takes, and the app
     * ignores SIGPIPE: the picker gets the defaults. */
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
    sk_picker_argv(prog, o, &argv);
    pid_t pid;
    sk_spawn_lock();
    int e = posix_spawnp(&pid, file, &fa, &at, sk_strv_argv(&argv), environ);
    sk_spawn_unlock();
    sk_strv_free(&argv);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    close(w);
    free(file);
    if (e) {
        close(r);
        cannot(done, ud, name_of(prog), e);
        return;
    }
    int fl = fcntl(r, F_GETFL);
    fcntl(r, F_SETFL, (fl < 0 ? 0 : fl) | O_NONBLOCK);
    child *c = sk_xcalloc(1, sizeof *c);
    c->loop = loop;
    c->prog = prog;
    c->fd = r;
    c->done = done;
    c->ud = ud;
    c->watch = sk_loop_watch(loop, r, POLLIN, on_out, c);
    /* With no SIGCHLD to take, the loop cannot reap it: its output
     * alone answers. */
    if (sk_loop_child(loop, pid, on_reaped, c) != 0) {
        c->wstatus = -1;
        c->reaped = true;
    }
}
