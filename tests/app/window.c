/* skinned's window, driven through its control socket: the live checks
 * of scripts/checks/tabs.js (decision 0023) in C, as decision 0036
 * plans. The real program runs with its window hidden; the checks type,
 * press the app's chords, click and read the probe. They need a display:
 * the window server on macOS, $DISPLAY on Linux (Xvfb in CI); without
 * one they print a skip line. */
#include "../../src/base/arena.h"
#include "../../src/base/json.h"
#include "../../src/base/strv.h"
#include "../../src/core/loop.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
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

/* The app on cmd in the test's directory, its control socket there. */
static bool start(app *a, const char *const *cmd)
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
    sk_strv_push(&argv, whole);
    sk_strv_push(&argv, "--");
    for (; *cmd; cmd++)
        sk_strv_push(&argv, *cmd);
    for (char **e = environ; *e; e++)
        if (strncmp(*e, "XDG_", 4) && strncmp(*e, "SKINNED_", 8))
            sk_strv_push(&env, *e);
    sk_strv_pushf(&env, "XDG_STATE_HOME=%s/state", tmp);
    sk_strv_pushf(&env, "XDG_RUNTIME_DIR=%s/run", tmp);
    sk_strv_pushf(&env, "SKINNED_CONTROL=%s", sock.ptr);
    sk_strv_push(&env, "SKINNED_HIDDEN=1");
    a->pid = fork();
    if (a->pid == 0) {
        int null = open("/dev/null", O_RDWR);
        if (null < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 || dup2(null, 2) < 0 || chdir(tmp) != 0)
            _exit(98);
        execve(whole, sk_strv_argv(&argv), sk_strv_argv(&env));
        _exit(99);
    }
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
    double w = num(sk_json_get(p, "window"), "w"), cols = num(sk_json_get(p, "window"), "cols");
    double cell = cols > 0 ? w / cols : 8;
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%.1f,\"y\":5,\"button\":1,\"clicks\":2}",
             cell * 7.5);
    ok(&a, req);
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":%.1f,\"y\":5,\"button\":1}", cell * 7.5);
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
    double w = num(sk_json_get(p, "window"), "w"), cols = num(sk_json_get(p, "window"), "cols");
    double cell = cols > 0 ? w / cols : 8;
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%.1f,\"y\":5}", cell * 10.5);
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
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%.1f,\"y\":5}", cell * 1.5);
    ok(&a, req);
    p = probe(&a, &ar);
    CHECK(!sk_json_as_string(sk_json_get(p, "link"), &link));
    sk_arena_free(&ar);
    ok(&a, "{\"op\":\"hangup\"}");
    finish(&a, 5000);
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

const sk_test sk_suite_window[] = {
    T(test_typing_reaches_the_tab),
    T(test_the_tab_chords),
    T(test_the_app_ends_with_the_last_tabs_status),
    T(test_a_double_click_selects_a_word),
    T(test_a_url_in_the_text_is_a_link),
    T(test_closing_the_window_hangs_up),
    {0},
};
