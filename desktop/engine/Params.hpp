/** The parameter table of Ammonite Desktop: every parameter, with a stable
 *  id that is never renumbered.
 *
 *  Ids 0..63 are global; slot s (0..15) owns 64 + 128 s .. 64 + 128 s + 127;
 *  the modulation pool owns 2112 .. 3135: 32 LFOs, the macros, 128 routes. Unused ids inside a block are
 *  reserved: hidden parameters today, room for tomorrow. Adding a parameter
 *  means taking the next free index of its block, never moving an existing
 *  one. Retired ids (version 2's per-channel and global LFOs and routes) stay
 *  in the table, hidden, so a version 2 patch can still be read and
 *  converted.
 *
 *  Ids from kPageIdBase (4096) up are the page's own parameters (the stage's
 *  look, its camera, its effects, the scope): never in this table, but a
 *  route may name one as its target. The engine leaves such a route be (it
 *  is not IsModTarget) and the page applies it to its own parameter.
 *
 *  A host sees every parameter but the hidden ones and the routes (pfNoHost:
 *  saved with the patch, never a host parameter; 128 x 4 would swamp a host's
 *  list). The host index is not the id.
 *
 *  Values are in natural units where that reads well (BPM, cents, pan -1..1,
 *  integers for steps) and 0..1 otherwise; the curves behind the 0..1 ones
 *  are in Dsp.hpp (times, cutoff). */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ammo {

constexpr int kSlots      = 16;
constexpr int kGlobalIds  = 64;
constexpr int kSlotStride = 128;
constexpr int kPoolBase   = kGlobalIds + kSlots * kSlotStride; // 2112: the modulation pool
constexpr int kPoolIds    = 1024;
constexpr int kNumIds     = kPoolBase + kPoolIds; // 3136
constexpr int kPageIdBase = 4096, kPageIds = 1024;  // the page's parameters: a route's target only

// the pool's caps (fixed at compile time: the audio thread allocates nothing, a host sees a fixed list)
constexpr int kPoolLfos = 32, kPoolMacros = 16, kPoolRoutes = 128;
// LFO l (0..31): kLfoBase + kLfoStride l + LfoParam; its eighth id is reserved
constexpr int kLfoBase = kPoolBase, kLfoStride = 8;
enum LfoParam
{
    lOn = 0, // in use: the page lists it (an LFO also runs while a live route takes it)
    lShape,
    lSync,
    lRate,
    lDiv,
    lPhase,
    lRestart, // 0 Free, n: the notes of channel n
    kLfoParamsUsed
};
// macro m (0..15): in use at kMacroOnBase + m; the value at kMacroValBase + m for m >= 4 (macros 1..4 are
// the global ids gMacro1..4, the host's automation path since version 2; kMacroValBase + 0..3 are reserved)
constexpr int kMacroOnBase = kPoolBase + kPoolLfos * kLfoStride; // 2368
constexpr int kMacroValBase = kMacroOnBase + kPoolMacros;       // 2384
// route r (0..127): kRouteBase + kRouteStride r + RouteParam; 2912 .. 3135 reserved
constexpr int kRouteBase = kMacroValBase + kPoolMacros, kRouteStride = 4; // 2400
enum RouteParam
{
    rSrc = 0, // ModSource
    rTarget,  // a parameter id (IsModTarget), or a page parameter's (kPageIdBase ..), -1: none
    rAmt,     // -1..1 of the target's whole range
    rPol,     // 0 Unipolar, 1 Bipolar (the default)
};
constexpr int kPoolEnd = kRouteBase + kPoolRoutes * kRouteStride; // 2912
/** A route's source: off, a pool LFO, a macro. */
enum ModSource
{
    kSrcOff    = 0,
    kSrcLfo1   = 1, // .. kSrcLfo1 + 31
    kSrcMacro1 = kSrcLfo1 + kPoolLfos, // 33 .. 48
    kNumModSources = kSrcMacro1 + kPoolMacros
};
inline int LfoId(int l, int k) { return kLfoBase + kLfoStride * l + k; }
inline int RouteId(int r, int k) { return kRouteBase + kRouteStride * r + k; }
inline int MacroOnId(int m) { return kMacroOnBase + m; }

enum GlobalParam
{
    gTempo = 0,
    gSync,
    gSwing,
    gRoot,
    gScale,
    gProg,
    gChange,
    gChord,
    gRestart,
    gOctave,
    gVolume,
    gMacro1,
    gMacro2,
    gMacro3,
    gMacro4,
    // phase 4, package B: the shared reverb
    gRevSize,
    gRevDecay,
    gRevDamping,
    gRevPreDelay,
    gRevWidth,
    gRevReturn,
    // phase 4, package B: the master bus
    gGlue,
    gCeiling,
    // the custom progression (Progression = Custom): its length and eight degrees
    gProgLen,
    gProgStep1, // .. gProgStep1 + 7
    // retired (hidden, read only to convert a version 2 patch): its two global LFOs, kGlobalLfoParams
    // each: shape, sync, rate, division, phase
    gLfo1Shape = gProgStep1 + 8,
    gLfo1Sync,
    gLfo1Rate,
    gLfo1Div,
    gLfo1Phase,
    gLfo2Shape, // .. gLfo2Shape + 4
    kNumGlobalParams = gLfo2Shape + 5
};
constexpr int kGlobalLfoParams = 5; // gLfo1Shape + kGlobalLfoParams * l + (shape, sync, rate, div, phase)

enum SlotParam
{
    cGen = 0,
    cInst,
    cLevel,
    cPan,
    cMute,
    cShow,
    cDegree,
    cOctave,
    cArpMode,
    cArpPool,
    cArpRange,
    cArpDiv,
    cArpLength,
    cArpGate,
    cArpDensity,
    cArpVary,
    cArpRatchet,
    cShape,
    cDetune,
    cSub,
    cNoise,
    cCutoff,
    cReso,
    cFilterType,
    cDrive,
    cAmpAttack,
    cAmpDecay,
    cAmpSustain,
    cAmpRelease,
    cFiltEnvAmount,
    cFiltAttack,
    cFiltDecay,
    cPitchEnvAmount,
    cPitchDecay,
    cGlide,
    // phase 4: the chord and bass generators
    cChordNotes,
    cChordVoicing,
    cChordLead,
    cChordRhythm,
    cChordStrum,
    cBassPattern,
    // phase 4: the rhythm generator and the drum voice
    cRhythmRotate,
    cRhythmChance,
    cRhythmPitch,
    cDrumModel,
    cDrumTune,
    cDrumDecay,
    cDrumTone,
    cDrumSnap,
    // phase 4, package B: the channel's inserts
    cFxDrive,
    cFxTone,
    cChorusOn,
    cChorusRate,
    cChorusDepth,
    cChorusMix,
    cDelayDiv,
    cDelayFeedback,
    cDelayPingPong,
    cDelayDamping,
    cDelayMix,
    // phase 4, package B: the sends
    cRevSend,
    cDuckSource,
    cDuckAmount,
    // phase 4, sound after the first listen
    cChorusMode,
    cChordLetRing,
    // modulation: a fine tune (the pitch an LFO can move: vibrato)
    cTune,
    // retired (hidden, read only to convert a version 2 patch): its two LFOs per channel and four routes
    cLfo1Shape, // kLfoParams each: shape, sync, rate, division, phase, retrigger
    cLfo1Sync,
    cLfo1Rate,
    cLfo1Div,
    cLfo1Phase,
    cLfo1Retrig,
    cLfo2Shape, // .. cLfo2Shape + 5
    cMod1Src = cLfo2Shape + 6, // kRouteParams each: source, destination, amount
    cMod1Dest,
    cMod1Amt,
    // which notes a pattern plays (ROTATE is cRhythmRotate, every generator's now)
    cNoteShift = cMod1Src + 4 * 3,
    cNoteCount,
    cSeed,
    kNumSlotParams
};
constexpr int kFirstRetiredSlotParam = cLfo1Shape, kEndRetiredSlotParam = cNoteShift; // 66 .. 89
constexpr int kSlotLfos = 2, kLfoParams = 6, kRoutes = 4, kRouteParams = 3;

enum ParamFlags : uint32_t
{
    pfAutomatable = 1,
    pfInteger     = 2, // steps or integers: shown as names or whole numbers
    pfHidden      = 4, // a reserved id: not shown, not saved
    pfBoolean     = 8,
    pfNoHost      = 16, // saved with the patch, not a host parameter (the routes)
};

struct ParamDef
{
    const char*        name;   // "Arp Mode"
    const char*        symbol; // "arp_mode" ([a-z0-9_])
    float              min, max, def;
    int                steps;  // > 0: a stepped parameter with that many values (min..max integers)
    const char* const* names;  // step names, or null for plain integers
    uint32_t           flags;
};

// ---- step names (the hardware's lists, kept word for word where they exist)
inline const char* const kSyncNames[]    = {"Host", "Internal"};
inline const char* const kNoteNames[]    = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline const char* const kScaleNames[]   = {"Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Harmonic minor", "Pentatonic", "Pentatonic minor", "Blues",
                                            "Maj", "Min", "Sus2", "Sus4", "Maj7", "Min7", "Dom7", "Dim", "Aug", "Add9"};
inline const char* const kProgNames[]    = {"Off", "Pop I V vi IV", "50s I vi IV V", "Sad vi IV I V", "Epic i VI III VII", "Jazz ii V I I", "Vamp I IV", "Canon", "Fall i VII VI v", "Custom"};
inline const char* const kChangeNames[]  = {"1/2 bar", "1 bar", "2 bars", "4 bars"};
inline const char* const kRomanNames[]   = {"I", "II", "III", "IV", "V", "VI", "VII"};
inline const char* const kRestartNames[] = {"Free", "Bar", "Chord"};
inline const char* const kGenNames[]     = {"Off", "Arp", "Chord", "Bass", "Rhythm"};
inline const char* const kInstNames[]    = {"Synth", "Drum"};
inline const char* const kArpModeNames[] = {"Off", "Loop", "Up", "Down", "Up-down", "Random"};
inline const char* const kArpPoolNames[] = {"Key", "Triad", "7th", "Fifths", "Root"};
inline const char* const kArpDivNames[]  = {"4/1",  "2/1", "1/1", "1/2.", "1/2",  "1/4.",  "1/4",
                                            "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
inline const char* const kOffOnNames[]   = {"Off", "On"};
inline const char* const kChordNotesNames[]   = {"Triad", "7th", "9th"};
inline const char* const kChordVoicingNames[] = {"Close", "Open", "Drop 2", "Spread"};
inline const char* const kChordLeadNames[]    = {"Fixed", "Smooth"};
inline const char* const kChordRhythmNames[]  = {"Hold", "Steps", "Arp"};
inline const char* const kBassPatternNames[]  = {"Root", "Root-5th", "Octaves", "Walk"};
inline const char* const kRhythmPitchNames[]  = {"Fixed", "Root", "Fifth"};
inline const char* const kDrumModelNames[]    = {"Kick", "Snare", "Hat", "Open hat", "Clap", "Tom", "Rim", "Noise"};
inline const char* const kDelayDivNames[]     = {"1/32", "1/16", "1/16T", "1/8", "1/8T", "1/8.", "1/4", "1/4T", "1/4.", "1/2", "1/1"};
inline const char* const kChorusModeNames[]   = {"Chorus", "Ensemble", "Vibrato"};
inline const char* const kNoteCountNames[]   = {"Auto", "Steps", "Hits"};
inline const char* const kSeedNames[]       = {"Free", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                             "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28",
                                             "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42",
                                             "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56",
                                             "57", "58", "59", "60", "61", "62", "63", "64"};
inline const char* const kDuckSourceNames[]   = {"Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
// modulation (Mod.hpp)
inline const char* const kLfoShapeNames[]  = {"Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Drift"};
inline const char* const kLfoSyncNames[]   = {"Free", "Tempo"};
inline const char* const kLfoDivNames[]    = {"16/1", "8/1", "4/1", "2/1", "1/1", "1/2.", "1/2", "1/4.",
                                              "1/4",  "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
inline const char* const kLfoRetrigNames[] = {"Free", "Note"}; // retired with version 2's LFOs
inline const char* const kModSrcNames[]    = {"Off", "LFO 1", "LFO 2", "Global LFO 1", "Global LFO 2", "Macro 1", "Macro 2", "Macro 3", "Macro 4"}; // retired
inline const char* const kLfoRestartNames[] = {"Free", "Notes 1", "Notes 2", "Notes 3", "Notes 4", "Notes 5", "Notes 6", "Notes 7", "Notes 8",
                                               "Notes 9", "Notes 10", "Notes 11", "Notes 12", "Notes 13", "Notes 14", "Notes 15", "Notes 16"};
inline const char* const kPolarityNames[]   = {"Unipolar", "Bipolar"};
/** The continuous parameters of a channel a route can move, by step (0: none). Version 2's routes saved the step,
 *  so a version 2 patch reads it from here: append only, never reorder. The names are the parameters' own. */
inline const int kModDests[] = {-1,
                                cLevel, cPan, cTune,
                                cShape, cDetune, cSub, cNoise, cDrive, cGlide,
                                cCutoff, cReso, cFilterType, cFiltEnvAmount, cFiltAttack, cFiltDecay,
                                cAmpAttack, cAmpDecay, cAmpSustain, cAmpRelease, cPitchEnvAmount, cPitchDecay,
                                cArpGate, cArpDensity, cArpVary, cArpRatchet, cChordStrum, cRhythmChance,
                                cDrumTune, cDrumDecay, cDrumTone, cDrumSnap,
                                cFxDrive, cFxTone, cChorusRate, cChorusDepth, cChorusMix,
                                cDelayFeedback, cDelayDamping, cDelayMix, cRevSend, cDuckAmount};
constexpr int            kNumModDests = sizeof(kModDests) / sizeof(kModDests[0]);
/** The step of slot parameter `p` in kModDests (0 if a route cannot move it). */
inline int ModDestStep(int p)
{
    for(int i = 1; i < kNumModDests; i++)
        if(kModDests[i] == p)
            return i;
    return 0;
}
inline const char* const kModDestNames[kNumModDests] = {"None",
                                                        "Level", "Pan", "Fine tune",
                                                        "Shape", "Detune", "Sub", "Noise", "Drive", "Glide",
                                                        "Cutoff", "Resonance", "Filter type", "Filter env amount", "Filter attack", "Filter decay",
                                                        "Amp attack", "Amp decay", "Amp sustain", "Amp release", "Pitch env amount", "Pitch decay",
                                                        "Arp gate", "Arp density", "Arp vary", "Arp ratchet", "Strum", "Chance",
                                                        "Drum tune", "Drum decay", "Drum tone", "Drum snap",
                                                        "FX drive", "FX drive tone", "Chorus rate", "Chorus depth", "Chorus mix",
                                                        "Delay feedback", "Delay damping", "Delay mix", "Reverb send", "Duck amount"};

constexpr uint32_t kA  = pfAutomatable;
constexpr uint32_t kAI = pfAutomatable | pfInteger;
constexpr uint32_t kRetired = pfHidden; // version 2's modulation ids: their defaults still read a version 2 patch

inline const ParamDef kGlobalDefs[kNumGlobalParams] = {
    {"Tempo", "tempo", 20.f, 240.f, 110.f, 0, nullptr, kA},
    {"Sync", "sync", 0.f, 1.f, 1.f, 2, kSyncNames, kAI}, // Internal: the Tempo knob, whatever the host plays
    {"Swing", "swing", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Root", "root", 0.f, 11.f, 9.f, 12, kNoteNames, kAI},
    {"Scale", "scale", 0.f, 19.f, 1.f, 20, kScaleNames, kAI},
    {"Progression", "prog", 0.f, 9.f, 0.f, 10, kProgNames, kAI},
    {"Chord change", "change", 0.f, 3.f, 1.f, 4, kChangeNames, kAI},
    {"Chord", "chord", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    {"Restart", "restart", 0.f, 2.f, 0.f, 3, kRestartNames, kAI},
    {"Master octave", "octave", -2.f, 2.f, 0.f, 5, nullptr, kAI},
    {"Volume", "volume", 0.f, 1.f, 0.8f, 0, nullptr, kA},
    {"Macro 1", "macro1", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Macro 2", "macro2", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Macro 3", "macro3", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Macro 4", "macro4", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Reverb size", "rev_size", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Reverb decay", "rev_decay", 0.f, 1.f, 0.45f, 0, nullptr, kA},
    {"Reverb damping", "rev_damp", 0.f, 1.f, 0.4f, 0, nullptr, kA},
    {"Reverb pre-delay", "rev_pre", 0.f, 1.f, 0.1f, 0, nullptr, kA},
    {"Reverb width", "rev_width", 0.f, 1.f, 1.f, 0, nullptr, kA},
    {"Reverb return", "rev_return", 0.f, 1.f, 0.6f, 0, nullptr, kA},
    {"Glue", "glue", 0.f, 1.f, 0.3f, 0, nullptr, kA},           // the master compressor: 0 off
    {"Ceiling", "ceiling", -12.f, 0.f, -0.3f, 0, nullptr, kA}, // the limiter, dBFS
    {"Progression length", "prog_len", 1.f, 8.f, 4.f, 8, nullptr, kAI}, // the custom one: I IV V I until edited
    {"Progression step 1", "prog_s1", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    {"Progression step 2", "prog_s2", 0.f, 6.f, 3.f, 7, kRomanNames, kAI},
    {"Progression step 3", "prog_s3", 0.f, 6.f, 4.f, 7, kRomanNames, kAI},
    {"Progression step 4", "prog_s4", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    {"Progression step 5", "prog_s5", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    {"Progression step 6", "prog_s6", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    {"Progression step 7", "prog_s7", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    {"Progression step 8", "prog_s8", 0.f, 6.f, 0.f, 7, kRomanNames, kAI},
    // retired: version 2's global LFOs (a version 2 patch's become pool LFOs)
    {"Global LFO 1 shape", "glfo1_shape", 0.f, 6.f, 0.f, 7, kLfoShapeNames, kRetired},
    {"Global LFO 1 sync", "glfo1_sync", 0.f, 1.f, 1.f, 2, kLfoSyncNames, kRetired},
    {"Global LFO 1 rate", "glfo1_rate", 0.f, 1.f, 0.5f, 0, nullptr, kRetired},
    {"Global LFO 1 division", "glfo1_div", 0.f, 15.f, 2.f, 16, kLfoDivNames, kRetired}, // 4 bars
    {"Global LFO 1 phase", "glfo1_phase", 0.f, 1.f, 0.f, 0, nullptr, kRetired},
    {"Global LFO 2 shape", "glfo2_shape", 0.f, 6.f, 1.f, 7, kLfoShapeNames, kRetired},
    {"Global LFO 2 sync", "glfo2_sync", 0.f, 1.f, 1.f, 2, kLfoSyncNames, kRetired},
    {"Global LFO 2 rate", "glfo2_rate", 0.f, 1.f, 0.5f, 0, nullptr, kRetired},
    {"Global LFO 2 division", "glfo2_div", 0.f, 15.f, 0.f, 16, kLfoDivNames, kRetired}, // 16 bars
    {"Global LFO 2 phase", "glfo2_phase", 0.f, 1.f, 0.f, 0, nullptr, kRetired},
};

inline const ParamDef kSlotDefs[kNumSlotParams] = {
    {"Generator", "gen", 0.f, 4.f, 0.f, 5, kGenNames, kAI},
    {"Instrument", "inst", 0.f, 1.f, 0.f, 2, kInstNames, kAI},
    {"Level", "level", 0.f, 1.f, 0.7f, 0, nullptr, kA},
    {"Pan", "pan", -1.f, 1.f, 0.f, 0, nullptr, kA},
    {"Mute", "mute", 0.f, 1.f, 0.f, 2, kOffOnNames, kAI | pfBoolean},
    {"Show", "show", 0.f, 1.f, 1.f, 2, kOffOnNames, pfInteger | pfBoolean}, // the eye: saved, not automated
    {"Degree", "degree", 0.f, 7.f, 0.f, 8, nullptr, kAI},
    {"Octave", "oct", -4.f, 4.f, 0.f, 9, nullptr, kAI},
    {"Arp mode", "arp_mode", 0.f, 5.f, 2.f, 6, kArpModeNames, kAI},
    {"Arp pool", "arp_pool", 0.f, 4.f, 3.f, 5, kArpPoolNames, kAI},
    {"Arp range", "arp_range", 1.f, 3.f, 1.f, 3, nullptr, kAI},
    {"Arp division", "arp_div", 0.f, 13.f, 6.f, 14, kArpDivNames, kAI},
    {"Arp length", "arp_len", 1.f, 16.f, 8.f, 16, nullptr, kAI},
    {"Arp gate", "arp_gate", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Arp density", "arp_density", 0.f, 1.f, 1.f, 0, nullptr, kA},
    {"Arp vary", "arp_vary", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Arp ratchet", "arp_ratchet", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Shape", "shape", 0.f, 1.f, 0.3f, 0, nullptr, kA},
    {"Detune", "detune", 0.f, 50.f, 6.f, 0, nullptr, kA},
    {"Sub", "sub", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Noise", "noise", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Cutoff", "cutoff", 0.f, 1.f, 0.8f, 0, nullptr, kA},
    {"Resonance", "reso", 0.f, 1.f, 0.2f, 0, nullptr, kA},
    {"Filter type", "filter_type", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Drive", "drive", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Amp attack", "amp_a", 0.f, 1.f, 0.1f, 0, nullptr, kA},
    {"Amp decay", "amp_d", 0.f, 1.f, 0.45f, 0, nullptr, kA},
    {"Amp sustain", "amp_s", 0.f, 1.f, 0.3f, 0, nullptr, kA},
    {"Amp release", "amp_r", 0.f, 1.f, 0.45f, 0, nullptr, kA},
    {"Filter env amount", "filt_amt", -1.f, 1.f, 0.f, 0, nullptr, kA},
    {"Filter attack", "filt_a", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Filter decay", "filt_d", 0.f, 1.f, 0.4f, 0, nullptr, kA},
    {"Pitch env amount", "pitch_amt", -1.f, 1.f, 0.f, 0, nullptr, kA},
    {"Pitch decay", "pitch_d", 0.f, 1.f, 0.3f, 0, nullptr, kA},
    {"Glide", "glide", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Chord notes", "chord_notes", 0.f, 2.f, 0.f, 3, kChordNotesNames, kAI},
    {"Chord voicing", "chord_voicing", 0.f, 3.f, 0.f, 4, kChordVoicingNames, kAI},
    {"Voice leading", "chord_lead", 0.f, 1.f, 1.f, 2, kChordLeadNames, kAI},
    {"Chord rhythm", "chord_rhythm", 0.f, 2.f, 0.f, 3, kChordRhythmNames, kAI},
    {"Strum", "chord_strum", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Bass pattern", "bass_pattern", 0.f, 3.f, 0.f, 4, kBassPatternNames, kAI},
    {"Rotate", "rhy_rotate", 0.f, 15.f, 0.f, 16, nullptr, kAI}, // every generator's (the symbol is the Rhythm's of old)
    {"Chance", "rhy_chance", 0.f, 1.f, 1.f, 0, nullptr, kA},
    {"Rhythm pitch", "rhy_pitch", 0.f, 2.f, 0.f, 3, kRhythmPitchNames, kAI},
    {"Drum", "drum_model", 0.f, 7.f, 0.f, 8, kDrumModelNames, kAI},
    {"Drum tune", "drum_tune", -12.f, 12.f, 0.f, 0, nullptr, kA},
    {"Drum decay", "drum_decay", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Drum tone", "drum_tone", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Drum snap", "drum_snap", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"FX drive", "fx_drive", 0.f, 1.f, 0.f, 0, nullptr, kA}, // 0: off (the voice's own Drive is id 24)
    {"FX drive tone", "fx_tone", -1.f, 1.f, 0.f, 0, nullptr, kA},
    {"Chorus", "cho_on", 0.f, 1.f, 0.f, 2, kOffOnNames, kAI | pfBoolean},
    {"Chorus rate", "cho_rate", 0.f, 1.f, 0.4f, 0, nullptr, kA},
    {"Chorus depth", "cho_depth", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Chorus mix", "cho_mix", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Delay division", "dly_div", 0.f, 10.f, 3.f, 11, kDelayDivNames, kAI},
    {"Delay feedback", "dly_fb", 0.f, 0.95f, 0.4f, 0, nullptr, kA},
    {"Ping-pong", "dly_pp", 0.f, 1.f, 0.f, 2, kOffOnNames, kAI | pfBoolean},
    {"Delay damping", "dly_damp", 0.f, 1.f, 0.3f, 0, nullptr, kA},
    {"Delay mix", "dly_mix", 0.f, 1.f, 0.f, 0, nullptr, kA}, // 0: off
    {"Reverb send", "rev_send", 0.f, 1.f, 0.f, 0, nullptr, kA},
    {"Duck source", "duck_src", 0.f, 16.f, 0.f, 17, kDuckSourceNames, kAI}, // 0: off, else the slot (1..16)
    {"Duck amount", "duck_amt", 0.f, 1.f, 0.5f, 0, nullptr, kA},
    {"Chorus mode", "cho_mode", 0.f, 2.f, 0.f, 3, kChorusModeNames, kAI},          // with Chorus (on / off) as before
    {"Let ring", "chord_ring", 0.f, 1.f, 0.f, 2, kOffOnNames, kAI | pfBoolean}, // Chord's Arp: each note held to the change
    {"Fine tune", "tune", -100.f, 100.f, 0.f, 0, nullptr, kA}, // cents, every instrument
    {"LFO 1 shape", "lfo1_shape", 0.f, 6.f, 0.f, 7, kLfoShapeNames, kRetired},
    {"LFO 1 sync", "lfo1_sync", 0.f, 1.f, 1.f, 2, kLfoSyncNames, kRetired},
    {"LFO 1 rate", "lfo1_rate", 0.f, 1.f, 0.5f, 0, nullptr, kRetired}, // Free: 0.02 .. 20 Hz (LfoHz)
    {"LFO 1 division", "lfo1_div", 0.f, 15.f, 4.f, 16, kLfoDivNames, kRetired}, // Tempo: 1 bar
    {"LFO 1 phase", "lfo1_phase", 0.f, 1.f, 0.f, 0, nullptr, kRetired},
    {"LFO 1 retrigger", "lfo1_retrig", 0.f, 1.f, 0.f, 2, kLfoRetrigNames, kRetired},
    {"LFO 2 shape", "lfo2_shape", 0.f, 6.f, 1.f, 7, kLfoShapeNames, kRetired},
    {"LFO 2 sync", "lfo2_sync", 0.f, 1.f, 0.f, 2, kLfoSyncNames, kRetired},
    {"LFO 2 rate", "lfo2_rate", 0.f, 1.f, 0.6f, 0, nullptr, kRetired}, // 1.25 Hz
    {"LFO 2 division", "lfo2_div", 0.f, 15.f, 8.f, 16, kLfoDivNames, kRetired}, // 1/4
    {"LFO 2 phase", "lfo2_phase", 0.f, 1.f, 0.f, 0, nullptr, kRetired},
    {"LFO 2 retrigger", "lfo2_retrig", 0.f, 1.f, 0.f, 2, kLfoRetrigNames, kRetired},
    {"Mod 1 source", "mod1_src", 0.f, 8.f, 0.f, 9, kModSrcNames, kRetired},
    {"Mod 1 destination", "mod1_dest", 0.f, (float)(kNumModDests - 1), 0.f, kNumModDests, kModDestNames, kRetired},
    {"Mod 1 amount", "mod1_amt", -1.f, 1.f, 0.f, 0, nullptr, kRetired}, // of the destination's whole range
    {"Mod 2 source", "mod2_src", 0.f, 8.f, 0.f, 9, kModSrcNames, kRetired},
    {"Mod 2 destination", "mod2_dest", 0.f, (float)(kNumModDests - 1), 0.f, kNumModDests, kModDestNames, kRetired},
    {"Mod 2 amount", "mod2_amt", -1.f, 1.f, 0.f, 0, nullptr, kRetired},
    {"Mod 3 source", "mod3_src", 0.f, 8.f, 0.f, 9, kModSrcNames, kRetired},
    {"Mod 3 destination", "mod3_dest", 0.f, (float)(kNumModDests - 1), 0.f, kNumModDests, kModDestNames, kRetired},
    {"Mod 3 amount", "mod3_amt", -1.f, 1.f, 0.f, 0, nullptr, kRetired},
    {"Mod 4 source", "mod4_src", 0.f, 8.f, 0.f, 9, kModSrcNames, kRetired},
    {"Mod 4 destination", "mod4_dest", 0.f, (float)(kNumModDests - 1), 0.f, kNumModDests, kModDestNames, kRetired},
    {"Mod 4 amount", "mod4_amt", -1.f, 1.f, 0.f, 0, nullptr, kRetired},
    {"Shift", "note_shift", 0.f, 15.f, 0.f, 16, nullptr, kAI},           // the Arp's notes, n places up the pool (mod its size)
    {"Count", "note_count", 0.f, 2.f, 0.f, 3, kNoteCountNames, kAI},    // Auto: the arp Steps, Chord's Arp Hits
    {"Seed", "seed", 0.f, 64.f, 0.f, 65, kSeedNames, kAI},             // the dice: Free, or a pattern per seed
};

inline const ParamDef kHiddenDef = {"(reserved)", "reserved", 0.f, 1.f, 0.f, 0, nullptr, pfHidden};

/** The modulation pool's definitions (ids kPoolBase ..) with their numbered names, built once into fixed
 *  buffers (no allocation), and the route sources' names. */
struct PoolTable
{
    ParamDef    defs[kPoolIds];
    const char* srcNames[kNumModSources];
    char        names[kPoolIds][28], syms[kPoolIds][20], src[kNumModSources][10];

    PoolTable()
    {
        static const char* const kLfoName[kLfoParamsUsed] = {"in use", "shape", "sync", "rate", "division", "phase", "restart"};
        static const char* const kLfoSym[kLfoParamsUsed]  = {"on", "shape", "sync", "rate", "div", "phase", "restart"};
        static const char* const kRouteName[kRouteStride] = {"source", "target", "amount", "polarity"};
        static const char* const kRouteSym[kRouteStride]  = {"src", "target", "amt", "pol"};
        // the LFOs' settings are host parameters (automatable); in use is the page's (saved, not automated)
        static const ParamDef kLfo[kLfoParamsUsed] = {
            {"", "", 0.f, 1.f, 0.f, 2, kOffOnNames, pfInteger | pfBoolean},
            {"", "", 0.f, 6.f, 0.f, 7, kLfoShapeNames, kAI},
            {"", "", 0.f, 1.f, 1.f, 2, kLfoSyncNames, kAI},
            {"", "", 0.f, 1.f, 0.5f, 0, nullptr, kA},        // Free: 0.02 .. 20 Hz (LfoHz)
            {"", "", 0.f, 15.f, 4.f, 16, kLfoDivNames, kAI}, // Tempo: 1 bar
            {"", "", 0.f, 1.f, 0.f, 0, nullptr, kA},
            {"", "", 0.f, 16.f, 0.f, 17, kLfoRestartNames, kAI}};
        for(int k = 0; k < kPoolIds; k++)
            defs[k] = kHiddenDef;
        std::snprintf(src[0], sizeof src[0], "Off");
        for(int i = 1; i < kNumModSources; i++)
            std::snprintf(src[i], sizeof src[i], i < kSrcMacro1 ? "LFO %d" : "Macro %d", i < kSrcMacro1 ? i : i - kSrcMacro1 + 1);
        for(int i = 0; i < kNumModSources; i++)
            srcNames[i] = src[i];
        for(int l = 0; l < kPoolLfos; l++)
            for(int k = 0; k < kLfoParamsUsed; k++)
                Set(LfoId(l, k), kLfo[k], "LFO %d %s", "lfo%d_%s", l + 1, kLfoName[k], kLfoSym[k]);
        for(int m = 0; m < kPoolMacros; m++)
        {
            Set(MacroOnId(m), {"", "", 0.f, 1.f, 0.f, 2, kOffOnNames, pfInteger | pfBoolean}, "Macro %d %s", "macro%d_%s", m + 1, "in use", "on");
            if(m >= 4)
                Set(kMacroValBase + m, {"", "", 0.f, 1.f, 0.f, 0, nullptr, kA}, "Macro %d%s", "macro%d%s", m + 1, "", "");
        }
        const ParamDef kRoute[kRouteStride] = {
            {"", "", 0.f, (float)(kNumModSources - 1), 0.f, kNumModSources, srcNames, pfInteger | pfNoHost},
            {"", "", -1.f, (float)(kPageIdBase + kPageIds - 1), -1.f, kPageIdBase + kPageIds + 1, nullptr, pfInteger | pfNoHost},
            {"", "", -1.f, 1.f, 0.f, 0, nullptr, pfNoHost}, // of the target's whole range
            {"", "", 0.f, 1.f, 1.f, 2, kPolarityNames, pfInteger | pfNoHost}};
        for(int r = 0; r < kPoolRoutes; r++)
            for(int k = 0; k < kRouteStride; k++)
                Set(RouteId(r, k), kRoute[k], "Route %d %s", "route%d_%s", r + 1, kRouteName[k], kRouteSym[k]);
    }

  private:
    void Set(int id, const ParamDef& d, const char* nameFmt, const char* symFmt, int n, const char* name, const char* sym)
    {
        const int k = id - kPoolBase;
        std::snprintf(names[k], sizeof names[k], nameFmt, n, name);
        std::snprintf(syms[k], sizeof syms[k], symFmt, n, sym);
        defs[k]        = d;
        defs[k].name   = names[k];
        defs[k].symbol = syms[k];
    }
};
inline const PoolTable& Pool()
{
    static const PoolTable t;
    return t;
}

inline int  SlotId(int slot, int p) { return kGlobalIds + slot * kSlotStride + p; }
inline bool IsSlotId(int id, int& slot, int& p)
{
    if(id < kGlobalIds || id >= kPoolBase)
        return false;
    slot = (id - kGlobalIds) / kSlotStride;
    p    = (id - kGlobalIds) % kSlotStride;
    return true;
}
inline bool IsGlobalId(int id) { return id >= 0 && id < kGlobalIds; }
inline bool IsPoolId(int id) { return id >= kPoolBase && id < kNumIds; }
inline bool IsRouteId(int id) { return id >= kRouteBase && id < kPoolEnd; }
/** The value id of macro m (0..15). */
inline int MacroId(int m) { return m < 4 ? gMacro1 + m : kMacroValBase + m; }

/** The definition behind an id; kHiddenDef for reserved ids. */
inline const ParamDef& ParamInfo(int id)
{
    int slot, p;
    if(IsGlobalId(id))
        return id < kNumGlobalParams ? kGlobalDefs[id] : kHiddenDef;
    if(IsSlotId(id, slot, p))
        return p < kNumSlotParams ? kSlotDefs[p] : kHiddenDef;
    if(IsPoolId(id))
        return Pool().defs[id - kPoolBase];
    return kHiddenDef;
}

/** The global parameters a route can move besides the channels' (kModDests): continuous, not the tempo. */
inline const int kModGlobalTargets[] = {gSwing, gVolume, gRevSize, gRevDecay, gRevDamping, gRevPreDelay, gRevWidth, gRevReturn, gGlue, gCeiling};
/** A route can move this parameter: a channel's continuous one in kModDests, or one of kModGlobalTargets. */
inline bool IsModTarget(int id)
{
    int slot, p;
    if(IsSlotId(id, slot, p))
        return ModDestStep(p) > 0;
    for(int g : kModGlobalTargets)
        if(g == id)
            return true;
    return false;
}

/** The ids a host sees, in order (globals, each slot's, the pool's): every id but the hidden ones and the
 *  routes. Built once. */
struct VisibleTable
{
    int count = 0, ids[kNumIds], index[kNumIds];
    VisibleTable()
    {
        for(int id = 0; id < kNumIds; id++)
        {
            const uint32_t f = ParamInfo(id).flags;
            index[id]        = (f & (pfHidden | pfNoHost)) ? -1 : count;
            if(index[id] >= 0)
                ids[count++] = id;
        }
    }
};
inline const VisibleTable& Visible()
{
    static const VisibleTable t;
    return t;
}
inline int VisibleParamCount() { return Visible().count; }
inline int VisibleId(int index) { return index >= 0 && index < Visible().count ? Visible().ids[index] : -1; }
inline int VisibleIndexOf(int id) { return id >= 0 && id < kNumIds ? Visible().index[id] : -1; }
/** Saved with the patch (Save, Load, presets): every id but the hidden ones, the routes too. */
inline bool IsSaved(int id) { return id >= 0 && id < kNumIds && !(ParamInfo(id).flags & pfHidden); }

inline float ClampParam(int id, float v)
{
    const ParamDef& d = ParamInfo(id);
    if(d.steps > 0)
        v = std::round(v);
    return v < d.min ? d.min : (v > d.max ? d.max : v);
}

/** The default patch: the hardware's three default arps, A minor, 110 BPM
 *  Applied over the table defaults. */
struct PatchEntry
{
    int   slot, param;
    float value;
};
inline const PatchEntry kDefaultPatch[] = {
    // slot 1: UP, FIFTHS, 1 octave, 1/4, length 8: a saw pluck
    {0, cGen, 1}, {0, cArpMode, 2}, {0, cArpPool, 3}, {0, cArpRange, 1}, {0, cArpDiv, kDiv4}, {0, cArpLength, 8},
    {0, cOctave, 0}, {0, cDegree, 0}, {0, cLevel, 0.8f}, {0, cShape, 0.667f}, {0, cAmpSustain, 0.4f}, {0, cAmpDecay, 0.656f},
    {0, cAmpRelease, 0.656f}, {0, cPan, 0.f},
    // slot 2: UP-DOWN, TRIAD, 2 octaves, 1/8, length 8: a square-ish pluck an octave up
    {1, cGen, 1}, {1, cArpMode, 4}, {1, cArpPool, 1}, {1, cArpRange, 2}, {1, cArpDiv, kDiv8}, {1, cArpLength, 8},
    {1, cOctave, 1}, {1, cDegree, 2}, {1, cLevel, 0.7f}, {1, cShape, 1.0f}, {1, cAmpSustain, 0.f}, {1, cAmpDecay, 0.585f},
    {1, cAmpRelease, 0.585f}, {1, cPan, -0.4f},
    // slot 3: RANDOM, KEY, 2 octaves, 1/16, length 6, density 0.6: a soft high sparkle
    {2, cGen, 1}, {2, cArpMode, 5}, {2, cArpPool, 0}, {2, cArpRange, 2}, {2, cArpDiv, kDiv16}, {2, cArpLength, 6},
    {2, cArpDensity, 0.6f}, {2, cOctave, 2}, {2, cDegree, 4}, {2, cLevel, 0.6f}, {2, cShape, 0.2f}, {2, cAmpSustain, 0.f},
    {2, cAmpDecay, 0.536f}, {2, cAmpRelease, 0.536f}, {2, cPan, 0.4f},
};
inline constexpr int kDefaultPatchSize = sizeof(kDefaultPatch) / sizeof(kDefaultPatch[0]);

} // namespace ammo
