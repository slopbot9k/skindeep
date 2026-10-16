#include "headless.h"

#include "../agents/agent.h"
#include "../base/json.h"
#include "../base/log.h"
#include "../base/paths.h"
#include "../core/bus.h"
#include "../core/command.h"
#include "../core/loop.h"
#include "../core/session.h"
#include "../platform/pty.h"
#include "runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define METRICS_MS 1000

typedef struct {
    sk_loop *loop;
    sk_session *session;
    sk_agent *agent;
    sk_bus *bus;
    FILE *metrics;
    bool raw;
    struct termios saved;
    int status;
    int stdin_id;
} headless;

static void log_line(void *ud, const char *msg)
{
    (void)ud;
    sk_log("%s", msg);
}

static void tell_line(void *ud, const char *msg)
{
    (void)ud;
    sk_tell("%s", msg);
}

static void write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return;
        p += w;
        n -= (size_t)w;
    }
}

static void on_spawn(void *ud, pid_t pid)
{
    (void)ud;
    sk_log("spawn %ld", (long)pid);
}

static void on_output(void *ud, const char *bytes, size_t n)
{
    (void)ud;
    write_all(1, bytes, n);
}

static void on_end(void *ud, int status)
{
    headless *h = ud;
    h->status = status;
    sk_loop_stop(h->loop);
}

static void on_stdin(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)revents;
    headless *h = ud;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n > 0) {
        sk_session_input(h->session, buf, (size_t)n);
        return;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    /* The end of our input is not the end of the command's. */
    sk_loop_unwatch(loop, h->stdin_id);
    h->stdin_id = 0;
}

static void on_winch(sk_loop *loop, int signo, void *ud)
{
    (void)loop, (void)signo;
    headless *h = ud;
    int cols, rows;
    if (sk_pty_get_size(0, &cols, &rows) == 0)
        sk_session_resize(h->session, cols, rows);
}

/* As closing a window does (decision 0016). */
static void on_stop(sk_loop *loop, int signo, void *ud)
{
    (void)loop;
    headless *h = ud;
    sk_log("signal %d: hanging up", signo);
    sk_session_hangup(h->session);
}

static const char *state_name(sk_ch_state s)
{
    return s == SK_CH_LIVE ? "live" : s == SK_CH_WAITING ? "waiting" : "off";
}

static void key_str(sk_jw *w, const char *key, const char *value)
{
    sk_jw_key(w, sk_str_c(key));
    if (value)
        sk_jw_string(w, sk_str_c(value));
    else
        sk_jw_null(w);
}

/* {"t", "agent", "report", "metrics"}: the time in ms since the epoch,
 * the adapter, what it follows, and the bus. */
static void write_metrics(headless *h)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    sk_report r;
    sk_agent_report(h->agent, &r);
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("t"));
    sk_jw_number(&w, (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000));
    key_str(&w, "agent", sk_agent_kind(h->agent));
    sk_jw_key(&w, SK_STR("report"));
    sk_jw_object(&w);
    key_str(&w, "session", r.session);
    key_str(&w, "title", r.title);
    key_str(&w, "notice", r.notice);
    sk_jw_key(&w, SK_STR("replaying"));
    sk_jw_bool(&w, r.replaying);
    sk_jw_key(&w, SK_STR("channels"));
    sk_jw_array(&w);
    for (size_t i = 0; i < r.n_channels; i++) {
        sk_jw_object(&w);
        key_str(&w, "name", r.channels[i].name);
        key_str(&w, "state", state_name(r.channels[i].state));
        key_str(&w, "target", r.channels[i].target);
        key_str(&w, "how", r.channels[i].how);
        sk_jw_object_end(&w);
    }
    sk_jw_array_end(&w);
    sk_jw_object_end(&w);
    sk_jw_key(&w, SK_STR("metrics"));
    sk_bus_write(h->bus, &w);
    sk_jw_object_end(&w);
    sk_buf_putc(&out, '\n');
    fwrite(out.ptr, 1, out.len, h->metrics);
    fflush(h->metrics);
    sk_buf_free(&out);
}

static void on_metrics(sk_loop *loop, void *ud)
{
    (void)loop;
    write_metrics(ud);
}

/* The terminal passes every byte through, as a window's terminal would
 * (decision 0002): no echo, no line editing, no signals of its own. */
static void raw_mode(headless *h)
{
    if (!isatty(0) || tcgetattr(0, &h->saved) != 0)
        return;
    struct termios t = h->saved;
    t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    h->raw = tcsetattr(0, TCSANOW, &t) == 0;
}

static void restore(headless *h)
{
    if (h->raw)
        tcsetattr(0, TCSANOW, &h->saved);
    h->raw = false;
}

/* SKINNED_RELAY, else the skinned-relay beside this program. */
static char *relay_path(const sk_envp *env, const char *self)
{
    const char *given = sk_envp_get(env, "SKINNED_RELAY");
    if (given && *given)
        return sk_xstrdup(given);
    char *me = strchr(self, '/') ? realpath(self, NULL) : sk_resolve_command(self, sk_envp_get(env, "PATH"));
    if (!me)
        return NULL;
    char *slash = strrchr(me, '/');
    if (slash)
        *slash = 0;
    char *p = sk_path_join(me, "skinned-relay");
    free(me);
    return p;
}

