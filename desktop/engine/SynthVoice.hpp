/** The synth voice (the instrument of phase 2): two PolyBLEP oscillators
 *  morphing sine -> triangle -> saw -> square, the second detuned in cents,
 *  a sub (an octave down, sine), white noise; tanh drive; a TPT state
 *  variable filter with a low -> band -> high morph; ADSR amp envelope, AD
 *  filter envelope (bipolar octaves), AD pitch envelope (semitones); glide.
 *  Monophonic: a new note retriggers from the current level, or glides.
 *
 *  Pitch, filter and the modulation envelopes advance on a control tick
 *  every kCtrl samples (the hardware's rule); the amp envelope per sample. */
#pragma once

#include "Dsp.hpp"

namespace ammo {

struct VoiceParams
{
    float shape = 0.3f, detuneCents = 6.f, sub = 0.f, noise = 0.f;
    float cutoff = 0.8f, reso = 0.2f, filterType = 0.f, drive = 0.f;
    float ampA = 0.1f, ampD = 0.45f, ampS = 0.3f, ampR = 0.45f;
    float filtAmount = 0.f, filtA = 0.f, filtD = 0.4f;
    float pitchAmount = 0.f, pitchD = 0.3f;
    float glide = 0.f;
    float tuneCents = 0.f; // FINE TUNE: on the pitch, with the PITCH envelope
};

class SynthVoice
{
  public:
    static constexpr int kCtrl = 16;

    void Init(float sampleRate, uint32_t seed)
    {
        sr_  = sampleRate;
        rng_ = Rng(seed);
        oscA_.Reset(0.f);
        oscB_.Reset(0.f); // in phase: a fixed offset would null one harmonic of the sum
        sub_.Reset(0.f);
        samples_ = lastWrap_ = 0;
        filter_.Reset();
        amp_   = Adsr();
        fenv_  = AdEnv();
        penv_  = AdEnv();
        midi_  = target_ = 60.f;
        gate_  = 0;
        ctrl_  = 0;
        level_ = 0.f;
        fresh_ = true;
    }

    /** Per block, before Process: the smoothed parameter values. */
    void SetParams(const VoiceParams& p)
    {
        // the times only when they move (modulation sets the parameters every kModBlock samples)
        const bool same = !fresh_ && p.ampA == p_.ampA && p.ampD == p_.ampD && p.ampS == p_.ampS && p.ampR == p_.ampR &&
                          p.filtA == p_.filtA && p.filtD == p_.filtD && p.pitchD == p_.pitchD && p.glide == p_.glide;
        p_     = p;
        fresh_ = false;
        if(same)
            return;
        amp_.SetTimes(AttackSeconds(p.ampA), DecaySeconds(p.ampD), p.ampS, DecaySeconds(p.ampR), sr_);
        // the modulation envelopes advance once per control tick
        fenv_.SetTimes(AttackSeconds(p.filtA), DecaySeconds(p.filtD), sr_ / (float)kCtrl);
        penv_.SetTimes(0.f, PitchDecaySeconds(p.pitchD), sr_ / (float)kCtrl);
        const float g = GlideSeconds(p.glide);
        glideCoef_    = g > 0.001f ? 1.f - std::exp(-4.6f * (float)kCtrl / (g * sr_)) : 1.f;
    }

    /** Start a note: `gateSamples` until the release (<= 0: until NoteOff). */
    void NoteOn(float midi, int gateSamples)
    {
        target_ = midi;
        if(!amp_.Active() || glideCoef_ >= 1.f)
            midi_ = midi; // from silence, or no glide: land on it
        amp_.Trigger();
        fenv_.Trigger();
        penv_.Trigger();
        gate_ = gateSamples > 0 ? gateSamples : -1;
    }
    void NoteOff() { amp_.Release(); }
    bool Active() const { return amp_.Active(); }
    float Level() const { return level_; }
    float Note() const { return midi_; }
    float Target() const { return target_; }

    /** Mono output, added into nothing: writes `out[0..n)`, and the amp
     *  envelope into `env[0..n)` when given (a ducking source). */
    void Process(float* out, int n, float* env = nullptr)
    {
        for(int i = 0; i < n; i++)
        {
            if(ctrl_ == 0)
                ControlTick();
            ctrl_ = (ctrl_ + 1) % kCtrl;

            if(gate_ > 0 && --gate_ == 0)
                amp_.Release();

            const float a = oscA_.Next(p_.shape), b = oscB_.Next(p_.shape);
            samples_++;
            if(oscA_.wrapped())
                lastWrap_ = samples_; // the first sample of the new cycle
            float s = (a + b) * 0.5f;
            if(p_.sub > 0.f)
                s += std::sin(sub_.phase * kTau) * p_.sub;
            sub_.phase += sub_.inc;
            if(sub_.phase >= 1.f)
                sub_.phase -= 1.f;
            if(p_.noise > 0.f)
                s += rng_.Bipolar() * p_.noise;
            if(p_.drive > 0.f)
            {
                const float gain = 1.f + p_.drive * 7.f;
                s                = std::tanh(s * gain) / std::tanh(gain);
            }
            s      = filter_.Next(s, p_.filterType);
            level_ = amp_.Next();
            out[i] = s * level_ * 0.3f;
            if(env)
                env[i] = level_;
        }
    }

    /** The main oscillator's period in samples, and the running sample count
     *  at which its last cycle began (for the telemetry's wave window). */
    float    PeriodSamples() const { return oscA_.inc > 1e-6f ? 1.f / oscA_.inc : 1.f; }
    uint32_t LastWrapSample() const { return lastWrap_; }
    uint32_t SamplesProcessed() const { return samples_; }

  private:
    void ControlTick()
    {
        midi_ += (target_ - midi_) * glideCoef_;
        const float pitchEnv = penv_.Next() * p_.pitchAmount * 12.f;
        const float hz       = MidiToHz(midi_ + pitchEnv + p_.tuneCents * 0.01f);
        const float det      = std::exp2(p_.detuneCents / 1200.f);
        oscA_.SetFreq(hz, sr_);
        oscB_.SetFreq(hz * det, sr_);
        sub_.SetFreq(hz * 0.5f, sr_);
        const float fe = fenv_.Next() * p_.filtAmount * 4.f; // +-4 octaves
        filter_.Set(CutoffHz(p_.cutoff) * std::exp2(fe), p_.reso, sr_);
    }

    float       sr_ = 48000.f;
    VoiceParams p_;
    Rng         rng_;
    Oscillator  oscA_, oscB_, sub_;
    Svf         filter_;
    Adsr        amp_;
    AdEnv       fenv_, penv_;
    float       midi_ = 60.f, target_ = 60.f, glideCoef_ = 1.f, level_ = 0.f;
    int         gate_ = 0, ctrl_ = 0;
    bool        fresh_ = true; // SetParams computes the times the first time whatever they are
    uint32_t    samples_ = 0, lastWrap_ = 0;
};

} // namespace ammo
