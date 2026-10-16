/* The metric writer, as test/writer.test.js has it: sources, the rate
 * table on a fake clock, history, defaults, reset, the name check, close,
 * and the vocabulary and limits list beside it. */
#include "../../src/core/bus.h"
#include "../../src/core/limits.h"
#include "../../src/core/names.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

static const sk_metric_default CLAUDE_DEFAULTS[] = {
    {"tools.total", {SK_VAL_NUM, 0, NULL}},
    {"agents.active", {SK_VAL_NUM, 0, NULL}},
    {"state.busy", {SK_VAL_NUM, 0, NULL}},
};
static const char *const CLAUDE_RATES[] = {"tokens.out_rate", "tools.rate"};
static const sk_source_spec CLAUDE = {CLAUDE_DEFAULTS, 3, CLAUDE_RATES, 2};

static double num(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_NUM ? v->num : NAN;
}

/* The snapshot as compact JSON, to compare whole. */
static char *snapshot(const sk_bus *b)
{
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_bus_write(b, &w);
    return out.ptr;
}

#define CHECK_SNAPSHOT(b, want)                                              \
    do {                                                                     \
        char *s_ = snapshot(b);                                              \
        CHECK_STR(s_, want);                                                 \
        free(s_);                                                            \
    } while (0)

static void test_tokens_out_rate_is_the_sum_over_10_s_per_second(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    const char *const rates[] = {"tokens.out_rate"};
    sk_source_spec spec = {NULL, 0, rates, 1};
    sk_source *s = sk_bus_source(b, &spec);
    sk_source_begin(s);
    sk_source_sample(s, "tokens.out_rate", 5000, false);
    sk_bus_tick(b, 1000);
    CHECK(num(b, "tokens.out_rate") == 500);
    sk_bus_tick(b, 10999);
    CHECK(num(b, "tokens.out_rate") == 500);
    sk_bus_tick(b, 11000);
    CHECK(num(b, "tokens.out_rate") == 0);
    sk_bus_free(b);
}

static void test_tools_rate_counts_calls_in_the_last_60_s(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    const char *const rates[] = {"tools.rate"};
    sk_source_spec spec = {NULL, 0, rates, 1};
    sk_source *s = sk_bus_source(b, &spec);
    sk_source_begin(s);
    for (int i = 0; i < 3; i++)
        sk_source_sample(s, "tools.rate", 1, false);
    sk_bus_tick(b, 0);
    CHECK(num(b, "tools.rate") == 3);
    sk_source_sample(s, "tools.rate", 1, false);
    sk_bus_tick(b, 30000);
    CHECK(num(b, "tools.rate") == 4);
    sk_bus_tick(b, 60000);
    CHECK(num(b, "tools.rate") == 1);
    sk_bus_tick(b, 90000);
    CHECK(num(b, "tools.rate") == 0);
    sk_bus_free(b);
}

static void test_pty_windows_are_1_s(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    const char *const rates[] = {"pty.out_bps", "pty.in_kps"};
    sk_source_spec spec = {NULL, 0, rates, 2};
    sk_source *s = sk_bus_source(b, &spec);
    sk_source_begin(s);
    sk_source_sample(s, "pty.out_bps", 1000, false);
    sk_source_sample(s, "pty.out_bps", 2000, false);
    sk_source_sample(s, "pty.in_kps", 1, false);
    sk_source_sample(s, "pty.in_kps", 1, false);
    sk_bus_tick(b, 5000);
    CHECK(num(b, "pty.out_bps") == 3000);
    CHECK(num(b, "pty.in_kps") == 2);
    sk_bus_tick(b, 6000);
    CHECK(num(b, "pty.out_bps") == 0);
    CHECK(num(b, "pty.in_kps") == 0);
    sk_bus_free(b);
}

static void test_history_is_dropped_counters_still_apply(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(b, &CLAUDE);
    sk_source_begin(s);
    sk_source_sample(s, "tokens.out_rate", 5000, true);
    sk_source_event(s, SK_EV_TOOL, 1, true);
    sk_source_add(s, "tokens.out", 5000);
    sk_source_pulse(s, "event.tool");
    sk_bus_tick(b, 1000);
    int ev[8];
    CHECK(num(b, "tokens.out_rate") == 0);
    CHECK_INT(sk_bus_drain_events(b, ev, 8), 0);
    CHECK(num(b, "tokens.out") == 5000);
    CHECK(num(b, "event.tool") == 1);
    sk_source_event(s, SK_EV_TOOL, 1, false);
    CHECK_INT(sk_bus_drain_events(b, ev, 8), 2);
    CHECK(ev[0] == SK_EV_TOOL && ev[1] == 1);
    sk_bus_free(b);
}

