"""LFO page: one LFO per target (PITCH, CUTOFF, AMP, PAN, SHAPE) and
oscillator, with RATE, DEPTH and WAVE; LFO SYNC = TEMPO locks them to the
master clock. All measured from the audio of one droning oscillator."""
from arpsynth import *

TARGETS = ["PITCH", "CUT", "AMP", "PAN", "SHAPE"]
WAVES = ["SINE", "TRI", "SAW", "SQUARE", "S+H", "DRIFT"]


def rate(hz): return np.log2(hz / 0.02) / 9.966


def drone_osc(o=0, shape=0.0, cutoff=1.0):
    """Osc o alone, droning (A2 / C4 / E5 by default), dry, no detune."""
    fresh()
    drone()
    solo(o)
    engine("DETUNE", 0, 0.0)
    engine("SHAPE", o, shape)
    engine("CUTOFF", o, cutoff)
    engine("RESO", o, 0.0)
    engine("WIDTH", o, 0.0)
    for t in TARGETS:                          # every LFO off (CUTOFF defaults to 0.15)
        engine(f"L{t}_D", o, 0.0)


def lfo(o, target, hz=None, depth=1.0, wave="SINE", sync_pos=None):
    engine(f"L{target}_R", o, rate(hz) if hz else sync_pos)
    engine(f"L{target}_D", o, depth)
    engine(f"L{target}_W", o, st(WAVES.index(wave), 6))


def track(x, fn, step=0.01, width=0.02):
    """fn over sliding windows: (times, values)."""
    ts = np.arange(0, len(x) / SR - width, step)
    return ts, np.array([fn(x[int(t * SR):int((t + width) * SR)]) for t in ts])


def zc_freq(x):
    s = np.signbit(x)
    i = np.flatnonzero(s[:-1] & ~s[1:])
    if len(i) < 3:
        return np.nan
    t = i + x[i] / (x[i] - x[i + 1])
    return float((len(t) - 1) * SR / (t[-1] - t[0]))


def period(ts, v):
    """Period of an oscillating track from its mean crossings."""
    c = v - np.nanmean(v)
    up = ts[1:][(c[:-1] < 0) & (c[1:] >= 0)]
    return float(np.diff(up).mean()) if len(up) > 2 else np.nan


def centroid(x):
    fr, sp = spectrum(x)
    return float((sp * fr).sum() / (sp.sum() + 1e-12))


BASE = 57                                      # osc 1 at A3 (OCTAVE +1) for pitch reads

# ---- PITCH: +-1 semitone at DEPTH 1, half at 0.5, at RATE
for depth, want in ((1.0, 1.0), (0.5, 0.5)):
    drone_osc()
    engine("OCTAVE", 0, st(2, 5))
    lfo(0, "PITCH", hz=2.0, depth=depth)
    l, r = render(3.0, 0.5)
    ts, f = track(l + r, zc_freq, width=0.03)
    dev = hz_midi(f) - BASE
    per = period(ts, dev)
    check(abs(dev.max() - want) < 0.12 and abs(dev.min() + want) < 0.12 and abs(per - 0.5) < 0.02,
          f"PITCH DEPTH {depth}: {dev.min():+.2f}..{dev.max():+.2f} st, period {per:.3f} s (2 Hz)")

# ---- waves on PITCH: square = two levels, saw = ramps, S+H = steps, DRIFT = smooth
def pitch_track(wave, hz=1.0):
    drone_osc()
    engine("OCTAVE", 0, st(2, 5))
    lfo(0, "PITCH", hz=hz, wave=wave)
    l, r = render(4.0, 0.5)
    ts, f = track(l + r, zc_freq, width=0.02)
    return ts, hz_midi(f) - BASE


ts, d = pitch_track("SQUARE")
share = np.mean((np.abs(np.abs(d) - 1) < 0.1))
check(share > 0.9, f"SQUARE: the pitch sits at +-1 st {share:.0%} of the time")
ts, d = pitch_track("SAW")
jumps = np.flatnonzero(np.diff(d) < -1.0)
rising = np.mean(np.diff(d)[np.diff(d) > -0.5] > 0)
check(len(jumps) in (3, 4, 5) and rising > 0.9, f"SAW: rises, drops {len(jumps)} times in 4 s at 1 Hz")
ts, d = pitch_track("S+H", hz=4.0)
flat = np.mean(np.abs(np.diff(d)) < 0.02)
levels = len(set(np.round(d[~np.isnan(d)], 1)))
check(flat > 0.7 and levels >= 6, f"S+H: holds between steps ({flat:.0%} flat), {levels} levels")
ts, d = pitch_track("DRIFT", hz=2.0)
check(np.nanmax(np.abs(np.diff(d))) < 0.2 and np.nanstd(d) > 0.1,
      f"DRIFT: wanders smoothly (largest 10 ms move {np.nanmax(np.abs(np.diff(d))):.2f} st)")
