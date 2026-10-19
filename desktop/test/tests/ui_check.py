"""The test plugin's UI, opened like a DAW opens it: the CLAP host of
clap_host.py gives the built .clap a window (tkinter) and plays it in real
time, waits for the UI to report in (through $AMMO_TEST_LOG, which the UI
appends to), screenshots it (desktop\\build\\ui_<variant>.png), drags the
Shape knob with the real mouse and checks what reached the host: the
gesture (begin / values / end) and the new value. The web variant must also
report a working WebGL context.

    python desktop\\test\\tests\\ui_check.py web
    python desktop\\test\\tests\\ui_check.py gl

A window shows for a few seconds and the mouse is moved: keep hands off.
The audio is computed, not played."""
import ctypes as C
import os
import sys
import time
import tkinter as tk

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clap_host import (BUILD, VARIANTS, CLAP_EVENT_PARAM_GESTURE_BEGIN, CLAP_EVENT_PARAM_GESTURE_END,  # noqa: E402
                       CLAP_EVENT_PARAM_VALUE, AudioBuffer, ClapHost, InEvents, IN_GET, IN_SIZE, Process, Window,
                       check, clap_path, done)

variant = sys.argv[1] if len(sys.argv) > 1 else "web"
path = clap_path(variant)
if not os.path.exists(path):
    print(f"skip: {path} not built (.\\desktop\\desktop.ps1 build)")
    raise SystemExit(0)
_, plugin_id, _ = VARIANTS[variant]
print(f"== {path}")

LOG = os.path.join(BUILD, f"ui_{variant}.log")
if os.path.exists(LOG):
    os.remove(LOG)
os.environ["AMMO_TEST_LOG"] = LOG  # before the DLL loads: the UI reads it once

from PIL import ImageGrab  # noqa: E402

user32 = C.windll.user32
user32.SetProcessDPIAware()  # real pixels, so screenshots and the mouse match
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0001, 0x0002, 0x0004

host = ClapHost(path, with_gui=True)
p = host.create(plugin_id)
infos = p.param_infos()
SHAPE = infos[0].id

# audio: 48 kHz, 480-frame blocks on a 10 ms timer; the plugin's out events are logged
SR, BLOCK = 48000, 480
in_cb = (IN_SIZE(lambda l: 0), IN_GET(lambda l, i: None))
in_events = InEvents(None, in_cb[0], in_cb[1])
bl = np.zeros(BLOCK, dtype=np.float32)
br = np.zeros(BLOCK, dtype=np.float32)
chans = (C.POINTER(C.c_float) * 2)(bl.ctypes.data_as(C.POINTER(C.c_float)), br.ctypes.data_as(C.POINTER(C.c_float)))
abuf = AudioBuffer(chans, None, 2, 0, 0)
steady = [0]
peak = [0.0]


def process():
    proc = Process(steady[0], BLOCK, None, None, C.pointer(abuf), 0, 1, C.pointer(in_events), C.pointer(p.out_events))
    p.call("process", C.byref(proc))
    steady[0] += BLOCK
    peak[0] = max(peak[0], float(np.max(np.abs(bl))))


p.activate(float(SR), BLOCK)

# ---- the window
gui = p.gui
check(gui is not None and gui.is_api_supported(p.p, b"win32", False), "the UI supports an embedded win32 window")
w, h = C.c_uint32(), C.c_uint32()
root = tk.Tk()
root.title(f"Ammonite Test {variant} UI check")
root.attributes("-topmost", True)
t_create = time.time()
check(gui.create(p.p, b"win32", False), "UI created")
gui.get_size(p.p, C.byref(w), C.byref(h))
W, H = w.value, h.value
check(W >= 720 and abs(W / H - 1.5) < 0.02, f"UI size {W} x {H} (720:480, scaled {W / 720:.2f})")
frame = tk.Frame(root, width=W, height=H)
frame.pack()
root.update()
parent = Window(b"win32", frame.winfo_id())
check(gui.set_parent(p.p, C.byref(parent)) and gui.show(p.p), "UI embedded in the host window and shown")
print(f"     UI create + show took {1000 * (time.time() - t_create):.0f} ms")


def pump(sec):
    t_end = time.time() + sec
    nxt = time.time()
    while time.time() < t_end:
        root.update()
        if time.time() >= nxt:
            process()
            nxt += BLOCK / SR
        time.sleep(0.002)


