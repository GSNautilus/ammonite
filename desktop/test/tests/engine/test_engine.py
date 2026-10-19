"""The whole engine: the default patch plays, 16 channels at once, four
instances side by side bit-identical to one alone, telemetry consistent
with the audio, the master limiter, CPU time per block, block sizes."""
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")

# ---- the default patch plays something finished at once
e = Engine()
L, R, fr = e.render(4.0, collect=True)
check(np.all(np.isfinite(L)) and rms(L) > 0.02 and rms(R) > 0.02 and np.max(np.abs(L)) < 1.0,
      f"default patch: plays (rms L {rms(L):.3f} R {rms(R):.3f}, peak {np.max(np.abs(L)):.3f})")
hits = [sum(1 for f in fr if f.ch[s].hit) for s in range(3)]
# 4 s at 110 BPM = 7.3 beats: 1/4 -> ~7, 1/8 -> ~14, 1/16 at density 0.6 -> ~17
check(6 <= hits[0] <= 8 and 13 <= hits[1] <= 16 and 14 <= hits[2] <= 20,
      f"note hits in 4 s per slot: {hits} (three arps at 1/4, 1/8 and 1/16 with density 0.6)")
act = [c.active for c in fr[-1].ch]
check(act[:3] == [1, 1, 1] and sum(act) == 3, f"telemetry: slots 1-3 active, {sum(act)} active in all")
check(all(fr[-1].ch[s].rms > 0 for s in range(3)) and fr[-1].ch[3].rms == 0 and fr[-1].masterRms > 0,
      "telemetry: the three active channels and the master have signal, slot 4 none")
pcs = set()
for f in fr:
    for s in range(3):
        if f.ch[s].hit:
            pcs.add(f.ch[s].pitchClass)
check(pcs and pcs <= {9, 11, 0, 2, 4, 5, 7}, f"telemetry: notes hit in A minor's pitch classes: {sorted(pcs)}")
check(fr[-1].keyMask == 0b101010110101 and bin(fr[-1].chordMask).count("1") == 3,  # A B C D E F G = bits 9 11 0 2 4 5 7
      f"telemetry: key mask {fr[-1].keyMask:012b} (A minor), chord mask {bin(fr[-1].chordMask).count('1')} notes")
# the wave window: one period, so its first and last samples are close (phase-aligned)
w = np.array(fr[-1].ch[0].wave)
check(np.max(np.abs(w)) > 0.001 and abs(w[0] - w[-1]) < 0.3 * np.max(np.abs(w)),
      f"telemetry: slot 1 wave window has signal (peak {np.max(np.abs(w)):.3f}) and tiles ({abs(w[0] - w[-1]) / np.max(np.abs(w)):.2f})")
e.close()

# ---- 16 channels at once
e = Engine()
for s in range(SLOTS):
    e.set("gen", 1, s)
    e.set("arp_mode", 2 + (s % 4), s)
    e.set("arp_div", DIVS.index(["1/4", "1/4T", "1/8", "1/8T", "1/16"][s % 5]), s)
    e.set("oct", (s % 4), s)
    e.set("degree", s % 7, s)
    e.set("level", 0.5, s)
    e.set("pan", -1 + 2 * s / 15, s)
L, R = e.render(4.0)
fr = e.frames()
check(np.all(np.isfinite(L)) and rms(L) > 0.02 and np.max(np.abs(L)) <= 1.0 and sum(c.active for c in fr[-1].ch) == 16,
      f"16 channels: finite, rms {rms(L):.3f}, peak {np.max(np.abs(L)):.3f} (limited), all 16 active")
e.close()

# ---- four instances side by side: each bit-identical to one alone
ref = Engine()
Lr, Rr = ref.render(3.0)
insts = [Engine() for _ in range(4)]
outs = [[] for _ in range(4)]
for _ in range(12):
    for i, inst in enumerate(insts):
        outs[i].append(inst.render(0.25)[0])
same = all(np.concatenate(outs[i]).tobytes() == Lr.tobytes() for i in range(4))
check(same, "four instances rendered alternately: each bit-identical to one instance alone")
check(len({inst.frames()[-1].instance for inst in insts}) == 4, "telemetry numbers the instances")
for inst in insts:
    inst.close()
ref.close()

# ---- block sizes do not change the audio
a, b = Engine(), Engine()
La, _ = a.render(2.0, block=512)
Lb, _ = b.render(2.0, block=37)
check(np.max(np.abs(La - Lb)) < 1e-6, f"37-sample blocks vs 512: identical audio (max diff {np.max(np.abs(La - Lb)):.2e})")
a.close()
b.close()

