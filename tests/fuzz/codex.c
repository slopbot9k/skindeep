/* The Codex adapter's rollout lines: whatever JSON lines a rollout holds,
 * the line handler never crashes, and the bus it fills still writes
 * valid JSON. The first byte picks whether lines are history. CODEX_HOME
 * names no directory, so discovery finds nothing to read. */
#include "../../src/agents/codex/codex.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "\"type\":\"session_meta\"", "\"type\":\"event_msg\"", "\"type\":\"response_item\"",
    "\"type\":\"turn_context\"", "\"type\":\"token_count\"", "\"payload\":{", "\"info\":{",
    "\"total_token_usage\":{", "\"last_token_usage\":{", "\"input_tokens\":", "\"cached_input_tokens\":",
    "\"output_tokens\":", "\"reasoning_output_tokens\":", "\"model_context_window\":", "\"rate_limits\":{",
    "\"primary\":{", "\"secondary\":{", "\"used_percent\":", "\"resets_at\":", "\"window_minutes\":",
    "\"model\":", "\"id\":", "\"timestamp\":\"2026-10-16T12:00:00.000Z\"", "\"function_call\"",
    "\"name\":\"shell\"", "\"user_message\"", "\"agent_message\"", "\"forked_from_id\":", NULL,
};

static char *const CMD[] = {"codex", NULL};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size)
        return 0;
    bool history = data[0] & 1;
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_envp env;
    sk_envp_from(&env, NULL);
    sk_envp_set(&env, "CODEX_HOME", "/nonexistent/codex-home");
    sk_agent_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cmd = CMD;
    ctx.cwd = "/w";
    ctx.env = &env;
    ctx.bus = bus;
    sk_part part = sk_provider_codex.create(&ctx);
    sk_arena a;
    sk_arena_init(&a, 0);
    const char *p = (const char *)data + 1, *end = (const char *)data + size;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const sk_json *j = sk_json_parse(&a, p, len, NULL);
        if (j)
            sk_codex_on_line(part.self, j, history);
        p += len + 1;
    }
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_bus_write(bus, &w);
    if (!sk_json_parse(&a, out.ptr, out.len, NULL))
        abort();
    sk_buf_free(&out);
    sk_arena_free(&a);
    part.ops->close(part.self);
    part.ops->free(part.self);
    sk_envp_free(&env);
    sk_bus_free(bus);
    return 0;
}
