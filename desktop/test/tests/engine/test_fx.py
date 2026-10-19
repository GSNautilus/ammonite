"""The effects (phase 4, package B), measured from the audio: delay echoes
on the division to the sample (straight, dotted, triplet) and falling by
the feedback; ping-pong alternating sides; the chorus widening (left and
right decorrelate) and staying in tune, its modes (Ensemble wider, every
mode in tune, Chorus held to 20 cents of swing, Vibrato all wet); drive
adding harmonics with the level kept; the reverb's decay time following
DECAY, its damping and width; ducking dipping the target on each source hit; the limiter holding
its ceiling (sample and inter-sample peaks) and the glue evening levels
out; no NaN and no tail left after silence; identical audio for any block
size with every effect on; CPU with 16 channels and every effect on.

A pluck: Loop at 1/1 on one channel, a 30 ms decay, so one short note
starts at sample 0 and every echo stands alone."""
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")
DEC_30MS = np.log2(6) / 9.64
DDIVS = ["1/32", "1/16", "1/16T", "1/8", "1/8T", "1/8.", "1/4", "1/4T", "1/4.", "1/2", "1/1"]
DDIV_BEATS = [0.125, 0.25, 1 / 6, 0.5, 1 / 3, 0.75, 1, 2 / 3, 1.5, 2, 4]


def pluck(e, bpm=120, octave=2):
    """One short note at sample 0, the next 2 s later (1/1 at 120 BPM)."""
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=octave, bpm=bpm, dec=DEC_30MS, shape=0.0)
    e.set("volume", 0.8)  # the default: a fresh engine's volume would glide from it for 20 ms while the note sounds


def delay(e, div="1/8", fb=0.5, damp=0.0, pp=0, mix=1.0, slot=0):
    e.set("dly_div", DDIVS.index(div), slot)
    e.set("dly_fb", fb, slot)
    e.set("dly_damp", damp, slot)
    e.set("dly_pp", pp, slot)
    e.set("dly_mix", mix, slot)


def tone(**kw):
    """A held A4 (440 Hz) at the voices' usual level: Loop at 1/1 at 60 BPM, gate 1, sustain 1."""
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=2, bpm=60, dec=0.5, sus=1.0, gate=1.0, att=0.0, **kw)
    return e


def corr(a, b):
    a = a - a.mean()
    b = b - b.mean()
    return float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-30))


# ---- delay: the echoes land on the division, to the sample, and fall by the feedback
e = Engine()
pluck(e)
D, _ = e.render(2.0)
e.close()
for div, bpm in [("1/8", 120), ("1/8.", 120), ("1/8T", 120), ("1/4", 90)]:
    e = Engine()
    pluck(e, bpm=bpm)
    delay(e, div, fb=0.5)
    W, _ = e.render(2.0)
    e.close()
    e = Engine()
    pluck(e, bpm=bpm)
    Dd, _ = e.render(2.0)
    e.close()
    E = W - Dd  # the echoes alone
    d = int(round(DDIV_BEATS[DDIVS.index(div)] * 60 / bpm * SR))
    first = E[d:2 * d]
    exact = float(np.max(np.abs(first - Dd[:d])))
    lead = float(np.max(np.abs(E[:d])))
    r2 = rms(E[2 * d:3 * d]) / rms(E[d:2 * d]) if 3 * d <= len(E) else None
    check(exact < 1e-6 and lead < 1e-7 and (r2 is None or abs(r2 - 0.5) < 0.03),
          f"delay {div} at {bpm} BPM: the first echo is the note {d} samples later, sample for sample (max diff {exact:.1e}), "
          f"nothing before it" + (f", the second at {r2:.3f} of it (feedback 0.5)" if r2 is not None else ""))

# the slowest tempo's longest echo: 1/1 at 20 BPM is 12 s, held exactly (the memory reaches it)
runs = []
for mix in (1.0, 0.0):
    e = Engine()
    pluck(e, bpm=20)
    delay(e, "1/1", fb=0.0, mix=mix)
    runs.append(e.render(12.5)[0])
    e.close()
