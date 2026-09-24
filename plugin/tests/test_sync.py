"""SYNC: the plugin engine on the host's clock (SetHostClock), or on TEMPO.

DAW (the default): the host's tempo; while its transport plays, its
position, so steps and chords sit on the DAW's grid and follow the
playhead; stopped, it runs on at the host tempo. FREE, or no host clock:
the TEMPO parameter, exactly as the hardware. Run through
plugin\\plugin.ps1 test.
"""
import ctypes
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BUILD = os.path.join(ROOT, "plugin", "build")
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.environ.setdefault("SYNTH_DLL", os.path.join(BUILD, "ammonite_engine.dll"))
from arpsynth import FN, FUNCS, check, done, onsets  # noqa: E402

F, H, Dbl = ctypes.c_float, ctypes.c_void_p, ctypes.c_double
D = ctypes.CDLL(os.path.abspath(os.environ.get("ENGINE_DLL") or os.path.join(BUILD, "ammonite_engine.dll")))
D.eng_create.restype = H
for n, args in {"eng_destroy": [H], "eng_init": [H, F],
                "eng_set_engine_param": [H, ctypes.c_int, ctypes.c_int, F],
                "eng_process": [H, ctypes.c_void_p, ctypes.c_int],
                "eng_set_host_clock": [H, ctypes.c_int, Dbl, Dbl],
                "eng_get_beat": [H], "eng_get_chord_numeral": [H]}.items():
    getattr(D, n).argtypes = args
D.eng_get_beat.restype = Dbl
D.eng_get_chord_numeral.restype = ctypes.c_char_p
SYNC = len(FUNCS)  # plugin only: the first function after the hardware's
SR, BLOCK = 48000, 256


class Eng:
    def __init__(self):
        self.h = H(D.eng_create())
        D.eng_init(self.h, SR)

    def close(self): D.eng_destroy(self.h)
    def set(self, f, o, v): D.eng_set_engine_param(self.h, FN[f] if isinstance(f, str) else f, o, v)
    def beat(self): return D.eng_get_beat(self.h)
    def numeral(self): return D.eng_get_chord_numeral(self.h).decode()

    def render(self, sec, host=None):
        """host = (playing, bpm, beat at the start) or None (no host clock).
        The host position advances with the audio, like a DAW's."""
        n = int(sec * SR)
        out = np.zeros(2 * n, dtype=np.float32)
        k = 0
        while k < n:
            m = min(BLOCK, n - k)
            if host is not None:
                playing, bpm, b = host
                D.eng_set_host_clock(self.h, int(playing), bpm, b + (bpm / 60.0 / SR) * k)
            D.eng_process(self.h, out[2 * k:].ctypes.data_as(ctypes.c_void_p), m)
            k += m
        return out[0::2].copy()


def ticker():
    """osc 1 alone, a short high note on every quarter note (1/4, LOOP)."""
    e = Eng()
    for o in range(3):
        e.set("AMODE", o, 0.0)
    e.set("AMODE", 0, 1.5 / 6)   # LOOP
    e.set("ADIV", 0, 2.5 / 9)    # 1/4
    e.set("APOOL", 0, 4.5 / 5)   # ROOT
    e.set("OCTAVE", 0, 3.5 / 5)  # +2
    e.set("EAATT", 0, 0.0)
    e.set("EADEC", 0, 0.1)
    e.set("EASUS", 0, 0.0)
    e.set("LEVEL", 1, 0.0)
    e.set("LEVEL", 2, 0.0)
    return e


def grid(ts, t0, period):
    """Largest distance (ms) of each onset from t0 + k * period."""
    k = np.round((ts - t0) / period)
    return float(np.max(np.abs(ts - (t0 + k * period)))) * 1000


# ---- the default is DAW; without a host clock it is the TEMPO parameter
e = ticker()
check(D.eng_get_beat(e.h) == 0.0, "a new engine starts at beat 0")
e.close()

# FREE and "DAW without a host" give the very same audio
a, b = ticker(), ticker()
a.set(SYNC, 0, 0.25)  # FREE
xa, xb = a.render(2.0), b.render(2.0)
check(xa.tobytes() == xb.tobytes(), "SYNC DAW without a host clock = FREE, bit for bit (the TEMPO parameter)")
t = onsets(xa)
check(abs(np.median(np.diff(t)) - 60 / 110) < 0.002, f"FREE: a quarter note every {1000 * np.median(np.diff(t)):.1f} ms (TEMPO 110 BPM: 545.5)")
a.close()
b.close()

# ---- DAW, playing: the host tempo and position rule
e = ticker()
x = e.render(3.0, host=(True, 90.0, 0.0))
t = onsets(x)
check(abs(t[0]) < 0.002 and abs(np.median(np.diff(t)) - 60 / 90) < 0.002 and grid(t, 0.0, 60 / 90) < 2.0,
      f"DAW playing from bar 1 at 90 BPM: first note at {1000 * t[0]:.1f} ms, then every"
      f" {1000 * np.median(np.diff(t)):.1f} ms (666.7), max {grid(t, 0.0, 60 / 90):.2f} ms off the host grid")
