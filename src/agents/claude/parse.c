/* Claude Code's side-channel payloads to metric updates, as
 * app/metrics/claude/parse.js has them (decisions 0011, 0012, 0034).
 *
 * Where a payload holds a value of the wrong type, JavaScript coerces
 * it; here a usage figure that is not a number reads as 0 and a model
 * that is not a string is passed over. */
#include "internal.h"

#include "../../base/utf8.h"
#include "../../core/limits.h"
#include "../../core/names.h"

#include <math.h>
#include <string.h>

/* ---- Updates ---- */

static void kv_put(sk_claude_kvs *out, const char *name, sk_val v)
{
    sk_claude_kv kv = {sk_xstrdup(name), v};
    sk_vec_push(out, kv);
}

static void kv_num(sk_claude_kvs *out, const char *name, double n)
{
    kv_put(out, name, SK_NUM(n));
}

static void kv_null(sk_claude_kvs *out, const char *name)
{
    kv_put(out, name, SK_NULLV);
}

/* A number, or null when it is NAN. */
static void kv_maybe(sk_claude_kvs *out, const char *name, double n)
{
    if (isnan(n))
        kv_null(out, name);
    else
        kv_num(out, name, n);
}

/* num(v): a finite number, else null. */
static void kv_finite(sk_claude_kvs *out, const char *name, const sk_json *v)
{
    double n;
    if (sk_json_as_number(v, &n))
        kv_num(out, name, n);
    else
        kv_null(out, name);
}

static void kv_text(sk_claude_kvs *out, const char *name, sk_val_type type, sk_str text)
{
    kv_put(out, name, (sk_val){type, 0, sk_xstrndup(text.ptr ? text.ptr : "", text.len)});
}

void sk_claude_kvs_free(sk_claude_kvs *v)
{
    for (size_t i = 0; i < v->len; i++) {
        free(v->ptr[i].name);
        free(v->ptr[i].val.text);
    }
    sk_vec_free(v);
}

const sk_claude_kv *sk_claude_kvs_get(const sk_claude_kvs *v, const char *name)
{
    for (size_t i = v->len; i > 0; i--)
        if (!strcmp(v->ptr[i - 1].name, name))
            return &v->ptr[i - 1];
    return NULL;
}

/* ---- Hooks ---- */

static bool is_one_of(sk_str s, const char *const *names, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (sk_str_eq_c(s, names[i]))
            return true;
    return false;
}

int sk_claude_tool_class(const sk_json *name)
{
    static const char *const READ[] = {"Read", "Grep", "Glob", "LS", "NotebookRead"};
    static const char *const EDIT[] = {"Edit", "MultiEdit", "Write", "NotebookEdit"};
    static const char *const SHELL[] = {"Bash", "BashOutput", "KillShell", "KillBash"};
    static const char *const WEB[] = {"WebFetch", "WebSearch"};
    static const char *const AGENT[] = {"Agent", "Task"};
    if (!sk_claude_truthy(name))
        return 0;
    sk_buf b = {0};
    sk_claude_to_string(&b, name);
    sk_str n = sk_buf_str(&b);
    int cls = is_one_of(n, READ, SK_LEN(READ))     ? 1
              : is_one_of(n, EDIT, SK_LEN(EDIT))   ? 2
              : is_one_of(n, SHELL, SK_LEN(SHELL)) ? 3
              : is_one_of(n, WEB, SK_LEN(WEB))     ? 4
              : is_one_of(n, AGENT, SK_LEN(AGENT)) ? 5
                                                   : 0;
    sk_buf_free(&b);
    return cls;
}

static const char *const HOOK_FIELDS[] = {
    "hook_event_name", "session_id", "transcript_path", "tool_name", "tool_use_id", "duration_ms",
    "agent_id", "agent_type", "trigger", "notification_type", "source", "model",
};

/* Prompt text, tool input and output and assistant messages never leave
 * this function. */
