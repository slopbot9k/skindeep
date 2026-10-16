/* The relays against a socket server in the test process, standing in for
 * a window, as hookdump does. */
#include "../../src/base/base64.h"
#include "../../src/base/http.h"
#include "../../src/core/loop.h"
#include "../../src/platform/proc.h"
#include "../../src/relay/relay.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char sock[512];
    int fd;
    bool silent;  /* takes the request, never answers */
    int wait_ms;  /* how long to wait for a connection */
    int got;      /* requests taken */
    sk_buf route, body;
    pthread_t thread;
} server;

static void *serve(void *ud)
{
    server *s = ud;
    struct pollfd p = {s->fd, POLLIN, 0};
    if (poll(&p, 1, s->wait_ms) <= 0)
        return NULL;
    int c = accept(s->fd, NULL, NULL);
    if (c < 0)
        return NULL;
    sk_http_req q;
    sk_http_req_init(&q, 0);
    char buf[4096];
    while (q.state == SK_HTTP_HEAD || q.state == SK_HTTP_BODY) {
        ssize_t n = read(c, buf, sizeof buf);
        if (n <= 0)
            break;
        sk_http_req_feed(&q, buf, (size_t)n);
    }
    if (q.state == SK_HTTP_DONE) {
        s->got++;
        sk_buf_append_str(&s->route, q.target);
        sk_buf_append_str(&s->body, sk_buf_str(&q.body));
    }
    if (s->silent) {
        sk_test_sleep_ms(800);
    } else {
        sk_buf a = {0};
        sk_http_write_204(&a);
        ssize_t w = write(c, a.ptr, a.len);
        (void)w;
        sk_buf_free(&a);
    }
    close(c);
    sk_http_req_free(&q);
    return NULL;
}

/* A window at root/hooks.sock, linked from root/pids/<pid>. */
static void server_start(server *s, const char *root, pid_t pid, int wait_ms, bool silent)
{
    memset(s, 0, sizeof *s);
    s->wait_ms = wait_ms;
    s->silent = silent;
    snprintf(s->sock, sizeof s->sock, "%s/hooks.sock", root);
    char pids[600], link[700];
    snprintf(pids, sizeof pids, "%s/pids", root);
    snprintf(link, sizeof link, "%s/%ld", pids, (long)pid);
    mkdir(pids, 0700);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    CHECK(strlen(s->sock) < sizeof addr.sun_path);
    memcpy(addr.sun_path, s->sock, strlen(s->sock));
    s->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(s->fd >= 0);
    CHECK(bind(s->fd, (struct sockaddr *)&addr, sizeof addr) == 0);
    CHECK(listen(s->fd, 4) == 0);
    CHECK(symlink(s->sock, link) == 0);
    pthread_create(&s->thread, NULL, serve, s);
}

static void server_stop(server *s)
{
    pthread_join(s->thread, NULL);
    close(s->fd);
    unlink(s->sock);
}

static void server_free(server *s)
{
    sk_buf_free(&s->route);
    sk_buf_free(&s->body);
}

/* A descriptor reading text from a file, as stdin would. */
static int input(const char *dir, const char *name, const char *text, size_t n)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f)
        return -1;
    fwrite(text, 1, n, f);
    fclose(f);
    return open(path, O_RDONLY);
}

static void test_proc_read_self(void)
{
    sk_proc_stat st;
    CHECK(sk_proc_read(getpid(), &st));
    CHECK_INT(st.ppid, getppid());
    CHECK(st.start > 0);
    sk_proc_stat again;
    CHECK(sk_proc_read(getpid(), &again) && again.start == st.start);
    pid_t child = fork();
    if (child == 0)
        _exit(0);
    waitpid(child, NULL, 0);
    CHECK(!sk_proc_read(child, &st)); /* gone */
}

