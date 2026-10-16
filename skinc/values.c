/* The value types of CSS Values and Units 4, Color 4, Images 3 and
 * Transforms 1 that decision 0037's properties take, each parsed from
 * component values into its computed form. */
#include "props.h"

#include <math.h>
#include <string.h>

/* ---- The cursor ---- */

static bool ws(const sk_cv *c)
{
    return c->kind == SK_CV_TOKEN && c->tok.type == SK_TOK_WHITESPACE;
}

void sk_vcur_init(sk_vcur *v, const sk_cv *cv, size_t n)
{
    v->cv = cv;
    v->n = n;
    v->i = 0;
}

const sk_cv *sk_vcur_peek(sk_vcur *v)
{
    while (v->i < v->n && ws(&v->cv[v->i]))
        v->i++;
    return v->i < v->n ? &v->cv[v->i] : NULL;
}

const sk_cv *sk_vcur_next(sk_vcur *v)
{
    const sk_cv *c = sk_vcur_peek(v);
    if (c)
        v->i++;
    return c;
}

bool sk_vcur_end(sk_vcur *v)
{
    return sk_vcur_peek(v) == NULL;
}

bool sk_vcur_comma(sk_vcur *v)
{
    const sk_cv *c = sk_vcur_peek(v);
    if (c && c->kind == SK_CV_TOKEN && c->tok.type == SK_TOK_COMMA) {
        v->i++;
        return true;
    }
    return false;
}

sk_vcur sk_vcur_item(sk_vcur *v)
{
    sk_vcur item;
    size_t start = v->i;
    while (v->i < v->n && !(v->cv[v->i].kind == SK_CV_TOKEN && v->cv[v->i].tok.type == SK_TOK_COMMA))
        v->i++;
    sk_vcur_init(&item, v->cv + start, v->i - start);
    if (v->i < v->n)
        v->i++;
    return item;
}

bool sk_v_ident(const sk_cv *v, const char *lower)
{
    return v && v->kind == SK_CV_TOKEN && v->tok.type == SK_TOK_IDENT && sk_ident_is(v->tok.text, lower);
}

static bool is(const sk_cv *v, uint8_t type)
{
    return v && v->kind == SK_CV_TOKEN && v->tok.type == type;
}

static bool fn(const sk_cv *v, const char *lower)
{
    return v && v->kind == SK_CV_FUNCTION && sk_ident_is(v->tok.text, lower);
}

static bool delim(const sk_cv *v, uint32_t ch)
{
    return is(v, SK_TOK_DELIM) && v->tok.delim == ch;
}

static bool finite_f(double d)
{
    return isfinite(d) && fabs(d) < 1e30;
}

/* ---- Numbers ---- */

bool sk_v_number(const sk_cv *v, float *out)
{
    if (!is(v, SK_TOK_NUMBER) || !finite_f(v->tok.num))
        return false;
    *out = (float)v->tok.num;
    return true;
}

bool sk_v_integer(const sk_cv *v, int32_t *out)
{
    if (!is(v, SK_TOK_NUMBER) || !v->tok.integer || !(fabs(v->tok.num) <= 2147483647.0))
        return false;
    *out = (int32_t)v->tok.num;
    return true;
}

bool sk_v_factor(const sk_cv *v, float *out)
{
    if (is(v, SK_TOK_PERCENTAGE) && finite_f(v->tok.num)) {
        *out = (float)(v->tok.num / 100);
        return true;
    }
    return sk_v_number(v, out);
}

bool sk_v_angle(const sk_cv *v, bool zero_ok, float *out)
{
    if (zero_ok && is(v, SK_TOK_NUMBER) && v->tok.num == 0) {
        *out = 0;
        return true;
    }
    if (!is(v, SK_TOK_DIMENSION) || !finite_f(v->tok.num))
        return false;
    double d = v->tok.num;
    sk_str u = v->tok.unit;
    if (sk_ident_is(u, "deg"))
        *out = (float)d;
    else if (sk_ident_is(u, "rad"))
        *out = (float)(d * 180 / M_PI);
    else if (sk_ident_is(u, "grad"))
        *out = (float)(d * 0.9);
    else if (sk_ident_is(u, "turn"))
        *out = (float)(d * 360);
    else
        return false;
    return true;
}

bool sk_v_time(const sk_cv *v, float *out)
{
    if (!is(v, SK_TOK_DIMENSION) || !finite_f(v->tok.num))
        return false;
    if (sk_ident_is(v->tok.unit, "s"))
        *out = (float)v->tok.num;
    else if (sk_ident_is(v->tok.unit, "ms"))
        *out = (float)(v->tok.num / 1000);
    else
        return false;
    return true;
}

/* ---- Lengths and calc() ---- */

/* A dimension's length in px, with em of font_size. */
static bool unit_px(sk_vctx *c, double num, sk_str u, double *px)
{
    static const struct {
        const char *unit;
        double px;
    } ABS[] = {{"px", 1}, {"in", 96}, {"cm", 96 / 2.54}, {"mm", 96 / 25.4}, {"q", 96 / 101.6}, {"pt", 96.0 / 72},
               {"pc", 16}};
    for (size_t i = 0; i < SK_LEN(ABS); i++)
        if (sk_ident_is(u, ABS[i].unit)) {
            *px = num * ABS[i].px;
            return true;
        }
    if (sk_ident_is(u, "em")) {
        *px = num * c->font_size;
        return true;
    }
    if (sk_ident_is(u, "rem")) {
        /* The document's root, which skins cannot style: 16px. */
        *px = num * 16;
        return true;
    }
    return false;
}

typedef struct {
    double px, pct, num;
    bool length; /* else a number */
} term;

