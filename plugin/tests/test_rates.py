"""The plugin engine at a host's sample rates, and the host parameter setter.

core/ only runs up to ~52 kHz (ReverbSc's buffer); the plugin engine holds
its reverb, delay and pre-delay lines up to 192 kHz and runs without the
reverb above that instead of crashing. Pitch, delay time and reverb do not
depend on the rate. Run through plugin\\plugin.ps1 test.
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
from arpsynth import FN, check, done, midi_hz  # noqa: E402

F, H = ctypes.c_float, ctypes.c_void_p
D = ctypes.CDLL(os.path.abspath(os.environ.get("ENGINE_DLL") or os.path.join(BUILD, "ammonite_engine.dll")))
D.eng_create.restype = H
for n, args in {"eng_destroy": [H], "eng_init": [H, F], "eng_reverb_ok": [H],
                "eng_set_engine_param": [H, ctypes.c_int, ctypes.c_int, F],
                "eng_set_param": [H, ctypes.c_int, ctypes.c_int, F],
                "eng_get_param": [H, ctypes.c_int, ctypes.c_int],
                "eng_process": [H, ctypes.c_void_p, ctypes.c_int],
                "eng_get_osc_note": [H, ctypes.c_int]}.items():
    getattr(D, n).argtypes = args
D.eng_get_param.restype = F
D.eng_get_osc_note.restype = F


class Eng:
    def __init__(self, sr):
        self.h = H(D.eng_create())
        self.sr = sr
        D.eng_init(self.h, sr)

    def close(self): D.eng_destroy(self.h)
    def set(self, f, o, v): D.eng_set_engine_param(self.h, FN[f], o, v)

    def all(self, f, v):
        for o in range(3):
            self.set(f, o, v)

    def render(self, sec, block=256):
        n = int(sec * self.sr)
        out = np.zeros(2 * n, dtype=np.float32)
        k = 0
        while k < n:
            m = min(block, n - k)
            D.eng_process(self.h, out[2 * k:].ctypes.data_as(ctypes.c_void_p), m)
            k += m
        return out[0::2].copy(), out[1::2].copy()


def rms(x): return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def peak_hz(x, sr, lo=40, hi=2000):
    w = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / sr)
    band = (f > lo) & (f < hi)
    i = np.argmax(np.where(band, w, 0))
    # parabolic interpolation of the peak bin
    a, b, c = np.log(w[i - 1] + 1e-12), np.log(w[i] + 1e-12), np.log(w[i + 1] + 1e-12)
    return f[i] + 0.5 * (a - c) / (a - 2 * b + c) * (f[1] - f[0])


def drone(e):
    """osc 1 alone as a steady drone: MODE OFF, DETUNE 0."""
    e.all("AMODE", 0.0)
    e.set("LEVEL", 1, 0.0)
    e.set("LEVEL", 2, 0.0)
    e.set("DETUNE", 0, 0.0)


def click(e):
    """osc 1 alone, one short high note per bar (A5, 1 ms attack)."""
    e.all("AMODE", 0.0)
    e.set("AMODE", 0, 1.5 / 6)       # LOOP: the pool's first note, every step
    e.set("ADIV", 0, 0.5 / 9)        # 1/1: one step per bar
    e.set("APOOL", 0, 4.5 / 5)       # ROOT
    e.set("OCTAVE", 0, 3.5 / 5)      # +2
    e.set("EAATT", 0, 0.0)
    e.set("EADEC", 0, 0.1)
    e.set("EASUS", 0, 0.0)
    e.set("LEVEL", 1, 0.0)
    e.set("LEVEL", 2, 0.0)


RATES = [44100, 48000, 88200, 96000, 176400, 192000]
pitch, tails, echoes = {}, {}, {}
for sr in RATES:
    e = Eng(sr)
    check(D.eng_reverb_ok(e.h) == 1, f"{sr} Hz: the reverb fits")
    drone(e)
    l, r = e.render(1.5)
    pitch[sr] = peak_hz(l[int(0.5 * sr):], sr)
    check(np.all(np.isfinite(l)) and np.all(np.isfinite(r)) and rms(l) > 0.01,
          f"{sr} Hz: drone plays (rms {rms(l):.3f}), finite")
    e.close()

    # reverb: a note into a full send, then its tail
    e = Eng(sr)
    click(e)
    e.set("RSEND", 0, 1.0)
    e.set("RSIZE", 0, 0.75)
    l, _ = e.render(2.1)
    tails[sr] = rms(l[int(1.2 * sr):])  # the note is long gone (next bar: 2.18 s)
    e.close()

    # delay: DIVISION 1/4 (1 beat at 110 BPM = 545 ms), SEND 1, no feedback
    e = Eng(sr)
    click(e)
    e.set("DSEND", 0, 1.0)
    e.set("DFEED", 0, 0.0)
    e.set("DDIV", 0, 6.5 / 11)
    l, _ = e.render(2.0)
    env = np.abs(l)
    hit = int(np.argmax(env > 0.3 * env.max()))
    seg = env[hit + int(0.3 * sr):hit + int(1.0 * sr)]
    echoes[sr] = (hit + int(0.3 * sr) + int(np.argmax(seg > 0.3 * seg.max())) - hit) / sr
    e.close()

# the same without the reverb send: the window is silent
e = Eng(48000)
click(e)
dry = rms(e.render(2.1)[0][int(1.2 * 48000):])
e.close()

base = pitch[48000]
check(abs(base - midi_hz(45)) < 1.0, f"48 kHz drone on A2 ({base:.2f} Hz)")
check(all(abs(1200 * np.log2(pitch[s] / base)) < 2 for s in RATES),
      "same pitch at every rate (cents vs 48 kHz: "
      + ", ".join(f"{s // 1000}k {1200 * np.log2(pitch[s] / base):+.2f}" for s in RATES) + ")")
check(all(0.5 * tails[48000] < tails[s] < 2 * tails[48000] and tails[s] > 100 * dry for s in RATES),
      f"reverb tail at every rate, about as loud as at 48 kHz (rms, dry {dry:.1e}: "
      + ", ".join(f"{s // 1000}k {tails[s]:.1e}" for s in RATES) + ")")
check(all(abs(echoes[s] - 60 / 110) < 0.01 for s in RATES),
      "delay repeat after one beat (545 ms) at every rate ("
      + ", ".join(f"{s // 1000}k {1000 * echoes[s]:.0f} ms" for s in RATES) + ")")

# above 192 kHz: no reverb, but no crash either
e = Eng(384000)
e.all("RSEND", 1.0)
l, r = e.render(0.5)
check(D.eng_reverb_ok(e.h) == 0 and np.all(np.isfinite(l)) and rms(l) > 0.01,
      "384 kHz: runs without the reverb (it does not fit), finite, plays")
e.close()

# the host switching rates: Init again, at a higher and back at a lower rate
ref = Eng(48000)
want = ref.render(1.0)[0]
ref.close()
e = Eng(48000)
e.render(0.3)
D.eng_init(e.h, 192000)
e.sr = 192000
hi = e.render(0.5)[0]
D.eng_init(e.h, 48000)
e.sr = 48000
got = e.render(1.0)[0]
e.close()
check(np.all(np.isfinite(hi)) and rms(hi) > 0.01 and got.tobytes() == want.tobytes(),
      "Init 48 -> 192 -> 48 kHz: plays at 192, then exactly like a fresh 48 kHz engine")

# SetParam (a host parameter) glides over ~20 ms like a pot; SetEngineParam jumps
e = Eng(48000)
drone(e)
e.render(0.5)
D.eng_set_param(e.h, FN["VOLUME"], 0, 0.0)
check(D.eng_get_param(e.h, FN["VOLUME"], 0) == 0.0, "SetParam: GetParam reads it back")
l, _ = e.render(0.2, block=48)
first, late = rms(l[:48]), rms(l[int(0.15 * 48000):])
check(first > 0.01 and late < 1e-4,
      f"SetParam VOLUME 0: the first block still sounds ({first:.3f}), 150 ms later silent ({late:.1e})")
e.close()

done()
