"""
Screen captures for the Ammonite manual.

Drives synthcore.dll headless exactly like a player would (knobs, pages,
pickup), runs the audio forward so the arps actually play, and saves:

  img/panel_*.png   the simulator window (cropped to the knobs + screen)
  img/screen_*.png  the round screen alone, 4x nearest-neighbour (crisp
                    pixels, the framebuffer the real display receives)

Run:  python capture.py            (every shot)
      python capture.py arp cover  (only shots whose name contains a word)
"""
import os
import sys

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "sim"))
sys.path.insert(0, os.path.join(HERE, "..", "tests"))

import ctypes  # noqa: E402

import numpy as np  # noqa: E402
import pygame  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

import panel_sim as ps  # noqa: E402  (loads sim\synthcore.dll)
from arpsynth import FN, PAGES  # noqa: E402

DLL = ps.DLL
DLL.synth_get_value_text.restype = ctypes.c_char_p
F = ctypes.c_float
OUT = os.path.join(HERE, "img")
FRAME = int(ps.SR * ps.FPS_SCREEN_MS / 1000)   # audio frames per screen frame
SCALE = 4                                      # screen-only upscale
CROP = (80, 225, 1320, 615)                    # knobs + screen of the sim window
PAGEPOT, SUBPOT = 10, 9

fb = np.zeros(240 * 240, dtype=np.uint16)
_audio = np.zeros(2 * 256, dtype=np.float32)


# ---- driving the panel
def fresh():
    """Power-on: every knob at its boot position (the core's defaults)."""
    DLL.synth_init(F(float(ps.SR)))
    for i in range(12):
        ps.params[i] = float(DLL.synth_get_pot(i))
        ps.push(i)


def show_stored():
    """Draw knobs 1-10 where their stored values are (as if picked up), so
    the captions show no [held] values. Display only: nothing is sent."""
    for i in range(10):
        ps.params[i] = float(DLL.synth_get_slot_value(i))


def turn(i, v):
    """Turn knob i to v like a hand. After a page change a knob only takes
    over once it has moved a little, so it is swung past v and back."""
    for _ in range(3):
        DLL.synth_set_pot(i, F(v + (0.1 if v < 0.5 else -0.1)))
        DLL.synth_set_pot(i, F(v))
        if abs(float(DLL.synth_get_slot_value(i)) - v) < 1e-4:
            break
    ps.params[i] = v


def page(name):
    k = PAGES.index(name)
    ps.params[PAGEPOT] = (k + 0.5) / len(PAGES)
    ps.push(PAGEPOT)
    assert DLL.synth_get_page() == k
    show_stored()


def sub(k):
    n = DLL.synth_get_num_subs(DLL.synth_get_page())
    turn(SUBPOT, (k + 0.5) / n)
    assert DLL.synth_get_sub(DLL.synth_get_page()) == k
    show_stored()


def engine(func, osc, v):
    DLL.synth_set_engine_param(FN[func], osc, F(v))


def st(k, n):
    return (k + 0.5) / n


def run(seconds, meter=None):
    """Play for `seconds`, rendering a screen frame every 38 ms like the
    hardware (the readout and maps fade by frame count). `meter` (a dict)
    collects the output's peak and RMS."""
    for _ in range(max(1, int(seconds * 1000 / ps.FPS_SCREEN_MS))):
        left = FRAME
        while left:
            m = min(256, left)
            DLL.synth_process(_audio.ctypes.data_as(ctypes.POINTER(F)), m)
            if meter is not None:
                a = _audio[:2 * m]
                meter["peak"] = max(meter.get("peak", 0.0), float(np.abs(a).max()))
                meter["sq"] = meter.get("sq", 0.0) + float(np.sum(a.astype(np.float64) ** 2))
                meter["n"] = meter.get("n", 0) + 2 * m
            left -= m
        DLL.synth_render(fb.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)))


QUIET = 3.6  # seconds: readout / map gone (66 + 20 frames) and a little more


# ---- saving
def fb_rgb():
    v = fb.byteswap().reshape(240, 240)
    rgb = np.empty((240, 240, 3), dtype=np.uint8)
    rgb[..., 0] = ((v >> 11) & 0x1F).astype(np.uint8) << 3
    rgb[..., 1] = ((v >> 5) & 0x3F).astype(np.uint8) << 2
    rgb[..., 2] = (v & 0x1F).astype(np.uint8) << 3
    return rgb


def save_screen(name):
    """The round screen, crisp pixels, transparent outside the circle."""
    size = 240 * SCALE
    img = Image.fromarray(fb_rgb()).resize((size, size), Image.NEAREST).convert("RGBA")
    big = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(big).ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
    img.putalpha(big.resize((size, size), Image.LANCZOS))
    img.save(os.path.join(OUT, f"screen_{name}.png"))


