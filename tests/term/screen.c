#include "screen.h"

#include "../../src/base/utf8.h"
#include "../../src/term/screen.h"

#include <stdlib.h>
#include <sys/stat.h>

static void on_reply(void *ud, const char *data, size_t len)
{
    sk_buf_append(&((tt *)ud)->replies, data, len);
}

static void on_title(void *ud, const char *title)
{
    tt *x = ud;
    sk_buf_clear(&x->title);
    sk_buf_append_c(&x->title, title);
}

static void on_bell(void *ud)
{
    ((tt *)ud)->bells++;
}

void tt_open(tt *x, int cols, int rows)
{
    memset(x, 0, sizeof *x);
    sk_term_callbacks cb = {x, on_reply, on_title, on_bell};
    x->t = sk_term_new(cols, rows, &cb);
}

void tt_close(tt *x)
{
    sk_term_free(x->t);
    sk_buf_free(&x->replies);
    sk_buf_free(&x->title);
}

void tt_feed(tt *x, const char *s)
{
    sk_term_feed(x->t, s, strlen(s));
}

void tt_feedn(tt *x, const char *s, size_t n)
{
    sk_term_feed(x->t, s, n);
}

char *tt_take(tt *x)
{
    char *s = sk_xstrdup(sk_buf_cstr(&x->replies));
    sk_buf_clear(&x->replies);
    return s;
}

sk_term_cell tt_cell(sk_term *t, int x, int y)
{
    int cols, rows;
    sk_term_size(t, &cols, &rows);
    sk_term_cell *row = sk_xcalloc((size_t)cols, sizeof *row);
    sk_term_row(t, y, row);
    sk_term_cell c = row[x];
    free(row);
    return c;
}

char *tt_rows(sk_term *t)
{
    sk_buf out = {0};
    sk_buffer *b = t->active;
    for (int y = 0; y < t->rows; y++) {
        if (y)
            sk_buf_putc(&out, '\n');
        sk_line *l = sk_screen_line_at(b, b->ydisp + y);
        if (l)
            sk_line_text(l, &out, true, 0, l->len);
    }
    sk_buf_reserve(&out, 0);
    return out.ptr;
}

static void color(sk_buf *b, const char *name, uint32_t v)
{
    switch (v & SK_ATTR_CM_MASK) {
    case SK_ATTR_CM_P16:
        sk_buf_printf(b, " %s=16:%u", name, v & 0xFF);
        break;
    case SK_ATTR_CM_P256:
        sk_buf_printf(b, " %s=256:%u", name, v & 0xFF);
        break;
    case SK_ATTR_CM_RGB:
        sk_buf_printf(b, " %s=#%06x", name, v & 0xFFFFFF);
        break;
    }
}

static const char *const styles[] = {"none", "single", "double", "curly", "dotted", "dashed", "6", "7"};

static void describe(sk_buf *b, const sk_term_cell *c)
{
    color(b, "fg", c->fg);
    color(b, "bg", c->bg);
    if (c->fg & SK_FG_BOLD)
        sk_buf_append_c(b, " bold");
    if (c->bg & SK_BG_DIM)
        sk_buf_append_c(b, " dim");
    if (c->bg & SK_BG_ITALIC)
        sk_buf_append_c(b, " italic");
    if (c->fg & SK_FG_UNDERLINE) {
        int s = (c->bg & SK_BG_HAS_EXTENDED) ? (int)((c->ext & SK_EXT_UNDERLINE_STYLE) >> SK_EXT_UNDERLINE_SHIFT) : 1;
        sk_buf_printf(b, " ul=%s", styles[s & 7]);
    }
    if (c->fg & SK_FG_BLINK)
        sk_buf_append_c(b, " blink");
    if (c->fg & SK_FG_INVERSE)
        sk_buf_append_c(b, " inverse");
    if (c->fg & SK_FG_INVISIBLE)
        sk_buf_append_c(b, " invisible");
    if (c->fg & SK_FG_STRIKETHROUGH)
        sk_buf_append_c(b, " strike");
    if (c->bg & SK_BG_OVERLINE)
        sk_buf_append_c(b, " overline");
    if (c->bg & SK_BG_PROTECTED)
        sk_buf_append_c(b, " protected");
    if (c->bg & SK_BG_HAS_EXTENDED) {
        color(b, "ulc", c->ext & SK_EXT_UNDERLINE_COLOR);
        if (c->link)
            sk_buf_printf(b, " link=%u", c->link);
    }
}

static bool same(const sk_term_cell *a, const sk_term_cell *b)
{
    if (a->fg != b->fg || a->bg != b->bg)
        return false;
    if (a->bg & SK_BG_HAS_EXTENDED)
        return a->ext == b->ext && a->link == b->link;
    return true;
}

