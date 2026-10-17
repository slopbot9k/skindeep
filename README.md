# skindeep

Skindeep Knickknacks, Instruments, and Needles, Delivering Essentially Equivalent Performance.

[**Read the dev diary**](https://b-g-goodell.github.io/skindeep/): one
entry per chapter, in plain words, with a picture. The latest:

[![Nine font names on a chalkboard with one circled, beside the front page shrinking](docs/diary/heroes/23-chalk-and-readme.png)](docs/diary/23-chalk-and-readme.md)

[Chapter 23: Chalk, and a shorter front page](docs/diary/23-chalk-and-readme.md)

Winamp-style skins for Claude Code and Codex on Linux and macOS. Your agent runs
as always in a real terminal, while the skin around it shows gauges,
needles and lamps that move with what the agent is doing, and a second
window draws a visualizer from it. Its commands are `claude-skins`,
`codex-skins` and `skinned`.

![skindeep running Claude Code in the deck skin: an answer in the first of two tabs, its task list beside it, the usage limits at the top right, a trace of each tab in the tab strip, and the visualization window with a pane per tab](docs/screenshot.png)

The gauges show how full the context is, how near auto-compact is, the
session's cost, your usage limits, and the tools and helpers the agent
runs. The app never types into the terminal, changes its output, calls
a model or edits your settings, and a strip at the top that no skin can
cover names the running command and its folder.

## Install

You need Linux with a desktop (X11, or Wayland through XWayland) or
macOS, `git`, `claude` or `codex` on PATH, a C compiler and GNU make: on
macOS, Apple's Command Line Tools. No Node.js, Python or fonts: the
DejaVu fonts are built in. On Linux, skin sounds play through `pw-play`,
`paplay` or `aplay`, and to draw the visualizer from your speakers, add
`parec` (`pulseaudio-utils`) or `pw-record` (`pipewire-bin`).

    git clone https://github.com/b-g-goodell/skindeep.git
    cd skindeep
    make install

It builds `skinned`, `skinned-relay` and `skinc`, links `skinned`,
`claude-skins` and `codex-skins` into `~/.local/bin`, or into `BINDIR`
if you set it (`make install BINDIR=...`), and names the optional tools
it did not find. Run it again after each `git pull`, and
`make uninstall` removes the links, as
[decision 0018](docs/decisions/0018-install.md) explains. Or tell your
agent:

> Install skindeep from https://github.com/b-g-goodell/skindeep
> by its README, running make install until it succeeds.

## Use

Type `claude-skins` for `claude` and `codex-skins` for `codex`: every
argument passes through, as in `claude-skins --resume`, and the command
exits with the CLI's own status. `SKINNED_SKIN=phosphor claude-skins`
picks a skin, and a profile set by `CLAUDE_CONFIG_DIR` or `CODEX_HOME`
works as usual, with its name in the window title.

The three bars at the top left open four menus in a bar: **File** for
the tabs, Settings and Housekeeping, **Agents** for Agent sessions and
Sessions dashboard, which watch every Claude Code and Codex session on
your machine, **Skins** for Open skin... and the skins, and
**Visualization** for its window, mode and input and Mute skin sounds.
A click on a title, or Left and Right, moves between them, and Escape
closes them.

On Linux, Ctrl+Shift+T opens a tab, Ctrl+Shift+W closes one and
Ctrl+Tab moves between them. Ctrl+Shift+C copies, Ctrl+Shift+V pastes
and Ctrl+click opens a link, while a bare Ctrl+V goes to the CLI. You
can drag a tab out to give it its own window, or right click it to
rename it. On macOS the chords take Cmd: Cmd+T, Cmd+W, Cmd+C,
Cmd+V and Cmd+click, with Cmd+Shift+] and Cmd+Shift+[ beside Ctrl+Tab,
and Cmd+Q quits. There every Ctrl key but Ctrl+Tab and Ctrl+Shift+Tab
goes to the CLI. The [decisions](docs/decisions/) say how each part
behaves.

`skinned` runs any command in a skin, as in `skinned -- htop`:

    skinned [--skin NAME|PATH] [--statusline=always|chain-only] [--no-viz] [--mute] -- COMMAND [ARGS...]

Any command gets CPU, memory and output meters, `claude` and `codex`
get the rest, and each launch writes a log, `<stamp>-<pid>.log`, to
`~/.local/state/claude-skins/` on Linux and
`~/Library/Application Support/claude-skins/` on macOS, or to
`$XDG_STATE_HOME/claude-skins/` where that is set.

## Where the meters come from

The meters, listed in [decision 0011](docs/decisions/0011-metric-vocabulary.md),
come from Claude Code's statusline, hooks and transcript, from Codex's
session file, and for any other command from the terminal and the
process table: `/proc` on Linux, libproc on macOS.
For Claude Code, the app adds `--settings <file>` to the command line,
and your own statusline still runs. Claude Code hides its shortcuts
hint under a statusline, so to keep the hint, run
`skinned --statusline=chain-only -- claude`, which blanks cost, limits
and the cache ratio.

## Visualization

The visualizer turns each tab's session into a made-up sound, so output
thumps, tool calls ring and the picture calms when the agent idles.
With two tabs or more, each tab draws its own small trace of it in the
tab strip: a moving band while its agent works, a flat green line once
it is done.
On Linux, **Visualization input** in the Visualization menu switches it
to what your speakers play, and it never records a microphone. Each
pane has a drop-down at its top left to pick its mode, as the menu's
Visualization mode does for the active tab's pane, and with the
window clicked, space steps through them all, six of which
[docs/viz-modes.png](docs/viz-modes.png) shows.
A click goes to the mode: a drag in Quantum raises a barrier, a click
in Go plays a stone, and a click on Carbon's level diagram excites the
atom to that level.

## Skins

| deck | phosphor |
| --- | --- |
| ![deck, with the task list in its playlist area](docs/skins/deck.png) | ![phosphor](docs/skins/phosphor.png) |
| **library** | **coffee** |
| ![library](docs/skins/library.png) | ![coffee, with the task list on its slate and a jar per usage limit](docs/skins/coffee.png) |

Drag the edge between the terminal and a panel beside it to give the
panel more room or less. The skin keeps the size, and **Reset panel
sizes** in the File menu's Settings brings back its own.

A plain `claude-skins` opens the skin you used last, and you can mute
its sounds from the Visualization menu or start muted with
`skinned --mute` or `SKINNED_MUTE=1`. To make your own, read
[docs/SKINS.md](docs/SKINS.md), pass its folder with `--skin`, which
compiles it with `skinc` and compiles it again as you edit it, and try
it on the fake agent in the checkout, which replays a recorded session
through the real hooks and statusline without spending tokens:

    skinned --skin ~/my-skin -- build/release/fake/claude

## Known limits

- Two Codex sessions started in one folder within 2 s may swap meters
  ([decision 0013](docs/decisions/0013-codex-channel.md)).
- Claude Code's `/context` breakdown has no channel, so the context
  gauge shows cached and uncached tokens instead.
- On Linux there is no input method yet, so a dead key types nothing
  and the key after it types its own character. XIM comes later
  ([decision 0036](docs/decisions/0036-c-port.md)); macOS has its input
  methods.
- The visualizer draws from your speakers on Linux only, for now.

## Development

`make` builds the programs and the tests into `build/release/`,
`make test` runs the tests, `make debug` runs them under
AddressSanitizer and UBSan, and `make fuzz-run` fuzzes every parser for
30 s each. The live checks of the window, in `tests/app/window.c`, drive
`skinned` through its control socket and run in `make test` wherever
there is a display. `build/release/fake/claude`, which `make` builds
from `tools/fakeclaude.c`, stands in for Claude Code. The design is in
[docs/DESIGN.md](docs/DESIGN.md), how it was built, chapter by chapter,
in the [dev diary](docs/diary/README.md), and
[decision 0019](docs/decisions/0019-tests.md) says what the tests cover.

## License

The code is under the MIT license, in [LICENSE](LICENSE), and the fonts,
images and libraries keep their own, listed in `ASSETS.json`.

skindeep is an independent project, not affiliated with Anthropic,
OpenAI or the makers of Winamp.
