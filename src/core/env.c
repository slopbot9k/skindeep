#include "env.h"

#include <stdlib.h>
#include <string.h>

void sk_envp_from(sk_envp *e, char *const *src)
{
    memset(e, 0, sizeof *e);
    sk_strv_extend(&e->vars, src);
}

static size_t find(const sk_envp *e, const char *name)
{
    size_t n = strlen(name);
    for (size_t i = 0; i < e->vars.len; i++)
        if (!strncmp(e->vars.ptr[i], name, n) && e->vars.ptr[i][n] == '=')
            return i;
    return SIZE_MAX;
}

const char *sk_envp_get(const sk_envp *e, const char *name)
{
    size_t i = find(e, name);
    return i == SIZE_MAX ? NULL : e->vars.ptr[i] + strlen(name) + 1;
}

void sk_envp_set(sk_envp *e, const char *name, const char *value)
{
    size_t i = find(e, name);
    if (i != SIZE_MAX) {
        free(e->vars.ptr[i]);
        memmove(e->vars.ptr + i, e->vars.ptr + i + 1, (e->vars.len - i) * sizeof *e->vars.ptr);
        e->vars.len--;
    }
    if (value)
        sk_strv_pushf(&e->vars, "%s=%s", name, value);
}

char *const *sk_envp_ptr(sk_envp *e)
{
    return sk_strv_argv(&e->vars);
}

void sk_envp_free(sk_envp *e)
{
    sk_strv_free(&e->vars);
}
