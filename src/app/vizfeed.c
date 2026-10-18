#include "vizfeed.h"

#include "../base/spawn.h"
#include "../core/command.h"
#include "../viz/viz.h"
#include "lastskin.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* viz.js's setInterval. */
#define FEED_MS 33
/* What a recorder's stderr keeps for the reason it failed. */
#define ERR_KEEP 400
/* How long a stopped recorder has to exit when the feed is freed. */
#define EXIT_MS 1000

const sk_viz_input_info sk_viz_inputs[SK_VIZ_INPUTS] = {
    {"session", "Session"},
    {"sound", "Sound output"},
    {"both", "Session and sound output"},
};

static const char *const PAREC_ARGS[] = {
    "--format=s16le", "--rate=44100", "--channels=1", "--latency-msec=20", "-d", "@DEFAULT_MONITOR@", NULL,
};
static const char *const PW_RECORD_ARGS[] = {
    "--format", "s16", "--rate", "44100", "--channels", "1", "-P", "{ stream.capture.sink=true }", "-", NULL,
};

const sk_viz_recorder sk_viz_recorders_linux[SK_VIZ_RECORDERS_LINUX] = {
    {"parec", PAREC_ARGS},
    {"pw-record", PW_RECORD_ARGS},
};

typedef struct {
    int id, mode;
} tab_mode;

typedef struct {
    int id;
    uint64_t at;
} heard_at;

/* The recorder running: its pid, the read ends of its stdout and
 * stderr, and whether it has sent a sample. */
typedef struct {
    pid_t pid; /* 0: none */
    int out, err;
    int out_watch, err_watch;
    size_t index;
    bool heard;
    sk_buf err_text;
} recorder;

struct sk_vizfeed {
    sk_vizfeed_config c;
    char *mode_dir, *input_dir;
    pid_t pid;
    int timer;
    SK_VEC(sk_vizfeed_tab) tabs;
    /* Tab id: the time of its last event, or of its first frame. */
    SK_VEC(heard_at) heard;
    /* Tab id: its pane's mode. A new pane starts at the first mode no
     * other pane shows, counting from base, the last one picked. */
    SK_VEC(tab_mode) modes;
    int base;
    /* The tab whose pane's drop-down is open. */
    bool has_menu;
    int menu;
    int input;
    bool open;
    unsigned opened;
    /* The sound output: its state, the reason it failed, the newest
     * samples in a ring, a byte of a sample split between reads. */
    int status;
    char *reason;
    float ring[SK_SYNTH_N];
    size_t at;
    int odd; /* -1: none */
    recorder rec;
    sk_strv why; /* each recorder's failure, this start */
    SK_VEC(pid_t) dying; /* stopped, not yet reaped */
};

static void logf_(sk_vizfeed *f, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(sk_vizfeed *f, const char *fmt, ...)
{
    if (!f->c.log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    f->c.log(f->c.ud, b.ptr);
    sk_buf_free(&b);
}

static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)(ts.tv_nsec / 1000000);
}

static bool sound_known(const sk_vizfeed *f)
{
    return f->c.n_recorders > 0;
}

/* ---- The records ---- */

static char *beside_last(const sk_env *e, const char *name)
{
    char *state = sk_path_state(e);
    char *d = state ? sk_path_join(state, name) : NULL;
    free(state);
    return d;
}

char *sk_vizfeed_mode_dir(const sk_env *e)
{
    return beside_last(e, "last-viz");
}

char *sk_vizfeed_input_dir(const sk_env *e)
{
    return beside_last(e, "last-viz-input");
}

static int input_find(const char *id)
{
    for (int i = 0; i < SK_VIZ_INPUTS; i++)
        if (id && strcmp(sk_viz_inputs[i].id, id) == 0)
            return i;
    return -1;
}

/* The records of dir, newest first, those that can never be picked
 * again removed. */
static void read_records(sk_vizfeed *f, const char *dir, const char *what, sk_last_records *out)
{
    if (!dir)
        return;
    sk_last_read(dir, out);
    errno = 0;
    sk_last_prune(dir, out, SK_LAST_RACE_MS);
    if (errno && errno != ENOENT)
        logf_(f, "last viz %s: %s", what, strerror(errno));
}