static void test_defaults_absent_before_begin(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(b, &CLAUDE);
    sk_bus_tick(b, 1000);
    CHECK_SNAPSHOT(b, "{}");
    sk_source_begin(s);
    CHECK_SNAPSHOT(b, "{\"tools.total\":0,\"agents.active\":0,\"state.busy\":0,"
                      "\"tokens.out_rate\":0,\"tools.rate\":0}");
    sk_source_add(s, "tools.total", 2);
    sk_source_begin(s);
    CHECK(num(b, "tools.total") == 2);
    sk_bus_free(b);
}

static void test_reset_keeps_events_model_and_limits(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(b, &CLAUDE);
    sk_source_begin(s);
    sk_source_add(s, "tools.total", 3);
    sk_source_add(s, "tools.Read", 3);
    sk_source_set_num(s, "tokens.in", 10);
    sk_source_set_num(s, "context.window", 200000);
    sk_source_set_num(s, "session.compactions", 1);
    sk_source_pulse(s, "event.tool");
    sk_source_set_str(s, "model.name", SK_STR("Opus 5.5"));
    sk_source_set_num(s, "limits.five_hour_pct", 7);
    sk_source_sample(s, "tools.rate", 1, false);
    sk_source_sample(s, "tokens.out_rate", 5000, false);
    sk_bus_tick(b, 1000);
    CHECK(num(b, "tools.rate") == 1);
    sk_source_reset(s);
    /* The JavaScript Map keeps first-insertion order; the reseeded names
     * come back after the kept ones there too, as Object.fromEntries
     * would list them. Compare as a set of pairs. */
    CHECK_INT(sk_bus_count(b), 8);
    CHECK(num(b, "tools.total") == 0 && num(b, "agents.active") == 0 && num(b, "state.busy") == 0);
    CHECK(num(b, "tokens.out_rate") == 0 && num(b, "tools.rate") == 0);
    CHECK(num(b, "event.tool") == 1 && num(b, "limits.five_hour_pct") == 7);
    const sk_val *m = sk_bus_get(b, "model.name");
    CHECK(m && m->type == SK_VAL_STR && !strcmp(m->text, "Opus 5.5"));
    CHECK(!sk_bus_get(b, "tokens.in") && !sk_bus_get(b, "tools.Read"));
    sk_bus_tick(b, 2000);
    CHECK(num(b, "tools.rate") == 0);
    CHECK(num(b, "tokens.out_rate") == 0);
    sk_bus_free(b);
}

static int logged;
static char last_log[256];
static void log_line(void *ud, const char *msg)
{
    (void)ud;
    logged++;
    snprintf(last_log, sizeof last_log, "%s", msg);
}

static void test_unknown_names_are_dropped_and_logged_once(void)
{
    logged = 0;
    sk_bus *b = sk_bus_new(log_line, NULL);
    sk_source *s = sk_bus_source(b, NULL);
    sk_source_set_num(s, "tools.typo-name!", 1);
    sk_source_add(s, "tools.typo-name!", 1);
    sk_source_set_num(s, "tools.Read", 1);
    CHECK(!sk_bus_get(b, "tools.typo-name!"));
    CHECK(num(b, "tools.Read") == 1);
    CHECK_INT(logged, 1);
    CHECK(strstr(last_log, "tools.typo-name!") != NULL);
    sk_bus_free(b);
}

static void test_writes_after_close_are_dropped(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(b, &CLAUDE);
    sk_source_begin(s);
    sk_source_sample(s, "tokens.out_rate", 5000, false);
    sk_bus_tick(b, 1000);
    sk_source_close(s);
    sk_source_set_num(s, "tokens.in", 1);
    sk_source_add(s, "tools.total", 1);
    sk_source_pulse(s, "event.tool");
    sk_source_sample(s, "tokens.out_rate", 5000, false);
    sk_source_event(s, SK_EV_TOOL, 1, false);
    sk_source_reset(s);
    sk_bus_tick(b, 20000);
    CHECK_SNAPSHOT(b, "{\"tools.total\":0,\"agents.active\":0,\"state.busy\":0,"
                      "\"tokens.out_rate\":500,\"tools.rate\":0}");
    int ev[4];
    CHECK_INT(sk_bus_drain_events(b, ev, 4), 0);
    sk_bus_free(b);
}

static void test_event_feed_keeps_the_newest_512(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(b, NULL);
    for (int i = 0; i < 300; i++)
        sk_source_event(s, SK_EV_OUTPUT, i, false);
    static int ev[1024];
    CHECK_INT(sk_bus_drain_events(b, ev, 1024), 512);
    CHECK_INT(ev[1], 44); /* 300 pairs, the oldest 44 dropped */
    CHECK_INT(ev[511], 299);
    sk_bus_free(b);
}

