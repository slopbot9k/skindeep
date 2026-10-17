/* The visualization's core side (decision 0015) as app/viz.js has it:
 * the frames each tab's bus gives, the events drained whether the
 * window is open or not, each pane's mode and the drop-downs, the
 * records of the last mode and input, and the sound output's recorder,
 * a fake parec in POSIX sh on PATH. */
#include "../../src/app/lastskin.h"
#include "../../src/app/vizfeed.h"
#include "../../src/viz/viz.h"
#include "../test.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_MSGS 64

typedef struct {
    sk_vizfeed_msg *msgs[MAX_MSGS];
    size_t n;
    sk_strv logs;
} rig;

static void keep_frame(void *ud, sk_vizfeed_msg *m)
{
    rig *r = ud;
    if (r->n < MAX_MSGS)
        r->msgs[r->n++] = m;
    else
        sk_vizfeed_msg_free(m);
}

static void keep_log(void *ud, const char *msg)
{
    sk_strv_push(&((rig *)ud)->logs, msg);
}

static void rig_forget(rig *r)
{
    for (size_t i = 0; i < r->n; i++)
        sk_vizfeed_msg_free(r->msgs[i]);
    r->n = 0;
}

static sk_vizfeed_msg *last_msg(rig *r)
{
    return r->n ? r->msgs[r->n - 1] : NULL;
}

/* A tab's bus as an agent's source writes it. */
typedef struct {
    sk_bus *bus;
    sk_source *src;
} fake_bus;

static void bus_open(fake_bus *b)
{
    b->bus = sk_bus_new(NULL, NULL);
    sk_source_spec spec = {NULL, 0, NULL, 0};
    b->src = sk_bus_source(b->bus, &spec);
    sk_source_begin(b->src);
}

static sk_vizfeed *feed_open(rig *r, sk_loop *loop, const char *mode_dir, const char *input_dir)
{
    memset(r, 0, sizeof *r);
    sk_vizfeed_config c = {0};
    c.loop = loop;
    c.mode_dir = mode_dir;
    c.input_dir = input_dir;
    c.pid = 4242;
    c.frame = keep_frame;
    c.log = keep_log;
    c.ud = r;
    c.manual = true;
    return sk_vizfeed_new(&c);
}

static void feed_close(sk_vizfeed *f, rig *r)
{
    sk_vizfeed_free(f);
    rig_forget(r);
    sk_strv_free(&r->logs);
}

