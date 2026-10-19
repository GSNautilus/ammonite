"""The factory presets through the C API: names, every preset applies and
plays (finite, below 0 dBFS, something sounding except Init), and the
recipes differ from one another."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403
import ctypes as C  # noqa: E402

print(f"== {DLL_PATH}")
DLL.ammo_preset_count.restype = C.c_int
DLL.ammo_preset_name.restype = C.c_char_p
DLL.ammo_preset_name.argtypes = [C.c_int]
DLL.ammo_preset_blurb.restype = C.c_char_p
DLL.ammo_preset_blurb.argtypes = [C.c_int]
DLL.ammo_apply_preset.argtypes = [C.c_void_p, C.c_int]

n = DLL.ammo_preset_count()
names = [DLL.ammo_preset_name(k).decode() for k in range(n)]
check(names == ["Init", "First light", "Slow tide", "Clockwork", "Dub echoes", "Late set", "Evening band", "Night groove", "Bells in D",
                 "Slow bloom"], f"{n} presets: {names}")
check(all(len(DLL.ammo_preset_blurb(k)) > 20 for k in range(n)), "every preset has a blurb")

renders = {}
for k, name in enumerate(names):
    e = Engine()
    ok = DLL.ammo_apply_preset(e.p, k)
    L, R, fr = e.render(6.0, collect=True)
    active = sum(c.active for c in fr[-1].ch)
    hits = sum(1 for f in fr for c in f.ch if c.hit)
    renders[name] = L
    if name == "Init":
        check(ok and active == 0 and rms(L) == 0.0, f"{name}: nothing on, silence")
    else:
        check(ok and np.all(np.isfinite(L)) and np.max(np.abs(L)) < 1.0 and rms(L) > 0.01 and active >= 2 and hits >= 4,
              f"{name}: plays ({active} channels, {hits} hits in 6 s, rms {rms(L):.3f}, peak {np.max(np.abs(L)):.3f}, "
              f"{fr[-1].bpm:.0f} BPM)")
    e.close()

e = Engine()
DLL.ammo_apply_preset(e.p, names.index("Bells in D"))
check(e.get("root") == 2 and e.get("scale") == 0 and e.get("prog") == 7 and e.get("restart") == 2 and e.get("pitch_amt", 1) == 1.0,
      "Bells in D: D major, Canon, restart on the chord, pitch envelope +12 on the bells")
DLL.ammo_apply_preset(e.p, names.index("Init"))
check(e.get("root") == 9 and e.get("gen", 0) == 0 and e.get("tempo") == 110, "then Init: back to the table defaults, slots off")
e.close()

# the levelling: every playing preset at about -18 dB K-weighted (12 s after 2 s, the default patch's glue)
levels = {}
for k, name in enumerate(names):
    if name == "Init":
        continue
    e = Engine()
    DLL.ammo_apply_preset(e.p, k)
    e.render(2.0)
    L, R = e.render(12.0)
    levels[name] = loudness(L, R)
    e.close()
check(all(abs(v + 18) < 1.5 for v in levels.values()),
      "levelled: " + ", ".join(f"{n} {v:.1f}" for n, v in levels.items()) + " dB K (want -18 +- 1.5)")
e = Engine()
DLL.ammo_apply_preset(e.p, names.index("Dub echoes"))
check(abs(e.get("dly_mix", 0) - 0.6) < 1e-6 and abs(e.get("dly_fb", 0) - 0.7) < 1e-6 and e.get("dly_pp", 0) == 1 and e.get("dly_div", 0) == 5,
      "Dub echoes: its stab thrown into a dotted-eighth ping-pong echo (feedback 0.7, mix 0.6)")
e.close()
e = Engine()
DLL.ammo_apply_preset(e.p, names.index("Slow bloom"))
_, _, fr = e.render(7.0, collect=True)
built = max(f.ch[0].voices for f in fr)
check(e.get("tempo") == 36 and e.get("chord_rhythm", 0) == 2 and e.get("chord_ring", 0) == 1 and e.get("cho_mode", 0) == 1
      and e.get("dly_div", 0) == 9 and e.get("rev_send", 0) > 0 and built == 4,
      f"Slow bloom: 36 BPM, the chord arpeggiated with Let ring ({built} voices built up in the first bar), Ensemble, "
      "a half-note echo, reverb")
e.close()

recipes = [n for n in names if n not in ("Init",)]
diff = all(renders[a].tobytes() != renders[b].tobytes() for i, a in enumerate(recipes) for b in recipes[i + 1:])
check(diff, "every playing preset renders differently from the others")
check(not DLL.ammo_apply_preset(Engine().p, 99), "an unknown preset index is refused")
done()
