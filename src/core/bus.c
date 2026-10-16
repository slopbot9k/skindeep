#include "bus.h"

#include "../base/map.h"
#include "names.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    uint64_t window, per; /* value = sum of the window's samples * per / window */
} RATES[] = {
    {"tokens.out_rate", 10000, 1000},
    {"tools.rate", 60000, 60000},
    {"pty.out_bps", 1000, 1000},
    {"pty.in_kps", 1000, 1000},
};

/* What a new session forgets. event.*, model.name and limits.* survive. */
static const char *const SESSION_PREFIXES[] = {
    "context.", "tokens.", "cost.", "lines.", "tools.", "agents.", "cache.", "state.", "checklist.",
    "session.compactions",
};

#define MAX_EVENTS 512

typedef struct {
    char *name;
    sk_val val;
} entry;

typedef struct {
    uint64_t at;
    double amount;
} sample;

typedef struct {
    size_t rate; /* index into RATES */
    SK_VEC(sample) window;
    double pending;
} rate_state;

struct sk_source {
    sk_bus *bus;
    sk_metric_default *defaults;
    size_t n_defaults;
    SK_VEC(rate_state) rates;
    bool begun, closed;
};

struct sk_bus {
    sk_map index; /* name -> entry */
    SK_VEC(entry *) order;
    SK_VEC(sk_source *) sources;
    int events[MAX_EVENTS];
    size_t n_events;
    sk_map refused;
    sk_log_fn log;
    void *log_ud;
};

static sk_val copy_val(sk_val v)
{
    if (v.text)
        v.text = sk_xstrdup(v.text);
    return v;
}

static void free_val(sk_val *v)
{
    free(v->text);
    v->text = NULL;
}

sk_bus *sk_bus_new(sk_log_fn log, void *log_ud)
{
    sk_bus *b = sk_xcalloc(1, sizeof *b);
    sk_map_init(&b->index);
    sk_map_init(&b->refused);
    b->log = log;
    b->log_ud = log_ud;
    return b;
}

void sk_bus_free(sk_bus *b)
{
    if (!b)
        return;
    for (size_t i = 0; i < b->order.len; i++) {
        free(b->order.ptr[i]->name);
        free_val(&b->order.ptr[i]->val);
        free(b->order.ptr[i]);
    }
    sk_vec_free(&b->order);
    for (size_t i = 0; i < b->sources.len; i++) {
        sk_source *s = b->sources.ptr[i];
        for (size_t k = 0; k < s->n_defaults; k++) {
            free((char *)s->defaults[k].name);
            free_val(&s->defaults[k].value);
        }
        free(s->defaults);
        for (size_t k = 0; k < s->rates.len; k++)
            sk_vec_free(&s->rates.ptr[k].window);
        sk_vec_free(&s->rates);
        free(s);
    }
    sk_vec_free(&b->sources);
    sk_map_free(&b->index);
    sk_map_free(&b->refused);
    free(b);
}

const sk_val *sk_bus_get(const sk_bus *b, const char *name)
{
    entry *e = sk_map_get(&b->index, sk_str_c(name));
    return e ? &e->val : NULL;
}

size_t sk_bus_count(const sk_bus *b)
{
    return b->order.len;
}

void sk_bus_at(const sk_bus *b, size_t i, const char **name, const sk_val **val)
{
    *name = b->order.ptr[i]->name;
    *val = &b->order.ptr[i]->val;
}

void sk_bus_write(const sk_bus *b, sk_jw *w)
{
    sk_jw_object(w);
    for (size_t i = 0; i < b->order.len; i++) {
        const entry *e = b->order.ptr[i];
        sk_jw_key(w, sk_str_c(e->name));
        switch (e->val.type) {
        case SK_VAL_NULL:
            sk_jw_null(w);
            break;
        case SK_VAL_NUM:
            sk_jw_number(w, e->val.num);
            break;
        case SK_VAL_STR:
            sk_jw_string(w, sk_str_c(e->val.text));
            break;
        case SK_VAL_LIST:
            sk_jw_raw(w, sk_str_c(e->val.text));
            break;
        }
    }
    sk_jw_object_end(w);
}

