/** The effects: a channel's inserts (DRIVE, CHORUS, DELAY), the shared
 *  REVERB and the DUCK follower, the master bus (GLUE, the look-ahead
 *  limiter), and their parameters, curves and building blocks (a halfband
 *  2x resampler, a 4-point interpolated read, a cheap sine).
 *
 *  Every processor runs sample by sample with its parameters smoothed per
 *  sample, so the audio is the same for any block size. Buffers are sized
 *  in Init (called from Engine::SetSampleRate), never on the audio thread.
 *  An insert that is off (drive 0, chorus off or its mix 0, delay mix 0) costs
 *  nothing: it fades out over a few milliseconds, then returns at once
 *  until it is turned on again (and starts clean then, no stale echoes). */
#pragma once

#include "Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace ammo {

/* ------------------------------------------------------------------ params */
struct FxParams
{
    float drive = 0.f, tone = 0.f;                                          // DRIVE: amount 0..1, tilt -1..1
    bool  chorusOn = false;                                                 // CHORUS
    int   chorusMode = 0;                                                   // kChorusPlain, kChorusEnsemble, kChorusVibrato
    float chorusRate = 0.4f, chorusDepth = 0.5f, chorusMix = 0.5f;
    int   delayDiv = 3;                                                     // DELAY: kDelayDivBeats index
    float delayFb = 0.4f, delayDamp = 0.3f, delayMix = 0.f;
    bool  delayPingPong = false;
    float revSend = 0.f;               // REVERB SEND, after the level
    int   duckSrc = 0;                 // DUCK: 0 off, else the slot (1..16) whose amp envelope ducks this one
    float duckAmt = 0.5f;
};

/* ------------------------------------------------------------------ curves */
inline float DriveGain(float a) { return 1.f + 31.f * a * a; }                                // into the shaper: 0 .. +30 dB
enum ChorusMode
{
    kChorusPlain = 0,
    kChorusEnsemble,
    kChorusVibrato
};
inline float ChorusHz(float v) { return 0.08f * std::pow(40.f, Clamp01(v)); }                 // Chorus, Ensemble: 0.08 .. 3.2 Hz
inline float VibratoHz(float v) { return 0.05f * std::exp2(Clamp01(v) * 7.f); }               // Vibrato: 0.05 .. 6.4 Hz
// the most a Chorus or Ensemble sweep may swing the pitch: 20 cents (a delay swept by A s at f Hz swings it by 2 pi f A)
constexpr float kChorusDev = 0.011486f; // 1 - 2^(-20 / 1200)
constexpr float kShimmerRate = 6.9f, kShimmerDepth = 0.125f; // Ensemble's second, faster sweep: its share of the swing 6.9 / 8
constexpr float kEnsembleGain = 0.57f; // three lines summed, about as loud as one (single notes, wet: 0.80 .. 1.23x)
// Ensemble's lines rest at staggered delays, so that a note's harmonics do not all cancel or add at once; 4 ms
// apart, so the sweeps (120 degrees apart, up to 3.4 ms) can never bring all three to one delay (that would take
// 4.6 ms of sweep): they stay at least 2 ms apart. The right side rests 2 ms later than the left, so the two never
// coincide (Chorus's two lines meet at every turn of the sweep)
inline const float kEnsembleBase[2][3] = {{0.006f, 0.010f, 0.014f}, {0.008f, 0.012f, 0.016f}}; // left, right
/** The sweep (the delay's swing either way, in seconds) for DEPTH 0..1 at `hz`: Chorus up to 4 ms and Ensemble up
 *  to 3 ms, each held to a swing of kChorusDev in pitch; Vibrato up to 4 ms, unheld. */
inline float ChorusSweep(int mode, float depth, float hz)
{
    const float d = Clamp01(depth), w = 2.f * kPi * hz;
    switch(mode)
    {
        case kChorusEnsemble: return d * std::fmin(0.003f, kChorusDev / (w * (1.f + kShimmerRate * kShimmerDepth)));
        case kChorusVibrato: return d * 0.004f;
        default: return d * std::fmin(0.004f, kChorusDev / w);
    }
}
inline float DelayDampHz(float v) { return 12000.f * std::pow(500.f / 12000.f, Clamp01(v)); } // 12 kHz .. 500 Hz
// the hardware's delay divisions, in its order (1/8. is dotted: one and a half eighths)
inline const double kDelayDivBeats[11] = {0.125, 0.25, 1.0 / 6.0, 0.5, 1.0 / 3.0, 0.75, 1.0, 2.0 / 3.0, 1.5, 2.0, 4.0};
constexpr float     kDelayMaxSec       = 12.f; // 1/1 at 20 BPM, the slowest tempo (4.6 MB a channel at 48 kHz)

