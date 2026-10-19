"""ctypes binding of ammonite_engine.dll (desktop/engine/api.h) plus the
analysis helpers the engine tests share: spectra, pitch, onsets, levels.
Style of the hardware tests (tests/arpsynth.py)."""
import ctypes as C
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.abspath(os.path.join(HERE, "..", "..", "..", "build"))
DLL_PATH = os.environ.get("AMMO_ENGINE_DLL") or os.path.join(BUILD, "cmake", "bin", "ammonite_engine.dll")

SLOTS, WAVE_LEN = 16, 128
GLOBAL_IDS, SLOT_STRIDE = 64, 128
SR = 48000
# the modulation pool (engine/Params.hpp): 32 LFOs of 8 ids, the macros' in-use flags and values 5..16, 128 routes of 4
POOL_BASE, NUM_IDS = 2112, 3136
POOL_LFOS, POOL_MACROS, POOL_ROUTES = 32, 16, 128
LFO_BASE, LFO_STRIDE = 2112, 8
MACRO_ON_BASE, MACRO_VAL_BASE = 2368, 2384
ROUTE_BASE, ROUTE_STRIDE = 2400, 4
LFO_KEYS = ["on", "shape", "sync", "rate", "div", "phase", "restart"]
ROUTE_KEYS = ["src", "target", "amt", "pol"]
SRC_LFO1, SRC_MACRO1 = 1, 33  # a route's source: 0 off, 1..32 LFO 1..32, 33..48 Macro 1..16


def lfo_id(l, key):
    """Id of pool LFO l's (0-based) setting by name (LFO_KEYS)."""
    return LFO_BASE + LFO_STRIDE * l + LFO_KEYS.index(key)


def route_id(r, key):
    """Id of pool route r's (0-based) field by name (ROUTE_KEYS)."""
    return ROUTE_BASE + ROUTE_STRIDE * r + ROUTE_KEYS.index(key)


def macro_id(m):
    """Id of macro m's (0-based) value: macros 1..4 are global ids 11..14."""
    return 11 + m if m < 4 else MACRO_VAL_BASE + m

fails = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        fails.append(msg)


def done():
    print(f"\n{len(fails)} failure(s)")
    for f in fails:
        print("  - " + f)
    raise SystemExit(1 if fails else 0)


class AmmoClock(C.Structure):
    _fields_ = [("valid", C.c_int), ("playing", C.c_int), ("bpm", C.c_double), ("beat", C.c_double)]


class AmmoParamInfo(C.Structure):
    _fields_ = [("id", C.c_int), ("name", C.c_char * 48), ("symbol", C.c_char * 32), ("group", C.c_char * 16),
                ("min", C.c_float), ("max", C.c_float), ("default", C.c_float), ("steps", C.c_int), ("flags", C.c_uint32)]


class AmmoChannelFrame(C.Structure):
    _fields_ = [("active", C.c_int), ("muted", C.c_int), ("gen", C.c_int), ("note", C.c_float), ("pitchClass", C.c_int), ("env", C.c_float),
                ("rms", C.c_float), ("peak", C.c_float), ("hit", C.c_int), ("hitSeed", C.c_uint32), ("step", C.c_int),
                ("length", C.c_int), ("stepNote", C.c_int8 * 16), ("voices", C.c_int), ("voiceNote", C.c_float * 6), ("duck", C.c_float), ("wave", C.c_float * WAVE_LEN)]


class AmmoFrame(C.Structure):
    _fields_ = [("seq", C.c_uint32), ("beat", C.c_double), ("bpm", C.c_double), ("playing", C.c_int), ("hostClock", C.c_int),
                ("chordRootPc", C.c_int), ("chordDeg", C.c_int), ("chordQuality", C.c_int), ("progPos", C.c_int),
                ("progLen", C.c_int), ("keyMask", C.c_int), ("chordMask", C.c_int), ("instance", C.c_int),
                ("sampleRate", C.c_double), ("block", C.c_uint32), ("masterRms", C.c_float), ("masterPeak", C.c_float), ("glueGr", C.c_float), ("limitGr", C.c_float),
                ("ch", AmmoChannelFrame * SLOTS), ("lfoRun", C.c_uint32), ("lfo", C.c_float * POOL_LFOS),
                ("route", C.c_float * POOL_ROUTES), ("cpu", C.c_float)]