static void cell_text(sk_buf *b, const sk_term_cell *c)
{
    if (c->comb) {
        for (uint32_t k = 0; k < c->ncomb; k++)
            sk_utf8_append(b, c->comb[k]);
    } else if (c->cp) {
        sk_utf8_append(b, c->cp);
    } else if (c->width) {
        sk_buf_putc(b, ' ');
    }
}

char *tt_dump(sk_term *t)
{
    sk_term_lock(t);
    sk_buf out = {0}, attrs = {0};
    sk_buffer *b = t->active;
    sk_term_cursor cur;
    sk_term_cursor_of(t, &cur);
    sk_buf_printf(&out, "cols %d rows %d cursor %d,%d%s buffer %s top %d base %d lines %d\n", t->cols, t->rows, cur.x,
                  cur.y, cur.hidden ? " hidden" : "", t->active == &t->alt ? "alt" : "normal", b->ydisp, b->ybase,
                  b->lines.len);
    sk_term_cell *row = sk_xcalloc((size_t)t->cols, sizeof *row);
    SK_VEC(uint32_t) links = {0};
    for (int y = 0; y < t->rows; y++) {
        sk_line *l = sk_screen_line_at(b, b->ydisp + y);
        sk_buf_putc(&out, l && l->wrapped ? '>' : '|');
        sk_term_row(t, y, row);
        for (int x = 0; x < t->cols; x++)
            cell_text(&out, &row[x]);
        sk_buf_append_c(&out, "|\n");
        for (int x = 0; x < t->cols;) {
            int end = x + 1;
            while (end < t->cols && same(&row[x], &row[end]))
                end++;
            if (row[x].fg || row[x].bg) {
                sk_buf_printf(&attrs, "attr %d %d-%d:", y, x, end - 1);
                describe(&attrs, &row[x]);
                sk_buf_putc(&attrs, '\n');
                if ((row[x].bg & SK_BG_HAS_EXTENDED) && row[x].link) {
                    bool seen = false;
                    for (size_t k = 0; k < links.len; k++)
                        seen |= links.ptr[k] == row[x].link;
                    if (!seen)
                        sk_vec_push(&links, row[x].link);
                }
            }
            x = end;
        }
    }
    free(row);
    sk_buf_append(&out, attrs.ptr, attrs.len);
    for (size_t k = 0; k < links.len; k++) {
        const char *uri = sk_term_link_uri(t, links.ptr[k]);
        const char *id = sk_term_link_id(t, links.ptr[k]);
        sk_buf_printf(&out, "link %u%s%s: %s\n", links.ptr[k], id ? " id=" : "", id ? id : "", uri ? uri : "(gone)");
    }
    if (b->ydisp > 0) {
        sk_buf_append_c(&out, "scrollback:\n");
        for (int y = 0; y < b->ydisp; y++) {
            sk_line *l = sk_screen_line(b, y);
            sk_buf_putc(&out, l->wrapped ? '>' : '|');
            sk_line_text(l, &out, true, 0, l->len);
            sk_buf_putc(&out, '\n');
        }
    }
    sk_vec_free(&links);
    sk_buf_free(&attrs);
    sk_term_unlock(t);
    return out.ptr;
}

void tt_golden(const char *file, int line, const char *name, sk_term *t)
{
    char *got = tt_dump(t);
    sk_buf path = {0};
    sk_buf_printf(&path, "tests/golden/term/%s.txt", name);
    const char *update = getenv("SK_GOLDEN_UPDATE");
    if (update && *update == '1') {
        FILE *f = fopen(path.ptr, "wb");
        if (f) {
            fputs(got, f);
            fclose(f);
            printf("    wrote %s\n", path.ptr);
        } else {
            sk_test_fail(file, line, "cannot write %s", path.ptr);
        }
        free(got);
        sk_buf_free(&path);
        return;
    }
    char *want = sk_test_read(path.ptr, NULL);
    if (!want || strcmp(want, got) != 0) {
        mkdir("build", 0755);
        mkdir("build/golden-out", 0755);
        mkdir("build/golden-out/term", 0755);
        sk_buf out = {0};
        sk_buf_printf(&out, "build/golden-out/term/%s.txt", name);
        FILE *f = fopen(out.ptr, "wb");
        if (f) {
            fputs(got, f);
            fclose(f);
        }
        if (!want) {
            sk_test_fail(file, line, "no golden screen %s (SK_GOLDEN_UPDATE=1 writes it); made %s", path.ptr, out.ptr);
        } else {
            size_t i = 0, ln = 1;
            while (want[i] && want[i] == got[i]) {
                if (want[i] == '\n')
                    ln++;
                i++;
            }
            sk_test_fail(file, line, "%s differs from line %zu; made %s", path.ptr, ln, out.ptr);
        }
        sk_buf_free(&out);
    }
    free(want);
    free(got);
    sk_buf_free(&path);
}
