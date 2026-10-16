/* The Claude session core on a bus and a fake clock, no I/O, as
 * test/claude-core.test.js has it. Snapshots compare as sets of names,
 * as deepStrictEqual compares objects. */
#include "../../src/agents/claude/internal.h"
#include "../test.h"

#include <math.h>

static sk_arena A;
static const sk_json *STATUS;
static const sk_json **HOOKS, **TRANSCRIPT;
static size_t N_HOOKS, N_TRANSCRIPT;

static const sk_json *json(const char *text)
{
    const sk_json *v = sk_json_parse(&A, text, strlen(text), NULL);
    if (!v)
        sk_test_fail(__FILE__, __LINE__, "bad JSON: %s", text);
    return v;
}

/* Each line of a fixture, parsed. */
static const sk_json **lines(const char *path, size_t *n)
{
    char *text = sk_test_read(path, NULL);
    CHECK(text != NULL);
    SK_VEC(const sk_json *) v = {0};
    for (const char *line = text; text && *line;) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len) {
            const sk_json *j = sk_json_parse(&A, line, len, NULL);
            CHECK(j != NULL);
            if (j)
                sk_vec_push(&v, j);
        }
        line += len + (nl ? 1 : 0);
    }
    free(text);
    const sk_json **out = sk_arena_array(&A, v.len ? v.len : 1, sizeof *out);
    for (size_t i = 0; i < v.len; i++)
        out[i] = v.ptr[i];
    *n = v.len;
    sk_vec_free(&v);
    return out;
}

static void fixtures(void)
{
    sk_arena_init(&A, 0);
    char *text = sk_test_read("test/fixtures/claude/statusline.json", NULL);
    CHECK(text != NULL);
    STATUS = json(text ? text : "{}");
    free(text);
    HOOKS = lines("test/fixtures/claude/hooks.jsonl", &N_HOOKS);
    TRANSCRIPT = lines("test/fixtures/claude/transcript-read.jsonl", &N_TRANSCRIPT);
}

/* base with key set to the JSON value, in place when present, else
 * appended, as Object.assign places it; a NULL value removes key. */
static const sk_json *at(const sk_json *base, const char *key, const char *value)
{
    sk_json *copy = sk_arena_alloc(&A, sizeof *copy);
    *copy = *base;
    copy->object.members = sk_arena_array(&A, base->object.len + 1, sizeof *copy->object.members);
    copy->object.len = 0;
    bool placed = false;
    for (size_t i = 0; i < base->object.len; i++) {
        sk_json_member m = base->object.members[i];
        if (sk_str_eq_c(m.key, key)) {
            placed = true;
            if (!value)
                continue;
            m.value = *json(value);
        }
        copy->object.members[copy->object.len++] = m;
    }
    if (!placed && value) {
        sk_json_member m = {sk_str_c(key), *json(value)};
        copy->object.members[copy->object.len++] = m;
    }
    return copy;
}

/* STATUS with session_id and transcript_path. */
static const sk_json *status_for(const char *session, const char *transcript)
{
    sk_buf s = {0}, t = {0};
    sk_buf_printf(&s, "\"%s\"", session);
    const sk_json *j = at(STATUS, "session_id", s.ptr);
    if (transcript) {
        sk_buf_printf(&t, "\"%s\"", transcript);
        j = at(j, "transcript_path", t.ptr);
    }
    sk_buf_free(&s);
    sk_buf_free(&t);
    return j;
}

typedef struct {
    sk_bus *bus;
    sk_claude_core *core;
} made;

static int logged;
static char last_log[512];
static void log_line(void *ud, const char *msg)
{
    (void)ud;
    logged++;
    snprintf(last_log, sizeof last_log, "%s", msg);
}

/* hooks on and no compaction configured, unless facts say. */
static made make_with(const sk_claude_facts *facts)
{
    made m;
    m.bus = sk_bus_new(NULL, NULL);
    m.core = sk_claude_core_new(sk_bus_source(m.bus, sk_claude_source_spec(facts)), facts);
    return m;
}

static made make(void)
{
    sk_claude_facts f = {.hooks = true};
    return make_with(&f);
}

