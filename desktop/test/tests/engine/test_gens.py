"""The chord and bass generators (phase 4): the chord's notes (read from the
telemetry, which reports every voice, and heard in the audio), NOTES,
VOICING, smooth voice leading against fixed, Hold coming in mid-chord,
Steps, STRUM, Arp (the note order from the root, one voice at a time, Let ring
building the chord to the change, on the grid to the sample); the bass's patterns (Root, Root-5th, Octaves, Walk leading
into the next chord); identical audio for any block size; switching
generators leaves no voice behind."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH}")
GENS = ["Off", "Arp", "Chord", "Bass"]
VOICINGS = ["Close", "Open", "Drop 2", "Spread"]
BASS = ["Root", "Root-5th", "Octaves", "Walk"]
CHORD_BASE = MIDI_BASE + 12 + 9  # A3: a chord at octave 0 in A


def solo_chord(e, notes=0, voicing="Close", smooth=0, rhythm=0, strum=0.0, octave=0, **kw):
    e.solo_arp(0, "Up", kw.pop("div", "1/8"), octave=octave, sus=1.0, dec=0.5, gate=kw.pop("gate", 1.0), **kw)
    e.set("root", 9)
    e.set("scale", SCALES.index("Minor"))
    e.set("gen", GENS.index("Chord"), 0)
    e.set("chord_notes", notes, 0)
    e.set("chord_voicing", VOICINGS.index(voicing), 0)
    e.set("chord_lead", smooth, 0)
    e.set("chord_rhythm", rhythm, 0)
    e.set("chord_strum", strum, 0)


def chord_now(e, seconds=0.3):
    """The notes the channel's voices are on, relative to A3, after `seconds`."""
    _, _, fr = e.render(seconds, collect=True)
    c = fr[-1].ch[0]
    return sorted(int(round(c.voiceNote[k] - CHORD_BASE)) for k in range(c.voices))


# ---- NOTES: triad, 7th, 9th of A minor's i chord
for notes, want in [(0, [0, 3, 7]), (1, [0, 3, 7, 10]), (2, [0, 3, 7, 10, 14])]:
    e = Engine()
    solo_chord(e, notes=notes)
    got = chord_now(e)
    check(got == want, f"chord notes {['Triad', '7th', '9th'][notes]}: {got} (want {want})")
    e.close()

# ...and they are what we hear: each chord tone well above a note not in the chord
e = Engine()
solo_chord(e)
L, _ = e.render(1.0)
seg = L[int(0.2 * SR):]
on = [band_level(seg, midi_hz(CHORD_BASE + k), 0.01) for k in (0, 3, 7)]
off = [band_level(seg, midi_hz(CHORD_BASE + k), 0.01) for k in (1, 5, 10)]
check(min(on) > 8 * max(off), f"heard: A C E each > 8x the levels at A# D G ({min(on):.1f} vs {max(off):.2f})")
e.close()

# ---- VOICING (Fixed: root position)
for voicing, want in [("Open", [0, 7, 15]), ("Drop 2", [-9, 0, 7]), ("Spread", [-12, 3, 19])]:
    e = Engine()
    solo_chord(e, voicing=voicing)
    got = chord_now(e)
    check(got == want, f"voicing {voicing}: {got} (want {want})")
    e.close()


# ---- voice leading: through a progression, Smooth moves far less than Fixed and stays home
def lead_path(smooth):
    e = Engine()
    solo_chord(e, notes=1, smooth=smooth)
    e.set("prog", PROGS.index("Canon"))
    e.set("change", 0)  # every half bar: 1 s at 120 BPM
    path = []
    for k in range(16):
        path.append(chord_now(e, 1.0))
    e.close()
    return path


def movement(path):
    return sum(sum(abs(a - b) for a, b in zip(p, q)) for p, q in zip(path, path[1:]))


fixed, smooth = lead_path(0), lead_path(1)
centres = [np.mean(p) for p in smooth]
check(movement(smooth) < 0.5 * movement(fixed),
      f"Smooth voice leading moves {movement(smooth)} semitones over 15 changes, Fixed {movement(fixed)}")
check(max(centres) - min(centres) <= 9, f"Smooth stays in its register: centres {min(centres):.1f} .. {max(centres):.1f}")
check(all(len(p) == 4 for p in smooth), "every chord of the progression has its four notes")

# ---- Hold: one strike per chord change, and it comes in mid-chord at once
e = Engine()
solo_chord(e)
e.set("prog", PROGS.index("Pop"))
e.set("sync", 0)  # the host's clock
L, _, fr = e.render(4.0, clock=(True, 120, 1.5), collect=True)  # starts mid-bar
check(fr[0].ch[0].voices == 3, f"Hold comes in at once mid-chord: {fr[0].ch[0].voices} voices in the first block")
hits = sum(f.ch[0].hit for f in fr)
check(hits == 3, f"Hold: strikes only at chord changes ({hits} blocks with a strike in 4 s: "
      "the joined chord, then the changes at beats 4 and 8)")
