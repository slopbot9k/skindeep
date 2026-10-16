/* Which URLs addon-web-links 0.12.0 keeps and where they end: its own
 * cases from WebLinksAddon.test.ts, then the regex's edges and the URL
 * check, case by case. A line's links are listed with a space between. */
#include "../../src/base/utf8.h"
#include "../../src/term/links.h"
#include "../test.h"

#include <stdio.h>

/* The links sk_term_url_next finds in a line of UTF-8. */
static char *links_in(const char *s)
{
    size_t n = strlen(s);
    SK_VEC(uint32_t) cps = {0};
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(s + i, n - i, &cp, NULL);
        sk_vec_push(&cps, cp);
    }
    sk_buf out = {0};
    size_t from = 0, start, end, last = 0;
    while (sk_term_url_next(cps.ptr, cps.len, &from, &start, &end)) {
        if (start < last || end <= start || end > from || from > cps.len)
            sk_test_fail(__FILE__, __LINE__, "bad bounds in %s", s);
        last = end;
        if (out.len)
            sk_buf_putc(&out, ' ');
        for (size_t i = start; i < end; i++)
            sk_utf8_append(&out, cps.ptr[i]);
    }
    if (from != cps.len)
        sk_test_fail(__FILE__, __LINE__, "the scan stopped short in %s", s);
    sk_vec_free(&cps);
    char *r = sk_xstrdup(sk_buf_cstr(&out));
    sk_buf_free(&out);
    return r;
}

typedef struct {
    int line;
    const char *text, *links;
} url_case;

#define U(text, links) {__LINE__, text, links}

static void run(const url_case *c, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char *got = links_in(c[i].text);
        if (strcmp(got, c[i].links))
            sk_test_fail(__FILE__, c[i].line, "%s:\n  got  \"%s\"\n  want \"%s\"", c[i].text, got, c[i].links);
        free(got);
    }
}

static const char *const country_tlds[] = {
    ".ac", ".ad", ".ae", ".af", ".ag", ".ai", ".al", ".am", ".ao", ".aq", ".ar", ".as", ".at", ".au", ".aw", ".ax",
    ".az", ".ba", ".bb", ".bd", ".be", ".bf", ".bg", ".bh", ".bi", ".bj", ".bm", ".bn", ".bo", ".bq", ".br", ".bs",
    ".bt", ".bw", ".by", ".bz", ".ca", ".cc", ".cd", ".cf", ".cg", ".ch", ".ci", ".ck", ".cl", ".cm", ".cn", ".co",
    ".cr", ".cu", ".cv", ".cw", ".cx", ".cy", ".cz", ".de", ".dj", ".dk", ".dm", ".do", ".dz", ".ec", ".ee", ".eg",
    ".eh", ".er", ".es", ".et", ".eu", ".fi", ".fj", ".fk", ".fm", ".fo", ".fr", ".ga", ".gd", ".ge", ".gf", ".gg",
    ".gh", ".gi", ".gl", ".gm", ".gn", ".gp", ".gq", ".gr", ".gs", ".gt", ".gu", ".gw", ".gy", ".hk", ".hm", ".hn",
    ".hr", ".ht", ".hu", ".id", ".ie", ".il", ".im", ".in", ".io", ".iq", ".ir", ".is", ".it", ".je", ".jm", ".jo",
    ".jp", ".ke", ".kg", ".kh", ".ki", ".km", ".kn", ".kp", ".kr", ".kw", ".ky", ".kz", ".la", ".lb", ".lc", ".li",
    ".lk", ".lr", ".ls", ".lt", ".lu", ".lv", ".ly", ".ma", ".mc", ".md", ".me", ".mg", ".mh", ".mk", ".ml", ".mm",
    ".mn", ".mo", ".mp", ".mq", ".mr", ".ms", ".mt", ".mu", ".mv", ".mw", ".mx", ".my", ".mz", ".na", ".nc", ".ne",
    ".nf", ".ng", ".ni", ".nl", ".no", ".np", ".nr", ".nu", ".nz", ".om", ".pa", ".pe", ".pf", ".pg", ".ph", ".pk",
    ".pl", ".pm", ".pn", ".pr", ".ps", ".pt", ".pw", ".py", ".qa", ".re", ".ro", ".rs", ".ru", ".rw", ".sa", ".sb",
    ".sc", ".sd", ".se", ".sg", ".sh", ".si", ".sk", ".sl", ".sm", ".sn", ".so", ".sr", ".ss", ".st", ".su", ".sv",
    ".sx", ".sy", ".sz", ".tc", ".td", ".tf", ".tg", ".th", ".tj", ".tk", ".tl", ".tm", ".tn", ".to", ".tr", ".tt",
    ".tv", ".tw", ".tz", ".ua", ".ug", ".uk", ".us", ".uy", ".uz", ".va", ".vc", ".ve", ".vg", ".vi", ".vn", ".vu",
    ".wf", ".ws", ".ye", ".yt", ".za", ".zm", ".zw",
};

