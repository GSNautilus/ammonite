/** The drum voice (phase 4): eight analog-style models, each a few
 *  oscillators, filtered noise and exponential envelopes, shaped by four
 *  knobs that mean the same thing on every model:
 *
 *  TUNE  semitones (-12..+12) on top of the note (a drum follows the note
 *        relative to C4, so a Rhythm channel can tune it to the chord; the
 *        tom and the rim are tuned to C at Tune 0 so they land on the
 *        chord's notes, the kick sits at its natural G1).
 *  DECAY how long it rings.
 *  TONE  brightness: the noise filter, or the kick's drive.
 *  SNAP  the attack: the pitch sweep of kick and tom, the noise in a
 *        snare, the click of a rim, the metal-to-noise mix of hats.
 *
 *  Kick: a sine swept down from up to 6x its pitch, a noise click, drive.
 *  Snare: two detuned sines and band/high-passed noise. Hats: six square
 *  waves at inharmonic ratios (the classic 808 set) and noise, high-passed;
 *  closed and open differ in decay. Clap: band-passed noise in three quick
 *  bursts, then a tail. Tom: a sine with a gentler sweep. Rim: two short
 *  sines and a noise click. Noise: low-passed noise, Tone sets the cutoff.
 *
 *  One voice per channel, retriggered (an open hat is choked by the next
 *  hit, as on the machines). Runs every sample whether sounding or not, so
 *  its output never depends on where blocks begin. */
#pragma once

#include "Dsp.hpp"

namespace ammo {

enum DrumModel
{
    kDrumKick = 0,
    kDrumSnare,
    kDrumHat,
    kDrumOpenHat,
    kDrumClap,
    kDrumTom,
    kDrumRim,
    kDrumNoise,
    kNumDrumModels
};

struct DrumParams
{
    int   model = kDrumKick;
    float tune = 0.f, decay = 0.5f, tone = 0.5f, snap = 0.5f;
};

class DrumVoice
{
  public:
    void Init(float sampleRate, uint32_t seed)
    {
        sr_  = sampleRate;
        rng_ = Rng(seed);
        nf_.Reset();
        bodyEnv_ = noiseEnv_ = pitchEnv_ = clickEnv_ = level_ = 0.f;
        ph1_ = ph2_ = 0.f;
        for(float& p : metal_)
            p = 0.f;
        t_ = 1 << 30;
        samples_ = lastWrap_ = 0;
        vel_ = 1.f;
    }

    void SetParams(const DrumParams& p) { p_ = p; }

