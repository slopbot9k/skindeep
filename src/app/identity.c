/* Port of app/identity.js (decision 0004), the pure parts, and the hook
 * the per-OS process info fills for liveCmdline. */
#include "identity.h"

#include "../base/paths.h"
#include "../base/utf8.h"
#include "js.h"

#include <string.h>

static bool plain_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           (c && strchr("_@%+=:,./-", c));
}

void sk_identity_quote(sk_buf *out, sk_str arg)
{
    bool plain = arg.len > 0;
    for (size_t i = 0; i < arg.len && plain; i++)
        plain = plain_char(arg.ptr[i]);
    if (plain) {
        sk_buf_append_str(out, arg);
        return;
    }
    sk_buf_putc(out, '\'');
    for (size_t i = 0; i < arg.len; i++) {
        if (arg.ptr[i] == '\'')
            sk_buf_append_c(out, "'\\''");
        else
            sk_buf_putc(out, arg.ptr[i]);
    }
    sk_buf_putc(out, '\'');
}

void sk_identity_quote_argv(sk_buf *out, char *const *argv)
{
    for (size_t i = 0; argv && argv[i]; i++) {
        if (i)
            sk_buf_putc(out, ' ');
        sk_identity_quote(out, sk_str_c(argv[i]));
    }
}

/* The length of home when word is home or lies under it, else 0. */
static size_t under_home(sk_str word, const char *home)
{
    size_t h = home ? strlen(home) : 0;
    if (!h || word.len < h || memcmp(word.ptr, home, h) != 0)
        return 0;
    return word.len == h || word.ptr[h] == '/' ? h : 0;
}

void sk_identity_tildify(sk_buf *out, const char *p, const char *home)
{
    size_t h = under_home(sk_str_c(p), home);
    if (h)
        sk_buf_putc(out, '~');
    sk_buf_append_c(out, p + h);
}

void sk_identity_shown_cmdline(sk_buf *out, const char *cmdline, const char *home)
{
    if (home && !strcmp(home, "/"))
        home = NULL;
    sk_str s = sk_str_c(cmdline);
    for (size_t i = 0;;) {
        size_t j = i;
        while (j < s.len && s.ptr[j] != ' ')
            j++;
        sk_str word = sk_str_n(s.ptr + i, j - i);
        size_t h = under_home(word, home);
        if (h)
            sk_buf_putc(out, '~');
        sk_buf_append_str(out, sk_str_sub(word, h, word.len));
        if (j >= s.len)
            break;
        sk_buf_putc(out, ' ');
        i = j + 1;
    }
}

/* configName: the name of a config dir other than the default, dflt
 * being the default's leaf, .claude or .codex. */
static bool config_name(sk_buf *out, const char *dir, const char *home, const char *dflt)
{
    if (!dir || !*dir)
        return false;
    sk_buf d = {0}, def = {0};
    sk_js_resolve(&d, sk_str_c(dir));
    sk_js_join(&def, sk_str_c(home ? home : ""), sk_str_c(dflt));
    bool named = false;
    if (strcmp(d.ptr, def.ptr) != 0) {
        sk_str name = sk_js_basename(sk_buf_str(&d));
        if (sk_str_eq(name, sk_js_basename(sk_buf_str(&def))))
            name = sk_js_basename(sk_js_dirname(sk_buf_str(&d)));
        /* .replace(/^\.?(claude|codex)-/, '').replace(/^\./, '') */
        sk_str rest = name.len && name.ptr[0] == '.' ? sk_str_sub(name, 1, name.len) : name;
        if (sk_str_prefix(rest, SK_STR("claude-")))
            name = sk_str_sub(rest, 7, rest.len);
        else if (sk_str_prefix(rest, SK_STR("codex-")))
            name = sk_str_sub(rest, 6, rest.len);
        if (name.len && name.ptr[0] == '.')
            name = sk_str_sub(name, 1, name.len);
        if (name.len) {
            sk_buf_append_str(out, name);
            named = true;
        }
    }
    sk_buf_free(&d);
    sk_buf_free(&def);
    return named;
}

void sk_identity_label(sk_buf *out, char *const *cmd, const sk_envp *env, const char *kind,
                       const char *home)
{
    sk_buf_append_str(out, sk_js_basename(sk_str_c(cmd && cmd[0] ? cmd[0] : "")));
    const char *var = NULL, *dflt = NULL;
    if (kind && !strcmp(kind, "claude")) {
        var = "CLAUDE_CONFIG_DIR";
        dflt = ".claude";
    } else if (kind && !strcmp(kind, "codex")) {
        var = "CODEX_HOME";
        dflt = ".codex";
    }
    if (!var || !env)
        return;
    sk_buf name = {0};
    if (config_name(&name, sk_envp_get(env, var), home, dflt))
        sk_buf_printf(out, " [%s]", name.ptr);
    sk_buf_free(&name);
}

void sk_identity_title(sk_buf *out, const char *label)
{
    sk_buf_printf(out, "%s in %s", label, SK_APP_NAME);
}

static sk_identity_argv_fn argv_source;

void sk_identity_set_argv_source(sk_identity_argv_fn fn)
{
    argv_source = fn;
}

bool sk_identity_live_cmdline(pid_t pid, sk_buf *out)
{
    sk_buf raw = {0};
    bool ok = argv_source && argv_source(pid, &raw);
    if (ok)
        sk_identity_cmdline_of(out, sk_buf_cstr(&raw), raw.len);
    sk_buf_free(&raw);
    return ok;
}

void sk_identity_cmdline_of(sk_buf *out, const char *raw, size_t n)
{
    sk_buf text = {0};
    sk_utf8_sanitize(&text, raw, n);
    sk_str s = sk_buf_str(&text);
    for (size_t i = 0, k = 0;; k++) {
        size_t j = i;
        while (j < s.len && s.ptr[j] != 0)
            j++;
        bool last = j >= s.len;
        /* .filter((s, i, a) => i < a.length - 1 || s) */
        if (!last || j > i) {
            if (k)
                sk_buf_putc(out, ' ');
            sk_identity_quote(out, sk_str_n(s.ptr + i, j - i));
        }
        if (last)
            break;
        i = j + 1;
    }
    sk_buf_free(&text);
}
