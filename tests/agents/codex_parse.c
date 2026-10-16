/* Codex rollout lines to metrics, as test/codex-parse.test.js has it: the
 * recorded rollout, the adapter on a temporary CODEX_HOME, and the limits
 * of recorded token_count lines (decisions 0011, 0013). Expected values
 * are taken from the fixtures as the JavaScript test takes them. */
#include "../../src/agents/codex/codex.h"
#include "../../src/base/paths.h"
#include "../test.h"

#include <math.h>
#include <sys/stat.h>

typedef struct {
    sk_arena arena;
    SK_VEC(const sk_json *) lines;
    char *text;
} fixture;

static void load(fixture *f, const char *path)
{
    memset(f, 0, sizeof *f);
    sk_arena_init(&f->arena, 0);
    size_t n;
    f->text = sk_test_read(path, &n);
    CHECK(f->text != NULL);
    for (size_t from = 0; f->text && from < n;) {
        const char *nl = memchr(f->text + from, '\n', n - from);
        size_t end = nl ? (size_t)(nl - f->text) : n;
        if (end > from) {
            const sk_json *v = sk_json_parse(&f->arena, f->text + from, end - from, NULL);
            CHECK(v != NULL);
            if (v)
                sk_vec_push(&f->lines, v);
        }
        from = end + 1;
    }
}

static void unload(fixture *f)
{
    sk_vec_free(&f->lines);
    sk_arena_free(&f->arena);
    free(f->text);
}

static const char *const RATES[] = {"tokens.out_rate", "tools.rate"};
static const sk_source_spec SPEC = {NULL, 0, RATES, 2};

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

static double field(const sk_json *v, const char *a, const char *b)
{
    double d = NAN;
    sk_json_as_number(sk_json_dig(v, a, b, NULL), &d);
    return d;
}

static bool str_is(const sk_json *v, const char *s)
{
    sk_str got;
    return sk_json_as_string(v, &got) && sk_str_eq_c(got, s);
}

/* Every line through one state, as run() does: the values set, the
 * counters, and every event in order. */
typedef struct {
    sk_codex_state s;
    sk_bus *bus;
    SK_VEC(int) events;
} run_t;

static void run(run_t *r, const fixture *f)
{
    memset(r, 0, sizeof *r);
    r->bus = sk_bus_new(NULL, NULL);
    sk_source *src = sk_bus_source(r->bus, &SPEC);
    for (size_t i = 0; i < f->lines.len; i++) {
        sk_codex_state_add(&r->s, f->lines.ptr[i], src, false);
        int ev[64];
        size_t n = sk_bus_drain_events(r->bus, ev, SK_LEN(ev));
        for (size_t k = 0; k < n; k++)
            sk_vec_push(&r->events, ev[k]);
    }
}

static void run_free(run_t *r)
{
    sk_codex_state_free(&r->s);
    sk_bus_free(r->bus);
    sk_vec_free(&r->events);
}

static const char *ROLLOUT = "test/fixtures/codex/rollout.jsonl";
static const char *LIMITS = "test/fixtures/codex/limits.jsonl";