static void test_a_frame_from_each_tab_s_bus(void)
{
    sk_loop *loop = sk_loop_new();
    rig r;
    sk_vizfeed *f = feed_open(&r, loop, NULL, NULL);
    fake_bus a, d, b;
    bus_open(&a);
    bus_open(&d);
    bus_open(&b);
    sk_source_set_num(a.src, "pty.out_bps", 1200);
    sk_source_set_num(a.src, "pty.in_kps", 3);
    sk_source_set_num(a.src, "pty.idle_s", 0.5);
    sk_source_set_num(a.src, "tokens.out_rate", 80);
    sk_source_set_num(a.src, "state.busy", 1);
    sk_source_set_num(a.src, "proc.cpu_pct", 40);
    sk_source_set_num(a.src, "context.used_pct", 33);
    sk_source_event(a.src, SK_EV_PROMPT, 0, false);
    sk_source_event(a.src, SK_EV_TOOL, SK_TOOL_EDIT, false);
    /* The dashboard's events are drained too, and go nowhere. */
    sk_source_event(d.src, SK_EV_STOP, 0, false);
    sk_source_set_str(b.src, "pty.out_bps", SK_STR("not a number"));
    sk_vizfeed_tab tabs[] = {{1, a.bus, true, true}, {2, d.bus, false, false}, {3, b.bus, false, true}};
    sk_vizfeed_tabs(f, tabs, SK_LEN(tabs));

    sk_vizfeed_tick(f, 1000);
    sk_vizfeed_msg *m = last_msg(&r);
    CHECK(m != NULL);
    if (m) {
        CHECK(!m->open);
        CHECK_INT(m->frame.n_panes, 2);
        CHECK_INT(m->frame.src, SK_VIZ_SRC_SESSION);
        CHECK_INT(m->frame.st, SK_VIZ_SOUND_NONE);
        CHECK_INT(m->frame.n_samples, 0);
        const sk_viz_pane_frame *p = m->frame.panes;
        CHECK_INT(p[0].tab, 1);
        CHECK_INT(p[0].place, 1);
        CHECK(p[0].active);
        /* Closed: no pane has a mode yet, none a drop-down. */
        CHECK_INT(p[0].mode, -1);
        CHECK(!p[0].open);
        CHECK(p[0].n[SK_VIZ_OUT_BPS] == 1200);
        CHECK(p[0].n[SK_VIZ_IN_KPS] == 3);
        CHECK(p[0].n[SK_VIZ_IDLE_S] == 0.5);
        CHECK(p[0].n[SK_VIZ_OUT_RATE] == 80);
        CHECK(p[0].n[SK_VIZ_BUSY] == 1);
        CHECK(p[0].n[SK_VIZ_CPU] == 40);
        CHECK(p[0].n[SK_VIZ_CTX] == 33);
        CHECK_INT(p[0].n_events, 4);
        if (p[0].n_events == 4) {
            CHECK_INT(p[0].events[0], SK_EV_PROMPT);
            CHECK_INT(p[0].events[2], SK_EV_TOOL);
            CHECK_INT(p[0].events[3], SK_TOOL_EDIT);
        }
        CHECK(!m->no_busy[0]);
        CHECK(m->quiet[0] == 0);
        /* The third tab of the list, whose numbers are none. */
        CHECK_INT(p[1].tab, 3);
        CHECK_INT(p[1].place, 3);
        CHECK(!p[1].active);
        CHECK(p[1].n[SK_VIZ_OUT_BPS] == 0);
        CHECK_INT(p[1].n_events, 0);
        CHECK(m->no_busy[1]);
    }
    int left[8];
    CHECK_INT(sk_bus_drain_events(d.bus, left, SK_LEN(left)), 0);

    /* The quiet seconds: since the last event, or the terminal's last
     * output when that is nearer. */
    sk_vizfeed_tick(f, 3500);
    m = last_msg(&r);
    CHECK(m && m->frame.n_panes == 2 && m->frame.panes[0].n_events == 0);
    if (m && m->frame.n_panes == 2) {
        CHECK(m->quiet[0] == 0.5);
        CHECK(m->quiet[1] == 2.5);
    }
    sk_source_event(b.src, SK_EV_OUTPUT, 0, false);
    sk_vizfeed_tick(f, 4000);
    m = last_msg(&r);
    if (m && m->frame.n_panes == 2)
        CHECK(m->quiet[1] == 0);

    /* Open: each pane has its mode, the first ones no other shows. */
    sk_vizfeed_open(f);
    sk_vizfeed_tick(f, 4033);
    m = last_msg(&r);
    CHECK(m && m->open && m->opened == 1);
    if (m && m->frame.n_panes == 2) {
        CHECK_INT(m->frame.panes[0].mode, 0);
        CHECK_INT(m->frame.panes[1].mode, 1);
    }
    /* A tab gone takes its pane, and its numbers, with it. */
    sk_vizfeed_tab after[] = {{3, b.bus, true, true}};
    sk_vizfeed_tabs(f, after, 1);
    sk_vizfeed_tick(f, 4066);
    m = last_msg(&r);
    if (m) {
        CHECK_INT(m->frame.n_panes, 1);
        CHECK(m->frame.n_panes != 1 || (m->frame.panes[0].tab == 3 && m->frame.panes[0].place == 1 &&
                                         m->frame.panes[0].active && m->frame.panes[0].mode == 1));
    }
    feed_close(f, &r);
    sk_bus_free(a.bus);
    sk_bus_free(d.bus);
    sk_bus_free(b.bus);
    sk_loop_free(loop);
}

static char *dir_in(const char *name)
{
    return sk_path_join(sk_test_tmpdir(), name);
}

