"""DELAY page: one stereo delay per oscillator. Echo time from DIVISION and
TEMPO, FEEDBACK, SEND and master DELAY, WIDTH (parallel -> ping-pong),
TONE, WOBBLE, and that each oscillator only feeds its own delay."""
from arpsynth import *

DDIVS = ["1/32", "1/16", "1/16T", "1/8", "1/8T", "1/8.", "1/4", "1/4T", "1/4.", "1/2", "1/1"]
DEC_30MS = np.log2(6) / 9.64


def pluck(o=0, ddiv="1/8", fb=0.0, send=1.0, width=0.0, tone=1.0, wobble=0.0, bpm=120, octave=3):
    """Osc o alone: a short sine pluck every 2 s (LOOP 1/1), dry, reverb off,
    its delay set directly."""
    fresh()
    drone()
    solo(o)
    engine("DETUNE", 0, 0.0)
    engine("SHAPE", o, 0.0)
    engine("CUTOFF", o, 1.0)
    engine("WIDTH", o, 0.0)
    engine("LCUT_D", o, 0.0)
    engine("AMODE", o, st(1, 6))               # LOOP
    engine("ADIV", o, st(0, 9))                # 1/1: a hit every 2 s at 120
    engine("EAATT", o, 0.0)
    engine("EADEC", o, DEC_30MS)
    engine("EASUS", o, 0.0)
    engine("OCTAVE", o, st(octave + 1, 5))
    engine("TEMPO", 0, (bpm - 40) / 200)
    engine("MDELAY", 0, 0.5)                   # master 1x
    engine("DDIV", o, st(DDIVS.index(ddiv), 11))
    engine("DFEED", o, fb)
    engine("DSEND", o, send)
    engine("DWIDTH", o, width)
    engine("DTONE", o, tone)
    engine("DWOBBLE", o, wobble)
    for k in range(3):
        if k != o:
            engine("DSEND", k, 0.0)


