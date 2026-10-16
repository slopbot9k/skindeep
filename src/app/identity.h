/* Who runs in a tab, as app/identity.js derives it (decision 0004): the
 * label, the window title, a word quoted for a shell, a path with the
 * home directory as ~, and the child's live command line.
 *
 * home is the app's home directory, as sk_env_current finds it. NULL or
 * "" means there is none, and nothing is written with ~. */
#ifndef SK_APP_IDENTITY_H
#define SK_APP_IDENTITY_H

#include "../base/str.h"
#include "../core/env.h"

#include <sys/types.h>

/* arg as it is when it is one or more of A-Z a-z 0-9 _ @ % + = : , . /
 * and -, else in single quotes with each ' as '\''. */
void sk_identity_quote(sk_buf *out, sk_str arg);
/* Every word quoted, joined by spaces. */
void sk_identity_quote_argv(sk_buf *out, char *const *argv);
/* p, with the home directory written as ~ when p is it or lies under
 * it. */
void sk_identity_tildify(sk_buf *out, const char *p, const char *home);
/* A command line as the strip shows it: each space-separated word that
 * is the home directory or lies under it written with ~, as a shell
 * would read it back. */
void sk_identity_shown_cmdline(sk_buf *out, const char *cmdline, const char *home);
/* The command's basename, with the config dir's name in brackets when
 * the session runs on one other than the default: claude [work] for
 * CLAUDE_CONFIG_DIR=~/.claude-work or ~/profiles/work/.claude. kind is
 * the dispatched provider's (decision 0021): "claude" reads
 * CLAUDE_CONFIG_DIR from env, "codex" CODEX_HOME, any other nothing. A
 * relative config dir is taken from the working directory. */
void sk_identity_label(sk_buf *out, char *const *cmd, const sk_envp *env, const char *kind,
                       const char *home);
/* "<label> in claude-skins". */
void sk_identity_title(sk_buf *out, const char *label);

/* The live command line. The per-OS process info supplies a process's
 * argv as the kernel keeps it: /proc/<pid>/cmdline on Linux,
 * KERN_PROCARGS2 on macOS. Until the caller sets it, no command line is
 * live. */

/* Appends pid's arguments to raw, each ended by a NUL. False when the
 * process is gone or cannot be read. */
typedef bool (*sk_identity_argv_fn)(pid_t pid, sk_buf *raw);
void sk_identity_set_argv_source(sk_identity_argv_fn fn);
/* The kernel's own, by platform/procinfo: the app's source. */
bool sk_identity_kernel_argv(pid_t pid, sk_buf *raw);
/* liveCmdline: pid's arguments, as the kernel sees them after any exec
 * in a wrapper, quoted and joined. False when unknown. */
bool sk_identity_live_cmdline(pid_t pid, sk_buf *out);
/* The same from n bytes of NUL-separated arguments: a last empty one,
 * the terminator's, is dropped, and invalid UTF-8 reads as U+FFFD. */
void sk_identity_cmdline_of(sk_buf *out, const char *raw, size_t n);

#endif
