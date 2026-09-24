"""
Ammonite panel simulator.

Same layout as the printed panel (12 pots, round GC9A01 center), same pixels
as the hardware (the C++ core renders the identical RGB565 framebuffer the
real display receives), same audio path (DaisySP running in synthcore.dll).

Run:  simulator.bat in the repository root (builds the DLL, then starts this).

Controls: drag a knob vertically (or scroll over it). Esc / close to quit.

Knob 11 selects the page; the caption under each knob shows what it does on
the active page (and which oscillator: one column per osc on column pages).
Knob 10 picks the sub-page on ARP, ENVELOPE, LFO and DELAY. A knob whose
stored value differs from its position (after a page change) shows the
stored value in brackets until the knob is moved past the pickup threshold.
"""
import ctypes
import os
import sys

import numpy as np
import pygame
import sounddevice as sd

HERE = os.path.dirname(os.path.abspath(__file__))
DLL = ctypes.CDLL(os.path.join(HERE, "synthcore.dll"))
DLL.synth_get_pot.restype = ctypes.c_float
DLL.synth_get_slot_value.restype = ctypes.c_float
DLL.synth_get_slot_name.restype = ctypes.c_char_p
DLL.synth_get_page_name.restype = ctypes.c_char_p
DLL.synth_get_sub_name.restype = ctypes.c_char_p

SR = 48000
FPS_SCREEN_MS = 38          # ~26 fps: the SPI frame time with FAST_DISPLAY_SPI (74 without)
WINW, WINH = 1400, 860      # panel aspect from the printed plate

# Normalized (x, y) centers measured off the panel screenshot.
KNOBS = [
    (0.130, 0.372), (0.240, 0.366), (0.352, 0.362),   # 0-2  left top
    (0.130, 0.610), (0.240, 0.603), (0.352, 0.597),   # 3-5  left bottom
    (0.660, 0.358), (0.766, 0.353), (0.873, 0.350),   # 6-8  right top
    (0.663, 0.592), (0.768, 0.587), (0.874, 0.582),   # 9-11 right bottom
]
SCREEN_C = (0.502, 0.478)
SCREEN_PX = 280             # rendered diameter of the active area
KNOB_R = 40

COL_BG = (24, 26, 30)
COL_PANEL = (46, 50, 56)
COL_EDGE = (70, 76, 84)
COL_KNOB = (28, 30, 34)
COL_RING = (110, 118, 128)
COL_MARK = (235, 238, 242)
COL_TEXT = (150, 156, 164)
COL_HELD = (214, 168, 90)   # stored value differs from the pot (not picked up)

params = [0.5] * 12         # pot positions; the core's defaults after init


def push(i):
    DLL.synth_set_pot(i, ctypes.c_float(params[i]))


def slot_name(i):
    name = DLL.synth_get_slot_name(i).decode("ascii")
    osc = DLL.synth_get_slot_osc(i)
    return f"{name} {osc}" if osc else name


def page_label():
    page = DLL.synth_get_page()
    label = f"{page + 1}  {DLL.synth_get_page_name(page).decode('ascii')}"
    sub = DLL.synth_get_sub_name(page, DLL.synth_get_sub(page)).decode("ascii")
    return f"{label} > {sub}" if sub else label


def audio_cb(outdata, frames, time_info, status):
    DLL.synth_process(
        outdata.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), frames)


def fb_to_surface(fb, size):
    v = fb.byteswap().reshape(240, 240)  # panel byte order -> native RGB565
    rgb = np.empty((240, 240, 3), dtype=np.uint8)
    rgb[..., 0] = ((v >> 11) & 0x1F).astype(np.uint8) << 3
    rgb[..., 1] = ((v >> 5) & 0x3F).astype(np.uint8) << 2
    rgb[..., 2] = (v & 0x1F).astype(np.uint8) << 3
    surf = pygame.surfarray.make_surface(rgb.swapaxes(0, 1))
    return pygame.transform.smoothscale(surf, (size, size))


def screen_frame(fb, mask):
    """Render the core's next frame into fb and return it as a round surface."""
    DLL.synth_render(fb.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)))
    surf = fb_to_surface(fb, SCREEN_PX).convert_alpha()
    surf.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MULT)
    return surf