e.close()

# ---- Steps: struck on the grid
e = Engine()
solo_chord(e, rhythm=1, div="1/4", gate=0.5)
L, _ = e.render(2.0)
on = onsets(L, rel=0.5, gap=0.1)
check(len(on) == 4 and np.allclose(np.diff(on), 0.5, atol=0.01), f"Steps at 1/4: 4 strikes 0.5 s apart: {np.round(on, 3)}")
e.close()

# ---- STRUM: the notes roll up 40 ms apart at full strum, low to high
e = Engine()
solo_chord(e, strum=1.0)
_, _, fr = e.render(0.3, block=32, collect=True)
t_first = {}
for i, f in enumerate(fr):
    c = f.ch[0]
    for k in range(c.voices):
        t_first.setdefault(int(round(c.voiceNote[k] - CHORD_BASE)), i * 32 / SR)
times = [t_first.get(k) for k in (0, 3, 7)]
ok = all(t is not None for t in times) and abs((times[1] - times[0]) - 0.04) < 0.003 and abs((times[2] - times[1]) - 0.04) < 0.003
check(ok, f"strum: A, C, E start 40 ms apart: {[round(t, 3) if t is not None else None for t in times]}")
e.close()


# ---- RHYTHM Arp: the voiced chord one note per step, from the root; Let ring builds it up to the change
def solo_chord_arp(e, mode="Up", ring=0, div="1/8", gate=0.5, **kw):
    solo_chord(e, rhythm=2, div=div, gate=gate, **kw)
    e.set("arp_mode", MODES.index(mode), 0)
    e.set("chord_ring", ring, 0)
    e.set("amp_r", 0.0, 0)  # a 5 ms release: a note is over before the next


def arp_notes(e, seconds):
    """The note each strike starts, in order (one per step), relative to A3."""
    _, _, fr = e.render(seconds, collect=True)
    out = []
    for f in fr:
        c = f.ch[0]
        if c.hit:
            out.append([int(round(c.voiceNote[k] - CHORD_BASE)) for k in range(c.voices)])
    return out, fr


# a bar is 2 s at 120 BPM: eight eighths, and the count starts again on the root at the bar (the window)
for mode, voicing, want in [("Up", "Close", [0, 3, 7, 0, 3, 7, 0, 3, 0, 3]), ("Down", "Close", [0, 7, 3, 0, 7, 3, 0, 7, 0, 7]),
                            ("Up-down", "Close", [0, 3, 7, 3, 0, 3, 7, 3, 0, 3]), ("Up", "Drop 2", [0, 7, -9, 0, 7, -9, 0, 7, 0, 7])]:
    e = Engine()
    solo_chord_arp(e, mode, voicing=voicing)
    got, fr = arp_notes(e, 2.5)
    one = all(len(g) == 1 for g in got) and max(f.ch[0].voices for f in fr) == 1
    got = [g[0] if len(g) == 1 else g for g in got]
    check(got == want and one, f"chord Arp {mode} ({voicing}): {got} (want {want}: the root first, round the voicing, "
          f"from the root again at the bar), one voice at a time")
    e.close()

# Let ring: a 7th at quarters over Pop, a bar per chord: the voices build 1 2 3 4, and at the change all four end
e = Engine()
solo_chord_arp(e, "Up", ring=1, div="1/4", notes=1)
e.set("prog", PROGS.index("Pop"))
_, _, fr = e.render(4.5, collect=True)
counts = [fr[int(t * SR / 512)].ch[0].voices for t in np.arange(0.25, 4.5, 0.5)]
check(counts == [1, 2, 3, 4, 1, 2, 3, 4, 1], f"Let ring: voices {counts} a quarter apart (want 1 2 3 4, released at the change, again)")
e.close()

# on the grid to the sample: sixteenths at 97 BPM (7422.68 samples each), one block per sample
e = Engine(max_block=1)
solo_chord_arp(e, "Up", div="1/16", bpm=97)
_, _, fr = e.render(1.0, block=1, collect=True)
hits = [i for i, f in enumerate(fr) if f.ch[0].hit]
want = [int(np.ceil(k * 0.25 * 60 / 97 * SR - 1e-6)) for k in range(len(hits))]
check(hits == want and len(hits) == 7, f"chord Arp sixteenths at 97 BPM start on the grid's samples: {hits}")
e.close()


