/* Ports app/history.js: decisions 0028 and 0033. No data is NAN inside,
 * null in the view. */
#include "history.h"

#include "../base/strv.h"

#include <math.h>
#include <string.h>

const sk_history_metric sk_history_metrics[SK_HISTORY_METRICS] = {
    {"tools", "tools.total"},     {"tokens", "tokens.out"}, {"cost", "cost.usd"},
    {"context", "context.used_pct"}, {"compactions", "session.compactions"},
};

typedef SK_VEC(double) doubles;

typedef struct {
    char *name;
    doubles v;
} series;

typedef struct {
    char *key, *meta, *session;
    bool ended;
    double ended_at;
    SK_VEC(bool) breaks;
    SK_VEC(series) series; /* in the order JavaScript keeps the keys */
} row;

struct sk_history {
    size_t points;
    double keep_ended_ms, interval_ms;
    doubles ticks;
    SK_VEC(row) rows;
    sk_strv dials;
};

static double num_of(const sk_val *v)
{
    return v && v->type == SK_VAL_NUM && isfinite(v->num) ? v->num : NAN;
}

void sk_history_pick(const sk_bus *bus, char *const *dials, sk_history_values *out)
{
    for (size_t i = 0; i < SK_HISTORY_METRICS; i++) {
        sk_history_value v = {sk_history_metrics[i].key, num_of(sk_bus_get(bus, sk_history_metrics[i].name))};
        sk_vec_push(out, v);
    }
    for (; dials && *dials; dials++) {
        sk_history_value v = {*dials, num_of(sk_bus_get(bus, *dials))};
        sk_vec_push(out, v);
    }
}

sk_history *sk_history_new(size_t points, double keep_ended_ms, double interval_ms)
{
    sk_history *h = sk_xcalloc(1, sizeof *h);
    h->points = points ? points : SK_HISTORY_POINTS;
    h->keep_ended_ms = keep_ended_ms ? keep_ended_ms : SK_HISTORY_KEEP_ENDED_MS;
    h->interval_ms = interval_ms ? interval_ms : SK_HISTORY_INTERVAL_MS;
    return h;
}

static void series_free(series *s)
{
    free(s->name);
    sk_vec_free(&s->v);
}

static void row_free(row *r)
{
    free(r->key);
    free(r->meta);
    free(r->session);
    sk_vec_free(&r->breaks);
    for (size_t i = 0; i < r->series.len; i++)
        series_free(&r->series.ptr[i]);
    sk_vec_free(&r->series);
}

void sk_history_free(sk_history *h)
{
    if (!h)
        return;
    for (size_t i = 0; i < h->rows.len; i++)
        row_free(&h->rows.ptr[i]);
    sk_vec_free(&h->rows);
    sk_vec_free(&h->ticks);
    sk_strv_free(&h->dials);
    free(h);
}

static bool is_metric(const char *name)
{
    for (size_t i = 0; i < SK_HISTORY_METRICS; i++)
        if (!strcmp(name, sk_history_metrics[i].key))
            return true;
    return false;
}

static series *series_of(row *r, const char *name)
{
    for (size_t i = 0; i < r->series.len; i++)
        if (!strcmp(r->series.ptr[i].name, name))
            return &r->series.ptr[i];
    return NULL;
}

/* A new series of n ticks of no data at the end of r's. */
static series *add_series(row *r, const char *name, size_t n)
{
    series s = {sk_xstrdup(name), {0}};
    for (size_t i = 0; i < n; i++)
        sk_vec_push(&s.v, NAN);
    sk_vec_push(&r->series, s);
    return &r->series.ptr[r->series.len - 1];
}

static bool listed(char *const *names, size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(names[i], name))
            return true;
    return false;
}

void sk_history_set_dials(sk_history *h, char *const *names, size_t n)
{
    sk_strv next = {0};
    for (size_t i = 0; i < n; i++)
        if (!is_metric(names[i]) && !listed(next.ptr, next.len, names[i]))
            sk_strv_push(&next, names[i]);
    for (size_t k = 0; k < h->rows.len; k++) {
        row *r = &h->rows.ptr[k];
        for (size_t i = 0; i < h->dials.len; i++) {
            series *s = series_of(r, h->dials.ptr[i]);
            if (s && !listed(next.ptr, next.len, s->name)) {
                size_t at = (size_t)(s - r->series.ptr);
                series_free(s);
                memmove(s, s + 1, (r->series.len - at - 1) * sizeof *s);
                r->series.len--;
            }
        }
        for (size_t i = 0; i < next.len; i++)
            if (!series_of(r, next.ptr[i]))
                add_series(r, next.ptr[i], h->ticks.len);
    }
    sk_strv_free(&h->dials);
    h->dials = next;
}

