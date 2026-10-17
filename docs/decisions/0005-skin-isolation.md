# 0005 Skin isolation: data, not code

Status: accepted 2026-10-11, revised 2026-10-12 for skin sounds
(decision 0024), and 2026-10-17 for the C port (decision 0036).

## Context

A skin is a directory of a manifest, images, fonts and CSS. It runs no
scripts, since the app runs beside the user's credentials, and it may
not hide the identity strip or the terminal.

## Decision

- `skinc` reads and validates `skin.json` and the stylesheet
  (`skinc/manifest.c`) and writes a pack (decision 0037). `skinned`
  reads packs alone, each through the loader's checks
  (`src/skin/pack.c`). It parses no manifest or CSS and decodes no
  image or sound file of a skin: the pack holds the pictures as QOI and
  the sounds as IMA ADPCM.
- skinc takes a file only when its realpath lies inside the skin's
  realpath and its extension is one of .css .png .jpg .jpeg .gif .webp
  .svg .ttf .otf .woff .woff2, or the sounds .ogg .opus .mp3 .wav of
  decision 0024. Of these it decodes PNG, JPEG and SVG images, fonts
  with TrueType outlines, and Ogg Vorbis and WAV sounds. Any other file
  of those types fails to compile.
- The four shipped skins are compiled during the build and linked into
  `skinned`. A skin opened by path, with `--skin` or from the menu, is
  compiled by `skinc` run as a child process (`src/skin/compile.c`), so
  a decoder that a hostile file breaks breaks in a process with nothing
  of the app's in it. The decoders are linked into `skinc` alone.
- The runtime lays the skin out and paints it inside the skin area
  alone, below the strips, and never inside the terminal's rectangle
  (`src/skin/view.c`). The clip is the runtime's, so no CSS rule moves
  a pixel past it: not `!important`, `position: fixed`, a z-index or a
  `:host` rule. The host's own box belongs to the app (decision 0037).
- The identity strip, the tab strip, the terminal, the drop-down and
  the dialogs are drawn by the app over the skin (`src/app/gui.c`), and
  the menu button and the splits' handles by the runtime after the
  skin. None of them is an element of the skin's tree.
- Fonts come from the manifest's font list alone. skinc's font checker
  (`src/text/fontcheck.c`) passes TrueType outlines only, and the pack
  loader runs it again before stb_truetype reads a font.
- An SVG file is used only as an image: skinc draws it to pixels with
  nanosvg, which runs no script, and the pack holds the pixels.
- Panel elements are divs skinc creates, with class names the manifest
  gives, restricted to `[a-z0-9-]+`.

## Why

CSS cannot run script, and with no script source a skin can name, the
skin is inert data. `skinned` holds no CSS engine and no decoder of a
skin's files, so what reaches it is a pack it checks byte by byte. The
clip to the skin area is the runtime's own, not a CSS property, so no
rule of the skin's can breach it.

## Rejected

- Containment on the shadow host, in the JavaScript app: `:host { ...
  !important }` overrode it.
- `@font-face` in skin CSS: fonts come from the manifest, whose files
  skinc checks. In the JavaScript app it did not register inside a
  shadow root in Chromium.
- A CSS engine or the image, font and sound decoders in `skinned`
  (decisions 0036 and 0037): a hostile skin file would be parsed in the
  session's process.
- Packing .json, .txt or .md files: nothing in the runtime needs them.
