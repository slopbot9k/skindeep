/* CSS through skinc's front end: any bytes as a skin's stylesheet go
 * through the tokenizer, the parser and the cascade over a skin of one
 * widget of nearly every type. What must hold: the tokenizer ends,
 * each token on a line no earlier than the last; a selector prints as
 * text that parses back to the same selector; and a skin that compiles
 * has, for every element, a state table of the size its inputs give,
 * each entry a style that exists. */
#include "../../skinc/front.h"
#include "fuzz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *const sk_fuzz_dict[] = {
    "{",          "}",        ";",          ":",         ",",          ".w",          ".on",       ".nodata",
    ".skin",      ".lamp",    ".seg",       ".item",     ".n-3",       ".span-5h",    ":hover",    "::before",
    "::after",    ":has(",    ":not(",      ":is(",      ":nth-child(", ":last-child", ":host",    " > ",
    " + ",        " ~ ",      "var(--",     "--x:",      "calc(",      "url(",        "!important", "@keyframes",
    "linear-gradient(", "radial-gradient(", "rgba(", "px", "%", "em", "inherit", "content:", "counter(",
    "counter-reset:", "counter-increment:", "animation:", "transition:", "background:", "font:", "border:",
    "display:", "transform:", "filter:", NULL,
};

static char dir[1024];
static FILE *devnull;

static void write_file(const char *name, const char *text)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        abort();
    fputs(text, f);
    fclose(f);
}

static void cleanup(void)
{
    const char *names[] = {"skin.json", "skin.css", "menu.svg"};
    for (size_t i = 0; i < 3; i++) {
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);
}

static void setup(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/sk-fuzz-css-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(dir))
        abort();
    atexit(cleanup);
    write_file("menu.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\"/>\n");
    write_file("skin.css", "");
    write_file("skin.json",
               "{\"name\": \"fuzz\", \"window\": {\"width\": 1000, \"height\": 700, \"minWidth\": 800, "
               "\"minHeight\": 500},\n"
               " \"menu\": {\"rect\": {\"right\": 8, \"top\": 8, \"width\": 24, \"height\": 24}, \"image\": "
               "\"menu.svg\"},\n"
               " \"terminal\": {\"rect\": {\"left\": 10, \"top\": 300, \"right\": 10, \"bottom\": 10},\n"
               "   \"theme\": {\"foreground\": \"#e0e0e0\", \"background\": \"#000000\"}},\n"
               " \"stylesheet\": \"skin.css\",\n"
               " \"panels\": [{\"class\": \"a b\", \"rect\": {\"left\": 0, \"top\": 0, \"right\": 0, \"height\": "
               "40}}],\n"
               " \"widgets\": [\n"
               "  {\"id\": \"l\", \"type\": \"led\", \"bind\": \"state.busy\", \"on\": 1, \"label\": \"x\", \"class\": "
               "\"k\", \"rect\": {\"left\": 0, \"top\": 50, \"width\": 50, \"height\": 20}},\n"
               "  {\"id\": \"b\", \"type\": \"bar\", \"bind\": \"cache.hit_ratio\", \"segments\": 3, \"label\": \"x\", "
               "\"resets\": \"limits.five_hour_resets_at\", \"rect\": {\"left\": 60, \"top\": 50, \"width\": 50, "
               "\"height\": 20}},\n"
               "  {\"id\": \"c\", \"type\": \"bar\", \"bind\": \"cost.usd\", \"fallback\": {\"bind\": \"tools.total\", "
               "\"label\": \"y\"}, \"rect\": {\"left\": 120, \"top\": 50, \"width\": 50, \"height\": 20}},\n"
               "  {\"id\": \"r\", \"type\": \"readout\", \"bind\": \"tools.total\", \"label\": \"x\", \"class\": "
               "\"row\", \"rect\": {\"left\": 180, \"top\": 50, \"width\": 50, \"height\": 20}},\n"
               "  {\"id\": \"g\", \"type\": \"gauge\", \"bind\": \"context.used_pct\", \"ticks\": 2, \"marks\": "
               "[{\"value\": 5}], \"rect\": {\"left\": 240, \"top\": 50, \"width\": 50, \"height\": 50}},\n"
               "  {\"id\": \"n\", \"type\": \"needle\", \"bind\": \"proc.cpu_pct\", \"ticks\": 2, \"rect\": {\"left\": "
               "300, \"top\": 50, \"width\": 50, \"height\": 50}},\n"
               "  {\"id\": \"kn\", \"type\": \"knob\", \"bind\": \"proc.cpu_pct\", \"rect\": {\"left\": 360, \"top\": "
               "50, \"width\": 50, \"height\": 50}},\n"
               "  {\"id\": \"t\", \"type\": \"list\", \"bind\": \"checklist.items\", \"class\": \"tl\", \"rect\": "
               "{\"left\": 420, \"top\": 50, \"width\": 50, \"height\": 100},\n"
               "   \"toggle\": {\"rect\": {\"left\": 480, \"top\": 50, \"width\": 50, \"height\": 20}}},\n"
               "  {\"id\": \"m\", \"type\": \"limits\", \"bind\": \"limits.items\", \"segments\": 2, \"itemClass\": "
               "\"j\", \"class\": \"lims\", \"rect\": {\"left\": 540, \"top\": 50, \"width\": 100, \"height\": 100}}\n"
               " ]}\n");
    devnull = fopen("/dev/null", "w");
}