def env_ms(x):
    """1 ms peak envelope."""
    return np.abs(x[: len(x) // 48 * 48]).reshape(-1, 48).max(axis=1)


def after_hit(seconds=1.9, **kw):
    """Audio from the second hit (one 1/1 step in; the first has died away)."""
    pluck(**kw)
    render(4 * 60 / kw.get("bpm", 120), 0)
    return render(seconds, 0)


def echo_level(e, t, width_ms=6):
    i = int(round(t * 1000))
    return float(e[i - width_ms:i + width_ms].max())


# ---- echo time = DIVISION of TEMPO
for ddiv, bpm, want in [("1/8", 120, 0.25), ("1/4", 120, 0.5), ("1/8", 60, 0.5), ("1/8.", 120, 0.375),
                        ("1/16T", 120, 1 / 12)]:
    l, r = after_hit(ddiv=ddiv, bpm=bpm)
    e = env_ms(l + r)
    lo = int(0.6 * want * 1000)
    t = (lo + int(np.argmax(e[lo:int(1.4 * want * 1000)]))) / 1000
    check(abs(t - want) < 0.003, f"{ddiv} at {bpm} BPM: first echo at {t * 1000:.0f} ms (want {want * 1000:.0f})")

# ---- SEND sets the first echo, FEEDBACK the ones after it
l, r = after_hit(ddiv="1/8", fb=np.float64(0.5 * 0.8 / 0.9), send=0.5)     # loop gain 0.5
e = env_ms(l + r)
dry, e1, e2, e3 = e[:10].max(), echo_level(e, 0.25), echo_level(e, 0.5), echo_level(e, 0.75)
check(abs(e1 / dry - 0.5) < 0.06, f"SEND 0.5: first echo at {e1 / dry:.2f} of the dry hit")
check(abs(e2 / e1 - 0.5) < 0.06 and abs(e3 / e2 - 0.5) < 0.06,
      f"FEEDBACK gain 0.5: each repeat {e2 / e1:.2f}, {e3 / e2:.2f} of the one before")
l, r = after_hit(ddiv="1/8", fb=0.0, send=0.0)
e = env_ms(l + r)
check(echo_level(e, 0.25) < 1e-4, "SEND 0: no echo")
pluck(ddiv="1/8", send=1.0)
engine("MDELAY", 0, 0.0)
render(2.0, 0)
l, r = render(1.0, 0)
check(echo_level(env_ms(l + r), 0.25) < 1e-4, "master DELAY 0: no echo")
pluck(ddiv="1/8", send=0.25)
engine("MDELAY", 0, 1.0)                       # 2x
render(2.0, 0)
l, r = render(1.0, 0)
e = env_ms(l + r)
check(abs(echo_level(e, 0.25) / e[:10].max() - 0.5) < 0.06, "master DELAY 2x doubles SEND 0.25 to 0.5")

# ---- WIDTH: 0 = the osc's own stereo image, 1 = ping-pong
l, r = after_hit(ddiv="1/8", fb=0.6, send=1.0, width=1.0)
el, er = env_ms(l), env_ms(r)
e1 = (echo_level(el, 0.25), echo_level(er, 0.25))
e2 = (echo_level(el, 0.5), echo_level(er, 0.5))
check(e1[1] < 0.01 * e1[0] and e2[0] < 0.01 * e2[1],
      f"WIDTH 1 ping-pong: echo 1 left ({e1[0]:.3f} / {e1[1]:.4f}), echo 2 right ({e2[0]:.4f} / {e2[1]:.3f})")
pluck(ddiv="1/8", fb=0.6, send=1.0, width=0.0)
engine("PAN", 0, 0.0)                          # osc hard left: its repeats stay left
render(2.0, 0)
l, r = render(1.0, 0)
el, er = env_ms(l), env_ms(r)
check(echo_level(er, 0.25) < 0.01 * echo_level(el, 0.25) and echo_level(er, 0.5) < 0.01 * echo_level(el, 0.5),
      "WIDTH 0: a hard-left oscillator's repeats stay left")

# ---- TONE darkens every repeat
def saw_pluck_bright(tone):
    pluck(ddiv="1/8", fb=0.7, send=1.0, tone=tone, octave=1)
    engine("SHAPE", 0, 1.0)
    render(2.0, 0)
    l, r = render(1.0, 0)
    x = (l + r)[int(0.75 * SR):int(0.78 * SR)]
    fr, sp = spectrum(x)
    return float((sp * fr).sum() / (sp.sum() + 1e-12))


b_hi, b_lo = saw_pluck_bright(1.0), saw_pluck_bright(0.0)
check(b_lo < 0.5 * b_hi, f"TONE 500 Hz vs 12 kHz: third echo centroid {b_lo:.0f} vs {b_hi:.0f} Hz")

# ---- WOBBLE: the echo spacing wanders
def spacing(wobble):
    pluck(ddiv="1/8", fb=0.85, send=1.0, wobble=wobble)
    render(2.0, 0)
    l, r = render(1.9, 0)
    t = onsets(l + r, rel=0.02, gap=0.1)
    return np.diff(t[:7]) * 1000


s0, s1 = spacing(0.0), spacing(1.0)
check(s0.std() < 0.05 and s1.std() > 0.1,
      f"WOBBLE: echo spacing spread {s0.std():.3f} ms at 0, {s1.std():.3f} ms at 1 (3 ms wander)")

# ---- each oscillator has its own delay
pluck(o=1, ddiv="1/8", send=0.0)               # osc 2 sounds, its SEND 0
engine("DSEND", 0, 1.0)                        # osc 1's delay wide open, but osc 1 is muted
render(2.0, 0)
l, r = render(1.0, 0)
check(echo_level(env_ms(l + r), 0.25) < 1e-4, "osc 2 does not reach osc 1's delay")

# ---- readouts
page("DELAY")
engine("DDIV", 0, st(5, 11))
engine("DFEED", 0, 0.9)
render(0.02, 0)
check((text(0), text(3)) == ("1/8.", "95"), f"DELAY readouts: {text(0)}, FEEDBACK {text(3)}")

done()