const sk_json *sk_claude_hook_meta(sk_arena *a, const sk_json *body)
{
    sk_json *m = sk_arena_zalloc(a, sizeof *m);
    m->type = SK_JSON_OBJECT;
    m->object.members = sk_arena_array(a, SK_LEN(HOOK_FIELDS), sizeof *m->object.members);
    for (size_t i = 0; i < SK_LEN(HOOK_FIELDS); i++) {
        const sk_json *v = sk_json_get(body, HOOK_FIELDS[i]);
        if (!v || v->type == SK_JSON_NULL || v->type == SK_JSON_ARRAY || v->type == SK_JSON_OBJECT)
            continue;
        sk_json_member *mem = &m->object.members[m->object.len++];
        mem->key = sk_str_c(HOOK_FIELDS[i]);
        mem->value = *v;
    }
    return m;
}

static bool event_is(const sk_json *m, const char *name)
{
    sk_str ev;
    return sk_json_as_string(sk_json_get(m, "hook_event_name"), &ev) && sk_str_eq_c(ev, name);
}

/* agents.active is the core's own count. */
void sk_claude_from_hook(const sk_json *m, sk_source *src)
{
    if (event_is(m, "PreToolUse")) {
        sk_source_pulse(src, "event.tool");
        sk_source_add(src, "tools.total", 1);
        const sk_json *tool = sk_json_get(m, "tool_name");
        if (sk_claude_truthy(tool)) {
            sk_buf name = {0}, key = {0};
            sk_claude_to_string(&name, tool);
            sk_metric_tool_key(&key, sk_buf_str(&name));
            sk_source_add(src, key.ptr, 1);
            sk_buf_free(&name);
            sk_buf_free(&key);
        }
    } else if (event_is(m, "SubagentStart")) {
        sk_source_pulse(src, "event.agent");
    } else if (event_is(m, "UserPromptSubmit")) {
        sk_source_set_num(src, "state.busy", 1);
        sk_source_pulse(src, "event.prompt");
    } else if (event_is(m, "Stop")) {
        sk_source_set_num(src, "state.busy", 0);
        sk_source_pulse(src, "event.stop");
    } else if (event_is(m, "PostCompact")) {
        sk_source_pulse(src, "event.compact");
        sk_source_add(src, "session.compactions", 1);
    }
}

/* ---- Usage and context ---- */

/* u.x || 0 for a number. */
static double figure(const sk_json *u, const char *key)
{
    const sk_json *v = sk_json_get(u, key);
    return v && v->type == SK_JSON_NUMBER ? v->number : 0;
}

void sk_claude_usage_of(const sk_json *u, sk_claude_usage *out)
{
    out->uncached = figure(u, "input_tokens");
    out->write = figure(u, "cache_creation_input_tokens");
    out->read = figure(u, "cache_read_input_tokens");
    out->output = figure(u, "output_tokens");
    out->used = out->uncached + out->write + out->read;
}

/* Math.max(0, x), which keeps NaN. */
static double js_max0(double x)
{
    return isnan(x) ? x : x > 0 ? x : 0;
}

void sk_claude_context_from(const sk_claude_usage *cu, double window, double threshold,
                            sk_claude_kvs *out)
{
    if (!cu) {
        static const char *const NONE[] = {"context.used_tokens", "context.used_pct",
                                           "context.compact_left_pct", "context.uncached",
                                           "context.cache_read", "context.cache_write",
                                           "context.output"};
        for (size_t i = 0; i < SK_LEN(NONE); i++)
            kv_null(out, NONE[i]);
        return;
    }
    kv_num(out, "context.uncached", cu->uncached);
    kv_num(out, "context.cache_write", cu->write);
    kv_num(out, "context.cache_read", cu->read);
    kv_num(out, "context.output", cu->output);
    kv_num(out, "context.used_tokens", cu->used);
    kv_maybe(out, "context.used_pct", sk_claude_truthy_num(window) ? cu->used / window * 100 : NAN);
    kv_maybe(out, "context.compact_left_pct",
             sk_claude_truthy_num(threshold) ? js_max0((1 - cu->used / threshold) * 100) : NAN);
}

