/* Port of app/settings.js (decisions 0022, 0035), with the checks of
 * the settings:set and panels:set handlers of app/main.js. */
#include "appsettings.h"

#include "js.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const double FONT_SIZES[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32};

const sk_setting sk_settings[SK_SETTINGS_COUNT] = {
    {"terminal.fontSize", "Terminal font size", "terminal.fontSize", "px", FONT_SIZES,
     SK_LEN(FONT_SIZES), 14},
};

static const sk_setting *by_id(sk_str id, size_t *index)
{
    for (size_t i = 0; i < SK_SETTINGS_COUNT; i++) {
        if (sk_str_eq_c(id, sk_settings[i].id)) {
            if (index)
                *index = i;
            return &sk_settings[i];
        }
    }
    return NULL;
}

char *sk_appsettings_file(const sk_env *e)
{
    char *state = sk_path_state(e);
    char *file = state ? sk_path_join(state, "settings.json") : NULL;
    free(state);
    return file;
}

bool sk_appsettings_valid(const char *id, const sk_json *v)
{
    const sk_setting *s = by_id(sk_str_c(id), NULL);
    if (!s || !v)
        return false;
    if (v->type == SK_JSON_NULL)
        return true;
    if (v->type != SK_JSON_NUMBER)
        return false;
    for (size_t i = 0; i < s->n_values; i++)
        if (s->values[i] == v->number)
            return true;
    return false;
}

bool sk_appsettings_size_ok(double v)
{
    return isfinite(v) && v >= 0;
}

