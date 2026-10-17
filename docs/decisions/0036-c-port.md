# 0036 The C port

Status: accepted 2026-10-16. The cutover is done (2026-10-17): the C app
is the app, and the JavaScript app of decision 0001 is in the
repository's history.

## Context

The maintainer asked for skindeep to be rewritten in C, with standard
libraries, on Linux and macOS, with no Python and no JavaScript run. A
review put the work at about 79,000–106,000 lines of C. Most of it is
the platform beneath the app, which Chromium, xterm.js, node-pty and
butterchurn supply today.

## Decision

- Shared code is ISO C17 and POSIX.1-2008 with its XSI option
  (`_XOPEN_SOURCE=700`, which the pty functions need), and nothing
  else. Code for one operating system lives under
  `src/platform/<os>/`.
- Linux and macOS. On Linux the app speaks the X11 wire protocol over a
  socket, without Xlib; Wayland desktops run it under XWayland. On
  macOS it uses Apple's system frameworks only (AppKit, CoreGraphics,
  AudioToolbox, libproc, sysctl), called from C through the Objective-C
  runtime. XQuartz is never used.
- Three programs. `skinned` is the app. `skinned-relay` replaces
  `app/relay/hook.py` and `app/relay/statusline.js`. `skinc` compiles a
  skin into a pack.
- Skins stay `skin.json` plus `skin.css`. `skinc` resolves the CSS once,
  for every state a widget can reach. It pre-renders the static layers
  at 1x and 2x and decodes the images and sounds. The app composites
  packs and never interprets CSS. The four shipped skins are compiled
  during the build and linked in as generated C arrays. A skin passed
  with `--skin` is compiled when it first loads, then cached.
- Vendored code: single-file libraries whose licence is free to use
  (public domain or the Unlicense, MIT, BSD, ISC, zlib). Each lives in
  `third_party/<library>/` beside its licence and is listed in
  `ASSETS.json` (decision 0017). Today these are stb_truetype,
  stb_image, stb_vorbis, nanosvg, nanosvgrast and qoi.
  - stb_truetype does no range checking, so it reads only the embedded
    fonts, the system's fonts, and skin fonts `skinc` has checked.
  - `skinc` accepts only TrueType outlines in skins.
- HTTPS goes through the `curl` command, run as a child. libcurl is not
  linked, because its licence is curl's own, not MIT.
- No Python and no JavaScript in the C tree, its build or its tests, and
  since the cutover no shell script: the fake CLIs, the live checks and
  the install are C and the makefile (decision 0018).
- The build is `make` with the system C compiler. The makefile uses only
  what GNU make has had since 3.81, the version macOS ships as
  `/usr/bin/make`; Linux distributions ship GNU make as `make`.
- A core thread owns every file descriptor in one `poll()` loop. A UI
  thread owns the windows. On macOS, AppKit's run loop holds the main
  thread.
- DejaVu Sans and DejaVu Sans Mono are embedded in `skinned`, since
  macOS does not ship them. This revises decision 0017.
- Input methods come first on macOS; XIM on Linux comes later. Speaker
  capture is Linux-only at first. macOS takes Cmd chords, and Linux
  keeps the Ctrl+Shift chords.
- State uses the directories of decision 0016, under the name
  `claude-skins`. On macOS, unless the XDG variables are set, these are
  `~/Library/Application Support/claude-skins` for state,
  `~/Library/Caches/claude-skins` for cache, and `$TMPDIR` for the
  runtime root.
- The work runs in nine phases. Phase 4 is the gate: if the terminal
  cannot render Claude Code and Codex exactly, the port stops there.
  0. Foundations.
  1. The relays.
  2. A headless core.
  3. Window, drawing and text.
  4. The terminal.
  5. `skinc`.
  6. The skin runtime and the app's UI.
  7. The visualizer.
  8. The cutover, which removes `app/`, `test/` and `package.json`.

  The C tree lived beside the JavaScript app until then. Comments in
  the C tree still name the JavaScript files each part was ported from
  (`app/...js`): they are in the history before the cutover's commit,
  "Cutover: the JavaScript and Python app removed".
- The test targets:
  - `make test` runs the C tests, using the fixtures in `tests/fixtures/`.
  - `make debug` runs the same tests under AddressSanitizer and UBSan.
  - `make fuzz-run` drives every parser with an in-tree,
    coverage-guided mutator, since Apple's tools ship no libFuzzer.

  CI runs all three on Linux and macOS, with no third-party actions.

## Why

- The maintainer set C with POSIX as the floor. Strict ISO C cannot open
  a window, a pty or a socket.
- Compiling skins keeps CSS for skin authors. It keeps a CSS engine and
  the image and sound decoders out of the running app, so untrusted
  files are parsed only by `skinc`.
- The single-file libraries save about 8,000–11,000 lines with no
  package manager and no install step.
- macOS offers no way to a window but its own frameworks. XQuartz is a
  separate install with poor Retina support.

## Rejected

- Strict ISO C17 alone: no windows, ptys, sockets or directory
  listings.
- A CSS engine in the running app: a browser's incremental restyle and
  relayout, run on the CPU every frame.
- A fixed skin drawing model: it would break the four skins and the
  format `docs/SKINS.md` documents.
- libcurl: its licence is not MIT.
- XQuartz.
- GTK, VTE, cairo and other system libraries on Linux: they fall
  outside the rule of standard libraries.
- CMake, and any build tool written in Python.