E, Dd = runs[0] - runs[1], runs[1]
d, n = 12 * SR, int(0.3 * SR)
exact = float(np.max(np.abs(E[d:d + n] - Dd[:n])))
lead = float(np.max(np.abs(E[:d])))
check(exact < 1e-6 and lead < 1e-7 and rms(Dd[:n]) > 1e-3,
      f"delay 1/1 at 20 BPM: the echo is the note 12 s later, sample for sample (max diff {exact:.1e}), nothing before it")

# feedback 0.8: each echo 0.8 of the one before
e = Engine()
pluck(e)
delay(e, "1/8", fb=0.8)
W, _ = e.render(2.0)
e.close()
E = W - D
d = 12000
ratios = [rms(E[(k + 1) * d:(k + 2) * d]) / rms(E[k * d:(k + 1) * d]) for k in range(1, 6)]
check(all(abs(r - 0.8) < 0.03 for r in ratios), "feedback 0.8: echo over echo " + " ".join(f"{r:.3f}" for r in ratios))

# damping darkens each repeat
e = Engine()
pluck(e, octave=3)
e.set("shape", 0.667, 0)  # a saw: something to darken
delay(e, "1/8", fb=0.8, damp=0.8)
W, _ = e.render(2.0)
e.close()
hf = [hf_ratio(W[k * d:(k + 1) * d], 2000) for k in range(1, 5)]
check(all(b < a for a, b in zip(hf, hf[1:])) and hf[-1] < 0.5 * hf[0],
      "damping 0.8: each repeat darker (energy above 2 kHz " + " ".join(f"{h:.3f}" for h in hf) + ")")

# ---- ping-pong: left, right, left
e = Engine()
pluck(e)
delay(e, "1/8", fb=0.7, pp=1)
L, R = e.render(2.0)
e.close()
sides = [(rms(L[k * d:(k + 1) * d]), rms(R[k * d:(k + 1) * d])) for k in range(1, 5)]
alt = all((l > 20 * r) if k % 2 == 0 else (r > 20 * l) for k, (l, r) in enumerate(sides))
check(alt, "ping-pong: echoes alternate L R L R (" + ", ".join(f"{l:.3f}/{r:.3f}" for l, r in sides) + ")")
e = Engine()
pluck(e)
e.set("pan", 0.8, 0)
delay(e, "1/8", fb=0.7, pp=0)
L, R = e.render(2.0)
e.close()
check(all(rms(R[k * d:(k + 1) * d]) > 3 * rms(L[k * d:(k + 1) * d]) for k in (1, 2, 3)),
      "ping-pong off: the echoes stay where the channel is panned (right)")

# ---- chorus: wider (the sides decorrelate), in tune
e = tone(shape=0.667)
L0, R0 = e.render(1.5)
e.set("cho_on", 1, 0)
e.set("cho_depth", 0.6, 0)
e.set("cho_rate", 0.4, 0)
e.set("cho_mix", 0.5, 0)
e.warm(0.5)
L1, R1 = e.render(3.0)
e.close()
c0, c1 = corr(L0[24000:], R0[24000:]), corr(L1, R1)
f0, f1 = peak_freq(L0[24000:], 100, 2000), peak_freq(L1 + R1, 100, 2000)
cents = 1200 * np.log2(f1 / f0)
check(c0 > 0.999 and c1 < 0.9, f"chorus: left / right correlation {c0:.3f} -> {c1:.3f} (wider)")
check(abs(cents) < 3 and abs(rms(L1) / rms(L0[24000:]) - 1) < 0.3,
      f"chorus: in tune ({f0:.1f} -> {f1:.1f} Hz, {cents:+.1f} cents), level {rms(L1) / rms(L0[24000:]):.2f}x")


# ---- chorus modes: Ensemble wider than Chorus; every mode in tune on average; Chorus held to 20 cents of swing,
# Vibrato wide; Vibrato is all wet
CHO_MODES = ["Chorus", "Ensemble", "Vibrato"]