/* The addon's testHostName, line by line. */
static void check_host(const char *host)
{
    char text[128], want[128];
    /* The text before http://host, the text after it, and the link's
     * part after the host. */
    static const struct {
        const char *before, *after, *link;
    } forms[] = {
        {"  ", "  ", ""},
        {"  ", "/a~b#c~d?e~f  ", "/a~b#c~d?e~f"},
        {"  ", "/colon:test  ", "/colon:test"},
        {"  ", "/colon:test:  ", "/colon:test"},
        {"\"", "/\"", "/"},
        {"'", "/'", "/"},
        {"", "/subpath/+/id", "/subpath/+/id"},
    };
    for (size_t i = 0; i < SK_LEN(forms); i++) {
        snprintf(text, sizeof text, "%shttp://%s%s", forms[i].before, host, forms[i].after);
        snprintf(want, sizeof want, "http://%s%s", host, forms[i].link);
        char *got = links_in(text);
        if (strcmp(got, want))
            sk_test_fail(__FILE__, __LINE__, "%s: got \"%s\"", text, got);
        free(got);
    }
}

static void test_addon_host_names(void)
{
    char host[32];
    for (size_t i = 0; i < SK_LEN(country_tlds); i++) {
        snprintf(host, sizeof host, "foo%s", country_tlds[i]);
        check_host(host);
        snprintf(host, sizeof host, "foo.com%s", country_tlds[i]);
        check_host(host);
    }
    check_host("foo.com");
}

/* Issue 4964 of xterm.js: upper case in scheme and host, default
 * ports, credentials. */
static void test_addon_upper_case(void)
{
    static const url_case cases[] = {
        U("  HTTP://EXAMPLE.COM  ", "HTTP://EXAMPLE.COM"),
        U("  HTTPS://Example.com  ", "HTTPS://Example.com"),
        U("  HTTP://Example.com:80  ", "HTTP://Example.com:80"),
        U("  HTTP://Example.com:80/staysUpper  ", "HTTP://Example.com:80/staysUpper"),
        U("  HTTP://Ab:xY@abc.com:80/staysUpper  ", "HTTP://Ab:xY@abc.com:80/staysUpper"),
        U("aaa http://example.com aaa http://example.com aaa", "http://example.com http://example.com"),
        U("￥￥￥ https://ko.wikipedia.org/wiki/위키백과:대문 aaa", "https://ko.wikipedia.org/wiki/위키백과:대문"),
        U("￥￥￥cafe\xcc\x81 http://test:password@example.com/some_path",
          "http://test:password@example.com/some_path"),
        U("￥￥￥cafe\xcc\x81 http://test:password@example.com/some_path?param=1%202%3",
          "http://test:password@example.com/some_path?param=1%202%3"),
    };
    run(cases, SK_LEN(cases));
}

