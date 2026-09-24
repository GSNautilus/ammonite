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
 */
#include <stdint.h>
#include "daisysp.h"

namespace synth
{
constexpr int kNumPots   = 12; // physical pots, panel order: 0-2 left top,
                               // 3-5 left bottom, 6-8 right top, 9-11 right bottom
constexpr int kSubPot    = 9;  // knob 10: shared function or sub-page selector
constexpr int kPagePot   = 10; // PAGE: the page selector (knob 11)
constexpr int kVolumePot = 11; // always VOLUME (knob 12)
constexpr int kNumPages  = 9;  // MAIN OSC ARP ENVELOPE FILTER LFO DELAY MIX KEY
constexpr int kOscs      = 3;

/** Large DSP state the shell allocates (SDRAM on the Seed) and lends to the
 *  core. Each oscillator owns a stereo delay; a line holds 6 s at 48 kHz (a
 *  whole bar at 40 BPM plus the wobble margin), 1.15 MB each. */
constexpr int kDelayMaxSamples = 288400;
using DelayLineT = daisysp::DelayLine<float, kDelayMaxSamples>;
/** Reverb pre-delay line: 341 ms at 48 kHz (PREDELAY tops out at 250 ms). */
constexpr int kPreLineSamples = 16384;
using PreLineT = daisysp::DelayLine<float, kPreLineSamples>;

struct Buffers
{
    DelayLineT*        delayL[kOscs];
    DelayLineT*        delayR[kOscs];
    daisysp::ReverbSc* reverb;
    PreLineT*          preL;
    PreLineT*          preR;
};

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
/** The readout's value text for this pot. Static buffer, UI thread only. */
const char* GetValueText(int pot);
/** Test hook (PC only): write an engine function directly, bypassing the
 *  panel and the 20 ms smoothing. func = index in enum Func
 *  (synth_core.cpp), osc 0..2 (0 for a global function). */
void SetEngineParam(int func, int osc, float value01);
/** Test hook: the pitch an oscillator is heading for, as a MIDI note
 *  (fractional with FINE): a drone's glide target, an arp's latest note. */
float GetOscNote(int osc);

/** The chord playing now, as the screen shows it: name ("Am", "Bdim",
 *  "Dm7") and roman numeral in the key ("vi", lowercase when minor).
 *  Static buffers, UI thread only. */
const char* GetChordName();
const char* GetChordNumeral();

/** Fill `nframes` of interleaved stereo float audio. */
void ProcessAudio(float* out, int nframes);

/** Draw the current UI into a 240x240 RGB565 (panel byte order) buffer. */
void RenderScreen(uint16_t* fb);

/** Diagnostic view: the raw 0-100 reading of every pot, pot number beside
 *  each. For bring-up of the pots (no meter needed). */
void RenderDiagnostics(uint16_t* fb);
} // namespace synth