if not os.path.exists(DLL_PATH):
    print(f"skip: {DLL_PATH} not built (.\\desktop\\desktop.ps1 build)")
    raise SystemExit(0)

DLL = C.CDLL(DLL_PATH)
DLL.ammo_create.restype = C.c_void_p
DLL.ammo_create.argtypes = [C.c_double, C.c_int]
DLL.ammo_destroy.argtypes = [C.c_void_p]
DLL.ammo_param_count.restype = C.c_int
DLL.ammo_param_info.argtypes = [C.c_int, C.POINTER(AmmoParamInfo)]
DLL.ammo_param_info_id.argtypes = [C.c_int, C.POINTER(AmmoParamInfo)]
DLL.ammo_param_step_name.argtypes = [C.c_int, C.c_int, C.c_char_p, C.c_int]
DLL.ammo_set_param.argtypes = [C.c_void_p, C.c_int, C.c_float]
DLL.ammo_get_param.restype = C.c_float
DLL.ammo_get_param.argtypes = [C.c_void_p, C.c_int]
DLL.ammo_process.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_int, C.POINTER(AmmoClock)]
DLL.ammo_read_frame.argtypes = [C.c_void_p, C.POINTER(AmmoFrame)]
DLL.ammo_save.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
DLL.ammo_load.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
DLL.ammo_version.restype = C.c_char_p
DLL.ammo_latency.restype = C.c_int
DLL.ammo_latency.argtypes = [C.c_void_p]

# ---- the parameter table, by symbol
PARAMS = {}
INFOS = []
for i in range(DLL.ammo_param_count()):
    pi = AmmoParamInfo()
    DLL.ammo_param_info(i, C.byref(pi))
    INFOS.append(pi)
    PARAMS[pi.symbol.decode()] = pi

GLOBAL = {p.symbol.decode(): p.id for p in INFOS if p.group == b"global"}
POOL = {p.symbol.decode(): p.id for p in INFOS if p.group == b"pool"}  # the host's part of it: no routes
SLOT_PARAMS = {}  # bare symbol -> offset in the slot block
for p in INFOS:
    if p.group.startswith(b"slot 1") and p.group == b"slot 1":
        sym = p.symbol.decode().split("_", 1)[1]
        SLOT_PARAMS[sym] = p.id - GLOBAL_IDS


def info_id(id_):
    """Any id's definition (hidden ones and the routes too), or None."""
    pi = AmmoParamInfo()
    return pi if DLL.ammo_param_info_id(id_, C.byref(pi)) else None


def pid(name, slot=None):
    """Id of a global parameter by symbol, or of a slot parameter (0-based slot)."""
    if slot is None:
        return GLOBAL[name]
    return GLOBAL_IDS + SLOT_STRIDE * slot + SLOT_PARAMS[name]


def step_name(id_, step):
    buf = C.create_string_buffer(64)
    return buf.value.decode() if DLL.ammo_param_step_name(id_, step, buf, 64) else None


