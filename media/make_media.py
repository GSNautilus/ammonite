"""
README media, all rendered from the engine itself (sim\\synthcore.dll).

  demo.mp4        a scripted one-minute performance: the round screen and its
                  sound, knob readouts and page maps appearing as it plays
  screen.gif      a silent loop of the same screen for the README
  audio\\*.mp3     the power-on sound and every recipe from the manual
  views.png       the four VIEWs side by side
  pages.png       the nine page maps
  panel.png       the simulator panel
  banner.png      the README header, banner.html screenshot by headless Edge
  enclosure.png   a render of the printed enclosure (hardware\\stl)

Needs ffmpeg on PATH. Run:  python media\\make_media.py [demo] [audio] [images]
"""
import ctypes
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "manual"))

import capture as cap  # noqa: E402  (loads the DLL, panel helpers)
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

DLL = cap.DLL
F = ctypes.c_float
SR = 48000
FRAME = cap.FRAME                       # audio samples per screen frame (38 ms)
FPS = f"{SR}/{FRAME}"                   # the screen's real frame rate, 26.3 fps
VIDEO = 720                             # screen size in the video (3x, crisp)


def find_ffmpeg():
    """$env:FFMPEG, else the first ffmpeg on PATH that actually runs (a conda
    environment can carry a copy that is missing its DLLs)."""
    cands = [os.environ.get("FFMPEG")] + [os.path.join(d, "ffmpeg.exe")
                                          for d in os.environ.get("PATH", "").split(os.pathsep)]
    for c in cands:
        if c and os.path.isfile(c):
            try:
                if subprocess.run([c, "-version"], capture_output=True).returncode == 0:
                    return c
            except OSError:
                pass
    raise SystemExit("no working ffmpeg found (set FFMPEG to ffmpeg.exe)")


FFMPEG = find_ffmpeg()


class Recorder:
    """Advances the engine one screen frame at a time, keeping the audio and
    (optionally) streaming each frame to ffmpeg."""

    def __init__(self, video_path=None):
        self.audio = []
        self.ff = None
        if video_path:
            pad = 800
            self.ff = subprocess.Popen(
                [FFMPEG, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                 "-s", "240x240", "-framerate", FPS, "-i", "-",
                 "-vf", f"scale={VIDEO}:{VIDEO}:flags=neighbor,pad={pad}:{pad}:(ow-iw)/2:(oh-ih)/2:black",
                 "-c:v", "libx264", "-preset", "slow", "-tune", "animation", "-pix_fmt", "yuv420p",
                 "-crf", "30", video_path],
                stdin=subprocess.PIPE)

    def frame(self):
        buf = np.zeros(2 * FRAME, dtype=np.float32)
        k = 0
        while k < FRAME:
            m = min(256, FRAME - k)
            DLL.synth_process(buf[2 * k:].ctypes.data_as(ctypes.POINTER(F)), m)
            k += m
        self.audio.append(buf)
        DLL.synth_render(cap.fb.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)))
        if self.ff:
            self.ff.stdin.write(cap.fb_rgb().tobytes())

    def play(self, seconds):
        for _ in range(int(round(seconds * SR / FRAME))):
            self.frame()

    def sweep(self, knob, target, seconds=0.8):
        """Turn a knob (1-12 as printed) smoothly to `target`, as a hand
        would: it takes over after a small move, then glides there."""
        i = knob - 1
        start = float(DLL.synth_get_slot_value(i)) if i < 10 else cap.ps.params[i]
        nudge = start + (0.03 if target >= start else -0.03)
        DLL.synth_set_pot(i, F(nudge))
        n = max(1, int(seconds * SR / FRAME))
        for s in range(1, n + 1):
            x = s / n
            v = nudge + (target - nudge) * (0.5 - 0.5 * np.cos(np.pi * x))
            DLL.synth_set_pot(i, F(v))
            cap.ps.params[i] = v
            self.frame()

    def page(self, name):
        cap.page(name)
        self.frame()

    def close(self):
        if self.ff:
            self.ff.stdin.close()
            self.ff.wait()
        return np.concatenate(self.audio)


