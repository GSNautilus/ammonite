"""The parameter table through the C API: count, unique and stable ids (a
golden list), groups, ranges, defaults, hidden ids, set / get, step names,
the default patch, save / load; the modulation pool's ids (its LFOs and
macros host parameters, its routes saved but not a host's), version 2's
retired ids."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine_api import *  # noqa: E402,F403

print(f"== {DLL_PATH} ({DLL.ammo_version().decode()})")

ids = [p.id for p in INFOS]
check(len(ids) == len(set(ids)), f"{len(ids)} parameters, ids unique")
check(len(GLOBAL) == 32 and GLOBAL["tempo"] == 0 and GLOBAL["volume"] == 10 and GLOBAL["macro4"] == 14,
      f"32 global parameters at ids 0..31: {sorted(GLOBAL.items(), key=lambda kv: kv[1])[:4]} ...")
check(len(SLOT_PARAMS) == 69 and pid("gen", 0) == 64 and pid("gen", 1) == 192 and pid("glide", 15) == 64 + 128 * 15 + 34,
      "69 parameters per slot; slot blocks of 128 from id 64 (slot 1 gen = 64, slot 2 gen = 192)")
# which notes a pattern plays: the next free slot indices after the retired 66..89
check(pid("note_shift", 0) == 64 + 90 and pid("note_count", 0) == 64 + 91 and pid("seed", 0) == 64 + 92
      and pid("rhy_rotate", 0) == 64 + 41,
      "Shift, Count, Seed at slot indices 90, 91, 92; Rotate (every generator's now) still 41")
si = info_id(pid("seed", 0))
check(si.steps == 65 and si.default == 0 and step_name(pid("seed", 0), 0) == "Free" and step_name(pid("seed", 0), 64) == "64"
      and step_name(pid("note_count", 0), 2) == "Hits" and info_id(pid("note_shift", 0)).max == 15,
      "Seed: Free, 1..64; Count: Auto, Steps, Hits; Shift 0..15")
# the pool's host parameters: per LFO in use and its six settings, the macros' in-use flags and macros 5..16
check(len(POOL) == 32 * 7 + 16 + 12 and len(INFOS) == 32 + 16 * 69 + len(POOL) == DLL.ammo_param_count(),
      f"the pool: {len(POOL)} host parameters (32 LFOs x 7, 16 in-use flags, macros 5..16); {len(INFOS)} host parameters in all")

# the golden ids: these never move
GOLDEN = {"tempo": 0, "sync": 1, "swing": 2, "root": 3, "scale": 4, "prog": 5, "change": 6, "chord": 7, "restart": 8,
          "octave": 9, "volume": 10, "macro1": 11,
          # phase 4, package B: the shared reverb
          "rev_size": 15, "rev_decay": 16, "rev_damp": 17, "rev_pre": 18, "rev_width": 19, "rev_return": 20,
          # phase 4, package B: the master bus
          "glue": 21, "ceiling": 22,
          # the custom progression
          "prog_len": 23, "prog_s1": 24, "prog_s2": 25, "prog_s8": 31}
GOLDEN_SLOT = {"gen": 0, "inst": 1, "level": 2, "pan": 3, "mute": 4, "show": 5, "degree": 6, "oct": 7, "arp_mode": 8,
               "arp_pool": 9, "arp_range": 10, "arp_div": 11, "arp_len": 12, "arp_gate": 13, "arp_density": 14,
               "arp_vary": 15, "arp_ratchet": 16, "shape": 17, "detune": 18, "sub": 19, "noise": 20, "cutoff": 21,
               "reso": 22, "filter_type": 23, "drive": 24, "amp_a": 25, "amp_d": 26, "amp_s": 27, "amp_r": 28,
               "filt_amt": 29, "filt_a": 30, "filt_d": 31, "pitch_amt": 32, "pitch_d": 33, "glide": 34,
               # phase 4: chord and bass
               "chord_notes": 35, "chord_voicing": 36, "chord_lead": 37, "chord_rhythm": 38, "chord_strum": 39,
               "bass_pattern": 40,
               # phase 4: rhythm and drum
               "rhy_rotate": 41, "rhy_chance": 42, "rhy_pitch": 43, "drum_model": 44, "drum_tune": 45, "drum_decay": 46,
               "drum_tone": 47, "drum_snap": 48,
               # phase 4, package B: the inserts
               "fx_drive": 49, "fx_tone": 50, "cho_on": 51, "cho_rate": 52, "cho_depth": 53, "cho_mix": 54, "dly_div": 55,
               "dly_fb": 56, "dly_pp": 57, "dly_damp": 58, "dly_mix": 59,
               # phase 4, package B: the sends
               "rev_send": 60, "duck_src": 61, "duck_amt": 62,
               # phase 4: chorus modes, the chord arp
               "cho_mode": 63, "chord_ring": 64,
               # modulation: fine tune (66..89, version 2's LFOs and routes, are retired)
               "tune": 65}
# the modulation pool (2112 ..): LFO l at 2112 + 8 l (in use, shape, sync, rate, division, phase, restart, a spare),
# the macros' in-use flags at 2368 + m, macros 5..16 at 2384 + m, route r at 2400 + 4 r (source, target, amount,
# polarity)
GOLDEN_POOL = {"lfo1_on": 2112, "lfo1_shape": 2113, "lfo1_sync": 2114, "lfo1_rate": 2115, "lfo1_div": 2116, "lfo1_phase": 2117,
               "lfo1_restart": 2118, "lfo2_on": 2120, "lfo32_on": 2360, "lfo32_restart": 2366, "macro1_on": 2368, "macro16_on": 2383,
               "macro5": 2388, "macro16": 2399}
GOLDEN_ROUTES = {2400: "route1_src", 2401: "route1_target", 2402: "route1_amt", 2403: "route1_pol", 2404: "route2_src", 2911: "route128_pol"}
check(all(GLOBAL[k] == v for k, v in GOLDEN.items()), "global ids match the golden list")
check(all(SLOT_PARAMS[k] == v for k, v in GOLDEN_SLOT.items()), "slot offsets match the golden list")
check(all(POOL[k] == v for k, v in GOLDEN_POOL.items()), "pool ids match the golden list")
rinfo = {i: info_id(i) for i in GOLDEN_ROUTES}
check(all(rinfo[i] and rinfo[i].symbol.decode() == s and rinfo[i].flags & 16 and not rinfo[i].flags & 4 for i, s in GOLDEN_ROUTES.items())
      and not any(p.symbol.startswith(b"route") for p in INFOS),
      "the routes: at their golden ids, flagged saved-not-host (16), and not among the host's parameters")
lr = info_id(lfo_id(0, "rate"))
check(lr.flags & 1 and not info_id(lfo_id(0, "on")).flags & 1 and info_id(macro_id(9)).flags & 1 and info_id(lfo_id(0, "on")).flags & 8
      and info_id(lfo_id(0, "restart")).steps == 17 and step_name(lfo_id(0, "restart"), 3) == "Notes 3",
      "an LFO's settings and the macros are automatable; in use is not (boolean, like the eye); Restart: Free, Notes 1..16")
retired = [32, 41, pid("gen", 0) + 66, pid("gen", 15) + 89, LFO_BASE + 7, MACRO_VAL_BASE, NUM_IDS - 1]
check(all(info_id(i) and info_id(i).flags & 4 for i in retired) and info_id(32).symbol == b"glfo1_shape",
      "retired and reserved ids are hidden: version 2's global LFOs (32..41) and per-slot LFOs and routes (66..89), "
      "an LFO's spare, 2384..2387, the pool's end")

names = {p.symbol.decode(): p for p in INFOS}
t = names["tempo"]
check(t.min == 20 and t.max == 240 and t.default == 110 and t.steps == 0 and t.group == b"global",
      f"Tempo: {t.min:.0f}..{t.max:.0f} default {t.default:.0f}, continuous, global")
m = names["s3_arp_mode"]
check(m.name == b"3 Arp mode" and m.steps == 6 and m.group == b"slot 3" and m.flags & 2,
      f"slot 3 Arp mode: '{m.name.decode()}', 6 steps, group '{m.group.decode()}'")
check(step_name(pid("arp_mode", 2), 4) == "Up-down" and step_name(pid("scale"), 6) == "Harmonic minor"
      and step_name(pid("oct", 0), -1) == "-1" and step_name(pid("root"), 9) == "A" and step_name(pid("tempo"), 0) is None,
      "step names: Up-down, Harmonic minor, -1 (plain integer), A; none for a continuous one")
show = names["s1_show"]
check(not (show.flags & 1) and (show.flags & 8), "Show (the eye) is not automatable and boolean")

e = Engine()
check(abs(e.get("tempo") - 110) < 1e-6 and e.get("root") == 9 and e.get("scale") == 1,
      f"defaults: tempo {e.get('tempo'):.0f}, root A ({e.get('root'):.0f}), Minor ({e.get('scale'):.0f})")
check(e.get("sync") == 1, f"default Sync: Internal ({e.get('sync'):.0f}), the Tempo knob whatever the host plays")
check(e.get("gen", 0) == 1 and e.get("gen", 1) == 1 and e.get("gen", 2) == 1 and e.get("gen", 3) == 0
      and e.get("arp_mode", 0) == 2 and e.get("arp_pool", 0) == 3 and e.get("arp_mode", 2) == 5 and abs(e.get("arp_density", 2) - 0.6) < 1e-6,
      "the default patch: slots 1-3 on (Up Fifths, Up-down Triad, Random Key density 0.6), slot 4 off")

e.set("tempo", 999)
e.set("arp_mode", 3.4, 5)
e.set("pan", -7, 5)
check(e.get("tempo") == 240 and e.get("arp_mode", 5) == 3 and e.get("pan", 5) == -1,
      "set clamps to the range and rounds stepped values (tempo 999 -> 240, mode 3.4 -> 3, pan -7 -> -1)")
hidden = GLOBAL_IDS + 100  # a reserved id in slot 1's block
e.set_id(hidden, 0.7)
check(e.get_id(hidden) == 0.0, "a reserved (hidden) id ignores writes")
e.set_id(pid("gen", 0) + 78, 3)
check(e.get_id(pid("gen", 0) + 78) == 0.0, "so does a retired one (version 2's slot 1 route 1 source)")
e.set_id(99999, 1.0)
check(e.get_id(99999) == 0.0 and e.get_id(-1) == 0.0, "ids out of range are ignored")

# save / load
e.set("tempo", 123)
e.set("cutoff", 0.33, 7)
e.set("gen", 1, 7)
blob = e.save()
lines = blob.splitlines()
check(lines[0] == "ammonite-desktop 4" and f"{pid('tempo')} 123" in blob and f"{pid('cutoff', 7)} 0.33" in blob
      and f"{pid('gen', 7)} 1" in blob and not any(ln.startswith(f"{pid('gen', 3)} ") for ln in lines),
      f"save: a versioned text, {len(lines) - 1} non-default values")
f = Engine()
f.set("tempo", 77)
f.set("gen", 1, 9)
ok = f.load(blob)
check(ok and f.get("tempo") == 123 and abs(f.get("cutoff", 7) - 0.33) < 1e-6 and f.get("gen", 7) == 1 and f.get("gen", 9) == 0
      and f.get("arp_mode", 0) == 2 and f.get("arp_density", 2) == e.get("arp_density", 2),
      "load: restores the saved values, resets the rest, keeps the default patch")
check(not f.load("nonsense") and f.get("tempo") == 123, "a bad chunk is refused and changes nothing")
# the pool saves with the patch: an LFO, a macro past the host's four, a route (not a host parameter, saved all the same)
e.lfo(4, "Drift", "Free", rate=0.3, restart=2)
e.set_id(macro_id(6), 0.4)
e.set_id(MACRO_ON_BASE + 6, 1)
e.route(77, SRC_LFO1 + 4, pid("cutoff", 2), -0.35, pol=0)
blob = e.save()
f.load("ammonite-desktop 3\n")
ok = f.load(blob)
check(ok and f"{route_id(77, 'amt')} -0.35" in blob and f.get_id(route_id(77, "src")) == SRC_LFO1 + 4 and f.get_id(route_id(77, "target")) == pid("cutoff", 2)
      and abs(f.get_id(route_id(77, "amt")) + 0.35) < 1e-6 and f.get_id(route_id(77, "pol")) == 0 and f.get_id(lfo_id(4, "shape")) == 6
      and f.get_id(lfo_id(4, "restart")) == 2 and abs(f.get_id(macro_id(6)) - 0.4) < 1e-6 and f.get_id(MACRO_ON_BASE + 6) == 1,
      "the pool saves and loads: an LFO's settings, Macro 7 and its flag, a route (unipolar, -0.35 on channel 3's cutoff)")
check(f.get_id(route_id(0, "target")) == -1 and f.get_id(route_id(0, "pol")) == 1, "a route as it comes: no target, bipolar")
# a version 1 file (before 4/1, 2/1 and the dotted divisions): its divisions keep their note values
v1 = f"ammonite-desktop 1\n{pid('arp_div', 0)} 0\n{pid('arp_div', 1)} 3\n{pid('arp_div', 2)} 8\n"
ok = f.load(v1)
got = [DIVS[int(f.get("arp_div", s))] for s in range(4)]
check(ok and got == ["1/1", "1/4T", "1/32", "1/4"], f"a version 1 file's divisions: 1/1 1/4T 1/32 and the default 1/4 -> {got}")
e.close()
f.close()
done()
