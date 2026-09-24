"""ENVELOPE FILTER (attack / decay / amount), ENVELOPE PITCH (amount /
decay) and GLIDE (arp notes; drones on chord changes). Brightness and pitch
are measured over time from the audio."""
from arpsynth import *

MODES = ["OFF", "LOOP", "UP", "DOWN", "UP-DN", "RANDOM"]
DIVS = ["1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"]
POOLS = ["KEY", "TRIAD", "7TH", "FIFTHS", "ROOT"]


def ms_to(sec, mn, octs): return np.log2(sec / mn) / octs      # env time -> knob
def att(sec): return ms_to(sec, 0.001, 12.0)
def dec(sec): return ms_to(sec, 0.005, 9.64)
def pdec(sec): return ms_to(sec, 0.005, 7.64)


def voice(mode="LOOP", div="1/4", pool="ROOT", shape=0.0, cutoff=1.0, sus=1.0, gate=0.9):
    """Osc 1 alone at A3, dry, a held note per step (SUSTAIN 1)."""
    fresh()
    drone()
    solo(0)
    engine("DETUNE", 0, 0.0)
    engine("SHAPE", 0, shape)
    engine("CUTOFF", 0, cutoff)
    engine("RESO", 0, 0.0)
    engine("AMODE", 0, st(MODES.index(mode), 6))
    engine("ADIV", 0, st(DIVS.index(div), 9))
    engine("APOOL", 0, st(POOLS.index(pool), 5))
    engine("ARANGE", 0, st(0, 3))
    engine("AGATE", 0, gate)
    engine("EAATT", 0, 0.0)
    engine("EASUS", 0, sus)
    engine("EFAMT", 0, 0.5)                    # filter env off unless a test sets it
    engine("EPAMT", 0, 0.5)                    # pitch env off
    engine("GLIDE", 0, 0.0)
    engine("OCTAVE", 0, st(2, 5))              # +1: A3
    engine("TEMPO", 0, (120 - 40) / 200)


def centroid(x):
    fr, sp = spectrum(x)
    return float((sp * fr).sum() / (sp.sum() + 1e-12))


def win(x, t0, t1): return x[int(t0 * SR):int(t1 * SR)]


def zc_freq(x):
    """Frequency from rising zero crossings (linear interpolation)."""
    s = np.signbit(x)
    i = np.flatnonzero(s[:-1] & ~s[1:])
    if len(i) < 3:
        return 0.0
    t = i + x[i] / (x[i] - x[i + 1])
    return float((len(t) - 1) * SR / (t[-1] - t[0]))


def semis(f, ref=57): return float(hz_midi(f) - ref) if f > 0 else -99.0


# ---- FILTER env: AMOUNT opens a dark saw at the hit, then it closes again
def bright(amount, a=0.0, d=0.2, cut=np.log2(2.5) / 7.23):   # CUTOFF 200 Hz
    voice(shape=1.0, cutoff=cut)
    engine("EFAMT", 0, amount)
    engine("EFATT", 0, a)
    engine("EFDEC", 0, dec(d))
    l, r = render(2.0, 0)
    x = l + r
    return [centroid(win(x, 0.5 + t, 0.5 + t + 0.02)) for t in (0.005, 0.1, 0.2, 0.4)]


c0 = bright(0.5)
c4 = bright(1.0)
check(max(c0) / min(c0) < 1.15, f"FILTER AMOUNT 0: brightness steady {[int(c) for c in c0]} Hz")
check(c4[0] > 2.5 * c4[3] and c4[3] < 1.3 * c0[3],
      f"FILTER AMOUNT +4 oct: bright at the hit, back to the base by 400 ms {[int(c) for c in c4]} Hz")
cneg = bright(0.0, cut=0.8)                    # from 5 kHz down
cflat = bright(0.5, cut=0.8)
check(cneg[0] < 0.6 * cflat[0] and cneg[3] > 0.85 * cflat[3],
      f"FILTER AMOUNT -4 oct: dark at the hit, opens back up {[int(c) for c in cneg]} Hz")
