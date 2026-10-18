/* app/session.js and app/exec-clean.py as one fork and exec (decisions
 * 0003, 0016). The child resets its signals, takes the pty as its
 * controlling terminal, keeps only fds 0 to 2, and execs; a failure
 * before the exec comes back as an errno through a close-on-exec socket
 * pair, which the parent reads before it goes on. So the spawn is over
 * when sk_session_start returns: the child has exec'd or failed to, its
 * process group exists, and node-pty's race between the spawn and
 * setsid is gone.
 *
 * The loop reaps the child (sk_loop_child) and calls back. Its last
 * bytes can reach the master after the SIGCHLD, so the end waits
 * for the master's end of output, EIO or 0, at most DRAIN_MS, as
 * node-pty waits for its socket to close. */
#include "session.h"

#include "../platform/pty.h"
#include "command.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* POSIX names no count of signals; Linux has 64, macOS 31. */
#define MAX_SIGNAL 65
#define DRAIN_MS 200
#define READ_CHUNK 16384
/* Read at most this much a wake, so one busy tab cannot hold the loop. */
#define READ_MAX (16 * READ_CHUNK)

/* What Chromium added to the app's environment at startup. The app's
 * own variables, every SKINNED_ one, go too: SKINNED_CONTROL names the
 * control socket, which a process in a tab must never find. */
static const char *const DROPPED_ENV[] = {"CHROME_DESKTOP", "FC_FONTATIONS"};
#define APP_PREFIX "SKINNED_"

/* Where the child failed, before the exec or at it. */
enum { STAGE_PTY = 1, STAGE_CWD, STAGE_EXEC };

typedef struct {
    int stage, err;
} child_error;

struct sk_session {
    sk_loop *loop;
    char *file, *cwd, *label;
    sk_strv argv, exec_argv;
    sk_envp env;
    sk_agent *agent;
    uint64_t (*now)(void);
    uint64_t grace_ms;
    void (*on_spawn)(void *ud, pid_t pid);
    void (*on_output)(void *ud, const char *bytes, size_t n);
    void (*on_end)(void *ud, int status);
    sk_log_fn log, tell;
    void *ud;

    sk_session_state state;
    pid_t pid;
    int status, exit_code;
    char *notice, *failure;
    int master;
    /* Loop ids, 0 for none. */
    int read_id, write_id, kill_id, drain_id, end_id;
    bool hung_up, reaped, out_done;
    sk_buf queue; /* input not yet written */
};

static void say(sk_session *s, sk_log_fn fn, const char *fmt, ...) SK_PRINTF(3, 4);
static void say(sk_session *s, sk_log_fn fn, const char *fmt, ...)
{
    if (!fn)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    fn(s->ud, b.ptr);
    sk_buf_free(&b);
}

int sk_session_exit_status(int ws)
{
    if (WIFEXITED(ws))
        return WEXITSTATUS(ws);
    if (WIFSIGNALED(ws))
        return 128 + WTERMSIG(ws);
    return SK_EXIT_APP;
}

void sk_session_child_env(const sk_envp *env, sk_envp *out)
{
    sk_envp_from(out, env ? env->vars.ptr : NULL);
    sk_envp_set(out, "TERM", "xterm-256color");
    sk_envp_set(out, "COLORTERM", "truecolor");
    for (size_t i = 0; i < SK_LEN(DROPPED_ENV); i++)
        sk_envp_set(out, DROPPED_ENV[i], NULL);
    for (size_t i = 0; i < out->vars.len;) {
        if (!strncmp(out->vars.ptr[i], APP_PREFIX, strlen(APP_PREFIX))) {
            free(out->vars.ptr[i]);
            memmove(out->vars.ptr + i, out->vars.ptr + i + 1, (out->vars.len - i) * sizeof *out->vars.ptr);
            out->vars.len--;
        } else {
            i++;
        }
    }
}

