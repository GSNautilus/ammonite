"""Modulation (engine/Mod.hpp, its pool):
what a route can take (every continuous channel parameter, the global ones,
nothing stepped); the LFOs read from telemetry (Tempo locked to the beat,
Free at its rate, phase, every shape, Random held per cycle, restart on a
channel's notes, its own or another's); routes heard in the audio (tremolo on
Level, vibrato on Fine tune, a filter sweep on Cutoff, a macro on Cutoff);
routes adding and clamping; one LFO on two channels; global targets (a macro
on the volume and the reverb); unipolar against bipolar; an LFO running only
because a route takes it; a dead route changing nothing; the same audio for
any block size; the factory presets' routes; a version 2 patch converted
into the pool; and the CPU with the whole pool live."""
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")

SHAPES, LDIVS = LFO_SHAPES, LFO_DIVS
MACRO = lambda m: SRC_MACRO1 + m  # noqa: E731
LFO = lambda l: SRC_LFO1 + l  # noqa: E731
PAGE_SC_GAIN = 4096 + 768 + 2  # the scope's Height, a page parameter (app.js VSCBASE + 2)
PAGE_IDS_END = 4096 + 1024     # kPageIdBase + kPageIds


def tone(**kw):
    """A held A4 (440 Hz): Loop at 1/1 at 60 BPM (4 s steps), gate 1, sustain 1."""
    e = Engine()
    e.solo_arp(0, "Loop", "1/1", length=16, pool="Root", octave=2, bpm=60, dec=0.5, sus=1.0, gate=1.0, att=0.0, **kw)
    return e


def lfo_hz(v):
    return 0.02 * 2 ** (v * 9.966)


def live_ids(e, ids):
    """The ids a route takes (its telemetry position is not -1), one route at a time."""
    out = []
    for t in ids:
        e.route(0, MACRO(0), t, 0.1)
        _, _, fr = e.render(0.02, collect=True)
        if fr[-1].route[0] >= 0:
            out.append(t)
    e.route(0, 0, -1, 0)
    return out


# ---- the targets: every continuous slot parameter and the continuous globals but the tempo; nothing stepped, no
# modulation parameter, no pool id
e = tone()
slot_cont = [p for p in INFOS if p.group == b"slot 1" and p.steps == 0]
slot_step = [p for p in INFOS if p.group == b"slot 1" and p.steps > 0]
glob_cont = [p for p in INFOS if p.group == b"global" and p.steps == 0 and p.id < GLOBAL_IDS]
got = live_ids(e, [p.id for p in slot_cont])
check(len(got) == len(slot_cont) == 41, f"every continuous parameter of a channel is a target ({len(got)} of {len(slot_cont)})")
got = live_ids(e, [p.id for p in slot_step])
check(not got, f"no stepped one ({len(slot_step)} tried: {[info_id(i).name.decode() for i in got] or 'none taken'})")
gnames = sorted(info_id(i).symbol.decode() for i in live_ids(e, [p.id for p in glob_cont]))
want = sorted(["swing", "volume", "rev_size", "rev_decay", "rev_damp", "rev_pre", "rev_width", "rev_return", "glue", "ceiling"])
check(gnames == want, f"the global targets: {', '.join(gnames)} (not the tempo or the macros)")
check(not live_ids(e, [lfo_id(0, "rate"), route_id(1, "amt"), macro_id(5), -1, 99999]), "not a pool parameter, nor none, nor an id out of range")
e.close()

# ---- Tempo: the LFO's phase is the beat's (32-sample blocks: one modulation update each, at the block's start)
e = tone()
e.lfo(0, "Sine", "Tempo", div="1/4")
e.route(0, LFO(0), pid("pan", 0), 0.0001)  # live: the grid runs
_, _, fr = e.render(2.0, block=32, collect=True)
bps = 60 / 60 / SR
err = max(abs(f.lfo[0] - np.sin(2 * np.pi * ((f.beat - 31 * bps) % 1.0))) for f in fr[4:])
check(err < 1e-3, f"Tempo 1/4 sine: the value is sin(2 pi beat) at every update (max error {err:.1e})")
e.close()