/* ---- The statusline ---- */

/* String(s).replace(/_/g, ' ').trim().slice(0, 40), its first letter
 * upper case. Only ASCII letters change case here. */
static void model_name(sk_buf *out, sk_str s)
{
    sk_buf spaced = {0};
    for (size_t i = 0; i < s.len; i++)
        sk_buf_putc(&spaced, s.ptr[i] == '_' ? ' ' : s.ptr[i]);
    size_t start = out->len;
    sk_claude_slice(out, sk_claude_trim(sk_buf_str(&spaced)), 40);
    if (out->len > start && out->ptr[start] >= 'a' && out->ptr[start] <= 'z')
        out->ptr[start] = (char)(out->ptr[start] - 'a' + 'A');
    sk_buf_free(&spaced);
}

/* One window under rate_limits as an item, as limitItems makes it. */
static void limit_item(sk_limits *l, sk_str key, const sk_json *w, bool with_model)
{
    static const struct {
        const char *key, *span;
    } WINDOWS[] = {{"five_hour", "5h"}, {"seven_day", "7d"}};
    const char *window = NULL, *label = NULL;
    sk_buf model = {0};
    bool whole = false;
    size_t part = SIZE_MAX;
    for (size_t i = 0; i < SK_LEN(WINDOWS); i++) {
        size_t n = strlen(WINDOWS[i].key);
        if (sk_str_eq_c(key, WINDOWS[i].key))
            whole = true, window = WINDOWS[i].span;
        else if (part == SIZE_MAX && key.len > n && !memcmp(key.ptr, WINDOWS[i].key, n) && key.ptr[n] == '_')
            part = i;
    }
    if (sk_str_eq_c(key, "spend_limit")) {
        label = "spend";
        window = NULL;
    } else if (!whole) {
        window = part != SIZE_MAX ? WINDOWS[part].span : NULL;
        sk_str display;
        if (sk_json_as_string(sk_json_get(w, "display_name"), &display) && sk_claude_trim(display).len)
            model_name(&model, display);
        else if (part != SIZE_MAX)
            model_name(&model, sk_str_sub(key, strlen(WINDOWS[part].key) + 1, SIZE_MAX));
        else
            model_name(&model, key);
    }
    /* Account-wide items first, then each model's. */
    if ((model.len > 0) == with_model) {
        double pct, at;
        if (!sk_json_as_number(sk_json_get(w, "used_percentage"), &pct))
            pct = NAN;
        if (!sk_json_as_number(sk_json_get(w, "resets_at"), &at))
            at = NAN;
        char *k = sk_xstrndup(key.ptr, key.len);
        sk_limits_add(l, k, window, model.len ? model.ptr : NULL, label, pct, at);
        free(k);
    }
    sk_buf_free(&model);
}

/* limits.items of rate_limits, an object or, as Object.entries reads
 * one, an array keyed by index. */
static void limit_items(const sk_json *rl, sk_claude_kvs *out)
{
    sk_limits l = {{0}, 0};
    for (int pass = 0; pass < 2; pass++) {
        size_t n = !rl ? 0 : rl->type == SK_JSON_OBJECT ? rl->object.len
                         : rl->type == SK_JSON_ARRAY    ? rl->array.len
                                                        : 0;
        for (size_t i = 0; i < n; i++) {
            const sk_json *w;
            sk_buf at = {0};
            sk_str key;
            if (rl->type == SK_JSON_OBJECT) {
                key = rl->object.members[i].key;
                w = &rl->object.members[i].value;
            } else {
                sk_buf_printf(&at, "%zu", i);
                key = sk_buf_str(&at);
                w = &rl->array.items[i];
            }
            if (w->type == SK_JSON_OBJECT)
                limit_item(&l, key, w, pass == 1);
            sk_buf_free(&at);
        }
    }
    if (l.n) {
        sk_buf list = {0};
        sk_buf_append_str(&list, sk_buf_str(&l.json));
        sk_buf_putc(&list, ']');
        kv_text(out, "limits.items", SK_VAL_LIST, sk_buf_str(&list));
        sk_buf_free(&list);
    } else {
        kv_null(out, "limits.items");
    }
    sk_limits_free(&l);
}

