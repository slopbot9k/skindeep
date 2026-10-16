/* The issue board of the sessions dashboard (decision 0029), as
 * test/board.test.js has it: remotes to forges, the daily series, and
 * one read per remote per refresh. The reads go through fake git, gh and
 * curl, sh scripts first on the children's PATH, which reach no network
 * and log what they were given. */
#include "../../src/base/paths.h"
#include "../../src/base/strv.h"
#include "../../src/sessions/board.h"
#include "../test.h"

#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

/* ---- Remotes ---- */

#define CHECK_REMOTE(url, forge_, key_)                                      \
    do {                                                                     \
        sk_remote r_;                                                        \
        CHECK(sk_board_parse_remote(url, &r_));                              \
        CHECK_INT(r_.forge, forge_);                                         \
        CHECK_STR(r_.key, key_);                                             \
        sk_remote_free(&r_);                                                 \
    } while (0)

#define CHECK_NO_REMOTE(url)                                                 \
    do {                                                                     \
        sk_remote r_;                                                        \
        CHECK(!sk_board_parse_remote(url, &r_));                             \
    } while (0)

static void test_remotes_https_ssh_and_scp_forms_map_to_the_forge_on_https(void)
{
    CHECK_REMOTE("https://git.example.org/b-g/claude-skins.git", SK_FORGE_GITEA,
                 "https://git.example.org/b-g/claude-skins");
    CHECK_REMOTE("https://github.com/o/r", SK_FORGE_GITHUB, "https://github.com/o/r");
    CHECK_REMOTE("git@github.com:o/r.git", SK_FORGE_GITHUB, "https://github.com/o/r");
    CHECK_REMOTE("ssh://git@git.example.org:2222/o/r.git", SK_FORGE_GITEA, "https://git.example.org/o/r");
    CHECK_REMOTE("https://git.example.org:8443/o/r/", SK_FORGE_GITEA, "https://git.example.org:8443/o/r");
    /* As new URL() reads them: the scheme's own port, case, dots. */
    CHECK_REMOTE("HTTPS://Git.Example.org:443/o/x/../r.git", SK_FORGE_GITEA, "https://git.example.org/o/r");
    CHECK_REMOTE("  https://github.com/o/r%2Egit\n", SK_FORGE_GITHUB, "https://github.com/o/r");
}

static void test_remotes_a_user_or_password_in_the_url_is_dropped(void)
{
    sk_remote r;
    CHECK(sk_board_parse_remote("https://me:secret-token@git.example.org/o/r.git", &r));
    CHECK_STR(r.key, "https://git.example.org/o/r");
    const char *fields[] = {r.scheme, r.host, r.port, r.owner, r.repo, r.key};
    for (size_t i = 0; i < SK_LEN(fields); i++)
        CHECK(!strstr(fields[i], "secret-token") && !strstr(fields[i], "me"));
    sk_remote_free(&r);
}

static void test_remotes_plain_http_only_on_the_loopback_host_no_other_form(void)
{
    CHECK_REMOTE("http://127.0.0.1:3000/o/r.git", SK_FORGE_GITEA, "http://127.0.0.1:3000/o/r");
    CHECK_REMOTE("http://[::1]:3000/o/r", SK_FORGE_GITEA, "http://[::1]:3000/o/r");
    CHECK_NO_REMOTE("http://git.example.org/o/r.git");
    CHECK_NO_REMOTE("/srv/git/r.git");
    CHECK_NO_REMOTE("file:///srv/git/r.git");
    CHECK_NO_REMOTE("https://gitlab.example.org/group/sub/r.git");
    CHECK_NO_REMOTE("");
    CHECK_NO_REMOTE(NULL);
    CHECK_NO_REMOTE("https://git.example.org:99999/o/r");
    CHECK_NO_REMOTE("https://git.example.org/o/r%zz");
}

/* ---- The daily series and the stamps ---- */

static double local(int y, int mon, int day, int h, int min)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon = mon;
    tm.tm_mday = day;
    tm.tm_hour = h;
    tm.tm_min = min;
    tm.tm_isdst = -1;
    return (double)mktime(&tm) * 1000;
}

static double day(int d, int h)
{
    return local(2026, 9, 12 + d, h, 0);
}

static char *ints(const int *v, size_t n)
{
    sk_buf b = {0};
    for (size_t i = 0; i < n; i++)
        sk_buf_printf(&b, "%s%d", i ? "," : "", v[i]);
    return b.ptr;
}