    /** A hit: the note tunes it (relative to C4), `vel` 0..1 scales it. */
    void Hit(float midi, float vel)
    {
        const int   m     = p_.model < 0 || p_.model >= kNumDrumModels ? kDrumKick : p_.model;
        const float ratio = std::exp2((Clamp(p_.tune, -24.f, 24.f) + Clamp(midi - 60.f, -36.f, 36.f)) / 12.f);
        const float d = Clamp01(p_.decay), tone = Clamp01(p_.tone), snap = Clamp01(p_.snap);
        model_    = m;
        vel_      = Clamp01(vel);
        t_        = 0;
        bodyEnv_  = 1.f;
        noiseEnv_ = 1.f;
        pitchEnv_ = 1.f;
        clickEnv_ = 1.f;
        ph1_ = ph2_ = 0.f; // every hit starts the same way, as a drum machine's
        nf_.Reset();
        bodyMul_ = noiseMul_ = pitchMul_ = clickMul_ = 0.f;
        sweep_ = 0.f;
        mixBody_ = mixNoise_ = 0.f;
        switch(m)
        {
            case kDrumKick:
                f1_       = 49.f * ratio; // G1
                sweep_    = 5.f * snap;
                pitchMul_ = Mul(0.03f);
                bodyMul_  = Mul(0.12f + d * d * 1.4f);
                clickMul_ = Mul(0.003f);
                mixBody_  = 1.f;
                mixNoise_ = 0.35f * snap;
                drive_    = 1.f + tone * 5.f;
                break;
            case kDrumSnare:
                f1_       = 185.f * ratio;
                f2_       = f1_ * 1.78f;
                sweep_    = 0.4f;
                pitchMul_ = Mul(0.02f);
                bodyMul_  = Mul(0.06f + d * 0.15f);
                noiseMul_ = Mul(0.08f + d * d * 0.5f);
                nf_.Set(1500.f + tone * 7000.f, 0.15f, sr_);
                mixBody_  = 1.f - 0.6f * snap;
                mixNoise_ = 0.4f + 0.6f * snap;
                break;
            case kDrumHat:
            case kDrumOpenHat:
                f1_       = ratio;
                noiseMul_ = Mul(m == kDrumHat ? 0.012f + d * 0.06f : 0.15f + d * d * 1.2f);
                nf_.Set(5000.f + tone * 7000.f, 0.2f, sr_);
                mixBody_  = 1.f - 0.7f * snap; // the metal
                mixNoise_ = 0.3f + 0.7f * snap;
                break;
            case kDrumClap:
                noiseMul_ = Mul(0.006f + snap * 0.004f);
                bodyMul_  = Mul(0.08f + d * 0.4f); // the tail
                nf_.Set((900.f + tone * 1800.f) * std::sqrt(ratio), 0.4f, sr_);
                mixNoise_ = 1.f;
                break;
            case kDrumTom:
                f1_       = 130.81f * ratio; // C3
                sweep_    = 0.8f * snap;
                pitchMul_ = Mul(0.06f);
                bodyMul_  = Mul(0.12f + d * d * 0.8f);
                clickMul_ = Mul(0.002f);
                mixBody_  = 1.f;
                mixNoise_ = 0.15f;
                break;
            case kDrumRim:
                f1_       = 1567.98f * ratio; // G6 and C5: a fifth apart
                f2_       = 523.25f * ratio;
                bodyMul_  = Mul(0.008f);
                pitchMul_ = Mul(0.008f * (1.f + 3.f * d)); // the low sine's decay
                clickMul_ = Mul(0.004f);
                nf_.Set(2500.f + tone * 4000.f, 0.5f, sr_);
                mixBody_  = 1.f;
                mixNoise_ = 0.3f + 0.7f * snap;
                break;
            default: // noise
                noiseMul_ = Mul(0.03f + d * d * 1.5f);
                nf_.Set(200.f * std::exp2(tone * 6.5f) * std::sqrt(ratio), 0.1f + 0.7f * snap, sr_);
                mixNoise_ = 1.f;
                break;
        }
    }

    bool  Active() const { return level_ > 1e-4f || t_ < 64; }
    float Level() const { return level_; }

    /** Writes `out[0..n)`, and the envelope into `env[0..n)` when given. */
    void Process(float* out, int n, float* env = nullptr)
    {
        for(int i = 0; i < n; i++)
        {
            out[i] = Next();
            if(env)
                env[i] = level_;
        }
    }

    float    PeriodSamples() const { return period_; }
    uint32_t LastWrapSample() const { return lastWrap_; }

  private:
    float Mul(float seconds) const { return std::exp(-1.f / (seconds * sr_)); }

