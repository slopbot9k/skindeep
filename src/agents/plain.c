/* The adapter of any CLI that is neither Claude Code nor Codex, as
 * app/metrics/plain.js: it injects nothing and has no channel, so the
 * session runs on Generic alone. */
#include "agent.h"

#include <string.h>

static void attach(void *self, pid_t pid)
{
    (void)self, (void)pid;
}

static void poll_part(void *self, uint64_t now)
{
    (void)self, (void)now;
}

static void report(void *self, sk_report *out)
{
    (void)self;
    memset(out, 0, sizeof *out);
}

static void nothing(void *self)
{
    (void)self;
}

static const sk_part_ops OPS = {NULL, attach, poll_part, report, NULL, nothing, nothing, NULL};

static bool matches(const char *base)
{
    (void)base;
    return false;
}

static sk_part create(const sk_agent_ctx *ctx)
{
    (void)ctx;
    return (sk_part){&OPS, NULL};
}

/* Nothing is known of the CLI's options, so a new tab carries none. */
static void carry(char *const *argv, sk_strv *args, sk_strv *dropped)
{
    (void)args;
    sk_strv_extend(dropped, argv);
}

const sk_provider sk_provider_plain = {"plain", matches, create, carry};