static bool calc_sum(sk_vctx *c, sk_vcur *v, term *out, int depth);

static bool calc_value(sk_vctx *c, sk_vcur *v, term *out, int depth)
{
    const sk_cv *x = sk_vcur_next(v);
    memset(out, 0, sizeof *out);
    if (!x || depth > 8)
        return false;
    if (is(x, SK_TOK_NUMBER)) {
        out->num = x->tok.num;
        return finite_f(x->tok.num);
    }
    if (is(x, SK_TOK_PERCENTAGE)) {
        out->length = true;
        out->pct = x->tok.num;
        return finite_f(x->tok.num);
    }
    if (is(x, SK_TOK_DIMENSION)) {
        out->length = true;
        return unit_px(c, x->tok.num, x->tok.unit, &out->px) && finite_f(out->px);
    }
    if ((x->kind == SK_CV_BLOCK && x->tok.type == SK_TOK_LPAREN) || fn(x, "calc")) {
        sk_vcur in;
        sk_vcur_init(&in, x->kids, x->n);
        return calc_sum(c, &in, out, depth + 1) && sk_vcur_end(&in);
    }
    return false;
}

static bool calc_product(sk_vctx *c, sk_vcur *v, term *out, int depth)
{
    if (!calc_value(c, v, out, depth))
        return false;
    for (;;) {
        const sk_cv *op = sk_vcur_peek(v);
        if (!delim(op, '*') && !delim(op, '/'))
            return true;
        sk_vcur_next(v);
        term r;
        if (!calc_value(c, v, &r, depth))
            return false;
        if (delim(op, '*')) {
            if (out->length && r.length)
                return false;
            if (r.length) {
                term t = r;
                r = *out;
                *out = t;
            }
            out->px *= r.num;
            out->pct *= r.num;
            out->num *= r.num;
        } else {
            if (r.length || r.num == 0)
                return false;
            out->px /= r.num;
            out->pct /= r.num;
            out->num /= r.num;
        }
    }
}

static bool calc_sum(sk_vctx *c, sk_vcur *v, term *out, int depth)
{
    if (!calc_product(c, v, out, depth))
        return false;
    for (;;) {
        const sk_cv *op = sk_vcur_peek(v);
        if (!delim(op, '+') && !delim(op, '-'))
            return true;
        sk_vcur_next(v);
        term r;
        if (!calc_product(c, v, &r, depth) || r.length != out->length)
            return false;
        double sign = delim(op, '-') ? -1 : 1;
        out->px += sign * r.px;
        out->pct += sign * r.pct;
        out->num += sign * r.num;
    }
}

bool sk_v_len(sk_vctx *c, const sk_cv *v, unsigned flags, sk_css_len *out)
{
    memset(out, 0, sizeof *out);
    out->kind = SK_LEN_PX;
    if (!v)
        return false;
    if (is(v, SK_TOK_IDENT)) {
        if ((flags & SK_L_AUTO) && sk_ident_is(v->tok.text, "auto"))
            out->kind = SK_LEN_AUTO;
        else if ((flags & SK_L_NONE) && sk_ident_is(v->tok.text, "none"))
            out->kind = SK_LEN_NONE;
        else if ((flags & SK_L_NORMAL) && sk_ident_is(v->tok.text, "normal"))
            out->kind = SK_LEN_NORMAL;
        else
            return false;
        return true;
    }
    double px = 0, pct = 0;
    bool literal = true;
    if (is(v, SK_TOK_NUMBER)) {
        if (v->tok.num != 0 && !(flags & SK_L_NUMBER))
            return false;
        px = v->tok.num;
    } else if (is(v, SK_TOK_PERCENTAGE)) {
        if (!(flags & SK_L_PCT))
            return false;
        pct = v->tok.num;
    } else if (is(v, SK_TOK_DIMENSION)) {
        if (!unit_px(c, v->tok.num, v->tok.unit, &px))
            return false;
    } else if (fn(v, "calc")) {
        sk_vcur in;
        sk_vcur_init(&in, v->kids, v->n);
        term t;
        if (!calc_sum(c, &in, &t, 0) || !sk_vcur_end(&in))
            return false;
        if (!t.length) {
            if (t.num != 0 && !(flags & SK_L_NUMBER))
                return false;
            t.px = t.num;
        }
        if (t.pct != 0 && !(flags & SK_L_PCT))
            return false;
        px = t.px;
        pct = t.pct;
        /* calc() clamps a negative result where negatives are not
         * allowed, rather than refusing it. */
        literal = false;
    } else if (c->internal && fn(v, "-sk-rect")) {
        sk_vcur in;
        sk_vcur_init(&in, v->kids, v->n);
        float f;
        if (!sk_v_number(sk_vcur_next(&in), &f) || !sk_vcur_end(&in))
            return false;
        out->kind = SK_LEN_RECT;
        out->px = f;
        return true;
    } else if (c->internal && fn(v, "-sk-value")) {
        out->kind = SK_LEN_VALUE;
        return true;
    } else {
        return false;
    }
    if (!finite_f(px) || !finite_f(pct))
        return false;
    if (literal && !(flags & SK_L_NEG) && (px < 0 || pct < 0))
        return false;
    out->px = (float)px;
    out->pct = (float)pct;
    return true;
}

/* ---- Colours ---- */

