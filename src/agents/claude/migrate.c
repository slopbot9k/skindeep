/* The settings files the JavaScript app wrote, carried over the cutover
 * (decision 0036). Their hooks ran app/relay/hook.py with python3 and
 * their statusline app/relay/statusline.js with node, each command as
 * settings.js quoted it:
 *
 *   '<python3>' -I -S '<repo>/app/relay/hook.py' '<root>'
 *   [ELECTRON_RUN_AS_NODE=1 ]'<node>' '<repo>/app/relay/statusline.js' '<root>' <chain>
 *
 * A background session keeps its --settings file for its life, so once
 * that relay is gone each such command is rewritten to skinned-relay in
 * the same file, the rest of the file as it was. While the JavaScript
 * relay is there, the file is left to the JavaScript app. */
#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* What a settings file may hold: one the app wrote is under 4 KiB. */
#define FILE_CAP (1 << 20)
#define MAX_WORDS 6

/* s, NUL-terminated, in a. */
static sk_str in_arena(sk_arena *a, sk_str s)
{
    return sk_str_n(sk_arena_strndup(a, s.ptr ? s.ptr : "", s.len), s.len);
}

/* The words of a command as settings.js's quote() and a space join them:
 * a word is '…' with '\'' for a quote inside, or bare. False for
 * anything else, or more than max words. */
static bool split(sk_arena *a, sk_str cmd, sk_str *words, size_t max, size_t *n)
{
    *n = 0;
    size_t i = 0;
    while (i < cmd.len) {
        if (*n == max)
            return false;
        sk_buf w = {0};
        sk_buf_reserve(&w, 0);
        if (cmd.ptr[i] == '\'') {
            for (;;) {
                const char *q = memchr(cmd.ptr + i + 1, '\'', cmd.len - i - 1);
                if (!q) {
                    sk_buf_free(&w);
                    return false;
                }
                sk_buf_append(&w, cmd.ptr + i + 1, (size_t)(q - cmd.ptr) - i - 1);
                i = (size_t)(q - cmd.ptr) + 1;
                if (cmd.len - i >= 4 && !memcmp(cmd.ptr + i, "\\''", 3)) {
                    sk_buf_putc(&w, '\'');
                    i += 2;
                    continue;
                }
                break;
            }
        } else {
            size_t j = i;
            while (j < cmd.len && cmd.ptr[j] != ' ' && cmd.ptr[j] != '\'')
                j++;
            if (j == i) {
                sk_buf_free(&w);
                return false;
            }
            sk_buf_append(&w, cmd.ptr + i, j - i);
            i = j;
        }
        words[(*n)++] = in_arena(a, sk_buf_str(&w));
        sk_buf_free(&w);
        if (i < cmd.len) {
            if (cmd.ptr[i] != ' ' || i + 1 == cmd.len)
                return false;
            i++;
        }
    }
    return *n > 0;
}

/* The JavaScript relay a command ran, when it is gone. */
static bool gone(sk_str script, const char *leaf)
{
    size_t n = strlen(leaf);
    if (script.len <= n || memcmp(script.ptr + script.len - n, leaf, n) || script.ptr[script.len - n - 1] != '/')
        return false;
    return access(script.ptr, F_OK) != 0 && (errno == ENOENT || errno == ENOTDIR);
}

/* A statusline chain as settings.js encoded it: base64url, or - for
 * none. */
