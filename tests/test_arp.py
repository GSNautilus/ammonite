"""Master clock + ARP (NOTES / RHYTHM / CHANCE) + AMP envelope, measured
from the audio: step timing, the three grids staying locked, pools, modes,
polymeter, Euclidean density, gate, release = decay, swing, ratchet, vary."""
from arpsynth import *

MODES = ["OFF", "LOOP", "UP", "DOWN", "UP-DN", "RANDOM"]
DIVS = ["1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"]
DIV_BEATS = [4, 2, 1, 2 / 3, 0.5, 1 / 3, 0.25, 1 / 6, 0.125]
POOLS = ["KEY", "TRIAD", "7TH", "FIFTHS", "ROOT"]
DEC_30MS = np.log2(6) / 9.64       # DECAY 30 ms (99 % in the shown time)


def setup(o, mode, div="1/8", length=16, pool="KEY", rng=1, density=1.0, gate=0.5,
          sus=0.0, dec=DEC_30MS, octave=1, bpm=120):
    """Osc o alone, a clean sine pluck, its arp set directly (engine)."""
    fresh()
    drone()
    solo(o)
    engine("DETUNE", 0, 0.0)
    engine("SHAPE", o, 0.0)
    engine("CUTOFF", o, 1.0)
    engine("AMODE", o, st(MODES.index(mode), 6))
    engine("ADIV", o, st(DIVS.index(div), 9))
    engine("ALENGTH", o, st(length - 1, 16))
    engine("APOOL", o, st(POOLS.index(pool), 5))
    engine("ARANGE", o, st(rng - 1, 3))
    engine("ADENSITY", o, density)
    engine("AGATE", o, gate)
    engine("EAATT", o, 0.0)
    engine("EADEC", o, dec)
    engine("EASUS", o, sus)
    engine("DEGREE", o, st(0, 8))
    engine("OCTAVE", o, st(octave + 1, 5))
    engine("TEMPO", 0, (bpm - 40) / 200)


def step_notes(nsteps, step_sec, base):
    """Pitch of each step (semitones above `base`) from the start (warm 0)."""
    l, r = render(nsteps * step_sec, 0)
    x = l + r
    out = []
    for k in range(nsteps):
        a, b = int((k * step_sec + 0.03) * SR), int(((k + 1) * step_sec - 0.02) * SR)
        out.append(int(round(hz_midi(peak_freq(x[a:b], 50, 4000)) - base)))
    return out


# ---- step timing (pluck, LOOP)
for div, bpm, want in [("1/8", 120, 0.25), ("1/8", 60, 0.5), ("1/16", 120, 0.125),
                       ("1/4T", 120, 1 / 3)]:
    setup(0, "LOOP", div, octave=3, bpm=bpm)
    l, r = render(3.0)
    iv = np.diff(onsets(l + r))
    check(len(iv) > 4 and abs(iv.mean() - want) < 0.001 and iv.std() < 0.001,
          f"{div} at {bpm} BPM: steps {iv.mean() * 1000:.1f} ms (want {want * 1000:.1f})")

# ---- three grids from one clock: no drift over a minute at an odd tempo
bpm = 137
errs = []
for o, div in enumerate(["1/4T", "1/8T", "1/16"]):
    setup(o, "LOOP", div, octave=3, bpm=bpm)
    l, r = render(60.0, 0)
    t = onsets(l + r)
    stp = DIV_BEATS[DIVS.index(div)] * 60 / bpm
    e = t - np.round(t / stp) * stp
    errs.append(e)
    # max err: onset detection jitter (sine phase vs the 1 ms attack); drift is the lock
    check(len(t) > 100 and np.abs(e).max() < 0.002 and abs(e[:20].mean() - e[-20:].mean()) < 0.0002,
          f"osc {o + 1} {div} @ {bpm}: {len(t)} steps on the shared grid for 60 s "
          f"(max err {np.abs(e).max() * 1000:.2f} ms, drift {(e[-20:].mean() - e[:20].mean()) * 1000:.3f} ms)")

# ---- pools (A minor, osc 1 at A3, UP from the root, 1 octave)
want = {"KEY": [0, 2, 3, 5, 7, 8, 10, 0], "TRIAD": [0, 3, 7, 0, 3, 7, 0, 3],
        "7TH": [0, 3, 7, 10, 0, 3, 7, 10], "FIFTHS": [0, 7, 0, 7, 0, 7, 0, 7],
        "ROOT": [0, 0, 0, 0, 0, 0, 0, 0]}
for pool, w in want.items():
    setup(0, "UP", "1/4", pool=pool, sus=1.0, gate=0.9)
    got = step_notes(8, 0.5, 57)
    check(got == w, f"POOL {pool}: {got}")
setup(0, "UP", "1/4", pool="TRIAD", rng=2, sus=1.0, gate=0.9)
got = step_notes(8, 0.5, 57)
check(got == [0, 3, 7, 12, 15, 19, 0, 3], f"TRIAD over RANGE 2: {got}")

# ---- modes
setup(0, "DOWN", "1/4", pool="KEY", sus=1.0, gate=0.9)
got = step_notes(8, 0.5, 57)
check(got == [10, 8, 7, 5, 3, 2, 0, 10], f"DOWN: {got}")
setup(0, "UP-DN", "1/4", pool="TRIAD", sus=1.0, gate=0.9)
got = step_notes(8, 0.5, 57)
check(got == [0, 3, 7, 3, 0, 3, 7, 3], f"UP-DN: {got}")
setup(0, "RANDOM", "1/4", pool="KEY", rng=2, sus=1.0, gate=0.9)
got = step_notes(12, 0.5, 57)
inkey = all((g % 12) in (0, 2, 3, 5, 7, 8, 10) and 0 <= g < 24 for g in got)
check(inkey and len(set(got)) >= 5, f"RANDOM: all in the 2-octave pool, varied: {got}")

