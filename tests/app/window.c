/* skinned's window, driven through its control socket: the live checks
 * of scripts/checks/tabs.js (decision 0023) in C, as decision 0036
 * plans. The real program runs with its window hidden; the checks type,
 * press the app's chords, click, drop files and read the probe. They
 * need a display: the window server on macOS, $DISPLAY on Linux (Xvfb
 * in CI); without one they print a skip line. */
#include "../../src/app/lastskin.h"
#include "../../src/base/arena.h"
#include "../../src/base/paths.h"
#include "../../src/base/json.h"
#include "../../src/base/strv.h"
#include "../../src/core/command.h"
#include "../../src/core/loop.h"
#include "../../src/sessions/files.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#ifdef __APPLE__
/* The app's chords: Cmd on macOS, Ctrl+Shift on Linux. */
#define APP_MODS "[\"super\"]"
#else
#define APP_MODS "[\"ctrl\",\"shift\"]"
#endif

typedef struct {
    pid_t pid;
    int fd;
    sk_buf in;
    int status;
} app;

static bool have_display(void)
{
#ifdef __APPLE__
    return true;
#else
    const char *d = getenv("DISPLAY");
    if (!d || !*d)
        printf("    (skipped: no $DISPLAY)\n");
    return d && *d;
#endif
}

/* The app on cmd in the test's directory, its control socket there.
 * With name, it runs as a link of that name to skinned, as make install
 * makes them, given cmd as its arguments; path_first goes before PATH. */
static bool start_as(app *a, const char *name, const char *const *cmd, const char *path_first)
{
    memset(a, 0, sizeof *a);
    a->fd = -1;
    const char *bin = getenv("SK_SKINNED_BIN");
    if (!bin || access(bin, X_OK) != 0 || !have_display())
        return false;
    const char *tmp = sk_test_tmpdir();
    char whole[4096];
    if (!realpath(bin, whole))
        return false;
    sk_buf sock = {0};
    sk_buf_printf(&sock, "%s/ctl.sock", tmp);
    sk_strv argv = {0}, env = {0};
    char *exe = sk_xstrdup(whole);
    if (name) {
        char *dir = sk_path_join(tmp, "as");
        free(exe);
        exe = sk_path_join(dir, name);
        CHECK_INT(sk_mkdirs(dir, 0700), 0);
        CHECK(symlink(whole, exe) == 0 || errno == EEXIST);
        free(dir);
    }
    sk_strv_push(&argv, exe);
    if (!name)
        sk_strv_push(&argv, "--");
    for (; *cmd; cmd++)
        sk_strv_push(&argv, *cmd);
    for (char **e = environ; *e; e++) {
        if (!strncmp(*e, "XDG_", 4) || !strncmp(*e, "SKINNED_", 8))
            continue;
        if (path_first && !strncmp(*e, "PATH=", 5))
            sk_strv_pushf(&env, "PATH=%s:%s", path_first, *e + 5);
        else
            sk_strv_push(&env, *e);
    }
    sk_strv_pushf(&env, "XDG_STATE_HOME=%s/state", tmp);
    sk_strv_pushf(&env, "XDG_RUNTIME_DIR=%s/run", tmp);
    sk_strv_pushf(&env, "SKINNED_CONTROL=%s", sock.ptr);
    sk_strv_push(&env, "SKINNED_HIDDEN=1");
    /* Silent: a skin's sounds would play on the machine's real output. */
    sk_strv_push(&env, "SKINNED_MUTE=1");
    a->pid = fork();
    if (a->pid == 0) {
        /* Its stderr is kept in the test's folder, for what a sanitizer
         * says (finish). */
        if (chdir(tmp) != 0)
            _exit(98);
        int null = open("/dev/null", O_RDWR), err = open("skinned.err", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (null < 0 || err < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 || dup2(err, 2) < 0)
            _exit(98);
        /* As a terminal starts it: no fd past stderr, so sk_tell writes
         * to stderr too. */
        if (null > 2)
            close(null);
        if (err > 2)
            close(err);
        execve(exe, sk_strv_argv(&argv), sk_strv_argv(&env));
        _exit(99);
    }
    free(exe);
    sk_strv_free(&argv);
    sk_strv_free(&env);
    /* The socket comes once the window is up. */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (sock.len >= sizeof addr.sun_path) {
        sk_test_fail(__FILE__, __LINE__, "%s: too long for a socket", sock.ptr);
        sk_buf_free(&sock);
        return false;
    }
    memcpy(addr.sun_path, sock.ptr, sock.len);
    sk_buf_free(&sock);
    for (uint64_t until = sk_now_ms() + 15000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
            a->fd = fd;
            return true;
        }
        if (fd >= 0)
            close(fd);
        int ws;
        if (waitpid(a->pid, &ws, WNOHANG) == a->pid) {
            a->pid = 0;
            sk_test_fail(__FILE__, __LINE__, "skinned exited before its socket came: %d", ws);
            return false;
        }
    }
    sk_test_fail(__FILE__, __LINE__, "no control socket after 15 s");
    return false;
}

static bool start(app *a, const char *const *cmd)
{
    return start_as(a, NULL, cmd, NULL);
}

/* The app's status once it has exited, killing it after ms. */
static int finish(app *a, int ms)
{
    if (a->fd >= 0)
        close(a->fd);
    a->fd = -1;
    sk_buf_free(&a->in);
    if (a->pid <= 0)
        return -1;
    int ws = 0;
    uint64_t until = sk_now_ms() + (uint64_t)ms;
    while (waitpid(a->pid, &ws, WNOHANG) == 0) {
        if (sk_now_ms() > until) {
            kill(a->pid, SIGKILL);
            waitpid(a->pid, &ws, 0);
            sk_test_fail(__FILE__, __LINE__, "skinned did not exit within %d ms", ms);
            break;
        }
        sk_test_sleep_ms(20);
    }
    a->pid = 0;
    /* What a sanitizer said is the failure, whatever the status. */
    sk_buf path = {0};
    sk_buf_printf(&path, "%s/skinned.err", sk_test_tmpdir());
    char *err = sk_test_read(path.ptr, NULL);
    if (err && (strstr(err, "Sanitizer") || strstr(err, "runtime error:")))
        sk_test_fail(__FILE__, __LINE__, "skinned's stderr:\n%.4000s", err);
    free(err);
    sk_buf_free(&path);
    return WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + WTERMSIG(ws);
}

