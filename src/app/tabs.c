/* Port of app/tabs.js (decisions 0003, 0023), with what app/main.js kept
 * of the tabs beside it: the closed tabs and the commands that reopen
 * and duplicate them (decision 0026), the follow of decision 0030, the
 * names of decision 0032, the strip's folders (decision 0004), New tab,
 * and pop out.
 *
 * The JavaScript learned of a session's end on a later tick. Here a
 * session may report it at once, even from inside hangup, so the tabs
 * walk ids, not the list, while they hang up, and free a gone tab only
 * when the outermost call returns. */
#include "tabs.h"

#include "../agents/agent.h"
#include "background.h"
#include "identity.h"
#include "js.h"

#include <stdarg.h>
#include <string.h>

/* EXIT_APP of decision 0003, the status of an app error. */
#define STATUS_APP_ERROR 125
#define NAME_MAX_UNITS 120

typedef struct {
    int win;
    sk_tab *tab;
} active_entry;

/* A closed tab, made once its CLI was reaped, so no reopened session
 * ever has two writers. */
typedef struct {
    sk_strv cmd;
    char *cwd;
    sk_envp env;
    char *file;
    char *session; /* NULL: none to resume */
} closed_tab;

struct sk_tabs {
    sk_tabs_config cfg;
    sk_tab_list list;
    SK_VEC(active_entry) actives;
    SK_VEC(closed_tab) closed;
    int current, next_id, next_win;
    int status, last_cli, exit_status;
    bool ending, done;
    sk_tab *last;
    sk_tab_list graveyard; /* gone, freed when the outermost call returns */
    unsigned depth;
};