/* x ? num(x.key) : null */
static void kv_if(sk_claude_kvs *out, const char *name, const sk_json *x, const char *key)
{
    if (sk_claude_truthy(x))
        kv_finite(out, name, sk_json_get(x, key));
    else
        kv_null(out, name);
}

/* The context family only when the payload carries the window size. */
void sk_claude_from_statusline(const sk_json *j, const sk_claude_compaction *c, sk_claude_kvs *out)
{
    const sk_json *cw = sk_json_get(j, "context_window");
    double window;
    if (sk_json_as_number(sk_json_get(cw, "context_window_size"), &window)) {
        double threshold = sk_claude_compact_threshold(window, c);
        kv_num(out, "context.window", window);
        kv_num(out, "context.window_inferred", 0);
        kv_maybe(out, "context.compact_threshold", threshold);
        kv_maybe(out, "context.compact_at_pct",
                 sk_claude_truthy_num(threshold) ? threshold / window * 100 : NAN);
        const sk_json *cu = sk_json_get(cw, "current_usage");
        sk_claude_usage u = {0, 0, 0, 0, 0};
        sk_claude_usage_of(cu, &u);
        sk_claude_context_from(sk_claude_truthy(cu) ? &u : NULL, window, threshold, out);
    }
    const sk_json *cost = sk_json_get(j, "cost");
    kv_finite(out, "cost.usd", sk_json_get(cost, "total_cost_usd"));
    kv_finite(out, "lines.added", sk_json_get(cost, "total_lines_added"));
    kv_finite(out, "lines.removed", sk_json_get(cost, "total_lines_removed"));
    const sk_json *cache = sk_json_get(j, "prompt_cache");
    if (sk_claude_truthy(cache))
        kv_finite(out, "cache.hit_ratio", sk_json_get(cache, "hit_ratio"));
    const sk_json *rl = sk_json_get(j, "rate_limits");
    if (rl && rl->type != SK_JSON_OBJECT && rl->type != SK_JSON_ARRAY)
        rl = NULL;
    const sk_json *five = sk_json_get(rl, "five_hour"), *seven = sk_json_get(rl, "seven_day");
    kv_if(out, "limits.five_hour_pct", five, "used_percentage");
    kv_if(out, "limits.five_hour_resets_at", five, "resets_at");
    kv_if(out, "limits.seven_day_pct", seven, "used_percentage");
    kv_if(out, "limits.seven_day_resets_at", seven, "resets_at");
    /* A gateway session's spend limit, absent elsewhere. */
    const sk_json *sl = sk_json_get(rl, "spend_limit");
    if (sk_claude_truthy(sl)) {
        kv_finite(out, "limits.spend_pct", sk_json_get(sl, "used_percentage"));
        kv_finite(out, "limits.spend_resets_at", sk_json_get(sl, "resets_at"));
        kv_finite(out, "limits.spend_usd", sk_json_get(sl, "used_usd"));
        kv_finite(out, "limits.spend_limit_usd", sk_json_get(sl, "limit_usd"));
    }
    limit_items(rl, out);
    const sk_json *model = sk_json_get(j, "model");
    const sk_json *display = sk_json_get(model, "display_name");
    if (sk_claude_truthy(model) && sk_claude_truthy(display)) {
        sk_buf s = {0}, cut = {0};
        sk_claude_to_string(&s, display);
        sk_claude_slice(&cut, sk_buf_str(&s), 40);
        kv_text(out, "model.name", SK_VAL_STR, sk_buf_str(&cut));
        sk_buf_free(&s);
        sk_buf_free(&cut);
    }
}

/* ---- Transcript totals ---- */

void sk_claude_totals_init(sk_claude_totals *t)
{
    memset(t, 0, sizeof *t);
    sk_map_init(&t->seen);
}