check(abs(e.beat() - 3.0 * 1.5) < 1e-6, f"the clock is the host's: beat {e.beat():.4f} after 3 s at 90 BPM (4.5)")
e.close()

# starting mid-beat: the step under the playhead plays at once, then the grid
e = ticker()
x = e.render(2.0, host=(True, 120.0, 2.5))  # half a beat before beat 3
t = onsets(x)
check(abs(t[0]) < 0.002 and abs(t[1] - 0.25) < 0.002 and grid(t[1:], 0.25, 0.5) < 2.0,
      f"DAW from beat 2.5: a note at once ({1000 * t[0]:.1f} ms), the next on beat 3"
      f" ({1000 * t[1]:.1f} ms, want 250), then on the beat")
e.close()

# relocating the playhead (a loop back, a jump): the grid follows at once
e = ticker()
x1 = e.render(1.0, host=(True, 120.0, 0.0))
x2 = e.render(1.2, host=(True, 120.0, 16.25))  # jump to bar 5, a quarter beat in
t = onsets(np.concatenate([x1, x2]))
after = t[t >= 1.0] - 1.0
check(abs(after[0]) < 0.006 and abs(after[1] - 0.375) < 0.002 and grid(after[1:], 0.375, 0.5) < 2.0,
      f"jump to beat 16.25: a note at the jump ({1000 * after[0]:.1f} ms), then on beat 17"
      f" ({1000 * after[1]:.1f} ms, want 375) and on")
check(abs(e.beat() - (16.25 + 1.2 * 2)) < 1e-3, f"clock after the jump: {e.beat():.3f} (18.65)")
e.close()

# ---- the progression sits on the DAW's bars: POP, a chord per bar
e = ticker()
e.set("PROG", 0, 1.5 / 9)    # POP  I V vi IV
e.set("CHANGE", 0, 1.5 / 4)  # 1 BAR
seen = []
for bar in range(6):
    e.render(0.01, host=(True, 120.0, 4.0 * bar + 0.5))
    seen.append(e.numeral())
check(len(set(seen[:4])) == 4 and seen[4] == seen[0] and seen[5] == seen[1],
      f"PROG POP on the DAW's bars 1-6: {seen} (four chords, then again)")
# the same bar gives the same chord however the playhead got there
e.render(0.01, host=(True, 120.0, 8.5))
back = e.numeral()
e.close()
f = ticker()
f.set("PROG", 0, 1.5 / 9)
f.set("CHANGE", 0, 1.5 / 4)
f.render(0.01, host=(True, 120.0, 8.5))
check(back == seen[2] == f.numeral(), f"bar 3 is {seen[2]!r} whether played into, jumped back to or started at")
f.close()

# ---- DAW, stopped: runs on at the host tempo, from where it is
e = ticker()
e.render(1.0, host=(True, 120.0, 0.0))  # playing: beat 2.0 after 1 s
x = e.render(2.0, host=(False, 150.0, 99.0))  # stopped at 150 BPM (its position ignored)
t = onsets(x)
check(abs(np.median(np.diff(t)) - 0.4) < 0.002 and abs(e.beat() - (2.0 + 2.0 * 2.5)) < 1e-3,
      f"DAW stopped: plays on at the host's 150 BPM (every {1000 * np.median(np.diff(t)):.1f} ms),"
      f" continuing from beat 2 (now {e.beat():.3f}, want 7)")
# ... and when the host starts playing, jumps to the host's position
e.render(0.01, host=(True, 150.0, 0.0))
check(abs(e.beat() - 0.025) < 1e-3, f"play again from bar 1: the clock jumps to the host ({e.beat():.3f})")
e.close()

# ---- FREE ignores the host clock
e = ticker()
e.set(SYNC, 0, 0.25)
x = e.render(2.0, host=(True, 90.0, 40.0))
t = onsets(x)
check(abs(np.median(np.diff(t)) - 60 / 110) < 0.002 and abs(e.beat() - 2.0 * 110 / 60) < 1e-3,
      "FREE with a host playing at 90 BPM, bar 11: TEMPO's 110 BPM from beat 0, as before")
e.close()

# ---- the delays take the host tempo too: DIVISION 1/4 at 90 BPM = 667 ms
e = ticker()
e.set("ADIV", 0, 0.5 / 9)    # 1/1: one note per bar
e.set("DSEND", 0, 1.0)
e.set("DFEED", 0, 0.0)
e.set("DDIV", 0, 6.5 / 11)   # 1/4
x = e.render(2.0, host=(True, 90.0, 0.0))
env = np.abs(x)
seg = env[int(0.3 * SR):int(1.5 * SR)]
echo = 0.3 + np.argmax(seg > 0.3 * seg.max()) / SR
check(abs(echo - 60 / 90) < 0.01, f"delay repeat after one host beat: {1000 * echo:.0f} ms (667)")
e.close()

done()
