"""The standalone host (AmmoniteTestHost.exe) with a variant of the test
plugin, driven like a user would: start it with N instances, wait for every
UI to report in (through $AMMO_TEST_LOG, shared by the host and the plugin
UIs), click into the plugin's UI, press Space and see whether the transport
toggled (the phase 1 keyboard question: does the key reach the host when the
plugin UI, a WebView2 for the web variant, has the focus?), screenshot, and
close the windows.

    python desktop\\test\\tests\\host_check.py web [instances]
    python desktop\\test\\tests\\host_check.py gl  [instances]

Windows show for a few seconds and the mouse is moved: keep hands off.
This one plays audio through the default output."""
import ctypes as C
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clap_host import BIN, BUILD, VARIANTS, check, done  # noqa: E402

variant = sys.argv[1] if len(sys.argv) > 1 else "web"
instances = int(sys.argv[2]) if len(sys.argv) > 2 else 1
exe = os.path.join(BIN, "AmmoniteTestHost.exe")
clap = os.path.join(BIN, VARIANTS[variant][0])
if not (os.path.exists(exe) and os.path.exists(clap)):
    print(f"skip: {exe} or {clap} not built (.\\desktop\\desktop.ps1 build)")
    raise SystemExit(0)
print(f"== {exe} {clap} -n {instances}")

LOG = os.path.join(BUILD, f"host_{variant}.log")
if os.path.exists(LOG):
    os.remove(LOG)
env = dict(os.environ, AMMO_TEST_LOG=LOG)

from PIL import ImageGrab  # noqa: E402

user32 = C.windll.user32
user32.SetProcessDPIAware()
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004
KEYEVENTF_KEYUP, VK_SPACE = 0x0002, 0x20
EnumWindowsProc = C.WINFUNCTYPE(C.c_bool, C.c_void_p, C.c_void_p)


def log_lines():
    try:
        with open(LOG, encoding="utf-8", errors="replace") as f:
            return [ln.split(" ", 1)[1].rstrip() if " " in ln else ln.rstrip() for ln in f]
    except FileNotFoundError:
        return []


def wait_for(pred, sec):
    t_end = time.time() + sec
    while time.time() < t_end:
        if pred(log_lines()):
            return True
        time.sleep(0.1)
    return False


def host_windows(pid):
    found = []

    def cb(hwnd, _):
        wpid = C.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, C.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            buf = C.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            if buf.value.startswith("Ammonite"):
                found.append((hwnd, buf.value))
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found


class RECT(C.Structure):
    _fields_ = [("l", C.c_long), ("t", C.c_long), ("r", C.c_long), ("b", C.c_long)]


class GUITHREADINFO(C.Structure):
    _fields_ = [("cbSize", C.c_ulong), ("flags", C.c_ulong), ("hwndActive", C.c_void_p), ("hwndFocus", C.c_void_p),
                ("hwndCapture", C.c_void_p), ("hwndMenuOwner", C.c_void_p), ("hwndMoveSize", C.c_void_p),
                ("hwndCaret", C.c_void_p), ("rcCaret", C.c_long * 4)]


def focus_report(hwnd):
    """Which window has the keyboard focus, as class names, for the log."""
    fg = user32.GetForegroundWindow()
    tid = user32.GetWindowThreadProcessId(hwnd, None)
    gti = GUITHREADINFO()
    gti.cbSize = C.sizeof(GUITHREADINFO)
    user32.GetGUIThreadInfo(tid, C.byref(gti))

    def cls(h):
        if not h:
            return "none"
        b = C.create_unicode_buffer(64)
        user32.GetClassNameW(C.c_void_p(h), b, 64)
        return b.value

    return f"foreground {'host' if fg == hwnd else cls(fg)}, focus {cls(gti.hwndFocus)}"


def client_rect_on_screen(hwnd):
    r = RECT()
    user32.GetClientRect(hwnd, C.byref(r))
    pt = C.c_long * 2
    p = pt(0, 0)
    user32.ClientToScreen(hwnd, C.byref(p))
    return p[0], p[1], r.r, r.b


