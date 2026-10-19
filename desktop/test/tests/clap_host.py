"""A minimal CLAP host in ctypes for the Ammonite Test plugins: the CLAP ABI
(the parts the checks use), an Inst class that activates, renders with
parameter and transport events, saves and loads state, and the check / done
helpers shared by test_plugin.py and ui_check.py.

Ported from plugin/tests/clap_abi.py + test_clap.py (the Seed
plugin's tests)."""
import ctypes as C
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.abspath(os.path.join(HERE, "..", "..", "build"))
BIN = os.path.join(BUILD, "cmake", "bin")
VARIANTS = {"web": ("AmmoniteTestWeb.clap", b"com.gsnautilus.ammonite-test-web", b"Ammonite Test Web"),
            "gl": ("AmmoniteTestGL.clap", b"com.gsnautilus.ammonite-test-gl", b"Ammonite Test GL"),
            "ammonite": ("AmmoniteDesktop.clap", b"com.gsnautilus.ammonite-desktop", b"Ammonite Desktop")}

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


CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_PARAM_GESTURE_BEGIN, CLAP_EVENT_PARAM_GESTURE_END = 5, 7, 8
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


# ---- GUI (clap.gui), for the UI check
class Window(C.Structure):
    _fields_ = [("api", C.c_char_p), ("handle", C.c_void_p)]


class HostGui(C.Structure):
    pass


HG_VOID = C.CFUNCTYPE(None, C.POINTER(Host))
HG_RESIZE = C.CFUNCTYPE(C.c_bool, C.POINTER(Host), C.c_uint32, C.c_uint32)
HG_BOOL = C.CFUNCTYPE(C.c_bool, C.POINTER(Host))
HG_CLOSED = C.CFUNCTYPE(None, C.POINTER(Host), C.c_bool)
HostGui._fields_ = [("resize_hints_changed", HG_VOID), ("request_resize", HG_RESIZE),
                    ("request_show", HG_BOOL), ("request_hide", HG_BOOL), ("closed", HG_CLOSED)]


class Gui(C.Structure):
    _fields_ = [("is_api_supported", C.CFUNCTYPE(C.c_bool, PP, C.c_char_p, C.c_bool)),
                ("get_preferred_api", C.c_void_p),
                ("create", C.CFUNCTYPE(C.c_bool, PP, C.c_char_p, C.c_bool)),
                ("destroy", C.CFUNCTYPE(None, PP)),
                ("set_scale", C.CFUNCTYPE(C.c_bool, PP, C.c_double)),
                ("get_size", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))),
                ("can_resize", C.CFUNCTYPE(C.c_bool, PP)),
                ("get_resize_hints", C.c_void_p),
                ("adjust_size", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))),
                ("set_size", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.c_uint32)),
                ("set_parent", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(Window))),
                ("set_transient", C.c_void_p),
                ("suggest_title", C.c_void_p),
                ("show", C.CFUNCTYPE(C.c_bool, PP)),
                ("hide", C.CFUNCTYPE(C.c_bool, PP))]


# -------------------------------------------------------------- the host
class ClapHost:
    """Loads a .clap and keeps the host callbacks alive. with_gui adds the
    clap.gui host extension (the UI check needs it)."""

    def __init__(self, path, with_gui=False):
        self.path = os.path.abspath(path)
        hg = [HG_VOID(lambda h: None), HG_RESIZE(lambda h, w, hh: True), HG_BOOL(lambda h: True),
              HG_CLOSED(lambda h, d: None)]
        self.host_gui = HostGui(hg[0], hg[1], hg[2], hg[2], hg[3])
        self._keep = [hg]

        def host_ext(h, ext):
            if with_gui and ext == b"clap.gui":
                return C.addressof(self.host_gui)
            return None

        self.cb = [HOST_GET_EXT(host_ext), HOST_REQ(lambda h: None)]
        self.host = Host(Version(1, 2, 0), None, b"ammonite-test-host", b"GSNautilus", b"", b"0.1",
                         self.cb[0], self.cb[1], self.cb[1], self.cb[1])
        self.lib = C.CDLL(self.path)
        self.entry = Entry.in_dll(self.lib, "clap_entry")
        assert self.entry.init(self.path.encode()), "clap_entry.init"
        self.factory = C.cast(self.entry.get_factory(b"clap.plugin-factory"), PF)

    def count(self):
        return self.factory.contents.get_plugin_count(self.factory)

    def descriptor(self, i=0):
        d = self.factory.contents.get_plugin_descriptor(self.factory, i).contents
        feats, k = [], 0
        while d.features[k]:
            feats.append(d.features[k].decode())
            k += 1
        return d, feats

    def create(self, plugin_id):
        return Inst(self, plugin_id)

    def deinit(self):
        self.entry.deinit()


