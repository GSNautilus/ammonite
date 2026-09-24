"""Chord progression (KEY page PROG / CHANGE / RESTART) and the chord in the
centre of the screen: every preset in a major key, a minor key and a chord
key, changes landing on bar lines, pattern restarts."""
import ctypes

from arpsynth import *

DLL.synth_get_chord_name.restype = ctypes.c_char_p
DLL.synth_get_chord_numeral.restype = ctypes.c_char_p
PROGS = ["OFF", "POP", "50S", "SAD", "EPIC", "JAZZ", "VAMP", "CANON", "FALL"]
DEGS = {"POP": [0, 4, 5, 3], "50S": [0, 5, 3, 4], "SAD": [5, 3, 0, 4], "EPIC": [0, 5, 2, 6],
        "JAZZ": [1, 4, 0, 0], "VAMP": [0, 3], "CANON": [0, 4, 5, 2, 3, 0, 3, 4],
        "FALL": [0, 6, 5, 4]}
MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]
CHANGE_BEATS = [2, 4, 8, 16]


def chord_name(): return DLL.synth_get_chord_name().decode()
def chord_num(): return DLL.synth_get_chord_numeral().decode()


def key_setup(root, scale, prog, change=1, bpm=120, restart=0):
    fresh()
    drone()
    engine("ROOT", 0, st(root, 12))
    engine("SCALE", 0, st(scale, 20))
    engine("PROG", 0, st(PROGS.index(prog), 9))
    engine("CHANGE", 0, st(change, 4))
    engine("RESTART", 0, st(restart, 3))
    engine("TEMPO", 0, (bpm - 40) / 200)
    engine("DEGREE", 0, st(0, 8))


def walk(n, change=1, bpm=120):
    """Osc 1 drone root (semitones above C2) + chord text, mid each chord."""
    cs = CHANGE_BEATS[change] * 60 / bpm
    out = []
    render(cs / 2, 0)
    for _ in range(n):
        out.append((int(round(note(0))) - 36, chord_name(), chord_num()))
        render(cs, 0)
    return out


# ---- every preset: major (C), minor (A), chord key (A MIN7, parent A minor)
for prog, degs in DEGS.items():
    n = len(degs)
    key_setup(0, 0, prog)                       # C major
    got = [r for r, _, _ in walk(2 * n)]
    want = [MAJOR[d] for d in degs] * 2
    check(got == want, f"{prog} in C major: roots {got}")
    key_setup(9, 1, prog)                       # A minor
    got = [r - 9 for r, _, _ in walk(n)]
    check(got == [MINOR[d] for d in degs], f"{prog} in A minor: roots {got}")
    key_setup(9, 15, prog)                      # A MIN7 (chord key)
    got = walk(n)
    ok = [r - 9 for r, _, _ in got] == [MINOR[d] for d in degs] and all(nm.endswith("m7") for _, nm, _ in got)
    check(ok, f"{prog} on an A MIN7 chord key: parallel m7 chords {[nm for _, nm, _ in got]}")

# ---- chord names and numerals
key_setup(0, 0, "POP")
got = [(nm, nu) for _, nm, nu in walk(4)]
check(got == [("C", "I"), ("G", "V"), ("Am", "vi"), ("F", "IV")], f"POP in C: {got}")
key_setup(0, 0, "FALL")
got = [(nm, nu) for _, nm, nu in walk(4)]
check(got == [("C", "I"), ("Bdim", "vii"), ("Am", "vi"), ("G", "V")], f"FALL in C: {got}")
key_setup(9, 1, "EPIC")
got = [(nm, nu) for _, nm, nu in walk(4)]
check(got == [("Am", "i"), ("F", "VI"), ("C", "III"), ("G", "VII")], f"EPIC in A minor: {got}")
key_setup(0, 0, "JAZZ")
got = [(nm, nu) for _, nm, nu in walk(4)]
check(got == [("Dm", "ii"), ("G", "V"), ("C", "I"), ("C", "I")], f"JAZZ in C: {got}")
key_setup(2, 13, "OFF")                         # D SUS4 chord key, manual CHORD I
render(0.1, 0)
check((chord_name(), chord_num()) == ("Dsus4", "I"), f"D SUS4 chord key: {chord_name()} {chord_num()}")