#define CHECK_INTS(v, want)                                                  \
    do {                                                                     \
        char *s_ = ints(v, SK_LEN(v));                                       \
        CHECK_STR(s_, want);                                                 \
        free(s_);                                                            \
    } while (0)

static void test_the_daily_series_open_at_each_days_end_closed_in_each_day_today_last(void)
{
    double now = local(2026, 9, 12, 15, 0);
    sk_issue issues[] = {
        {day(-20, 12), NAN},
        {day(-10, 12), day(-5, 12)},
        {day(-3, 12), day(0, 9)},
        {day(0, 10), NAN},
    };
    sk_board_series s;
    sk_board_series_of(issues, SK_LEN(issues), now, &s);
    CHECK_INTS(s.open, "1,1,1,2,2,2,2,2,1,1,2,2,2,2");
    CHECK_INTS(s.closed, "0,0,0,0,0,0,0,0,1,0,0,0,0,1");
    CHECK_INT(s.open_now, 2);
    CHECK_INT(s.closed14, 2);
}

static double stamp_of(const char *json)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *v = json ? sk_json_parse(&a, json, strlen(json), NULL) : NULL;
    double t = sk_board_stamp(v);
    sk_arena_free(&a);
    return t;
}

static void test_stamps_ghs_empty_and_zero_year_closed_at_read_as_open(void)
{
    CHECK(isnan(stamp_of("\"\"")));
    CHECK(isnan(stamp_of("\"0001-01-01T00:00:00Z\"")));
    CHECK(isnan(stamp_of("null")));
    CHECK(isnan(stamp_of(NULL)));
    /* Date.parse('2026-10-12T10:00:00Z') */
    CHECK(stamp_of("\"2026-10-12T10:00:00Z\"") == 1791799200000.0);
    CHECK(stamp_of("\"2026-10-12T10:00:00.123+02:00\"") == 1791799200000.0 - 7200000 + 123);
    CHECK(stamp_of("\"2026-10-12\"") == 1791763200000.0);
    CHECK(stamp_of("\"2026-10-12T10:00:00\"") == local(2026, 9, 12, 10, 0));
    CHECK(isnan(stamp_of("\"2026-13-12T10:00:00Z\"")));
    CHECK(isnan(stamp_of("\"yesterday\"")));
    CHECK(isnan(stamp_of("1791799200000")));
}

/* ---- Reading boards through the fakes ---- */

static const char FAKE_GIT[] =
    "#!/bin/sh\n"
    "echo \"prompt=$GIT_TERMINAL_PROMPT gh=$GH_PROMPT_DISABLED askpass=${GIT_ASKPASS-unset}\" >> \"$FAKE_LOG/env.log\"\n"
    "if [ \"$1\" = -C ]; then\n"
    "  echo \"$*\" >> \"$FAKE_LOG/git.log\"\n"
    "  case \"$2\" in\n"
    "    /a) echo https://git.example.org/o/r.git ;;\n"
    "    /a/sub) echo git@git.example.org:o/r.git ;;\n"
    "    /b) echo https://git.example.org/o/locked ;;\n"
    "    /gh) echo git@github.com:o/r.git ;;\n"
    "    /ghx) echo https://github.com/o/missing ;;\n"
    "    /nologin) echo https://nologin.example.org/o/r ;;\n"
    "    /many) echo https://git.example.org/o/many ;;\n"
    "    *) exit 2 ;;\n"
    "  esac\n"
    "  exit 0\n"
    "fi\n"
    "echo \"$*\" >> \"$FAKE_LOG/credential-argv.log\"\n"
    "input=$(cat)\n"
    "printf '%s\\n--\\n' \"$input\" >> \"$FAKE_LOG/credential.log\"\n"
    "case \"$input\" in\n"
    "  *host=nologin.example.org*) printf 'protocol=https\\nhost=nologin.example.org\\n' ;;\n"
    "  *) printf 'protocol=https\\nhost=git.example.org\\nusername=me\\npassword=s3cret-tok\\n' ;;\n"
    "esac\n";

