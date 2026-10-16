#include "agentdirs.h"

#include "../base/paths.h"
#include "../base/utf8.h"

static char *dir_of(const sk_envp *env, const char *var, const char *dflt)
{
    const char *v = env ? sk_envp_get(env, var) : NULL;
    if (v && *v)
        return sk_xstrdup(v);
    sk_env e;
    sk_env_current(&e);
    return e.home ? sk_path_join(e.home, dflt) : NULL;
}

char *sk_claude_config_dir(const sk_envp *env)
{
    return dir_of(env, "CLAUDE_CONFIG_DIR", ".claude");
}

char *sk_codex_home(const sk_envp *env)
{
    return dir_of(env, "CODEX_HOME", ".codex");
}

void sk_claude_slug(sk_buf *out, sk_str cwd)
{
    for (size_t i = 0; i < cwd.len;) {
        char c = cwd.ptr[i];
        if ((unsigned char)c < 0x80) {
            bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            sk_buf_putc(out, keep ? c : '-');
            i++;
            continue;
        }
        uint32_t cp;
        i += sk_utf8_decode(cwd.ptr + i, cwd.len - i, &cp, NULL);
        sk_buf_append_c(out, cp > 0xFFFF ? "--" : "-");
    }
}