# ---- bass patterns (octave 1: A3 = 57)
def solo_bass(e, pattern, div="1/8", **kw):
    e.solo_arp(0, "Up", div, octave=1, sus=1.0, dec=0.5, gate=1.0, **kw)
    e.set("root", 9)
    e.set("scale", SCALES.index("Minor"))
    e.set("gen", GENS.index("Bass"), 0)
    e.set("bass_pattern", BASS.index(pattern), 0)


BASS_BASE = MIDI_BASE + 9 + 12
for pattern, want in [("Root", [0, 0, 0, 0]), ("Root-5th", [0, 7, 0, 7]), ("Octaves", [0, 12, 0, 12])]:
    e = Engine()
    solo_bass(e, pattern)
    L, _ = e.render(4 * STEP if (STEP := 0.25) else 1)
    got = step_pitches(L, 0.25, 4, base=BASS_BASE)
    check(got == want, f"bass {pattern}: {got} (want {want})")
    e.close()

# Walk over Pop (i v VI iv in A minor), a bar per chord at 1/4: root, third, fifth, then a half step under E
e = Engine()
solo_bass(e, "Walk", div="1/4")
e.set("prog", PROGS.index("Pop"))
L, _ = e.render(8 * 0.5)
got = step_pitches(L, 0.5, 8, base=BASS_BASE)
check(got[:4] == [0, 3, 7, 6], f"Walk, bar 1 (A minor into E minor): {got[:4]} (want A C E then D# = 0 3 7 6)")
check(got[4] == 7, f"Walk lands on the next root, the note it led to: {got[4]} (want E = 7)")
check(got[7] in (7, 9), f"Walk, bar 2 into F (VI): {got[7]} (a half step next to F = 8)")
e.close()

# ---- the same audio for any block size
for name, setup in [("chord", lambda e: (solo_chord(e, notes=1, smooth=1, rhythm=1, strum=0.6, div="1/8"),
                                         e.set("prog", PROGS.index("Pop")))),
                    ("chord Arp, let ring, ratchet and vary", lambda e: (
                        solo_chord(e, notes=2, voicing="Open", smooth=1, rhythm=2, div="1/16"), e.set("prog", PROGS.index("Canon")),
                        e.set("arp_mode", MODES.index("Up-down"), 0), e.set("chord_ring", 1, 0), e.set("arp_ratchet", 0.3, 0),
                        e.set("arp_vary", 0.2, 0), e.set("arp_density", 0.7, 0))),
                    ("bass", lambda e: (solo_bass(e, "Walk", div="1/8"), e.set("prog", PROGS.index("Jazz"))))]:
    outs = []
    for blk in (37, 512):
        e = Engine(max_block=blk)
        setup(e)
        L, _ = e.render(6.0, block=blk)
        outs.append(L)
        e.close()
    check(np.array_equal(outs[0], outs[1]), f"{name}: identical audio at 37 and 512 samples per block")

# ---- OCTAVE spans -4..+4 on top of the master's -2..+2; past C0 or C9 the notes fold back by octaves
for octave, master, want in [(-1, 0, [-12, -9, -5]), (-4, -2, [21, 24, 28]), (4, 2, [117, 120, 124])]:
    e = Engine()
    solo_chord(e, octave=octave)
    e.set("octave", master)
    got = chord_now(e)
    absolute = [g + CHORD_BASE for g in got]
    if octave == -1:
        check(got == want, f"chord at octave -1: {got} (want {want}, an octave below home)")
    elif master < 0:
        check(absolute == want, f"chord at octave -4, master -2 (A-3 before folding): {absolute} (want A0 C1 E1 = {want}, voicing kept)")
    else:
        check(max(absolute) <= 120 and np.diff(absolute).tolist() == [3, 4],
              f"chord at octave +4, master +2 folds under C9: {absolute} (its voicing kept)")
    e.close()
e = Engine()
solo_bass(e, "Root")
e.set("oct", -4, 0)
e.set("octave", -2)
_, _, fr = e.render(0.3, collect=True)
low = fr[-1].ch[0].voiceNote[0]
check(low == MIDI_BASE + 9 - 72 + 48, f"bass at octave -4, master -2 folds up to A0: {low:.0f} (want {MIDI_BASE + 9 - 72 + 48})")
e.close()

# ---- switching generators: the old notes end
e = Engine()
solo_chord(e, notes=2)
chord_now(e, 0.5)
e.set("gen", GENS.index("Bass"), 0)
e.set("bass_pattern", 0, 0)
e.set("amp_r", 0.1, 0)
_, _, fr = e.render(1.0, collect=True)
check(fr[-1].ch[0].voices == 1, f"chord -> bass: the chord's voices end ({fr[-1].ch[0].voices} sounding after 1 s)")
e.close()

done()