sk_session *sk_session_new(const sk_session_opts *o)
{
    sk_session *s = sk_xcalloc(1, sizeof *s);
    s->loop = o->loop;
    s->file = o->file ? sk_xstrdup(o->file) : NULL;
    s->cwd = o->cwd ? sk_xstrdup(o->cwd) : NULL;
    s->label = sk_xstrdup(o->label ? o->label : "");
    sk_strv_extend(&s->argv, o->argv);
    sk_envp_from(&s->env, o->env ? o->env->vars.ptr : NULL);
    s->agent = o->agent;
    s->now = o->now;
    s->grace_ms = o->grace_ms ? o->grace_ms : SK_SESSION_GRACE_MS;
    s->on_spawn = o->on_spawn;
    s->on_output = o->on_output;
    s->on_end = o->on_end;
    s->log = o->log;
    s->tell = o->tell;
    s->ud = o->ud;
    s->state = SK_SESSION_IDLE;
    s->status = -1;
    s->master = -1;
    return s;
}

static const char *argv0(const sk_session *s)
{
    return s->argv.len ? s->argv.ptr[0] : "";
}

static void unwatch(sk_session *s, int *id)
{
    if (*id)
        sk_loop_unwatch(s->loop, *id);
    *id = 0;
}

static void cancel(sk_session *s, int *id)
{
    if (*id)
        sk_loop_cancel(s->loop, *id);
    *id = 0;
}

/* Everything the session holds on the loop and the pty. */
static void stop(sk_session *s)
{
    unwatch(s, &s->read_id);
    unwatch(s, &s->write_id);
    cancel(s, &s->kill_id);
    cancel(s, &s->drain_id);
    if (s->master >= 0)
        close(s->master);
    s->master = -1;
    sk_buf_free(&s->queue);
}

static void fire_end(sk_loop *loop, void *ud)
{
    (void)loop;
    sk_session *s = ud;
    s->end_id = 0;
    s->on_end(s->ud, s->status);
}

/* The first end stands. */
static void finish(sk_session *s, int code)
{
    if (s->state == SK_SESSION_ENDED)
        return;
    s->status = code;
    stop(s);
    if (s->agent)
        sk_agent_close(s->agent);
    s->state = SK_SESSION_ENDED;
    if (s->on_end)
        s->end_id = sk_loop_timer(s->loop, 0, 0, fire_end, s);
}

/* As session.js signals: the group, or the child alone while its group
 * does not exist. Never once the child is reaped, when its pid may name
 * another process. */
/* The children alive, for the end an internal error makes: a slot per
 * pid, taken at the spawn and given back at the reap, read with no lock
 * or allocation from whatever thread dies. */
#define LIVE_SLOTS 256
static atomic_int live[LIVE_SLOTS];

static void live_add(pid_t pid)
{
    for (int i = 0; i < LIVE_SLOTS; i++) {
        int none = 0;
        if (atomic_compare_exchange_strong(&live[i], &none, (int)pid))
            return;
    }
}

static void live_drop(pid_t pid)
{
    for (int i = 0; i < LIVE_SLOTS; i++) {
        int p = (int)pid;
        if (atomic_compare_exchange_strong(&live[i], &p, 0))
            return;
    }
}

static void signal_group(int pid, int sig)
{
    if (kill(-pid, sig) != 0 && errno == ESRCH)
        kill(pid, sig);
}