void sk_claude_totals_free(sk_claude_totals *t)
{
    size_t i = 0;
    sk_str k;
    void *v;
    while (sk_map_next(&t->seen, &i, &k, &v))
        free(v);
    sk_map_free(&t->seen);
    free(t->model);
    memset(t, 0, sizeof *t);
}

/* The key of a message id in the seen map, by type and value, as a Map
 * tells them apart. An object or array id is a key nothing else
 * matches: false. */
static bool id_key(const sk_json *id, sk_buf *key)
{
    switch (id->type) {
    case SK_JSON_STRING:
        sk_buf_putc(key, 's');
        sk_buf_append_str(key, id->string);
        return true;
    case SK_JSON_NUMBER:
        sk_buf_putc(key, 'n');
        sk_json_format_number(key, id->number);
        return true;
    case SK_JSON_BOOL:
        sk_buf_append_c(key, id->boolean ? "btrue" : "bfalse");
        return true;
    default:
        return false;
    }
}

bool sk_claude_totals_add(sk_claude_totals *t, const sk_json *obj, double *d_out)
{
    sk_str type;
    if (!sk_json_as_string(sk_json_get(obj, "type"), &type) || !sk_str_eq_c(type, "assistant") ||
        sk_claude_truthy(sk_json_get(obj, "isSidechain")))
        return false;
    const sk_json *msg = sk_json_get(obj, "message");
    const sk_json *id = sk_json_get(msg, "id"), *u = sk_json_get(msg, "usage");
    if (!sk_claude_truthy(id) || !sk_claude_truthy(u))
        return false;
    sk_claude_usage s;
    sk_claude_usage_of(u, &s);
    double cur[4] = {s.used, s.output, s.read, s.write};
    double none[4] = {0, 0, 0, 0};
    sk_buf key = {0};
    double *prev = none;
    bool keyed = id_key(id, &key);
    if (keyed) {
        double *seen = sk_map_get(&t->seen, sk_buf_str(&key));
        if (seen)
            prev = seen;
    }
    t->in += cur[0] - prev[0];
    t->out += cur[1] - prev[1];
    t->read += cur[2] - prev[2];
    t->write += cur[3] - prev[3];
    *d_out = cur[1] - prev[1];
    if (keyed) {
        void **slot = sk_map_put(&t->seen, sk_buf_str(&key));
        if (!*slot)
            *slot = sk_xmalloc(sizeof cur);
        memcpy(*slot, cur, sizeof cur);
    }
    sk_buf_free(&key);
    t->has_last = true;
    t->last = s;
    sk_str model;
    if (sk_json_as_string(sk_json_get(msg, "model"), &model) && model.len) {
        free(t->model);
        t->model = sk_xstrndup(model.ptr, model.len);
    }
    return true;
}

void sk_claude_totals_metrics(const sk_claude_totals *t, sk_claude_kvs *out)
{
    kv_num(out, "tokens.in", t->in);
    kv_num(out, "tokens.out", t->out);
    kv_num(out, "tokens.cache_read", t->read);
    kv_num(out, "tokens.cache_write", t->write);
}

double sk_claude_totals_hit_ratio(const sk_claude_totals *t)
{
    return sk_claude_truthy_num(t->in) ? t->read / t->in : NAN;
}

/* ---- The checklist ---- */

enum { TODO, DOING, DONE };
static const char *const STATE_NAMES[] = {"todo", "doing", "done"};
#define TEXT_MAX 200

struct sk_claude_item {
    char *id, *text;
    int state;
};

/* STATE[status]: pending, in_progress or completed, else -1. */
static int state_of(const sk_json *status)
{
    sk_str s;
    if (!sk_json_as_string(status, &s))
        return -1;
    return sk_str_eq_c(s, "pending") ? TODO
           : sk_str_eq_c(s, "in_progress") ? DOING
           : sk_str_eq_c(s, "completed") ? DONE
                                          : -1;
}