size_t sk_bus_drain_events(sk_bus *b, int *out, size_t cap)
{
    size_t n = SK_MIN(cap - cap % 2, b->n_events);
    if (!n)
        return 0;
    memcpy(out, b->events, n * sizeof *out);
    memmove(b->events, b->events + n, (b->n_events - n) * sizeof *out);
    b->n_events -= n;
    return n;
}

bool sk_bus_accepts(sk_bus *b, sk_str name)
{
    if (sk_metric_known(name))
        return true;
    if (!sk_map_has(&b->refused, name)) {
        sk_map_put(&b->refused, name);
        if (b->log) {
            sk_buf msg = {0};
            sk_buf_printf(&msg, "metric name refused: %.*s", (int)name.len, name.ptr);
            b->log(b->log_ud, msg.ptr);
            sk_buf_free(&msg);
        }
    }
    return false;
}

/* The raw writes, for sources alone. */
static void put(sk_bus *b, const char *name, sk_val v)
{
    entry *e = sk_map_get(&b->index, sk_str_c(name));
    if (!e) {
        e = sk_xcalloc(1, sizeof *e);
        e->name = sk_xstrdup(name);
        *sk_map_put(&b->index, sk_str_c(name)) = e;
        sk_vec_push(&b->order, e);
    } else {
        free_val(&e->val);
    }
    e->val = copy_val(v);
}

static void forget_session(sk_bus *b)
{
    size_t keep = 0;
    for (size_t i = 0; i < b->order.len; i++) {
        entry *e = b->order.ptr[i];
        bool drop = false;
        for (size_t p = 0; p < SK_LEN(SESSION_PREFIXES) && !drop; p++)
            drop = strncmp(e->name, SESSION_PREFIXES[p], strlen(SESSION_PREFIXES[p])) == 0;
        if (drop) {
            sk_map_del(&b->index, sk_str_c(e->name));
            free(e->name);
            free_val(&e->val);
            free(e);
        } else {
            b->order.ptr[keep++] = e;
        }
    }
    b->order.len = keep;
}

static void emit(sk_bus *b, int kind, int cls)
{
    if (b->n_events + 2 > MAX_EVENTS) {
        memmove(b->events, b->events + 2, (b->n_events - 2) * sizeof *b->events);
        b->n_events -= 2;
    }
    b->events[b->n_events++] = kind;
    b->events[b->n_events++] = cls;
}

sk_source *sk_bus_source(sk_bus *b, const sk_source_spec *spec)
{
    sk_source *s = sk_xcalloc(1, sizeof *s);
    s->bus = b;
    if (spec && spec->n_defaults) {
        s->defaults = sk_xcalloc(spec->n_defaults, sizeof *s->defaults);
        for (size_t i = 0; i < spec->n_defaults; i++) {
            s->defaults[i].name = sk_xstrdup(spec->defaults[i].name);
            s->defaults[i].value = copy_val(spec->defaults[i].value);
        }
        s->n_defaults = spec->n_defaults;
    }
    for (size_t i = 0; spec && i < spec->n_rates; i++) {
        for (size_t r = 0; r < SK_LEN(RATES); r++) {
            if (strcmp(spec->rates[i], RATES[r].name) == 0 && sk_bus_accepts(b, sk_str_c(RATES[r].name))) {
                rate_state rs = {r, {0}, 0};
                sk_vec_push(&s->rates, rs);
            }
        }
    }
    sk_vec_push(&b->sources, s);
    return s;
}

static void clear_windows(sk_source *s)
{
    for (size_t i = 0; i < s->rates.len; i++) {
        s->rates.ptr[i].window.len = 0;
        s->rates.ptr[i].pending = 0;
    }
}