void sk_session_hangup_every(uint64_t grace_ms)
{
    for (int i = 0; i < LIVE_SLOTS; i++) {
        int p = atomic_load(&live[i]);
        if (p > 0)
            signal_group(p, SIGHUP);
    }
    struct timespec t0, now, nap = {0, 20 * 1000000L};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        bool any = false;
        for (int i = 0; i < LIVE_SLOTS; i++) {
            int p = atomic_load(&live[i]);
            if (p <= 0)
                continue;
            /* Reaped here, or already by the loop. */
            pid_t r = waitpid(p, NULL, WNOHANG);
            if (r == p || (r < 0 && errno == ECHILD))
                atomic_store(&live[i], 0);
            else
                any = true;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t ms = (uint64_t)(now.tv_sec - t0.tv_sec) * 1000 + (uint64_t)((now.tv_nsec - t0.tv_nsec) / 1000000L);
        if (!any || ms >= grace_ms)
            break;
        nanosleep(&nap, NULL);
    }
    for (int i = 0; i < LIVE_SLOTS; i++) {
        int p = atomic_load(&live[i]);
        if (p > 0)
            signal_group(p, SIGKILL);
    }
    /* The app leaves once its children are reaped, as main.js waited:
     * a killed child dies at once, so a short wait is enough. */
    struct timespec tick = {0, 10 * 1000000L};
    for (int round = 0; round < 50; round++) {
        bool any = false;
        for (int i = 0; i < LIVE_SLOTS; i++) {
            int p = atomic_load(&live[i]);
            if (p <= 0)
                continue;
            pid_t r = waitpid(p, NULL, WNOHANG);
            if (r == p || (r < 0 && errno == ECHILD))
                atomic_store(&live[i], 0);
            else
                any = true;
        }
        if (!any)
            break;
        nanosleep(&tick, NULL);
    }
}

static void signal_child(sk_session *s, int sig)
{
    if (!s->pid || s->reaped)
        return;
    if (kill(-s->pid, sig) != 0 && errno == ESRCH)
        kill(s->pid, sig);
}

static void output_done(sk_session *s)
{
    s->out_done = true;
    unwatch(s, &s->read_id);
    if (s->reaped)
        finish(s, s->exit_code);
}

static void read_master(sk_session *s)
{
    char buf[READ_CHUNK];
    size_t total = 0;
    while (s->master >= 0 && !s->out_done && total < READ_MAX) {
        ssize_t n = read(s->master, buf, sizeof buf);
        if (n > 0) {
            total += (size_t)n;
            if (s->agent)
                sk_agent_on_output(s->agent, (size_t)n);
            if (s->on_output)
                s->on_output(s->ud, buf, (size_t)n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN) {
            return;
        } else {
            /* 0, or EIO on Linux and macOS: every slave descriptor is
             * closed. */
            output_done(s);
        }
    }
}

static void on_master(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)fd, (void)revents;
    read_master(ud);
}

static void on_writable(sk_loop *loop, int fd, short revents, void *ud);

static void flush(sk_session *s)
{
    while (s->queue.len) {
        ssize_t w = write(s->master, s->queue.ptr, s->queue.len);
        if (w > 0) {
            sk_buf_consume(&s->queue, (size_t)w);
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else if (w < 0 && errno == EAGAIN) {
            if (!s->write_id)
                s->write_id = sk_loop_watch(s->loop, s->master, POLLOUT, on_writable, s);
            return;
        } else {
            sk_buf_clear(&s->queue); /* no reader is left */
        }
    }
    unwatch(s, &s->write_id);
}

static void on_writable(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)fd, (void)revents;
    flush(ud);
}

static void on_kill(sk_loop *loop, void *ud)
{
    (void)loop;
    sk_session *s = ud;
    s->kill_id = 0;
    signal_child(s, SIGKILL);
}

static void on_drain(sk_loop *loop, void *ud)
{
    (void)loop;
    sk_session *s = ud;
    s->drain_id = 0;
    read_master(s);
    finish(s, s->exit_code);
}

static void reaped(sk_session *s)
{
    cancel(s, &s->kill_id);
    say(s, s->log, "child %ld exited: %d", (long)s->pid, s->exit_code);
    if (s->failure)
        say(s, s->tell, "cannot start %s: %s", argv0(s), s->failure);
    read_master(s);
    if (s->state == SK_SESSION_ENDED)
        return;
    if (s->out_done)
        finish(s, s->exit_code);
    else if (!s->drain_id)
        s->drain_id = sk_loop_timer(s->loop, DRAIN_MS, 0, on_drain, s);
}

static void on_child(sk_loop *loop, pid_t pid, int wstatus, void *ud)
{
    (void)loop, (void)pid;
    sk_session *s = ud;
    /* -1: reaped by someone else, its status lost. */
    s->exit_code = wstatus >= 0 ? sk_session_exit_status(wstatus) : SK_EXIT_APP;
    s->reaped = true;
    live_drop(s->pid);
    reaped(s);
}