static void logf_(sk_tabs *t, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(sk_tabs *t, const char *fmt, ...)
{
    if (!t->cfg.log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    t->cfg.log(t->cfg.ud, b.ptr);
    sk_buf_free(&b);
}

static void emit_change(sk_tabs *t)
{
    if (t->cfg.change)
        t->cfg.change(t->cfg.ud);
}

/* A string a session returned, "" counting as none, as the JavaScript's
 * truthiness had it. */
static const char *some(const char *s)
{
    return s && *s ? s : NULL;
}

static bool same(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

static const char *session_id(const sk_tab *tab)
{
    const sk_tab_session *s = &tab->session;
    return s->ops && s->ops->session_id ? some(s->ops->session_id(s->self)) : NULL;
}

static const char *record_title(const sk_tab *tab)
{
    const sk_tab_session *s = &tab->session;
    return s->ops && s->ops->title ? some(s->ops->title(s->self)) : NULL;
}

static const char *last_screen(const sk_tab *tab)
{
    const sk_tab_session *s = &tab->session;
    return s->ops && s->ops->last_screen ? some(s->ops->last_screen(s->self)) : NULL;
}

static void free_tab(sk_tab *tab)
{
    if (tab->session.ops && tab->session.ops->free)
        tab->session.ops->free(tab->session.self);
    sk_strv_free(&tab->cmd);
    sk_envp_free(&tab->env);
    free(tab->cwd);
    free(tab->file);
    free(tab->label);
    free(tab->named);
    free(tab->named_over);
    free(tab);
}

static void enter(sk_tabs *t)
{
    t->depth++;
}

static void leave(sk_tabs *t)
{
    if (--t->depth)
        return;
    for (size_t i = 0; i < t->graveyard.len; i++)
        free_tab(t->graveyard.ptr[i]);
    t->graveyard.len = 0;
}

sk_tabs *sk_tabs_new(const sk_tabs_config *cfg)
{
    sk_tabs *t = sk_xcalloc(1, sizeof *t);
    t->cfg = *cfg;
    t->next_id = 1;
    t->next_win = 1;
    t->status = -1;
    return t;
}

static void free_closed(closed_tab *e)
{
    sk_strv_free(&e->cmd);
    sk_envp_free(&e->env);
    free(e->cwd);
    free(e->file);
    free(e->session);
}

void sk_tabs_free(sk_tabs *t)
{
    if (!t)
        return;
    for (size_t i = 0; i < t->list.len; i++)
        free_tab(t->list.ptr[i]);
    for (size_t i = 0; i < t->graveyard.len; i++)
        free_tab(t->graveyard.ptr[i]);
    if (t->last)
        free_tab(t->last);
    for (size_t i = 0; i < t->closed.len; i++)
        free_closed(&t->closed.ptr[i]);
    sk_vec_free(&t->list);
    sk_vec_free(&t->graveyard);
    sk_vec_free(&t->actives);
    sk_vec_free(&t->closed);
    free(t);
}

sk_tab *sk_tabs_get(const sk_tabs *t, int id)
{
    for (size_t i = 0; i < t->list.len; i++)
        if (t->list.ptr[i]->id == id)
            return t->list.ptr[i];
    return NULL;
}

size_t sk_tabs_count(const sk_tabs *t)
{
    return t->list.len;
}

sk_tab *sk_tabs_at(const sk_tabs *t, size_t i)
{
    return i < t->list.len ? t->list.ptr[i] : NULL;
}

static size_t index_of(const sk_tab_list *l, const sk_tab *tab)
{
    for (size_t i = 0; i < l->len; i++)
        if (l->ptr[i] == tab)
            return i;
    return SIZE_MAX;
}

void sk_tabs_in_win(const sk_tabs *t, int win, sk_tab_list *out)
{
    out->len = 0;
    for (size_t i = 0; i < t->list.len; i++)
        if (t->list.ptr[i]->win == win)
            sk_vec_push(out, t->list.ptr[i]);
}

static size_t count_in_win(const sk_tabs *t, int win)
{
    size_t n = 0;
    for (size_t i = 0; i < t->list.len; i++)
        n += t->list.ptr[i]->win == win;
    return n;
}

size_t sk_tabs_wins(const sk_tabs *t, int *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < t->list.len; i++) {
        int w = t->list.ptr[i]->win;
        bool seen = false;
        for (size_t k = 0; k < i && !seen; k++)
            seen = t->list.ptr[k]->win == w;
        if (seen)
            continue;
        if (n < cap)
            out[n] = w;
        n++;
    }
    return n;
}

sk_tab *sk_tabs_active_of(const sk_tabs *t, int win)
{
    for (size_t i = 0; i < t->actives.len; i++)
        if (t->actives.ptr[i].win == win)
            return t->actives.ptr[i].tab;
    return NULL;
}

sk_tab *sk_tabs_active(const sk_tabs *t)
{
    return sk_tabs_active_of(t, t->current);
}

static void set_active(sk_tabs *t, int win, sk_tab *tab)
{
    for (size_t i = 0; i < t->actives.len; i++) {
        if (t->actives.ptr[i].win == win) {
            t->actives.ptr[i].tab = tab;
            return;
        }
    }
    sk_vec_push(&t->actives, ((active_entry){win, tab}));
}

static void drop_active(sk_tabs *t, int win)
{
    for (size_t i = 0; i < t->actives.len; i++) {
        if (t->actives.ptr[i].win == win) {
            t->actives.ptr[i] = t->actives.ptr[--t->actives.len];
            return;
        }
    }
}

int sk_tabs_current(const sk_tabs *t)
{
    return t->current;
}

void sk_tabs_set_current(sk_tabs *t, int win)
{
    t->current = win;
}

void sk_tabs_touch(sk_tabs *t, int win)
{
    if (count_in_win(t, win))
        t->current = win;
}

static void select_tab(sk_tabs *t, sk_tab *tab)
{
    if (!tab || tab == sk_tabs_active_of(t, tab->win) || index_of(&t->list, tab) == SIZE_MAX)
        return;
    set_active(t, tab->win, tab);
    tab->unseen = false;
    if (t->cfg.active)
        t->cfg.active(t->cfg.ud, tab);
    emit_change(t);
}

void sk_tabs_activate(sk_tabs *t, int id)
{
    select_tab(t, sk_tabs_get(t, id));
}

/* A window that lost its last tab while others have tabs: current moves
 * to the first window left, as app/main.js moved it. */
static void emptied(sk_tabs *t, int win)
{
    if (t->current == win && t->list.len)
        t->current = t->list.ptr[0]->win;
    if (t->cfg.empty)
        t->cfg.empty(t->cfg.ud, win);
}

/* A tab for spec in win, its session made, or NULL with *err set. */
static sk_tab *make_tab(sk_tabs *t, const sk_tab_spec *spec, int win, char **err)
{
    sk_tab *tab = sk_xcalloc(1, sizeof *tab);
    tab->id = t->next_id++;
    tab->win = win;
    tab->kind = spec->kind;
    sk_strv_extend(&tab->cmd, spec->cmd);
    sk_strv_argv(&tab->cmd);
    tab->cwd = sk_xstrdup(spec->cwd ? spec->cwd : "");
    const sk_envp *env = spec->env ? spec->env : spec->kind == SK_TAB_CLI ? t->cfg.env : NULL;
    sk_envp_from(&tab->env, env ? env->vars.ptr : NULL);
    const char *file = spec->file ? spec->file : spec->kind == SK_TAB_CLI ? t->cfg.file : NULL;
    tab->file = file ? sk_xstrdup(file) : NULL;
    if (spec->label) {
        tab->label = sk_xstrdup(spec->label);
    } else {
        sk_buf label = {0};
        sk_identity_label(&label, tab->cmd.ptr, &tab->env, sk_agent_provider(tab->cmd.ptr)->kind,
                          t->cfg.home);
        tab->label = sk_xstrdup(sk_buf_cstr(&label));
        sk_buf_free(&label);
    }
    char *why = NULL;
    if (t->cfg.make(t->cfg.ud, tab, spec, &why) != 0 || !tab->session.ops) {
        if (tab->session.ops)
            tab->session.ops->free(tab->session.self);
        tab->session.ops = NULL;
        free_tab(tab);
        if (!why)
            why = sk_xstrdup("the tab could not be made");
        if (err)
            *err = why;
        else
            free(why);
        return NULL;
    }
    return tab;
}

sk_tab *sk_tabs_add(sk_tabs *t, const sk_tab_spec *spec, int win, char **err)
{
    if (err)
        *err = NULL;
    if (t->ending)
        return NULL;
    sk_tab *tab = make_tab(t, spec, win < 0 ? t->current : win, err);
    if (!tab)
        return NULL;
    sk_vec_push(&t->list, tab);
    select_tab(t, tab);
    return tab;
}

void sk_tabs_step(sk_tabs *t, int d, int win)
{
    if (win < 0)
        win = t->current;
    sk_tab_list mine = {0};
    sk_tabs_in_win(t, win, &mine);
    long n = (long)mine.len;
    if (n >= 2) {
        size_t at = index_of(&mine, sk_tabs_active_of(t, win));
        long i = at == SIZE_MAX ? -1 : (long)at;
        select_tab(t, mine.ptr[(i + (d < 0 ? -1 : 1) + n) % n]);
    }
    sk_vec_free(&mine);
}

void sk_tabs_move(sk_tabs *t, int id, long index)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (!tab)
        return;
    sk_tab_list mine = {0};
    sk_tabs_in_win(t, tab->win, &mine);
    size_t to = index < 0 ? 0 : SK_MIN((size_t)index, mine.len - 1);
    size_t from = index_of(&mine, tab);
    if (from != to) {
        if (from < to)
            memmove(mine.ptr + from, mine.ptr + from + 1, (to - from) * sizeof *mine.ptr);
        else
            memmove(mine.ptr + to + 1, mine.ptr + to, (from - to) * sizeof *mine.ptr);
        mine.ptr[to] = tab;
        for (size_t i = 0, k = 0; i < t->list.len; i++)
            if (t->list.ptr[i]->win == tab->win)
                t->list.ptr[i] = mine.ptr[k++];
        emit_change(t);
    }
    sk_vec_free(&mine);
}

void sk_tabs_detach(sk_tabs *t, int id, int win)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (!tab || tab->win == win)
        return;
    int from = tab->win;
    sk_tab_list mine = {0};
    sk_tabs_in_win(t, from, &mine);
    size_t i = index_of(&mine, tab);
    size_t at = index_of(&t->list, tab);
    memmove(t->list.ptr + at, t->list.ptr + at + 1, (t->list.len - at - 1) * sizeof *t->list.ptr);
    t->list.ptr[t->list.len - 1] = tab;
    tab->win = win;
    if (sk_tabs_active_of(t, from) == tab) {
        drop_active(t, from);
        sk_tabs_in_win(t, from, &mine);
        if (mine.len)
            select_tab(t, mine.ptr[SK_MIN(i, mine.len - 1)]);
    }
    select_tab(t, tab);
    if (!count_in_win(t, from))
        emptied(t, from);
    emit_change(t);
    sk_vec_free(&mine);
}

