/** DSP primitives for the engine: random numbers, parameter smoothing, the
 *  curves behind 0..1 parameters, a PolyBLEP morphing oscillator, a TPT
 *  state-variable filter, envelopes. Header-only, no allocation. */
#pragma once

#include <cmath>
#include <cstdint>

namespace ammo {

constexpr float kPi  = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Clamp01(float v) { return Clamp(v, 0.f, 1.f); }
inline float MidiToHz(float m) { return 440.f * std::exp2((m - 69.f) / 12.f); }

/* ------------------------------------------------------------------ curves */
// 0..1 parameters to their units.
inline float AttackSeconds(float v) { return 0.001f * std::exp2(v * 12.f); }       // 1 ms .. 4 s
inline float DecaySeconds(float v) { return 0.005f * std::exp2(v * 9.64f); }       // 5 ms .. 4 s
inline float PitchDecaySeconds(float v) { return 0.005f * std::exp2(v * 7.64f); }  // 5 ms .. 1 s
inline float CutoffHz(float v) { return 20.f * std::pow(1000.f, Clamp01(v)); }     // 20 Hz .. 20 kHz
inline float GlideSeconds(float v) { return v * v; }                               // 0 .. 1 s, fine near 0

/* --------------------------------------------------------------------- rng */
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    uint32_t Next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float Uniform() { return (float)(Next() >> 8) * (1.f / 16777216.f); } // [0, 1)
    float Bipolar() { return Uniform() * 2.f - 1.f; }
};

/* ---------------------------------------------------------------- smoother */
/** One pole toward a target; Set() jumps. */
struct Smoother
{
    float value = 0.f, target = 0.f, coef = 1.f;
    void SetTime(float seconds, float sampleRate, int stride = 1)
    {
        coef = seconds > 0.f ? 1.f - std::exp(-(float)stride / (seconds * sampleRate)) : 1.f;
    }
    void  Set(float v) { value = target = v; }
    void  Target(float v) { target = v; }
    float Next()
    {
        value += (target - value) * coef;
        return value;
    }
    bool Settled(float eps = 1e-5f) const { return std::fabs(target - value) < eps; }
};

/* -------------------------------------------------------------- oscillator */
/** PolyBLEP oscillator morphing sine -> triangle -> saw -> square by
 *  `shape` 0..1 (crossfades between neighbours). The triangle is the
 *  integrated band-limited square. Phase 0..1; wrapped() tells a cycle
 *  began on the last sample (the telemetry aligns its wave window to it). */
struct Oscillator
{
    float phase = 0.f, inc = 0.f, tri = 0.f;
    bool  wrap = false;

    void SetFreq(float hz, float sampleRate) { inc = Clamp(hz / sampleRate, 0.f, 0.45f); }
    void Reset(float ph = 0.f)
    {
        phase = ph;
        tri   = 0.f;
    }
    bool wrapped() const { return wrap; }

    static float Blep(float t, float dt)
    {
        if(t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.f;
        }
        if(t > 1.f - dt)
        {
            t = (t - 1.f) / dt;
            return t * t + t + t + 1.f;
        }
        return 0.f;
    }

    float Next(float shape)
    {
        const float t  = phase;
        const float dt = inc > 1e-6f ? inc : 1e-6f;
        const float sine = std::sin(t * kTau);
        float saw = 2.f * t - 1.f;
        saw -= Blep(t, dt);
        float sq = t < 0.5f ? 1.f : -1.f;
        sq += Blep(t, dt);
        sq -= Blep(std::fmod(t + 0.5f, 1.f), dt);
        // triangle: leaky integral of the square, scaled to +-1
        tri = tri * (1.f - dt * 0.5f) + sq * 4.f * dt;
        const float triOut = Clamp(tri, -1.f, 1.f);

        phase += inc;
        wrap = phase >= 1.f;
        if(wrap)
            phase -= 1.f;

        const float x = Clamp01(shape) * 3.f;
        if(x < 1.f)
            return sine + (triOut - sine) * x;
        if(x < 2.f)
            return triOut + (saw - triOut) * (x - 1.f);
        return saw + (sq - saw) * (x - 2.f);
    }
};

