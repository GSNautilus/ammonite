# Ammonite VST — handoff and plan

Written 2026-09-24 at the end of the session that published Ammonite
(https://github.com/GSNautilus/ammonite, listening page
https://gsnautilus.github.io/ammonite/). A new session builds the plugin from
here. Read this, then `docs/ENGINE_PLAN.md` ("Constraints & landmines" first).

**Goal:** Ammonite as a **Windows x64 VST3 plugin, plus CLAP**, running the
very same engine as the firmware (`core/`). No AU, no macOS/Linux for now.
The firmware must keep building from the same core.

**Decided with the user (2026-09-24):**

- **The plugin gets its own copy of the engine** in `plugin/engine/`. `core/`,
  the firmware, the simulator and the 193 tests stay exactly as they are
  (tagged `v1.0`, local, not pushed). The copy becomes multi-instance and
  gains the host features; a fix or sound change in one engine has to be
  carried to the other by hand. Reuniting them later means proving the two
  bit-identical first.
- **DAW sync:** a new SYNC parameter DAW / FREE, default DAW. DAW: BPM and
  song position come from the host (`beat_` = host PPQ while playing), so
  pattern step 1 and the progression land on the DAW's bars. FREE: the
  TEMPO knob and its own clock, like the hardware. With the transport
  stopped, DAW mode keeps playing at the host BPM; on play it jumps to the
  host position.
- **UI:** the hardware panel only (12 knobs, page / section label, the round
  screen); every parameter is also in the host's generic list.
- **Sample rates:** native at the host rate up to 192 kHz; the plugin's
  engine sizes its delay lines for 192 kHz. Tests also run at 96 kHz.
- **Identity:** "Ammonite" by "GSNautilus", CLAP ID
  `com.gsnautilus.ammonite`, DPF brand / unique IDs `GSNa` / `Ammo` (the
  VST3 UID derives from them: never change them after release).
  Categories Instrument | Synth | Generator.

---

## Working environment

- Repo: `C:\Users\Nautilus\Projects\ammonite` (the old Daisy workspace copy
  `Projects\Daisy\synth_arp` is retired; do not work there).
- Commands the user runs are **PowerShell**. From the repo root:
  `.\ammonite.ps1 libs | build | flash | sim | test | manual`,
  `.\simulator.bat`. The agent's own shell is Git Bash.
- Python: conda env `codex` (`$env:AMMONITE_PYTHON` points at its python.exe
  for the scripts). `ziglang` builds `sim\synthcore.dll` (`sim\build_dll.ps1`).
- **C++ for the plugin: Visual Studio Build Tools 2022** is installed:
  MSVC 14.39 (`VC\Auxiliary\Build\vcvars64.bat`), CMake and Ninja under
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\`,
  Windows SDK 10.0.22621.
- **Hosts to test in:** Ableton Live 12 (trial) and FL Studio 2025. The system
  VST3 folder `C:\Program Files\Common Files\VST3` does not exist yet and
  needs admin rights: installing there is the user's move (or point the DAW
  at a custom plugin folder).
- GitHub CLI: `C:\Users\Nautilus\gh_cli\bin\gh.exe` (authenticated as
  GSNautilus).
- Repo rules: commits use the repo-local no-reply identity already set in
  `.git/config` (keep it). End commit messages with the Co-Authored-By
  trailer. **Never push, tag or release without the user's go-ahead.**
  Never launch multi-agent fan-outs without stating the scale and getting an
  OK first.

## The engine today

- `core/synth_core.h` is the whole interface: `Init(samplerate, Buffers)`,
  `SetPot / GetPot` (12 panel pots, with page / section pickup),
  `GetPage / GetPageName / GetNumSubs / GetSub / GetSubName`,
  `GetSlotValue / GetSlotName / GetSlotOsc / GetValueText`,
  `SetEngineParam(func, osc, v)` (test hook: bypasses the 20 ms smoothing),
  `GetOscNote`, `GetChordName / GetChordNumeral`,
  `ProcessAudio(out, nframes)` (interleaved stereo float, any block size),
  `RenderScreen(fb)` (240x240 RGB565 in **panel byte order**; see
  `sim/panel_sim.py fb_to_surface` for the byteswap).
- `core/api.cpp` wraps it as a C API (`synth_*`) for the simulator DLL; the
  tests (`tests/arpsynth.py`) call that DLL through ctypes.
- **All mutable state is file-level statics in `core/synth_core.cpp`**
  (2395 lines, 128 `static` declarations): one engine per process. A DAW can
  load several instances and run them on different threads at once, so this
  is the first thing to change.
- Threading model (already used by firmware and sim): the UI side writes
  `std::atomic<float> params_[func][osc]`; the audio thread smooths them and
  computes stepped values (`step_`, with hysteresis); the screen only reads.
  The audio thread publishes what the screen needs (ring notes, positions,
  chord). Keep this split in the plugin: DSP on the audio thread, UI and
  `RenderScreen` on the UI thread.
- Functions: `enum Func` + `kFuncs[]` in `synth_core.cpp` (the tests mirror
  the order in `tests/arpsynth.py FUNCS`). Pages: `kPages[]`.
- Big buffers are lent by the shell through `synth::Buffers`: 6 delay lines of
  `kDelayMaxSamples = 288400` (6 s at 48 kHz, a compile-time template size),
  the `ReverbSc`, two pre-delay lines. On the PC they are statics in
  `api.cpp`; on the Seed they sit in SDRAM (`firmware/synth_main.cpp`).
- Timing: `beat_` (quarter notes since Init, a double) advances by TEMPO each
  block; every arp grid, the progression and synced LFOs derive from it.
  `RenderScreen` is **frame-counted** (readout fade = 66 + 20 frames at the
  display's ~26 fps): call it on a ~38 ms timer, not on every repaint.
- Firmware limits: **C++14**, flash **90.26 %** (118300 B of 128 KB). The screen
  section is compiled `-Os` via `#pragma GCC optimize`. Check the `make`
  memory table after every core change.

## Plan

### Step 1: DONE (2026-09-24): the plugin's engine is an instance

- `plugin/engine/` = `core/` v1.0 copied, namespace `ammonite`: all 48
  mutable statics (plus the function-local ones: value / chord text
  buffers, the WHEEL view's note memory, the unit circle) live in
  `Engine::Impl`; the 48 functions that touch them are its members;
  `class Engine` (`engine.h`) forwards the old API one to one. Constant
  tables and pure helpers are unchanged. `core/` is not touched.
- `plugin/engine/api.cpp`: the `synth_*` calls on a default instance plus
  `eng_create / eng_destroy / eng_*(handle, ...)`.
- `.\plugin\plugin.ps1 test` builds `plugin\build\core_ref.dll` (from
  `core/`) and `plugin\build\ammonite_engine.dll` with the simulator's
  compiler and flags, runs the whole hardware suite (`tests\`) against the
  plugin engine, then `plugin	ests	est_engine_copy.py`:
  - MATCH: 40 random scenarios (params, pot moves, page changes, block
    sizes 1..600, 44.1 / 48 kHz, screens interleaved) are bit-identical
    between core/ and the copy, default and created instance. Checked that
    it bites: a one-ULP change of a gain fails audio, one color step fails
    screens.
  - INSTANCES: same input -> same output; different settings, blocks
    alternating -> each equals its own solo run on core/; two engines on
    two threads with a third rendering their screens -> unchanged; a new
    engine after others were destroyed starts clean.

**Found on the way (for step 2):**

- **core/ crashes above ~52 kHz** (fresh start): `ReverbSc`'s fixed
  `aux_[DSY_REVERBSC_MAX_SIZE]` (98936 floats, sized for 48 kHz) is too
  small, `Init` returns early and the first process writes through a null
  delay-line pointer. Worse, a re-`Init` at a higher rate after a working
  one leaves the old lines in place (no crash, wrong reverb). The define is
  unconditional in `reverbsc.h`, so the plugin needs its own copy of
  ReverbSc (LGPL, keep the notice) with a buffer for 192 kHz, and the
  engine must treat a failed `Init` as an error. The hardware (48 kHz) is
  not affected.
- `Init` does not reset the WHEEL view's note memory (a function-level
  static in core/): harmless, but one reason two engines only match
  bit for bit when they have the same history.
- A fresh worktree has no submodules: `git submodule update --init
  --recursive lib/DaisySP` (libDaisy is only needed for the firmware).

### Plugin shell: DONE (2026-09-24), pulled forward (user: "into a DAW first")

A first loadable plugin before the rest of step 2 and the UI of step 3.

- Engine: 44.1-192 kHz (own ReverbSc copy, lines sized for 192 kHz; above
  it no reverb instead of a crash), host API (`FuncName / FuncPerOsc /
  FuncSteps / FuncDefault / FuncPlace / FuncStepText`, smoothed `SetParam`,
  `GetParam`, `ReverbOk`). Still bit-identical to core/ at 48 kHz.
- `plugin/lib/DPF` (submodule), `plugin/src/DistrhoPluginInfo.h` (IDs as
  decided), `plugin/src/AmmonitePlugin.cpp`: one engine per instance, no
  UI (the host's generic parameter list), a MIDI input it ignores, the
  engine's own TEMPO (no DAW sync yet). Flush-to-zero in `run`.
- **168 parameters**, ID = index over (func, osc) in enum Func order, named
  "PAGE SECTION NAME osc" ("ENVELOPE AMP ATTACK 1", "VOLUME"). Continuous:
  0..100; stepped: the step with the panel's names. The host saves them
  with the project (DPF's own state: parameter values only).
- `.\plugin\plugin.ps1 build` (MSVC 14.39 + CMake + Ninja; `cl` forced,
  the Daisy ARM g++ is on PATH; static CRT) ->
  `plugin\build\cmake\bin\Ammonite.vst3` and `Ammonite.clap`.
  `.\plugin\plugin.ps1 install` (admin PowerShell) copies them to
  `C:\Program Files\Common Files\VST3` and `...\CLAP`.
- `plugin/tests/test_clap.py`: a ctypes CLAP host loads the built
  `Ammonite.clap`: descriptor, 168 unique parameters, plays at 48 / 96 /
  192 kHz, same sound as the tested engine DLL (MSVC vs zig, correlation
  1.0000), automation event, two instances side by side, state save / load.
- Not verified yet: the VST3 in a real host (the user: Ableton Live 12,
  FL Studio 2025; Live has no CLAP).



- **Tempo / transport:** an API the plugin calls each block with the host's
  BPM, song position (PPQ) and playing flag; `beat_` follows the host. Decide
  the behaviour with the user (see decisions).
- **Sample rate:** the delay lines hold 6 s at 48 kHz. For 44.1 / 48 kHz
  nothing changes. For 88.2 / 96 kHz and up either make the line length a
  build-time setting for the plugin (4x = ~28 MB per instance) or run the
  engine at 48 kHz and resample. Decide with the user.
- **Parameters for automation:** expose every `(func, osc)` as a host
  parameter with a **stable ID** (never reorder `enum Func` after release, or
  give each function an explicit ID). Needs: read a param, its name and its
  value text for any `(func, osc)` (today the text is per pot); a **smoothed**
  setter for automation (the test hook is not smoothed).
- **State:** save / load every param (plus the chosen page and sections) as a
  versioned chunk, so projects reopen as they were.

### Step 3: the plugin, with DPF

- DPF (https://github.com/DISTRHO/DPF, ISC license) as a submodule
  `lib/DPF`; one codebase builds **VST3 and CLAP** (also LV2 / VST2, not
  needed). Avoid JUCE (AGPL or paid).
- New `plugin/` folder: `DistrhoPluginInfo.h`, the DSP side wrapping one
  Engine, the UI side. A synth: no audio inputs, stereo out; no MIDI in for
  now (Ammonite plays by itself).
- UI = the simulator panel: 12 knobs, page / section label, the 240x240
  screen drawn from `RenderScreen` (scaled, with a round mask). With a mouse
  there is no need for pickup jumps: when the page changes, draw each knob at
  its stored value. Knob drags become host parameter edits
  (begin / change / end) so automation records them.
- The UI reaches the engine through DPF's direct-access option (plugin and
  UI in one process), keeping the thread split above.

### Step 4: build and ship

- `.\ammonite.ps1 plugin` builds `Ammonite.vst3` and `Ammonite.clap` (MSVC +
  CMake + Ninja).
- Test in Ableton Live 12 and FL Studio 2025: loads, plays, several
  instances at once, follows the DAW tempo, automation, save and reopen.
- Then README (tick the to-do item, how to install), and a release, after
  the user's go-ahead. Later: GitHub Actions builds.

## Decisions (taken 2026-09-24, see the top of this file)

1. DAW sync: SYNC DAW / FREE, default DAW; keeps playing when stopped.
2. UI: the hardware panel only.
3. Sample rates: native, up to 192 kHz.
4. Identity: Ammonite / GSNautilus / `com.gsnautilus.ammonite` / `GSNa` `Ammo`.
5. Firmware: stays on `core/` untouched; the plugin has its own engine copy.
