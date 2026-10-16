#include "headless.h"

#include "../base/json.h"
#include "../base/log.h"
#include "../base/paths.h"
#include "../core/command.h"
#include "../core/loop.h"
#include "../platform/pty.h"
#include "clitab.h"
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
    sk_tabs *tabs;
    sk_clitab_config cli;
    FILE *metrics;
    bool raw;
    struct termios saved;
    int status;
    int stdin_id;
    /* --record: the first tab's output, and where its resizes go. */
    int record_fd;
    FILE *resizes;
    unsigned long long recorded;
    int record_tab;
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

/* The window's own session: the active tab's, while it is a CLI's. */
static sk_session *active_session(headless *h)
{
    return sk_clitab_session(sk_tabs_active(h->tabs));
}

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    headless *h = ud;
    return sk_clitab_make(&h->cli, tab, spec, err);
}

static void on_spawn(void *ud, sk_tab *tab, pid_t pid)
{
    (void)ud;
    sk_log("tab %d: spawn %ld", tab->id, (long)pid);
}

/* The terminal shows the active tab, as the window would. */
static void on_output(void *ud, sk_tab *tab, const char *bytes, size_t n)
{
    headless *h = ud;
    if (h->record_fd >= 0 && tab->id == h->record_tab) {
        write_all(h->record_fd, bytes, n);
        h->recorded += n;
    }
    if (tab == sk_tabs_active(h->tabs))
        write_all(1, bytes, n);
    else
        sk_tabs_output(h->tabs, tab);
}

static void on_ended(void *ud, int status)
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
        sk_session *s = active_session(h);
        if (s)
            sk_session_input(s, buf, (size_t)n);
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
    sk_session *s = active_session(h);
    if (s && sk_pty_get_size(0, &cols, &rows) == 0) {
        if (sk_session_resize(s, cols, rows) == 0 && h->resizes) {
            fprintf(h->resizes, "%llu %d %d\n", h->recorded, cols, rows);
            fflush(h->resizes);
        }
    }
}