static void test_token_counts_follow_the_last_token_count_event(void)
{
    fixture f;
    load(&f, ROLLOUT);
    run_t r;
    run(&r, &f);
    const sk_json *last = NULL, *primary = NULL;
    for (size_t i = 0; i < f.lines.len; i++) {
        const sk_json *p = sk_json_get(f.lines.ptr[i], "payload");
        if (!str_is(sk_json_get(p, "type"), "token_count"))
            continue;
        if (sk_codex_truthy(sk_json_get(p, "info")))
            last = sk_json_get(p, "info");
        if (sk_codex_truthy(sk_json_dig(p, "rate_limits", "primary", NULL)))
            primary = sk_json_dig(p, "rate_limits", "primary", NULL);
    }
    CHECK(last && primary);
    CHECK(num(r.bus, "tokens.in") == field(last, "total_token_usage", "input_tokens"));
    CHECK(num(r.bus, "tokens.out") == field(last, "total_token_usage", "output_tokens"));
    CHECK(num(r.bus, "tokens.cache_read") == field(last, "total_token_usage", "cached_input_tokens"));
    double win = NAN;
    sk_json_as_number(sk_json_get(last, "model_context_window"), &win);
    CHECK(num(r.bus, "context.window") == win);
    double used = field(last, "last_token_usage", "input_tokens");
    CHECK(num(r.bus, "context.used_tokens") == used);
    CHECK(fabs(num(r.bus, "context.used_pct") - used / win * 100) < 1e-9);
    double minutes = NAN, pct = NAN, resets = NAN;
    sk_json_as_number(sk_json_get(primary, "window_minutes"), &minutes);
    sk_json_as_number(sk_json_get(primary, "used_percent"), &pct);
    sk_json_as_number(sk_json_get(primary, "resets_at"), &resets);
    CHECK(minutes == 10080);
    CHECK(num(r.bus, "limits.seven_day_pct") == pct);
    CHECK(num(r.bus, "limits.seven_day_resets_at") == resets);
    const sk_val *model = sk_bus_get(r.bus, "model.name");
    CHECK(model && model->type == SK_VAL_STR);
    CHECK_STR(model ? model->text : NULL, "gpt-6-astra");
    /* The values the filters above find in this fixture. */
    CHECK(num(r.bus, "tokens.in") == 25443068);
    CHECK(num(r.bus, "limits.seven_day_pct") == 63);
    run_free(&r);
    unload(&f);
}

static void test_tool_calls_are_counted_by_name_and_classed(void)
{
    fixture f;
    load(&f, ROLLOUT);
    run_t r;
    run(&r, &f);
    size_t tools = 0, exec = 0;
    for (size_t i = 0; i < f.lines.len; i++) {
        if (!str_is(sk_json_get(f.lines.ptr[i], "type"), "response_item"))
            continue;
        tools++;
        if (str_is(sk_json_dig(f.lines.ptr[i], "payload", "name", NULL), "exec"))
            exec++;
    }
    CHECK(num(r.bus, "tools.total") == (double)tools);
    CHECK(num(r.bus, "tools.exec") == (double)exec);
    size_t shell = 0;
    for (size_t i = 0; i + 1 < r.events.len; i += 2)
        if (r.events.ptr[i] == SK_EV_TOOL && r.events.ptr[i + 1] == 3)
            shell++;
    CHECK_INT(shell, exec);
    CHECK_INT(tools, 187);
    CHECK_INT(exec, 166);
    CHECK_INT(sk_codex_tool_class(SK_STR("apply_patch")), 2);
    CHECK_INT(sk_codex_tool_class(SK_STR("spawn_agent")), 5);
    run_free(&r);
    unload(&f);
}

static void test_turns_set_busy_and_session_meta_is_kept(void)
{
    fixture f;
    load(&f, ROLLOUT);
    run_t r;
    run(&r, &f);
    CHECK_STR(r.s.meta_source, "cli");
    size_t started = 0;
    for (size_t i = 0; i < f.lines.len; i++)
        if (str_is(sk_json_dig(f.lines.ptr[i], "payload", "type", NULL), "task_started"))
            started++;
    CHECK(num(r.bus, "event.prompt") == (double)started);
    CHECK_INT(started, 4);
    run_free(&r);
    unload(&f);
}

/* The tool classes, every branch of toolClass. */
static void test_tool_classes(void)
{
    struct {
        const char *name;
        int cls;
    } cases[] = {
        {"exec", 3},         {"shell", 3},       {"container.exec", 3}, {"unified_exec", 3},
        {"exec2", 0},        {"edit", 2},        {"write_file", 2},     {"grep", 1},
        {"search", 1},       {"web_search", 4},  {"web_fetch_x", 4},    {"list_agents", 5},
        {"respawn", 5},      {"wait", 0},        {"", 0},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++)
        CHECK_INT(sk_codex_tool_class(sk_str_c(cases[i].name)), cases[i].cls);
}

/* An adapter on a temp CODEX_HOME, launched as `codex resume <id>` so
 * discovery takes the argv route to the rollout written there. */
#define ID "01a12c5b-c2a0-7221-b9e6-000000000777"

typedef struct {
    sk_bus *bus;
    sk_envp env;
    sk_part part;
    char *home;
} adapter;

static char *const CMD[] = {"codex", "resume", ID, NULL};