# ---- LENGTH: the pattern restarts every LENGTH steps (polymeter)
setup(0, "UP", "1/4", length=5, pool="KEY", rng=2, sus=1.0, gate=0.9)
got = step_notes(12, 0.5, 57)
check(got == [0, 2, 3, 5, 7] * 2 + [0, 2], f"LENGTH 5 over a 14-note pool: {got}")

# ---- the arp follows the chord (KEY CHORD IV = D minor in A minor)
setup(0, "UP", "1/4", pool="TRIAD", sus=1.0, gate=0.9)
engine("CHORD", 0, st(3, 7))
got = step_notes(6, 0.5, 57)
check(got == [5, 8, 12, 5, 8, 12], f"CHORD IV: TRIAD from D (D F A): {got}")

# ---- DENSITY: Euclidean hits over LENGTH
setup(0, "LOOP", "1/8", length=8, density=3 / 8, octave=3)
l, r = render(4.0, 0)
steps = [int(v) for v in np.round(onsets(l + r) / 0.25)]
check(steps == [0, 3, 6, 8, 11, 14], f"DENSITY 3/8: hits on steps {steps} (tresillo)")
page("ARP")
sub(2)
check(text(0) == "3/8", f"DENSITY readout {text(0)}")
setup(0, "LOOP", "1/8", length=8, density=0.0, octave=3)
l, r = render(2.0, 0)
check(np.abs(l + r).max() < 1e-6, "DENSITY 0: silent from the first sample (no hit, no swell)")

# ---- GATE: note length as a fraction of the step (SUSTAIN 1, fast release)
for g in (0.2, 0.7):
    setup(0, "LOOP", "1/4", gate=g, sus=1.0, dec=0.0, octave=3)
    l, r = render(4.0)
    x = l + r
    fr = np.abs(x[: len(x) // 48 * 48]).reshape(-1, 48).max(axis=1)
    duty = float((fr > 0.5 * fr.max()).mean())
    check(abs(duty - (0.05 + 0.95 * g)) < 0.02, f"GATE {g}: duty {duty:.3f} (want {0.05 + 0.95 * g:.3f})")

# ---- release = decay: after the gate the note fades at the DECAY rate
dec200 = np.log2(40) / 9.64        # DECAY 200 ms
setup(0, "LOOP", "1/4", gate=0.5, sus=1.0, dec=dec200, octave=3)
l, r = render(2.0, 1.0)            # warm 1 s = two whole steps: a step starts at t = 0
x = np.abs(l + r)
fr = x[: len(x) // 48 * 48].reshape(-1, 48).max(axis=1)   # 1 ms frames
off = int(round(0.525 * 0.5 * 1000))                      # gate end, ms into the step
held, later = fr[off - 20], fr[off + 100]
ratio = later / held
check(0.06 < ratio < 0.16, f"release follows DECAY 200 ms: 100 ms after the gate {ratio:.3f} (~0.10)")

# ---- SWING 1: 1/8 steps alternate 2/3 : 1/3 of a quarter
setup(0, "LOOP", "1/8", octave=3)
engine("SWING", 0, 1.0)
l, r = render(3.0, 1.0)
iv = np.diff(onsets(l + r))
long_, short_ = iv[0::2], iv[1::2]
ok = np.allclose(sorted([long_.mean(), short_.mean()]), [1 / 6, 1 / 3], atol=0.002)
check(ok, f"SWING 1 at 1/8, 120 BPM: {long_.mean() * 1000:.0f} / {short_.mean() * 1000:.0f} ms")

# ---- RATCHET 1: every step splits into 2..4 hits
setup(0, "LOOP", "1/4", octave=3)
engine("ARATCHET", 0, 1.0)
l, r = render(4.0, 1.0)
t = onsets(l + r)
per = [int(v) for v in np.bincount((t / 0.5).astype(int), minlength=8)[:8]]
check(min(per) >= 2 and max(per) <= 4 and len(set(per)) > 1, f"RATCHET 1: hits per step {per}")

# ---- VARY 1 turns LOOP (one note) into random pool notes
setup(0, "LOOP", "1/4", pool="KEY", rng=2, sus=1.0, gate=0.9)
engine("AVARY", 0, 1.0)
got = step_notes(12, 0.5, 57)
check(len(set(got[1:])) >= 4, f"VARY 1 on LOOP: {got}")

# ---- MODE OFF: a steady drone, envelope bypassed
fresh()
drone()
engine("MREVERB", 0, 0.0)
engine("DETUNE", 0, 0.0)           # detuned pairs beat (~1 Hz): chorus, not gating
l, r = render(2.0, 0.5)
x = l + r
w = np.array([rms(x[i:i + 2400]) for i in range(0, len(x) - 2400, 2400)])
check(w.max() / w.min() < 1.1, f"MODE OFF: steady drone (level {w.min():.3f}..{w.max():.3f})")

# ---- power-on: all three arpeggiate
for o in range(3):
    fresh()
    solo(o)
    l, r = render(3.0, 0.5)
    n = len(onsets(l + r, rel=0.4))
    check(n >= 4, f"boot: osc {o + 1} plays notes ({n} onsets in 3 s)")

done()
