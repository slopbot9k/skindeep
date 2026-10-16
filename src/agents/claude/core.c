/* The Claude session as its three channels describe it, as
 * app/metrics/claude/core.js has it (decisions 0009, 0011, 0012, 0032,
 * 0034): which session and transcript are current, what a change
 * resets, which lines are history, which source wins each metric, and
 * the subagent count. No I/O and no clock: the bus owns the rates. */
#include "internal.h"

#include <math.h>
#include <string.h>

/* What a statusline payload claims for the rest of the session. */
enum { CLAIM_CONTEXT = 1, CLAIM_MODEL = 2, CLAIM_CACHE = 4 };

struct sk_claude_core {
    sk_source *src;
    sk_claude_facts facts;
    char *session, *transcript, *title;
    sk_claude_totals totals;
    sk_claude_checklist checklist;
    unsigned claims;
    double agents;
    int stops;
    bool status_seen;
    sk_arena arena; /* for hook metadata */
};

static const sk_metric_default HOOK_DEFAULTS[] = {
    {"tools.total", {SK_VAL_NUM, 0, NULL}},
    {"agents.active", {SK_VAL_NUM, 0, NULL}},
    {"state.busy", {SK_VAL_NUM, 0, NULL}},
};
static const char *const HOOK_RATES[] = {"tokens.out_rate", "tools.rate"};
static const char *const OUT_RATE[] = {"tokens.out_rate"};
static const sk_source_spec WITH_HOOKS = {HOOK_DEFAULTS, SK_LEN(HOOK_DEFAULTS), HOOK_RATES, SK_LEN(HOOK_RATES)};
static const sk_source_spec WITHOUT_HOOKS = {NULL, 0, OUT_RATE, SK_LEN(OUT_RATE)};

/* The hook-fed defaults and tools.rate only when the hooks were
 * injected (decision 0009). */
const sk_source_spec *sk_claude_source_spec(const sk_claude_facts *f)
{
    return f->hooks ? &WITH_HOOKS : &WITHOUT_HOOKS;
}

static void quiet(sk_claude_core *c)
{
    c->stops = 0;
    c->status_seen = false;
}

sk_claude_core *sk_claude_core_new(sk_source *src, const sk_claude_facts *facts)
{
    sk_claude_core *c = sk_xcalloc(1, sizeof *c);
    c->src = src;
    c->facts = *facts;
    sk_claude_totals_init(&c->totals);
    sk_claude_checklist_init(&c->checklist);
    sk_arena_init(&c->arena, 4096);
    quiet(c);
    return c;
}

void sk_claude_core_free(sk_claude_core *c)
{
    if (!c)
        return;
    free(c->session);
    free(c->transcript);
    free(c->title);
    sk_claude_totals_free(&c->totals);
    sk_claude_checklist_free(&c->checklist);
    sk_arena_free(&c->arena);
    free(c);
}

const char *sk_claude_core_session(const sk_claude_core *c)
{
    return c->session;
}

const char *sk_claude_core_transcript(const sk_claude_core *c)
{
    return c->transcript;
}

const char *sk_claude_core_title(const sk_claude_core *c)
{
    return c->title;
}

static void forget_transcript(sk_claude_core *c)
{
    sk_claude_totals_free(&c->totals);
    sk_claude_totals_init(&c->totals);
    sk_claude_checklist_free(&c->checklist);
    sk_claude_checklist_init(&c->checklist);
}

/* A payload without a session id is placed in no session and ignored. */
bool sk_claude_core_enter_ids(sk_claude_core *c, const char *session, const char *transcript)
{
    if (!session || !*session)
        return false;
    if (c->session && strcmp(session, c->session) != 0) {
        sk_source_reset(c->src);
        forget_transcript(c);
        c->claims = 0;
        c->agents = 0;
        free(c->title);
        c->title = NULL;
        quiet(c);
    }
    if (!c->session || strcmp(session, c->session) != 0) {
        free(c->session);
        c->session = sk_xstrdup(session);
    }
    sk_source_begin(c->src);
    if (transcript && *transcript && (!c->transcript || strcmp(transcript, c->transcript) != 0)) {
        free(c->transcript);
        c->transcript = sk_xstrdup(transcript);
        forget_transcript(c);
    }
    return true;
}

/* A string field of j as a C string in the core's arena, or NULL. */
static const char *field(sk_claude_core *c, const sk_json *j, const char *key)
{
    sk_str s;
    if (!sk_json_as_string(sk_json_get(j, key), &s))
        return NULL;
    return sk_arena_strndup(&c->arena, s.ptr, s.len);
}

bool sk_claude_core_enter(sk_claude_core *c, const sk_json *j)
{
    if (!j || j->type != SK_JSON_OBJECT)
        return false;
    bool ok = sk_claude_core_enter_ids(c, field(c, j, "session_id"), field(c, j, "transcript_path"));
    sk_arena_reset(&c->arena);
    return ok;
}

static int event_kind(sk_str ev)
{
    static const struct {
        const char *name;
        int kind;
    } KINDS[] = {
        {"PreToolUse", SK_EV_TOOL}, {"UserPromptSubmit", SK_EV_PROMPT}, {"Stop", SK_EV_STOP},
        {"SubagentStart", SK_EV_AGENT}, {"PostCompact", SK_EV_COMPACT},
    };
    for (size_t i = 0; i < SK_LEN(KINDS); i++)
        if (sk_str_eq_c(ev, KINDS[i].name))
            return KINDS[i].kind;
    return 0;
}