static void unmake(made *m)
{
    sk_claude_core_free(m->core);
    sk_bus_free(m->bus);
}

static sk_claude_compaction compaction(const sk_claude_kept *sources, size_t n, char *const *argv,
                                       const char *env_var)
{
    sk_envp e;
    char *vars[] = {(char *)env_var, NULL};
    sk_envp_from(&e, env_var ? vars : NULL);
    sk_claude_compaction c;
    sk_claude_compaction_of(&c, sources, n, argv, &e);
    sk_envp_free(&e);
    return c;
}

static const sk_claude_kept S600 = {.has_window = true, .window = 600000};

static double num(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_NUM ? v->num : NAN;
}

static bool is_null(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_NULL;
}

static char *snapshot(const sk_bus *b)
{
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_bus_write(b, &w);
    return out.ptr;
}

/* The members of an object as sorted "key=value" lines, those whose key
 * keep() accepts. */
static char *as_set(const char *object, bool (*keep)(sk_str))
{
    const sk_json *o = json(object);
    sk_strv v = {0};
    for (size_t i = 0; o && i < o->object.len; i++) {
        if (keep && !keep(o->object.members[i].key))
            continue;
        sk_buf line = {0}, val = {0};
        sk_jw w;
        sk_jw_init(&w, &val, 0);
        sk_jw_value(&w, &o->object.members[i].value);
        sk_buf_printf(&line, "%.*s=%s", (int)o->object.members[i].key.len, o->object.members[i].key.ptr,
                      sk_buf_cstr(&val));
        sk_strv_push(&v, line.ptr);
        sk_buf_free(&line);
        sk_buf_free(&val);
    }
    for (size_t i = 1; i < v.len; i++)
        for (size_t k = i; k > 0 && strcmp(v.ptr[k - 1], v.ptr[k]) > 0; k--) {
            char *t = v.ptr[k];
            v.ptr[k] = v.ptr[k - 1];
            v.ptr[k - 1] = t;
        }
    sk_buf joined = {0};
    for (size_t i = 0; i < v.len; i++)
        sk_buf_printf(&joined, "%s\n", v.ptr[i]);
    sk_strv_free(&v);
    sk_buf_reserve(&joined, 0);
    return joined.ptr;
}

static char *set_of(const sk_bus *b, bool (*keep)(sk_str))
{
    char *s = snapshot(b);
    char *set = as_set(s, keep);
    free(s);
    return set;
}

#define CHECK_SNAPSHOT(b, want)                                              \
    do {                                                                     \
        char *got_ = set_of(b, NULL), *want_ = as_set(want, NULL);           \
        CHECK_STR(got_, want_);                                              \
        free(got_);                                                          \
        free(want_);                                                         \
    } while (0)

static void hook(made *m, const char *body)
{
    sk_claude_core_hook(m->core, json(body));
}