def chorused(mode, rate, depth, mix, shape=0.667, seconds=4.0):
    e = tone(shape=shape)
    e.render(0.5)
    e.set("cho_on", 1, 0)
    e.set("cho_mode", CHO_MODES.index(mode), 0)
    e.set("cho_rate", rate, 0)
    e.set("cho_depth", depth, 0)
    e.set("cho_mix", mix, 0)
    e.warm(1.0)
    L, R = e.render(seconds)
    e.close()
    return L, R


def inst_freq(x, sr=SR):
    """The instantaneous frequency (Hz, per sample) of x's analytic signal, and its power (a weight: three
    lines summed nearly cancel at moments, where the phase jumps)."""
    n = len(x)
    h = np.zeros(n)
    h[0] = 1
    h[1:n // 2] = 2
    h[n // 2] = 1
    z = np.fft.ifft(np.fft.fft(x) * h)
    f = np.diff(np.unwrap(np.angle(z))) * sr / (2 * np.pi)
    return f, np.abs(z[1:]) ** 2


def wide(L, R, n=960):
    """The sides' correlation in 20 ms windows: the mean of its size, and the most they ever agree."""
    cs = np.array([corr(L[i:i + n], R[i:i + n]) for i in range(0, len(L) - n, n)])
    return float(np.mean(np.abs(cs))), float(np.max(cs))


# wet only, so the dry sound (the same on both sides) does not hide it; Chorus's two lines meet at every turn of
# the sweep (the sides nearly mono for a moment), Ensemble's never do
e = tone(shape=0.667)
dry = rms(e.render(1.0)[0][24000:])
e.close()
wides, lv = {}, {}
for m in CHO_MODES:
    L, R = chorused(m, 0.4, 0.6, 1.0)
    wides[m], lv[m] = wide(L, R), rms(L) / dry
check(wides["Ensemble"][1] < wides["Chorus"][1] - 0.2 and wides["Ensemble"][0] < wides["Chorus"][0] and wides["Vibrato"][1] > 0.999
      and 0.75 < lv["Ensemble"] < 1.3,
      f"Ensemble keeps the sides apart more than Chorus (rate 0.4, depth 0.6, wet): in 20 ms windows they agree at most "
      f"{wides['Ensemble'][1]:.2f} against {wides['Chorus'][1]:.2f}, mean |correlation| {wides['Ensemble'][0]:.2f} against "
      f"{wides['Chorus'][0]:.2f}; Vibrato moves both together; Ensemble's level {lv['Ensemble']:.2f}x the dry")

# in tune: the power-weighted mean frequency over whole sweeps (Chorus, Ensemble at 0.5 Hz, Vibrato at 4 Hz, full
# depth, wet only), on a sine
HZ = {"Chorus": 0.5, "Ensemble": 0.5, "Vibrato": 4.0}
HZ_RATE = {"Chorus": np.log(0.5 / 0.08) / np.log(40), "Ensemble": np.log(0.5 / 0.08) / np.log(40), "Vibrato": np.log2(4 / 0.05) / 7}
tune = {}
for m in CHO_MODES:
    L, _ = chorused(m, HZ_RATE[m], 1.0, 1.0, shape=0.0, seconds=9.0)
    f, wgt = inst_freq(L.astype(np.float64))
    per = int(SR / HZ[m])
    keep = (len(f) - 6000) // per * per  # the analytic signal's ends are unreliable: whole sweeps from 3000 samples in
    f, wgt = f[3000:3000 + keep], wgt[3000:3000 + keep]
    tune[m] = 1200 * np.log2(np.sum(f * wgt) / np.sum(wgt) / 440.0)
check(all(abs(c) < 1.0 for c in tune.values()),
      "every mode in tune on average: " + ", ".join(f"{m} {c:+.2f}" for m, c in tune.items()) + " cents")


def swing_cents(L, k=96):
    f, _ = inst_freq(L.astype(np.float64))
    f = np.convolve(f, np.ones(k) / k, mode="valid")[3000:-3000]  # 2 ms averaged: the estimate's ripple
    return float(np.max(np.abs(1200 * np.log2(f / 440.0))))


sw = {(m, r): swing_cents(chorused(m, r, 1.0, 1.0, shape=0.0, seconds=6.0)[0]) for m, r in
      [("Chorus", 1.0), ("Chorus", 0.6), ("Chorus", 0.3), ("Vibrato", 1.0)]}
check(max(sw[("Chorus", r)] for r in (1.0, 0.6, 0.3)) < 20.5 and sw[("Vibrato", 1.0)] > 250,
      f"Chorus at full depth swings the pitch at most 20 cents: {sw[('Chorus', 1.0)]:.1f} / {sw[('Chorus', 0.6)]:.1f} / "
      f"{sw[('Chorus', 0.3)]:.1f} cents at 3.2 / 0.73 / 0.24 Hz (was +-2.6 semitones at the top); "
      f"Vibrato keeps the wide range: {sw[('Vibrato', 1.0)]:.0f} cents at 6.4 Hz")

# Vibrato is the wet sound alone: at depth 0 it is the dry sound 1 ms (48 samples) later, whatever MIX says
e = tone(shape=0.667)
D, _ = e.render(1.0)
e.close()
e = tone(shape=0.667)
e.set("cho_on", 1, 0)
e.set("cho_mode", CHO_MODES.index("Vibrato"), 0)
e.set("cho_depth", 0.0, 0)
e.set("cho_mix", 0.3, 0)
V, _ = e.render(1.0)
e.close()
err = float(np.max(np.abs(V[4800:] - D[4800 - 48:-48])))
check(err < 1e-6 and rms(V[4800:]) > 0.05, f"Vibrato has no dry sound: at depth 0, mix 0.3 it is the dry sound 48 samples "
      f"later (largest difference {err:.1e})")

# ---- drive: harmonics, the level kept
e = tone(shape=0.0)
L0, _ = e.render(1.0)
f0 = peak_freq(L0, 100, 2000)
h0 = band_level(L0, 3 * f0) / band_level(L0, f0)
levels = []
h = []
for amt in (0.3, 0.6, 1.0):
    e.set("fx_drive", amt, 0)
    e.warm(0.2)
    L1, _ = e.render(1.0)
    h.append(band_level(L1, 3 * f0) / band_level(L1, f0))
    levels.append(rms(L1) / rms(L0))
e.close()
check(h0 < 0.01 and h[0] > 0.02 and h[0] < h[1] < h[2],
      f"drive: the 3rd harmonic rises {h0:.4f} -> " + " / ".join(f"{x:.3f}" for x in h) + " (drive 0.3 / 0.6 / 1)")
check(all(0.7 < x < 1.45 for x in levels), "drive: level kept within 3 dB (" + " / ".join(f"{x:.2f}x" for x in levels) + ")")
e = tone(shape=0.0)
e.set("fx_drive", 0.6, 0)
e.set("fx_tone", -1.0, 0)
e.warm(0.3)
dark, _ = e.render(1.0)
e.set("fx_tone", 1.0, 0)
e.warm(0.3)
bright, _ = e.render(1.0)
e.close()
check(hf_ratio(bright, 1500) > 3 * hf_ratio(dark, 1500),
      f"drive tone: bright {hf_ratio(bright, 1500):.4f} vs dark {hf_ratio(dark, 1500):.4f} of the energy above 1.5 kHz")

# ---- reverb: the tail's decay time follows DECAY
def t60_of(x, sr=SR):
    """-60 dB time from the slope of the tail's envelope between 5 and 35 dB below its peak (a line fit in dB)."""
    env, dt = env_frames(x, 10)
    db = 20 * np.log10(env + 1e-12)
    pk = int(np.argmax(db))
    seg = db[pk:]
    i0 = pk + int(np.argmax(seg < db[pk] - 5))
    i1 = pk + int(np.argmax(seg < db[pk] - 35))
    if i1 <= i0 + 3:
        return float("nan")
    k = np.arange(i0, i1)
    slope = np.polyfit(k * dt, db[i0:i1], 1)[0]
    return -60.0 / slope


def wet(decay=0.45, size=0.5, damp=0.0, width=1.0, seconds=4.0):
    """The reverb alone of one pluck (a 30 ms note at sample 0, the next 6 s later): send 1, the dry level 0."""
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=1, bpm=40, dec=DEC_30MS, shape=0.667, cutoff=1.0)
    e.set("volume", 0.8)
    e.set("rev_send", 1.0, 0)
    e.set("rev_decay", decay)
    e.set("rev_size", size)
    e.set("rev_damp", damp)
    e.set("rev_width", width)
    e.set("rev_pre", 0.0)
    e.set("rev_return", 1.0)
    L, R = e.render(seconds)
    D = Engine()
    D.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=1, bpm=40, dec=DEC_30MS, shape=0.667, cutoff=1.0)
    D.set("volume", 0.8)
    Ld, Rd = D.render(seconds)
    e.close()
    D.close()
    return L - Ld, R - Rd


