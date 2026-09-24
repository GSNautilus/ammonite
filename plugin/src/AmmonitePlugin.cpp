/** Ammonite as a VST3 / CLAP plugin (DPF): one engine (plugin/engine) per
 *  instance, every engine function as a host parameter.
 *
 *  Parameters: AmmoniteParams.hpp (IDs and values).
 *
 *  UI: the panel (AmmoniteUI.cpp) reaches this instance's engine directly
 *  (DPF direct access): screen, page and readouts on the UI thread, the
 *  audio on the audio thread, as on the hardware.
 *
 *  Clock: SYNC (DAW by default) hands the host's tempo and position to the
 *  engine each block; FREE runs on the TEMPO parameter, like the hardware. */
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"
#include "AmmoniteParams.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <xmmintrin.h> // flush-to-zero

START_NAMESPACE_DISTRHO

using ammonite::Engine;
using ammonite::ParamRef;
using ammonite::Params;

namespace
{
/** The big DSP buffers an engine borrows (~30 MB, sized for 192 kHz). */
struct Buffers
{
    ammonite::DelayLineT delayL[ammonite::kOscs];
    ammonite::DelayLineT delayR[ammonite::kOscs];
    ammonite::ReverbSc   reverb;
    ammonite::PreLineT   preL, preR;
};

/** Denormals off while the engine runs (reverb and delay tails decay into
 *  them, and they are slow on x86). Restores the host's mode after. */
struct ScopedFlushToZero
{
    unsigned int saved;
    ScopedFlushToZero() : saved(_mm_getcsr()) { _mm_setcsr(saved | 0x8040); } // FTZ | DAZ
    ~ScopedFlushToZero() { _mm_setcsr(saved); }
};
} // namespace

class AmmonitePlugin : public Plugin
{
  public:
    AmmonitePlugin()
    : Plugin((uint32_t)Params().size(), 0, 0), bufs_(new Buffers()), engine_(new Engine())
    {
        const std::vector<ParamRef>& ps = Params();
        value_.resize(ps.size());
        for(size_t i = 0; i < ps.size(); i++)
            value_[i] = Engine::FuncDefault(ps[i].func, ps[i].osc);
        // DPF's CLAP wrapper counts bar beats in quarter notes, the VST3 one
        // in time-signature beats (see HostBeat).
        clapBeats_ = std::strcmp(getPluginFormatName(), "CLAP") == 0;
        InitEngine(getSampleRate());
    }

    Engine* GetEngine() { return engine_.get(); }

  protected:
    /* ---------------------------------------------------------- info */
    const char* getLabel() const override { return "Ammonite"; }
    const char* getDescription() const override
    {
        return "A three-voice arpeggiator synthesizer that plays by itself: three "
               "oscillators, each with its own arp, envelope, filter and delay, "
               "over a chord progression.";
    }
    const char* getMaker() const override { return "GSNautilus"; }
    const char* getHomePage() const override { return "https://github.com/GSNautilus/ammonite"; }
    const char* getLicense() const override { return "MIT (ReverbSc: LGPL-2.1)"; }
    uint32_t    getVersion() const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override { return d_cconst('A', 'm', 'm', 'o'); }