/* As closing the window does (decision 0016). */
static void on_stop(sk_loop *loop, int signo, void *ud)
{
    (void)loop;
    headless *h = ud;
    sk_log("signal %d: hanging up", signo);
    sk_tabs_hangup_all(h->tabs);
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

/* {"t", "tab", "agent", "report", "metrics"}: the time in ms since the
 * epoch, the active tab, its adapter, what it follows, and its bus. */
static void write_metrics(headless *h)
{
    sk_tab *tab = sk_tabs_active(h->tabs);
    sk_agent *agent = sk_clitab_agent(tab);
    if (!agent)
        return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    sk_report r;
    sk_agent_report(agent, &r);
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("t"));
    sk_jw_number(&w, (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000));
    sk_jw_key(&w, SK_STR("tab"));
    sk_jw_number(&w, tab->id);
    key_str(&w, "agent", sk_agent_kind(agent));
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
    sk_bus_write(sk_clitab_bus(tab), &w);
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

/* SKINNED_RELAY, else the skinned-relay beside this program: the file
 * itself, past any link to it, as bin/skinned's readlink -f found it. */
static char *relay_path(const sk_envp *env, const char *self)
{
    const char *given = sk_envp_get(env, "SKINNED_RELAY");
    if (given && *given)
        return sk_xstrdup(given);
    char *found = strchr(self, '/') ? sk_xstrdup(self) : sk_resolve_command(self, sk_envp_get(env, "PATH"));
    char *me = found ? realpath(found, NULL) : NULL;
    free(found);
    if (!me)
        return NULL;
    char *slash = strrchr(me, '/');
    if (slash)
        *slash = 0;
    char *p = sk_path_join(me, "skinned-relay");
    free(me);
    return p;
}

/* The record the terminal's tests replay (phase 4): a header line, then
 * the tab's bytes, with FILE.resizes beside it. */
static bool start_record(headless *h, const char *path, int tab, int cols, int rows)
{
    h->record_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (h->record_fd < 0)
        return false;
    sk_buf head = {0}, side = {0};
    sk_buf_printf(&head, "skinned-record 1 %d %d\n", cols, rows);
    write_all(h->record_fd, head.ptr, head.len);
    sk_buf_printf(&side, "%s.resizes", path);
    h->resizes = fopen(side.ptr, "w");
    if (h->resizes)
        fcntl(fileno(h->resizes), F_SETFD, FD_CLOEXEC);
    sk_buf_free(&head);
    sk_buf_free(&side);
    h->record_tab = tab;
    return h->resizes != NULL;
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

int sk_headless_run(const sk_options *o, const sk_headless_opts *ho, const sk_envp *env, const char *self)
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
    char *metrics = NULL, *log_path = NULL, *relay = NULL;
    FILE *mf = NULL;
    int log_fd = -1;
    if (!state || !root || !store || !run_dir || sk_mkdirs(state, 0700) != 0) {
        sk_tell("cannot make the app's directories: %s", strerror(errno));
        goto out;
    }
    metrics = ho->metrics ? sk_xstrdup(ho->metrics) : named(state, "jsonl");
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
    /* As process.cwd() throws in app/main.js: a folder that is gone is
     * not the root's. */
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) {
        sk_tell("cannot open the tab: %s", strerror(errno));
        goto out;
    }

    headless h;
    memset(&h, 0, sizeof h);
    h.loop = sk_loop_new();
    h.metrics = mf;
    h.status = SK_EXIT_APP;
    h.record_fd = -1;
    h.cli.loop = h.loop;
    h.cli.run_dir = run_dir;
    h.cli.root = root;
    h.cli.store = store;
    h.cli.relay = relay;
    h.cli.statusline = o->statusline;
    h.cli.spawned = on_spawn;
    h.cli.output = on_output;
    h.cli.log = log_line;
    h.cli.tell = tell_line;
    h.cli.ud = &h;
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make_tab;
    tc.follow_background = true;
    tc.tell = tell_line;
    tc.log = log_line;
    tc.ended = on_ended;
    tc.ud = &h;
    tc.cmd = o->cmd;
    tc.file = file;
    tc.env = env;
    tc.home = sk_envp_get(env, "HOME");
    h.tabs = sk_tabs_new(&tc);
    h.cli.tabs = h.tabs;

    /* The first tab runs the command as typed (decision 0003). */
    sk_tab_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.kind = SK_TAB_CLI;
    spec.cmd = o->cmd;
    spec.cwd = cwd;
    char *err = NULL;
    sk_tab *tab = sk_tabs_add(h.tabs, &spec, -1, &err);
    int cols = 80, rows = 24;
    sk_pty_get_size(0, &cols, &rows);
    if (tab && ho->record && !start_record(&h, ho->record, tab->id, cols, rows)) {
        sk_tell("cannot write %s: %s", ho->record, strerror(errno));
        status = SK_EXIT_APP;
    } else if (tab) {
        sk_tell("metrics in %s, log in %s", metrics, log_path);
        sk_log("headless: %s, relay %s", file, relay ? relay : "(none)");
        raw_mode(&h);
        h.stdin_id = sk_loop_watch(h.loop, 0, POLLIN, on_stdin, &h);
        sk_loop_signal(h.loop, SIGWINCH, on_winch, &h);
        sk_loop_signal(h.loop, SIGINT, on_stop, &h);
        sk_loop_signal(h.loop, SIGTERM, on_stop, &h);
        sk_loop_signal(h.loop, SIGHUP, on_stop, &h);
        sk_loop_timer(h.loop, METRICS_MS, METRICS_MS, on_metrics, &h);
        sk_session_start(sk_clitab_session(tab), cols, rows);
        sk_loop_run(h.loop);
        restore(&h);
        status = sk_tabs_done(h.tabs) ? sk_tabs_exit_status(h.tabs) : h.status;
        sk_tell("exit %d; metrics in %s", status, metrics);
    } else {
        sk_tell("cannot open the tab: %s", err ? err : "unknown");
    }
    free(err);
    /* No metrics line after the end: the agent is closed and reports
     * nothing. */
    sk_tabs_free(h.tabs);
    sk_loop_free(h.loop);
    if (h.record_fd >= 0)
        close(h.record_fd);
    if (h.resizes)
        fclose(h.resizes);

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
    free(metrics);
    free(log_path);
    free(relay);
    return status;
}
