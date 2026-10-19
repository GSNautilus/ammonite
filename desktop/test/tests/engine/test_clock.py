"""The master clock, measured from the audio: step timing per division and
tempo, three grids locked over a minute at an odd tempo, swing, host sync
(steps on the host's beats, a position jump, internal mode ignores the
host's tempo and beat); the transport in either sync (stopped: no new notes,
the releases and effects ring out, the clock holds; Play goes on from there);
no host clock at all: the engine plays itself."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")
DEC_30MS = np.log2(6) / 9.64  # DECAY 30 ms

# ---- step timing (Loop: the same note every step)
for div, bpm, want in [("1/8", 120, 0.25), ("1/8", 60, 0.5), ("1/16", 120, 0.125), ("1/4T", 120, 1 / 3), ("1/4", 137, 60 / 137)]:
    e = Engine()
    e.solo_arp(0, "Loop", div, octave=3, bpm=bpm, dec=DEC_30MS)
    L, R = e.render(4.0)
    iv = np.diff(onsets(L))
    check(len(iv) > 4 and abs(iv.mean() - want) < 0.001 and iv.std() < 0.001,
          f"{div} at {bpm} BPM: steps {iv.mean() * 1000:.1f} ms (want {want * 1000:.1f}), jitter {iv.std() * 1000:.2f} ms")
    e.close()

# ---- the long and the dotted divisions (a step of 4/1 is four bars)
for div, bpm, want, secs in [("1/8.", 120, 0.375, 4.0), ("1/4.", 120, 0.75, 6.0), ("1/2.", 120, 1.5, 9.0), ("2/1", 240, 2.0, 11.0),
                             ("4/1", 240, 4.0, 21.0)]:
    e = Engine()
    e.solo_arp(0, "Loop", div, octave=3, bpm=bpm, dec=DEC_30MS)
    L, _ = e.render(secs)
    iv = np.diff(onsets(L))
    check(len(iv) >= 4 and abs(iv.mean() - want) < 0.001 and iv.std() < 0.001,
          f"{div} at {bpm} BPM: {len(iv)} steps of {iv.mean() * 1000:.1f} ms (want {want * 1000:.1f}), jitter {iv.std() * 1000:.2f} ms")
    e.close()

# a step longer than the restart period counts freely: 2/1, LENGTH 2, half the steps, Restart Bar -> every other step
e = Engine()
e.solo_arp(0, "Loop", "2/1", octave=3, bpm=240, dec=DEC_30MS, length=2)
e.set("arp_density", 0.5, 0)
e.set("restart", 1)
L, _ = e.render(13.0)
iv = np.diff(onsets(L))
check(len(iv) >= 2 and np.allclose(iv, 4.0, atol=0.001), f"2/1 with Restart Bar plays every other step: {np.round(iv, 3)} s (want 4 s apart)")
e.close()

# ---- one clock, three divisions: no drift over a minute at 137 BPM
# (one channel per instance, so the onsets are clean; the clock is the same code)
bps = 137 / 60
worst = 0.0
counts = []
for div in ["1/4T", "1/8T", "1/16"]:
    e = Engine()
    e.solo_arp(0, "Loop", div, octave=3, bpm=137, dec=DEC_30MS)
    L, _ = e.render(60.0)
    lat = e.latency() / SR  # the master limiter's look-ahead: a host shifts the output back by it
    t = onsets(L) - lat
    d = DIV_BEATS[DIVS.index(div)] / bps  # seconds per step
    err = np.abs(t / d - np.round(t / d)) * d
    worst = max(worst, float(err.max()))
    counts.append(len(t))
    e.close()
check(counts[0] >= 205 and counts[1] >= 410 and counts[2] >= 547 and worst < 0.002,
      f"60 s at 137 BPM on 1/4T, 1/8T, 1/16: {counts} onsets (205 / 411 / 548), worst {1000 * worst:.2f} ms off the grid (no drift)")

# ---- swing: even steps longer, odd shorter, pairs unchanged
e = Engine()
e.solo_arp(0, "Loop", "1/8", octave=3, bpm=120, dec=DEC_30MS)
e.set("swing", 0.6)
e.warm(0.5)
L, _ = e.render(4.0)
iv = np.diff(onsets(L))
long_, short = iv[0::2], iv[1::2]
if abs(long_.mean() - 0.25) < abs(short.mean() - 0.25):  # phase of the pairs
    long_, short = short, long_
check(len(iv) > 8 and abs(long_.mean() + short.mean() - 0.5) < 0.004 and 0.28 < long_.mean() < 0.32 and 0.18 < short.mean() < 0.22,
      f"swing 0.6 on 1/8 at 120: steps {1000 * long_.mean():.1f} / {1000 * short.mean():.1f} ms (300 / 200), pairs {1000 * (long_.mean() + short.mean()):.1f} ms, {len(iv)} steps")
e.close()

# ---- host sync
e = Engine()
e.solo_arp(0, "Loop", "1/4", octave=3, bpm=100, dec=DEC_30MS)
e.set("sync", 0)  # host
L, _ = e.render(4.0, clock=(True, 90.0, 0.0))
t = onsets(L)
off = float(np.max(np.abs(t - np.round(t / (60 / 90)) * (60 / 90)))) * 1000 if len(t) else 999
check(len(t) >= 5 and abs(np.median(np.diff(t)) - 60 / 90) < 0.002 and off < 2.5,
      f"host clock 90 BPM from beat 0: a step every {1000 * np.median(np.diff(t)):.1f} ms (666.7), {off:.2f} ms off the beats")
L, _ = e.render(1.2, clock=(True, 120.0, 16.25))  # the playhead jumps to beat 16.25
t = onsets(L)
check(len(t) >= 2 and t[0] < 0.01 and abs(t[1] - 0.375) < 0.004,
      f"playhead jump to beat 16.25 at 120: a step at once ({1000 * t[0]:.1f} ms), the next on beat 17 ({1000 * t[1]:.1f} ms, want 375)")
L, _ = e.render(2.0, clock=(False, 150.0, 3.0))  # stopped: no new notes
t = onsets(L)
check(len(t) == 0, f"transport stopped in Host sync: no new notes ({len(t)} onsets in 2 s)")
e.set("sync", 1)  # internal: the host is ignored
e.set("tempo", 110)
L, _ = e.render(2.0, clock=(True, 90.0, 0.0))
t = onsets(L[int(0.1 * SR):])
check(len(t) >= 3 and abs(np.median(np.diff(t)) - 60 / 110) < 0.002,
      f"Sync Internal: the host is ignored, Tempo 110 (every {1000 * np.median(np.diff(t)):.1f} ms)")
e.close()

# Internal is the default; it keeps its own tempo and beat but follows the host's play / stop
e = Engine()
e.solo_arp(0, "Loop", "1/4", octave=3, bpm=100, dec=DEC_30MS)
e.frames()
_, _, on = e.render(0.5, clock=(True, 90.0, 40.0), collect=True)
_, _, off = e.render(0.5, clock=(False, 90.0, 40.0), collect=True)
check(e.get("sync") == 1 and on[-1].playing == 1 and off[-1].playing == 0 and abs(on[-1].bpm - 100) < 1e-6 and on[-1].beat < 2.0,
      f"default Sync Internal: Tempo 100 against the host's 90 ({on[-1].bpm:.0f}), its own beat ({on[-1].beat:.2f}, not the host's 40), "
      f"playing {on[-1].playing} / {off[-1].playing} with the host's transport")
e.close()

# ---- the transport, Internal: Stop ends the notes (no new ones, the releases and the reverb ring out, the clock
# holds), Play goes on from where it stopped. Notes counted from the telemetry (the reverb would fool onsets).
e = Engine()
e.solo_arp(0, "Loop", "1/4", octave=3, bpm=120, dec=DEC_30MS)
e.set("rev_send", 0.6, 0)
e.set("rev_return", 1.0)
e.frames()
L1, _, f1 = e.render(2.25, clock=(True, 90.0, 0.0), collect=True)  # 4.5 beats: stopped mid-step
n1 = sum(f.ch[0].hit for f in f1)
L2, _, f2 = e.render(3.0, clock=(False, 90.0, 0.0), collect=True)
n2 = sum(f.ch[0].hit for f in f2)
tail = float(np.sqrt(np.mean(L2[:int(0.2 * SR)] ** 2)))
late = float(np.sqrt(np.mean(L2[int(2.5 * SR):] ** 2)))
beats = [f.beat for f in f2]
check(n1 >= 4 and n2 == 0 and f2[-1].playing == 0, f"Internal, playing: {n1} notes in 2.25 s; stopped: {n2} new ones in 3 s")
check(tail > 1e-3 and late < 0.2 * tail,
      f"stopped: the reverb rings out (rms {tail:.4f} over the first 0.2 s, {late:.5f} after 2.5 s), not cut")
check(max(beats) - min(beats) < 1e-9 and abs(beats[0] - 4.5) < 0.001,
      f"stopped: the clock holds at beat {beats[0]:.4f} (4.5), every frame ({max(beats) - min(beats):.1e} apart)")
L3, _, f3 = e.render(2.0, clock=(True, 90.0, 0.0), collect=True)
hit3 = [i for i, f in enumerate(f3) if f.ch[0].hit]
t3 = hit3[0] * e.max_block / SR if hit3 else -1
check(f3[-1].playing == 1 and abs(f3[0].beat - 4.5) < 0.03 and abs(t3 - 0.25) < e.max_block / SR + 1e-6 and len(hit3) >= 3,
      f"Play again: on from beat {f3[0].beat:.3f}, the first note {1000 * t3:.0f} ms in (the next step, beat 5: 250 ms), {len(hit3)} in 2 s")
e.close()

# ---- a transport with no tempo or position (Mini's: valid 0): its play / stop still rules, the Tempo knob the rest
e = Engine()
e.solo_arp(0, "Loop", "1/4", octave=3, bpm=120, dec=DEC_30MS)
e.set("sync", 0)  # even Host sync: no tempo comes, so Tempo
e.frames()
hits = {}
for playing in (1, 0, 1):
    n = 0
    for k in range(int(1.0 * SR) // 512):
        L = np.zeros(512, dtype=np.float32)
        R = np.zeros(512, dtype=np.float32)
        DLL.ammo_process(e.p, L.ctypes.data_as(C.POINTER(C.c_float)), R.ctypes.data_as(C.POINTER(C.c_float)), 512,
                         C.byref(AmmoClock(0, playing, 0.0, 0.0)))
        fr = e.frames()
        n += sum(f.ch[0].hit for f in fr)
        last = fr[-1] if fr else last
    hits[len(hits)] = (n, last.playing, last.bpm)
check(hits[0][0] >= 2 and hits[1][0] == 0 and hits[2][0] >= 1 and [h[1] for h in hits.values()] == [1, 0, 1] and hits[0][2] == 120,
      f"a transport with no tempo (valid 0): play {hits[0][0]} notes, stop {hits[1][0]}, play {hits[2][0]}; at Tempo {hits[0][2]:.0f}")
e.close()

# ---- no clock at all (a test, the bench): the engine plays itself
e = Engine()
e.solo_arp(0, "Loop", "1/4", octave=3, bpm=120, dec=DEC_30MS)
e.frames()
L, _, fr = e.render(2.0, collect=True)
check(len(onsets(L)) >= 3 and fr[-1].playing == 1, f"no host clock: plays ({len(onsets(L))} notes in 2 s), playing {fr[-1].playing}")
e.close()

# ---- telemetry follows the clock
e = Engine()
e.solo_arp(0, "Loop", "1/4", octave=3, bpm=120, dec=DEC_30MS)
e.set("sync", 0)
e.frames()
_, _, fr = e.render(1.0, clock=(True, 120.0, 8.0), collect=True)
check(len(fr) >= 93 and fr[0].hostClock == 1 and fr[0].playing == 1 and abs(fr[0].bpm - 120) < 1e-6
      and 8.0 <= fr[0].beat < 8.05 and 9.9 < fr[-1].beat <= 10.0,
      f"telemetry: {len(fr)} frames (one per block), host clock, playing, beat {fr[0].beat:.2f} -> {fr[-1].beat:.2f} (8 -> 10)")
e.close()
done()