static void test_c1_statusline_gives_context_cost_limits_model_and_cache(void)
{
    fixtures();
    sk_claude_facts f = {.hooks = true, .compaction = compaction(&S600, 1, NULL, NULL)};
    made m = make_with(&f);
    sk_claude_core_statusline(m.core, STATUS);
    CHECK(num(m.bus, "context.window") == 1000000);
    CHECK(num(m.bus, "context.window_inferred") == 0);
    CHECK(num(m.bus, "context.used_tokens") == 4 + 15443 + 40263);
    CHECK(fabs(num(m.bus, "context.used_pct") - 5.571) < 0.001);
    CHECK(num(m.bus, "context.compact_threshold") == 567000);
    CHECK(fabs(num(m.bus, "context.compact_left_pct") - (1 - 55710.0 / 567000) * 100) < 1e-9);
    CHECK(num(m.bus, "context.cache_read") == 40263);
    CHECK(num(m.bus, "context.cache_write") == 15443);
    CHECK(num(m.bus, "context.uncached") == 4);
    CHECK(num(m.bus, "cost.usd") == 0.3751829);
    CHECK(num(m.bus, "limits.five_hour_pct") == 1);
    const sk_val *model = sk_bus_get(m.bus, "model.name");
    CHECK(model && model->type == SK_VAL_STR && !strcmp(model->text, "Opus 5.5"));
    CHECK(num(m.bus, "cache.hit_ratio") == 0.6009763039151228);
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c2_a_null_current_usage_reads_as_no_data(void)
{
    sk_arena_init(&A, 0);
    made m = make();
    sk_claude_core_statusline(
        m.core, json("{\"session_id\":\"s\",\"context_window\":{\"context_window_size\":200000,\"current_usage\":null}}"));
    CHECK(num(m.bus, "context.window") == 200000);
    CHECK(is_null(m.bus, "context.used_tokens"));
    CHECK(is_null(m.bus, "context.used_pct"));
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c3_hooks_count_tools_by_type_and_track_busy_state(void)
{
    fixtures();
    made m = make();
    for (size_t i = 0; i < N_HOOKS; i++)
        sk_claude_core_hook(m.core, HOOKS[i]);
    CHECK_SNAPSHOT(m.bus, "{\"event.prompt\":4,\"event.stop\":4,\"event.tool\":1,\"agents.active\":0,"
                          "\"state.busy\":0,\"tools.total\":1,\"tools.Read\":1,\"tools.rate\":0,"
                          "\"tokens.out_rate\":0}");
    sk_buf body = {0};
    sk_str sid;
    CHECK(N_HOOKS == 12 && sk_json_as_string(sk_json_get(HOOKS[11], "session_id"), &sid));
    sk_buf_printf(&body, "{\"session_id\":\"%.*s\",\"hook_event_name\":\"PreToolUse\",\"tool_name\":\"mcp__srv__do-it\"}",
                  (int)sid.len, sid.ptr);
    hook(&m, body.ptr);
    CHECK(num(m.bus, "tools.mcp__srv__do_it") == 1);
    sk_buf_free(&body);
    unmake(&m);
    sk_arena_free(&A);
}

static bool main_chain_response(const sk_json *o)
{
    sk_str type;
    return sk_json_as_string(sk_json_get(o, "type"), &type) && sk_str_eq_c(type, "assistant") &&
           sk_json_get(sk_json_get(o, "message"), "usage") && !sk_claude_truthy(sk_json_get(o, "isSidechain"));
}

static void test_c4_the_transcript_counts_each_message_id_once(void)
{
    fixtures();
    made m = make();
    hook(&m, "{\"session_id\":\"s\",\"transcript_path\":\"T\",\"hook_event_name\":\"SessionStart\"}");
    for (size_t i = 0; i < N_TRANSCRIPT; i++)
        sk_claude_core_line(m.core, "T", TRANSCRIPT[i], true);
    /* The last output figure of each message id. */
    struct {
        sk_str id;
        double out;
    } per[64];
    size_t n_per = 0;
    const sk_json *last = NULL;
    for (size_t i = 0; i < N_TRANSCRIPT; i++) {
        const sk_json *o = TRANSCRIPT[i];
        if (!main_chain_response(o))
            continue;
        last = o;
        const sk_json *msg = sk_json_get(o, "message");
        sk_str id;
        double out;
        CHECK(sk_json_as_string(sk_json_get(msg, "id"), &id));
        CHECK(sk_json_as_number(sk_json_dig(msg, "usage", "output_tokens", NULL), &out));
        size_t k = 0;
        while (k < n_per && !sk_str_eq(per[k].id, id))
            k++;
        if (k == n_per && n_per < SK_LEN(per))
            per[n_per++].id = id;
        if (k < SK_LEN(per))
            per[k].out = out;
    }
    double out = 0;
    for (size_t k = 0; k < n_per; k++)
        out += per[k].out;
    CHECK(n_per > 1);
    CHECK(num(m.bus, "tokens.out") == out);
    const sk_val *model = sk_bus_get(m.bus, "model.name");
    CHECK(model && model->type == SK_VAL_STR && !strncmp(model->text, "claude-", 7));
    char *before = snapshot(m.bus);
    sk_claude_core_line(m.core, "T", last, false);
    char *after = snapshot(m.bus);
    CHECK_STR(after, before);
    free(before);
    free(after);
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c5_a_session_change_resets_the_family_keeps_events_seeds_at_once(void)
{
    fixtures();
    made m = make();
    hook(&m, "{\"session_id\":\"a\",\"transcript_path\":\"A\",\"hook_event_name\":\"SessionStart\"}");
    hook(&m, "{\"session_id\":\"a\",\"transcript_path\":\"A\",\"hook_event_name\":\"UserPromptSubmit\"}");
    hook(&m, "{\"session_id\":\"a\",\"transcript_path\":\"A\",\"hook_event_name\":\"PreToolUse\",\"tool_name\":\"Read\"}");
    hook(&m, "{\"session_id\":\"a\",\"transcript_path\":\"A\",\"hook_event_name\":\"SubagentStart\"}");
    hook(&m, "{\"session_id\":\"a\",\"transcript_path\":\"A\",\"hook_event_name\":\"PostCompact\"}");
    for (size_t i = 0; i < N_TRANSCRIPT; i++)
        sk_claude_core_line(m.core, "A", TRANSCRIPT[i], true);
    sk_claude_core_statusline(m.core, status_for("a", "A"));
    CHECK(num(m.bus, "tools.total") == 1);
    CHECK(num(m.bus, "tokens.out") > 0);
    hook(&m, "{\"session_id\":\"b\",\"transcript_path\":\"B\",\"hook_event_name\":\"SessionStart\"}");
    CHECK_STR(sk_claude_core_transcript(m.core), "B");
    CHECK_SNAPSHOT(m.bus,
                   "{\"event.prompt\":1,\"event.tool\":1,\"event.agent\":1,\"event.compact\":1,"
                   "\"model.name\":\"Opus 5.5\",\"limits.five_hour_pct\":1,\"limits.five_hour_resets_at\":1791787200,"
                   "\"limits.seven_day_pct\":0,\"limits.seven_day_resets_at\":1792357200,\"limits.items\":["
                   "{\"key\":\"five_hour\",\"label\":\"5h\",\"window\":\"5h\",\"model\":null,\"pct\":1,"
                   "\"resets_at\":1791787200},{\"key\":\"seven_day\",\"label\":\"7d\",\"window\":\"7d\","
                   "\"model\":null,\"pct\":0,\"resets_at\":1792357200}],"
                   "\"tools.total\":0,\"agents.active\":0,\"state.busy\":0,\"tools.rate\":0,\"tokens.out_rate\":0}");
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c6_a_stray_subagentstop_leaves_agents_active_at_0(void)
{
    sk_arena_init(&A, 0);
    made m = make();
    hook(&m, "{\"session_id\":\"s\",\"hook_event_name\":\"SubagentStop\"}");
    CHECK(num(m.bus, "agents.active") == 0);
    hook(&m, "{\"session_id\":\"s\",\"hook_event_name\":\"SubagentStart\"}");
    CHECK(num(m.bus, "agents.active") == 1);
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c7_replayed_lines_never_feed_the_output_rate_live_lines_do(void)
{
    fixtures();
    made m = make();
    hook(&m, "{\"session_id\":\"s\",\"transcript_path\":\"T\",\"hook_event_name\":\"SessionStart\"}");
    for (size_t i = 0; i < N_TRANSCRIPT; i++)
        sk_claude_core_line(m.core, "T", TRANSCRIPT[i], true);
    sk_bus_tick(m.bus, 1000);
    CHECK(num(m.bus, "tokens.out_rate") == 0);
    sk_claude_core_line(
        m.core, "T",
        json("{\"type\":\"assistant\",\"message\":{\"id\":\"msg_live\",\"usage\":{\"input_tokens\":1,\"output_tokens\":5000}}}"),
        false);
    sk_bus_tick(m.bus, 2000);
    CHECK(num(m.bus, "tokens.out_rate") == 500);
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c13_a_payload_of_the_wrong_shape_writes_nothing(void)
{
    sk_arena_init(&A, 0);
    const char *bad[] = {"null", "[]", "\"x\"", "{\"session_id\":{}}"};
    made m = make();
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        sk_claude_core_hook(m.core, json(bad[i]));
        sk_claude_core_statusline(m.core, json(bad[i]));
        sk_claude_core_line(m.core, NULL, json(bad[i]), false);
    }
    char *s = snapshot(m.bus);
    CHECK_STR(s, "{}");
    free(s);
    hook(&m, "{\"session_id\":\"s\",\"transcript_path\":\"T\",\"hook_event_name\":\"SessionStart\"}");
    char *before = snapshot(m.bus);
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        sk_claude_core_hook(m.core, json(bad[i]));
        sk_claude_core_statusline(m.core, json(bad[i]));
        sk_claude_core_line(m.core, "T", json(bad[i]), false);
    }
    char *after = snapshot(m.bus);
    CHECK_STR(after, before);
    CHECK_STR(sk_claude_core_transcript(m.core), "T");
    free(before);
    free(after);
    unmake(&m);
    sk_arena_free(&A);
}

static bool tokens_key(sk_str k)
{
    return sk_str_prefix(k, SK_STR("tokens."));
}

static bool context_key(sk_str k)
{
    return sk_str_prefix(k, SK_STR("context."));
}

static void test_c14_after_a_transcript_switch_lines_of_the_old_file_write_nothing(void)
{
    fixtures();
    made m = make();
    hook(&m, "{\"session_id\":\"s\",\"transcript_path\":\"A\",\"hook_event_name\":\"SessionStart\"}");
    hook(&m, "{\"session_id\":\"s\",\"transcript_path\":\"B\",\"hook_event_name\":\"UserPromptSubmit\"}");
    CHECK_STR(sk_claude_core_transcript(m.core), "B");
    for (size_t i = 0; i < N_TRANSCRIPT; i++)
        sk_claude_core_line(m.core, "A", TRANSCRIPT[i], true);
    char *tokens = set_of(m.bus, tokens_key), *context = set_of(m.bus, context_key);
    CHECK_STR(tokens, "tokens.out_rate=0\n");
    CHECK_STR(context, "");
    free(tokens);
    free(context);
    unmake(&m);
    sk_arena_free(&A);
}

/* Feeds the core as the adapter does: a payload, then, when it names a
 * new transcript of files, that transcript's history as replayed lines. */
static void feed(made *m, bool statusline, const sk_json *payload, const char *const *files)
{
    const char *t = sk_claude_core_transcript(m->core);
    char *before = t ? sk_xstrdup(t) : NULL;
    if (statusline)
        sk_claude_core_statusline(m->core, payload);
    else
        sk_claude_core_hook(m->core, payload);
    const char *now = sk_claude_core_transcript(m->core);
    bool changed = now && (!before || strcmp(before, now) != 0);
    for (size_t f = 0; changed && files[f]; f++)
        if (!strcmp(files[f], now))
            for (size_t i = 0; i < N_TRANSCRIPT; i++)
                sk_claude_core_line(m->core, now, TRANSCRIPT[i], true);
    free(before);
}

static bool claimed_key(sk_str k)
{
    return sk_str_prefix(k, SK_STR("context.")) || sk_str_eq_c(k, "model.name") || sk_str_eq_c(k, "cache.hit_ratio");
}

static void test_c8_statusline_and_transcript_in_any_order_give_one_context_family(void)
{
    fixtures();
    const char *files[] = {"T1", "T2", NULL};
    char *got[3];
    for (int order = 0; order < 3; order++) {
        sk_claude_facts f = {.hooks = true, .compaction = compaction(&S600, 1, NULL, NULL)};
        made m = make_with(&f);
        const sk_json *status = status_for("s1", "T1");
        if (order == 0) { /* A */
            feed(&m, true, status, files);
        } else if (order == 1) { /* B */
            feed(&m, false,
                 json("{\"session_id\":\"s1\",\"transcript_path\":\"T1\",\"hook_event_name\":\"SessionStart\","
                      "\"source\":\"resume\"}"),
                 files);
            feed(&m, true, status, files);
        } else { /* H */
            feed(&m, true, status, files);
            feed(&m, true, status_for("s2", "T2"), files);
        }
        CHECK(num(m.bus, "context.window_inferred") == 0);
        if (order == 0) {
            CHECK(num(m.bus, "context.window") == 1000000);
            const sk_val *model = sk_bus_get(m.bus, "model.name");
            CHECK(model && model->type == SK_VAL_STR && !strcmp(model->text, "Opus 5.5"));
        }
        got[order] = set_of(m.bus, claimed_key);
        unmake(&m);
    }
    CHECK_STR(got[1], got[0]);
    CHECK_STR(got[2], got[0]);
    for (int i = 0; i < 3; i++)
        free(got[i]);
    sk_arena_free(&A);
}

static void test_c9_a_statusline_without_prompt_cache_leaves_the_ratio_to_the_transcript(void)
{
    fixtures();
    made m = make();
    const char *files[] = {"T", NULL};
    feed(&m, false,
         json("{\"session_id\":\"s\",\"transcript_path\":\"T\",\"hook_event_name\":\"SessionStart\",\"source\":\"resume\"}"),
         files);
    double replayed = num(m.bus, "cache.hit_ratio");
    CHECK(!isnan(replayed));
    feed(&m, true, at(status_for("s", "T"), "prompt_cache", NULL), files);
    CHECK(num(m.bus, "cache.hit_ratio") == replayed);
    sk_claude_core_line(m.core, "T",
                        json("{\"type\":\"assistant\",\"message\":{\"id\":\"msg_live\",\"usage\":{\"input_tokens\":1000,"
                             "\"cache_creation_input_tokens\":0,\"cache_read_input_tokens\":0,\"output_tokens\":10}}}"),
                        false);
    CHECK(num(m.bus, "cache.hit_ratio") < replayed); /* a live line moves the ratio */
    unmake(&m);
    sk_arena_free(&A);
}

static void test_c10_statusline_off_the_inferred_window_follows_compact_window(void)
{
    fixtures();
    char *ac[] = {"--autocompact", "300000", NULL};
    struct {
        const char *name;
        sk_claude_compaction c;
        double window, threshold;
    } rows[] = {
        {"env", compaction(NULL, 0, NULL, "CLAUDE_CODE_AUTO_COMPACT_WINDOW=400000"), 400000, 367000},
        {"--autocompact", compaction(NULL, 0, ac, NULL), 300000, 267000},
        {"settings", compaction(&S600, 1, NULL, NULL), 600000, 567000},
        {"none", compaction(NULL, 0, NULL, NULL), 200000, 167000},
    };
    const char *files[] = {"T", NULL};
    for (size_t i = 0; i < SK_LEN(rows); i++) {
        sk_claude_facts f = {.hooks = true, .compaction = rows[i].c};
        made m = make_with(&f);
        feed(&m, false, json("{\"session_id\":\"s\",\"transcript_path\":\"T\",\"hook_event_name\":\"SessionStart\"}"),
             files);
        if (num(m.bus, "context.window") != rows[i].window ||
            num(m.bus, "context.compact_threshold") != rows[i].threshold ||
            num(m.bus, "context.window_inferred") != 1)
            sk_test_fail(__FILE__, __LINE__, "row %s", rows[i].name);
        unmake(&m);
    }
    sk_arena_free(&A);
}

static void test_c11_one_usage_reads_the_same_through_transcript_and_statusline(void)
{
    sk_arena_init(&A, 0);
    const char *u = "{\"input_tokens\":7,\"cache_creation_input_tokens\":1500,\"cache_read_input_tokens\":42000,"
                    "\"output_tokens\":321}";
    sk_buf s = {0}, l = {0};
    sk_buf_printf(&s, "{\"session_id\":\"s\",\"context_window\":{\"context_window_size\":200000,\"current_usage\":%s}}", u);
    sk_buf_printf(&l, "{\"type\":\"assistant\",\"message\":{\"id\":\"msg_1\",\"usage\":%s}}", u);
    made a = make(), b = make();
    sk_claude_core_statusline(a.core, json(s.ptr));
    hook(&b, "{\"session_id\":\"s\",\"transcript_path\":\"T\",\"hook_event_name\":\"SessionStart\"}");
    sk_claude_core_line(b.core, "T", json(l.ptr), false);
    const char *keys[] = {"context.used_tokens", "context.uncached", "context.cache_read", "context.cache_write",
                          "context.output"};
    const double want[] = {43507, 7, 42000, 1500, 321};
    for (size_t i = 0; i < SK_LEN(keys); i++) {
        CHECK(num(a.bus, keys[i]) == want[i]);
        CHECK(num(b.bus, keys[i]) == want[i]);
    }
    sk_buf_free(&s);
    sk_buf_free(&l);
    unmake(&a);
    unmake(&b);
    sk_arena_free(&A);
}

static void test_c12_without_injected_hooks_no_hook_meter_is_written(void)
{
    fixtures();
    const char *five[] = {"tools.total", "agents.active", "state.busy", "tools.rate", "tokens.out_rate"};
    sk_claude_facts f = {.hooks = false};
    made a = make_with(&f);
    for (size_t i = 0; i < N_HOOKS; i++)
        sk_claude_core_hook(a.core, HOOKS[i]);
    sk_bus_tick(a.bus, 1000);
    for (size_t i = 0; i < SK_LEN(five); i++)
        CHECK(sk_bus_get(a.bus, five[i]) == NULL);
    made b = make_with(&f);
    sk_claude_core_statusline(b.core, status_for("s", "T"));
    sk_bus_tick(b.bus, 1000);
    for (size_t i = 0; i < 4; i++)
        CHECK(sk_bus_get(b.bus, five[i]) == NULL);
    CHECK(num(b.bus, "tokens.out_rate") == 0);
    unmake(&a);
    unmake(&b);
    sk_arena_free(&A);
}

/* Logs of facts.statusline with hooks on after the given payloads. */
static int silent_logs(bool statusline, const char *const *payloads)
{
    logged = 0;
    sk_claude_facts f = {.hooks = true, .statusline = statusline, .log = log_line};
    made m = make_with(&f);
    for (size_t i = 0; payloads[i]; i++) {
        if (!strcmp(payloads[i], "status"))
            sk_claude_core_statusline(m.core, status_for("s1", NULL));
        else
            hook(&m, payloads[i]);
    }
    unmake(&m);
    return logged;
}

static void test_c15_a_silent_injected_statusline_is_logged_once_per_session(void)
{
    fixtures();
    const char *s1 = "{\"hook_event_name\":\"Stop\",\"session_id\":\"s1\"}";
    const char *s2 = "{\"hook_event_name\":\"Stop\",\"session_id\":\"s2\"}";
    const char *four[] = {s1, s1, s1, s1, NULL};
    CHECK_INT(silent_logs(true, four), 1);
    CHECK(strstr(last_log, "statusline") != NULL);
    const char *two_sessions[] = {s1, s1, s2, s2, NULL};
    CHECK_INT(silent_logs(true, two_sessions), 2);
    const char *seen[] = {"status", s1, s1, NULL};
    CHECK_INT(silent_logs(true, seen), 0);
    const char *off[] = {s1, s1, NULL};
    CHECK_INT(silent_logs(false, off), 0);
    sk_arena_free(&A);
}

static void test_c16_a_custom_title_names_the_session_a_new_session_clears_it(void)
{
    sk_arena_init(&A, 0);
    made m = make();
    sk_claude_core_enter_ids(m.core, "S1", "T");
    CHECK(sk_claude_core_title(m.core) == NULL);
    sk_claude_core_line(m.core, "T", json("{\"type\":\"custom-title\",\"customTitle\":\"other\",\"sessionId\":\"S2\"}"), true);
    CHECK(sk_claude_core_title(m.core) == NULL);
    sk_claude_core_line(m.core, "T", json("{\"type\":\"custom-title\",\"customTitle\":\"  math  \",\"sessionId\":\"S1\"}"),
                        true);
    CHECK_STR(sk_claude_core_title(m.core), "math");
    sk_claude_core_line(m.core, "T", json("{\"type\":\"custom-title\",\"customTitle\":\"later\"}"), false);
    CHECK_STR(sk_claude_core_title(m.core), "later");
    sk_claude_core_line(m.core, "T", json("{\"type\":\"custom-title\",\"customTitle\":\"\"}"), false);
    CHECK_STR(sk_claude_core_title(m.core), "later");
    sk_buf long_title = {0};
    sk_buf_append_c(&long_title, "{\"type\":\"custom-title\",\"customTitle\":\"");
    for (int i = 0; i < 200; i++)
        sk_buf_putc(&long_title, 'x');
    sk_buf_append_c(&long_title, "\"}");
    sk_claude_core_line(m.core, "T", json(long_title.ptr), false);
    CHECK_INT(strlen(sk_claude_core_title(m.core)), 120);
    sk_buf_free(&long_title);
    sk_claude_core_enter_ids(m.core, "S2", "T2");
    CHECK(sk_claude_core_title(m.core) == NULL);
    unmake(&m);
    sk_arena_free(&A);
}

/* Decision 0034: the checklist family follows the transcript and is
 * forgotten with the session. */
static const sk_json *todo(const char *todos, size_t n)
{
    sk_buf b = {0};
    sk_buf_printf(&b,
                  "{\"type\":\"assistant\",\"isSidechain\":false,\"message\":{\"id\":\"msg_todo_%zu\",\"usage\":"
                  "{\"input_tokens\":1,\"output_tokens\":20},\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_1\","
                  "\"name\":\"TodoWrite\",\"input\":{\"todos\":%s}}]}}",
                  n, todos);
    const sk_json *v = json(b.ptr);
    sk_buf_free(&b);
    return v;
}

static void test_c17_a_todowrite_writes_the_checklist_a_session_change_forgets_it(void)
{
    sk_arena_init(&A, 0);
    made m = make();
    hook(&m, "{\"session_id\":\"a\",\"transcript_path\":\"A\",\"hook_event_name\":\"SessionStart\"}");
    sk_claude_core_line(m.core, "A",
                        todo("[{\"content\":\"read\",\"status\":\"in_progress\"},{\"content\":\"wire\",\"status\":"
                             "\"pending\"},{\"content\":\"check\",\"status\":\"pending\"}]",
                             3),
                        true);
    const sk_val *items = sk_bus_get(m.bus, "checklist.items");
    CHECK(items && items->type == SK_VAL_LIST);
    if (items)
        CHECK_STR(items->text, "[{\"text\":\"read\",\"state\":\"doing\"},{\"text\":\"wire\",\"state\":\"todo\"},"
                               "{\"text\":\"check\",\"state\":\"todo\"}]");
    CHECK(num(m.bus, "checklist.count") == 3 && num(m.bus, "checklist.done") == 0);
    CHECK(num(m.bus, "tokens.out") == 20); /* the line's usage still counts */
    sk_claude_core_line(m.core, "B", todo("[]", 0), false);
    CHECK(num(m.bus, "checklist.count") == 3); /* a line of another file writes nothing */
    hook(&m, "{\"session_id\":\"b\",\"transcript_path\":\"B\",\"hook_event_name\":\"SessionStart\"}");
    CHECK(sk_bus_get(m.bus, "checklist.items") == NULL);
    CHECK(sk_bus_get(m.bus, "checklist.count") == NULL);
    unmake(&m);
    sk_arena_free(&A);
}

const sk_test sk_suite_claude_core[] = {
    T(test_c1_statusline_gives_context_cost_limits_model_and_cache),
    T(test_c2_a_null_current_usage_reads_as_no_data),
    T(test_c3_hooks_count_tools_by_type_and_track_busy_state),
    T(test_c4_the_transcript_counts_each_message_id_once),
    T(test_c5_a_session_change_resets_the_family_keeps_events_seeds_at_once),
    T(test_c6_a_stray_subagentstop_leaves_agents_active_at_0),
    T(test_c7_replayed_lines_never_feed_the_output_rate_live_lines_do),
    T(test_c13_a_payload_of_the_wrong_shape_writes_nothing),
    T(test_c14_after_a_transcript_switch_lines_of_the_old_file_write_nothing),
    T(test_c8_statusline_and_transcript_in_any_order_give_one_context_family),
    T(test_c9_a_statusline_without_prompt_cache_leaves_the_ratio_to_the_transcript),
    T(test_c10_statusline_off_the_inferred_window_follows_compact_window),
    T(test_c11_one_usage_reads_the_same_through_transcript_and_statusline),
    T(test_c12_without_injected_hooks_no_hook_meter_is_written),
    T(test_c15_a_silent_injected_statusline_is_logged_once_per_session),
    T(test_c16_a_custom_title_names_the_session_a_new_session_clears_it),
    T(test_c17_a_todowrite_writes_the_checklist_a_session_change_forgets_it),
    {0},
};
