# Headless checks of synthcore.dll

Each script loads `..\sim\synthcore.dll` through `arpsynth.py` (shared
helpers: pages, sub-pages, pickup-aware `setp`, `fresh`, `render`, spectra),
drives the pots through the C API and measures the audio. Build the DLL
first (`simulator.bat` or `sim\build_dll.ps1`), then from PowerShell:

    python tests\test_panel.py

(or `.\ammonite.ps1 test` to run them all).

- test_panel.py   pages, column layout, sub-pages, pickup, value texts, page / sub maps
- test_voices.py  three droning oscillators: key, chord degree, octave, FINE, level,
                  filter type, master cutoff, pan / width, reverb send
- test_arp.py     master clock + ARP + AMP envelope: step timing, grids locked over a
                  minute, pools, modes, LENGTH polymeter, Euclidean DENSITY, GATE,
                  release = decay, SWING, RATCHET, VARY, drone, power-on
- test_env.py     FILTER env (amount +-, attack, decay), PITCH env (amount +-, decay),
                  GLIDE on arp notes, drones gliding on chord changes
- test_lfo.py     15 LFOs: PITCH / CUTOFF / AMP / PAN / SHAPE depth and rate, the six
                  waves, per-osc independence, LFO SYNC = TEMPO on the beat grid
- test_delay.py   per-osc delays: echo time per DIVISION / TEMPO, SEND, FEEDBACK,
                  master DELAY, WIDTH parallel vs ping-pong, TONE, WOBBLE
- test_screen.py  VIEW RINGS (arc hues, rests, moving dot, envelope height, drone
                  ring), PITCH (one hue per wave, following the note), WHEEL (chord
                  tones), SCOPE, on the rendered framebuffer
- test_prog.py    chord progression: every preset in major, minor and a chord key,
                  chord names / numerals, CHANGE lengths, changes on bar lines,
                  RESTART FREE / BAR / CHORD, the chord in the centre of the screen

Measurement lessons that cost time are noted in docs\ENGINE_PLAN.md
("Measurement traps").