def write_wav(path, stereo, peak_db=-1.0):
    """Interleaved float stereo -> 16-bit WAV, normalised to peak_db. (The
    engine runs at VOLUME 50 with headroom to spare; this is the volume
    knob turned up, nothing else.)"""
    gain = 10 ** (peak_db / 20) / max(1e-9, float(np.abs(stereo).max()))
    pcm = np.clip(stereo * gain, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((pcm * 32767).astype("<i2").tobytes())


def fade_out(stereo, seconds):
    n = int(seconds * SR) * 2
    stereo = stereo.copy()
    stereo[-n:] *= np.repeat(np.linspace(1, 0, n // 2), 2)
    return stereo


# ---------------------------------------------------------------- the demo
def demo():
    tmp_video = os.path.join(HERE, "_video.mp4")
    tmp_wav = os.path.join(HERE, "_audio.wav")
    cap.fresh()
    r = Recorder(tmp_video)
    r.play(6.0)                          # power-on: three arps in A minor
    r.page("KEY")
    r.play(1.2)
    r.sweep(1, cap.st(1, 9), 0.9)        # PROG -> POP: the chords start moving
    r.play(6.0)
    r.page("DELAY")
    r.play(1.0)
    r.sweep(8, 0.45, 0.7)                # OSC 2's delay SEND
    r.sweep(9, 0.35, 0.7)                # OSC 3's delay SEND
    r.play(4.0)
    r.page("MIX")
    r.play(0.8)
    for k in (7, 8, 9):                  # reverb sends
        r.sweep(k, 0.4, 0.5)
    r.sweep(10, 0.85, 0.6)               # SIZE
    r.play(4.0)
    r.page("ARP")
    r.play(0.8)
    r.sweep(10, cap.st(2, 3), 0.6)       # section CHANCE
    r.play(0.6)
    r.sweep(9, 0.25, 0.6)                # OSC 3 RATCHET
    r.sweep(6, 0.3, 0.6)                 # OSC 3 VARY
    r.play(5.0)
    r.page("KEY")
    r.play(0.8)
    r.sweep(10, cap.st(0, 4), 0.6)       # VIEW -> RINGS
    r.play(6.0)
    r.page("MAIN")
    r.play(0.8)
    r.sweep(7, 0.2, 2.5)                 # master CUTOFF down...
    r.play(1.0)
    r.sweep(7, 0.5, 2.5)                 # ...and back
    r.play(3.0)
    r.page("KEY")
    r.play(0.8)
    r.sweep(10, cap.st(2, 4), 0.6)       # VIEW -> WHEEL
    r.play(7.0)
    audio = fade_out(r.close(), 3.0)
    write_wav(tmp_wav, audio)
    out = os.path.join(HERE, "demo.mp4")
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", tmp_video, "-i", tmp_wav,
                    "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest", out], check=True)
    # the README loop: power-on, the progression starting, the delays arriving
    gif = os.path.join(HERE, "screen.gif")
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-ss", "1", "-t", "12", "-i", tmp_video,
                    "-vf", "fps=13,scale=400:400:flags=lanczos,split[a][b];"
                           "[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=none",
                    gif], check=True)
    os.remove(tmp_video)
    os.remove(tmp_wav)
    print("demo:", out, f"{os.path.getsize(out) / 1e6:.1f} MB;", gif, f"{os.path.getsize(gif) / 1e6:.1f} MB")


# ---------------------------------------------------------------- audio
def audio():
    from recipes import RECIPES
    folder = os.path.join(HERE, "audio")
    os.makedirs(folder, exist_ok=True)
    jobs = [("power_on", "Power-on", [])] + [(x["name"], x["title"], x["moves"]) for x in RECIPES]
    for name, title, moves in jobs:
        cap.fresh()
        for pg, sec, knob, v in moves:
            cap.page(pg)
            if DLL.synth_get_num_subs(DLL.synth_get_page()) > 1:
                cap.sub(sec)
            cap.turn(knob - 1, v)
        r = Recorder()
        r.play(40.0)
        wav = os.path.join(folder, f"{name}.wav")
        write_wav(wav, fade_out(r.close(), 4.0))
        mp3 = os.path.join(folder, f"{name}.mp3")
        subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", wav, "-b:a", "192k",
                        "-metadata", f"title=Ammonite - {title}", "-metadata", "artist=Ammonite",
                        mp3], check=True)
        os.remove(wav)
        print("audio:", mp3)