static const struct {
    const char *name;
    uint32_t rgb;
} NAMED[] = {
    {"aliceblue", 0xf0f8ff},      {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff},           {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff},          {"beige", 0xf5f5dc},        {"bisque", 0xffe4c4},         {"black", 0x000000},
    {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff},         {"blueviolet", 0x8a2be2},     {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887},      {"cadetblue", 0x5f9ea0},    {"chartreuse", 0x7fff00},     {"chocolate", 0xd2691e},
    {"coral", 0xff7f50},          {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc},     {"crimson", 0xdc143c},
    {"cyan", 0x00ffff},           {"darkblue", 0x00008b},     {"darkcyan", 0x008b8b},       {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9},       {"darkgreen", 0x006400},    {"darkgrey", 0xa9a9a9},       {"darkkhaki", 0xbdb76b},
    {"darkmagenta", 0x8b008b},    {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00},   {"darkorchid", 0x9932cc},
    {"darkred", 0x8b0000},        {"darksalmon", 0xe9967a},   {"darkseagreen", 0x8fbc8f},   {"darkslateblue", 0x483d8b},
    {"darkslategray", 0x2f4f4f},  {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1}, {"darkviolet", 0x9400d3},
    {"deeppink", 0xff1493},       {"deepskyblue", 0x00bfff},  {"dimgray", 0x696969},        {"dimgrey", 0x696969},
    {"dodgerblue", 0x1e90ff},     {"firebrick", 0xb22222},    {"floralwhite", 0xfffaf0},    {"forestgreen", 0x228b22},
    {"fuchsia", 0xff00ff},        {"gainsboro", 0xdcdcdc},    {"ghostwhite", 0xf8f8ff},     {"gold", 0xffd700},
    {"goldenrod", 0xdaa520},      {"gray", 0x808080},         {"green", 0x008000},          {"greenyellow", 0xadff2f},
    {"grey", 0x808080},           {"honeydew", 0xf0fff0},     {"hotpink", 0xff69b4},        {"indianred", 0xcd5c5c},
    {"indigo", 0x4b0082},         {"ivory", 0xfffff0},        {"khaki", 0xf0e68c},          {"lavender", 0xe6e6fa},
    {"lavenderblush", 0xfff0f5},  {"lawngreen", 0x7cfc00},    {"lemonchiffon", 0xfffacd},   {"lightblue", 0xadd8e6},
    {"lightcoral", 0xf08080},     {"lightcyan", 0xe0ffff},    {"lightgoldenrodyellow", 0xfafad2},
    {"lightgray", 0xd3d3d3},      {"lightgreen", 0x90ee90},   {"lightgrey", 0xd3d3d3},      {"lightpink", 0xffb6c1},
    {"lightsalmon", 0xffa07a},    {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa},  {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899}, {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0},  {"lime", 0x00ff00},
    {"limegreen", 0x32cd32},      {"linen", 0xfaf0e6},        {"magenta", 0xff00ff},        {"maroon", 0x800000},
    {"mediumaquamarine", 0x66cdaa}, {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3},   {"mediumpurple", 0x9370db},
    {"mediumseagreen", 0x3cb371}, {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a},
    {"mediumturquoise", 0x48d1cc}, {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970},
    {"mintcream", 0xf5fffa},      {"mistyrose", 0xffe4e1},    {"moccasin", 0xffe4b5},       {"navajowhite", 0xffdead},
    {"navy", 0x000080},           {"oldlace", 0xfdf5e6},      {"olive", 0x808000},          {"olivedrab", 0x6b8e23},
    {"orange", 0xffa500},         {"orangered", 0xff4500},    {"orchid", 0xda70d6},         {"palegoldenrod", 0xeee8aa},
    {"palegreen", 0x98fb98},      {"paleturquoise", 0xafeeee}, {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5},
    {"peachpuff", 0xffdab9},      {"peru", 0xcd853f},         {"pink", 0xffc0cb},           {"plum", 0xdda0dd},
    {"powderblue", 0xb0e0e6},     {"purple", 0x800080},       {"rebeccapurple", 0x663399},  {"red", 0xff0000},
    {"rosybrown", 0xbc8f8f},      {"royalblue", 0x4169e1},    {"saddlebrown", 0x8b4513},    {"salmon", 0xfa8072},
    {"sandybrown", 0xf4a460},     {"seagreen", 0x2e8b57},     {"seashell", 0xfff5ee},       {"sienna", 0xa0522d},
    {"silver", 0xc0c0c0},         {"skyblue", 0x87ceeb},      {"slateblue", 0x6a5acd},      {"slategray", 0x708090},
    {"slategrey", 0x708090},      {"snow", 0xfffafa},         {"springgreen", 0x00ff7f},    {"steelblue", 0x4682b4},
    {"tan", 0xd2b48c},            {"teal", 0x008080},         {"thistle", 0xd8bfd8},        {"tomato", 0xff6347},
    {"turquoise", 0x40e0d0},      {"violet", 0xee82ee},       {"wheat", 0xf5deb3},          {"white", 0xffffff},
    {"whitesmoke", 0xf5f5f5},     {"yellow", 0xffff00},       {"yellowgreen", 0x9acd32},
};

static sk_css_color rgb_hex(uint32_t rgb, float a)
{
    sk_css_color c = {(float)((rgb >> 16) & 255) / 255, (float)((rgb >> 8) & 255) / 255, (float)(rgb & 255) / 255, a};
    return c;
}

bool sk_named_color(sk_str name, sk_css_color *out)
{
    if (sk_ident_is(name, "transparent")) {
        *out = rgb_hex(0, 0);
        return true;
    }
    for (size_t i = 0; i < SK_LEN(NAMED); i++)
        if (sk_ident_is(name, NAMED[i].name)) {
            *out = rgb_hex(NAMED[i].rgb, 1);
            return true;
        }
    return false;
}

static float clamp01(double v)
{
    return (float)(v < 0 ? 0 : v > 1 ? 1 : v);
}