/* One request and its answer, parsed into ar; NULL after 10 s. */
static const sk_json *ask(app *a, sk_arena *ar, const char *req)
{
    size_t n = strlen(req);
    if (write(a->fd, req, n) != (ssize_t)n || write(a->fd, "\n", 1) != 1)
        return NULL;
    for (uint64_t until = sk_now_ms() + 10000;;) {
        char *nl = a->in.len ? memchr(a->in.ptr, '\n', a->in.len) : NULL;
        if (nl) {
            size_t len = (size_t)(nl - a->in.ptr);
            const sk_json *v = sk_json_parse(ar, a->in.ptr, len, NULL);
            sk_buf_consume(&a->in, len + 1);
            return v;
        }
        struct pollfd pf = {a->fd, POLLIN, 0};
        uint64_t now = sk_now_ms();
        if (now >= until || poll(&pf, 1, (int)(until - now)) <= 0)
            return NULL;
        char buf[65536];
        ssize_t r = read(a->fd, buf, sizeof buf);
        if (r <= 0)
            return NULL;
        sk_buf_append(&a->in, buf, (size_t)r);
    }
}

static bool ok(app *a, const char *req)
{
    sk_arena ar;
    sk_arena_init(&ar, 0);
    bool good = false;
    const sk_json *v = ask(a, &ar, req);
    sk_json_as_bool(sk_json_get(v, "ok"), &good);
    if (!good)
        sk_test_fail(__FILE__, __LINE__, "%s: not ok", req);
    sk_arena_free(&ar);
    return good;
}

/* The probe, parsed into ar. */
static const sk_json *probe(app *a, sk_arena *ar)
{
    return ask(a, ar, "{\"op\":\"probe\"}");
}

static bool screen_has(const sk_json *p, const char *needle)
{
    sk_str s = {0};
    return sk_json_as_string(sk_json_get(p, "screen"), &s) && s.ptr && strstr(s.ptr, needle);
}

/* Probes until the active screen shows needle, at most ms. */
static bool wait_screen(app *a, const char *needle, int ms)
{
    for (uint64_t until = sk_now_ms() + (uint64_t)ms;; sk_test_sleep_ms(50)) {
        sk_arena ar;
        sk_arena_init(&ar, 0);
        bool there = screen_has(probe(a, &ar), needle);
        sk_arena_free(&ar);
        if (there)
            return true;
        if (sk_now_ms() > until)
            return false;
    }
}

static double num(const sk_json *v, const char *key)
{
    double d = -1;
    sk_json_as_number(sk_json_get(v, key), &d);
    return d;
}

static void test_typing_reaches_the_tab(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    ok(&a, "{\"op\":\"type\",\"text\":\"hello window\"}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Enter\"}");
    /* The pty echoes the line, then cat writes it back. */
    CHECK(wait_screen(&a, "hello window\nhello window", 5000));
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* tabs.js keys: New tab makes a second, active tab with its own CLI;
 * typing reaches only the active one; Ctrl+Tab goes back; the chords
 * send the CLI nothing; Close tab leaves one. */
static void test_the_tab_chords(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *p = probe(&a, &ar);
    CHECK_INT(sk_json_get(p, "tabs") ? sk_json_get(p, "tabs")->array.len : 0, 1);
    bool strip = true;
    sk_json_as_bool(sk_json_dig(p, "window", "strip", NULL), &strip);
    CHECK(!strip);
    ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (sk_json_get(p, "tabs") && sk_json_get(p, "tabs")->array.len == 2)
            break;
    }
    const sk_json *tabs = sk_json_get(p, "tabs");
    CHECK(tabs && tabs->array.len == 2);
    if (tabs && tabs->array.len == 2) {
        const sk_json *one = sk_json_at(tabs, 0), *two = sk_json_at(tabs, 1);
        bool active = false;
        sk_json_as_bool(sk_json_get(two, "active"), &active);
        CHECK(active);
        CHECK(num(one, "pid") > 0 && num(two, "pid") > 0 && num(one, "pid") != num(two, "pid"));
        sk_json_as_bool(sk_json_dig(p, "window", "strip", NULL), &strip);
        CHECK(strip);
    }
    ok(&a, "{\"op\":\"type\",\"text\":\"second\"}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Enter\"}");
    CHECK(wait_screen(&a, "second\nsecond", 5000));
    ok(&a, "{\"op\":\"key\",\"key\":\"Tab\",\"mods\":[\"ctrl\"]}");
    sk_test_sleep_ms(300);
    p = probe(&a, &ar);
    CHECK_INT(num(p, "active"), 1);
    CHECK(!screen_has(p, "second"));
    ok(&a, "{\"op\":\"key\",\"key\":\"w\",\"mods\":" APP_MODS "}");
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (sk_json_get(p, "tabs") && sk_json_get(p, "tabs")->array.len == 1)
            break;
    }
    CHECK(sk_json_get(p, "tabs") && sk_json_get(p, "tabs")->array.len == 1);
    CHECK(screen_has(p, "second"));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* Decision 0003: the app ends with its last tab, with its status. */
static void test_the_app_ends_with_the_last_tabs_status(void)
{
    app a;
    const char *cmd[] = {"sh", "-c", "sleep 0.5; exit 7", NULL};
    if (!start(&a, cmd))
        return;
    CHECK_INT(finish(&a, 10000), 7);
}

/* The middle of cell (col, row) in window points, from the probe's
 * grid: the skin places the terminal, and sizes its font. */
static void cell_at(const sk_json *p, double col, double row, double *x, double *y)
{
    const sk_json *g = sk_json_dig(p, "window", "grid", NULL);
    double v[4] = {4, 26, 8, 16};
    for (size_t i = 0; i < 4; i++)
        sk_json_as_number(sk_json_at(g, i), &v[i]);
    *x = v[0] + v[2] * (col + 0.5);
    *y = v[1] + v[3] * (row + 0.5);
}

