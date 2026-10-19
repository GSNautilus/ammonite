/** Ammonite Test, the DSP: one voice at 110 Hz, an oscillator morphing
 *  sine -> triangle -> saw -> soft square (the same formulas as the shell
 *  shader), a decay envelope retriggered on every beat of the host clock
 *  (a free 100 BPM clock when the host gives none), stereo out at about
 *  -12 dBFS, any sample rate. Once per block it publishes a telemetry
 *  frame (Telemetry.hpp) the UI reads.
 *
 *  Parameters: 0 Shape (0..1, default 0.25), 1 Level (0..1, default 0.5),
 *  both automatable, smoothed over 10 ms on the audio thread. */
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"
#include "Telemetry.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <xmmintrin.h> // flush-to-zero

START_NAMESPACE_DISTRHO

namespace
{
enum
{
    kShape = 0,
    kLevel,
    kParamCount
};

constexpr float  kPi      = 3.14159265358979f;
constexpr float  kFreq    = 110.f;  // A2
constexpr double kFreeBpm = 100.0;
constexpr int    kHist    = 4096;   // > one period at 192 kHz (1745 samples)

std::atomic<int> gInstances{0};

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// The four shapes over one period p in 0..1 (as ORB_VS in test/web/index.html).
inline float Tri(float p) { return p < .25f ? 4.f * p : (p < .75f ? 2.f - 4.f * p : 4.f * p - 4.f); }
inline float Saw7(float p) // the first 7 harmonics: band-limited for the ear and the eye
{
    float s = 0.f;
    for(int k = 1; k <= 7; k++)
        s += std::sin(p * 2.f * kPi * (float)k) / (float)k * ((k & 1) ? 1.f : -1.f);
    return s * 0.6f;
}
inline float Sqr(float p) { return std::tanh(std::sin(p * 2.f * kPi) * 5.f); }
inline float Wave(float p, float shape)
{
    const float s = std::sin(p * 2.f * kPi), x = Clamp01(shape) * 3.f;
    if(x < 1.f)
        return s + (Tri(p) - s) * x;
    if(x < 2.f)
        return Tri(p) + (Saw7(p) - Tri(p)) * (x - 1.f);
    return Saw7(p) + (Sqr(p) - Saw7(p)) * (x - 2.f);
}

/** Denormals off while we run; restores the host's mode after. */
struct ScopedFlushToZero
{
    unsigned int saved;
    ScopedFlushToZero() : saved(_mm_getcsr()) { _mm_setcsr(saved | 0x8040); }
    ~ScopedFlushToZero() { _mm_setcsr(saved); }
};
} // namespace

class TestPlugin : public Plugin
{
  public:
    TestPlugin() : Plugin(kParamCount, 0, 0)
    {
        target_[kShape] = 0.25f;
        target_[kLevel] = 0.5f;
        // DPF's CLAP wrapper counts bar beats in quarter notes, the VST3 one
        // in time-signature beats (see HostBeat).
        clapBeats_ = std::strcmp(getPluginFormatName(), "CLAP") == 0;
        std::memset(hist_, 0, sizeof hist_);
        Reset(getSampleRate());
    }

    ammo::Telemetry& Telemetry() { return tele_; }

