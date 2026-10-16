/* Codex rollout lines to metric writes, as app/metrics/codex/parse.js
 * (decisions 0011, 0013), with the JavaScript value rules the Codex
 * files share. Codex writes numbers where it counts; any other value
 * reads as no number where JavaScript would have coerced it. */
#include "codex.h"

#include "../../base/utf8.h"
#include "../../core/limits.h"
#include "../../core/names.h"

#include <math.h>
#include <string.h>

bool sk_codex_truthy(const sk_json *v)
{
    if (!v)
        return false;
    switch (v->type) {
    case SK_JSON_BOOL:
        return v->boolean;
    case SK_JSON_NUMBER:
        return v->number != 0 && !isnan(v->number);
    case SK_JSON_STRING:
        return v->string.len > 0;
    case SK_JSON_ARRAY:
    case SK_JSON_OBJECT:
        return true;
    default:
        return false;
    }
}

static void js_number(sk_buf *out, double d)
{
    if (isnan(d))
        sk_buf_append_c(out, "NaN");
    else if (isinf(d))
        sk_buf_append_c(out, d > 0 ? "Infinity" : "-Infinity");
    else
        sk_json_format_number(out, d);
}

void sk_codex_js_string(sk_buf *out, const sk_json *v)
{
    if (!v) {
        sk_buf_append_c(out, "undefined");
        return;
    }
    switch (v->type) {
    case SK_JSON_NULL:
        sk_buf_append_c(out, "null");
        break;
    case SK_JSON_BOOL:
        sk_buf_append_c(out, v->boolean ? "true" : "false");
        break;
    case SK_JSON_NUMBER:
        js_number(out, v->number);
        break;
    case SK_JSON_STRING:
        sk_buf_append_str(out, v->string);
        break;
    case SK_JSON_ARRAY:
        /* Array.prototype.join: null items are empty. */
        for (size_t i = 0; i < v->array.len; i++) {
            if (i)
                sk_buf_putc(out, ',');
            if (v->array.items[i].type != SK_JSON_NULL)
                sk_codex_js_string(out, &v->array.items[i]);
        }
        break;
    case SK_JSON_OBJECT:
        sk_buf_append_c(out, "[object Object]");
        break;
    default:
        break;
    }
}

/* WhiteSpace and LineTerminator of ECMAScript. */
static bool js_space(uint32_t c)
{
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
           c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

sk_str sk_codex_trim(sk_str s)
{
    size_t start = s.len, end = 0;
    for (size_t i = 0; i < s.len;) {
        uint32_t cp;
        size_t n = sk_utf8_decode(s.ptr + i, s.len - i, &cp, NULL);
        if (!js_space(cp)) {
            if (start == s.len)
                start = i;
            end = i + n;
        }
        i += n;
    }
    return start == s.len ? sk_str_n(s.ptr, 0) : sk_str_n(s.ptr + start, end - start);
}

void sk_codex_slice16(sk_buf *out, sk_str s, size_t max)
{
    size_t units = 0;
    for (size_t i = 0; i < s.len;) {
        uint32_t cp;
        size_t n = sk_utf8_decode(s.ptr + i, s.len - i, &cp, NULL);
        units += cp > 0xFFFF ? 2 : 1;
        if (units > max)
            break;
        sk_buf_append(out, s.ptr + i, n);
        i += n;
    }
}

/* names.js num(): a finite number, else NAN for no data. */
static double num(const sk_json *v)
{
    double d;
    return sk_json_as_number(v, &d) ? d : NAN;
}

/* +v, for a value that is a number in practice. */
static double to_number(const sk_json *v)
{
    if (!v || v->type == SK_JSON_NULL)
        return v ? 0 : NAN;
    if (v->type == SK_JSON_NUMBER)
        return v->number;
    if (v->type == SK_JSON_BOOL)
        return v->boolean;
    return NAN;
}

/* v || 0, in arithmetic. */
static double or0(const sk_json *v)
{
    return sk_codex_truthy(v) ? to_number(v) : 0;
}

static bool is_container(const sk_json *v)
{
    return v && (v->type == SK_JSON_OBJECT || v->type == SK_JSON_ARRAY);
}

/* A number, or null where the JavaScript gave null. */
static void set_or_null(sk_source *src, const char *name, double v)
{
    if (isfinite(v))
        sk_source_set_num(src, name, v);
    else
        sk_source_set_null(src, name);
}

static bool one_of(sk_str s, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (sk_str_eq_c(s, list[i]))
            return true;
    return false;
}

static bool contains(sk_str s, const char *needle)
{
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= s.len; i++)
        if (!memcmp(s.ptr + i, needle, n))
            return true;
    return false;
}