/* A double click selects a word, as xterm.js does. */
static void test_a_double_click_selects_a_word(void)
{
    app a;
    const char *cmd[] = {"sh", "-c", "echo pick selectme please; exec cat", NULL};
    if (!start(&a, cmd))
        return;
    CHECK(wait_screen(&a, "selectme", 5000));
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *p = probe(&a, &ar);
    double x, y;
    cell_at(p, 7, 0, &x, &y);
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%.1f,\"y\":%.1f,\"button\":1,\"clicks\":2}",
             x, y);
    ok(&a, req);
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":%.1f,\"y\":%.1f,\"button\":1}", x, y);
    ok(&a, req);
    p = probe(&a, &ar);
    sk_str sel = {0};
    CHECK(sk_json_as_string(sk_json_get(p, "selection"), &sel));
    CHECK(sel.ptr && !strcmp(sel.ptr, "selectme"));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    finish(&a, 5000);
}

/* addon-web-links: a URL in the text is a link under the pointer. */
static void test_a_url_in_the_text_is_a_link(void)
{
    app a;
    const char *cmd[] = {"sh", "-c", "echo see https://example.com/a/b here; exec cat", NULL};
    if (!start(&a, cmd))
        return;
    CHECK(wait_screen(&a, "here", 5000));
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *p = probe(&a, &ar);
    double x, y, off_x, off_y;
    cell_at(p, 10, 0, &x, &y);
    cell_at(p, 1, 0, &off_x, &off_y);
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%.1f,\"y\":%.1f}", x, y);
    ok(&a, req);
    p = probe(&a, &ar);
    sk_str link = {0};
    CHECK(sk_json_as_string(sk_json_get(p, "link"), &link));
    CHECK(link.ptr && !strcmp(link.ptr, "https://example.com/a/b"));
    /* A change of tabs with a link under the pointer drops it once. */
    ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    sk_test_sleep_ms(500);
    p = probe(&a, &ar);
    CHECK(p != NULL);
    CHECK(!sk_json_as_string(sk_json_get(p, "link"), &link));
    ok(&a, "{\"op\":\"key\",\"key\":\"Tab\",\"mods\":[\"ctrl\"]}");
    sk_test_sleep_ms(300);
    ok(&a, req);
    /* Off the URL, no link. */
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%.1f,\"y\":%.1f}", off_x, off_y);
    ok(&a, req);
    p = probe(&a, &ar);
    CHECK(!sk_json_as_string(sk_json_get(p, "link"), &link));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    finish(&a, 5000);
}

static size_t len_of(const sk_json *v)
{
    return v && v->type == SK_JSON_ARRAY ? v->array.len : 0;
}

/* Decision 0023: a tab dragged out of its strip and let go outside the
 * window leaves for a new window, which then works on its own and goes
 * with its last tab. */
static void test_a_tab_dragged_out_gets_its_own_window(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *p = NULL;
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (len_of(sk_json_get(p, "tabs")) == 2)
            break;
    }
    CHECK_INT(len_of(sk_json_get(p, "tabs")), 2);
    /* The second tab of the strip, below the identity strip: "cat" makes
     * each tab about 70 points wide, so x 70 to 140, y 22 to 46. */
    ok(&a, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":100,\"y\":34,\"button\":1}");
    ok(&a, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":112,\"y\":34}");
    ok(&a, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":120,\"y\":-60,\"button\":1}");
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (len_of(sk_json_get(p, "windows")) == 2)
            break;
    }
    const sk_json *wins = sk_json_get(p, "windows");
    CHECK_INT(len_of(wins), 2);
    /* At the drop, up and left of it, at the first window's size, as
     * popTab placed it. */
    const sk_json *w0 = sk_json_at(wins, 0), *w1 = sk_json_at(wins, 1);
    if (num(w0, "id") != 0) {
        const sk_json *t = w0;
        w0 = w1;
        w1 = t;
    }
    CHECK_INT((int)num(w1, "x"), SK_MAX(0, (int)num(w0, "x") + 120 - 40));
    CHECK_INT((int)num(w1, "y"), SK_MAX(0, (int)num(w0, "y") - 60 - 10));
    CHECK_INT((int)num(w1, "w"), (int)num(w0, "w"));
    CHECK_INT((int)num(w1, "h"), (int)num(w0, "h"));
    int second = -1;
    for (size_t i = 0; i < len_of(wins); i++) {
        const sk_json *w = sk_json_at(wins, i);
        CHECK_INT(len_of(sk_json_get(w, "tabs")), 1);
        if (num(w, "id") != 0)
            second = (int)num(w, "id");
    }
    CHECK(second > 0);
    /* Typing in the new window reaches its tab alone. */
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"type\",\"text\":\"away\\n\",\"win\":%d}", second);
    ok(&a, req);
    bool there = false;
    for (uint64_t until = sk_now_ms() + 5000; !there && sk_now_ms() < until; sk_test_sleep_ms(50)) {
        snprintf(req, sizeof req, "{\"op\":\"probe\",\"win\":%d}", second);
        there = screen_has(ask(&a, &ar, req), "away\naway");
    }
    CHECK(there);
    CHECK(!screen_has(probe(&a, &ar), "away"));
    /* Closing it hangs up its tab; the first window stays. */
    snprintf(req, sizeof req, "{\"op\":\"close\",\"win\":%d}", second);
    ok(&a, req);
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (len_of(sk_json_get(p, "windows")) == 1)
            break;
    }
    CHECK_INT(len_of(sk_json_get(p, "windows")), 1);
    CHECK_INT(len_of(sk_json_get(p, "tabs")), 1);
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* Decision 0016: closing the window hangs up every tab. */
static void test_closing_the_window_hangs_up(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    ok(&a, "{\"op\":\"close\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* tabs.js: a dragged tab goes before the first other tab whose middle
 * lies right of the pointer. Three tabs of cat: the first dragged past
 * the second's middle, not the third's. */
static void test_a_tab_dragged_along_the_strip(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *p = NULL;
    for (int k = 0; k < 2; k++)
        ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (len_of(sk_json_dig(p, "window", "tabs", NULL)) == 3)
            break;
    }
    const sk_json *tabs = sk_json_dig(p, "window", "tabs", NULL);
    CHECK_INT(len_of(tabs), 3);
    if (len_of(tabs) == 3) {
        double one = -1, two = -1, three = -1;
        sk_json_as_number(sk_json_at(tabs, 0), &one);
        sk_json_as_number(sk_json_at(tabs, 1), &two);
        sk_json_as_number(sk_json_at(tabs, 2), &three);
        /* From the first tab's middle to just past the second's. */
        const sk_json *r = sk_json_dig(p, "window", "tab_rects", NULL);
        double x0 = -1, w0 = 0, x1 = -1, w1 = 0;
        sk_json_as_number(sk_json_at(sk_json_at(r, 0), 0), &x0);
        sk_json_as_number(sk_json_at(sk_json_at(r, 0), 1), &w0);
        sk_json_as_number(sk_json_at(sk_json_at(r, 1), 0), &x1);
        sk_json_as_number(sk_json_at(sk_json_at(r, 1), 1), &w1);
        CHECK(x0 == 0 && w0 > 0 && x1 > 0 && w1 > 0);
        double from = x0 + w0 / 2, to = x1 + w1 / 2 + 5;
        char req[128];
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%g,\"y\":34}", from);
        ok(&a, req);
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%g,\"y\":34}", from + 10);
        ok(&a, req);
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%g,\"y\":40}", to);
        ok(&a, req);
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":%g,\"y\":40}", to);
        ok(&a, req);
        double first = -1;
        for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
            p = probe(&a, &ar);
            sk_json_as_number(sk_json_at(sk_json_dig(p, "window", "tabs", NULL), 0), &first);
            if (first == two)
                break;
        }
        tabs = sk_json_dig(p, "window", "tabs", NULL);
        double at[3] = {-1, -1, -1};
        for (size_t i = 0; i < 3; i++)
            sk_json_as_number(sk_json_at(tabs, i), &at[i]);
        CHECK(at[0] == two && at[1] == one && at[2] == three);
    }
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* The probe once its drop-down shows menu, or with menu NULL once it is
 * closed; NULL after ms. */
