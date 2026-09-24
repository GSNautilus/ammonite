"""The built plugin, loaded like a DAW loads it: a minimal CLAP host in
ctypes drives plugin/build/cmake/bin/Ammonite.clap. Checks what a host
relies on: the descriptor, the parameter list, playing at several sample
rates, automation events, two instances side by side, save / load state,
and SYNC DAW following the host transport (tempo, position, stop).

Needs .\\plugin\\plugin.ps1 build first (skipped otherwise). The VST3 is
the same DSP code behind DPF's VST3 wrapper; it is checked in the DAWs.
"""
import ctypes as C
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BUILD = os.path.join(ROOT, "plugin", "build")
CLAP = os.path.abspath(os.environ.get("CLAP_PATH") or os.path.join(BUILD, "cmake", "bin", "Ammonite.clap"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.environ.setdefault("SYNTH_DLL", os.path.join(BUILD, "ammonite_engine.dll"))
from arpsynth import check, done, onsets  # noqa: E402

if not os.path.exists(CLAP):
    print(f"skip: {CLAP} not built (.\\plugin\\plugin.ps1 build)")
    raise SystemExit(0)


# ------------------------------------------------------------- CLAP ABI
class Version(C.Structure):
    _fields_ = [("major", C.c_uint32), ("minor", C.c_uint32), ("revision", C.c_uint32)]


class Descriptor(C.Structure):
    _fields_ = [("clap_version", Version), ("id", C.c_char_p), ("name", C.c_char_p),
                ("vendor", C.c_char_p), ("url", C.c_char_p), ("manual_url", C.c_char_p),
                ("support_url", C.c_char_p), ("version", C.c_char_p),
                ("description", C.c_char_p), ("features", C.POINTER(C.c_char_p))]


class Host(C.Structure):
    pass


HOST_GET_EXT = C.CFUNCTYPE(C.c_void_p, C.POINTER(Host), C.c_char_p)
HOST_REQ = C.CFUNCTYPE(None, C.POINTER(Host))
Host._fields_ = [("clap_version", Version), ("host_data", C.c_void_p), ("name", C.c_char_p),
                 ("vendor", C.c_char_p), ("url", C.c_char_p), ("version", C.c_char_p),
                 ("get_extension", HOST_GET_EXT), ("request_restart", HOST_REQ),
                 ("request_process", HOST_REQ), ("request_callback", HOST_REQ)]


class AudioBuffer(C.Structure):
    _fields_ = [("data32", C.POINTER(C.POINTER(C.c_float))), ("data64", C.c_void_p),
                ("channel_count", C.c_uint32), ("latency", C.c_uint32), ("constant_mask", C.c_uint64)]


class EventHeader(C.Structure):
    _fields_ = [("size", C.c_uint32), ("time", C.c_int32), ("space_id", C.c_uint16),
                ("type", C.c_uint16), ("flags", C.c_uint32)]


class ParamValueEvent(C.Structure):
    _fields_ = [("header", EventHeader), ("param_id", C.c_uint32), ("cookie", C.c_void_p),
                ("note_id", C.c_int32), ("port_index", C.c_int16), ("channel", C.c_int16),
                ("key", C.c_int16), ("value", C.c_double)]


class Transport(C.Structure):
    _fields_ = [("header", EventHeader), ("flags", C.c_uint32), ("song_pos_beats", C.c_int64),
                ("song_pos_seconds", C.c_int64), ("tempo", C.c_double), ("tempo_inc", C.c_double),
                ("loop_start_beats", C.c_int64), ("loop_end_beats", C.c_int64),
                ("loop_start_seconds", C.c_int64), ("loop_end_seconds", C.c_int64),
                ("bar_start", C.c_int64), ("bar_number", C.c_int32), ("tsig_num", C.c_uint16),
                ("tsig_denom", C.c_uint16)]


CLAP_EVENT_TRANSPORT = 9
HAS_TEMPO, HAS_BEATS, HAS_TSIG, IS_PLAYING = 1, 2, 8, 16
BEATTIME = 1 << 31


class InEvents(C.Structure):
    pass


IN_SIZE = C.CFUNCTYPE(C.c_uint32, C.POINTER(InEvents))
IN_GET = C.CFUNCTYPE(C.c_void_p, C.POINTER(InEvents), C.c_uint32)
InEvents._fields_ = [("ctx", C.c_void_p), ("size", IN_SIZE), ("get", IN_GET)]


class OutEvents(C.Structure):
    pass


OUT_PUSH = C.CFUNCTYPE(C.c_bool, C.POINTER(OutEvents), C.c_void_p)
OutEvents._fields_ = [("ctx", C.c_void_p), ("try_push", OUT_PUSH)]


class Process(C.Structure):
    _fields_ = [("steady_time", C.c_int64), ("frames_count", C.c_uint32), ("transport", C.c_void_p),
                ("audio_inputs", C.c_void_p), ("audio_outputs", C.POINTER(AudioBuffer)),
                ("audio_inputs_count", C.c_uint32), ("audio_outputs_count", C.c_uint32),
                ("in_events", C.POINTER(InEvents)), ("out_events", C.POINTER(OutEvents))]


class Plugin(C.Structure):
    pass


PP = C.POINTER(Plugin)
Plugin._fields_ = [("desc", C.POINTER(Descriptor)), ("plugin_data", C.c_void_p),
                   ("init", C.CFUNCTYPE(C.c_bool, PP)), ("destroy", C.CFUNCTYPE(None, PP)),
                   ("activate", C.CFUNCTYPE(C.c_bool, PP, C.c_double, C.c_uint32, C.c_uint32)),
                   ("deactivate", C.CFUNCTYPE(None, PP)),
                   ("start_processing", C.CFUNCTYPE(C.c_bool, PP)),
                   ("stop_processing", C.CFUNCTYPE(None, PP)), ("reset", C.CFUNCTYPE(None, PP)),
                   ("process", C.CFUNCTYPE(C.c_int32, PP, C.POINTER(Process))),
                   ("get_extension", C.CFUNCTYPE(C.c_void_p, PP, C.c_char_p)),
                   ("on_main_thread", C.CFUNCTYPE(None, PP))]


class Factory(C.Structure):
    pass


PF = C.POINTER(Factory)
Factory._fields_ = [("get_plugin_count", C.CFUNCTYPE(C.c_uint32, PF)),
                    ("get_plugin_descriptor", C.CFUNCTYPE(C.POINTER(Descriptor), PF, C.c_uint32)),
                    ("create_plugin", C.CFUNCTYPE(PP, PF, C.POINTER(Host), C.c_char_p))]


class Entry(C.Structure):
    _fields_ = [("clap_version", Version), ("init", C.CFUNCTYPE(C.c_bool, C.c_char_p)),
                ("deinit", C.CFUNCTYPE(None)), ("get_factory", C.CFUNCTYPE(C.c_void_p, C.c_char_p))]


class ParamInfo(C.Structure):
    _fields_ = [("id", C.c_uint32), ("flags", C.c_uint32), ("cookie", C.c_void_p),
                ("name", C.c_char * 256), ("module", C.c_char * 1024), ("min_value", C.c_double),
                ("max_value", C.c_double), ("default_value", C.c_double)]


class Params(C.Structure):
    _fields_ = [("count", C.CFUNCTYPE(C.c_uint32, PP)),
                ("get_info", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.POINTER(ParamInfo))),
                ("get_value", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.POINTER(C.c_double))),
                ("value_to_text", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.c_double, C.c_char_p, C.c_uint32)),
                ("text_to_value", C.c_void_p),
                ("flush", C.CFUNCTYPE(None, PP, C.POINTER(InEvents), C.POINTER(OutEvents)))]


