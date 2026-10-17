# skindeep design

Status: current, 2026-10-17, for the C app of decision 0036. User
documentation is `README.md`, the skin format `docs/SKINS.md`.

Contents: Goal, Architecture, Decisions.

## Goal

A Winamp-style skinnable GUI shell for terminal coding agents on Linux
and macOS. `claude-skins` and `codex-skins` open the real, unmodified
CLI in a skinned window with live animated gauges fed by side channels,
and a visualization window, in any of four sample skins, one of which
plays quiet sounds of its place.

## Architecture

Three programs, built by `make` from ISO C17 and POSIX:

    bin/claude-skins, bin/codex-skins   (sh: exec skinned -- claude|codex)
    bin/skinned   (sh: exec build/release/skinned)
        |
    skinned  (src/main/skinned.c)
        |-- core thread: one poll() loop (src/core/loop.c) owns every
        |   file descriptor: the ptys, the hook sockets, the followed
        |   files, the recorder and opener children, the control socket
        |     |-- tabs.c       every window's tabs: order, the active one,
        |     |                the app's end with the last tab's status
        |     |-- clitab.c     a CLI tab: its Session and agent
        |     |-- core/session.c  the CLI's fork, exec and pty, one kill
        |     |                policy, its exit status
        |     |-- agents/      the adapter seam (agent.c), one clock
        |     |                polling every part at 250 ms:
        |     |   |-- claude/  settings writer, hook and statusline
        |     |   |            receiver on the tab's Unix socket
        |     |   |            (server.c), transcript tail, session core,
        |     |   |            the compaction rule, carried options
        |     |   |-- codex/   rollout discovery and parser, thread names
        |     |   |-- generic.c  the process tree's CPU and memory, pty
        |     |   |            rates, idle
        |     |   `-- plain.c  the adapter of any other CLI
        |     |-- core/bus.c   per tab: named metrics (null = no data)
        |     |                and their one writer: sources, rates,
        |     |                defaults, reset
        |     |-- sessions/    the live agent sessions of this user, the
        |     |                profiles New tab... offers, the dashboard's
        |     |                history, each project's issue board
        |     |-- monitor.c, dashboard.c, foreign.c, newtab.c
        |     |                read-only monitor tabs, the dashboard tab,
        |     |                sessions started elsewhere, New tab...
        |     |-- skins.c      the shipped packs, a skin opened by path
        |     |                compiled by a skinc child and cached, the
        |     |                last skin used, the watch of its files
        |     `-- vizfeed.c    every tab's numbers and events, 30 a
        |                      second, the modes, the sound output's
        |                      recorder
        |
        `-- UI thread (on macOS the main thread, under AppKit's run
            loop): every window, drawn in software and presented
            (src/app/gui.c)
              |-- identity strip | tab strip from the second tab, each
              |   tab's visualization a small band (tabviz.c)
              |-- the skin: the pack laid out, painted and animated
              |   (src/skin/view.c), its state tables looked up as the
              |   metrics and the pointer change, the splits' handles
              |   and the menu button above it
              |-- one terminal per tab (src/term/), the active one drawn
              |   by src/ui/termview.c at the skin's terminal rect
              |-- the drop-down, a bar of four menus (menu.c), the New
              |   tab... dialog (chooser.c), the dashboard's pane
              |   (dashview.c), a tab's rename
              |-- the skin's declared sounds (src/audio/), one mixer for
              |   every window
              `-- viz window (vizwin.c), one pane per tab: the
                  synthesiser (src/viz/synth.c) -> the MilkDrop modes
                  (milk.c) or the drawn modes (scope, classic analyzer,
                  AVS ports, Fern, Go, Zoom out, Gravity, Gravity 3D,
                  Quantum, Carbon)

    skinned-relay  (src/relay/)
        hook ROOT                Claude Code's command hooks
        statusline ROOT CHAIN    its statusLine, which runs the
                                 profile's own statusline in parallel
        both post to <root>/pids/<pid of the tab's CLI>, found by
        ancestry, a link to the tab's Unix socket

    skinc  (skinc/)
        skinc DIR [-o PACK]      checks a skin, resolves its CSS for
                                 every state, pre-renders its static
                                 layers at 1x and 2x, decodes its
                                 images, fonts and sounds into a pack

    claude child  <-- --settings <state>/settings/<hash>.json
        statusLine: skinned-relay statusline <root> <chain>
        hooks:      skinned-relay hook <root>

The platform layers, `src/platform/<os>/`, hold what POSIX leaves to
each system:

- Windows: on Linux, X11's wire protocol over a socket, with no Xlib
  (`src/platform/x11/`); Wayland desktops run it under XWayland. On
  macOS, AppKit and CoreGraphics through the Objective-C runtime.
- The pty's controlling terminal and modes, the process tree, a
  process's fds, name, folder and start time (`/proc` on Linux, libproc
  and sysctl on macOS).
- Sound output: an AudioQueue on macOS, `pw-play`, `paplay` or `aplay`
  as a child on Linux. Speaker capture for the visualizer is `parec` or
  `pw-record` on Linux, and none yet on macOS.
