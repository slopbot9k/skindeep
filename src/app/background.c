/* Port of app/background.js (decision 0030). Its regex,
 *
 *   /is running in the background \(([0-9a-f]{8})\)\..{0,400}?Run `claude attach \1` to open it/
 *
 * is matched by hand, not with <regex.h>: POSIX extended expressions
 * have no backreference, and both forms count .{0,400} in bytes or the
 * locale's characters, where JavaScript counts UTF-16 code units. The
 * text it runs on has every run of whitespace made one space, so the
 * dot matches anything there. The first match is the earliest prefix
 * that the attach line with the same id follows within 400 code units,
 * which is what the matcher below looks for. */
#include "background.h"

#include "../base/utf8.h"
#include "js.h"

#include <string.h>

static bool blank(const char *row)
{
    return sk_js_trim(sk_str_c(row)).len == 0;
}

/* rows.join(' ').replace(/\s+/g, ' '), invalid UTF-8 as U+FFFD. */
static void collapse(sk_buf *out, const char *const *rows, size_t from, size_t n)
{
    bool space = false, first = true;
    for (size_t i = from; i < n; i++) {
        if (blank(rows[i]))
            continue;
        if (!first)
            space = true;
        first = false;
        sk_str r = sk_str_c(rows[i]);
        for (size_t k = 0; k < r.len;) {
            uint32_t cp;
            k += sk_utf8_decode(r.ptr + k, r.len - k, &cp, NULL);
            if (sk_js_space(cp)) {
                space = true;
                continue;
            }
            if (space)
                sk_buf_putc(out, ' ');
            space = false;
            sk_utf8_append(out, cp);
        }
    }
    if (space)
        sk_buf_putc(out, ' ');
}

static bool hex8(const char *p)
{
    for (int i = 0; i < 8; i++)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
            return false;
    return true;
}

static bool match(sk_str t, char id[9])
{
    static const char PREFIX[] = "is running in the background (";
    static const char RUN[] = "Run `claude attach ";
    static const char TAIL[] = "` to open it";
    const size_t pre = sizeof PREFIX - 1, run = sizeof RUN - 1, tail = sizeof TAIL - 1;
    for (size_t p = 0; p + pre + 10 <= t.len; p++) {
        const char *h = t.ptr + p + pre;
        if (memcmp(t.ptr + p, PREFIX, pre) || !hex8(h) || h[8] != ')' || h[9] != '.')
            continue;
        size_t r = p + pre + 10, units = 0;
        for (;;) {
            if (t.len - r >= run + 8 + tail && !memcmp(t.ptr + r, RUN, run) &&
                !memcmp(t.ptr + r + run, h, 8) && !memcmp(t.ptr + r + run + 8, TAIL, tail)) {
                memcpy(id, h, 8);
                id[8] = 0;
                return true;
            }
            if (r >= t.len)
                break;
            uint32_t cp;
            size_t len = sk_utf8_decode(t.ptr + r, t.len - r, &cp, NULL);
            units += cp > 0xFFFF ? 2 : 1;
            if (units > 400)
                break;
            r += len;
        }
    }
    return false;
}

bool sk_background_refusal_rows(const char *const *rows, size_t n, char id[9])
{
    size_t from = n, kept = 0;
    while (from > 0 && kept < SK_BACKGROUND_TAIL_ROWS)
        if (!blank(rows[--from]))
            kept++;
    sk_buf text = {0};
    collapse(&text, rows, from, n);
    bool found = match(sk_buf_str(&text), id);
    sk_buf_free(&text);
    return found;
}

bool sk_background_refusal(const char *screen, char id[9])
{
    if (!screen)
        return false;
    sk_strv rows = {0};
    for (const char *p = screen;;) {
        const char *nl = strchr(p, '\n');
        sk_strv_push_n(&rows, p, nl ? (size_t)(nl - p) : strlen(p));
        if (!nl)
            break;
        p = nl + 1;
    }
    bool found = sk_background_refusal_rows((const char *const *)rows.ptr, rows.len, id);
    sk_strv_free(&rows);
    return found;
}

bool sk_background_bare_resume(char *const *args)
{
    for (size_t i = 0; args && args[i]; i++) {
        const char *a = args[i];
        if (!strcmp(a, "--continue") || !strcmp(a, "-c") || !strncmp(a, "--resume=", 9))
            continue;
        if (!strcmp(a, "--resume") || !strcmp(a, "-r")) {
            if (args[i + 1] && args[i + 1][0] != '-')
                i++;
            continue;
        }
        return false;
    }
    return true;
}

bool sk_background_attach_for(char *const *cmd, const char *screen, sk_strv *out)
{
    char id[9];
    if (!cmd || !cmd[0] || !sk_background_bare_resume(cmd + 1) || !sk_background_refusal(screen, id))
        return false;
    sk_strv_push(out, cmd[0]);
    sk_strv_push(out, "attach");
    sk_strv_push(out, id);
    return true;
}
