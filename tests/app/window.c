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
    /* DejaVu Sans Mono at 14 px: 8 points a cell, the grid 4 points in
     * below the 22-point identity strip, row 0 centred at y 34. */
    (void)w, (void)cols;
    double cell = 8;
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"down\",\"x\":%.1f,\"y\":34,\"button\":1,\"clicks\":2}",
             4 + cell * 7.5);
    ok(&a, req);
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"up\",\"x\":%.1f,\"y\":34,\"button\":1}", 4 + cell * 7.5);
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
    /* DejaVu Sans Mono at 14 px: 8 points a cell, the grid 4 points in
     * below the 22-point identity strip, row 0 centred at y 34. */
    (void)w, (void)cols;
    double cell = 8;
    char req[256];
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%.1f,\"y\":34}", 4 + cell * 10.5);
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
    snprintf(req, sizeof req, "{\"op\":\"mouse\",\"kind\":\"move\",\"x\":%.1f,\"y\":34}", 4 + cell * 1.5);
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

const sk_test sk_suite_window[] = {
    T(test_typing_reaches_the_tab),
    T(test_the_tab_chords),
    T(test_the_app_ends_with_the_last_tabs_status),
    T(test_a_double_click_selects_a_word),
    T(test_a_url_in_the_text_is_a_link),
    T(test_a_tab_dragged_out_gets_its_own_window),
    T(test_closing_the_window_hangs_up),
    T(test_the_menus),
    {0},
};