/* The spec pid's record holds, or NULL. */
static char *record_of(const char *dir, pid_t pid)
{
    sk_last_records r = {0};
    sk_last_read(dir, &r);
    char *spec = NULL;
    for (size_t i = 0; i < r.len && !spec; i++)
        if (r.ptr[i].pid == pid)
            spec = sk_xstrdup(r.ptr[i].spec);
    sk_last_records_free(&r);
    return spec;
}

static void test_each_pane_s_mode_and_its_drop_down(void)
{
    sk_loop *loop = sk_loop_new();
    char *md = dir_in("last-viz"), *id = dir_in("last-viz-input");
    rig r;
    sk_vizfeed *f = feed_open(&r, loop, md, id);
    sk_vizfeed_tab tabs[] = {{1, NULL, false, true}, {2, NULL, true, true}, {5, NULL, false, false}};
    sk_vizfeed_tabs(f, tabs, SK_LEN(tabs));
    CHECK_INT(sk_vizfeed_mode_of(f, 1), 0);
    CHECK_INT(sk_vizfeed_mode_of(f, 2), 1);
    CHECK_INT(sk_vizfeed_mode_of(f, 5), -1);

    /* The space bar steps the active tab's pane alone, and records. */
    sk_vizfeed_step(f);
    CHECK_INT(sk_vizfeed_mode_of(f, 2), 2);
    CHECK_INT(sk_vizfeed_mode_of(f, 1), 0);
    char *spec = record_of(md, 4242);
    CHECK_STR(spec, "plasma");
    free(spec);
    /* The menu lists every mode, the active pane's marked. */
    sk_vizfeed_choice modes[SK_VIZ_MODES];
    CHECK_INT(sk_vizfeed_modes(f, modes), SK_VIZ_MODES);
    for (int i = 0; i < SK_VIZ_MODES; i++)
        CHECK(modes[i].current == (i == 2) && modes[i].available);
    CHECK_STR(modes[16].id, "carbon");
    CHECK_STR(modes[12].label, "Zoom out");
    /* Its pick shows in the active pane and opens the window. */
    CHECK(!sk_vizfeed_is_open(f));
    CHECK(sk_vizfeed_select(f, "go"));
    CHECK(!sk_vizfeed_select(f, "nope"));
    CHECK(sk_vizfeed_is_open(f));
    CHECK_INT(sk_vizfeed_mode_of(f, 2), sk_viz_mode_find("go"));
    spec = record_of(md, 4242);
    CHECK_STR(spec, "go");
    free(spec);
    /* The space bar wraps. */
    CHECK(sk_vizfeed_select(f, "carbon"));
    sk_vizfeed_step(f);
    CHECK_INT(sk_vizfeed_mode_of(f, 2), 0);

    /* Two panes side by side in 720 by 460: the second's button opens
     * its drop-down, and again closes it. */
    const int W = 720, H = 460;
    sk_rect p1 = sk_viz_pane_rect(1, 2, W, H);
    sk_viz_menu menu;
    sk_viz_menu_of(p1, SK_VIZ_MODES, W, H, &menu);
    double bx = menu.button.x + 5, by = menu.button.y + 5;
    sk_vizfeed_press(f, bx, by, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 2);
    sk_vizfeed_tick(f, 100);
    sk_vizfeed_msg *m = last_msg(&r);
    CHECK(m && m->frame.n_panes == 2 && !m->frame.panes[0].open && m->frame.panes[1].open);
    sk_vizfeed_press(f, bx, by, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 0);
    /* Open, a row shows its mode in that pane alone, and closes it. */
    sk_vizfeed_press(f, bx, by, W, H);
    sk_rect row = menu.items[sk_viz_mode_find("quantum")];
    sk_vizfeed_press(f, row.x + 3, row.y + 3, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 0);
    CHECK_INT(sk_vizfeed_mode_of(f, 2), sk_viz_mode_find("quantum"));
    CHECK_INT(sk_vizfeed_mode_of(f, 1), 0);
    /* The first pane's button while the second's list is open: the list
     * closes and the first's opens. A press on the picture closes it. */
    sk_rect p0 = sk_viz_pane_rect(0, 2, W, H);
    sk_viz_menu menu0;
    sk_viz_menu_of(p0, SK_VIZ_MODES, W, H, &menu0);
    sk_vizfeed_press(f, bx, by, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 2);
    sk_vizfeed_press(f, menu0.button.x + 1, menu0.button.y + 1, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 1);
    sk_vizfeed_press(f, p0.x + p0.w / 2.0, p0.y + p0.h - 10, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 0);
    /* The same, posted from the window's thread. */
    sk_vizfeed_post_press(f, bx, by, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 0);
    sk_loop_once(loop, 0);
    CHECK_INT(sk_vizfeed_menu_tab(f), 2);
    sk_vizfeed_post_step(f);
    sk_loop_once(loop, 0);
    CHECK_INT(sk_vizfeed_mode_of(f, 2), sk_viz_mode_find("carbon"));

    /* A new pane starts at the first mode no other shows, counting from
     * the last pick; a pane gone takes its drop-down. */
    sk_vizfeed_tab three[] = {{1, NULL, false, true}, {7, NULL, true, true}};
    sk_vizfeed_tabs(f, three, SK_LEN(three));
    CHECK_INT(sk_vizfeed_mode_of(f, 7), sk_viz_mode_find("carbon"));
    CHECK_INT(sk_vizfeed_menu_tab(f), 0);
    CHECK_INT(sk_vizfeed_mode_of(f, 2), -1);
    CHECK(sk_vizfeed_set_mode(f, 7, 3));
    CHECK(!sk_vizfeed_set_mode(f, 9, 3));
    CHECK(!sk_vizfeed_set_mode(f, 7, SK_VIZ_MODES));
    sk_vizfeed_active(f, 1);
    sk_vizfeed_step(f);
    CHECK_INT(sk_vizfeed_mode_of(f, 1), 1);
    CHECK_INT(sk_vizfeed_mode_of(f, 7), 3);

    /* Closed by the user: the drop-down goes, and a close posted for an
     * earlier window is not this one's. */
    sk_vizfeed_press(f, menu0.button.x + 1, menu0.button.y + 1, W, H);
    CHECK_INT(sk_vizfeed_menu_tab(f), 1);
    sk_vizfeed_post_closed(f, 0);
    sk_loop_once(loop, 0);
    CHECK(sk_vizfeed_is_open(f));
    sk_vizfeed_post_closed(f, 1);
    sk_loop_once(loop, 0);
    CHECK(!sk_vizfeed_is_open(f));
    CHECK_INT(sk_vizfeed_menu_tab(f), 0);
    sk_vizfeed_toggle(f);
    CHECK(sk_vizfeed_is_open(f));
    sk_vizfeed_tick(f, 200);
    m = last_msg(&r);
    CHECK(m && m->opened == 2);
    sk_vizfeed_toggle(f);
    CHECK(!sk_vizfeed_is_open(f));
    feed_close(f, &r);
    free(md);
    free(id);
    sk_loop_free(loop);
}