void sk_tabs_close(sk_tabs *t, int id)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (!tab)
        return;
    enter(t);
    tab->closed = true;
    tab->session.ops->hangup(tab->session.self);
    leave(t);
}

/* The ids of the tabs, of win alone when win >= 0, so a session that
 * ends while it is hung up leaves the walk whole. */
typedef SK_VEC(int) id_list;

static void ids_of(const sk_tabs *t, int win, id_list *ids)
{
    for (size_t i = 0; i < t->list.len; i++)
        if (win < 0 || t->list.ptr[i]->win == win)
            sk_vec_push(ids, t->list.ptr[i]->id);
}

void sk_tabs_close_win(sk_tabs *t, int win)
{
    id_list ids = {0};
    ids_of(t, win, &ids);
    enter(t);
    for (size_t i = 0; i < ids.len; i++)
        sk_tabs_close(t, ids.ptr[i]);
    leave(t);
    sk_vec_free(&ids);
}

void sk_tabs_hangup_all(sk_tabs *t)
{
    t->ending = true;
    id_list ids = {0};
    ids_of(t, -1, &ids);
    enter(t);
    for (size_t i = 0; i < ids.len; i++)
        sk_tabs_close(t, ids.ptr[i]);
    leave(t);
    sk_vec_free(&ids);
}

