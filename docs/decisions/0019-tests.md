# 0019 Tests and dev tools

Status: accepted 2026-10-11. Revised 2026-10-15: the live check suite
and its pass record. Revised 2026-10-17 for the C port (decision 0036):
the C suite replaces `npm test`, and the live checks, the driver and the
pass record went with the JavaScript app.

## Context

Tests only where they pay. The C port must also show that it does what
the JavaScript app did, from the bytes a parser takes to the pixels a
skin draws.

## Decision

- `make test` builds `build/release/tests/run` and runs it from the
  repository root, with `SK_SKINNED_BIN`, `SK_RELAY_BIN` and
  `SK_SKINC_BIN` naming the programs just built. `tests/run.c` runs
  every suite that `tests/suites.h` lists, once each, or those whose
  `suite.test` name begins with `T`, as in `make test T=json`. A test
  gets a fresh directory, removed with it. A failed check prints its
  file and line, and the test goes on. Nothing in the suite is Python or
  JavaScript.
- Fixtures live in `tests/fixtures/`: `claude/` (statusline JSON, hook
  payloads, a reduced transcript), `codex/` (a reduced rollout and its
  limits), `skins/` (a skin of every widget, a hostile one, and broken
  skins that each fail with the expected file and field), and `term/`
  (vim and less recorded on a real pty by `skinned --record`, with
  their resizes).
- What the suites cover, by folder of `tests/`:
  - `base/`, `core/`: JSON, HTTP, SHA-256, base64, UTF-8, the paths of
    decision 0036, the asset manifest; the loop; the metric writer
    (`core/bus.c`) on a fake clock: each rate, history dropped, defaults
    absent before `begin`, the reset, the name check, `close`; the tail;
    the Session (`core/session.c`) on real ptys: exit statuses, the kill
    policy and its grace, bytes, input and environment. Every Session
    test runs to its end, then kills the child's group, since a child
    that ignores SIGHUP outlives the test otherwise.
  - `agents/`, `relay/`, `sessions/`: the adapter seam (`agents/agent.c`):
    the dispatch and label rows, the failure rule, a close during
    prepare, the order of operations, Generic on a fake clock; the
    Claude parser, session core, settings writer on a temp profile, git
    root and runDir, the compaction rule and the adapter on its socket;
    the Codex parser, discovery and thread names; `skinned-relay`
    against a socket server in the test; the sessions, profiles, history
    and boards of decisions 0027 to 0029.
  - `term/`, `ui/`, `text/`, `gfx/`: the terminal, including xterm.js
    6.0.0's own parser, buffer, input, selection and terminal tests
    ported onto it (the `x*.c` files), and the recorded sessions
    replayed whole and in pieces of 7 bytes; the drawing, the fonts, the
    font checker and the glyph cache.
  - `skinc/`, `skin/`: the lexer, parser, selectors, values and cascade;
    the validator over `tests/fixtures/skins/`, with validate.js's
    messages in its order; the splits; the shipped skins through the
    front end; the pack loader and its checks; the compile and cache of
    a skin opened by path; the layout, the paint, the blur and the
    runtime.
  - `app/`, `platform/`: the options, the identity strip's text, the
    runtime dirs, the settings, the tabs, monitor and dashboard tabs,
    New tab..., the menus, the last skin, the visualization's feed and
    window, the tab strip's bands, `skinned --headless` end to end on
    `tools/fake/claude`; the process tree, its fds and the per-pid
    info; X11's wire protocol against a fake server
    (`platform/x11_fake.c`); AppKit on hidden windows, on macOS.
  - `audio/`, `viz/`: IMA ADPCM, the mixer, the skin's player, the
    output through a null sink, so nothing reaches a speaker; the
    synthesiser, the pane layout, Go's rules, the physics of Gravity,
    Gravity 3D, Quantum and Carbon, and each mode's frames.
- Golden images: a test draws into memory and compares the image with
  `tests/golden/NAME.qoi` (`tests/golden.c`). A pixel may miss each
  channel by the test's tolerance, since float rounding may differ
  between machines. A whole skin may miss by more than 24 in 300 pixels
  per million, for its blurs, glyphs and resampled images. A miss
  writes what was drawn to `build/golden-out/NAME.qoi`, which
  `build/release/qoi2png` turns into a PNG to look at.
  `SK_GOLDEN_UPDATE=1` writes the golden images instead, once a change
  of picture is meant. The terminal's goldens are text, in
  `tests/golden/term/`.