/* A hook payload when no hooks were injected is not this session's. */
void sk_claude_core_hook(sk_claude_core *c, const sk_json *body)
{
    if (!c->facts.hooks || !sk_claude_core_enter(c, body))
        return;
    const sk_json *m = sk_claude_hook_meta(&c->arena, body);
    sk_claude_from_hook(m, c->src);
    sk_str ev = {"", 0};
    sk_json_as_string(sk_json_get(m, "hook_event_name"), &ev);
    int kind = event_kind(ev);
    if (kind)
        sk_source_event(c->src, kind, kind == SK_EV_TOOL ? sk_claude_tool_class(sk_json_get(m, "tool_name")) : 0,
                        false);
    if (sk_str_eq_c(ev, "PreToolUse"))
        sk_source_sample(c->src, "tools.rate", 1, false);
    /* The statusline runs at least once a turn. Silent through two turns,
     * something overrides it, such as remote managed settings. */
    if (sk_str_eq_c(ev, "Stop") && c->facts.statusline && !c->status_seen && ++c->stops == 2)
        sk_claude_logf(c->facts.log, c->facts.log_ud,
                       "claude adapter: session %s: the injected statusline stayed silent through two "
                       "turns, so managed settings may override it",
                       c->session);
    bool start = sk_str_eq_c(ev, "SubagentStart");
    if (start || sk_str_eq_c(ev, "SubagentStop")) {
        c->agents = fmax(0, c->agents + (start ? 1 : -1));
        sk_source_set_num(c->src, "agents.active", c->agents);
    }
    sk_arena_reset(&c->arena);
}

static unsigned claim_of(const char *name)
{
    if (!strncmp(name, "context.", 8))
        return CLAIM_CONTEXT;
    if (!strcmp(name, "model.name"))
        return CLAIM_MODEL;
    if (!strcmp(name, "cache.hit_ratio"))
        return CLAIM_CACHE;
    return 0;
}

static void apply(sk_claude_core *c, const sk_claude_kvs *kvs)
{
    for (size_t i = 0; i < kvs->len; i++)
        sk_source_set(c->src, kvs->ptr[i].name, kvs->ptr[i].val);
}

void sk_claude_core_statusline(sk_claude_core *c, const sk_json *j)
{
    if (!sk_claude_core_enter(c, j))
        return;
    c->status_seen = true;
    sk_claude_kvs kvs = {0};
    sk_claude_from_statusline(j, &c->facts.compaction, &kvs);
    for (size_t i = 0; i < kvs.len; i++)
        c->claims |= claim_of(kvs.ptr[i].name);
    apply(c, &kvs);
    sk_claude_kvs_free(&kvs);
}

/* Context figures from the transcript when no statusline runs. */
static void fallback_context(sk_claude_core *c)
{
    const sk_claude_compaction *cp = &c->facts.compaction;
    const sk_claude_usage *last = c->totals.has_last ? &c->totals.last : NULL;
    double window = sk_claude_compact_window(cp, last ? last->used : 0);
    double threshold = sk_claude_compact_threshold(window, cp);
    sk_source_set_num(c->src, "context.window", window);
    sk_source_set_num(c->src, "context.window_inferred", 1);
    sk_source_set(c->src, "context.compact_threshold", isnan(threshold) ? SK_NULLV : SK_NUM(threshold));
    sk_source_set(c->src, "context.compact_at_pct",
                  sk_claude_truthy_num(threshold) ? SK_NUM(threshold / window * 100) : SK_NULLV);
    sk_claude_kvs kvs = {0};
    sk_claude_context_from(last, window, threshold, &kvs);
    apply(c, &kvs);
    sk_claude_kvs_free(&kvs);
}

/* One line of the transcript file. replayed: it was in the file when
 * the tail started. */
void sk_claude_core_line(sk_claude_core *c, const char *file, const sk_json *obj, bool replayed)
{
    if (!c->transcript || !file || strcmp(file, c->transcript) != 0 || !obj || obj->type != SK_JSON_OBJECT)
        return;
    sk_buf t = {0};
    if (sk_claude_title_of(obj, c->session, &t)) {
        free(c->title);
        c->title = sk_xstrdup(sk_buf_cstr(&t));
    }
    sk_buf_free(&t);
    sk_claude_kvs kvs = {0};
    if (sk_claude_checklist_add(&c->checklist, obj)) {
        sk_claude_checklist_metrics(&c->checklist, &kvs);
        apply(c, &kvs);
        sk_claude_kvs_free(&kvs);
    }
    double d_out = 0;
    if (!sk_claude_totals_add(&c->totals, obj, &d_out))
        return;
    if (d_out > 0)
        sk_source_sample(c->src, "tokens.out_rate", d_out, replayed);
    sk_claude_totals_metrics(&c->totals, &kvs);
    apply(c, &kvs);
    sk_claude_kvs_free(&kvs);
    if (c->totals.model && !(c->claims & CLAIM_MODEL))
        sk_source_set_str(c->src, "model.name", sk_str_c(c->totals.model));
    if (!(c->claims & CLAIM_CACHE)) {
        double r = sk_claude_totals_hit_ratio(&c->totals);
        sk_source_set(c->src, "cache.hit_ratio", isnan(r) ? SK_NULLV : SK_NUM(r));
    }
    if (!(c->claims & CLAIM_CONTEXT))
        fallback_context(c);
}