/* String(s ?? '').trim().slice(0, 200), malloc'd. */
static char *item_text(const sk_json *v)
{
    sk_buf s = {0}, out = {0};
    if (v && v->type != SK_JSON_NULL)
        sk_claude_to_string(&s, v);
    sk_claude_slice(&out, sk_claude_trim(sk_buf_str(&s)), TEXT_MAX);
    sk_buf_free(&s);
    sk_buf_reserve(&out, 0);
    return out.ptr;
}

static sk_claude_item *item_new(char *id, char *text, int state)
{
    sk_claude_item *it = sk_xmalloc(sizeof *it);
    it->id = id;
    it->text = text;
    it->state = state;
    return it;
}

static void item_free(sk_claude_item *it)
{
    free(it->id);
    free(it->text);
    free(it);
}

static void pending_clear(sk_claude_checklist *c)
{
    for (size_t i = 0; i < c->pending.len; i++)
        free(c->pending.ptr[i].tool_use_id);
    c->pending.len = 0;
}

static void items_clear(sk_claude_checklist *c)
{
    for (size_t i = 0; i < c->items.len; i++)
        item_free(c->items.ptr[i]);
    c->items.len = 0;
    pending_clear(c);
}

void sk_claude_checklist_init(sk_claude_checklist *c)
{
    memset(c, 0, sizeof *c);
}

void sk_claude_checklist_free(sk_claude_checklist *c)
{
    items_clear(c);
    sk_vec_free(&c->items);
    sk_vec_free(&c->pending);
    c->created = 0;
}

static size_t pending_find(const sk_claude_checklist *c, sk_str id)
{
    for (size_t i = 0; i < c->pending.len; i++)
        if (sk_str_eq_c(id, c->pending.ptr[i].tool_use_id))
            return i;
    return SIZE_MAX;
}

static void pending_drop(sk_claude_checklist *c, size_t i)
{
    free(c->pending.ptr[i].tool_use_id);
    memmove(&c->pending.ptr[i], &c->pending.ptr[i + 1], (c->pending.len - i - 1) * sizeof *c->pending.ptr);
    c->pending.len--;
}

static bool is_object_like(const sk_json *v)
{
    return v && (v->type == SK_JSON_OBJECT || v->type == SK_JSON_ARRAY);
}