- The window checks, `tests/app/window.c`, run the real `skinned` on a
  hidden window (`SKINNED_HIDDEN=1`) and drive it through its control
  socket: with `SKINNED_CONTROL=PATH` the app listens on a Unix socket
  at PATH, mode 0600, for one JSON request a line (`src/app/control.h`):
  keys, typed text, the mouse, the wheel, a menu, focus, a close, a
  hangup, a screenshot, and the probe. The probe answers each window's
  menu, skin, terminal screen, selection and visualization panes, and
  every tab's id, kind, pid, label, name, folder and command line. The
  checks type, press the app's chords, select a word, find a link, drag
  tabs along the strip and out of the window, use the menus, the
  sessions dashboard, rename and New tab..., show the skin, settle
  skins raced at launch, and read the exit status. They need a display:
  the window server on macOS, `$DISPLAY` on Linux; without one they
  print a skip line. The live checks of `scripts/checks/` and their
  pass record are gone: what of them the C app runs is here, in `make
  test`.
- `make debug` runs the same tests built with AddressSanitizer and
  UBSan, in `build/debug/`, every UBSan finding fatal.
- `make fuzz-run` runs each parser's fuzz target for `FUZZ_SECONDS`
  (default 30) from its corpus in `tests/fuzz/corpus/`: JSON, HTTP,
  UTF-8, base64, the Claude and Codex parsers, text, paths, links, the
  terminal, CSS, PNG, JPEG, SVG, Vorbis, the font checker, ADPCM and
  packs. The mutator is in the tree (`tests/fuzz/driver.c`), since
  Apple's tools ship no libFuzzer. Under clang, coverage from
  `-fsanitize-coverage=trace-pc-guard` guides it, and each input that
  reaches a new edge joins the corpus; under gcc it mutates blind. A
  crash, a sanitizer report or a hang over 5 s writes the input to
  `crash-<hash>` or `hang-<hash>`, and `fuzz-NAME -x FILE` runs it
  again.
- CI (`.github/workflows/c.yml`) runs on Linux with gcc and with clang,
  and on macOS, with no third-party action: the build, `install.sh` and
  `uninstall.sh` into a folder of the runner's, `make test`, `make
  debug`, and on clang and macOS 30 s of `make fuzz-run`. On Linux the
  tests run under Xvfb, so the window checks have a display.
- `tools/fake/claude`: a replay fake named `claude`, in POSIX sh, so the
  app injects its settings. It finds the injected `--settings`, plays
  the hook payloads of `tests/fixtures/claude/hooks.jsonl` through the
  hook command and `statusline.json` through the statusLine command,
  both naming a copy of the reduced transcript, then exits. It spends no
  tokens. `FAKE_CLAUDE_PAUSE` sets the seconds between events.
- `tools/try-relays`: the relays on a real Claude Code session, with
  `hookdump` standing in for the window and every post logged.
- `skinned --headless[=FILE]` runs the tabs with no window, in the
  terminal it runs in, writing the active tab's meters once a second as
  JSON lines; `--record=FILE` keeps the first tab's pty output for the
  terminal's tests.
- Tools to look with, none part of `make`: `make skinshot` (a shipped
  skin laid out and painted at 1x and 2x), `make skinrun` (a shipped
  skin live in a window on a scripted session), `make vizshot` and
  `make vizwin` (a visualizer mode, and the visualization's window on
  fake tabs), `make milkshot` (a MilkDrop preset's frames).

## Why

Parsers and the validator have exact inputs and outputs, where tests are
cheap and catch regressions. The port must draw what the JavaScript app
drew, so its pictures are pinned by golden images, with a tolerance for
the rounding that differs between machines. The window checks drive the
real program through a socket of its own, where the JavaScript app's
live checks needed Playwright and Chromium. The sanitizers and the fuzz
targets watch the memory that C leaves to the programmer, above all in
what reads untrusted bytes.

## Rejected

- Pixel-diff tests of skins without a tolerance: brittle. The
  JavaScript app judged the look by eye, which a port cannot.
- Tests of the widget animation maths: seen on frame sequences instead.
- libFuzzer: Apple's tools ship none.
