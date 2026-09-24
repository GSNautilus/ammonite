"""The plugin UI, opened like a DAW opens it: the CLAP host of test_clap.py
gives the built Ammonite.clap a window (tkinter) and plays it in real time,
then drives the panel with mouse messages and checks what reaches the host:
the gesture (begin / value / end) and the new parameter value. Screenshots
go to plugin/build/ui_*.png. A window shows for a few seconds; the audio is
not played (the host only computes it).

Needs .\\plugin\\plugin.ps1 build first. Run it by hand
(python plugin\\tests\\ui_check.py), not in plugin.ps1 test: the mouse
messages go to a real window, so moving the real mouse over it during the
run can disturb the drags.
"""
import ctypes as C
import os
import sys
import time
import tkinter as tk

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BUILD = os.path.join(ROOT, "plugin", "build")
CLAP = os.path.abspath(os.environ.get("CLAP_PATH") or os.path.join(BUILD, "cmake", "bin", "Ammonite.clap"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, HERE)
os.environ.setdefault("SYNTH_DLL", os.path.join(BUILD, "ammonite_engine.dll"))
from arpsynth import check, done  # noqa: E402

if not os.path.exists(CLAP):
    print(f"skip: {CLAP} not built (.\\plugin\\plugin.ps1 build)")
    raise SystemExit(0)

from clap_abi import *  # noqa: E402,F401,F403
from PIL import ImageGrab  # noqa: E402

C.windll.user32.SetProcessDPIAware()  # real pixels, so the screenshots match
user32 = C.windll.user32
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON = 0x200, 0x201, 0x202, 1
EV_VALUE, EV_BEGIN, EV_END = 5, 7, 8  # CLAP_EVENT_PARAM_VALUE, _GESTURE_BEGIN, _GESTURE_END


# ---------------------------------------------------------------- host
hg_cb = [HG_VOID(lambda h: None), HG_RESIZE(lambda h, w, hh: True), HG_BOOL(lambda h: True),
         HG_CLOSED(lambda h, d: None)]
host_gui = HostGui(hg_cb[0], hg_cb[1], hg_cb[2], hg_cb[2], hg_cb[3])


def host_ext(h, ext):
    return C.addressof(host_gui) if ext == b"clap.gui" else None


host_cb = [HOST_GET_EXT(host_ext), HOST_REQ(lambda h: None)]
host = Host(Version(1, 2, 0), None, b"ammonite-test", b"GSNautilus", b"", b"0.1",
            host_cb[0], host_cb[1], host_cb[1], host_cb[1])

lib = C.CDLL(CLAP)
entry = Entry.in_dll(lib, "clap_entry")
entry.init(CLAP.encode())
factory = C.cast(entry.get_factory(b"clap.plugin-factory"), PF)
p = factory.contents.create_plugin(factory, C.byref(host), b"com.gsnautilus.ammonite")
p.contents.init(p)
params = C.cast(p.contents.get_extension(p, b"clap.params"), C.POINTER(Params)).contents
gui = C.cast(p.contents.get_extension(p, b"clap.gui"), C.POINTER(Gui)).contents

by_name = {}
for i in range(params.count(p)):
    pi = ParamInfo()
    params.get_info(p, i, C.byref(pi))
    by_name[pi.name.decode()] = pi.id


def value(name):
    v = C.c_double()
    params.get_value(p, by_name[name], C.byref(v))
    return v.value


# audio: 48 kHz, 480-frame blocks on a 10 ms timer; out events recorded
SR, BLOCK = 48000, 480
out_log = []


def push(lst, ev):
    hdr = C.cast(ev, C.POINTER(EventHeader)).contents
    if hdr.type in (EV_BEGIN, EV_END):
        out_log.append((hdr.type, C.cast(ev, C.POINTER(C.c_uint32 * 5)).contents[4], None))
    elif hdr.type == EV_VALUE:
        e = C.cast(ev, C.POINTER(ParamValueEvent)).contents
        out_log.append((EV_VALUE, e.param_id, e.value))
    return True


out_cb = OUT_PUSH(push)
out_events = OutEvents(None, out_cb)
in_cb = (IN_SIZE(lambda l: 0), IN_GET(lambda l, i: None))
in_events = InEvents(None, in_cb[0], in_cb[1])
bl = np.zeros(BLOCK, dtype=np.float32)
br = np.zeros(BLOCK, dtype=np.float32)
chans = (C.POINTER(C.c_float) * 2)(bl.ctypes.data_as(C.POINTER(C.c_float)),
                                    br.ctypes.data_as(C.POINTER(C.c_float)))
abuf = AudioBuffer(chans, None, 2, 0, 0)
steady = [0]
peak = [0.0]


def process():
    proc = Process(steady[0], BLOCK, None, None, C.pointer(abuf), 0, 1, C.pointer(in_events),
                   C.pointer(out_events))
    p.contents.process(p, C.byref(proc))
    steady[0] += BLOCK
    peak[0] = max(peak[0], float(np.max(np.abs(bl))))


p.contents.activate(p, float(SR), 1, BLOCK)
p.contents.start_processing(p)

# ---------------------------------------------------------------- window
check(gui.is_api_supported(p, b"win32", False), "the UI supports an embedded win32 window")
check(gui.create(p, b"win32", False), "UI created")
w, h = C.c_uint32(), C.c_uint32()
gui.get_size(p, C.byref(w), C.byref(h))
W, H = w.value, h.value
check(W > 600 and abs(W / H - 1400 / 860) < 0.02, f"UI size {W} x {H} (the panel's 1400:860)")

root = tk.Tk()
root.title("Ammonite UI check")
root.attributes("-topmost", True)
frame = tk.Frame(root, width=W, height=H)
frame.pack()
root.update()
parent = Window(b"win32", frame.winfo_id())
check(gui.set_parent(p, C.byref(parent)) and gui.show(p), "UI embedded in the host window and shown")


def pump(sec):
    t_end = time.time() + sec
    nxt = time.time()
    while time.time() < t_end:
        root.update()
        if time.time() >= nxt:
            process()
            nxt += BLOCK / SR
        time.sleep(0.002)


def shot(name):
    root.update()
    x, y = frame.winfo_rootx(), frame.winfo_rooty()
    img = ImageGrab.grab(bbox=(x, y, x + W, y + H))
    path = os.path.join(BUILD, f"ui_{name}.png")
    img.save(path)
    return img


child = user32.GetWindow(frame.winfo_id(), 5)  # GW_CHILD: the plugin's window
check(child != 0, "the plugin made its own child window")
S = W / 1400.0
KNOBS = [(0.130, 0.372), (0.240, 0.366), (0.352, 0.362), (0.130, 0.610), (0.240, 0.603),
         (0.352, 0.597), (0.660, 0.358), (0.766, 0.353), (0.873, 0.350), (0.663, 0.592),
         (0.768, 0.587), (0.874, 0.582)]


def knob_px(k):
    return int(KNOBS[k][0] * 1400 * S), int(KNOBS[k][1] * 860 * S)


def lparam(x, y):
    return (y << 16) | (x & 0xFFFF)


def drag(k, dy_logical, steps=8):
    """Press on knob k, move up by dy (panel units), release."""
    x, y = knob_px(k)
    user32.PostMessageW(child, WM_MOUSEMOVE, 0, lparam(x, y))
    user32.PostMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, lparam(x, y))
    pump(0.05)
    for i in range(1, steps + 1):
        yy = int(y - dy_logical * S * i / steps)
        user32.PostMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, lparam(x, yy))
        pump(0.02)
    user32.PostMessageW(child, WM_LBUTTONUP, 0, lparam(x, int(y - dy_logical * S)))
    pump(0.1)