/* Where the regex starts and stops. */
static void test_regex_edges(void)
{
    static const url_case cases[] = {
        U("see http://example.com.", "http://example.com"),
        U("(http://example.com)", "http://example.com"),
        U("<http://example.com>", "http://example.com"),
        U("[http://example.com]", "http://example.com"),
        U("{http://example.com}", "http://example.com"),
        U("`http://example.com`", "http://example.com"),
        U("http://example.com/foo_(bar)", "http://example.com/foo_"),
        U("http://example.com/a,b,c,", "http://example.com/a,b,c"),
        U("http://example.com/a?b.", "http://example.com/a?b"),
        U("http://example.com/a?", "http://example.com/a"),
        U("http://example.com/x!", "http://example.com/x"),
        U("http://example.com/?q=1&r=2;s=3", "http://example.com/?q=1&r=2;s=3"),
        U("http://example.com/#!/hash", "http://example.com/#"),
        U("http://example.com/~user/", "http://example.com/~user/"),
        U("http://example.com/~", "http://example.com/"),
        U("http://example.com/x*", "http://example.com/x*"),
        U("http://example.com/x**y", "http://example.com/x*"),
        U("http://example.com/[a]", "http://example.com/[a"),
        U("http://example.com/path:", "http://example.com/path"),
        U("http://example.com/x'y", "http://example.com/x"),
        U("http://example.com/x\"y", "http://example.com/x"),
        U("http://example.com/x|y", "http://example.com/x"),
        U("http://example.com/x^y", "http://example.com/x"),
        U("http://example.com/x\\y", "http://example.com/x"),
        U("http://example.com/x\ty", "http://example.com/x"),
        U("http://example.com/x\xc2\xa0y", "http://example.com/x"),
        U("http://example.com/x\xe3\x80\x80y", "http://example.com/x"),
        U("http://example.com/x\xe2\x80\xa8y", "http://example.com/x"),
        U("http://example.com/x\xef\xbb\xbfy", "http://example.com/x"),
        U("http://example.com/x\xe2\x80\x8by", "http://example.com/x\xe2\x80\x8by"),
        U("http://example.com/日本語", "http://example.com/日本語"),
        U("http://example.com/😀", "http://example.com/😀"),
        U("http://example.com/😀.", "http://example.com/😀"),
        U("xhttp://example.com", "http://example.com"),
        U("https://example.com", "https://example.com"),
        U("HTTPS://EXAMPLE.COM", "HTTPS://EXAMPLE.COM"),
        U("Http://example.com", ""),
        U("hTTP://example.com", ""),
        U("HTTPs://example.com", ""),
        U("httpS://example.com", ""),
        U("httpss://example.com", ""),
        U("ftp://example.com", ""),
        U("http:/example.com", ""),
        U("http:example.com", ""),
        U("http://", ""),
        U("http:// x", ""),
        U("http://.", ""),
        U("http://:", ""),
        U("http://a", "http://a"),
        U("http://a b http://c", "http://a http://c"),
        U("http://a.com http://b.com", "http://a.com http://b.com"),
        U("http://http://a.com", "http://http://a.com"),
        U("http://:80 http://ok.com", "http://ok.com"),
        U("http://example.com/path?q=a@b", "http://example.com/path?q=a@b"),
        U("http://example.com@evil.com/", "http://example.com@evil.com/"),
        U("", ""),
        U("no links here", ""),
    };
    run(cases, SK_LEN(cases));
}

/* Matches the URL check keeps: the text begins with the scheme, the
 * credentials and the host as the URL Standard writes them back. */
static void test_urls_kept(void)
{
    static const url_case cases[] = {
        U("http://localhost:3000/api", "http://localhost:3000/api"),
        U("http://127.0.0.1:8080/", "http://127.0.0.1:8080/"),
        U("http://0.0.0.0/", "http://0.0.0.0/"),
        U("http://255.255.255.255/", "http://255.255.255.255/"),
        U("http://1.2.3.4./", "http://1.2.3.4./"),
        U("http://1.2.3.4../", "http://1.2.3.4../"),
        U("http://[::1]:8080/", "http://[::1]:8080/"),
        U("http://[::1]/", "http://[::1]/"),
        U("http://[2001:db8::1]/x", "http://[2001:db8::1]/x"),
        U("http://[FE80::1]:80/", "http://[FE80::1]:80/"),
        U("http://[1::2:0:0:3:0]/", "http://[1::2:0:0:3:0]/"),
        U("http://[::]/", "http://[::]/"),
        U("http://[1:2:3:4:5:6:7:8]/", "http://[1:2:3:4:5:6:7:8]/"),
        U("http://user@example.com", "http://user@example.com"),
        U("http://user:pass@example.com/", "http://user:pass@example.com/"),
        U("http://us%20er@example.com/", "http://us%20er@example.com/"),
        U("http://example.com:/", "http://example.com:/"),
        U("http://example.com:0", "http://example.com:0"),
        U("http://example.com:00", "http://example.com:00"),
        U("http://example.com:080", "http://example.com:080"),
        U("http://example.com:65535/", "http://example.com:65535/"),
        U("https://example.com:443/", "https://example.com:443/"),
        U("https://example.com:0443/", "https://example.com:0443/"),
        U("https://example.com:80/", "https://example.com:80/"),
        U("http://example.com./", "http://example.com./"),
        U("http://a..b/", "http://a..b/"),
        U("http://foo_bar.example.com/", "http://foo_bar.example.com/"),
        U("http://-a-.com/", "http://-a-.com/"),
        U("http://a$b&c+d,e;f=g~h.com/", "http://a$b&c+d,e;f=g~h.com/"),
        U("http://a.com*", "http://a.com*"),
        U("http://a.com,http://b.com", "http://a.com,http://b.com"),
        U("http://xn--n3h.com/", "http://xn--n3h.com/"),
        U("http://XN--MNCHEN-3YA.de/", "http://XN--MNCHEN-3YA.de/"),
        U("http://\xe2\x84\xaa" "elvin.com/", "http://\xe2\x84\xaa" "elvin.com/"),
        U("http://example.com\x01", "http://example.com\x01"),
    };
    run(cases, SK_LEN(cases));
}

