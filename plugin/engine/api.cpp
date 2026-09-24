/** C API of the plugin's engine copy, for the headless tests (ctypes).
 *  Not part of the plugin itself.
 *
 *  synth_*: the very calls of core/api.cpp, on one default instance, so the
 *  whole test suite runs against this engine unchanged (SYNTH_DLL).
 *  eng_*:   the same calls on an instance from eng_create(), to run several
 *           engines side by side. */
#include "engine.h"

#if defined(_WIN32)
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C"
#endif

using ammonite::Engine;

/** An engine with the big DSP buffers it borrows (~30 MB: sized for 192 kHz). */
struct Instance
{
    Engine                   engine;
    ammonite::DelayLineT     delayL[ammonite::kOscs];
    ammonite::DelayLineT     delayR[ammonite::kOscs];
    ammonite::ReverbSc       reverb;
    ammonite::PreLineT       preL, preR;

    void Init(float samplerate)
    {
        ammonite::Buffers buffers;
        for(int o = 0; o < ammonite::kOscs; o++)
        {
            buffers.delayL[o] = &delayL[o];
            buffers.delayR[o] = &delayR[o];
        }
        buffers.reverb = &reverb;
        buffers.preL   = &preL;
        buffers.preR   = &preR;
        engine.Init(samplerate, buffers);
    }
};

static Instance* Default()
{
    static Instance* inst = new Instance();
    return inst;
}
static Engine& E(void* h)
{
    return static_cast<Instance*>(h)->engine;
}

/* ----------------------------------------------------------- instances */
API void* eng_create()
{
    return new Instance(); // value-initialized: zeroed like statics
}
API void eng_destroy(void* h)
{
    delete static_cast<Instance*>(h);
}
API void eng_init(void* h, float samplerate)
{
    static_cast<Instance*>(h)->Init(samplerate);
}
API void eng_set_pot(void* h, int pot, float value) { E(h).SetPot(pot, value); }
API float eng_get_pot(void* h, int pot) { return E(h).GetPot(pot); }
API int eng_get_page(void* h) { return E(h).GetPage(); }
API const char* eng_get_page_name(void* h, int page) { return E(h).GetPageName(page); }
API int eng_get_num_subs(void* h, int page) { return E(h).GetNumSubs(page); }
API int eng_get_sub(void* h, int page) { return E(h).GetSub(page); }
API const char* eng_get_sub_name(void* h, int page, int sub) { return E(h).GetSubName(page, sub); }
API void eng_set_engine_param(void* h, int func, int osc, float value)
{
    E(h).SetEngineParam(func, osc, value);
}
API float eng_get_osc_note(void* h, int osc) { return E(h).GetOscNote(osc); }
API const char* eng_get_value_text(void* h, int pot) { return E(h).GetValueText(pot); }
API float eng_get_slot_value(void* h, int pot) { return E(h).GetSlotValue(pot); }
API const char* eng_get_slot_name(void* h, int pot) { return E(h).GetSlotName(pot); }
API int eng_get_slot_osc(void* h, int pot) { return E(h).GetSlotOsc(pot); }
API const char* eng_get_chord_name(void* h) { return E(h).GetChordName(); }
API const char* eng_get_chord_numeral(void* h) { return E(h).GetChordNumeral(); }
API void eng_process(void* h, float* interleaved_stereo, int nframes)
{
    E(h).ProcessAudio(interleaved_stereo, nframes);
}
API void eng_render(void* h, uint16_t* fb240x240) { E(h).RenderScreen(fb240x240); }
API void eng_set_param(void* h, int func, int osc, float value) { E(h).SetParam(func, osc, value); }
API float eng_get_param(void* h, int func, int osc) { return E(h).GetParam(func, osc); }
API int eng_reverb_ok(void* h) { return E(h).ReverbOk() ? 1 : 0; }
API void eng_set_host_clock(void* h, int playing, double bpm, double beat)
{
    E(h).SetHostClock(playing != 0, bpm, beat);
}
API double eng_get_beat(void* h) { return E(h).GetBeat(); }

/* ------------------------------------- the simulator API, default instance */
API void synth_init(float samplerate) { eng_init(Default(), samplerate); }
API void synth_set_pot(int pot, float value) { eng_set_pot(Default(), pot, value); }
API float synth_get_pot(int pot) { return eng_get_pot(Default(), pot); }
API int synth_get_page() { return eng_get_page(Default()); }
API const char* synth_get_page_name(int page) { return eng_get_page_name(Default(), page); }
API int synth_get_num_subs(int page) { return eng_get_num_subs(Default(), page); }
API int synth_get_sub(int page) { return eng_get_sub(Default(), page); }
API const char* synth_get_sub_name(int page, int sub)
{
    return eng_get_sub_name(Default(), page, sub);
}
API void synth_set_engine_param(int func, int osc, float value)
{
    eng_set_engine_param(Default(), func, osc, value);
}
API float synth_get_osc_note(int osc) { return eng_get_osc_note(Default(), osc); }
API const char* synth_get_value_text(int pot) { return eng_get_value_text(Default(), pot); }
API float synth_get_slot_value(int pot) { return eng_get_slot_value(Default(), pot); }
API const char* synth_get_slot_name(int pot) { return eng_get_slot_name(Default(), pot); }
API int synth_get_slot_osc(int pot) { return eng_get_slot_osc(Default(), pot); }
API const char* synth_get_chord_name() { return eng_get_chord_name(Default()); }
API const char* synth_get_chord_numeral() { return eng_get_chord_numeral(Default()); }
API void synth_process(float* interleaved_stereo, int nframes)
{
    eng_process(Default(), interleaved_stereo, nframes);
}
API void synth_render(uint16_t* fb240x240) { eng_render(Default(), fb240x240); }
