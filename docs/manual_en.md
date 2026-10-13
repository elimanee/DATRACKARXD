# DATRACKARXD

Just a tracker (like keygen music lmao) for my needs. A small tracker in the spirit of Furnace
(https://github.com/tildearrow/furnace), written in C++ with Dear ImGui and SDL2: chiptune sounds,
real emulated chips (AY / YM2149, SN76489, Game Boy, OPL3), 4-operator FM synthesis, SID-style
synthesis, samples, per-channel and master effects, MIDI keyboard input, and import of Furnace,
DefleMask, FamiTracker, MOD, XM, IT, S3M and MIDI modules.

C++17 - ImGui + SDL2

## Features

- 1 to 32 channels, up to 8 effect columns per channel
- Instrument types:
  - **Chip**: pulse (4 duty), 32-step triangle (NES), saw, LFSR noise (normal / metallic / periodic), sine, 32x16 wavetable
  - **FM** 4 operators in the style of the Yamaha OPN (Mega Drive): 8 algorithms, feedback, AR/DR/SL/D2R/RR envelopes, presets
  - **Sample**: `.wav`, `.flac`, `.mp3`, `.ogg` files or microphone recordings, loops (forward, ping-pong, backward), multi-samples per note, fadeout
  - **Real emulated chips**: **AY-3-8910 / YM2149** (ZX Spectrum, CPC, Atari ST, with the "buzzer" envelope),
    **SN76489** (Master System), **Game Boy** (pulse with sweep, wave, noise) and **OPL3** (AdLib, 2 or 4 operators)
- **ADSR envelope**, two **LFOs** (pitch, volume, pulse width, cutoff, panning) and **wavetable morphing** on instruments
- **Per-channel effects**: bitcrusher, distortion, chorus, 3-band EQ, compressor
- **Sample editor**: selection, zoom, cut / copy / paste, crop, normalize, fades, reverse,
  amplify, resample, pencil drawing, loop on selection, microphone recording
- **SID-style synthesis** (C64): resonant low-pass / band-pass / high-pass filter on any instrument, ring mod, hard sync, fine pulse width with PWM
- Furnace-style **macros** on all instruments: volume, arpeggio (relative or fixed note), duty, waveform, pitch, cutoff (with loop `|` and release `/`)
- **Mixer**: reverb, tempo-synced ping-pong delay, master filter, per-channel sends
- **MIDI keyboard**: play, play chords and record from a MIDI keyboard (velocity included)
- **Instrument presets**: `.dti` (save / load), import of `.fui` (Furnace) and `.wav`
- **Import** of `.fur` (Furnace), `.dmf` (DefleMask), `.ftm` / `.dnm` / `.0cc` (FamiTracker), `.mod`, `.xm`, `.it`, `.s3m` and `.mid` files
- **Export** to WAV, OGG, MIDI, **XM / MOD / IT modules** (synthesized instruments are rendered to samples)
  and **VGM** (the registers of the emulated chips, for VGM players and real hardware)
- **Keygen player**: a standalone, compressed program that plays the song with a logo, animated background (stars,
  plasma, tunnel, rotozoom), 3D object, scrollers, VU meters and flashes in time with the music
- **DATRACKARXD Keygen**: a separate app for building these players, with a live preview
- Themes: Furnace, **Keygen** (neon, with a sine scroller, copper bars and a starfield), FastTracker, Amber CRT
- Keyboard-driven pattern editor (physical key layout, works with both AZERTY and QWERTY), selection, copy/paste, transpose
- FL Studio-style piano roll, with a chord mode ("Voices") that spreads notes across several channels
- **Arrangement view**: the song as a timeline (channels x orders) with a note preview, several **subsongs** per file
- Curve-based **automation**: volume, panning, cutoff, sends, effect settings and master over the whole song
- **Pattern tools**: interpolate, humanize, scale, reverse, expand / shrink, find / replace, swing
- **Metronome**, **loop** over a range of orders, **count-in** before recording, **quantization** of MIDI recording
- Undo / redo everywhere: notes, instruments, orders, song settings, samples
- **Settings**: audio output and latency, custom keyboard shortcuts, interface size (75 to 250 %), theme
- **English or French interface**, with this built-in manual (Help > Manual, `F1`)
- **Autosave** and recovery after a crash, recent files, drag and drop of songs, instruments and
  samples onto the window
- Per-channel order list (like Furnace), add / duplicate / clone
- Per-channel oscilloscope, mute / solo
- Saving in a `.dtk` text format, export to WAV (full mix or one track per channel), OGG, MIDI, XM, MOD, IT and VGM
- A built-in keygen demo that loads at startup, and `songs/chips.dtk` to hear the emulated chips

## Download

Compiled builds for Windows, Linux and macOS are on the Releases page
(https://github.com/elimanee/DATRACKARXD/releases). Just unzip the archive and
run `datrackarxd` (or `datrackarxd.exe`): there is nothing else to install.

- **macOS**: the application is not signed. The first time you launch it, right-click > Open, or run
  `xattr -d com.apple.quarantine datrackarxd` in a terminal.
- **Windows**: SmartScreen may show a warning for the same reason; click "More info" > "Run anyway".

The archive contains two programs: `datrackarxd` (the tracker) and `datrackarxd-keygen` (the
DATRACKARXD Keygen app, see below).

To publish a new version, update `VERSION` in `CMakeLists.txt`, then either:
- in the **Actions** tab on GitHub, open the "Build" workflow, click "Run workflow" and
  type the version (for example `v0.2.0`);
- or push a `vX.Y.Z` tag.

GitHub Actions builds for all three systems, creates the tag if needed and publishes the release with the archives.

## Building

You need CMake >= 3.16 and a C++17 compiler. ImGui, RtMidi, libogg, libvorbis, ymfm and dr_libs are
downloaded automatically; so is SDL2 if it is not installed. On Linux, MIDI input uses ALSA (`libasound2-dev`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/datrackarxd              # or build\Release\datrackarxd.exe on Windows
ctest --test-dir build           # the tests (add -C Release on Windows)
```

On Linux, installing `libsdl2-dev` saves you from compiling SDL2.

## Command line

```sh
datrackarxd song.dtk                       # open a song (or a .fur/.mod/.xm/.it/.s3m)
datrackarxd --export song.xm out.wav 2     # render to WAV (2 loops)
datrackarxd --export song.ftm out.ogg      # render to OGG Vorbis
datrackarxd --export-midi song.dtk out.mid # write a MIDI file
datrackarxd --export-stems song.fur piste  # one WAV track per channel: piste_01.wav, piste_02.wav...
datrackarxd --export-module song.dtk out.xm   # XM, MOD or IT module (based on the extension)
datrackarxd --export-vgm song.dtk out.vgm     # emulated chip registers as VGM
datrackarxd --convert song.it song.dtk     # import a module (or a .mid) and save it as .dtk
datrackarxd --convert-instrument lead.fui lead.dti   # instrument preset from a .fui / .wav
datrackarxd --export-player song.dtk intro "LOGO" "texte du scroller"   # standalone keygen player
datrackarxd --export-demo demo.wav         # render the demo
datrackarxd --save-demo demo.dtk           # write the demo to a file
```

## Keyboard

| Keys | Action |
|---|---|
| `Z S X D C V G B H N J M` | Notes (bottom row, physical position) |
| `Q 2 W 3 E R 5 T 6 Y 7 U I` | Notes, octave + 1 |
| `1` or `²`/`` ` `` | Note off |
| `0-9 A-F` | Hex values (instrument, volume, effects) |
| `Space` | Record (edit) mode on/off |
| `Enter` / `Shift+Enter` | Play/stop from the start of the order / from the cursor |
| `F5` / `F6` / `F8` | Play the song / play from the cursor / stop |
| Arrows, `PgUp/PgDn`, `Home/End` | Move around |
| `Tab` / `Shift+Tab` | Next / previous channel |
| `Shift` + movement, mouse drag | Selection |
| `Ctrl+C / X / V` | Copy / cut / paste |
| `Ctrl+A` | Select the channel, then the whole pattern |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+Up/Down` (+`Shift`) | Transpose by 1 (12) semitones |
| `Ctrl+I` | Interpolate the volumes and effect values of the selection |
| `Ctrl+H` | Find / replace |
| Right-click in the pattern | Selection tools (see below) |
| `Delete` | Clear |
| `Insert` / `Backspace` | Insert / delete a row |
| Numpad `/` `*` | Octave - / + |
| `Ctrl+N / O / S` | New / open / save |
| `Ctrl+Shift+S` | Save as |
| `Ctrl+,` | Settings |
| `F1` | Manual |
| Click on a channel name | Mute (right-click: solo) |

Outside the pattern editor, the note keys play the current instrument (handy in the instrument editor).

These are the default keys: every shortcut except the notes and the cursor keys can be changed in
**Settings > Keyboard**. On macOS, `Cmd` works like `Ctrl`.

## Settings, autosave and files

**File > Settings...** (`Ctrl+,`):

- **Audio**: the output device (or the system's) and the buffer size, with the latency it gives. A
  small buffer answers the keyboard faster; if the sound crackles, raise it. An unplugged device is
  replaced by the system's one.
- **Interface**: the language (English / Francais), the interface size (75 to 250 %, for big screens)
  and the theme.
- **Keyboard**: click a shortcut, then press the new key (with `Ctrl`, `Shift` or `Alt` if you like);
  `Esc` cancels, `Backspace` removes the shortcut. Two actions on the same key are shown in orange. The
  menus always show the keys you chose.
- **Files**: autosave (off, or every 1, 2, 5 or 10 minutes) and clearing the recent files list.

**Autosave**: unsaved changes are copied to the settings folder. If DATRACKARXD closes without saving
(crash, power cut), the next start offers to **recover** that copy. Saving the song or quitting normally
removes it. Songs are also written to a temporary file first, then moved over the old one: an
interrupted save never leaves a half-written file.

**File > Open recent** keeps the last 10 songs. You can also **drag and drop** onto the window a song
(it opens, after asking if there are unsaved changes), an instrument (`.dti`, `.fui`, `.wav`, added to
the list) or a sample (`.flac`, `.mp3`, `.ogg`, added to the samples).

The settings folder (`datrackarxd.cfg`, the window layout `datrackarxd.ini` and the autosave) is:

- Windows: `%APPDATA%\DATRACKARXD\datrackarxd\`
- macOS: `~/Library/Application Support/DATRACKARXD/datrackarxd/`
- Linux: `~/.local/share/DATRACKARXD/datrackarxd/`

If a `datrackarxd.cfg` is in the current folder (portable use, or settings from earlier versions),
everything stays in that folder.

## Effects

| Effect | Description |
|---|---|
| `00xy` | Arpeggio: note, +x, +y semitones |
| `01xx` / `02xx` | Pitch slide up / down (xx/32 semitone per tick) |
| `03xx` | Portamento to the note on the row |
| `04xy` | Vibrato: speed x, depth y |
| `05xy` / `06xy` | Volume slide while the vibrato / portamento continues |
| `07xy` | Tremolo: speed x, depth y |
| `08xy` | Panning: left volume x, right y |
| `0Axy` | Volume slide: +x or -y per tick |
| `0Bxx` | Jump to order xx |
| `0Cxx` | Retrigger the note every xx ticks |
| `0Dxx` | Go to row xx of the next order |
| `0Fxx` | Speed (ticks per row) |
| `10xx` | Waveform (0 pulse ... 5 wavetable; wavetable number on a wavetable channel). AY: mixer (1 tone, 2 noise, 4 envelope). OPL: waveform 0-7 |
| `12xx` | Duty (0-3), or pulse width (01-FF) with "Fine pulse width". Noise: 1 = metallic, 3 = periodic. AY: noise period. SN / Game Boy noise: mode |
| `13xx` | Instrument filter cutoff (00-FF) |
| `80xx` | Linear panning: 00 left, 80 center, FF right |
| `90xx`-`92xx` | Sample start position (bytes 0, 1, 2) |
| `C0xx`-`C3xx` | Tick rate in Hz |
| `E1xy` / `E2xy` | Slide up / down by y semitones at speed x |
| `E5xx` | Fine pitch: `80` = none, `00`-`FF` = -1 to +1 semitone |
| `E6xx` | Pattern loop: `E600` marks the start, `E6xx` repeats xx times |
| `E8xx` | Pitch bend: (xx - 80) / 16 semitones (`00` = -8, `80` = none, `A0` = +2, `FF` = +7.9). Glides over the row and stays until the next `E8`/`E9` |
| `E9xx` | Wide pitch bend: (xx - 80) / 4 semitones (`00` = -32, `80` = none, `B0` = +12, `FF` = +31.75), same glide |
| `ECxx` | Cut the note after xx ticks |
| `EDxx` | Delay the row by xx ticks |
| `EExx` | Repeat the row xx times (pattern delay) |
| `F0xx` | Tempo in BPM |
| `F1xx` / `F2xx` | Small pitch slide, once |
| `F3xx` / `F4xx` | Raise / lower the volume by xx, once |

Volume column: `00`-`7F`. An effect set to `00` stops the arpeggio, slides or vibrato.

## Piano roll

The **Piano roll** tab (next to Pattern) shows the current channel's pattern as notes,
as in FL Studio:

| Action | Result |
|---|---|
| Click in the grid | Adds a note ("Length" long, current instrument), which you can drag right away |
| Drag a note | Move it (time and pitch) |
| Drag the right edge | Change its length |
| Right-click (or drag) | Erase |
| Ctrl + drag | Rectangle selection (Shift to add), Ctrl+A to select all |
| Arrows | Move the selection (Up/Down: semitone, Shift: octave), Delete to erase |
| Mouse wheel | Scroll; Shift: horizontal, Ctrl: time zoom, Alt: pitch zoom |
| Keyboard on the left | Listen to a note |
| Ruler at the top | Place the cursor (and playback) |
| Bottom lane, "Vol" | Draw the velocity (volume column) |
| Bottom lane, "Pitch" | Draw the channel's pitch bend (`E8xx`/`E9xx`); right-click to erase, Ctrl to snap to semitones, "+-N" changes the range |
| Bottom lane, "Press" / "Slide" | Per-note expression (see MPE below) |

Greyed-out notes belong to the other channels ("Ghosts" checkbox). A tracker channel plays only one
note at a time: if two notes overlap, the first one stops where the next one starts.

For **chords**, set "Voices" to 2 to 8: the piano roll then shows the current channel and the following
ones together, and each note you place goes to a free channel. Everything can be undone with Ctrl+Z.

### Per-note expression (MPE)

Like Ableton's MPE, the **MPE** checkbox gives each note its own curves, which move with it
(move, copy, transpose):

| Action (MPE checked) | Result |
|---|---|
| Drag along a note | Draw its pitch curve (bend), shown on the note; Ctrl to snap to semitones |
| Right-click (or drag) on a note | Erase the curve at that point |
| Double-click on a note | Reset all of its expression to zero |
| Right edge of a note | Change its length, as usual |
| "Press" lane | Pressure: the note's volume over time (volume column) |
| "Slide" lane | Timbre: the instrument's filter cutoff (`13xx`, filter enabled) over time |

The Press and Slide lanes change the selected notes, or otherwise all the notes under the mouse.
In the pattern, a curve is written as `E8xx` (`E9xx` beyond ±8 semitones) on the note's rows,
with an `E880` at its end so that the next note starts again without bend. A channel plays only one note
at a time: with "Voices", each note of a chord therefore gets its own bend, as in MPE.

## MIDI keyboard

The **MIDI** menu lists the MIDI inputs (your choice is remembered the next time you launch the app). Notes play
the current instrument; a chord is spread over the piano roll's "Voices" channels. In **Record** mode:

- when stopped, notes are written on the cursor row, which moves forward once all keys are released;
- during playback, they are written on the row being played, with a note off when you release the key.

"Velocity -> volume" puts the velocity in the volume column; a "program change" selects the instrument.
The **pitch bend wheel** bends the notes being played ("Pitch wheel range" sets the range,
2 semitones by default); in Record mode during playback, it is written as `E8xx` row by row.

**MPE controller** (Roli Seaboard, LinnStrument, Osmose, Push 3...): check "MPE controller" in the
MIDI menu. Each note arrives on its own MIDI channel and gets its own tracker channel (8 voices if
"Voices" is 1). Per note:

- the MIDI channel's pitch bend bends the note ("MPE pitch range", 48 semitones by default as in the
  MPE standard) -> `E8xx`/`E9xx`;
- CC74 (slide) sets the filter cutoff -> `13xx`;
- pressure (channel aftertouch) varies the volume between 30 % and the velocity -> volume column.

In Record mode during playback, everything is written row by row on the note's channel, with `E880` at
the note off if the note was bent.

## Arrangement and subsongs

The **Arrangement** tab (next to Pattern) shows the order list as a timeline: one row per channel, one
cell per order with the pattern number and a preview of the notes.

| Action | Result |
|---|---|
| Click / Shift+click / drag | Select an order (and the channel), extend the order selection |
| Double-click | Open the cell in the pattern editor |
| Right-click on a cell | Pattern number +1 / -1, new empty pattern, make unique copy, clear |
| Alt+wheel | Change the pattern number |
| Ctrl+wheel / Shift+wheel | Zoom / scroll |
| New, Dup, Clone, Del, left / right arrow buttons | Insert, repeat, clone, delete, move the selected orders |
| Loop | Playback loops over the selected orders |

At the top left is the list of **subsongs**: each one has its own orders, patterns, tempo and
automation, and shares the instruments, samples and channel settings (handy for game
music with several tracks, or an intro and a loop). `+` adds one, `-` deletes
the one shown. Exports and the keygen player play the subsong that is shown.

## Automation

Below the timeline, **Add lane** adds a curve: volume, panning, instrument filter cutoff,
reverb send, delay send or a setting of a channel effect (Channel FX) of a channel, or master volume and
master cutoff. Click: add a point, drag: move it, right-click: delete it. Points snap to
rows (hold Alt to place them freely). The curve holds its value before the first point and after the
last one, and follows the orders when you insert, duplicate, move or delete them.

## Pattern tools

In the Edit menu and on right-click in the pattern, applied to the selection (or, with no selection, to the
whole channel under the cursor):

- **Interpolate** (`Ctrl+I`): fills in the volumes and effect values between the first and last value;
- **Scale volumes** (25 to 200 %), **Humanize volumes** (random variation of ±8);
- **Reverse rows**, **Expand x2** (one row out of two, twice as slow), **Shrink /2** (twice as fast);
- **Find & replace** (`Ctrl+H`): find a note, an instrument or an effect command, and transpose,
  change the instrument, volume or command, in the selection, the pattern or the whole song.

In the Song window, **Swing...** sets the speed and the swing (rows alternate between a
long one and a short one).

## Metronome, count-in and quantization

**Metronome** in the top bar clicks on the beats during playback (with an accent on each bar,
based on Highlight 1 and 2); it is never heard in exports. **Rec. options** sets:

- **Count-in**: in Record mode, playback starts after 1 to 4 bars of clicks;
- **MIDI quantize**: notes played on the MIDI keyboard during playback go to the nearest row,
  every 2 rows or on the beat, instead of the current row;
- the metronome volume.

## Mixer and effects

The **Mixer** window contains the reverb, the delay (time in rows, so it is synced to the tempo,
with a ping-pong option), the master filter and the main volume, then for each channel: level, panning,
reverb send and delay send. Everything is saved in the song.

A channel's **FX** button opens the **Channel FX** window: a chain of effects processed from left to
right, before the mixer (up to 8). The arrows change the order, the checkbox turns an effect on or off, and
double-clicking a setting resets it to its default.

| Effect | Settings |
|---|---|
| Bitcrusher | bits (1-16), downsampling, mix |
| Distortion | drive, tone, mix, type (soft, hard clipping, foldback) |
| Chorus | rate, depth, mix, feedback |
| EQ | bass (200 Hz), mids (adjustable frequency), treble (4 kHz), from -24 to +12 dB |
| Compressor | threshold, ratio, attack, release (with automatic makeup gain) |

## Emulated chips

An instrument's **Type** can be a real chip. Each channel that plays it has its own chip, emulated at the
register level: integer periods (high notes are slightly out of tune, as on the real
machine), 16-step volume, and the original noise generators and hardware envelopes.

| Chip | Settings | Duty / Waveform macros |
|---|---|---|
| **AY-3-8910 / YM2149** | clock (ZX, CPC, Atari ST, MSX), model, tone / noise / envelope, noise period, envelope shape and speed | noise period / mixer (1 tone, 2 noise, 4 envelope) |
| **SN76489** | clock (NTSC, PAL, BBC), tone, white or periodic noise | noise mode |
| **Game Boy** | pulse channel (duty, envelope, sweep), wave (the instrument's wavetable, 4 volumes) or noise (short or long) | duty / - |
| **OPL3** (AdLib) | 2 or 4 operators, algorithm, feedback, and per operator MULT, TL, AR, DR, SL, RR, KSL, waveform (8), sustain, tremolo, vibrato, KSR | - / waveform |

AY tip: the Atari ST **buzzer** sound is obtained with Tone and Noise unchecked, Envelope checked, a
sawtooth (8) or triangle (10) shape and "Env. pitch" set to "Note x1": the envelope becomes the
waveform and follows the note. On Game Boy, a volume macro replaces the hardware envelope.

The OPL3 is emulated by ymfm (https://github.com/aaronsgiles/ymfm). The SID filter, ADSR, LFOs and
per-channel effects also work on the chips.

## Envelope, LFO and morphing

The **Envelope & LFO** section of the instrument editor (all types) adds:

- an **ADSR** envelope (attack, decay, sustain, release in seconds) on top of the volume; the
  note keeps sounding during the release after the note off;
- two **LFOs** restarted on each note: target (pitch, volume, pulse width, cutoff, panning),
  shape (sine, triangle, saw, square, random), speed in Hz, depth and delay before they
  kick in.

On a wavetable instrument (and the Game Boy wave channel), **Morph** moves the waveform from the
wavetable to a second wavetable, once, looping or back and forth.

## Sample editor

The **Samples** tab (next to Pattern) loads `.wav`, `.flac`, `.mp3` and `.ogg` files (mixed down to mono)
and **Record** records from the selected microphone or line input.

| Action | Result |
|---|---|
| Drag over the waveform | Select (Shift+click to extend) |
| Wheel / Shift+wheel | Zoom / scroll |
| Cut, Copy, Paste, Delete, Crop | Cut, copy, paste (at the cursor or replacing the selection), delete, keep only the selection |
| Silence, Normalize, Fade in/out, Reverse, Invert, Remove DC | On the selection, or the whole sample with no selection |
| Amplify | Multiply by the chosen percentage |
| Resample | Change the sample rate without changing the pitch (lower = grittier and smaller) |
| Loop selection | Loop over the selection |
| Pencil | Draw the waveform with the mouse (zoom in for precision) |

Everything can be undone with Ctrl+Z.

## SID-style synthesis

In the instrument editor, the **SID** section adds:

- a resonant **filter** (low-pass, band-pass, high-pass) on any instrument type, with the
  **Cutoff** macro and the `13xx` effect to move it;
- **Ring mod** and **Sync** with the previous channel (channel 1 uses the last one), like the C64 voices;
- **Fine pulse width**: pulse width 1-255 instead of 4 duty values, with automatic **PWM** sweep.

C64 instruments from Furnace and DefleMask files are imported with their filter, ring mod and sync.

## Instrument presets

In the instrument list, **Save...** saves the selected instrument as `.dti` (with its samples and
wavetables) and **Load...** adds an instrument from a `.dti`, a Furnace `.fui` or a `.wav`.

## Export

In the File menu:

- **Export WAV** / **Export OGG** (adjustable quality) / **Export MIDI** (one track per channel, noise on the drum channel);
- **Export module (XM, MOD, IT)**: see below;
- **Export VGM**: see below;
- **Export keygen player**: choose the logo, the title and the scroller text; you get a standalone
  program that plays the song in a loop with the starfield, copper bars, logo and
  scroller (Space: pause, F: fullscreen, Esc: quit). The program needs nothing else.

An **Export report** window then lists whatever could not be carried over to the chosen format.

### XM, MOD and IT modules

The song becomes a module that can be played by FastTracker 2, ProTracker, Impulse Tracker, OpenMPT, MilkyTracker,
Schism Tracker or libxmp:

- sample instruments keep their samples (resampled to 8363 Hz for MOD);
- the others (chip, FM, SID, emulated chips, with their macros, ADSR and LFOs) are **rendered
  to samples**: a held note is recorded, and if the sound keeps going a clean loop is searched for in it;
  otherwise the sample contains the note and its release. Settings: the render sample rate and
  the length of the held note;
- effects (arpeggio, slides, portamento, vibrato, tremolo, volume, panning, retrigger, jumps,
  speed, tempo) are translated; a second effect goes into the volume column when the format allows it;
- XM: 32 channels maximum; MOD: 4 to 32 channels, notes from C-3 to B-5 (ProTracker octaves);
  IT: 64 channels, patterns of 200 rows maximum (longer ones are cut).

Not exported, and listed in the report: channel effects, reverb / delay /
master filter, automation and subsongs other than the one shown.

### VGM

The VGM format records the writes to the chip registers: the file can be played with VGM
players (foobar2000 + VGMStream, VGMPlay, vgm2wav...) and can be played on real hardware. Only the channels
that play an **emulated chip** instrument are exported:

- AY-3-8910 / YM2149: 6 voices (two chips);
- SN76489: 3 square voices, or 2 voices + noise;
- Game Boy: the two pulses, the wave and the noise;
- OPL3: 18 2-operator voices or 6 4-operator voices.

Channel effects, the mixer and automation do not exist on the chips and are therefore not in the
VGM; the report lists them, along with the channels that were left out.

## DATRACKARXD Keygen (separate app)

`datrackarxd-keygen` is used only to build keygen players:

1. open a song (**Open song...** button or drag and drop a file: `.dtk`, `.fur`, `.dmf`,
   `.ftm`, `.mod`, `.xm`, `.it`, `.s3m`, `.mid`); it starts playing right away;
2. set the logo, the window title and the scroller text;
3. adjust the look; the preview shows exactly what you will get:
   - background: starfield, plasma, tunnel, rotozoom or black, or **a sequence** of all four with
     crossfades (adjustable length for each scene);
   - solid-shaded 3D object: cube, pyramid or octahedron, which grows on the beats;
   - logo: wave, pulsing with the music or glitch;
   - scroller: sine, bounce or roll, with its speed and height; a second **greetings**
     scroller at the top;
   - rainbow colors or a single color, copper bars, oscilloscope, per-channel **VU meters** and
     **flash** on strong beats;
4. **Export player...** writes the standalone player.

The player is a copy of this small app with the song inside, compressed (zlib), so it is lighter
than one made with the tracker. If `datrackarxd-keygen` is next to the tracker, the tracker's export uses it too.

```sh
datrackarxd-keygen                                    # open the app
datrackarxd-keygen song.xm                            # open it with a song
datrackarxd-keygen --export song.fur intro "LOGO" "texte du scroller"
```

## Module import

File > Open / import (Ctrl+O) also opens `.fur`, `.dmf`, `.ftm`, `.mod`, `.xm`, `.it`, `.s3m` and `.mid` files. The song
is converted to the DATRACKARXD engine, so the result is close to the original without being identical:

- **Furnace (.fur)**: each chip channel becomes the closest voice (FM, pulse, triangle, noise,
  wavetable or sample). FM instruments (OPN, OPM, OPL, OPLL), macros, wavetables, samples
  and the Game Boy or C64 envelope (with filter, ring mod and sync) are converted. Chip-specific
  effects (FM LFO, etc.) are ignored.
- **DefleMask (.dmf)**: Genesis, SMS, Game Boy, PC Engine, NES, C64, Arcade, Neo Geo... same as for Furnace.
  Samples play at their original rate.
- **FamiTracker (.ftm, .dnm, .0cc)**: 2A03 with DPCM, and the VRC6, VRC7, FDS, MMC5, N163 and
  Sunsoft 5B expansions; sequences, grooves and tempo.
- **MIDI (.mid)**: notes are aligned to rows (sixteenth notes, or thirty-second notes if
  needed), each part gets a chip instrument based on its General MIDI program and as many channels
  as it has simultaneous notes; drums (channel 10) become chip percussion. Pitch bend
  becomes `E8xx`/`E9xx` (and MIDI export writes the bends, with a range of 12 semitones, or
  48 if the song bends further).
- **MOD / S3M / XM / IT**: samples, loops, multi-samples, volume envelopes (converted to macros),
  fadeout, classic effects (arpeggio, slides, vibrato, tremolo, retrigger, pattern loops...).
  Not supported: panning and pitch envelopes, filters, global and channel volumes, NNA (IT).

Once imported, Ctrl+S saves it as `.dtk`.

## Macros

In the instrument editor, each macro can be drawn with the mouse or typed as text:

```
15 13 11 | 9 8 7 / 4 2 0
```

`|` marks the start of the loop, and `/` after a value holds it until the note off.
A volume macro with a release point gives a real "release" (fade-out after the note off).

## Code

- `src/song.*`: data model and `.dtk` format
- `src/engine.*`: pattern playback, effects, macros, synthesis (chip, FM, samples, SID), WAV export
- `src/effects.*`: resonant filter, reverb, delay, per-channel effects
- `src/chips.*`: emulated chips (AY / YM2149, SN76489, Game Boy, OPL3 via ymfm)
- `src/audiofile.*`, `src/sampleops.*`, `src/recorder.*`: WAV / FLAC / MP3 / OGG loading, sample editor tools, recording
- `src/export.cpp`, `src/midifile.cpp`: OGG export, MIDI import / export
- `src/modexport.*`, `src/vgmexport.*`: XM / MOD / IT and VGM export
- `src/import*.cpp`, `src/fur_internal.h`, `src/inflate.cpp`: FUR, DMF, FTM, MOD, S3M, XM, IT import (and zlib compression / decompression)
- `src/midi.*`: MIDI keyboard input (RtMidi)
- `src/payload.*`, `src/player.cpp`, `src/keygen_fx.*`: standalone keygen player and its scene
- `src/keygen_main.cpp`, `src/filebrowser.*`: the DATRACKARXD Keygen app
- `src/theme.*`: color themes
- `src/app.*`: application, menus, shortcuts, file dialog
- `src/ui_settings.cpp`, `src/shortcuts.*`, `src/paths.*`: settings, shortcuts, autosave, recent files
- `src/i18n.*`, `src/lang_fr.inc`: interface translation (French texts)
- `src/ui_manual.cpp`, `docs/manual_en.md`: built-in manual (README.md in French, `docs/manual_en.md` in English)
- `tests/`: tests (`ctest`) and translation checks
- `src/ui_pattern.cpp`: pattern editor
- `src/ui_instrument.cpp`: instrument list and editor (chip, FM, sample), Samples window
- `src/ui_windows.cpp`: orders, song, mixer, oscilloscope, keygen scroller, help
- `src/ui_arrange.cpp`: arrangement view, subsongs, automation editor
- `src/demo.cpp`: demo song

## Libraries

Dear ImGui (MIT, https://github.com/ocornut/imgui), SDL2 (zlib, https://libsdl.org),
RtMidi (https://github.com/thestk/rtmidi), libogg and libvorbis (BSD, https://xiph.org),
ymfm (BSD-3-Clause, OPL3 emulation, https://github.com/aaronsgiles/ymfm) and
dr_flac / dr_mp3 (public domain / MIT-0, https://github.com/mackron/dr_libs). Their licenses are
in the `licenses` folder of the published archives.