def screen_mask():
    mask = pygame.Surface((SCREEN_PX, SCREEN_PX), pygame.SRCALPHA)
    pygame.draw.circle(mask, (255, 255, 255, 255),
                       (SCREEN_PX // 2, SCREEN_PX // 2), SCREEN_PX // 2)
    return mask


def knob_positions():
    return [(int(x * WINW), int(y * WINH)) for x, y in KNOBS]


def screen_pos():
    return (int(SCREEN_C[0] * WINW), int(SCREEN_C[1] * WINH))


def draw_panel(win, font, small, screen_surf):
    """The whole panel: plate, screen, page label, knobs with captions.
    (Also used by manual/capture.py for the manual's images.)"""
    kpos, spos = knob_positions(), screen_pos()
    win.fill(COL_BG)
    panel = pygame.Rect(30, 40, WINW - 60, WINH - 80)
    pygame.draw.rect(win, COL_PANEL, panel, border_radius=14)
    pygame.draw.rect(win, COL_EDGE, panel, width=2, border_radius=14)

    # screen bezel + live frame
    pygame.draw.circle(win, (12, 12, 14), spos, SCREEN_PX // 2 + 14)
    pygame.draw.circle(win, COL_EDGE, spos, SCREEN_PX // 2 + 14, 2)
    win.blit(screen_surf,
             (spos[0] - SCREEN_PX // 2, spos[1] - SCREEN_PX // 2))

    # active page (and sub-page), top center of the panel
    setlbl = font.render(page_label(), True, COL_MARK)
    win.blit(setlbl, (spos[0] - setlbl.get_width() // 2, 52))

    # knobs
    for i, (kx, ky) in enumerate(kpos):
        pygame.draw.circle(win, COL_KNOB, (kx, ky), KNOB_R)
        pygame.draw.circle(win, COL_RING, (kx, ky), KNOB_R, 2)
        # pointer: 270-degree travel, min at 7:30, max at 4:30
        ang = np.deg2rad(-135.0 + params[i] * 270.0)
        tip = (kx + KNOB_R * 0.78 * np.sin(ang),
               ky - KNOB_R * 0.78 * np.cos(ang))
        pygame.draw.line(win, COL_MARK, (kx, ky), tip, 4)
        lbl = font.render(str(i + 1), True, COL_TEXT)
        win.blit(lbl, (kx - lbl.get_width() // 2, ky - KNOB_R - 22))
        stored = float(DLL.synth_get_slot_value(i))
        if i == 10:  # PAGE selector: no stored value
            txt, col = f"{params[i]:.2f}", COL_TEXT
        elif abs(stored - params[i]) > 0.005:
            txt, col = f"{params[i]:.2f} [{stored:.2f}]", COL_HELD
        else:
            txt, col = f"{params[i]:.2f}", COL_TEXT
        val = small.render(txt, True, col)
        win.blit(val, (kx - val.get_width() // 2, ky + KNOB_R + 8))
        cap = small.render(slot_name(i), True, COL_TEXT)
        win.blit(cap, (kx - cap.get_width() // 2, ky + KNOB_R + 24))


def make_fonts():
    return (pygame.font.SysFont("consolas", 15),
            pygame.font.SysFont("consolas", 12))


def main():
    pygame.init()
    win = pygame.display.set_mode((WINW, WINH))
    pygame.display.set_caption("Ammonite simulator - knob 11 = PAGE")
    font, small = make_fonts()
    clock = pygame.time.Clock()

    DLL.synth_init(ctypes.c_float(float(SR)))
    for i in range(12):
        params[i] = float(DLL.synth_get_pot(i))
        push(i)  # like the firmware's first ADC pass: the panel = boot state

    fb = np.zeros(240 * 240, dtype=np.uint16)
    mask = screen_mask()

    stream = sd.OutputStream(samplerate=SR, channels=2, dtype="float32",
                             blocksize=256, callback=audio_cb)
    stream.start()

    kpos = knob_positions()
    drag = None
    screen_surf = None
    last_frame = 0

    running = True
    while running:
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                running = False
            elif e.type == pygame.KEYDOWN and e.key == pygame.K_ESCAPE:
                running = False
            elif e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
                for i, (kx, ky) in enumerate(kpos):
                    if (e.pos[0] - kx) ** 2 + (e.pos[1] - ky) ** 2 < KNOB_R ** 2:
                        drag = (i, e.pos[1], params[i])
            elif e.type == pygame.MOUSEBUTTONUP and e.button == 1:
                drag = None
            elif e.type == pygame.MOUSEMOTION and drag is not None:
                i, y0, v0 = drag
                params[i] = max(0.0, min(1.0, v0 + (y0 - e.pos[1]) * 0.005))
                push(i)
            elif e.type == pygame.MOUSEWHEEL:
                mx, my = pygame.mouse.get_pos()
                for i, (kx, ky) in enumerate(kpos):
                    if (mx - kx) ** 2 + (my - ky) ** 2 < KNOB_R ** 2:
                        params[i] = max(0.0, min(1.0, params[i] + e.y * 0.02))
                        push(i)

        now = pygame.time.get_ticks()
        if screen_surf is None or now - last_frame >= FPS_SCREEN_MS:
            screen_surf = screen_frame(fb, mask)
            last_frame = now

        draw_panel(win, font, small, screen_surf)
        pygame.display.flip()
        if os.environ.get("SIM_SHOT"):  # self-test: save one frame and exit
            pygame.image.save(win, os.environ["SIM_SHOT"])
            running = False
        clock.tick(60)

    stream.stop()
    stream.close()
    pygame.quit()


if __name__ == "__main__":
    main()
