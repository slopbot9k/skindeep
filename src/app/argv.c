/* Port of app/argv.js (decision 0025). The JavaScript walks UTF-16 code
 * units and every character it looks for is ASCII, so walking bytes
 * splits UTF-8 the same way. */
#include "argv.h"

#include <string.h>

static void end_word(sk_strv *words, sk_buf *word)
{
    sk_strv_push_n(words, sk_buf_cstr(word), word->len);
    sk_buf_clear(word);
}

const char *sk_argv_split(const char *line, sk_strv *argv)
{
    sk_strv words = {0};
    sk_buf word = {0};
    bool in_word = false;
    const char *error = NULL;
    size_t n = strlen(line), i = 0;
    while (i < n && !error) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            if (in_word)
                end_word(&words, &word);
            in_word = false;
            i++;
        } else if (c == '\'') {
            const char *end = memchr(line + i + 1, '\'', n - i - 1);
            if (!end) {
                error = "unclosed single quote";
                break;
            }
            sk_buf_append(&word, line + i + 1, (size_t)(end - line) - i - 1);
            in_word = true;
            i = (size_t)(end - line) + 1;
        } else if (c == '"') {
            in_word = true;
            for (i++;; i++) {
                if (i >= n) {
                    error = "unclosed double quote";
                    break;
                }
                if (line[i] == '"')
                    break;
                if (line[i] == '\\' && i + 1 < n && strchr("\"\\$`", line[i + 1]))
                    i++;
                sk_buf_putc(&word, line[i]);
            }
            i++;
        } else if (c == '\\') {
            if (i + 1 >= n) {
                error = "a backslash ends the line";
                break;
            }
            sk_buf_putc(&word, line[i + 1]);
            in_word = true;
            i += 2;
        } else {
            sk_buf_putc(&word, c);
            in_word = true;
            i++;
        }
    }
    if (!error && in_word)
        end_word(&words, &word);
    if (!error && !words.len)
        error = "the command line is empty";
    if (!error)
        sk_strv_extend(argv, words.ptr);
    sk_strv_free(&words);
    sk_buf_free(&word);
    return error;
}
