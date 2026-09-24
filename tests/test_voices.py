"""Three droning oscillators (arps OFF): key, chord degree, octave / degree, FINE,
level, filter type, pan / width, reverb send, master cutoff. Measured
from the audio, not the parameters."""
from arpsynth import *

_fresh = fresh


def fresh():
    """These checks are about the voices: all arps OFF (drones)."""
    _fresh()
    drone()


def solo_pitch(o, seconds=1.0):
    """Measured fundamental of oscillator o alone, as a sine."""
    solo(o)
    engine("DETUNE", 0, 0.0)                   # else two peaks, +-8..10 cents apart
    engine("SHAPE", o, 0.0)
    engine("CUTOFF", o, 1.0)
    l, r = render(seconds, 0.4)
    return peak_freq(l + r, 20, 2000)


def cents(f, ref): return 1200 * np.log2(f / ref)


# ---- default: A minor, chord I, degrees 0 / 2 / 4 = A2 C4 E5
fresh()
for o, m in enumerate([45, 60, 76]):
    f = solo_pitch(o)
    check(abs(cents(f, midi_hz(m))) < 5, f"osc {o + 1} sounds MIDI {m} ({f:.1f} Hz)")

# ---- ROOT, SCALE, CHORD through the panel
fresh()
page("MAIN")
setp(3, st(0, 12))                             # ROOT C
render(0.05, 0)
check([note(o) for o in range(3)] == [36, 51, 67], "ROOT C: C2 Eb3+12 G  (C minor triad)")
setp(4, st(0, 20))                             # SCALE MAJOR
render(0.05, 0)
check([note(o) for o in range(3)] == [36, 52, 67], "SCALE MAJOR: the third becomes E")
page("KEY")
setp(2, st(3, 7))                              # CHORD IV
render(0.05, 0)
check([note(o) for o in range(3)] == [41, 57, 72], "CHORD IV in C major: F A C (F major triad)")
setp(2, st(4, 7))                              # CHORD V
render(0.05, 0)
check([note(o) for o in range(3)] == [43, 59, 74], "CHORD V: G B D")
setp(2, st(6, 7))                              # CHORD VII
render(0.05, 0)
check([note(o) for o in range(3)] == [47, 62, 77], "CHORD VII: B D F (diminished, in key)")

# ---- chord keys: DEGREE counts chord tones, the chord moves along its parent scale
fresh()
page("MAIN")
setp(4, st(15, 20))                            # SCALE MIN7 (chord), tonic A
check(text(4) == "MIN7", "SCALE list continues into chords (MIN7)")
render(0.05, 0)
check([note(o) for o in range(3)] == [45, 64, 81], "A MIN7, degrees 0 2 4: A E(+12) A(+24)")
page("OSC")
setp(7, st(1, 8))                              # DEGREE osc 2 -> 1: the minor third
render(0.05, 0)
check(note(1) == 60, "DEGREE 1 on a MIN7 key = the minor third (C4)")
page("KEY")
setp(2, st(3, 7))                              # CHORD IV in parent A minor: D
render(0.05, 0)
check(note(0) == 50 and note(1) == 65, "MIN7 on IV: D minor-7 shape (D3, F4)")

# ---- KEY OCTAVE, FINE (measured)
fresh()
page("KEY")
setp(4, st(3, 5))                              # OCTAVE +1
setp(5, 1.0)                                   # FINE +50 cents
f = solo_pitch(0)
check(abs(cents(f, midi_hz(57.5))) < 5, f"KEY OCTAVE +1, FINE +50: A3 + 50 cents ({f:.1f} Hz)")

# ---- LEVEL mutes, per oscillator
fresh()
engine("MREVERB", 0, 0.0)
page("MAIN")
setp(1, 0.0)
setp(2, 0.0)
l, r = render(0.5)
m = l + r
check(band_level(m, midi_hz(45)) > 50 * max(band_level(m, midi_hz(60)), 1e-9),
      "LEVEL 2 and 3 at 0: only osc 1 (A2) is left")

# ---- FILTER TYPE: LP keeps the fundamental, HP at 1 kHz removes it
fresh()
solo(0)                                        # A2 saw, 110 Hz
page("FILTER")
setp(0, 0.54)                                  # CUTOFF 1.2 kHz
l, r = render(0.5)
lp = band_level(l + r, 110)
setp(6, 1.0)                                   # TYPE HP
check(text(6) == "HP", "TYPE readout HP")
l, r = render(0.5)
hp = band_level(l + r, 110)
check(hp < lp * 0.05, f"HP at 1.2 kHz cuts 110 Hz by > 26 dB ({20 * np.log10(hp / lp):.1f} dB)")

# ---- master CUTOFF darkens everything
fresh()
engine("MREVERB", 0, 0.0)


def centroid():
    l, r = render(0.5)
    fr, sp = spectrum(l + r)
    return float((sp * fr).sum() / sp.sum())


c_mid = centroid()
page("MAIN")
setp(6, 0.0)                                   # master CUTOFF -3 oct
c_low = centroid()
check(c_low < c_mid * 0.6, f"master CUTOFF -3 oct: centroid {c_mid:.0f} -> {c_low:.0f} Hz")

# ---- PAN and WIDTH
fresh()
solo(0)
page("MIX")
setp(0, 0.0)                                   # PAN hard left
setp(3, 0.0)                                   # WIDTH 0
l, r = render(0.5)
check(rms(r) < 0.001 * rms(l), "PAN hard left: right channel silent")
setp(0, 0.5)
l, r = render(0.5)
check(np.corrcoef(l, r)[0, 1] > 0.999, "PAN centre, WIDTH 0: L = R")
setp(3, 1.0)                                   # WIDTH 1: the two oscillators split L / R
l, r = render(0.5)
c = np.corrcoef(l, r)[0, 1]
check(c < 0.9, f"WIDTH 1: L and R differ (correlation {c:.2f})")

# ---- reverb send: a tail after the oscillators stop
def tail(send):
    fresh()
    for o in range(3):
        engine("RSEND", o, send)
    engine("MREVERB", 0, 0.5)
    engine("MDELAY", 0, 0.0)                   # the reverb's tail only, no repeats
    render(1.0)
    for o in range(3):
        engine("LEVEL", o, 0.0)
    render(0.1, 0)
    l, r = render(0.3, 0)
    return rms(l) + rms(r)


t0, t1 = tail(0.0), tail(1.0)
check(t0 < 1e-6 and t1 > 0.005, f"REV SEND: send 0 stops dead ({t0:.1e}), send 1 leaves a tail ({t1:.3f})")

# ---- power-on is dry: no delay or reverb tail once the oscillators stop
_fresh()                                       # the real defaults (arps on)
render(3.0, 0)
for o in range(3):
    engine("LEVEL", o, 0.0)
render(0.05, 0)
l, r = render(1.0, 0)
check(np.abs(l).max() + np.abs(r).max() < 1e-6, "defaults: effects off (nothing rings on after the notes)")
page("LFO")
sub(1)
check([text(3 + o) for o in range(3)] == ["0", "0", "0"], "defaults: CUTOFF LFO depth 0")

# ---- level sanity at defaults
fresh()
l, r = render(2.0)
pk = max(np.abs(l).max(), np.abs(r).max())
check(np.isfinite(l).all() and 0.05 < pk < 1.0, f"defaults: clean, peak {pk:.2f}")

done()