/* Matches the URL check drops. */
static void test_urls_dropped(void)
{
    static const url_case cases[] = {
        /* Ports */
        U("http://example.com:80a/", ""),
        U("http://example.com:65536/", ""),
        U("http://example.com:99999999999999999999/", ""),
        U("http://example.com:08080/", ""),
        U("http://example.com::80/", ""),
        U("http://example.com:8:80/", ""),
        /* No host */
        U("http://:80/", ""),
        U("http:///x", ""),
        U("http:////", ""),
        U("http:///", ""),
        U("http://?q", ""),
        U("http://#f", ""),
        U("http://user@/", ""),
        U("http://user:pass@", ""),
        /* Credentials the Standard writes otherwise */
        U("http://@example.com/", ""),
        U("http://:pass@example.com/", ""),
        U("http://user:@example.com/", ""),
        U("http://a@b@example.com/", ""),
        U("http://u:p:q@example.com/", ""),
        U("http://u;x@example.com/", ""),
        U("http://u=x@example.com/", ""),
        U("http://u[x@example.com/", ""),
        U("http://\xc3\xbc@example.com/", ""),
        /* Domains it rewrites or refuses */
        U("http://m\xc3\xbcnchen.de/", ""),
        U("http://例え.jp/", ""),
        U("http://\xef\xbc\xa5xample.com/", ""),
        U("http://exa%41mple.com/", ""),
        U("http://%zz.com/", ""),
        U("http://a%2Fb.com/", ""),
        U("http://a[b]/", ""),
        U("http://a\x01.com/", ""),
        U("http://a\x7f.com/", ""),
        U("http://xn--/", ""),
        U("http://xn--abc-/", ""),
        U("http://a.xn--/", ""),
        U("http://xn--99999999999/", ""),
        /* IPv4 it rewrites or refuses */
        U("http://127.1/", ""),
        U("http://0x7f.0.0.1/", ""),
        U("http://017.0.0.1/", ""),
        U("http://4294967295/", ""),
        U("http://4294967296/", ""),
        U("http://256.0.0.1/", ""),
        U("http://1.2.3.256/", ""),
        U("http://1.2.3.4.5/", ""),
        U("http://1.2.3.09/", ""),
        U("http://1..2/", ""),
        U("http://foo.1/", ""),
        U("http://foo.0x/", ""),
        U("http://foo.0xg1/", "http://foo.0xg1/"),
        U("http://0x/", ""),
        /* IPv6 it rewrites or refuses */
        U("http://[::1]", ""),
        U("http://[::1/", ""),
        U("http://[0:0:0:0:0:0:0:1]/", ""),
        U("http://[::ffff:1.2.3.4]/", ""),
        U("http://[1::2::3]/", ""),
        U("http://[1:2:3:4:5:6:7:8:9]/", ""),
        U("http://[1:2:3]/", ""),
        U("http://[12345::]/", ""),
        U("http://[::1]x/", ""),
        U("http://[::1]:99999/", ""),
        U("http://[:1]/", ""),
        U("http://[1:]/", ""),
        U("http://[::1.2.3]/", ""),
        U("http://[::01.2.3.4]/", ""),
        U("http://[::256.2.3.4]/", ""),
        U("http://[01::1]/", ""),
        U("http://[1:0::1]/", ""),
        U("http://[1:0:0:2::3:0]/", ""),
    };
    run(cases, SK_LEN(cases));
}

const sk_test sk_suite_links[] = {
    T(test_addon_host_names),
    T(test_addon_upper_case),
    T(test_regex_edges),
    T(test_urls_kept),
    T(test_urls_dropped),
    {0},
};
