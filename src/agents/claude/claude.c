/* A stub provider until the claude adapter of phase 2 replaces it: it
 * matches `claude` and `claude_<suffix>` and acts as plain. */
#include "../agent.h"

#include <string.h>

static bool matches(const char *base)
{
    size_t n = strlen("claude");
    return !strncmp(base, "claude", n) && (base[n] == 0 || (base[n] == '_' && base[n + 1]));
}

static sk_part create(const sk_agent_ctx *ctx)
{
    return sk_provider_plain.create(ctx);
}

static void carry(char *const *argv, sk_strv *args, sk_strv *dropped)
{
    sk_provider_plain.carry(argv, args, dropped);
}

const sk_provider sk_provider_claude = {"claude", matches, create, carry};
