"""Shared helpers for the headless tests: load the DLL, drive the panel like
a player would (pages, sub-pages, pickup), render and measure audio."""
import ctypes
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
# SYNTH_DLL overrides the DLL (e.g. a copy built elsewhere while the
# simulator holds sim\synthcore.dll open).
DLL = ctypes.CDLL(os.environ.get("SYNTH_DLL") or os.path.join(HERE, "..", "sim", "synthcore.dll"))
DLL.synth_get_pot.restype = ctypes.c_float
DLL.synth_get_slot_value.restype = ctypes.c_float
DLL.synth_get_slot_name.restype = ctypes.c_char_p
DLL.synth_get_value_text.restype = ctypes.c_char_p
DLL.synth_get_page_name.restype = ctypes.c_char_p
DLL.synth_get_sub_name.restype = ctypes.c_char_p
DLL.synth_get_osc_note.restype = ctypes.c_float
F = ctypes.c_float
SR = 48000
SUBPOT, PAGEPOT, VOLPOT = 9, 10, 11

PAGES = ["MAIN", "OSC", "ARP", "ENVELOPE", "FILTER", "LFO", "DELAY", "MIX", "KEY"]
# enum Func in synth_core.cpp, same order (for synth_set_engine_param)
FUNCS = """LEVEL ROOT SCALE TEMPO MCUTOFF MDELAY MREVERB SWING SHAPE OCTAVE DEGREE DETUNE
AMODE APOOL ARANGE ADIV ALENGTH AGATE ADENSITY AVARY ARATCHET EAATT EADEC EASUS EFATT EFDEC
EFAMT EPAMT EPDEC GLIDE CUTOFF RESO FTYPE DRIVE LPITCH_R LPITCH_D LPITCH_W LCUT_R LCUT_D LCUT_W
LAMP_R LAMP_D LAMP_W LPAN_R LPAN_D LPAN_W LSHAPE_R LSHAPE_D LSHAPE_W DDIV DFEED DSEND DTONE
DWOBBLE DWIDTH PAN WIDTH RSEND RSIZE PROG CHANGE CHORD RESTART KOCT FINE LFOSYNC RDAMP RPRE
VIEW VOLUME""".split()
FN = {n: i for i, n in enumerate(FUNCS)}

fails = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)


def done():
    print(f"\n{len(fails)} failure(s)")
    for f in fails:
        print("  - " + f)
    raise SystemExit(1 if fails else 0)


# ---- panel
def pot(i, v): DLL.synth_set_pot(i, F(v))
def potpos(i): return float(DLL.synth_get_pot(i))
def slot(i): return round(float(DLL.synth_get_slot_value(i)), 4)
def name(i): return DLL.synth_get_slot_name(i).decode()
def osc_of(i): return DLL.synth_get_slot_osc(i)
def text(i): return DLL.synth_get_value_text(i).decode()
def cur_page(): return DLL.synth_get_page()
def page_name(p): return DLL.synth_get_page_name(p).decode()
def num_subs(p): return DLL.synth_get_num_subs(p)
def cur_sub(): return DLL.synth_get_sub(cur_page())
def sub_name(p, s): return DLL.synth_get_sub_name(p, s).decode()
def note(o): return float(DLL.synth_get_osc_note(o))
def engine(func, osc, v): DLL.synth_set_engine_param(FN[func], osc, F(v))


def setp(i, v):
    """Turn pot i to v like a hand would. Pickup ignores a pot that has not
    moved more than 0.02 since its page was chosen, so a pot already near v
    is moved away first."""
    if abs(potpos(i) - v) < 0.03:
        pot(i, v + 0.2 if v < 0.5 else v - 0.2)
    pot(i, v)


def page(p):
    """Select a page by index or name (knob 11)."""
    k = PAGES.index(p) if isinstance(p, str) else p
    pot(PAGEPOT, (k + 0.5) / len(PAGES))
    assert cur_page() == k


def sub(k):
    """Select sub-page k of the current page (knob 10)."""
    setp(SUBPOT, (k + 0.5) / num_subs(cur_page()))
    assert cur_sub() == k


def st(k, n):
    """Pot position of step k of n."""
    return (k + 0.5) / n


def fresh():
    """Re-init and give every pot its boot reading (the core's defaults)."""
    DLL.synth_init(F(SR))
    for i in range(12):
        pot(i, potpos(i))


# ---- audio
def render(seconds=1.0, warm=0.3):
    """Stereo audio after `warm` seconds: (L, R)."""
    buf = np.zeros(2 * 256, dtype=np.float32)
    for _ in range(int(warm * SR / 256)):
        DLL.synth_process(buf.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), 256)
    n = int(SR * seconds)
    out = np.zeros(2 * n, dtype=np.float32)
    k = 0
    while k < n:
        m = min(256, n - k)
        DLL.synth_process(out[2 * k:].ctypes.data_as(ctypes.POINTER(ctypes.c_float)), m)
        k += m
    return out[0::2].copy(), out[1::2].copy()


def spectrum(x):
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    return np.fft.rfftfreq(len(x), 1 / SR), sp


def peak_freq(x, lo=20, hi=4000):
    fr, sp = spectrum(x)
    band = (fr > lo) & (fr < hi)
    i = int(np.argmax(sp * band))
    a, b, c = np.log(sp[i - 1] + 1e-12), np.log(sp[i] + 1e-12), np.log(sp[i + 1] + 1e-12)
    d = 0.5 * (a - c) / (a - 2 * b + c)
    return float((i + d) * SR / len(x))


def band_level(x, f0, width=0.03):
    """Spectral magnitude around f0 (+-width relative)."""
    fr, sp = spectrum(x)
    m = (fr > f0 * (1 - width)) & (fr < f0 * (1 + width))
    return float(sp[m].max()) if m.any() else 0.0


def midi_hz(m): return 440.0 * 2 ** ((m - 69) / 12)
def hz_midi(f): return 69 + 12 * np.log2(f / 440.0)


def onsets(x, rel=0.3, gap=0.015):
    """Note starts (seconds): the first sample above rel * peak after at
    least `gap` seconds below it. Use a high pitch for ms precision (the
    first crossing can wait up to half a period)."""
    a = np.abs(x)
    idx = np.flatnonzero(a > rel * a.max())
    if len(idx) == 0:
        return np.array([])
    starts = [idx[0]] + [j for i, j in zip(idx[:-1], idx[1:]) if j - i > gap * SR]
    return np.array(starts) / SR


def rms(x): return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def drone():
    """All three arps OFF: the oscillators drone (envelopes bypassed)."""
    for k in range(3):
        engine("AMODE", k, 0.0)


def solo(o):
    """Only oscillator o sounds, dry (via the engine: tests the voice, not the panel)."""
    for k in range(3):
        engine("LEVEL", k, 0.8 if k == o else 0.0)
    engine("MREVERB", 0, 0.0)
    engine("MDELAY", 0, 0.0)
