/* The skinc suites' throwaway skins and lookups. */
#include "util.h"

#include "../../src/base/paths.h"
#include "../test.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void sk_test_write(const char *dir, const char *name, const char *text)
{
    sk_test_write_bytes(dir, name, text, strlen(text));
}

void sk_test_write_bytes(const char *dir, const char *name, const void *data, size_t n)
{
    char *path = sk_path_join(dir, name);
    char *slash = strrchr(path, '/');
    if (slash) {
        *slash = 0;
        sk_mkdirs(path, 0700);
        *slash = '/';
    }
    FILE *f = fopen(path, "wb");
    if (f) {
        if (n)
            fwrite(data, 1, n, f);
        fclose(f);
    }
    free(path);
}

const char *sk_test_skin(const char *panels, const char *widgets, const char *css)
{
    static unsigned n;
    static char dir[1024];
    snprintf(dir, sizeof dir, "%s/skin%u", sk_test_tmpdir(), n++);
    mkdir(dir, 0700);
    sk_buf m = {0};
    sk_buf_printf(&m,
                  "{\"name\": \"test\", \"window\": {\"width\": 1000, \"height\": 700, \"minWidth\": 800, "
                  "\"minHeight\": 500},\n"
                  " \"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 24, \"height\": 24}, \"image\": "
                  "\"menu.svg\"},\n"
                  " \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, \"right\": 10, \"bottom\": 10},\n"
                  "   \"theme\": {\"foreground\": \"#e0e0e0\", \"background\": \"#000000\"}},\n"
                  " \"stylesheet\": \"skin.css\",\n"
                  " \"panels\": %s,\n"
                  " \"widgets\": %s}\n",
                  panels ? panels : "[]", widgets ? widgets : "[]");
    sk_test_write(dir, "skin.json", sk_buf_cstr(&m));
    sk_buf_free(&m);
    sk_test_write(dir, "menu.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\"/>\n");
    sk_test_write(dir, "skin.css", css ? css : "");
    return dir;
}

bool sk_test_compile(sk_front *f, sk_diags *d, const char *panels, const char *widgets, const char *css)
{
    const char *dir = sk_test_skin(panels, widgets, css);
    return sk_front_run(f, dir, d);
}

sk_css_rule *sk_test_rule(sk_css_sheet *sheet, const char *css, sk_diags *d)
{
    sk_css_parse(sheet, "t.css", css, strlen(css), d);
    return sheet->rules.len ? &sheet->rules.ptr[0] : NULL;
}

const char *sk_test_prop(const sk_css_style *s, uint16_t prop)
{
    static sk_buf b;
    sk_buf_clear(&b);
    if (s)
        sk_prop_print(&b, prop, s);
    else
        sk_buf_append_c(&b, "(no style)");
    return sk_buf_cstr(&b);
}

static bool no_image(void *data, sk_str url, uint32_t *index)
{
    (void)data;
    *index = sk_str_eq_c(url, "a.png") ? 3 : 0;
    return sk_str_eq_c(url, "a.png");
}

static bool no_keyframes(void *data, sk_str name, uint32_t *index)
{
    (void)data;
    *index = 7;
    return sk_str_eq_c(name, "spin");
}

const char *sk_test_value(const char *prop, const char *value)
{
    static sk_buf out;
    sk_buf css = {0};
    sk_buf_printf(&css, "x { %s: %s }", prop, value);
    sk_css_sheet sheet;
    sk_diags d = {0};
    sk_css_rule *r = sk_test_rule(&sheet, sk_buf_cstr(&css), &d);
    sk_buf_free(&css);
    const char *result = NULL;
    if (r && r->ndecls == 1 && !d.items.len) {
        sk_css_style parent, s;
        sk_style_initial(&parent, (sk_css_color){0, 0, 0, 1});
        parent.font_size = 20;
        parent.font_weight = 400;
        sk_vctx c;
        memset(&c, 0, sizeof c);
        c.arena = &sheet.arena;
        c.font_size = 10;
        c.parent_font_size = 20;
        c.color = (sk_css_color){1, 0, 0, 1};
        c.parent_color = (sk_css_color){0, 0, 1, 1};
        c.parent = &parent;
        c.image = no_image;
        c.keyframes = no_keyframes;
        sk_style_initial(&s, c.color);
        const sk_css_decl *decl = &r->decls[0];
        const sk_propdef *p = sk_prop_find(decl->name);
        const sk_shorthand *sh = p ? NULL : sk_shorthand_find(decl->name);
        bool ok = p ? sk_prop_parse(p->id, &c, decl->value, decl->n, &s)
                    : sh && sk_shorthand_parse(sh, &c, decl->value, decl->n, &s);
        if (ok) {
            sk_buf_clear(&out);
            if (p) {
                sk_prop_print(&out, p->id, &s);
            } else {
                for (size_t i = 0; i < sh->n; i++) {
                    if (i)
                        sk_buf_append_c(&out, " | ");
                    sk_prop_print(&out, sh->longhands[i], &s);
                }
            }
            result = sk_buf_cstr(&out);
        }
    }
    sk_css_sheet_free(&sheet);
    sk_diags_free(&d);
    return result;
}

bool sk_test_said(const sk_diags *d, const char *needle)
{
    for (size_t i = 0; i < d->items.len; i++)
        if (strstr(d->items.ptr[i], needle))
            return true;
    return false;
}