static const char FAKE_CURL[] =
    "#!/bin/sh\n"
    "echo \"$*\" >> \"$FAKE_LOG/curl-argv.log\"\n"
    "config=$(cat)\n"
    "printf '%s\\n--\\n' \"$config\" >> \"$FAKE_LOG/curl-config.log\"\n"
    "case \"$config\" in\n"
    "  *'/repos/o/locked/'*) printf '{\"message\":\"not found\"}\\n404' ;;\n"
    "  *'/repos/o/many/'*'page=1\"'*)\n"
    "    i=0; printf '['\n"
    "    while [ $i -lt 50 ]; do\n"
    "      if [ $i -gt 0 ]; then printf ','; fi\n"
    "      printf '{\"created_at\":\"2026-10-01T00:00:00Z\",\"closed_at\":null}'\n"
    "      i=$((i + 1))\n"
    "    done\n"
    "    printf ']\\n200' ;;\n"
    "  *'/repos/o/many/'*'page=2\"'*)\n"
    "    printf '[{\"created_at\":\"2026-10-01T00:00:00Z\",\"closed_at\":\"2026-10-02T00:00:00Z\"},'\n"
    "    printf '{\"created_at\":\"2026-10-01T00:00:00Z\"},{\"created_at\":\"0001-01-01T00:00:00Z\"}]\\n200' ;;\n"
    "  *) printf '[{\"created_at\":\"2026-10-12T00:00:00Z\",\"closed_at\":null}]\\n200' ;;\n"
    "esac\n";

static const char FAKE_GH[] =
    "#!/bin/sh\n"
    "echo \"$*\" >> \"$FAKE_LOG/gh.log\"\n"
    "[ \"$1\" = issue ] && [ \"$2\" = list ] || exit 1\n"
    "case \"$*\" in\n"
    "  *'--repo o/r '*)\n"
    "    printf '[{\"createdAt\":\"2026-10-10T00:00:00Z\",\"closedAt\":\"\"},'\n"
    "    printf '{\"createdAt\":\"2026-10-01T00:00:00Z\",\"closedAt\":\"2026-10-11T12:00:00Z\"}]\\n' ;;\n"
    "  *) echo 'GraphQL: Could not resolve to a Repository' >&2; exit 1 ;;\n"
    "esac\n";

static void write_script(const char *dir, const char *name, const char *text)
{
    char *p = sk_path_join(dir, name);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
    CHECK(chmod(p, 0755) == 0);
    free(p);
}

typedef struct {
    sk_loop *loop;
    sk_boards *boards;
    char *bin, *logs;
    sk_strv env;
    sk_buf log;
} fixture;

static double fake_now;

static double now_fn(void)
{
    return fake_now;
}

static void on_log(void *ud, const char *msg)
{
    sk_buf_printf(ud, "%s\n", msg);
}

/* Boards whose children find the fakes in bin; with_gh false leaves gh
 * out. */
static void fixture_init(fixture *fx, bool with_gh)
{
    memset(fx, 0, sizeof *fx);
    const char *root = sk_test_tmpdir();
    fx->bin = sk_path_join(root, with_gh ? "bin" : "bin-no-gh");
    fx->logs = sk_path_join(root, "logs");
    CHECK(sk_mkdirs(fx->bin, 0777) == 0 && sk_mkdirs(fx->logs, 0777) == 0);
    write_script(fx->bin, "git", FAKE_GIT);
    write_script(fx->bin, "curl", FAKE_CURL);
    if (with_gh)
        write_script(fx->bin, "gh", FAKE_GH);
    sk_strv_pushf(&fx->env, "PATH=%s:/usr/bin:/bin", fx->bin);
    sk_strv_pushf(&fx->env, "FAKE_LOG=%s", fx->logs);
    sk_strv_push(&fx->env, "GIT_ASKPASS=/usr/bin/false");
    fx->loop = sk_loop_new();
    fake_now = local(2026, 9, 12, 15, 0);
    sk_boards_opts o = {fx->loop, now_fn, 0, on_log, &fx->log, sk_strv_argv(&fx->env)};
    fx->boards = sk_boards_new(&o);
}

static void fixture_free(fixture *fx)
{
    sk_boards_free(fx->boards);
    sk_loop_free(fx->loop);
    sk_strv_free(&fx->env);
    sk_buf_free(&fx->log);
    free(fx->bin);
    free(fx->logs);
}

/* Asks for each folder, then lets every child finish, three times: a
 * folder's board is read on the get after its remote is known. */
