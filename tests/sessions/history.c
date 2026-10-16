/* The sessions dashboard's history (decision 0028), as
 * test/history.test.js has it: series aligned to the ticks, the ring,
 * ended rows kept then dropped, the sums across sessions and the
 * per-minute rates, and the dials of decision 0033. */
#include "../../src/sessions/history.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>

/* One session's sample: values as "key=value" pairs, NULL-terminated. */
typedef struct {
    const char *key;
    const char *const *pairs;
    const char *session;
} entry;

/* Ticks h with entries, each with meta { label: key }. */
static void tick(sk_history *h, double now, const entry *entries, size_t n)
{
    sk_history_values vals[8] = {{0}};
    sk_history_sample s[8] = {{0}};
    sk_buf meta[8] = {{0}};
    for (size_t i = 0; i < n; i++) {
        for (const char *const *p = entries[i].pairs; p && *p; p++) {
            const char *eq = strchr(*p, '=');
            sk_history_value v = {sk_xstrndup(*p, (size_t)(eq - *p)), atof(eq + 1)};
            sk_vec_push(&vals[i], v);
        }
        sk_buf_printf(&meta[i], "{\"label\":\"%s\"}", entries[i].key);
        s[i] = (sk_history_sample){entries[i].key, &vals[i], meta[i].ptr, entries[i].session};
    }
    sk_history_tick(h, now, s, n);
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < vals[i].len; k++)
            free((char *)vals[i].ptr[k].key);
        sk_vec_free(&vals[i]);
        sk_buf_free(&meta[i]);
    }
}

/* The view, parsed, for the checks below. */
typedef struct {
    sk_arena arena;
    sk_buf text;
    const sk_json *v;
} view;

static void view_of(const sk_history *h, view *out)
{
    memset(out, 0, sizeof *out);
    sk_arena_init(&out->arena, 0);
    sk_jw w;
    sk_jw_init(&w, &out->text, 0);
    sk_history_view(h, &w);
    out->v = sk_json_parse(&out->arena, out->text.ptr, out->text.len, NULL);
    CHECK(out->v != NULL);
}

static void view_free(view *v)
{
    sk_arena_free(&v->arena);
    sk_buf_free(&v->text);
}

static char *json_of(const sk_json *v)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    if (v)
        sk_jw_value(&w, v);
    else
        sk_buf_append_c(&b, "undefined");
    return b.ptr;
}

#define CHECK_JSON(value, want)                                              \
    do {                                                                     \
        char *j_ = json_of(value);                                           \
        CHECK_STR(j_, want);                                                 \
        free(j_);                                                            \
    } while (0)

static const sk_json *row_of(const view *v, const char *key)
{
    const sk_json *rows = sk_json_get(v->v, "rows");
    for (size_t i = 0; rows && i < rows->array.len; i++) {
        sk_str k;
        if (sk_json_as_string(sk_json_get(&rows->array.items[i], "key"), &k) && sk_str_eq_c(k, key))
            return &rows->array.items[i];
    }
    return NULL;
}

static void test_a_session_that_starts_later_is_padded_one_that_ends_is_marked(void)
{
    sk_history *h = sk_history_new(0, 0, 5000);
    tick(h, 0, (entry[]){{"a", (const char *[]){"tools=1", NULL}, NULL}}, 1);
    tick(h, 5000, (entry[]){{"a", (const char *[]){"tools=2", NULL}, NULL}, {"b", (const char *[]){"tools=7", NULL}, NULL}}, 2);
    tick(h, 10000, (entry[]){{"b", (const char *[]){"tools=8", NULL}, NULL}}, 1);
    view v;
    view_of(h, &v);
    const sk_json *a = row_of(&v, "a"), *b = row_of(&v, "b");
    CHECK_JSON(sk_json_get(v.v, "ticks"), "[0,5000,10000]");
    CHECK_JSON(sk_json_dig(a, "series", "tools", NULL), "[1,2,null]");
    CHECK_JSON(sk_json_dig(b, "series", "tools", NULL), "[null,7,8]");
    CHECK_JSON(sk_json_get(a, "ended"), "true");
    CHECK_JSON(sk_json_get(b, "ended"), "false");
    CHECK_JSON(sk_json_get(a, "label"), "\"a\"");
    CHECK_JSON(sk_json_dig(a, "series", "cost", NULL), "[null,null,null]");
    /* The whole of a row, in the order JSON.stringify writes it. */
    CHECK_JSON(b, "{\"key\":\"b\",\"ended\":false,\"series\":{\"tools\":[null,7,8],\"tokens\":[null,null,null],"
                  "\"cost\":[null,null,null],\"context\":[null,null,null],\"compactions\":[null,null,null]},"
                  "\"label\":\"b\"}");
    view_free(&v);
    sk_history_free(h);
}

