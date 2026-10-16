/* The Claude adapter's core: whatever JSON lines arrive as hook payloads,
 * statusline input or transcript lines, it never crashes, and the bus it
 * fills still writes valid JSON. Every line goes to all three, since a
 * payload of one kind can arrive where another is expected. The first
 * byte picks the facts and whether lines are replayed. */
#include "../../src/agents/claude/internal.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "\"hook_event_name\":", "\"session_id\":", "\"transcript_path\":\"/t.jsonl\"", "\"type\":\"assistant\"",
    "\"type\":\"user\"", "\"type\":\"system\"", "\"subtype\":\"compact_boundary\"", "\"message\":{",
    "\"usage\":{", "\"input_tokens\":", "\"output_tokens\":", "\"cache_read_input_tokens\":",
    "\"cache_creation_input_tokens\":", "\"model\":", "\"display_name\":", "\"context_window\":{",
    "\"cost\":{", "\"total_cost_usd\":", "\"rate_limits\":{", "\"five_hour\":{", "\"seven_day\":{",
    "\"used_percentage\":", "\"resets_at\":", "\"UserPromptSubmit\"", "\"PreToolUse\"",
    "\"PostToolUse\"", "\"SessionStart\"", "\"Stop\"", "\"isSidechain\":true", "\"timestamp\":",
    "\"2026-10-16T12:00:00.000Z\"", "\"customTitle\":", "\"tool_use\"", "\"content\":[", NULL,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size)
        return 0;
    unsigned how = data[0];
    sk_claude_facts facts = {.hooks = how & 1, .statusline = (how & 2) != 0};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_claude_core *core = sk_claude_core_new(sk_bus_source(bus, sk_claude_source_spec(&facts)), &facts);
    sk_arena a;
    sk_arena_init(&a, 0);
    const char *p = (const char *)data + 1, *end = (const char *)data + size;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const sk_json *j = sk_json_parse(&a, p, len, NULL);
        if (j) {
            sk_claude_core_hook(core, j);
            sk_claude_core_statusline(core, j);
            sk_claude_core_line(core, "/t.jsonl", j, (how & 4) != 0);
        }
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
    sk_claude_core_free(core);
    sk_bus_free(bus);
    return 0;
}
