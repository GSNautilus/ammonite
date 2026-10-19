"""Which notes a pattern plays: ROTATE on
every generator through the shared step grid (Arp, Bass, Chord Steps and
Arp; Rhythm's as before), SHIFT, COUNT (Steps, Hits; Chord's Arp counting
from the change), SEED (the same notes every LENGTH, another seed another
pattern, Free never repeating; VARY, RATCHET, CHANCE too), the telemetry's
notes of the pattern (stepNote) against what plays, a version 3 patch's
leftover rotate, and identical audio for any block size with all of it on."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")
GENS = ["Off", "Arp", "Chord", "Bass", "Rhythm"]
BASE = MIDI_BASE + 9 + 12  # A3: the arp's tonic at octave 1 in A
STEP = 0.25  # 1/8 at 120 BPM
REST, UNKNOWN = -1, -2
R, T, F = 0, 3, 7  # A minor's triad: root, third, fifth (semitones above A)


def arp(length=8, density=0.5, mode="Up", pool="Triad", **slot):
    e = Engine(max_block=256)
    e.solo_arp(0, mode, "1/8", length=length, pool=pool, rng=1, density=density, dec=0.5, sus=1.0, gate=0.9, octave=1)
    e.set("root", 9)
    e.set("scale", SCALES.index("Minor"))
    for k, v in slot.items():
        e.set(k, v, 0)
    return e


def played(e, steps, base=BASE, chord=False):
    """The hits of `steps` steps as (step, note above `base`) from the telemetry, and the last frame's stepNote.
    chord: the note is the one voice sounding (Chord's Arp plays each note on its own voice; release it short)."""
    _, _, fr = e.render(steps * STEP, collect=True)
    out = []
    for f in fr:
        c = f.ch[0]
        if c.hit:
            n = c.voiceNote[0] if chord and c.voices == 1 else (-999 if chord else c.note)
            out.append((c.step, int(round(n)) - base))
    return out, list(fr[-1].ch[0].stepNote)[:fr[-1].ch[0].length]


def cycle(hits, length=8):
    """Notes of the first LENGTH cycle as a list per step (None: no hit)."""
    out = [None] * length
    for s, n in hits[:]:
        if out[s] is None:
            out[s] = n
    return out


def expect(notes, base=BASE):
    """A stepNote list from a per-step list of notes above `base` (None: a rest)."""
    return [REST if n is None else base + n for n in notes]


# ---- the worked example: LENGTH 8, density 0.5, a triad, Up
cases = [
    ("Steps", {}, [R, None, F, None, T, None, R, None]),
    ("Rotate 1", {"rhy_rotate": 1}, [None, T, None, R, None, F, None, T]),
    ("Shift 1", {"note_shift": 1}, [T, None, R, None, F, None, T, None]),
    ("Count Hits", {"note_count": 2}, [R, None, T, None, F, None, R, None]),
    ("Count Hits + Rotate 1", {"note_count": 2, "rhy_rotate": 1}, [None, R, None, T, None, F, None, R]),
    ("Count Steps (as Auto)", {"note_count": 1}, [R, None, F, None, T, None, R, None]),
    ("Down, Count Hits", {"note_count": 2, "arp_mode": MODES.index("Down")}, [F, None, T, None, R, None, F, None]),
    ("Up-down, Count Hits, LENGTH 16, 6 hits", {"note_count": 2, "arp_mode": MODES.index("Up-down"), "arp_len": 16,
                                                 "arp_density": 0.375},
     [R, None, None, T, None, None, F, None, T, None, None, R, None, None, T, None]),
    ("Loop, Shift 2", {"arp_mode": MODES.index("Loop"), "note_shift": 2}, [F, None, F, None, F, None, F, None]),
    ("Shift 4 (mod 3: 1)", {"note_shift": 4}, [T, None, R, None, F, None, T, None]),
]
for name, kw, want in cases:
    e = arp(**kw)
    hits, pred = played(e, 2 * int(kw.get("arp_len", 8)))
    n = int(kw.get("arp_len", 8))
    got = cycle(hits, n)
    second = cycle([(s, x) for i, (s, x) in enumerate(hits) if i >= len([w for w in want if w is not None])], n)
    check(got == want and second == want, f"arp {name}: {got} (want {want}, the second cycle {second})")
    check(pred == expect(want), f"   ...and the telemetry foresaw it: {pred}")
    e.close()

# ---- SEED: the same notes every LENGTH, another seed another pattern; Free never repeats
def seeded_run(seed, mode="Random", cycles=3, **kw):
    e = arp(length=8, density=1.0, mode=mode, pool="Key", arp_vary=kw.pop("vary", 0.0), seed=seed, **kw)
    hits, pred = played(e, 8 * cycles)
    e.close()
    notes = [x for _, x in hits]
    return [notes[8 * c:8 * c + 8] for c in range(cycles)], pred


runs = {s: seeded_run(s) for s in (0, 1, 2, 17)}
for s in (1, 2, 17):
    cyc, pred = runs[s]
    check(cyc[0] == cyc[1] == cyc[2] and len(set(cyc[0])) >= 3, f"Random, seed {s}: the same 8 notes every LENGTH: {cyc[0]}")
    check(pred == [BASE + x for x in cyc[0]], f"   ...foreseen by the telemetry: {[p - BASE for p in pred]}")
check(len({tuple(runs[s][0][0]) for s in (1, 2, 17)}) == 3, "Random: seeds 1, 2, 17 give three different patterns")
cyc, pred = runs[0]
check(cyc[0] != cyc[1] and cyc[1] != cyc[2], f"Random, Free: the cycles differ ({cyc[0]} / {cyc[1]})")
check(pred == [BASE + x for x in cyc[2]], f"   ...the telemetry cannot foresee them: it shows what each step last played: {pred}")
e = arp(length=8, density=1.0, mode="Random", pool="Key")
_, first = played(e, 3.5)
e.close()
check(first[:3] != [UNKNOWN] * 3 and first[4:] == [UNKNOWN] * 4, f"   ...and nothing (-2) where a step has not played yet: {first}")
cyc, pred = seeded_run(5, mode="Up", vary=0.5)
check(cyc[0] == cyc[1] == cyc[2] and cyc[0] != [0, 2, 3, 5, 7, 8, 10, 0], f"Up with VARY 0.5, seed 5: varied, the same every LENGTH: {cyc[0]}")
check(pred == [BASE + x for x in cyc[0]], "   ...foreseen by the telemetry")
cyc, pred = seeded_run(0, mode="Up", vary=0.5)
check(cyc[0] != cyc[1] or cyc[1] != cyc[2], "Up with VARY 0.5, Free: the cycles differ")
check(pred == [BASE + x for x in cyc[2]], "   ...unforeseen: the telemetry shows what played")
cyc, _ = seeded_run(5, mode="Random", note_shift=1)
check(cyc[0] == cyc[1], "Random, seed 5, Shift 1: still the same every LENGTH")


# RATCHET's dice: the same steps roll every LENGTH (hits counted per step on a fine block)
def ratchet_steps(seed):
    e = arp(length=8, density=1.0, arp_ratchet=0.5, seed=seed)
    _, _, fr = e.render(8 * 3 * STEP, block=32, collect=True)
    e.close()
    per = [0] * 24
    k = -1
    last = None
    for f in fr:
        c = f.ch[0]
        if c.step != last:
            k += 1
            last = c.step
        if c.hit and 0 <= k < 24:
            per[k] += 1
    return [per[8 * i:8 * i + 8] for i in range(3)]


r = ratchet_steps(9)
check(r[0] == r[1] == r[2] and max(r[0]) > 1 and min(r[0]) == 1, f"RATCHET 0.5, seed 9: the same steps roll every LENGTH: {r[0]}")
r = ratchet_steps(0)
check(not (r[0] == r[1] == r[2]), f"RATCHET 0.5, Free: the rolls move ({r[0]} / {r[1]} / {r[2]})")


# ---- ROTATE on Bass: the rhythm turns, the pattern starts on its first hit
def bass(rot, pattern="Root-5th"):
    e = arp()
    e.set("gen", GENS.index("Bass"), 0)
    e.set("bass_pattern", ["Root", "Root-5th", "Octaves", "Walk"].index(pattern), 0)
    e.set("rhy_rotate", rot, 0)
    hits, pred = played(e, 16)
    e.close()
    return cycle(hits), pred


got, pred = bass(0)
check(got == [R, None, F, None, R, None, F, None], f"bass Root-5th, rotate 0: {got}")
got, pred = bass(1)
check(got == [None, R, None, F, None, R, None, F], f"bass Root-5th, rotate 1: hits on 1 3 5 7, root first: {got}")
check(pred == expect(got), f"   ...foreseen: {pred}")
got, pred = bass(3, "Octaves")
check(got == [None, R, None, 12, None, R, None, 12], f"bass Octaves, rotate 3: {got}")


# ---- ROTATE on Chord: Steps strikes turn, Arp's notes count from the change (Auto) or follow the step
def chord(rhythm, **kw):
    e = arp()
    e.set("gen", GENS.index("Chord"), 0)
    e.set("chord_rhythm", rhythm, 0)
    e.set("chord_lead", 0, 0)
    e.set("amp_r", 0.0, 0)  # a 5 ms release: a note is over before the next
    for k, v in kw.items():
        e.set(k, v, 0)
    hits, pred = played(e, 16, base=BASE + 12, chord=rhythm == 2)  # a chord sits an octave above the arp
    e.close()
    return hits, pred


hits, pred = chord(1, rhy_rotate=1)
steps = sorted({s for s, _ in hits})
check(steps == [1, 3, 5, 7], f"chord Steps, rotate 1: strikes on {steps}")
check(pred == [REST, BASE + 12, REST, BASE + 12, REST, BASE + 12, REST, BASE + 12], f"   ...foreseen (the chord's root): {pred}")
hits, pred = chord(1)
check(sorted({s for s, _ in hits}) == [0, 2, 4, 6], "chord Steps, rotate 0: strikes on 0 2 4 6")
hits, pred = chord(2, rhy_rotate=1)
check(cycle(hits) == [None, R, None, T, None, F, None, R], f"chord Arp, rotate 1 (Auto: hits from the change): {cycle(hits)}")
check(pred == expect(cycle(hits), BASE + 12), f"   ...foreseen: {pred}")
hits, pred = chord(2, rhy_rotate=1, note_count=1)
check(cycle(hits) == [None, T, None, R, None, F, None, T], f"chord Arp, rotate 1, Count Steps: {cycle(hits)}")
check(pred == expect(cycle(hits), BASE + 12), f"   ...foreseen: {pred}")
hits, pred = chord(2, note_shift=1)
check(cycle(hits) == [T, None, F, None, R, None, T, None], f"chord Arp, Shift 1: {cycle(hits)}")
hits, pred = chord(2, note_count=2)
check(cycle(hits) == [R, None, T, None, F, None, R, None], f"chord Arp, Count Hits (as Auto): {cycle(hits)}")

# ---- Rhythm's ROTATE as it was: the pattern turned right, every hit where (p * hits) % len < hits
for length, density, rot in [(8, 0.375, 3), (16, 0.3125, 5), (12, 0.5, 13)]:
    e = arp(length=length, density=density)
    e.set("gen", GENS.index("Rhythm"), 0)
    e.set("rhy_rotate", rot, 0)
    hits, pred = played(e, length)
    e.close()
    k = int(density * length + 0.5)
    rr = rot % length
    want = [s for s in range(length) if (((s - rr) % length) * k) % length < k]
    got = sorted({s for s, _ in hits})
    check(got == want, f"rhythm LENGTH {length}, {k} hits, rotate {rot}: {got} (want {want})")
    check([s for s, n in enumerate(pred) if n != REST] == want, f"   ...foreseen: {pred}")

# CHANCE with a seed drops the same hits every LENGTH
e = arp(length=8, density=1.0, rhy_chance=0.5, seed=4)
e.set("gen", GENS.index("Rhythm"), 0)
_, _, fr = e.render(8 * 3 * STEP, collect=True)
on = [f.ch[0].step for f in fr if f.ch[0].hit]
pred = list(fr[-1].ch[0].stepNote)[:8]
e.close()
cyc = [on[i:i + len(on) // 3] for i in range(0, len(on), len(on) // 3)][:3]
check(len(on) % 3 == 0 and cyc[0] == cyc[1] == cyc[2] and 2 <= len(cyc[0]) <= 6,
      f"rhythm CHANCE 0.5, seed 4: the same hits every LENGTH: {cyc[0]}")
check([s for s, n in enumerate(pred) if n != REST] == cyc[0], f"   ...foreseen: {pred}")

# ---- a version 3 patch: ROTATE was the Rhythm's alone; another generator's leftover goes back to 0
text = "\n".join(["ammonite-desktop 3", f"{pid('gen', 0)} 1", f"{pid('rhy_rotate', 0)} 3", f"{pid('gen', 1)} 4",
                  f"{pid('rhy_rotate', 1)} 2", f"{pid('gen', 2)} 2", f"{pid('rhy_rotate', 2)} 5"]) + "\n"
e = Engine()
e.load(text)
check(e.get("rhy_rotate", 0) == 0 and e.get("rhy_rotate", 1) == 2 and e.get("rhy_rotate", 2) == 0,
      "a version 3 patch: rotate kept on the Rhythm channel, back to 0 on the Arp and Chord ones")
e.set("rhy_rotate", 3, 0)
e.set("note_shift", 2, 0)
e.set("note_count", 2, 0)
e.set("seed", 7, 0)
saved = e.save()
e2 = Engine()
e2.load(saved)
check(saved.startswith("ammonite-desktop 4") and e2.get("rhy_rotate", 0) == 3 and e2.get("note_shift", 0) == 2
      and e2.get("note_count", 0) == 2 and e2.get("seed", 0) == 7, "version 4 saves and loads ROTATE, SHIFT, COUNT, SEED")
check(all(e2.get(k, 5) == 0 for k in ("note_shift", "note_count", "seed")), "   ...and a channel without them loads the defaults")
e.close()
e2.close()

# ---- identical audio for any block size, with every new control on, every generator
def all_on(block):
    e = Engine(max_block=512)
    e.solo_arp(0, "Random", "1/16", length=12, pool="Key", rng=2, density=0.6, dec=0.4, sus=0.3, gate=0.6)
    e.set("swing", 0.4)
    e.set("prog", PROGS.index("Pop"))
    e.set("change", 0)
    for s, gen in enumerate(("Arp", "Bass", "Chord", "Rhythm")):
        if s:
            for k in ("arp_mode", "arp_div", "arp_len", "arp_pool", "arp_density", "arp_gate", "amp_d", "amp_s"):
                e.set(k, e.get(k, 0), s)
            e.set("level", 0.6, s)
        e.set("gen", GENS.index(gen), s)
        e.set("rhy_rotate", 3 + s, s)
        e.set("note_shift", 2, s)
        e.set("note_count", 2 if s % 2 else 1, s)
        e.set("seed", 11 + s, s)
        e.set("arp_vary", 0.3, s)
        e.set("arp_ratchet", 0.3, s)
    e.set("chord_rhythm", 2, 2)
    e.set("rhy_chance", 0.7, 3)
    L, Rr = e.render(6.0, block=block)
    e.close()
    return L, Rr


ref = all_on(512)
same = all(all_on(b)[0].tobytes() == ref[0].tobytes() for b in (37, 61, 128))
check(same and rms(ref[0]) > 0.01, "every new control on, four generators: identical audio for blocks of 512, 37, 61 and 128")
done()
