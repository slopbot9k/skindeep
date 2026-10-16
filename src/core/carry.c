#include "carry.h"

#include <string.h>

static const sk_option *lookup(const sk_option *table, size_t n, sk_str name)
{
    for (size_t i = 0; i < n; i++)
        if (sk_str_eq_c(name, table[i].name))
            return &table[i];
    return NULL;
}

void sk_carry_pick(char *const *argv, const sk_option *table, size_t n, sk_strv *args, sk_strv *dropped)
{
    for (size_t i = 0; argv && argv[i]; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) {
            for (; argv[i]; i++)
                sk_strv_push(dropped, argv[i]);
            break;
        }
        if (a[0] != '-' || !strcmp(a, "-")) {
            sk_strv_push(dropped, a);
            continue;
        }
        bool is_long = a[1] == '-';
        const char *eq = is_long ? strchr(a, '=') : NULL;
        bool attached = eq && eq > a;
        sk_str name = eq ? sk_str_n(a, (size_t)(eq - a)) : sk_str_c(a);
        const sk_option *o = lookup(table, n, name);
        sk_arity arity = o ? o->arity : SK_ARITY_FLAG;
        if (!o && !is_long && strlen(a) > 2) {
            const sk_option *s = lookup(table, n, sk_str_n(a, 2));
            if (s && s->arity == SK_ARITY_ONE) {
                o = s;
                arity = SK_ARITY_ONE;
                attached = true;
            }
        }
        if (!o) {
            sk_strv_push(dropped, a);
            continue;
        }
        sk_strv_push(args, a);
        if (attached)
            continue;
#define MORE (argv[i + 1] && argv[i + 1][0] != '-')
        if (arity == SK_ARITY_ONE && argv[i + 1])
            sk_strv_push(args, argv[++i]);
        else if (arity == SK_ARITY_OPTIONAL && MORE)
            sk_strv_push(args, argv[++i]);
        else if (arity == SK_ARITY_MANY)
            while (MORE)
                sk_strv_push(args, argv[++i]);
#undef MORE
    }
}
