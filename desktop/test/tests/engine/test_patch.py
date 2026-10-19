"""Patch files (.ammo): the engine's chunk plus a `page {json}` line. Every
factory preset survives save -> file -> load with every parameter equal;
the page line is skipped by the engine; loading into a running engine
equals applying the preset; a file that is not a patch is refused."""
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403
import ctypes as C  # noqa: E402

print(f"== {DLL_PATH}")
DLL.ammo_preset_count.restype = C.c_int
DLL.ammo_preset_name.restype = C.c_char_p
DLL.ammo_preset_name.argtypes = [C.c_int]
DLL.ammo_apply_preset.argtypes = [C.c_void_p, C.c_int]

IDS = [p.id for p in INFOS]
names = [DLL.ammo_preset_name(k).decode() for k in range(DLL.ammo_preset_count())]
PAGE = {"v": 1, "looks": {"0": "ripple", "3": "glow"}, "vp": {"4096": 0.25, "4112": 0.9}, "view": "merged"}


def values(e):
    return np.array([e.get_id(i) for i in IDS], dtype=np.float32)


def patch_file(e):
    """What the plugin writes: the chunk, then the page's line."""
    return e.save() + "page " + json.dumps(PAGE, separators=(",", ":")) + "\n"


tmp = tempfile.mkdtemp(prefix="ammo_patch_")
for k, name in enumerate(names):
    a = Engine()
    DLL.ammo_apply_preset(a.p, k)
    a.render(0.2)
    path = os.path.join(tmp, f"{name}.ammo")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(patch_file(a))
    with open(path, encoding="utf-8") as f:
        text = f.read()
    b = Engine()
    ok = b.load(text)
    same = np.array_equal(values(a), values(b))
    page = [ln for ln in text.splitlines() if ln.startswith("page ")]
    check(ok and same and len(page) == 1 and json.loads(page[0][5:]) == PAGE,
          f"{name}: save -> file -> load, all {len(IDS)} parameters equal, the page line intact")
    a.close()
    b.close()

# the page line is invisible to the engine: a chunk with and without it loads the same
a = Engine()
DLL.ammo_apply_preset(a.p, names.index("Night groove"))
chunk = a.save()
b, c = Engine(), Engine()
b.load(chunk)
c.load(chunk + "page {\"looks\":{\"1\":\"breath\"}}\n")
check(np.array_equal(values(b), values(c)), "the page line changes no parameter")
# the page line anywhere in the file, even first after the header
c2 = Engine()
lines = chunk.splitlines()
c2.load("\n".join([lines[0], "page {}"] + lines[1:]) + "\n")
check(np.array_equal(values(b), values(c2)), "a page line between the header and the values is skipped too")
for x in (b, c, c2):
    x.close()

# loading into a running engine equals applying the preset: the same parameters, and from a fresh start the
# same audio
run = Engine()
DLL.ammo_apply_preset(run.p, names.index("Evening band"))
run.render(1.5)
run.load(chunk + "page {}\n")
ref = Engine()
DLL.ammo_apply_preset(ref.p, names.index("Night groove"))
check(np.array_equal(values(run), values(ref)), "Night groove loaded into an engine playing Evening band: every parameter as applying the preset")
f1, f2 = Engine(), Engine()
f1.load(chunk + "page {}\n")
DLL.ammo_apply_preset(f2.p, names.index("Night groove"))
L1, _ = f1.render(3.0)
L2, _ = f2.render(3.0)
check(L1.tobytes() == L2.tobytes() and rms(L1) > 0.01, f"a fresh engine from the file sounds as one given the preset (rms {rms(L1):.3f})")
for x in (run, ref, f1, f2, a):
    x.close()

# not a patch: refused, nothing changed
e = Engine()
before = values(e)
check(not e.load("page {}\n1 120\n") and not e.load("") and np.array_equal(before, values(e)),
      "a file without the header is refused and leaves every parameter as it was")
e.close()
done()
