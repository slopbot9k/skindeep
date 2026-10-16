/* Port of New tab...'s half in app/main.js (decision 0025): liveCwd,
 * chooserState, folderOf, chooserOpen and the chooser:forget and
 * chooser:folder handlers. */
#include "newtab.h"

#include "../core/command.h"
#include "../platform/procinfo.h"
#include "../sessions/files.h"
#include "argv.h"
#include "identity.h"
#include "js.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

void sk_newtab_state_free(sk_newtab_state *s)
{
    for (int a = 0; a < SK_PROFILE_AGENTS; a++) {
        sk_newtab_agent *g = &s->agents[a];
        for (size_t i = 0; i < g->profiles.len; i++) {
            free(g->profiles.ptr[i].dir);
            free(g->profiles.ptr[i].shown);
        }
        sk_vec_free(&g->profiles);
        free(g->line);
        free(g->dropped);
        free(g->profile);
    }
    free(s->folder);
    free(s->home);
    free(s->base);
    memset(s, 0, sizeof *s);
}

void sk_newtab_pick_free(sk_newtab_pick *p)
{
    free(p->agent);
    free(p->profile);
    free(p->folder);
    free(p->line);
    memset(p, 0, sizeof *p);
}

static void logf_(const sk_newtab_config *cfg, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(const sk_newtab_config *cfg, const char *fmt, ...)
{
    if (!cfg->log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    cfg->log(cfg->ud, b.ptr);
    sk_buf_free(&b);
}

/* Date.now(). */
static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000);
}

