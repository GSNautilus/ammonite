/** Ammonite Desktop as a plugin (DPF): ammo::Engine behind the host's
 *  parameter list. Host parameter index i <-> engine id VisibleId(i)
 *  (engine/Params.hpp). The modulation routes are not host parameters: they
 *  are the plugin's one state, "routes" (the engine's route chunk), saved
 *  and restored with the host's project. The host clock comes from DPF's
 *  time position. */
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"
#include "Engine.hpp"

#include <cstring>
#include <memory>

START_NAMESPACE_DISTRHO

class AmmonitePlugin : public Plugin
{
  public:
    AmmonitePlugin() : Plugin((uint32_t)ammo::VisibleParamCount(), 0, 1)
    {
        clapBeats_ = std::strcmp(getPluginFormatName(), "CLAP") == 0;
        engine_.reset(new ammo::Engine(getSampleRate(), (int)getBufferSize()));
        setLatency((uint32_t)engine_->latency());
    }

    ammo::Engine* GetEngine() { return engine_.get(); }

  protected:
    /* ---------------------------------------------------------- info */
    const char* getLabel() const override { return "AmmoniteDesktop"; }
    const char* getDescription() const override
    {
        return "A generative, harmony-driven instrument that plays itself: channels of arps and voices over a chord "
               "progression, drawn as waveform shells.";
    }
    const char* getMaker() const override { return "GSNautilus"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t    getVersion() const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override { return AMMO_UNIQUE_ID; }

    /* ---------------------------------------------------- parameters */
    void initParameter(uint32_t index, Parameter& p) override
    {
        const int             id = ammo::VisibleId((int)index);
        const ammo::ParamDef& d  = ammo::ParamInfo(id);
        int                   slot, sp;
        char                  name[64], sym[64];
        if(ammo::IsSlotId(id, slot, sp))
        {
            std::snprintf(name, sizeof name, "%d %s", slot + 1, d.name);
            std::snprintf(sym, sizeof sym, "s%d_%s", slot + 1, d.symbol);
        }
        else
        {
            std::snprintf(name, sizeof name, "%s", d.name);
            std::snprintf(sym, sizeof sym, "%s", d.symbol);
        }
        p.name       = name;
        p.symbol     = sym;
        p.ranges.min = d.min;
        p.ranges.max = d.max;
        p.ranges.def = d.def;
        p.hints      = 0;
        if(d.flags & ammo::pfAutomatable)
            p.hints |= kParameterIsAutomatable;
        if(d.flags & ammo::pfInteger)
            p.hints |= kParameterIsInteger;
        if(d.flags & ammo::pfBoolean)
            p.hints |= kParameterIsBoolean;
        if(d.steps > 0 && d.names != nullptr)
        {
            p.enumValues.count          = (uint8_t)d.steps;
            p.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const ev = new ParameterEnumerationValue[d.steps];
            for(int s = 0; s < d.steps; s++)
            {
                ev[s].value = d.min + (float)s;
                ev[s].label = d.names[s];
            }
            p.enumValues.values = ev;
        }
    }

    float getParameterValue(uint32_t index) const override { return engine_->GetParam(ammo::VisibleId((int)index)); }
    void  setParameterValue(uint32_t index, float value) override { engine_->SetParam(ammo::VisibleId((int)index), value); }

    /* --------------------------------------------------------- state */
    void initState(uint32_t index, State& state) override
    {
        if(index != 0)
            return;
        state.key          = "routes";
        state.defaultValue = "";
        state.label        = "Modulation routes";
        state.hints        = 0;
    }

    String getState(const char* key) const override
    {
        return String(std::strcmp(key, "routes") == 0 ? engine_->SaveRoutes().c_str() : "");
    }

    void setState(const char* key, const char* value) override
    {
        if(std::strcmp(key, "routes") == 0 && value != nullptr)
            engine_->LoadRoutes(value, std::strlen(value)); // "" (the default): no chunk, nothing changes
    }

    /* --------------------------------------------------------- audio */
    void sampleRateChanged(double newSampleRate) override
    {
        engine_->SetSampleRate(newSampleRate);
        setLatency((uint32_t)engine_->latency());
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent*, uint32_t) override
    {
        const TimePosition& t = getTimePosition();
        ammo::HostClock     hc;
        hc.valid   = t.bbt.valid;
        hc.playing = t.playing || !t.bbt.valid; // a host that says nothing of its transport: the engine plays itself
        if(t.bbt.valid)
        {
            hc.bpm = t.bbt.beatsPerMinute;
            const double beats = (double)(t.bbt.bar - 1) * t.bbt.beatsPerBar + (double)(t.bbt.beat - 1)
                                 + t.bbt.tick / t.bbt.ticksPerBeat;
            // DPF's CLAP wrapper counts bar beats in quarter notes, the VST3 one in time-signature beats
            hc.beat = clapBeats_ || t.bbt.beatType <= 0.f ? beats : beats * 4.0 / t.bbt.beatType;
        }
        engine_->Process(outputs[0], outputs[1], (int)frames, &hc);
    }

  private:
    std::unique_ptr<ammo::Engine> engine_;
    bool                          clapBeats_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmmonitePlugin)
};

Plugin* createPlugin()
{
    return new AmmonitePlugin();
}

END_NAMESPACE_DISTRHO

/** For the UI (direct access): the engine behind a plugin instance. */
ammo::Engine* AmmoniteEngineOf(void* pluginInstance)
{
    return pluginInstance != nullptr ? static_cast<DISTRHO_NAMESPACE::AmmonitePlugin*>(pluginInstance)->GetEngine()
                                     : nullptr;
}