int sk_codex_tool_class(sk_str n)
{
    static const char *const shell[] = {"exec",           "shell",        "local_shell",
                                        "container.exec", "exec_command", "unified_exec"};
    static const char *const edit[] = {"apply_patch", "write_file", "edit"};
    static const char *const reads[] = {"read_file", "view_image", "list_dir", "grep", "search"};
    if (one_of(n, shell, SK_LEN(shell)))
        return 3;
    if (one_of(n, edit, SK_LEN(edit)))
        return 2;
    if (one_of(n, reads, SK_LEN(reads)))
        return 1;
    if (sk_str_prefix(n, SK_STR("web_search")) || sk_str_prefix(n, SK_STR("web_fetch")))
        return 4;
    if (contains(n, "agent") || contains(n, "spawn"))
        return 5;
    return 0;
}

/* A window's scalar names: five_hour for 5 h or less, seven_day for
 * about a week. A 30 day window has only its item. */
static const char *scalar(double minutes)
{
    if (minutes <= 300)
        return "five_hour";
    if (minutes >= 6 * 1440 && minutes <= 8 * 1440)
        return "seven_day";
    return NULL;
}

static bool ascii_letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* The last word of a hyphenated model name, GPT-5.3-Codex-Spark as
 * Spark, so a quarter tile has room for it; else the whole name. */
static void short_name(sk_buf *out, const char *name)
{
    const char *last = strrchr(name, '-');
    last = last ? last + 1 : name;
    bool word = ascii_letter(last[0]);
    for (const char *p = last + 1; word && *p; p++)
        word = ascii_letter(*p) || *p == ' ';
    sk_buf_append_c(out, word ? last : name);
}

static void free_limit(sk_codex_limit *l)
{
    free(l->id);
    free(l->model);
}

static sk_codex_limit *find_limit(sk_codex_state *s, const char *id, size_t *at)
{
    for (size_t i = 0; i < s->limits.len; i++) {
        if (!strcmp(s->limits.ptr[i].id, id)) {
            *at = i;
            return &s->limits.ptr[i];
        }
    }
    return NULL;
}

static void add_items(sk_limits *items, const sk_codex_limit *l)
{
    for (size_t i = 0; i < l->n; i++) {
        const sk_codex_window *w = &l->windows[i];
        sk_buf window = {0}, key = {0}, label = {0};
        bool spanned = sk_limit_span(w->minutes, &window);
        const char *win = spanned ? window.ptr : "null";
        sk_buf_printf(&key, "%s.%s", l->id, win);
        if (l->model) {
            short_name(&label, l->model);
            sk_buf_printf(&label, " %s", win);
        }
        sk_limits_add(items, key.ptr, spanned ? window.ptr : NULL, l->model,
                      l->model ? label.ptr : spanned ? window.ptr : NULL, w->pct, w->resets_at);
        sk_buf_free(&window);
        sk_buf_free(&key);
        sk_buf_free(&label);
    }
}

/* One token_count's rate_limits: one limit's snapshot, its limit_id and
 * limit_name, and its primary and secondary windows. The limit codex (or
 * one with no limit_id) is the account's and fills the five-hour and
 * seven-day names, null for a window it no longer sends. Another, such
 * as codex_bengalfox named GPT-5.3-Codex-Spark, is one model's and has
 * only its items. Snapshots of different limits interleave in a rollout. */