static void adapter_for(adapter *a, bool with_rollout, const fixture *f)
{
    memset(a, 0, sizeof *a);
    a->home = sk_path_join(sk_test_tmpdir(), "codex-adapter");
    if (with_rollout) {
        char *d = sk_path_join(a->home, "sessions/2026/10/12");
        CHECK_INT(sk_mkdirs(d, 0700), 0);
        char *file = sk_path_join(d, "rollout-2026-10-12T10-00-00-" ID ".jsonl");
        sk_buf line = {0};
        sk_jw w;
        sk_jw_init(&w, &line, 0);
        sk_jw_value(&w, f->lines.ptr[0]);
        sk_buf_putc(&line, '\n');
        FILE *out = fopen(file, "wb");
        CHECK(out != NULL);
        if (out) {
            fwrite(line.ptr, 1, line.len, out);
            fclose(out);
        }
        sk_buf_free(&line);
        free(file);
        free(d);
    }
    a->bus = sk_bus_new(NULL, NULL);
    sk_envp_from(&a->env, NULL);
    sk_envp_set(&a->env, "CODEX_HOME", a->home);
    sk_agent_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cmd = CMD;
    ctx.cwd = "/work/project";
    ctx.env = &a->env;
    ctx.bus = a->bus;
    a->part = sk_provider_codex.create(&ctx);
}

static void adapter_free(adapter *a)
{
    a->part.ops->close(a->part.self);
    a->part.ops->free(a->part.self);
    sk_bus_free(a->bus);
    sk_envp_free(&a->env);
    free(a->home);
}

static void prepare(adapter *a)
{
    sk_strv args = {0};
    char *notice = NULL;
    CHECK_INT(a->part.ops->prepare(a->part.self, &args, &notice), 0);
    CHECK_INT(args.len, 0);
    CHECK(notice == NULL);
    sk_strv_free(&args);
    free(notice);
}

static const char *followed(adapter *a)
{
    sk_report r;
    a->part.ops->report(a->part.self, &r);
    return r.channels[0].target;
}

static void test_the_codex_adapter_has_no_defaults_before_it_follows_a_rollout(void)
{
    adapter a;
    adapter_for(&a, false, NULL);
    prepare(&a);
    a.part.ops->poll(a.part.self, sk_now_ms());
    CHECK(followed(&a) == NULL);
    const char *names[] = {"tools.total", "tools.rate", "agents.active", "state.busy",
                           "tokens.out_rate"};
    for (size_t i = 0; i < SK_LEN(names); i++)
        CHECK(sk_bus_get(a.bus, names[i]) == NULL);
    sk_report r;
    a.part.ops->report(a.part.self, &r);
    CHECK_STR(r.notice, "meters waiting: no rollout found");
    CHECK_INT(r.channels[0].state, SK_CH_WAITING);
    adapter_free(&a);
}

static void test_a_live_tool_call_raises_tools_rate_once_the_bus_ticks(void)
{
    fixture f;
    load(&f, ROLLOUT);
    adapter a;
    adapter_for(&a, true, &f);
    prepare(&a);
    uint64_t now = sk_now_ms();
    a.part.ops->poll(a.part.self, now);
    CHECK(followed(&a) != NULL);
    CHECK(num(a.bus, "tools.total") == 0);
    const char *tool = "{\"timestamp\":\"2026-10-12T16:00:00.000Z\",\"type\":\"response_item\","
                       "\"payload\":{\"type\":\"function_call\",\"name\":\"exec\","
                       "\"arguments\":\"{}\",\"call_id\":\"c1\"}}";
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *line = sk_json_parse(&arena, tool, strlen(tool), NULL);
    sk_codex_on_line(a.part.self, line, false);
    sk_bus_tick(a.bus, now);
    CHECK(num(a.bus, "tools.rate") == 1);
    CHECK(num(a.bus, "tools.total") == 1);
    sk_codex_on_line(a.part.self, line, true);
    sk_bus_tick(a.bus, now + 1);
    CHECK(num(a.bus, "tools.rate") == 1); /* a replayed call is no rate sample */
    sk_arena_free(&arena);
    adapter_free(&a);
    unload(&f);
}

/* The values feed() collects: one state, the last value of each name. */
static sk_bus *feed(const fixture *f, size_t from, size_t to)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *src = sk_bus_source(b, &SPEC);
    sk_codex_state s = {0};
    for (size_t i = from; i < to && i < f->lines.len; i++)
        sk_codex_state_add(&s, f->lines.ptr[i], src, false);
    sk_codex_state_free(&s);
    return b;
}