static bool hex_color(sk_str h, sk_css_color *out)
{
    unsigned d[8];
    if (h.len != 3 && h.len != 4 && h.len != 6 && h.len != 8)
        return false;
    for (size_t i = 0; i < h.len; i++) {
        char ch = h.ptr[i];
        if (ch >= '0' && ch <= '9')
            d[i] = (unsigned)(ch - '0');
        else if ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'f')
            d[i] = (unsigned)((ch | 0x20) - 'a' + 10);
        else
            return false;
    }
    unsigned ch[4] = {0, 0, 0, 255};
    if (h.len <= 4)
        for (size_t i = 0; i < h.len; i++)
            ch[i] = d[i] * 17;
    else
        for (size_t i = 0; i < h.len / 2; i++)
            ch[i] = d[2 * i] * 16 + d[2 * i + 1];
    out->r = (float)ch[0] / 255;
    out->g = (float)ch[1] / 255;
    out->b = (float)ch[2] / 255;
    out->a = (float)ch[3] / 255;
    return true;
}

/* The arguments of rgb() or hsl(): three channels and an alpha, comma
 * separated (the legacy syntax) or space separated with / before the
 * alpha. pct: whether each channel was a percentage. */
static bool color_args(const sk_cv *f, double ch[4], bool pct[3], bool hue)
{
    sk_vcur v;
    sk_vcur_init(&v, f->kids, f->n);
    const sk_cv *x[4] = {0};
    int count = 0;
    bool commas = false, slash = false;
    for (int k = 0; k < 4; k++) {
        const sk_cv *a = sk_vcur_next(&v);
        if (!a)
            break;
        x[count++] = a;
        const sk_cv *sep = sk_vcur_peek(&v);
        if (!sep)
            break;
        if (is(sep, SK_TOK_COMMA)) {
            if (k == 0)
                commas = true;
            else if (!commas)
                return false;
            sk_vcur_next(&v);
        } else if (delim(sep, '/')) {
            if (commas || k != 2)
                return false;
            slash = true;
            sk_vcur_next(&v);
        } else if (commas) {
            return false;
        }
    }
    if (!sk_vcur_end(&v) || count < 3 || (slash && count != 4))
        return false;
    for (int k = 0; k < 3; k++) {
        const sk_cv *a = x[k];
        if (k == 0 && hue) {
            float deg;
            if (sk_v_angle(a, false, &deg))
                ch[0] = deg;
            else if (is(a, SK_TOK_NUMBER))
                ch[0] = a->tok.num;
            else
                return false;
            pct[0] = false;
            continue;
        }
        if (is(a, SK_TOK_PERCENTAGE)) {
            ch[k] = a->tok.num;
            pct[k] = true;
        } else if (is(a, SK_TOK_NUMBER) && !hue) {
            ch[k] = a->tok.num;
            pct[k] = false;
        } else {
            return false;
        }
        if (!finite_f(ch[k]))
            return false;
    }
    /* The legacy syntax does not mix numbers and percentages. */
    if (commas && !hue && (pct[0] != pct[1] || pct[1] != pct[2]))
        return false;
    ch[3] = 1;
    if (count == 4) {
        const sk_cv *a = x[3];
        if (is(a, SK_TOK_PERCENTAGE))
            ch[3] = a->tok.num / 100;
        else if (is(a, SK_TOK_NUMBER))
            ch[3] = a->tok.num;
        else
            return false;
        if (!finite_f(ch[3]))
            return false;
    }
    return true;
}

static double hue_to_rgb(double m1, double m2, double h)
{
    if (h < 0)
        h += 1;
    if (h > 1)
        h -= 1;
    if (h * 6 < 1)
        return m1 + (m2 - m1) * h * 6;
    if (h * 2 < 1)
        return m2;
    if (h * 3 < 2)
        return m1 + (m2 - m1) * (2.0 / 3 - h) * 6;
    return m1;
}

bool sk_v_color(sk_vctx *c, const sk_cv *v, sk_css_color *out)
{
    if (!v)
        return false;
    if (is(v, SK_TOK_HASH))
        return hex_color(v->tok.text, out);
    if (is(v, SK_TOK_IDENT)) {
        if (sk_ident_is(v->tok.text, "currentcolor")) {
            *out = c->color;
            return true;
        }
        return sk_named_color(v->tok.text, out);
    }
    double ch[4];
    bool pct[3];
    if (fn(v, "rgb") || fn(v, "rgba")) {
        if (!color_args(v, ch, pct, false))
            return false;
        for (int k = 0; k < 3; k++)
            ch[k] = pct[k] ? ch[k] / 100 : ch[k] / 255;
        *out = (sk_css_color){clamp01(ch[0]), clamp01(ch[1]), clamp01(ch[2]), clamp01(ch[3])};
        return true;
    }
    if (fn(v, "hsl") || fn(v, "hsla")) {
        if (!color_args(v, ch, pct, true))
            return false;
        double h = fmod(fmod(ch[0], 360) + 360, 360) / 360;
        double s = ch[1] / 100, l = ch[2] / 100;
        s = s < 0 ? 0 : s > 1 ? 1 : s;
        l = l < 0 ? 0 : l > 1 ? 1 : l;
        double m2 = l <= 0.5 ? l * (s + 1) : l + s - l * s;
        double m1 = l * 2 - m2;
        *out = (sk_css_color){clamp01(hue_to_rgb(m1, m2, h + 1.0 / 3)), clamp01(hue_to_rgb(m1, m2, h)),
                              clamp01(hue_to_rgb(m1, m2, h - 1.0 / 3)), clamp01(ch[3])};
        return true;
    }
    return false;
}

/* ---- Images ---- */

