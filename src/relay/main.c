/* skinned-relay: the command Claude Code runs for the app's hooks and
 * statusline (decisions 0009, 0010, 0030, 0036).
 *
 *   skinned-relay hook ROOT
 *   skinned-relay statusline ROOT CHAIN
 *
 * ROOT is the app's runtime root; CHAIN the profile's own statusline
 * command in base64url, or - for none. */
#include "relay.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    if (argc >= 2 && !strcmp(argv[1], "hook"))
        return sk_relay_hook(argc > 2 ? argv[2] : NULL, 0, getppid());
    if (argc >= 2 && !strcmp(argv[1], "statusline"))
        return sk_relay_statusline(argc > 2 ? argv[2] : NULL, argc > 3 ? argv[3] : NULL, 0, 1,
                                   getppid());
    fputs("usage: skinned-relay hook ROOT\n"
          "       skinned-relay statusline ROOT CHAIN\n",
          stderr);
    return 2;
}
