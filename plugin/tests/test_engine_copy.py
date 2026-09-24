"""The plugin's engine copy (plugin/engine) against the hardware core (core/).

1. MATCH: the same calls through plugin/build/core_ref.dll (core/, v1.0)
   and plugin/build/ammonite_engine.dll (its default instance and a created
   instance) give bit-identical audio, screens and panel text. Random
   parameter sets, pot moves, page changes, block sizes, sample rates.
2. INSTANCES: several engines side by side stay independent: identical
   input gives identical output, one's settings never leak into another,
   interleaved or concurrent processing (threads) changes nothing.

Run through plugin\\plugin.ps1 test, which builds both DLLs first.
REF_DLL / ENGINE_DLL override the two paths.
"""
import ctypes
import os
import random
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tests"))
BUILD = os.path.join(ROOT, "plugin", "build")
os.environ.setdefault("SYNTH_DLL", os.path.join(BUILD, "ammonite_engine.dll"))  # for arpsynth
from arpsynth import FUNCS, check, done  # noqa: E402

import numpy as np  # noqa: E402

F = ctypes.c_float
H = ctypes.c_void_p
NPOTS, OSCS = 12, 3

def load(path):
    dll = ctypes.CDLL(os.path.abspath(path))
    for n in ("synth_get_slot_name", "synth_get_value_text", "synth_get_chord_name",
              "synth_get_chord_numeral"):
        getattr(dll, n).restype = ctypes.c_char_p
    for n in ("synth_get_pot", "synth_get_slot_value", "synth_get_osc_note"):
        getattr(dll, n).restype = F
    return dll


REF = load(os.environ.get("REF_DLL") or os.path.join(BUILD, "core_ref.dll"))
CPY = load(os.environ.get("ENGINE_DLL") or os.path.join(BUILD, "ammonite_engine.dll"))
CPY.eng_create.restype = H
CPY.eng_destroy.argtypes = [H]
CPY.eng_init.argtypes = [H, F]
CPY.eng_set_pot.argtypes = [H, ctypes.c_int, F]
CPY.eng_set_engine_param.argtypes = [H, ctypes.c_int, ctypes.c_int, F]
CPY.eng_process.argtypes = [H, ctypes.c_void_p, ctypes.c_int]
CPY.eng_render.argtypes = [H, ctypes.c_void_p]
for n in ("eng_get_pot", "eng_get_slot_value", "eng_get_osc_note", "eng_get_slot_name",
          "eng_get_value_text", "eng_get_chord_name", "eng_get_chord_numeral", "eng_get_page",
          "eng_get_slot_osc"):
    getattr(CPY, n).argtypes = [H] + ([ctypes.c_int] if n not in (
        "eng_get_chord_name", "eng_get_chord_numeral", "eng_get_page") else [])
for n in ("eng_get_slot_name", "eng_get_value_text", "eng_get_chord_name",
          "eng_get_chord_numeral"):
    getattr(CPY, n).restype = ctypes.c_char_p
for n in ("eng_get_pot", "eng_get_slot_value", "eng_get_osc_note"):
    getattr(CPY, n).restype = F


class Global:
    """An engine behind the handle-less synth_* calls of a DLL."""

    def __init__(self, dll):
        self.d = dll

    def init(self, sr): self.d.synth_init(F(sr))
    def param(self, f, o, v): self.d.synth_set_engine_param(f, o, F(v))
    def pot(self, i, v): self.d.synth_set_pot(i, F(v))
    def process(self, buf, n): self.d.synth_process(buf.ctypes.data_as(ctypes.c_void_p), n)
    def render(self, fb): self.d.synth_render(fb.ctypes.data_as(ctypes.c_void_p))

    def panel(self):
        d = self.d
        return (d.synth_get_page(),
                tuple((d.synth_get_slot_name(i), d.synth_get_value_text(i), d.synth_get_slot_osc(i),
                       d.synth_get_slot_value(i), d.synth_get_pot(i)) for i in range(NPOTS)),
                d.synth_get_chord_name(), d.synth_get_chord_numeral(),
                tuple(d.synth_get_osc_note(o) for o in range(OSCS)))