char *const *sk_history_dials(sk_history *h)
{
    return sk_strv_argv(&h->dials);
}

/* The keys every row keeps: the metrics, then the dials. */
static const char *key_at(const sk_history *h, size_t i)
{
    return i < SK_HISTORY_METRICS ? sk_history_metrics[i].key : h->dials.ptr[i - SK_HISTORY_METRICS];
}

static size_t key_count(const sk_history *h)
{
    return SK_HISTORY_METRICS + h->dials.len;
}

static double value_of(const sk_history_values *vals, const char *key)
{
    for (size_t i = 0; vals && i < vals->len; i++)
        if (!strcmp(vals->ptr[i].key, key))
            return isfinite(vals->ptr[i].value) ? vals->ptr[i].value : NAN;
    return NAN;
}

static row *row_of(sk_history *h, const char *key)
{
    for (size_t i = 0; i < h->rows.len; i++)
        if (!strcmp(h->rows.ptr[i].key, key))
            return &h->rows.ptr[i];
    return NULL;
}

static bool sampled(const sk_history_sample *samples, size_t n, const char *key)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(samples[i].key, key))
            return true;
    return false;
}

void sk_history_tick(sk_history *h, double now, const sk_history_sample *samples, size_t n)
{
    sk_vec_push(&h->ticks, now);
    size_t before = h->ticks.len - 1;
    for (size_t i = 0; i < n; i++) {
        const sk_history_sample *s = &samples[i];
        row *r = row_of(h, s->key);
        if (!r || r->ended) {
            /* A session that ended and came back is a new row in the
             * old one's place, as Map.set keeps a key's place. */
            if (r) {
                row_free(r);
            } else {
                sk_vec_push(&h->rows, (row){0});
                r = &h->rows.ptr[h->rows.len - 1];
            }
            memset(r, 0, sizeof *r);
            r->key = sk_xstrdup(s->key);
            for (size_t k = 0; k < before; k++)
                sk_vec_push(&r->breaks, false);
            for (size_t k = 0; k < key_count(h); k++)
                add_series(r, key_at(h, k), before);
        }
        free(r->meta);
        r->meta = s->meta ? sk_xstrdup(s->meta) : NULL;
        bool brk = s->session && *s->session && r->session && strcmp(s->session, r->session);
        sk_vec_push(&r->breaks, brk);
        if (s->session && *s->session) {
            free(r->session);
            r->session = sk_xstrdup(s->session);
        }
        for (size_t k = 0; k < key_count(h); k++) {
            const char *key = key_at(h, k);
            series *to = series_of(r, key);
            sk_vec_push(&to->v, value_of(s->values, key));
        }
    }
    for (size_t i = 0; i < h->rows.len;) {
        row *r = &h->rows.ptr[i];
        if (sampled(samples, n, r->key)) {
            i++;
            continue;
        }
        if (!r->ended) {
            r->ended = true;
            r->ended_at = now;
        }
        if (now - r->ended_at > h->keep_ended_ms) {
            row_free(r);
            memmove(r, r + 1, (h->rows.len - i - 1) * sizeof *r);
            h->rows.len--;
            continue;
        }
        sk_vec_push(&r->breaks, false);
        for (size_t k = 0; k < key_count(h); k++) {
            series *to = series_of(r, key_at(h, k));
            sk_vec_push(&to->v, NAN);
        }
        i++;
    }
    if (h->ticks.len > h->points) {
        size_t cut = h->ticks.len - h->points;
        memmove(h->ticks.ptr, h->ticks.ptr + cut, h->points * sizeof *h->ticks.ptr);
        h->ticks.len = h->points;
        for (size_t i = 0; i < h->rows.len; i++) {
            row *r = &h->rows.ptr[i];
            size_t keep = r->breaks.len > cut ? r->breaks.len - cut : 0;
            memmove(r->breaks.ptr, r->breaks.ptr + (r->breaks.len - keep), keep * sizeof *r->breaks.ptr);
            r->breaks.len = keep;
            for (size_t k = 0; k < r->series.len; k++) {
                doubles *v = &r->series.ptr[k].v;
                size_t left = v->len > cut ? v->len - cut : 0;
                memmove(v->ptr, v->ptr + (v->len - left), left * sizeof *v->ptr);
                v->len = left;
            }
        }
    }
}

/* Each of the len ticks' rise of s over the last minute, into out. */
static void per_minute(const sk_history *h, const series *s, const row *r, double *out, size_t len)
{
    double ticks = floor(60000 / h->interval_ms + 0.5);
    size_t n = ticks >= 1 ? (size_t)ticks : 1;
    size_t from = 0;
    for (size_t i = 0; i < len; i++) {
        if (i < r->breaks.len && r->breaks.ptr[i])
            from = i;
        out[i] = 0;
        if (i >= s->v.len || isnan(s->v.ptr[i]))
            continue;
        size_t j = SK_MAX(from, i >= n ? i - n : 0);
        while (j < i && isnan(s->v.ptr[j]))
            j++;
        if (j < i)
            out[i] = SK_MAX(0, s->v.ptr[i] - s->v.ptr[j]);
    }
}