static void limits(sk_codex_state *s, const sk_json *rl, sk_source *src)
{
    sk_str lid;
    char *id = sk_json_as_string(sk_json_get(rl, "limit_id"), &lid) && lid.len
                   ? sk_xstrndup(lid.ptr, lid.len)
                   : sk_xstrdup("codex");
    bool account = !strcmp(id, "codex");
    char *model = NULL;
    if (!account) {
        sk_str name;
        sk_str from = sk_json_as_string(sk_json_get(rl, "limit_name"), &name) &&
                              sk_codex_trim(name).len
                          ? name
                          : sk_str_c(id);
        sk_buf b = {0};
        sk_codex_slice16(&b, from, 40);
        model = sk_xstrdup(sk_buf_cstr(&b));
        sk_buf_free(&b);
    }
    sk_codex_window windows[2];
    size_t n = 0;
    const char *const which[] = {"primary", "secondary"};
    for (size_t i = 0; i < 2; i++) {
        const sk_json *w = sk_json_get(rl, which[i]);
        double minutes = num(sk_json_get(w, "window_minutes"));
        double pct = num(sk_json_get(w, "used_percent"));
        if (!is_container(w) || !isfinite(minutes) || minutes == 0 || !isfinite(pct))
            continue;
        windows[n++] = (sk_codex_window){minutes, pct, num(sk_json_get(w, "resets_at"))};
    }
    if (n == 2 && windows[1].minutes < windows[0].minutes) {
        sk_codex_window t = windows[0];
        windows[0] = windows[1];
        windows[1] = t;
    }
    size_t at = 0;
    sk_codex_limit *l = find_limit(s, id, &at);
    if (n) {
        if (!l) {
            sk_codex_limit fresh = {.id = sk_xstrdup(id)};
            sk_vec_push(&s->limits, fresh);
            l = &s->limits.ptr[s->limits.len - 1];
        }
        free(l->model);
        l->model = model ? sk_xstrdup(model) : NULL;
        memcpy(l->windows, windows, n * sizeof *windows);
        l->n = n;
    } else if (l) {
        free_limit(l);
        memmove(l, l + 1, (s->limits.len - at - 1) * sizeof *l);
        s->limits.len--;
    }
    if (account) {
        const char *const names[] = {"five_hour", "seven_day"};
        for (size_t k = 0; k < 2; k++) {
            const sk_codex_window *w = NULL;
            for (size_t i = 0; i < n && !w; i++) {
                const char *sc = scalar(windows[i].minutes);
                if (sc && !strcmp(sc, names[k]))
                    w = &windows[i];
            }
            sk_buf name = {0};
            sk_buf_printf(&name, "limits.%s_pct", names[k]);
            set_or_null(src, name.ptr, w ? w->pct : NAN);
            sk_buf_clear(&name);
            sk_buf_printf(&name, "limits.%s_resets_at", names[k]);
            set_or_null(src, name.ptr, w ? w->resets_at : NAN);
            sk_buf_free(&name);
        }
    }
    /* Account-wide first, then each model's in order of appearance. */
    sk_limits items = {0};
    size_t first = 0;
    sk_codex_limit *codex = find_limit(s, "codex", &first);
    if (codex)
        add_items(&items, codex);
    for (size_t i = 0; i < s->limits.len; i++)
        if (&s->limits.ptr[i] != codex)
            add_items(&items, &s->limits.ptr[i]);
    sk_limits_set(&items, src);
    sk_limits_free(&items);
    free(model);
    free(id);
}

static void token_count(sk_codex_state *s, const sk_json *p, sk_source *src, bool history)
{
    const sk_json *info = sk_json_get(p, "info");
    const sk_json *t = sk_json_get(info, "total_token_usage");
    if (sk_codex_truthy(info) && sk_codex_truthy(t)) {
        const sk_json *last = sk_json_get(info, "last_token_usage");
        double win = num(sk_json_get(info, "model_context_window"));
        double used = num(sk_json_get(last, "input_tokens"));
        const sk_json *in = sk_json_get(t, "input_tokens");
        set_or_null(src, "tokens.in", num(in));
        set_or_null(src, "tokens.out", num(sk_json_get(t, "output_tokens")));
        set_or_null(src, "tokens.cache_read", num(sk_json_get(t, "cached_input_tokens")));
        set_or_null(src, "tokens.cache_write", num(sk_json_get(t, "cache_write_input_tokens")));
        if (sk_codex_truthy(in))
            sk_source_set_num(src, "cache.hit_ratio",
                              or0(sk_json_get(t, "cached_input_tokens")) / to_number(in));
        else
            sk_source_set_null(src, "cache.hit_ratio");
        set_or_null(src, "context.window", win);
        set_or_null(src, "context.used_tokens", used);
        if (isfinite(win) && win != 0 && isfinite(used))
            sk_source_set_num(src, "context.used_pct", used / win * 100);
        else
            sk_source_set_null(src, "context.used_pct");
        set_or_null(src, "context.cache_read", num(sk_json_get(last, "cached_input_tokens")));
        if (isfinite(used))
            sk_source_set_num(src, "context.uncached",
                              used - or0(sk_json_get(last, "cached_input_tokens")));
        else
            sk_source_set_null(src, "context.uncached");
        set_or_null(src, "context.output", num(sk_json_get(last, "output_tokens")));
        double out = num(sk_json_get(t, "output_tokens"));
        if (!isfinite(out))
            out = 0;
        if (s->have_prev && out > s->prev_out)
            sk_source_sample(src, "tokens.out_rate", out - s->prev_out, history);
        s->have_prev = true;
        s->prev_out = out;
    }
    const sk_json *rl = sk_json_get(p, "rate_limits");
    if (is_container(rl))
        limits(s, rl, src);
}

