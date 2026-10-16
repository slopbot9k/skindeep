#include "names.h"

#include "../base/utf8.h"

#include <string.h>

const sk_metric_name sk_metric_names[] = {
    {"context.used_pct", "percent of the context window in use"},
    {"context.used_tokens", "tokens in the context window"},
    {"context.window", "context window size in tokens"},
    {"context.window_inferred", "1 when the window size is inferred, 0 when read"},
    {"context.compact_threshold", "tokens at which auto-compact fires"},
    {"context.compact_at_pct", "auto-compact threshold as a percent of the window"},
    {"context.compact_left_pct", "percent of the way left before auto-compact"},
    {"context.uncached", "context tokens sent uncached"},
    {"context.cache_read", "context tokens read from the prompt cache"},
    {"context.cache_write", "context tokens written to the prompt cache"},
    {"context.output", "output tokens of the last response"},
    {"tokens.in", "input tokens this session, cached and uncached"},
    {"tokens.out", "output tokens this session"},
    {"tokens.cache_read", "cache-read input tokens this session"},
    {"tokens.cache_write", "cache-write input tokens this session"},
    {"tokens.out_rate", "output tokens per second over the last 10 s"},
    {"cache.hit_ratio", "share of input read from the prompt cache, 0 to 1"},
    {"cost.usd", "session cost in US dollars as the CLI reports it"},
    {"limits.five_hour_pct", "percent of the five-hour usage limit used"},
    {"limits.five_hour_resets_at", "when the five-hour limit resets, Unix seconds"},
    {"limits.seven_day_pct", "percent of the seven-day usage limit used"},
    {"limits.seven_day_resets_at", "when the seven-day limit resets, Unix seconds"},
    {"limits.spend_pct", "percent of a gateway session's spend limit used"},
    {"limits.spend_usd", "US dollars spent against a gateway session's spend limit"},
    {"limits.spend_limit_usd", "a gateway session's spend limit in US dollars"},
    {"limits.spend_resets_at", "when a gateway session's spend limit resets, Unix seconds"},
    {"limits.items", "every usage limit the provider sends now, [{ key, label, window, model, pct, "
                     "resets_at }]"},
    {"tools.total", "tool calls this session"},
    {"tools.rate", "tool calls in the last 60 s"},
    {"agents.active", "subagents running now"},
    {"lines.added", "lines added this session"},
    {"lines.removed", "lines removed this session"},
    {"state.busy", "1 while the agent works on a prompt, else 0"},
    {"model.name", "model name (text)"},
    {"session.clock_s", "seconds since launch"},
    {"session.compactions", "compactions this session"},
    {"checklist.items", "the agent's task list, [{ text, state }], state todo, doing or done"},
    {"checklist.count", "items of the agent's task list"},
    {"checklist.done", "items of the agent's task list that are done"},
    {"pty.out_bps", "terminal output bytes per second"},
    {"pty.in_kps", "input writes (keys, pastes) per second"},
    {"pty.idle_s", "seconds since the last terminal output"},
    {"proc.cpu_pct", "CPU percent of the CLI process tree"},
    {"proc.mem_mb", "resident memory of the CLI process tree in MiB"},
    {"event.output", "counter: output bursts"},
    {"event.tool", "counter: tool calls"},
    {"event.prompt", "counter: prompts submitted"},
    {"event.stop", "counter: turns finished"},
    {"event.compact", "counter: compactions"},
    {"event.agent", "counter: subagents started"},
};

const size_t sk_metric_names_len = SK_LEN(sk_metric_names);

static bool safe(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool sk_metric_known(sk_str name)
{
    for (size_t i = 0; i < sk_metric_names_len; i++)
        if (sk_str_eq_c(name, sk_metric_names[i].name))
            return true;
    if (!sk_str_prefix(name, SK_STR("tools.")))
        return false;
    sk_str rest = sk_str_sub(name, 6, SIZE_MAX);
    if (rest.len < 1 || rest.len > 64)
        return false;
    for (size_t i = 0; i < rest.len; i++)
        if (!safe(rest.ptr[i]))
            return false;
    return true;
}

bool sk_metric_is_list(sk_str name)
{
    return sk_str_eq_c(name, "checklist.items") || sk_str_eq_c(name, "limits.items");
}

/* JavaScript's String(name).replace(/[^A-Za-z0-9_]/g, '_').slice(0, 64)
 * counts UTF-16 code units: a character beyond U+FFFF is two of them,
 * so it becomes two underscores. */
void sk_metric_tool_key(sk_buf *out, sk_str tool)
{
    sk_buf_append_c(out, "tools.");
    size_t units = 0;
    for (size_t i = 0; i < tool.len && units < 64;) {
        char c = tool.ptr[i];
        if ((unsigned char)c < 0x80) {
            sk_buf_putc(out, safe(c) ? c : '_');
            units++;
            i++;
            continue;
        }
        uint32_t cp;
        i += sk_utf8_decode(tool.ptr + i, tool.len - i, &cp, NULL);
        for (int k = cp > 0xFFFF ? 2 : 1; k > 0 && units < 64; k--, units++)
            sk_buf_putc(out, '_');
    }
}
