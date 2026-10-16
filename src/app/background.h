/* Claude Code background sessions seen from a tab (decision 0030), as
 * app/background.js sees them. Picked in the --resume picker, a session
 * that runs in the background opens in place: Claude Code replaces
 * itself with `claude attach ID`. It refuses that while its command line
 * carries session settings, and the app's injected --settings is one,
 * so it prints how to open the session and exits. When the user's own
 * arguments carried none, a bare CLI would have opened it, and the app
 * runs `claude attach ID` in the tab's place. When they carried some,
 * the refusal is the CLI's own and stands. */
#ifndef SK_APP_BACKGROUND_H
#define SK_APP_BACKGROUND_H

#include "../base/strv.h"

/* The refusal is looked for in the last 8 rows that are not blank: the
 * CLI prints it last. */
#define SK_BACKGROUND_TAIL_ROWS 8

/* The job id of the refusal at the end of the screen's rows, 8 hex
 * digits, into id with a NUL. False when there is none. Claude Code
 * 2.1.296 prints it, wrapped at the terminal's width, as `"TITLE" is
 * running in the background (ID). Run \`claude attach ID\` to open it,
 * or ...`. */
bool sk_background_refusal_rows(const char *const *rows, size_t n, char id[9]);
/* The same for a screen as text, rows ended by \n; NULL has none. */
bool sk_background_refusal(const char *screen, char id[9]);
/* args, the user's arguments, only resume or continue a session: then
 * nothing of the user's makes Claude Code refuse. NULL-terminated. */
bool sk_background_bare_resume(char *const *args);
/* The command that opens the session the tab's Claude refused to open,
 * cmd[0] attach ID, into out; false when there is none. cmd: the tab's
 * command; screen: its final screen. */
bool sk_background_attach_for(char *const *cmd, const char *screen, sk_strv *out);

#endif
