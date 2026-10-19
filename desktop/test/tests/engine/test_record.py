"""Recording (engine/Record.hpp): the master output through the record tap
into a 32-bit float WAV, drained by another thread as the plugin's UI does.
The file is the rendered output bit for bit, at 64 and 4096 frames per
block, with no frame dropped; soundfile reads it back at the right rate and
length; only what is rendered while armed is in it."""
import os
import sys
import tempfile

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403
import ctypes as C  # noqa: E402

print(f"== {DLL_PATH}")
DLL.ammo_preset_count.restype = C.c_int
DLL.ammo_preset_name.restype = C.c_char_p
DLL.ammo_preset_name.argtypes = [C.c_int]
DLL.ammo_apply_preset.argtypes = [C.c_void_p, C.c_int]
DLL.ammo_record_start.restype = C.c_int
DLL.ammo_record_start.argtypes = [C.c_void_p, C.c_char_p]
DLL.ammo_record_stop.restype = C.c_longlong
DLL.ammo_record_stop.argtypes = [C.c_void_p]
DLL.ammo_record_dropped.restype = C.c_uint
DLL.ammo_record_dropped.argtypes = [C.c_void_p]

names = [DLL.ammo_preset_name(k).decode() for k in range(DLL.ammo_preset_count())]
tmp = tempfile.mkdtemp(prefix="ammo_rec_")
SECS = 4.0

for sr, block, max_block in [(48000, 64, 512), (48000, 4096, 4096), (44100, 512, 512)]:
    e = Engine(sr=sr, max_block=max_block)
    DLL.ammo_apply_preset(e.p, names.index("Night groove"))
    e.render(0.5, block=block)
    path = os.path.join(tmp, f"rec {sr} {block} Night groove.wav")  # spaces, as the plugin's names have
    ok = DLL.ammo_record_start(e.p, path.encode("utf-8"))
    L, R = e.render(SECS, block=block)
    frames = DLL.ammo_record_stop(e.p)
    dropped = DLL.ammo_record_dropped(e.p)
    e.render(0.5, block=block)  # after the stop: not in the file
    n = int(SECS * sr)
    check(ok == 1 and frames == n and dropped == 0,
          f"{sr} Hz, {block} frames per block: {frames} frames written of {n}, {dropped} dropped")
    data, rate = sf.read(path, dtype="float32", always_2d=True)
    info = sf.info(path)
    check(rate == sr and data.shape == (n, 2) and info.subtype == "FLOAT",
          f"soundfile reads it: {rate} Hz, {data.shape[0]} frames x {data.shape[1]}, {info.subtype}")
    check(data[:, 0].tobytes() == L.tobytes() and data[:, 1].tobytes() == R.tobytes() and rms(L) > 0.01,
          f"the file is the rendered output, bit for bit (rms {rms(L):.3f})")
    e.close()

# a second recording on the same engine starts clean (nothing left over from the first)
e = Engine()
DLL.ammo_apply_preset(e.p, names.index("Evening band"))
for i in range(2):
    path = os.path.join(tmp, f"again {i}.wav")
    DLL.ammo_record_start(e.p, path.encode("utf-8"))
    L, R = e.render(1.0)
    DLL.ammo_record_stop(e.p)
    e.render(0.3)  # between recordings: dropped from both
    data, _ = sf.read(path, dtype="float32", always_2d=True)
    check(data.shape[0] == len(L) and data[:, 0].tobytes() == L.tobytes(), f"recording {i + 1} of 2 on one engine: exactly its own second")
check(DLL.ammo_record_stop(e.p) == -1, "stopping when nothing records says so (-1)")
check(DLL.ammo_record_start(e.p, os.path.join(tmp, "no such folder", "x.wav").encode()) == 0,
      "a path that cannot be written is refused")
e.close()
done()
