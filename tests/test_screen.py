"""Screen views (KEY page VIEW): RINGS (waveform rings, step arcs in the
note's fifths hue, rests grey, the playing step's dot moving, height
following the envelope), PITCH (each wave one hue: its current note),
WHEEL (chord tones in the fifths circle), SCOPE.
Checked on the rendered framebuffer."""
import colorsys
import ctypes

from arpsynth import *

fb = np.zeros(240 * 240, dtype=np.uint16)
RING_R = [102, 80, 58]


def frame(n=1):
    for _ in range(n):
        DLL.synth_render(fb.ctypes.data_as(ctypes.POINTER(ctypes.c_uint16)))
    v = fb.byteswap().reshape(240, 240).astype(np.int32)   # panel order -> RGB565
    return np.stack([((v >> 11) & 31) * 8, ((v >> 5) & 63) * 4, (v & 31) * 8], axis=-1)


def band(img, r0, r1, a0=0.0, a1=360.0):
    """Lit pixels between radii r0..r1 and angles a0..a1 (deg, 0 = 12 o'clock, clockwise)."""
    y, x = np.mgrid[0:240, 0:240]
    r = np.hypot(x - 120, y - 120)
    a = (np.degrees(np.arctan2(x - 120, 120 - y)) + 360) % 360
    m = (r >= r0) & (r <= r1) & (a >= a0) & (a < a1) & (img.sum(axis=-1) > 30)
    return img[m], np.stack([x[m], y[m]], axis=-1)


def hue(px):
    """Mean hue (0..1) of lit pixels, weighted by brightness."""
    hs = [colorsys.rgb_to_hsv(*(p / 255.0)) for p in px]
    ang = np.array([h[0] for h in hs]) * 2 * np.pi
    w = np.array([h[2] * h[1] for h in hs]) + 1e-9
    return float((np.arctan2((np.sin(ang) * w).sum(), (np.cos(ang) * w).sum()) / (2 * np.pi)) % 1)


def fifths_hue(pc): return (pc * 7 % 12) / 12


def near(h, want, tol=0.05): return min(abs(h - want), 1 - abs(h - want)) < tol


def one_osc(mode="LOOP", length=8, density=1.0, pool="ROOT", sus=0.0):
    """Osc 1 alone on A (root pool), 1/4 at 120; the other two silent and off."""
    fresh()
    for k in (1, 2):
        engine("AMODE", k, 0.0)
        engine("LEVEL", k, 0.0)
    engine("AMODE", 0, st(["OFF", "LOOP", "UP", "DOWN", "UP-DN", "RANDOM"].index(mode), 6))
    engine("APOOL", 0, st(["KEY", "TRIAD", "7TH", "FIFTHS", "ROOT"].index(pool), 5))
    engine("ADIV", 0, st(2, 9))
    engine("ALENGTH", 0, st(length - 1, 16))
    engine("ADENSITY", 0, density)
    engine("EASUS", 0, sus)
    engine("TEMPO", 0, (120 - 40) / 200)
    engine("MDELAY", 0, 0.0)
    engine("MREVERB", 0, 0.0)
    engine("VIEW", 0, st(0, 4))                # RINGS: the per-step arc checks below


# ---- VIEW: PITCH by default (user 2026-09-24), three rings drawn
fresh()
page("KEY")
check(text(9) == "PITCH", "VIEW defaults to PITCH")
render(2.0, 0)
img = frame(100)
lit = [len(band(img, R - 9, R + 9)[0]) for R in RING_R]
check(all(n > 300 for n in lit), f"three rings drawn ({lit} px)")

# ---- a step's arc takes its note's fifths hue (osc 1 plays only A)
one_osc()
render(4.1, 0)
img = frame(100)
px, _ = band(img, 95, 109)
h = hue(px)
check(near(h, fifths_hue(9)), f"osc 1 playing A: ring hue {h:.3f} (A in fifths order = {fifths_hue(9):.3f})")

# ---- DENSITY rests are grey arcs
one_osc(length=8, density=0.5)                 # hits on steps 0 2 4 6, rests on 1 3 5 7
render(4.1, 0)                                 # two whole bars: every step drawn once
img = frame(100)
rest_px, _ = band(img, 96, 108, 45 - 12, 45 + 12)      # step 1's arc
hit_px, _ = band(img, 96, 108, 90 - 12, 90 + 12)       # step 2's arc
sat = lambda p: float(np.mean([colorsys.rgb_to_hsv(*(q / 255.0))[1] for q in p])) if len(p) else 0
check(sat(rest_px) < 0.2 and sat(hit_px) > 0.6,
      f"rest arcs grey (saturation {sat(rest_px):.2f}), hit arcs colored ({sat(hit_px):.2f})")