static void test_the_records_of_the_last_mode_and_input(void)
{
    sk_loop *loop = sk_loop_new();
    char *md = dir_in("last-viz"), *id = dir_in("last-viz-input");
    /* Another window's records: the newest that names a mode wins, and
     * records long older than the newest are pruned. */
    CHECK_INT(sk_last_write(md, 11, "gravity", 100000), 0);
    CHECK_INT(sk_last_write(md, 12, "nonsense", 100500), 0);
    CHECK_INT(sk_last_write(md, 13, "fern", 50000), 0);
    CHECK_INT(sk_last_write(id, 11, "both", 100000), 0);
    rig r;
    sk_vizfeed_config c = {0};
    c.loop = loop;
    c.mode_dir = md;
    c.input_dir = id;
    c.pid = 4242;
    c.manual = true;
    c.recorders = sk_viz_recorders_linux;
    c.n_recorders = SK_VIZ_RECORDERS_LINUX;
    memset(&r, 0, sizeof r);
    c.frame = keep_frame;
    c.ud = &r;
    sk_vizfeed *f = sk_vizfeed_new(&c);
    sk_vizfeed_tab tabs[] = {{1, NULL, true, true}, {2, NULL, false, true}};
    sk_vizfeed_tabs(f, tabs, SK_LEN(tabs));
    CHECK_INT(sk_vizfeed_mode_of(f, 1), sk_viz_mode_find("gravity"));
    CHECK_INT(sk_vizfeed_mode_of(f, 2), sk_viz_mode_find("gravity3d"));
    CHECK_INT(sk_vizfeed_input(f), SK_VIZ_SRC_BOTH);
    char *p = sk_path_join(md, "13");
    CHECK(access(p, F_OK) != 0);
    free(p);
    sk_vizfeed_choice in[SK_VIZ_INPUTS];
    CHECK_INT(sk_vizfeed_inputs(f, in), SK_VIZ_INPUTS);
    CHECK(!in[0].current && !in[1].current && in[2].current);
    CHECK(in[1].available && in[2].available);
    CHECK_STR(in[1].label, "Sound output");
    /* A pick of input is this window's record. */
    const char *why = NULL;
    CHECK(!sk_vizfeed_select_input(f, "radio", &why));
    CHECK(why != NULL);
    CHECK(sk_vizfeed_select_input(f, "session", &why));
    char *spec = record_of(id, 4242);
    CHECK_STR(spec, "session");
    free(spec);
    sk_vizfeed_free(f);
    rig_forget(&r);

    /* The next launch reads the newest: this one's. */
    c.pid = 4343;
    f = sk_vizfeed_new(&c);
    CHECK_INT(sk_vizfeed_input(f), SK_VIZ_SRC_SESSION);
    sk_vizfeed_free(f);

    /* With no recorder, the sound inputs are unavailable, and a record
     * of one starts on the sessions. */
    CHECK_INT(sk_last_write(id, 4242, "sound", 1e13), 0);
    c.recorders = NULL;
    c.n_recorders = 0;
#ifdef __APPLE__
    f = sk_vizfeed_new(&c);
    CHECK_INT(sk_vizfeed_input(f), SK_VIZ_SRC_SESSION);
    sk_vizfeed_inputs(f, in);
    CHECK(in[0].available && !in[1].available && !in[2].available);
    CHECK(!sk_vizfeed_select_input(f, "sound", &why));
    CHECK_STR(why, "the sound output is recorded on Linux only");
    sk_vizfeed_free(f);
#else
    f = sk_vizfeed_new(&c);
    CHECK_INT(sk_vizfeed_input(f), SK_VIZ_SRC_SOUND);
    sk_vizfeed_inputs(f, in);
    CHECK(in[1].available && in[2].available);
    sk_vizfeed_free(f);
#endif
    /* Without a home there is nowhere to record. */
    sk_env e = {0};
    e.os = SK_OS_LINUX;
    CHECK(sk_vizfeed_mode_dir(&e) == NULL);
    e.home = "/home/u";
    char *d = sk_vizfeed_mode_dir(&e);
    CHECK_STR(d, "/home/u/.local/state/claude-skins/last-viz");
    free(d);
    d = sk_vizfeed_input_dir(&e);
    CHECK_STR(d, "/home/u/.local/state/claude-skins/last-viz-input");
    free(d);
    free(md);
    free(id);
    sk_loop_free(loop);
}

