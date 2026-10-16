/* The JSONL tail, as test/tail.test.js has it, on temp files. */
#include "../../src/core/tail.h"
#include "../test.h"

#include <stdlib.h>
#include <sys/stat.h>

typedef struct {
    SK_VEC(long) values; /* each line's "i" (or other number), -1 when absent */
    SK_VEC(bool) history;
    sk_buf last_text; /* the "p" string of the last line that had one */
} got_t;

static void on_line(void *ud, const sk_json *obj, bool history)
{
    got_t *g = ud;
    double d = -1;
    const char *keys[] = {"i", "h", "a", "b", "c", "q", "live"};
    for (size_t k = 0; k < SK_LEN(keys); k++)
        if (sk_json_as_number(sk_json_get(obj, keys[k]), &d))
            break;
    sk_vec_push(&g->values, (long)d);
    sk_vec_push(&g->history, history);
    sk_str p;
    if (sk_json_as_string(sk_json_get(obj, "p"), &p)) {
        sk_buf_clear(&g->last_text);
        sk_buf_append_str(&g->last_text, p);
    }
}

static void got_free(got_t *g)
{
    sk_vec_free(&g->values);
    sk_vec_free(&g->history);
    sk_buf_free(&g->last_text);
}

static char *path_in_tmp(const char *name)
{
    sk_buf p = {0};
    sk_buf_printf(&p, "%s/%s", sk_test_tmpdir(), name);
    return p.ptr;
}

static void write_file(const char *path, const char *mode, const char *text, size_t n)
{
    FILE *f = fopen(path, mode);
    fwrite(text, 1, n, f);
    fclose(f);
}

static void append(const char *path, const char *text)
{
    write_file(path, "ab", text, strlen(text));
}

/* n lines of {"i":<k>,"pad":"x..."} */
static void write_lines(const char *path, int n)
{
    sk_buf b = {0};
    char pad[1001];
    memset(pad, 'x', 1000);
    pad[1000] = 0;
    for (int i = 0; i < n; i++)
        sk_buf_printf(&b, "{\"i\":%d,\"pad\":\"%s\"}\n", i, pad);
    write_file(path, "wb", b.ptr, b.len);
    sk_buf_free(&b);
}

static size_t live_count(const got_t *g)
{
    size_t n = 0;
    for (size_t i = 0; i < g->history.len; i++)
        n += !g->history.ptr[i];
    return n;
}

static bool in_order(const got_t *g, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (g->values.ptr[i] != (long)i)
            return false;
    return true;
}

