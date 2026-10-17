# 0017 Assets, fonts and their licenses

Status: accepted 2026-10-11, revised 2026-10-13 for the app's own fonts,
2026-10-16 for vendored C code and the C port's fonts (decision 0036),
and revised 2026-10-17 for the C port (decision 0036).

## Context

Only CC0, public domain, MIT, BSD, Apache-2.0, zlib, Unlicense or CC-BY
with credit may ship, and fonts may also be SIL OFL, kept beside their
license. Every asset goes in one manifest.

The app's own text, the strip, the menu, the widgets' defaults and every
terminal, names DejaVu Sans Mono and DejaVu Sans. In the JavaScript app,
a machine without them drew whatever its fallback font was, with other
cell sizes in the terminal.

## Decision

- `ASSETS.json` at the repository root: an array of `{path, source,
  author, license, notes?}`, license as an SPDX id. Files the project
  draws itself are listed with author `claude-skins` and license
  `project-original`.
- Fonts live in the skin that uses them:
  `skins/<skin>/fonts/<Family>/<file>` beside that family's license file,
  copied verbatim.
- Images: `skins/<skin>/images/`.
- Vendored C source (decision 0036): `third_party/<library>/`, each file
  beside its library's licence, copied verbatim. Each is listed in
  `ASSETS.json` like an asset, with the same licence list and the
  commit it was copied from.
- `skinned` embeds DejaVu Sans Mono, in its four styles, and DejaVu
  Sans and DejaVu Sans Bold (decision 0036), from `third_party/dejavu/`,
  the 2.37 release unchanged, beside its licence. The Bitstream Vera
  license (`Bitstream-Vera`) is allowed for those files alone. The
  system's fonts are read only for characters DejaVu lacks
  (`src/text/discover.c`), and `install.sh` asks for no font. The
  JavaScript app took both fonts from the system, as a prerequisite
  that `install.sh` checked with `fc-list`.
- A test walks `skins/` and fails for any file that is neither the
  manifest, the CSS, nor listed in `ASSETS.json` with an allowed license,
  and for any listed file that does not exist.
- Skin names are generic words (`deck`, `phosphor`), free of Winamp,
  Pip-Boy, Fallout and Vault-Tec, and no shipped file carries those names
  or their art.

## Why

The manifest is checkable, so the licence rule holds as skins change.
Fonts inside each skin keep a skin a self-contained directory.

## Rejected

- One shared fonts directory: a skin would no longer be self-contained.
- Shipping DejaVu in `app/fonts/` (2.6 MB, under the Bitstream Vera
  license, done and undone on 2026-10-13): Debian, Ubuntu, Fedora and
  openSUSE package the 2.37 release, the last there is, and Arch the
  same with 18 later upstream commits, so a bundled copy draws nothing
  different, and on Debian and Ubuntu the libraries Electron needs
  already pull it in. The C port of decision 0036 embeds both fonts all
  the same, because macOS does not ship them.
- A Markdown table manifest: harder to check mechanically.