/* ---- The sound output ---- */

static void put_script(const char *dir, const char *name, const char *body)
{
    char *p = sk_path_join(dir, name);
    FILE *fp = fopen(p, "w");
    CHECK(fp != NULL);
    if (fp) {
        fprintf(fp, "#!/bin/sh\n%s", body);
        fclose(fp);
    }
    chmod(p, 0755);
    free(p);
}

/* Runs the loop until the sound output is in state, or 3 s. */
static bool run_until(sk_loop *loop, sk_vizfeed *f, int state)
{
    uint64_t end = sk_now_ms() + 3000;
    while (sk_vizfeed_sound(f, NULL) != state && sk_now_ms() < end)
        sk_loop_once(loop, 20);
    return sk_vizfeed_sound(f, NULL) == state;
}

/* PATH for one test, and back. */
static char *path_swap(const char *path)
{
    const char *old = getenv("PATH");
    char *keep = sk_xstrdup(old ? old : "");
    setenv("PATH", path, 1);
    return keep;
}

static void path_back(char *keep)
{
    setenv("PATH", keep, 1);
    free(keep);
}

static sk_vizfeed *sound_feed(rig *r, sk_loop *loop)
{
    memset(r, 0, sizeof *r);
    sk_vizfeed_config c = {0};
    c.loop = loop;
    c.manual = true;
    c.recorders = sk_viz_recorders_linux;
    c.n_recorders = SK_VIZ_RECORDERS_LINUX;
    c.frame = keep_frame;
    c.log = keep_log;
    c.ud = r;
    return sk_vizfeed_new(&c);
}