static void record(sk_vizfeed *f, const char *dir, const char *what, const char *id)
{
    if (dir && sk_last_write(dir, f->pid, id, wall_ms()) != 0)
        logf_(f, "last viz %s: %s", what, strerror(errno));
}

/* ---- The panes and their modes ---- */

static tab_mode *mode_entry(sk_vizfeed *f, int id)
{
    for (size_t i = 0; i < f->modes.len; i++)
        if (f->modes.ptr[i].id == id)
            return &f->modes.ptr[i];
    return NULL;
}

static const sk_vizfeed_tab *pane_tab(const sk_vizfeed *f, int id)
{
    for (size_t i = 0; i < f->tabs.len; i++)
        if (f->tabs.ptr[i].id == id && f->tabs.ptr[i].pane)
            return &f->tabs.ptr[i];
    return NULL;
}

/* viz.js's panes(): the modes of tabs gone are dropped, and so is the
 * drop-down of a pane gone; each new pane gets its first mode. */
static void sync_panes(sk_vizfeed *f)
{
    size_t k = 0;
    for (size_t i = 0; i < f->modes.len; i++)
        if (pane_tab(f, f->modes.ptr[i].id))
            f->modes.ptr[k++] = f->modes.ptr[i];
    f->modes.len = k;
    if (f->has_menu && !pane_tab(f, f->menu))
        f->has_menu = false;
    for (size_t i = 0; i < f->tabs.len; i++) {
        const sk_vizfeed_tab *t = &f->tabs.ptr[i];
        if (!t->pane || mode_entry(f, t->id))
            continue;
        int mode = f->base;
        for (int n = 0; n < SK_VIZ_MODES; n++) {
            int m = (f->base + n) % SK_VIZ_MODES;
            bool used = false;
            for (size_t j = 0; j < f->modes.len && !used; j++)
                used = f->modes.ptr[j].mode == m;
            if (!used) {
                mode = m;
                break;
            }
        }
        tab_mode e = {t->id, mode};
        sk_vec_push(&f->modes, e);
    }
}

/* The active tab's pane, or NULL. */
static const sk_vizfeed_tab *active_pane(sk_vizfeed *f)
{
    sync_panes(f);
    for (size_t i = 0; i < f->tabs.len; i++)
        if (f->tabs.ptr[i].pane && f->tabs.ptr[i].active)
            return &f->tabs.ptr[i];
    return NULL;
}

static void set_mode(sk_vizfeed *f, int tab, int mode)
{
    tab_mode *e = mode_entry(f, tab);
    if (e && e->mode == mode)
        return;
    if (e) {
        e->mode = mode;
    } else {
        tab_mode n = {tab, mode};
        sk_vec_push(&f->modes, n);
    }
    f->base = mode;
    record(f, f->mode_dir, "mode", sk_viz_modes[mode].id);
}

/* ---- The sound output ---- */

static void push_bytes(sk_vizfeed *f, const uint8_t *b, size_t n)
{
    size_t k = 0;
    if (f->odd >= 0 && n) {
        int16_t v = (int16_t)(uint16_t)((unsigned)f->odd | (unsigned)b[0] << 8);
        f->ring[f->at] = (float)v / 32768.0f;
        f->at = (f->at + 1) % SK_SYNTH_N;
        f->odd = -1;
        k = 1;
    }
    for (; k + 1 < n; k += 2) {
        int16_t v = (int16_t)(uint16_t)((unsigned)b[k] | (unsigned)b[k + 1] << 8);
        f->ring[f->at] = (float)v / 32768.0f;
        f->at = (f->at + 1) % SK_SYNTH_N;
    }
    if (k < n)
        f->odd = b[k];
}

static void close_fd(sk_vizfeed *f, int *fd, int *watch)
{
    if (*watch)
        sk_loop_unwatch(f->c.loop, *watch);
    if (*fd >= 0)
        close(*fd);
    *watch = 0;
    *fd = -1;
}