static void test_the_ring_keeps_the_last_points_ticks_an_ended_row_goes_after_keep_ended_ms(void)
{
    sk_history *h = sk_history_new(3, 10000, 5000);
    for (int i = 0; i < 5; i++) {
        char pair[32];
        snprintf(pair, sizeof pair, "tools=%d", i);
        tick(h, i * 5000, (entry[]){{"a", (const char *[]){pair, NULL}, NULL}}, 1);
    }
    view v;
    view_of(h, &v);
    CHECK_JSON(sk_json_get(v.v, "ticks"), "[10000,15000,20000]");
    CHECK_JSON(sk_json_dig(sk_json_at(sk_json_get(v.v, "rows"), 0), "series", "tools", NULL), "[2,3,4]");
    view_free(&v);
    tick(h, 25000, NULL, 0);
    tick(h, 35000, NULL, 0);
    CHECK_INT(sk_history_row_count(h), 1);
    tick(h, 40000, NULL, 0);
    CHECK_INT(sk_history_row_count(h), 0);
    CHECK_INT(sk_history_tick_count(h), 3);
    sk_history_free(h);
}

static void test_across_sessions_per_minute_rises_summed_never_scaled_cost_summed(void)
{
    sk_history *h = sk_history_new(0, 0, 30000);
    const char *a[3][4] = {{"tokens=100", "tools=1", "cost=0.5", NULL},
                           {"tokens=160", "tools=3", "cost=0.6", NULL},
                           {"tokens=20", "tools=4", "cost=0.7", NULL}};
    const char *b[3][3] = {{"tokens=10", "cost=1", NULL}, {"tokens=40", "cost=1", NULL}, {"tokens=90", "cost=2", NULL}};
    for (int i = 0; i < 3; i++)
        tick(h, i * 30000, (entry[]){{"a", a[i], NULL}, {"b", b[i], NULL}}, 2);
    view v;
    view_of(h, &v);
    const sk_json *across = sk_json_get(v.v, "across");
    CHECK_JSON(sk_json_get(across, "tokensPerMin"), "[0,90,80]");
    CHECK_JSON(sk_json_get(across, "toolsPerMin"), "[0,2,3]");
    const sk_json *cost = sk_json_get(across, "cost");
    const double want[] = {1.5, 1.6, 2.7};
    for (size_t i = 0; i < 3; i++) {
        double c = 0;
        CHECK(sk_json_as_number(sk_json_at(cost, i), &c));
        CHECK(round(c * 10) / 10 == want[i]);
    }
    view_free(&v);
    sk_history_free(h);
}

static double rate_at(const view *v, size_t i)
{
    double d = -1;
    CHECK(sk_json_as_number(sk_json_at(sk_json_dig(v->v, "across", "tokensPerMin", NULL), i), &d));
    return d;
}

static void test_a_burst_5_s_after_the_dashboard_opens_reads_its_own_size_for_a_minute(void)
{
    sk_history *h = sk_history_new(0, 0, 5000);
    double tokens[25] = {0, 2083};
    for (int i = 2; i <= 24; i++)
        tokens[i] = 2083 + floor((i - 1) * 1000.0 / 12 + 0.5);
    for (int i = 0; i < 25; i++) {
        char pair[32];
        snprintf(pair, sizeof pair, "tokens=%.0f", tokens[i]);
        tick(h, i * 5000, (entry[]){{"a", (const char *[]){pair, NULL}, NULL}}, 1);
    }
    view v;
    view_of(h, &v);
    CHECK(rate_at(&v, 1) == 2083);
    CHECK(rate_at(&v, 12) == tokens[12]);
    CHECK(rate_at(&v, 13) == tokens[13] - tokens[1]);
    CHECK(rate_at(&v, 24) == 1000);
    view_free(&v);
    sk_history_free(h);
}

