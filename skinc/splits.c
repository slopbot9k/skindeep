#include "splits.h"

#include <math.h>

/* sk_pack_rect's keys: left, right, width, top, bottom, height. */
int sk_split_edge_key(uint8_t edge)
{
    static const int KEY[] = {0, 1, 3, 4};
    return KEY[edge & 3];
}

int sk_split_size_key(uint8_t edge)
{
    return sk_split_horizontal(edge) ? 2 : 5;
}

bool sk_split_horizontal(uint8_t edge)
{
    return edge == SK_SPLIT_LEFT || edge == SK_SPLIT_RIGHT;
}

void sk_split_range(const sk_skin *skin, const sk_pack_split *s, double W, double H, double *min, double *max,
                    double *def)
{
    double d = skin->terminal_rect.v[sk_split_edge_key(s->edge)];
    double shortfall = sk_split_horizontal(s->edge) ? skin->width - W : skin->height - SK_STRIP - H;
    *def = d;
    *min = s->min;
    *max = fmax(d, fmin(s->max, s->max - shortfall));
}

void sk_split_sizes(const sk_skin *skin, const double *saved, double W, double H, double *out)
{
    for (size_t i = 0; i < skin->nsplits; i++) {
        double min, max, def;
        sk_split_range(skin, &skin->splits[i], W, H, &min, &max, &def);
        double v = saved && isfinite(saved[i]) ? saved[i] : def;
        out[i] = fmin(max, fmax(min, v));
    }
}

sk_pack_rect sk_split_rect(const sk_skin *skin, const sk_pack_rect *r, const double *at)
{
    sk_pack_rect out = *r;
    for (int key = 0; key < 6; key++) {
        int s = r->follow[key];
        if (s < 0 || (size_t)s >= skin->nsplits || !(r->has & (1 << key)))
            continue;
        double def = skin->terminal_rect.v[sk_split_edge_key(skin->splits[s].edge)];
        out.v[key] = (float)(out.v[key] + (at[s] - def));
    }
    return out;
}

sk_pack_rect sk_split_terminal(const sk_skin *skin, const double *at)
{
    sk_pack_rect out = skin->terminal_rect;
    for (size_t i = 0; i < skin->nsplits; i++)
        out.v[sk_split_edge_key(skin->splits[i].edge)] = (float)at[i];
    return out;
}

bool sk_split_movable(uint8_t edge, int key, const sk_pack_rect *r)
{
    int e = sk_split_edge_key(edge), z = sk_split_size_key(edge);
    if (key == e)
        return r->has & (1 << e);
    return key == z && (r->has & (1 << e)) && (r->has & (1 << z));
}