_win = None


def save_panel(name):
    """The simulator window as the player sees it, cropped to the plate."""
    global _win
    if _win is None:
        pygame.init()
        _win = pygame.display.set_mode((ps.WINW, ps.WINH))
    font, small = ps.make_fonts()
    ps.draw_panel(_win, font, small, ps.screen_frame(fb, ps.screen_mask()))
    tmp = os.path.join(OUT, f"panel_{name}.png")
    pygame.image.save(_win, tmp)
    Image.open(tmp).crop(CROP).save(tmp)


# ---- the shots
def shot_cover():
    fresh()
    run(7.3)
    save_screen("cover")


def shot_views():
    """The four VIEWs of the power-on patch, no overlay."""
    for view, name in enumerate(("rings", "pitch", "wheel", "scope")):
        fresh()
        engine("VIEW", 0, st(view, 4))
        run(6.1)
        save_screen(f"view_{name}")


def shot_arp():
    """The DENSITY readout, then the rings showing the rests it made."""
    fresh()
    run(1.0)
    page("ARP")
    sub(2)
    run(0.3)
    turn(2, 0.5)   # osc 3's DENSITY (knob 3) to 3 of its 6 steps
    run(0.5)
    save_screen("arp_readout_density")
    run(QUIET)
    save_screen("arp_density_rings")


def shot_pages():
    """Every page as the player first sees it (PAGE just turned: the page
    map on the screen), and every section's map."""
    for name in PAGES:
        fresh()
        run(1.0)
        page(name)
        run(0.6)
        low = name.lower()
        save_panel(f"page_{low}")
        save_screen(f"pagemap_{low}")
        p = DLL.synth_get_page()
        n = DLL.synth_get_num_subs(p)
        if n > 1:
            for k in range(n):
                sub(k)
                run(0.6)
                sname = DLL.synth_get_sub_name(p, k).decode().lower()
                save_screen(f"sub_{low}_{sname}")
                if k == n - 1:
                    save_panel(f"page_{low}_{sname}")


def shot_panel():
    """Power-on, the view with no overlay; and two knob readouts."""
    fresh()
    run(QUIET + 1.5)
    save_panel("main_idle")
    turn(5, 0.40)                       # TEMPO (knob 6) to 120 BPM
    run(0.4)
    save_screen("readout_tempo")
    page("FILTER")
    run(QUIET)
    turn(1, 0.58)                       # OSC 2's CUTOFF (knob 2)
    run(0.4)
    save_screen("readout_cutoff")


def shot_prog():
    """A progression running: POP in A minor, on its third chord."""
    fresh()
    engine("PROG", 0, st(1, 9))
    run(5.6)
    save_screen("key_prog")


def shot_recipes():
    """Each recipe dialled in through the panel; the readouts go to
    recipes.json for the manual's tables."""
    import json
    from recipes import RECIPES
    out = {}
    for r in RECIPES:
        fresh()
        rows = []
        for pg, sec, knob, v in r["moves"]:
            page(pg)
            if DLL.synth_get_num_subs(DLL.synth_get_page()) > 1:
                sub(sec)
            turn(knob - 1, v)
            osc = DLL.synth_get_slot_osc(knob - 1)
            p = DLL.synth_get_page()
            rows.append(dict(page=pg, section=DLL.synth_get_sub_name(p, DLL.synth_get_sub(p)).decode()
                             if DLL.synth_get_num_subs(p) > 1 else "",
                             knob=knob, name=DLL.synth_get_slot_name(knob - 1).decode(), osc=osc,
                             text=DLL.synth_get_value_text(knob - 1).decode()))
        engine("VIEW", 0, st(r["view"], 4))
        meter = {}
        run(r["seconds"], meter)
        save_screen(f"recipe_{r['name']}")
        rms = (meter["sq"] / meter["n"]) ** 0.5
        print(f"  {r['name']}: peak {meter['peak']:.2f}, rms {rms:.3f}")
        out[r["name"]] = dict(rows=rows, peak=meter["peak"], rms=rms)
    with open(os.path.join(HERE, "recipes.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)


SHOTS = {n[5:]: f for n, f in sorted(globals().items()) if n.startswith("shot_")}


def main():
    os.makedirs(OUT, exist_ok=True)
    want = sys.argv[1:]
    for name, fn in SHOTS.items():
        if not want or any(w in name for w in want):
            print("shot", name)
            fn()


if __name__ == "__main__":
    main()