/* What the recorder's stdout holds now: its samples. */
static void read_out(sk_vizfeed *f)
{
    uint8_t buf[8192];
    while (f->rec.out >= 0) {
        ssize_t n = read(f->rec.out, buf, sizeof buf);
        if (n > 0) {
            f->rec.heard = true;
            f->status = SK_VIZFEED_SOUND_LIVE;
            push_bytes(f, buf, (size_t)n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                close_fd(f, &f->rec.out, &f->rec.out_watch);
            break;
        }
    }
}

/* What its stderr holds now, the last ERR_KEEP bytes kept. */
static void read_err(sk_vizfeed *f)
{
    char buf[1024];
    while (f->rec.err >= 0) {
        ssize_t n = read(f->rec.err, buf, sizeof buf);
        if (n > 0) {
            sk_buf_append(&f->rec.err_text, buf, (size_t)n);
            if (f->rec.err_text.len > ERR_KEEP)
                sk_buf_consume(&f->rec.err_text, f->rec.err_text.len - ERR_KEEP);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                close_fd(f, &f->rec.err, &f->rec.err_watch);
            break;
        }
    }
}

static void on_out(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)fd, (void)revents;
    read_out(ud);
}

static void on_err(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)fd, (void)revents;
    read_err(ud);
}

static void close_rec(sk_vizfeed *f)
{
    close_fd(f, &f->rec.out, &f->rec.out_watch);
    close_fd(f, &f->rec.err, &f->rec.err_watch);
    sk_buf_free(&f->rec.err_text);
    f->rec.pid = 0;
    f->rec.heard = false;
}

/* Node's name for a signal, as the exit's reason gives it. */
static const char *signal_name(int sig)
{
    static const struct {
        int sig;
        const char *name;
    } names[] = {
        {SIGHUP, "SIGHUP"},   {SIGINT, "SIGINT"},   {SIGQUIT, "SIGQUIT"}, {SIGILL, "SIGILL"},
        {SIGABRT, "SIGABRT"}, {SIGFPE, "SIGFPE"},   {SIGKILL, "SIGKILL"}, {SIGSEGV, "SIGSEGV"},
        {SIGPIPE, "SIGPIPE"}, {SIGALRM, "SIGALRM"}, {SIGTERM, "SIGTERM"}, {SIGUSR1, "SIGUSR1"},
        {SIGUSR2, "SIGUSR2"}, {SIGBUS, "SIGBUS"},
    };
    for (size_t i = 0; i < SK_LEN(names); i++)
        if (names[i].sig == sig)
            return names[i].name;
    return NULL;
}

static void why_add(sk_vizfeed *f, const char *cmd, const char *what)
{
    sk_strv_pushf(&f->why, "%s: %s", cmd, what);
}

static void on_exit_(sk_loop *loop, pid_t pid, int ws, void *ud);

/* A descriptor moved above stdio, closed on exec. */
static int high_cloexec(int fd)
{
    int r = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    close(fd);
    return r;
}

static bool make_pipe(int p[2])
{
    sk_spawn_lock();
    if (pipe(p) != 0) {
        sk_spawn_unlock();
        return false;
    }
    p[0] = high_cloexec(p[0]);
    p[1] = high_cloexec(p[1]);
    sk_spawn_unlock();
    if (p[0] >= 0 && p[1] >= 0)
        return true;
    if (p[0] >= 0)
        close(p[0]);
    if (p[1] >= 0)
        close(p[1]);
    return false;
}

/* Recorder i at path, its stdout and stderr to the loop; false with
 * *why. */