class Handle:
    """An engine from eng_create()."""

    def __init__(self):
        self.h = H(CPY.eng_create())

    def close(self):
        CPY.eng_destroy(self.h)

    def init(self, sr): CPY.eng_init(self.h, sr)
    def param(self, f, o, v): CPY.eng_set_engine_param(self.h, f, o, v)
    def pot(self, i, v): CPY.eng_set_pot(self.h, i, v)
    def process(self, buf, n): CPY.eng_process(self.h, buf.ctypes.data_as(ctypes.c_void_p), n)
    def render(self, fb): CPY.eng_render(self.h, fb.ctypes.data_as(ctypes.c_void_p))

    def panel(self):
        h = self.h
        return (CPY.eng_get_page(h),
                tuple((CPY.eng_get_slot_name(h, i), CPY.eng_get_value_text(h, i),
                       CPY.eng_get_slot_osc(h, i), CPY.eng_get_slot_value(h, i),
                       CPY.eng_get_pot(h, i)) for i in range(NPOTS)),
                CPY.eng_get_chord_name(h), CPY.eng_get_chord_numeral(h),
                tuple(CPY.eng_get_osc_note(h, o) for o in range(OSCS)))


def scenario(seed, seconds):
    """A reproducible list of calls: init, random engine params, then blocks
    of random size with pot moves, page changes and screen renders between."""
    r = random.Random(seed)
    # Above ~52 kHz ReverbSc's fixed buffer is too small and core/ crashes (step 2)
    sr = r.choice([44100, 48000])
    ops = [("init", sr)]
    if seed > 0:
        for f in range(len(FUNCS)):
            if r.random() < 0.6:
                for o in range(OSCS):  # a global function only reads osc 0
                    ops.append(("param", f, o, r.random()))
    left = int(seconds * sr)
    while left > 0:
        n = min(left, r.choice([1, 7, 48, 64, 256, 256, 512, r.randint(1, 600)]))
        ops.append(("process", n))
        left -= n
        x = r.random()
        if x < 0.25:
            ops.append(("pot", r.randrange(NPOTS), r.random()))
        elif x < 0.3:
            ops.append(("pot", 10, r.random()))  # PAGE
        if r.random() < 0.15:
            ops.append(("render",))
    ops.append(("render",))
    return ops


def run(eng, ops):
    """Apply ops; return (audio bytes, [screens], [panel states])."""
    audio, screens, panels = [], [], []
    fb = np.zeros(240 * 240, dtype=np.uint16)
    for op in ops:
        k = op[0]
        if k == "init":
            eng.init(op[1])
        elif k == "param":
            eng.param(op[1], op[2], op[3])
        elif k == "pot":
            eng.pot(op[1], op[2])
            panels.append(eng.panel())
        elif k == "process":
            buf = np.empty(2 * op[1], dtype=np.float32)
            eng.process(buf, op[1])
            audio.append(buf)
        elif k == "render":
            eng.render(fb)
            screens.append(fb.tobytes())
            panels.append(eng.panel())
    return np.concatenate(audio), screens, panels


def same(a, b):
    return (a[0].tobytes() == b[0].tobytes(), a[1] == b[1], a[2] == b[2])


# ---------------------------------------------------------------- 1. MATCH
# One created instance lives through all scenarios, like the two default
# instances: Init does not reset everything (the WHEEL view's note memory,
# a function-level static in core/), so all three need the same history.
# The first scenario runs on three engines that never ran before.
N_SCEN = 40
bad = []
loud = 0
h = Handle()
for seed in range(N_SCEN):
    ops = scenario(seed, 1.5 if seed else 4.0)
    ref = run(Global(REF), ops)
    cpy = run(Global(CPY), ops)
    hnd = run(h, ops)
    if not (all(same(ref, cpy)) and all(same(ref, hnd))):
        bad.append((seed, same(ref, cpy), same(ref, hnd)))
    loud += float(np.max(np.abs(ref[0]))) > 0.01