static void test_t1_large_file_once_as_history(void)
{
    char *file = path_in_tmp("log.jsonl");
    write_lines(file, 9000);
    struct stat st;
    stat(file, &st);
    CHECK(st.st_size > 8 * 1024 * 1024);
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    sk_tail_poll(t, 1000);
    CHECK_INT(g.values.len, 9000);
    CHECK(in_order(&g, 9000));
    CHECK_INT(live_count(&g), 0);
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

static void test_t2_appended_lines_and_a_partial_line(void)
{
    char *file = path_in_tmp("log.jsonl");
    append(file, "{\"h\":1}\n");
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    CHECK_INT(g.values.len, 1);
    CHECK(g.history.ptr[0]);
    append(file, "{\"a\":1}\n{\"b\":");
    CHECK_INT(g.values.len, 1); /* nothing arrives unpolled */
    sk_tail_poll(t, 1000);
    CHECK_INT(g.values.len, 2);
    CHECK(g.values.ptr[1] == 1 && !g.history.ptr[1]);
    append(file, "2}\n");
    sk_tail_poll(t, 1200);
    CHECK_INT(g.values.len, 2); /* within 500 ms */
    sk_tail_poll(t, 1500);
    CHECK_INT(g.values.len, 3);
    CHECK(g.values.ptr[2] == 2 && !g.history.ptr[2]);
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

static void test_t3_nothing_after_stop(void)
{
    char *file = path_in_tmp("log.jsonl");
    append(file, "{\"h\":1}\n");
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    sk_tail_stop(t);
    append(file, "{\"a\":1}\n");
    sk_tail_poll(t, 1000);
    CHECK_INT(g.values.len, 1);
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

static void test_t4_character_split_at_the_chunk_arrives_whole(void)
{
    char *file = path_in_tmp("log.jsonl");
    const size_t chunk = SK_TAIL_CHUNK;
    sk_buf b = {0};
    sk_buf_append_c(&b, "{\"p\":\"");
    while (b.len < chunk - 1)
        sk_buf_putc(&b, 'x');
    sk_buf_append_c(&b, "\xC3\xA9\"}\n{\"q\":1}\n"); /* U+00E9 across the boundary */
    CHECK(b.ptr[chunk - 1] == '\xC3');
    write_file(file, "wb", b.ptr, b.len);
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    sk_tail_poll(t, 1000);
    CHECK_INT(g.values.len, 2);
    CHECK(sk_str_suffix(sk_buf_str(&g.last_text), SK_STR("x\xC3\xA9")));
    CHECK(!strstr(g.last_text.ptr, "\xEF\xBF\xBD"));
    CHECK_INT(g.values.ptr[1], 1);
    sk_tail_free(t);
    got_free(&g);
    sk_buf_free(&b);
    free(file);
}

static void test_t5_history_a_chunk_per_poll_then_appended(void)
{
    char *file = path_in_tmp("log.jsonl");
    write_lines(file, 18000);
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    size_t first = g.values.len;
    CHECK(first > 0 && first < 9000);
    CHECK(sk_tail_replaying(t));
    append(file, "{\"live\":1}\n");
    sk_tail_poll(t, 1000);
    CHECK(g.values.len > first && g.values.len < 18000);
    sk_tail_poll(t, 1001);
    CHECK_INT(g.values.len, 18000);
    CHECK(!sk_tail_replaying(t));
    CHECK(in_order(&g, 18000));
    CHECK_INT(live_count(&g), 0);
    sk_tail_poll(t, 1002);
    CHECK_INT(g.values.len, 18001);
    CHECK(g.values.ptr[18000] == 1 && !g.history.ptr[18000]);
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

static void test_t6_stopped_tail_reads_no_more_history(void)
{
    char *file = path_in_tmp("log.jsonl");
    write_lines(file, 9000);
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    size_t first = g.values.len;
    sk_tail_stop(t);
    CHECK(!sk_tail_replaying(t));
    sk_tail_poll(t, 1000);
    CHECK_INT(g.values.len, first);
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

static void test_t7_an_end_reads_to_that_byte_and_nothing_after(void)
{
    char *file = path_in_tmp("log.jsonl");
    append(file, "{\"a\":1}\n{\"b\":2}\n");
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, (int64_t)strlen("{\"a\":1}\n"));
    append(file, "{\"c\":3}\n");
    sk_tail_poll(t, 1000);
    sk_tail_poll(t, 2000);
    CHECK_INT(g.values.len, 1);
    CHECK(g.values.ptr[0] == 1 && g.history.ptr[0]);
    CHECK(!sk_tail_replaying(t));
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

static void test_skips_bad_lines_and_survives_truncation(void)
{
    char *file = path_in_tmp("log.jsonl");
    append(file, "not json\n\n{\"a\":1}\r\n");
    got_t g = {0};
    sk_tail *t = sk_tail_new(file, on_line, &g, -1);
    CHECK_INT(g.values.len, 1);
    write_file(file, "wb", "{\"b\":2}\n", 8); /* shorter: read again from the start */
    sk_tail_poll(t, 1000);
    CHECK_INT(g.values.len, 2);
    CHECK(g.values.ptr[1] == 2 && !g.history.ptr[1]);
    sk_tail_free(t);
    got_free(&g);
    free(file);
}

const sk_test sk_suite_tail[] = {
    T(test_t1_large_file_once_as_history),
    T(test_t2_appended_lines_and_a_partial_line),
    T(test_t3_nothing_after_stop),
    T(test_t4_character_split_at_the_chunk_arrives_whole),
    T(test_t5_history_a_chunk_per_poll_then_appended),
    T(test_t6_stopped_tail_reads_no_more_history),
    T(test_t7_an_end_reads_to_that_byte_and_nothing_after),
    T(test_skips_bad_lines_and_survives_truncation),
    {0},
};