pump(2.0)
img = np.asarray(shot("main")).astype(int)
sx, sy, r = int(0.502 * 1400 * S), int(0.478 * 860 * S), int(120 * S)
screen = img[sy - r:sy + r, sx - r:sx + r]
lit = float(np.mean(screen.max(axis=2) > 40))
check(peak[0] > 0.005, f"it plays while the UI is open (peak {peak[0]:.3f})")
check(lit > 0.02, f"the round screen shows the engine's frame ({100 * lit:.1f} % of it lit)")

# knob 2 on MAIN = LEVEL 2 (default 70): drag up 40 units = +0.2 -> 90
out_log.clear()
before = value("MAIN LEVEL 2")
drag(1, 40)
after = value("MAIN LEVEL 2")
pid = by_name["MAIN LEVEL 2"]
types = [t for t, i, _ in out_log if i == pid]
check(abs(before - 70) < 1e-3 and abs(after - 90) < 0.6,
      f"drag knob 2 up: MAIN LEVEL 2 {before:.1f} -> {after:.1f} (want 90)")
check(types[:1] == [EV_BEGIN] and types[-1:] == [EV_END] and EV_VALUE in types,
      f"the host got a gesture: begin, {types.count(EV_VALUE)} values, end (automation records)")

# knob 11 = PAGE: drag up two pages -> ARP; knob 1 there = MODE 1 (a step)
drag(10, 40)  # +0.2 of the travel: MAIN (step 0 of 9) -> ARP (step 2)
pump(0.3)
shot("page")
mode_before = value("ARP NOTES MODE 1")
drag(0, 60)  # +0.3 of the travel = about 2 steps of 6
mode_after = value("ARP NOTES MODE 1")
check(mode_after == mode_before + 2 or mode_after == mode_before + 1,
      f"page 3 (ARP): knob 1 is now MODE 1, a step: {mode_before:.0f} -> {mode_after:.0f}")

# the SYNC button toggles the parameter
sync0 = value("SYNC")
x, y = int(1280 * S), int(73 * S)
user32.PostMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, lparam(x, y))
user32.PostMessageW(child, WM_LBUTTONUP, 0, lparam(x, y))
pump(0.3)
check(value("SYNC") == 1 - sync0, f"the SYNC button: {sync0:.0f} -> {value('SYNC'):.0f}")
pump(1.0)
shot("after")

gui.hide(p)
gui.destroy(p)
root.destroy()
p.contents.stop_processing(p)
p.contents.deactivate(p)
p.contents.destroy(p)
entry.deinit()
done()