static bool url_of(const sk_cv *v, sk_str *out)
{
    if (is(v, SK_TOK_URL)) {
        *out = v->tok.text;
        return true;
    }
    if (fn(v, "url")) {
        sk_vcur in;
        sk_vcur_init(&in, v->kids, v->n);
        const sk_cv *s = sk_vcur_next(&in);
        if (!is(s, SK_TOK_STRING) || !sk_vcur_end(&in))
            return false;
        *out = s->tok.text;
        return true;
    }
    return false;
}

/* A stop list from the cursor: colours, each with up to two positions;
 * a colour hint is refused. */
static bool stops(sk_vctx *c, sk_vcur *v, sk_css_gradient *g, bool angular)
{
    SK_VEC(sk_css_stop) list = {0};
    (void)angular;
    while (!sk_vcur_end(v)) {
        sk_vcur item = sk_vcur_item(v);
        sk_css_stop s;
        memset(&s, 0, sizeof s);
        s.pos.kind = SK_LEN_AUTO;
        if (!sk_v_color(c, sk_vcur_next(&item), &s.color)) {
            c->why = "a gradient's stop needs a colour first (colour hints are not supported)";
            sk_vec_free(&list);
            return false;
        }
        int npos = 0;
        sk_css_len pos[2];
        while (!sk_vcur_end(&item) && npos < 2) {
            if (!sk_v_len(c, sk_vcur_next(&item), SK_L_PCT | SK_L_NEG, &pos[npos])) {
                sk_vec_free(&list);
                return false;
            }
            npos++;
        }
        if (!sk_vcur_end(&item)) {
            sk_vec_free(&list);
            return false;
        }
        if (npos)
            s.pos = pos[0];
        sk_vec_push(&list, s);
        if (npos == 2) {
            s.pos = pos[1];
            sk_vec_push(&list, s);
        }
        if (list.len > SK_PACK_MAX_LIST)
            break;
    }
    if (list.len < 2 || list.len > SK_PACK_MAX_LIST) {
        c->why = "a gradient needs two colour stops at least";
        sk_vec_free(&list);
        return false;
    }
    g->nstops = (uint16_t)list.len;
    sk_css_stop *s = sk_arena_array(c->arena, list.len, sizeof *s);
    memcpy(s, list.ptr, list.len * sizeof *s);
    g->stops = s;
    sk_vec_free(&list);
    return true;
}

static bool linear(sk_vctx *c, const sk_cv *f, sk_css_gradient *g)
{
    sk_vcur v;
    sk_vcur_init(&v, f->kids, f->n);
    g->kind = SK_GRAD_LINEAR;
    g->angle = 180;
    const sk_cv *x = sk_vcur_peek(&v);
    float deg;
    if (sk_v_angle(x, true, &deg)) {
        sk_vcur_next(&v);
        g->angle = deg;
        if (!sk_vcur_comma(&v))
            return false;
    } else if (sk_v_ident(x, "to")) {
        sk_vcur_next(&v);
        int h = 0, vert = 0; /* -1 left/top, 1 right/bottom */
        for (int k = 0; k < 2; k++) {
            const sk_cv *s = sk_vcur_peek(&v);
            if (sk_v_ident(s, "left") && !h)
                h = -1;
            else if (sk_v_ident(s, "right") && !h)
                h = 1;
            else if (sk_v_ident(s, "top") && !vert)
                vert = -1;
            else if (sk_v_ident(s, "bottom") && !vert)
                vert = 1;
            else
                break;
            sk_vcur_next(&v);
        }
        if ((!h && !vert) || !sk_vcur_comma(&v))
            return false;
        if (h && vert)
            g->to_corner = (uint8_t)(4 | (h > 0 ? 1 : 0) | (vert > 0 ? 2 : 0));
        else
            g->angle = h ? (h > 0 ? 90.0f : 270.0f) : (vert > 0 ? 180.0f : 0.0f);
    }
    return stops(c, &v, g, false);
}