/* ------------------------------------------------------------------ filter */
/** TPT (Zavalishin) state-variable filter. type 0 = low, 0.5 = band,
 *  1 = high (crossfade). Stays stable under fast cutoff modulation. */
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f;
    float g = 0.5f, k = 1.f, a1 = 0.5f, a2 = 0.25f, a3 = 0.125f;

    void Set(float cutoffHz, float reso, float sampleRate)
    {
        const float fc = Clamp(cutoffHz, 10.f, sampleRate * 0.45f);
        g              = std::tan(kPi * fc / sampleRate);
        k              = 2.f - 1.9f * Clamp01(reso); // Q 0.5 .. 10
        a1             = 1.f / (1.f + g * (g + k));
        a2             = g * a1;
        a3             = g * a2;
    }
    void Reset() { ic1 = ic2 = 0.f; }
    float Next(float v0, float type)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1            = 2.f * v1 - ic1;
        ic2            = 2.f * v2 - ic2;
        const float lp = v2, bp = v1, hp = v0 - k * v1 - v2;
        const float t = Clamp01(type) * 2.f;
        return t < 1.f ? lp + (bp - lp) * t : bp + (hp - bp) * (t - 1.f);
    }
};

/* --------------------------------------------------------------- envelopes */
/** ADSR with exponential segments: attack toward 1.3 (so it reaches 1 at
 *  the rate's time), decay to sustain, release to 0. Retrigger from the
 *  current level (no click). Times via the curves above. */
struct Adsr
{
    enum Stage
    {
        kIdle = 0,
        kAttack,
        kDecay,
        kSustain,
        kRelease
    };
    float value = 0.f, sustain = 0.f;
    float ca = 1.f, cd = 1.f, cr = 1.f;
    Stage stage = kIdle;

    void SetTimes(float attackSec, float decaySec, float sus, float releaseSec, float sampleRate)
    {
        ca      = 1.f - std::exp(-1.f / (attackSec * sampleRate / 1.3f));
        cd      = 1.f - std::exp(-4.6f / (decaySec * sampleRate));
        cr      = 1.f - std::exp(-4.6f / (releaseSec * sampleRate));
        sustain = Clamp01(sus);
    }
    void Trigger() { stage = kAttack; }
    void Release()
    {
        if(stage != kIdle)
            stage = kRelease;
    }
    bool Active() const { return stage != kIdle; }
    float Next()
    {
        switch(stage)
        {
            case kAttack:
                value += (1.3f - value) * ca;
                if(value >= 1.f)
                {
                    value = 1.f;
                    stage = kDecay;
                }
                break;
            case kDecay:
                value += (sustain - value) * cd;
                if(std::fabs(value - sustain) < 1e-4f)
                    stage = kSustain;
                break;
            case kSustain: value = sustain; break;
            case kRelease:
                value += (0.f - value) * cr;
                if(value < 1e-5f)
                {
                    value = 0.f;
                    stage = kIdle;
                }
                break;
            default: value = 0.f; break;
        }
        return value;
    }
};

/** AD envelope for filter and pitch: jumps to 1 (from wherever it is) on a
 *  trigger after an optional attack, then decays to 0. */
struct AdEnv
{
    float value = 0.f, ca = 1.f, cd = 1.f;
    bool  rising = false;
    void SetTimes(float attackSec, float decaySec, float sampleRate)
    {
        ca = attackSec <= 0.0011f ? 1.f : 1.f - std::exp(-1.f / (attackSec * sampleRate / 1.3f));
        cd = 1.f - std::exp(-4.6f / (decaySec * sampleRate));
    }
    void Trigger() { rising = true; }
    float Next()
    {
        if(rising)
        {
            value += (1.3f - value) * ca;
            if(value >= 1.f)
            {
                value  = 1.f;
                rising = false;
            }
        }
        else
            value += (0.f - value) * cd;
        return value;
    }
};

} // namespace ammo
