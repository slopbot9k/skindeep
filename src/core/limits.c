#include "limits.h"

#include <math.h>

bool sk_limit_span(double m, sk_buf *out)
{
    if (!isfinite(m) || m <= 0)
        return false;
    if (fmod(m, 1440) == 0) {
        sk_json_format_number(out, m / 1440);
        sk_buf_putc(out, 'd');
    } else if (fmod(m, 60) == 0) {
        sk_json_format_number(out, m / 60);
        sk_buf_putc(out, 'h');
    } else {
        sk_json_format_number(out, m);
        sk_buf_putc(out, 'm');
    }
    return true;
}

static bool given(const char *s)
{
    return s && *s;
}

void sk_limits_add(sk_limits *l, const char *key, const char *window, const char *model,
                   const char *label, double pct, double resets_at)
{
    if (!isfinite(pct))
        return;
    sk_buf lab = {0};
    if (given(label)) {
        sk_buf_append_c(&lab, label);
    } else {
        if (given(model))
            sk_buf_append_c(&lab, model);
        if (given(window)) {
            if (lab.len)
                sk_buf_putc(&lab, ' ');
            sk_buf_append_c(&lab, window);
        }
        if (!lab.len)
            sk_buf_append_c(&lab, key);
    }
    sk_jw w;
    sk_jw_init(&w, &l->json, 0);
    w.depth = 1; /* inside the array the list will close */
    w.first = l->n == 0;
    if (l->n == 0)
        sk_buf_putc(&l->json, '[');
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("key"));
    sk_jw_string(&w, sk_str_c(key));
    sk_jw_key(&w, SK_STR("label"));
    sk_jw_string(&w, sk_buf_str(&lab));
    sk_jw_key(&w, SK_STR("window"));
    if (given(window))
        sk_jw_string(&w, sk_str_c(window));
    else
        sk_jw_null(&w);
    sk_jw_key(&w, SK_STR("model"));
    if (given(model))
        sk_jw_string(&w, sk_str_c(model));
    else
        sk_jw_null(&w);
    sk_jw_key(&w, SK_STR("pct"));
    sk_jw_number(&w, pct);
    sk_jw_key(&w, SK_STR("resets_at"));
    if (isfinite(resets_at))
        sk_jw_number(&w, resets_at);
    else
        sk_jw_null(&w);
    sk_jw_object_end(&w);
    l->n++;
    sk_buf_free(&lab);
}

void sk_limits_set(sk_limits *l, sk_source *src)
{
    if (!l->n) {
        sk_source_set_null(src, "limits.items");
        return;
    }
    sk_buf list = {0};
    sk_buf_append_str(&list, sk_buf_str(&l->json));
    sk_buf_putc(&list, ']');
    sk_source_set_list(src, "limits.items", sk_buf_str(&list));
    sk_buf_free(&list);
}

void sk_limits_free(sk_limits *l)
{
    sk_buf_free(&l->json);
    l->n = 0;
}
