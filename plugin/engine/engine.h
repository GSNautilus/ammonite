#pragma once
/**
 * The synth's portable brain: parameters, audio, and screen rendering.
 * No hardware anywhere in here - the Daisy firmware and the PC simulator
 * are both thin shells around this one interface.
 *
 * Panel model (arp synth, ENGINE_PLAN.md "Panel"): 12 pots. Pot 10 (PAGE,
 * knob 11) picks one of kNumPages pages; pot 11 is always VOLUME. On a
 * COLUMN page the three columns are the three oscillators: pots 0/3/6 drive
 * osc 1, 1/4/7 osc 2, 2/5/8 osc 3, one function per row. Pot 9 (knob 10) is
 * either one shared function or, on ARP / ENVELOPE / LFO / DELAY, the
 * SUB-PAGE selector. Every (page, sub-page, pot) slot keeps its own value,
 * and a pot only writes its slot after it has been moved since the page or
 * sub-page was chosen (pickup), so navigating never changes the sound.
 *
 * Build with -DUSE_DAISYSP_LGPL (ReverbSc lives in DaisySP-LGPL).
 *
 * PLUGIN COPY of core/synth_core.h (v1.0), made multi-instance: the free
 * functions became the methods of Engine. The firmware never uses it.
 */
#include <stdint.h>
#include "daisysp.h"
#include "reverbsc.h"

namespace ammonite
{
constexpr int kNumPots   = 12; // physical pots, panel order: 0-2 left top,
                               // 3-5 left bottom, 6-8 right top, 9-11 right bottom
constexpr int kSubPot    = 9;  // knob 10: shared function or sub-page selector
constexpr int kPagePot   = 10; // PAGE: the page selector (knob 11)
constexpr int kVolumePot = 11; // always VOLUME (knob 12)
constexpr int kNumPages  = 9;  // MAIN OSC ARP ENVELOPE FILTER LFO DELAY MIX KEY
constexpr int kOscs      = 3;

/** Highest sample rate the buffers hold (the hardware runs at 48 kHz). */
constexpr int kMaxSampleRate = 192000;
/** Large DSP state the shell allocates (SDRAM on the Seed) and lends to the
 *  core. Each oscillator owns a stereo delay. The longest delay time stays
 *  the hardware's: kDelayMaxSamples48 at 48 kHz (6 s, a whole bar at 40 BPM
 *  plus the wobble margin); the plugin's lines hold it at kMaxSampleRate,
 *  4.6 MB each. */
constexpr int kDelayMaxSamples48 = 288400;
constexpr int kDelayMaxSamples   = kDelayMaxSamples48 * (kMaxSampleRate / 48000);
using DelayLineT = daisysp::DelayLine<float, kDelayMaxSamples>;
/** Reverb pre-delay line: 341 ms at 192 kHz (PREDELAY tops out at 250 ms). */
constexpr int kPreLineSamples = 16384 * (kMaxSampleRate / 48000);
using PreLineT = daisysp::DelayLine<float, kPreLineSamples>;

struct Buffers
{
    DelayLineT*        delayL[kOscs];
    DelayLineT*        delayR[kOscs];
    ReverbSc*          reverb;
    PreLineT*          preL;
    PreLineT*          preR;
};

/** One Ammonite engine. A host may run any number of them, each on its own
 *  threads: all state is per instance. Threading as on the hardware: the UI
 *  side calls SetPot / the getters / RenderScreen, the audio side calls
 *  ProcessAudio; they meet only through atomics. */
class Engine
{
  public:
    Engine();
    ~Engine();
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void Init(float samplerate, const Buffers& buffers);