static bool radial(sk_vctx *c, const sk_cv *f, sk_css_gradient *g)
{
    sk_vcur v;
    sk_vcur_init(&v, f->kids, f->n);
    g->kind = SK_GRAD_RADIAL;
    g->extent = SK_EXTENT_FARTHEST_CORNER;
    g->cx = (sk_css_len){0, 50, SK_LEN_PX};
    g->cy = (sk_css_len){0, 50, SK_LEN_PX};
    bool shape = false, size = false, any = false;
    sk_css_len lens[2];
    int nlens = 0;
    for (;;) {
        const sk_cv *x = sk_vcur_peek(&v);
        if (!x || is(x, SK_TOK_COMMA) || sk_v_ident(x, "at"))
            break;
        if (!shape && (sk_v_ident(x, "circle") || sk_v_ident(x, "ellipse"))) {
            shape = true;
            g->circle = sk_v_ident(x, "circle");
        } else if (!size && is(x, SK_TOK_IDENT)) {
            static const struct {
                const char *name;
                uint8_t extent;
            } EXT[] = {{"closest-side", SK_EXTENT_CLOSEST_SIDE},
                       {"closest-corner", SK_EXTENT_CLOSEST_CORNER},
                       {"farthest-side", SK_EXTENT_FARTHEST_SIDE},
                       {"farthest-corner", SK_EXTENT_FARTHEST_CORNER}};
            bool hit = false;
            for (size_t i = 0; i < SK_LEN(EXT); i++)
                if (sk_v_ident(x, EXT[i].name)) {
                    g->extent = EXT[i].extent;
                    hit = true;
                }
            if (!hit)
                return false;
            size = true;
        } else if (!size && nlens < 2 && sk_v_len(c, x, SK_L_PCT, &lens[nlens])) {
            nlens++;
            const sk_cv *y = NULL;
            sk_vcur peek = v;
            sk_vcur_next(&peek);
            y = sk_vcur_peek(&peek);
            if (nlens == 1 && y && sk_v_len(c, y, SK_L_PCT, &lens[1])) {
                sk_vcur_next(&v);
                nlens = 2;
            }
            size = true;
        } else {
            return false;
        }
        any = true;
        sk_vcur_next(&v);
    }
    if (nlens) {
        g->extent = SK_EXTENT_EXPLICIT;
        if (nlens == 1) {
            if (shape && !g->circle)
                return false;
            if (lens[0].pct != 0)
                return false; /* a circle's radius is a length */
            g->circle = 1;
            g->rx = g->ry = lens[0];
        } else {
            if (shape && g->circle)
                return false;
            g->circle = 0;
            g->rx = lens[0];
            g->ry = lens[1];
        }
    }
    if (sk_v_ident(sk_vcur_peek(&v), "at")) {
        sk_vcur_next(&v);
        sk_css_position p;
        if (!sk_v_position(c, &v, &p))
            return false;
        /* Kept from the top left: an edge offset from the end becomes
         * 100% less it. */
        g->cx = p.x;
        g->cy = p.y;
        if (p.x_edge == SK_EDGE_END)
            g->cx = (sk_css_len){-p.x.px, 100 - p.x.pct, SK_LEN_PX};
        if (p.y_edge == SK_EDGE_END)
            g->cy = (sk_css_len){-p.y.px, 100 - p.y.pct, SK_LEN_PX};
        any = true;
    }
    if (any && !sk_vcur_comma(&v))
        return false;
    return stops(c, &v, g, false);
}

bool sk_v_image(sk_vctx *c, const sk_cv *v, bool none_ok, sk_css_image *out)
{
    if (!v)
        return false;
    if (none_ok && sk_v_ident(v, "none")) {
        memset(out, 0, sizeof *out);
        out->kind = SK_IMAGE_NONE;
        return true;
    }
    sk_str url;
    if (url_of(v, &url)) {
        uint32_t index = 0;
        if (!c->image || !c->image(c->data, url, &index)) {
            c->why = "the url names no image of the skin";
            return false;
        }
        memset(out, 0, sizeof *out);
        out->kind = SK_IMAGE_URL;
        out->source = index;
        return true;
    }
    bool lin = fn(v, "linear-gradient"), rlin = fn(v, "repeating-linear-gradient");
    bool rad = fn(v, "radial-gradient"), rrad = fn(v, "repeating-radial-gradient");
    if (!lin && !rlin && !rad && !rrad)
        return false;
    sk_css_gradient *g = sk_arena_zalloc(c->arena, sizeof *g);
    g->repeating = rlin || rrad;
    if (!((lin || rlin) ? linear(c, v, g) : radial(c, v, g)))
        return false;
    memset(out, 0, sizeof *out);
    out->kind = SK_IMAGE_GRADIENT;
    out->gradient = g;
    return true;
}

/* ---- Positions ---- */

typedef enum { K_NONE, K_LEFT, K_RIGHT, K_TOP, K_BOTTOM, K_CENTER } pos_kw;

static pos_kw pos_keyword(const sk_cv *v)
{
    if (sk_v_ident(v, "left"))
        return K_LEFT;
    if (sk_v_ident(v, "right"))
        return K_RIGHT;
    if (sk_v_ident(v, "top"))
        return K_TOP;
    if (sk_v_ident(v, "bottom"))
        return K_BOTTOM;
    if (sk_v_ident(v, "center"))
        return K_CENTER;
    return K_NONE;
}

static sk_css_len pct(float p)
{
    sk_css_len l = {0, p, SK_LEN_PX};
    return l;
}

