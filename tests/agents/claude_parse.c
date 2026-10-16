/* Claude's payloads to metrics, as test/claude-parse.test.js has them:
 * the compaction rule, the hook metadata, the statusline's limits and
 * the checklist of decision 0034. */
#include "../../src/agents/claude/internal.h"
#include "../test.h"

#include <math.h>

static sk_arena arena;

static const sk_json *json(const char *text)
{
    const sk_json *v = sk_json_parse(&arena, text, strlen(text), NULL);
    if (!v)
        sk_test_fail(__FILE__, __LINE__, "bad JSON: %s", text);
    return v;
}

static const sk_json *fixture_status(void)
{
    char *text = sk_test_read("test/fixtures/claude/statusline.json", NULL);
    CHECK(text != NULL);
    const sk_json *v = json(text ? text : "{}");
    free(text);
    return v;
}

/* The fixture with rate_limits replaced by limits, given as JSON, or
 * removed when limits is NULL. */
static const sk_json *with_limits(const char *limits)
{
    const sk_json *base = fixture_status();
    sk_json *copy = sk_arena_alloc(&arena, sizeof *copy);
    *copy = *base;
    copy->object.members = sk_arena_array(&arena, base->object.len, sizeof *copy->object.members);
    copy->object.len = 0;
    for (size_t i = 0; i < base->object.len; i++) {
        sk_json_member m = base->object.members[i];
        if (sk_str_eq_c(m.key, "rate_limits")) {
            if (!limits)
                continue;
            m.value = *json(limits);
        }
        copy->object.members[copy->object.len++] = m;
    }
    return copy;
}

static double threshold(double window, const sk_claude_kept *sources, size_t n, char *const *argv,
                        const char *const *env)
{
    sk_envp e;
    sk_envp_from(&e, (char *const *)env);
    sk_claude_compaction c;
    sk_claude_compaction_of(&c, sources, n, argv, &e);
    sk_envp_free(&e);
    return sk_claude_compact_threshold(window, &c);
}

static void test_compact_threshold_follows_the_binary_rule(void)
{
    sk_claude_kept s600 = {.has_window = true, .window = 600000};
    sk_claude_kept off = {.env = {NULL, NULL, "true"}};
    const char *none[] = {NULL};
    const char *env400k[] = {"CLAUDE_CODE_AUTO_COMPACT_WINDOW=400k", NULL};
    const char *disabled[] = {"DISABLE_AUTO_COMPACT=1", NULL};
    char *ac[] = {"--autocompact", "300000", NULL}, *ac_eq[] = {"--autocompact=300000", NULL};
    CHECK(threshold(1000000, NULL, 0, NULL, none) == 967000);
    CHECK(threshold(200000, NULL, 0, NULL, none) == 167000);
    CHECK(threshold(1000000, &s600, 1, NULL, none) == 567000);
    CHECK(threshold(1000000, &s600, 1, NULL, env400k) == 367000);
    CHECK(threshold(1000000, NULL, 0, ac, none) == 267000);
    CHECK(threshold(1000000, NULL, 0, ac_eq, none) == 267000);
    CHECK(threshold(200000, &s600, 1, NULL, none) == 167000);
    CHECK(isnan(threshold(1000000, NULL, 0, NULL, disabled)));
    CHECK(isnan(threshold(1000000, &off, 1, NULL, none)));
    double t;
    CHECK(sk_claude_parse_tokens(SK_STR(" 1M "), &t) && t == 1000000);
    CHECK(sk_claude_parse_tokens(SK_STR("2.5k"), &t) && t == 2500);
    CHECK(!sk_claude_parse_tokens(SK_STR("1.k"), &t) && !sk_claude_parse_tokens(SK_STR("-5"), &t));
}

