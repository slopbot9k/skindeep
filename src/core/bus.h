/* Named metrics (decisions 0011, 0021), as app/metrics/bus.js has them.
 * A value is a number, a string, a list or null for no data; an absent
 * name has never been written. Event metrics are counters that only
 * increase. The bus is read through sk_bus_get, the snapshot and the
 * event feed. The only way to write is a source, which owns its rate
 * windows, its defaults, the session reset and the name check. */
#ifndef SK_BUS_H
#define SK_BUS_H

#include "../base/json.h"

typedef enum { SK_VAL_NULL, SK_VAL_NUM, SK_VAL_STR, SK_VAL_LIST } sk_val_type;

typedef struct {
    sk_val_type type;
    double num;
    char *text; /* SK_VAL_STR: the string; SK_VAL_LIST: the list as compact JSON */
} sk_val;

#define SK_NUM(n) ((sk_val){SK_VAL_NUM, (n), NULL})
#define SK_NULLV ((sk_val){SK_VAL_NULL, 0, NULL})

/* Event kinds of the visualization feed (decision 0015). */
enum { SK_EV_OUTPUT = 1, SK_EV_TOOL, SK_EV_PROMPT, SK_EV_STOP, SK_EV_AGENT, SK_EV_COMPACT };

typedef void (*sk_log_fn)(void *ud, const char *msg);

typedef struct sk_bus sk_bus;
typedef struct sk_source sk_source;

typedef struct {
    const char *name;
    sk_val value;
} sk_metric_default;

/* What a source declares: names seeded at begin and reset, and the rate
 * names it samples, from tokens.out_rate, tools.rate, pty.out_bps and
 * pty.in_kps. */
typedef struct {
    const sk_metric_default *defaults;
    size_t n_defaults;
    const char *const *rates;
    size_t n_rates;
} sk_source_spec;

/* log: where refused names go, once each; NULL drops them. */
sk_bus *sk_bus_new(sk_log_fn log, void *log_ud);
/* Frees the bus and every source it made. */
void sk_bus_free(sk_bus *b);

/* The value of name, or NULL when absent. Valid until the next write. */
const sk_val *sk_bus_get(const sk_bus *b, const char *name);
/* The values in the order they first appeared, as the JavaScript Map. */
size_t sk_bus_count(const sk_bus *b);
void sk_bus_at(const sk_bus *b, size_t i, const char **name, const sk_val **val);
/* The snapshot as one JSON object. */
void sk_bus_write(const sk_bus *b, sk_jw *w);
/* Moves up to cap numbers of the event feed, (kind, class) pairs, into
 * out, oldest first. The feed keeps the newest 512 numbers. */
size_t sk_bus_drain_events(sk_bus *b, int *out, size_t cap);
/* Recomputes every rate of every live source at time now, in ms. */
void sk_bus_tick(sk_bus *b, uint64_t now);
/* True when name may be written; a refused name is logged once. */
bool sk_bus_accepts(sk_bus *b, sk_str name);

/* A write handle for one channel, owned by the bus. */
sk_source *sk_bus_source(sk_bus *b, const sk_source_spec *spec);
/* The channel is live: unset defaults take their values, unset rates 0. */
void sk_source_begin(sk_source *s);
void sk_source_set(sk_source *s, const char *name, sk_val v);
void sk_source_set_num(sk_source *s, const char *name, double n);
void sk_source_set_null(sk_source *s, const char *name);
void sk_source_set_str(sk_source *s, const char *name, sk_str text);
/* list: compact JSON of an array. */
void sk_source_set_list(sk_source *s, const char *name, sk_str list);
/* name += n; an unset or non-number name counts from 0. */
void sk_source_add(sk_source *s, const char *name, double n);
void sk_source_pulse(sk_source *s, const char *name);
/* history: the event came from a file's history and is not fed. */
void sk_source_event(sk_source *s, int kind, int cls, bool history);
/* amount counts in name's rate window from the next tick on, unless it
 * is history. */
void sk_source_sample(sk_source *s, const char *name, double amount, bool history);
/* A new session: forgets the session's names (all but event.*,
 * model.name and limits.*), empties the windows, and reseeds at once. */
void sk_source_reset(sk_source *s);
/* Every later write is dropped and ticks leave the source alone. */
void sk_source_close(sk_source *s);

#endif