class OStream(C.Structure):
    pass


class IStream(C.Structure):
    pass


OS_WRITE = C.CFUNCTYPE(C.c_int64, C.POINTER(OStream), C.c_void_p, C.c_uint64)
IS_READ = C.CFUNCTYPE(C.c_int64, C.POINTER(IStream), C.c_void_p, C.c_uint64)
OStream._fields_ = [("ctx", C.c_void_p), ("write", OS_WRITE)]
IStream._fields_ = [("ctx", C.c_void_p), ("read", IS_READ)]


class State(C.Structure):
    _fields_ = [("save", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(OStream))),
                ("load", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(IStream)))]


CLAP_EVENT_PARAM_VALUE = 5

# ------------------------------------------------------------ the host
host_cb = [HOST_GET_EXT(lambda h, i: None), HOST_REQ(lambda h: None)]
host = Host(Version(1, 2, 0), None, b"ammonite-test", b"GSNautilus", b"", b"0.1",
            host_cb[0], host_cb[1], host_cb[1], host_cb[1])

lib = C.CDLL(CLAP)
entry = Entry.in_dll(lib, "clap_entry")
check(entry.init(CLAP.encode()), "clap_entry.init")
factory = C.cast(entry.get_factory(b"clap.plugin-factory"), PF)
check(factory.contents.get_plugin_count(factory) == 1, "one plugin in the factory")
desc = factory.contents.get_plugin_descriptor(factory, 0).contents
feats = []
i = 0
while desc.features[i]:
    feats.append(desc.features[i].decode())
    i += 1