void sk_tabs_fail(sk_tabs *t, const char *err)
{
    sk_buf msg = {0};
    sk_buf_printf(&msg, "internal error: %s", err ? err : "");
    if (t->done || t->status >= 0) {
        if (t->cfg.log)
            t->cfg.log(t->cfg.ud, msg.ptr);
    } else {
        t->status = STATUS_APP_ERROR;
        if (t->cfg.tell)
            t->cfg.tell(t->cfg.ud, msg.ptr);
        sk_tabs_hangup_all(t);
    }
    sk_buf_free(&msg);
}

/* The follow of app/main.js (decision 0030). */
static bool follow_background(sk_tabs *t, const sk_tab *tab, sk_tab_spec *spec, sk_strv *cmd)
{
    if (tab->kind != SK_TAB_CLI || strcmp(sk_agent_provider(tab->cmd.ptr)->kind, "claude") != 0)
        return false;
    if (!sk_background_attach_for(tab->cmd.ptr, last_screen(tab), cmd))
        return false;
    sk_buf line = {0};
    for (size_t i = 0; i < cmd->len; i++)
        sk_buf_printf(&line, "%s%s", i ? " " : "", cmd->ptr[i]);
    logf_(t, "tab %d: claude refused to open a background session in place, opening it with: %s",
          tab->id, line.ptr);
    sk_buf_free(&line);
    spec->cmd = cmd->ptr;
    return true;
}

/* The successor of a tab whose CLI ended by itself, in its place, or
 * NULL. make's failure is logged and leaves no successor. */