static void test_hook_metadata_drops_content_fields(void)
{
    sk_arena_init(&arena, 0);
    char *text = sk_test_read("test/fixtures/claude/hooks.jsonl", NULL);
    CHECK(text != NULL);
    int lines = 0;
    for (const char *line = text; text && *line;) {
        const char *nl = strchr(line, '\n');
        size_t n = nl ? (size_t)(nl - line) : strlen(line);
        const char *at = line;
        line += n + (nl ? 1 : 0);
        if (!n)
            continue;
        sk_buf one = {0};
        sk_buf_append(&one, at, n);
        /* The line with content fields added after its last member. */
        while (one.len && one.ptr[one.len - 1] != '}')
            one.len--;
        one.len--;
        sk_buf_append_c(&one, ", \"prompt\": \"secret\", \"tool_input\": {\"a\": 1}, \"tool_response\": \"x\", "
                              "\"last_assistant_message\": \"y\"}");
        const sk_json *h = json(one.ptr);
        const sk_json *m = sk_claude_hook_meta(&arena, h);
        const char *content[] = {"prompt", "tool_input", "tool_response", "last_assistant_message"};
        for (size_t k = 0; k < SK_LEN(content); k++)
            CHECK(sk_json_get(m, content[k]) == NULL);
        CHECK(sk_json_get(m, "hook_event_name") != NULL);
        CHECK(sk_json_get(m, "cwd") == NULL); /* not a field the app keeps */
        sk_buf_free(&one);
        lines++;
    }
    CHECK_INT(lines, 12);
    free(text);
    sk_arena_free(&arena);
}

static double kv_num(const sk_claude_kvs *kvs, const char *name)
{
    const sk_claude_kv *kv = sk_claude_kvs_get(kvs, name);
    return kv && kv->val.type == SK_VAL_NUM ? kv->val.num : NAN;
}

static bool kv_null(const sk_claude_kvs *kvs, const char *name)
{
    const sk_claude_kv *kv = sk_claude_kvs_get(kvs, name);
    return kv && kv->val.type == SK_VAL_NULL;
}

static const char *kv_text(const sk_claude_kvs *kvs, const char *name)
{
    const sk_claude_kv *kv = sk_claude_kvs_get(kvs, name);
    return kv && kv->val.text ? kv->val.text : NULL;
}

/* Every usage figure the statusline carries, as Claude Code 2.1.301
 * builds rate_limits: five_hour and seven_day with their resets, and a
 * gateway session's spend_limit. */
static void test_limits_resets_and_a_gateway_spend_limit_become_metrics(void)
{
    sk_arena_init(&arena, 0);
    sk_claude_compaction c = {0, false};
    const sk_json *j = with_limits("{\"five_hour\":{\"used_percentage\":1,\"resets_at\":1791787200},"
                                   "\"seven_day\":{\"used_percentage\":0,\"resets_at\":1792357200},"
                                   "\"spend_limit\":{\"used_percentage\":42.5,\"resets_at\":1792400000,"
                                   "\"used_usd\":85,\"limit_usd\":200,\"period\":\"month\"}}");
    sk_claude_kvs m = {0};
    sk_claude_from_statusline(j, &c, &m);
    CHECK(kv_num(&m, "limits.five_hour_pct") == 1 && kv_num(&m, "limits.five_hour_resets_at") == 1791787200);
    CHECK(kv_num(&m, "limits.seven_day_pct") == 0 && kv_num(&m, "limits.seven_day_resets_at") == 1792357200);
    CHECK(kv_num(&m, "limits.spend_pct") == 42.5 && kv_num(&m, "limits.spend_resets_at") == 1792400000);
    CHECK(kv_num(&m, "limits.spend_usd") == 85 && kv_num(&m, "limits.spend_limit_usd") == 200);
    CHECK_STR(kv_text(&m, "limits.items"),
              "[{\"key\":\"five_hour\",\"label\":\"5h\",\"window\":\"5h\",\"model\":null,\"pct\":1,"
              "\"resets_at\":1791787200},"
              "{\"key\":\"seven_day\",\"label\":\"7d\",\"window\":\"7d\",\"model\":null,\"pct\":0,"
              "\"resets_at\":1792357200},"
              "{\"key\":\"spend_limit\",\"label\":\"spend\",\"window\":null,\"model\":null,\"pct\":42.5,"
              "\"resets_at\":1792400000}]");
    sk_claude_kvs_free(&m);

    sk_claude_kvs none = {0};
    sk_claude_from_statusline(with_limits(NULL), &c, &none);
    CHECK(kv_null(&none, "limits.five_hour_resets_at"));
    CHECK(sk_claude_kvs_get(&none, "limits.spend_pct") == NULL);
    CHECK(kv_null(&none, "limits.items"));
    sk_claude_kvs_free(&none);
    sk_arena_free(&arena);
}

