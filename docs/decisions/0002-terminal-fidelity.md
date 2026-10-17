# 0002 Terminal fidelity

Status: accepted 2026-10-11, revised 2026-10-12 for the tab shortcuts
(decision 0023), and revised 2026-10-17 for the C port (decision 0036).

## Context

The pane must behave as a plain terminal: truecolor, resize, mouse,
bracketed paste, copy and paste, Ctrl-C and Esc, links through the
system's opener, wide characters. The app may never inject input or
rewrite output.

## Decision

- Bytes: the core thread reads the pty's raw bytes and feeds them to the
  tab's terminal (`src/term/`), whose UTF-8 decoder handles sequences
  split across chunks. The pane shows the CLI's bytes, nothing else.
- The terminal follows xterm.js 6.0.0, which the JavaScript app ran: its
  parser, its handlers and the sequences they ignore, its buffers with
  5000 lines of scrollback and reflow, its selection, and the bytes its
  keys, mouse, focus reports and paste send (`src/term/keys.c`).
- Env: `TERM=xterm-256color`, `COLORTERM=truecolor`.
- Width: the tables of xterm.js's Unicode 11 addon (`src/term/width.c`).
- Renderer: `src/ui/termview.c` draws the terminal in software into the
  window's frame, as xterm.js's WebGL renderer drew it: the cell size
  from the font, its colours, its underlines and cursors, and its own
  box-drawing and block glyphs.
- Menu: no accelerator eats a terminal key. On Linux the app has no
  menu bar. On macOS the app menu holds only Quit on Cmd+Q, so every
  other Cmd chord reaches the window.
- Keys: everything goes to the pty except the app's chords
  (`src/app/gui.c`). On Linux: Ctrl+Shift+C (copy), Ctrl+Shift+V and
  Shift+Insert (paste), and the tab shortcuts Ctrl+Shift+T,
  Ctrl+Shift+W, Ctrl+Tab and Ctrl+Shift+Tab (decision 0023). On macOS:
  Cmd+C, Cmd+V and Shift+Insert, and Cmd+T, Cmd+W, Ctrl+Tab,
  Ctrl+Shift+Tab, Cmd+Shift+] and Cmd+Shift+[. Every Cmd chord is the
  app's and sends the CLI nothing.
- Alt and Option: on Linux, Alt sends an ESC prefix. On macOS, Option
  types the layout's character, as xterm.js's `macOptionIsMeta: false`
  does.
- Selection: on Linux, copied to X11's PRIMARY selection when the drag
  that made it ends. A middle click pastes PRIMARY, unless the CLI's
  mouse mode takes the click. macOS has no PRIMARY.
- Paste: through the terminal's paste, which honours bracketed paste
  mode.
- Drop: a dropped file pastes its shell-quoted path, as gnome-terminal
  does.
- Links: OSC 8 links, and URLs found in the text as xterm.js's web-links
  addon finds them (`src/term/links.c`). Ctrl+click opens one on Linux,
  Cmd+click on macOS, by spawning `xdg-open` or `open` with an argv
  array: http, https and file URLs, but never a file URL that names a
  program, an executable file, an application bundle, a `.desktop`
  launcher or Terminal settings (`src/app/opener.c`). On macOS `open`
  would start any of them, from a link whose text may hide where it
  points.

## Why

Raw bytes make "byte for byte" literally true. Following xterm.js keeps
what Claude Code and Codex drew in the JavaScript app, the gate of
decision 0036's phase 4. A default menu binds keys a terminal needs:
Electron's bound Ctrl+W, Ctrl+R, Ctrl+Q, Ctrl+Z and zoom, which is why
the JavaScript app removed its own before the first window.

## Rejected

- String decoding of the pty's output: correct in practice but not byte
  for byte.
- A second middle-click handler: Chromium already pasted PRIMARY in the
  JavaScript app, so one of its own would have pasted twice. The C app
  has the one handler.
- Plain click on links: collides with the CLI's own mouse handling.