/* What the child needs, made before the fork: after it, in a process
 * with other threads, only async-signal-safe calls are allowed. */
typedef struct {
    const char *slave, *file, *cwd;
    char *const *argv, *const *envp;
    int report_fd;
    struct sigaction dfl;
    sigset_t none;
} plan;

/* The parent words the reason, which strerror cannot do here, and the
 * child writes it on its stderr, as exec-clean.py did. Before the pty is
 * on fd 2, fd 2 is the app's, so nothing is written. */
static _Noreturn void child_fail(const plan *p, int stage, int code)
{
    child_error e = {stage, errno};
    ssize_t w = write(p->report_fd, &e, sizeof e);
    char msg[1024];
    size_t n = 0;
    while (n < sizeof msg) {
        ssize_t r = read(p->report_fd, msg + n, sizeof msg - n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        n += (size_t)r;
    }
    if (stage != STAGE_PTY && n)
        w = write(2, msg, n);
    (void)w;
    _exit(code);
}

static _Noreturn void run_child(const plan *p)
{
    /* Ignored signals and the mask survive an exec; the app's must not
     * reach the CLI. The mask held every signal since before the fork,
     * so none of the app's handlers ran here. */
    for (int sig = 1; sig < MAX_SIGNAL; sig++)
        sigaction(sig, &p->dfl, NULL);
    sigprocmask(SIG_SETMASK, &p->none, NULL);
    if (setsid() < 0)
        child_fail(p, STAGE_PTY, SK_EXIT_APP);
    int fd = open(p->slave, O_RDWR);
    if (fd < 0 || sk_pty_set_ctty(fd) != 0)
        child_fail(p, STAGE_PTY, SK_EXIT_APP);
    for (int i = 0; i < 3; i++)
        if (dup2(fd, i) < 0)
            child_fail(p, STAGE_PTY, SK_EXIT_APP);
    if (fd > 2)
        close(fd);
    sk_pty_close_from(3, p->report_fd);
    if (p->cwd && chdir(p->cwd) != 0)
        child_fail(p, STAGE_CWD, SK_EXIT_APP);
    execve(p->file, p->argv, p->envp);
    child_fail(p, STAGE_EXEC, errno == ENOENT ? SK_EXIT_NOT_FOUND : SK_EXIT_CANNOT_EXEC);
}

/* As exec-clean.py words it. */
static char *describe(const sk_session *s, const child_error *e)
{
    sk_buf b = {0};
    struct stat st;
    if (e->stage == STAGE_EXEC && e->err == ENOENT && stat(s->file, &st) == 0)
        sk_buf_append_c(&b, "required file not found"); /* the interpreter is missing */
    else if (e->stage == STAGE_EXEC)
        sk_buf_append_c(&b, strerror(e->err));
    else if (e->stage == STAGE_CWD)
        sk_buf_printf(&b, "cannot change to %s: %s", s->cwd, strerror(e->err));
    else
        sk_buf_printf(&b, "cannot take the terminal: %s", strerror(e->err));
    return b.ptr;
}

static ssize_t read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

/* The channel the child reports a failure on, and reads its wording
 * from; the exec closes it. The child's end is above 2, so its dup2s
 * onto 0 to 2 cannot replace it. */
static int report_channel(int sv[2])
{
    int raw[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, raw) != 0)
        return -1;
    sv[0] = raw[0];
    sv[1] = fcntl(raw[1], F_DUPFD_CLOEXEC, 3);
    close(raw[1]);
    if (sv[1] < 0 || fcntl(sv[0], F_SETFD, FD_CLOEXEC) != 0) {
        close(sv[0]);
        if (sv[1] >= 0)
            close(sv[1]);
        return -1;
    }
    return 0;
}

static bool valid_size(int cols, int rows)
{
    return cols >= 1 && rows >= 1 && cols <= USHRT_MAX && rows <= USHRT_MAX;
}