    /* ---------------------------------------------------- parameters */
    void initParameter(uint32_t index, Parameter& p) override
    {
        const ParamRef& r     = Params()[index];
        const int       steps = Engine::FuncSteps(r.func);

        int page = -1, sub = 0;
        Engine::FuncPlace(r.func, &page, &sub);
        char name[64] = "";
        if(page >= 0)
        {
            std::strcat(name, engine_->GetPageName(page));
            std::strcat(name, " ");
            const char* s = engine_->GetSubName(page, sub);
            if(s[0])
            {
                std::strcat(name, s);
                std::strcat(name, " ");
            }
        }
        std::strcat(name, Engine::FuncName(r.func));
        if(Engine::FuncPerOsc(r.func))
        {
            char osc[4];
            std::snprintf(osc, sizeof osc, " %d", r.osc + 1);
            std::strcat(name, osc);
        }
        p.name = name;

        // symbol: [a-z0-9_], unique (page, section, name, osc make it so)
        char sym[64];
        size_t k = 0;
        for(const char* c = name; *c && k < sizeof sym - 1; c++)
        {
            const char ch = *c;
            if((ch >= 'A' && ch <= 'Z'))
                sym[k++] = (char)(ch - 'A' + 'a');
            else if((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                sym[k++] = ch;
            else if(k > 0 && sym[k - 1] != '_')
                sym[k++] = '_';
        }
        sym[k] = 0;
        p.symbol = sym;

        p.hints = kParameterIsAutomatable;
        if(steps > 0)
        {
            p.hints |= kParameterIsInteger;
            p.ranges.min = 0.f;
            p.ranges.max = (float)(steps - 1);
            p.ranges.def = ammonite::EngineToHost(r.func, value_[index]);
            p.enumValues.count          = (uint8_t)steps;
            p.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const ev = new ParameterEnumerationValue[steps];
            for(int s = 0; s < steps; s++)
            {
                char txt[16];
                ev[s].value = (float)s;
                ev[s].label = Engine::FuncStepText(r.func, s, txt, sizeof txt);
            }
            p.enumValues.values = ev;
        }
        else
        {
            p.ranges.min = 0.f;
            p.ranges.max = 100.f;
            p.ranges.def = value_[index] * 100.f;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        return ammonite::EngineToHost(Params()[index].func, value_[index]);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        const ParamRef& r = Params()[index];
        const float     v = ammonite::HostToEngine(r.func, value);
        value_[index]     = v;
        engine_->SetParam(r.func, r.osc, v); // smoothed by the audio thread
    }

    /* --------------------------------------------------------- audio */
    void sampleRateChanged(double newSampleRate) override { InitEngine(newSampleRate); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent*, uint32_t) override
    {
        ScopedFlushToZero ftz;
        float*            outL = outputs[0];
        float*            outR = outputs[1];

        // The host clock. A host that stops reporting bars while stopped
        // keeps its last tempo; one that never reported any leaves SYNC DAW
        // on the TEMPO parameter.
        const TimePosition& t       = getTimePosition();
        const bool          valid   = t.bbt.valid;
        const bool          playing = t.playing && valid;
        if(valid)
        {
            hostBpm_  = t.bbt.beatsPerMinute;
            haveHost_ = true;
        }
        const double beat0 = valid ? HostBeat(t.bbt) : 0.0;
        const double bpf   = hostBpm_ / 60.0 / getSampleRate(); // beats per frame

        uint32_t done = 0;
        while(done < frames)
        {
            const uint32_t n = frames - done < kChunk ? frames - done : kChunk;
            if(haveHost_)
                engine_->SetHostClock(playing, hostBpm_, beat0 + bpf * (double)done);
            engine_->ProcessAudio(chunk_, (int)n);
            for(uint32_t i = 0; i < n; i++)
            {
                outL[done + i] = chunk_[2 * i];
                outR[done + i] = chunk_[2 * i + 1];
            }
            done += n;
        }
    }

  private:
    /** Quarter notes since the song start, from DPF's bar / beat / tick.
     *  Exact for x/4 signatures. Ammonite's bars are 4 quarter notes, so in
     *  other signatures its bars and the DAW's drift apart anyway. */
    double HostBeat(const TimePosition::BarBeatTick& b) const
    {
        const double beats = (double)(b.bar - 1) * b.beatsPerBar + (double)(b.beat - 1)
                             + b.tick / b.ticksPerBeat;
        return clapBeats_ || b.beatType <= 0.f ? beats : beats * 4.0 / b.beatType;
    }

    /** Init resets every function to its default: put the host's values
     *  back, unsmoothed (nothing is playing yet). */
    void InitEngine(double sampleRate)
    {
        ammonite::Buffers b;
        for(int o = 0; o < ammonite::kOscs; o++)
        {
            b.delayL[o] = &bufs_->delayL[o];
            b.delayR[o] = &bufs_->delayR[o];
        }
        b.reverb = &bufs_->reverb;
        b.preL   = &bufs_->preL;
        b.preR   = &bufs_->preR;
        engine_->Init((float)sampleRate, b);
        const std::vector<ParamRef>& ps = Params();
        for(size_t i = 0; i < ps.size(); i++)
            engine_->SetEngineParam(ps[i].func, ps[i].osc, value_[i]);
    }

    static constexpr uint32_t kChunk = 256;

    std::unique_ptr<Buffers> bufs_;
    std::unique_ptr<Engine>  engine_;
    std::vector<float>       value_; // engine values (0..1) of every parameter
    bool                     clapBeats_ = false;
    bool                     haveHost_  = false; // the host ever sent a clock
    double                   hostBpm_   = 120.0;
    float                    chunk_[2 * kChunk];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmmonitePlugin)
};

Plugin* createPlugin()
{
    return new AmmonitePlugin();
}

END_NAMESPACE_DISTRHO

ammonite::Engine* ammonite::EngineOf(void* pluginInstance)
{
    return static_cast<DISTRHO_NAMESPACE::AmmonitePlugin*>(pluginInstance)->GetEngine();
}