static void write_numbers(sk_jw *w, const double *v, size_t n)
{
    sk_jw_array(w);
    for (size_t i = 0; i < n; i++)
        sk_jw_number(w, v[i]);
    sk_jw_array_end(w);
}

/* { key, ended, series } with meta's members assigned over it, as
 * Object.assign does: a member of the same name takes its place. */
static void write_row(sk_jw *w, const row *r)
{
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *meta = r->meta ? sk_json_parse(&arena, r->meta, strlen(r->meta), NULL) : NULL;
    if (meta && meta->type != SK_JSON_OBJECT)
        meta = NULL;
    const sk_json *v;
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("key"));
    if ((v = sk_json_get(meta, "key")))
        sk_jw_value(w, v);
    else
        sk_jw_string(w, sk_str_c(r->key));
    sk_jw_key(w, SK_STR("ended"));
    if ((v = sk_json_get(meta, "ended")))
        sk_jw_value(w, v);
    else
        sk_jw_bool(w, r->ended);
    sk_jw_key(w, SK_STR("series"));
    if ((v = sk_json_get(meta, "series"))) {
        sk_jw_value(w, v);
    } else {
        sk_jw_object(w);
        for (size_t k = 0; k < r->series.len; k++) {
            sk_jw_key(w, sk_str_c(r->series.ptr[k].name));
            write_numbers(w, r->series.ptr[k].v.ptr, r->series.ptr[k].v.len);
        }
        sk_jw_object_end(w);
    }
    for (size_t i = 0; meta && i < meta->object.len; i++) {
        const sk_json_member *m = &meta->object.members[i];
        if (sk_str_eq_c(m->key, "key") || sk_str_eq_c(m->key, "ended") || sk_str_eq_c(m->key, "series"))
            continue;
        sk_jw_key(w, m->key);
        sk_jw_value(w, &m->value);
    }
    sk_jw_object_end(w);
    sk_arena_free(&arena);
}

void sk_history_view(const sk_history *h, sk_jw *w)
{
    size_t n = h->ticks.len;
    double *tok = sk_xcalloc(n + 1, sizeof *tok), *tools = sk_xcalloc(n + 1, sizeof *tools);
    double *across_tok = sk_xcalloc(n + 1, sizeof *tok), *across_tools = sk_xcalloc(n + 1, sizeof *tok),
           *across_cost = sk_xcalloc(n + 1, sizeof *tok);
    for (size_t k = 0; k < h->rows.len; k++) {
        row *r = &h->rows.ptr[k];
        per_minute(h, series_of(r, "tokens"), r, tok, n);
        per_minute(h, series_of(r, "tools"), r, tools, n);
        const series *cost = series_of(r, "cost");
        for (size_t i = 0; i < n; i++) {
            across_tok[i] += tok[i];
            across_tools[i] += tools[i];
            if (i < cost->v.len && !isnan(cost->v.ptr[i]))
                across_cost[i] += cost->v.ptr[i];
        }
    }
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("ticks"));
    write_numbers(w, h->ticks.ptr, n);
    sk_jw_key(w, SK_STR("intervalMs"));
    sk_jw_number(w, h->interval_ms);
    sk_jw_key(w, SK_STR("across"));
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("tokensPerMin"));
    write_numbers(w, across_tok, n);
    sk_jw_key(w, SK_STR("toolsPerMin"));
    write_numbers(w, across_tools, n);
    sk_jw_key(w, SK_STR("cost"));
    write_numbers(w, across_cost, n);
    sk_jw_object_end(w);
    sk_jw_key(w, SK_STR("dials"));
    sk_jw_array(w);
    for (size_t i = 0; i < h->dials.len; i++)
        sk_jw_string(w, sk_str_c(h->dials.ptr[i]));
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("rows"));
    sk_jw_array(w);
    for (size_t k = 0; k < h->rows.len; k++)
        write_row(w, &h->rows.ptr[k]);
    sk_jw_array_end(w);
    sk_jw_object_end(w);
    free(tok);
    free(tools);
    free(across_tok);
    free(across_tools);
    free(across_cost);
}

size_t sk_history_tick_count(const sk_history *h)
{
    return h->ticks.len;
}

size_t sk_history_row_count(const sk_history *h)
{
    return h->rows.len;
}

const char *sk_history_row_key(const sk_history *h, size_t i)
{
    return i < h->rows.len ? h->rows.ptr[i].key : NULL;
}
