"""Panel framework: 9 pages, column layout, sub-pages, pickup, value texts,
page / sub-page map frames."""
import ctypes

import numpy as np

from arpsynth import *

fresh()

# ---- pages and sub-pages
check([page_name(p) for p in range(9)] == PAGES, "nine pages in order")
check([num_subs(p) for p in range(9)] == [1, 1, 3, 3, 1, 5, 2, 1, 1], "sub-page counts")
check([sub_name(2, s) for s in range(3)] == ["NOTES", "RHYTHM", "CHANCE"], "ARP sub-pages")
check([sub_name(3, s) for s in range(3)] == ["AMP", "FILTER", "PITCH"], "ENVELOPE sub-pages")
check([sub_name(5, s) for s in range(5)] == ["PITCH", "CUTOFF", "AMP", "PAN", "SHAPE"],
      "LFO sub-pages = targets")
check([sub_name(6, s) for s in range(2)] == ["TIME", "COLOR"], "DELAY sub-pages")
for p in range(9):
    page(p)
check(cur_page() == 8, "every page reachable")

# ---- MAIN (a global page)
page("MAIN")
check([name(i) for i in range(12)] == ["LEVEL", "LEVEL", "LEVEL", "ROOT", "SCALE", "TEMPO",
                                        "CUTOFF", "DELAY", "REVERB", "SWING", "PAGE", "VOLUME"],
      "MAIN knob names")
check([osc_of(i) for i in range(12)] == [1, 2, 3] + [0] * 9, "MAIN: LEVEL per osc, rest global")
check((text(3), text(4), text(5)) == ("A", "MINOR", "110 BPM"), "MAIN defaults: A MINOR 110 BPM")

# ---- column pages: one function per row, osc = column
rows = {
    ("OSC", 0): ["SHAPE", "OCTAVE", "DEGREE", "DETUNE"],
    ("ARP", 0): ["MODE", "POOL", "RANGE", "SECTION"],
    ("ARP", 1): ["DIVISION", "LENGTH", "GATE", "SECTION"],
    ("ARP", 2): ["DENSITY", "VARY", "RATCHET", "SECTION"],
    ("ENVELOPE", 0): ["ATTACK", "DECAY", "SUSTAIN", "SECTION"],
    ("ENVELOPE", 1): ["ATTACK", "DECAY", "AMOUNT", "SECTION"],
    ("ENVELOPE", 2): ["AMOUNT", "DECAY", "GLIDE", "SECTION"],
    ("FILTER", 0): ["CUTOFF", "RESO", "TYPE", "DRIVE"],
    ("LFO", 3): ["RATE", "DEPTH", "WAVE", "TARGET"],
    ("DELAY", 0): ["DIVISION", "FEEDBACK", "SEND", "SECTION"],
    ("DELAY", 1): ["TONE", "WOBBLE", "WIDTH", "SECTION"],
    ("MIX", 0): ["PAN", "WIDTH", "REV SEND", "SIZE"],
}
for (pg, sb), want in rows.items():
    page(pg)
    if num_subs(cur_page()) > 1:
        sub(sb)
    got = [name(r * 3) for r in range(3)] + [name(9)]
    same = all(name(r * 3 + c) == name(r * 3) for r in range(3) for c in range(3))
    oscs = [osc_of(i) for i in range(9)] == [1, 2, 3] * 3
    check(got == want and same and oscs, f"{pg}/{sb}: rows {got}, columns = osc 1-3")
page("KEY")
check([name(i) for i in range(10)] == ["PROG", "CHANGE", "CHORD", "RESTART", "OCTAVE", "FINE",
                                        "LFO SYNC", "DAMPING", "PREDELAY", "VIEW"], "KEY knob names")
check((text(0), text(2), text(9)) == ("OFF", "I", "PITCH"), "KEY defaults: PROG OFF, CHORD I, VIEW PITCH")

# ---- pickup across pages
fresh()
page("OSC")
setp(1, 0.9)                                   # SHAPE osc 2
check(slot(1) == 0.9, "OSC: knob 2 sets SHAPE 2")
page("FILTER")
check(abs(slot(1) - 0.642) < 1e-3, "FILTER: knob 2 shows CUTOFF 2, not the pot position")
pot(1, 0.91)                                   # a nudge inside the pickup margin
check(abs(slot(1) - 0.642) < 1e-3, "a nudge < 0.02 does not pick up")
pot(1, 0.95)
check(slot(1) == 0.95, "a real move picks up CUTOFF 2")
page("OSC")
check(slot(1) == 0.9, "back on OSC, SHAPE 2 kept its value")

