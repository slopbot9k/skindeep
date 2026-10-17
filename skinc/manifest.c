/* app/skins/validate.js in C. Every check keeps its message and its
 * place in the order, and JavaScript's own semantics where a check
 * leans on them: `in`, String() in a message, Object.keys's order
 * (the JSON parser keeps it), a regular expression tested on a value
 * that is not a string. Then skinc's own refusals, and the model. */
#include "skin.h"

#include "../src/app/js.h"
#include "../src/base/json.h"
#include "../src/base/paths.h"
#include "../src/core/names.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *const sk_ext_image[] = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", NULL};
const char *const sk_ext_font[] = {".ttf", ".otf", ".woff", ".woff2", NULL};
const char *const sk_ext_audio[] = {".ogg", ".opus", ".mp3", ".wav", NULL};
const char *const sk_ext_image_font[] = {".png", ".jpg",  ".jpeg", ".gif",   ".webp", ".svg",
                                         ".ttf", ".otf",  ".woff", ".woff2", NULL};
static const char *const EXT_CSS[] = {".css", NULL};
static const char *const EXT_PNG[] = {".png", NULL};
static const char *const ALLOWED_FILES[] = {".json", ".css",  ".txt",   ".md",  ".png",  ".jpg",  ".jpeg",
                                            ".gif",  ".webp", ".svg",   ".ttf", ".otf",  ".woff", ".woff2",
                                            ".ogg",  ".opus", ".mp3",   ".wav", NULL};

#define MAX_SOUNDS 16
#define MAX_INTERVAL 3600

const char *const sk_theme_keys[24] = {
    "foreground", "background",  "cursor",       "cursorAccent", "selectionBackground", "selectionForeground",
    "selectionInactiveBackground", "black",      "red",          "green",               "yellow",
    "blue",       "magenta",     "cyan",         "white",        "brightBlack",         "brightRed",
    "brightGreen", "brightYellow", "brightBlue", "brightMagenta", "brightCyan",         "brightWhite", NULL};

static const char *const COMMON[] = {"id", "type", "bind", "rect", "follow", "class", "label", "min", "max", "format",
                                     "damping", NULL};
static const struct {
    const char *type;
    const char *const *keys;
} TYPES[] = {
    {"gauge", (const char *const[]){"startAngle", "endAngle", "thickness", "ticks", "marks", NULL}},
    {"needle", (const char *const[]){"startAngle", "endAngle", "face", "ticks", "resets", NULL}},
    {"knob", (const char *const[]){"image", "startAngle", "endAngle", NULL}},
    {"bar", (const char *const[]){"orientation", "segments", "gap", "resets", "fallback", NULL}},
    {"led", (const char *const[]){"on", "pulse", "pulseMs", "blink", NULL}},
    {"readout", (const char *const[]){"prefix", "suffix", "resets", "fallback", NULL}},
    {"sprite", (const char *const[]){"sheet", "frames", "frameWidth", "frameHeight", "layout", "mode", "fps", NULL}},
    {"list", (const char *const[]){"rows", "toggle", NULL}},
    {"limits", (const char *const[]){"meter", "direction", "suffix", "itemClass", "orientation", "segments", "gap",
                                     "startAngle", "endAngle", "ticks", "face", NULL}},
};
static const char *const EDGES[] = {"left", "right", "top", "bottom"};
static const char *const SIZE[] = {"width", "width", "height", "height"};
static const char *const RECT_KEYS[] = {"left", "right", "width", "top", "bottom", "height", NULL};

/* ---- JavaScript ---- */

/* typeof v === 'object' && v !== null: objects and arrays. */
static bool is_obj(const sk_json *v)
{
    return v && (v->type == SK_JSON_OBJECT || v->type == SK_JSON_ARRAY);
}

static bool is_array(const sk_json *v)
{
    return v && v->type == SK_JSON_ARRAY;
}

/* !v: undefined, null, false, 0, NaN, "". */
static bool falsy(const sk_json *v)
{
    if (!v || v->type == SK_JSON_NULL)
        return true;
    if (v->type == SK_JSON_BOOL)
        return !v->boolean;
    if (v->type == SK_JSON_NUMBER)
        return v->number == 0 || isnan(v->number);
    if (v->type == SK_JSON_STRING)
        return v->string.len == 0;
    return false;
}

static bool is_num(const sk_json *v)
{
    double d;
    return sk_json_as_number(v, &d);
}

static double num_of(const sk_json *v)
{
    double d = NAN;
    sk_json_as_number(v, &d);
    return d;
}

static bool is_str(const sk_json *v)
{
    return v && v->type == SK_JSON_STRING && v->string.len > 0;
}

static bool is_string(const sk_json *v)
{
    return v && v->type == SK_JSON_STRING;
}

static bool str_eq(const sk_json *v, const char *s)
{
    return is_string(v) && sk_str_eq_c(v->string, s);
}

/* key in v. */
static bool has(const sk_json *v, const char *key)
{
    if (!v)
        return false;
    if (v->type == SK_JSON_OBJECT) {
        for (size_t i = 0; i < v->object.len; i++)
            if (sk_str_eq_c(v->object.members[i].key, key))
                return true;
        return false;
    }
    if (v->type == SK_JSON_ARRAY) {
        if (!strcmp(key, "length"))
            return true;
        char *end;
        errno = 0;
        unsigned long i = strtoul(key, &end, 10);
        return *key >= '0' && *key <= '9' && !*end && (key[0] != '0' || !key[1]) && i < v->array.len;
    }
    return false;
}

/* v[key], undefined (NULL) when absent. */
static const sk_json *get(const sk_json *v, const char *key)
{
    if (!v || v->type != SK_JSON_OBJECT)
        return NULL;
    return sk_json_get(v, key);
}

/* String(v), as a template literal writes it. */
static void js_str(sk_buf *b, const sk_json *v)
{
    if (!v) {
        sk_buf_append_c(b, "undefined");
        return;
    }
    switch (v->type) {
    case SK_JSON_NULL:
        sk_buf_append_c(b, "null");
        break;
    case SK_JSON_BOOL:
        sk_buf_append_c(b, v->boolean ? "true" : "false");
        break;
    case SK_JSON_NUMBER:
        sk_json_format_number(b, v->number);
        break;
    case SK_JSON_STRING:
        sk_buf_append_str(b, v->string);
        break;
    case SK_JSON_ARRAY:
        for (size_t i = 0; i < v->array.len; i++) {
            if (i)
                sk_buf_putc(b, ',');
            const sk_json *x = &v->array.items[i];
            if (x->type != SK_JSON_NULL)
                js_str(b, x);
        }
        break;
    default:
        sk_buf_append_c(b, "[object Object]");
        break;
    }
}

/* Number(v), for the arithmetic validate.js does on `x || 0`. */
static double js_num(const sk_json *v)
{
    if (!v)
        return NAN;
    switch (v->type) {
    case SK_JSON_NULL:
        return 0;
    case SK_JSON_BOOL:
        return v->boolean ? 1 : 0;
    case SK_JSON_NUMBER:
        return v->number;
    case SK_JSON_STRING: {
        sk_str t = sk_js_trim(v->string);
        if (!t.len)
            return 0;
        char *s = sk_xstrndup(t.ptr, t.len), *end;
        double d = strtod(s, &end);
        bool ok = *end == 0 && !(t.len > 1 && (s[1] == 'x' || s[1] == 'X'));
        free(s);
        return ok ? d : NAN;
    }
    default:
        return NAN;
    }
}

/* (v || 0) as a number. */
static double or0(const sk_json *v)
{
    return falsy(v) ? 0 : js_num(v);
}

static bool match_id(sk_str s)
{
    if (!s.len)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

/* /^[a-z0-9-]+( [a-z0-9-]+)*$/ */
static bool match_class(sk_str s)
{
    size_t start = 0;
    for (size_t i = 0; i <= s.len; i++) {
        if (i == s.len || s.ptr[i] == ' ') {
            if (!match_id(sk_str_sub(s, start, i - start)))
                return false;
            start = i + 1;
        }
    }
    return true;
}

static bool hex_digits(sk_str s, size_t from)
{
    for (size_t i = from; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

/* #rgb, #rrggbb or #rrggbbaa; opaque: the first two only. */
static bool match_color(sk_str s, bool opaque)
{
    if (!s.len || s.ptr[0] != '#' || !hex_digits(s, 1))
        return false;
    size_t n = s.len - 1;
    return n == 3 || n == 6 || (!opaque && n == 8);
}

static bool match_format(sk_str s)
{
    static const char *const F[] = {"int", "pct", "usd", "tokens", "duration", "until", "bytes", "text"};
    for (size_t i = 0; i < SK_LEN(F); i++)
        if (sk_str_eq_c(s, F[i]))
            return true;
    return s.len == 7 && !memcmp(s.ptr, "fixed:", 6) && s.ptr[6] >= '0' && s.ptr[6] <= '4';
}

static bool match_family(sk_str s)
{
    if (!s.len)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
              c == '-'))
            return false;
    }
    return true;
}

static bool known(const sk_json *v)
{
    sk_buf b = {0};
    js_str(&b, v);
    bool k = sk_metric_known(sk_buf_str(&b));
    sk_buf_free(&b);
    return k;
}

/* ---- Numbers as JavaScript prints them ---- */

static void jnum(sk_buf *b, double d)
{
    sk_json_format_number(b, d);
}

/* x.toFixed(digits): the n nearest x * 10^digits, the larger on a tie,
 * from the exact decimal expansion of the double. */
void sk_js_fixed(sk_buf *out, double x, int digits)
{
    if (!isfinite(x) || fabs(x) >= 1e21) {
        jnum(out, x);
        return;
    }
    char exact[512];
    snprintf(exact, sizeof exact, "%.60f", fabs(x));
    char *dot = strchr(exact, '.');
    size_t ip = (size_t)(dot - exact);
    /* Digits: the integer part, then the fraction. */
    char digs[512];
    size_t n = 0;
    for (size_t i = 0; i < ip; i++)
        digs[n++] = exact[i];
    for (int i = 0; i < digits; i++)
        digs[n++] = dot[1 + i];
    bool up = dot[1 + digits] >= '5';
    for (size_t i = n; up && i-- > 0;) {
        if (digs[i] == '9') {
            digs[i] = '0';
        } else {
            digs[i]++;
            up = false;
        }
    }
    bool neg = x < 0;
    sk_buf b = {0};
    if (up)
        sk_buf_putc(&b, '1');
    sk_buf_append(&b, digs, n - (size_t)digits);
    if (digits) {
        sk_buf_putc(&b, '.');
        sk_buf_append(&b, digs + n - digits, (size_t)digits);
    }
    /* toFixed keeps the sign of a negative x that rounds to 0; -0 has none. */
    if (neg)
        sk_buf_putc(out, '-');
    sk_buf_append(out, b.ptr, b.len);
    sk_buf_free(&b);
}