h.close()
check(not bad, f"MATCH: {N_SCEN} scenarios bit-identical to core/ (audio, screens, panel text),"
      f" default instance and a created one ({loud} with sound)"
      + (f"; differ (seed, (audio, screens, panel) default, created): {bad}" if bad else ""))
check(loud >= N_SCEN * 3 // 4, f"MATCH: most scenarios make sound ({loud}/{N_SCEN})")


# ------------------------------------------------------------ 2. INSTANCES
def only_audio(ops):
    return [op for op in ops if op[0] != "render" and op[0] != "pot"]


opsA = only_audio(scenario(101, 3.0))
opsB = only_audio(scenario(0, 3.0))
wantA = run(Global(REF), opsA)[0]
wantB = run(Global(REF), opsB)[0]

# identical input -> identical output, and equal to core/
c, d = Handle(), Handle()
outC, outD = run(c, opsA)[0], run(d, opsA)[0]
c.close()
d.close()
check(outC.tobytes() == outD.tobytes() == wantA.tobytes(),
      "INSTANCES: two engines fed the same calls give the same audio, equal to core/")


def interleaved(e1, ops1, e2, ops2):
    """Run two op lists, alternating one op of each."""
    res = {id(e1): [], id(e2): []}
    it = [(e1, iter(ops1)), (e2, iter(ops2))]
    live = True
    while live:
        live = False
        for e, ops in it:
            op = next(ops, None)
            if op is None:
                continue
            live = True
            if op[0] == "init":
                e.init(op[1])
            elif op[0] == "param":
                e.param(op[1], op[2], op[3])
            elif op[0] == "process":
                buf = np.empty(2 * op[1], dtype=np.float32)
                e.process(buf, op[1])
                res[id(e)].append(buf)
    return np.concatenate(res[id(e1)]), np.concatenate(res[id(e2)])


# different settings, processed alternately: no cross-talk
a, b = Handle(), Handle()
outA, outB = interleaved(a, opsA, b, opsB)
check(outA.tobytes() == wantA.tobytes() and outB.tobytes() == wantB.tobytes(),
      "INSTANCES: random settings in one, defaults in the other, blocks alternating:"
      " each equals its own run on core/ (no cross-talk)")
n = min(len(outA), len(outB))
check(np.max(np.abs(outA[:n] - outB[:n])) > 0.01, "INSTANCES: and the two really sound different")

# the default instance runs alongside created ones without disturbing them
g = Global(CPY)
outG, outA2 = interleaved(g, opsB, a, opsA)
check(outG.tobytes() == wantB.tobytes() and outA2.tobytes() == wantA.tobytes(),
      "INSTANCES: default instance and a created one, alternating: both unchanged")
a.close()
b.close()

# concurrent: two engines on two threads, a third thread rendering both
# screens and reading their panels the whole time (the UI side of the split)
a, b = Handle(), Handle()
outs = {}


def audio_thread(key, e, ops):
    outs[key] = run(e, ops)[0]


stop = threading.Event()
renders = [0]


def ui_thread():
    fb = np.zeros(240 * 240, dtype=np.uint16)
    while not stop.is_set():
        for e in (a, b):
            e.render(fb)
            e.panel()
            renders[0] += 1


# init first (Init is not meant to race the UI thread)
a.init(opsA[0][1])
b.init(opsB[0][1])
ta = threading.Thread(target=audio_thread, args=("A", a, opsA[1:]))
tb = threading.Thread(target=audio_thread, args=("B", b, opsB[1:]))
tu = threading.Thread(target=ui_thread)
tu.start()
ta.start()
tb.start()
ta.join()
tb.join()
stop.set()
tu.join()
check(outs["A"].tobytes() == wantA.tobytes() and outs["B"].tobytes() == wantB.tobytes(),
      f"INSTANCES: two engines on two threads at once, screens rendered alongside"
      f" ({renders[0]} renders): audio unchanged")
a.close()
b.close()

# a new engine after others were destroyed starts clean
e = Handle()
outE = run(e, opsA)[0]
e.close()
check(outE.tobytes() == wantA.tobytes(), "INSTANCES: an engine created after others were"
      " destroyed starts from a clean state")

done()
