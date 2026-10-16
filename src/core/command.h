/* The command a tab runs, found as app/cli.js's resolveCommand finds it,
 * and the exit statuses of decision 0003. */
#ifndef SK_COMMAND_H
#define SK_COMMAND_H

#include "../base/base.h"

/* The app itself failed: a bad skin at launch, an internal error, a pty
 * that cannot be spawned. */
#define SK_EXIT_APP 125
/* The command resolved, but its exec failed for another reason than a
 * missing file, as bash reports it. */
#define SK_EXIT_CANNOT_EXEC 126
/* The command is not on PATH, or the file or the interpreter it names is
 * missing. */
#define SK_EXIT_NOT_FOUND 127

/* The executable regular file cmd names, malloc'd, or NULL. A cmd with a
 * '/' is a path, made absolute against the working directory. Any other
 * is looked for in each entry of path, a PATH value, whose empty entries
 * are skipped. The result is normalized as Node's path module does. */
char *sk_resolve_command(const char *cmd, const char *path);

#endif