static const char *list(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_LIST ? v->text : NULL;
}

static void test_each_limit_id_is_its_own_limit(void)
{
    fixture f;
    load(&f, LIMITS);
    sk_bus *b = feed(&f, 0, 5);
    const sk_json *acc = sk_json_dig(f.lines.ptr[1], "payload", "rate_limits", "primary", NULL);
    const sk_json *spark = sk_json_dig(f.lines.ptr[3], "payload", "rate_limits", NULL);
    CHECK(str_is(sk_json_get(spark, "limit_name"), "GPT-5.3-Codex-Spark"));
    double pct = NAN, resets = NAN;
    sk_json_as_number(sk_json_get(acc, "used_percent"), &pct);
    sk_json_as_number(sk_json_get(acc, "resets_at"), &resets);
    CHECK(num(b, "limits.seven_day_pct") == pct);
    CHECK(num(b, "limits.seven_day_resets_at") == resets);
    CHECK(is_null(b, "limits.five_hour_pct"));
    /* acc: 10 % reset at 1789664314; spark: 0 % reset at 1789113304 (5 h)
     * and 1789700104 (7 d). */
    CHECK_STR(list(b, "limits.items"),
              "[{\"key\":\"codex.7d\",\"label\":\"7d\",\"window\":\"7d\",\"model\":null,"
              "\"pct\":10,\"resets_at\":1789664314},"
              "{\"key\":\"codex_bengalfox.5h\",\"label\":\"Spark 5h\",\"window\":\"5h\","
              "\"model\":\"GPT-5.3-Codex-Spark\",\"pct\":0,\"resets_at\":1789113304},"
              "{\"key\":\"codex_bengalfox.7d\",\"label\":\"Spark 7d\",\"window\":\"7d\","
              "\"model\":\"GPT-5.3-Codex-Spark\",\"pct\":0,\"resets_at\":1789700104}]");
    sk_bus_free(b);
    unload(&f);
}

static void test_a_30_day_window_is_its_own_item(void)
{
    fixture f;
    load(&f, LIMITS);
    sk_bus *b = feed(&f, 5, f.lines.len);
    double minutes = field(sk_json_dig(f.lines.ptr[5], "payload", "rate_limits", NULL), "primary",
                           "window_minutes");
    CHECK(minutes == 43200);
    CHECK(is_null(b, "limits.seven_day_pct"));
    CHECK_STR(list(b, "limits.items"), "[{\"key\":\"codex.30d\",\"label\":\"30d\",\"window\":\"30d\","
                                       "\"model\":null,\"pct\":0,\"resets_at\":1790039444}]");
    sk_bus_free(b);
    b = feed(&f, 4, 5);
    CHECK(is_null(b, "limits.items"));
    sk_bus_free(b);
    unload(&f);
}

/* Values Codex does not write, read as parse.js reads them: an array
 * payload, a number for a model, a tool with no name, an empty limit_id,
 * a negative window, a blank limit_name. */