static void test_the_sound_output_from_a_fake_parec(void)
{
    char *bin = dir_in("bin");
    mkdir(bin, 0700);
    char *args = sk_path_join(bin, "args");
    sk_buf body = {0};
    /* 0.5, -0.5, then a sample split across two writes: 0x1234. */
    sk_buf_printf(&body,
                  "printf '%%s\\n' \"$@\" > '%s'\n"
                  "printf '\\000\\100\\000\\300\\064'\n"
                  "/bin/sleep 0.2\n"
                  "printf '\\022'\n"
                  "exec /bin/sleep 30\n",
                  args);
    put_script(bin, "parec", body.ptr);
    sk_buf_free(&body);
    char *keep = path_swap(bin);
    sk_loop *loop = sk_loop_new();
    rig r;
    sk_vizfeed *f = sound_feed(&r, loop);
    sk_vizfeed_tab tabs[] = {{1, NULL, true, true}};
    sk_vizfeed_tabs(f, tabs, 1);
    CHECK_INT(sk_vizfeed_sound(f, NULL), SK_VIZFEED_SOUND_OFF);
    /* Not recorded while the window is closed. */
    CHECK(sk_vizfeed_select_input(f, "sound", NULL));
    CHECK(sk_vizfeed_is_open(f));
    CHECK_INT(sk_vizfeed_sound(f, NULL), SK_VIZFEED_SOUND_STARTING);
    sk_vizfeed_tick(f, 10);
    sk_vizfeed_msg *m = last_msg(&r);
    CHECK(m && m->frame.st == SK_VIZ_SOUND_NONE && m->frame.n_samples == 0 && m->frame.src == SK_VIZ_SRC_SOUND);
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_LIVE));
    uint64_t end = sk_now_ms() + 2000;
    float s[SK_SYNTH_N];
    do {
        sk_loop_once(loop, 20);
        sk_vizfeed_samples(f, s);
    } while (s[SK_SYNTH_N - 1] == -0.5f && sk_now_ms() < end);
    CHECK(s[SK_SYNTH_N - 3] == 0.5f);
    CHECK(s[SK_SYNTH_N - 2] == -0.5f);
    CHECK(s[SK_SYNTH_N - 1] == (float)0x1234 / 32768.0f);
    CHECK(s[0] == 0);
    sk_vizfeed_tick(f, 20);
    m = last_msg(&r);
    CHECK(m && m->frame.st == SK_VIZ_SOUND_LIVE && m->frame.n_samples == SK_SYNTH_N);
    if (m && m->frame.n_samples == SK_SYNTH_N)
        CHECK(m->frame.samples[SK_SYNTH_N - 3] == 0.5f);
    char *said = sk_test_read(args, NULL);
    CHECK_STR(said, "--format=s16le\n--rate=44100\n--channels=1\n--latency-msec=20\n-d\n@DEFAULT_MONITOR@\n");
    free(said);

    /* Closed, the recorder is stopped; the session alone stops it too. */
    sk_vizfeed_close(f);
    CHECK_INT(sk_vizfeed_sound(f, NULL), SK_VIZFEED_SOUND_OFF);
    sk_vizfeed_open(f);
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_LIVE));
    CHECK(sk_vizfeed_select_input(f, "session", NULL));
    CHECK_INT(sk_vizfeed_sound(f, NULL), SK_VIZFEED_SOUND_OFF);
    sk_vizfeed_tick(f, 30);
    m = last_msg(&r);
    CHECK(m && m->frame.n_samples == 0);
    /* Both kinds of input take it. */
    CHECK(sk_vizfeed_select_input(f, "both", NULL));
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_LIVE));
    feed_close(f, &r);
    sk_loop_free(loop);
    path_back(keep);
    free(args);
    free(bin);
}

