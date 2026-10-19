/** Modulation: one pool of
 *  sources, LFOs and macros, and the routes that take them to parameters.
 *
 *  Sources: up to 32 LFOs and 16 macros (0..1), none belonging to a
 *  channel. An LFO is -1..1: Sine, Triangle, Saw up / down and Square start
 *  a cycle at 0 or on their edge; Random holds a new value per cycle, Drift
 *  glides between them. Free runs at RATE (0.02 .. 20 Hz); Tempo reads its
 *  phase off the master clock (beat / DIVISION), so it lines up with the bar
 *  wherever the song starts. PHASE offsets the cycle (two LFOs a quarter
 *  apart); RESTART Notes of channel n restarts it on each of that channel's
 *  notes.
 *
 *  A route (source, target, amount, polarity) adds amount x source to the
 *  target's position in its own range (0..1 from min to max), so amount 1
 *  sweeps a target across all of it and the curves behind a parameter still
 *  hold (Cutoff moves in octaves). Bipolar takes an LFO as it is (around the
 *  knob); Unipolar as (s + 1) / 2 (up from the knob, down for a negative
 *  amount); a macro is 0..1 either way. A target is a channel's continuous
 *  parameter or a global one (IsModTarget). Routes on the same target add;
 *  the result is clamped to the range. A route may also name one of the
 *  page's parameters (kPageIdBase ..): the stage's look, camera, effects and
 *  scope. The engine does nothing with it (not a target here); the page
 *  applies it the same way, from the LFOs' telemetry and the macros.
 *
 *  The engine applies modulation on a grid of kModBlock samples counted
 *  from its start (any block size gives the same audio), and only while a
 *  route is live on a playing channel or a global target; otherwise once
 *  per block. */
#pragma once

#include "Dsp.hpp"
#include "Params.hpp"

#include <cmath>
#include <cstdint>

namespace ammo {

constexpr int kModBlock = 32; // samples between modulation updates (0.67 ms at 48 kHz)

enum LfoShape
{
    kLfoSine = 0,
    kLfoTriangle,
    kLfoSawUp,
    kLfoSawDown,
    kLfoSquare,
    kLfoRandom,
    kLfoDrift,
    kNumLfoShapes
};
// the Tempo divisions, longest first (kLfoDivNames), in beats
constexpr int   kNumLfoDivs = 16;
inline const double kLfoDivBeats[kNumLfoDivs] = {64.0, 32.0, 16.0, 8.0, 4.0, 3.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};

/** Free RATE 0..1 -> Hz: 0.02 * 2^(9.966 v), 0.02 .. 20 Hz (the hardware's curve). */
inline float LfoHz(float v) { return 0.02f * std::exp2(Clamp01(v) * 9.966f); }

struct LfoParams
{
    int   shape = kLfoSine, div = 4;
    bool  tempo = true, retrig = false;
    float rate = 0.5f, phase = 0.f;
    int   restart = 0; // retrig: the notes of channel restart (1..16), 0 Free
};

/** -1..1 for a hash of a cycle number: Random's held values. */
inline float CycleRandom(int64_t k, uint32_t seed)
{
    uint32_t x = (uint32_t)(uint64_t)k * 0x9E3779B1u ^ (uint32_t)((uint64_t)k >> 32) * 0x85EBCA77u ^ seed;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (float)(x >> 8) * (2.f / 16777216.f) - 1.f;
}

/** One shape at position `pos` (cycles: the integer part counts them). */
inline float LfoShapeAt(int shape, double pos, uint32_t seed)
{
    const double  fl = std::floor(pos);
    const int64_t k  = (int64_t)fl;
    const float   f  = (float)(pos - fl);
    switch(shape)
    {
        case kLfoTriangle: return f < 0.25f ? 4.f * f : (f < 0.75f ? 2.f - 4.f * f : 4.f * f - 4.f);
        case kLfoSawUp: return 2.f * f - 1.f;
        case kLfoSawDown: return 1.f - 2.f * f;
        case kLfoSquare: return f < 0.5f ? 1.f : -1.f;
        case kLfoRandom: return CycleRandom(k, seed);
        case kLfoDrift:
        {
            const float a = CycleRandom(k, seed), b = CycleRandom(k + 1, seed), s = f * f * (3.f - 2.f * f);
            return a + (b - a) * s;
        }
        default: return std::sin(f * kTau);
    }
}

/** One LFO. Free keeps its own phase; Tempo reads the beat (from the
 *  last restart with RESTART on a channel's notes). */
class Lfo
{
  public:
    void Init(uint32_t seed)
    {
        seed_  = seed;
        free_  = 0.0;
        cycle_ = trigs_ = 0;
        trigBeat_ = 0.0;
        trig_  = false;
        value_ = 0.f;
    }