/* A skin name or split id: /^[a-z0-9-]+$/. */
static bool name_ok(sk_str s)
{
    if (s.len == 0)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

/* A key JavaScript keeps among the array indexes: those come first, in
 * ascending order, and every other key in the order it was added. */
static bool array_index(sk_str k, uint64_t *i)
{
    if (k.len == 0 || (k.len > 1 && k.ptr[0] == '0') || !sk_str_to_u64(k, i))
        return false;
    return *i < 4294967295u;
}

/* A new key goes before existing in an object's key order. */
static bool goes_before(const char *existing, sk_str key)
{
    uint64_t a = 0, b = 0;
    if (!array_index(key, &b))
        return false;
    return !array_index(sk_str_c(existing), &a) || a > b;
}

static void add_size(sk_panel_sizes *p, sk_str id, double size)
{
    size_t at = 0;
    while (at < p->sizes.len && !goes_before(p->sizes.ptr[at].id, id))
        at++;
    sk_vec_push(&p->sizes, ((sk_panel_size){NULL, 0}));
    memmove(p->sizes.ptr + at + 1, p->sizes.ptr + at, (p->sizes.len - 1 - at) * sizeof *p->sizes.ptr);
    p->sizes.ptr[at] = (sk_panel_size){sk_xstrndup(id.ptr, id.len), size};
}

static sk_panel_sizes *add_skin(sk_appsettings *v, sk_str skin)
{
    size_t at = 0;
    while (at < v->panels.len && !goes_before(v->panels.ptr[at].skin, skin))
        at++;
    sk_vec_push(&v->panels, ((sk_panel_sizes){0}));
    memmove(v->panels.ptr + at + 1, v->panels.ptr + at, (v->panels.len - 1 - at) * sizeof *v->panels.ptr);
    v->panels.ptr[at] = (sk_panel_sizes){sk_xstrndup(skin.ptr, skin.len), {0}};
    return &v->panels.ptr[at];
}

static sk_panel_sizes *find_skin(const sk_appsettings *v, const char *skin)
{
    for (size_t i = 0; i < v->panels.len; i++)
        if (!strcmp(v->panels.ptr[i].skin, skin))
            return &v->panels.ptr[i];
    return NULL;
}

static void free_skin(sk_panel_sizes *p)
{
    for (size_t i = 0; i < p->sizes.len; i++)
        free(p->sizes.ptr[i].id);
    sk_vec_free(&p->sizes);
    free(p->skin);
}

/* panelsOf: the members already come in JavaScript's order. */
static void panels_of(sk_appsettings *out, const sk_json *raw)
{
    if (!raw || raw->type != SK_JSON_OBJECT)
        return;
    for (size_t i = 0; i < raw->object.len; i++) {
        const sk_json_member *skin = &raw->object.members[i];
        if (!name_ok(skin->key) || skin->value.type != SK_JSON_OBJECT)
            continue;
        sk_panel_sizes kept = {0};
        for (size_t k = 0; k < skin->value.object.len; k++) {
            const sk_json_member *m = &skin->value.object.members[k];
            double size = 0;
            if (name_ok(m->key) && sk_json_as_number(&m->value, &size) && sk_appsettings_size_ok(size))
                sk_vec_push(&kept.sizes, ((sk_panel_size){sk_xstrndup(m->key.ptr, m->key.len), size}));
        }
        if (kept.sizes.len) {
            kept.skin = sk_xstrndup(skin->key.ptr, skin->key.len);
            sk_vec_push(&out->panels, kept);
        }
    }
}

void sk_appsettings_read(sk_appsettings *out, const char *text, size_t n)
{
    memset(out, 0, sizeof *out);
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *raw = sk_json_parse(&a, text, n, NULL);
    for (size_t i = 0; raw && i < SK_SETTINGS_COUNT; i++) {
        const sk_json *v = sk_json_get(raw, sk_settings[i].id);
        if (sk_appsettings_valid(sk_settings[i].id, v) && v->type == SK_JSON_NUMBER) {
            out->set[i] = true;
            out->value[i] = v->number;
        }
    }
    panels_of(out, sk_json_get(raw, "panels"));
    sk_arena_free(&a);
}

void sk_appsettings_load(sk_appsettings *out, const char *file)
{
    sk_buf text = {0};
    FILE *f = fopen(file, "rb");
    bool ok = f != NULL;
    if (f) {
        char chunk[4096];
        size_t got;
        while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
            sk_buf_append(&text, chunk, got);
        ok = !ferror(f);
        fclose(f);
    }
    if (!ok)
        sk_buf_clear(&text);
    sk_appsettings_read(out, sk_buf_cstr(&text), text.len);
    sk_buf_free(&text);
}

void sk_appsettings_write(const sk_appsettings *v, sk_buf *out)
{
    sk_jw w;
    sk_jw_init(&w, out, 1);
    sk_jw_object(&w);
    for (size_t i = 0; i < SK_SETTINGS_COUNT; i++) {
        sk_jw_key(&w, sk_str_c(sk_settings[i].id));
        if (v->set[i])
            sk_jw_number(&w, v->value[i]);
        else
            sk_jw_null(&w);
    }
    sk_jw_key(&w, SK_STR("panels"));
    sk_jw_object(&w);
    for (size_t i = 0; i < v->panels.len; i++) {
        const sk_panel_sizes *p = &v->panels.ptr[i];
        sk_jw_key(&w, sk_str_c(p->skin));
        sk_jw_object(&w);
        for (size_t k = 0; k < p->sizes.len; k++) {
            sk_jw_key(&w, sk_str_c(p->sizes.ptr[k].id));
            sk_jw_number(&w, p->sizes.ptr[k].size);
        }
        sk_jw_object_end(&w);
    }
    sk_jw_object_end(&w);
    sk_jw_object_end(&w);
    sk_buf_putc(out, '\n');
}

int sk_appsettings_save(const char *file, const sk_appsettings *v)
{
    sk_buf dir = {0}, tmp = {0}, text = {0};
    sk_buf_append_str(&dir, sk_js_dirname(sk_str_c(file)));
    sk_buf_printf(&tmp, "%s.%ld.tmp", file, (long)getpid());
    sk_appsettings_write(v, &text);
    int rc = -1, fd = -1;
    if (sk_mkdirs(dir.ptr, 0777) != 0)
        goto out;
    fd = open(tmp.ptr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
        goto out;
    for (size_t done = 0; done < text.len;) {
        ssize_t n = write(fd, text.ptr + done, text.len - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            goto out;
        done += (size_t)n;
    }
    if (close(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    rc = rename(tmp.ptr, file);
out:;
    int saved = errno;
    if (fd >= 0)
        close(fd);
    if (rc != 0)
        unlink(tmp.ptr);
    sk_buf_free(&dir);
    sk_buf_free(&tmp);
    sk_buf_free(&text);
    errno = saved;
    return rc;
}

void sk_appsettings_free(sk_appsettings *v)
{
    for (size_t i = 0; i < v->panels.len; i++)
        free_skin(&v->panels.ptr[i]);
    sk_vec_free(&v->panels);
}

bool sk_appsettings_set(sk_appsettings *v, const char *id, const sk_json *value)
{
    size_t i = 0;
    if (!sk_appsettings_valid(id, value) || !by_id(sk_str_c(id), &i))
        return false;
    v->set[i] = value->type == SK_JSON_NUMBER;
    v->value[i] = v->set[i] ? value->number : 0;
    return true;
}

void sk_appsettings_set_panel(sk_appsettings *v, const char *skin, const char *id, double size)
{
    sk_panel_sizes *p = find_skin(v, skin);
    if (!id) {
        if (p) {
            size_t at = (size_t)(p - v->panels.ptr);
            free_skin(p);
            memmove(p, p + 1, (v->panels.len - at - 1) * sizeof *p);
            v->panels.len--;
        }
        return;
    }
    if (!p)
        p = add_skin(v, sk_str_c(skin));
    for (size_t i = 0; i < p->sizes.len; i++) {
        if (!strcmp(p->sizes.ptr[i].id, id)) {
            p->sizes.ptr[i].size = size;
            return;
        }
    }
    add_size(p, sk_str_c(id), size);
}

bool sk_appsettings_panel(const sk_appsettings *v, const char *skin, const char *id, double *size)
{
    const sk_panel_sizes *p = find_skin(v, skin);
    for (size_t i = 0; p && i < p->sizes.len; i++) {
        if (!strcmp(p->sizes.ptr[i].id, id)) {
            *size = p->sizes.ptr[i].size;
            return true;
        }
    }
    return false;
}

/* skinValue: the manifest's value at the setting's path, || fallback. */
static double skin_value(const sk_setting *s, const sk_json *skin)
{
    const sk_json *v = skin;
    for (const char *p = s->overrides; v && *p;) {
        const char *dot = strchr(p, '.');
        size_t n = dot ? (size_t)(dot - p) : strlen(p);
        char *key = sk_xstrndup(p, n);
        v = sk_json_get(v, key);
        free(key);
        p += n + (dot ? 1 : 0);
    }
    double d = 0;
    return sk_json_as_number(v, &d) && d != 0 ? d : s->fallback;
}

static char *choice_label(const char *prefix, double v, const char *unit, const char *suffix)
{
    sk_buf b = {0};
    sk_buf_append_c(&b, prefix);
    sk_json_format_number(&b, v);
    sk_buf_printf(&b, " %s%s", unit, suffix);
    return b.ptr;
}

void sk_appsettings_menu(const sk_appsettings *v, const sk_json *skin,
                         sk_setting_row rows[SK_SETTINGS_COUNT])
{
    for (size_t i = 0; i < SK_SETTINGS_COUNT; i++) {
        const sk_setting *s = &sk_settings[i];
        sk_setting_row *row = &rows[i];
        *row = (sk_setting_row){s, {0}};
        sk_setting_choice own = {true, 0, choice_label("Skin default (", skin_value(s, skin), s->unit, ")"),
                                 !v->set[i]};
        sk_vec_push(&row->choices, own);
        for (size_t k = 0; k < s->n_values; k++) {
            sk_setting_choice c = {false, s->values[k], choice_label("", s->values[k], s->unit, ""),
                                   v->set[i] && v->value[i] == s->values[k]};
            sk_vec_push(&row->choices, c);
        }
    }
}

void sk_appsettings_menu_free(sk_setting_row rows[SK_SETTINGS_COUNT])
{
    for (size_t i = 0; i < SK_SETTINGS_COUNT; i++) {
        for (size_t k = 0; k < rows[i].choices.len; k++)
            free(rows[i].choices.ptr[k].label);
        sk_vec_free(&rows[i].choices);
    }
}
