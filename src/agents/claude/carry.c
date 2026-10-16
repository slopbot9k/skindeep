/* The options of Claude Code 2.1.296 (`claude --help`) a new tab
 * carries, as app/metrics/claude/carry.js lists them (decision 0023):
 * those that set up any session the same way, the restricting ones
 * above all. Not carried: those of one session or one run (--resume,
 * --continue, --fork-session, --session-id, --from-pr, --teleport,
 * --name, --worktree, --tmux, --remote-control, --cloud, --desktop,
 * --bg, --file, --debug-file, --print and its output options) and any
 * option missing here. */
#include "internal.h"

#include "../../core/carry.h"

static const sk_option OPTIONS[] = {
    {"--allow-dangerously-skip-permissions", SK_ARITY_FLAG},
    {"--ax-screen-reader", SK_ARITY_FLAG},
    {"--bare", SK_ARITY_FLAG},
    {"--brief", SK_ARITY_FLAG},
    {"--chrome", SK_ARITY_FLAG},
    {"--no-chrome", SK_ARITY_FLAG},
    {"--dangerously-skip-permissions", SK_ARITY_FLAG},
    {"--disable-slash-commands", SK_ARITY_FLAG},
    {"--exclude-dynamic-system-prompt-sections", SK_ARITY_FLAG},
    {"--ide", SK_ARITY_FLAG},
    {"--no-session-persistence", SK_ARITY_FLAG},
    {"--restricted", SK_ARITY_FLAG},
    {"--safe-mode", SK_ARITY_FLAG},
    {"--strict-mcp-config", SK_ARITY_FLAG},
    {"--verbose", SK_ARITY_FLAG},
    {"--agent", SK_ARITY_ONE},
    {"--agents", SK_ARITY_ONE},
    {"--append-system-prompt", SK_ARITY_ONE},
    {"--append-system-prompt-file", SK_ARITY_ONE},
    {"--autocompact", SK_ARITY_ONE},
    {"--effort", SK_ARITY_ONE},
    {"--fallback-model", SK_ARITY_ONE},
    {"--max-budget-usd", SK_ARITY_ONE},
    {"--model", SK_ARITY_ONE},
    {"--permission-mode", SK_ARITY_ONE},
    {"--plugin-dir", SK_ARITY_ONE},
    {"--plugin-url", SK_ARITY_ONE},
    {"--setting-sources", SK_ARITY_ONE},
    {"--settings", SK_ARITY_ONE},
    {"--system-prompt", SK_ARITY_ONE},
    {"--system-prompt-file", SK_ARITY_ONE},
    {"--system-prompt-snapshot", SK_ARITY_ONE},
    {"--add-dir", SK_ARITY_MANY},
    {"--allowedTools", SK_ARITY_MANY},
    {"--allowed-tools", SK_ARITY_MANY},
    {"--betas", SK_ARITY_MANY},
    {"--disallowedTools", SK_ARITY_MANY},
    {"--disallowed-tools", SK_ARITY_MANY},
    {"--mcp-config", SK_ARITY_MANY},
    {"--tools", SK_ARITY_MANY},
    {"-d", SK_ARITY_OPTIONAL},
    {"--debug", SK_ARITY_OPTIONAL},
    {"--prompt-suggestions", SK_ARITY_OPTIONAL},
};

void sk_claude_carry(char *const *argv, sk_strv *args, sk_strv *dropped)
{
    sk_carry_pick(argv, OPTIONS, SK_LEN(OPTIONS), args, dropped);
}
