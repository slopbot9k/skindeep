# 0037 Skin packs: CSS compiled once, per state

Status: accepted 2026-10-16. Phase 5 of decision 0036 builds `skinc`;
phase 6 builds the runtime that loads what it writes. skinc's front end
(below, through the states) is built; the layout, the paint, the pack
writer and the decoders are phase 5's next steps.

## Context

Decision 0036 keeps skins as `skin.json` and `skin.css` and moves the
CSS out of the running app: `skinc` compiles a skin into a pack ahead
of time, and `skinned` reads packs only. The JavaScript app builds a
skin as DOM (`app/renderer/skin.js`, `widgets.js`) styled by
`app/renderer/widgets.css` and the skin's sheet in a shadow root, and a
browser restyles it as widgets toggle classes. A pack must hold
everything that restyle and the paint need, in a form the runtime can
use without a CSS engine, and the runtime must be able to check every
byte of it, since a user's pack sits in a cache directory like any
other file.

What changes while the app runs is small and fixed. Widgets toggle
`.on`, `.nodata`, `.open`, `.fallback`, the list's row states and the
limits widget's `.n-<count>`; the stage toggles `.checklist` and
`.checklist-off`; the skins add `:hover`, `:nth-child()` over fixed
segment counts, `+` from a lamp to its label and one
`.lava:has(.lamp.on)` (`skins/coffee/skin.css:57`). The rest of a skin
is fixed, much of it at a fixed size.

## Decision

### The compiler and where packs live

- `skinc DIR` checks a skin as `app/skins/validate.js` does, every
  message as it writes it and in its order, then refuses what it
  cannot compile, giving the file and the field, or `line N` in the
  stylesheet: `skins/x/skin.css: line 12: "cursor": "wait" is not a
  valid value: cursor takes auto, default, pointer or text`. Its own
  refusals come after validate.js's.
- `skinc DIR -o PACK` writes the pack. The build compiles the four
  shipped skins into `build/<mode>/gen/<name>.pack` and links them into
  `skinned` as generated C arrays (`tools/bin2c`).