static const sk_json *wait_menu(app *a, sk_arena *ar, const char *menu, int ms)
{
    for (uint64_t until = sk_now_ms() + (uint64_t)ms;; sk_test_sleep_ms(50)) {
        const sk_json *p = probe(a, ar);
        bool open = false;
        sk_json_as_bool(sk_json_dig(p, "menu", "open", NULL), &open);
        sk_str shown = {0};
        sk_json_as_string(sk_json_dig(p, "menu", "shown", NULL), &shown);
        if (menu ? open && sk_str_eq_c(shown, menu) : !open)
            return p;
        if (sk_now_ms() > until)
            return NULL;
    }
}

/* The drop-down's row whose text is text, from a probe. */
static const sk_json *menu_row(const sk_json *p, const char *text)
{
    const sk_json *rows = sk_json_dig(p, "menu", "rows", NULL);
    for (size_t i = 0; rows && i < rows->array.len; i++) {
        sk_str t = {0};
        sk_json_as_string(sk_json_get(sk_json_at(rows, i), "text"), &t);
        if (sk_str_eq_c(t, text))
            return sk_json_at(rows, i);
    }
    return NULL;
}

/* A press in the middle of a row, x points in from the drop-down's
 * left. */
static void press_row(app *a, const sk_json *p, const sk_json *row, double x)
{
    char req[160];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%g,\"y\":%g}",
             num(sk_json_get(p, "menu"), "x") + x, num(row, "y") + num(row, "h") / 2);
    ok(a, req);
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":%g,\"y\":%g}",
             num(sk_json_get(p, "menu"), "x") + x, num(row, "y") + num(row, "h") / 2);
    ok(a, req);
}

/* The drop-down of decision 0022, from a stand-in for the skin's
 * button: it opens on its menu, Right and Escape step and close it,
 * New tab opens a tab, Clear logs answers with a note, and a font size
 * picked in Settings sizes the terminal and is saved. */
static void test_the_menus(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    ok(&a, "{\"op\":\"menu\",\"show\":\"File\",\"button\":[100,40,30,30]}");
    const sk_json *p = wait_menu(&a, &ar, "File", 5000);
    CHECK(p != NULL);
    CHECK(menu_row(p, "New tab") != NULL);
    CHECK_INT(num(sk_json_get(p, "menu"), "y"), 72);
    ok(&a, "{\"op\":\"key\",\"key\":\"Right\"}");
    p = wait_menu(&a, &ar, "Agents", 5000);
    CHECK(p != NULL);
    CHECK(menu_row(p, "Sessions dashboard") != NULL);
    /* Every other key is the menu's while it is open: none reaches cat. */
    ok(&a, "{\"op\":\"type\",\"text\":\"dropped\"}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Escape\"}");
    CHECK(wait_menu(&a, &ar, NULL, 5000) != NULL);
    p = probe(&a, &ar);
    CHECK(!screen_has(p, "dropped"));

    ok(&a, "{\"op\":\"menu\",\"show\":\"File\"}");
    p = wait_menu(&a, &ar, "File", 5000);
    const sk_json *row = p ? menu_row(p, "New tab") : NULL;
    CHECK(row != NULL);
    if (row)
        press_row(&a, p, row, 20);
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (sk_json_get(p, "tabs") && sk_json_get(p, "tabs")->array.len == 2)
            break;
    }
    CHECK(sk_json_get(p, "tabs") && sk_json_get(p, "tabs")->array.len == 2);
    bool open = true;
    sk_json_as_bool(sk_json_dig(p, "menu", "open", NULL), &open);
    CHECK(!open);

    /* A row that waits keeps the menu open with its answer. */
    ok(&a, "{\"op\":\"menu\",\"show\":\"File\"}");
    p = wait_menu(&a, &ar, "File", 5000);
    row = p ? menu_row(p, "Clear logs") : NULL;
    CHECK(row != NULL);
    if (row)
        press_row(&a, p, row, 20);
    sk_str msg = {0};
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (sk_json_as_string(sk_json_dig(p, "menu", "message", NULL), &msg))
            break;
    }
    CHECK(msg.ptr && !strncmp(msg.ptr, "Removed ", 8) && strstr(msg.ptr, "of ended windows."));

    /* Settings: the flyout opens on its row, and 16 px is picked. */
    int cols = (int)num(sk_json_get(p, "window"), "cols");
    row = menu_row(p, "Terminal font size");
    CHECK(row != NULL);
    if (row) {
        double mx = num(sk_json_get(p, "menu"), "x"), mw = num(sk_json_get(p, "menu"), "w");
        double ry = num(row, "y");
        char req[160];
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%g,\"y\":%g}", mx + 20, ry + 5);
        ok(&a, req);
        p = probe(&a, &ar);
        const sk_json *fly = sk_json_dig(p, "menu", "flyout", NULL);
        size_t pick = (size_t)-1;
        for (size_t i = 0; fly && fly->type == SK_JSON_ARRAY && i < fly->array.len; i++) {
            sk_str t = {0};
            sk_json_as_string(sk_json_get(sk_json_at(fly, i), "text"), &t);
            if (sk_str_eq_c(t, "  16 px"))
                pick = i;
        }
        CHECK(pick != (size_t)-1);
        /* The flyout's rows start level with its own, to its right. */
        double y = ry + 20.8 * (double)pick + 10;
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%g,\"y\":%g}", mx + mw + 20, y);
        ok(&a, req);
        snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%g,\"y\":%g}", mx + mw + 20, y);
        ok(&a, req);
        CHECK(wait_menu(&a, &ar, NULL, 5000) != NULL);
        for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
            p = probe(&a, &ar);
            if (num(sk_json_get(p, "window"), "cols") < cols)
                break;
        }
        CHECK(num(sk_json_get(p, "window"), "cols") < cols);
        sk_buf f = {0};
        sk_buf_printf(&f, "%s/state/claude-skins/settings.json", sk_test_tmpdir());
        char *text = sk_test_read(f.ptr, NULL);
        CHECK(text && strstr(text, "16"));
        free(text);
        sk_buf_free(&f);
    }
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* Agents' Sessions dashboard opens the dashboard's tab (decision 0028),
 * whose pane shows its samples; asked again, it makes it active. */
