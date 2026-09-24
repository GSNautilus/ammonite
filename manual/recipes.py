"""
The manual's recipes: each is a list of knob moves from power-on.

A move is (page, section, knob, position): section is the section's index
(0 on pages without sections), knob is 1-10 as printed on the panel, and
position is the knob position 0..1. capture.py turns the knobs through the
panel like a player, records what the readout says (so the manual prints
exactly what the screen shows), and captures the screen as it plays.
"""


def st(k, n):
    """Knob position of step k of n (stepped functions)."""
    return (k + 0.5) / n


def cols(page, section, row, a, b, c):
    """One row of a column page for OSC 1, 2, 3 (row 0 = knobs 1-3)."""
    return [(page, section, row * 3 + 1 + o, v) for o, v in enumerate((a, b, c)) if v is not None]


RECIPES = [
    dict(
        name="tide",
        title="Slow tide",
        blurb="Three drones that slide from chord to chord under a slow, breathing "
              "filter. No arps at all: the progression does the moving.",
        view=2,  # WHEEL
        seconds=9.0,
        moves=[
            ("MAIN", 0, 6, 0.15),                       # TEMPO 70
            *cols("OSC", 0, 0, 0.25, 0.10, 0.0),        # SHAPE
            ("OSC", 0, 10, 0.6),                        # DETUNE 12 ct
            *cols("ARP", 0, 0, st(0, 6), st(0, 6), st(0, 6)),   # MODE OFF
            *cols("ENVELOPE", 2, 2, 1.0, 1.0, 1.0),     # GLIDE 1 s
            *cols("FILTER", 0, 0, 0.45, 0.50, 0.55),    # CUTOFF
            *cols("LFO", 1, 1, 0.35, 0.35, 0.35),       # CUTOFF LFO DEPTH
            *cols("MIX", 0, 1, 0.6, 0.6, 0.6),          # WIDTH
            *cols("MIX", 0, 2, 0.6, 0.6, 0.6),          # REV SEND
            ("MIX", 0, 10, 0.9),                        # SIZE
            ("KEY", 0, 1, st(3, 9)),                    # PROG SAD
            ("KEY", 0, 2, st(2, 4)),                    # CHANGE 2 BARS
        ],
    ),
    dict(
        name="clockwork",
        title="Clockwork",
        blurb="Three sixteenth-note patterns of 8, 7 and 5 steps, each with a few steps "
              "left out. They line up only once every 280 steps, so the groove keeps "
              "turning over.",
        view=0,  # RINGS
        seconds=7.0,
        moves=[
            ("MAIN", 0, 6, 0.40),                       # TEMPO 120
            *cols("ARP", 0, 0, st(2, 6), st(5, 6), st(4, 6)),   # UP RANDOM UP-DN
            *cols("ARP", 0, 1, st(4, 5), st(1, 5), st(0, 5)),   # ROOT TRIAD KEY
            *cols("ARP", 1, 0, st(6, 9), st(6, 9), st(6, 9)),   # 1/16
            *cols("ARP", 1, 1, st(7, 16), st(6, 16), st(4, 16)),  # LENGTH 8 7 5
            *cols("ARP", 2, 0, 0.625, 0.571, 0.6),      # DENSITY 5/8 4/7 3/5
            *cols("ENVELOPE", 0, 1, 0.51, 0.51, 0.51),  # DECAY 150 ms
            *cols("ENVELOPE", 0, 2, 0.0, None, None),   # SUSTAIN 0
            *cols("ENVELOPE", 1, 2, 0.75, 0.75, 0.75),  # FILTER AMOUNT +2 oct
            *cols("FILTER", 0, 0, 0.45, 0.45, 0.45),    # CUTOFF
            *cols("FILTER", 0, 1, 0.5, 0.5, 0.5),       # RESO
            *cols("DELAY", 0, 2, None, 0.35, None),     # SEND on OSC 2
        ],
    ),
    dict(
        name="dub",
        title="Dub echoes",
        blurb="A sparse, resonant chord stab on OSC 1 thrown into a long, darkening, "
              "wobbly ping-pong echo, over a sine bass that follows the chords.",
        view=1,  # PITCH
        seconds=8.0,
        moves=[
            ("MAIN", 0, 2, 0.0),                        # LEVEL 2 off
            ("MAIN", 0, 3, 0.55),                       # LEVEL 3
            ("MAIN", 0, 4, st(7, 12)),                  # ROOT G
            ("MAIN", 0, 6, 0.25),                       # TEMPO 90
            *cols("OSC", 0, 0, 0.5, None, 0.0),         # SHAPE square / sine
            *cols("OSC", 0, 1, st(2, 5), None, st(0, 5)),   # OCTAVE +1 / -1
            *cols("OSC", 0, 2, None, None, st(0, 8)),   # DEGREE 3 +0
            *cols("ARP", 0, 0, st(5, 6), None, st(0, 6)),   # RANDOM / OFF
            *cols("ARP", 1, 0, st(4, 9), None, None),   # DIVISION 1/8
            *cols("ARP", 1, 2, 0.2, None, None),        # GATE
            *cols("ARP", 2, 0, 0.375, None, None),      # DENSITY 3/8
            *cols("ENVELOPE", 0, 1, 0.55, None, None),  # DECAY 200 ms
            *cols("ENVELOPE", 0, 2, 0.0, None, None),   # SUSTAIN 0
            *cols("FILTER", 0, 0, 0.6, None, None),     # CUTOFF
            *cols("FILTER", 0, 1, 0.55, None, None),    # RESO
            *cols("FILTER", 0, 2, 0.5, None, None),     # TYPE BP
            *cols("DELAY", 0, 1, 0.7, None, None),      # FEEDBACK
            *cols("DELAY", 0, 2, 0.6, None, None),      # SEND
            *cols("DELAY", 1, 0, 0.35, None, None),     # TONE 1.5 kHz
            *cols("DELAY", 1, 1, 0.7, None, None),      # WOBBLE
            *cols("MIX", 0, 2, 0.3, None, None),        # REV SEND
            ("KEY", 0, 1, st(6, 9)),                    # PROG VAMP
            ("KEY", 0, 2, st(2, 4)),                    # CHANGE 2 BARS
        ],
    ),
    dict(
        name="jazz",
        title="Late set",
        blurb="A swung ii-V-I in C: a gliding bass on the chord roots, an arp through "
              "each seventh chord, and a looser triplet line on top.",
        view=1,  # PITCH
        seconds=8.0,
        moves=[
            ("MAIN", 0, 4, st(0, 12)),                  # ROOT C
            ("MAIN", 0, 5, st(0, 20)),                  # SCALE MAJOR
            ("MAIN", 0, 6, 0.28),                       # TEMPO 96
            ("MAIN", 0, 10, 0.5),                       # SWING
            *cols("OSC", 0, 0, 0.0, 0.3, 0.2),          # SHAPE
            *cols("OSC", 0, 2, None, st(0, 8), st(0, 8)),   # DEGREE 0
            *cols("ARP", 0, 0, st(0, 6), st(2, 6), st(5, 6)),   # OFF UP RANDOM
            *cols("ARP", 0, 1, None, st(2, 5), st(2, 5)),   # POOL 7TH
            *cols("ARP", 1, 0, None, st(4, 9), st(3, 9)),   # 1/8, 1/4T
            *cols("ARP", 2, 1, None, None, 0.3),        # VARY
            *cols("ENVELOPE", 0, 1, None, 0.61, None),  # DECAY 300 ms
            *cols("ENVELOPE", 0, 2, None, 0.2, None),   # SUSTAIN
            *cols("MIX", 0, 2, None, 0.35, 0.35),       # REV SEND
            ("KEY", 0, 1, st(5, 9)),                    # PROG JAZZ
        ],
    ),
    dict(
        name="bells",
        title="Bells in D",
        blurb="Pachelbel's chords in D major, played by sine bells: every note starts an "
              "octave high and drops into place in a few milliseconds, which is what "
              "makes it ring.",
        view=0,  # RINGS
        seconds=7.0,
        moves=[
            ("MAIN", 0, 4, st(2, 12)),                  # ROOT D
            ("MAIN", 0, 5, st(0, 20)),                  # SCALE MAJOR
            ("MAIN", 0, 6, 0.20),                       # TEMPO 80
            *cols("OSC", 0, 0, 0.3, 0.0, 0.0),          # SHAPE
            *cols("ARP", 0, 0, st(0, 6), st(2, 6), st(3, 6)),   # OFF UP DOWN
            *cols("ARP", 2, 0, None, None, 0.75),       # DENSITY 5/6
            *cols("ENVELOPE", 0, 1, None, 0.78, 0.78),  # DECAY 900 ms
            *cols("ENVELOPE", 2, 0, None, 1.0, 1.0),    # PITCH AMOUNT +12
            *cols("ENVELOPE", 2, 1, None, 0.1, 0.1),    # PITCH DECAY
            *cols("DELAY", 0, 2, None, None, 0.3),      # SEND
            *cols("MIX", 0, 2, None, 0.45, 0.45),       # REV SEND
            ("MIX", 0, 10, 0.85),                       # SIZE
            ("KEY", 0, 1, st(7, 9)),                    # PROG CANON
            ("KEY", 0, 2, st(0, 4)),                    # CHANGE 1/2 BAR
            ("KEY", 0, 4, st(2, 3)),                    # RESTART CHORD
        ],
    ),
]