e = tone()
e.lfo(0, "Saw up", "Tempo", div="1/1", phase=0.25)
e.route(0, LFO(0), pid("pan", 0), 0.0001)
_, _, fr = e.render(3.0, block=32, collect=True)
err = max(abs(f.lfo[0] - (2 * (((f.beat - 31 * bps) / 4 + 0.25) % 1.0) - 1)) for f in fr[4:] if ((f.beat - 31 * bps) / 4 + 0.25) % 1.0 > 0.01)
check(err < 1e-3, f"Tempo 1/1 saw, phase 0.25: a quarter of a bar ahead (max error {err:.1e})")
e.close()

# ---- Free: the rate in Hz, counted by zero crossings of the telemetry
for rate in (0.45, 0.6):
    e = tone()
    e.lfo(3, "Sine", "Free", rate=rate)
    e.route(9, LFO(3), pid("pan", 0), 0.0001)
    _, _, fr = e.render(8.0, block=256, collect=True)
    v = np.array([f.lfo[3] for f in fr])
    ups = np.where((v[:-1] < 0) & (v[1:] >= 0))[0]
    hz = (len(ups) - 1) / ((ups[-1] - ups[0]) * 256 / SR) if len(ups) > 2 else 0
    check(abs(hz / lfo_hz(rate) - 1) < 0.02, f"Free rate {rate}: {hz:.3f} Hz (expected {lfo_hz(rate):.3f})")
    e.close()

# ---- every shape stays in -1..1; Square is only +-1; Random holds one value per cycle and moves on
e = tone()
e.route(0, LFO(0), pid("pan", 0), 0.0001)
for sh in SHAPES:
    e.lfo(0, sh, "Tempo", div="1/4")
    _, _, fr = e.render(2.0, block=32, collect=True)
    v = np.array([f.lfo[0] for f in fr[2:]])
    ok = v.min() >= -1.0001 and v.max() <= 1.0001 and v.max() - v.min() > 0.3
    if sh == "Square":
        ok = ok and np.all(np.isclose(np.abs(v), 1.0))
    if sh == "Random":
        beats = np.array([f.beat - 31 * bps for f in fr[2:]])
        cyc = np.floor(beats).astype(int)
        held_ = all(np.ptp(v[cyc == c]) < 1e-6 for c in np.unique(cyc))
        ok = ok and held_ and len(np.unique(np.round(v, 5))) >= len(np.unique(cyc)) - 1
    if sh == "Drift":
        ok = ok and np.max(np.abs(np.diff(v))) < 0.05  # smooth: no jumps
    check(ok, f"{sh}: {v.min():+.2f} .. {v.max():+.2f}")
e.close()

# ---- RESTART on a channel's notes: the cycle starts again on each (a saw up begins at -1): its own channel's, or
# another's (an LFO on channel 1's sixteenths, restarted by channel 2's half notes and by nothing else)
e = Engine()
e.solo_arp(0, "Up", "1/4", bpm=120, dec=0.3)
e.lfo(0, "Saw up", "Free", rate=0.2, restart=1)  # 0.08 Hz: without the restart it would barely move
e.route(0, LFO(0), pid("pan", 0), 0.0001)
_, _, fr = e.render(3.0, block=32, collect=True)
after = [fr[i + 1].lfo[0] for i in range(len(fr) - 1) if fr[i].ch[0].hit]
check(len(after) >= 5 and all(v == -1.0 for v in after), f"restart on its channel's notes: after each of {len(after)} notes the saw is back at its start")
e.close()
e = Engine()
e.solo_arp(0, "Up", "1/16", bpm=120, dec=0.3)
e.set("gen", 1, 1)
e.set("arp_div", DIVS.index("1/2"), 1)
e.set("arp_density", 1.0, 1)
e.lfo(0, "Saw up", "Free", rate=0.2, restart=2)
e.route(0, LFO(0), pid("cutoff", 0), 0.1)
_, _, fr = e.render(4.0, block=32, collect=True)
after2 = [fr[i + 1].lfo[0] for i in range(len(fr) - 1) if fr[i].ch[1].hit]
own = [fr[i + 1].lfo[0] for i in range(len(fr) - 1) if fr[i].ch[0].hit and not fr[i].ch[1].hit]
check(len(after2) >= 3 and all(v == -1.0 for v in after2) and len(own) >= 20 and all(v > -1.0 for v in own),
      f"restart on channel 2's notes (the LFO moves channel 1): back at its start after each of {len(after2)}, "
      f"not after channel 1's own {len(own)}")