# ---------------------------------------------------------------- images
def disc(name, size):
    im = Image.open(os.path.join(ROOT, "manual", "img", f"screen_{name}.png")).convert("RGBA")
    return im.resize((size, size), Image.LANCZOS)


def label_font(px):
    try:
        return ImageFont.truetype(r"C:\Windows\Fonts\bahnschrift.ttf", px)
    except OSError:
        return ImageFont.load_default()


def montage(names, labels, cols, out, size=300, gap=36):
    rows = (len(names) + cols - 1) // cols
    W = cols * size + (cols + 1) * gap
    H = rows * (size + 44) + (rows + 1) * gap - rows * 8
    im = Image.new("RGB", (W, H), (17, 18, 20))
    d = ImageDraw.Draw(im)
    f = label_font(22)
    for k, (n, lab) in enumerate(zip(names, labels)):
        x = gap + (k % cols) * (size + gap)
        y = gap + (k // cols) * (size + 44 + gap - 8)
        d.ellipse((x - 5, y - 5, x + size + 5, y + size + 5), fill=(44, 47, 52))
        im.paste(disc(n, size), (x, y), disc(n, size))
        w = d.textlength(lab, font=f)
        d.text((x + (size - w) / 2, y + size + 12), lab, font=f, fill=(200, 203, 208))
    im.save(out)
    print("image:", out)


ENCLOSURE_PARTS = {           # part: color (PLA-ish off-white, the front plate a touch warmer)
    "front_panel": (0.84, 0.80, 0.74), "top_rail": (0.90, 0.89, 0.86),
    "front_rail": (0.90, 0.89, 0.86), "back_rail": (0.90, 0.89, 0.86),
    "bottom_panel": (0.78, 0.77, 0.74), "back_panel": (0.88, 0.87, 0.84)}


def render_stl(parts, out, width=1600, height=1000, azim=-58, elev=26, ss=2, bg=(255, 255, 255),
               explode=None):
    """A small z-buffer renderer: orthographic view, Lambert shading with a
    key and a fill light, 2x supersampled. Enough for clean product shots of
    the printed parts without a 3D package. `explode` offsets parts
    ({part: (dx, dy, dz)} in mm)."""
    import trimesh
    az, el = np.radians(azim), np.radians(elev)
    # camera basis: view direction from spherical angles (z up)
    fwd = -np.array([np.cos(el) * np.cos(az), np.cos(el) * np.sin(az), np.sin(el)])
    right = np.cross(fwd, [0, 0, 1.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    key = -np.array([0.35, -0.55, -0.75])
    key /= np.linalg.norm(key)
    fill = np.array([-0.6, 0.4, 0.3])
    fill /= np.linalg.norm(fill)
    tris, cols = [], []
    for name, base in parts.items():
        m = trimesh.load(os.path.join(ROOT, "hardware", "stl", f"{name}.stl"))
        t = m.triangles.copy()
        if explode and name in explode:
            t += np.array(explode[name])
        n = m.face_normals
        shade = 0.58 + 0.40 * np.clip(n @ key, 0, 1) + 0.12 * np.clip(n @ fill, 0, 1)
        tris.append(t)
        cols.append(np.clip(np.outer(shade, base), 0, 1))
    tris = np.concatenate(tris)
    cols = np.concatenate(cols)
    # project
    pts = tris.reshape(-1, 3)
    X, Y, Z = pts @ right, pts @ up, pts @ fwd
    W, H = width * ss, height * ss
    span = max((X.max() - X.min()) / W, (Y.max() - Y.min()) / H) * 1.08
    cx, cy = (X.max() + X.min()) / 2, (Y.max() + Y.min()) / 2
    px = ((X - cx) / span + W / 2).reshape(-1, 3)
    py = (H / 2 - (Y - cy) / span).reshape(-1, 3)
    pz = Z.reshape(-1, 3)
    zbuf = np.full((H, W), np.inf)
    img = np.empty((H, W, 3))
    img[:] = np.array(bg) / 255.0
    for k in range(len(tris)):
        x0, x1 = int(max(0, np.floor(px[k].min()))), int(min(W - 1, np.ceil(px[k].max())))
        y0, y1 = int(max(0, np.floor(py[k].min()))), int(min(H - 1, np.ceil(py[k].max())))
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        (ax_, bx, cx_), (ay, by, cy_) = px[k], py[k]
        den = (by - cy_) * (ax_ - cx_) + (cx_ - bx) * (ay - cy_)
        if abs(den) < 1e-12:
            continue
        l1 = ((by - cy_) * (gx - cx_) + (cx_ - bx) * (gy - cy_)) / den
        l2 = ((cy_ - ay) * (gx - cx_) + (ax_ - cx_) * (gy - cy_)) / den
        l3 = 1 - l1 - l2
        inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
        if not inside.any():
            continue
        z = l1 * pz[k, 0] + l2 * pz[k, 1] + l3 * pz[k, 2]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        win = inside & (z < sub)
        sub[win] = z[win]
        img[y0:y1 + 1, x0:x1 + 1][win] = cols[k]
    # a soft contact shadow is overkill; a hairline outline is not: darken
    # pixels where depth jumps (silhouettes and part edges)
    zf = np.where(np.isinf(zbuf), zbuf[np.isfinite(zbuf)].max() + 50, zbuf)
    edge = np.zeros((H, W), bool)
    edge[1:, :] |= np.abs(np.diff(zf, axis=0)) > 2.0
    edge[:, 1:] |= np.abs(np.diff(zf, axis=1)) > 2.0
    img[edge] *= 0.62
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    im = im.resize((width, height), Image.LANCZOS)
    im.save(out)
    print("image:", out)


def enclosure_render(out):
    render_stl(ENCLOSURE_PARTS, out)


def banner(out):
    """The README header: banner.html (the listening page's header) screenshot
    by headless Edge at 2x, transparent outside its rounded card."""
    import time
    edge = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
    src = os.path.join(HERE, "banner.html")
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([edge, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--force-device-scale-factor=2", "--window-size=1200,300",
                    "--default-background-color=00000000", "--virtual-time-budget=5000",
                    f"--screenshot={out}", "file:///" + src.replace("\\", "/")],
                   check=True, capture_output=True)
    for _ in range(120):   # msedge.exe returns before the file is written
        if os.path.exists(out) and os.path.getsize(out) > 0:
            time.sleep(0.5)
            break
        time.sleep(0.5)
    print("image:", out)


def images():
    banner(os.path.join(HERE, "banner.png"))
    montage(["view_pitch", "view_rings", "view_wheel", "view_scope"],
            ["PITCH", "RINGS", "WHEEL", "SCOPE"], 4, os.path.join(HERE, "views.png"))
    names = ["main", "osc", "arp", "envelope", "filter", "lfo", "delay", "mix", "key"]
    montage([f"pagemap_{n}" for n in names], [f"{k + 1}  {n.upper()}" for k, n in enumerate(names)],
            3, os.path.join(HERE, "pages.png"), size=260)
    Image.open(os.path.join(ROOT, "manual", "img", "panel_main_idle.png")).save(os.path.join(HERE, "panel.png"))
    print("image:", os.path.join(HERE, "panel.png"))
    enclosure_render(os.path.join(HERE, "enclosure.png"))


if __name__ == "__main__":
    want = sys.argv[1:] or ["images", "audio", "demo"]
    if "images" in want:
        images()
    if "audio" in want:
        audio()
    if "demo" in want:
        demo()
