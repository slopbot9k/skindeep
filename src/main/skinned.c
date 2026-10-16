/* skinned: the app (decision 0036): CMD's tabs in a window, or with
 * --headless in the terminal it runs in.
 *
 *   skinned [--statusline=always|chain-only] -- CMD [ARGS...]
 *   skinned --headless[=FILE] [--record=FILE] [...] -- CMD [ARGS...]
 *
 * --record keeps the first tab's pty output for the terminal's tests.
 * The other options of bin/skinned are taken and do nothing yet. */
#include "../app/gui.h"
#include "../app/headless.h"
#include "../app/identity.h"
#include "../core/command.h"

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

int main(int argc, char **argv)
{
    (void)argc;
    hold_stdio();
    signal(SIGPIPE, SIG_IGN);
    sk_tell_adopt(3);
    sk_identity_set_argv_source(sk_identity_kernel_argv);
    /* --headless is this build's own option, taken before the parser
     * that knows bin/skinned's options sees the rest. */
    sk_strv own = {0};
    sk_headless_opts ho = {0};
    bool headless = false;
    size_t i = 0;
    for (; argv[i]; i++) {
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
        status = sk_headless_run(&o, &ho, &env, argv[0]);
    } else {
        status = sk_gui_run(&o, &env, argv[0]);
    }
    sk_buf_free(&err);
    sk_envp_free(&env);
    sk_strv_free(&own);
    return status;
}
