/** C API for the PC simulator DLL. Not compiled into the firmware. */
#include "synth_core.h"

#if defined(_WIN32)
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C"
#endif

// On PC the big DSP buffers are plain statics inside the DLL.
static synth::DelayLineT delayL[synth::kOscs];
static synth::DelayLineT delayR[synth::kOscs];
static daisysp::ReverbSc reverb;
static synth::PreLineT   preL, preR;

API void synth_init(float samplerate)
{
    synth::Buffers buffers;
    for(int o = 0; o < synth::kOscs; o++)
    {
        buffers.delayL[o] = &delayL[o];
        buffers.delayR[o] = &delayR[o];
    }
    buffers.reverb = &reverb;
    buffers.preL   = &preL;
    buffers.preR   = &preR;
    synth::Init(samplerate, buffers);
}

API void synth_set_pot(int pot, float value)
{
    synth::SetPot(pot, value);
}

API float synth_get_pot(int pot)
{
    return synth::GetPot(pot);
}

API int synth_get_page()
{
    return synth::GetPage();
}

API const char* synth_get_page_name(int page)
{
    return synth::GetPageName(page);
}

API int synth_get_num_subs(int page)
{
    return synth::GetNumSubs(page);
}

API int synth_get_sub(int page)
{
    return synth::GetSub(page);
}

API const char* synth_get_sub_name(int page, int sub)
{
    return synth::GetSubName(page, sub);
}

API void synth_set_engine_param(int func, int osc, float value)
{
    synth::SetEngineParam(func, osc, value);
}

API float synth_get_osc_note(int osc)
{
    return synth::GetOscNote(osc);
}

API const char* synth_get_value_text(int pot)
{
    return synth::GetValueText(pot);
}

API float synth_get_slot_value(int pot)
{
    return synth::GetSlotValue(pot);
}

API const char* synth_get_slot_name(int pot)
{
    return synth::GetSlotName(pot);
}

API int synth_get_slot_osc(int pot)
{
    return synth::GetSlotOsc(pot);
}

API const char* synth_get_chord_name()
{
    return synth::GetChordName();
}

API const char* synth_get_chord_numeral()
{
    return synth::GetChordNumeral();
}

API void synth_process(float* interleaved_stereo, int nframes)
{
    synth::ProcessAudio(interleaved_stereo, nframes);
}

API void synth_render(uint16_t* fb240x240)
{
    synth::RenderScreen(fb240x240);
}
