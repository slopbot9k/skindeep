# 0008 Finding and switching skins

Status: accepted 2026-10-11, identity rule added 2026-10-12, the menu
moved to the skin's button and the default to the last skin used by
decision 0022 the same day, and revised 2026-10-17 for the C port
(decision 0036).

## Context

Both sample skins must load and switching between them must work, seen
in screenshots. A broken skin fails loudly, naming the file and field.

## Decision

- The shipped skins' sources live in `<repo>/skins/<name>/`. The build
  compiles each into a pack linked into `skinned` (decision 0037).
  `--skin` takes a shipped skin's name, or a path to any skin directory,
  which serves skinners: a value with a `/` is a path, compiled by
  `skinc`. A name the build did not link in is a folder of `skins/`,
  opened as a path is, as `registry.js` took any folder there by name.
- Default skin: `deck`, where no last skin used is recorded (decision
  0022).
- Identity: a skin's id is its name. A shipped skin's manifest name
  equals its directory name (`tests/skinc/validate.c` checks it). A skin
  opened by path takes its manifest name as its id, and when a shipped
  skin has that name, the path skin replaces it for the rest of the run.
  The menu then shows the id with the path skin's folder, so the
  replacement is never hidden. Opening the shipped folder itself
  restores the shipped skin. `src/app/skins.c` holds the rule.
- Runtime switch: the skin's menu button (decision 0022) opens the
  app's own drop-down (`src/app/menu.c`), drawn above the skin and below
  the strip. Its Skins menu lists the skins, a path skin with its
  folder. Selecting the current skin loads it again: a skin opened by
  path is compiled again from disk, which serves a skinner editing it,
  and a shipped skin is its linked-in pack again. A skin opened by path
  is also watched once a second and compiled again when a file changes
  (decision 0037). The app draws the menu itself, so a test can open and
  click it: `tests/app/window.c` does, through the `SKINNED_CONTROL`
  socket.
- A skin that fails validation at launch: the error goes to the caller's
  stderr (decision 0003) in the form `skins/x/skin.json: widgets[3].bind: unknown
  metric "foo"`, and the app exits 125 before spawning the CLI.
- A skin that fails at runtime switch: the current skin stays, and the
  menu area shows the same error text.

## Why

A menu the app draws can be read and clicked by a test. Exiting at
launch is the loud failure the requirements ask for, and keeping the
current skin on a runtime failure never drops a live session.

## Rejected

- Collisions refused (one namespace): a copy of a sample skin, a
  skinner's likely first step, would not load until renamed.
- The directory as identity and the name a label (maintainer, 2026-10-12):
  opaque ids in the JavaScript app's `skin://` URLs and two entries of
  one name in the menu.
- A native context menu: a test can neither read it nor click it, as
  Electron's capturePage could not show it nor Playwright click it.
- An XDG data directory of user skins: `--skin PATH` covers it.
- Running with a fallback skin on a launch failure: hides the error.