# ---- CHANGE lengths
for ch, beats in enumerate(CHANGE_BEATS):
    key_setup(0, 0, "VAMP", change=ch, bpm=240)
    cs = beats * 60 / 240
    render(cs * 0.9, 0)
    a = int(round(note(0)))
    render(cs * 0.2, 0)
    b = int(round(note(0)))
    check((a - 36, b - 36) == (0, 5), f"CHANGE {['1/2', '1', '2', '4'][ch]} bar: C -> F after {cs:.2f} s")

# ---- changes land on bar lines: the step starting on the bar gets the new chord
def arp_osc(div, restart=0, length=16, pool="ROOT", prog="POP", change=1, octave=3):
    key_setup(0, 0, prog, change=change, restart=restart)
    solo(0)
    engine("DETUNE", 0, 0.0)
    engine("SHAPE", 0, 0.0)
    engine("CUTOFF", 0, 1.0)
    engine("AMODE", 0, st(2, 6))                # UP
    engine("APOOL", 0, st(["KEY", "TRIAD", "7TH", "FIFTHS", "ROOT"].index(pool), 5))
    engine("ARANGE", 0, st(1, 3))
    engine("ADIV", 0, st(["1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"].index(div), 9))
    engine("ALENGTH", 0, st(length - 1, 16))
    engine("AGATE", 0, 0.9)
    engine("EASUS", 0, 1.0)
    engine("EAATT", 0, 0.0)
    engine("OCTAVE", 0, st(octave + 1, 5))


def pitch_at(x, t0, t1, base):
    return int(round(hz_midi(peak_freq(x[int(t0 * SR):int(t1 * SR)], 100, 6000)) - base))


arp_osc("1/4", octave=1)                        # C3 base, ROOT pool: the chord root each step
l, r = render(8.0, 0)
x = l + r
got = [pitch_at(x, k * 0.5 + 0.03, (k + 1) * 0.5 - 0.02, 48) % 12 for k in range(16)]
check(got == [0] * 4 + [7] * 4 + [9] * 4 + [5] * 4, f"1/4 steps: chord changes every 4th step {got}")
arp_osc("1/16T", octave=3)                      # 83 ms steps, 24 per bar
l, r = render(4.0, 0)
x = l + r
st_ = 1 / 12                                    # 1/16T at 120 BPM
before = pitch_at(x, 23 * st_ + 0.01, 24 * st_ - 0.005, 72) % 12
after = pitch_at(x, 24 * st_ + 0.01, 25 * st_ - 0.005, 72) % 12
check((before, after) == (0, 7), f"1/16T: step 23 still C, step 24 (on the bar) G ({before}, {after})")

# ---- RESTART: FREE / BAR / CHORD (LENGTH 3 across 4-step bars)
def seq(restart, change=1, prog="OFF", n=12):
    arp_osc("1/4", restart=restart, length=3, pool="KEY", prog=prog, change=change, octave=1)
    l, r = render(n * 0.5, 0)
    x = l + r
    return [pitch_at(x, k * 0.5 + 0.03, (k + 1) * 0.5 - 0.02, 48) for k in range(n)]


got = seq(0)
check(got == [0, 2, 4] * 4, f"RESTART FREE: LENGTH 3 runs across bars {got}")
got = seq(1)
check(got == [0, 2, 4, 0] * 3, f"RESTART BAR: back to step 1 on every bar {got}")
got = seq(2, change=2, n=16)                    # every 2 bars = 8 steps
check(got == [0, 2, 4, 0, 2, 4, 0, 2] * 2, f"RESTART CHORD (every 2 bars): {got}")

# ---- KEY page readouts
key_setup(0, 0, "CANON", change=2, restart=1)
page("KEY")
check((text(0), text(1), text(3)) == ("CANON", "2 BARS", "BAR"), "KEY readouts: CANON, 2 BARS, BAR")

# ---- the chord in the centre of the screen once the readout fades
fb = np.zeros(240 * 240, dtype=np.uint16)
key_setup(0, 0, "POP")
render(4.5, 0)                                  # third chord: Am
for _ in range(100):                            # let any readout fade (86 frames)
    DLL.synth_render(fb.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)))
f = fb.reshape(240, 240)
lit = int((f[95:165, 80:160] != 0).sum())
check(chord_name() == "Am" and lit > 150, f"centre shows the chord ({chord_name()}, {lit} px lit)")

done()