/* The selector printed parses back to itself. */
static void round_trip(const sk_selector *s)
{
    sk_buf a = {0}, css = {0}, b = {0};
    sk_selector_print(&a, s);
    sk_buf_printf(&css, "%s {}", sk_buf_cstr(&a));
    sk_css_sheet sheet;
    sk_diags d = {0};
    sk_css_parse(&sheet, "rt.css", sk_buf_cstr(&css), css.len, &d);
    if (d.items.len || sheet.rules.len != 1 || sheet.rules.ptr[0].sel->n != 1) {
        fprintf(stderr, "selector \"%s\" did not parse back: %s\n", sk_buf_cstr(&a),
                d.items.len ? d.items.ptr[0] : "?");
        abort();
    }
    sk_selector_print(&b, &sheet.rules.ptr[0].sel->items[0]);
    if (strcmp(sk_buf_cstr(&a), sk_buf_cstr(&b)) != 0 || sheet.rules.ptr[0].sel->items[0].spec != s->spec) {
        fprintf(stderr, "selector \"%s\" came back as \"%s\"\n", sk_buf_cstr(&a), sk_buf_cstr(&b));
        abort();
    }
    sk_css_sheet_free(&sheet);
    sk_diags_free(&d);
    sk_buf_free(&a);
    sk_buf_free(&b);
    sk_buf_free(&css);
}

static void tokens(const char *s, size_t n)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    sk_lexer lx;
    sk_lexer_init(&lx, s, n, &a);
    uint32_t line = 1;
    size_t count = 0;
    for (;;) {
        sk_tok t = sk_lex(&lx);
        if (t.line < line)
            abort();
        line = t.line;
        if (t.type == SK_TOK_EOF)
            break;
        /* Each token takes a byte at least. */
        if (++count > n)
            abort();
    }
    sk_arena_free(&a);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!devnull)
        setup();
    const char *css = (const char *)data;
    tokens(css, size);
    sk_diags d = {0};
    sk_front f;
    bool ok = sk_front_run_text(&f, dir, css, size, 4096, &d);
    if (f.has_css)
        for (size_t i = 0; i < f.css.rules.len; i++)
            for (size_t k = 0; k < f.css.rules.ptr[i].sel->n; k++)
                round_trip(&f.css.rules.ptr[i].sel->items[k]);
    if (ok) {
        if (f.states > 4096 + f.els.len)
            abort();
        for (size_t i = 0; i < f.els.len; i++) {
            const sk_el *e = f.els.ptr[i];
            size_t product = 1;
            for (uint8_t k = 0; k < e->ninputs; k++) {
                if (e->inputs[k].radix < 2 || e->inputs[k].owner->index >= f.els.len ||
                    f.els.ptr[e->inputs[k].owner->index] != e->inputs[k].owner)
                    abort();
                product *= e->inputs[k].radix;
            }
            if (e->ntable != product || e->ninputs > SK_PACK_MAX_INPUTS)
                abort();
            for (size_t t = 0; t < e->ntable; t++)
                if (e->table[t] >= f.styles.len)
                    abort();
        }
        sk_front_summary(&f, devnull, true);
    }
    sk_front_free(&f);
    sk_diags_free(&d);
    return 0;
}