static bool spawn(sk_vizfeed *f, size_t i, const char *path, char **why)
{
    const sk_viz_recorder *r = &f->c.recorders[i];
    int po[2], pe[2];
    if (!make_pipe(po)) {
        *why = sk_xstrdup(strerror(errno));
        return false;
    }
    if (!make_pipe(pe)) {
        *why = sk_xstrdup(strerror(errno));
        close(po[0]);
        close(po[1]);
        return false;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, po[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pe[1], 2);
    /* The loop's thread may block signals, and the app ignores SIGPIPE:
     * the recorder gets the defaults. */
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, def;
    sigemptyset(&none);
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &def);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    sk_strv argv = {0};
    sk_strv_push(&argv, r->cmd);
    for (const char *const *a = r->args; a && *a; a++)
        sk_strv_push(&argv, *a);
    pid_t pid;
    sk_spawn_lock();
    int err = posix_spawn(&pid, path, &fa, &at, sk_strv_argv(&argv), environ);
    sk_spawn_unlock();
    sk_strv_free(&argv);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    close(po[1]);
    close(pe[1]);
    if (err) {
        close(po[0]);
        close(pe[0]);
        *why = sk_xstrdup(err == ENOENT ? "not found" : strerror(err));
        return false;
    }
    for (int k = 0; k < 2; k++) {
        int fd = k ? pe[0] : po[0], fl = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, (fl < 0 ? 0 : fl) | O_NONBLOCK);
    }
    f->rec.pid = pid;
    f->rec.index = i;
    f->rec.heard = false;
    f->rec.out = po[0];
    f->rec.err = pe[0];
    f->rec.out_watch = sk_loop_watch(f->c.loop, po[0], POLLIN, on_out, f);
    f->rec.err_watch = sk_loop_watch(f->c.loop, pe[0], POLLIN, on_err, f);
    sk_loop_child(f->c.loop, pid, on_exit_, f);
    return true;
}

/* Recorder i, else the next once it fails before its first sample. */
static void attempt(sk_vizfeed *f, size_t i)
{
    for (; i < f->c.n_recorders; i++) {
        const sk_viz_recorder *r = &f->c.recorders[i];
        const char *env_path = getenv("PATH");
        char *path = sk_resolve_command(r->cmd, env_path ? env_path : "/usr/bin:/bin");
        if (!path) {
            why_add(f, r->cmd, "not found");
            continue;
        }
        char *why = NULL;
        bool ok = spawn(f, i, path, &why);
        free(path);
        if (ok)
            return;
        why_add(f, r->cmd, why);
        free(why);
    }
    sk_buf all = {0};
    for (size_t k = 0; k < f->why.len; k++)
        sk_buf_printf(&all, "%s%s", k ? "; " : "", f->why.ptr[k]);
    if (!f->why.len)
        sk_buf_append_c(&all, "no recorder of the sound output on this system");
    free(f->reason);
    f->reason = sk_xstrdup(sk_buf_cstr(&all));
    sk_buf_free(&all);
    f->status = SK_VIZFEED_SOUND_FAILED;
    logf_(f, "sound output: %s", f->reason);
}

static void on_exit_(sk_loop *loop, pid_t pid, int ws, void *ud)
{
    (void)loop;
    sk_vizfeed *f = ud;
    if (pid != f->rec.pid) {
        for (size_t i = 0; i < f->dying.len; i++)
            if (f->dying.ptr[i] == pid) {
                f->dying.ptr[i] = f->dying.ptr[--f->dying.len];
                break;
            }
        return;
    }
    read_out(f);
    read_err(f);
    sk_buf what = {0};
    const char *sig = ws != -1 && WIFSIGNALED(ws) ? signal_name(WTERMSIG(ws)) : NULL;
    if (sig)
        sk_buf_printf(&what, "exited %s", sig);
    else if (ws != -1 && WIFSIGNALED(ws))
        sk_buf_printf(&what, "exited signal %d", WTERMSIG(ws));
    else if (ws != -1 && WIFEXITED(ws))
        sk_buf_printf(&what, "exited %d", WEXITSTATUS(ws));
    else
        sk_buf_append_c(&what, "exited");
    /* The last line of what it said. */
    sk_str err = sk_str_trim(sk_buf_str(&f->rec.err_text));
    if (err.len) {
        size_t start = err.len;
        while (start > 0 && err.ptr[start - 1] != '\n')
            start--;
        sk_buf_printf(&what, ": %.*s", (int)(err.len - start), err.ptr + start);
    }
    const char *cmd = f->c.recorders[f->rec.index].cmd;
    size_t next = f->rec.index + 1;
    bool heard = f->rec.heard;
    close_rec(f);
    if (!heard) {
        why_add(f, cmd, sk_buf_cstr(&what));
        attempt(f, next);
    } else {
        free(f->reason);
        sk_buf r = {0};
        sk_buf_printf(&r, "%s: %s", cmd, sk_buf_cstr(&what));
        f->reason = r.ptr;
        f->status = SK_VIZFEED_SOUND_FAILED;
        logf_(f, "sound output: %s", f->reason);
    }
    sk_buf_free(&what);
}