static void test_the_sessions_dashboard(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    ok(&a, "{\"op\":\"menu\",\"show\":\"Agents\",\"button\":[100,40,30,30]}");
    const sk_json *p = wait_menu(&a, &ar, "Agents", 5000);
    const sk_json *row = p ? menu_row(p, "Sessions dashboard") : NULL;
    CHECK(row != NULL);
    if (row)
        press_row(&a, p, row, 20);
    double ticks = 0;
    for (uint64_t until = sk_now_ms() + 8000; sk_now_ms() < until; sk_test_sleep_ms(100)) {
        p = probe(&a, &ar);
        ticks = num(sk_json_get(p, "dashboard"), "ticks");
        if (ticks >= 1)
            break;
    }
    CHECK(ticks >= 1);
    const sk_json *tabs = sk_json_get(p, "tabs");
    CHECK_INT(len_of(tabs), 2);
    sk_str kind = {0};
    sk_json_as_string(sk_json_get(sk_json_at(tabs, 1), "kind"), &kind);
    CHECK_MEM(kind, "dashboard");
    /* Back to cat, and the menu again: the same tab, made active. */
    ok(&a, "{\"op\":\"key\",\"key\":\"Tab\",\"mods\":[\"ctrl\"]}");
    ok(&a, "{\"op\":\"menu\",\"show\":\"Agents\"}");
    p = wait_menu(&a, &ar, "Agents", 5000);
    row = p ? menu_row(p, "Sessions dashboard") : NULL;
    if (row)
        press_row(&a, p, row, 20);
    bool back = false;
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until && !back; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        back = sk_json_get(p, "dashboard") && sk_json_get(p, "dashboard")->type == SK_JSON_OBJECT;
    }
    CHECK(back);
    CHECK_INT(len_of(sk_json_get(p, "tabs")), 2);
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* The name of the probe's tab id, into out. */
static bool tab_name(const sk_json *p, double id, sk_str *out)
{
    const sk_json *tabs = sk_json_get(p, "tabs");
    for (size_t i = 0; i < len_of(tabs); i++)
        if (num(sk_json_at(tabs, i), "id") == id)
            return sk_json_as_string(sk_json_get(sk_json_at(tabs, i), "name"), out);
    return false;
}

/* Rename from tab one's drop-down: the name typed over the selected
 * one, ended as how says ("Enter", "Escape" or a press in the
 * terminal), and the name the probe then shows. */
static bool renamed(app *a, sk_arena *ar, double id, double x, const char *text, const char *how, const char *want)
{
    char req[160];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"button\":3,\"x\":%g,\"y\":34}", x);
    ok(a, req);
    const sk_json *p = probe(a, ar);
    bool popup = false;
    sk_json_as_bool(sk_json_dig(p, "rename", "popup", NULL), &popup);
    CHECK(popup);
    /* Rename, the drop-down's one item, 1 + 4 points below its top. */
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%g,\"y\":%g}", x + 20, 34.0 + 1 + 4 + 12);
    ok(a, req);
    p = probe(a, ar);
    CHECK_INT(num(sk_json_get(p, "rename"), "editing"), id);
    snprintf(req, sizeof req, "{\"op\":\"type\",\"text\":\"%s\"}", text);
    ok(a, req);
    if (!strcmp(how, "press")) {
        ok(a, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":400,\"y\":300}");
        ok(a, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":400,\"y\":300}");
    } else {
        snprintf(req, sizeof req, "{\"op\":\"key\",\"key\":\"%s\"}", how);
        ok(a, req);
    }
    sk_str name = {0};
    for (uint64_t until = sk_now_ms() + 3000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(a, ar);
        if (tab_name(p, id, &name) && sk_str_eq_c(name, want))
            return num(sk_json_get(p, "rename"), "editing") == 0;
    }
    sk_test_fail(__FILE__, __LINE__, "the name is \"%.*s\", not \"%s\"", (int)name.len, name.ptr ? name.ptr : "",
                 want);
    return false;
}

/* Decision 0032: a right click on a tab opens its drop-down, Rename
 * edits its name in place, Enter or a press elsewhere commits, Escape
 * cancels. */
