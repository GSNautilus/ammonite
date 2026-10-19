/** The clock: what the host tells us about a block, and the one-master-clock
 *  arithmetic from the hardware (step indices derived from the absolute beat
 *  position, swing stretching even steps and shrinking odd ones, so grids on
 *  different divisions never drift apart). */
#pragma once

#include <cmath>

namespace ammo {

/** What the host knows at a block's first sample (quarter notes since the
 *  song start). valid = false: no tempo or position (playing still counts:
 *  stopped, the engine starts no notes and its clock holds). */
struct HostClock
{
    bool   valid   = false;
    bool   playing = false;
    double bpm     = 120.0;
    double beat    = 0.0;
};

/** The block's clock as the engine resolved it: for every channel. */
struct BlockClock
{
    double beat0 = 0.0; // beat at the first sample
    double bps   = 0.0; // beats per sample
    double swing = 0.0; // 0..1/3
    double bpm   = 120.0;
    int    frames = 0;
    bool   playing = false;
    double lastBeat() const { return beat0 + bps * (double)(frames - 1); }
};

// the step divisions, longest first; each note value dotted, straight, triplet (as kArpDivNames)
inline const double kArpDivBeats[14] = {16.0, 8.0, 4.0, 3.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
enum ArpDiv
{
    kDiv4_1, kDiv2_1, kDiv1_1, kDiv2D, kDiv2, kDiv4D, kDiv4, kDiv4T, kDiv8D, kDiv8, kDiv8T, kDiv16, kDiv16T, kDiv32
};
// patch files before version 2 had 1/1 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32: their index -> today's
inline const int kArpDivFromV1[9] = {kDiv1_1, kDiv2, kDiv4, kDiv4T, kDiv8, kDiv8T, kDiv16, kDiv16T, kDiv32};

/** Step index of master-clock position `beat` for a step of `d` beats with
 *  swing `s` (0..1/3): even steps last d(1+s), odd ones d(1-s). */
inline long long StepIndex(double beat, double d, double s)
{
    const long long pair = (long long)std::floor(beat / (2.0 * d));
    const double    t    = beat - (double)pair * 2.0 * d;
    return 2 * pair + (t >= d * (1.0 + s) ? 1 : 0);
}
inline double StepStart(long long k, double d, double s)
{
    return (double)(k / 2) * 2.0 * d + ((k & 1) ? d * (1.0 + s) : 0.0);
}
inline double StepBeats(long long k, double d, double s) { return (k & 1) ? d * (1.0 - s) : d * (1.0 + s); }

} // namespace ammo
