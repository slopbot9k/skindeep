# 0016 Runtime files and lifecycle

Status: accepted 2026-10-11, revised 2026-10-12 for the Session, again
2026-10-12 for tabs (decision 0023), and for the hook links, the kept
settings and the last screen (decision 0030), and 2026-10-13 for the launch's
pages, and revised 2026-10-17 for the C port (decision 0036).

## Context

The app writes a generated settings file, listens on a hook socket per
tab, and keeps a log. Several instances may run at once. Closing the
window must end the session the way closing a terminal does.

## Decision

- The directories are decision 0036's, under the name `claude-skins`
  (`src/base/paths.c`). State is `$XDG_STATE_HOME/claude-skins/`, else
  `~/.local/state/claude-skins/` on Linux and
  `~/Library/Application Support/claude-skins/` on macOS. The runtime
  root is `$XDG_RUNTIME_DIR/claude-skins/`, else
  `$TMPDIR/claude-skins-<uid>/`, else `/tmp/claude-skins-<uid>/`; macOS
  sets no `XDG_RUNTIME_DIR`, so there it is under `$TMPDIR`.
- Runtime dir: `<runtime root>/<pid>/`, mode 0700, holding one dir per
  tab, `tab-<id>/`, mode 0700, for that tab's hook socket
  (`src/app/runtime.c`, `src/app/clitab.c`). A tab's socket is removed
  when the tab ends, the runtime dir on exit. At start, dirs whose pid
  is dead are removed.
- The launch's pages are gone with the JavaScript app, which served
  them from memory so that a window opened later ran the build its
  launch started with. `skinned` is one program with the shipped skins
  linked in, so every window of a launch runs the code the launch
  started with.
- The runtime root's `pids/`, mode 0700: one link per tab's CLI, named
  for its pid, to that tab's socket (decision 0030). The tab's end
  removes it, and at start a link whose socket is gone is removed.
- The generated Claude settings live in `<state>/settings/`, kept: a
  session sent to the background outlives the window and starts again
  from them (decision 0030).
- Log: `<state>/skinned-<pid>.log`, the app's own lines, stamped in UTC
  (`src/base/log.c`). The newest 10 are kept.
- One kill policy, `src/core/session.c`, for a close of a tab or of the
  main window, SIGINT, SIGTERM or SIGHUP to the app, and an internal
  error: SIGHUP to each child's process group (the recorded pid, a
  session leader since the child calls `setsid` before its exec), wait
  up to 2 s, then SIGKILL of that group. The app exits once the last
  child is reaped, with the status of decision 0023.
- Child exit: the tab closes, after its last screen is read when the
  child exited by itself (at most 1 s). The last tab's child exit ends
  the app, its last screen written to the app's stdout when that is a
  terminal. A Claude tab whose CLI refused to open a background session
  in place is followed by `claude attach ID` in its place (decision
  0030).
- The app never exits while any tab's child lives.
- The Session closes its tab's agent adapter (decision 0021) at every
  end, so no hook socket, tail or timer outlives the session.
- A close before the CLI started ends the app without starting it.
- The viz window closes alone, and the menu reopens it.

## Why

SIGHUP to the group is what a terminal emulator does, and it reaches the
CLI's own children (MCP servers, shells). In the JavaScript app,
Electron could not exit while a node-pty child lived (measured
2026-10-12): an internal error that exited at once left the app running
without a window, and the launcher blocked, until a child that ignored
SIGHUP ended. Per-instance runtime dirs keep instances apart and leave
nothing behind.

## Rejected

- `/tmp`: shared by every user of the machine, where the runtime dir is
  private to the user and emptied at logout. `$TMPDIR` or `/tmp` is the
  fallback only without `XDG_RUNTIME_DIR`.
- One shared Electron userData for every instance, in the JavaScript
  app: two instances would contend for its lock.