static char *string_or_null(const sk_json *v)
{
    sk_str s;
    return sk_json_as_string(v, &s) ? sk_xstrndup(s.ptr, s.len) : NULL;
}

static void tool_call(const sk_json *p, sk_str type, sk_source *src, bool history)
{
    sk_buf name = {0};
    bool named = true;
    if (sk_str_eq_c(type, "local_shell_call")) {
        sk_buf_append_c(&name, "shell");
    } else if (sk_str_eq_c(type, "web_search_call")) {
        sk_buf_append_c(&name, "web_search");
    } else {
        const sk_json *n = sk_json_get(p, "name");
        named = sk_codex_truthy(n);
        if (named)
            sk_codex_js_string(&name, n);
    }
    sk_buf key = {0};
    sk_metric_tool_key(&key, named ? sk_buf_str(&name) : type);
    sk_source_add(src, "tools.total", 1);
    sk_source_add(src, "event.tool", 1);
    sk_source_add(src, key.ptr, 1);
    sk_source_event(src, SK_EV_TOOL, sk_codex_tool_class(sk_buf_str(&name)), history);
    sk_source_sample(src, "tools.rate", 1, history);
    sk_buf_free(&key);
    sk_buf_free(&name);
}

void sk_codex_state_add(sk_codex_state *s, const sk_json *obj, sk_source *src, bool history)
{
    const sk_json *p = sk_json_get(obj, "payload");
    if (!is_container(p))
        return;
    sk_str type = {0}, ptype = {0};
    bool typed = sk_json_as_string(sk_json_get(obj, "type"), &type);
    bool ptyped = sk_json_as_string(sk_json_get(p, "type"), &ptype);
    bool event = typed && sk_str_eq_c(type, "event_msg") && ptyped;
    if (typed && sk_str_eq_c(type, "session_meta")) {
        free(s->meta_id);
        free(s->meta_cwd);
        free(s->meta_source);
        s->meta_id = string_or_null(sk_json_get(p, "id"));
        s->meta_cwd = string_or_null(sk_json_get(p, "cwd"));
        s->meta_source = string_or_null(sk_json_get(p, "source"));
    } else if (typed && sk_str_eq_c(type, "turn_context")) {
        const sk_json *model = sk_json_get(p, "model");
        if (sk_codex_truthy(model)) {
            sk_buf whole = {0}, cut = {0};
            sk_codex_js_string(&whole, model);
            sk_codex_slice16(&cut, sk_buf_str(&whole), 40);
            sk_source_set_str(src, "model.name", sk_buf_str(&cut));
            sk_buf_free(&whole);
            sk_buf_free(&cut);
        }
    } else if (event && sk_str_eq_c(ptype, "token_count")) {
        token_count(s, p, src, history);
    } else if (event && sk_str_eq_c(ptype, "task_started")) {
        sk_source_set_num(src, "state.busy", 1);
        sk_source_add(src, "event.prompt", 1);
        sk_source_event(src, SK_EV_PROMPT, 0, history);
    } else if (event && sk_str_eq_c(ptype, "task_complete")) {
        sk_source_set_num(src, "state.busy", 0);
        sk_source_add(src, "event.stop", 1);
        sk_source_event(src, SK_EV_STOP, 0, history);
    } else if (typed && sk_str_eq_c(type, "compacted")) {
        sk_source_add(src, "event.compact", 1);
        sk_source_add(src, "session.compactions", 1);
        sk_source_event(src, SK_EV_COMPACT, 0, history);
    } else if (typed && sk_str_eq_c(type, "response_item") && ptyped) {
        static const char *const calls[] = {"function_call", "custom_tool_call", "local_shell_call",
                                            "web_search_call"};
        if (one_of(ptype, calls, SK_LEN(calls)))
            tool_call(p, ptype, src, history);
    }
}

void sk_codex_state_free(sk_codex_state *s)
{
    for (size_t i = 0; i < s->limits.len; i++)
        free_limit(&s->limits.ptr[i]);
    sk_vec_free(&s->limits);
    free(s->meta_id);
    free(s->meta_cwd);
    free(s->meta_source);
    memset(s, 0, sizeof *s);
}
