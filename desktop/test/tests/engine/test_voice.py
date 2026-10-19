"""The synth voice, measured from the audio: shapes by spectrum, detune,
sub and noise, the filter (cutoff, resonance, type), envelopes (attack,
decay, sustain, release), pitch envelope, glide, drive, level and pan,
silence after notes stop, every sample rate.

Timing rule: a fresh Engine starts at beat 0, and the first arp step lands
on sample 0 of the first render. Envelope and pitch measurements use fresh
engines so the note start is known exactly."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")


def tone(**kw):
    """A held A4 (440 Hz): Loop at 1/1 at 60 BPM (4 s steps), gate 1, sustain 1."""
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=2, bpm=60, dec=0.5, sus=1.0, gate=1.0, att=0.0, **kw)
    return e


def held(e, seconds=1.0, warm=0.3):
    e.warm(warm)
    return e.render(seconds)


def harmonics(x, f0, ks):
    base = band_level(x, f0)
    return [band_level(x, k * f0) / base for k in ks]


# ---- shapes: harmonic content at 440 Hz (a 4 s step: parameter changes apply to the sounding note)
e = tone(shape=0.0)
L, _ = held(e)
f0 = peak_freq(L, 100, 2000)
h2, h3 = harmonics(L, 440, (2, 3))
check(abs(f0 - 440) < 1 and h2 < 0.02 and h3 < 0.02, f"shape 0 = sine at {f0:.1f} Hz: 2nd {h2:.3f}, 3rd {h3:.3f} of the fundamental")
e.set("shape", 1 / 3, 0)
L, _ = held(e)
h2, h3, h5 = harmonics(L, 440, (2, 3, 5))
check(h2 < 0.05 and 0.07 < h3 < 0.16 and 0.02 < h5 < 0.07, f"shape 1/3 = triangle: 2nd {h2:.3f}, 3rd {h3:.3f} (1/9), 5th {h5:.3f} (1/25)")
e.set("shape", 2 / 3, 0)
L, _ = held(e)
h2, h3 = harmonics(L, 440, (2, 3))
check(0.4 < h2 < 0.6 and 0.26 < h3 < 0.4, f"shape 2/3 = saw: 2nd {h2:.3f} (1/2), 3rd {h3:.3f} (1/3)")
e.set("shape", 1.0, 0)
L, _ = held(e)
h2, h3 = harmonics(L, 440, (2, 3))
check(h2 < 0.05 and 0.26 < h3 < 0.4, f"shape 1 = square: 2nd {h2:.3f}, 3rd {h3:.3f} (1/3)")
e.close()

# aliasing: a saw at 1760 Hz (slot octave 3 + master octave 1) should put little energy between its harmonics
e = tone(shape=2 / 3)
e.set("oct", 3, 0)
e.set("octave", 1)
e.render(4.0)  # the next step takes the new octave
L, _ = e.render(1.0)
f0 = 1760.0
fr, sp = spectrum(L)
harm = np.zeros_like(fr, dtype=bool)
for k in range(1, 14):
    harm |= np.abs(fr - k * f0) < 40
between = float(np.max(sp[(~harm) & (fr > 200) & (fr < 20000)]))
ratio = between / band_level(L, f0)
check(abs(peak_freq(L, 1000, 3000) - f0) < 2 and ratio < 0.01,
      f"saw at {f0:.0f} Hz: the worst alias between harmonics is {20 * np.log10(ratio + 1e-12):.0f} dB below the fundamental")
e.close()

# ---- detune: two peaks, beating
e = tone(shape=0.0, detune=20)
L, _ = held(e, 2.0)
fr, sp = spectrum(L)
m = (fr > 430) & (fr < 460)
peaks = fr[m][np.argsort(sp[m])[-2:]]
check(abs(peaks.max() - peaks.min() - 440 * (2 ** (20 / 1200) - 1)) < 1.5,
      f"detune 20 cents: peaks {peaks.min():.1f} and {peaks.max():.1f} Hz ({peaks.max() - peaks.min():.1f} Hz apart, want 5.1)")
e.close()

# ---- sub and noise
e = tone(shape=0.0)
e.set("sub", 1.0, 0)
L, _ = held(e)
r = band_level(L, 220) / band_level(L, 440)
check(0.8 < r < 1.3, f"sub 1: an octave below at {r:.2f}x the fundamental (as loud)")
e.set("sub", 0.0, 0)
e.set("noise", 1.0, 0)
L, _ = held(e)
check(hf_ratio(L, 5000) > 0.2, f"noise 1: {100 * hf_ratio(L, 5000):.0f} % of the energy above 5 kHz")
e.close()

# ---- filter: cutoff, resonance, type on a saw
e = tone(shape=2 / 3, cutoff=1.0)
L0, _ = held(e)
e.set("cutoff", 0.5, 0)  # 20 * 1000^0.5 = 632 Hz
L1, _ = held(e)
r0, r1 = hf_ratio(L0, 2000), hf_ratio(L1, 2000)
check(r1 < 0.3 * r0, f"cutoff 0.5 (632 Hz) on a saw: energy above 2 kHz {100 * r0:.1f} % -> {100 * r1:.2f} %")
e.set("reso", 0.9, 0)
L2, _ = held(e)
check(band_level(L2, 660, 0.08) > 1.8 * band_level(L1, 660, 0.08),
      f"resonance 0.9: the cutoff region {band_level(L2, 660, 0.08) / band_level(L1, 660, 0.08):.1f}x louder")
e.set("reso", 0.0, 0)
e.set("filter_type", 1.0, 0)  # high-pass: a 2-pole at 632 Hz leaves 0.33 of a 440 Hz fundamental
L3, _ = held(e)
check(band_level(L3, 440) < 0.45 * band_level(L0, 440) and hf_ratio(L3, 2000) > 0.3,
      f"filter type 1 (high-pass at 632 Hz): the fundamental {band_level(L3, 440) / band_level(L0, 440):.2f}x (0.33), HF share {100 * hf_ratio(L3, 2000):.0f} %")
e.close()


def envelope_of(e, seconds, frame_ms=5):
    L, _ = e.render(seconds)
    env, dt = env_frames(L, frame_ms)
    return env, dt


# ---- attack 200 ms (fresh engine: the note starts at t = 0)
e = Engine()
e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=2, bpm=60, shape=0.0, gate=1.0, sus=1.0, dec=0.5)
e.set("amp_a", np.log2(200) / 12, 0)  # 1 ms * 2^(12 v) = 200 ms
env, dt = envelope_of(e, 1.0)
i90 = int(np.argmax(env > 0.9 * env.max()))
att = i90 * dt
check(0.15 < att < 0.26, f"attack 200 ms: 0 -> 90 % in {1000 * att:.0f} ms")
e.close()

# ---- release 300 ms: gate 0.5 of a 1 s step (1/2 at 60 BPM... use 1/4 at 60 = 1 s steps)
e = Engine()
e.solo_arp(0, "Loop", "1/4", length=16, pool="Root", octave=2, bpm=60, shape=0.0, gate=0.5, sus=1.0, att=0.0, dec=0.5)
e.set("amp_r", np.log2(60) / 9.64, 0)  # 5 ms * 2^(9.64 v) = 300 ms
env, dt = envelope_of(e, 1.0)
rel_from = int(0.5 / dt)  # the gate closes at 0.5 s
rel_to = int(np.argmax(env[rel_from:] < 0.05 * env.max())) + rel_from
rel = (rel_to - rel_from) * dt
check(0.2 < rel < 0.4, f"release 300 ms: the gate closes at 500 ms, 5 % reached {1000 * rel:.0f} ms later (99 % in 300)")
e.close()

# ---- decay 200 ms to sustain 0.25: compared with the same note at sustain 1 (the full level)
levels = {}
for sus in (0.25, 1.0):
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=2, bpm=60, shape=0.0, gate=1.0, sus=sus, att=0.0,
               dec=np.log2(40) / 9.64)  # 200 ms
    env, dt = envelope_of(e, 1.0)
    levels[sus] = (env[int(0.6 / dt):int(0.95 / dt)].mean(), env[int(0.1 / dt):int(0.15 / dt)].mean())
    e.close()
ratio = levels[0.25][0] / levels[1.0][0]
mid = levels[0.25][1] / levels[1.0][1]  # 100-150 ms into a 200 ms decay: about 0.25 + 0.75 * e^-2.9 = 0.29
check(0.23 < ratio < 0.27 and 0.26 < mid < 0.36,
      f"decay 200 ms to sustain 0.25: holds at {ratio:.3f} of the full level, {mid:.2f} at 100-150 ms")

# ---- pitch envelope: +12 semitones falling over 200 ms
e = Engine()
e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=2, bpm=60, shape=0.0, gate=1.0, sus=1.0, att=0.0, dec=0.5)
e.set("pitch_amt", 1.0, 0)
e.set("pitch_d", np.log2(40) / 7.64, 0)  # 5 ms * 2^(7.64 v) = 200 ms
L, _ = e.render(1.0)
f_start = peak_freq(L[int(0.01 * SR):int(0.05 * SR)], 300, 1200)
f_end = peak_freq(L[int(0.7 * SR):], 300, 1200)
check(f_start > 600 and abs(f_end - 440) < 2, f"pitch env +12 st, 200 ms: {f_start:.0f} Hz at 10-50 ms, {f_end:.1f} Hz at the end")
e.close()

# ---- glide 0.5 s between A4 and A5 (Up over Root, range 2, 1 s steps at 120 BPM / 1/2)
e = Engine()
e.solo_arp(0, "Up", "1/2", length=16, pool="Root", rng=2, octave=2, bpm=120, shape=0.0, gate=1.0, sus=1.0, att=0.0, dec=0.5)
e.set("glide", np.sqrt(0.5), 0)  # glide v^2 = 0.5 s
L, _ = e.render(2.0)
f_a = peak_freq(L[int(0.8 * SR):int(0.95 * SR)], 300, 1200)      # the first note, settled
f_mid = peak_freq(L[int(1.08 * SR):int(1.16 * SR)], 300, 1200)   # 80-160 ms into the glide
f_b = peak_freq(L[int(1.85 * SR):int(1.98 * SR)], 300, 1200)     # the octave, reached
check(abs(f_a - 440) < 2 and 500 < f_mid < 800 and f_b > 860,
      f"glide 0.5 s to the octave: {f_a:.0f} Hz before, {f_mid:.0f} Hz mid-glide, {f_b:.0f} Hz after")
e.close()

# ---- drive, level, pan
e = tone(shape=0.0)
L0, _ = held(e)
e.set("drive", 1.0, 0)
L1, _ = held(e)
check(band_level(L1, 1320) / band_level(L1, 440) > 10 * (band_level(L0, 1320) / band_level(L0, 440) + 1e-6),
      f"drive 1 on a sine: 3rd harmonic {band_level(L1, 1320) / band_level(L1, 440):.3f} of the fundamental (clean {band_level(L0, 1320) / band_level(L0, 440):.4f})")
e.set("drive", 0.0, 0)
e.set("level", 0.5, 0)
L2, _ = held(e)
check(0.2 < rms(L2) / rms(L0) < 0.3, f"level 0.5: {rms(L2) / rms(L0):.2f}x (audio taper, want 0.25)")
e.set("level", 1.0, 0)
e.set("pan", 1.0, 0)
L3, R3 = held(e)
check(rms(L3) < 0.01 * rms(R3), f"pan hard right: L {rms(L3):.4f}, R {rms(R3):.3f}")
e.set("pan", 0.0, 0)
L4, R4 = held(e)
check(abs(rms(L4) - rms(R4)) < 1e-3 and abs(rms(L4) / rms(R3) - 0.707) < 0.03,
      f"pan centre: L = R = {rms(L4) / rms(R3):.3f} of hard (constant power)")
e.close()

# ---- silence after the notes stop; finite at every rate
e = Engine()
e.solo_arp(0, "Loop", "1/8", length=16, pool="Root", octave=2, dec=0.3, sus=0.0)
e.render(1.0)
e.set("gen", 0, 0)
L, _ = e.render(1.0)
check(rms(L[int(0.5 * SR):]) < 1e-6 and np.all(L[int(0.9 * SR):] == 0), "generator off: digital silence within half a second")
e.close()
for sr in (44100, 96000, 192000):
    e = Engine(sr=sr)
    e.solo_arp(0, "Up", "1/8", pool="Key", rng=2, octave=2, shape=0.6, dec=0.4)
    e.set("sub", 0.5, 0)
    e.set("noise", 0.2, 0)
    e.set("drive", 0.5, 0)
    e.set("reso", 0.8, 0)
    L, R = e.render(2.0)
    f0 = peak_freq(L[int(0.03 * sr):int(0.23 * sr)], 100, 2000, sr)
    check(np.all(np.isfinite(L)) and np.all(np.isfinite(R)) and rms(L) > 0.01 and np.max(np.abs(L)) <= 1.0,
          f"{sr / 1000:g} kHz: finite, plays (rms {rms(L):.3f}, peak {np.max(np.abs(L)):.3f}), first note {f0:.0f} Hz")
    e.close()
done()