static sk_tab *successor(sk_tabs *t, sk_tab *tab, int code)
{
    bool (*follow)(void *, const sk_tab *, int, sk_tab_spec *, sk_strv *) = t->cfg.follow;
    if ((!follow && !t->cfg.follow_background) || tab->closed || t->ending)
        return NULL;
    sk_tab_spec spec = {SK_TAB_CLI, tab->cmd.ptr, tab->cwd, &tab->env, tab->file, NULL, NULL, NULL};
    sk_strv cmd = {0};
    bool wanted = follow ? follow(t->cfg.ud, tab, code, &spec, &cmd)
                         : follow_background(t, tab, &spec, &cmd);
    sk_tab *next = NULL;
    if (wanted) {
        char *err = NULL;
        next = make_tab(t, &spec, tab->win, &err);
        if (!next)
            logf_(t, "follow: %s", err);
        free(err);
    }
    sk_strv_free(&cmd);
    return next;
}

static void remember_closed(sk_tabs *t, const sk_tab *tab)
{
    closed_tab e = {0};
    sk_strv_extend(&e.cmd, tab->cmd.ptr);
    e.cwd = sk_xstrdup(tab->cwd);
    sk_envp_from(&e.env, tab->env.vars.ptr);
    e.file = tab->file ? sk_xstrdup(tab->file) : NULL;
    const char *s = session_id(tab);
    e.session = s ? sk_xstrdup(s) : NULL;
    sk_vec_push(&t->closed, e);
}

/* The tab to the right of a gone active tab becomes active, else the
 * one to its left, in its own window. */
static void gone(sk_tabs *t, sk_tab *tab, int code)
{
    size_t i = index_of(&t->list, tab);
    if (i == SIZE_MAX)
        return;
    sk_tab *next = successor(t, tab, code);
    int win = tab->win;
    bool was_active = sk_tabs_active_of(t, win) == tab;
    if (next) {
        t->list.ptr[i] = next;
        if (t->cfg.gone)
            t->cfg.gone(t->cfg.ud, tab, code, next);
        if (was_active) {
            drop_active(t, win);
            select_tab(t, next);
        } else {
            emit_change(t);
        }
        sk_vec_push(&t->graveyard, tab);
        return;
    }
    sk_tab_list mine = {0};
    sk_tabs_in_win(t, win, &mine);
    size_t j = index_of(&mine, tab);
    memmove(t->list.ptr + i, t->list.ptr + i + 1, (t->list.len - i - 1) * sizeof *t->list.ptr);
    t->list.len--;
    if (tab->kind == SK_TAB_CLI)
        t->last_cli = code;
    if (!t->ending && tab->kind == SK_TAB_CLI)
        remember_closed(t, tab);
    if (t->cfg.gone)
        t->cfg.gone(t->cfg.ud, tab, code, NULL);
    if (!t->list.len) {
        t->done = t->ending = true;
        t->last = tab;
        t->exit_status = t->status >= 0 ? t->status : t->last_cli;
        if (t->cfg.ended)
            t->cfg.ended(t->cfg.ud, t->exit_status);
        sk_vec_free(&mine);
        return;
    }
    sk_tabs_in_win(t, win, &mine);
    if (was_active) {
        drop_active(t, win);
        if (mine.len)
            select_tab(t, mine.ptr[SK_MIN(j, mine.len - 1)]);
    }
    if (!mine.len)
        emptied(t, win);
    emit_change(t);
    sk_vec_push(&t->graveyard, tab);
    sk_vec_free(&mine);
}

void sk_tabs_ended(sk_tabs *t, int id, int code)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (!tab)
        return;
    enter(t);
    gone(t, tab, code);
    leave(t);
}

bool sk_tabs_done(const sk_tabs *t)
{
    return t->done;
}

bool sk_tabs_ending(const sk_tabs *t)
{
    return t->ending;
}

int sk_tabs_status(const sk_tabs *t)
{
    return t->status;
}

int sk_tabs_exit_status(const sk_tabs *t)
{
    return t->exit_status;
}

sk_tab *sk_tabs_last(const sk_tabs *t)
{
    return t->last;
}