/* Up to four values, as CSS Backgrounds 3's <position>. */
bool sk_v_position(sk_vctx *c, sk_vcur *v, sk_css_position *out)
{
    pos_kw kw[4];
    sk_css_len len[4];
    bool has_len[4];
    int n = 0;
    while (n < 4) {
        const sk_cv *x = sk_vcur_peek(v);
        if (!x)
            break;
        pos_kw k = pos_keyword(x);
        sk_css_len l;
        if (k != K_NONE) {
            kw[n] = k;
            has_len[n] = false;
        } else if (sk_v_len(c, x, SK_L_PCT | SK_L_NEG, &l)) {
            kw[n] = K_NONE;
            len[n] = l;
            has_len[n] = true;
        } else {
            break;
        }
        sk_vcur_next(v);
        n++;
    }
    memset(out, 0, sizeof *out);
    out->x = out->y = pct(50);
    if (n == 0)
        return false;
    if (n == 1) {
        if (has_len[0]) {
            out->x = len[0];
        } else if (kw[0] == K_TOP || kw[0] == K_BOTTOM) {
            out->y = pct(kw[0] == K_TOP ? 0 : 100);
        } else if (kw[0] != K_CENTER) {
            out->x = pct(kw[0] == K_LEFT ? 0 : 100);
        }
        return true;
    }
    if (n == 2) {
        /* x then y, unless the keywords say otherwise. */
        pos_kw a = kw[0], b = kw[1];
        bool swap = (a == K_TOP || a == K_BOTTOM) || (b == K_LEFT || b == K_RIGHT);
        int xi = swap ? 1 : 0, yi = swap ? 0 : 1;
        if (swap && (has_len[0] || has_len[1]))
            return false;
        pos_kw xk = kw[xi], yk = kw[yi];
        if (xk == K_TOP || xk == K_BOTTOM || yk == K_LEFT || yk == K_RIGHT)
            return false;
        out->x = has_len[xi] ? len[xi] : pct(xk == K_LEFT ? 0 : xk == K_RIGHT ? 100 : 50);
        out->y = has_len[yi] ? len[yi] : pct(yk == K_TOP ? 0 : yk == K_BOTTOM ? 100 : 50);
        return true;
    }
    /* Three or four: keywords, each but center followed by an offset. */
    sk_css_len offset[2] = {pct(0), pct(0)};
    pos_kw side[2] = {K_NONE, K_NONE};
    int s = 0;
    for (int i = 0; i < n; i++) {
        if (has_len[i])
            return false;
        if (s >= 2)
            return false;
        side[s] = kw[i];
        if (i + 1 < n && has_len[i + 1]) {
            if (kw[i] == K_CENTER)
                return false;
            offset[s] = len[i + 1];
            i++;
        }
        s++;
    }
    if (s != 2)
        return false;
    for (int k = 0; k < 2; k++) {
        pos_kw sd = side[k];
        bool horiz = sd == K_LEFT || sd == K_RIGHT;
        if (sd == K_CENTER)
            horiz = k == 0 ? !(side[1] == K_LEFT || side[1] == K_RIGHT) : (side[0] == K_TOP || side[0] == K_BOTTOM);
        if (horiz) {
            out->x = sd == K_CENTER ? pct(50) : offset[k];
            out->x_edge = sd == K_RIGHT ? SK_EDGE_END : SK_EDGE_START;
        } else {
            out->y = sd == K_CENTER ? pct(50) : offset[k];
            out->y_edge = sd == K_BOTTOM ? SK_EDGE_END : SK_EDGE_START;
        }
    }
    bool h0 = side[0] == K_LEFT || side[0] == K_RIGHT, h1 = side[1] == K_LEFT || side[1] == K_RIGHT;
    bool v0 = side[0] == K_TOP || side[0] == K_BOTTOM, v1 = side[1] == K_TOP || side[1] == K_BOTTOM;
    if ((h0 && h1) || (v0 && v1))
        return false;
    return true;
}

/* ---- Shadows ---- */

bool sk_v_shadow(sk_vctx *c, sk_vcur *v, bool box, sk_css_shadow *out)
{
    memset(out, 0, sizeof *out);
    out->color = c->color;
    bool inset = false, color = false;
    int nlen = 0;
    float lens[4] = {0};
    while (!sk_vcur_end(v)) {
        const sk_cv *x = sk_vcur_peek(v);
        sk_css_len l;
        if (box && !inset && sk_v_ident(x, "inset")) {
            inset = true;
            sk_vcur_next(v);
        } else if (!color && sk_v_color(c, x, &out->color)) {
            color = true;
            sk_vcur_next(v);
        } else if (nlen == 0 && sk_v_len(c, x, SK_L_NEG, &l)) {
            /* The lengths come together. */
            while (nlen < (box ? 4 : 3) && sk_v_len(c, sk_vcur_peek(v), SK_L_NEG, &l)) {
                lens[nlen++] = l.px;
                sk_vcur_next(v);
            }
        } else {
            return false;
        }
    }
    if (nlen < 2)
        return false;
    if (nlen >= 3 && lens[2] < 0)
        return false;
    out->x = lens[0];
    out->y = lens[1];
    out->blur = lens[2];
    out->spread = lens[3];
    out->inset = inset;
    return true;
}

/* ---- Easing ---- */

static bool timing_of(const sk_cv *v, sk_css_timing *out);

/* Leaves *out alone unless v is an easing function. */
bool sk_v_timing(const sk_cv *v, sk_css_timing *out)
{
    sk_css_timing t;
    if (!timing_of(v, &t))
        return false;
    *out = t;
    return true;
}

static bool timing_of(const sk_cv *v, sk_css_timing *out)
{
    memset(out, 0, sizeof *out);
    static const struct {
        const char *name;
        float x1, y1, x2, y2;
    } CUBIC[] = {{"ease", .25f, .1f, .25f, 1},
                 {"ease-in", .42f, 0, 1, 1},
                 {"ease-out", 0, 0, .58f, 1},
                 {"ease-in-out", .42f, 0, .58f, 1}};
    if (sk_v_ident(v, "linear")) {
        out->kind = SK_TIMING_LINEAR;
        return true;
    }
    for (size_t i = 0; i < SK_LEN(CUBIC); i++)
        if (sk_v_ident(v, CUBIC[i].name)) {
            *out = (sk_css_timing){SK_TIMING_CUBIC, 0, 0, CUBIC[i].x1, CUBIC[i].y1, CUBIC[i].x2, CUBIC[i].y2};
            return true;
        }
    if (sk_v_ident(v, "step-start") || sk_v_ident(v, "step-end")) {
        out->kind = SK_TIMING_STEPS;
        out->steps = 1;
        out->steps_pos = sk_v_ident(v, "step-start") ? SK_STEP_JUMP_START : SK_STEP_JUMP_END;
        return true;
    }
    if (fn(v, "cubic-bezier")) {
        sk_vcur in;
        sk_vcur_init(&in, v->kids, v->n);
        float p[4];
        for (int k = 0; k < 4; k++) {
            if (k && !sk_vcur_comma(&in))
                return false;
            if (!sk_v_number(sk_vcur_next(&in), &p[k]))
                return false;
        }
        if (!sk_vcur_end(&in) || p[0] < 0 || p[0] > 1 || p[2] < 0 || p[2] > 1)
            return false;
        *out = (sk_css_timing){SK_TIMING_CUBIC, 0, 0, p[0], p[1], p[2], p[3]};
        return true;
    }
    if (fn(v, "steps")) {
        sk_vcur in;
        sk_vcur_init(&in, v->kids, v->n);
        int32_t n;
        if (!sk_v_integer(sk_vcur_next(&in), &n) || n < 1 || n > 65535)
            return false;
        out->kind = SK_TIMING_STEPS;
        out->steps = (uint16_t)n;
        out->steps_pos = SK_STEP_JUMP_END;
        if (sk_vcur_comma(&in)) {
            const sk_cv *p = sk_vcur_next(&in);
            if (sk_v_ident(p, "jump-start") || sk_v_ident(p, "start"))
                out->steps_pos = SK_STEP_JUMP_START;
            else if (sk_v_ident(p, "jump-end") || sk_v_ident(p, "end"))
                out->steps_pos = SK_STEP_JUMP_END;
            else if (sk_v_ident(p, "jump-none"))
                out->steps_pos = SK_STEP_JUMP_NONE;
            else if (sk_v_ident(p, "jump-both"))
                out->steps_pos = SK_STEP_JUMP_BOTH;
            else
                return false;
            if (out->steps_pos == SK_STEP_JUMP_NONE && n < 2)
                return false;
        }
        return sk_vcur_end(&in);
    }
    return false;
}

