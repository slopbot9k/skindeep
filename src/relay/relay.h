/* The relays Claude Code runs (decisions 0009, 0010, 0030), in place of
 * app/relay/hook.py and app/relay/statusline.js. Each finds the window
 * whose tab started its process tree and posts its stdin there. The
 * hook relay always exits 0 and the statusline relay prints only the
 * profile's own statusline, so neither ever changes the session.
 * Callers ignore SIGPIPE, so a peer that went away is an error, not a
 * death. */
#ifndef SK_RELAY_H
#define SK_RELAY_H

#include "../base/base.h"

#include <sys/types.h>

#define SK_RELAY_MAX_DEPTH 64
#define SK_RELAY_HOOK_MS 500       /* hook.py's socket timeout */
#define SK_RELAY_STATUSLINE_MS 500 /* statusline.js's hard limit */

/* The socket of the nearest process, from `from` up, at most 64
 * generations, with a live link in root/pids. A link whose socket went
 * with its window does not count, and the search goes on above it.
 * Returns a malloc'd path, or NULL. */
char *sk_relay_window(const char *root, pid_t from);

/* POSTs body to path over the Unix socket sock and waits for the answer,
 * all within timeout_ms. 0 when an answer came, else -1. */
int sk_relay_post(const char *sock, const char *path, const char *body, size_t n, int timeout_ms);

/* The hook relay: posts stdin to /hook when it is 1 byte to 64 KiB.
 * Always returns 0. */
int sk_relay_hook(const char *root, int in_fd, pid_t from);

/* The statusline relay: runs chain, the profile's statusline command in
 * base64url or "-" for none, through /bin/sh with stdin and its output
 * on out_fd, while it posts stdin to /statusline. Returns the command's
 * exit status, 128 when a signal ended it (as statusline.js did), 1 when
 * it could not start, 0 with no command. A SIGTERM during the command
 * passes it on and exits 143. */
int sk_relay_statusline(const char *root, const char *chain, int in_fd, int out_fd, pid_t from);

#endif