    /** The value at a sub-block's first sample (beat: the master clock
     *  there), then the free phase advanced by `frames`. */
    float Step(const LfoParams& p, double beat, int frames, float sampleRate)
    {
        double pos;
        if(p.tempo)
        {
            const double d = kLfoDivBeats[p.div < 0 ? 0 : (p.div >= kNumLfoDivs ? kNumLfoDivs - 1 : p.div)];
            // from the last note (each retrigger a cycle of its own, so Random moves on), else the song's beat
            pos = (p.retrig && trig_) ? (double)trigs_ + (beat - trigBeat_) / d + (double)p.phase : beat / d + (double)p.phase;
        }
        else
            pos = (double)cycle_ + free_ + (double)p.phase;
        value_ = LfoShapeAt(p.shape, pos, seed_);
        free_ += (double)LfoHz(p.rate) * (double)frames / (double)sampleRate;
        if(free_ >= 1.0)
        {
            const double w = std::floor(free_);
            cycle_ += (int64_t)w;
            free_ -= w;
        }
        return value_;
    }

    /** A note started (RESTART on its channel): the cycle begins again, from PHASE. */
    void Retrigger(double beat)
    {
        free_     = 0.0;
        cycle_++; // Random and Drift take a new value
        trigs_ += 1024; // past any cycles the last note's run went through
        trigBeat_ = beat;
        trig_     = true;
    }

    float Value() const { return value_; }

  private:
    uint32_t seed_  = 1;
    double   free_  = 0.0;
    int64_t  cycle_ = 0, trigs_ = 0;
    double   trigBeat_ = 0.0;
    bool     trig_  = false;
    float    value_ = 0.f;
};

/** A pool route read from the parameters (`get(id)`: a value by id). */
struct PoolRoute
{
    int   src = kSrcOff, target = -1;
    float amt = 0.f;
    bool  uni = false;
};
template <typename Get>
inline PoolRoute ReadRoute(Get get, int r)
{
    PoolRoute  rt;
    const long s = std::lround(get(RouteId(r, rSrc))), t = std::lround(get(RouteId(r, rTarget)));
    rt.src       = s > kSrcOff && s < kNumModSources ? (int)s : kSrcOff;
    rt.target    = t >= 0 && IsModTarget((int)t) ? (int)t : -1;
    rt.amt       = get(RouteId(r, rAmt));
    rt.uni       = std::lround(get(RouteId(r, rPol))) == 0;
    return rt;
}
/** A route that moves something: a source, a target a route can take, an amount. */
inline bool Live(const PoolRoute& r) { return r.src != kSrcOff && r.target >= 0 && r.amt != 0.f; }
/** What a route adds to its target's position (0..1 of its range) for source value `s`. */
inline float RouteOffset(const PoolRoute& r, float s)
{
    if(r.src < kSrcMacro1 && r.uni)
        s = (s + 1.f) * 0.5f;
    return r.amt * s;
}

/** Pool LFO l's settings read from the parameters. */
template <typename Get>
inline LfoParams ReadLfo(Get get, int l)
{
    LfoParams p;
    p.shape   = (int)std::lround(get(LfoId(l, lShape)));
    p.tempo   = std::lround(get(LfoId(l, lSync))) == 1;
    p.rate    = get(LfoId(l, lRate));
    p.div     = (int)std::lround(get(LfoId(l, lDiv)));
    p.phase   = get(LfoId(l, lPhase));
    p.restart = (int)std::lround(get(LfoId(l, lRestart)));
    p.retrig  = p.restart > 0;
    return p;
}

} // namespace ammo