static void test_a_new_session_in_the_same_row_starts_the_minute_again(void)
{
    sk_history *h = sk_history_new(0, 0, 5000);
    const char *steps[4][2] = {{"s1", "tokens=100"}, {"s1", "tokens=150"}, {"s2", "tokens=900000"}, {"s2", "tokens=900040"}};
    for (int i = 0; i < 4; i++)
        tick(h, i * 5000, (entry[]){{"a", (const char *[]){steps[i][1], NULL}, steps[i][0]}}, 1);
    view v;
    view_of(h, &v);
    CHECK_JSON(sk_json_dig(v.v, "across", "tokensPerMin", NULL), "[0,50,0,40]");
    view_free(&v);
    sk_history_free(h);
}

static char *values_json(const sk_history_values *vals)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    sk_jw_object(&w);
    for (size_t i = 0; i < vals->len; i++) {
        sk_jw_key(&w, sk_str_c(vals->ptr[i].key));
        sk_jw_number(&w, vals->ptr[i].value);
    }
    sk_jw_object_end(&w);
    return b.ptr;
}

static void test_pick_takes_the_dashboards_meters_from_a_bus_numbers_only(void)
{
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_source_spec spec = {0};
    sk_source *src = sk_bus_source(bus, &spec);
    sk_source_set_num(src, "tools.total", 3);
    sk_source_set_str(src, "tokens.out", SK_STR("x"));
    sk_source_set_null(src, "cost.usd");
    sk_source_set_num(src, "context.used_pct", 41.5);
    sk_source_set_str(src, "model.name", SK_STR("m"));
    sk_history_values vals = {0};
    sk_history_pick(bus, NULL, &vals);
    char *got = values_json(&vals);
    CHECK_STR(got, "{\"tools\":3,\"tokens\":null,\"cost\":null,\"context\":41.5,\"compactions\":null}");
    free(got);
    sk_vec_free(&vals);
    sk_bus_free(bus);
}

/* Decision 0033: a dial opened mid-run is a series of its own, null
 * before it opened, and gone when closed. */
static void sample_bus(sk_history *h, sk_bus *bus, double now)
{
    sk_history_values vals = {0};
    sk_history_pick(bus, sk_history_dials(h), &vals);
    sk_history_sample s = {"a", &vals, "{\"label\":\"a\"}", NULL};
    sk_history_tick(h, now, &s, 1);
    sk_vec_free(&vals);
}

static void test_a_dial_opened_mid_run_starts_as_no_data_and_goes_when_closed(void)
{
    sk_history *h = sk_history_new(0, 0, 5000);
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_source_spec spec = {0};
    sk_source *src = sk_bus_source(bus, &spec);
    sk_source_set_num(src, "tools.total", 3);
    sk_source_set_num(src, "tools.Read", 2);
    sample_bus(h, bus, 0);
    char *read[] = {"tools.Read", "tools.Read", "tools"};
    sk_history_set_dials(h, read, 3);
    sample_bus(h, bus, 5000);
    sk_source_set_num(src, "tools.Read", 5);
    sample_bus(h, bus, 10000);
    view v;
    view_of(h, &v);
    CHECK_JSON(sk_json_get(v.v, "dials"), "[\"tools.Read\"]");
    const sk_json *row = sk_json_at(sk_json_get(v.v, "rows"), 0);
    CHECK_JSON(sk_json_dig(row, "series", "tools.Read", NULL), "[null,2,5]");
    CHECK_JSON(sk_json_dig(row, "series", "tools", NULL), "[3,3,3]");
    view_free(&v);
    sk_history_set_dials(h, NULL, 0);
    sample_bus(h, bus, 15000);
    view_of(h, &v);
    row = sk_json_at(sk_json_get(v.v, "rows"), 0);
    CHECK_JSON(sk_json_get(v.v, "dials"), "[]");
    CHECK(sk_json_dig(row, "series", "tools.Read", NULL) == NULL);
    CHECK_JSON(sk_json_dig(row, "series", "tools", NULL), "[3,3,3,3]");
    view_free(&v);
    sk_bus_free(bus);
    sk_history_free(h);
}

const sk_test sk_suite_history[] = {
    T(test_a_session_that_starts_later_is_padded_one_that_ends_is_marked),
    T(test_the_ring_keeps_the_last_points_ticks_an_ended_row_goes_after_keep_ended_ms),
    T(test_across_sessions_per_minute_rises_summed_never_scaled_cost_summed),
    T(test_a_burst_5_s_after_the_dashboard_opens_reads_its_own_size_for_a_minute),
    T(test_a_new_session_in_the_same_row_starts_the_minute_again),
    T(test_pick_takes_the_dashboards_meters_from_a_bus_numbers_only),
    T(test_a_dial_opened_mid_run_starts_as_no_data_and_goes_when_closed),
    {0},
};
