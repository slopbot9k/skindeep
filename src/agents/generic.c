#include "generic.h"

#include "../platform/proctree.h"

#include <math.h>
#include <string.h>

#define SILENCE_MS 250
#define SAMPLE_MS 1000

typedef struct {
    sk_source *src;
    uint64_t (*now)(void);
    uint64_t start;
    bool out_seen;
    uint64_t last_out;
    bool sampled;
    uint64_t last_sample;
    bool have_prev;
    uint64_t prev_cpu_ms, prev_at;
    pid_t pid;
} generic;

static const sk_metric_default DEFAULTS[] = {{"event.output", {SK_VAL_NUM, 0, NULL}}};
static const char *const RATES[] = {"pty.out_bps", "pty.in_kps"};
static const sk_source_spec SPEC = {DEFAULTS, 1, RATES, 2};

static void sample(generic *g, uint64_t now)
{
    uint64_t cpu = 0, rss = 0;
    if (!sk_proc_tree_usage(g->pid, &cpu, &rss))
        cpu = rss = 0;
    if (g->have_prev && now > g->prev_at) {
        double pct = ((double)cpu - (double)g->prev_cpu_ms) / (double)(now - g->prev_at) * 100;
        sk_source_set_num(g->src, "proc.cpu_pct", pct > 0 ? pct : 0);
    }
    g->have_prev = true;
    g->prev_cpu_ms = cpu;
    g->prev_at = now;
    sk_source_set_num(g->src, "proc.mem_mb", (double)rss / 1048576);
}

static void poll_part(void *self, uint64_t now)
{
    generic *g = self;
    if (!g->pid)
        return;
    uint64_t since = g->out_seen ? g->last_out : g->start;
    sk_source_set_num(g->src, "pty.idle_s", ((double)now - (double)since) / 1000);
    sk_source_set_num(g->src, "session.clock_s", floor(((double)now - (double)g->start) / 1000));
    if (g->sampled && now - g->last_sample < SAMPLE_MS)
        return;
    g->sampled = true;
    g->last_sample = now;
    sample(g, now);
}

static void attach(void *self, pid_t pid)
{
    generic *g = self;
    g->pid = pid;
    sk_source_begin(g->src);
    poll_part(g, g->now ? g->now() : sk_now_ms());
}

static void report(void *self, sk_report *out)
{
    generic *g = self;
    memset(out, 0, sizeof *out);
    sk_ch_state state = g->pid ? SK_CH_LIVE : SK_CH_WAITING;
    out->channels[0] = (sk_channel){"pty", state, NULL, NULL};
    out->channels[1] = (sk_channel){"proc", state, NULL, NULL};
    out->n_channels = 2;
}

static void close_part(void *self)
{
    generic *g = self;
    sk_source_close(g->src);
}

static void free_part(void *self)
{
    free(self);
}

static const sk_part_ops OPS = {NULL, attach, poll_part, report, NULL, close_part, free_part};

sk_part sk_generic_create(const sk_agent_ctx *ctx)
{
    generic *g = sk_xcalloc(1, sizeof *g);
    g->src = sk_bus_source(ctx->bus, &SPEC);
    g->now = ctx->now;
    g->start = ctx->launched ? ctx->launched : sk_agent_now(ctx);
    return (sk_part){&OPS, g};
}

void sk_generic_on_output(void *self, size_t n, uint64_t now)
{
    generic *g = self;
    if (!g->out_seen || now - g->last_out > SILENCE_MS) {
        sk_source_pulse(g->src, "event.output");
        sk_source_event(g->src, SK_EV_OUTPUT, 0, false);
    }
    g->out_seen = true;
    g->last_out = now;
    sk_source_sample(g->src, "pty.out_bps", (double)n, false);
}

void sk_generic_on_input(void *self)
{
    generic *g = self;
    sk_source_sample(g->src, "pty.in_kps", 1, false);
}

void sk_generic_set_start(void *self, uint64_t start)
{
    generic *g = self;
    g->start = start;
}