static void test_a_tab_renamed(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    const sk_json *p = NULL;
    /* Both tabs in the strip, laid out: a loaded machine, as under the
     * sanitizers, takes a while. */
    for (uint64_t until = sk_now_ms() + 15000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        double w = 0;
        sk_json_as_number(sk_json_at(sk_json_at(sk_json_dig(p, "window", "tab_rects", NULL), 0), 1), &w);
        if (len_of(sk_json_dig(p, "window", "tabs", NULL)) == 2 && w > 0)
            break;
    }
    double id = -1, x0 = 0, w0 = 0;
    sk_json_as_number(sk_json_at(sk_json_dig(p, "window", "tabs", NULL), 0), &id);
    const sk_json *r = sk_json_dig(p, "window", "tab_rects", NULL);
    sk_json_as_number(sk_json_at(sk_json_at(r, 0), 0), &x0);
    sk_json_as_number(sk_json_at(sk_json_at(r, 0), 1), &w0);
    CHECK(id > 0 && w0 > 0);
    double x = x0 + w0 / 2;
    CHECK(renamed(&a, &ar, id, x, "work", "Enter", "work"));
    CHECK(renamed(&a, &ar, id, x, "zzz", "Escape", "work"));
    CHECK(renamed(&a, &ar, id, x, "elsewhere", "press", "elsewhere"));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* tabs.js holds the strip while a name is edited: the other tab's CLI
 * ends during the edit, and the field stays where it is until Enter. The
 * first tab's sh ends by itself; New tab runs a bare sh, which stays. */
static void test_the_strip_waits_for_a_rename(void)
{
    app a;
    const char *cmd[] = {"sh", "-c", "sleep 3", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    const sk_json *p = NULL;
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (len_of(sk_json_dig(p, "window", "tabs", NULL)) == 2)
            break;
    }
    double second = -1, x1 = 0, w1 = 0;
    sk_json_as_number(sk_json_at(sk_json_dig(p, "window", "tabs", NULL), 1), &second);
    const sk_json *r = sk_json_dig(p, "window", "tab_rects", NULL);
    sk_json_as_number(sk_json_at(sk_json_at(r, 1), 0), &x1);
    sk_json_as_number(sk_json_at(sk_json_at(r, 1), 1), &w1);
    char req[160];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"button\":3,\"x\":%g,\"y\":34}", x1 + w1 / 2);
    ok(&a, req);
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%g,\"y\":%g}", x1 + w1 / 2 + 20, 34.0 + 17);
    ok(&a, req);
    ok(&a, "{\"op\":\"type\",\"text\":\"kept\"}");
    /* The first tab ends; the strip and the field stay. */
    for (uint64_t until = sk_now_ms() + 8000; sk_now_ms() < until; sk_test_sleep_ms(100)) {
        p = probe(&a, &ar);
        if (len_of(sk_json_dig(p, "window", "tabs", NULL)) == 1)
            break;
    }
    CHECK_INT(len_of(sk_json_dig(p, "window", "tabs", NULL)), 1);
    bool strip = false;
    sk_json_as_bool(sk_json_dig(p, "window", "strip", NULL), &strip);
    CHECK(strip);
    CHECK_INT(num(sk_json_get(p, "rename"), "editing"), second);
    ok(&a, "{\"op\":\"key\",\"key\":\"Enter\"}");
    sk_str name = {0};
    for (uint64_t until = sk_now_ms() + 3000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (tab_name(p, second, &name) && sk_str_eq_c(name, "kept"))
            break;
    }
    CHECK_MEM(name, "kept");
    sk_json_as_bool(sk_json_dig(p, "window", "strip", NULL), &strip);
    CHECK(!strip);
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

static void click_box(app *a, const sk_json *box);

/* The skin (decisions 0008, 0022, 0037): deck by default; its own button
 * opens the drop-down below it and closes it; Skins switches to coffee,
 * which the probe and the next launch's record name. */
