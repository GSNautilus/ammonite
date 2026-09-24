# Ammonite VST — handoff and plan

Written 2026-09-24 at the end of the session that published Ammonite
(https://github.com/GSNautilus/ammonite, listening page
https://gsnautilus.github.io/ammonite/). A new session builds the plugin from
here. Read this, then `docs/ENGINE_PLAN.md` ("Constraints & landmines" first).

**Goal:** Ammonite as a **Windows x64 VST3 plugin, plus CLAP**, running the
very same engine as the firmware (`core/`). No AU, no macOS/Linux for now.
The firmware must keep building from the same core.

**Start by talking, not coding:** confirm the open decisions (last section)
with the user before step 1.

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

### Step 1: the engine becomes an instance (no behaviour change)

- Move every mutable file-level variable into one `struct Engine` (or class);
  constant tables (`kFuncs`, `kPages`, names, masks) stay `static const`.
- Functions take the instance (methods, or `Engine&` arguments). Keep the
  static text buffers of `GetValueText` / chord names per instance too.
- Firmware: one statically allocated instance; its size lands in `.bss`, so
  check SRAM (38 % today) and flash in the `make` table.
- C API: add `synth_create / synth_destroy` with a handle, keeping the
  current handle-less functions working on a default instance so the
  simulator, tests, manual and media scripts keep running unchanged.
- Done when: all 193 tests pass; a new test runs two instances side by side
  (independent params, identical output for identical input, no cross-talk);
  firmware builds with flash still comfortably below 100 %; the user flashes
  it and confirms it still sounds and looks the same.

### Step 2: what a host needs from the core

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

## Decisions to take with the user first

1. **DAW sync:** always follow the DAW's tempo and position, or a switch
   between DAW and TEMPO knob? Do patterns restart when the transport starts?
2. **UI:** the hardware panel exactly (12 knobs, pages), or that plus a flat
   view of every parameter? (Hosts also offer a generic parameter list.)
3. **Sample rates:** 44.1 / 48 kHz only at first, or up to 96 / 192 kHz?
4. **Identity:** plugin name "Ammonite", vendor "GSNautilus", plugin IDs.
5. **Firmware on the refactored core right away** (recommended: one core),
   with a hardware check by the user after step 1.