static void test_window_by_ancestry(void)
{
    const char *root = sk_test_tmpdir();
    char pids[600], target[600], mine[700], parents[700];
    snprintf(pids, sizeof pids, "%s/pids", root);
    snprintf(target, sizeof target, "%s/live.sock", root);
    snprintf(mine, sizeof mine, "%s/%ld", pids, (long)getpid());
    snprintf(parents, sizeof parents, "%s/%ld", pids, (long)getppid());
    CHECK(sk_relay_window(root, getpid()) == NULL);
    CHECK(sk_relay_window(NULL, getpid()) == NULL);
    mkdir(pids, 0700);
    fclose(fopen(target, "w"));
    CHECK(symlink(target, parents) == 0);
    /* This process's own link is dead: the search goes on to the parent. */
    CHECK(symlink("/nonexistent/socket", mine) == 0);
    char *found = sk_relay_window(root, getpid());
    CHECK_STR(found, parents);
    free(found);
}

static void test_post_round_trip(void)
{
    server s;
    server_start(&s, sk_test_tmpdir(), getpid(), 2000, false);
    CHECK_INT(sk_relay_post(s.sock, "/hook", "{\"a\":1}", 7, 1000), 0);
    server_stop(&s);
    CHECK_INT(s.got, 1);
    CHECK_STR(sk_buf_cstr(&s.route), "/hook");
    CHECK_STR(sk_buf_cstr(&s.body), "{\"a\":1}");
    server_free(&s);
}

static void test_post_gives_up_in_time(void)
{
    server s;
    server_start(&s, sk_test_tmpdir(), getpid(), 2000, true);
    uint64_t start = sk_now_ms();
    CHECK_INT(sk_relay_post(s.sock, "/hook", "{}", 2, 100), -1);
    CHECK(sk_now_ms() - start < 500);
    server_stop(&s);
    server_free(&s);
    char missing[600];
    snprintf(missing, sizeof missing, "%s/none.sock", sk_test_tmpdir());
    CHECK_INT(sk_relay_post(missing, "/hook", "{}", 2, 100), -1);
}

static void test_hook_relay(void)
{
    const char *root = sk_test_tmpdir();
    server s;
    server_start(&s, root, getpid(), 2000, false);
    int in = input(root, "event.json", "{\"hook_event_name\":\"Stop\"}", 26);
    CHECK_INT(sk_relay_hook(root, in, getpid()), 0);
    close(in);
    server_stop(&s);
    CHECK_STR(sk_buf_cstr(&s.route), "/hook");
    CHECK_STR(sk_buf_cstr(&s.body), "{\"hook_event_name\":\"Stop\"}");
    server_free(&s);
}

static void test_hook_relay_skips_what_it_must(void)
{
    const char *root = sk_test_tmpdir();
    server s;
    server_start(&s, root, getpid(), 300, false);
    sk_buf big = {0};
    while (big.len <= SK_HTTP_MAX_BODY)
        sk_buf_append_c(&big, "xxxxxxxxxxxxxxxx");
    int in = input(root, "big.json", big.ptr, big.len);
    CHECK_INT(sk_relay_hook(root, in, getpid()), 0); /* over 64 KiB: nothing sent */
    close(in);
    in = input(root, "empty.json", "", 0);
    CHECK_INT(sk_relay_hook(root, in, getpid()), 0); /* empty: nothing sent */
    close(in);
    server_stop(&s);
    CHECK_INT(s.got, 0);
    server_free(&s);
    sk_buf_free(&big);
}

static char *chain_of(const char *cmd)
{
    sk_buf b = {0};
    sk_base64url_encode(&b, cmd, strlen(cmd));
    return b.ptr;
}

static void test_statusline_relay(void)
{
    const char *root = sk_test_tmpdir();
    server s;
    server_start(&s, root, getpid(), 2000, false);
    const char *body = "{\"model\":{\"display_name\":\"Opus\"}}";
    int in = input(root, "status.json", body, strlen(body));
    char out_path[600];
    snprintf(out_path, sizeof out_path, "%s/out", root);
    int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    char *chain = chain_of("cat; printf ' and more'; exit 3");
    CHECK_INT(sk_relay_statusline(root, chain, in, out, getpid()), 3);
    close(in);
    close(out);
    server_stop(&s);
    char *printed = sk_test_read(out_path, NULL);
    CHECK_STR(printed, "{\"model\":{\"display_name\":\"Opus\"}} and more");
    CHECK_STR(sk_buf_cstr(&s.route), "/statusline");
    CHECK_STR(sk_buf_cstr(&s.body), body);
    free(printed);
    free(chain);
    server_free(&s);
}