static void seed(sk_source *s)
{
    for (size_t i = 0; i < s->n_defaults; i++) {
        const sk_metric_default *d = &s->defaults[i];
        if (!sk_bus_get(s->bus, d->name) && sk_bus_accepts(s->bus, sk_str_c(d->name)))
            put(s->bus, d->name, d->value);
    }
    for (size_t i = 0; i < s->rates.len; i++) {
        const char *r = RATES[s->rates.ptr[i].rate].name;
        if (!sk_bus_get(s->bus, r))
            put(s->bus, r, SK_NUM(0));
    }
}

void sk_source_begin(sk_source *s)
{
    if (s->begun || s->closed)
        return;
    s->begun = true;
    seed(s);
}

void sk_source_set(sk_source *s, const char *name, sk_val v)
{
    if (!s->closed && sk_bus_accepts(s->bus, sk_str_c(name)))
        put(s->bus, name, v);
}

void sk_source_set_num(sk_source *s, const char *name, double n)
{
    sk_source_set(s, name, SK_NUM(n));
}

void sk_source_set_null(sk_source *s, const char *name)
{
    sk_source_set(s, name, SK_NULLV);
}

void sk_source_set_str(sk_source *s, const char *name, sk_str text)
{
    char *t = sk_xstrndup(text.ptr ? text.ptr : "", text.len);
    sk_source_set(s, name, (sk_val){SK_VAL_STR, 0, t});
    free(t);
}

void sk_source_set_list(sk_source *s, const char *name, sk_str list)
{
    char *t = sk_xstrndup(list.ptr ? list.ptr : "", list.len);
    sk_source_set(s, name, (sk_val){SK_VAL_LIST, 0, t});
    free(t);
}

void sk_source_add(sk_source *s, const char *name, double n)
{
    if (s->closed || !sk_bus_accepts(s->bus, sk_str_c(name)))
        return;
    const sk_val *v = sk_bus_get(s->bus, name);
    put(s->bus, name, SK_NUM((v && v->type == SK_VAL_NUM ? v->num : 0) + n));
}

void sk_source_pulse(sk_source *s, const char *name)
{
    sk_source_add(s, name, 1);
}

void sk_source_event(sk_source *s, int kind, int cls, bool history)
{
    if (!s->closed && !history)
        emit(s->bus, kind, cls);
}

void sk_source_sample(sk_source *s, const char *name, double amount, bool history)
{
    if (s->closed || history)
        return;
    for (size_t i = 0; i < s->rates.len; i++)
        if (strcmp(RATES[s->rates.ptr[i].rate].name, name) == 0)
            s->rates.ptr[i].pending += amount;
}

void sk_source_reset(sk_source *s)
{
    if (s->closed)
        return;
    forget_session(s->bus);
    clear_windows(s);
    if (s->begun)
        seed(s);
}

void sk_source_close(sk_source *s)
{
    s->closed = true;
}

static void tick_source(sk_source *s, uint64_t now)
{
    if (!s->begun || s->closed)
        return;
    for (size_t i = 0; i < s->rates.len; i++) {
        rate_state *r = &s->rates.ptr[i];
        uint64_t window = RATES[r->rate].window;
        if (r->pending) {
            sample smp = {now, r->pending};
            sk_vec_push(&r->window, smp);
        }
        r->pending = 0;
        size_t drop = 0;
        while (drop < r->window.len && r->window.ptr[drop].at + window <= now)
            drop++;
        if (drop) {
            memmove(r->window.ptr, r->window.ptr + drop, (r->window.len - drop) * sizeof *r->window.ptr);
            r->window.len -= drop;
        }
        double sum = 0;
        for (size_t k = 0; k < r->window.len; k++)
            sum += r->window.ptr[k].amount;
        put(s->bus, RATES[r->rate].name, SK_NUM(sum * (double)RATES[r->rate].per / (double)window));
    }
}

void sk_bus_tick(sk_bus *b, uint64_t now)
{
    for (size_t i = 0; i < b->sources.len; i++)
        tick_source(b->sources.ptr[i], now);
}
