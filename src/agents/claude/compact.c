/* The auto-compact rule of Claude Code 2.1.296 and its inputs, as
 * app/metrics/claude/compact.js has them (decision 0012). */
#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *const sk_claude_compact_keys[SK_CLAUDE_COMPACT_KEYS] = {
    "CLAUDE_CODE_AUTO_COMPACT_WINDOW", "DISABLE_AUTO_COMPACT", "DISABLE_COMPACT"};

enum { KEY_WINDOW, KEY_DISABLE_AUTO, KEY_DISABLE };

/* Math.round of a value that is not negative: ties go up. */
static double round_half_up(double x)
{
    double f = floor(x);
    return x - f >= 0.5 ? f + 1 : f;
}

/* /^(\d+(?:\.\d+)?)([kKmM]?)$/ on the trimmed text. */
bool sk_claude_parse_tokens(sk_str s, double *out)
{
    s = sk_claude_trim(s);
    size_t i = 0;
    while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9')
        i++;
    if (i == 0)
        return false;
    if (i < s.len && s.ptr[i] == '.') {
        size_t j = i + 1;
        while (j < s.len && s.ptr[j] >= '0' && s.ptr[j] <= '9')
            j++;
        if (j == i + 1)
            return false;
        i = j;
    }
    size_t digits = i;
    double mult = 1;
    if (i < s.len) {
        char c = s.ptr[i++];
        if (c == 'k' || c == 'K')
            mult = 1e3;
        else if (c == 'm' || c == 'M')
            mult = 1e6;
        else
            return false;
    }
    if (i != s.len)
        return false;
    char *num = sk_xstrndup(s.ptr, digits);
    double v = strtod(num, NULL);
    free(num);
    *out = round_half_up(v * mult);
    return true;
}

/* The value is set and not "", "0" or any case of "false". */
static bool truthy_text(const char *v)
{
    if (!v || !*v || !strcmp(v, "0"))
        return false;
    if (strlen(v) != 5)
        return true;
    const char *f = "false";
    for (int i = 0; i < 5; i++) {
        char c = v[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != f[i])
            return true;
    }
    return false;
}

/* parseTokens(s) || ...: a parse of 0 counts as none. */
static double tokens_or_zero(const char *s)
{
    double v;
    return s && sk_claude_parse_tokens(sk_str_c(s), &v) ? v : 0;
}

/* The value of --autocompact V or --autocompact=V. */
static const char *autocompact_arg(char *const *argv)
{
    for (size_t i = 0; argv && argv[i]; i++) {
        if (!strcmp(argv[i], "--autocompact"))
            return argv[i + 1];
        if (!strncmp(argv[i], "--autocompact=", 14))
            return argv[i] + 14;
    }
    return NULL;
}

void sk_claude_compaction_of(sk_claude_compaction *out, const sk_claude_kept *sources, size_t n,
                             char *const *argv, const sk_envp *env)
{
    const char *child[SK_CLAUDE_COMPACT_KEYS] = {NULL, NULL, NULL};
    const char *settings[SK_CLAUDE_COMPACT_KEYS] = {NULL, NULL, NULL};
    for (size_t k = 0; k < SK_CLAUDE_COMPACT_KEYS && env; k++)
        child[k] = sk_envp_get(env, sk_claude_compact_keys[k]);
    bool has_window = false;
    double settings_window = 0;
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < SK_CLAUDE_COMPACT_KEYS; k++)
            if (sources[i].env[k])
                settings[k] = sources[i].env[k];
        if (sources[i].has_window) {
            has_window = true;
            settings_window = sources[i].window;
        }
    }
    out->disabled = truthy_text(child[KEY_DISABLE_AUTO]) || truthy_text(child[KEY_DISABLE]) ||
                    truthy_text(settings[KEY_DISABLE_AUTO]) || truthy_text(settings[KEY_DISABLE]);
    const char *w = child[KEY_WINDOW] && *child[KEY_WINDOW] ? child[KEY_WINDOW] : settings[KEY_WINDOW];
    double window = tokens_or_zero(w);
    if (!sk_claude_truthy_num(window))
        window = tokens_or_zero(autocompact_arg(argv));
    if (!sk_claude_truthy_num(window) && has_window)
        window = settings_window;
    out->window = sk_claude_truthy_num(window) ? window : 0;
}

double sk_claude_compact_window(const sk_claude_compaction *c, double used)
{
    if (sk_claude_truthy_num(c->window))
        return c->window;
    return used > 200000 ? 1000000 : 200000;
}

/* W = compactWindow - min(maxOutput, 20000), threshold = W - 13000, with
 * compactWindow capped at the window. */
double sk_claude_compact_threshold(double window, const sk_claude_compaction *c)
{
    if (!sk_claude_truthy_num(window) || c->disabled)
        return NAN;
    double w = sk_claude_truthy_num(c->window) ? c->window : window;
    return fmin(w, window) - 20000 - 13000;
}
