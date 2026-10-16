/* The app's own command line and its messages to the user (decision
 * 0003), as app/cli.js and bin/skinned have them:
 *
 *   skinned [--skin NAME|PATH] [--statusline=always|chain-only]
 *           [--no-viz] [--mute] -- CMD [ARGS...]
 *
 * SKINNED_SKIN names the skin when --skin does not, and SKINNED_MUTE=1
 * starts with the skin's sounds muted (decision 0024). A refused command
 * line is told to the user and the app exits 125, the status of an app
 * error (decision 0003). */
#ifndef SK_APP_OPTIONS_H
#define SK_APP_OPTIONS_H

#include "../agents/agent.h"
#include "../core/env.h"

#define SK_USAGE                                                             \
    "usage: skinned [--skin NAME|PATH] [--statusline=always|chain-only] "    \
    "[--no-viz] [--mute] -- CMD [ARGS...]\n"                                 \
    "  SKINNED_SKIN=NAME sets the default skin, SKINNED_MUTE=1 starts with " \
    "skin sounds muted"

typedef struct {
    const char *skin;               /* NULL: none given */
    sk_statusline_mode statusline;  /* decision 0020 */
    bool viz, mute;
    char *const *cmd;               /* after --, NULL-terminated, never empty */
} sk_options;

/* argv: the app's own, program first, NULL-terminated; env: its
 * environment. The strings stay argv's and env's. True, or false with
 * the message for the user in err: an option of ours unknown, as
 * bin/skinned refuses it, or no command after --. */
bool sk_options_parse(sk_options *o, char *const *argv, const sk_envp *env, sk_buf *err);

/* fd is the caller's stderr, which the launcher keeps on fd 3 while the
 * app's own stderr goes to its log. It is reopened close-on-exec and fd
 * closed, so no CLI child inherits it, and sk_tell writes there. When fd
 * is not open, sk_tell keeps writing to 2. Returns the descriptor
 * sk_tell now writes to. */
int sk_tell_adopt(int fd);
/* "skinned: <msg>\n" to the user, in one write. */
void sk_tell(const char *fmt, ...) SK_PRINTF(1, 2);

#endif