/* <dir>/headless-<pid>.<suffix> */
static char *named(const char *dir, const char *suffix)
{
    sk_buf name = {0};
    sk_buf_printf(&name, "headless-%ld.%s", (long)getpid(), suffix);
    char *p = sk_path_join(dir, name.ptr);
    sk_buf_free(&name);
    return p;
}

int sk_headless_run(const sk_options *o, const char *metrics_path, const sk_envp *env, const char *self)
{
    char *file = sk_resolve_command(o->cmd[0], sk_envp_get(env, "PATH"));
    if (!file) {
        sk_tell("%s: command not found", o->cmd[0]);
        return SK_EXIT_NOT_FOUND;
    }
    int status = SK_EXIT_APP;
    sk_env e;
    sk_env_current(&e);
    char *state = sk_path_state(&e), *root = sk_path_runtime_base(&e);
    char *store = sk_runtime_settings_dir(&e);
    char *run_dir = sk_runtime_create(&e, sk_envp_get(env, "SKINNED_RUNTIME_DIR"));
    char *tab_dir = run_dir ? sk_path_join(run_dir, "tab-1") : NULL;
    char *metrics = NULL, *log_path = NULL, *relay = NULL;
    FILE *mf = NULL;
    int log_fd = -1;
    if (!state || !root || !store || !tab_dir || sk_mkdirs(state, 0700) != 0 || sk_mkdirs(tab_dir, 0700) != 0) {
        sk_tell("cannot make the app's directories: %s", strerror(errno));
        goto out;
    }
    metrics = metrics_path ? sk_xstrdup(metrics_path) : named(state, "jsonl");
    log_path = named(state, "log");
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    mf = fopen(metrics, "a");
    if (log_fd < 0 || !mf) {
        sk_tell("cannot write %s: %s", log_fd < 0 ? log_path : metrics, strerror(errno));
        goto out;
    }
    fcntl(fileno(mf), F_SETFD, FD_CLOEXEC);
    sk_log_set_fd(log_fd);
    relay = relay_path(env, self);
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        snprintf(cwd, sizeof cwd, "/");

    headless h;
    memset(&h, 0, sizeof h);
    h.loop = sk_loop_new();
    h.bus = sk_bus_new(log_line, NULL);
    h.metrics = mf;
    h.status = SK_EXIT_APP;
    sk_agent_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cmd = o->cmd;
    ctx.cwd = cwd;
    ctx.env = env;
    ctx.run_dir = tab_dir;
    ctx.root = root;
    ctx.store = store;
    ctx.relay = relay;
    ctx.statusline = o->statusline;
    ctx.bus = h.bus;
    ctx.loop = h.loop;
    ctx.log = log_line;
    h.agent = sk_agent_open(&ctx);
    const char *slash = strrchr(o->cmd[0], '/');
    sk_session_opts so;
    memset(&so, 0, sizeof so);
    so.loop = h.loop;
    so.file = file;
    so.argv = o->cmd;
    so.env = env;
    so.label = slash ? slash + 1 : o->cmd[0];
    so.agent = h.agent;
    so.on_spawn = on_spawn;
    so.on_output = on_output;
    so.on_end = on_end;
    so.log = log_line;
    so.tell = tell_line;
    so.ud = &h;
    h.session = sk_session_new(&so);

    int cols = 80, rows = 24;
    sk_pty_get_size(0, &cols, &rows);
    sk_tell("metrics in %s, log in %s", metrics, log_path);
    sk_log("headless: %s, relay %s", file, relay ? relay : "(none)");
    raw_mode(&h);
    h.stdin_id = sk_loop_watch(h.loop, 0, POLLIN, on_stdin, &h);
    sk_loop_signal(h.loop, SIGWINCH, on_winch, &h);
    sk_loop_signal(h.loop, SIGINT, on_stop, &h);
    sk_loop_signal(h.loop, SIGTERM, on_stop, &h);
    sk_loop_signal(h.loop, SIGHUP, on_stop, &h);
    sk_loop_timer(h.loop, METRICS_MS, METRICS_MS, on_metrics, &h);
    sk_session_start(h.session, cols, rows);
    sk_loop_run(h.loop);
    /* No line after the end: the agent is closed and reports nothing. */
    restore(&h);
    status = h.status;
    sk_session_free(h.session);
    sk_agent_free(h.agent);
    sk_loop_free(h.loop);
    sk_bus_free(h.bus);
    sk_tell("exit %d; metrics in %s", status, metrics);

out:
    if (mf)
        fclose(mf);
    if (log_fd >= 0) {
        sk_log_set_fd(2);
        close(log_fd);
    }
    if (run_dir)
        sk_runtime_remove(run_dir);
    free(file);
    free(state);
    free(root);
    free(store);
    free(run_dir);
    free(tab_dir);
    free(metrics);
    free(log_path);
    free(relay);
    return status;
}