EMPTY_IN = (IN_SIZE(lambda l: 0), IN_GET(lambda l, i: None))


class Inst:
    def __init__(self, host, plugin_id):
        self.host = host
        self.p = host.factory.contents.create_plugin(host.factory, C.byref(host.host), plugin_id)
        assert self.p and self.p.contents.init(self.p), "create / init"
        self.params = C.cast(self.p.contents.get_extension(self.p, b"clap.params"), C.POINTER(Params))
        self.state = C.cast(self.p.contents.get_extension(self.p, b"clap.state"), C.POINTER(State))
        gui = self.p.contents.get_extension(self.p, b"clap.gui")
        self.gui = C.cast(gui, C.POINTER(Gui)).contents if gui else None
        self.steady = 0
        self.pending = []
        self.out_log = []  # (type, param_id, value) of every event the plugin sent out
        self._out_cb = OUT_PUSH(self._push)
        self.out_events = OutEvents(None, self._out_cb)
        self.sr = 48000.0
        self.maxf = 512
        self.active = False

    def _push(self, lst, ev):
        hdr = C.cast(ev, C.POINTER(EventHeader)).contents
        if hdr.type in (CLAP_EVENT_PARAM_GESTURE_BEGIN, CLAP_EVENT_PARAM_GESTURE_END):
            self.out_log.append((hdr.type, C.cast(ev, C.POINTER(C.c_uint32 * 5)).contents[4], None))
        elif hdr.type == CLAP_EVENT_PARAM_VALUE:
            e = C.cast(ev, C.POINTER(ParamValueEvent)).contents
            self.out_log.append((CLAP_EVENT_PARAM_VALUE, e.param_id, e.value))
        return True

    def call(self, name, *a):
        return getattr(self.p.contents, name)(self.p, *a)

    # ---- parameters
    def param_infos(self):
        infos = []
        for i in range(self.params.contents.count(self.p)):
            pi = ParamInfo()
            self.params.contents.get_info(self.p, i, C.byref(pi))
            infos.append(pi)
        return infos

    def value(self, pid):
        v = C.c_double()
        self.params.contents.get_value(self.p, pid, C.byref(v))
        return v.value

    def set_param(self, pid, value):
        """Queued: sent with the next process() call, at its first sample."""
        self.pending.append((pid, value))

    # ---- lifecycle
    def activate(self, sr, maxf=512):
        self.sr, self.maxf = sr, maxf
        ok = self.call("activate", sr, 1, maxf) and self.call("start_processing")
        self.active = bool(ok)
        return ok

    def deactivate(self):
        if self.active:
            self.call("stop_processing")
            self.call("deactivate")
            self.active = False

    def destroy(self):
        self.deactivate()
        self.call("destroy")

    # ---- audio
    def render(self, sec, sizes=(512, 256, 100, 37, 512), host=None):
        """Mono-summed stereo out as (L, R). host = (playing, bpm, beat at the
        start, beats per bar): a CLAP transport on every block, advancing with
        the audio."""
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
                           C.pointer(buf), 0, 1, C.pointer(ins), C.pointer(self.out_events))
            self.call("process", C.byref(proc))
            L[k:k + m], R[k:k + m] = bl, br
            k += m
            self.steady += m
        return L, R

    # ---- state
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


# ------------------------------------------------------------- analysis
def rms(x):
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def dominant_hz(x, sr):
    """The strongest spectral line (Hz) of a signal, by FFT peak."""
    x = x.astype(np.float64) * np.hanning(len(x))
    spec = np.abs(np.fft.rfft(x))
    spec[0] = 0
    return float(np.argmax(spec) * sr / len(x))


def hf_ratio(x, sr, cut=250.0):
    """Energy above `cut` Hz over the whole energy: ~0 for a 110 Hz sine,
    clearly more for a saw."""
    x = x.astype(np.float64) * np.hanning(len(x))
    spec = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    tot = float(np.sum(spec[1:]))
    return float(np.sum(spec[f > cut]) / tot) if tot > 0 else 0.0


def beat_times(x, sr):
    """Starts of the beat envelope: the envelope (10 ms RMS frames) jumping up
    by more than 60 % from one frame to the next."""
    n = int(sr * 0.010)
    m = len(x) // n
    env = np.sqrt(np.mean(x[:m * n].astype(np.float64).reshape(m, n) ** 2, axis=1))
    out, last = [], -10
    for i in range(1, m):
        if env[i] > env[i - 1] * 1.6 and env[i] > 0.02 and i - last > 5:
            out.append(i * n / sr)
            last = i
    return np.array(out)


def clap_path(variant):
    return os.environ.get("CLAP_PATH") or os.path.join(BIN, VARIANTS[variant][0])