static void test_statusline_without_command_or_window(void)
{
    const char *root = sk_test_tmpdir();
    char out_path[600];
    snprintf(out_path, sizeof out_path, "%s/out", root);
    int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int in = input(root, "a.json", "{}", 2);
    /* No window anywhere above: the command still runs. */
    char *chain = chain_of("printf ok");
    CHECK_INT(sk_relay_statusline(root, chain, in, out, getpid()), 0);
    close(in);
    /* No command: nothing printed, status 0. */
    in = input(root, "b.json", "{}", 2);
    CHECK_INT(sk_relay_statusline(root, "-", in, out, getpid()), 0);
    close(in);
    in = input(root, "c.json", "{}", 2);
    CHECK_INT(sk_relay_statusline(root, "not*base64", in, out, getpid()), 0);
    close(in);
    close(out);
    char *printed = sk_test_read(out_path, NULL);
    CHECK_STR(printed, "ok");
    free(printed);
    free(chain);
}

static void test_statusline_signal_and_unread_stdin(void)
{
    const char *root = sk_test_tmpdir();
    int out = open("/dev/null", O_WRONLY);
    /* A command killed by a signal reports 128, as statusline.js did. */
    char *chain = chain_of("kill -KILL $$");
    int in = input(root, "a.json", "{}", 2);
    CHECK_INT(sk_relay_statusline(root, chain, in, out, getpid()), 128);
    close(in);
    free(chain);
    /* A command that never reads a large stdin still ends the relay. */
    sk_buf big = {0};
    while (big.len < 512 * 1024)
        sk_buf_append_c(&big, "{\"pad\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}\n");
    in = input(root, "big.json", big.ptr, big.len);
    chain = chain_of("exit 4");
    uint64_t start = sk_now_ms();
    CHECK_INT(sk_relay_statusline(root, chain, in, out, getpid()), 4);
    CHECK(sk_now_ms() - start < 2000);
    close(in);
    close(out);
    free(chain);
    sk_buf_free(&big);
}

/* The built program, run as Claude Code runs it: its parent, this test,
 * holds the link. */
static void test_program_end_to_end(void)
{
    const char *bin = getenv("SK_RELAY_BIN");
    if (!bin || access(bin, X_OK) != 0) {
        printf("    (skipped: SK_RELAY_BIN not set)\n");
        return;
    }
    const char *root = sk_test_tmpdir();
    server s;
    server_start(&s, root, getpid(), 3000, false);
    int in = input(root, "status.json", "{\"n\":1}", 7);
    int out[2];
    CHECK(pipe(out) == 0);
    char *chain = chain_of("printf 'from the profile'");
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in, 0);
        dup2(out[1], 1);
        execl(bin, bin, "statusline", root, chain, (char *)NULL);
        _exit(99);
    }
    close(out[1]);
    char printed[128] = {0};
    size_t got = 0;
    ssize_t n;
    while (got < sizeof printed - 1 && (n = read(out[0], printed + got, sizeof printed - 1 - got)) > 0)
        got += (size_t)n;
    int ws = 0;
    waitpid(pid, &ws, 0);
    close(out[0]);
    close(in);
    server_stop(&s);
    CHECK(WIFEXITED(ws) && WEXITSTATUS(ws) == 0);
    CHECK_STR(printed, "from the profile");
    CHECK_STR(sk_buf_cstr(&s.body), "{\"n\":1}");
    free(chain);
    server_free(&s);
}

const sk_test sk_suite_relay[] = {
    T(test_proc_read_self),
    T(test_window_by_ancestry),
    T(test_post_round_trip),
    T(test_post_gives_up_in_time),
    T(test_hook_relay),
    T(test_hook_relay_skips_what_it_must),
    T(test_statusline_relay),
    T(test_statusline_without_command_or_window),
    T(test_statusline_signal_and_unread_stdin),
    T(test_program_end_to_end),
    {0},
};