const char *sk_tabs_last_screen(const sk_tabs *t)
{
    if (!t->last || t->last->closed || t->status >= 0)
        return NULL;
    return last_screen(t->last);
}

/* What a new tab did not carry, for its strip, cut to fit. */
static char *dropped_note(char *const *dropped)
{
    if (!dropped || !dropped[0])
        return NULL;
    sk_buf s = {0}, note = {0};
    sk_identity_quote_argv(&s, dropped);
    sk_buf_append_c(&note, "not carried: ");
    if (sk_js_units(sk_buf_str(&s)) > 60) {
        sk_js_cut(&note, sk_buf_str(&s), 57);
        sk_buf_append_c(&note, "...");
    } else {
        sk_buf_append_str(&note, sk_buf_str(&s));
    }
    sk_buf_free(&s);
    return note.ptr;
}

sk_tab *sk_tabs_new_tab(sk_tabs *t, const char *cwd, char **err)
{
    sk_strv args = {0}, dropped = {0}, cmd = {0};
    sk_agent_carry(t->cfg.cmd, &args, &dropped);
    sk_strv_push(&cmd, t->cfg.cmd[0]);
    sk_strv_extend(&cmd, args.ptr);
    char *note = dropped_note(dropped.ptr);
    sk_tab_spec spec = {SK_TAB_CLI, cmd.ptr, cwd, NULL, NULL, NULL, note, NULL};
    sk_tab *tab = sk_tabs_add(t, &spec, -1, err);
    free(note);
    sk_strv_free(&args);
    sk_strv_free(&dropped);
    sk_strv_free(&cmd);
    return tab;
}

/* sessionCmd: the command that resumes cmd's session, or with fork
 * copies it: the options New tab carries and the session's id. With no
 * id, a new session of the same command. */
static void session_cmd(char *const *cmd, const char *session, bool fork, sk_strv *out)
{
    sk_strv args = {0}, dropped = {0};
    sk_agent_carry(cmd, &args, &dropped);
    const char *kind = sk_agent_provider(cmd)->kind;
    sk_strv_push(out, cmd[0]);
    if (session && !strcmp(kind, "codex"))
        sk_strv_push(out, fork ? "fork" : "resume");
    sk_strv_extend(out, args.ptr);
    if (session && !strcmp(kind, "claude")) {
        sk_strv_push(out, "--resume");
        sk_strv_push(out, session);
        if (fork)
            sk_strv_push(out, "--fork-session");
    } else if (session && !strcmp(kind, "codex")) {
        sk_strv_push(out, session);
    }
    sk_strv_free(&args);
    sk_strv_free(&dropped);
}

sk_tab *sk_tabs_reopen(sk_tabs *t, char **err)
{
    if (err)
        *err = NULL;
    if (!t->closed.len)
        return NULL;
    closed_tab e = t->closed.ptr[--t->closed.len];
    for (size_t i = 0; e.session && i < t->list.len; i++) {
        sk_tab *live = t->list.ptr[i];
        if (same(session_id(live), e.session)) {
            select_tab(t, live);
            free_closed(&e);
            return live;
        }
    }
    sk_strv cmd = {0};
    session_cmd(e.cmd.ptr, e.session, false, &cmd);
    sk_tab_spec spec = {SK_TAB_CLI, cmd.ptr, e.cwd, &e.env, e.file, NULL, NULL, NULL};
    char *why = NULL;
    sk_tab *tab = sk_tabs_add(t, &spec, -1, &why);
    sk_strv_free(&cmd);
    if (!tab && why) {
        sk_vec_push(&t->closed, e);
        logf_(t, "reopen: %s", why);
        if (err)
            *err = why;
        else
            free(why);
        return NULL;
    }
    free_closed(&e);
    return tab;
}