static double channel(int v)
{
    double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static int hex2(const char *h)
{
    int v = 0;
    for (int i = 0; i < 2; i++) {
        char c = h[i];
        v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

static double luminance(const char *hex)
{
    char h[7];
    const char *p = hex + 1;
    if (strlen(p) == 3) {
        for (int i = 0; i < 3; i++)
            h[2 * i] = h[2 * i + 1] = p[i];
    } else {
        memcpy(h, p, 6);
    }
    h[6] = 0;
    return 0.2126 * channel(hex2(h)) + 0.7152 * channel(hex2(h + 2)) + 0.0722 * channel(hex2(h + 4));
}

double sk_contrast(const char *a, const char *b)
{
    double l1 = luminance(a), l2 = luminance(b);
    if (l2 > l1) {
        double t = l1;
        l1 = l2;
        l2 = t;
    }
    return (l1 + 0.05) / (l2 + 0.05);
}

/* ---- The checker ---- */

typedef struct {
    sk_skin *skin;
    sk_diags *d;
    const char *mf; /* the manifest as shown */
    char *root;     /* path.resolve(dir) */
    char *real;     /* its realpath */
    size_t errors_at;
} checker;

static void err(checker *k, const char *field, const char *fmt, ...) SK_PRINTF(3, 4);

static void err(checker *k, const char *field, const char *fmt, ...)
{
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    sk_diag(k->d, k->mf, field, "%s", sk_buf_cstr(&b));
    sk_buf_free(&b);
}

static size_t nerrors(const checker *k)
{
    return k->d->items.len - k->errors_at;
}

/* Unknown fields, in Object.keys's order. */
static void keys(checker *k, const sk_json *obj, const char *field, const char *const *allowed1,
                 const char *const *allowed2)
{
    sk_buf f = {0};
    if (obj->type == SK_JSON_OBJECT) {
        for (size_t i = 0; i < obj->object.len; i++) {
            sk_str key = obj->object.members[i].key;
            bool ok = false;
            for (const char *const *a = allowed1; a && *a && !ok; a++)
                ok = sk_str_eq_c(key, *a);
            for (const char *const *a = allowed2; a && *a && !ok; a++)
                ok = sk_str_eq_c(key, *a);
            if (!ok) {
                sk_buf_clear(&f);
                sk_buf_printf(&f, "%s%s%.*s", field, *field ? "." : "", (int)key.len, key.ptr);
                err(k, f.ptr, "unknown field");
            }
        }
    } else if (obj->type == SK_JSON_ARRAY) {
        for (size_t i = 0; i < obj->array.len; i++) {
            sk_buf_clear(&f);
            sk_buf_printf(&f, "%s%s%zu", field, *field ? "." : "", i);
            err(k, f.ptr, "unknown field");
        }
    }
    sk_buf_free(&f);
}

static const char *ext_of(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base)
        return "";
    return dot;
}

static bool ext_in(const char *path, const char *const *exts)
{
    const char *e = ext_of(path);
    char low[32];
    size_t n = strlen(e);
    if (n >= sizeof low)
        return false;
    for (size_t i = 0; i <= n; i++)
        low[i] = (char)(e[i] >= 'A' && e[i] <= 'Z' ? e[i] + 32 : e[i]);
    for (; *exts; exts++)
        if (!strcmp(low, *exts))
            return true;
    return false;
}

static bool scheme(const char *p)
{
    if (!((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z'))
        return false;
    for (const char *q = p + 1; *q; q++) {
        char c = *q;
        if (c == ':')
            return true;
        if (!(((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '.' || c == '-'))
            return false;
    }
    return false;
}

/* path.resolve(from, p) for an absolute from. */
static char *resolve(const char *from, const char *p)
{
    sk_buf b = {0};
    sk_js_join(&b, sk_str_c(from), sk_str_c(p));
    while (b.len > 1 && b.ptr[b.len - 1] == '/')
        b.ptr[--b.len] = 0;
    return b.ptr ? b.ptr : sk_xstrdup("");
}

char *sk_skin_file(const sk_skin *skin, const char *p, const char *field, const char *const *exts, const char *from,
                   const char *file, sk_diags *d)
{
    if (!p || !*p) {
        sk_diag(d, file, field, "must be a relative file path");
        return NULL;
    }
    if (p[0] == '/' || scheme(p)) {
        sk_diag(d, file, field, "\"%s\": must be a path relative to the skin", p);
        return NULL;
    }
    char *full = resolve(from, p);
    if (!ext_in(full, exts)) {
        sk_buf list = {0};
        for (const char *const *e = exts; *e; e++)
            sk_buf_printf(&list, "%s%s", e == exts ? "" : " ", *e);
        sk_diag(d, file, field, "\"%s\": file type not allowed here (%s)", p, sk_buf_cstr(&list));
        sk_buf_free(&list);
        free(full);
        return NULL;
    }
    char *real = realpath(full, NULL);
    free(full);
    if (!real) {
        sk_diag(d, file, field, "\"%s\": file not found", p);
        return NULL;
    }
    size_t rl = strlen(skin->real);
    if (strcmp(real, skin->real) != 0 && !(strncmp(real, skin->real, rl) == 0 && real[rl] == '/')) {
        sk_diag(d, file, field, "\"%s\": outside the skin directory", p);
        free(real);
        return NULL;
    }
    struct stat st;
    if (stat(real, &st) != 0 || !S_ISREG(st.st_mode)) {
        sk_diag(d, file, field, "\"%s\": not a file", p);
        free(real);
        return NULL;
    }
    return real;
}

/* file() with the manifest's root and file, a JSON value as p. */
static char *file(checker *k, const sk_json *p, const char *field, const char *const *exts)
{
    if (!is_str(p)) {
        err(k, field, "must be a relative file path");
        return NULL;
    }
    char *s = sk_xstrndup(p->string.ptr, p->string.len);
    char *real = sk_skin_file(k->skin, s, field, exts, k->root, k->mf, k->d);
    free(s);
    return real;
}

static void rect(checker *k, const sk_json *r, const char *field, bool stretch)
{
    if (falsy(r) || !is_obj(r)) {
        err(k, field, "missing rect");
        return;
    }
    keys(k, r, field, RECT_KEYS, NULL);
    sk_buf f = {0};
    if (r->type == SK_JSON_OBJECT) {
        for (size_t i = 0; i < r->object.len; i++)
            if (!is_num(&r->object.members[i].value)) {
                sk_buf_clear(&f);
                sk_buf_printf(&f, "%s.%.*s", field, (int)r->object.members[i].key.len, r->object.members[i].key.ptr);
                err(k, f.ptr, "must be a number of pixels");
            }
    } else {
        for (size_t i = 0; i < r->array.len; i++)
            if (!is_num(&r->array.items[i])) {
                sk_buf_clear(&f);
                sk_buf_printf(&f, "%s.%zu", field, i);
                err(k, f.ptr, "must be a number of pixels");
            }
    }
    sk_buf_free(&f);
    int h = has(r, "left") + has(r, "right") + has(r, "width");
    int v = has(r, "top") + has(r, "bottom") + has(r, "height");
    if (stretch) {
        static const char *const LTRB[] = {"left", "right", "top", "bottom"};
        for (int i = 0; i < 4; i++)
            if (!has(r, LTRB[i]))
                err(k, field, "needs %s: the terminal stretches with the window", LTRB[i]);
    } else {
        if (h != 2)
            err(k, field, "needs exactly two of left, right, width");
        if (v != 2)
            err(k, field, "needs exactly two of top, bottom, height");
    }
}

/* A rect the box can be computed from. */
static bool placed(const sk_json *r, bool stretch)
{
    if (!is_obj(r))
        return false;
    if (r->type == SK_JSON_OBJECT) {
        for (size_t i = 0; i < r->object.len; i++)
            if (!is_num(&r->object.members[i].value))
                return false;
    } else {
        for (size_t i = 0; i < r->array.len; i++)
            if (!is_num(&r->array.items[i]))
                return false;
    }
    if (stretch)
        return has(r, "left") && has(r, "right") && has(r, "top") && has(r, "bottom");
    return has(r, "left") + has(r, "right") + has(r, "width") == 2 &&
           has(r, "top") + has(r, "bottom") + has(r, "height") == 2;
}

static bool sized(const sk_json *w)
{
    return !falsy(w) && is_num(get(w, "width")) && is_num(get(w, "height")) && is_num(get(w, "minWidth")) &&
           is_num(get(w, "minHeight"));
}

/* A rect as numbers: has bit i for key i of RECT_KEYS. */
typedef struct {
    double v[6];
    unsigned has;
} rectv;

static rectv rect_of(const sk_json *r)
{
    rectv out = {{0}, 0};
    for (int i = 0; i < 6; i++)
        if (has(r, RECT_KEYS[i])) {
            out.has |= 1u << i;
            out.v[i] = num_of(get(r, RECT_KEYS[i]));
        }
    return out;
}

typedef struct {
    double x0, x1, y0, y1;
} box_t;

static void axis(const rectv *r, int s, int e, int n, double L, double *a, double *b)
{
    if (r->has & (1u << s)) {
        *a = r->v[s];
        *b = (r->has & (1u << n)) ? r->v[s] + r->v[n] : L - r->v[e];
    } else {
        *a = L - r->v[e] - r->v[n];
        *b = L - r->v[e];
    }
}

static box_t box(const rectv *r, double W, double H)
{
    box_t b;
    axis(r, 0, 1, 2, W, &b.x0, &b.x1);
    axis(r, 3, 4, 5, H, &b.y0, &b.y1);
    return b;
}

static bool over(box_t a, box_t b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

/* ---- Splits, as app/renderer/splits.js computes them ---- */

typedef struct {
    sk_str id;
    int edge;
    double min, max;
} split_t;

typedef struct {
    split_t *s;
    size_t n;
} splits_t;

static int edge_index(const sk_json *e)
{
    for (int i = 0; i < 4; i++)
        if (str_eq(e, EDGES[i]))
            return i;
    return -1;
}

static bool horizontal(int edge)
{
    return edge == 0 || edge == 1;
}

/* splits.js's sizes() with every split at min or max. */
static void sizes(const splits_t *sp, const rectv *term, const sk_json *win, bool at_max, double W, double H,
                  double *out)
{
    for (size_t i = 0; i < sp->n; i++) {
        const split_t *s = &sp->s[i];
        double def = term->v[s->edge == 0 ? 0 : s->edge == 1 ? 1 : s->edge == 2 ? 3 : 4];
        double shortv = horizontal(s->edge) ? num_of(get(win, "width")) - W
                                            : num_of(get(win, "height")) - SK_STRIP - H;
        double max = fmax(def, fmin(s->max, s->max - shortv));
        double v = at_max ? s->max : s->min;
        out[i] = fmin(max, fmax(s->min, v));
    }
}

static int rect_key(sk_str key)
{
    for (int i = 0; i < 6; i++)
        if (sk_str_eq_c(key, RECT_KEYS[i]))
            return i;
    return -1;
}

/* splits.js's rect(): each followed key moved by its split's change. */
static rectv follow_rect(const rectv *r, const sk_json *follow, const splits_t *sp, const rectv *term,
                         const double *at)
{
    rectv out = *r;
    if (!follow || follow->type != SK_JSON_OBJECT)
        return out;
    for (size_t i = 0; i < follow->object.len; i++) {
        int key = rect_key(follow->object.members[i].key);
        const sk_json *id = &follow->object.members[i].value;
        if (key < 0 || !is_string(id))
            continue;
        for (size_t j = 0; j < sp->n; j++)
            if (sk_str_eq(sp->s[j].id, id->string)) {
                if (out.has & (1u << key)) {
                    int e = sp->s[j].edge;
                    double def = term->v[e == 0 ? 0 : e == 1 ? 1 : e == 2 ? 3 : 4];
                    out.v[key] += at[j] - def;
                }
                break;
            }
    }
    return out;
}

static rectv terminal_at(const rectv *term, const splits_t *sp, const double *at)
{
    rectv out = *term;
    for (size_t i = 0; i < sp->n; i++) {
        int e = sp->s[i].edge;
        out.v[e == 0 ? 0 : e == 1 ? 1 : e == 2 ? 3 : 4] = at[i];
    }
    return out;
}

/* splits.js's movable(). */
static bool movable(int edge, sk_str key, const sk_json *r)
{
    if (sk_str_eq_c(key, EDGES[edge]))
        return has(r, EDGES[edge]);
    return sk_str_eq_c(key, SIZE[edge]) && has(r, EDGES[edge]) && has(r, SIZE[edge]);
}

static void follow_check(checker *k, const sk_json *o, const sk_json *r, const char *fld, const splits_t *ids)
{
    sk_buf f = {0};
    if (falsy(o) || !is_obj(o) || is_array(o)) {
        sk_buf_printf(&f, "%s.follow", fld);
        err(k, f.ptr, "must be an object of rect keys and split ids");
        sk_buf_free(&f);
        return;
    }
    for (size_t i = 0; i < o->object.len; i++) {
        sk_str key = o->object.members[i].key;
        const sk_json *id = &o->object.members[i].value;
        sk_buf_clear(&f);
        sk_buf_printf(&f, "%s.follow.%.*s", fld, (int)key.len, key.ptr);
        const split_t *s = NULL;
        if (is_string(id))
            for (size_t j = 0; j < ids->n; j++)
                if (sk_str_eq(ids->s[j].id, id->string))
                    s = &ids->s[j];
        sk_buf ids_ = {0};
        js_str(&ids_, id);
        if (rect_key(key) < 0)
            err(k, f.ptr, "not a rect key");
        else if (!s)
            err(k, f.ptr, "unknown split \"%s\"", sk_buf_cstr(&ids_));
        else if (is_obj(r) && !movable(s->edge, key, r))
            err(k, f.ptr, "the %s split \"%s\" moves %s, or %s on a rect with %s", EDGES[s->edge], sk_buf_cstr(&ids_),
                EDGES[s->edge], SIZE[s->edge], EDGES[s->edge]);
        sk_buf_free(&ids_);
    }
    sk_buf_free(&f);
}

/* pngSize: the IHDR's size, or false. */
static bool png_size(const char *path, uint32_t *w, uint32_t *h)
{
    unsigned char b[24] = {0};
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    ssize_t got = 0;
    while (got < 24) {
        ssize_t r = read(fd, b + got, (size_t)(24 - got));
        if (r <= 0)
            break;
        got += r;
    }
    close(fd);
    if (b[0] != 0x89 || b[1] != 0x50 || b[2] != 0x4e || b[3] != 0x47 || memcmp(b + 12, "IHDR", 4) != 0)
        return false;
    *w = (uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | b[19];
    *h = (uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | b[23];
    return true;
}

/* Every file under dir, sorted as Node's readdir sorts them. */
static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool plain_name(const char *n)
{
    static const char *const P[] = {"LICENSE", "LICENCE", "COPYING", "README", "NOTICE", "AUTHORS", "OFL"};
    for (size_t i = 0; i < SK_LEN(P); i++) {
        size_t l = strlen(P[i]);
        if (strncasecmp(n, P[i], l) == 0 && (n[l] == 0 || n[l] == '.' || n[l] == '_' || n[l] == '-')) {
            for (const char *q = n + l; *q; q++)
                if (*q == '\n' || *q == '\r')
                    return false;
            return true;
        }
    }
    return false;
}

static void walk(checker *k, const char *path, const char *shown)
{
    DIR *d = opendir(path);
    if (!d)
        return;
    SK_VEC(char *) names = {0};
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            sk_vec_push(&names, sk_xstrdup(e->d_name));
    closedir(d);
    if (names.len)
        qsort(names.ptr, names.len, sizeof *names.ptr, by_name);
    for (size_t i = 0; i < names.len; i++) {
        char *p = sk_path_join(path, names.ptr[i]);
        char *s = sk_path_join(shown, names.ptr[i]);
        struct stat st;
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode))
            walk(k, p, s);
        else if (!ext_in(names.ptr[i], ALLOWED_FILES) && !plain_name(names.ptr[i]))
            sk_diag(k->d, s, "(file)", "file type not allowed in a skin");
        free(p);
        free(s);
        free(names.ptr[i]);
    }
    sk_vec_free(&names);
}

/* ---- validate() ---- */

static char *field_of(const char *fmt, ...) SK_PRINTF(1, 2);

static char *field_of(const char *fmt, ...)
{
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    return b.ptr ? b.ptr : sk_xstrdup("");
}

static void check_window(checker *k, const sk_json *win)
{
    if (falsy(win) || !is_obj(win)) {
        err(k, "window", "missing");
        return;
    }
    static const char *const W[] = {"width", "height", "minWidth", "minHeight", NULL};
    keys(k, win, "window", W, NULL);
    for (int i = 0; W[i]; i++) {
        const sk_json *v = get(win, W[i]);
        if (!is_num(v) || num_of(v) < 200) {
            char *f = field_of("window.%s", W[i]);
            err(k, f, "must be a number of pixels, at least 200");
            free(f);
        } else if (num_of(v) > SK_PACK_MAX_SIDE) {
            /* The loader's bound (pack.h). */
            char *f = field_of("window.%s", W[i]);
            err(k, f, "must be at most 16384 pixels");
            free(f);
        }
    }
    const sk_json *mw = get(win, "minWidth"), *w = get(win, "width"), *mh = get(win, "minHeight"),
                  *h = get(win, "height");
    if (is_num(mw) && is_num(w) && num_of(mw) > num_of(w))
        err(k, "window.minWidth", "larger than width");
    if (is_num(mh) && is_num(h) && num_of(mh) > num_of(h))
        err(k, "window.minHeight", "larger than height");
}

static void theme_entry(checker *k, const sk_json *v, const char *field)
{
    if (is_string(v) && match_color(v->string, false))
        return;
    sk_buf s = {0};
    js_str(&s, v);
    err(k, field, "\"%s\": must be a #rgb, #rrggbb or #rrggbbaa colour", sk_buf_cstr(&s));
    sk_buf_free(&s);
}

static void check_terminal(checker *k, const sk_json *t, const sk_json *win)
{
    if (falsy(t) || !is_obj(t)) {
        err(k, "terminal", "missing");
        return;
    }
    static const char *const T[] = {"rect", "theme", "fontFamily", "fontSize", NULL};
    keys(k, t, "terminal", T, NULL);
    rect(k, get(t, "rect"), "terminal.rect", true);
    if (has(t, "fontFamily") && !is_str(get(t, "fontFamily")))
        err(k, "terminal.fontFamily", "must be a string");
    bool given = has(t, "fontSize");
    const sk_json *fsv = get(t, "fontSize");
    double fs = given ? num_of(fsv) : 14;
    bool fs_ok = given ? is_num(fsv) : true;
    if (!fs_ok || fs < 8 || fs > 32)
        err(k, "terminal.fontSize", "must be a number from 8 to 32");
    const sk_json *th = get(t, "theme");
    if (falsy(th) || !is_obj(th)) {
        err(k, "terminal.theme", "missing");
    } else {
        keys(k, th, "terminal.theme", sk_theme_keys, NULL);
        for (size_t i = 0; th->type == SK_JSON_OBJECT && i < th->object.len; i++) {
            char *f = field_of("terminal.theme.%.*s", (int)th->object.members[i].key.len, th->object.members[i].key.ptr);
            theme_entry(k, &th->object.members[i].value, f);
            free(f);
        }
        for (size_t i = 0; th->type == SK_JSON_ARRAY && i < th->array.len; i++) {
            char *f = field_of("terminal.theme.%zu", i);
            theme_entry(k, &th->array.items[i], f);
            free(f);
        }
        static const char *const FB[] = {"foreground", "background"};
        char *fbv[2] = {NULL, NULL};
        for (int i = 0; i < 2; i++) {
            const sk_json *v = get(th, FB[i]);
            sk_buf s = {0};
            js_str(&s, v);
            if (falsy(v) || !match_color(sk_buf_str(&s), true)) {
                char *f = field_of("terminal.theme.%s", FB[i]);
                err(k, f, "required as an opaque #rgb or #rrggbb colour");
                free(f);
            } else {
                fbv[i] = sk_xstrdup(sk_buf_cstr(&s));
            }
            sk_buf_free(&s);
        }
        if (fbv[0] && fbv[1]) {
            double c = sk_contrast(fbv[0], fbv[1]);
            if (c < 4.5) {
                sk_buf s = {0};
                sk_js_fixed(&s, c, 2);
                err(k, "terminal.theme.foreground", "contrast with background is %s, needs at least 4.5",
                    sk_buf_cstr(&s));
                sk_buf_free(&s);
            }
        }
        free(fbv[0]);
        free(fbv[1]);
    }
    const sk_json *r = get(t, "rect");
    if (!falsy(win) && !falsy(r) && is_num(get(win, "minWidth")) && is_num(get(win, "minHeight")) && fs_ok) {
        double cols = floor((num_of(get(win, "minWidth")) - or0(get(r, "left")) - or0(get(r, "right"))) / (fs * 0.6));
        double rows = floor((num_of(get(win, "minHeight")) - SK_STRIP - or0(get(r, "top")) - or0(get(r, "bottom"))) /
                            (fs * 1.2));
        if (cols < 40 || rows < 10) {
            sk_buf a = {0}, b = {0};
            jnum(&a, cols);
            jnum(&b, rows);
            err(k, "terminal.rect", "leaves about %s x %s cells at the minimum window, needs 40 x 10", sk_buf_cstr(&a),
                sk_buf_cstr(&b));
            sk_buf_free(&a);
            sk_buf_free(&b);
        }
    }
}

static void check_splits(checker *k, const sk_json *root, const sk_json *t, splits_t *ids)
{
    if (!has(root, "splits"))
        return;
    const sk_json *sp = get(root, "splits");
    if (!is_array(sp)) {
        err(k, "splits", "must be an array");
        return;
    }
    bool edges[4] = {false, false, false, false};
    ids->s = sk_arena_array(&k->skin->arena, sp->array.len ? sp->array.len : 1, sizeof *ids->s);
    for (size_t i = 0; i < sp->array.len; i++) {
        const sk_json *s = &sp->array.items[i];
        char *fld = field_of("splits[%zu]", i);
        if (falsy(s) || !is_obj(s) || is_array(s)) {
            err(k, fld, "must be an object");
            free(fld);
            continue;
        }
        static const char *const S[] = {"id", "edge", "min", "max", NULL};
        keys(k, s, fld, S, NULL);
        bool ok = true;
        const sk_json *id = get(s, "id");
        char *f = field_of("%s.id", fld);
        if (!is_str(id) || !match_id(id->string)) {
            ok = false;
            err(k, f, "must be lowercase letters, digits and dashes");
        } else {
            for (size_t j = 0; j < ids->n; j++)
                if (sk_str_eq(ids->s[j].id, id->string)) {
                    ok = false;
                    err(k, f, "duplicate id \"%.*s\"", (int)id->string.len, id->string.ptr);
                    break;
                }
        }
        free(f);
        int edge = edge_index(get(s, "edge"));
        f = field_of("%s.edge", fld);
        if (edge < 0) {
            ok = false;
            err(k, f, "must be left, right, top or bottom");
        } else if (edges[edge]) {
            ok = false;
            err(k, f, "a second split on the terminal's %s edge", EDGES[edge]);
        } else {
            edges[edge] = true;
        }
        free(f);
        static const char *const MM[] = {"min", "max"};
        for (int m = 0; m < 2; m++) {
            const sk_json *v = get(s, MM[m]);
            if (!is_num(v) || num_of(v) < 0) {
                ok = false;
                f = field_of("%s.%s", fld, MM[m]);
                err(k, f, "must be a number of pixels, at least 0");
                free(f);
            }
        }
        const sk_json *tr = !falsy(t) ? get(t, "rect") : NULL;
        const sk_json *def = !falsy(tr) && edge >= 0 ? get(tr, EDGES[edge]) : NULL;
        if (ok && is_num(def)) {
            double dv = num_of(def), mn = num_of(get(s, "min")), mx = num_of(get(s, "max"));
            if (!(mn <= dv && dv <= mx)) {
                sk_buf b = {0};
                jnum(&b, dv);
                err(k, fld, "the terminal's %s of %s must lie from min to max", EDGES[edge], sk_buf_cstr(&b));
                sk_buf_free(&b);
            }
        }
        if (ok) {
            split_t x = {id->string, edge, num_of(get(s, "min")), num_of(get(s, "max"))};
            ids->s[ids->n++] = x;
        }
        free(fld);
    }
}

static void check_menu(checker *k, const sk_json *m, const sk_json *win, const sk_json *t)
{
    if (falsy(m) || !is_obj(m) || is_array(m)) {
        err(k, "menu", "missing: every skin places the menu button");
        return;
    }
    static const char *const M[] = {"rect", "image", "hover", "pressed", NULL};
    keys(k, m, "menu", M, NULL);
    rect(k, get(m, "rect"), "menu.rect", false);
    free(file(k, get(m, "image"), "menu.image", sk_ext_image));
    static const char *const HP[] = {"hover", "pressed"};
    for (int i = 0; i < 2; i++)
        if (has(m, HP[i])) {
            char *f = field_of("menu.%s", HP[i]);
            free(file(k, get(m, HP[i]), f, sk_ext_image));
            free(f);
        }
    const sk_json *tr = !falsy(t) ? get(t, "rect") : NULL;
    if (sized(win) && placed(get(m, "rect"), false) && !falsy(t) && placed(tr, true)) {
        bool said[3] = {false, false, false};
        rectv mr = rect_of(get(m, "rect")), trv = rect_of(tr);
        for (int pass = 0; pass < 2; pass++) {
            double W = num_of(get(win, pass ? "width" : "minWidth"));
            double H = num_of(get(win, pass ? "height" : "minHeight"));
            const char *at = pass ? "initial" : "minimum";
            box_t b = box(&mr, W, H - SK_STRIP), tb = box(&trv, W, H - SK_STRIP);
            double w = b.x1 - b.x0, h = b.y1 - b.y0;
            if ((w < 16 || h < 16) && !said[0]) {
                said[0] = true;
                sk_buf a = {0}, c = {0};
                jnum(&a, w);
                jnum(&c, h);
                err(k, "menu.rect", "is %s x %s px at the %s window, needs at least 16 x 16", sk_buf_cstr(&a),
                    sk_buf_cstr(&c), at);
                sk_buf_free(&a);
                sk_buf_free(&c);
            }
            if ((b.x0 < 0 || b.y0 < 0 || b.x1 > W || b.y1 > H - SK_STRIP) && !said[1]) {
                said[1] = true;
                err(k, "menu.rect", "lies outside the skin area at the %s window", at);
            }
            if (over(b, tb) && !said[2]) {
                said[2] = true;
                err(k, "menu.rect", "overlaps the terminal at the %s window", at);
            }
        }
    }
}

static void check_fonts(checker *k, const sk_json *root)
{
    if (!has(root, "fonts"))
        return;
    const sk_json *fonts = get(root, "fonts");
    if (!is_array(fonts)) {
        err(k, "fonts", "must be an array");
        return;
    }
    for (size_t i = 0; i < fonts->array.len; i++) {
        const sk_json *f = &fonts->array.items[i];
        char *fld = field_of("fonts[%zu]", i);
        if (falsy(f) || !is_obj(f)) {
            err(k, fld, "must be an object");
            free(fld);
            continue;
        }
        static const char *const F[] = {"family", "file", "weight", "style", NULL};
        keys(k, f, fld, F, NULL);
        const sk_json *family = get(f, "family");
        char *x = field_of("%s.family", fld);
        if (!is_str(family) || !match_family(family->string))
            err(k, x, "must be letters, digits, spaces, dashes");
        free(x);
        x = field_of("%s.file", fld);
        free(file(k, get(f, "file"), x, sk_ext_font));
        free(x);
        if (has(f, "style") && !str_eq(get(f, "style"), "normal") && !str_eq(get(f, "style"), "italic")) {
            x = field_of("%s.style", fld);
            err(k, x, "must be normal or italic");
            free(x);
        }
        free(fld);
    }
}

static void check_stylesheet(checker *k, const sk_json *root, sk_css_check check_css, void *data)
{
    if (!has(root, "stylesheet"))
        return;
    char *css = file(k, get(root, "stylesheet"), "stylesheet", EXT_CSS);
    if (!css)
        return;
    const char *rel = css + strlen(k->skin->real);
    while (*rel == '/')
        rel++;
    char *shown = sk_path_join(k->skin->dir, rel);
    if (check_css)
        check_css(data, css, shown, k->d);
    free(shown);
    free(css);
}

static void check_panels(checker *k, const sk_json *root, const splits_t *ids)
{
    if (!has(root, "panels"))
        return;
    const sk_json *panels = get(root, "panels");
    if (!is_array(panels)) {
        err(k, "panels", "must be an array");
        return;
    }
    for (size_t i = 0; i < panels->array.len; i++) {
        const sk_json *p = &panels->array.items[i];
        char *fld = field_of("panels[%zu]", i);
        if (falsy(p) || !is_obj(p)) {
            err(k, fld, "must be an object");
            free(fld);
            continue;
        }
        static const char *const P[] = {"class", "rect", "follow", NULL};
        keys(k, p, fld, P, NULL);
        const sk_json *c = get(p, "class");
        char *x = field_of("%s.class", fld);
        if (!is_str(c) || !match_class(c->string))
            err(k, x, "must be lowercase class names");
        free(x);
        x = field_of("%s.rect", fld);
        rect(k, get(p, "rect"), x, false);
        free(x);
        if (has(p, "follow"))
            follow_check(k, get(p, "follow"), get(p, "rect"), fld, ids);
        free(fld);
    }
}

static void check_toggle(checker *k, const sk_json *w, const char *fld, const sk_json *win, const sk_json *t,
                         const splits_t *ids)
{
    const sk_json *g = get(w, "toggle");
    char *tf = field_of("%s.toggle", fld);
    if (falsy(g) || !is_obj(g) || is_array(g)) {
        err(k, tf, "must be an object of rect, labels and class");
        free(tf);
        return;
    }
    static const char *const G[] = {"rect", "labels", "class", "follow", NULL};
    keys(k, g, tf, G, NULL);
    char *x = field_of("%s.rect", tf);
    rect(k, get(g, "rect"), x, false);
    if (has(g, "follow"))
        follow_check(k, get(g, "follow"), get(g, "rect"), tf, ids);
    if (has(g, "labels")) {
        const sk_json *l = get(g, "labels");
        bool ok = is_array(l) && l->array.len == 2 && is_str(&l->array.items[0]) && is_str(&l->array.items[1]);
        if (!ok) {
            char *y = field_of("%s.labels", tf);
            err(k, y, "must be two strings, the list's and what it replaces");
            free(y);
        }
    }
    if (has(g, "class") && (!is_str(get(g, "class")) || !match_class(get(g, "class")->string))) {
        char *y = field_of("%s.class", tf);
        err(k, y, "must be lowercase class names");
        free(y);
    }
    if (sized(win) && placed(get(g, "rect"), false) && placed(get(w, "rect"), false)) {
        bool said[2] = {false, false};
        rectv gr = rect_of(get(g, "rect")), wr = rect_of(get(w, "rect"));
        const sk_json *tr = !falsy(t) ? get(t, "rect") : NULL;
        rectv trv = rect_of(tr);
        for (int pass = 0; pass < 2; pass++) {
            double W = num_of(get(win, pass ? "width" : "minWidth"));
            double H = num_of(get(win, pass ? "height" : "minHeight"));
            const char *at = pass ? "initial" : "minimum";
            box_t b = box(&gr, W, H - SK_STRIP);
            if (over(b, box(&wr, W, H - SK_STRIP)) && !said[0]) {
                said[0] = true;
                err(k, x, "overlaps the list at the %s window", at);
            }
            if (!falsy(t) && placed(tr, true) && over(b, box(&trv, W, H - SK_STRIP)) && !said[1]) {
                said[1] = true;
                err(k, x, "overlaps the terminal at the %s window", at);
            }
        }
    }
    free(x);
    free(tf);
}

static int type_index(const sk_json *type)
{
    for (size_t i = 0; i < SK_LEN(TYPES); i++)
        if (str_eq(type, TYPES[i].type))
            return (int)i;
    return -1;
}

static const char *list_of(int type)
{
    return type == SK_WIDGET_LIST ? "checklist.items" : type == SK_WIDGET_LIMITS ? "limits.items" : NULL;
}

static void check_widget(checker *k, const sk_json *w, const char *fld, const sk_json *win, const sk_json *t,
                         const splits_t *ids, void *seen_);

static void check_widgets(checker *k, const sk_json *root, const sk_json *win, const sk_json *t, const splits_t *ids)
{
    if (!has(root, "widgets"))
        return;
    const sk_json *widgets = get(root, "widgets");
    if (!is_array(widgets)) {
        err(k, "widgets", "must be an array");
        return;
    }
    SK_VEC(sk_str) seen = {0};
    for (size_t i = 0; i < widgets->array.len; i++) {
        char *fld = field_of("widgets[%zu]", i);
        check_widget(k, &widgets->array.items[i], fld, win, t, ids, &seen);
        free(fld);
    }
    sk_vec_free(&seen);
}

/* One error at <fld>.<name>. */
static void err_at(checker *k, const char *fld, const char *name, const char *fmt, ...) SK_PRINTF(4, 5);

static void err_at(checker *k, const char *fld, const char *name, const char *fmt, ...)
{
    char *x = field_of("%s.%s", fld, name);
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    err(k, x, "%s", sk_buf_cstr(&b));
    sk_buf_free(&b);
    free(x);
}

static void check_marks(checker *k, const sk_json *w, const char *fld)
{
    const sk_json *marks = get(w, "marks");
    if (!is_array(marks)) {
        err_at(k, fld, "marks", "must be an array");
        return;
    }
    for (size_t j = 0; j < marks->array.len; j++) {
        const sk_json *m = &marks->array.items[j];
        char *mf = field_of("%s.marks[%zu]", fld, j);
        if (falsy(m) || !is_obj(m)) {
            err(k, mf, "must be an object");
            free(mf);
            continue;
        }
        static const char *const MK[] = {"bind", "value", "class", NULL};
        keys(k, m, mf, MK, NULL);
        if (has(m, "bind") == has(m, "value"))
            err(k, mf, "needs exactly one of bind, value");
        if (has(m, "bind") && !known(get(m, "bind"))) {
            sk_buf s = {0};
            js_str(&s, get(m, "bind"));
            err_at(k, mf, "bind", "unknown metric \"%s\"", sk_buf_cstr(&s));
            sk_buf_free(&s);
        }
        if (has(m, "value") && !is_num(get(m, "value")))
            err_at(k, mf, "value", "must be a number");
        if (has(m, "class") && (!is_str(get(m, "class")) || !match_class(get(m, "class")->string)))
            err_at(k, mf, "class", "must be lowercase class names");
        free(mf);
    }
}

static void check_sprite(checker *k, const sk_json *w, const char *fld)
{
    static const char *const FR[] = {"frames", "frameWidth", "frameHeight"};
    for (int j = 0; j < 3; j++)
        if (!is_num(get(w, FR[j])) || num_of(get(w, FR[j])) < 1)
            err_at(k, fld, FR[j], "required, a positive number");
    if (has(w, "layout") && !str_eq(get(w, "layout"), "row") && !str_eq(get(w, "layout"), "column"))
        err_at(k, fld, "layout", "must be row or column");
    if (has(w, "mode") && !str_eq(get(w, "mode"), "value") && !str_eq(get(w, "mode"), "run"))
        err_at(k, fld, "mode", "must be value or run");
    char *x = field_of("%s.sheet", fld);
    char *sheet = file(k, get(w, "sheet"), x, EXT_PNG);
    if (sheet && is_num(get(w, "frames")) && is_num(get(w, "frameWidth")) && is_num(get(w, "frameHeight"))) {
        uint32_t sw, sh;
        bool col = str_eq(get(w, "layout"), "column");
        double fr = num_of(get(w, "frames")), fw = num_of(get(w, "frameWidth")), fh = num_of(get(w, "frameHeight"));
        double ww = col ? fw : fw * fr, wh = col ? fh * fr : fh;
        if (!png_size(sheet, &sw, &sh)) {
            err(k, x, "not a PNG file");
        } else if (sw != ww || sh != wh) {
            sk_buf a = {0};
            sk_buf_printf(&a, "is %u x %u px, ", (unsigned)sw, (unsigned)sh);
            jnum(&a, fr);
            sk_buf_append_c(&a, " frames of ");
            jnum(&a, fw);
            sk_buf_append_c(&a, " x ");
            jnum(&a, fh);
            sk_buf_printf(&a, " in a %s need ", col ? "column" : "row");
            jnum(&a, ww);
            sk_buf_append_c(&a, " x ");
            jnum(&a, wh);
            err(k, x, "%s", sk_buf_cstr(&a));
            sk_buf_free(&a);
        }
    }
    free(sheet);
    free(x);
}

static void check_widget(checker *k, const sk_json *w, const char *fld, const sk_json *win, const sk_json *t,
                         const splits_t *ids, void *seen_)
{
    SK_VEC(sk_str) *seen = seen_;
    if (falsy(w) || !is_obj(w)) {
        err(k, fld, "must be an object");
        return;
    }
    int type = type_index(get(w, "type"));
    if (type < 0) {
        sk_buf s = {0};
        js_str(&s, get(w, "type"));
        err_at(k, fld, "type",
               "unknown widget type \"%s\", one of gauge, needle, knob, bar, led, readout, sprite, list, limits",
               sk_buf_cstr(&s));
        sk_buf_free(&s);
        return;
    }
    keys(k, w, fld, COMMON, TYPES[type].keys);
    const sk_json *id = get(w, "id");
    if (!is_str(id) || !match_id(id->string)) {
        err_at(k, fld, "id", "must be lowercase letters, digits and dashes");
    } else {
        bool dup = false;
        for (size_t j = 0; j < seen->len; j++)
            dup = dup || sk_str_eq(seen->ptr[j], id->string);
        if (dup)
            err_at(k, fld, "id", "duplicate id \"%.*s\"", (int)id->string.len, id->string.ptr);
        else
            sk_vec_push(seen, id->string);
    }
    const sk_json *bind = get(w, "bind");
    const char *lo = list_of(type);
    if (!is_str(bind))
        err_at(k, fld, "bind", "missing metric name");
    else if (!sk_metric_known(bind->string))
        err_at(k, fld, "bind", "unknown metric \"%.*s\"", (int)bind->string.len, bind->string.ptr);
    else if (lo && !sk_str_eq_c(bind->string, lo))
        err_at(k, fld, "bind", "\"%.*s\": a %s widget shows %s", (int)bind->string.len, bind->string.ptr,
               TYPES[type].type, lo);
    else if (!lo && sk_metric_is_list(bind->string))
        err_at(k, fld, "bind", "\"%.*s\": a list-valued metric needs a %s widget", (int)bind->string.len,
               bind->string.ptr, sk_str_eq_c(bind->string, "checklist.items") ? "list" : "limits");
    if (type == SK_WIDGET_LIMITS) {
        if (has(w, "meter") && !str_eq(get(w, "meter"), "bar") && !str_eq(get(w, "meter"), "needle"))
            err_at(k, fld, "meter", "must be bar or needle");
        if (has(w, "direction") && !str_eq(get(w, "direction"), "row") && !str_eq(get(w, "direction"), "column"))
            err_at(k, fld, "direction", "must be row or column");
        if (has(w, "itemClass") && (!is_str(get(w, "itemClass")) || !match_class(get(w, "itemClass")->string)))
            err_at(k, fld, "itemClass", "must be lowercase class names");
    }
    if (has(w, "resets") && (!is_str(get(w, "resets")) || !sk_metric_known(get(w, "resets")->string))) {
        sk_buf s = {0};
        js_str(&s, get(w, "resets"));
        err_at(k, fld, "resets", "unknown metric \"%s\"", sk_buf_cstr(&s));
        sk_buf_free(&s);
    }
    if (has(w, "fallback")) {
        const sk_json *f = get(w, "fallback");
        char *x = field_of("%s.fallback", fld);
        if (falsy(f) || !is_obj(f) || is_array(f)) {
            err(k, x, "must be an object of bind, label and resets");
        } else {
            static const char *const FB[] = {"bind", "label", "resets", NULL};
            keys(k, f, x, FB, NULL);
            static const char *const BR[] = {"bind", "resets"};
            for (int j = 0; j < 2; j++)
                if (has(f, BR[j]) && (!is_str(get(f, BR[j])) || !sk_metric_known(get(f, BR[j])->string))) {
                    sk_buf s = {0};
                    js_str(&s, get(f, BR[j]));
                    err_at(k, x, BR[j], "unknown metric \"%s\"", sk_buf_cstr(&s));
                    sk_buf_free(&s);
                }
            if (!has(f, "bind"))
                err_at(k, x, "bind", "missing metric name");
            if (has(f, "label") && !is_string(get(f, "label")))
                err_at(k, x, "label", "must be a string");
        }
        free(x);
    }
    char *x = field_of("%s.rect", fld);
    rect(k, get(w, "rect"), x, false);
    free(x);
    if (has(w, "follow"))
        follow_check(k, get(w, "follow"), get(w, "rect"), fld, ids);
    if (has(w, "class") && (!is_str(get(w, "class")) || !match_class(get(w, "class")->string)))
        err_at(k, fld, "class", "must be lowercase class names");
    if (has(w, "label") && !is_string(get(w, "label")))
        err_at(k, fld, "label", "must be a string");
    static const char *const NUMS[] = {"min", "max", "startAngle", "endAngle", "thickness", "ticks",   "segments",
                                       "gap", "on",  "pulseMs",    "blink",    "fps",       "damping", "rows"};
    for (size_t j = 0; j < SK_LEN(NUMS); j++)
        if (has(w, NUMS[j]) && !is_num(get(w, NUMS[j])))
            err_at(k, fld, NUMS[j], "must be a number");
    if (is_num(get(w, "min")) && is_num(get(w, "max")) && num_of(get(w, "max")) <= num_of(get(w, "min")))
        err_at(k, fld, "max", "must be larger than min");
    if (has(w, "format") && (!is_string(get(w, "format")) || !match_format(get(w, "format")->string))) {
        sk_buf s = {0};
        js_str(&s, get(w, "format"));
        err_at(k, fld, "format", "\"%s\": one of int, fixed:N, pct, usd, tokens, duration, until, bytes, text",
               sk_buf_cstr(&s));
        sk_buf_free(&s);
    }
    static const char *const PS[] = {"prefix", "suffix"};
    for (int j = 0; j < 2; j++)
        if (has(w, PS[j]) && !is_string(get(w, PS[j])))
            err_at(k, fld, PS[j], "must be a string");
    if (has(w, "orientation") && !str_eq(get(w, "orientation"), "horizontal") &&
        !str_eq(get(w, "orientation"), "vertical"))
        err_at(k, fld, "orientation", "must be horizontal or vertical");
    if (has(w, "pulse") && (!get(w, "pulse") || get(w, "pulse")->type != SK_JSON_BOOL))
        err_at(k, fld, "pulse", "must be true or false");
    if (type == SK_WIDGET_LED && !has(w, "on") && falsy(get(w, "pulse")))
        err(k, fld, "a led needs on (a threshold) or pulse: true");
    if (has(w, "face")) {
        x = field_of("%s.face", fld);
        free(file(k, get(w, "face"), x, sk_ext_image));
        free(x);
    }
    if (has(w, "image")) {
        x = field_of("%s.image", fld);
        free(file(k, get(w, "image"), x, sk_ext_image));
        free(x);
    }
    if (has(w, "marks"))
        check_marks(k, w, fld);
    if (type == SK_WIDGET_LIST && has(w, "toggle"))
        check_toggle(k, w, fld, win, t, ids);
    if (type == SK_WIDGET_SPRITE)
        check_sprite(k, w, fld);
}

static void check_sounds(checker *k, const sk_json *root)
{
    if (!has(root, "sounds"))
        return;
    const sk_json *sounds = get(root, "sounds");
    if (!is_array(sounds)) {
        err(k, "sounds", "must be an array");
        return;
    }
    if (sounds->array.len > MAX_SOUNDS)
        err(k, "sounds", "has %zu sounds, at most %d", sounds->array.len, MAX_SOUNDS);
    for (size_t i = 0; i < sounds->array.len; i++) {
        const sk_json *s = &sounds->array.items[i];
        char *fld = field_of("sounds[%zu]", i);
        if (falsy(s) || !is_obj(s) || is_array(s)) {
            err(k, fld, "must be an object");
            free(fld);
            continue;
        }
        static const char *const S[] = {"file", "mode", "volume", "interval", NULL};
        keys(k, s, fld, S, NULL);
        char *x = field_of("%s.file", fld);
        free(file(k, get(s, "file"), x, sk_ext_audio));
        free(x);
        const sk_json *mode = get(s, "mode");
        if (!str_eq(mode, "loop") && !str_eq(mode, "random"))
            err_at(k, fld, "mode", "must be loop or random");
        if (has(s, "volume") &&
            (!is_num(get(s, "volume")) || num_of(get(s, "volume")) < 0 || num_of(get(s, "volume")) > 1))
            err_at(k, fld, "volume", "must be a number from 0 to 1");
        if (str_eq(mode, "random")) {
            const sk_json *iv = get(s, "interval");
            if (!is_array(iv) || iv->array.len != 2 || !is_num(&iv->array.items[0]) || !is_num(&iv->array.items[1]))
                err_at(k, fld, "interval", "required for a random sound: [min, max] in seconds");
            else if (num_of(&iv->array.items[0]) < 1 || num_of(&iv->array.items[1]) < num_of(&iv->array.items[0]) ||
                     num_of(&iv->array.items[1]) > MAX_INTERVAL)
                err_at(k, fld, "interval", "needs 1 <= min <= max <= %d seconds", MAX_INTERVAL);
        } else if (has(s, "interval")) {
            err_at(k, fld, "interval", "only a random sound has an interval");
        }
        free(fld);
    }
}

/* The splits at their min at the minimum window and at their max at
 * the initial window (decision 0035). */
typedef struct {
    char *fld;
    rectv rect;
    const sk_json *follow;
} room_el;

static void check_room(checker *k, const sk_json *root, const splits_t *ids)
{
    const sk_json *win = get(root, "window"), *t = get(root, "terminal"), *m = get(root, "menu");
    rectv term = rect_of(get(t, "rect"));
    SK_VEC(room_el) els = {0};
    room_el menu = {sk_xstrdup("menu"), rect_of(get(m, "rect")), NULL};
    sk_vec_push(&els, menu);
    const sk_json *panels = get(root, "panels"), *widgets = get(root, "widgets");
    for (size_t i = 0; is_array(panels) && i < panels->array.len; i++) {
        const sk_json *p = &panels->array.items[i];
        room_el e = {field_of("panels[%zu]", i), rect_of(get(p, "rect")), get(p, "follow")};
        sk_vec_push(&els, e);
    }
    for (size_t i = 0; is_array(widgets) && i < widgets->array.len; i++) {
        const sk_json *w = &widgets->array.items[i];
        room_el e = {field_of("widgets[%zu]", i), rect_of(get(w, "rect")), get(w, "follow")};
        sk_vec_push(&els, e);
        const sk_json *g = get(w, "toggle");
        if (!falsy(g)) {
            room_el te = {field_of("widgets[%zu].toggle", i), rect_of(get(g, "rect")), get(g, "follow")};
            sk_vec_push(&els, te);
        }
    }
    const sk_json *fsv = get(t, "fontSize");
    double fs = falsy(fsv) ? 14 : js_num(fsv);
    double *at = sk_xcalloc(ids->n ? ids->n : 1, sizeof *at);
    box_t *a = sk_xcalloc(els.len, sizeof *a), *b = sk_xcalloc(els.len, sizeof *b);
    for (int pass = 0; pass < 2; pass++) {
        double W = num_of(get(win, pass ? "width" : "minWidth"));
        double H = num_of(get(win, pass ? "height" : "minHeight")) - SK_STRIP;
        char *where = field_of("with the splits at their %s at the %s window", pass ? "max" : "min",
                               pass ? "initial" : "minimum");
        sizes(ids, &term, win, pass == 1, W, H, at);
        rectv tr = terminal_at(&term, ids, at);
        double cols = floor((W - tr.v[0] - tr.v[1]) / (fs * 0.6));
        double rows = floor((H - tr.v[3] - tr.v[4]) / (fs * 1.2));
        if (cols < 40 || rows < 10) {
            sk_buf c1 = {0}, c2 = {0};
            jnum(&c1, cols);
            jnum(&c2, rows);
            err(k, "terminal.rect", "leaves about %s x %s cells %s, needs 40 x 10", sk_buf_cstr(&c1), sk_buf_cstr(&c2),
                where);
            sk_buf_free(&c1);
            sk_buf_free(&c2);
        }
        box_t ta = box(&term, W, H), tb = box(&tr, W, H);
        for (size_t i = 0; i < els.len; i++) {
            a[i] = box(&els.ptr[i].rect, W, H);
            rectv moved = follow_rect(&els.ptr[i].rect, els.ptr[i].follow, ids, &term, at);
            b[i] = box(&moved, W, H);
        }
        for (size_t i = 0; i < els.len; i++) {
            char *f = field_of("%s.rect", els.ptr[i].fld);
            if (!falsy(els.ptr[i].follow) && (b[i].x1 <= b[i].x0 || b[i].y1 <= b[i].y0))
                err(k, f, "has no size %s", where);
            if (!over(a[i], ta) && over(b[i], tb))
                err(k, f, "overlaps the terminal %s", where);
            for (size_t j = i + 1; j < els.len; j++)
                if (!over(a[i], a[j]) && over(b[i], b[j]))
                    err(k, f, "meets %s %s", els.ptr[j].fld, where);
            free(f);
        }
        free(where);
    }
    for (size_t i = 0; i < els.len; i++)
        free(els.ptr[i].fld);
    sk_vec_free(&els);
    free(at);
    free(a);
    free(b);
}

/* ---- skinc's own refusals ---- */

static const char *const DECODE_IMAGE[] = {".png", ".jpg", ".jpeg", ".gif", ".svg", NULL};
static const char *const DECODE_FONT[] = {".ttf", ".otf", NULL};
static const char *const DECODE_AUDIO[] = {".ogg", ".wav", NULL};

static void skinc_file(checker *k, const sk_json *p, const char *field, const char *const *ok, const char *what)
{
    if (!is_str(p))
        return;
    char *s = sk_xstrndup(p->string.ptr, p->string.len);
    if (!ext_in(s, ok))
        err(k, field, "\"%s\": skinc cannot compile this file type: %s", s, what);
    free(s);
}

static bool weight_of(const sk_json *v, uint16_t *out)
{
    *out = 400;
    if (!v || str_eq(v, "normal"))
        return true;
    if (str_eq(v, "bold")) {
        *out = 700;
        return true;
    }
    double d = v->type == SK_JSON_NUMBER ? v->number : v->type == SK_JSON_STRING ? js_num(v) : NAN;
    if (!(d >= 1 && d <= 1000))
        return false;
    *out = (uint16_t)lround(d);
    return true;
}

static void skinc_checks(checker *k, const sk_json *root)
{
    const sk_json *fonts = get(root, "fonts");
    for (size_t i = 0; is_array(fonts) && i < fonts->array.len; i++) {
        const sk_json *f = &fonts->array.items[i];
        char *x = field_of("fonts[%zu].file", i);
        skinc_file(k, get(f, "file"), x, DECODE_FONT, "fonts are .ttf or .otf with TrueType outlines");
        free(x);
        uint16_t w;
        if (!weight_of(get(f, "weight"), &w)) {
            x = field_of("fonts[%zu].weight", i);
            err(k, x, "must be normal, bold or a number from 1 to 1000");
            free(x);
        }
    }
    const sk_json *m = get(root, "menu");
    static const char *const MI[] = {"image", "hover", "pressed"};
    for (int i = 0; i < 3; i++) {
        char *x = field_of("menu.%s", MI[i]);
        skinc_file(k, get(m, MI[i]), x, DECODE_IMAGE, "images are PNG, JPEG, GIF or SVG");
        free(x);
    }
    const sk_json *widgets = get(root, "widgets");
    for (size_t i = 0; is_array(widgets) && i < widgets->array.len; i++) {
        const sk_json *w = &widgets->array.items[i];
        static const char *const WI[] = {"face", "image"};
        for (int j = 0; j < 2; j++) {
            char *x = field_of("widgets[%zu].%s", i, WI[j]);
            skinc_file(k, get(w, WI[j]), x, DECODE_IMAGE, "images are PNG, JPEG, GIF or SVG");
            free(x);
        }
        static const char *const COUNTS[] = {"ticks", "segments", "frames", "rows"};
        for (size_t j = 0; j < SK_LEN(COUNTS); j++) {
            const sk_json *v = get(w, COUNTS[j]);
            if (is_num(v) && (num_of(v) < 0 || num_of(v) > 256)) {
                char *x = field_of("widgets[%zu].%s", i, COUNTS[j]);
                err(k, x, "skinc takes 0 to 256");
                free(x);
            }
        }
    }
    const sk_json *sounds = get(root, "sounds");
    for (size_t i = 0; is_array(sounds) && i < sounds->array.len; i++) {
        char *x = field_of("sounds[%zu].file", i);
        skinc_file(k, get(&sounds->array.items[i], "file"), x, DECODE_AUDIO,
                   "sounds are Ogg Vorbis (.ogg) or PCM WAVE (.wav)");
        free(x);
    }
}

/* ---- The model ---- */

static const char *dup_str(sk_skin *s, const sk_json *v)
{
    if (!is_string(v))
        return NULL;
    return sk_arena_strndup(&s->arena, v->string.ptr ? v->string.ptr : "", v->string.len);
}

static float fnum(const sk_json *v, float dflt)
{
    return is_num(v) ? (float)num_of(v) : dflt;
}

static int split_index(const sk_skin *s, const sk_json *id)
{
    for (size_t i = 0; is_string(id) && i < s->nsplits; i++)
        if (sk_str_eq_c(id->string, s->splits[i].id))
            return (int)i;
    return -1;
}

static sk_pack_rect model_rect(const sk_skin *s, const sk_json *r, const sk_json *follow)
{
    sk_pack_rect out;
    memset(&out, 0, sizeof out);
    for (int i = 0; i < 6; i++) {
        out.follow[i] = -1;
        if (has(r, RECT_KEYS[i])) {
            out.has |= (uint8_t)(1 << i);
            out.v[i] = (float)num_of(get(r, RECT_KEYS[i]));
        }
    }
    for (size_t i = 0; follow && follow->type == SK_JSON_OBJECT && i < follow->object.len; i++) {
        int key = rect_key(follow->object.members[i].key);
        if (key >= 0)
            out.follow[key] = (int8_t)split_index(s, &follow->object.members[i].value);
    }
    return out;
}

static uint32_t color_of(const char *hex)
{
    const char *p = hex + 1;
    size_t n = strlen(p);
    unsigned ch[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < (n <= 4 ? n : n / 2); i++)
        ch[i] = n <= 4 ? (unsigned)hex2((char[]){p[i], p[i]}) : (unsigned)hex2(p + 2 * i);
    return (uint32_t)ch[3] << 24 | (uint32_t)ch[0] << 16 | (uint32_t)ch[1] << 8 | ch[2];
}

static void model_widget(sk_skin *s, const sk_json *w, sk_skin_widget *out)
{
    memset(out, 0, sizeof *out);
    sk_pack_widget *x = &out->w;
    x->type = (uint8_t)type_index(get(w, "type"));
    x->id = dup_str(s, get(w, "id"));
    x->bind = dup_str(s, get(w, "bind"));
    x->label = dup_str(s, get(w, "label"));
    x->prefix = dup_str(s, get(w, "prefix"));
    x->suffix = dup_str(s, get(w, "suffix"));
    x->resets = dup_str(s, get(w, "resets"));
    const sk_json *fb = get(w, "fallback");
    if (fb) {
        x->fallback_bind = dup_str(s, get(fb, "bind"));
        x->fallback_label = dup_str(s, get(fb, "label"));
        x->fallback_resets = dup_str(s, get(fb, "resets"));
    }
    out->has_min = has(w, "min");
    out->has_max = has(w, "max");
    out->has_damping = has(w, "damping");
    out->has_start = has(w, "startAngle");
    out->has_end = has(w, "endAngle");
    out->has_thickness = has(w, "thickness");
    out->has_gap = has(w, "gap");
    out->has_label = has(w, "label");
    out->has_format = has(w, "format");
    x->min = fnum(get(w, "min"), 0);
    x->max = fnum(get(w, "max"), 100);
    /* widgets.js's defaults, by type. */
    bool needle = x->type == SK_WIDGET_NEEDLE ||
                  (x->type == SK_WIDGET_LIMITS && str_eq(get(w, "meter"), "needle"));
    float d_start = needle ? -60 : -135, d_end = needle ? 60 : 135;
    float d_damp = x->type == SK_WIDGET_NEEDLE ? 0.55f : x->type == SK_WIDGET_KNOB ? 0.8f : 1;
    if (x->type == SK_WIDGET_LIMITS && needle)
        d_damp = 0.55f;
    x->damping = fnum(get(w, "damping"), d_damp);
    x->start_angle = fnum(get(w, "startAngle"), d_start);
    x->end_angle = fnum(get(w, "endAngle"), d_end);
    x->thickness = fnum(get(w, "thickness"), 10);
    x->gap = fnum(get(w, "gap"), 2);
    x->on = fnum(get(w, "on"), 0);
    x->has_on = has(w, "on");
    x->pulse = get(w, "pulse") && get(w, "pulse")->type == SK_JSON_BOOL && get(w, "pulse")->boolean;
    x->pulse_ms = fnum(get(w, "pulseMs"), 250);
    x->blink = fnum(get(w, "blink"), 0);
    x->fps = fnum(get(w, "fps"), 12);
    x->ticks = (uint16_t)fnum(get(w, "ticks"), 0);
    x->segments = (uint16_t)fnum(get(w, "segments"), 0);
    x->frames = (uint16_t)fnum(get(w, "frames"), 0);
    x->rows = (uint16_t)fnum(get(w, "rows"), 0);
    x->frame_w = fnum(get(w, "frameWidth"), 0);
    x->frame_h = fnum(get(w, "frameHeight"), 0);
    x->vertical = str_eq(get(w, "orientation"), "vertical");
    x->column = str_eq(get(w, "layout"), "column");
    x->run = str_eq(get(w, "mode"), "run");
    x->needle_meters = str_eq(get(w, "meter"), "needle");
    x->across = str_eq(get(w, "direction"), "row");
    const sk_json *fmt = get(w, "format");
    x->format = SK_FORMAT_INT;
    if (is_string(fmt)) {
        static const struct {
            const char *name;
            uint8_t f;
        } F[] = {{"int", SK_FORMAT_INT},       {"pct", SK_FORMAT_PCT},           {"usd", SK_FORMAT_USD},
                 {"tokens", SK_FORMAT_TOKENS}, {"duration", SK_FORMAT_DURATION}, {"until", SK_FORMAT_UNTIL},
                 {"bytes", SK_FORMAT_BYTES},   {"text", SK_FORMAT_TEXT}};
        for (size_t i = 0; i < SK_LEN(F); i++)
            if (sk_str_eq_c(fmt->string, F[i].name))
                x->format = F[i].f;
        if (sk_str_prefix(fmt->string, SK_STR("fixed:"))) {
            x->format = SK_FORMAT_FIXED;
            x->digits = (uint8_t)(fmt->string.ptr[6] - '0');
        }
    }
    const sk_json *marks = get(w, "marks");
    if (is_array(marks) && marks->array.len) {
        sk_pack_mark *m = sk_arena_array(&s->arena, marks->array.len, sizeof *m);
        for (size_t i = 0; i < marks->array.len; i++) {
            const sk_json *mk = &marks->array.items[i];
            m[i].bind = dup_str(s, get(mk, "bind"));
            m[i].value = fnum(get(mk, "value"), 0);
            m[i].klass = dup_str(s, get(mk, "class"));
        }
        x->marks = m;
        x->nmarks = (uint16_t)marks->array.len;
    }
    x->el = x->toggle = x->face = x->image = x->sheet = SK_PACK_NONE;
    out->rect = model_rect(s, get(w, "rect"), get(w, "follow"));
    out->klass = dup_str(s, get(w, "class"));
    out->item_class = dup_str(s, get(w, "itemClass"));
    out->face = dup_str(s, get(w, "face"));
    out->image = dup_str(s, get(w, "image"));
    out->sheet = dup_str(s, get(w, "sheet"));
    const sk_json *g = get(w, "toggle");
    if (x->type == SK_WIDGET_LIST && g) {
        out->has_toggle = true;
        out->toggle_rect = model_rect(s, get(g, "rect"), get(g, "follow"));
        const sk_json *labels = get(g, "labels");
        out->toggle_labels[0] = is_array(labels) ? dup_str(s, &labels->array.items[0]) : "tasks";
        out->toggle_labels[1] = is_array(labels) ? dup_str(s, &labels->array.items[1]) : "readouts";
        out->toggle_class = dup_str(s, get(g, "class"));
    }
}

static void model(sk_skin *s, const sk_json *root)
{
    s->name = dup_str(s, get(root, "name"));
    s->version = dup_str(s, get(root, "version"));
    s->author = dup_str(s, get(root, "author"));
    s->description = dup_str(s, get(root, "description"));
    s->license = dup_str(s, get(root, "license"));
    const sk_json *win = get(root, "window");
    s->width = fnum(get(win, "width"), 0);
    s->height = fnum(get(win, "height"), 0);
    s->min_width = fnum(get(win, "minWidth"), 0);
    s->min_height = fnum(get(win, "minHeight"), 0);
    const sk_json *sp = get(root, "splits");
    if (is_array(sp) && sp->array.len) {
        s->splits = sk_arena_array(&s->arena, sp->array.len, sizeof *s->splits);
        for (size_t i = 0; i < sp->array.len; i++) {
            const sk_json *x = &sp->array.items[i];
            s->splits[i].id = dup_str(s, get(x, "id"));
            s->splits[i].edge = (uint8_t)edge_index(get(x, "edge"));
            s->splits[i].min = fnum(get(x, "min"), 0);
            s->splits[i].max = fnum(get(x, "max"), 0);
        }
        s->nsplits = sp->array.len;
    }
    const sk_json *m = get(root, "menu");
    s->menu_rect = model_rect(s, get(m, "rect"), NULL);
    s->menu_image = dup_str(s, get(m, "image"));
    s->menu_hover = dup_str(s, get(m, "hover"));
    s->menu_pressed = dup_str(s, get(m, "pressed"));
    const sk_json *t = get(root, "terminal");
    s->terminal_rect = model_rect(s, get(t, "rect"), NULL);
    s->terminal_font = dup_str(s, get(t, "fontFamily"));
    s->terminal_font_size = fnum(get(t, "fontSize"), 14);
    const sk_json *th = get(t, "theme");
    for (int i = 0; i < 23; i++) {
        const sk_json *c = get(th, sk_theme_keys[i]);
        if (is_string(c)) {
            char *hex = sk_xstrndup(c->string.ptr, c->string.len);
            s->theme[i] = color_of(hex);
            s->has_theme |= 1u << i;
            free(hex);
        }
    }
    const sk_json *fonts = get(root, "fonts");
    if (is_array(fonts) && fonts->array.len) {
        s->fonts = sk_arena_array(&s->arena, fonts->array.len, sizeof *s->fonts);
        for (size_t i = 0; i < fonts->array.len; i++) {
            const sk_json *f = &fonts->array.items[i];
            s->fonts[i].family = dup_str(s, get(f, "family"));
            s->fonts[i].file = dup_str(s, get(f, "file"));
            weight_of(get(f, "weight"), &s->fonts[i].weight);
            s->fonts[i].style = str_eq(get(f, "style"), "italic") ? SK_FSTYLE_ITALIC : SK_FSTYLE_NORMAL;
        }
        s->nfonts = fonts->array.len;
    }
    s->stylesheet = dup_str(s, get(root, "stylesheet"));
    const sk_json *panels = get(root, "panels");
    if (is_array(panels) && panels->array.len) {
        s->panels = sk_arena_array(&s->arena, panels->array.len, sizeof *s->panels);
        for (size_t i = 0; i < panels->array.len; i++) {
            const sk_json *p = &panels->array.items[i];
            s->panels[i].klass = dup_str(s, get(p, "class"));
            s->panels[i].rect = model_rect(s, get(p, "rect"), get(p, "follow"));
        }
        s->npanels = panels->array.len;
    }
    const sk_json *widgets = get(root, "widgets");
    if (is_array(widgets) && widgets->array.len) {
        s->widgets = sk_arena_array(&s->arena, widgets->array.len, sizeof *s->widgets);
        for (size_t i = 0; i < widgets->array.len; i++)
            model_widget(s, &widgets->array.items[i], &s->widgets[i]);
        s->nwidgets = widgets->array.len;
    }
    const sk_json *sounds = get(root, "sounds");
    if (is_array(sounds) && sounds->array.len) {
        s->sounds = sk_arena_array(&s->arena, sounds->array.len, sizeof *s->sounds);
        for (size_t i = 0; i < sounds->array.len; i++) {
            const sk_json *x = &sounds->array.items[i];
            s->sounds[i].file = dup_str(s, get(x, "file"));
            s->sounds[i].mode = str_eq(get(x, "mode"), "random") ? SK_SOUND_RANDOM : SK_SOUND_LOOP;
            s->sounds[i].volume = fnum(get(x, "volume"), 0.3f);
            const sk_json *iv = get(x, "interval");
            if (is_array(iv) && iv->array.len == 2) {
                s->sounds[i].interval[0] = fnum(&iv->array.items[0], 0);
                s->sounds[i].interval[1] = fnum(&iv->array.items[1], 0);
            }
        }
        s->nsounds = sounds->array.len;
    }
}

/* ---- Loading ---- */

static char *read_all(const char *path, size_t *n, int *error)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        *error = errno;
        return NULL;
    }
    sk_buf b = {0};
    char chunk[65536];
    for (;;) {
        ssize_t r = read(fd, chunk, sizeof chunk);
        if (r < 0) {
            *error = errno;
            close(fd);
            sk_buf_free(&b);
            return NULL;
        }
        if (r == 0)
            break;
        sk_buf_append(&b, chunk, (size_t)r);
    }
    close(fd);
    *n = b.len;
    if (!b.ptr)
        sk_buf_reserve(&b, 0);
    return b.ptr;
}

bool sk_skin_load(sk_skin *skin, const char *dir, sk_diags *d, sk_css_check check_css, void *data)
{
    memset(skin, 0, sizeof *skin);
    sk_arena_init(&skin->arena, 0);
    size_t dl = strlen(dir);
    while (dl > 1 && dir[dl - 1] == '/')
        dl--;
    skin->dir = sk_arena_strndup(&skin->arena, dir, dl);
    checker k;
    memset(&k, 0, sizeof k);
    k.skin = skin;
    k.d = d;
    k.errors_at = d->items.len;
    sk_buf rb = {0};
    sk_js_resolve(&rb, sk_str_c(skin->dir));
    k.root = rb.ptr ? rb.ptr : sk_xstrdup("/");
    skin->real = realpath(k.root, NULL);
    bool ok = false;
    char *mfp = sk_path_join(skin->dir, "skin.json");
    k.mf = mfp;
    char *text = NULL;
    sk_arena ja;
    sk_arena_init(&ja, 0);
    if (!skin->real) {
        sk_diag(d, skin->dir, "skin", "directory not found");
        goto out;
    }
    char *path = sk_path_join(skin->real, "skin.json");
    size_t n = 0;
    int error = 0;
    text = read_all(path, &n, &error);
    free(path);
    if (!text) {
        if (error == ENOENT)
            err(&k, "(file)", "missing");
        else
            err(&k, "(file)", "not valid JSON: %s", strerror(error));
        goto out;
    }
    sk_json_error je;
    sk_json *root = sk_json_parse(&ja, text, n, &je);
    if (!root) {
        err(&k, "(file)", "not valid JSON: %s at byte %zu", je.message, je.offset);
        goto out;
    }
    if (root->type != SK_JSON_OBJECT) {
        err(&k, "(root)", "must be an object");
        goto out;
    }
    static const char *const TOP[] = {"name",   "version", "author", "description", "license", "window", "terminal",
                                      "menu",   "fonts",   "stylesheet", "panels",  "widgets", "sounds", "splits",
                                      NULL};
    keys(&k, root, "", TOP, NULL);
    const sk_json *name = get(root, "name");
    if (!is_str(name) || !match_id(name->string))
        err(&k, "name", "must be lowercase letters, digits and dashes");
    static const char *const META[] = {"version", "author", "description", "license"};
    for (size_t i = 0; i < SK_LEN(META); i++)
        if (has(root, META[i]) && !is_string(get(root, META[i])))
            err(&k, META[i], "must be a string");
    const sk_json *win = get(root, "window"), *t = get(root, "terminal");
    check_window(&k, win);
    check_terminal(&k, t, win);
    splits_t ids = {NULL, 0};
    check_splits(&k, root, t, &ids);
    check_menu(&k, get(root, "menu"), win, t);
    check_fonts(&k, root);
    check_stylesheet(&k, root, check_css, data);
    check_panels(&k, root, &ids);
    check_widgets(&k, root, win, t, &ids);
    check_sounds(&k, root);
    if (!nerrors(&k) && ids.n)
        check_room(&k, root, &ids);
    walk(&k, skin->real, skin->dir);
    if (!nerrors(&k))
        skinc_checks(&k, root);
    if (!nerrors(&k)) {
        model(skin, root);
        ok = true;
    }
out:
    free(text);
    free(mfp);
    free(k.root);
    sk_arena_free(&ja);
    return ok;
}

void sk_skin_free(sk_skin *skin)
{
    free(skin->real);
    sk_arena_free(&skin->arena);
    memset(skin, 0, sizeof *skin);
}