static void test_lists_and_strings_in_the_snapshot(void)
{
    sk_bus *b = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(b, NULL);
    sk_source_set_list(s, "checklist.items", SK_STR("[{\"text\":\"a\",\"state\":\"done\"}]"));
    sk_source_set_str(s, "model.name", SK_STR("Opus \"5.5\""));
    sk_source_set_null(s, "cost.usd");
    CHECK_SNAPSHOT(b, "{\"checklist.items\":[{\"text\":\"a\",\"state\":\"done\"}],"
                      "\"model.name\":\"Opus \\\"5.5\\\"\",\"cost.usd\":null}");
    sk_bus_free(b);
}

static void test_vocabulary(void)
{
    CHECK(sk_metric_known(SK_STR("context.used_pct")));
    CHECK(sk_metric_known(SK_STR("tools.Read")));
    CHECK(sk_metric_known(SK_STR("tools.mcp__github__list_issues")));
    CHECK(!sk_metric_known(SK_STR("tools.")));
    CHECK(!sk_metric_known(SK_STR("tools.a-b")));
    CHECK(!sk_metric_known(SK_STR("context.nope")));
    CHECK(sk_metric_is_list(SK_STR("limits.items")));
    CHECK(!sk_metric_is_list(SK_STR("limits.five_hour_pct")));
    struct {
        const char *tool, *key;
    } keys[] = {
        {"Read", "tools.Read"},
        {"mcp__srv__do-it", "tools.mcp__srv__do_it"},
        {"caf\xC3\xA9", "tools.caf_"},
        {"x\xF0\x9F\x98\x80y", "tools.x__y"}, /* two UTF-16 units, two _ */
    };
    for (size_t i = 0; i < SK_LEN(keys); i++) {
        sk_buf k = {0};
        sk_metric_tool_key(&k, sk_str_c(keys[i].tool));
        CHECK_STR(k.ptr, keys[i].key);
        sk_buf_free(&k);
    }
    sk_buf long_key = {0};
    sk_metric_tool_key(&long_key, sk_str_c("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    CHECK_INT(long_key.len, 6 + 64);
    sk_buf_free(&long_key);
}

static void test_limits_list(void)
{
    struct {
        double minutes;
        const char *span;
    } spans[] = {{300, "5h"}, {10080, "7d"}, {43200, "30d"}, {90, "90m"}, {1440, "1d"}};
    for (size_t i = 0; i < SK_LEN(spans); i++) {
        sk_buf b = {0};
        CHECK(sk_limit_span(spans[i].minutes, &b));
        CHECK_STR(b.ptr, spans[i].span);
        sk_buf_free(&b);
    }
    sk_buf none = {0};
    CHECK(!sk_limit_span(0, &none) && !sk_limit_span(NAN, &none) && none.len == 0);

    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_source *s = sk_bus_source(bus, NULL);
    sk_limits l = {{0}, 0};
    sk_limits_set(&l, s);
    CHECK(sk_bus_get(bus, "limits.items") && sk_bus_get(bus, "limits.items")->type == SK_VAL_NULL);
    sk_limits_add(&l, "five_hour", "5h", NULL, NULL, 12.5, 1760000000);
    sk_limits_add(&l, "spark", "5h", "Spark", NULL, 3, NAN);
    sk_limits_add(&l, "none", "7d", NULL, NULL, NAN, 0); /* no reading: no item */
    sk_limits_add(&l, "spend", NULL, NULL, "Spend", 40, NAN);
    sk_limits_set(&l, s);
    const sk_val *v = sk_bus_get(bus, "limits.items");
    CHECK(v && v->type == SK_VAL_LIST);
    if (v)
        CHECK_STR(v->text,
                  "[{\"key\":\"five_hour\",\"label\":\"5h\",\"window\":\"5h\",\"model\":null,\"pct\":12.5,"
                  "\"resets_at\":1760000000},"
                  "{\"key\":\"spark\",\"label\":\"Spark 5h\",\"window\":\"5h\",\"model\":\"Spark\",\"pct\":3,"
                  "\"resets_at\":null},"
                  "{\"key\":\"spend\",\"label\":\"Spend\",\"window\":null,\"model\":null,\"pct\":40,"
                  "\"resets_at\":null}]");
    sk_limits_free(&l);
    sk_bus_free(bus);
}

const sk_test sk_suite_bus[] = {
    T(test_tokens_out_rate_is_the_sum_over_10_s_per_second),
    T(test_tools_rate_counts_calls_in_the_last_60_s),
    T(test_pty_windows_are_1_s),
    T(test_history_is_dropped_counters_still_apply),
    T(test_defaults_absent_before_begin),
    T(test_reset_keeps_events_model_and_limits),
    T(test_unknown_names_are_dropped_and_logged_once),
    T(test_writes_after_close_are_dropped),
    T(test_event_feed_keeps_the_newest_512),
    T(test_lists_and_strings_in_the_snapshot),
    T(test_vocabulary),
    T(test_limits_list),
    {0},
};
