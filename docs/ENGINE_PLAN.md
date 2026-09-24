# Ammonite — Engine Build Plan

Living design and build notes for Ammonite (developed as `synth_arp`, named
Ammonite on 2026-09-24). Designed in discussion 2026-09-23. It started as a
copy of an earlier drone synth, whose pieces (MorphOsc, envelope, delay loop,
reverb wrapper, pickup) it reuses.

> Paths: this repository was split out of a larger Daisy workspace on
> 2026-09-24. The build history below keeps the old names (`synth_arp\core` is
> now `core\`, `MyProjects_arp` is `firmware\`, `synth_arp\sim` is `sim\`,
> the map file is `firmwareuildmmonite.map`).

## What exists (2026-09-24)

| Piece | Path | Status |
|---|---|---|
| Portable core | `core\` | **build steps 1-7 done** (2026-09-23): all 9 pages and sub-pages, three oscillators with key + chord degree, filters, mix, reverb; master clock, three arps, amp envelopes; chord progression; filter / pitch envelopes + glide; 15 LFOs; per-osc delays; RINGS / PITCH / WHEEL / SCOPE views; fast display SPI (confirmed on hardware). CPU on the Seed still to measure |
| PC simulator | `sim\panel_sim.py` + `synthcore.dll` (`simulator.bat`) | runs; top label = page (and sub-page), captions = function + osc |
| Firmware shell | `firmware\` (`TARGET = ammonite`, builds from `core\`, libs from the `lib\` submodules) | `make` clean, no warnings: flash 90.3 %, SDRAM 7.4 MB (6 delay lines already allocated), `audio.o` at 0x30000000 |
| Tests | `tests\` | `arpsynth.py` helpers, `test_panel.py` (49 checks), `test_voices.py` (22), `test_arp.py` (31), `test_prog.py` (40), `test_env.py` (16), `test_lfo.py` (16), `test_delay.py` (16), `test_screen.py` (12), all pass (193 checks) |
| Hardware | `hardware\stl\`, `hardware\wiring\` | enclosure v1 (six parts), wiring diagrams |
| Manual | `manual\` | owner's manual, A4 PDF. `capture.py` drives the DLL headless (reuses `panel_sim.draw_panel`) for every screen / panel image and plays `recipes.py` through the panel into `recipes.json`; `build.py` prints `ammonite.html` twice with headless Edge (contents page numbers). `msedge.exe` returns before the PDF exists: `build.py` waits for it. Re-run after any change to names, ranges or defaults |
| README media | `media\` | `make_media.py` renders the demo video, GIF, recipe MP3s and images from the engine (and the STLs) |

**The architecture law: every engine feature goes in `core\` only.**
The shells (panel_sim.py, synth_main.cpp) stay thin: controls in, audio and
framebuffer out.

### Workflows (PowerShell, from the repository root)

- Simulator: `.\simulator.bat` (or `.mmonite.ps1 sim`)
- Libraries, once: `.mmonite.ps1 libs`
- Firmware build: `.mmonite.ps1 build`
- Flash: `.mmonite.ps1 flash` (bootloader: hold BOOT, tap RESET, release BOOT)
- Firmware check from Git Bash: `cd firmware && make`
- DLL build: `simuild_dll.ps1` (add new DaisySP .cpp files to its `$sources`)
- Headless screenshot: `SIM_SHOT=<png>` + `SDL_VIDEODRIVER=dummy` on panel_sim.py
- Tests: `.mmonite.ps1 test`, or `python tests	est_x.py`
  (env `SYNTH_DLL=<path>` points them at another DLL, e.g. one built elsewhere
  while a running simulator holds `sim\synthcore.dll` open)
- Python: `$env:AMMONITE_PYTHON` if set, else `python` (packages in `requirements.txt`)
- Manual: `.mmonite.ps1 manual`; README media: `python media\make_media.py`

---

## The instrument (user-decided 2026-09-23)

An **arpeggiator box built on three independent monophonic oscillators**
(OSC 1-3). Each oscillator has its own shape, pitch, arp, envelopes, filter,
LFOs, delay and mix. A shared key (ROOT + SCALE) and an optional chord
PROGRESSION tie them together; one master clock (TEMPO, SWING) drives all
three arps and the chord changes. An oscillator with its arp OFF drones, so
the old drone box is one setting of this one.

Dropped from the reference: CHAOS, SHIMMER (all pitch shifters), TEXTURE
(sub, noise, ensemble), the FREE / NOTES tuning modes.

### Signal flow

```
per OSC n (x3, mono):
  2 detuned MorphOsc (SHAPE, DETUNE)            pitch = key note + PITCH env + LFO + glide
  -> drive (FILTER page DRIVE, shared amount), per oscillator
  -> SVF, LP -> BP -> HP morph (CUTOFF, RESO, TYPE; FILTER env, LFO, master CUTOFF)
     (two SVFs per osc, one per oscillator, so WIDTH can pan them apart)
  -> amp envelope (A, D, S; release = D) x LEVEL x AMP LFO
  -> scope capture (post filter + env, pre pan)  -> screen ring n
  -> pan (PAN + PAN LFO), WIDTH spreads the osc's two oscillators L/R
  -> dry sum
  -> own stereo delay (DIVISION, FEEDBACK, SEND x master DELAY, TONE, WOBBLE, WIDTH)
  -> reverb send = (dry + delay return) x REV SEND x master REVERB
sum of dry + 3 delay returns + ReverbSc (SIZE, DAMPING, PREDELAY)
  -> soft limiter -> VOLUME -> out L/R
```

Modulation per oscillator: 5 LFOs (one per target: PITCH, CUTOFF, AMP, PAN,
SHAPE), a FILTER envelope and a PITCH envelope, all computed on the
**control tick, every 16 samples** (0.33 ms; changed from "once per block"
in step 4, since the sim's 5 ms blocks are too coarse for fast envelopes).
The amp envelope stays **per sample** (steps would click).

### Clock (one master, never three timers)

- One beat position (`beat_`, quarter notes since Init) advances by TEMPO
  every block, so a tempo change never jumps it; a bar is 4 beats.
- Each oscillator's step index is derived from that position and its
  DIVISION, with SWING applied per division (even steps stretched up to a
  third, odd ones shrunk). Polyrhythms therefore stay locked forever.
- Events are sample-accurate inside the block (onset on its sample, gates
  and ratchet hits counted in samples).
- Chord changes and RESTART happen on bar lines of the same counter.

### Harmony

- **ROOT** = the key's tonic, C..B. **SCALE** = one list of 10 scales then
  10 chords (the reference's `kScaleMask` / `kChordMask`, TYPE knob folded in).
- The **current chord** is a scale degree: from the PROGRESSION when PROG is
  on, else from KEY page CHORD (manual, default I).
- Each oscillator's note pool: start at (current chord degree + the osc's
  DEGREE), build the POOL type from the scale (KEY = every scale note,
  TRIAD / 7TH = stacked diatonic thirds, FIFTHS = root + in-key fifth,
  ROOT = octaves only), repeat over RANGE octaves, place at base octave 2
  + the osc's OCTAVE + master OCTAVE.
- **Chord keys** (SCALE set to a chord): POOL KEY = the chord's tones; the
  progression moves the chord's root along its parent scale (natural minor
  for MIN, MIN7, DIM; major otherwise) and keeps the chord quality
  (parallel chords). *Tune by ear once it runs.*
- MODE OFF: the oscillator drones on its pool's first note (its DEGREE on
  the current chord) and follows chord changes with its GLIDE.

---

## Panel

### Conventions

- **Knob 11 = PAGE selector** (stepped, 9 pages, `StepWithHyst`),
  **knob 12 = VOLUME** on every page. Unchanged from the reference.
- **Column = oscillator** on every column page:

  ```
  LEFT GROUP              RIGHT GROUP
  [1 ] [2 ] [3 ]          [7 ] [8 ] [9 ]     row A (1-3), row C (7-9)
  [4 ] [5 ] [6 ]          [10] [PAGE][VOL]   row B (4-6), knob 10
  osc1 osc2 osc3          osc1 osc2 osc3
  ```

  Row A, row B and row C each hold one function for all three oscillators.
- **Knob 10** is either one **shared** function, or the **sub-page
  selector** on ARP, ENVELOPE, LFO and DELAY. Selecting a sub-page never
  changes the sound.
- **Pickup:** every (page, sub-page, knob) slot keeps its own value.
  Changing page **or sub-page** re-arms pickup on knobs 1-9 exactly like a
  page change in the reference (`kPickup` 0.02). Knob 10 as a selector is
  picked up the same way; a jump in the selection is harmless because
  selecting is silent.
- **Per-oscillator functions** hold three values (`params_[func][osc]`), so
  a page row is written once and expanded to the three columns (see "How the
  code holds it"). Stepped values are computed on the audio thread with
  `StepWithHyst`, the screen only reads them (reference rule).
- Names are **<= 8 characters**. The readout shows the function name with
  the sub-page and `OSC n` above it (nothing for a global function).
- MAIN and KEY are **global pages** (no columns): ten independent functions.

### How the code holds it (built in step 1)

- **`enum Func` + `kFuncs[]`** (`synth_core.cpp`): every function once, with
  its name, whether it is per-osc, its step count and step names (or a
  numeric base / sign), and defaults per osc. Values live in
  `params_[func][osc]` (atomic), `smooth_[func][osc]` (audio thread) and
  `step_[func][osc]` (stepped functions, hysteresis on the audio thread; the
  screen only reads them). Global functions use osc 0.
- **`kPages[]`**: per page and sub-page, the (func, osc) each of pots 0-8
  drives (packed `func | osc << 8`; `COLS(a, b, c)` writes a column page's
  three rows), plus knob 10 = a function or `kSubSel`.
- There is no separate slot store: every slot is a distinct (func, osc), so
  a slot's value *is* its param. Pickup state is `potRef_ / picked_`; the
  sub-page per page is `sub_[page]` with its selector reading `subSel_[page]`.
- **Adding a function:** a `Func` entry (the enum and `kFuncs` must stay in
  the same order; `tests/arpsynth.py FUNCS` mirrors it), use it in
  `ProcessAudio`, place it in `kPages`, add a `GetValueText` case if percent
  is not the right unit.
- Core API: `SetPot / GetPot`, `GetPage / GetPageName / GetNumSubs / GetSub /
  GetSubName`, `GetSlotValue / GetSlotName / GetSlotOsc / GetValueText`,
  test hooks `SetEngineParam(func, osc, v)` and `GetOscNote(osc)`. The DLL
  exports them as `synth_*` (`api.cpp`).

### Pages

Ranges and curves follow the reference wherever a function is reused.

**Defaults = a simple start (user 2026-09-23):** power-on is three plain
oscillators and their arps with every effect and modulation off: delay
SENDs, REV SENDs, all LFO DEPTHs, FILTER and PITCH env AMOUNTs, DRIVE and
MIX WIDTH at 0 (the masters MAIN DELAY / REVERB stay at 1x, so raising a
send works at once). On the Seed, the boot page takes its knobs' physical
positions; that is why "off" lives in the per-osc functions on the other
pages, not in the MAIN masters. Checked in `test_voices.py`: nothing rings
on once the notes stop.

**1 MAIN** (global, the power-on performance page)

| Knob | Function | Notes |
|---|---|---|
| 1-3 | LEVEL 1-3 | osc levels, audio taper (p^2). Defaults 0.8 / 0.7 / 0.6 |
| 4 | ROOT | key tonic, stepped 12, C..B (default A) |
| 5 | SCALE | stepped 20: MAJOR MINOR DORIAN PHRYGIAN LYDIAN MIXO "HARM MIN" PENTA "PENTA MI" BLUES, then MAJ MIN SUS2 SUS4 MAJ7 MIN7 DOM7 DIM AUG ADD9 (default MINOR) |
| 6 | TEMPO | 40-240 BPM (default 110) |
| 7 | CUTOFF | master: +-3 octaves on all three cutoffs, centre = as set |
| 8 | DELAY | master: 0-2x all three delay SENDs, centre = as set |
| 9 | REVERB | master: 0-2x all three REV SENDs, centre = as set |
| 10 | SWING | 0-1 (reference behaviour) |

**2 OSC** (columns)

| Row | Function | Notes |
|---|---|---|
| A | SHAPE | MorphOsc: 0 sine, 0.5 square, 1 PolyBLEP saw. Defaults 1 / 0.5 / 0.3 |
| B | OCTAVE | stepped -1..+3 above base octave 2. Defaults 0 / +1 / +2 |
| C | DEGREE | stepped 0..7 scale steps above the current chord root. Defaults 0 / 2 / 4 (all arps OFF = the chord's triad) |
| 10 | DETUNE | shared, 0-20 cents between each osc's two oscillators |

**3 ARP** (columns, knob 10 = sub-page)

| Sub | Row A | Row B | Row C |
|---|---|---|---|
| NOTES | MODE: stepped OFF, LOOP (retrigger, no pitch change), UP, DOWN, UP-DN, RANDOM | POOL: stepped KEY, TRIAD, 7TH, FIFTHS, ROOT | RANGE: stepped 1-3 octaves |
| RHYTHM | DIVISION: stepped 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 | LENGTH: stepped 1-16 steps (the pattern restarts after LENGTH steps: polymeter) | GATE: 0.05-1 of the step |
| CHANCE | DENSITY: how many of the LENGTH steps play, **spread evenly (Euclidean)**, 1 = every step. Deterministic, so a pattern repeats | VARY: chance a played step takes a random pool note | RATCHET: chance a played step splits into 2-4 fast repeats |

Defaults: osc 1 UP, FIFTHS, 1 oct, 1/4, LENGTH 8; osc 2 UP-DN, TRIAD,
2 oct, 1/8, LENGTH 8; osc 3 RANDOM, KEY, 2 oct, 1/16, LENGTH 6, DENSITY
0.6. VARY and RATCHET 0.

**4 ENVELOPE** (columns, knob 10 = sub-page)

| Sub | Row A | Row B | Row C |
|---|---|---|---|
| AMP | ATTACK 1 ms-4 s | DECAY 5 ms-4 s, **also the release time** | SUSTAIN 0-1 |
| FILTER | ATTACK 1 ms-4 s | DECAY 5 ms-4 s (AD, retriggered per note) | AMOUNT bipolar +-4 octaves of CUTOFF |
| PITCH | AMOUNT bipolar +-12 semitones | DECAY 5 ms-1 s (starts at AMOUNT, falls to 0) | GLIDE 0-1 s portamento between arp notes |

Release follows DECAY (user-decided): SUSTAIN 0 = pluck, gate irrelevant;
SUSTAIN 1 = holds for GATE, then fades over DECAY. A new note restarts from
the current level (no click). MODE OFF bypasses the amp envelope (rises at
ATTACK, holds). Defaults: plucks, SUSTAIN 0.4 / 0 / 0, DECAY 400 / 250 /
180 ms, FILTER AMOUNT 0 (was +1.5 oct), PITCH AMOUNT 0, GLIDE 0 / 0 / 0.

**5 FILTER** (columns)

| Row | Function | Notes |
|---|---|---|
| A | CUTOFF | 80 Hz-12 kHz exp |
| B | RESO | 0-0.85 |
| C | TYPE | continuous LP (0) -> BP (0.5) -> HP (1), crossfade of the SVF outputs |
| 10 | DRIVE | shared, pre-filter tanh 1-8x, level-compensated (default 0: clean) |

**6 LFO** (columns, knob 10 = sub-page = target)

Sub-pages PITCH, CUTOFF, AMP, PAN, SHAPE; every one has the same rows, so
each oscillator has **one independent LFO per target** (15 total).

| Row | Function | Notes |
|---|---|---|
| A | RATE | 0.02-20 Hz exp; with LFO SYNC = TEMPO, stepped 4 bars .. 1/16 of the master clock |
| B | DEPTH | 0-1. Full scale: PITCH +-1 semitone, CUTOFF +-3 oct, AMP 100 % dip, PAN full L-R, SHAPE +-0.5 |
| C | WAVE | stepped SINE, TRI, SAW, SQUARE, S&H, DRIFT (smooth random) |

Defaults: depth 0 everywhere (CUTOFF was 0.15 until the simple-start
defaults below; its slow unrelated rates 0.09 / 0.13 / 0.17 Hz remain).

**7 DELAY** (columns, knob 10 = sub-page) — **one stereo delay per oscillator**

| Sub | Row A | Row B | Row C |
|---|---|---|---|
| TIME | DIVISION: stepped 1/32 .. 1/1 incl. T and dotted (reference list minus OFF) | FEEDBACK: 0-1 on the reference's stretched curve | SEND 0-1 |
| COLOR | TONE: loop lowpass 500 Hz-12 kHz | WOBBLE: tape wander 0-3 ms | WIDTH: mono .. ping-pong |

Loop topology, send level and makeup gain from the reference's delay rework
(half-level loop, 20 Hz highpass). Buffers: 6 lines of 6 s (a bar at
40 BPM), 1.15 MB each, SDRAM on the Seed, lent through `synth::Buffers`.
Defaults: 1/8. / 1/4 / 1/8T, feedback 0.4, **send 0** (was 0.2 / 0.35 / 0.3).

**8 MIX** (columns)

| Row | Function | Notes |
|---|---|---|
| A | PAN | defaults 0.5 / 0.3 / 0.7 |
| B | WIDTH | the osc's two oscillators spread L/R, 0 = mono (default 0, was 0.3) |
| C | REV SEND | 0-1 (default 0) |
| 10 | SIZE | reverb size (reference SIZE) |

**9 KEY** (global)

| Knob | Function | Notes |
|---|---|---|
| 1 | PROG | stepped: OFF, POP 1-5-6-4, 50S 1-6-4-5, SAD 6-4-1-5, EPIC 1-6-3-7, JAZZ 2-5-1-1, VAMP 1-4, CANON 1-5-6-3-4-1-4-5, FALL 1-7-6-5 (scale degrees, so each works in any ROOT and SCALE) |
| 2 | CHANGE | stepped 1/2, 1, 2, 4 bars per chord |
| 3 | CHORD | manual chord degree I..VII while PROG is OFF |
| 4 | RESTART | stepped FREE, BAR, CHORD: when all patterns jump back to step 1 |
| 5 | OCTAVE | master -2..+2 |
| 6 | FINE | +-50 cents |
| 7 | LFO SYNC | FREE / TEMPO |
| 8 | DAMPING | reverb (reference) |
| 9 | PREDELAY | reverb (reference) |
| 10 | VIEW | stepped RINGS, PITCH, WHEEL, SCOPE (PITCH added 2026-09-23 on user request, the default since 2026-09-24) |

The progression changes chord on bar lines; a step already sounding
finishes, the next step uses the new chord. ROOT still transposes the whole
progression while it runs.

---

## Screen (user-decided 2026-09-23)

### Color: pitch class in circle-of-fifths order

`hue = ((pc * 7) mod 12) / 12`, pc = pitch class with C = 0. Notes a fifth
apart get neighbouring hues, so a chord's notes cluster (Am = green to
yellow, F = magenta to red, G = orange to teal) and each chord of a
progression gets its own color mood. Replaces the reference's register-based
`PitchColor`.

### VIEW RINGS

- Three concentric rings, **outer = OSC 1** (r 100), OSC 2 (r 78),
  OSC 3 (r 56), each divided into LENGTH arcs starting at 12 o'clock.
- **Each ring is drawn as that oscillator's waveform**, subtly: the captured
  signal (post filter and envelope, pre pan), read phase-locked to the osc's
  own phase so it stands still. Cycles around the ring follow the note's
  frequency (an octave up = twice as many); height follows the envelope
  (up to +-7 px at full level, user-approved height), so rings jump on each
  note and settle.
- **Each arc takes the color of its step's note** (user choice): the current
  step's arc bright (full, 1.6 px), others at ~40 %, skipped steps
  (DENSITY) dark grey.
- Step dots on the ring: current step r 7 with a faint halo, others r 4.5
  at 40 %, skipped = hollow grey, ratchet = split dot.
- Centre: current chord name (e.g. `Am`) large in its root's hue, below it
  the degree numeral and progression position (`vi  3/4`), then 4 beat dots.
- Mockups of rings, wheel and both waveform variants were shown in the
  design session (2026-09-23).

### VIEW PITCH (user request 2026-09-23; the default since 2026-09-24)

The RINGS view with each oscillator's whole wave in **one color**: the
fifths hue of the note that oscillator is on (`oscNote_`), changing as the
arp moves, instead of one color per step arc. The step dots keep their
per-step colors, so the pattern stays readable. `DrawRings(fb, alpha,
oneHue)`.

### VIEW WHEEL

Pitch classes around the circle in fifths order (C at the top); radius =
octave (low inside). Scale notes marked in their hue, current chord's tones
larger and joined into a polygon (its shape shows the chord quality);
each playing note a bright disc with its osc number beside it, previous
notes as a short fading trail. Chord name in the centre.

### VIEW SCOPE

The reference's scope: three overlaid traces, now colored by the fifths hue.

### Overlays (every view)

- **Readout** when a knob moves: name, `OSC n` / `SHARED`, gauge, value
  text; fades after ~2.5 s (reference timing).
- **Page map** when PAGE turns. Column pages list their rows, which reads
  better on a round screen than a 3x3 name grid:
  `1-3 SHAPE / 4-6 OCTAVE / 7-9 DEGREE / 10 DETUNE`. Global pages (MAIN,
  KEY) keep the reference's two-column map.
- **Sub map** when knob 10 turns on a sub-page: the page name, every
  sub-page listed with the current one highlighted and a dot on the ones in
  use (LFO: any DEPTH > 0), then the chosen sub's rows as in the page map.
- Font gains the lowercase glyphs chord names and numerals need
  (`m`, `i`, `v`, `d`, `s`, plus `#` which exists).

### Frame rate

Moving waveforms touch most of the screen, so it stays a full-frame push
(~74 ms, ~13 fps, like the reference scope). Later, with the user at the
hardware: try the SPI prescaler one step faster (~25 MHz) and keep it only
if the panel shows no glitches.

---

## Constraints & landmines (each of these cost real debugging once)

- **C++14 only** in core (firmware toolchain default). No C++17 features.
- Framebuffer stays in **panel byte order** (`canvas.h Color()` handles it);
  the sim un-swaps. Never "fix" the swap.
- Params cross threads: keep the `std::atomic<float>` array + per-block
  smoothing pattern already in `synth_core.cpp`.
- Audio API is interleaved stereo float, `ProcessAudio(out, nframes)`; the
  Daisy callback passes `size/2` frames. Sim block = 256, firmware block = 48.
- **Big DSP buffers**: `ReverbSc` and long `DelayLine`s are hundreds of KB.
  PC: fine as statics inside the DLL. Firmware: they overflow main RAM — the
  established pattern is the shell owning placement: declare
  `static daisysp::ReverbSc DSY_SDRAM_BSS verb;` (and SDRAM delay buffers) in
  `synth_main.cpp` and pass references into `synth::Init(...)` through
  `synth::Buffers`. Add a field there, in `api.cpp` and in `synth_main.cpp`
  together. Verify firmware RAM in the `make` output memory table after
  adding each effect.
- **NEVER put a big buffer in `DMA_BUFFER_MEM_SECTION`.** That section is
  the start of RAM_D2 and libDaisy's MPU makes only its first 32 KB
  non-cacheable. The 115 KB display framebuffer placed there (copied from
  04_hue) pushed libDaisy's audio DMA buffers (`audio.o .sram1_bss`) past
  32 KB into cacheable RAM -> the codec played stale-cache garbage: loud,
  garbled, even for a pure sine, knobs still "doing something". Screen fine,
  ADC fine, CPU 38 %, SDRAM OK. Found via `build/synth.map` (2026-09-21).
  The display uses blocking SPI, so the framebuffer lives in plain `.bss`.
  Check the map after adding any large static: `audio.o` must be at
  0x30000000..0x30007FFF (`build/synth_arp.map` here).
- **SEED3 GROUNDS ARE NOT BONDED.** AGND (pin 20) and DGND (pin 40) must be
  jumpered together externally (Seed3 datasheet v2.1 p.12, fig 1.5). Without
  it the ADC reference floats: all 12 channels read ~100 with random dips to
  70-80, a grounded pin still reads 100, even the DAC loopback self-test
  fails. Cost a full evening on 2026-09-21.
- **Wiring (all 12 pots confirmed on hardware 2026-09-21).** Seed3 physical
  pins: pin 1 = square pad beside USB-C, 1–20 up one side, 21–40 down the
  other. Knob k (panel order) -> ADC A(k-1): A0..A10 on pins 22..32, A11 on
  pin **35** (33/34 are D26/D27, no ADC). Pot ends: +3V3A pin 21 and AGND
  pin 20. Bottom rows are mounted upside down: firmware inverts pots 4-6 and
  10-12 (`kPotInvert` in `synth_main.cpp`). Display: VCC 38 (3V3D), GND 40,
  SCL 9, SDA 11, DC 7, CS 8, RST 6. Audio: OUT L 18, OUT R 19, AGND 20.
  VIN (39) is input-only and dead on USB power - never use it as a supply.
- **Flash is the tight budget** (128 KB internal). Watch the `make` table
  after every step and `arm-none-eabi-nm --size-sort -S -C
  build/synth_arp.elf | tail` for the big symbols. Paid for once: the
  compiler inlined the `canvas.h` text helpers into every call site
  (RenderScreen 7.7 KB) - they are `SYNTHUI_NOINLINE` now; and one
  `fmod(double)` pulled in ~0.8 KB of libm (use `x - floor(x / m) * m`).
  Since step 7 the whole screen section of `synth_core.cpp` is compiled
  `-Os` via `#pragma GCC push_options / optimize("Os")` (GCC only; clang
  in the sim ignores it): -5 KB. Keep new UI code inside that section and
  audio code outside it.
- **Zig floods the console with libc++ `-Wnullability-completeness` warnings**
  (~2 min) on a cache miss: zig building its bundled C++ runtime, not our
  code. Once per environment; the next build is ~1 s. Do not kill it.
- **DLL build fails with `lld-link: could not open '...\zig\o\<hash>\*.obj'`**:
  a zig process was killed mid-build. `build_dll.ps1` detects the message and
  retries once with a cache nonce define.
- **DLL build fails with "Permission denied"** when a `panel_sim.py` process
  is still alive. `synth.bat` closes it; otherwise find it with
  `Get-CimInstance Win32_Process | ? CommandLine -like '*panel_sim*'`.
- Flashing is the user's move. Never claim hardware behavior verified
  unless the user reports it.

### Measurement traps (from the reference tests)

- Exact unison with DETUNE 0 cancels the fundamental; use a little detune.
- A sine gated every step has sidebands; use GATE 1 / SUSTAIN 1 for spectra.
- A decorrelated stereo tail beats if L+R is summed first; measure per channel.
- Every helper that switches pages (and now sub-pages) must switch back.
- Onset-based pitch reads misread ~1 frame in 20; count a pitch only when
  it appears twice.
- `setp(pot, v)` must move a pot first if it already sits at `v` (pickup).
- DETUNE splits each osc into two spectral peaks (+-8..10 cents at the
  default): pitch reads need DETUNE 0 (the two start phases are a quarter
  cycle apart, so DETUNE 0 does not cancel).
- Params smooth over 20 ms. Through the panel that is right, but in a test
  the default sends and levels leak ~50 ms into the reverb after Init, and
  the tank rings on at about -33 dB for seconds. So `SetEngineParam` (the
  test hook) also sets the smoothed value: engine-set values apply at once
  and tests can expect digital silence.
- Onset times: the first sample above 30 % of the peak lags the true onset
  by up to half a period plus the attack; measure timing on a high note
  (A5) with a 1 ms attack. Judge grid lock by drift, not by max error.

---

## Build order (each step ends runnable in the sim, tested, firmware `make` clean)

0. **DONE — Setup** (2026-09-23): `synth\` copied to `synth_arp\`,
   `05_synth` to `06_arp` (Makefile paths + TARGET), sim title, .gitignore.
   DLL and firmware both build from the copy (flash 90.1 %).
1. **DONE — Framework + three drone oscillators** (2026-09-23). The core
   was rewritten around `Func` / `kPages` (see "How the code holds it"):
   all 9 pages and 14 sub-pages are placed, every function stores its value
   and shows its value text; CHAOS, SHIMMER (all GrainShifters), TEXTURE and
   the old tests are gone. Sounding: MAIN (LEVEL x3, ROOT, SCALE with the 10
   chords, master CUTOFF and REVERB), OSC, FILTER (with DRIVE and TYPE),
   MIX (PAN, WIDTH, REV SEND, SIZE), and from KEY: CHORD (manual), OCTAVE,
   FINE, DAMPING, PREDELAY (pulled forward from step 2 / 6). The reverb is a
   send / return now (return 0.5 at send 1). `Buffers` already carries the 6
   delay lines (step 6 only has to use them). The screen still shows the
   reference scope, colored by the fifths hue; page map (rows) and sub map
   (rows + sub-page dots) are new. Verified (`test_panel.py`,
   `test_voices.py`): page / sub-page pickup, columns only touch their osc,
   default notes A2 C4 E5 measured within 5 cents, ROOT / SCALE / CHORD
   I-IV-V-VII, a MIN7 chord key and its IV, KEY OCTAVE + FINE measured, HP
   at 1.2 kHz cuts 110 Hz by 37 dB, master CUTOFF -3 oct moves the centroid
   1140 -> 289 Hz, PAN / WIDTH, reverb tail. Firmware `make` clean.
   Hardware not yet heard.
2. **DONE — Clock + ARP + AMP envelope** (2026-09-23). `beat_` (double,
   quarter notes since Init) advances by TEMPO each block; an osc's step
   index = `StepIndex(beat, DIVISION, swing)` (even steps d(1+s), odd
   d(1-s), s = SWING / 3), so the three grids share one origin and cannot
   drift. A step starting inside a block is found by comparing the index at
   the block's last sample with `lastStep`, and lands on its exact sample.
   Pattern position = step index mod LENGTH; DENSITY hits = round(DENSITY x
   LENGTH), step plays if `(pos * hits) % LENGTH < hits` (Euclidean, starts
   on a hit; readout "3/8"). Pool (`BuildPool`): POOL type from DEGREE on
   the current chord over RANGE octaves; FIFTHS uses `KeyFifth` (key note
   nearest a perfect fifth). LOOP retriggers the pool's first note. VARY
   swaps a played step for a random pool note; RATCHET splits a played step
   into 2-4 even hits of the same note, each with GATE of its share. Per osc
   rng (xorshift). Arp notes jump on their onset sample (GLIDE is step 4);
   drones glide 80 ms. Amp envelope per sample after the filter: linear
   attack, exponential decay to SUSTAIN, release at the decay rate, restart
   from the current level; an arp voice rests silent (release stage) until
   its first hit, and a drone switched to an arp fades at DECAY until the
   next step. Verified (`test_arp.py`, 31 checks): 1/8 @ 120 = 250.0 ms,
   @ 60 = 500.0, 1/16 = 125.0, 1/4T = 333.4; 1/4T, 1/8T and 1/16 at 137 BPM
   on the shared grid for 60 s with < 0.04 ms drift; KEY / TRIAD / 7TH /
   FIFTHS / ROOT pools, RANGE 2, DOWN, UP-DN, RANDOM in pool; LENGTH 5 over
   a 14-note pool repeats every 5; CHORD IV moves the pool to D F A;
   DENSITY 3/8 hits steps 0 3 6 (tresillo), DENSITY 0 silent from sample
   one; GATE duty 0.244 / 0.718; release 100 ms after the gate at 0.108 of
   the held level (DECAY 200 ms, expect ~0.10); SWING 1 = 333 / 167 ms;
   RATCHET 2-4 hits per step; VARY on LOOP; MODE OFF steady; all three
   arpeggiate at power-on. Firmware `make` clean, flash 87.1 %.
   Hardware not yet heard (CPU check pending).
3. **DONE — Progression** (2026-09-23). `kProgs[]` holds the presets as
   scale degrees; `ChordAt(beat)` = PROG's degree at floor(beat / CHANGE)
   mod its length (counted from beat 0, so changes fall on bar lines), or
   KEY CHORD when PROG is OFF. The block computes the chord *now* (drones,
   screen), but an arp step builds its pool on `ChordAt(its own start)`,
   so a step starting exactly on a bar line already plays the new chord
   (`kBeatEps` keeps float rounding from putting it in the old one; checked
   at 1/16T, whose 24th step is the bar line). RESTART: the pattern
   position counts steps from `RestartBeat()` = 0 (FREE), the last bar line
   (BAR) or the last CHANGE boundary (CHORD; with PROG OFF it restarts every
   CHANGE anyway, since manual chord moves have no grid). The screen centre
   shows the chord name in its root's fifths hue (`GetChordName`: root +
   quality: "", m, dim, + or sus from the triad on a scale key, the chord
   type's suffix on a chord key), the numeral (`GetChordNumeral`, lowercase
   when minor), the progression position ("3/4") and 4 beat dots, on a
   black disc over the scope; it fades in as the readout fades out. Font
   gained a d i j m s v. Verified (`test_prog.py`, 40 checks): all 8
   presets in C major, A minor and on an A MIN7 chord key (parallel m7
   chords); POP / FALL / EPIC / JAZZ names and numerals (C I, Bdim vii, Am
   vi, Dm ii ...); Dsus4 on a chord key; CHANGE 1/2..4 bars; chord changes
   every 4th quarter step; RESTART FREE / BAR / CHORD sequences; KEY
   readouts; the centre drawn. Flash: `canvas.h` draw helpers are now
   `noinline` (RenderScreen 7.7 -> 5.1 KB), total 87.7 %.
4. **DONE — FILTER and PITCH envelopes + GLIDE** (2026-09-23). The sample
   loop now has a **control tick every kCtrl = 16 samples** (0.33 ms, same
   at the sim's 256 and the Seed's 48): `ControlTick` glides the pitch,
   sets both oscillators' frequency (note + PITCH env) and both SVFs'
   cutoff (CUTOFF + master + FILTER env), then advances the two envelopes.
   The plan said "block rate" for modulation, but a 5 ms sim block is too
   coarse for 5 ms envelopes, and the tick costs little (6 `Svf::SetFreq`
   per tick). A hit (`Strike`) sets the target note (jumping at once when
   GLIDE is 0, so onsets stay sample-exact), kicks the PITCH env to 1 and
   restarts the FILTER env's attack from its current level; the cutoff
   follows on the next tick. FILTER env: linear attack (1 ms..4 s), then
   exponential decay to 0 (5 ms..4 s, 99 % in the shown time), AMOUNT
   +-4 oct; drones never hit, so it rests at 0 for them. PITCH env: starts
   at AMOUNT (whole semitones, +-12) and decays to 0 (5 ms..1 s); ratchet
   hits re-kick it. GLIDE: exponential, 99 % in GLIDE (0..1 s) for arp
   notes; drones glide on chord changes over max(GLIDE, 370 ms) (the old
   80 ms time constant). Verified (`test_env.py`, 16 checks): FILTER
   AMOUNT 0 steady, +4 oct 2.8x brighter at the hit and back by 400 ms,
   -4 oct dark then open, ATTACK 200 ms peaks late, DECAY 1 s vs 50 ms;
   PITCH +12 over 1 s measured +10.0 / +2.4 / +0.2 st at 40 / 350 / 875 ms
   (theory +10 / +2.4 / +0.2), -12 mirrors it, 50 ms settles; GLIDE 0
   jumps, 500 ms reaches +2.1 st of a fifth at 40 ms (theory +2.2); a drone
   slides A2 -> D3 in ~0.37 s and slower with GLIDE 1 s; the pitch env
   leaves drones alone. Flash 88.2 %.
5. **DONE — LFO page** (2026-09-23). 15 LFOs (`lfo_[target][osc]`) on the
   control tick. FREE: phase advances by RATE (0.02..20 Hz); the three
   oscillators start a third of a cycle apart. TEMPO (KEY LFO SYNC): RATE
   picks one of 4 BARS, 2 BARS, 1 BAR, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16
   (hysteresis, readout shows the division) and the phase is
   frac(beat / division) of the master clock, so synced LFOs line up with
   the bar and each other. Waves: SINE (the MorphOsc parabola, no trig),
   TRI, SAW (rising), SQUARE, S+H (new random value each cycle), DRIFT
   (smoothstep from the last random value to the next). Destinations, full
   scale at DEPTH 1: PITCH +-1 st (added in `SetPitch`), CUTOFF +-3 oct
   (added to the tick's cutoff), AMP dips to 1 - DEPTH at the wave's bottom
   (never boosts; ramped per sample across the tick), PAN +-0.5 of the
   position (pan gains now per tick), SHAPE +-0.5. Verified (`test_lfo.py`,
   16 checks): PITCH +-1.00 / +-0.50 st at 2 Hz (period 0.500 s); SQUARE
   two levels, SAW rising with one drop per cycle, S+H holding, DRIFT
   smooth, TRI +-1; CUTOFF 285..2021 Hz at 1 Hz; AMP low/high 0.00 and
   0.47 (want 0 and 0.5); PAN -1..+1; SHAPE 3rd harmonic 0.015..0.248; osc
   1's LFO leaves osc 2 alone; SYNC 1/4 square flips on the 1/8 grid
   (within the 10 ms detection window). Flash 89.2 %.
6. **DONE — Delays** (2026-09-23; the reverb landed in step 1). One stereo
   delay per oscillator (`delays_[o]`, lines from `Buffers`, SDRAM on the
   Seed), processed inside the oscillator loop on the osc's own panned
   signal. Time = DIVISION (1/32 .. 1/1, incl. T and dotted) of TEMPO,
   slewed 80 ms in the log domain (snapped on the first block), plus WOBBLE
   (an Ornstein-Uhlenbeck walk per side, 4 s memory, 0.3 s smoothing, up to
   3 ms), ramped per sample. Loop: taps -> TONE one-pole (500 Hz..12 kHz)
   -> 20 Hz DC blocker; lines written at kDelaySend (0.5) through a soft
   clip, the repeats made up 2x. SEND x master DELAY (0..2x) feeds it,
   FEEDBACK on the reference's stretched curve. WIDTH 0 = parallel L / R
   lines (the osc's pan survives in its repeats), 1 = ping-pong (mono in on
   L, L -> R -> L). Dry + repeats go on to the reverb send. Test helper
   `solo()` now also sets master DELAY 0 (a dry voice). Verified
   (`test_delay.py`, 16 checks): first echo at 251 / 501 / 501 / 376 /
   84 ms for 1/8, 1/4, 1/8 @ 60, 1/8., 1/16T (want 250 / 500 / 500 / 375 /
   83); SEND 0.5 -> first echo 0.50 of the hit; loop gain 0.5 -> repeats
   0.50, 0.50; SEND 0 and master 0 silent; master 2x doubles; ping-pong
   echo 1 left, echo 2 right; a hard-left osc's repeats stay left; TONE
   500 Hz third echo 305 Hz centroid vs 2486; WOBBLE spreads the echo
   spacing 0.52 ms (0.01 ms without); osc 2 never reaches osc 1's delay.
   Flash 90.7 %, SDRAM unchanged (lines allocated in step 1).
7. **DONE — Screen views** (2026-09-23). KEY VIEW picks RINGS (default),
   WHEEL or SCOPE; the view dims under the readout and hides under the maps,
   the chord centre (disc now r 48) fades in as the readout fades. The audio
   thread publishes per osc `ringLen_` (0 = drone), `ringPos_`,
   `ringNote_[o][pos]` (MIDI, or rest / unplayed), `ringRatchet_`,
   `oscHz_` (sounding frequency), and `dispKeyMask_ / dispChordMask_`.
   **RINGS** (r 102 / 80 / 58, outer = OSC 1): 256 points round the ring
   (sin / cos table), each point's radius = R + the captured signal at the
   matching phase of one period of the osc's scope ring (period from
   `oscHz_`, aligned on a rising zero crossing), repeated
   round(3 * f / 110) times (3 at A2, 2..24); segments colored per step arc
   (arcs centred on the dots), rests grey, unplayed dark. **Made more
   prominent on user request (2026-09-23, "too subtle")**: the signal is
   divided by LEVEL (`oscLvl_`, so a quiet osc still draws a full ring;
   the envelope still sets the height), 40 px per unit soft-limited to
   +-9 px (was x 20 clamped at +-7), played arcs at 70 % of their hue (was
   40 %), the playing step at 100 % (was 90 %), a 2 px stroke everywhere
   (the user tried 2 px only on the playing arc and went back to 2 px
   always). Rings are 22 px apart, so two +-9 px waves plus the stroke
   never touch. **Readout over the view (user, same day: "dims far too
   much")**: the view keeps 75 % under the readout (was 40 %) and 20 %
   under the page / sub maps (was hidden); the readout's three text lines
   sit on bands darkened in step with its fade (`TextOnDark`). Readout and
   centre chord hand over (text out in the first half of the fade, chord
   in during the second) instead of overlapping. Dots: playing = r 7 +
   r 10 halo, played r 4, ratchet = two r 3, rest hollow, unplayed r 2. A
   drone is one unbroken ring in its note's hue. **WHEEL**: 12 notes in
   fifths order at r 94 (C on top, so hue and position agree); chord tones
   r 6 joined in wheel order (the chord's shape), key notes r 3 dim,
   others r 1; each osc's note a disc (r 7 / 5 / 4 for OSC 1 / 2 / 3) at
   its pitch class, radius 30 + (note - 36) x 56/48 (higher = nearer the
   rim), its previous note dim; chord name + numeral in the middle.
   **SCOPE**: the reference scope. Canvas gained `DrawLine`. The whole
   screen section is compiled `-Os` (`#pragma GCC optimize`): the views
   pushed flash to 94.9 %, `Os` brought RenderScreen 9.4 -> 3.5 KB and
   flash to 89.9 %. Verified (`test_screen.py`, 9 checks, on the rendered
   framebuffer): three rings drawn; osc 1 playing A -> ring hue 0.254
   (A in fifths order 0.250); DENSITY rests grey (saturation 0.14 vs 1.00);
   the playing dot at 45 then 90 deg (LENGTH 8); ring spread larger just
   after a hit than 420 ms later; a drone ring unbroken in A's hue; WHEEL
   lights A C E, not C# F# G#; SCOPE draws.
8. **Defaults + hardware pass** — agent side done 2026-09-23, **the rest
   is the user's**:
   - Power-on levels (sim, 20 s at VOLUME 1): peak 0.60, RMS 0.082, the
     limiter never bends (no sample above 0.8); the three oscillators alone
     with their effects sit at RMS 0.10 / 0.12 / 0.10.
   - CPU, PC estimate: the arp DLL costs 1.22x the reference drone DLL at
     48-frame blocks (1.23x in a worst case: every LFO on, ratchets, 1/16T,
     all delays and sends up). The reference measured 38 % on the Seed
     before its shimmer / chaos / texture pages existed; the Seed also pays
     more than a PC for SDRAM reads, and this concept reads 6 delay lines
     against the reference's 2. So: probably fine, **must be measured**.
   - `DIAG_ADC 1` build still compiles (85.2 %): it shows CPU avg / max per
     audio block on the screen (plus the pot readings).
   - Open, user: listen to the power-on sound and each page; flash
     (`.\daisy.ps1 flash 06_arp`); CPU on the Seed with `DIAG_ADC 1`
     (`MyProjects\06_arp\synth_main.cpp` line 26; max must stay under ~90);
     the faster display below is done.
   - **Faster display (works on the hardware, user confirmed 2026-09-23)**: the screen felt
     slow once the waves were bold; rendering is not the cost (0.04 ms per
     RINGS frame on the PC), the SPI push is (74 ms at 12.5 MHz). SPI1's
     kernel clock was PLL2P = 25 MHz and PS_2 is the smallest prescaler, so
     `synth_main.cpp` `FAST_DISPLAY_SPI 1` switches the SPI1/2/3 mux to
     PLL3P (49.15 MHz, already running for the SAI) with
     `__HAL_RCC_SPI123_CONFIG` - **not** `HAL_RCCEx_PeriphCLKConfig`, which
     would reprogram PLL3 and could stop the audio. SCK 24.6 MHz, ~38 ms
     per frame, ~26 fps. The sim now draws every 38 ms, and the readout's
     frame counts doubled (kIdleStart 66, kFadeFrames 20 = 2.5 s + 0.75 s
     at 26 fps; with FAST_DISPLAY_SPI 0 they would take twice as long).
     If a rewired panel ever shows garbage or flicker, set it to 0.
