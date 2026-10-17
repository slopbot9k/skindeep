# 0024 Skin sounds

Status: accepted 2026-10-12, revised 2026-10-17 for the C port
(decision 0036).

## Context

The coffee shop skin plays quiet ambient sounds. Skins are
data and run no code (decision 0005), and decision 0015 opened no audio
stream for the visualization. A skin's sounds need a declaration the
validator checks, a player the app owns, and a way to silence them.

## Decision

- The manifest's optional `sounds`: at most 16 entries `{ file, mode,
  volume?, interval? }`. `file` is a `.ogg`, `.opus`, `.mp3` or `.wav`
  file inside the skin, by the validator's rule. skinc then refuses
  `.opus` and `.mp3` as files it cannot compile, so a skin's sounds are
  Ogg Vorbis or PCM WAVE (decision 0037). `mode` is `loop`, played
  without end, or `random`, played once after a wait drawn evenly from
  `interval`, `[min,
  max]` seconds with 1 <= min <= max <= 3600, then waiting again.
  `volume` is 0 to 1, default 0.3. A loop with an interval, an unknown
  field, a missing file or a file of another type is an error, as
  elsewhere in the manifest.
- skinc decodes each sound into the pack: mono, a stereo source mixed
  down, at its own rate of 8 to 48 kHz, at most 300 s, stored as IMA
  ADPCM (`skinc/media.c`). The app decodes no sound file.
- The player, `src/audio/sounds.c`, one a window, plays the current
  skin's declared sounds and nothing else, from its pack, through one
  mixer for the launch (`src/audio/mixer.c`), at half the
  manifest's volume: the loudest a skin can play is half of full scale,
  the default 0.15. A random sound's first play waits its interval too,
  so no one-shot sounds at launch.
- The mixer's output is the system's (`src/platform/*/audio.c`): an
  AudioToolbox AudioQueue on macOS, and on Linux the first of
  `pw-play`, `paplay` and `aplay` on PATH, a child fed raw samples
  through a pipe. It opens the first time a sound may play and stops
  after 3 s of silence. With no device the sounds are silent, with a
  log line.
- Only while the skin is current: choosing another skin, or the same
  skin again (a reload), stops every sound and every wait, then starts
  the new skin's.
- Mute: for the launch, every window alike (decision 0023). `--mute` of
  `skinned` or `SKINNED_MUTE=1` starts
  muted, and the menu's Mute skin sounds and Unmute skin sounds toggle
  it. Muted, nothing plays and nothing waits. Unmuted, the skin's sounds
  start over. A player starts muted until the app has set it, so a
  muted launch never sounds. The CLI's environment never holds
  `SKINNED_MUTE`.
- A loop plays its decoded samples again from the start, so a sound
  made to loop loops without a gap.
- Nothing is captured for the skin's sounds. No microphone. The
  visualization's sound output input (decision 0015)
  records the default sink's monitor, which carries these sounds too.
- The tests of `tests/audio/` play through the null sink of
  `platform/audio.h`, so nothing they play reaches a speaker, and read
  each sound's state from the player.

## Why

Sound files a skin declares are data, like its images: the app decides
when and how loud they play, and the validator sees every file. Half
volume and a first wait keep a skin quiet by construction, and one mute
covers every skin.

## Rejected

- Sounds tied to session events (a cup set down when a turn ends): the
  issue left them optional, and the random plays already make the room.
- Remembering the mute across launches: `--mute` and `SKINNED_MUTE` say
  it once for every launch.
- An AudioContext for gapless loops, in the JavaScript app: a media
  element looped a sound made to loop, with no new audio graph in the
  page. The C mixer loops the samples itself.
- The menu entry only for a skin with sounds: muting before switching to
  such a skin would be impossible.