# ---- the playing step's dot moves one step (360 / LENGTH degrees) per step
def big_dot_angle(img):
    """Angle of the brightest cluster on the outer ring (the playing step)."""
    px, xy = band(img, 94, 110)
    b = px.sum(axis=-1)
    top = xy[b >= np.percentile(b, 97)]
    ang = np.degrees(np.arctan2(top[:, 0] - 120, 120 - top[:, 1])) % 360
    return float(np.degrees(np.arctan2(np.sin(np.radians(ang)).mean(), np.cos(np.radians(ang)).mean())) % 360)


one_osc(length=8)
render(0.6, 0)                                 # step 1 (starts at 0.5 s)
a1 = big_dot_angle(frame(100))
render(0.5, 0)                                 # step 2
a2 = big_dot_angle(frame(1))
check(abs(a1 - 45) < 8 and abs(a2 - 90) < 8, f"playing dot at {a1:.0f} then {a2:.0f} deg (LENGTH 8: 45, 90)")

# ---- the waveform's height follows the envelope (pluck: tall at the hit, flat later)
def spread(img, R=102):
    _, xy = band(img, R - 9, R + 9, 10, 35)    # between the dots at 0 and 45 deg
    r = np.hypot(xy[:, 0] - 120, xy[:, 1] - 120)
    return float(r.std())


one_osc(length=8, density=0.5)                 # hits on even steps: a hit at 1.0 s (step 2)
render(1.02, 0)                                # 20 ms after the hit at 1.0 s
s_hit = spread(frame(100))
render(0.4, 0)                                 # 420 ms after: the pluck has died
s_late = spread(frame(1))
check(s_hit > 1.5 * s_late, f"ring spread {s_hit:.2f} px just after a hit, {s_late:.2f} px later")

# ---- a droning oscillator is one unbroken ring in its note's hue
one_osc(mode="OFF")
render(1.0, 0)
img = frame(100)
px, _ = band(img, 95, 109)
quarters = [len(band(img, 95, 109, a, a + 90)[0]) for a in (0, 90, 180, 270)]
check(min(quarters) > 60 and near(hue(px), fifths_hue(9)), f"drone ring unbroken {quarters}, hue of A")

# ---- PITCH: the same rings, but each wave in one hue, its oscillator's note
def quadrant_hues(img):
    # between the dots (LENGTH 8: a dot every 45 deg; the dots keep their step colors)
    return [hue(band(img, 95, 109, a + 12, a + 33)[0]) for a in (0, 90, 180, 270)]


def hue_spread(hs):
    return max(min(abs(a - b), 1 - abs(a - b)) for a in hs for b in hs)


one_osc(mode="UP", pool="TRIAD")               # A C E, one per quarter note
render(4.2, 0)                                 # every arc has played: A, C and E arcs
img = frame(100)
spread_rings = hue_spread(quadrant_hues(img))
engine("VIEW", 0, st(1, 4))
render(0.01, 0)                                # stepped values update on the audio thread
img = frame(1)
hs = quadrant_hues(img)
cur = int(round(note(0))) % 12
check(hue_spread(hs) < 0.03 and near(hs[0], fifths_hue(cur)) and spread_rings > 0.1,
      f"PITCH: one hue all round ({hs[0]:.3f}, note pc {cur} = {fifths_hue(cur):.3f}); RINGS spread {spread_rings:.2f}")
render(0.5, 0)                                 # next step, next note
img = frame(1)
cur2 = int(round(note(0))) % 12
check(cur2 != cur and near(quadrant_hues(img)[0], fifths_hue(cur2)), f"PITCH follows the note (pc {cur} -> {cur2})")
page("KEY")                                    # (the page map would cover the rings above)
check(text(9) == "PITCH", f"VIEW readout {text(9)}")

# ---- WHEEL: the chord's tones lit at their fifths-circle places
fresh()
engine("VIEW", 0, st(2, 4))
render(1.0, 0)                                 # A minor, chord I: A C E
img = frame(100)


def wheel_dot(pc, R=94):
    a = (pc * 7 % 12) * 30
    px, _ = band(img, R - 7, R + 7, a - 6, a + 6)
    return len(px)


chord = [wheel_dot(pc) for pc in (9, 0, 4)]    # A C E
other = [wheel_dot(pc) for pc in (1, 6, 8)]    # C# F# G#: not in A minor
check(min(chord) > 60 and max(other) < 15, f"WHEEL: chord tones lit {chord}, off-key notes tiny {other}")

# ---- SCOPE: traces across the middle
engine("VIEW", 0, st(3, 4))
render(0.5, 0)
img = frame(100)
row = img[100:140, 5:60].sum(axis=-1)
check((row > 30).sum() > 40, "SCOPE: a trace left of the centre")

done()