static char *tildify(const char *p, const char *home)
{
    sk_buf b = {0};
    sk_identity_tildify(&b, p, home);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

/* kindOf: the provider of cmd by its basename (decision 0021). */
static bool runs(char *const *cmd, sk_profile_agent a)
{
    return !strcmp(sk_agent_provider(cmd)->kind, sk_profile_agents[a].kind);
}

/* readOnly: a monitor and the dashboard run no CLI of the app's. */
static bool cli_tab(const sk_tab *t)
{
    return t && t->kind == SK_TAB_CLI;
}

/* liveCwd: the live folder of a tab's CLI, else the folder it started
 * in. */
static char *live_cwd(const sk_newtab_config *cfg, const sk_tab *tab)
{
    char *d = NULL;
    if (cfg->live_cwd) {
        d = cfg->live_cwd(cfg->ud, tab);
    } else {
        const sk_tab_session *s = &tab->session;
        pid_t pid = s->ops && s->ops->pid ? s->ops->pid(s->self) : 0;
        if (pid > 0)
            d = sk_procinfo_cwd(pid);
    }
    if (d && sk_files_is_dir(d))
        return d;
    free(d);
    return sk_xstrdup(tab->cwd);
}

/* The folder a relative one is taken from: the active tab's live
 * folder, else the app's own. */
static char *base_of(const sk_newtab_config *cfg, int win)
{
    sk_tab *at = sk_tabs_active_of(cfg->tabs, win < 0 ? sk_tabs_current(cfg->tabs) : win);
    if (at)
        return live_cwd(cfg, at);
    if (cfg->cwd)
        return sk_xstrdup(cfg->cwd);
    sk_buf b = {0};
    sk_js_resolve(&b, sk_str_c("."));
    return b.ptr;
}

static char *quoted(char *const *words)
{
    sk_buf b = {0};
    sk_identity_quote_argv(&b, words);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

static void offer_agent(const sk_newtab_config *cfg, sk_profile_agent a, const sk_tab *from,
                        const sk_profile_records *records, const sk_profile_founds *found, sk_newtab_agent *out)
{
    out->agent = a;
    bool own = runs(cfg->cmd, a);
    sk_strv args = {0}, dropped = {0}, line = {0}, open = {0};
    if (own)
        sk_agent_carry(cfg->cmd, &args, &dropped);
    sk_strv_push(&line, own ? cfg->cmd[0] : sk_profile_agents[a].cmd);
    sk_strv_extend(&line, args.ptr);
    out->line = quoted(sk_strv_argv(&line));
    out->dropped = quoted(sk_strv_argv(&dropped));
    for (size_t i = 0; i < sk_tabs_count(cfg->tabs); i++) {
        sk_tab *t = sk_tabs_at(cfg->tabs, i);
        if (cli_tab(t) && runs(t->cmd.ptr, a)) {
            char *p = sk_profiles_of(&t->env, a);
            sk_strv_push(&open, p);
            free(p);
        }
    }
    sk_profile_choices choices = {0};
    sk_profiles_list(a, records, sk_strv_argv(&open), open.len, found, &choices);
    for (size_t i = 0; i < choices.len; i++) {
        const sk_profile_choice *c = &choices.ptr[i];
        sk_newtab_profile p = {sk_xstrdup(c->dir), tildify(c->dir, cfg->home), c->is_default, c->remembered};
        sk_vec_push(&out->profiles, p);
    }
    out->profile = from && runs(from->cmd.ptr, a) ? sk_profiles_of(&from->env, a) : sk_profiles_default(a);
    sk_profile_choices_free(&choices);
    sk_strv_free(&args);
    sk_strv_free(&dropped);
    sk_strv_free(&line);
    sk_strv_free(&open);
}

void sk_newtab_offer(const sk_newtab_config *cfg, int win, bool fresh, sk_newtab_state *out)
{
    memset(out, 0, sizeof *out);
    /* A fresh scan even within the millisecond of the last, which an
     * age of 0 would not take. */
    static const sk_profile_founds none = {0};
    const sk_profile_founds *found = cfg->foreign ? sk_foreign_home_scan(cfg->foreign, fresh ? -1 : HUGE_VAL) : &none;
    if (win < 0)
        win = sk_tabs_current(cfg->tabs);
    sk_tab *at = sk_tabs_active_of(cfg->tabs, win);
    const sk_tab *from = cli_tab(at) ? at : NULL;
    sk_profile_records records = {0};
    if (cfg->profiles_dir)
        sk_profiles_read(cfg->profiles_dir, &records);
    for (int a = 0; a < SK_PROFILE_AGENTS; a++)
        offer_agent(cfg, (sk_profile_agent)a, from, &records, found, &out->agents[a]);
    sk_profile_records_free(&records);
    sk_profile_agent kind;
    out->agent = from && sk_profiles_agent_of(sk_str_c(sk_agent_provider(from->cmd.ptr)->kind), &kind)
                     ? kind
                     : SK_PROFILE_CLAUDE;
    out->base = base_of(cfg, win);
    out->folder = tildify(out->base, cfg->home);
    out->home = cfg->home ? sk_xstrdup(cfg->home) : NULL;
}

char *sk_newtab_folder_of(const char *typed, const char *home, const char *base)
{
    sk_str t = sk_js_trim(sk_str_c(typed ? typed : ""));
    sk_buf p = {0}, all = {0}, out = {0};
    if (home && sk_str_eq_c(t, "~"))
        sk_buf_append_c(&p, home);
    else if (home && sk_str_prefix(t, SK_STR("~/")))
        sk_js_join(&p, sk_str_c(home), sk_str_sub(t, 2, SIZE_MAX));
    else
        sk_buf_append_str(&p, t);
    /* path.resolve(base, p). */
    if (!(p.len && p.ptr[0] == '/')) {
        sk_buf_append_c(&all, base ? base : "");
        sk_buf_putc(&all, '/');
    }
    sk_buf_append_str(&all, sk_buf_str(&p));
    sk_js_resolve(&out, sk_buf_str(&all));
    sk_buf_free(&p);
    sk_buf_free(&all);
    sk_buf_reserve(&out, 0);
    return out.ptr;
}

char *sk_newtab_browse_start(const char *typed, const char *home, const char *base)
{
    char *at = sk_newtab_folder_of(typed, home, base);
    if (sk_files_is_dir(at))
        return at;
    free(at);
    return sk_xstrdup(home ? home : base ? base : "/");
}

static int fail(char **err, const char *fmt, ...) SK_PRINTF(2, 3);
static int fail(char **err, const char *fmt, ...)
{
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    *err = b.ptr;
    return -1;
}

/* What the pick runs once its folder and profile are checked: its
 * words, its environment and its program. 0, or -1 with *err. */
static int command_of(const sk_newtab_config *cfg, const sk_newtab_pick *pick, sk_profile_agent a,
                      const char *cwd, sk_strv *argv, sk_envp *env, char **file, char **err)
{
    const char *why = sk_argv_split(pick->line ? pick->line : "", argv);
    if (why)
        return fail(err, "command line: %s", why);
    sk_envp_from(env, cfg->env ? cfg->env->vars.ptr : NULL);
    sk_profiles_env_for(env, a, pick->profile);
    const char *c = argv->ptr[0];
    sk_buf at = {0};
    if (strchr(c, '/')) {
        sk_buf all = {0};
        if (c[0] != '/') {
            sk_buf_append_c(&all, cwd);
            sk_buf_putc(&all, '/');
        }
        sk_buf_append_c(&all, c);
        sk_js_resolve(&at, sk_buf_str(&all));
        sk_buf_free(&all);
    } else {
        sk_buf_append_c(&at, c);
    }
    *file = sk_resolve_command(sk_buf_cstr(&at), sk_envp_get(env, "PATH"));
    sk_buf_free(&at);
    if (!*file)
        return fail(err, "%s: command not found", c);
    return 0;
}

int sk_newtab_open(const sk_newtab_config *cfg, int win, const sk_newtab_pick *pick, char **err)
{
    *err = NULL;
    sk_profile_agent a;
    if (!pick->agent || !sk_profiles_agent_of(sk_str_c(pick->agent), &a))
        return fail(err, "no agent %s", pick->agent ? pick->agent : "undefined");
    char *base = base_of(cfg, win);
    char *cwd = sk_newtab_folder_of(pick->folder, cfg->home, base);
    free(base);
    int r = -1;
    char *dflt = NULL, *file = NULL;
    sk_strv argv = {0};
    sk_envp env = {0};
    if (!sk_files_is_dir(cwd)) {
        char *shown = tildify(cwd, cfg->home);
        fail(err, "no such folder: %s", shown);
        free(shown);
        goto out;
    }
    if (!pick->profile || pick->profile[0] != '/') {
        fail(err, "choose a profile");
        goto out;
    }
    dflt = sk_profiles_default(a);
    bool is_default = !strcmp(pick->profile, dflt);
    if (!is_default && !sk_files_is_dir(pick->profile)) {
        char *shown = tildify(pick->profile, cfg->home);
        fail(err, "no such profile folder: %s", shown);
        free(shown);
        goto out;
    }
    if (command_of(cfg, pick, a, cwd, &argv, &env, &file, err) != 0)
        goto out;
    sk_tab_spec spec = {SK_TAB_CLI, argv.ptr, cwd, &env, file, NULL, NULL, NULL};
    char *why = NULL;
    sk_tab *tab = sk_tabs_add(cfg->tabs, &spec, win, &why);
    if (!tab && why) {
        logf_(cfg, "new tab: %s", why);
        fail(err, "cannot open the tab: %s", why);
        free(why);
        goto out;
    }
    /* No tab and no reason: the app is ending, and tabs.add's null was
     * no error either. */
    if (!is_default && cfg->profiles_dir &&
        sk_profiles_record(cfg->profiles_dir, a, pick->profile, cfg->now ? cfg->now() : wall_ms()) != 0)
        logf_(cfg, "profiles: %s", strerror(errno));
    r = 0;
out:
    free(cwd);
    free(dflt);
    free(file);
    sk_strv_free(&argv);
    sk_envp_free(&env);
    return r;
}

void sk_newtab_forget(const sk_newtab_config *cfg, const char *agent, const char *dir)
{
    sk_profile_agent a;
    if (cfg->profiles_dir && agent && dir && sk_profiles_agent_of(sk_str_c(agent), &a))
        sk_profiles_forget(cfg->profiles_dir, a, dir);
}