want = {0.3: 0.2 * 100 ** 0.3, 0.5: 0.2 * 100 ** 0.5, 0.7: 0.2 * 100 ** 0.7}
got = {}
for dec, t in want.items():
    WL, WR = wet(decay=dec, seconds=min(8.0, 0.5 + 0.8 * t))
    got[dec] = t60_of(WL + WR)
check(all(abs(got[d] / want[d] - 1) < 0.25 for d in want),
      "reverb decay: T60 " + ", ".join(f"{got[d]:.2f} s (want {want[d]:.2f})" for d in want))
WL, WR = wet(decay=0.5, damp=0.8, seconds=2.0)
WLb, _ = wet(decay=0.5, damp=0.0, seconds=2.0)
check(hf_ratio(WL[SR // 2:], 3000) < 0.5 * hf_ratio(WLb[SR // 2:], 3000),
      f"reverb damping: the tail darker ({hf_ratio(WL[SR // 2:], 3000):.4f} vs {hf_ratio(WLb[SR // 2:], 3000):.4f} above 3 kHz)")
c1 = corr(WLb[SR // 4:], _[SR // 4:])
WL0, WR0 = wet(decay=0.5, width=0.0, seconds=2.0)
check(c1 < 0.5 and np.max(np.abs(WL0 - WR0)) < 1e-6,
      f"reverb width: left / right correlation {c1:.2f} at width 1, identical at width 0")
WL, WR = wet(decay=0.5, seconds=1.0)
WLs, WRs = wet(decay=0.5, size=1.0, seconds=1.0)
first = lambda x: np.argmax(np.abs(x) > 0.05 * np.max(np.abs(x))) / SR  # noqa: E731
check(np.all(np.isfinite(WL)) and np.max(np.abs(WL)) < 1.0 and rms(WL) > 1e-3,
      f"reverb: finite, below full scale (peak {np.max(np.abs(WL)):.3f}), the tail there (rms {rms(WL):.3f}); size 1 vs 0.5: "
      f"rms {rms(WLs):.3f} vs {rms(WL):.3f}")

# ---- duck: a kick (its level at 0, so only its envelope is heard of it) ducks a held pad on every hit
def ducked(src, amount=0.8, seconds=2.0):
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=1, bpm=120, dec=0.5, sus=1.0, gate=1.0, shape=0.3)
    e.set("volume", 0.8)
    e.set("gen", 4, 1)  # slot 2: Rhythm on the kick, every beat
    e.set("inst", 1, 1)
    e.set("drum_model", 0, 1)
    e.set("drum_decay", 0.2, 1)  # a short kick: the duck follows its envelope
    e.set("arp_div", DIVS.index("1/4"), 1)
    e.set("arp_len", 4, 1)
    e.set("arp_density", 1.0, 1)
    e.set("level", 0.0, 1)
    e.set("duck_src", src, 0)
    e.set("duck_amt", amount, 0)
    e.warm(0.25)
    L, _, fr = e.render(seconds, collect=True)
    e.close()
    return L, fr


L0, _ = ducked(0)
L1, fr = ducked(2)
env0, dt = env_frames(L0, 5)
env1, _ = env_frames(L1, 5)
beats = [0.25 + 0.5 * k for k in range(1, 3)]  # the kick's hits after the warm-up (a beat every 0.5 s)
dips = []
for b in beats:
    i = int(round(b / dt))
    before, low, after = env1[i - 3], env1[i:i + 12].min(), env1[i + 80]
    dips.append((low / before, after / env0[i + 80]))
check(np.std(env0[10:]) / np.mean(env0[10:]) < 0.05 and all(lo < 0.4 and af > 0.85 for lo, af in dips),
      "duck 0.8: the pad dips on every kick (" + ", ".join(f"to {lo:.2f}, back to {af:.2f} 400 ms on" for lo, af in dips) + "), flat without")
check(max(f.ch[0].duck for f in fr) > 0.6 and max(f.ch[1].duck for f in fr) == 0,
      f"telemetry: the pad's duck reaches {max(f.ch[0].duck for f in fr):.2f}, the kick's 0")
L2, _ = ducked(1)
check(np.max(np.abs(L2 - L0)) < 1e-6, "a slot set to duck from itself is not ducked")

# ---- the master bus: the limiter holds its ceiling on a hot patch (sample and inter-sample peaks)
def upsampled_peak(x, k=4):
    sp = np.fft.rfft(x.astype(np.float64))
    y = np.fft.irfft(sp, len(x) * k) * k
    return float(np.max(np.abs(y[k * 2000:-k * 2000])))  # not the ends: the FFT wraps the clip round on itself


def hot(ceiling, volume=1.0, glue=0.3):
    e = Engine()
    for s in range(SLOTS):
        e.set("gen", 1, s)
        e.set("level", 1.0, s)
        e.set("amp_s", 1.0, s)
        e.set("arp_gate", 1.0, s)
        e.set("shape", 1.0, s)
        e.set("fx_drive", 0.5, s)
    e.set("ceiling", ceiling)
    e.set("volume", volume)
    e.set("glue", glue)
    e.warm(0.5)
    L, R, fr = e.render(3.0, collect=True)
    e.close()
    return L, R, fr


for ceil in (-0.3, -3.0, -9.0):
    L, R, fr = hot(ceil)
    c = 10 ** (ceil / 20)
    sp_ = max(np.max(np.abs(L)), np.max(np.abs(R)))
    tp = max(upsampled_peak(L), upsampled_peak(R))
    gr = max(f.limitGr for f in fr)
    check(sp_ <= c + 1e-6 and 20 * np.log10(tp / c) < 0.5 and gr > 3,
          f"limiter at {ceil:+.1f} dB: sample peak {20 * np.log10(sp_):+.2f} dB, inter-sample {20 * np.log10(tp):+.2f} dB "
          f"(4x), {gr:.1f} dB of reduction")
L, R, _ = hot(-3.0, volume=0.5)
check(np.max(np.abs(L)) <= 0.5 * 10 ** (-3 / 20) + 1e-6, "the volume comes after the limiter: at volume 0.5 the peak is half the ceiling")
e = Engine()
check(e.latency() == round(0.0015 * SR) + 5, f"the limiter's look-ahead: {e.latency()} samples of latency at {SR} Hz (reported to the host)")
e.close()

# glue: evens out (a loud chord loses more than a quiet one gains back), its make-up lifts the quiet one
def glue_level(level, amount):
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Triad", rng=1, octave=1, bpm=60, dec=0.5, sus=1.0, gate=1.0, shape=0.667, level=level)
    e.set("glue", amount)
    e.warm(1.0)
    L, R, fr = e.render(2.0, collect=True)
    e.close()
    return 20 * np.log10(rms(L) + 1e-12), max(f.glueGr for f in fr)


q0, _ = glue_level(0.25, 0.0)
q1, qgr = glue_level(0.25, 0.6)
l0, _ = glue_level(1.0, 0.0)
l1, lgr = glue_level(1.0, 0.6)
check(qgr < 0.5 and 4.0 < q1 - q0 < 6.0 and lgr > 1.0 and (l1 - l0) < (q1 - q0) - 1.0,
      f"glue 0.6: a quiet note {q1 - q0:+.1f} dB (make-up, {qgr:.1f} dB reduction), a loud one {l1 - l0:+.1f} dB "
      f"({lgr:.1f} dB reduction): the gap closes by {(q1 - q0) - (l1 - l0):.1f} dB")

# ---- silence: the tails ring out, then nothing (no NaN, no denormals left)
e = Engine()
for s in range(3):
    e.set("fx_drive", 0.5, s)
    e.set("cho_on", 1, s)
    e.set("rev_send", 0.5, s)
    delay(e, "1/4", fb=0.5, damp=0.3, pp=1, mix=0.7, slot=s)
e.render(2.0)
e.all_off()
L, R, fr = e.render(20.0, collect=True)
tail = np.flatnonzero(np.abs(L) + np.abs(R) > 0)
end = (tail[-1] + 1) / SR if len(tail) else 0
check(np.all(np.isfinite(L)) and np.all(np.isfinite(R)) and 0.5 < end < 19.0 and np.all(L[-SR:] == 0) and rms(L[:SR // 2]) > 1e-3,
      f"after the notes stop the echoes ring on, then silence: last sample at {end:.1f} s, exact zeros after, all finite")
check(all(c.rms == 0 for c in fr[-1].ch), "the rung-out channels are skipped (channel rms 0)")
t0 = time.perf_counter()
e.render(10.0)
t_quiet = time.perf_counter() - t0
e.close()
check(t_quiet < 0.2, f"10 s of silence after the tails: {t_quiet:.3f} s to render (no denormal slowdown)")


# ---- the same audio for any block size, every effect on
def everything(e):
    for s in range(3):
        e.set("fx_drive", 0.4 + 0.2 * s, s)
        e.set("fx_tone", 0.5 - 0.4 * s, s)
        e.set("cho_on", 1, s)
        e.set("cho_rate", 0.3 + 0.2 * s, s)
        e.set("cho_mode", s, s)  # Chorus, Ensemble, Vibrato
        delay(e, ["1/8.", "1/4T", "1/16"][s], fb=0.6, damp=0.4, pp=s % 2, mix=0.5, slot=s)
        e.set("rev_send", 0.3 + 0.2 * s, s)
    e.set("duck_src", 1, 1)  # slot 1 ducks slot 2
    e.set("rev_size", 0.7)
    e.set("rev_pre", 0.2)


outs = []
for blk in (37, 512):
    e = Engine(max_block=blk)
    everything(e)
    L, R = e.render(4.0, block=blk)
    outs.append((L, R))
    e.close()
check(np.array_equal(outs[0][0], outs[1][0]) and np.array_equal(outs[0][1], outs[1][1]),
      "every effect on: identical audio at 37 and 512 samples per block")

# ---- CPU: 16 channels, every effect on
e = Engine()
for s in range(SLOTS):
    e.set("gen", 1, s)
    e.set("fx_drive", 0.5, s)
    e.set("cho_on", 1, s)
    delay(e, "1/8.", fb=0.6, damp=0.4, pp=s % 2, mix=0.4, slot=s)
    e.set("rev_send", 0.3, s)
    e.set("duck_src", 1 if s else 0, s)
e.render(0.5)
t0 = time.perf_counter()
e.render(10.0)
dt = time.perf_counter() - t0
e.close()
check(dt < 3.0, f"CPU 16 channels, every effect on: 10 s of audio in {dt:.2f} s ({100 * dt / 10:.1f} % of real time)")

done()