  protected:
    /* ---------------------------------------------------------- info */
    const char* getLabel() const override { return DISTRHO_PLUGIN_NAME; }
    const char* getDescription() const override
    {
        return "Ammonite Desktop phase 1 test plugin (" AMMO_VARIANT " UI): one voice, one knob, one shell.";
    }
    const char* getMaker() const override { return "GSNautilus"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t    getVersion() const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override { return AMMO_UNIQUE_ID; }

    /* ---------------------------------------------------- parameters */
    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints      = kParameterIsAutomatable;
        p.ranges.min = 0.f;
        p.ranges.max = 1.f;
        switch(index)
        {
            case kShape:
                p.name       = "Shape";
                p.symbol     = "shape";
                p.ranges.def = 0.25f;
                break;
            case kLevel:
                p.name       = "Level";
                p.symbol     = "level";
                p.ranges.def = 0.5f;
                break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParamCount ? target_[index].load(std::memory_order_relaxed) : 0.f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if(index < kParamCount)
            target_[index].store(Clamp01(value), std::memory_order_relaxed);
    }

    /* --------------------------------------------------------- audio */
    void sampleRateChanged(double newSampleRate) override { Reset(newSampleRate); }

    /** Instances are numbered when first activated: DPF also constructs a
     *  throwaway instance per process for the descriptor, never activated. */
    void activate() override
    {
        if(instance_ == 0)
            instance_ = ++gInstances;
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent*, uint32_t) override
    {
        ScopedFlushToZero ftz;
        float* const      outL = outputs[0];
        float* const      outR = outputs[1];
        const double      sr   = getSampleRate();

        // The clock: the host's while it plays, else our own, continuing
        // from where the last block ended at the last known tempo.
        const TimePosition& t       = getTimePosition();
        const bool          valid   = t.bbt.valid;
        const bool          playing = valid && t.playing;
        if(valid)
        {
            bpm_      = t.bbt.beatsPerMinute;
            haveHost_ = true;
        }
        const double beat0 = playing ? HostBeat(t.bbt) : beat_;
        const double bpf   = bpm_ / 60.0 / sr; // beats per frame

        const float shapeT = target_[kShape].load(std::memory_order_relaxed);
        const float levelT = target_[kLevel].load(std::memory_order_relaxed);

        double sumSq = 0.0;
        float  peak  = 0.f;
        for(uint32_t i = 0; i < frames; i++)
        {
            shape_ += (shapeT - shape_) * smooth_;
            level_ += (levelT - level_) * smooth_;

            const long long bi = (long long)std::floor(beat0 + bpf * (double)i);
            if(bi != lastBeat_)
            {
                lastBeat_ = bi;
                env_      = 1.f; // the pluck
                phase_    = 0.f; // from a zero crossing: no click, and the wave window realigns
                wrapPos_  = histPos_;
            }
            env_ *= envDecay_;

            const float w = Wave(phase_, shape_);
            const float s = w * env_ * level_ * 0.5f; // Level 0.5 -> peak 0.25 = -12 dBFS
            outL[i] = outR[i] = s;

            hist_[histPos_] = s;
            histPos_        = (histPos_ + 1) & (kHist - 1);
            phase_ += inc_;
            if(phase_ >= 1.f)
            {
                phase_ -= 1.f;
                wrapPos_ = histPos_; // first sample of the new period
            }
            sumSq += (double)s * s;
            if(s > peak)
                peak = s;
            else if(-s > peak)
                peak = -s;
        }
        beat_ = beat0 + bpf * (double)frames;

        PublishFrame(frames, sr, playing, (float)std::sqrt(sumSq / (frames > 0 ? frames : 1)), peak);
    }

  private:
    /** Quarter notes since the song start, from DPF's bar / beat / tick. */
    double HostBeat(const TimePosition::BarBeatTick& b) const
    {
        const double beats = (double)(b.bar - 1) * b.beatsPerBar + (double)(b.beat - 1) + b.tick / b.ticksPerBeat;
        return clapBeats_ || b.beatType <= 0.f ? beats : beats * 4.0 / b.beatType;
    }

    void Reset(double sr)
    {
        if(sr <= 0.0)
            sr = 48000.0;
        inc_      = kFreq / (float)sr;
        smooth_   = 1.f - std::exp(-1.f / (0.010f * (float)sr)); // 10 ms
        envDecay_ = std::exp(-1.f / (0.25f * (float)sr));        // 250 ms
        shape_    = target_[kShape].load(std::memory_order_relaxed);
        level_    = target_[kLevel].load(std::memory_order_relaxed);
        if(!haveHost_)
            bpm_ = kFreeBpm;
        std::memset(hist_, 0, sizeof hist_);
        histPos_ = wrapPos_ = 0;
        phase_   = 0.f;
    }

    /** One period of the output ending at the last phase wrap, resampled to
     *  kWaveLen points: since it starts and ends at phase 0 it tiles. */
    void PublishFrame(uint32_t frames, double sr, bool playing, float rms, float peak)
    {
        ammo::Frame& f = tele_.beginWrite();
        f.seq          = ++seq_;
        f.shape        = shape_;
        f.level        = level_;
        f.rms          = rms;
        f.peak         = peak;
        f.env          = env_;
        f.beat         = beat_;
        f.bpm          = bpm_;
        f.playing      = playing;
        f.hostClock    = haveHost_;
        f.instance     = instance_;
        f.sampleRate   = sr;
        f.blockSize    = frames;
        const int period = (int)(sr / kFreq);
        for(int k = 0; k < ammo::kWaveLen; k++)
        {
            const int idx = (wrapPos_ - period + (period * k) / ammo::kWaveLen + kHist) & (kHist - 1);
            f.wave[k]     = hist_[idx];
        }
        tele_.publish();
    }

    ammo::Telemetry    tele_;
    int                instance_ = 0;
    std::atomic<float> target_[kParamCount];
    float              shape_ = 0.25f, level_ = 0.5f, smooth_ = 0.01f;
    float              phase_ = 0.f, inc_ = 0.f, env_ = 0.f, envDecay_ = 0.999f;
    double             beat_ = 0.0, bpm_ = kFreeBpm;
    long long          lastBeat_ = -1;
    bool               haveHost_ = false, clapBeats_ = false;
    uint32_t           seq_ = 0;
    float              hist_[kHist];
    int                histPos_ = 0, wrapPos_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestPlugin)
};

Plugin* createPlugin()
{
    return new TestPlugin();
}

END_NAMESPACE_DISTRHO

ammo::Telemetry* ammo::TelemetryOf(void* pluginInstance)
{
    return pluginInstance != nullptr ? &static_cast<DISTRHO_NAMESPACE::TestPlugin*>(pluginInstance)->Telemetry()
                                     : nullptr;
}