check(desc.id == b"com.gsnautilus.ammonite" and desc.name == b"Ammonite"
      and desc.vendor == b"GSNautilus" and "instrument" in feats,
      f"descriptor: {desc.id.decode()}, {desc.name.decode()} by {desc.vendor.decode()},"
      f" version {desc.version.decode()}, features {feats}")

EMPTY_IN_CB = (IN_SIZE(lambda l: 0), IN_GET(lambda l, i: None))
OUT_CB = OUT_PUSH(lambda l, e: True)
out_events = OutEvents(None, OUT_CB)


class Inst:
    def __init__(self):
        self.p = factory.contents.create_plugin(factory, C.byref(host), b"com.gsnautilus.ammonite")
        assert self.p and self.p.contents.init(self.p)
        self.params = C.cast(self.p.contents.get_extension(self.p, b"clap.params"), C.POINTER(Params))
        self.state = C.cast(self.p.contents.get_extension(self.p, b"clap.state"), C.POINTER(State))
        self.steady = 0
        self.pending = []

    def call(self, name, *a):
        return getattr(self.p.contents, name)(self.p, *a)

    def activate(self, sr, maxf=512):
        self.sr = sr
        self.maxf = maxf
        ok = self.call("activate", sr, 1, maxf)
        return ok and self.call("start_processing")

    def deactivate(self):
        self.call("stop_processing")
        self.call("deactivate")

    def set_param(self, pid, value):
        """Queued: sent with the next process() call, at its first sample."""
        self.pending.append((pid, value))

    def render(self, sec, sizes=(512, 256, 100, 37, 512), host=None):
        """host = (playing, bpm, beat at the start, beats per bar): a CLAP
        transport on every block, advancing with the audio."""
        n = int(sec * self.sr)
        L = np.zeros(n, dtype=np.float32)
        R = np.zeros(n, dtype=np.float32)
        k, j = 0, 0
        while k < n:
            m = min(sizes[j % len(sizes)], n - k, self.maxf)
            j += 1
            bl = np.zeros(m, dtype=np.float32)
            br = np.zeros(m, dtype=np.float32)
            chans = (C.POINTER(C.c_float) * 2)(bl.ctypes.data_as(C.POINTER(C.c_float)),
                                                br.ctypes.data_as(C.POINTER(C.c_float)))
            buf = AudioBuffer(chans, None, 2, 0, 0)
            evs = [ParamValueEvent(EventHeader(C.sizeof(ParamValueEvent), 0, 0, CLAP_EVENT_PARAM_VALUE, 0),
                                   pid, None, -1, -1, -1, -1, val) for pid, val in self.pending]
            self.pending = []
            cb = (IN_SIZE(lambda l: len(evs)), IN_GET(lambda l, i: C.addressof(evs[i])))
            ins = InEvents(None, cb[0], cb[1])
            tr = None
            if host is not None:
                playing, bpm, b0, num = host
                beat = b0 + bpm / 60.0 / self.sr * k
                bar = int(beat // num)
                tr = Transport(EventHeader(C.sizeof(Transport), 0, 0, CLAP_EVENT_TRANSPORT, 0),
                               HAS_TEMPO | HAS_BEATS | HAS_TSIG | (IS_PLAYING if playing else 0),
                               int(round(beat * BEATTIME)), 0, bpm, 0.0, 0, 0, 0, 0,
                               bar * num * BEATTIME, bar, num, 4)
            proc = Process(self.steady, m, C.addressof(tr) if tr is not None else None, None,
                           C.pointer(buf), 0, 1, C.pointer(ins), C.pointer(out_events))
            self.call("process", C.byref(proc))
            L[k:k + m], R[k:k + m] = bl, br
            k += m
            self.steady += m
        return L, R

    def value(self, pid):
        v = C.c_double()
        self.params.contents.get_value(self.p, pid, C.byref(v))
        return v.value

    def save(self):
        data = bytearray()

        def w(s, b, n):
            data.extend(C.string_at(b, n))
            return n
        cb = OS_WRITE(w)
        ok = self.state.contents.save(self.p, C.byref(OStream(None, cb)))
        return ok, bytes(data)

    def load(self, data):
        pos = [0]

        def r(s, b, n):
            chunk = data[pos[0]:pos[0] + n]
            C.memmove(b, chunk, len(chunk))
            pos[0] += len(chunk)
            return len(chunk)
        cb = IS_READ(r)
        return self.state.contents.load(self.p, C.byref(IStream(None, cb)))

    def destroy(self):
        self.call("destroy")


def rms(x): return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


# ---- parameters
a = Inst()
nparam = a.params.contents.count(a.p)
infos = []
for i in range(nparam):
    pi = ParamInfo()
    a.params.contents.get_info(a.p, i, C.byref(pi))
    infos.append(pi)
names = [p.name.decode() for p in infos]
ids = [p.id for p in infos]
by_name = {p.name.decode(): p for p in infos}
check(nparam > 100 and len(set(names)) == nparam and len(set(ids)) == nparam,
      f"{nparam} parameters, names and IDs unique (first: {names[0]!r}, last: {names[-1]!r})")
lv = by_name.get("MAIN LEVEL 1")
check(lv is not None and lv.min_value == 0 and lv.max_value == 100 and abs(lv.default_value - 80) < 1e-4,
      "MAIN LEVEL 1: 0..100, default 80")
mode = by_name.get("ARP NOTES MODE 1")
txt = C.create_string_buffer(64)
ok = mode is not None and a.params.contents.value_to_text(a.p, mode.id, 2.0, txt, 64)
check(mode is not None and mode.max_value == 5 and ok and txt.value.decode().upper().startswith("UP"),
      f"ARP NOTES MODE 1: stepped 0..5, step 2 reads {txt.value.decode()!r}")
print("   e.g.", ", ".join(names[i] for i in (0, 1, 30, 100, nparam - 1)))

# ---- play
check(a.activate(48000.0), "activate at 48 kHz, start processing")
L, R = a.render(3.0)
check(np.all(np.isfinite(L)) and np.all(np.isfinite(R)) and rms(L) > 0.005 and rms(R) > 0.005,
      f"plays by itself at 48 kHz (rms L {rms(L):.3f} R {rms(R):.3f})")

# the same engine as the tested DLL (MSVC vs clang: close, not bit-identical)
eng = C.CDLL(os.path.abspath(os.path.join(BUILD, "ammonite_engine.dll")))
eng.eng_create.restype = C.c_void_p
h = C.c_void_p(eng.eng_create())
eng.eng_init.argtypes = [C.c_void_p, C.c_float]
eng.eng_init(h, 48000.0)
ref = np.zeros(2 * 3 * 48000, dtype=np.float32)
eng.eng_process.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
for k in range(0, 3 * 48000, 256):
    eng.eng_process(h, ref[2 * k:].ctypes.data, 256)
corr = float(np.corrcoef(ref[0::2], L)[0, 1])
check(corr > 0.99, f"sounds like the tested engine (correlation {corr:.5f})")

# ---- automation: VOLUME to 0
vol = by_name["VOLUME"].id
a.set_param(vol, 0.0)
L, _ = a.render(0.5)
check(abs(a.value(vol)) < 1e-9 and rms(L[:256]) > 1e-3 and rms(L[int(0.2 * 48000):]) < 1e-5,
      f"automation VOLUME 0: glides out (first block {rms(L[:256]):.3f}), silent after 200 ms")

# ---- two instances: b plays on while a is silent; b equals a fresh third one
b, c = Inst(), Inst()
b.activate(48000.0)
c.activate(48000.0)
b.set_param(by_name["MAIN TEMPO"].id, 70.0)
c.set_param(by_name["MAIN TEMPO"].id, 70.0)
outs_a, outs_b = [], []
for _ in range(8):
    outs_a.append(a.render(0.25)[0])
    outs_b.append(b.render(0.25)[0])
Lb = np.concatenate(outs_b)
Lc = c.render(2.0)[0]
check(rms(np.concatenate(outs_a)) < 1e-5 and rms(Lb) > 0.005 and Lb.tobytes() == Lc.tobytes(),
      "two instances alternating: one silent, the other plays, bit-identical to a third run alone")

# ---- state: save b (TEMPO 70), load into a fresh one
ok, blob = b.save()
d = Inst()
ok2 = d.load(blob)
tempo = by_name["MAIN TEMPO"].id
check(ok and ok2 and len(blob) > 0 and abs(d.value(tempo) - 70.0) < 1e-3 and abs(d.value(vol) - 50.0) < 1e-3,
      f"state: save ({len(blob)} bytes) and load restores TEMPO 70 and the rest (VOLUME 50)")

# ---- other rates
for sr in (96000.0, 192000.0):
    a.deactivate()
    a.set_param(vol, 50.0)
    ok = a.activate(sr)
    L, R = a.render(1.0)
    check(ok and np.all(np.isfinite(L)) and rms(L) > 0.005,
          f"re-activate at {sr / 1000:.0f} kHz: plays (rms {rms(L):.3f}), finite")

# ---- SYNC DAW (the default) on a CLAP transport, through the plugin
s = Inst()
s.activate(48000.0)
for name, v in [("ARP NOTES MODE 1", 1), ("ARP NOTES MODE 2", 0), ("ARP NOTES MODE 3", 0),
                ("ARP RHYTHM DIVISION 1", 2), ("ARP NOTES POOL 1", 4), ("OSC OCTAVE 1", 3),
                ("ENVELOPE AMP ATTACK 1", 0), ("ENVELOPE AMP DECAY 1", 10),
                ("ENVELOPE AMP SUSTAIN 1", 0), ("MAIN LEVEL 2", 0), ("MAIN LEVEL 3", 0)]:
    s.set_param(by_name[name].id, v)  # osc 1 alone: a short high note on every quarter
s.render(0.5)  # settle (no transport: TEMPO)
L = s.render(3.0, host=(True, 90.0, 0.0, 4))[0]
t = onsets(L[int(0.2 * 48000):]) + 0.2
off = float(np.max(np.abs(t - np.round(t / (60 / 90)) * (60 / 90)))) * 1000
check(len(t) >= 3 and abs(np.median(np.diff(t)) - 60 / 90) < 0.002 and off < 3,
      f"CLAP transport at 90 BPM from bar 1: a note every {1000 * np.median(np.diff(t)):.1f} ms"
      f" (666.7), {off:.2f} ms off the host's beats")
L = s.render(1.2, host=(True, 120.0, 16.25, 4))[0]  # the playhead jumps to bar 5 + 1/4 beat
t = onsets(L)
check(abs(t[0]) < 0.006 and abs(t[1] - 0.375) < 0.003,
      f"playhead jump to beat 16.25: a note at once ({1000 * t[0]:.1f} ms), the next on beat 17"
      f" ({1000 * t[1]:.1f} ms, want 375)")
L = s.render(2.0, host=(False, 150.0, 3.0, 4))[0]  # stopped
t = onsets(L)
check(abs(np.median(np.diff(t)) - 0.4) < 0.002,
      f"transport stopped at 150 BPM: plays on, a note every {1000 * np.median(np.diff(t)):.1f} ms (400)")
s.set_param(by_name["SYNC"].id, 0)  # FREE
L = s.render(2.0, host=(True, 90.0, 0.0, 4))[0]
t = onsets(L[int(0.1 * 48000):])
check(abs(np.median(np.diff(t)) - 60 / 110) < 0.002,
      f"SYNC FREE: the host is ignored, TEMPO 110 BPM (every {1000 * np.median(np.diff(t)):.1f} ms)")
s.deactivate()
s.destroy()

for x in (a, b, c, d):
    x.deactivate() if x is not d else None
    x.destroy()
entry.deinit()
done()