# ---- the limiter: 16 loud channels stay below 0 dBFS, a quiet signal passes untouched
e = Engine()
for s in range(SLOTS):
    e.set("gen", 1, s)
    e.set("level", 1.0, s)
    e.set("amp_s", 1.0, s)
    e.set("arp_gate", 1.0, s)
    e.set("shape", 1.0, s)
e.set("volume", 1.0)
L, _ = e.render(2.0)
check(np.max(np.abs(L)) <= 1.0, f"16 loud channels: peak {np.max(np.abs(L)):.3f} <= 1.0")
e.close()

# ---- CPU: the default patch and 16 channels, 512-sample blocks at 48 kHz
for label, slots in [("default patch (3 channels)", 3), ("16 channels", 16)]:
    e = Engine()
    for s in range(slots):
        e.set("gen", 1, s)
    e.render(0.5)
    t0 = time.perf_counter()
    e.render(10.0)
    dt = time.perf_counter() - t0
    check(dt < 2.0, f"CPU {label}: 10 s of audio in {dt:.2f} s ({100 * dt / 10:.1f} % of real time)")
    e.close()

# ---- the CPU meter: every frame carries its block's render time over the block's length, plausible, and close to
# what the wall clock measures from outside (the meter leaves out only the calls' overhead)
e = Engine()
e.render(0.5)
e.frames()
t0 = time.perf_counter()
_, _, fr = e.render(4.0, block=512, collect=True)
dt = time.perf_counter() - t0
cpu = np.array([f.cpu for f in fr])
check(len(fr) == -(-int(4.0 * SR) // 512) and np.all(np.isfinite(cpu)) and np.all(cpu > 0) and np.all(cpu < 1.0),
      f"CPU meter, default patch at 512-sample blocks: every frame filled, mean {100 * cpu.mean():.2f} %, peak {100 * cpu.max():.2f} %")
check(cpu.mean() <= dt / 4.0 * 1.05, f"CPU meter: mean {100 * cpu.mean():.2f} % within the wall clock's {100 * dt / 4.0:.2f} % of real time")
e.close()
e = Engine()
for s in range(16):
    e.set("gen", 1, s)
e.render(0.5)
e.frames()
cpu16 = np.array([f.cpu for f in e.render(2.0, block=512, collect=True)[2]])
check(cpu16.mean() > cpu.mean(), f"CPU meter: 16 channels {100 * cpu16.mean():.2f} % over the default patch's {100 * cpu.mean():.2f} %")
e.close()

# ---- state: a changed patch survives save / load with identical audio
a = Engine()
a.set("tempo", 95)
a.set("scale", 4)
a.set("gen", 1, 5)
a.set("arp_div", DIVS.index("1/16"), 5)
a.set("shape", 0.9, 5)
blob = a.save()
b = Engine()
b.load(blob)
La, _ = a.render(2.0)
Lb, _ = b.render(2.0)
check(La.tobytes() == Lb.tobytes(), f"save / load ({len(blob)} bytes): the restored instance renders the same audio")
a.close()
b.close()

# ---- mute is a mixer mute: the output falls to silence within 20 ms, the pattern keeps its place,
# unmute comes back in place on the grid (the same audio as a channel never muted)
def pad(e):
    """One held pad with a long release: what used to ring on after a mute."""
    e.solo_arp(slot=0, mode="Up", div="1/8", sus=0.8, dec=0.5, att=0.01, gate=0.9, bpm=120)
    e.set("amp_r", 2.0, 0)

ref, m = Engine(), Engine()
pad(ref)
pad(m)
ref.render(1.0)
m.render(1.0)
ref.frames()  # the warm-up's frames out of the ring
m.frames()
Lref, _, fref = ref.render(3.0, block=64, collect=True)
m.set("mute", 1, 0)
L1, _, f1 = m.render(1.0, block=64, collect=True)
m.set("mute", 0, 0)
L2, _, f2 = m.render(2.0, block=64, collect=True)
n20 = int(0.020 * SR)
check(np.max(np.abs(L1[:n20 // 2])) > 0.01 and np.max(np.abs(L1[n20:])) == 0.0,
      f"mute: the pad falls to silence within 20 ms (peak after 20 ms {np.max(np.abs(L1[n20:])):.1e}) instead of ringing out its release")
check(all(f.ch[0].active == 1 and f.ch[0].muted == 1 for f in f1) and all(f.ch[0].muted == 0 for f in f2[2:]),
      "mute: telemetry keeps the channel active and flags it muted")
check([f.ch[0].step for f in f1] == [f.ch[0].step for f in fref[:len(f1)]],
      "mute: the step position runs on exactly as unmuted")
nu = int(0.012 * SR)
tail = Lref[len(L1) + nu:]
check(np.array_equal(L2[nu:], tail), "unmute: after the 10 ms fade the audio is the never-muted audio, sample for sample (in place on the grid)")
m.close()
ref.close()
done()
