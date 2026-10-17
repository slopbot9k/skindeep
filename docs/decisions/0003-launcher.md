# 0003 Launcher, commands, exit status, stderr

Status: accepted 2026-10-11, revised 2026-10-12 for portable commands,
for the Session's exit status, for tabs (decision 0023), for `--mute`
(decision 0024), and for the one exec route through `exec-clean.py`,
and revised 2026-10-17 for the C port (decision 0036).

## Context

A user types `claude-skins` or `codex-skins` where they would type
`claude` or `codex`. Arguments pass through, cwd is the caller's, the app
exits with the CLI's status. The app's own log lines must not land in
the user's shell.

## Decision

- `skinned [--skin NAME|PATH] [--statusline=always|chain-only]
  [--no-viz] [--mute] -- CMD [ARGS]`, the program `make` builds,
  installed as a link to it (decision 0018). Run by a name with no
  slash, it finds its own file on PATH, so it finds `skinned-relay` and
  the checkout's `skins/` beside it. It checks its own options (`src/app/options.c`): an unknown option of its own, or no
  command after `--`, prints the usage and exits 125.
- User-facing errors (bad skin, missing command) go to the caller's
  stderr, each line starting `skinned: `. The app's own lines go to its
  log, `<state>/<stamp>-<pid>.log` (decision 0016). When the caller
  leaves its stderr open on fd 3, as the JavaScript app's launcher did,
  `skinned` writes the user's errors there instead, and no child
  inherits it.
- Exit status: the child's status, of the tab that ends last when the
  window has several (decision 0023). 128+n when the child dies by
  signal n. 127 when CMD is not found on PATH, checked before any window
  opens. 125 when the app itself fails (bad skin at launch, a window
  that cannot open, a directory it cannot make). An internal error ends
  the child first, by the kill policy of 0016, then exits 125. 125 too
  when the pty cannot be spawned, with `cannot start CMD: <reason>` on
  stderr.
- 127 or 126 when CMD resolved but its exec fails, as bash gives: 127
  when the file or the interpreter it names is missing, else 126. The
  reason goes to stderr as `cannot start CMD: <reason>`, and to the
  problem a tab's end shows when other tabs remain.
- `skinned` forks and execs every CLI itself (`src/core/session.c`), as
  `app/exec-clean.py` did for the JavaScript app. The child resets its
  signals, takes the pty as its controlling terminal, keeps only fds 0
  to 2 and execs. A failure before the exec, or at it, comes back as an
  errno over a close-on-exec socket pair, so the spawn is over, exec'd
  or failed, before the app goes on. No interpreter is needed.
- 129 when the window is closed, or the app is signalled, before the CLI
  started: the status a hung-up child usually reports, so the caller
  sees the same status for a close before the start as after it.
- The first decided status stands. An app error after the child's exit
  is logged, and the child's status is reported: the CLI's work is done
  and its status is known.
- `claude-skins` and `codex-skins` are links to `skinned`, which, run by
  either name, runs as `skinned -- claude ARGS` or `skinned -- codex
  ARGS` (`src/main/skinned.c`). Every argument goes to the CLI, so no
  option of the app can shadow one of the CLI's. `SKINNED_SKIN` picks
  the skin. A session on another config dir sets `CLAUDE_CONFIG_DIR` or
  `CODEX_HOME`, as it would for the bare CLI, and New tab... sets it per
  tab (decision 0025).
- The working directory is inherited, never changed.

## Why

A log of the app's own keeps the user's terminal clean while errors
meant for the user still reach it. In the JavaScript app, fd 3 did the
same against Chromium's noise. The 125/127/128+n convention is env(1)'s
and the shell's, so it cannot collide with a CLI's own status in the
ordinary range. Exec'ing the child itself gives each failed exec bash's
status and wording, where node-pty ended every one with status 1 and its
own message, and its child kept Electron's fds.

## Rejected

- A `--label` option: free text in the identity strip could lie
  (decision 0004).
- Printing the app's log lines on stderr: they would mix with the
  user's shell.
- Exit 2 for a bad skin: collides with CLI statuses.
- Generated wrappers per profile directory: they assume one machine's
  layout of config dirs, where the environment variable each CLI already
  reads works everywhere.