slow = bright(1.0, a=att(0.2), d=0.4)
check(slow[2] > 2 * slow[0], f"FILTER ATTACK 200 ms: brightest late, not at the hit {[int(c) for c in slow]} Hz")
short_, long_ = bright(1.0, d=0.05), bright(1.0, d=1.0)
check(long_[1] > 2 * short_[1], f"FILTER DECAY 1 s stays open longer than 50 ms ({int(long_[1])} vs {int(short_[1])} Hz at 100 ms)")

# ---- PITCH env: the note starts AMOUNT away and falls back over DECAY
def pitch_curve(amount_st, d):
    voice(div="1/1")                           # 2 s steps: one hit per curve
    engine("EPAMT", 0, 0.5 + amount_st / 24)
    engine("EPDEC", 0, pdec(d))
    l, r = render(2.0, 0)
    x = l + r
    return [semis(zc_freq(win(x, a, b))) for a, b in ((0.02, 0.06), (0.3, 0.4), (0.8, 0.95))]


up = pitch_curve(12, 1.0)
check(8.5 < up[0] < 11.5 and 1 < up[1] < 4.5 and abs(up[2]) < 0.6,
      f"PITCH +12 ST, DECAY 1 s: {up[0]:+.1f} -> {up[1]:+.1f} -> {up[2]:+.1f} st (want ~+10, ~+2.5, 0)")
dn = pitch_curve(-12, 1.0)
check(-11.5 < dn[0] < -8.5 and abs(dn[2]) < 0.6, f"PITCH -12 ST: {dn[0]:+.1f} -> {dn[2]:+.1f} st")
fast = pitch_curve(12, 0.05)
check(all(abs(v) < 0.05 for v in fast[1:]), f"PITCH DECAY 50 ms: back on the note by 300 ms {[round(v, 2) for v in fast]}")
page("ENVELOPE")
sub(2)
check(text(0) == "+12 ST", f"PITCH AMOUNT readout {text(0)}")

# ---- GLIDE: arp notes slide from the previous note (FIFTHS: A3 -> E4 -> A3 ...)
def glide_curve(g):
    voice(mode="UP", div="1/2", pool="FIFTHS")   # 1 s steps
    engine("GLIDE", 0, g)
    l, r = render(3.0, 0)
    x = l + r
    return [semis(zc_freq(win(x, 1.0 + a, 1.0 + b))) for a, b in ((0.02, 0.06), (0.6, 0.9))]


g0 = glide_curve(0.0)
check(abs(g0[0] - 7) < 0.4 and abs(g0[1] - 7) < 0.3, f"GLIDE 0: the fifth is there at once {g0}")
g5 = glide_curve(0.5)
check(1.0 < g5[0] < 4.0 and abs(g5[1] - 7) < 0.3, f"GLIDE 500 ms: slides up from the root {g5[0]:+.1f} -> {g5[1]:+.1f} st")
page("ENVELOPE")
sub(2)
check(text(6) == "500 MS", f"GLIDE readout {text(6)}")

# ---- drones glide on chord changes: at least 370 ms, GLIDE makes it longer
def drone_change(g):
    fresh()
    drone()
    solo(0)
    engine("DETUNE", 0, 0.0)
    engine("SHAPE", 0, 0.0)
    engine("CUTOFF", 0, 1.0)
    engine("EPAMT", 0, 1.0)                    # +12 pitch env: must not touch a drone
    engine("GLIDE", 0, g)
    render(0.5, 0)
    engine("CHORD", 0, st(3, 7))               # i -> iv: A2 -> D3 (+5)
    l, r = render(1.2, 0)
    x = l + r
    return [semis(zc_freq(win(x, a, b)), 45) for a, b in ((0.03, 0.08), (0.5, 0.6), (1.0, 1.15))]


d0 = drone_change(0.0)
check(0.3 < d0[0] < 4.0 and abs(d0[1] - 5) < 0.3 and abs(d0[2] - 5) < 0.2,
      f"drone, GLIDE 0: slides A2 -> D3 in ~0.37 s {d0}")
d1 = drone_change(1.0)
check(d1[1] < 4.8 and abs(d1[2] - 5) < 0.3, f"drone, GLIDE 1 s: still sliding at 0.5 s {d1}")

done()
