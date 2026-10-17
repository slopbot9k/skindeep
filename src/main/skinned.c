/* skinned: the app (decision 0036): CMD's tabs in a window, or with
 * --headless in the terminal it runs in.
 *
 *   skinned [--skin NAME|PATH] [--statusline=always|chain-only] [--no-viz]
 *           [--mute] -- CMD [ARGS...]
 *   skinned --headless[=FILE] [--record=FILE] [...] -- CMD [ARGS...]
 *   claude-skins [ARGS...], codex-skins [ARGS...]: links to skinned
 *
 * --record keeps the first tab's pty output for the terminal's tests. */
#include "../app/gui.h"
#include "../app/headless.h"
#include "../app/identity.h"
#include "../base/log.h"
#include "../core/command.h"
#include "../core/session.h"

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

/* fds 0 to 2 open, on /dev/null when the app was started without them:
 * else a pipe or file the app opens takes their number, and a child's
 * dup2 onto its stdio goes wrong. */
static void hold_stdio(void)
{
    int fd;
    while ((fd = open("/dev/null", O_RDWR)) >= 0 && fd <= 2)
        ;
    if (fd > 2)
        close(fd);
}

/* An internal error (decision 0003): the user told, every CLI hung up
 * by the kill policy of decision 0016, then 125. sk_die can come from
 * any thread, out of memory: nothing here allocates. */
static void internal_error(const char *msg)
{
    sk_log_now("internal error: ", msg);
    sk_tell_now("internal error: ", msg);
    sk_session_hangup_every(SK_SESSION_GRACE_MS);
    _exit(SK_EXIT_APP);
}

/* Installed, the program is also claude-skins and codex-skins, links to
 * skinned (decision 0018): by such a name every argument goes to that
 * CLI, as bin/'s wrappers ran it. */
static const char *cli_of(const char *argv0)
{
    const char *base = strrchr(argv0, '/');
    base = base ? base + 1 : argv0;
    if (!strcmp(base, "claude-skins"))
        return "claude";
    if (!strcmp(base, "codex-skins"))
        return "codex";
    return NULL;
}

/* argv[0] as a path, as given when it holds a slash, else the file PATH
 * finds by its name, as the shell found it: the program finds its relay
 * and the checkout's skins/ from where it lies. malloc'd. */
static char *self_of(const char *argv0)
{
    char *found = strchr(argv0, '/') ? NULL : sk_resolve_command(argv0, getenv("PATH"));
    return found ? found : sk_xstrdup(argv0);
}

int main(int argc, char **argv)
{
    (void)argc;
    hold_stdio();
    signal(SIGPIPE, SIG_IGN);
    sk_tell_adopt(3);
    sk_die_hook(internal_error);
    sk_identity_set_argv_source(sk_identity_kernel_argv);
    char *self = self_of(argv[0]);
    const char *cli = cli_of(argv[0]);
    /* --headless is this build's own option, taken before the parser
     * that knows the launcher's options sees the rest. */
    sk_strv own = {0};
    sk_headless_opts ho = {0};
    bool headless = false;
    size_t i = 0;
    if (cli) {
        sk_strv_push(&own, argv[0]);
        sk_strv_push(&own, "--");
        sk_strv_push(&own, cli);
        i = 1;
    }
    for (; argv[i] && !cli; i++) {
        if (i > 0 && !strcmp(argv[i], "--"))
            break;
        if (i > 0 && !strcmp(argv[i], "--headless")) {
            headless = true;
            continue;
        }
        if (i > 0 && !strncmp(argv[i], "--headless=", 11)) {
            headless = true;
            ho.metrics = argv[i] + 11;
            continue;
        }
        if (i > 0 && !strncmp(argv[i], "--record=", 9)) {
            ho.record = argv[i] + 9;
            continue;
        }
        sk_strv_push(&own, argv[i]);
    }
    for (; argv[i]; i++)
        sk_strv_push(&own, argv[i]);

    sk_envp env;
    sk_envp_from(&env, environ);
    sk_options o;
    sk_buf err = {0};
    int status;
    if (!sk_options_parse(&o, sk_strv_argv(&own), &env, &err)) {
        sk_tell("%s", sk_buf_cstr(&err));
        status = SK_EXIT_APP;
    } else if (headless) {
        status = sk_headless_run(&o, &ho, &env, self);
    } else {
        status = sk_gui_run(&o, &env, self);
    }
    sk_buf_free(&err);
    sk_envp_free(&env);
    free(self);
    sk_strv_free(&own);
    return status;
}
