/* skinned --headless (phase 2 of decision 0036): the tabs' model and a
 * CLI tab's core with no window. The first tab runs the command on a pty
 * through its adapter and the relays, while the terminal skinned runs in
 * stands in for the window: raw mode, keys to the active tab's pty, its
 * output to stdout, the window size followed. Once a second the active
 * tab's bus and adapter report go out as one JSON line to the metrics
 * file, the meters a skin would show. */
#ifndef SK_APP_HEADLESS_H
#define SK_APP_HEADLESS_H

#include "../core/env.h"
#include "options.h"

/* metrics: the file for the JSON lines, NULL for
 * <state>/headless-<pid>.jsonl. self: argv[0], to find skinned-relay
 * beside it. Returns the exit status of decision 0003: the command's, or
 * 125 or 127. */
int sk_headless_run(const sk_options *o, const char *metrics, const sk_envp *env, const char *self);

#endif
