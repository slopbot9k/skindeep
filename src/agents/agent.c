#include "agent.h"

#include "generic.h"

#include <stdarg.h>
#include <string.h>

struct sk_agent {
    const char *kind;
    sk_part adapter, generic;
    bool adapter_closed, generic_closed;
    sk_bus *bus;
    sk_loop *loop;
    int timer;
    sk_log_fn log;
    void *log_ud;
    uint64_t (*now)(void);
    bool prepared, attached, closed;
    const char *failed;
    sk_report merged;
};

static const sk_provider *const TABLE[] = {&sk_provider_claude, &sk_provider_codex};

const sk_provider *sk_agent_provider(char *const *cmd)
{
    const char *base = cmd && cmd[0] ? cmd[0] : "";
    const char *slash = strrchr(base, '/');
    if (slash)
        base = slash + 1;
    for (size_t i = 0; i < SK_LEN(TABLE); i++)
        if (TABLE[i]->matches(base))
            return TABLE[i];
    return &sk_provider_plain;
}

void sk_agent_carry(char *const *cmd, sk_strv *args, sk_strv *dropped)
{
    sk_agent_provider(cmd)->carry(cmd && cmd[0] ? cmd + 1 : cmd, args, dropped);
}

static void logf_(sk_agent *a, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(sk_agent *a, const char *fmt, ...)
{
    if (!a->log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    a->log(a->log_ud, b.ptr);
    sk_buf_free(&b);
}

uint64_t sk_agent_now(const sk_agent_ctx *ctx)
{
    return ctx->now ? ctx->now() : sk_now_ms();
}

static void on_tick(sk_loop *loop, void *ud)
{
    (void)loop;
    sk_agent *a = ud;
    sk_agent_tick(a, a->now ? a->now() : sk_now_ms());
}

static sk_agent *make(const sk_agent_ctx *ctx, const char *kind, sk_part adapter)
{
    sk_agent *a = sk_xcalloc(1, sizeof *a);
    a->kind = kind;
    a->bus = ctx->bus;
    a->log = ctx->log;
    a->log_ud = ctx->log_ud;
    a->now = ctx->now;
    a->adapter = adapter;
    a->generic = sk_generic_create(ctx);
    if (ctx->loop && !ctx->manual_ticks) {
        a->loop = ctx->loop;
        a->timer = sk_loop_timer(a->loop, SK_AGENT_TICK_MS, SK_AGENT_TICK_MS, on_tick, a);
    }
    return a;
}

sk_agent *sk_agent_open_with(const sk_agent_ctx *ctx, const sk_provider *p)
{
    return make(ctx, p->kind, p->create(ctx));
}

sk_agent *sk_agent_watch(const sk_agent_ctx *ctx, const char *kind, sk_part adapter)
{
    return make(ctx, kind, adapter);
}

sk_agent *sk_agent_open(const sk_agent_ctx *ctx)
{
    return sk_agent_open_with(ctx, sk_agent_provider(ctx->cmd));
}

const char *sk_agent_kind(const sk_agent *a)
{
    return a->kind;
}

void *sk_agent_generic(sk_agent *a)
{
    return a->generic.self;
}

static void close_part(sk_agent *a, sk_part *p, bool *closed)
{
    (void)a;
    if (*closed)
        return;
    *closed = true;
    p->ops->close(p->self);
}

void sk_agent_prepare(sk_agent *a, sk_strv *args, char **notice)
{
    if (a->prepared)
        sk_die("agent: prepare twice");
    a->prepared = true;
    *notice = NULL;
    struct {
        sk_part *part;
        bool *closed;
    } parts[] = {{&a->adapter, &a->adapter_closed}, {&a->generic, &a->generic_closed}};
    for (size_t i = 0; i < SK_LEN(parts); i++) {
        sk_part *p = parts[i].part;
        if (*parts[i].closed || !p->ops->prepare)
            continue;
        char *n = NULL;
        if (p->ops->prepare(p->self, args, &n) != 0) {
            logf_(a, "%s adapter failed", a->kind);
            close_part(a, p, parts[i].closed);
            a->failed = SK_AGENT_FAILED;
            free(n);
            free(*notice);
            *notice = sk_xstrdup(SK_AGENT_FAILED);
            continue;
        }
        if (n && !*notice)
            *notice = n;
        else
            free(n);
    }
}

void sk_agent_attach(sk_agent *a, pid_t pid)
{
    if (!a->prepared)
        sk_die("agent: attach before prepare");
    if (a->attached)
        sk_die("agent: attach twice");
    a->attached = true;
    if (!a->adapter_closed)
        a->adapter.ops->attach(a->adapter.self, pid);
    if (!a->generic_closed)
        a->generic.ops->attach(a->generic.self, pid);
}

void sk_agent_on_output(sk_agent *a, size_t n, uint64_t now)
{
    if (!a->generic_closed)
        sk_generic_on_output(a->generic.self, n, now);
}

void sk_agent_on_input(sk_agent *a)
{
    if (!a->generic_closed)
        sk_generic_on_input(a->generic.self);
}

void sk_agent_tick(sk_agent *a, uint64_t now)
{
    if (a->closed)
        return;
    if (!a->adapter_closed)
        a->adapter.ops->poll(a->adapter.self, now);
    if (!a->generic_closed)
        a->generic.ops->poll(a->generic.self, now);
    sk_bus_tick(a->bus, now);
}

void sk_agent_report(sk_agent *a, sk_report *out)
{
    sk_report r = {0}, g = {0};
    if (!a->adapter_closed)
        a->adapter.ops->report(a->adapter.self, &r);
    if (!a->generic_closed)
        a->generic.ops->report(a->generic.self, &g);
    memset(&a->merged, 0, sizeof a->merged);
    a->merged.session = r.session;
    a->merged.title = r.title;
    a->merged.notice = a->failed ? a->failed : r.notice;
    a->merged.replaying = r.replaying;
    for (size_t i = 0; i < r.n_channels && a->merged.n_channels < SK_MAX_CHANNELS; i++)
        a->merged.channels[a->merged.n_channels++] = r.channels[i];
    for (size_t i = 0; i < g.n_channels && a->merged.n_channels < SK_MAX_CHANNELS; i++)
        a->merged.channels[a->merged.n_channels++] = g.channels[i];
    *out = a->merged;
}

bool sk_agent_rename(sk_agent *a, const char *name)
{
    if (a->closed || a->adapter_closed || !a->adapter.ops->rename)
        return false;
    return a->adapter.ops->rename(a->adapter.self, name);
}

void sk_agent_close(sk_agent *a)
{
    if (a->closed)
        return;
    a->closed = true;
    if (a->loop)
        sk_loop_cancel(a->loop, a->timer);
    close_part(a, &a->adapter, &a->adapter_closed);
    close_part(a, &a->generic, &a->generic_closed);
}

void sk_agent_free(sk_agent *a)
{
    if (!a)
        return;
    sk_agent_close(a);
    a->adapter.ops->free(a->adapter.self);
    a->generic.ops->free(a->generic.self);
    free(a);
}