e.close()

# ---- tremolo: an LFO (Free 4 Hz sine) on Level, amount 0.5: the envelope moves at 4 Hz
e = tone()
e.lfo(7, "Sine", "Free", rate=np.log2(4 / 0.02) / 9.966)
e.route(40, LFO(7), pid("level", 0), -0.5)  # level 1: down from the top
e.warm(0.3)
L, _ = e.render(2.0)
env0, _ = env_frames(L, ms=5)
env = env0 - env0.mean()
sp = np.abs(np.fft.rfft(env * np.hanning(len(env))))
fq = np.fft.rfftfreq(len(env), 0.005)
pk = fq[np.argmax(sp[1:]) + 1]
depth = np.percentile(env0, 95) / max(1e-9, np.percentile(env0, 5))
check(abs(pk - 4.0) < 0.3 and depth > 3, f"tremolo: the level moves at {pk:.2f} Hz, {20 * np.log10(depth):.1f} dB deep")
e.close()

# ---- vibrato: an LFO (Free 0.5 Hz sine) on Fine tune, amount 0.25 = +-50 cents
e = tone()
e.lfo(0, "Sine", "Free", rate=np.log2(0.5 / 0.02) / 9.966)
e.route(0, LFO(0), pid("tune", 0), 0.25)
e.warm(0.3)
L, _ = e.render(4.0)
w = 4096
cents = [1200 * np.log2(peak_freq(L[i:i + w], 300, 600) / 440) for i in range(0, len(L) - w, w // 2)]
check(80 < max(cents) - min(cents) < 105 and abs(max(cents) + min(cents)) < 15,
      f"vibrato on Fine tune: {min(cents):+.0f} .. {max(cents):+.0f} cents (+-50 at amount 0.25)")
e.close()
e = tone()
e.set("tune", -30, 0)
e.warm(0.3)
L, _ = e.render(1.0)
c = 1200 * np.log2(peak_freq(L, 300, 600) / 440)
check(abs(c + 30) < 3, f"Fine tune -30 cents: {c:+.1f} cents")
e.close()

# ---- a filter sweep: a Tempo square on Cutoff, a half note long (2 s at 60 BPM), opens and shuts the filter
e = tone(shape=0.667, cutoff=0.35)
e.lfo(0, "Square", "Tempo", div="1/2")
e.route(0, LFO(0), pid("cutoff", 0), 0.3)
e.warm(0.1)
L, _ = e.render(4.0)
hi = [hf_ratio(L[int(t * SR):int((t + 0.2) * SR)], 3000) for t in np.arange(0.05, 3.5, 0.25)]
check(max(hi) > 5 * min(hi), f"Cutoff square: the top end swings {max(hi) / max(min(hi), 1e-9):.0f}x between the halves")
e.close()


# ---- a macro: Macro 1 on Cutoff; at 0 it changes nothing, at 1 it opens the filter (and Macro 9, past the host's
# four, does the same)
def bright(macro, with_route=True, m=0):
    e = tone(shape=0.667, cutoff=0.3)
    if with_route:
        e.route(5, MACRO(m), pid("cutoff", 0), 0.4)
    e.set_id(macro_id(m), macro)
    e.warm(0.3)
    L, _ = e.render(1.0)
    e.close()
    return L


L0, Lr, L1, L9 = bright(0.0, False), bright(0.0), bright(1.0), bright(1.0, m=8)
check(np.max(np.abs(L0 - Lr)) < 1e-6, f"Macro 1 at 0 on Cutoff: the same audio as no route (max diff {np.max(np.abs(L0 - Lr)):.1e})")
check(hf_ratio(L1, 3000) > 4 * hf_ratio(L0, 3000), f"Macro 1 at 1 on Cutoff: the top end {hf_ratio(L1, 3000) / max(hf_ratio(L0, 3000), 1e-9):.0f}x brighter")
check(np.array_equal(L1, L9), "Macro 9 (a pool id, not a host's macro 1..4) does the same as Macro 1")

# ---- a dead route (amount 0, no source, no target, a target a route cannot take) changes nothing, and the
# telemetry says so
ref = tone()
ref.warm(0.3)
Lref, _, fr = ref.render(1.0, collect=True)
check(all(f.route[r] == -1 for f in fr for r in range(POOL_ROUTES)) and all(f.lfoRun == 0 for f in fr),
      "no route: every route position reads -1, no LFO runs")
ref.close()
for label, r in [("amount 0", (LFO(0), pid("cutoff", 0), 0.0)), ("source Off", (0, pid("cutoff", 0), 0.5)),
                 ("no target", (LFO(0), -1, 0.5)), ("a stepped target", (LFO(0), pid("arp_div", 0), 0.5)),
                 ("the tempo as target", (MACRO(0), pid("tempo"), 0.5)), ("a page parameter as target", (MACRO(0), PAGE_SC_GAIN, 0.5))]:
    e = tone()
    e.lfo(0, on=0)
    e.set("macro1", 1.0)
    ref = tone()
    ref.set("macro1", 1.0)
    e.route(17, *r)
    e.warm(0.3)
    ref.warm(0.3)
    L, _ = e.render(1.0)
    Lref, _ = ref.render(1.0)
    check(np.array_equal(L, Lref), f"a route with {label}: the same audio as none")
    e.close()
    ref.close()

# ---- a route onto one of the page's parameters (kPageIdBase ..: the stage's look, the scope) keeps its target, in a
# saved patch too, and the engine leaves it to the page (no telemetry position)
e = tone()
e.route(5, MACRO(0), PAGE_SC_GAIN, 0.5)
e.render(0.05)
_, _, fr = e.render(0.05, collect=True)
check(e.get_id(route_id(5, 'target')) == PAGE_SC_GAIN and fr[-1].route[5] == -1,
      f"a route on page id {PAGE_SC_GAIN} (the scope's Height) keeps its target ({e.get_id(route_id(5, 'target')):.0f}); the engine sends no position for it")
f = Engine()
f.load(e.save())
check(f.get_id(route_id(5, 'target')) == PAGE_SC_GAIN, "and a saved patch brings it back")
f.set_id(route_id(5, 'target'), PAGE_IDS_END + 50)
check(f.get_id(route_id(5, 'target')) == PAGE_IDS_END - 1, f"a target past the page's ids is clamped to its last ({f.get_id(route_id(5, 'target')):.0f})")
e.close()
f.close()

# ---- routes on one target add, and the sum is clamped to its range
e = tone(cutoff=0.5)
e.set("macro1", 1.0)
e.set("macro2", 1.0)
e.route(0, MACRO(0), pid("cutoff", 0), 0.2)
e.route(7, MACRO(1), pid("cutoff", 0), 0.1)
e.route(3, MACRO(0), pid("level", 0), 0.5)  # level 1: past the top
_, _, fr = e.render(0.2, collect=True)
m = fr[-1].route
check(abs(m[0] - 0.8) < 1e-5 and abs(m[7] - 0.8) < 1e-5 and m[3] == 1.0 and m[1] == -1,
      f"two routes on Cutoff add (0.5 + 0.2 + 0.1 = {m[0]:.3f}, both report it); Level clamped at {m[3]:.2f}")
e.close()

# ---- one LFO on two channels: they move together (opposite amounts: their pans mirror each other)
e = Engine()
e.solo_arp(0, "Up", "1/8", bpm=120)
e.set("gen", 1, 1)
e.set("pan", 0, 1)  # the default patch pans it
e.lfo(12, "Triangle", "Tempo", div="1/1")
e.route(0, LFO(12), pid("pan", 0), 0.4)
e.route(1, LFO(12), pid("pan", 1), -0.4)
_, _, fr = e.render(4.0, block=32, collect=True)
g = np.array([f.lfo[12] for f in fr])
want = np.array([(lambda u: 4 * u if u < 0.25 else (2 - 4 * u if u < 0.75 else 4 * u - 4))(((f.beat - 31 * 2 * bps) / 4) % 1) for f in fr])
p0 = np.array([f.route[0] for f in fr])
p1 = np.array([f.route[1] for f in fr])
check(np.max(np.abs(g - want)) < 1e-3, f"LFO 13: a triangle a bar long, on the beat (max error {np.max(np.abs(g - want)):.1e})")
check(np.max(np.abs((p0 - 0.5) + (p1 - 0.5))) < 1e-4 and np.ptp(p0) > 0.7,
      f"two channels on it, opposite amounts: their pans mirror each other ({p0.min():.2f} .. {p0.max():.2f})")
e.close()


# ---- global targets: a macro on the volume (heard: -6 dB at amount -0.5 from 1) and on the reverb (seen)
def vol(macro):
    e = tone()
    e.set("volume", 1.0)
    e.set("ceiling", 0.0)
    e.set("level", 0.25, 0)
    e.route(2, MACRO(3), pid("volume"), -0.5)
    e.route(3, MACRO(3), pid("rev_size"), 0.3)
    e.set("macro4", macro)
    e.warm(0.5)
    L, _, fr = e.render(1.0, collect=True)
    e.close()
    return L, fr


La, fa = vol(0.0)
Lb, fb = vol(1.0)
ratio = rms(Lb) / rms(La)
check(abs(ratio - 0.5) < 0.02, f"Macro 4 on Volume, -0.5: the output at {20 * np.log10(ratio):.2f} dB (want -6.02)")
check(abs(fa[-1].route[3] - 0.5) < 1e-5 and abs(fb[-1].route[3] - 0.8) < 1e-5 and abs(fb[-1].route[2] - 0.5) < 1e-5,
      f"... and on Reverb size (+0.3): {fa[-1].route[3]:.2f} -> {fb[-1].route[3]:.2f}; Volume reads {fb[-1].route[2]:.2f}")
e = Engine()
e.all_off()
e.set("swing", 0.2)
e.lfo(0, "Sine", "Free", rate=0.5)
e.route(0, LFO(0), pid("swing"), 0.1)
_, _, fr = e.render(1.0, block=32, collect=True)
sw = np.array([f.route[0] for f in fr[2:]])
check(sw.min() >= 0.0999 and sw.max() <= 0.3001 and np.ptp(sw) > 0.05,
      f"a global target with no channel playing: it moves all the same (swing {sw.min():.3f} .. {sw.max():.3f})")
e.close()

# ---- Unipolar against Bipolar: an LFO bipolar swings the pan both ways from its place, unipolar only up from it
# (down with a negative amount); a macro is 0..1 either way
for pol, amt, lo, hi in [(1, 0.4, 0.1, 0.9), (0, 0.4, 0.5, 0.9), (0, -0.4, 0.1, 0.5)]:
    e = tone()
    e.lfo(2, "Sine", "Tempo", div="1/4")
    e.route(6, LFO(2), pid("pan", 0), amt, pol=pol)
    _, _, fr = e.render(2.0, block=64, collect=True)
    p = np.array([f.route[6] for f in fr[2:]])
    check(abs(p.min() - lo) < 0.01 and abs(p.max() - hi) < 0.01,
          f"{'bipolar' if pol else 'unipolar'} {amt:+.1f}: the pan's place {p.min():.3f} .. {p.max():.3f} (want {lo} .. {hi})")
    e.close()
pos = []
for pol in (0, 1):
    e = tone()
    e.set("macro2", 0.5)
    e.route(0, MACRO(1), pid("pan", 0), 0.4, pol=pol)
    _, _, fr = e.render(0.1, collect=True)
    pos.append(fr[-1].route[0])
    e.close()
check(abs(pos[0] - 0.7) < 1e-5 and pos[0] == pos[1], f"a macro at 0.5, +0.4: {pos[0]:.3f} unipolar, {pos[1]:.3f} bipolar (the same)")

# ---- an LFO runs while it is in use or a route takes it: in use only through a route it still moves its target;
# in use with no route it runs for the page and changes nothing; neither, it does not run
e = tone(shape=0.667, cutoff=0.35)
e.lfo(20, "Square", "Tempo", div="1/2", on=0)
e.route(30, LFO(20), pid("cutoff", 0), 0.3)
e.warm(0.1)
L, _, fr = e.render(4.0, collect=True)
hi = [hf_ratio(L[int(t * SR):int((t + 0.2) * SR)], 3000) for t in np.arange(0.05, 3.5, 0.25)]
check(all(f.lfoRun == 1 << 20 for f in fr) and np.ptp([f.lfo[20] for f in fr]) == 2.0 and max(hi) > 5 * min(hi),
      "LFO 21 not in use, a route takes it: it runs (telemetry) and sweeps the cutoff")
e.close()
a, b = tone(), tone()
a.lfo(4, "Sine", "Free", rate=0.6, on=1)
a.lfo(5, "Sine", "Free", rate=0.6, on=0)
La, _, fr = a.render(1.0, collect=True)
Lb, _ = b.render(1.0)
check(all(f.lfoRun == 1 << 4 for f in fr) and np.ptp([f.lfo[4] for f in fr]) > 0.5 and np.array_equal(La, Lb),
      "LFO 5 in use, no route: it runs (telemetry), the audio is untouched; LFO 6, neither: it does not run")
a.close()
b.close()


# ---- the same audio for any block size, with routes live on the voice, the strip, the effects and a global, and
# an LFO restarting on notes
def busy(e):
    e.solo_arp(0, "Up-down", "1/16", bpm=128, dec=0.4, shape=0.667, cutoff=0.5)
    e.lfo(0, "Sine", "Free", rate=0.55)
    e.lfo(1, "Drift", "Tempo", div="1/2", restart=1)
    e.lfo(2, "Random", "Tempo", div="1/4")
    e.route(0, LFO(0), pid("cutoff", 0), 0.3)
    e.route(1, LFO(1), pid("tune", 0), 0.1)
    e.route(2, LFO(2), pid("pan", 0), 0.5)
    e.route(3, LFO(0), pid("dly_mix", 0), 0.4, pol=0)
    e.route(4, LFO(1), pid("rev_size"), 0.3)
    e.set("dly_fb", 0.5, 0)
    e.set("rev_send", 0.3, 0)


a, b = Engine(), Engine()
busy(a)
busy(b)
La, _ = a.render(3.0, block=512)
Lb, _ = b.render(3.0, block=37)
check(np.max(np.abs(La - Lb)) < 1e-6, f"routes live, 37-sample blocks vs 512: identical audio (max diff {np.max(np.abs(La - Lb)):.2e})")
a.close()
b.close()

# ---- the factory presets: every one's macros do something (Bloom and Space on every channel, routes 4 s + 3 and
# 4 s + 4, and they are in use; how they sound is for the ear), and Slow tide's filters move on their three LFOs
DLL.ammo_preset_count.restype = C.c_int
DLL.ammo_preset_name.restype = C.c_char_p
DLL.ammo_preset_name.argtypes = [C.c_int]
DLL.ammo_apply_preset.argtypes = [C.c_void_p, C.c_int]
names = [DLL.ammo_preset_name(k).decode() for k in range(DLL.ammo_preset_count())]


def preset(k, **glob):
    e = Engine()
    DLL.ammo_apply_preset(e.p, k)
    for g, v in glob.items():
        e.set(g, v)
    e.warm(1.0)
    L, _, fr = e.render(4.0, collect=True)
    on = [e.get_id(MACRO_ON_BASE + m) for m in range(POOL_MACROS)]
    e.close()
    return L, fr, on


for k, nm in enumerate(names):
    if nm == "Init":
        continue
    L0, fr0, on = preset(k)
    Lb, frb, _ = preset(k, macro1=1.0)
    _, frs, _ = preset(k, macro3=1.0)
    act = [s for s in range(SLOTS) if fr0[-1].ch[s].active]
    moved = all(frb[-1].route[4 * s + 2] > fr0[-1].route[4 * s + 2] + 0.1 and frs[-1].route[4 * s + 3] > fr0[-1].route[4 * s + 3] + 0.1
                for s in act)
    diff = rms(Lb - L0) / max(rms(L0), 1e-9)
    check(moved and diff > 0.05 and on[0] == 1 and on[2] == 1 and not any(on[4:]),
          f"{nm}: Bloom and Space routed on all {len(act)} channels and move them; Bloom changes the audio "
          f"({100 * diff:.0f} % of its level); macros in use {[m + 1 for m in range(POOL_MACROS) if on[m]]}")
k = names.index("Slow tide")
_, fr, _ = preset(k)
cut = np.array([[f.route[4 * s] for s in range(3)] for f in fr])
check(all(np.ptp(cut[:, s]) > 0.01 for s in range(3)) and np.ptp(cut[:, 0]) < 0.25 and fr[-1].lfoRun == 0b111,
      f"Slow tide: each drone's cutoff moves on its own LFO ({', '.join(f'{np.ptp(cut[:, s]):.3f}' for s in range(3))} of the range in 4 s)")

# ---- a version 2 patch (per-channel LFOs and routes, global LFOs, by their retired ids) loads into the
# pool and renders exactly as the same patch built on the pool directly
C1, MOD1 = 66, 78  # channel LFO 1's settings (shape, sync, rate, div, phase, retrigger; LFO 2 at +6) and route 1's


def sid_(slot, p):
    return GLOBAL_IDS + SLOT_STRIDE * slot + p


def v2_text():
    lines = ["ammonite-desktop 2"]
    add = lambda i, v: lines.append(f"{i} {v}")  # noqa: E731
    for s, kv in ((0, [("arp_mode", 4), ("arp_div", DIVS.index("1/16")), ("cutoff", 0.5), ("shape", 0.667)]), (1, [("gen", 1)]), (2, [("gen", 1)])):
        for sym, v in kv:
            add(pid(sym, s), v)
    add(pid("tempo"), 128)
    add(sid_(0, C1), 6), add(sid_(0, C1 + 1), 0), add(sid_(0, C1 + 2), 0.55), add(sid_(0, C1 + 5), 1)  # ch 1 LFO 1: Drift, Free, restart
    add(sid_(1, C1 + 6), 5), add(sid_(1, C1 + 11), 1)  # ch 2 LFO 2: Random (its default Free 1.25 Hz), restart
    add(sid_(2, C1), 3)  # ch 3 LFO 1: no route takes it (dropped)
    add(37, 6), add(40, 13), add(33, 0)  # global LFO 2: Drift on 1/16; global LFO 1 Free (no route takes it at first)

    def rt(s, r, src, dest, amt):
        add(sid_(s, MOD1 + 3 * r), src), add(sid_(s, MOD1 + 3 * r + 1), dest), add(sid_(s, MOD1 + 3 * r + 2), amt)
    rt(0, 0, 1, 10, 0.3)  # LFO 1 -> Cutoff
    rt(0, 1, 4, 2, 0.5)  # global LFO 2 -> Pan
    rt(0, 2, 5, 10, 0.2)  # Macro 1 -> Cutoff
    rt(1, 0, 2, 3, 0.1)  # LFO 2 -> Fine tune
    rt(1, 3, 3, 39, 0.4)  # global LFO 1 -> Delay mix
    rt(2, 1, 1, 10, 0.0)  # amount 0: dead
    rt(2, 2, 8, 32, 0.6)  # Macro 4 -> FX drive
    add(pid("dly_fb", 1), 0.5), add(pid("macro1"), 0.6), add(pid("macro4"), 0.8)
    return "\n".join(lines) + "\n"


def v3_direct(e):
    """The same on the pool, in the conversion's order: LFOs as routes first take them (ch 1 LFO 1, global 2, ch 2
    LFO 2, global 1), routes in order (ch 1 route 1 .. ch 16 route 4) without the dead one."""
    e.load("ammonite-desktop 3\n")
    for s, kv in ((0, [("arp_mode", 4), ("arp_div", DIVS.index("1/16")), ("cutoff", 0.5), ("shape", 0.667)]), (1, [("gen", 1)]), (2, [("gen", 1)])):
        for sym, v in kv:
            e.set(sym, v, s)
    e.set("tempo", 128)
    e.lfo(0, "Drift", "Free", rate=0.55, div="1/1", restart=1)
    e.lfo(1, "Drift", "Tempo", div="1/16")
    e.lfo(2, "Random", "Free", rate=0.6, div="1/4", restart=2)
    e.lfo(3, "Sine", "Free", rate=0.5, div="4/1")
    e.route(0, LFO(0), pid("cutoff", 0), 0.3)
    e.route(1, LFO(1), pid("pan", 0), 0.5)
    e.route(2, MACRO(0), pid("cutoff", 0), 0.2)
    e.route(3, LFO(2), pid("tune", 1), 0.1)
    e.route(4, LFO(3), pid("dly_mix", 1), 0.4)
    e.route(5, MACRO(3), pid("fx_drive", 2), 0.6)
    e.set_id(MACRO_ON_BASE + 0, 1)
    e.set_id(MACRO_ON_BASE + 3, 1)
    e.set("dly_fb", 0.5, 1), e.set("macro1", 0.6), e.set("macro4", 0.8)


a, b = Engine(), Engine()
ok = a.load(v2_text())
v3_direct(b)
sa, sb = a.save(), b.save()
check(ok and sa == sb, "a version 2 patch loads into the pool: the same chunk as the patch built on the pool directly"
      + ("" if sa == sb else f"\n{sa}\n---\n{sb}"))
retired = [i for i in range(POOL_BASE) if 32 <= i < 42 or (i >= GLOBAL_IDS and 66 <= (i - GLOBAL_IDS) % SLOT_STRIDE < 90)]
check(sa.splitlines()[0] == "ammonite-desktop 4" and not any(int(ln.split()[0]) in retired for ln in sa.splitlines()[1:]),
      "saved again: version 4, no retired id in it")
check(sum(a.get_id(lfo_id(l, "on")) for l in range(POOL_LFOS)) == 4 and a.get_id(lfo_id(0, "restart")) == 1 and a.get_id(lfo_id(2, "restart")) == 2
      and a.get_id(lfo_id(1, "restart")) == 0 and sum(a.get_id(route_id(r, "src")) > 0 for r in range(POOL_ROUTES)) == 6,
      "four LFOs (the unrouted one dropped; a channel's Restart Note -> that channel's notes, a global one free), six routes (the dead one dropped)")
La, _ = a.render(4.0, block=128)
Lb, _ = b.render(4.0, block=128)
check(np.array_equal(La, Lb) and rms(La) > 0.01, f"... and renders bit-identically to it (max diff {np.max(np.abs(La - Lb)):.1e})")
a.close()
b.close()

# ---- CPU: the whole pool live: 32 LFOs, 128 routes over 16 channels and the globals, 512-sample blocks
e = Engine()
for l in range(POOL_LFOS):
    e.lfo(l, SHAPES[l % 7], "Free" if l % 2 else "Tempo", rate=0.3 + 0.01 * l, div=LDIVS[4 + l % 10], restart=l % 17)
SLOT_TARGETS = ["cutoff", "tune", "pan", "shape", "level", "rev_send", "dly_mix", "reso"]
GLOBAL_TARGETS = ["swing", "volume", "rev_size", "rev_decay", "rev_damp", "rev_pre", "rev_width", "rev_return"]
r = 0
for s in range(SLOTS):
    e.set("gen", 1, s)
    e.set("dly_fb", 0.4, s)
    for k, sym in enumerate(SLOT_TARGETS[:8 if s < 15 else 0]):
        e.route(r, LFO(r % POOL_LFOS) if r % 4 else MACRO(r % POOL_MACROS), pid(sym, s), 0.1, pol=r % 2)
        r += 1
for sym in GLOBAL_TARGETS:
    e.route(r, LFO(r % POOL_LFOS), pid(sym), 0.05)
    r += 1
for m in range(POOL_MACROS):
    e.set_id(macro_id(m), 0.5)
e.render(0.5)
e.frames()
t0 = time.perf_counter()
_, _, fr = e.render(10.0, collect=True)
dt = time.perf_counter() - t0
nl = bin(fr[-1].lfoRun).count("1")
nr = sum(1 for x in fr[-1].route if x >= 0)
cpu = np.mean([f.cpu for f in fr])
check(nl == 32 and nr == 128 and dt < 2.0,
      f"CPU, the whole pool: {nl} LFOs, {nr} routes live, 16 channels: 10 s of audio in {dt:.2f} s ({100 * dt / 10:.1f} % of real time; "
      f"the engine's own meter {100 * cpu:.1f} %)")
e.close()

done()
