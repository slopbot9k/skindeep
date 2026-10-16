/* skinned's window (phase 4 of decision 0036): the tabs' terminals in a
 * window, with the plain tab strip of decision 0023 until the skins of
 * phase 6 draw it. The core thread runs the tabs, their ptys and their
 * agents, and parses each tab's output into its terminal as it comes;
 * the UI thread, the main one, draws the active tab and turns keys,
 * clicks and the wheel into what the tab's CLI reads. */
#ifndef SK_APP_GUI_H
#define SK_APP_GUI_H

#include "../core/env.h"
#include "options.h"

/* self: argv[0], to find skinned-relay beside it. Returns the exit
 * status of decision 0003. */
int sk_gui_run(const sk_options *o, const sk_envp *env, const char *self);

#endif