static void sound_start(sk_vizfeed *f)
{
    if (f->rec.pid || f->status == SK_VIZFEED_SOUND_LIVE)
        return;
    memset(f->ring, 0, sizeof f->ring);
    f->at = 0;
    f->odd = -1;
    f->status = SK_VIZFEED_SOUND_STARTING;
    free(f->reason);
    f->reason = NULL;
    sk_strv_free(&f->why);
    attempt(f, 0);
}

/* The recorder this feed started, and no other process. */
static void sound_stop(sk_vizfeed *f)
{
    pid_t pid = f->rec.pid;
    close_rec(f);
    f->odd = -1;
    if (f->status != SK_VIZFEED_SOUND_FAILED)
        f->status = SK_VIZFEED_SOUND_OFF;
    if (pid > 0) {
        kill(pid, SIGTERM);
        sk_vec_push(&f->dying, pid);
    }
}

/* Recording while the window is open on an input that takes the sound
 * output, else not. */
static void sync_sound(sk_vizfeed *f)
{
    if (f->input > 0 && f->open)
        sound_start(f);
    else
        sound_stop(f);
}

/* ---- The feed ---- */

static void on_timer(sk_loop *loop, void *ud)
{
    (void)loop;
    sk_vizfeed_tick(ud, sk_now_ms());
}

sk_vizfeed *sk_vizfeed_new(const sk_vizfeed_config *c)
{
    sk_vizfeed *f = sk_xcalloc(1, sizeof *f);
    f->c = *c;
    if (!c->recorders) {
#ifdef __APPLE__
        f->c.n_recorders = 0;
#else
        f->c.recorders = sk_viz_recorders_linux;
        f->c.n_recorders = SK_VIZ_RECORDERS_LINUX;
#endif
    }
    f->mode_dir = c->mode_dir ? sk_xstrdup(c->mode_dir) : NULL;
    f->input_dir = c->input_dir ? sk_xstrdup(c->input_dir) : NULL;
    f->c.mode_dir = f->mode_dir;
    f->c.input_dir = f->input_dir;
    f->pid = c->pid ? c->pid : getpid();
    f->odd = -1;
    f->rec.out = f->rec.err = -1;

    sk_last_records r = {0};
    read_records(f, f->mode_dir, "mode", &r);
    for (size_t i = 0; i < r.len; i++) {
        int m = sk_viz_mode_find(r.ptr[i].spec);
        if (m >= 0) {
            f->base = m;
            break;
        }
    }
    sk_last_records_free(&r);
    read_records(f, f->input_dir, "input", &r);
    for (size_t i = 0; i < r.len; i++) {
        int k = input_find(r.ptr[i].spec);
        if (k >= 0) {
            /* A system that cannot record the sound output starts on the
             * sessions. */
            f->input = k > 0 && !sound_known(f) ? 0 : k;
            break;
        }
    }
    sk_last_records_free(&r);
    if (!c->manual && c->loop)
        f->timer = sk_loop_timer(c->loop, FEED_MS, FEED_MS, on_timer, f);
    return f;
}

void sk_vizfeed_free(sk_vizfeed *f)
{
    if (!f)
        return;
    if (f->timer)
        sk_loop_cancel(f->c.loop, f->timer);
    sound_stop(f);
    /* What was stopped and not yet reaped is waited for here, killed if
     * it lingers. */
    for (size_t i = 0; i < f->dying.len; i++) {
        pid_t pid = f->dying.ptr[i];
        sk_loop_unchild(f->c.loop, pid);
        int ws;
        uint64_t since = sk_now_ms();
        while (waitpid(pid, &ws, WNOHANG) == 0) {
            if (sk_now_ms() - since >= EXIT_MS) {
                kill(pid, SIGKILL);
                waitpid(pid, &ws, 0);
                break;
            }
            struct timespec ts = {0, 5 * 1000000};
            nanosleep(&ts, NULL);
        }
    }
    sk_vec_free(&f->dying);
    sk_strv_free(&f->why);
    free(f->reason);
    sk_vec_free(&f->tabs);
    sk_vec_free(&f->heard);
    sk_vec_free(&f->modes);
    free(f->mode_dir);
    free(f->input_dir);
    free(f);
}