static bool tool_use(sk_claude_checklist *c, const sk_json *b)
{
    const sk_json *input = sk_json_get(b, "input");
    if (!is_object_like(input))
        input = NULL;
    sk_str name;
    if (!sk_json_as_string(sk_json_get(b, "name"), &name))
        return false;
    if (sk_str_eq_c(name, "TodoWrite")) {
        const sk_json *todos = sk_json_get(input, "todos");
        if (!todos || todos->type != SK_JSON_ARRAY)
            return false;
        items_clear(c);
        size_t n = 0;
        for (size_t i = 0; i < todos->array.len; i++) {
            const sk_json *t = &todos->array.items[i];
            if (!is_object_like(t))
                continue;
            const sk_json *content = sk_json_get(t, "content");
            int st = state_of(sk_json_get(t, "status"));
            sk_buf id = {0};
            sk_buf_printf(&id, "%zu", ++n);
            sk_vec_push(&c->items, item_new(id.ptr, item_text(content ? content : sk_json_get(t, "activeForm")),
                                            st < 0 ? TODO : st));
        }
        return true;
    }
    if (sk_str_eq_c(name, "TaskCreate")) {
        const sk_json *subject = sk_json_get(input, "subject");
        if (!subject || subject->type != SK_JSON_STRING)
            return false;
        sk_buf id = {0};
        sk_json_format_number(&id, ++c->created);
        sk_claude_item *it = item_new(id.ptr, item_text(subject), TODO);
        sk_vec_push(&c->items, it);
        sk_str use;
        if (sk_json_as_string(sk_json_get(b, "id"), &use)) {
            size_t at = pending_find(c, use);
            if (at != SIZE_MAX) {
                c->pending.ptr[at].it = it;
            } else {
                sk_claude_pending p = {it, sk_xstrndup(use.ptr, use.len)};
                sk_vec_push(&c->pending, p);
            }
        }
        return true;
    }
    if (sk_str_eq_c(name, "TaskUpdate")) {
        sk_buf want = {0};
        sk_claude_to_string(&want, sk_json_get(input, "taskId"));
        size_t at = SIZE_MAX;
        for (size_t i = 0; i < c->items.len && at == SIZE_MAX; i++)
            if (!strcmp(c->items.ptr[i]->id, sk_buf_cstr(&want)))
                at = i;
        sk_buf_free(&want);
        if (at == SIZE_MAX)
            return false;
        sk_claude_item *it = c->items.ptr[at];
        const sk_json *status = sk_json_get(input, "status");
        sk_str st;
        if (sk_json_as_string(status, &st) && sk_str_eq_c(st, "deleted")) {
            /* A pending result can no longer change what shows. */
            for (size_t i = c->pending.len; i > 0; i--)
                if (c->pending.ptr[i - 1].it == it)
                    pending_drop(c, i - 1);
            memmove(&c->items.ptr[at], &c->items.ptr[at + 1], (c->items.len - at - 1) * sizeof *c->items.ptr);
            c->items.len--;
            item_free(it);
            return true;
        }
        bool changed = false;
        int s = state_of(status);
        if (s >= 0 && s != it->state) {
            it->state = s;
            changed = true;
        }
        const sk_json *subject = sk_json_get(input, "subject");
        if (subject && subject->type == SK_JSON_STRING) {
            char *t = item_text(subject);
            if (strcmp(t, it->text) != 0) {
                free(it->text);
                it->text = t;
                changed = true;
            } else {
                free(t);
            }
        }
        return changed;
    }
    return false;
}

/* The line's task-list match of
 * /^[ \t]*[-*+][ \t]+\[([ xX])\][ \t]+(\S.*)$/: the mark and the text. */
static bool task_line(sk_str line, char *mark, sk_str *text)
{
    size_t i = 0, n = line.len;
    const char *p = line.ptr;
    while (i < n && (p[i] == ' ' || p[i] == '\t'))
        i++;
    if (i >= n || (p[i] != '-' && p[i] != '*' && p[i] != '+'))
        return false;
    i++;
    size_t gap = i;
    while (i < n && (p[i] == ' ' || p[i] == '\t'))
        i++;
    if (i == gap || i + 3 > n || p[i] != '[' || (p[i + 1] != ' ' && p[i + 1] != 'x' && p[i + 1] != 'X') ||
        p[i + 2] != ']')
        return false;
    *mark = p[i + 1];
    i += 3;
    gap = i;
    while (i < n && (p[i] == ' ' || p[i] == '\t'))
        i++;
    if (i == gap || i >= n)
        return false;
    uint32_t cp;
    sk_utf8_decode(p + i, n - i, &cp, NULL);
    if (sk_claude_space(cp))
        return false;
    *text = sk_str_n(p + i, n - i);
    return true;
}

/* The length of the line terminator at s[i], or 0: \n, \r, U+2028 and
 * U+2029, where the m flag's ^ and $ match. */
static size_t terminator(const char *s, size_t n, size_t i)
{
    if (s[i] == '\n' || s[i] == '\r')
        return 1;
    if (i + 3 <= n && (unsigned char)s[i] == 0xE2 && (unsigned char)s[i + 1] == 0x80 &&
        ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9))
        return 3;
    return 0;
}