static void test_the_skin(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *p = probe(&a, &ar);
    sk_str name = {0};
    sk_json_as_string(sk_json_dig(p, "skin", "name", NULL), &name);
    CHECK_MEM(name, "deck");
    const sk_json *b = sk_json_dig(p, "skin", "button", NULL);
    CHECK(b && len_of(b) == 4);
    if (b) {
        click_box(&a, b);
        p = wait_menu(&a, &ar, "File", 5000);
        CHECK(p != NULL);
        /* Below the button, as menu.js puts it. */
        double by = -1, bh = -1;
        sk_json_as_number(sk_json_at(b, 1), &by);
        sk_json_as_number(sk_json_at(b, 3), &bh);
        CHECK(num(sk_json_get(p, "menu"), "y") >= by + bh);
        click_box(&a, sk_json_dig(probe(&a, &ar), "skin", "button", NULL));
        CHECK(wait_menu(&a, &ar, NULL, 5000) != NULL);
    }
    ok(&a, "{\"op\":\"menu\",\"show\":\"Skins\"}");
    p = wait_menu(&a, &ar, "Skins", 5000);
    const sk_json *row = p ? menu_row(p, "  coffee") : NULL;
    CHECK(row != NULL);
    if (row)
        press_row(&a, p, row, 20);
    for (uint64_t until = sk_now_ms() + 8000; sk_now_ms() < until; sk_test_sleep_ms(100)) {
        p = probe(&a, &ar);
        if (sk_json_as_string(sk_json_dig(p, "skin", "name", NULL), &name) && sk_str_eq_c(name, "coffee"))
            break;
    }
    CHECK_MEM(name, "coffee");
    sk_buf rec = {0};
    sk_buf_printf(&rec, "%s/state/claude-skins/last-skins", sk_test_tmpdir());
    sk_strv names = {0};
    bool recorded = false;
    if (sk_files_list(rec.ptr, &names))
        for (size_t i = 0; i < names.len; i++) {
            sk_buf f = {0};
            sk_buf_printf(&f, "%s/%s", rec.ptr, names.ptr[i]);
            char *text = sk_test_read(f.ptr, NULL);
            recorded |= text && strstr(text, "\"coffee\"");
            free(text);
            sk_buf_free(&f);
        }
    CHECK(recorded);
    sk_strv_free(&names);
    sk_buf_free(&rec);
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* A CLI that exits on its own with a status other than 0 while another
 * tab remains is a problem under Housekeeping (decision 0023); one the
 * user closed is none. */
static void test_a_failed_cli_is_a_problem(void)
{
    app a;
    const char *cmd[] = {"sh", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    ok(&a, "{\"op\":\"key\",\"key\":\"t\",\"mods\":" APP_MODS "}");
    const sk_json *p = NULL;
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        double pid = 0;
        sk_json_as_number(sk_json_get(sk_json_at(sk_json_get(p, "tabs"), 1), "pid"), &pid);
        if (len_of(sk_json_get(p, "tabs")) == 2 && pid > 0)
            break;
    }
    CHECK_INT(len_of(sk_json_get(p, "tabs")), 2);
    CHECK_INT(len_of(sk_json_get(p, "problems")), 0);
    /* The new tab, active, ends with 3. */
    sk_test_sleep_ms(300);
    ok(&a, "{\"op\":\"type\",\"text\":\"exit 3\\n\"}");
    sk_str title = {0};
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until && !title.len; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        sk_json_as_string(sk_json_at(sk_json_get(p, "problems"), 0), &title);
    }
    CHECK(sk_str_suffix(title, SK_STR(" exited with status 3")));
    CHECK_INT(len_of(sk_json_get(p, "tabs")), 1);
    CHECK_INT(len_of(sk_json_get(p, "problems")), 1);
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* An internal error (decision 0003): the user told, every CLI hung up by
 * the kill policy of decision 0016, one that ignores the hang-up killed
 * after the grace, and 125. */
static void test_an_internal_error_ends_the_clis(void)
{
    app a;
    const char *cmd[] = {"sh", "-c", "trap '' HUP; while :; do sleep 1; done", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    double pid = 0;
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until && pid <= 0; sk_test_sleep_ms(50))
        sk_json_as_number(sk_json_get(sk_json_at(sk_json_get(probe(&a, &ar), "tabs"), 0), "pid"), &pid);
    CHECK(pid > 0);
    /* Answered by no one: the app is gone. */
    (void)ask(&a, &ar, "{\"op\":\"die\"}");
    sk_arena_free(&ar);
    CHECK_INT(finish(&a, 6000), 125);
    /* Its group gone: the grandchildren, killed with it, are reaped by
     * the system a moment after. */
    bool gone = pid <= 0;
    for (uint64_t until = sk_now_ms() + 3000; !gone && sk_now_ms() < until; sk_test_sleep_ms(20))
        gone = kill(-(pid_t)pid, 0) != 0 && errno == ESRCH;
    CHECK(gone);
    sk_buf path = {0};
    sk_buf_printf(&path, "%s/skinned.err", sk_test_tmpdir());
    char *err = sk_test_read(path.ptr, NULL);
    if (!(err && strstr(err, "skinned: internal error: asked to by the control socket\n")))
        sk_test_fail(__FILE__, __LINE__, "the user was not told; stderr:\n%.2000s", err ? err : "(none)");
    free(err);
    sk_buf_free(&path);
}

/* Installed, skinned is also codex-skins and claude-skins, links that
 * make install makes (decision 0018): by such a name every argument
 * goes to that CLI, as `skinned -- codex ARGS` runs it. */
static void test_a_link_named_codex_skins_runs_codex(void)
{
    char *bin = sk_path_join(sk_test_tmpdir(), "fakebin"), *codex = sk_path_join(bin, "codex");
    CHECK_INT(sk_mkdirs(bin, 0700), 0);
    /* cat as the CLI, which waits on its terminal. */
    char *cat = sk_resolve_command("cat", getenv("PATH"));
    CHECK(cat && symlink(cat, codex) == 0);
    free(cat);
    app a;
    const char *args[] = {"-u", NULL};
    if (!start_as(&a, "codex-skins", args, bin)) {
        free(bin);
        free(codex);
        return;
    }
    sk_arena ar;
    sk_arena_init(&ar, 0);
    const sk_json *argv = sk_json_get(sk_json_at(sk_json_get(probe(&a, &ar), "tabs"), 0), "argv");
    sk_str s0 = {0}, s1 = {0};
    CHECK(len_of(argv) == 2 && sk_json_as_string(sk_json_at(argv, 0), &s0) && sk_str_eq_c(s0, "codex") &&
          sk_json_as_string(sk_json_at(argv, 1), &s1) && sk_str_eq_c(s1, "-u"));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
    free(bin);
    free(codex);
}

/* A Claude Code that refuses to resume a session running in the
 * background, as 2.1.296 does, ends by itself: the tab reads the
 * refusal off its last screen and opens the session with `claude
 * attach ID` in its place (decision 0030). */
static void test_a_refused_background_session_opens_with_attach(void)
{
    char *dir = sk_path_join(sk_test_tmpdir(), "fakebin"), *claude = sk_path_join(dir, "claude");
    CHECK_INT(sk_mkdirs(dir, 0700), 0);
    FILE *f = fopen(claude, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("#!/bin/sh\n"
              "for a in \"$@\"; do [ \"$a\" = attach ] && { echo attached; exec cat; }; done\n"
              "echo '\"Fix it\" is running in the background (5e3a91c0). Run `claude attach 5e3a91c0` to open it,"
              " or `claude stop 5e3a91c0` first'\n"
              "exit 1\n",
              f);
        fclose(f);
    }
    chmod(claude, 0755);
    app a;
    const char *cmd[] = {claude, "--resume", "abc", NULL};
    if (!start(&a, cmd)) {
        free(dir);
        free(claude);
        return;
    }
    sk_arena ar;
    sk_arena_init(&ar, 0);
    bool attached = false;
    for (uint64_t until = sk_now_ms() + 8000; sk_now_ms() < until && !attached; sk_test_sleep_ms(100)) {
        const sk_json *argv = sk_json_get(sk_json_at(sk_json_get(probe(&a, &ar), "tabs"), 0), "argv");
        sk_str s0 = {0}, s1 = {0}, s2 = {0};
        attached = len_of(argv) == 3 && sk_json_as_string(sk_json_at(argv, 0), &s0) &&
                   sk_str_eq_c(s0, claude) && sk_json_as_string(sk_json_at(argv, 1), &s1) &&
                   sk_str_eq_c(s1, "attach") && sk_json_as_string(sk_json_at(argv, 2), &s2) &&
                   sk_str_eq_c(s2, "5e3a91c0");
    }
    CHECK(attached);
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
    free(dir);
    free(claude);
}

/* Windows that recorded different skins within 5 s of each other: a
 * launch with no dialog to show takes the newest, forgets the records,
 * and records its own. */
static void test_raced_skins(void)
{
    sk_buf rec = {0};
    sk_buf_printf(&rec, "%s/state/claude-skins/last-skins", sk_test_tmpdir());
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double now = (double)ts.tv_sec * 1000 + (double)ts.tv_nsec / 1e6;
    CHECK_INT(sk_last_write(rec.ptr, 999991, "deck", now - 1500), 0);
    CHECK_INT(sk_last_write(rec.ptr, 999992, "coffee", now - 500), 0);
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd)) {
        sk_buf_free(&rec);
        return;
    }
    sk_arena ar;
    sk_arena_init(&ar, 0);
    sk_str name = {0};
    sk_json_as_string(sk_json_dig(probe(&a, &ar), "skin", "name", NULL), &name);
    CHECK_MEM(name, "coffee");
    sk_last_records r = {0};
    sk_last_read(rec.ptr, &r);
    CHECK_INT(r.len, 1);
    if (r.len) {
        CHECK_INT(r.ptr[0].pid, a.pid);
        CHECK_STR(r.ptr[0].spec, "coffee");
    }
    sk_last_records_free(&r);
    sk_arena_free(&ar);
    sk_buf_free(&rec);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