- The folder picker and the one question asked before a window opens:
  AppKit on macOS, `zenity` or `kdialog` on Linux.

Beneath them all: `src/gfx/` draws, composites and strokes paths into
images; `src/text/` reads fonts with stb_truetype, DejaVu Sans and
DejaVu Sans Mono embedded, the system's fonts for fallback; `src/base/`
holds JSON, the HTTP/1.1 the hook socket speaks, SHA-256, base64, the
paths and the log. Vendored single-file libraries live in `third_party/`
(decision 0017).

Skin packs (decision 0037): `skinc` reads `skin.json` and `skin.css`
once. It checks the manifest, parses the CSS subset, builds the element
tree, and computes each element's style in every state its selectors
can reach. It pre-renders what never changes, and decodes the images to
QOI, the sounds to IMA ADPCM and keeps the fonts its checker passed. The
four shipped skins compile during the build and link into `skinned`. A
skin given by path compiles in a `skinc` child into
`<cache>/packs/<hash>.pack`. The app loads every pack through one
loader that checks every byte, and never interprets CSS.

Data flow: each tab's pty bytes go, on the core thread, into its
terminal unchanged. Side channels (statusline JSON, command hooks,
transcript JSONL, Codex rollout, the process table) feed the tab's bus.
The active tab's metrics go to the skin 10 times a second. Every tab's
numbers and events go 30 times a second to the viz window and to the
tab strip. Nothing flows back into the pty but the user's own keys,
pastes and mouse reports.

Directories (decision 0016): state under `$XDG_STATE_HOME/claude-skins`,
else `~/.local/state/claude-skins` on Linux and
`~/Library/Application Support/claude-skins` on macOS; cache under
`~/.cache/claude-skins` or `~/Library/Caches/claude-skins`; the
runtime root under `$XDG_RUNTIME_DIR`, else `$TMPDIR`.

## Decisions

| No. | Decision |
| --- | --- |
| [0001](decisions/0001-stack.md) | Stack and process model (superseded by 0036) |
| [0002](decisions/0002-terminal-fidelity.md) | Terminal fidelity |
| [0003](decisions/0003-launcher.md) | Launcher, commands, exit status, stderr |
| [0004](decisions/0004-identity.md) | Identity strip and window title |
| [0005](decisions/0005-skin-isolation.md) | Skin isolation: data, not code |
| [0006](decisions/0006-skin-layout.md) | Skin layout model and manifest shape |
| [0007](decisions/0007-widgets.md) | Widget set and animation |
| [0008](decisions/0008-skin-switching.md) | Finding and switching skins |
| [0009](decisions/0009-claude-channels.md) | Claude Code side channels |
| [0010](decisions/0010-statusline-relay-and-chain.md) | Statusline relay and chaining |
| [0011](decisions/0011-metric-vocabulary.md) | Metric vocabulary |
| [0012](decisions/0012-context-gauge.md) | Context gauge and distance to auto-compact |
| [0013](decisions/0013-codex-channel.md) | Codex side channel |
| [0014](decisions/0014-generic-meters.md) | CLI-agnostic meters |
| [0015](decisions/0015-visualization.md) | Visualization window |
| [0016](decisions/0016-runtime-lifecycle.md) | Runtime files and lifecycle |
| [0017](decisions/0017-assets.md) | Assets, fonts and their licenses |
| [0018](decisions/0018-install.md) | install.sh and uninstall.sh |
| [0019](decisions/0019-tests.md) | Tests and dev tools |
| [0020](decisions/0020-statusline-default.md) | Statusline injection default |
| [0021](decisions/0021-agent-adapter-seam.md) | Agent adapter seam |
| [0022](decisions/0022-file-menu.md) | The menus and the last skin used |
| [0023](decisions/0023-tabs.md) | Tabs in one window |
| [0024](decisions/0024-skin-sounds.md) | Skin sounds |
| [0025](decisions/0025-new-tab-chooser.md) | New tab... with another agent, profile or folder |
| [0026](decisions/0026-reopen-duplicate.md) | Reopen closed tab and Duplicate tab |
| [0027](decisions/0027-agent-sessions.md) | Agent sessions and the read-only monitor tab |
| [0028](decisions/0028-sessions-dashboard.md) | The sessions dashboard |
| [0029](decisions/0029-issue-board.md) | The issue board in the sessions dashboard |
| [0030](decisions/0030-background-sessions.md) | Background sessions, subcommands and the last screen |
| [0031](decisions/0031-fuzz.md) | Fuzz testing with written predictions |
| [0032](decisions/0032-rename.md) | A session's name in its tab, by /rename or a right click |
| [0033](decisions/0033-dials.md) | Readouts open dials on the sessions dashboard |
| [0034](decisions/0034-checklist.md) | The agent's task list in a list widget |
| [0035](decisions/0035-resizable-panels.md) | Resizable panels: a dragged boundary between the terminal and a panel |
| [0036](decisions/0036-c-port.md) | The C port: C17 and POSIX, Linux and macOS, skins compiled by skinc |
| [0037](decisions/0037-skin-packs.md) | Skin packs: CSS compiled once, per state |

Channel facts: `docs/research/side-channels.md`.