/* 0, or -1 with the reason in why and nothing left open. */
static int spawn(sk_session *s, int cols, int rows, sk_buf *why)
{
    if (!valid_size(cols, rows)) {
        sk_buf_printf(why, "cols and rows must be 1 to %d, not %d and %d", USHRT_MAX, cols, rows);
        return -1;
    }
    if (!s->file) {
        sk_buf_append_c(why, "command not found");
        return -1;
    }
    int master = -1, slave = -1, report[2] = {-1, -1};
    char *name = NULL;
    const char *what = "cannot open a pty";
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || fcntl(master, F_SETFD, FD_CLOEXEC) != 0 || grantpt(master) != 0 ||
        unlockpt(master) != 0)
        goto fail;
    const char *pts = ptsname(master);
    if (!pts)
        goto fail;
    name = sk_xstrdup(pts);
    /* Held across the fork, so the slave stays open until the child has
     * opened its own. */
    slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0 || sk_pty_set_modes(slave) != 0 || sk_pty_set_size(slave, cols, rows) != 0)
        goto fail;
    what = "cannot make a socket pair";
    if (report_channel(report) != 0)
        goto fail;
    sk_envp env;
    sk_session_child_env(&s->env, &env);
    plan p;
    memset(&p, 0, sizeof p);
    p.slave = name;
    p.file = s->file;
    p.cwd = s->cwd;
    p.argv = sk_strv_argv(&s->exec_argv);
    p.envp = sk_envp_ptr(&env);
    p.report_fd = report[1];
    p.dfl.sa_handler = SIG_DFL;
    sigemptyset(&p.dfl.sa_mask);
    sigemptyset(&p.none);
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    pid_t pid = fork();
    if (pid == 0)
        run_child(&p);
    int fork_errno = errno;
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    sk_envp_free(&env);
    /* Closed now, while the child holds the slave: a last close waits
     * up to 0.6 s for the master to read, then drops the output (macOS),
     * and the parent cannot read while it waits. */
    close(slave);
    slave = -1;
    close(report[1]);
    report[1] = -1;
    what = "cannot fork";
    errno = fork_errno;
    if (pid < 0)
        goto fail;

    child_error ce;
    if (read_full(report[0], &ce, sizeof ce) == (ssize_t)sizeof ce) {
        s->failure = describe(s, &ce);
        sk_buf msg = {0};
        sk_buf_printf(&msg, "%s: %s\n", argv0(s), s->failure);
        ssize_t w = send(report[0], msg.ptr, msg.len, MSG_NOSIGNAL);
        (void)w;
        sk_buf_free(&msg);
    }
    close(report[0]);
    free(name);
    int fl = fcntl(master, F_GETFL);
    if (fl >= 0)
        fcntl(master, F_SETFL, fl | O_NONBLOCK);
    s->master = master;
    s->pid = pid;
    live_add(pid);
    /* Fails only when SIGCHLD cannot be caught at all. */
    if (sk_loop_child(s->loop, pid, on_child, s) != 0)
        sk_die("cannot watch for the exit of child %ld", (long)pid);
    s->read_id = sk_loop_watch(s->loop, master, POLLIN, on_master, s);
    return 0;

fail:
    sk_buf_printf(why, "%s: %s", what, strerror(errno ? errno : EINVAL));
    for (int i = 0; i < 2; i++)
        if (report[i] >= 0)
            close(report[i]);
    if (slave >= 0)
        close(slave);
    if (master >= 0)
        close(master);
    free(name);
    return -1;
}