static void pump(fixture *fx, const char *const *folders)
{
    for (int round = 0; round < 3; round++) {
        for (const char *const *f = folders; *f; f++) {
            sk_board out;
            sk_boards_get(fx->boards, *f, &out);
        }
        uint64_t until = sk_now_ms() + 10000;
        while (sk_boards_busy(fx->boards) && sk_now_ms() < until)
            sk_loop_once(fx->loop, 50);
        CHECK(!sk_boards_busy(fx->boards));
    }
}

static sk_board get(fixture *fx, const char *folder)
{
    sk_board out;
    sk_boards_get(fx->boards, folder, &out);
    return out;
}

/* The lines of one of the fakes' logs, or "" when it has none. */
static char *log_text(const fixture *fx, const char *name)
{
    char *p = sk_path_join(fx->logs, name);
    char *text = sk_test_read(p, NULL);
    free(p);
    return text ? text : sk_xstrdup("");
}

static size_t lines(const fixture *fx, const char *name)
{
    char *text = log_text(fx, name);
    size_t n = 0;
    for (const char *c = text; *c; c++)
        n += *c == '\n';
    free(text);
    return n;
}

static char *report(const fixture *fx)
{
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_boards_report(fx->boards, &w);
    return out.ptr;
}

static void test_one_read_per_remote_per_refresh_shared_by_its_folders_a_miss_kept(void)
{
    fixture fx;
    fixture_init(&fx, true);
    CHECK_INT(get(&fx, "/a").state, SK_BOARD_LOADING);
    pump(&fx, (const char *[]){"/a", "/a/sub", "/b", "/c", NULL});
    sk_board a = get(&fx, "/a"), sub = get(&fx, "/a/sub");
    CHECK_INT(a.state, SK_BOARD_OK);
    CHECK_INT(a.series.open_now, 1);
    CHECK_INT(sub.state, SK_BOARD_OK);
    CHECK_INT(sub.series.open_now, 1);
    CHECK_INT(get(&fx, "/b").state, SK_BOARD_NO_ACCESS);
    CHECK_INT(get(&fx, "/c").state, SK_BOARD_NONE);
    CHECK(!sk_boards_busy(fx.boards));
    CHECK_INT(lines(&fx, "curl-argv.log"), 2);
    char *r = report(&fx);
    CHECK_STR(r, "[{\"key\":\"https://git.example.org/o/r\",\"fetches\":1,\"error\":null},"
                 "{\"key\":\"https://git.example.org/o/locked\",\"fetches\":1,\"error\":\"HTTP 404\"}]");
    free(r);
    CHECK(strstr(sk_buf_cstr(&fx.log), "board: https://git.example.org/o/r: 1 issues\n") != NULL);
    CHECK(strstr(sk_buf_cstr(&fx.log), "board: https://git.example.org/o/locked: no access (HTTP 404)\n") != NULL);

    fake_now += 299000;
    pump(&fx, (const char *[]){"/a", "/a/sub", "/b", NULL});
    CHECK_INT(lines(&fx, "curl-argv.log"), 2); /* no read before the refresh is due */
    fake_now += 2000;
    pump(&fx, (const char *[]){"/a", "/a/sub", "/b", NULL});
    CHECK_INT(lines(&fx, "curl-argv.log"), 4); /* one more read of each remote once due */
    fixture_free(&fx);
}

static void test_gitea_the_password_reaches_curl_on_its_stdin_alone(void)
{
    fixture fx;
    fixture_init(&fx, true);
    pump(&fx, (const char *[]){"/a", NULL});
    CHECK_INT(get(&fx, "/a").state, SK_BOARD_OK);
    char *argv = log_text(&fx, "curl-argv.log"), *config = log_text(&fx, "curl-config.log"),
         *cred = log_text(&fx, "credential.log"), *cred_argv = log_text(&fx, "credential-argv.log"),
         *env = log_text(&fx, "env.log");
    CHECK_STR(argv, "-q --config -\n");
    CHECK(strstr(config, "url = \"https://git.example.org/api/v1/repos/o/r/issues?state=all&type=issues&limit=50"
                         "&page=1\"\n") != NULL);
    CHECK(strstr(config, "header = \"Authorization: token s3cret-tok\"\n") != NULL);
    CHECK(strstr(config, "noproxy = \"*\"\n") != NULL);
    CHECK(strstr(config, "location") == NULL);
    CHECK_STR(cred, "protocol=https\nhost=git.example.org\n--\n");
    CHECK_STR(cred_argv, "-c core.askPass= -c credential.interactive=never credential fill\n");
    /* git and gh never prompt: no terminal, no askpass program. */
    CHECK(strstr(env, "prompt=0 gh=1 askpass=unset\n") == env);
    CHECK(strstr(sk_buf_cstr(&fx.log), "s3cret") == NULL);
    free(argv);
    free(config);
    free(cred);
    free(cred_argv);
    free(env);
    fixture_free(&fx);
}

