# 0033 Readouts open dials on the sessions dashboard

Status: accepted 2026-10-14 (the maintainer's words of the same
day), revised 2026-10-17 for the C port (decision 0036).

## Context

The deck skin's playlist area is eighteen readouts that do nothing. A
pointer over one shows the I-beam, which promises text selection the
rows do not offer. The maintainer's floor: each row does something on a
click, "even if it's just clicking to open or close new dials on the
dashboard".

## Decision

- Every readout whose `format` is not `text` takes a click, in every
  skin. The click opens or closes a dial of the readout's metric on
  the sessions dashboard (decision 0028): a column of that metric,
  beside the five fixed ones, a sparkline and the last value on every
  session's row, sampled from each session's bus by the metric's name.
  A text readout, such as the model's name, cannot be drawn, so it
  takes no click.
- The dials are one set for the app, not per window or tab. Opening
  the first dial while no sessions dashboard tab exists opens one and
  makes it active, as the menu entry does. Later clicks toggle without
  changing the tab, and closing the last dial leaves the dashboard
  open.
- A widget that takes a click carries the class `click`, shows the
  pointer, and brightens under it. One whose dial is open carries
  `open`, outlined in the skin's `--fill`. A skin may restyle both. The
  open mark stays while the readout has no data, as it has while the
  dashboard tab is the active one and the skin's meters show nothing.
- No widget shows the I-beam: `.w` sets `cursor: default` and
  `user-select: none` in the app's widget styles (`skinc/appcss.c`),
  so a widget is a display and never selectable text. The dashboard's
  own rows show the pointer while live, the arrow otherwise.
- The press of a click on a widget is the skin's, never the terminal's,
  so the terminal keeps the keyboard.
- The dial toggle goes from the window's thread to the core thread,
  which keeps the dials open and sends them to every window and
  dashboard (`do_dial` in `src/app/gui.c`). The sessions dashboard's
  history (`src/sessions/history.c`) keeps one series per dial, null
  for the ticks before the dial opened and dropped when it closes.

## Why

A sparkline column is the dashboard's own idiom, so a dial costs no new
drawing and no CSS reach into the dashboard pane, which skin CSS cannot
touch (decision 0028). One set for the app keeps a dial opened from any
window visible in every dashboard.

## Rejected

- A gauge or needle drawn beside the row: the skin area has no free
  room, and the terminal paints above the skin.
- Opening the dashboard behind the active tab: the click would show
  nothing but the row's mark.
- A click on every widget type: the issue asks for the rows, and a
  gauge's or bar's click has no obvious dial.