# ---- sub-pages: selector on knob 10, pickup re-armed on a sub change
fresh()
page("LFO")
check(name(9) == "TARGET" and text(9) == "PITCH", "LFO: knob 10 = TARGET, starts on PITCH")
setp(0, 0.8)                                   # PITCH RATE osc 1
sub(1)
check(text(9) == "CUTOFF" and name(0) == "RATE", "knob 10 moves to the CUTOFF target")
check(abs(slot(0) - 0.218) < 1e-3, "CUTOFF RATE 1 untouched by the knob left at 0.8")
pot(0, 0.81)
check(abs(slot(0) - 0.218) < 1e-3, "sub-page change re-arms pickup")
pot(0, 0.5)
check(slot(0) == 0.5, "moved: CUTOFF RATE 1 = 0.5")
sub(0)
check(slot(0) == 0.8, "PITCH RATE 1 still 0.8")
page("ARP")
check(cur_sub() == 0, "each page keeps its own sub-page (ARP starts on NOTES)")
page("LFO")
check(cur_sub() == 0 and text(9) == "PITCH", "LFO remembers its sub-page")

# ---- columns only touch their oscillator (arps OFF: steady notes)
fresh()
drone()
render(0.05, 0)
before = [note(o) for o in range(3)]
check(before == [45.0, 60.0, 76.0], f"default notes A2 C4 E5 (A minor triad), got {before}")
page("OSC")
setp(4, st(3, 5))                              # OCTAVE osc 2: +1 -> +2
render(0.05, 0)
after = [note(o) for o in range(3)]
check(after == [45.0, 72.0, 76.0], f"OCTAVE 2 +2 moves only osc 2, got {after}")
check(text(4) == "+2", "OCTAVE readout +2")

# ---- value texts
fresh()
page("ARP")
check((text(0), text(1), text(2)) == ("UP", "UP-DN", "RANDOM"), "ARP MODE defaults")
check((text(3), text(4), text(5)) == ("FIFTHS", "TRIAD", "KEY"), "ARP POOL defaults")
sub(1)
check((text(0), text(1), text(2), text(3), text(5)) == ("1/4", "1/8", "1/16", "8", "6"),
      "ARP RHYTHM: DIVISION 1/4 1/8 1/16, LENGTH 8 8 6")
page("FILTER")
check(text(0) == "1.2 KHZ" and text(6) == "LP", f"FILTER texts ({text(0)}, {text(6)})")
page("MIX")
check((text(0), text(1), text(2)) == ("C", "L40", "R40"), "MIX PAN texts")

# ---- selector hysteresis: noise at a page boundary does not flip pages
fresh()
b = 2 / 9                                      # boundary between OSC and ARP
pot(PAGEPOT, b - 0.02)
flips = 0
last = cur_page()
for k in range(50):
    pot(PAGEPOT, b + (0.006 if k % 2 else -0.006))
    flips += cur_page() != last
    last = cur_page()
check(flips == 0, "page selector ignores noise at a boundary")

# ---- screens: page map, sub map and readout all draw
fb = np.zeros(240 * 240, dtype=np.uint16)


def frame():
    DLL.synth_render(fb.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)))
    return fb.copy()


def lit(f, y0, y1):
    return int((f.reshape(240, 240)[y0:y1] != 0).sum())


fresh()
render(0.1, 0)
page("OSC")
f = frame()
check(lit(f, 20, 45) > 50 and lit(f, 75, 180) > 200, "page map: title and rows drawn")
page("LFO")
setp(SUBPOT, st(1, 5))
f = frame()
check(lit(f, 50, 72) > 50 and lit(f, 198, 214) > 20, "sub map: sub name and dots drawn")
setp(0, 0.3)
f = frame()
check(lit(f, 100, 140) > 100 and lit(f, 78, 94) > 20, "readout: name and 'CUTOFF  OSC 1' line drawn")

done()
