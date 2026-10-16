/* hookdump: stands in for a window, so the relays can be tried before the
 * app exists (phase 1 of decision 0036).
 *
 *   hookdump ROOT [PID]
 *
 * It listens on ROOT/hookdump.sock, links ROOT/pids/PID to it, and prints
 * every post it gets: the route and size on one line, then the body.
 * PID defaults to that of the shell that started it. A Claude Code started
 * from that shell, with settings whose hooks run `skinned-relay hook ROOT`
 * and whose statusLine runs `skinned-relay statusline ROOT -`, reports
 * here. Ctrl+C removes the link and the socket. */
#include "../src/base/http.h"
#include "../src/base/paths.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static char sock_path[1024], link_path[1024];

static void on_stop(int signo)
{
    (void)signo;
    unlink(link_path);
    unlink(sock_path);
    _exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fputs("usage: hookdump ROOT [PID]\n", stderr);
        return 2;
    }
    const char *root = argv[1];
    long pid = argc == 3 ? atol(argv[2]) : (long)getppid();
    snprintf(sock_path, sizeof sock_path, "%s/hookdump.sock", root);
    snprintf(link_path, sizeof link_path, "%s/pids/%ld", root, pid);
    char pids[1024];
    snprintf(pids, sizeof pids, "%s/pids", root);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (sk_mkdirs(pids, 0700) != 0 || strlen(sock_path) >= sizeof addr.sun_path) {
        fprintf(stderr, "hookdump: cannot use %s\n", root);
        return 1;
    }
    memcpy(addr.sun_path, sock_path, strlen(sock_path));
    unlink(sock_path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 16) != 0) {
        fprintf(stderr, "hookdump: %s: %s\n", sock_path, strerror(errno));
        return 1;
    }
    unlink(link_path);
    if (symlink(sock_path, link_path) != 0) {
        fprintf(stderr, "hookdump: %s: %s\n", link_path, strerror(errno));
        unlink(sock_path);
        return 1;
    }
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    signal(SIGPIPE, SIG_IGN);
    fprintf(stderr, "hookdump: %s -> %s\n", link_path, sock_path);
    for (;;) {
        int c = accept(fd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        sk_http_req q;
        sk_http_req_init(&q, 0);
        char buf[4096];
        while (q.state == SK_HTTP_HEAD || q.state == SK_HTTP_BODY) {
            ssize_t n = read(c, buf, sizeof buf);
            if (n <= 0)
                break;
            sk_http_req_feed(&q, buf, (size_t)n);
        }
        sk_buf answer = {0};
        sk_http_write_204(&answer);
        ssize_t w = write(c, answer.ptr, answer.len);
        (void)w;
        close(c);
        if (q.state == SK_HTTP_DONE)
            printf("%.*s %.*s (%zu bytes%s)\n%.*s\n", (int)q.method.len, q.method.ptr,
                   (int)q.target.len, q.target.ptr, (size_t)q.content_length,
                   q.oversize ? ", over the cap" : "", (int)q.body.len, sk_buf_cstr(&q.body));
        else
            printf("(a request that did not parse: %s)\n", q.error ? q.error : "cut short");
        fflush(stdout);
        sk_buf_free(&answer);
        sk_http_req_free(&q);
    }
    on_stop(0);
    return 0;
}
