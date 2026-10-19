"""The rhythm generator and the drum voice (phase 4), measured from the
audio: Euclidean hits and ROTATE, CHANCE, rolls; each drum model's
character (a kick low and swept down, a snare's noise, hats high and the
open one longer, a clap's three bursts, a tom above the kick, a short rim),
TUNE and DECAY, a tom following the chord root; identical audio for any
block size; every model finite and below full scale."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")
GENS = ["Off", "Arp", "Chord", "Bass", "Rhythm"]
MODELS = ["Kick", "Snare", "Hat", "Open hat", "Clap", "Tom", "Rim", "Noise"]


def solo_drum(e, model="Kick", div="1/8", length=8, hits=None, rotate=0, chance=1.0, tune=0.0, decay=0.5, tone=0.5,
              snap=0.5, pitch=0, ratchet=0.0, vary=0.0):
    e.solo_arp(0, "Up", div, length=length)
    e.set("gen", GENS.index("Rhythm"), 0)
    e.set("inst", 1, 0)
    e.set("arp_density", (hits if hits is not None else length) / length, 0)
    e.set("arp_ratchet", ratchet, 0)
    e.set("arp_vary", vary, 0)
    e.set("oct", 0, 0)
    e.set("rhy_rotate", rotate, 0)
    e.set("rhy_chance", chance, 0)
    e.set("rhy_pitch", pitch, 0)
    e.set("drum_model", MODELS.index(model), 0)
    e.set("drum_tune", tune, 0)
    e.set("drum_decay", decay, 0)
    e.set("drum_tone", tone, 0)
    e.set("drum_snap", snap, 0)


def hit_steps(e, steps, step=0.25):
    """Which steps (at 1/8, 120 BPM: 0.25 s) started a hit, from the telemetry."""
    _, _, fr = e.render(steps * step, block=64, collect=True)
    out = set()
    for i, f in enumerate(fr):
        if f.ch[0].hit:
            out.add(int(round(i * 64 / SR / step)))
    return sorted(out)


def one_hit(model, seconds=1.0, **kw):
    """One hit, alone: a whole bar per step so nothing else follows it."""
    e = Engine()
    solo_drum(e, model, div="1/1", length=1, **kw)
    L, _ = e.render(seconds)
    e.close()
    return L


# ---- Euclid, rotate, chance
e = Engine()
solo_drum(e, length=8, hits=3)
got = hit_steps(e, 8)
check(got == [0, 3, 6], f"3 hits in 8: steps {got} (want 0 3 6)")
e.close()
e = Engine()
solo_drum(e, length=8, hits=3, rotate=1)
got = hit_steps(e, 8)
check(got == [1, 4, 7], f"rotated by 1: steps {got} (want 1 4 7)")
e.close()
e = Engine()
solo_drum(e, length=4, hits=4, chance=0.5)
got = hit_steps(e, 64)
check(20 <= len(got) <= 44, f"chance 50 %: {len(got)} of 64 steps play")
e.close()
e = Engine()
solo_drum(e, "Rim", div="1/4", length=1, ratchet=1.0)
L, _ = e.render(0.5)
on = onsets(L, rel=0.3, gap=0.02)
check(2 <= len(on) <= 4, f"ratchet: a roll of {len(on)} hits in one step")
e.close()

# ---- the models' character
kick = one_hit("Kick")
early, late = peak_freq(kick[int(0.002 * SR):int(0.02 * SR)], 20, 2000), peak_freq(kick[int(0.1 * SR):int(0.3 * SR)], 20, 2000)
check(45 < late < 55 and early > 1.5 * late, f"kick: swept down from {early:.0f} Hz to {late:.0f} Hz")
check(hf_ratio(kick, 2000) < 0.05, f"kick: little above 2 kHz ({hf_ratio(kick, 2000):.3f})")

tom = one_hit("Tom")
tf_ = peak_freq(tom[int(0.1 * SR):int(0.25 * SR)], 20, 2000)
check(tf_ > 1.6 * late, f"tom ({tf_:.0f} Hz) sits above the kick ({late:.0f} Hz)")

snare = one_hit("Snare")
check(hf_ratio(snare, 2000) > 0.2, f"snare: its noise above 2 kHz ({hf_ratio(snare, 2000):.2f} of the energy)")

hat, ohat = one_hit("Hat"), one_hit("Open hat")
check(hf_ratio(hat, 4000) > 0.7, f"hat: mostly above 4 kHz ({hf_ratio(hat, 4000):.2f})")


def ring_time(x, floor=0.05):
    env, dt = env_frames(x, 2)
    pk = int(np.argmax(env))
    below = np.flatnonzero(env[pk:] < floor * env[pk])
    return (below[0] if len(below) else len(env) - pk) * dt


check(ring_time(ohat) > 3 * ring_time(hat), f"open hat rings {ring_time(ohat) * 1000:.0f} ms, closed {ring_time(hat) * 1000:.0f} ms")

clap = one_hit("Clap")
env, dt = env_frames(clap[:int(0.05 * SR)], 1)
bursts = sum(1 for i in range(1, len(env)) if env[i] > 1.6 * env[i - 1] and env[i] > 0.2 * env.max())
check(bursts >= 2, f"clap: bursts after the first ({bursts} jumps in the first 50 ms)")

rim = one_hit("Rim")
check(ring_time(rim) < 0.08, f"rim: short ({ring_time(rim) * 1000:.0f} ms)")

# ---- TUNE and DECAY
up = one_hit("Kick", tune=12.0)
f_up = peak_freq(up[int(0.1 * SR):int(0.3 * SR)], 20, 2000)
check(abs(f_up / late - 2.0) < 0.1, f"kick tune +12: {late:.1f} -> {f_up:.1f} Hz (x{f_up / late:.2f})")
short, long_ = one_hit("Kick", decay=0.1, seconds=2.0), one_hit("Kick", decay=0.9, seconds=2.0)
check(ring_time(long_) > 2.5 * ring_time(short), f"decay: {ring_time(short) * 1000:.0f} ms -> {ring_time(long_) * 1000:.0f} ms")

# ---- a tom on the chord root through a progression (Pop in A minor: A, E, F, D)
e = Engine()
solo_drum(e, "Tom", div="1/1", length=1, pitch=1, snap=0.0, decay=0.8)
e.set("root", 9)
e.set("scale", SCALES.index("Minor"))
e.set("prog", PROGS.index("Pop"))
L, _ = e.render(8.0)
pcs = []
for bar in range(4):
    seg = L[int((bar * 2 + 0.3) * SR):int((bar * 2 + 1.0) * SR)]
    f = peak_freq(seg, 40, 1000)
    pcs.append(int(round(hz_midi(f))) % 12)
check(pcs == [9, 4, 5, 2], f"tom follows the root: pitch classes {pcs} (want A E F D = 9 4 5 2)")
e.close()

# ---- the same audio for any block size; every model finite and in range
outs = []
for blk in (37, 512):
    e = Engine(max_block=blk)
    solo_drum(e, "Snare", div="1/16", length=7, hits=4, rotate=2, chance=0.7, ratchet=0.3, vary=0.5)
    L, _ = e.render(4.0, block=blk)
    outs.append(L)
    e.close()
check(np.array_equal(outs[0], outs[1]), "rhythm + drum: identical audio at 37 and 512 samples per block")
peaks = {m: float(np.max(np.abs(one_hit(m)))) for m in MODELS}
check(all(np.isfinite(p) and 0.02 < p < 1.0 for p in peaks.values()), f"every model plays, below full scale: "
      + ", ".join(f"{m} {p:.2f}" for m, p in peaks.items()))

done()