proc = subprocess.Popen([exe, clap, "-n", str(instances)], env=env)
try:
    ok = wait_for(lambda ls: any(ln.startswith("host: running") for ln in ls), 15)
    check(ok, "the host started (plugin loaded, audio device opened, windows up)")
    wins = []
    for _ in range(50):
        wins = host_windows(proc.pid)
        if len(wins) >= instances:
            break
        time.sleep(0.1)
    check(len(wins) == instances, f"{len(wins)} host window(s) (want {instances})")
    ok = wait_for(lambda ls: sum(1 for ln in ls if "first tick" in ln) >= instances, 10)
    check(ok, f"every UI received telemetry ({instances})")
    if variant in ("web", "ammonite"):
        ok = wait_for(lambda ls: sum(1 for ln in ls if ln == "page ready") >= instances, 15)
        check(ok, f"every page loaded and reported ready ({instances})")
        gl = [ln for ln in log_lines() if ln.startswith("gl ")]
        check(len(gl) >= instances and all(g.startswith("gl ok") for g in gl), f"WebGL ok in every instance ({gl[:1]})")
    else:
        check(sum(1 for ln in log_lines() if "gl program linked" in ln) >= instances, "the GL shell program linked in every instance")
    audio = [ln for ln in log_lines() if ln.startswith("host: audio:")]
    print("     " + (audio[0] if audio else "no audio line"))
    time.sleep(1.5)

    # the first window: click into the plugin UI (the shell area), then Space
    hwnd, title = wins[-1]  # the last created is on top
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    x, y, w, h = client_rect_on_screen(hwnd)
    img = ImageGrab.grab(bbox=(x, y, x + w, y + h))
    shot = os.path.join(BUILD, f"host_{variant}.png")
    img.save(shot)
    arr = np.asarray(img).astype(int)
    lit = float(np.mean(arr.max(axis=2) > 40))
    check(lit > 0.01, f"the plugin UI is painted inside the host window ({100 * lit:.1f} % lit) -> {shot}")

    user32.SetCursorPos(x + w // 2, y + h // 2)
    time.sleep(0.1)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(0.3)
    print("     after clicking into the plugin UI: " + focus_report(hwnd))
    before = sum(1 for ln in log_lines() if ln.startswith("host: transport"))
    user32.keybd_event(VK_SPACE, 0, 0, 0)
    user32.keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.6)
    after = [ln for ln in log_lines() if ln.startswith("host: transport")]
    check(len(after) > before,
          f"Space with the plugin UI focused reaches the host: {after[-1] if after else 'no transport change'}")
    time.sleep(1.0)
    user32.keybd_event(VK_SPACE, 0, 0, 0)
    user32.keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.4)

    # the host window itself (title bar) focused: Space must work there in any case
    user32.SetCursorPos(x + w // 2, y - 12)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(0.3)
    print("     after clicking the title bar: " + focus_report(hwnd))
    before = sum(1 for ln in log_lines() if ln.startswith("host: transport"))
    user32.keybd_event(VK_SPACE, 0, 0, 0)
    user32.keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.6)
    after = [ln for ln in log_lines() if ln.startswith("host: transport")]
    check(len(after) > before, f"Space with the host window focused: {after[-1] if after else 'no transport change'}")
    time.sleep(1.5)

    if variant == "ammonite":
        # package A in the real plugin: R records (the UI tick drains the tap into a WAV), Ctrl+S opens the
        # native Save dialog (from uiIdle, outside the web view's message handler) without stalling the page
        user32.SetCursorPos(x + w // 2, y + h // 2)
        user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        time.sleep(0.3)
        VK_R, VK_CONTROL, VK_S = 0x52, 0x11, 0x53
        user32.keybd_event(VK_R, 0, 0, 0)
        user32.keybd_event(VK_R, 0, KEYEVENTF_KEYUP, 0)
        time.sleep(2.5)
        user32.keybd_event(VK_R, 0, 0, 0)
        user32.keybd_event(VK_R, 0, KEYEVENTF_KEYUP, 0)
        ok = wait_for(lambda ls: any(ln.startswith("recording stopped") for ln in ls), 3)
        started = [ln for ln in log_lines() if ln.startswith("recording ") and not ln.startswith("recording stopped")]
        stopped = [ln for ln in log_lines() if ln.startswith("recording stopped")]
        check(ok and started and stopped and " 0 frames dropped, ok" in stopped[-1],
              f"R, 2.5 s, R in the page: {stopped[-1] if stopped else 'no recording'}")
        if started:
            path = started[-1][len("recording "):]
            try:
                import soundfile as sf
                info = sf.info(path)
                check(info.samplerate > 0 and info.channels == 2 and info.subtype == "FLOAT" and 2.0 < info.duration < 3.5,
                      f"the WAV reads back: {info.samplerate} Hz, {info.duration:.2f} s, {info.subtype}")
            finally:
                if os.path.exists(path):
                    os.remove(path)  # not the user's: the test's own take

        ticks_before = sum(1 for ln in log_lines() if "ticks;" in ln)
        user32.keybd_event(VK_CONTROL, 0, 0, 0)
        user32.keybd_event(VK_S, 0, 0, 0)
        user32.keybd_event(VK_S, 0, KEYEVENTF_KEYUP, 0)
        user32.keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0)
        dialog = None
        for _ in range(40):
            time.sleep(0.1)
            found = []

            def cb(hwnd_, _):
                wpid = C.c_ulong()
                user32.GetWindowThreadProcessId(hwnd_, C.byref(wpid))
                b = C.create_unicode_buffer(64)
                user32.GetClassNameW(hwnd_, b, 64)
                if wpid.value == proc.pid and user32.IsWindowVisible(hwnd_) and b.value == "#32770":
                    found.append(hwnd_)
                return True

            user32.EnumWindows(EnumWindowsProc(cb), 0)
            if found:
                dialog = found[0]
                break
        check(dialog is not None, "Ctrl+S in the page opens the native Save dialog")
        time.sleep(1.5)  # the page keeps ticking under the dialog
        if dialog:
            user32.PostMessageW(C.c_void_p(dialog), 0x0010, 0, 0)  # WM_CLOSE: cancel
        time.sleep(0.8)
        ok = wait_for(lambda ls: sum(1 for ln in ls if "ticks;" in ln) > ticks_before, 8)
        check(ok and proc.poll() is None, "after the dialog is cancelled the host runs and the page still gets telemetry")

    tail = [ln for ln in log_lines() if "ticks" in ln]
    if tail:
        print("     " + tail[-1])
finally:
    for hwnd, _ in host_windows(proc.pid):
        user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    try:
        proc.wait(timeout=8)
        check(proc.returncode == 0, f"the host exited cleanly (code {proc.returncode})")
    except subprocess.TimeoutExpired:
        proc.kill()
        check(False, "the host did not exit after its windows closed (killed)")
done()