/* limits.items (decision 0011). Claude Code 2.1.301 sends no per-model
 * window: these are the shapes the parser takes when one arrives. */
static void test_every_window_becomes_an_item_the_accounts_first(void)
{
    sk_arena_init(&arena, 0);
    sk_claude_compaction c = {0, false};
    const sk_json *j = with_limits(
        "{\"seven_day_opus\":{\"used_percentage\":31,\"resets_at\":1792357200},\"five_hour\":null,"
        "\"seven_day\":{\"used_percentage\":0,\"resets_at\":1792357200},"
        "\"seven_day_overage_included\":{\"used_percentage\":7,\"resets_at\":1792357300,\"display_name\":\"Fable\"},"
        "\"odd\":{\"resets_at\":5}}");
    sk_claude_kvs m = {0};
    sk_claude_from_statusline(j, &c, &m);
    CHECK_STR(kv_text(&m, "limits.items"),
              "[{\"key\":\"seven_day\",\"label\":\"7d\",\"window\":\"7d\",\"model\":null,\"pct\":0,"
              "\"resets_at\":1792357200},"
              "{\"key\":\"seven_day_opus\",\"label\":\"Opus 7d\",\"window\":\"7d\",\"model\":\"Opus\",\"pct\":31,"
              "\"resets_at\":1792357200},"
              "{\"key\":\"seven_day_overage_included\",\"label\":\"Fable 7d\",\"window\":\"7d\",\"model\":\"Fable\","
              "\"pct\":7,\"resets_at\":1792357300}]");
    sk_claude_kvs_free(&m);
    sk_arena_free(&arena);
}

/* Decision 0034. The TaskCreate and TaskUpdate shapes are Claude Code
 * 2.1.284's, the TodoWrite shape the older tool's. */
static const sk_json *use(const char *name, const char *input, const char *id)
{
    sk_buf b = {0};
    sk_buf_printf(&b,
                  "{\"type\":\"assistant\",\"isSidechain\":false,\"message\":{\"content\":[{\"type\":\"tool_use\","
                  "\"id\":\"%s\",\"name\":\"%s\",\"input\":%s}]}}",
                  id ? id : "toolu_x", name, input);
    const sk_json *v = json(b.ptr);
    sk_buf_free(&b);
    return v;
}

static const sk_json *result(const char *id, const char *task)
{
    sk_buf b = {0};
    sk_buf_printf(&b,
                  "{\"type\":\"user\",\"isSidechain\":false,\"message\":{\"content\":[{\"type\":\"tool_result\","
                  "\"tool_use_id\":\"%s\",\"content\":\"ok\"}]},\"toolUseResult\":%s}",
                  id, task);
    const sk_json *v = json(b.ptr);
    sk_buf_free(&b);
    return v;
}

static const sk_json *text_line(const char *t)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    sk_buf_append_c(&b, "{\"type\":\"assistant\",\"isSidechain\":false,\"message\":{\"content\":[{\"type\":\"text\",\"text\":");
    sk_jw_string(&w, sk_str_c(t));
    sk_buf_append_c(&b, "}]}}");
    const sk_json *v = json(b.ptr);
    sk_buf_free(&b);
    return v;
}

#define CHECK_ITEMS(c, want)                                                 \
    do {                                                                     \
        sk_claude_kvs kvs_ = {0};                                            \
        sk_claude_checklist_metrics(c, &kvs_);                               \
        CHECK_STR(kv_text(&kvs_, "checklist.items"), want);                  \
        sk_claude_kvs_free(&kvs_);                                           \
    } while (0)