void sk_vizfeed_tabs(sk_vizfeed *f, const sk_vizfeed_tab *tabs, size_t n)
{
    f->tabs.len = 0;
    for (size_t i = 0; i < n; i++)
        sk_vec_push(&f->tabs, tabs[i]);
}

void sk_vizfeed_active(sk_vizfeed *f, int id)
{
    for (size_t i = 0; i < f->tabs.len; i++)
        f->tabs.ptr[i].active = f->tabs.ptr[i].id == id;
}

static double metric(const sk_bus *b, const char *name)
{
    const sk_val *v = b ? sk_bus_get(b, name) : NULL;
    return v && v->type == SK_VAL_NUM && isfinite(v->num) ? v->num : 0;
}

static heard_at *heard_of(sk_vizfeed *f, int id)
{
    for (size_t i = 0; i < f->heard.len; i++)
        if (f->heard.ptr[i].id == id)
            return &f->heard.ptr[i];
    return NULL;
}

void sk_vizfeed_msg_free(sk_vizfeed_msg *m)
{
    if (!m)
        return;
    if (m->frame.panes)
        for (size_t i = 0; i < m->frame.n_panes; i++)
            free((void *)m->frame.panes[i].events);
    free((void *)m->frame.panes);
    free((void *)m->frame.samples);
    free((void *)m->no_busy);
    free((void *)m->quiet);
    free(m);
}

/* viz.js's feed(): numbers only, per pane in the list's order. */
void sk_vizfeed_tick(sk_vizfeed *f, uint64_t now)
{
    static const char *const NAMES[SK_VIZ_NUMS] = {
        "pty.out_bps", "pty.in_kps", "pty.idle_s", "tokens.out_rate", "state.busy", "proc.cpu_pct", "context.used_pct",
    };
    if (f->open)
        sync_panes(f);
    size_t n = 0;
    for (size_t i = 0; i < f->tabs.len; i++)
        n += f->tabs.ptr[i].pane;
    sk_vizfeed_msg *m = sk_xcalloc(1, sizeof *m);
    sk_viz_pane_frame *panes = sk_xcalloc(n ? n : 1, sizeof *panes);
    bool *no_busy = sk_xcalloc(n ? n : 1, sizeof *no_busy);
    double *quiet = sk_xcalloc(n ? n : 1, sizeof *quiet);

    /* The tabs gone are forgotten. */
    size_t k = 0;
    for (size_t i = 0; i < f->heard.len; i++) {
        bool kept = false;
        for (size_t j = 0; j < f->tabs.len && !kept; j++)
            kept = f->tabs.ptr[j].id == f->heard.ptr[i].id;
        if (kept)
            f->heard.ptr[k++] = f->heard.ptr[i];
    }
    f->heard.len = k;

    size_t p = 0;
    int buf[512];
    for (size_t i = 0; i < f->tabs.len; i++) {
        const sk_vizfeed_tab *t = &f->tabs.ptr[i];
        /* Every tab's events, a pane's kept for its frame. */
        SK_VEC(int) ev = {0};
        for (size_t got = 0; t->bus && (got = sk_bus_drain_events(t->bus, buf, SK_LEN(buf))) > 0;)
            for (size_t j = 0; j < got; j++)
                sk_vec_push(&ev, buf[j]);
        heard_at *h = heard_of(f, t->id);
        if (!h) {
            heard_at a = {t->id, now};
            sk_vec_push(&f->heard, a);
            h = &f->heard.ptr[f->heard.len - 1];
        } else if (ev.len) {
            h->at = now;
        }
        if (!t->pane) {
            sk_vec_free(&ev);
            continue;
        }
        sk_viz_pane_frame *pf = &panes[p];
        pf->tab = t->id;
        pf->place = (int)i + 1;
        pf->active = t->active;
        tab_mode *e = f->open ? mode_entry(f, t->id) : NULL;
        pf->mode = e ? e->mode : -1;
        pf->open = f->open && f->has_menu && f->menu == t->id;
        for (int j = 0; j < SK_VIZ_NUMS; j++)
            pf->n[j] = metric(t->bus, NAMES[j]);
        pf->events = ev.ptr;
        pf->n_events = ev.len;
        const sk_val *idle = t->bus ? sk_bus_get(t->bus, "pty.idle_s") : NULL;
        double since = (double)(now - SK_MIN(now, h->at)) / 1000.0;
        quiet[p] = idle && idle->type == SK_VAL_NUM && isfinite(idle->num) ? SK_MIN(idle->num, since) : since;
        const sk_val *busy = t->bus ? sk_bus_get(t->bus, "state.busy") : NULL;
        no_busy[p] = !(busy && busy->type == SK_VAL_NUM);
        p++;
    }
    m->frame.panes = panes;
    m->frame.n_panes = n;
    m->no_busy = no_busy;
    m->quiet = quiet;
    m->frame.src = f->input;
    m->frame.st = f->status == SK_VIZFEED_SOUND_LIVE     ? SK_VIZ_SOUND_LIVE
                  : f->status == SK_VIZFEED_SOUND_FAILED ? SK_VIZ_SOUND_FAILED
                                                         : SK_VIZ_SOUND_NONE;
    if (f->open && f->input > 0 && f->status == SK_VIZFEED_SOUND_LIVE) {
        float *s = sk_xmalloc(SK_SYNTH_N * sizeof *s);
        sk_vizfeed_samples(f, s);
        m->frame.samples = s;
        m->frame.n_samples = SK_SYNTH_N;
    }
    m->open = f->open;
    m->opened = f->opened;
    if (f->c.frame)
        f->c.frame(f->c.ud, m);
    else
        sk_vizfeed_msg_free(m);
}

