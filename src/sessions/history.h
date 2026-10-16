/* The sessions dashboard's history (decision 0028), as app/history.js
 * keeps it: one sample of each session's meters per tick, a ring of
 * points ticks, in memory only. Every session's series is aligned to the
 * ticks, no data (null) where it had none or did not run. A session that
 * ends keeps its row, marked ended, for keep_ended_ms. A row's breaks
 * mark the ticks where the agent began another session (/clear,
 * /resume), whose totals are not a rise. The dials (decision 0033) are
 * more series, keyed by their bus name. */
#ifndef SK_HISTORY_H
#define SK_HISTORY_H

#include "../core/bus.h"

#define SK_HISTORY_POINTS 720
#define SK_HISTORY_KEEP_ENDED_MS 600000
#define SK_HISTORY_INTERVAL_MS 5000

/* The meters a row draws, by bus name (decision 0011): tools, tokens
 * (the output: input counts every request's whole context again), cost,
 * context and compactions. */
typedef struct {
    const char *key, *name;
} sk_history_metric;

#define SK_HISTORY_METRICS 5
extern const sk_history_metric sk_history_metrics[SK_HISTORY_METRICS];

/* One series' value in a sample; NAN is no data. */
typedef struct {
    const char *key; /* a metric's key, or a dial's bus name */
    double value;
} sk_history_value;
typedef SK_VEC(sk_history_value) sk_history_values;

/* What the bus gives each metric and each dial: numbers only, NAN for
 * anything else. The keys point at the table and at dials, NULL-
 * terminated. Appends to out. */
void sk_history_pick(const sk_bus *bus, char *const *dials, sk_history_values *out);

typedef struct {
    const char *key;
    const sk_history_values *values; /* NULL: no data */
    /* A JSON object whose members join the row in the view (the
     * dashboard's label, folder, state...), or NULL. */
    const char *meta;
    const char *session; /* the agent's session id, or NULL */
} sk_history_sample;

typedef struct sk_history sk_history;

/* 0 for any argument takes its default above. interval_ms: the time
 * between ticks, for the per-minute rates. */
sk_history *sk_history_new(size_t points, double keep_ended_ms, double interval_ms);
void sk_history_free(sk_history *h);

/* The dials open now, each once. A new one starts as no data over the
 * ticks so far, a closed one is dropped. */
void sk_history_set_dials(sk_history *h, char *const *names, size_t n);
/* NULL-terminated, valid until the next sk_history_set_dials. */
char *const *sk_history_dials(sk_history *h);

/* One tick at now (ms): samples, keys unique, are the sessions live now.
 * A known key absent from them has ended. */
void sk_history_tick(sk_history *h, double now, const sk_history_sample *samples, size_t n);

/* What the page draws, as JSON: { ticks, intervalMs, across: {
 * tokensPerMin, toolsPerMin, cost }, dials, rows: [{ key, ended, series,
 * ...meta }] }, across summing the sessions that ran. Each tick's rise
 * of a counting series over the last minute is never scaled: before a
 * minute of samples exists, the time before the first counts as no rise
 * (maintainer, 2026-10-12). It is 0 where the series fell or had no
 * data, and the minute never reaches back past a break. */
void sk_history_view(const sk_history *h, sk_jw *w);

/* For the live checks: the ticks kept, and the rows' keys in order. */
size_t sk_history_tick_count(const sk_history *h);
size_t sk_history_row_count(const sk_history *h);
const char *sk_history_row_key(const sk_history *h, size_t i);

#endif