static void test_todowrite_replaces_the_list_whole_an_empty_one_clears_it(void)
{
    sk_arena_init(&arena, 0);
    sk_claude_checklist c;
    sk_claude_checklist_init(&c);
    CHECK(sk_claude_checklist_add(&c, use("TodoWrite",
                                          "{\"todos\":[{\"content\":\"read\",\"status\":\"in_progress\",\"activeForm\":"
                                          "\"reading\"},{\"content\":\"wire\",\"status\":\"pending\"},{\"content\":"
                                          "\"check\",\"status\":\"completed\"}]}",
                                          NULL)));
    CHECK_ITEMS(&c, "[{\"text\":\"read\",\"state\":\"doing\"},{\"text\":\"wire\",\"state\":\"todo\"},"
                    "{\"text\":\"check\",\"state\":\"done\"}]");
    sk_claude_kvs m = {0};
    sk_claude_checklist_metrics(&c, &m);
    CHECK(kv_num(&m, "checklist.count") == 3 && kv_num(&m, "checklist.done") == 1);
    sk_claude_kvs_free(&m);
    CHECK(sk_claude_checklist_add(&c, use("TodoWrite", "{\"todos\":[{\"content\":\"only\",\"status\":\"pending\"}]}",
                                          NULL)));
    CHECK_ITEMS(&c, "[{\"text\":\"only\",\"state\":\"todo\"}]");
    CHECK(sk_claude_checklist_add(&c, use("TodoWrite", "{\"todos\":[]}", NULL)));
    sk_claude_kvs e = {0};
    sk_claude_checklist_metrics(&c, &e);
    CHECK_STR(kv_text(&e, "checklist.items"), "[]");
    CHECK(kv_num(&e, "checklist.count") == 0 && kv_num(&e, "checklist.done") == 0);
    sk_claude_kvs_free(&e);
    sk_claude_checklist_free(&c);
    sk_arena_free(&arena);
}

static void test_taskcreate_adds_under_its_result_id_taskupdate_moves_renames_deletes(void)
{
    sk_arena_init(&arena, 0);
    sk_claude_checklist c;
    sk_claude_checklist_init(&c);
    CHECK(sk_claude_checklist_add(&c, use("TaskCreate", "{\"subject\":\"Map the orders\",\"description\":\"d\"}", "a")));
    CHECK(sk_claude_checklist_add(&c, use("TaskCreate", "{\"subject\":\"Cut them\",\"description\":\"d\"}", "b")));
    CHECK(!sk_claude_checklist_add(&c, result("a", "{\"task\":{\"id\":\"7\",\"subject\":\"Map the orders\"}}")));
    CHECK(!sk_claude_checklist_add(&c, result("b", "{\"task\":{\"id\":\"8\",\"subject\":\"Cut them\"}}")));
    CHECK(sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"7\",\"status\":\"in_progress\"}", NULL)));
    CHECK_ITEMS(&c, "[{\"text\":\"Map the orders\",\"state\":\"doing\"},{\"text\":\"Cut them\",\"state\":\"todo\"}]");
    CHECK(!sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"7\",\"status\":\"in_progress\"}", NULL)));
    CHECK(!sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"9\",\"status\":\"completed\"}", NULL)));
    CHECK(sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"8\",\"subject\":\"Cut the three\"}", NULL)));
    CHECK(sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"7\",\"status\":\"completed\"}", NULL)));
    CHECK(sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"8\",\"status\":\"deleted\"}", NULL)));
    CHECK_ITEMS(&c, "[{\"text\":\"Map the orders\",\"state\":\"done\"}]");
    sk_claude_checklist_free(&c);
    sk_arena_free(&arena);
}

static void test_taskcreate_without_a_result_is_numbered_in_order(void)
{
    sk_arena_init(&arena, 0);
    sk_claude_checklist c;
    sk_claude_checklist_init(&c);
    sk_claude_checklist_add(&c, use("TaskCreate", "{\"subject\":\"one\",\"description\":\"d\"}", "a"));
    sk_claude_checklist_add(&c, use("TaskCreate", "{\"subject\":\"two\",\"description\":\"d\"}", "b"));
    CHECK(sk_claude_checklist_add(&c, use("TaskUpdate", "{\"taskId\":\"2\",\"status\":\"completed\"}", NULL)));
    CHECK_ITEMS(&c, "[{\"text\":\"one\",\"state\":\"todo\"},{\"text\":\"two\",\"state\":\"done\"}]");
    sk_claude_checklist_free(&c);
    sk_arena_free(&arena);
}