sk_tab *sk_tabs_duplicate(sk_tabs *t, char **err)
{
    if (err)
        *err = NULL;
    sk_tab *from = sk_tabs_active(t);
    if (!from || from->kind != SK_TAB_CLI)
        return NULL;
    sk_tab_list mine = {0};
    sk_tabs_in_win(t, from->win, &mine);
    long at = (long)index_of(&mine, from) + 1;
    sk_vec_free(&mine);
    sk_strv cmd = {0};
    session_cmd(from->cmd.ptr, session_id(from), true, &cmd);
    sk_tab_spec spec = {SK_TAB_CLI, cmd.ptr, from->cwd, &from->env, from->file, NULL, NULL, NULL};
    char *why = NULL;
    sk_tab *tab = sk_tabs_add(t, &spec, from->win, &why);
    sk_strv_free(&cmd);
    if (tab)
        sk_tabs_move(t, tab->id, at);
    if (why)
        logf_(t, "duplicate: %s", why);
    if (err)
        *err = why;
    else
        free(why);
    return tab;
}

void sk_tabs_menu_state(const sk_tabs *t, sk_tabs_menu *out)
{
    const closed_tab *e = t->closed.len ? &t->closed.ptr[t->closed.len - 1] : NULL;
    const sk_tab *a = sk_tabs_active(t);
    if (a && a->kind != SK_TAB_CLI)
        a = NULL;
    out->reopen = e != NULL;
    out->reopen_fresh = e && !e->session;
    out->duplicate = a != NULL;
    out->duplicate_fresh = a && !session_id(a);
}

const char *sk_tab_name(const sk_tab *tab)
{
    const char *r = record_title(tab);
    if (tab->named && same(r, tab->named_over))
        return tab->named;
    return r ? r : tab->named ? tab->named : tab->label;
}

bool sk_tabs_rename(sk_tabs *t, int id, const char *name)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (!tab || !name)
        return false;
    sk_buf n = {0};
    sk_js_cut(&n, sk_js_trim(sk_str_c(name)), NAME_MAX_UNITS);
    if (!n.len) {
        sk_buf_free(&n);
        return false;
    }
    const char *over = record_title(tab);
    free(tab->named);
    free(tab->named_over);
    tab->named = n.ptr;
    tab->named_over = over ? sk_xstrdup(over) : NULL;
    const sk_tab_session *s = &tab->session;
    bool recorded = s->ops->rename && s->ops->rename(s->self, tab->named);
    sk_buf quoted = {0};
    sk_jw w;
    sk_jw_init(&w, &quoted, 0);
    sk_jw_string(&w, sk_str_c(tab->named));
    logf_(t, "tab %d: renamed %s%s", id, quoted.ptr, recorded ? " in the CLI's record" : "");
    sk_buf_free(&quoted);
    return true;
}

bool sk_tabs_folder(const sk_tabs *t, const sk_tab *tab, sk_buf *out)
{
    const char *name = sk_tab_name(tab);
    size_t n = 0;
    for (size_t i = 0; i < t->list.len; i++)
        if (t->list.ptr[i]->win == tab->win && !strcmp(sk_tab_name(t->list.ptr[i]), name))
            n++;
    if (n < 2)
        return false;
    sk_str base = sk_js_basename(sk_str_c(tab->cwd));
    sk_buf_append_str(out, base.len ? base : sk_str_c(tab->cwd));
    return true;
}

bool sk_tabs_output(sk_tabs *t, sk_tab *tab)
{
    if (tab->moving || tab->unseen || tab == sk_tabs_active_of(t, tab->win))
        return false;
    tab->unseen = true;
    return true;
}

bool sk_tabs_pop_begin(sk_tabs *t, int id, int from_win)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (!tab || tab->win != from_win || count_in_win(t, from_win) < 2 || t->ending || tab->moving)
        return false;
    tab->moving = true;
    return true;
}

int sk_tabs_pop_end(sk_tabs *t, int id)
{
    if (!sk_tabs_get(t, id))
        return -1;
    int win = t->next_win++;
    sk_tabs_detach(t, id, win);
    t->current = win;
    return win;
}

void sk_tabs_pop_done(sk_tabs *t, int id)
{
    sk_tab *tab = sk_tabs_get(t, id);
    if (tab)
        tab->moving = false;
}