    /** A pot reading, 0..1, from the shell's UI loop. The first reading of a
     *  pot after Init is adopted as-is (the panel defines the boot state). */
    void  SetPot(int pot, float value01);
    /** Last reading the core received for this pot (before any reading: the
     *  position the pot "should" be in, so a simulator can draw it). */
    float GetPot(int pot);
    /** Active page, 0..kNumPages-1. */
    int   GetPage();
    /** Name of a page, e.g. "OSC". */
    const char* GetPageName(int page);
    /** Number of sub-pages of a page (1 = none, knob 10 is a function). */
    int   GetNumSubs(int page);
    /** The page's chosen sub-page (0 when it has none). */
    int   GetSub(int page);
    /** Name of a sub-page, e.g. "RHYTHM" ("" when the page has none). */
    const char* GetSubName(int page, int sub);
    /** Stored value of the slot this pot controls on the active page and
     *  sub-page (pot 10: the page reading, pot 9 on a sub-page page: the
     *  selector reading, pot 11: VOLUME). */
    float GetSlotValue(int pot);
    /** Name of what this pot does right now: a function name, "PAGE",
     *  "VOLUME", or the sub-page selector's name. */
    const char* GetSlotName(int pot);
    /** Which oscillator this pot's function belongs to: 1..3, or 0 for a
     *  shared / global function or a selector. */
    int   GetSlotOsc(int pot);
    /** The readout's value text for this pot. Per-instance buffer, UI thread only. */
    const char* GetValueText(int pot);
    /** Test hook (PC only): write an engine function directly, bypassing the
     *  panel and the 20 ms smoothing. func = index in enum Func
     *  (engine.cpp), osc 0..2 (0 for a global function). */
    void SetEngineParam(int func, int osc, float value01);
    /** Test hook: the pitch an oscillator is heading for, as a MIDI note
     *  (fractional with FINE): a drone's glide target, an arp's latest note. */
    float GetOscNote(int osc);

    /** The chord playing now, as the screen shows it: name ("Am", "Bdim",
     *  "Dm7") and roman numeral in the key ("vi", lowercase when minor).
     *  Per-instance buffers, UI thread only. */
    const char* GetChordName();
    const char* GetChordNumeral();

    /** Fill `nframes` of interleaved stereo float audio. */
    void ProcessAudio(float* out, int nframes);

    /** Draw the current UI into a 240x240 RGB565 (panel byte order) buffer. */
    void RenderScreen(uint16_t* fb);

    /** Diagnostic view: the raw 0-100 reading of every pot, pot number beside
     *  each. For bring-up of the pots (no meter needed). */
    void RenderDiagnostics(uint16_t* fb);

    /* --------------------------------------------- for a host (the plugin) */
    /** The engine functions (enum Func in engine.cpp). New functions are
     *  only ever added at the END of enum Func, so (func, osc) stays a
     *  stable ID for host parameters and saved state. */
    static int         NumFuncs();
    static const char* FuncName(int func);   // "ATTACK" (not unique: see FuncPlace)
    static bool        FuncPerOsc(int func); // three values, one per osc (else osc 0)
    static int         FuncSteps(int func);  // 0 = continuous, else stepped positions
    static float       FuncDefault(int func, int osc);
    /** Where a function sits on the panel: page and sub-page (0 when the page
     *  has none); page -1 for VOLUME, which is knob 12 on every page. */
    static void        FuncPlace(int func, int* page, int* sub);
    /** Text of step `step` of a stepped function ("UP", "+1", "1/8"), into buf. */
    static const char* FuncStepText(int func, int step, char* buf, int size);
    /** A parameter change from the host (automation): like a pot, the audio
     *  thread smooths it over 20 ms. Any thread. */
    void  SetParam(int func, int osc, float value01);
    float GetParam(int func, int osc);
    /** False when the last Init could not fit the reverb (above 192 kHz):
     *  the engine then runs without it. */
    bool  ReverbOk();
    /** The host's clock for the NEXT ProcessAudio call (audio thread, just
     *  before it): transport playing, tempo, and the position at the call's
     *  first sample in quarter notes since the song start. With SYNC DAW the
     *  engine follows it; with FREE, or without a call, it runs on TEMPO. */
    void  SetHostClock(bool playing, double bpm, double beat);
    /** The master clock after the last ProcessAudio, in quarter notes. */
    double GetBeat();

    /* ------------------------------ for a mouse panel (the plugin's UI thread) */
    /** Show this page and section. No pots, no pickup: a mouse knob always
     *  edits the stored value (the host parameter) directly. */
    void SetPanelPage(int page, int sub);
    /** A knob was touched: its readout appears on the screen, as when a pot
     *  moves on the hardware (the page map for 10 = PAGE, the section map for
     *  9 on a page with sections). */
    void ShowPot(int pot);
    /** The function a knob drives on the current page and section; false for
     *  PAGE and the section selector. VOLUME for knob 12. */
    bool GetSlotFunc(int pot, int* func, int* osc);

  private:
    struct Impl;
    Impl* impl_;
};
} // namespace ammonite