ts, d = pitch_track("TRI")
check(abs(np.nanmax(d) - 1) < 0.12 and abs(np.nanmin(d) + 1) < 0.12, f"TRI: +-1 st ({np.nanmin(d):+.2f}..{np.nanmax(d):+.2f})")

# ---- CUTOFF: a saw's brightness swings at the LFO rate
drone_osc(shape=1.0, cutoff=0.45)              # ~760 Hz
engine("OCTAVE", 0, st(2, 5))
lfo(0, "CUT", hz=1.0)
l, r = render(4.0, 0.5)
ts, c = track(l + r, centroid, step=0.02, width=0.04)
per = period(ts, c)
check(c.max() > 2.5 * c.min() and abs(per - 1.0) < 0.05,
      f"CUTOFF: brightness {c.min():.0f}..{c.max():.0f} Hz, period {per:.2f} s")

# ---- AMP: dips only, to 1 - DEPTH
for depth in (1.0, 0.5):
    drone_osc()
    lfo(0, "AMP", hz=2.0, depth=depth, wave="SQUARE")
    l, r = render(3.0, 0.5)
    ts, a = track(l + r, rms, step=0.01, width=0.02)
    hi, lo = np.percentile(a, 95), np.percentile(a, 5)
    check(abs(lo / hi - (1 - depth)) < 0.05, f"AMP DEPTH {depth}: low / high = {lo / hi:.2f} (want {1 - depth:.2f})")

# ---- PAN: swings left and right
drone_osc()
lfo(0, "PAN", hz=1.0)
l, r = render(3.0, 0.5)
win = int(0.02 * SR)
bal = np.array([(rms(r[i:i + win]) - rms(l[i:i + win])) / (rms(r[i:i + win]) + rms(l[i:i + win]) + 1e-9)
                for i in range(0, len(l) - win, int(0.02 * SR))])
check(bal.max() > 0.9 and bal.min() < -0.9, f"PAN: balance swings {bal.min():+.2f}..{bal.max():+.2f}")

# ---- SHAPE: a sine gains odd harmonics as the wave pushes toward square
drone_osc(shape=0.0)
lfo(0, "SHAPE", hz=1.0)
l, r = render(3.0, 0.5)
f0 = 110.0
ts, h3 = track(l + r, lambda w: band_level(w, 3 * f0, 0.1) / (band_level(w, f0, 0.1) + 1e-9), step=0.02, width=0.1)
check(h3.max() > 0.15 and h3.min() < 0.02, f"SHAPE: 3rd harmonic / fundamental {h3.min():.3f}..{h3.max():.3f}")

# ---- one LFO per oscillator: osc 1's PITCH LFO leaves osc 2 alone
drone_osc(o=1)
engine("SHAPE", 1, 0.0)
lfo(0, "PITCH", hz=2.0)
l, r = render(2.0, 0.5)
ts, f = track(l + r, zc_freq, width=0.03)
dev = hz_midi(f) - 60
check(np.nanmax(np.abs(dev)) < 0.03, f"osc 1's PITCH LFO does not touch osc 2 ({np.nanmax(np.abs(dev)):.3f} st)")

# ---- LFO SYNC = TEMPO: RATE is a division, phase locked to the beat
drone_osc()
engine("OCTAVE", 0, st(2, 5))
engine("LFOSYNC", 0, st(1, 2))
engine("TEMPO", 0, (120 - 40) / 200)
lfo(0, "PITCH", sync_pos=st(4, 9), wave="SQUARE")   # 1/4 at 120 BPM = 0.5 s
l, r = render(4.0, 0)
ts, d = track(l + r, zc_freq, step=0.002, width=0.01)
d = hz_midi(d) - BASE
lv = np.where(d > 0.5, 1, np.where(d < -0.5, -1, 0))   # clear +1 / -1 readings only
k = np.flatnonzero(lv != 0)
ch = np.flatnonzero(lv[k][1:] != lv[k][:-1])
flips = (ts[k[ch]] + ts[k[ch + 1]]) / 2 + 0.005        # window centre between the levels
flips = flips[flips > 0.1]
err = np.abs(flips - np.round(flips / 0.25) * 0.25)
check(len(flips) >= 12 and err.max() < 0.012, f"SYNC 1/4 square: {len(flips)} flips on the 1/8-note grid (max off {err.max() * 1000:.1f} ms)")
page("LFO")
check(text(0) == "1/4", f"SYNC RATE readout {text(0)}")
engine("LFOSYNC", 0, st(0, 2))
render(0.02, 0)
check(text(0).endswith("HZ"), f"FREE RATE readout {text(0)}")

done()