static void test_markdown_task_list_replaces_the_list_other_text_and_sidechains_do_not(void)
{
    sk_arena_init(&arena, 0);
    sk_claude_checklist c;
    sk_claude_checklist_init(&c);
    CHECK(sk_claude_checklist_add(&c, text_line("The plan:\n\n- [ ] read the manifest\n* [x] wire the widget\n"
                                                "  - [X] run the checks\n- not a task\n")));
    CHECK_ITEMS(&c, "[{\"text\":\"read the manifest\",\"state\":\"todo\"},{\"text\":\"wire the widget\",\"state\":"
                    "\"done\"},{\"text\":\"run the checks\",\"state\":\"done\"}]");
    CHECK(!sk_claude_checklist_add(&c, text_line("No list here, [x] inline.")));
    CHECK(!sk_claude_checklist_add(
        &c, json("{\"type\":\"assistant\",\"isSidechain\":true,\"message\":{\"content\":[{\"type\":\"tool_use\","
                 "\"id\":\"t\",\"name\":\"TodoWrite\",\"input\":{\"todos\":[]}}]}}")));
    CHECK(!sk_claude_checklist_add(
        &c, json("{\"type\":\"assistant\",\"message\":{\"content\":[{\"type\":\"tool_use\",\"name\":\"TodoWrite\","
                 "\"input\":null}]}}")));
    CHECK(!sk_claude_checklist_add(&c, json("null")));
    CHECK_INT(c.items.len, 3);
    sk_buf long_line = {0};
    sk_buf_append_c(&long_line, "- [ ] ");
    for (int i = 0; i < 300; i++)
        sk_buf_putc(&long_line, 'x');
    CHECK(sk_claude_checklist_add(&c, text_line(long_line.ptr)));
    sk_claude_kvs m = {0};
    sk_claude_checklist_metrics(&c, &m);
    const char *items = kv_text(&m, "checklist.items");
    CHECK(items && strlen(items) == strlen("[{\"text\":\"\",\"state\":\"todo\"}]") + 200);
    sk_claude_kvs_free(&m);
    sk_buf_free(&long_line);
    sk_claude_checklist_free(&c);
    sk_arena_free(&arena);
}

/* String(), trim() and slice() as JavaScript counts: UTF-16 units. */
static void test_javascript_strings(void)
{
    sk_arena_init(&arena, 0);
    struct {
        const char *json, *string;
    } cases[] = {
        {"5", "5"}, {"1.5e300", "1.5e+300"}, {"true", "true"}, {"null", "null"},
        {"[1,null,[2,\"a\"]]", "1,,2,a"}, {"{\"a\":1}", "[object Object]"},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_buf b = {0};
        sk_claude_to_string(&b, json(cases[i].json));
        CHECK_STR(sk_buf_cstr(&b), cases[i].string);
        sk_buf_free(&b);
    }
    sk_buf u = {0};
    sk_claude_to_string(&u, NULL);
    CHECK_STR(u.ptr, "undefined");
    sk_buf_free(&u);
    CHECK_MEM(sk_claude_trim(SK_STR("\xC2\xA0 a b\t\xE2\x80\xA8")), "a b");
    CHECK_MEM(sk_claude_trim(SK_STR(" \n ")), "");
    sk_buf s = {0};
    sk_claude_slice(&s, SK_STR("ab\xF0\x9F\x98\x80" "c"), 3); /* the emoji is two units: cut in half */
    CHECK_STR(s.ptr, "ab\xEF\xBF\xBD");
    sk_buf_free(&s);
    sk_arena_free(&arena);
}

const sk_test sk_suite_claude_parse[] = {
    T(test_compact_threshold_follows_the_binary_rule),
    T(test_hook_metadata_drops_content_fields),
    T(test_limits_resets_and_a_gateway_spend_limit_become_metrics),
    T(test_every_window_becomes_an_item_the_accounts_first),
    T(test_todowrite_replaces_the_list_whole_an_empty_one_clears_it),
    T(test_taskcreate_adds_under_its_result_id_taskupdate_moves_renames_deletes),
    T(test_taskcreate_without_a_result_is_numbered_in_order),
    T(test_markdown_task_list_replaces_the_list_other_text_and_sidechains_do_not),
    T(test_javascript_strings),
    {0},
};