static void test_odd_values_read_as_the_javascript_reads_them(void)
{
    const char *lines[] = {
        "{\"type\":\"event_msg\",\"payload\":[]}",
        "{\"type\":\"compacted\",\"payload\":[]}",
        "{\"type\":\"turn_context\",\"payload\":{\"model\":[\"a\",null,\"b\"]}}",
        "{\"type\":\"response_item\",\"payload\":{\"type\":\"function_call\"}}",
        "{\"type\":\"response_item\",\"payload\":{\"type\":\"local_shell_call\",\"name\":\"x\"}}",
        "{\"type\":\"response_item\",\"payload\":{\"type\":\"message\",\"name\":\"exec\"}}",
        "{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":"
        "{\"total_token_usage\":{\"input_tokens\":0}},\"rate_limits\":{\"limit_id\":\"\","
        "\"primary\":{\"window_minutes\":-60,\"used_percent\":5}}}}",
        "{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":"
        "{\"limit_id\":\"m1\",\"limit_name\":\"  \",\"primary\":{\"window_minutes\":90,"
        "\"used_percent\":1},\"secondary\":{\"window_minutes\":0,\"used_percent\":2}}}}",
        "[1,2]",
        "null",
    };
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *src = sk_bus_source(b, &SPEC);
    sk_codex_state s = {0};
    int ev[16];
    size_t n_ev = 0;
    for (size_t i = 0; i < SK_LEN(lines); i++) {
        sk_codex_state_add(&s, sk_json_parse(&a, lines[i], strlen(lines[i]), NULL), src, false);
        n_ev += sk_bus_drain_events(b, ev + n_ev, SK_LEN(ev) - n_ev);
    }
    CHECK(num(b, "event.compact") == 1);
    const sk_val *model = sk_bus_get(b, "model.name");
    CHECK_STR(model ? model->text : NULL, "a,,b");
    CHECK(num(b, "tools.function_call") == 1);
    CHECK(num(b, "tools.shell") == 1);
    CHECK(num(b, "tools.total") == 2);
    CHECK_INT(n_ev, 6); /* compact, then the two tools: none and shell */
    CHECK(n_ev == 6 && ev[0] == SK_EV_COMPACT && ev[3] == 0 && ev[5] == 3);
    CHECK(num(b, "tokens.in") == 0);
    CHECK(is_null(b, "cache.hit_ratio"));
    CHECK(num(b, "limits.five_hour_pct") == 5);
    CHECK(is_null(b, "limits.five_hour_resets_at"));
    CHECK_STR(list(b, "limits.items"),
              "[{\"key\":\"codex.null\",\"label\":\"codex.null\",\"window\":null,\"model\":null,"
              "\"pct\":5,\"resets_at\":null},"
              "{\"key\":\"m1.90m\",\"label\":\"m1 90m\",\"window\":\"90m\",\"model\":\"m1\","
              "\"pct\":1,\"resets_at\":null}]");
    sk_codex_state_free(&s);
    sk_bus_free(b);
    sk_arena_free(&a);
}

/* The value rules the parser keeps: String(), trim() and slice(). */
static void test_javascript_value_rules(void)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    struct {
        const char *json, *string;
        bool truthy;
    } cases[] = {
        {"\"gpt\"", "gpt", true},   {"\"\"", "", false},          {"0", "0", false},
        {"1.5", "1.5", true},       {"true", "true", true},       {"null", "null", false},
        {"{}", "[object Object]", true}, {"[1,null,[2,\"x\"]]", "1,,2,x", true}, {"[]", "", true},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        const sk_json *v = sk_json_parse(&a, cases[i].json, strlen(cases[i].json), NULL);
        sk_buf b = {0};
        sk_codex_js_string(&b, v);
        CHECK_STR(sk_buf_cstr(&b), cases[i].string);
        CHECK(sk_codex_truthy(v) == cases[i].truthy);
        sk_buf_free(&b);
    }
    CHECK_MEM(sk_codex_trim(SK_STR("\xC2\xA0 a b\t\xE2\x80\xA8")), "a b");
    CHECK_MEM(sk_codex_trim(SK_STR(" \n ")), "");
    sk_buf b = {0};
    sk_codex_slice16(&b, SK_STR("ab\xF0\x9F\x98\x80" "c"), 3); /* the emoji is two units */
    CHECK_STR(sk_buf_cstr(&b), "ab");
    sk_buf_clear(&b);
    sk_codex_slice16(&b, SK_STR("ab\xF0\x9F\x98\x80" "c"), 4);
    CHECK_STR(sk_buf_cstr(&b), "ab\xF0\x9F\x98\x80");
    sk_buf_free(&b);
    sk_arena_free(&a);
}

const sk_test sk_suite_codex_parse[] = {
    T(test_token_counts_follow_the_last_token_count_event),
    T(test_tool_calls_are_counted_by_name_and_classed),
    T(test_turns_set_busy_and_session_meta_is_kept),
    T(test_tool_classes),
    T(test_the_codex_adapter_has_no_defaults_before_it_follows_a_rollout),
    T(test_a_live_tool_call_raises_tools_rate_once_the_bus_ticks),
    T(test_each_limit_id_is_its_own_limit),
    T(test_a_30_day_window_is_its_own_item),
    T(test_odd_values_read_as_the_javascript_reads_them),
    T(test_javascript_value_rules),
    {0},
};