# ---- names of stepped values (indices)
MODES = ["Off", "Loop", "Up", "Down", "Up-down", "Random"]
POOLS = ["Key", "Triad", "7th", "Fifths", "Root"]
DIVS = ["4/1", "2/1", "1/1", "1/2.", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32"]
DIV_BEATS = [16, 8, 4, 3, 2, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.25, 1 / 6, 0.125]
SCALES = ["Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Harmonic minor", "Pentatonic",
          "Pentatonic minor", "Blues", "Maj", "Min", "Sus2", "Sus4", "Maj7", "Min7", "Dom7", "Dim", "Aug", "Add9"]
PROGS = ["Off", "Pop", "50s", "Sad", "Epic", "Jazz", "Vamp", "Canon", "Fall", "Custom"]
MIDI_BASE = 36
LFO_SHAPES = ["Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Drift"]
LFO_DIVS = ["16/1", "8/1", "4/1", "2/1", "1/1", "1/2.", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32"]


class Engine:
    def __init__(self, sr=SR, max_block=512):
        self.sr = sr
        self.p = DLL.ammo_create(float(sr), max_block)
        self.max_block = max_block

    def latency(self):
        """Samples the output lags the music (the master limiter's look-ahead)."""
        return int(DLL.ammo_latency(self.p))

    def close(self):
        if self.p:
            DLL.ammo_destroy(self.p)
            self.p = None

    def set(self, name, value, slot=None):
        DLL.ammo_set_param(self.p, pid(name, slot), float(value))

    def get(self, name, slot=None):
        return float(DLL.ammo_get_param(self.p, pid(name, slot)))

    def set_id(self, id_, value):
        DLL.ammo_set_param(self.p, id_, float(value))

    def get_id(self, id_):
        return float(DLL.ammo_get_param(self.p, id_))

    def lfo(self, l, shape="Sine", sync="Tempo", rate=0.5, div="1/4", phase=0.0, restart=0, on=1):
        """Pool LFO l (0-based): in use, its settings (LFO_SHAPES, LFO_DIVS; restart 0 Free, n the notes of channel n)."""
        for k, v in (("on", on), ("shape", LFO_SHAPES.index(shape)), ("sync", 1 if sync == "Tempo" else 0), ("rate", rate),
                     ("div", LFO_DIVS.index(div)), ("phase", phase), ("restart", restart)):
            self.set_id(lfo_id(l, k), v)

    def route(self, r, src, target, amt, pol=1):
        """Pool route r (0-based): source (0 off, SRC_LFO1 + l, SRC_MACRO1 + m), target id (-1 none), amount, polarity
        (1 Bipolar, 0 Unipolar)."""
        for k, v in (("src", src), ("target", target), ("amt", amt), ("pol", pol)):
            self.set_id(route_id(r, k), v)

    def all_off(self):
        """Every slot's generator off: silence, a clean start for one voice."""
        for s in range(SLOTS):
            self.set("gen", 0, s)

    def solo_arp(self, slot=0, mode="Up", div="1/8", length=16, pool="Key", rng=1, density=1.0, gate=0.5,
                 sus=0.0, dec=0.3, att=0.0, octave=1, degree=0, shape=0.0, cutoff=1.0, detune=0.0, bpm=120,
                 level=1.0):
        """One slot alone: a clean pluck with its arp set."""
        self.all_off()
        self.set("sync", 1)  # internal clock
        self.set("tempo", bpm)
        self.set("swing", 0)
        self.set("prog", 0)
        self.set("chord", 0)
        self.set("restart", 0)
        self.set("octave", 0)
        self.set("volume", 1.0)
        self.set("glue", 0)  # the master compressor off: the voice measured as it is (the limiter stays)
        s = slot
        self.set("gen", 1, s)
        self.set("mute", 0, s)
        self.set("level", level, s)
        self.set("pan", 0, s)
        self.set("arp_mode", MODES.index(mode), s)
        self.set("arp_div", DIVS.index(div), s)
        self.set("arp_len", length, s)
        self.set("arp_pool", POOLS.index(pool), s)
        self.set("arp_range", rng, s)
        self.set("arp_density", density, s)
        self.set("arp_gate", gate, s)
        self.set("arp_vary", 0, s)
        self.set("arp_ratchet", 0, s)
        self.set("degree", degree, s)
        self.set("oct", octave, s)
        self.set("shape", shape, s)
        self.set("detune", detune, s)
        self.set("sub", 0, s)
        self.set("noise", 0, s)
        self.set("cutoff", cutoff, s)
        self.set("reso", 0, s)
        self.set("filter_type", 0, s)
        self.set("drive", 0, s)
        self.set("amp_a", att, s)
        self.set("amp_d", dec, s)
        self.set("amp_s", sus, s)
        self.set("amp_r", dec, s)
        self.set("filt_amt", 0, s)
        self.set("pitch_amt", 0, s)
        self.set("glide", 0, s)

    def render(self, seconds, clock=None, block=None, collect=False):
        """Stereo audio (L, R). clock = (playing, bpm, beat at the start):
        a host clock advancing with the audio; None = none at all.
        collect = True also returns every telemetry frame (read block by
        block: the ring only holds the last 32)."""
        n = int(seconds * self.sr)
        L = np.zeros(n, dtype=np.float32)
        R = np.zeros(n, dtype=np.float32)
        blk = block or self.max_block
        frames = []
        k = 0
        while k < n:
            m = min(blk, n - k)
            cl = None
            if clock is not None:
                playing, bpm, b0 = clock
                cl = AmmoClock(1, 1 if playing else 0, bpm, b0 + (bpm / 60.0 / self.sr) * k if playing else b0)
            DLL.ammo_process(self.p, L[k:].ctypes.data_as(C.POINTER(C.c_float)),
                             R[k:].ctypes.data_as(C.POINTER(C.c_float)), m, C.byref(cl) if cl is not None else None)
            if collect:
                frames.extend(self.frames())
            k += m
        return (L, R, frames) if collect else (L, R)

    def warm(self, seconds=0.2, clock=None):
        self.render(seconds, clock)

    def frames(self):
        """Every telemetry frame waiting."""
        out = []
        f = AmmoFrame()
        while DLL.ammo_read_frame(self.p, C.byref(f)):
            out.append(AmmoFrame.from_buffer_copy(f))
        return out

    def save(self):
        n = DLL.ammo_save(self.p, None, 0)
        buf = C.create_string_buffer(n + 1)
        DLL.ammo_save(self.p, buf, n + 1)
        return buf.value.decode()

    def load(self, text):
        b = text.encode()
        return bool(DLL.ammo_load(self.p, b, len(b)))


# ---- analysis
def rms(x):
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def spectrum(x, sr=SR):
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    return np.fft.rfftfreq(len(x), 1 / sr), sp


def peak_freq(x, lo=20, hi=4000, sr=SR):
    fr, sp = spectrum(x, sr)
    band = (fr > lo) & (fr < hi)
    i = int(np.argmax(sp * band))
    if i <= 0 or i >= len(sp) - 1:
        return float(fr[i])
    a, b, c = np.log(sp[i - 1] + 1e-12), np.log(sp[i] + 1e-12), np.log(sp[i + 1] + 1e-12)
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
    return float((i + d) * sr / len(x))


def band_level(x, f0, width=0.03, sr=SR):
    fr, sp = spectrum(x, sr)
    m = (fr > f0 * (1 - width)) & (fr < f0 * (1 + width))
    return float(sp[m].max()) if m.any() else 0.0


def hf_ratio(x, cut, sr=SR):
    fr, sp = spectrum(x, sr)
    e = sp ** 2
    tot = float(np.sum(e[1:]))
    return float(np.sum(e[fr > cut]) / tot) if tot > 0 else 0.0


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def hz_midi(f):
    return 69 + 12 * np.log2(f / 440.0)


def onsets(x, rel=0.3, gap=0.015, sr=SR):
    """Note starts (seconds): the first sample above rel * peak after at
    least `gap` seconds below it."""
    a = np.abs(x)
    if a.max() <= 0:
        return np.array([])
    idx = np.flatnonzero(a > rel * a.max())
    if len(idx) == 0:
        return np.array([])
    starts = [idx[0]] + [j for i, j in zip(idx[:-1], idx[1:]) if j - i > gap * sr]
    return np.array(starts) / sr


def loudness(L, R, sr=SR):
    """A level in the spirit of BS.1770 (dB, K-weighted, no gating): a 2nd-order high-pass at 38 Hz and a shelf
    of +4 dB above 2 kHz, applied in the spectrum, the power of both sides summed."""
    out = 0.0
    for x in (L, R):
        sp = np.fft.rfft(x.astype(np.float64))
        f = np.fft.rfftfreq(len(x), 1 / sr)
        hp = (f / 38.0) ** 4 / (1 + (f / 38.0) ** 4)
        sh = 1 + (10 ** 0.4 - 1) * (f / 2000.0) ** 2 / (1 + (f / 2000.0) ** 2)
        out += np.sum(np.abs(sp) ** 2 * hp * sh) / len(x) ** 2 * 2
    return float(10 * np.log10(out + 1e-20))


def env_frames(x, ms=5, sr=SR):
    n = int(sr * ms / 1000)
    m = len(x) // n
    return np.sqrt(np.mean(x[:m * n].astype(np.float64).reshape(m, n) ** 2, axis=1)), n / sr


def step_pitches(x, step_sec, n_steps, sr=SR, base=None, head=0.03, tail=0.02):
    """Pitch (MIDI, rounded) of each step, read in the step's middle."""
    out = []
    for k in range(n_steps):
        a, b = int((k * step_sec + head) * sr), int(((k + 1) * step_sec - tail) * sr)
        seg = x[a:b]
        if len(seg) < 64 or rms(seg) < 1e-4:
            out.append(None)
            continue
        m = hz_midi(peak_freq(seg, 30, 6000, sr))
        out.append(int(round(m - base)) if base is not None else int(round(m)))
    return out