static bool markdown(sk_claude_checklist *c, sk_str s)
{
    SK_VEC(sk_claude_item *) found = {0};
    for (size_t start = 0, i = 0;; i++) {
        size_t t = i < s.len ? terminator(s.ptr, s.len, i) : 1;
        if (!t)
            continue;
        char mark;
        sk_str text;
        if (task_line(sk_str_n(s.ptr + start, i - start), &mark, &text)) {
            sk_buf id = {0};
            sk_buf_printf(&id, "%zu", found.len + 1);
            sk_json v = {.type = SK_JSON_STRING, .string = text};
            sk_vec_push(&found, item_new(id.ptr, item_text(&v), mark == ' ' ? TODO : DONE));
        }
        if (i >= s.len)
            break;
        i += t - 1;
        start = i + 1;
    }
    if (!found.len) {
        sk_vec_free(&found);
        return false;
    }
    items_clear(c);
    for (size_t i = 0; i < found.len; i++)
        sk_vec_push(&c->items, found.ptr[i]);
    sk_vec_free(&found);
    return true;
}

bool sk_claude_checklist_add(sk_claude_checklist *c, const sk_json *obj)
{
    if (!obj || obj->type != SK_JSON_OBJECT || sk_claude_truthy(sk_json_get(obj, "isSidechain")))
        return false;
    const sk_json *content = sk_json_get(sk_json_get(obj, "message"), "content");
    size_t n = content && content->type == SK_JSON_ARRAY ? content->array.len : 0;
    sk_str type;
    if (!sk_json_as_string(sk_json_get(obj, "type"), &type))
        return false;
    bool changed = false;
    if (sk_str_eq_c(type, "assistant")) {
        for (size_t i = 0; i < n; i++) {
            const sk_json *b = &content->array.items[i];
            sk_str bt, text;
            if (b->type != SK_JSON_OBJECT || !sk_json_as_string(sk_json_get(b, "type"), &bt))
                continue;
            if (sk_str_eq_c(bt, "tool_use"))
                changed = tool_use(c, b) || changed;
            else if (sk_str_eq_c(bt, "text") && sk_json_as_string(sk_json_get(b, "text"), &text))
                changed = markdown(c, text) || changed;
        }
    } else if (sk_str_eq_c(type, "user")) {
        const sk_json *r = sk_json_get(obj, "toolUseResult");
        const sk_json *task = is_object_like(r) ? sk_json_get(r, "task") : NULL;
        if (!sk_claude_truthy(task) || !is_object_like(task))
            task = NULL;
        for (size_t i = 0; i < n; i++) {
            const sk_json *b = &content->array.items[i];
            sk_str bt, use;
            if (!sk_json_as_string(sk_json_get(b, "type"), &bt) || !sk_str_eq_c(bt, "tool_result") ||
                !sk_json_as_string(sk_json_get(b, "tool_use_id"), &use))
                continue;
            size_t at = pending_find(c, use);
            if (at == SIZE_MAX)
                continue;
            sk_claude_item *it = c->pending.ptr[at].it;
            pending_drop(c, at);
            const sk_json *id = sk_json_get(task, "id");
            if (id && id->type != SK_JSON_NULL) {
                sk_buf s = {0};
                sk_claude_to_string(&s, id);
                sk_buf_reserve(&s, 0);
                free(it->id);
                it->id = s.ptr;
            }
        }
    }
    return changed;
}

void sk_claude_checklist_metrics(const sk_claude_checklist *c, sk_claude_kvs *out)
{
    sk_buf list = {0};
    sk_jw w;
    sk_jw_init(&w, &list, 0);
    sk_jw_array(&w);
    size_t done = 0;
    for (size_t i = 0; i < c->items.len; i++) {
        const sk_claude_item *it = c->items.ptr[i];
        sk_jw_object(&w);
        sk_jw_key(&w, SK_STR("text"));
        sk_jw_string(&w, sk_str_c(it->text));
        sk_jw_key(&w, SK_STR("state"));
        sk_jw_string(&w, sk_str_c(STATE_NAMES[it->state]));
        sk_jw_object_end(&w);
        done += it->state == DONE;
    }
    sk_jw_array_end(&w);
    kv_text(out, "checklist.items", SK_VAL_LIST, sk_buf_str(&list));
    kv_num(out, "checklist.count", (double)c->items.len);
    kv_num(out, "checklist.done", (double)done);
    sk_buf_free(&list);
}
