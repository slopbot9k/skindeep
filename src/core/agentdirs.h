/* Where Claude Code and Codex keep their files, as the JavaScript app
 * finds them (app/metrics/claude/settings.js, app/sessions.js,
 * app/metrics/codex/discovery.js). */
#ifndef SK_AGENTDIRS_H
#define SK_AGENTDIRS_H

#include "env.h"

/* env's CLAUDE_CONFIG_DIR when set and not empty, else ~/.claude, ~ being
 * the app's own home. malloc'd; NULL when there is no home. */
char *sk_claude_config_dir(const sk_envp *env);
/* env's CODEX_HOME when set and not empty, else ~/.codex. */
char *sk_codex_home(const sk_envp *env);
/* Claude Code's folder name for a working directory under projects/:
 * cwd.replace(/[^A-Za-z0-9]/g, '-'), which works on UTF-16 code units,
 * so a character beyond U+FFFF becomes two dashes. Appends to out. */
void sk_claude_slug(sk_buf *out, sk_str cwd);

#endif