/* ------------------------------------------------------------------ blocks */
/** sin(2 pi p) for a phase p in [0, 1): an odd 5th-order polynomial of a
 *  triangle, exact at 0 and the peaks, error < 1e-3 (an LFO's sine). */
inline float FastSin01(float p)
{
    const float x  = p < 0.25f ? 4.f * p : (p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f); // -1..1
    const float x2 = x * x;
    return x * (1.5707963f + x2 * (-0.6459640f + x2 * 0.0751677f)); // sin(pi/2 x)
}

/** 4-point Hermite read of a ring buffer (mask = size - 1) at `pos` samples
 *  before the write index `w` (pos >= 1). An integer pos reads exactly. */
inline float ReadHermite(const float* buf, int mask, int w, float pos)
{
    const int   ip = (int)pos;
    const float f  = pos - (float)ip;
    const int   i0 = w - ip;
    const float xm1 = buf[(i0 + 1) & mask], x0 = buf[i0 & mask], x1 = buf[(i0 - 1) & mask], x2 = buf[(i0 - 2) & mask];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

/** ReadHermite on a ring of any length (`size`), `w` in [0, size) and `pos`
 *  at most size - 4 back: the long delay line, where rounding its length up
 *  to a power of two would nearly double 12 s of memory. */
inline float ReadHermiteRing(const float* buf, int size, int w, float pos)
{
    const int   ip  = (int)pos;
    const float f   = pos - (float)ip;
    auto        at  = [&](int i) { return buf[i < 0 ? i + size : (i >= size ? i - size : i)]; };
    const int   i0  = w - ip;
    const float xm1 = at(i0 + 1), x0 = at(i0), x1 = at(i0 - 1), x2 = at(i0 - 2);
    const float c1  = 0.5f * (x1 - xm1);
    const float c2  = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
    const float c3  = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

/** The halfband lowpass behind the 2x resampling: a 23-tap Blackman-windowed
 *  sinc. Its six odd taps per side, as interpolation weights c[i] for the
 *  pairs (x[k - i], x[k + 1 + i]) around a midpoint (they sum to 1/2). */
struct Halfband
{
    float c[6];
    Halfband()
    {
        float sum = 0.f;
        for(int i = 0; i < 6; i++)
        {
            const int    j = 2 * i + 1;
            const double w = 0.42 + 0.5 * std::cos(3.14159265358979 * j / 12.0) + 0.08 * std::cos(2.0 * 3.14159265358979 * j / 12.0);
            c[i]           = (float)(std::sin(1.5707963267949 * j) / (3.14159265358979 * j) * w);
            sum += c[i];
        }
        for(int i = 0; i < 6; i++)
            c[i] *= 0.5f / sum;
    }
};
inline const Halfband& HalfbandTaps()
{
    static const Halfband h;
    return h;
}

/** A value that ramps linearly to 0 or 1 over `sec`: an insert's on / off. */
struct Fade
{
    float value = 0.f, step = 1.f / 240.f;
    void  SetTime(float sec, float sr) { step = 1.f / (sec * sr); }
    float Next(float target)
    {
        if(value < target)
            value = std::fmin(target, value + step);
        else if(value > target)
            value = std::fmax(target, value - step);
        return value;
    }
};

/* ------------------------------------------------------------------- drive */
/** tanh saturation at twice the sample rate, then a tone tilt. The input is
 *  upsampled (halfband), shaped, filtered and decimated: 12 samples of
 *  latency, so turning it on or off crossfades with the dry signal over
 *  5 ms. Level: the gain after the shaper keeps a sine of the voices' usual
 *  peak (0.3) at its rms whatever the drive (a full drive is a fuzz whose
 *  quiet tails come up). TONE tilts around 700 Hz, +-6 dB at the ends. */
class Drive
{
  public:
    static constexpr int kLatency = 12;

    void Init(float sr)
    {
        tiltA_ = 1.f - std::exp(-kTau * 700.f / sr);
        amount_.SetTime(0.02f, sr);
        tone_.SetTime(0.02f, sr);
        fade_.SetTime(0.005f, sr);
        fade_.value = 0.f;
        Clear();
    }
    bool Engaged() const { return fade_.value > 0.f; }

    /** In place over x[0..n). */
    void Process(float* x, int n, float amount, float tone)
    {
        const float target = amount > 0.f ? 1.f : 0.f;
        if(target == 0.f && fade_.value == 0.f)
            return;
        if(fade_.value == 0.f)
        {
            Clear(); // from bypass: no stale history
            amount_.Set(amount);
            tone_.Set(tone);
        }
        amount_.Target(amount);
        tone_.Target(tone);
        const float* c = HalfbandTaps().c;
        for(int k = 0; k < n; k++)
        {
            const float in = x[k];
            xh_[w_ & 31]   = in;
            const float g  = DriveGain(amount_.Next());
            const float ga = g * kRef;
            const float mk = std::sqrt(1.f + 0.5f * ga * ga) / g; // make-up: tanh(g x) * mk keeps the reference rms
            // the even sample is x[n-6]; the odd one the halfband midpoint between x[n-6] and x[n-5]
            float mid = 0.f;
            for(int i = 0; i < 6; i++)
                mid += c[i] * (xh_[(w_ - 6 - i) & 31] + xh_[(w_ - 5 + i) & 31]);
            const float ve = std::tanh(g * xh_[(w_ - 6) & 31]) * mk;
            const float vm = std::tanh(g * mid) * mk;
            // decimate: centred on the even sample 6 back, the odd ones around it
            float y = 0.5f * ve_[(w_ - 6) & 31];
            for(int i = 0; i < 6; i++)
                y += 0.5f * c[i] * (vm_[(w_ - 6 + i) & 31] + vm_[(w_ - 7 - i) & 31]);
            ve_[w_ & 31] = ve;
            vm_[w_ & 31] = vm;
            w_++;
            // tone tilt
            lp_ += (y - lp_) * tiltA_;
            const float t = tone_.Next();
            y             = lp_ * std::exp2(-t) + (y - lp_) * std::exp2(t);
            const float f = fade_.Next(target);
            x[k]          = f >= 1.f ? y : in + (y - in) * f;
        }
    }

  private:
    static constexpr float kRef = 0.3f;
    void Clear()
    {
        std::memset(xh_, 0, sizeof xh_);
        std::memset(ve_, 0, sizeof ve_);
        std::memset(vm_, 0, sizeof vm_);
        lp_ = 0.f;
        w_  = 0;
    }
    float    xh_[32] = {}, ve_[32] = {}, vm_[32] = {};
    unsigned w_ = 0;
    float    lp_ = 0.f, tiltA_ = 0.1f;
    Smoother amount_, tone_;
    Fade     fade_;
};

/* ------------------------------------------------------------------ chorus */
/** Modulated delay lines on each side, by MODE:
 *  - Chorus: one line a side, the sides swept in opposite phase by one sine
 *    LFO, 8 ms +- up to 4 ms: a centred sound comes out wider (the sides
 *    decorrelate) and, the sweep being symmetric, in tune.
 *  - Ensemble: three lines a side, their sweeps 120 degrees apart (the right
 *    side's half a cycle on: 60, 180, 300 degrees), resting at 6, 10 and 14 ms
 *    (the right side at 8, 12, 16), each +- up to 3 ms,
 *    with a faster shimmer on top (6.9x the rate, an eighth of the sweep):
 *    the string machine's lush, wide sound. The three sweeps sum to zero, so
 *    the average delay stays put.
 *  - Vibrato: one line a side, both swept together, and only the wet sound
 *    (MIX does nothing): a pitch wobble, 1 ms + up to 8 ms of sweep.
 *  Chorus and Ensemble are kept chorus-like: RATE 0.08..3.2 Hz, and DEPTH
 *  scales a sweep that is held where the pitch would swing more than
 *  20 cents (kChorusDev). Vibrato keeps the wide range: RATE 0.05..6.4 Hz,
 *  DEPTH up to +-4 ms (at 6.4 Hz +-2.6 semitones).
 *  MIX crossfades dry and wet at equal power. A change of mode dips to the
 *  dry sound for the fade (10 ms) and starts the new one clean. */
class Chorus
{
  public:
    void Init(float sr)
    {
        sr_ = sr;
        int need = (int)(0.020f * sr) + 8, size = 64; // Ensemble: up to 16 + 3.4 ms
        while(size < need)
            size <<= 1;
        bufL_.assign((size_t)size, 0.f);
        bufR_.assign((size_t)size, 0.f);
        mask_ = size - 1;
        mix_.SetTime(0.02f, sr);
        depth_.SetTime(0.05f, sr);
        inc_.SetTime(0.05f, sr);
        fade_.SetTime(0.01f, sr);
        fade_.value = 0.f;
        phase_ = fast_ = 0.f;
        w_    = 0;
        mode_ = kChorusPlain;
    }
    bool Engaged() const { return fade_.value > 0.f; }
    int  Span() const { return mask_ + 1; }

    void Process(float* L, float* R, int n, bool on, int mode, float rate, float depth, float mix)
    {
        if(mode != mode_ && fade_.value == 0.f)
            mode_ = mode; // a new mode starts clean, below
        const bool  vib    = mode_ == kChorusVibrato;
        const float target = on && (vib || mix > 0.f) && mode == mode_ ? 1.f : 0.f;
        if(target == 0.f && fade_.value == 0.f)
            return;
        const float hz = vib ? VibratoHz(rate) : ChorusHz(rate), sweep = ChorusSweep(mode_, depth, hz) * sr_;
        if(fade_.value == 0.f)
        {
            std::fill(bufL_.begin(), bufL_.end(), 0.f);
            std::fill(bufR_.begin(), bufR_.end(), 0.f);
            mix_.Set(vib ? 1.f : mix);
            depth_.Set(sweep);
            inc_.Set(hz / sr_);
        }
        mix_.Target(vib ? 1.f : mix);
        depth_.Target(sweep);
        inc_.Target(hz / sr_);
        float* bl = bufL_.data();
        float* br = bufR_.data();
        for(int k = 0; k < n; k++)
        {
            bl[w_ & mask_] = L[k];
            br[w_ & mask_] = R[k];
            const float a = depth_.Next(), inc = inc_.Next();
            float       wl, wr;
            if(mode_ == kChorusEnsemble)
            {
                const float sh = kShimmerDepth * a;
                wl = wr = 0.f;
                for(int j = 0; j < 3; j++)
                {
                    const float off = (float)j * (1.f / 3.f);
                    const float s   = a * FastSin01(Wrap01(phase_ + off)) + sh * FastSin01(Wrap01(fast_ + off));
                    wl += ReadHermite(bl, mask_, w_, kEnsembleBase[0][j] * sr_ + s);
                    wr += ReadHermite(br, mask_, w_, kEnsembleBase[1][j] * sr_ - s);
                }
                wl *= kEnsembleGain;
                wr *= kEnsembleGain;
                fast_ = Wrap01(fast_ + kShimmerRate * inc);
            }
            else if(vib)
            {
                const float d = sr_ / 1000.f + a + a * FastSin01(phase_); // both sides together, 1 ms at rest
                wl = ReadHermite(bl, mask_, w_, d);
                wr = ReadHermite(br, mask_, w_, d);
            }
            else
            {
                const float base = 0.008f * sr_, s = a * FastSin01(phase_);
                wl = ReadHermite(bl, mask_, w_, base + s);
                wr = ReadHermite(br, mask_, w_, base - s);
            }
            phase_ = Wrap01(phase_ + inc);
            w_++;
            // equal power: dry and wet are mostly uncorrelated, so a plain crossfade would dip 3 dB at 50 %
            const float m = mix_.Next() * fade_.Next(target), gd = std::sqrt(1.f - m), gw = std::sqrt(m);
            L[k]          = L[k] * gd + wl * gw;
            R[k]          = R[k] * gd + wr * gw;
        }
    }

  private:
    static float Wrap01(float p) { return p >= 1.f ? p - 1.f : p; }

    std::vector<float> bufL_, bufR_;
    int                mask_ = 0, w_ = 0, mode_ = kChorusPlain;
    float              sr_ = 48000.f, phase_ = 0.f, fast_ = 0.f;
    Smoother           mix_, depth_, inc_;
    Fade               fade_;
};

/* ------------------------------------------------------------------- delay */
/** A tempo-synced stereo echo. Each side's line feeds back into itself
 *  (the echoes stay where the sound is panned), or, PING-PONG, the input
 *  (both sides summed) enters the left line and each line feeds the other:
 *  left, right, left. A one-pole low-pass in the loop (DAMPING, 12 kHz ..
 *  500 Hz) darkens every repeat. MIX is the echoes' level over the dry
 *  sound (1: the first echo as loud as the note). The time follows the
 *  tempo, gliding (a tape's pitch bend) when it changes; an integer time is
 *  read exactly, so echoes land on the sample. */
class Delay
{
  public:
    void Init(float sr)
    {
        sr_   = sr;
        size_ = (int)(kDelayMaxSec * sr) + 8;
        bufL_.assign((size_t)size_, 0.f);
        bufR_.assign((size_t)size_, 0.f);
        maxD_ = (float)(int)(kDelayMaxSec * sr);
        mix_.SetTime(0.02f, sr);
        fb_.SetTime(0.02f, sr);
        damp_.SetTime(0.02f, sr);
        time_.SetTime(0.12f, sr);
        fade_.SetTime(0.01f, sr);
        fade_.value = 0.f;
        w_ = written_ = 0;
        lpL_ = lpR_ = 0.f;
    }
    bool  Engaged() const { return fade_.value > 0.f; }
    float Samples() const { return time_.value; }

    /** `delaySamples`: the division at the current tempo. */
    void Process(float* L, float* R, int n, double delaySamples, float fb, float damp, bool pingPong, float mix)
    {
        const float target = mix > 0.f ? 1.f : 0.f;
        if(target == 0.f && fade_.value == 0.f)
            return;
        double d = delaySamples < 1.0 ? 1.0 : (delaySamples > maxD_ ? maxD_ : delaySamples);
        if(std::fabs(d - std::round(d)) < 1e-4)
            d = std::round(d);
        const float dampA = 1.f - std::exp(-kTau * DelayDampHz(damp) / sr_);
        if(fade_.value == 0.f)
        {
            // from bypass: start clean (reads past what was written since are silence), on the values
            written_ = 0;
            lpL_ = lpR_ = 0.f;
            time_.Set((float)d);
            mix_.Set(mix);
            fb_.Set(fb);
            damp_.Set(dampA);
        }
        time_.Target((float)d);
        mix_.Target(mix);
        fb_.Target(fb);
        damp_.Target(dampA);
        float* bl = bufL_.data();
        float* br = bufR_.data();
        for(int k = 0; k < n; k++)
        {
            float t = time_.value;
            if(t != time_.target)
            {
                t = time_.Next();
                if(std::fabs(t - time_.target) < 1e-3f)
                    t = time_.value = time_.target;
            }
            float ol, orr;
            if((float)written_ > t + 2.f)
            {
                ol  = ReadHermiteRing(bl, size_, w_, t);
                orr = ReadHermiteRing(br, size_, w_, t);
            }
            else
            {
                ol  = ReadFresh(bl, t);
                orr = ReadFresh(br, t);
            }
            const float a = damp_.Next(), g = fb_.Next();
            lpL_ += (ol - lpL_) * a;
            lpR_ += (orr - lpR_) * a;
            const float inL = L[k], inR = R[k];
            if(pingPong)
            {
                bl[w_] = (inL + inR) * 0.5f + g * lpR_;
                br[w_] = g * lpL_;
            }
            else
            {
                bl[w_] = inL + g * lpL_;
                br[w_] = inR + g * lpR_;
            }
            if(++w_ == size_)
                w_ = 0;
            if(written_ < (1 << 30))
                written_++;
            const float m = mix_.Next() * fade_.Next(target);
            L[k]          = inL + ol * m;
            R[k]          = inR + orr * m;
        }
    }

  private:
    /** The Hermite read near the start of what was written since the delay
     *  engaged: anything older is the past before it, read as silence. */
    float ReadFresh(const float* buf, float pos) const
    {
        const int   ip = (int)pos;
        const float f  = pos - (float)ip;
        float       x[4];
        for(int j = 0; j < 4; j++)
        {
            const int age = ip - 1 + j; // 1: the last sample written
            const int i   = w_ - age;
            x[j]          = age >= 1 && age <= written_ ? buf[i < 0 ? i + size_ : i] : 0.f;
        }
        const float c1 = 0.5f * (x[2] - x[0]);
        const float c2 = x[0] - 2.5f * x[1] + 2.f * x[2] - 0.5f * x[3];
        const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
        return ((c3 * f + c2) * f + c1) * f + x[1];
    }

    std::vector<float> bufL_, bufR_;
    int                size_ = 0, w_ = 0, written_ = 0;
    float              sr_ = 48000.f, maxD_ = 576000.f, lpL_ = 0.f, lpR_ = 0.f;
    Smoother           mix_, fb_, damp_, time_;
    Fade               fade_;
};

/* ------------------------------------------------------------------ reverb */
struct ReverbParams
{
    float size = 0.5f, decay = 0.45f, damp = 0.4f, pre = 0.1f, width = 1.f, ret = 0.6f;
};
inline float ReverbSize(float v) { return 0.4f + 1.2f * Clamp01(v); }                          // line lengths x0.4 .. x1.6
inline float ReverbT60(float v) { return 0.2f * std::pow(100.f, Clamp01(v)); }                 // 0.2 .. 20 s
inline float ReverbDampHz(float v) { return 18000.f * std::pow(1200.f / 18000.f, Clamp01(v)); } // 18 kHz .. 1.2 kHz
inline float ReverbPreSec(float v) { return 0.25f * Clamp01(v); }                              // 0 .. 250 ms

/** The shared reverb: an 8-line feedback delay network. The send (stereo)
 *  passes a PRE-DELAY and two short allpass diffusers per side, then feeds
 *  the even lines from the left and the odd from the right. Each line is
 *  read with a slow wander of its own (+-0.3 ms, 0.1..0.9 Hz: no metallic
 *  ringing), low-passed (DAMPING), scaled for DECAY (the T60: -60 dB after
 *  that time, whatever the line's length) and mixed into all eight by a
 *  Hadamard matrix. SIZE scales every line (x0.4 .. x1.6, gliding). Two
 *  orthogonal sign patterns of the lines give left and right; WIDTH
 *  narrows them to mono at 0. Gains are updated every 32 samples on a
 *  running count, so the audio does not depend on the block size. */
class Reverb
{
  public:
    static constexpr int kLines = 8;

    void Init(float sr)
    {
        sr_             = sr;
        const float k   = sr / 48000.f;
        const float mod = 0.0006f * sr + 4.f; // the wander reaches twice its depth
        for(int i = 0; i < kLines; i++)
        {
            base_[i] = kBase[i] * k;
            line_[i].Init((int)(base_[i] * ReverbSize(1.f) + mod) + 8);
            lp_[i] = 0.f;
            ph_[i] = 0.137f * (float)i;
        }
        pre_[0].Init((int)(ReverbPreSec(1.f) * sr) + 8);
        pre_[1].Init((int)(ReverbPreSec(1.f) * sr) + 8);
        for(int s = 0; s < 2; s++)
            for(int j = 0; j < 2; j++)
                ap_[s][j].Init(std::max(1, (int)(kAp[s][j] * k)));
        modDepth_ = 0.0003f * sr;
        size_.SetTime(0.3f, sr);
        pre_t_.SetTime(0.1f, sr);
        width_.SetTime(0.02f, sr);
        ret_.SetTime(0.02f, sr);
        size_.Set(ReverbSize(0.5f));
        pre_t_.Set(0.f);
        fresh_ = true;
        ctr_   = 0;
        quiet_ = 1 << 30;
    }

    /** Adds the reverb of sendL / sendR into outL / outR. `fed`: a send was
     *  non-zero this block. Skipped (nothing added) once the tail is gone. */
    void Process(const float* sendL, const float* sendR, float* outL, float* outR, int n, const ReverbParams& p, bool fed)
    {
        if(!fed && !Ringing())
            return;
        if(fresh_)
        {
            size_.Set(ReverbSize(p.size));
            pre_t_.Set(ReverbPreSec(p.pre) * sr_);
            width_.Set(p.width);
            ret_.Set(p.ret);
            fresh_ = false;
        }
        size_.Target(ReverbSize(p.size));
        pre_t_.Target(ReverbPreSec(p.pre) * sr_);
        width_.Target(p.width);
        ret_.Target(p.ret);
        const float t60 = ReverbT60(p.decay), dampHz = ReverbDampHz(p.damp);
        float       pk  = 0.f;
        for(int k = 0; k < n; k++)
        {
            const float sz = size_.Next();
            if((ctr_++ & 31) == 0)
            {
                // per line: -60 dB after t60 seconds for its own length; the damping low-pass
                for(int i = 0; i < kLines; i++)
                    g_[i] = std::pow(10.f, -3.f * base_[i] * sz / (t60 * sr_));
                dampA_ = 1.f - std::exp(-kTau * dampHz / sr_);
                for(int i = 0; i < kLines; i++)
                    inc_[i] = kRate[i] / sr_;
            }
            // pre-delay, diffusion
            pre_[0].Write(sendL[k]);
            pre_[1].Write(sendR[k]);
            const float pd = pre_t_.Next();
            float       l  = pd >= 1.f ? pre_[0].Read(pd) : sendL[k];
            float       r  = pd >= 1.f ? pre_[1].Read(pd) : sendR[k];
            for(int j = 0; j < 2; j++)
            {
                l = ap_[0][j].Next(l);
                r = ap_[1][j].Next(r);
            }
            // the lines
            float y[kLines], z[kLines];
            for(int i = 0; i < kLines; i++)
            {
                ph_[i] += inc_[i];
                if(ph_[i] >= 1.f)
                    ph_[i] -= 1.f;
                y[i] = line_[i].Read(base_[i] * sz + modDepth_ * (1.f + FastSin01(ph_[i])));
                lp_[i] += (y[i] - lp_[i]) * dampA_;
                z[i] = lp_[i] * g_[i];
            }
            Hadamard8(z);
            for(int i = 0; i < kLines; i++)
                line_[i].Write(z[i] + ((i & 1) ? r : l));
            // out: two orthogonal sign patterns; width by mid / side
            const float oL = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * kOut;
            const float oR = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * kOut;
            const float m = 0.5f * (oL + oR), s = 0.5f * (oL - oR) * width_.Next(), g = ret_.Next();
            outL[k] += (m + s) * g;
            outR[k] += (m - s) * g;
            const float a = std::fmax(std::fabs(oL), std::fabs(oR));
            if(a > pk)
                pk = a;
        }
        quiet_ = !fed && pk < 1e-6f ? quiet_ + n : 0;
    }

    /** The tail is still there: it has not been quiet for a whole pass through the longest line and the pre-delay. */
    bool Ringing() const { return quiet_ <= (int)(base_[kLines - 1] * ReverbSize(1.f) + ReverbPreSec(1.f) * sr_) + 64; }

  private:
    /** A delay line read at a fractional distance (>= 1) behind the last write. */
    struct Line
    {
        std::vector<float> buf;
        int                mask = 0, w = 0;
        void Init(int need)
        {
            int size = 64;
            while(size < need)
                size <<= 1;
            buf.assign((size_t)size, 0.f);
            mask = size - 1;
            w    = 0;
        }
        void  Write(float v) { buf[(size_t)(w++ & mask)] = v; }
        float Read(float pos) const { return ReadHermite(buf.data(), mask, w - 1, pos - 1.f); }
    };
    /** Schroeder allpass, gain 0.6. */
    struct Allpass
    {
        std::vector<float> buf;
        int                n = 1, i = 0;
        void Init(int len)
        {
            buf.assign((size_t)len, 0.f);
            n = len;
            i = 0;
        }
        float Next(float x)
        {
            const float d = buf[(size_t)i];
            const float v = x + 0.6f * d;
            buf[(size_t)i] = v;
            i              = i + 1 == n ? 0 : i + 1;
            return d - 0.6f * v;
        }
    };
    static void Hadamard8(float* x)
    {
        for(int h = 1; h < 8; h <<= 1)
            for(int i = 0; i < 8; i += h << 1)
                for(int j = i; j < i + h; j++)
                {
                    const float a = x[j], b = x[j + h];
                    x[j]          = a + b;
                    x[j + h]      = a - b;
                }
        for(int i = 0; i < 8; i++)
            x[i] *= 0.35355339f; // 1 / sqrt(8): energy kept
    }

    // line lengths at 48 kHz and size 1 (about 46..100 ms, mutually prime), the diffusers', the wander rates
    static constexpr float kBase[kLines] = {2203.f, 2473.f, 2767.f, 3089.f, 3449.f, 3851.f, 4297.f, 4799.f};
    static constexpr float kAp[2][2]     = {{142.f, 379.f}, {107.f, 277.f}};
    static constexpr float kRate[kLines] = {0.11f, 0.17f, 0.23f, 0.31f, 0.43f, 0.53f, 0.71f, 0.89f};
    static constexpr float kOut          = 0.6f;

    float    sr_ = 48000.f, base_[kLines] = {}, g_[kLines] = {}, lp_[kLines] = {}, ph_[kLines] = {}, inc_[kLines] = {};
    float    dampA_ = 0.5f, modDepth_ = 14.f;
    Line     line_[kLines], pre_[2];
    Allpass  ap_[2][2];
    Smoother size_, pre_t_, width_, ret_;
    bool     fresh_ = true;
    unsigned ctr_   = 0;
    int      quiet_ = 1 << 30;
};

/* -------------------------------------------------------------- master bus */
/** GLUE: a gentle bus compressor. Stereo-linked RMS (50 ms), 2:1 with a
 *  6 dB soft knee, 30 ms attack, 300 ms release (both on the gain, in dB).
 *  AMOUNT moves the threshold from -6 to -30 dBFS rms and brings the
 *  make-up with it (a quarter of the threshold's depth: what a signal
 *  halfway into it loses), so turning it up evens the mix out without
 *  making it quieter. 0: off (after a 50 ms glide), no CPU. */
class Glue
{
  public:
    void Init(float sr)
    {
        det_  = 1.f - std::exp(-1.f / (0.05f * sr));
        atk_  = 1.f - std::exp(-1.f / (0.03f * sr));
        rel_  = 1.f - std::exp(-1.f / (0.3f * sr));
        amt_.SetTime(0.05f, sr);
        amt_.Set(0.f);
        pow_ = 0.f;
        grDb_ = 0.f;
    }
    /** In place; returns the deepest gain reduction in the block (dB, >= 0). */
    float Process(float* L, float* R, int n, float amount)
    {
        amt_.Target(amount);
        if(amount <= 0.f && amt_.value < 1e-4f)
        {
            amt_.value = 0.f;
            return 0.f;
        }
        float worst = 0.f;
        for(int k = 0; k < n; k++)
        {
            const float l = L[k], r = R[k];
            pow_ += (0.5f * (l * l + r * r) - pow_) * det_;
            const float a    = amt_.Next();
            const float thr  = -6.f - 24.f * a;
            const float over = 10.f * std::log10(pow_ + 1e-12f) - thr;
            const float gr   = over <= -3.f ? 0.f : (over >= 3.f ? 0.5f * over : 0.5f * (over + 3.f) * (over + 3.f) / 12.f);
            grDb_ += (gr - grDb_) * (gr > grDb_ ? atk_ : rel_);
            const float g = std::pow(10.f, (-0.25f * thr - grDb_) / 20.f * (a < 0.05f ? a * 20.f : 1.f)); // fades in from 0
            L[k]          = l * g;
            R[k]          = r * g;
            if(grDb_ > worst)
                worst = grDb_;
        }
        return worst;
    }

  private:
    float    det_ = 0.001f, atk_ = 0.001f, rel_ = 0.0001f, pow_ = 0.f, grDb_ = 0.f;
    Smoother amt_;
};

/** The look-ahead limiter: nothing passes the CEILING. Each sample's peak
 *  is the larger of itself and the midpoint to the next (the drive's
 *  halfband: a 2x oversampled, true-peak-ish detection); the gain
 *  it needs passes a 100 ms release, a minimum over the 1.5 ms look-ahead
 *  and a moving average as long, so the gain is down before the peak
 *  arrives and moves smoothly; the audio is delayed to meet it. A last hard
 *  clamp at the ceiling catches rounding. Latency: Latency() samples. */
class Limiter
{
  public:
    void Init(float sr)
    {
        look_ = std::max(1, (int)std::lround(0.0015 * sr));
        int size = 16;
        while(size < look_ + 16)
            size <<= 1;
        mask_ = size - 1;
        dl_.assign((size_t)size, 0.f);
        dr_.assign((size_t)size, 0.f);
        hold_.assign((size_t)look_, 1.f);
        avg_.assign((size_t)look_, 1.f);
        sum_ = (double)look_;
        rel_ = 1.f - std::exp(-1.f / (0.1f * sr));
        env_ = 1.f;
        ceil_.SetTime(0.02f, sr);
        fresh_ = true;
        w_ = h_ = 0;
    }
    int Latency() const { return look_ + 5; }

    /** In place; returns the deepest gain reduction in the block (dB, >= 0). */
    float Process(float* L, float* R, int n, float ceilingDb)
    {
        const float c = std::pow(10.f, ceilingDb / 20.f);
        if(fresh_)
        {
            ceil_.Set(c);
            fresh_ = false;
        }
        ceil_.Target(c);
        float worst = 1.f;
        for(int k = 0; k < n; k++)
        {
            dl_[(size_t)(w_ & mask_)] = L[k];
            dr_[(size_t)(w_ & mask_)] = R[k];
            // the peak at the sample 6 back, and on to the one after it
            const float cc  = ceil_.Next();
            const float pk  = std::fmax(Peak(dl_), Peak(dr_));
            const float req = pk > cc ? cc / pk : 1.f;
            env_            = std::fmin(req, env_ + (1.f - env_) * rel_);
            hold_[(size_t)h_] = env_;
            float mn          = env_;
            for(int i = 0; i < look_; i++)
                mn = std::fmin(mn, hold_[(size_t)i]);
            sum_ += (double)mn - (double)avg_[(size_t)h_];
            avg_[(size_t)h_] = mn;
            h_               = h_ + 1 == look_ ? 0 : h_ + 1;
            const float g    = (float)(sum_ / (double)look_);
            // the audio that gain was made for: L + 5 samples back
            const int   at = w_ - look_ - 5;
            const float ol = Clamp(dl_[(size_t)(at & mask_)] * g, -cc, cc);
            const float orr = Clamp(dr_[(size_t)(at & mask_)] * g, -cc, cc);
            L[k]           = ol;
            R[k]           = orr;
            w_++;
            if(g < worst)
                worst = g;
        }
        return worst < 1.f ? -20.f * std::log10(worst) : 0.f;
    }

  private:
    /** max(|x[n-6]|, |the halfband midpoint between x[n-6] and x[n-5]|) */
    float Peak(const std::vector<float>& d) const
    {
        const float* c   = HalfbandTaps().c;
        float        mid = 0.f;
        for(int i = 0; i < 6; i++)
            mid += c[i] * (d[(size_t)((w_ - 6 - i) & mask_)] + d[(size_t)((w_ - 5 + i) & mask_)]);
        return std::fmax(std::fabs(d[(size_t)((w_ - 6) & mask_)]), std::fabs(mid));
    }

    std::vector<float> dl_, dr_, hold_, avg_;
    double             sum_ = 1.0;
    int                look_ = 72, mask_ = 127, w_ = 0, h_ = 0;
    float              rel_ = 0.0002f, env_ = 1.f;
    Smoother           ceil_;
    bool               fresh_ = true;
};

/* -------------------------------------------------------------------- duck */
/** A channel ducked by another's amp envelope: the envelope through a
 *  follower (3 ms down, 80 ms back up), the gain 1 - AMOUNT x that. */
struct Ducker
{
    float d = 0.f, atk = 0.1f, rel = 0.001f, amt = 0.f;
    void  Init(float sr)
    {
        atk = 1.f - std::exp(-1.f / (0.003f * sr));
        rel = 1.f - std::exp(-1.f / (0.08f * sr));
        d   = 0.f;
    }
    float Next(float env)
    {
        d += (env - d) * (env > d ? atk : rel);
        return d;
    }
};

} // namespace ammo