#ifdef __APPLE__
#define CMD_MODS "[\"super\"]"
#else
#define CMD_MODS "[\"ctrl\"]"
#endif

/* A press and release in the middle of a box [x, y, w, h] in points. */
static void click_box(app *a, const sk_json *box)
{
    double b[4] = {0};
    for (size_t i = 0; i < 4; i++)
        sk_json_as_number(sk_json_at(box, i), &b[i]);
    char req[128];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%g,\"y\":%g}", b[0] + b[2] / 2, b[1] + b[3] / 2);
    ok(a, req);
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":%g,\"y\":%g}", b[0] + b[2] / 2, b[1] + b[3] / 2);
    ok(a, req);
}

/* Files dropped on the window are pasted into the active tab, each path
 * quoted for a shell and a space after, by the paste that honours
 * bracketed paste mode (decision 0002). The CLI turns the mode on, says
 * so, then shows what reaches it, its controls made visible. */
static void test_files_dropped_are_pasted_quoted(void)
{
    app a;
    const char *cmd[] = {"sh", "-c", "printf '\\033[?2004hready\\n'; exec cat -v", NULL};
    if (!start(&a, cmd))
        return;
    CHECK(wait_screen(&a, "ready", 5000));
    ok(&a, "{\"op\":\"drop\",\"paths\":[\"/tmp/two words\",\"/srv/plain.txt\",\"/tmp/it's\"]}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Enter\"}");
    CHECK(wait_screen(&a, "^[[200~'/tmp/two words' /srv/plain.txt '/tmp/it'\\''s' ^[[201~", 5000));
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

/* New tab... (decision 0025) from File: it opens with the command line
 * in focus and takes the keys; a folder that is not there is refused in
 * the dialog, which stays open; Escape closes it and the terminal has
 * the keys again. Nothing is launched: cat is the command line. */
static void test_the_new_tab_chooser(void)
{
    app a;
    const char *cmd[] = {"cat", NULL};
    if (!start(&a, cmd))
        return;
    sk_arena ar;
    sk_arena_init(&ar, 0);
    ok(&a, "{\"op\":\"menu\",\"show\":\"File\",\"button\":[100,40,30,30]}");
    const sk_json *p = wait_menu(&a, &ar, "File", 5000);
    const sk_json *row = p ? menu_row(p, "New tab...") : NULL;
    CHECK(row != NULL);
    if (row)
        press_row(&a, p, row, 20);
    /* Open, and drawn: the boxes come with the first frame. */
    bool open = false;
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        sk_json_as_bool(sk_json_dig(p, "chooser", "open", NULL), &open);
        if (open && sk_json_dig(p, "chooser_boxes", "folder", NULL))
            break;
    }
    CHECK(open);
    sk_str focus = {0};
    sk_json_as_string(sk_json_dig(p, "chooser", "focus", NULL), &focus);
    CHECK_MEM(focus, "line");
    /* cat as the command line, a folder that is not there. */
    ok(&a, "{\"op\":\"key\",\"key\":\"a\",\"mods\":" CMD_MODS "}");
    ok(&a, "{\"op\":\"type\",\"text\":\"cat\"}");
    const sk_json *folder = sk_json_dig(p, "chooser_boxes", "folder", NULL);
    CHECK(folder != NULL);
    if (folder)
        click_box(&a, folder);
    ok(&a, "{\"op\":\"key\",\"key\":\"a\",\"mods\":" CMD_MODS "}");
    ok(&a, "{\"op\":\"type\",\"text\":\"/no/such/folder\"}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Enter\"}");
    sk_str err = {0};
    for (uint64_t until = sk_now_ms() + 5000; sk_now_ms() < until; sk_test_sleep_ms(50)) {
        p = probe(&a, &ar);
        if (sk_json_as_string(sk_json_dig(p, "chooser", "error", NULL), &err) && err.len)
            break;
    }
    CHECK(err.ptr && strstr(err.ptr, "no such folder: /no/such/folder"));
    CHECK_INT(len_of(sk_json_get(p, "tabs")), 1);
    /* A file dropped meanwhile is not pasted into the terminal. */
    ok(&a, "{\"op\":\"drop\",\"paths\":[\"/while/open\"]}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Escape\"}");
    p = probe(&a, &ar);
    sk_json_as_bool(sk_json_dig(p, "chooser", "open", NULL), &open);
    CHECK(!open);
    ok(&a, "{\"op\":\"type\",\"text\":\"after\"}");
    ok(&a, "{\"op\":\"key\",\"key\":\"Enter\"}");
    CHECK(wait_screen(&a, "after\nafter", 5000));
    CHECK(!screen_has(probe(&a, &ar), "/while/open"));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    CHECK_INT(finish(&a, 5000), 128 + SIGHUP);
}

const sk_test sk_suite_window[] = {
    T(test_typing_reaches_the_tab),
    T(test_the_tab_chords),
    T(test_the_app_ends_with_the_last_tabs_status),
    T(test_a_double_click_selects_a_word),
    T(test_a_url_in_the_text_is_a_link),
    T(test_a_tab_dragged_out_gets_its_own_window),
    T(test_closing_the_window_hangs_up),
    T(test_a_tab_dragged_along_the_strip),
    T(test_the_menus),
    T(test_the_sessions_dashboard),
    T(test_files_dropped_are_pasted_quoted),
    T(test_the_new_tab_chooser),
    T(test_a_tab_renamed),
    T(test_the_strip_waits_for_a_rename),
    T(test_the_skin),
    T(test_raced_skins),
    T(test_a_refused_background_session_opens_with_attach),
    T(test_a_failed_cli_is_a_problem),
    T(test_a_link_named_codex_skins_runs_codex),
    T(test_an_internal_error_ends_the_clis),
    {0},
};