void sk_session_start(sk_session *s, int cols, int rows)
{
    if (s->state != SK_SESSION_IDLE)
        return;
    s->state = SK_SESSION_PREPARING;
    sk_strv args = {0};
    if (s->agent) {
        char *notice = NULL;
        sk_agent_prepare(s->agent, &args, &notice);
        if (notice) {
            free(s->notice);
            s->notice = notice;
        }
    }
    /* A hang-up from inside the prepare: nothing spawns. */
    if (s->state != SK_SESSION_PREPARING) {
        sk_strv_free(&args);
        return;
    }
    if (s->argv.len) {
        sk_strv_push(&s->exec_argv, s->argv.ptr[0]);
        sk_strv_extend(&s->exec_argv, sk_strv_argv(&args));
        sk_strv_extend(&s->exec_argv, s->argv.ptr + 1);
    }
    sk_strv_free(&args);
    sk_buf why = {0};
    if (!s->argv.len)
        sk_buf_append_c(&why, "no command");
    if (why.len || spawn(s, cols, rows, &why) != 0) {
        say(s, s->log, "spawn failed: %s", why.ptr);
        say(s, s->tell, "cannot start %s: %s", argv0(s), why.ptr);
        free(s->failure);
        s->failure = why.ptr;
        finish(s, SK_EXIT_APP);
        return;
    }
    s->state = SK_SESSION_RUNNING;
    sk_buf line = {0};
    for (size_t i = 0; i < s->exec_argv.len; i++)
        sk_buf_printf(&line, "%s%s", i ? " " : "", s->exec_argv.ptr[i]);
    say(s, s->log, "spawn %ld: %s: %s: %s", (long)s->pid, s->label, s->file, sk_buf_cstr(&line));
    sk_buf_free(&line);
    if (s->agent)
        sk_agent_attach(s->agent, s->pid);
    if (s->on_spawn)
        s->on_spawn(s->ud, s->pid);
}

void sk_session_input(sk_session *s, const void *bytes, size_t n)
{
    if (s->master < 0 || s->reaped)
        return;
    if (s->agent)
        sk_agent_on_input(s->agent);
    sk_buf_append(&s->queue, bytes, n);
    flush(s);
}

int sk_session_resize(sk_session *s, int cols, int rows)
{
    if (s->master < 0 || s->reaped || !valid_size(cols, rows))
        return -1;
    return sk_pty_set_size(s->master, cols, rows);
}

void sk_session_hangup(sk_session *s)
{
    if (s->state == SK_SESSION_ENDED || s->hung_up)
        return;
    if (s->state == SK_SESSION_IDLE || s->state == SK_SESSION_PREPARING) {
        finish(s, SK_EXIT_BEFORE_SPAWN);
        return;
    }
    /* Its status is known and stands (decision 0003). */
    if (s->reaped) {
        finish(s, s->exit_code);
        return;
    }
    s->hung_up = true;
    s->state = SK_SESSION_ENDING;
    signal_child(s, SIGHUP);
    s->kill_id = sk_loop_timer(s->loop, s->grace_ms, 0, on_kill, s);
}

sk_session_state sk_session_get_state(const sk_session *s)
{
    return s->state;
}

bool sk_session_hung_up(const sk_session *s)
{
    return s->hung_up;
}

pid_t sk_session_pid(const sk_session *s)
{
    return s->pid;
}

int sk_session_status(const sk_session *s)
{
    return s->state == SK_SESSION_ENDED ? s->status : -1;
}

const char *sk_session_notice(const sk_session *s)
{
    return s->notice;
}

const char *sk_session_failure(const sk_session *s)
{
    return s->failure;
}

char *const *sk_session_argv(const sk_session *s)
{
    return s->exec_argv.ptr;
}

const char *sk_session_label(const sk_session *s)
{
    return s->label;
}

void sk_session_free(sk_session *s)
{
    if (!s)
        return;
    if (s->pid && !s->reaped) {
        sk_loop_unchild(s->loop, s->pid);
        signal_child(s, SIGKILL);
        while (waitpid(s->pid, NULL, 0) < 0 && errno == EINTR)
            ;
        s->reaped = true;
        live_drop(s->pid);
    }
    stop(s);
    cancel(s, &s->end_id);
    if (s->agent && s->state != SK_SESSION_ENDED)
        sk_agent_close(s->agent);
    free(s->file);
    free(s->cwd);
    free(s->label);
    free(s->notice);
    free(s->failure);
    sk_strv_free(&s->argv);
    sk_strv_free(&s->exec_argv);
    sk_envp_free(&s->env);
    free(s);
}