static void test_a_recorder_that_fails_and_the_next(void)
{
    char *bin = dir_in("bin");
    mkdir(bin, 0700);
    put_script(bin, "parec", "echo 'Connection failure: refused' >&2\nexit 3\n");
    char *keep = path_swap(bin);
    sk_loop *loop = sk_loop_new();
    rig r;
    sk_vizfeed *f = sound_feed(&r, loop);
    CHECK(sk_vizfeed_select_input(f, "sound", NULL));
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_FAILED));
    const char *why = NULL;
    sk_vizfeed_sound(f, &why);
    CHECK_STR(why, "parec: exited 3: Connection failure: refused; pw-record: not found");
    CHECK_INT(r.logs.len, 1);
    if (r.logs.len)
        CHECK_STR(r.logs.ptr[0], "sound output: parec: exited 3: Connection failure: refused; pw-record: not found");
    sk_vizfeed_tick(f, 10);
    sk_vizfeed_msg *m = last_msg(&r);
    CHECK(m && m->frame.st == SK_VIZ_SOUND_FAILED);
    /* Failed stays failed while the window stays open on it; another
     * input clears it. */
    sk_vizfeed_close(f);
    CHECK_INT(sk_vizfeed_sound(f, NULL), SK_VIZFEED_SOUND_FAILED);

    /* The next recorder, when the first fails before a sample. One that
     * dies after its samples is not replaced. */
    put_script(bin, "pw-record", "printf '\\000\\100'\nexec /bin/sleep 0.3\n");
    CHECK(sk_vizfeed_select_input(f, "both", NULL));
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_LIVE));
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_FAILED));
    sk_vizfeed_sound(f, &why);
    CHECK_STR(why, "pw-record: exited 0");
    feed_close(f, &r);
    sk_loop_free(loop);
    path_back(keep);
    free(bin);
}

static void test_the_recorder_is_ended_with_the_feed(void)
{
    char *bin = dir_in("bin");
    mkdir(bin, 0700);
    char *pidfile = sk_path_join(bin, "pid");
    sk_buf body = {0};
    sk_buf_printf(&body, "echo $$ > '%s'\nprintf '\\000\\000'\nexec /bin/sleep 30\n", pidfile);
    put_script(bin, "parec", body.ptr);
    sk_buf_free(&body);
    char *keep = path_swap(bin);
    sk_loop *loop = sk_loop_new();
    rig r;
    sk_vizfeed *f = sound_feed(&r, loop);
    CHECK(sk_vizfeed_select_input(f, "sound", NULL));
    CHECK(run_until(loop, f, SK_VIZFEED_SOUND_LIVE));
    char *text = sk_test_read(pidfile, NULL);
    pid_t pid = text ? (pid_t)atol(text) : 0;
    free(text);
    CHECK(pid > 0 && kill(pid, 0) == 0);
    feed_close(f, &r);
    /* Reaped by the feed: no such process now. */
    CHECK(pid > 0 && kill(pid, 0) != 0 && errno == ESRCH);
    sk_loop_free(loop);
    path_back(keep);
    free(pidfile);
    free(bin);
}

const sk_test sk_suite_vizfeed[] = {
    T(test_a_frame_from_each_tab_s_bus),
    T(test_each_pane_s_mode_and_its_drop_down),
    T(test_the_records_of_the_last_mode_and_input),
    T(test_the_sound_output_from_a_fake_parec),
    T(test_a_recorder_that_fails_and_the_next),
    T(test_the_recorder_is_ended_with_the_feed),
    {0},
};