static bool chain_word(sk_str s)
{
    if (!s.len)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

/* The command rewritten to relay, in a, or false to keep it. */
static bool rewrite(sk_arena *a, sk_str cmd, const char *relay, sk_str *out)
{
    sk_str w[MAX_WORDS];
    size_t n;
    if (!split(a, cmd, w, MAX_WORDS, &n))
        return false;
    sk_buf b = {0};
    if (n == 5 && sk_str_eq_c(w[1], "-I") && sk_str_eq_c(w[2], "-S") && gone(w[3], "hook.py")) {
        sk_claude_quote(&b, relay);
        sk_buf_append_c(&b, " hook ");
        sk_claude_quote(&b, w[4].ptr);
    } else {
        size_t k = n && sk_str_eq_c(w[0], "ELECTRON_RUN_AS_NODE=1");
        if (n - k != 4 || !gone(w[k + 1], "statusline.js") || !chain_word(w[k + 3]))
            return false;
        sk_claude_quote(&b, relay);
        sk_buf_append_c(&b, " statusline ");
        sk_claude_quote(&b, w[k + 2].ptr);
        sk_buf_putc(&b, ' ');
        sk_buf_append_str(&b, w[k + 3]);
    }
    *out = in_arena(a, sk_buf_str(&b));
    sk_buf_free(&b);
    return true;
}

/* v's "command", rewritten when it ran a JavaScript relay. */
static bool rewrite_in(sk_arena *a, sk_json *v, const char *relay)
{
    if (!v || v->type != SK_JSON_OBJECT)
        return false;
    for (size_t i = 0; i < v->object.len; i++) {
        sk_json_member *m = &v->object.members[i];
        if (sk_str_eq_c(m->key, "command") && m->value.type == SK_JSON_STRING)
            return rewrite(a, m->value.string, relay, &m->value.string);
    }
    return false;
}

static sk_json *member(sk_json *v, const char *key)
{
    return (sk_json *)sk_json_get(v, key);
}

/* Every hook's and the statusline's command in the tree. */
static bool rewrite_tree(sk_arena *a, sk_json *root, const char *relay)
{
    bool changed = rewrite_in(a, member(root, "statusLine"), relay);
    sk_json *hooks = member(root, "hooks");
    if (!hooks || hooks->type != SK_JSON_OBJECT)
        return changed;
    for (size_t i = 0; i < hooks->object.len; i++) {
        sk_json *list = &hooks->object.members[i].value;
        for (size_t j = 0; list->type == SK_JSON_ARRAY && j < list->array.len; j++) {
            sk_json *inner = member(&list->array.items[j], "hooks");
            for (size_t k = 0; inner && inner->type == SK_JSON_ARRAY && k < inner->array.len; k++)
                changed |= rewrite_in(a, &inner->array.items[k], relay);
        }
    }
    return changed;
}

static bool read_capped(const char *path, sk_buf *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    char chunk[16384];
    bool ok = true;
    for (;;) {
        ssize_t r = read(fd, chunk, sizeof chunk);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 || out->len + (size_t)SK_MAX(r, 0) > FILE_CAP)
            ok = false;
        if (r <= 0 || !ok)
            break;
        sk_buf_append(out, chunk, (size_t)r);
    }
    close(fd);
    return ok;
}

/* text in place of path's, by write and rename, private as the app
 * writes it. */
static bool replace(const char *path, const sk_buf *text)
{
    sk_buf tmp = {0};
    sk_buf_printf(&tmp, "%s.%ld.tmp", path, (long)getpid());
    int fd = open(tmp.ptr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    bool ok = fd >= 0;
    for (size_t done = 0; ok && done < text->len;) {
        ssize_t w = write(fd, text->ptr + done, text->len - done);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            ok = false;
        else
            done += (size_t)w;
    }
    if (fd >= 0 && close(fd) != 0)
        ok = false;
    if (ok && rename(tmp.ptr, path) != 0)
        ok = false;
    if (!ok && fd >= 0)
        unlink(tmp.ptr);
    sk_buf_free(&tmp);
    return ok;
}

int sk_claude_migrate_settings(const char *store, const char *relay)
{
    if (!store || !relay || access(relay, X_OK) != 0)
        return 0;
    DIR *d = opendir(store);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len < 6 || e->d_name[0] == '.' || strcmp(e->d_name + len - 5, ".json"))
            continue;
        sk_buf path = {0}, text = {0};
        sk_buf_printf(&path, "%s/%s", store, e->d_name);
        sk_arena a;
        sk_arena_init(&a, 0);
        sk_json *root = read_capped(path.ptr, &text) ? sk_json_parse(&a, text.ptr ? text.ptr : "", text.len, NULL)
                                                      : NULL;
        if (root && root->type == SK_JSON_OBJECT && rewrite_tree(&a, root, relay)) {
            /* As settings.js wrote it: JSON.stringify(settings, null, 1). */
            sk_buf out = {0};
            sk_jw w;
            sk_jw_init(&w, &out, 1);
            sk_jw_value(&w, root);
            if (replace(path.ptr, &out))
                n++;
            sk_buf_free(&out);
        }
        sk_arena_free(&a);
        sk_buf_free(&path);
        sk_buf_free(&text);
    }
    closedir(d);
    return n;
}