/* ---- Transforms ---- */

/* The arguments of a function, comma separated, as a list of values. */
static int args(const sk_cv *f, const sk_cv **out, int max)
{
    sk_vcur in;
    sk_vcur_init(&in, f->kids, f->n);
    int n = 0;
    while (!sk_vcur_end(&in)) {
        if (n && !sk_vcur_comma(&in))
            return -1;
        const sk_cv *x = sk_vcur_next(&in);
        if (!x || n >= max)
            return -1;
        out[n++] = x;
    }
    return n;
}

static bool one_transform(sk_vctx *c, const sk_cv *f, sk_css_xf *x)
{
    memset(x, 0, sizeof *x);
    const sk_cv *a[6];
    int n = args(f, a, 6);
    sk_str name = f->tok.text;
    x->tx.kind = x->ty.kind = SK_LEN_PX;
    if (n < 1)
        return false;
    if (sk_ident_is(name, "translate") || sk_ident_is(name, "translatex") || sk_ident_is(name, "translatey")) {
        x->kind = SK_XF_TRANSLATE;
        bool only_y = sk_ident_is(name, "translatey"), only_x = sk_ident_is(name, "translatex");
        if ((only_x || only_y) && n != 1)
            return false;
        if (n > 2)
            return false;
        if (!sk_v_len(c, a[0], SK_L_PCT | SK_L_NEG, only_y ? &x->ty : &x->tx))
            return false;
        if (n == 2 && !sk_v_len(c, a[1], SK_L_PCT | SK_L_NEG, &x->ty))
            return false;
        return true;
    }
    if (sk_ident_is(name, "rotate")) {
        x->kind = SK_XF_ROTATE;
        return n == 1 && sk_v_angle(a[0], true, &x->v[0]);
    }
    if (sk_ident_is(name, "scale") || sk_ident_is(name, "scalex") || sk_ident_is(name, "scaley")) {
        x->kind = SK_XF_SCALE;
        bool sx = sk_ident_is(name, "scalex"), sy = sk_ident_is(name, "scaley");
        if ((sx || sy) && n != 1)
            return false;
        if (n > 2)
            return false;
        float f0, f1;
        if (!sk_v_factor(a[0], &f0))
            return false;
        f1 = f0;
        if (n == 2 && !sk_v_factor(a[1], &f1))
            return false;
        x->v[0] = sy ? 1 : f0;
        x->v[1] = sx ? 1 : f1;
        return true;
    }
    if (sk_ident_is(name, "skew") || sk_ident_is(name, "skewx") || sk_ident_is(name, "skewy")) {
        x->kind = SK_XF_SKEW;
        bool kx = sk_ident_is(name, "skewx"), ky = sk_ident_is(name, "skewy");
        if (((kx || ky) && n != 1) || n > 2)
            return false;
        float d0, d1 = 0;
        if (!sk_v_angle(a[0], true, &d0))
            return false;
        if (n == 2 && !sk_v_angle(a[1], true, &d1))
            return false;
        x->v[0] = ky ? 0 : d0;
        x->v[1] = ky ? d0 : d1;
        return true;
    }
    if (sk_ident_is(name, "matrix")) {
        x->kind = SK_XF_MATRIX;
        if (n != 6)
            return false;
        for (int k = 0; k < 6; k++)
            if (!sk_v_number(a[k], &x->v[k]))
                return false;
        return true;
    }
    return false;
}

bool sk_v_transform(sk_vctx *c, sk_vcur *v, sk_css_xf **out, uint16_t *n)
{
    *out = NULL;
    *n = 0;
    if (sk_v_ident(sk_vcur_peek(v), "none")) {
        sk_vcur_next(v);
        return sk_vcur_end(v);
    }
    SK_VEC(sk_css_xf) list = {0};
    while (!sk_vcur_end(v)) {
        const sk_cv *f = sk_vcur_next(v);
        sk_css_xf x;
        if (!f || f->kind != SK_CV_FUNCTION || !one_transform(c, f, &x) || list.len >= SK_PACK_MAX_LIST) {
            sk_vec_free(&list);
            return false;
        }
        sk_vec_push(&list, x);
    }
    if (!list.len)
        return false;
    *n = (uint16_t)list.len;
    *out = sk_arena_array(c->arena, list.len, sizeof **out);
    memcpy(*out, list.ptr, list.len * sizeof **out);
    sk_vec_free(&list);
    return true;
}