- A skin opened with `--skin DIR` or from the menu is compiled by
  `skinned` running `skinc` as a child process, so a decoder that a
  hostile PNG, SVG, font or Ogg file breaks breaks in a process with
  nothing of the app's in it. The pack goes to
  `<cache>/packs/<hash>.pack` (decision 0036's cache directory), the
  hash a SHA-256 of the pack format's major and minor, skinc's own
  version, and every file of the skin, path and bytes, in sorted
  order. It is written to a temporary name and renamed.
- While a skin opened by path is current, the runtime checks the
  size and modification time of its files once a second; a change
  compiles it again, and a pack that compiles replaces the skin as a
  reload does (decision 0008). A pack that fails to compile keeps the
  current skin and shows skinc's messages where the menu shows a
  skin's errors.
- Every pack, shipped or cached, goes through the same loader and
  its checks. A cached pack that fails them is deleted and compiled
  again.

### The CSS subset

skinc reads CSS by CSS Syntax Level 3. What the four skins and
`widgets.css` use is the subset; anything else is an error.

- At-rules: `@keyframes` (a name or a string; frames `from`, `to`
  and percentages, comma lists; a keyframe sets `transform`,
  `opacity` and `animation-timing-function` only). `@import` keeps
  validate.js's error. `@font-face` is refused, as decision 0005
  refuses it: fonts come from the manifest. Every other at-rule, and
  a rule nested in a rule, is refused.
- Selectors: type, universal, class and id selectors (no element has
  an id, so `#x` matches nothing); `:hover`, `:first-child`,
  `:last-child`, `:only-child`, `:nth-child(An+B)`,
  `:nth-last-child(An+B)`, `:not()`, `:is()`, `:where()`, `:has()`
  with relative selectors (not nested), `:host` and `:host()`; the
  descendant, `>`, `+` and `~` combinators; `::before` and `::after`
  (and `:before`, `:after`) at the end. Specificity is Selectors 4's,
  each part saturating at 255. Refused: attribute selectors,
  namespaces, `:root`, every other pseudo-class and pseudo-element.
- `!important`, the CSS-wide `inherit`, `initial` and `unset`, custom
  properties (any value, inherited) and `var()` with fallbacks, cycles
  invalid at computed-value time. A declaration that is invalid after
  `var()` is an error at its line, not a silent unset.
- Properties: `display` (none, block, inline, inline-block, flex,
  grid), `position` (static, relative, absolute, fixed), `float`,
  `box-sizing`, `top` `right` `bottom` `left` and `inset`, `width`,
  `height`, `min-*`, `max-*`, `margin*`, `padding*`; `border` and its
  sides, widths, styles (none, hidden, solid) and colours,
  `border-radius` and its corners (with `/`), `border-image` and its
  longhands, `outline` and its longhands, `outline-offset`;
  `overflow`, `overflow-x`, `overflow-y` (visible, hidden, clip);
  `z-index`, `visibility`, `opacity`, `pointer-events` (auto, none),
  `cursor` (auto, default, pointer, text), `mix-blend-mode`,
  `isolation`, `contain` (none only), `user-select` (accepted: a skin
  has no selection); `flex` and its longhands, `flex-flow`,
  `flex-direction`, `flex-wrap` (nowrap only), `justify-content`,
  `align-items`, `align-self`, `gap`, `row-gap`, `column-gap`;
  `grid-template-columns`, `grid-template-rows` (fr, lengths, auto,
  `repeat(n, ...)`), `grid-auto-rows`, `grid-auto-columns`,
  `grid-column`, `grid-row` and their longhands (lines and spans);
  `transform` (translate, translateX/Y, rotate, scale, scaleX/Y, skew,
  skewX/Y, matrix), `transform-origin` (z 0 only), `filter` (blur,
  brightness, contrast, saturate, grayscale, sepia, invert, opacity,
  hue-rotate, drop-shadow), `clip-path` (none, polygon),
  `image-rendering`; `background` and `background-color`, `-image`,
  `-position`, `-size`, `-repeat`, `-blend-mode` (normal, multiply,
  screen, overlay, darken, lighten, plus-lighter: the modes
  `src/gfx/draw.h` composites), `-origin`, `-clip`, attachment scroll
  only; `box-shadow`; `color`, `font` (style, weight, size,
  line-height, family; no small-caps or stretch), `font-family`,
  `font-size`, `font-weight`, `font-style`, `line-height`,
  `letter-spacing`, `text-transform`, `text-align` (no justify),
  `white-space` (normal, nowrap, pre), `text-overflow`,
  `text-decoration` and its line and colour, `text-shadow`,
  `text-anchor`, `content` (strings, `counter(name)`), `counter-reset`,
  `counter-increment`; SVG's `fill`, `stroke` (none or a colour),
  `stroke-width`, `stroke-dasharray`, `stroke-linecap`,
  `stroke-linejoin`; `animation` and its eight longhands,
  `transition` and its four, a transition naming a longhand of the
  subset or `all`.
- Values: lengths in px, em, rem (16px), in, cm, mm, q, pt, pc,
  percentages, `calc()` of sums and products of those and numbers;
  angles in deg, rad, grad, turn; times in s and ms; colours as
  `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()` and `hsl()` in both
  syntaxes, the 148 named colours, `transparent`, `currentcolor`;
  images as `url()` of a file of the skin relative to the stylesheet
  (validate.js's checks), `data:image/` urls, `linear-gradient()`,
  `radial-gradient()` and their repeating forms, stops with up to two
  positions, no colour hints; easing as the keywords, `cubic-bezier()`
  and `steps()`. Refused: vw, vh, ch, ex, conic gradients, colour
  hints, `minmax()`, named grid lines, `attr()`, `counters()`, a
  counter style other than decimal, `url()` filters and clip shapes.
- The cascade has the user agent's few rules skins rely on (a div is
  a block, a link is not shown, an svg clips), then SVG presentation
  attributes, then `widgets.css` and the skin's sheet, then the style
  attributes `skin.js` and `widgets.js` set, then the important
  layers in reverse. A panel's, widget's or toggle's rect is its style
  attribute: an `!important` rule overrides it, as in the browser.
- `:host` matches the host, the root of the tree. Its own box belongs
  to the app: the runtime paints none of it and ignores where it puts
  it; only what the stage inherits from it reaches the skin. The skin
  is clipped to the skin area whatever its CSS says, and the strip,
  the terminal and the menu button paint above it (decision 0005).

### The element tree

skinc builds the tree `skin.js` and `widgets.js` build: the host; the
two stylesheet links (display none, kept for `:nth-child`); the stage
`div.skin`; the panels in the manifest's order; each widget's root
(`.w .w-<type>` and its classes, `.click`, `.vertical`, `.across` or
`.down` as `widgets.js` adds them) and its parts; a list's toggle
right after its list. SVG parts carry their geometry in viewBox units
(arcs, lines, circles, text and image boxes, the centres a needle's or
knob's group turns about). Three parts are templates:

- A list's row is one element standing for every row, `repeated`.
  Its counters count by row (below). A selector that reaches a row by
  position (`:nth-child()`, `+`, `~`, `:has()` into the rows) is
  refused: rows come and go.
- A limits widget holds its meters once for each count from 1 to
  `SK_PACK_MAX_LIMITS` (8): the meters of count n are siblings of one
  another for `:nth-child()` and `+`, and exist only while the widget
  shows n meters. A provider sending more limits shows the first 8.
- `::before` and `::after` exist where a rule may give one content;
  in a state where `content` computes to none the box is not
  generated.

### States

- A state variable belongs to one element, its owner, and gives it
  classes, or `:hover`:

  | Variable | Owner | Values and classes |
  | --- | --- | --- |
  | hover | any element | 0, 1 (`:hover`) |
  | nodata | a widget's root, a `.resets` | 0, 1 `.nodata` |
  | open | a readout that takes a click | 0, 1 `.open` |
  | fallback | a bar or readout with a fallback | 0, 1 `.fallback` |
  | on | a segment, a lamp, a toggle's option | 0, 1 `.on` |
  | list | the stage | 0, 1 `.checklist`, 2 `.checklist-off` |
  | item | a list's row | 0 `.todo`, 1 `.doing`, 2 `.done` |
  | count | a limits widget | n `.n-<n>` for 0 to 8, 0 also `.nodata` |
  | model | a limit's meter | 0 `.account`, 1 `.model` |
  | span | a limit's meter | 0 `.spend`, 1..k the `.span-<x>` classes the sheets name, k+1 any other |

- Discovery matches every selector against an element with every
  variable unknown, in three-valued logic. A selector surely false is
  no candidate; the variables read on the way are those its result can
  depend on. AND stops at a sure false and OR at a sure true in both
  modes, so a state's matching reads no variable discovery missed.
- An element's inputs are the variables its candidates read, and
  those of its parent's inputs that change what it inherits: an input
  is passed down when changing it alone changes an inherited property,
  a property some declaration inherits explicitly, or a custom
  property, in some state. A meter's count is fixed by its group, so
  it is never an input of a meter or what a meter holds.
- The state table has an entry for every combination of the inputs'
  values, the first input least significant; each entry is a style
  index. Equal styles are stored once. At most 10 inputs and 4096
  entries an element, 65536 in all; past that skinc refuses the skin
  and names the element and its variables.
- The runtime sets the variables: hover from the pointer (the topmost
  box under it whose `pointer-events` is not none, and its
  ancestors), nodata from the metric, open from the dials (decision
  0033), fallback and on from the widget's logic, list from the task
  list (decision 0034), item per row, count from `limits.items`,
  model and span from each item (`span-` and the lowercased letters
  and digits of its window, looked up among the pack's span classes).
  On a change it looks the style up, `sk_pack_style_of`.
- Transitions: when an element's style changes, each property its new
  style's `transition` names eases from the old computed value to the
  new (colours, lengths, opacity, shadows of equal count, transforms
  of matching functions); the rest switch at once. Animations: an
  animation starts when its name appears in the element's style,
  keeps its clock while its play state is paused, and stops when the
  name goes. A keyframe track animates `transform` and `opacity`,
  frames merged by offset, a frame's missing property interpolated
  from its neighbours or the element's own value.
- A style is CSS's computed values: `display` blockified for an
  absolute or floated box and a flex or grid item, `float` none on an
  absolute box, lengths in px but for percentages, rect keys and
  widget values, colours resolved.
- Counters are counted once, through the tree in document order, the
  elements displayed in some state counted as displayed:
  `counter()` becomes text, or, inside a list's row, the row's
  ordinal `base + step * row`. A counter reset or incremented in some
  states only, or read after a list's rows, is refused.

### What is pre-rendered and what the runtime paints

The runtime can paint any element from its computed style with the
painter skinc uses (phase 5 puts it in the app's library, so the two
draw alike). Layers are what skinc paints ahead of time, at 1x and 2x:

- STATIC: a subtree whose elements change in no state, run no
  animation, show no value (text, fill, arc, mark, turn, frame) and
  keep their size at every window size and split position: one layer
  covering all it paints, its overflow and shadows included, moved
  as a whole when a split moves it: coffee's corkboard, amp and
  turntable deck, library's card catalog and drawers, the faces, tracks
  and ticks of dials.
- STATE: such a subtree whose paint changes with state alone, one
  layer for each painted state, up to 8: the lamps of leds, coffee's
  lava lamp body, the toggles' options.
- SPRITE: what the runtime turns or moves: a needle's group and a
  knob's turning group (their filters drawn in their own coordinates,
  as SVG does, so a drop shadow turns with them), and a fixed element
  whose only change is a transform animation (coffee's record, the
  lava blobs). An opacity animation, as phosphor's scanlines flicker,
  applies to whatever layer the element has as it is composited.
- NINE: the box shadows and border image of a box that stretches with
  the window or a split, as a nine-slice; the runtime paints the
  box's backgrounds itself, on a resize.
- MASK: the clip something changing is drawn through: a clip-path
  (the lava lamp's body around its blobs), or rounded corners (the
  radar's round sheet around its frames).
- The runtime paints, each time their content or size changes: text
  of values and labels, a bar's fill, a gauge's arc and marks, list
  rows, a limits widget's meters, a sprite's frame, the stretched
  panels. What the four skins need that cannot be pre-rendered:
  - blurred text shadows on changing text: every widget of phosphor
    (`.w`, 5px) and deck's digits (6px); and coffee's lava label,
    whose colour and two glows (4px and 10px) ease for 0.4 s between
    its states;
  - drop shadows on changing shapes: every gauge's fill arc
    (`widgets.css`), coffee's platter, library's chalk; coffee's
    press and jars, whose fill and tape labels change under the
    shadow of the whole widget;
  - `filter: brightness(1.3)` under the pointer on every clickable
    readout, over its changing text;
  - turned or centred boxes sized by changing text: coffee's handbill
    (rotate(-3deg)), its jars' tape labels (the provider's label,
    translate(-50%, -50%) rotate(-4deg));
  - deck's marquee, translateX(-100%) of the model name's own width;
  - the boxes that stretch with the window or a split, their
    gradients sized by the box: coffee's room overlay and its string
    lights, phosphor's screen and scanlines, deck's body, plates and
    playlist, coffee's wood under a multiply blend;
  - the limits grids, laid out by count with the last meter spanning
    two columns.

  These are small boxes, painted when they change, except the
  stretched panels, painted on a resize.

### Layout

skinc lays out what it pre-renders and the runtime the rest, with the
same code:

- The stage is the skin area at its layout size (the area divided by
  decision 0006's scale k); panels, widgets and toggles are placed by
  their rects, the followed keys moved by their splits
  (`skinc/splits.c`, `app/renderer/splits.js` in C).
- Positioning: static, relative, absolute and fixed (absolute against
  the skin area); the containing block the nearest positioned
  ancestor's padding box; insets, sizes, min and max sizes,
  box-sizing; an absolute box with both insets of an axis auto sits at
  its static position, a flex or grid container's alignment of a lone
  item.
- Block layout stacks boxes; vertical margins between boxes in block
  flow are refused (no skin uses them); flex items, grid items and
  absolute boxes take margins.
- Inline content: text and inline boxes (spans, inline `::before` and
  `::after`) on lines, broken at spaces where `white-space` is normal;
  inline boxes carry text properties only (padding, borders,
  backgrounds and margins on them are refused); `text-align`,
  `letter-spacing`, `text-transform`, `line-height` (normal is the
  font's ascent, descent and gap), baselines, `text-overflow:
  ellipsis` where overflow is not visible, decorations.
- Flex: one line; direction and its reverses, `justify-content`,
  `align-items` and `align-self` with first baselines, gaps, grow,
  shrink and basis (auto is the content's size: text's max-content
  width, an svg's from its viewBox ratio), the automatic minimum size
  of Flexbox 4.5, max sizes.
- Grid: explicit columns and rows, rows added with
  `grid-auto-rows`, items placed row by row, spans and line numbers,
  gaps; items stretch.
- Floats: a float sits at its container's edge at the top of its
  line; text beside it does not wrap around it.
- SVG: an svg is a replaced box of its viewBox's ratio (300 x 150
  with no size), its content mapped by `preserveAspectRatio` xMidYMid
  meet (none for a needle's face); strokes, fills, dashes (in
  `pathLength` units on a gauge's arcs), text anchors from the
  computed styles.
- Overflow hidden and clip clip to the padding box, rounded;
  visibility hidden keeps the space; transforms apply after layout
  about `transform-origin`, translate's percentages of the element's
  own box; painting order and stacking contexts as CSS 2.1's
  Appendix E.
- The runtime lays out again: a widget's subtree when its text, state
  or box changes; a list's rows, the row template once per item in
  the items' column, the rows past the box clipped; a limits widget's
  meters, the group of the count shown; everything on a resize or a
  split's drag.

### The file

All integers little-endian, floats IEEE 754 binary32. `src/skin/pack.h`
holds the constants and the in-memory form.

- Header, 64 bytes: magic `SKINPACK` (8), major u16 (1), minor u16
  (0), header size u32 (64), file size u64, section count u32, flags
  u32 (0), the source hash (32 bytes, the cache key's SHA-256). Then
  the section table: per section u32 id (four ASCII bytes), u32
  offset, u32 size, u32 count.
- A string is a u32 offset into STRS of a NUL-terminated UTF-8
  string; `0xFFFFFFFF` is none. Every index is u32, none
  `0xFFFFFFFF`. A rect is 6 f32 (left, right, width, top, bottom,
  height), u8 has (bit i: key i given), 6 i8 follows (a split, or
  -1), a pad byte: 32 bytes.
- Sections, each 4-byte aligned, of `count` fixed records unless
  said:
  - STRS: the strings.
  - META, one record of 216 bytes: name, version, author,
    description, licence (strings); width, height, min width, min
    height (f32); the menu's rect and its image, hover and pressed
    sources; the terminal's rect, font (a pack font or none) and
    size; the theme's 23 colours as u32 0xAARRGGBB in `THEME_KEYS`'s
    order and a u32 of which were given.
  - SPLT, 16 bytes: id (string), edge u8 (left, right, top, bottom),
    3 pad, min, max (f32). SPAN: strings, the span classes.
  - RECT: rects, of panels, widgets and toggles.
  - WIDG, 128 bytes: type, format, digits u8, flags u8 (has on,
    pulse, vertical, column, run, needle meters, across); id, bind,
    label, prefix, suffix, resets, fallback bind, label and resets
    (strings); min, max, damping, start angle, end angle, thickness,
    gap, on, pulse ms, blink, fps, frame width, frame height (f32);
    ticks, segments, frames, rows (u16); first mark u32, mark count
    u16, pad u16; root element, toggle element, face, image, sheet.
    MARK, 12 bytes: bind (string or none), value f32, class (string).
  - ELEM, 52 bytes: parent, first child, next sibling; kind, role,
    group, meter (u8); widget, rect, svg; text source u8, input count
    u8, pad u16; text (string); first input, table (the first entry in
    STAB); first layer, layer count.
  - SVGG, 28 bytes: kind u8, flag u8, pad u16, 5 f32, image source.
  - INPT, 8 bytes: owner element, variable u8, radix u8, pad u16.
  - STAB: u32 style indexes.
  - STYL, count styles: count + 1 u32 offsets from the section's
    start, then each style: u16 property count, then per property in
    increasing id, u16 id, u16 length and its payload. A property left
    out has its initial value, currentcolor there being the style's
    `color`. A loader skips an id it does not know, by its length.
  - KEYF, count tracks: count + 1 u32 offsets, then each: name
    (string), u16 key count, then per key: offset f32, has transform,
    has opacity, has timing (u8), pad u8, opacity f32, timing,
    transform list.
  - IMGS, 16 bytes: path (string), width, height u16, first variant
    u32, variant count u16, pad u16. VARI, 16 bytes: width, height
    u16, scale u8 (0 the file's own pixels, 1 or 2 drawn at that
    scale), 3 pad, BLOB offset, length: a QOI image.
  - LAYR, 48 bytes: element, style index (none: every state); kind,
    scale u8, pad u16; x, y, ox, oy f32; width, height u16; slice
    4 u16; BLOB offset, length: a QOI image.
  - FONT, 16 bytes: family (string), weight u16, style u8, pad u8,
    BLOB offset, length: TrueType bytes skinc's checker passed.
  - SNDS, 36 bytes: path (string), mode u8, pad u8, block bytes u16,
    volume, interval min, max (f32), rate, frames, BLOB offset,
    length: mono IMA ADPCM, WAVE format 0x11's blocks (a 4-byte
    header, 1 + 2 (block bytes - 4) samples), at the source's rate,
    a stereo source mixed down.
  - BLOB: bytes.
- Payloads in STYL: a keyword or bool u8; a number f32; z-index u8
  auto and i32; a length u8 kind, f32 px, f32 pct; a colour 4 f32
  (r, g, b, a, 0 to 1, not premultiplied); an image u8 kind then a
  u32 source (url) or a gradient: kind, repeating, circle, extent,
  corner (u8), angle f32, rx, ry, cx, cy lengths, u16 stop count and
  per stop a colour and a length; a position two lengths and two edge
  bytes; a background size u8 kind and two lengths; a shadow x, y,
  blur, spread f32, a colour, inset u8; a transform function u8 kind,
  two lengths, 6 f32; a filter u8 kind, amount f32, a shadow; an
  easing u8 kind, u8 step position, u16 steps, 4 f32; a content part
  u8 kind, text (string), base and step i32; a grid track u8 kind, a
  length, fr f32; a grid line u8 kind, i16. A list is u16 count then
  its items. Per property its field's types in `sk_css_style`'s
  order: two lengths for a corner's radius; four f32, u8 percent
  bits, u8 fill for `border-image-slice`; four lengths and u8 number
  bits for its width; u8 evenodd and a list of 2n lengths for
  `clip-path`; a list of strings, u16 face and u8 synthesis for
  `font-family`; u8 none and a list of parts for `content`; u8 kind
  and a colour for `fill` and `stroke`. `counter-reset`,
  `counter-increment`, `contain`, `flex-wrap` and `user-select` are
  never written: skinc has used them, or they hold nothing.
- Versioning: a loader refuses another major. A minor adds sections,
  fields at the end of a fixed record (the record size being the
  section's size over its count, at least the size the loader knows),
  or property ids, all of which an older loader skips; a field an
  older pack lacks takes its default.
- The loader's checks, any one failing refusing the pack: the magic,
  the major, the file size equal to the bytes and at most 1 GiB, the
  header and table inside it; each section inside the file, aligned,
  apart from the others, its id once, each 1.0 section present, a
  fixed section's size a whole multiple of its count; every string
  inside STRS, terminated there, valid UTF-8; every index below its
  target's count; the tree a single tree from element 0, no element
  reached twice, at most 65535 elements; inputs at most 10 an
  element, radices as the variables give them, tables at most 4096
  entries and 65536 in all, inside STAB, each entry below the style
  count; STYL's offsets rising inside the section, each style's
  properties rising, each payload exactly its length, every list at
  most 256 items, every float finite (an iteration count may be
  infinite) and within its range; QOI images decoding to their
  record's size, at most 16384 a side and 2^26 pixels in all; fonts
  passing the same checker skinc runs; sounds' blocks covering their
  frames, rates 8 to 48 kHz, volumes 0 to 1, intervals as decision
  0024's; layers' kinds, scales and images valid.

## Why

- Compiling per state keeps CSS's own meaning, browser for browser,
  without a style engine in the app: the variables are few and fixed,
  and discovery finds for each element only the ones its selectors
  read, so the tables stay small (the four skins reach 731 to 6576
  states in all, 129 to 479 distinct styles).
- Computed styles, not pre-rendered pictures alone, because the skins
  stretch with the window and the splits and show changing values:
  the runtime must paint some of every skin. With one painter shared,
  a layer is a cache, never a second rendering of the same CSS.
- The tree and the inheritance kept as the browser has them, links
  included, so `:nth-child`, `+` and `:host` mean what they meant.
- A format of fixed records and length-prefixed payloads, so every
  byte can be checked before use, and an older loader can skip what a
  newer skinc adds.
- skinc as a child process for user skins: the untrusted decoders
  never share memory with the session.

## Rejected

- A CSS engine in the app (decision 0036's rejection).
- Pre-rendering every state of every element as pictures: the
  stretching panels and the changing text cannot be, and the counts
  multiply (a deck row has 24 states).
- Selector matching at run time over a shipped tree: the CSS would
  still have to be parsed and cascaded in the app.
- Enumerating every combination of every variable in the skin: the
  product is astronomical; per element, over the variables its
  selectors read, it is at most a few dozen in the shipped skins.
- One row element per possible list item: the rows are unbounded.
  One template and an ordinal keep the pack small.
- Packs as JSON: every number and list would need parsing and
  checking anyway, and the layers and fonts are binary.
- Linking skinc into skinned: a decoder flaw would reach the session.