    float Next()
    {
        samples_++;
        if(t_ < (1 << 30))
            t_++;
        const float noise = rng_.Bipolar();
        float       s = 0.f, env = 0.f;
        switch(model_)
        {
            case kDrumKick:
            case kDrumTom:
            {
                const float hz = f1_ * (1.f + sweep_ * pitchEnv_);
                pitchEnv_ *= pitchMul_;
                Wrap(ph1_, hz);
                float body = std::sin(ph1_ * kTau) * bodyEnv_;
                if(model_ == kDrumKick)
                    body = std::tanh(body * drive_) / std::tanh(drive_);
                s   = body * mixBody_ + noise * clickEnv_ * mixNoise_;
                env = bodyEnv_;
                bodyEnv_ *= bodyMul_;
                clickEnv_ *= clickMul_;
                break;
            }
            case kDrumSnare:
            {
                const float k = 1.f + sweep_ * pitchEnv_;
                pitchEnv_ *= pitchMul_;
                Wrap(ph1_, f1_ * k);
                ph2_ += f2_ * k / sr_;
                if(ph2_ >= 1.f)
                    ph2_ -= 1.f;
                const float body = (std::sin(ph1_ * kTau) + 0.6f * std::sin(ph2_ * kTau)) * 0.62f * bodyEnv_;
                const float nz   = nf_.Next(noise, 0.75f) * noiseEnv_ * 1.6f;
                s   = body * mixBody_ + nz * mixNoise_;
                env = std::fmax(bodyEnv_, noiseEnv_);
                bodyEnv_ *= bodyMul_;
                noiseEnv_ *= noiseMul_;
                break;
            }
            case kDrumHat:
            case kDrumOpenHat:
            {
                static const float kRatios[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
                float metal = 0.f;
                for(int k = 0; k < 6; k++)
                {
                    metal_[k] += kRatios[k] * f1_ / sr_;
                    if(metal_[k] >= 1.f)
                        metal_[k] -= 1.f;
                    metal += metal_[k] < 0.5f ? 1.f : -1.f;
                }
                const float x = nf_.Next(metal * (1.f / 6.f) * mixBody_ + noise * mixNoise_, 1.f);
                s   = x * noiseEnv_ * 1.4f;
                env = noiseEnv_;
                noiseEnv_ *= noiseMul_;
                Wrap(ph1_, 220.f);
                break;
            }
            case kDrumClap:
            {
                // three bursts 10 ms apart, then the tail from 30 ms
                const int burst = (int)(0.01f * sr_);
                if(t_ == burst || t_ == 2 * burst)
                    noiseEnv_ = 1.f;
                const float tail = t_ >= 3 * burst ? bodyEnv_ : 0.f;
                if(t_ >= 3 * burst)
                    bodyEnv_ *= bodyMul_;
                const float e = std::fmax(noiseEnv_, 0.7f * tail);
                s   = nf_.Next(noise, 0.5f) * e * 2.2f;
                env = e;
                noiseEnv_ *= noiseMul_;
                Wrap(ph1_, 220.f);
                break;
            }
            case kDrumRim:
            {
                Wrap(ph1_, f1_);
                ph2_ += f2_ / sr_;
                if(ph2_ >= 1.f)
                    ph2_ -= 1.f;
                const float hi = std::sin(ph1_ * kTau) * bodyEnv_, lo = std::sin(ph2_ * kTau) * pitchEnv_ * 0.7f;
                const float nz = nf_.Next(noise, 0.5f) * clickEnv_ * 1.5f;
                s   = (hi + lo) * mixBody_ * 0.8f + nz * mixNoise_;
                env = std::fmax(bodyEnv_, pitchEnv_);
                bodyEnv_ *= bodyMul_;
                pitchEnv_ *= pitchMul_;
                clickEnv_ *= clickMul_;
                break;
            }
            default:
            {
                s   = nf_.Next(noise, 0.f) * noiseEnv_ * 1.2f;
                env = noiseEnv_;
                noiseEnv_ *= noiseMul_;
                Wrap(ph1_, 220.f);
                break;
            }
        }
        if(t_ >= (1 << 30))
            s = env = 0.f;
        level_ = env * vel_;
        return s * vel_ * 0.6f;
    }

    /** Advance a body phase; its wraps align the telemetry's wave window. */
    void Wrap(float& ph, float hz)
    {
        const float inc = Clamp(hz / sr_, 1e-6f, 0.45f);
        period_         = 1.f / inc;
        ph += inc;
        if(ph >= 1.f)
        {
            ph -= 1.f;
            lastWrap_ = samples_;
        }
    }

    float      sr_ = 48000.f;
    DrumParams p_;
    Rng        rng_;
    Svf        nf_;
    int        model_ = kDrumKick, t_ = 1 << 30;
    float      vel_ = 1.f, level_ = 0.f, period_ = 218.f;
    float      f1_ = 50.f, f2_ = 100.f, sweep_ = 0.f, drive_ = 1.f, mixBody_ = 1.f, mixNoise_ = 0.f;
    float      ph1_ = 0.f, ph2_ = 0.f, metal_[6] = {};
    float      bodyEnv_ = 0.f, noiseEnv_ = 0.f, pitchEnv_ = 0.f, clickEnv_ = 0.f;
    float      bodyMul_ = 0.f, noiseMul_ = 0.f, pitchMul_ = 0.f, clickMul_ = 0.f;
    uint32_t   samples_ = 0, lastWrap_ = 0;
};

} // namespace ammo