def log_lines():
    try:
        with open(LOG, encoding="utf-8", errors="replace") as f:
            return [ln.split(" ", 1)[1].rstrip() if " " in ln else ln.rstrip() for ln in f]
    except FileNotFoundError:
        return []


def shot(name):
    root.update()
    x, y = frame.winfo_rootx(), frame.winfo_rooty()
    img = ImageGrab.grab(bbox=(x, y, x + W, y + H))
    out = os.path.join(BUILD, f"ui_{variant}_{name}.png")
    img.save(out)
    return img, out


# let it come up: the web view needs a moment to load the page
pump(3.0)
lines = log_lines()
print("     UI log: " + " | ".join(lines[:8]))
check(any("ui created" in ln for ln in lines), "the UI logged its creation")
check(any("first tick" in ln for ln in lines), "the UI received telemetry from the audio thread")
if variant == "web":
    check(any("webview created" in ln for ln in lines), "the WebView2 was created")
    check(any(ln == "page ready" for ln in lines), "the page loaded and reported ready")
    gl = [ln for ln in lines if ln.startswith("gl ")]
    check(bool(gl) and gl[0].startswith("gl ok"), f"WebGL context and shaders: {gl[0] if gl else 'no report'}")
else:
    check(any("gl program linked" in ln for ln in lines), "the OpenGL 2 shell program linked")
    check(not any("gl fail" in ln or "gl shader compile failed" in ln for ln in lines), "no OpenGL failures logged")
check(peak[0] > 0.1, f"it plays while the UI is open (peak {peak[0]:.3f})")

img, out = shot("main")
arr = np.asarray(img).astype(int)
lit = float(np.mean(arr.max(axis=2) > 40))
cx, cy, r = W // 2, H // 2, int(H * 0.3)
shell = arr[cy - r:cy + r, cx - r:cx + r]
shell_lit = float(np.mean(shell.max(axis=2) > 24))  # the shell is dim between beats: a low bar
check(lit > 0.01 and shell_lit > 0.05,
      f"the window is painted ({100 * lit:.1f} % of pixels lit, shell area {100 * shell_lit:.0f} %) -> {out}")

# ---- drag the Shape knob with the real mouse: up 40 layout px = +40/180 = +0.222
S = W / 720.0
kx, ky = frame.winfo_rootx() + int(70 * S), frame.winfo_rooty() + int(410 * S)
before = p.value(SHAPE)


def drag_shape(dy_px, steps=10):
    user32.SetCursorPos(kx, ky)
    pump(0.15)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    pump(0.1)
    for i in range(1, steps + 1):
        user32.SetCursorPos(kx, ky - int(dy_px * S * i / steps))
        pump(0.03)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    pump(0.3)


p.out_log.clear()
drag_shape(40)
after = p.value(SHAPE)
if abs(after - before) < 1e-6:
    # Seen once: the very first press after the page came up was lost. Say so and try again.
    print("     the first drag did not register; retrying once")
    pump(0.5)
    p.out_log.clear()
    drag_shape(40)
    after = p.value(SHAPE)
types = [t for t, i, _ in p.out_log if i == SHAPE]
check(abs(before - 0.25) < 1e-3 and abs(after - 0.472) < 0.04,
      f"drag the Shape knob up: {before:.3f} -> {after:.3f} (want ~0.472)")
check(types[:1] == [CLAP_EVENT_PARAM_GESTURE_BEGIN] and types[-1:] == [CLAP_EVENT_PARAM_GESTURE_END]
      and CLAP_EVENT_PARAM_VALUE in types,
      f"the host got a gesture: begin, {types.count(CLAP_EVENT_PARAM_VALUE)} values, end (automation records)")

# double-click resets to the default
user32.SetCursorPos(kx, ky)
pump(0.05)
for _ in range(2):
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    pump(0.08)
pump(0.3)
check(abs(p.value(SHAPE) - 0.25) < 1e-3, f"double-click: back to the default ({p.value(SHAPE):.3f})")
user32.SetCursorPos(frame.winfo_rootx() + W - 10, frame.winfo_rooty() + 10)

pump(1.0)
_, out2 = shot("after")
lines = log_lines()
tail = [ln for ln in lines if "ticks" in ln or "eval" in ln]
if tail:
    print("     " + tail[-1])

gui.hide(p.p)
gui.destroy(p.p)
root.destroy()
p.destroy()
host.deinit()
done()
