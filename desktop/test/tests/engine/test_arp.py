"""The arp generator and the harmony behind it, measured from the audio:
pools in a key, modes, range, degree, octave, LENGTH polymeter, Euclidean
density, gate, vary, ratchet, chord changes through a progression, chord
keys, RESTART."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")
DEC_30MS = np.log2(6) / 9.64
STEP = 0.25  # 1/8 at 120 BPM


def pitches(e, n, step=STEP, base=None):
    L, _ = e.render(n * step)
    return step_pitches(L, step, n, base=base)


# ---- pools in A minor (root A = 9, the tonic at octave 1 = MIDI 36 + 9 + 12 = 57)
BASE = MIDI_BASE + 9 + 12
for pool, want in [("Key", [0, 2, 3, 5, 7, 8, 10, 0]), ("Triad", [0, 3, 7, 0, 3, 7, 0, 3]),
                   ("7th", [0, 3, 7, 10, 0, 3, 7, 10]), ("Fifths", [0, 7, 0, 7, 0, 7, 0, 7]), ("Root", [0] * 8)]:
    e = Engine()
    e.solo_arp(0, "Up", "1/8", length=16, pool=pool, rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
    e.set("root", 9)
    e.set("scale", SCALES.index("Minor"))
    got = pitches(e, 8, base=BASE)
    check(got == want, f"A minor, Up, pool {pool}: {got} (want {want})")
    e.close()

# ---- modes over a Triad (3 notes)
for mode, want in [("Down", [7, 3, 0, 7, 3, 0]), ("Up-down", [0, 3, 7, 3, 0, 3]), ("Loop", [0, 0, 0, 0, 0, 0])]:
    e = Engine()
    e.solo_arp(0, mode, "1/8", length=16, pool="Triad", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
    got = pitches(e, 6, base=BASE)
    check(got == want, f"mode {mode}: {got} (want {want})")
    e.close()
e = Engine()
e.solo_arp(0, "Random", "1/8", length=16, pool="Key", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
got = pitches(e, 16, base=BASE)
check(all(g in [0, 2, 3, 5, 7, 8, 10] for g in got) and len(set(got)) >= 4,
      f"Random: every note in the pool, {len(set(got))} different in 16 steps: {got}")
e.close()

# ---- range, degree, octave, master octave, root
e = Engine()
e.solo_arp(0, "Up", "1/8", length=16, pool="Triad", rng=2, dec=0.5, sus=1.0, gate=1.0, octave=1)
got = pitches(e, 7, base=BASE)
check(got == [0, 3, 7, 12, 15, 19, 0], f"range 2 octaves: {got}")
e.set("degree", 2, 0)  # the third of the chord: C E G in A minor
e.set("arp_range", 1, 0)
e.render(0.1)
got = pitches(e, 3, base=BASE)
check(sorted(got) == [3, 7, 10], f"degree 2 (chord built on the third): {sorted(got)} (want C E G = 3 7 10)")
e.set("degree", 0, 0)
e.set("oct", 2, 0)
e.set("octave", -1)
e.set("root", 0)  # C minor
e.render(0.1)
got = pitches(e, 3)
check(min(got) == MIDI_BASE + 0 + 12 * (2 - 1), f"octave 2, master -1, root C: lowest note MIDI {min(got)} (want {MIDI_BASE + 12})")
e.close()

# ---- LENGTH polymeter: 5 steps of a 7-note pool restart every 5
e = Engine()
e.solo_arp(0, "Up", "1/8", length=5, pool="Key", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
got = pitches(e, 10, base=BASE)
check(got == [0, 2, 3, 5, 7, 0, 2, 3, 5, 7], f"length 5 over a 7-note pool: {got}")
e.close()

# ---- Euclidean density: 3 of 8 -> steps 0, 3, 6 (the hardware's spread)
e = Engine()
e.solo_arp(0, "Loop", "1/8", length=8, pool="Root", dec=DEC_30MS, density=3 / 8, octave=3)
L, _ = e.render(4.0)
t = onsets(L)
steps = sorted(set(int(round(x / STEP)) % 8 for x in t))
check(steps == [0, 3, 6], f"density 3/8 over 8 steps: steps {steps} (want [0, 3, 6]), {len(t)} hits in 4 s")
e.set("arp_density", 1.0, 0)
L, _ = e.render(2.0)
check(len(onsets(L)) == 8, f"density 1: every step ({len(onsets(L))} in 2 s)")
e.set("arp_density", 0.0, 0)
L, _ = e.render(1.0)
check(rms(L[int(0.2 * SR):]) < 1e-4, "density 0: nothing plays")
e.close()

# ---- gate: the note length follows GATE with sustain 1
e = Engine()
e.solo_arp(0, "Loop", "1/4", length=16, pool="Root", dec=DEC_30MS, sus=1.0, gate=0.3, octave=3, att=0.0)
e.warm(0.5)
L, _ = e.render(2.0)
env, dt = env_frames(L, 5)
on = env > 0.3 * env.max()
# fraction of the step the note is on
frac = on.mean()
check(0.28 < frac < 0.42, f"gate 0.3 at 1/4: the note sounds {100 * frac:.0f} % of the step (30 % + a 30 ms release)")
e.set("arp_gate", 1.0, 0)
e.warm(0.5)
L, _ = e.render(2.0)
env, dt = env_frames(L, 5)
check((env > 0.3 * env.max()).mean() > 0.9, f"gate 1: legato, {100 * (env > 0.3 * env.max()).mean():.0f} % on")
e.close()

# ---- vary and ratchet
e = Engine()
e.solo_arp(0, "Loop", "1/8", length=16, pool="Key", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
e.set("arp_vary", 1.0, 0)
got = pitches(e, 16, base=BASE)
check(len(set(got)) >= 4, f"vary 1 on Loop: random pool notes, {len(set(got))} different: {got}")
e.set("arp_vary", 0.0, 0)
e.set("arp_ratchet", 1.0, 0)
e.set("amp_d", DEC_30MS, 0)
e.set("amp_s", 0.0, 0)
e.set("amp_r", DEC_30MS, 0)
e.set("oct", 3, 0)
e.warm(0.3)
L, _ = e.render(4.0)
t = onsets(L, gap=0.02)
per_step = len(t) / 16
check(2.0 <= per_step <= 4.0, f"ratchet 1: {per_step:.2f} hits per step on average (2..4)")
e.close()

# ---- a progression changes the chord: Pop I V vi IV in C major, 1 bar each, Triad Up
e = Engine()
e.solo_arp(0, "Up", "1/8", length=16, pool="Triad", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
e.set("root", 0)
e.set("scale", SCALES.index("Major"))
e.set("prog", PROGS.index("Pop"))
e.set("change", 1)  # 1 bar
base = MIDI_BASE + 12
got = pitches(e, 32, base=base)  # 4 bars of 8 eighths
bars = [sorted(set(got[8 * b:8 * b + 8])) for b in range(4)]
# chord tones stacked up from each chord's root (the hardware's way): C E G, G B D, A C E, F A C
check(bars == [[0, 4, 7], [7, 11, 14], [9, 12, 16], [5, 9, 12]],
      f"Pop in C major, Triad Up: bars {bars} (want C, G, Am, F, each from its root)")
fr = e.frames()
check(fr[-1].progLen == 4 and fr[-1].progPos == 3 and fr[-1].chordRootPc == 5,
      f"telemetry: progression {fr[-1].progPos + 1}/{fr[-1].progLen}, chord root pitch class {fr[-1].chordRootPc} (F = 5)")
e.close()

# ---- Custom: the progression from its step degrees, as long as its length says; the rest are ignored
e = Engine()
e.solo_arp(0, "Up", "1/8", length=16, pool="Triad", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
e.set("root", 0)
e.set("scale", SCALES.index("Major"))
e.set("prog", PROGS.index("Custom"))
e.set("change", 1)  # 1 bar
e.set("prog_len", 3)
for i, d in enumerate([1, 4, 0, 6]):  # ii V I (the 4th, vii, is past the length)
    e.set(f"prog_s{i + 1}", d)
got = pitches(e, 32, base=MIDI_BASE + 12)  # 4 bars: ii V I and round to ii again
bars = [sorted(set(got[8 * b:8 * b + 8])) for b in range(4)]
check(bars == [[2, 5, 9], [7, 11, 14], [0, 4, 7], [2, 5, 9]],
      f"Custom ii V I, length 3, in C major: bars {bars} (want Dm, G, C, Dm)")
fr = e.frames()
check(fr[-1].progLen == 3 and fr[-1].progPos == 0 and fr[-1].chordDeg == 1,
      f"telemetry: progression {fr[-1].progPos + 1}/{fr[-1].progLen}, degree {fr[-1].chordDeg} (ii = 1)")
e.close()

# ---- a chord key: Min7 with Key pool = the chord tones; Pop moves it in parallel
e = Engine()
e.solo_arp(0, "Up", "1/8", length=16, pool="Key", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
e.set("root", 9)
e.set("scale", SCALES.index("Min7"))
got = pitches(e, 4, base=BASE)
check(got == [0, 3, 7, 10], f"chord key Min7, pool Key: the chord's tones {got}")
e.close()

# ---- restart: a 6-step pattern (6 of the 7 key notes) rolls on with Free, restarts every bar (8 eighths) with Bar
for restart, want in [(0, [0, 2, 3, 5, 7, 8, 0, 2, 3, 5, 7, 8, 0, 2, 3, 5]), (1, [0, 2, 3, 5, 7, 8, 0, 2, 0, 2, 3, 5, 7, 8, 0, 2])]:
    e = Engine()
    e.solo_arp(0, "Up", "1/8", length=6, pool="Key", rng=1, dec=0.5, sus=1.0, gate=1.0, octave=1)
    e.set("restart", restart)
    got = pitches(e, 16, base=BASE)
    name = ["Free", "Bar"][restart]
    check(got == want, f"restart {name}, length 6 at 1/8: {got}")
    e.close()
done()