static void test_gitea_pages_of_50_then_the_rest_no_login_without_a_password(void)
{
    fixture fx;
    fixture_init(&fx, true);
    pump(&fx, (const char *[]){"/many", "/nologin", NULL});
    sk_board many = get(&fx, "/many");
    CHECK_INT(many.state, SK_BOARD_OK);
    CHECK_INT(many.series.open_now, 51);
    CHECK_INT(many.series.closed14, 1);
    CHECK(strstr(sk_buf_cstr(&fx.log), "board: https://git.example.org/o/many: 52 issues\n") != NULL);
    CHECK_INT(lines(&fx, "curl-argv.log"), 2);
    CHECK_INT(get(&fx, "/nologin").state, SK_BOARD_NO_ACCESS);
    char *r = report(&fx);
    CHECK(strstr(r, "{\"key\":\"https://nologin.example.org/o/r\",\"fetches\":1,\"error\":\"no login\"}") != NULL);
    free(r);
    fixture_free(&fx);
}

static void test_github_through_gh_with_its_own_login(void)
{
    fixture fx;
    fixture_init(&fx, true);
    pump(&fx, (const char *[]){"/gh", "/ghx", NULL});
    sk_board gh = get(&fx, "/gh");
    CHECK_INT(gh.state, SK_BOARD_OK);
    CHECK_INT(gh.series.open_now, 1);
    CHECK_INT(gh.series.closed14, 1);
    /* The two run at once, so either may log first. */
    char *calls = log_text(&fx, "gh.log");
    CHECK_INT(lines(&fx, "gh.log"), 2);
    CHECK(strstr(calls, "issue list --repo o/r --state all --json createdAt,closedAt --limit 500\n") != NULL);
    CHECK(strstr(calls, "issue list --repo o/missing --state all --json createdAt,closedAt --limit 500\n") != NULL);
    free(calls);
    CHECK_INT(get(&fx, "/ghx").state, SK_BOARD_NO_ACCESS);
    char *r = report(&fx);
    CHECK_STR(r, "[{\"key\":\"https://github.com/o/r\",\"fetches\":1,\"error\":null},"
                 "{\"key\":\"https://github.com/o/missing\",\"fetches\":1,\"error\":\"gh: exit 1\"}]");
    free(r);
    CHECK_INT(lines(&fx, "curl-argv.log"), 0);
    fixture_free(&fx);
}

static void test_a_tool_that_is_not_there_cannot_start(void)
{
    fixture fx;
    fixture_init(&fx, false);
    pump(&fx, (const char *[]){"/gh", NULL});
    CHECK_INT(get(&fx, "/gh").state, SK_BOARD_NO_ACCESS);
    char *r = report(&fx);
    CHECK_STR(r, "[{\"key\":\"https://github.com/o/r\",\"fetches\":1,\"error\":\"gh: cannot start\"}]");
    free(r);
    /* Freed while its children run: they are ended, not left behind. */
    sk_board out;
    sk_boards_get(fx.boards, "/a", &out);
    CHECK(sk_boards_busy(fx.boards));
    fixture_free(&fx);
}

const sk_test sk_suite_board[] = {
    T(test_remotes_https_ssh_and_scp_forms_map_to_the_forge_on_https),
    T(test_remotes_a_user_or_password_in_the_url_is_dropped),
    T(test_remotes_plain_http_only_on_the_loopback_host_no_other_form),
    T(test_the_daily_series_open_at_each_days_end_closed_in_each_day_today_last),
    T(test_stamps_ghs_empty_and_zero_year_closed_at_read_as_open),
    T(test_one_read_per_remote_per_refresh_shared_by_its_folders_a_miss_kept),
    T(test_gitea_the_password_reaches_curl_on_its_stdin_alone),
    T(test_gitea_pages_of_50_then_the_rest_no_login_without_a_password),
    T(test_github_through_gh_with_its_own_login),
    T(test_a_tool_that_is_not_there_cannot_start),
    {0},
};