bool sk_vizfeed_is_open(const sk_vizfeed *f)
{
    return f->open;
}

void sk_vizfeed_open(sk_vizfeed *f)
{
    if (f->open)
        return;
    f->open = true;
    f->opened++;
    sync_sound(f);
}

/* As the window's closed event has it: no drop-down open, and the sound
 * output no longer recorded. */
void sk_vizfeed_close(sk_vizfeed *f)
{
    if (!f->open)
        return;
    f->open = false;
    f->has_menu = false;
    sync_sound(f);
}

void sk_vizfeed_toggle(sk_vizfeed *f)
{
    if (f->open)
        sk_vizfeed_close(f);
    else
        sk_vizfeed_open(f);
}

size_t sk_vizfeed_modes(sk_vizfeed *f, sk_vizfeed_choice *out)
{
    const sk_vizfeed_tab *a = active_pane(f);
    const tab_mode *e = a ? mode_entry(f, a->id) : NULL;
    for (int i = 0; i < SK_VIZ_MODES; i++)
        out[i] = (sk_vizfeed_choice){sk_viz_modes[i].id, sk_viz_modes[i].label, e && e->mode == i, true};
    return SK_VIZ_MODES;
}

size_t sk_vizfeed_inputs(const sk_vizfeed *f, sk_vizfeed_choice *out)
{
    for (int i = 0; i < SK_VIZ_INPUTS; i++)
        out[i] = (sk_vizfeed_choice){sk_viz_inputs[i].id, sk_viz_inputs[i].label, i == f->input,
                                     i == 0 || sound_known(f)};
    return SK_VIZ_INPUTS;
}

bool sk_vizfeed_select(sk_vizfeed *f, const char *id)
{
    int i = sk_viz_mode_find(id);
    if (i < 0)
        return false;
    const sk_vizfeed_tab *a = active_pane(f);
    if (a)
        set_mode(f, a->id, i);
    sk_vizfeed_open(f);
    return true;
}

bool sk_vizfeed_select_input(sk_vizfeed *f, const char *id, const char **why)
{
    int i = input_find(id);
    if (i < 0) {
        if (why)
            *why = "no such visualization input";
        return false;
    }
    if (i > 0 && !sound_known(f)) {
        if (why)
            *why = "the sound output is recorded on Linux only";
        return false;
    }
    if (i != f->input) {
        f->input = i;
        record(f, f->input_dir, "input", id);
        if (f->status == SK_VIZFEED_SOUND_FAILED)
            f->status = SK_VIZFEED_SOUND_OFF;
    }
    sk_vizfeed_open(f);
    sync_sound(f);
    return true;
}

