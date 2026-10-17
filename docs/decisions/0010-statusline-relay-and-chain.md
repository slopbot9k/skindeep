# 0010 Statusline relay and chaining

Status: accepted 2026-10-11, managed settings read 2026-10-12, the
relay's arguments revised 2026-10-12 (decision 0030), and revised
2026-10-17 for the C port (decision 0036).

## Context

The injected statusLine overrides the profile's (precedence: managed >
--settings > local > project > user, key by key). The in-terminal status
line must stay what the profile makes it.

## Decision

- The injected command is `<relay> statusline ROOT CHAIN`. `<relay>` is
  the absolute path of `skinned-relay`, found beside the `skinned`
  program, or `SKINNED_RELAY` when set. ROOT is the runtime root, CHAIN
  the chained command in base64url, or `-` for none.
- The relay reads stdin, then at once starts the chained command (if
  any) through `/bin/sh -c` with the same stdin and cwd, and in parallel
  POSTs the JSON to the tab it finds by ancestry (decision 0030), the
  whole POST limited to 500 ms, ignoring every failure. The chain writes
  to the relay's stdout itself. The relay passes SIGTERM on to it, and
  exits with the chain's code once the chain has ended and the POST is
  done or timed out. With no chain it prints nothing and exits 0. It
  never prints text of its own. With no window, as in a session sent to
  the background, the chain still runs.
- The chain is resolved at launch from user
  (`$CLAUDE_CONFIG_DIR/settings.json`), project
  (`<project>/.claude/settings.json`) and local
  (`<project>/.claude/settings.local.json`), later wins, honouring a
  `--setting-sources` the user passed. The project dir is the git root
  when the cwd lies inside a repository, else the cwd: measured
  2026-10-11, a statusLine in the root's `.claude/settings.local.json`
  showed when claude was launched from a subdirectory.
- The injected entry copies `padding`, `refreshInterval` and
  `hideVimModeIndicator` from the effective one.
- Managed settings, the highest source: the writer reads
  `managed-settings.json` and then `managed-settings.d/*.json` in name
  order, in `/etc/claude-code/` on Linux and in
  `/Library/Application Support/ClaudeCode/` on macOS, through
  the same filter as the other files plus the keys `disableAllHooks`
  and `allowManagedHooksOnly`. A managed statusLine means ours never
  runs, and the meters fall back to the transcript. Either hook key
  means our hooks never fire, and the hook meters read no data. Each
  sets the strip notice `meters limited: managed settings` and a log
  line naming the keys. Managed compaction values enter the gauge
  (decision 0012).
- Remote managed settings are not files. They show only as an injected
  statusline that stays silent: after two Stop hooks with no statusline
  payload, the log says so once per session.
- The SDK-only `--managed-settings` flag is out of scope.

## Why

Running the chain first and in parallel keeps the profile's own status
line live even if the app stalls. Reading settings files is allowed,
editing them is not.

## Rejected

- POST before the chain: a stalled app would freeze the profile's
  status line.
- Putting the port in the child env: every subprocess of the CLI would
  inherit it.
- A chain file in the tab's dir: a session sent to the background
  outlives the dir and loses its own statusline (decision 0030).
