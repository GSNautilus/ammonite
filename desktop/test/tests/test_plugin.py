"""The built test plugin, loaded like a DAW loads it (the CLAP host of
clap_host.py), one variant per run:

    python desktop\\test\\tests\\test_plugin.py web
    python desktop\\test\\tests\\test_plugin.py gl

Checks what a host relies on: the descriptor, the 2 parameters, playing at
44.1 / 48 / 96 kHz (a 110 Hz tone at the right level), Shape automation
changing the spectrum, two instances side by side with different Shape
giving independent output, state save / load, and the beat envelope
following the host transport. Needs .\\desktop\\desktop.ps1 build first.
The VST3 is the same DSP behind DPF's VST3 wrapper; it is checked in the DAWs."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clap_host import (ClapHost, VARIANTS, beat_times, check, clap_path, dominant_hz, done,  # noqa: E402
                       hf_ratio, rms)

variant = sys.argv[1] if len(sys.argv) > 1 else "web"
path = clap_path(variant)
if not os.path.exists(path):
    print(f"skip: {path} not built (.\\desktop\\desktop.ps1 build)")
    raise SystemExit(0)
_, plugin_id, plugin_name = VARIANTS[variant]
print(f"== {path}")

host = ClapHost(path)
check(host.count() == 1, "one plugin in the factory")
desc, feats = host.descriptor(0)
check(desc.id == plugin_id and desc.name == plugin_name and desc.vendor == b"GSNautilus" and "instrument" in feats,
      f"descriptor: {desc.id.decode()}, {desc.name.decode()} by {desc.vendor.decode()},"
      f" version {desc.version.decode()}, features {feats}")

# ---- parameters
a = host.create(plugin_id)
infos = a.param_infos()
names = [p.name.decode() for p in infos]
check(names == ["Shape", "Level"], f"2 parameters: {names}")
check(all(p.min_value == 0 and p.max_value == 1 for p in infos)
      and abs(infos[0].default_value - 0.25) < 1e-6 and abs(infos[1].default_value - 0.5) < 1e-6,
      "ranges 0..1, defaults Shape 0.25, Level 0.5")
SHAPE, LEVEL = infos[0].id, infos[1].id

# ---- plays: a 110 Hz tone at -12 dBFS (Level 0.5 -> peak 0.25)
check(a.activate(48000.0), "activate at 48 kHz, start processing")
a.set_param(SHAPE, 0.0)  # pure sine for the level and pitch checks
L, R = a.render(2.0)
peak = float(np.max(np.abs(L)))
check(np.all(np.isfinite(L)) and np.all(np.isfinite(R)) and L.tobytes() == R.tobytes() and 0.21 < peak < 0.29,
      f"plays at 48 kHz: finite, L == R, peak {peak:.3f} (want ~0.25 = -12 dBFS)")
f0 = dominant_hz(L[int(0.05 * 48000):int(0.55 * 48000)], 48000)
check(abs(f0 - 110) < 3, f"the tone is 110 Hz (measured {f0:.1f} Hz)")
beats = beat_times(L, 48000)
gap = float(np.median(np.diff(beats))) if len(beats) > 2 else 0
check(len(beats) >= 3 and abs(gap - 0.6) < 0.02,
      f"no host clock: the envelope retriggers every {gap * 1000:.0f} ms (free clock, 100 BPM = 600)")

# ---- Shape automation changes the spectrum
sine = hf_ratio(L[int(0.65 * 48000):int(1.15 * 48000)], 48000)
a.set_param(SHAPE, 0.7)  # saw
L, _ = a.render(1.3)
saw = hf_ratio(L[int(0.65 * 48000):int(1.15 * 48000)], 48000)
check(sine < 0.03 and saw > 0.08 and saw > 4 * sine,
      f"Shape automation: energy above 250 Hz {100 * sine:.1f} % (sine) -> {100 * saw:.1f} % (saw)")
check(abs(a.value(SHAPE) - 0.7) < 1e-6, "the host reads the automated value back (0.7)")

# ---- Level
a.set_param(SHAPE, 0.0)
a.set_param(LEVEL, 1.0)
L, _ = a.render(1.3)
pk1 = float(np.max(np.abs(L[int(0.1 * 48000):])))
a.set_param(LEVEL, 0.0)
L, _ = a.render(0.5)
check(0.44 < pk1 < 0.56 and rms(L[int(0.1 * 48000):]) < 1e-5,
      f"Level 1.0: peak {pk1:.3f} (want ~0.5); Level 0: silent after the 10 ms smoothing")
a.set_param(LEVEL, 0.5)

# ---- other rates
for sr in (44100.0, 96000.0):
    a.deactivate()
    ok = a.activate(sr)
    L, _ = a.render(1.0)
    f0 = dominant_hz(L[int(0.05 * sr):int(0.55 * sr)], sr)
    check(ok and np.all(np.isfinite(L)) and abs(f0 - 110) < 3 and 0.21 < float(np.max(np.abs(L))) < 0.29,
          f"re-activate at {sr / 1000:g} kHz: plays 110 Hz ({f0:.1f}), peak {float(np.max(np.abs(L))):.3f}")

# ---- two instances side by side: a sine and a saw, rendered alternately
b, c = host.create(plugin_id), host.create(plugin_id)
b.activate(48000.0)
c.activate(48000.0)
b.set_param(SHAPE, 0.0)
c.set_param(SHAPE, 0.9)
outs_b, outs_c = [], []
for _ in range(6):
    outs_b.append(b.render(0.25)[0])
    outs_c.append(c.render(0.25)[0])
Lb, Lc = np.concatenate(outs_b), np.concatenate(outs_c)
rb, rc = hf_ratio(Lb[int(0.65 * 48000):int(1.15 * 48000)], 48000), hf_ratio(Lc[int(0.65 * 48000):int(1.15 * 48000)], 48000)
check(rb < 0.03 and rc > 0.08 and rms(Lb) > 0.01 and rms(Lc) > 0.01,
      f"two instances alternating: one a sine ({100 * rb:.1f} % HF), the other a square-ish saw ({100 * rc:.1f} % HF)")

# ---- state: save c (Shape 0.9), load into a fresh one
c.set_param(LEVEL, 0.3)
c.render(0.1)
ok, blob = c.save()
d = host.create(plugin_id)
ok2 = d.load(blob)
check(ok and ok2 and len(blob) > 0 and abs(d.value(SHAPE) - 0.9) < 1e-4 and abs(d.value(LEVEL) - 0.3) < 1e-4,
      f"state: save ({len(blob)} bytes) and load restores Shape 0.9 and Level 0.3")

# ---- the host transport: the envelope follows the host's beats
s = host.create(plugin_id)
s.activate(48000.0)
s.set_param(SHAPE, 0.0)
L = s.render(3.0, host=(True, 120.0, 0.0, 4))[0]
t = beat_times(L[int(0.1 * 48000):], 48000) + 0.1
gap = float(np.median(np.diff(t))) if len(t) > 2 else 0
check(len(t) >= 4 and abs(gap - 0.5) < 0.015,
      f"CLAP transport at 120 BPM: the envelope retriggers every {gap * 1000:.0f} ms (want 500)")
L = s.render(2.0, host=(False, 150.0, 3.0, 4))[0]  # stopped: the free clock continues at the host's tempo
t = beat_times(L, 48000)
gap = float(np.median(np.diff(t))) if len(t) > 2 else 0
check(len(t) >= 3 and abs(gap - 0.4) < 0.015,
      f"transport stopped at 150 BPM: plays on, every {gap * 1000:.0f} ms (want 400)")

for x in (a, b, c, d, s):
    x.destroy()
host.deinit()
done()