void sk_vizfeed_step(sk_vizfeed *f)
{
    const sk_vizfeed_tab *a = active_pane(f);
    tab_mode *e = a ? mode_entry(f, a->id) : NULL;
    if (e)
        set_mode(f, e->id, (e->mode + 1) % SK_VIZ_MODES);
}

void sk_vizfeed_press(sk_vizfeed *f, double x, double y, int w, int h)
{
    sync_panes(f);
    int ids[256], n = 0, open_at = -1;
    for (size_t i = 0; i < f->tabs.len && n < (int)SK_LEN(ids); i++) {
        if (!f->tabs.ptr[i].pane)
            continue;
        if (f->has_menu && f->tabs.ptr[i].id == f->menu)
            open_at = n;
        ids[n++] = f->tabs.ptr[i].id;
    }
    sk_viz_hit hit;
    bool was = f->has_menu;
    int was_tab = f->menu;
    f->has_menu = false;
    if (!sk_viz_press(x, y, n, SK_VIZ_MODES, w, h, open_at, &hit) || hit.pane < 0 || hit.pane >= n)
        return;
    int tab = ids[hit.pane];
    if (hit.button) {
        f->has_menu = !(was && was_tab == tab);
        f->menu = tab;
    } else if (hit.item >= 0 && hit.item < SK_VIZ_MODES) {
        set_mode(f, tab, hit.item);
    }
}

bool sk_vizfeed_set_mode(sk_vizfeed *f, int tab, int mode)
{
    sync_panes(f);
    if (mode < 0 || mode >= SK_VIZ_MODES || !pane_tab(f, tab))
        return false;
    set_mode(f, tab, mode);
    return true;
}

int sk_vizfeed_mode_of(sk_vizfeed *f, int tab)
{
    sync_panes(f);
    const tab_mode *e = mode_entry(f, tab);
    return e ? e->mode : -1;
}

int sk_vizfeed_menu_tab(const sk_vizfeed *f)
{
    return f->has_menu ? f->menu : 0;
}

int sk_vizfeed_input(const sk_vizfeed *f)
{
    return f->input;
}

int sk_vizfeed_sound(const sk_vizfeed *f, const char **reason)
{
    if (reason)
        *reason = f->reason;
    return f->status;
}

void sk_vizfeed_samples(const sk_vizfeed *f, float *out)
{
    memcpy(out, f->ring + f->at, (SK_SYNTH_N - f->at) * sizeof *out);
    memcpy(out + (SK_SYNTH_N - f->at), f->ring, f->at * sizeof *out);
}

/* ---- From other threads ---- */

typedef struct {
    sk_vizfeed *f;
    int kind;
    double x, y;
    int w, h;
    unsigned opened;
} post_msg;

enum { POST_PRESS, POST_STEP, POST_CLOSED };

static void do_post(void *ud)
{
    post_msg *m = ud;
    if (m->kind == POST_PRESS)
        sk_vizfeed_press(m->f, m->x, m->y, m->w, m->h);
    else if (m->kind == POST_STEP)
        sk_vizfeed_step(m->f);
    else if (m->f->open && m->f->opened == m->opened)
        sk_vizfeed_close(m->f);
    free(m);
}

/* A message the loop never ran, as the app ends. */
static void drop_post(void *ud)
{
    free(ud);
}

static void post(sk_vizfeed *f, post_msg m)
{
    post_msg *p = sk_xmalloc(sizeof *p);
    *p = m;
    p->f = f;
    sk_loop_post_owned(f->c.loop, do_post, p, drop_post);
}

void sk_vizfeed_post_press(sk_vizfeed *f, double x, double y, int w, int h)
{
    post(f, (post_msg){f, POST_PRESS, x, y, w, h, 0});
}

void sk_vizfeed_post_step(sk_vizfeed *f)
{
    post(f, (post_msg){f, POST_STEP, 0, 0, 0, 0, 0});
}

void sk_vizfeed_post_closed(sk_vizfeed *f, unsigned opened)
{
    post(f, (post_msg){f, POST_CLOSED, 0, 0, 0, 0, opened});
}
