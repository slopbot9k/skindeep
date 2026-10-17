# 0021 Agent adapter seam

Status: accepted 2026-10-12, one clock added the same day, revised
2026-10-17 for the C port (decision 0036).

## Context

The JavaScript app's main.js knew each agent adapter's internals: two
predicates chose one, their constructors took different options, only
Claude had a prepare and a notice, attach was guarded, Generic was fed
by hand, and no adapter was ever closed. A failed Claude prepare left
its hook server listening. The dispatch rule lived in three places and
disagreed: `codex_work` got the Codex adapter but no profile in its
label, and `claude_work` got no adapter.

## Decision

- `src/agents/agent.c` holds the seam. `sk_agent_provider(cmd)` reads
  the command's basename against a table of providers, each a row
  `{ kind, matches(basename), create(ctx), carry }`, first match wins,
  else the plain provider of `src/agents/plain.c`. Claude matches
  `^claude(_.+)?$`, Codex `^codex(_.+)?$` (maintainer, 2026-10-12): a
  launcher such as `claude_work` or `codex_lab` gets its CLI's adapter,
  and hyphen names, where other tools live, never get Claude's
  `--settings`.
- `sk_agent_open(ctx)` builds the chosen adapter beside Generic and
  returns one object, the Session's only adapter (`src/core/session.c`).
  `ctx` is an `sk_agent_ctx`: the command, cwd and environment, the
  tab's dir, the runtime root, the settings store, the relay's path, the
  statusline mode, the bus, the core loop and the log. It writes nothing
  to the bus and starts no channel.
- Operations, in order: `sk_agent_prepare` once, before the spawn,
  giving the arguments to inject and the notice, `sk_agent_attach(pid)`
  once, after it, `sk_agent_on_output` and `sk_agent_on_input` per pty
  chunk and input, `sk_agent_report`, and `sk_agent_close`. A part's
  `prepare` and `rename` may be absent and the seam checks for them, so
  no caller checks for one.
- The failure rule: `sk_agent_prepare` never fails. A part whose prepare
  fails is closed at once, its socket closed and its arguments dropped,
  the failure logged, and the notice reads `meters limited: adapter
  failed`. The session runs on what remains.
- `sk_agent_attach` before `sk_agent_prepare`, or twice, aborts the app:
  a programming error. `sk_agent_close` is idempotent and valid at any
  point, after which nothing stays listening. Prepare runs to its end
  before the spawn, so no close can come during one. The Session closes
  the agent at every end (decision 0016).
- One clock. The seam owns the only timer of the metrics, a 250 ms timer
  on the core loop, reading `ctx->now`, the monotonic clock unless a
  test gives its own. Every 250 ms it calls each part's `poll(now)`,
  then `sk_bus_tick(bus, now)`. Adapters hold no timer and throttle
  their own reads: a followed file (`src/core/tail.c`) every 500 ms,
  Codex discovery and its subagent count every 1000 ms, the process tree
  every 1000 ms. A poll returns nothing, so a part's failure reaches no
  other part. `sk_agent_close` cancels the timer.
- The following report: `sk_agent_report` gives `{ session, title,
  notice, channels, replaying }`, and `sk_agent_kind` the agent.
  `session` is Claude's session id or Codex's thread id, `title` the
  session's name in the CLI's own record or none (decision 0032). Each
  channel (`hooks`, `statusline`, `transcript`, `rollout`, `pty`,
  `proc`) is `off` (not injected), `waiting` or `live`, with the file it
  follows and the route that found it. The notice derives from the
  states: `meters limited: --settings given`, `meters limited: adapter
  failed`, `meters waiting: no rollout found` (decision 0013). The
  window re-sends the strip when the notice changes, at its 2 s poll.
- The window label names the profile of the dispatched adapter
  (decision 0004).

## Why

One seam makes a provider a row and a module, with no caller change,
which an agent manager listing many sessions would need. The failure
rule holds for every adapter, so a broken channel costs its meters,
never the session.

## Rejected

- Widening dispatch to hyphen names: any program named `claude-<x>`
  would get Claude's `--settings` file and break.
- A `{ argv, notice }` result: the Session already takes `{ args,
  notice }`.
