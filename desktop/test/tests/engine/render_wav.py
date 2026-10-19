"""Render the engine offline to a WAV file, sample-exact, without playing live:

    python desktop\test\tests\engine\render_wav.py [seconds] [out.wav] [state.txt | patch.ammo]
    python desktop\test\tests\engine\render_wav.py --preset "Night groove" [seconds] [out.wav]

Default: 16 s of the default patch to desktop\build\default_patch.wav. A
state file (the engine's save chunk, or a patch file saved from the plugin)
loads a patch first; --preset takes a factory preset by name (the file
defaults to desktop\build\<preset>.wav)."""
import ctypes as C
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import BUILD, DLL, Engine  # noqa: E402

args = sys.argv[1:]
preset = None
if "--preset" in args:
    i = args.index("--preset")
    if i + 1 >= len(args):
        raise SystemExit('--preset needs a name, e.g. --preset "Night groove"')
    preset = args[i + 1]
    del args[i:i + 2]

seconds = float(args[0]) if len(args) > 0 else 16.0
default_name = (preset or "default_patch").replace(" ", "_").lower() + ".wav"
out = args[1] if len(args) > 1 else os.path.join(BUILD, default_name)
e = Engine()
if preset is not None:
    DLL.ammo_preset_count.restype = C.c_int
    DLL.ammo_preset_name.restype = C.c_char_p
    DLL.ammo_preset_name.argtypes = [C.c_int]
    DLL.ammo_apply_preset.argtypes = [C.c_void_p, C.c_int]
    names = [DLL.ammo_preset_name(k).decode() for k in range(DLL.ammo_preset_count())]
    match = [k for k, n in enumerate(names) if n.lower() == preset.lower()]
    if not match:
        raise SystemExit(f"no preset {preset!r}; the presets: {', '.join(names)}")
    DLL.ammo_apply_preset(e.p, match[0])
elif len(args) > 2:
    with open(args[2], encoding="utf-8") as f:
        assert e.load(f.read()), "bad state file"
L, R = e.render(seconds)
e.close()
pcm = (np.clip(np.stack([L, R], axis=1), -1, 1) * 32767).astype("<i2")
with wave.open(out, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(48000)
    w.writeframes(pcm.tobytes())
print(f"wrote {out}: {seconds:g} s{' of ' + preset if preset else ''}, peak {np.max(np.abs(L)):.3f}")
