/** Factory presets: Init, the default patch, and the hardware's manual
 *  recipes translated to this engine.
 *  A preset is the table defaults plus a list of (id, value). Hardware
 *  drones (arp OFF) become Loop at 1/1, gate 1, sustain 1, glide; the
 *  hardware's DELAY SEND becomes the channel's delay MIX (its power-on
 *  divisions 1/8., 1/4, 1/8T per oscillator), its REV SEND the reverb send;
 *  its LFOs a pool LFO on a route (Slow tide's slow filter).
 *
 *  The macros do the same in every preset (Mod.hpp routes, at 0 they change
 *  nothing, so the levelling holds): BLOOM opens the filters, SPACE sends
 *  more into the reverb, GRIT drives a channel or two, MOTION thickens the
 *  patterns (density); a preset marks the macros it routes in use. Pool
 *  routes 4 s + 1 .. 4 s + 4 belong to channel s + 1 (version 2's four per
 *  channel): 3 and 4 hold Bloom and Space; 1 and 2 an LFO, or Grit and
 *  Motion. The LFOs are numbered per preset from 1.
 *
 *  Levelled (package B3): every preset plays at about -18 dB (a K-weighted
 *  level, test_presets.py) through its TRIM, a factor on every channel's
 *  level. */
#pragma once

#include "Channel.hpp" // the generators' enums (Arp, Chord, Bass)
#include "Mod.hpp"
#include "Params.hpp"

namespace ammo {

struct PresetEntry
{
    int   id;
    float value;
};

struct Preset
{
    const char*        name;
    const char*        blurb;
    const PresetEntry* entries;
    int                count;
    float              trim; // every channel's level times this (the loudness levelling)
};

#define AMMO_S(slot, param, value) {SlotId(slot, param), (float)(value)}
#define AMMO_G(param, value) {(param), (float)(value)}

// Shape mapping from the hardware (0 sine, 0.5 square, 1 saw) to this morph
// (0 sine, 1/3 triangle, 2/3 saw, 1 square): chosen by character.
constexpr float kSine = 0.f, kSoft = 0.2f, kTri = 0.333f, kSaw = 0.667f, kSquare = 1.f;
// Time curves: DecaySeconds(v) = 5 ms * 2^(9.64 v); AttackSeconds(v) = 1 ms * 2^(12 v)
constexpr float kDec150 = 0.509f, kDec200 = 0.552f, kDec300 = 0.613f, kDec400 = 0.656f, kDec900 = 0.777f, kDec2s = 0.897f;
constexpr float kAtt1s = 0.830f, kAtt300 = 0.686f;

/** A drone-ish channel: Loop every bar, legato, held, gliding. */
#define AMMO_DRONE(slot)                                                                                     \
    AMMO_S(slot, cGen, 1), AMMO_S(slot, cArpMode, kArpLoop), AMMO_S(slot, cArpPool, kPoolRoot),               \
        AMMO_S(slot, cArpDiv, kDiv1_1), AMMO_S(slot, cArpLength, 1), AMMO_S(slot, cArpGate, 1.f),                   \
        AMMO_S(slot, cAmpSustain, 1.f), AMMO_S(slot, cAmpAttack, kAtt300), AMMO_S(slot, cAmpRelease, kDec900), \
        AMMO_S(slot, cGlide, 0.7f)

/** Pool route 4 slot + r (r 1..4: the slot's own, as version 2's four): source, the slot parameter it moves, amount;
 *  bipolar (the default). */
#define AMMO_ROUTE(slot, r, src, param, amt)                                                                              \
    {RouteId(4 * (slot) + (r) - 1, rSrc), (float)(src)}, {RouteId(4 * (slot) + (r) - 1, rTarget), (float)SlotId(slot, param)}, \
        {RouteId(4 * (slot) + (r) - 1, rAmt), (float)(amt)}
/** Pool LFO l (1..32), in use: Free at RATE (0..1, LfoHz), or Tempo at a division (kLfoDivNames), and its phase. */
#define AMMO_LFO_FREE(l, shape, rate, phase)                                                                    \
    {LfoId((l) - 1, lOn), 1.f}, {LfoId((l) - 1, lShape), (float)(shape)}, {LfoId((l) - 1, lSync), 0.f},          \
        {LfoId((l) - 1, lRate), (float)(rate)}, {LfoId((l) - 1, lPhase), (float)(phase)}
#define AMMO_LFO_TEMPO(l, shape, div, phase)                                                                    \
    {LfoId((l) - 1, lOn), 1.f}, {LfoId((l) - 1, lShape), (float)(shape)}, {LfoId((l) - 1, lSync), 1.f},          \
        {LfoId((l) - 1, lDiv), (float)(div)}, {LfoId((l) - 1, lPhase), (float)(phase)}
/** The source code of pool LFO l (1..32). */
#define AMMO_LFO(l) (kSrcLfo1 + (l) - 1)
/** Macro m (1..16) in use (the page lists it). */
#define AMMO_MACRO_ON(m) {MacroOnId((m) - 1), 1.f}
/** Bloom (Macro 1) opens the filter, Space (Macro 3) sends into the reverb: routes 3 and 4. A drum's Bloom is its Tone. */
#define AMMO_MACROS(slot)                                                                                        \
    AMMO_ROUTE(slot, 3, kSrcMacro1, cCutoff, 0.3f), AMMO_ROUTE(slot, 4, kSrcMacro1 + 2, cRevSend, 0.45f), AMMO_MACRO_ON(1), \
        AMMO_MACRO_ON(3)
#define AMMO_MACROS_DRUM(slot)                                                                                     \
    AMMO_ROUTE(slot, 3, kSrcMacro1, cDrumTone, 0.35f), AMMO_ROUTE(slot, 4, kSrcMacro1 + 2, cRevSend, 0.3f), AMMO_MACRO_ON(1), \
        AMMO_MACRO_ON(3)
/** Grit (Macro 4) on the channel's drive, Motion (Macro 2) on its density: route r. */
#define AMMO_GRIT(slot, r)   AMMO_ROUTE(slot, r, kSrcMacro1 + 3, cFxDrive, 0.6f), AMMO_MACRO_ON(4)
#define AMMO_MOTION(slot, r) AMMO_ROUTE(slot, r, kSrcMacro1 + 1, cArpDensity, 0.45f), AMMO_MACRO_ON(2)
// the LFO divisions by name (kLfoDivNames)
constexpr int kLfo4Bars = 2, kLfo2Bars = 3, kLfo1Bar = 4, kLfoHalf = 6;

// The levelling (package B3): per preset a trim on every channel's level, so that each plays at about -18 dB
// K-weighted (test_presets.py measures it) through the default GLUE (0.3).
constexpr float kTrimFirst = 1.02f, kTrimTide = 0.62f, kTrimClockwork = 1.13f, kTrimDub = 1.16f, kTrimJazz = 0.93f,
                kTrimBand = 1.07f, kTrimGroove = 0.41f, kTrimBells = 0.83f, kTrimBloom = 0.92f;
// delay divisions by name (kDelayDivNames)
constexpr int kDelay8T = 4, kDelay8Dot = 5, kDelay4 = 6, kDelay4Dot = 8, kDelay2 = 9;

inline const PresetEntry kPresetInit[] = {
    AMMO_G(gTempo, 110.f), // the table defaults: nothing on
};

inline const PresetEntry kPresetFirstLight[] = {
    // the default patch (kDefaultPatch), spelled out so a preset is self-contained
    AMMO_S(0, cGen, 1), AMMO_S(0, cArpMode, 2), AMMO_S(0, cArpPool, 3), AMMO_S(0, cArpRange, 1), AMMO_S(0, cArpDiv, kDiv4),
    AMMO_S(0, cArpLength, 8), AMMO_S(0, cOctave, 0), AMMO_S(0, cDegree, 0), AMMO_S(0, cLevel, 0.8f), AMMO_S(0, cShape, kSaw),
    AMMO_S(0, cAmpSustain, 0.4f), AMMO_S(0, cAmpDecay, kDec400), AMMO_S(0, cAmpRelease, kDec400),
    AMMO_S(1, cGen, 1), AMMO_S(1, cArpMode, 4), AMMO_S(1, cArpPool, 1), AMMO_S(1, cArpRange, 2), AMMO_S(1, cArpDiv, kDiv8),
    AMMO_S(1, cArpLength, 8), AMMO_S(1, cOctave, 1), AMMO_S(1, cDegree, 2), AMMO_S(1, cLevel, 0.7f), AMMO_S(1, cShape, kSquare),
    AMMO_S(1, cAmpSustain, 0.f), AMMO_S(1, cAmpDecay, 0.585f), AMMO_S(1, cAmpRelease, 0.585f), AMMO_S(1, cPan, -0.4f),
    AMMO_S(2, cGen, 1), AMMO_S(2, cArpMode, 5), AMMO_S(2, cArpPool, 0), AMMO_S(2, cArpRange, 2), AMMO_S(2, cArpDiv, kDiv16),
    AMMO_S(2, cArpLength, 6), AMMO_S(2, cArpDensity, 0.6f), AMMO_S(2, cOctave, 2), AMMO_S(2, cDegree, 4), AMMO_S(2, cLevel, 0.6f),
    AMMO_S(2, cShape, kSoft), AMMO_S(2, cAmpSustain, 0.f), AMMO_S(2, cAmpDecay, 0.536f), AMMO_S(2, cAmpRelease, 0.536f),
    AMMO_S(2, cPan, 0.4f),
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2), AMMO_GRIT(0, 1), AMMO_MOTION(1, 1), AMMO_MOTION(2, 1),
};

inline const PresetEntry kPresetTide[] = {
    // three drones sliding chord to chord, Sad vi IV I V every 2 bars, 70 BPM
    AMMO_G(gTempo, 70.f), AMMO_G(gProg, 3), AMMO_G(gChange, 2),
    AMMO_DRONE(0), AMMO_S(0, cShape, 0.4f), AMMO_S(0, cDetune, 12.f), AMMO_S(0, cCutoff, 0.45f), AMMO_S(0, cLevel, 0.8f),
    AMMO_S(0, cOctave, 0), AMMO_S(0, cDegree, 0), AMMO_S(0, cPan, -0.3f), AMMO_S(0, cAmpAttack, kAtt1s),
    AMMO_DRONE(1), AMMO_S(1, cShape, kSoft), AMMO_S(1, cDetune, 12.f), AMMO_S(1, cCutoff, 0.5f), AMMO_S(1, cLevel, 0.7f),
    AMMO_S(1, cOctave, 1), AMMO_S(1, cDegree, 2), AMMO_S(1, cPan, 0.3f), AMMO_S(1, cAmpAttack, kAtt1s),
    AMMO_DRONE(2), AMMO_S(2, cShape, kSine), AMMO_S(2, cDetune, 12.f), AMMO_S(2, cCutoff, 0.55f), AMMO_S(2, cLevel, 0.6f),
    AMMO_S(2, cOctave, 2), AMMO_S(2, cDegree, 4), AMMO_S(2, cPan, 0.f), AMMO_S(2, cAmpAttack, kAtt1s),
    AMMO_S(0, cRevSend, 0.6f), AMMO_S(1, cRevSend, 0.6f), AMMO_S(2, cRevSend, 0.6f), AMMO_G(gRevDecay, 0.55f), AMMO_G(gRevSize, 0.7f),
    // the recipe's slow filter: LFO CUTOFF DEPTH 35 on each (+-1 octave), at the hardware's 0.09, 0.13 and 0.17 Hz,
    // a third of a cycle apart: three LFOs, one per drone
    AMMO_LFO_FREE(1, kLfoSine, 0.218f, 0.f), AMMO_ROUTE(0, 1, AMMO_LFO(1), cCutoff, 0.105f),
    AMMO_LFO_FREE(2, kLfoSine, 0.271f, 0.333f), AMMO_ROUTE(1, 1, AMMO_LFO(2), cCutoff, 0.105f),
    AMMO_LFO_FREE(3, kLfoSine, 0.31f, 0.667f), AMMO_ROUTE(2, 1, AMMO_LFO(3), cCutoff, 0.105f),
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2),
};

inline const PresetEntry kPresetClockwork[] = {
    // three sixteenth patterns of 8, 7 and 5 steps with steps left out; 120 BPM
    AMMO_G(gTempo, 120.f),
    AMMO_S(0, cGen, 1), AMMO_S(0, cArpMode, kArpUp), AMMO_S(0, cArpPool, kPoolRoot), AMMO_S(0, cArpDiv, kDiv16), AMMO_S(0, cArpLength, 8),
    AMMO_S(0, cArpDensity, 0.625f), AMMO_S(0, cArpRange, 1), AMMO_S(0, cOctave, 0), AMMO_S(0, cShape, kSaw), AMMO_S(0, cLevel, 0.8f),
    AMMO_S(0, cAmpDecay, kDec150), AMMO_S(0, cAmpRelease, kDec150), AMMO_S(0, cAmpSustain, 0.f), AMMO_S(0, cCutoff, 0.45f),
    AMMO_S(0, cReso, 0.5f), AMMO_S(0, cFiltEnvAmount, 0.5f), AMMO_S(0, cFiltDecay, kDec150), AMMO_S(0, cPan, -0.35f),
    AMMO_S(1, cGen, 1), AMMO_S(1, cArpMode, kArpRandom), AMMO_S(1, cArpPool, kPoolTriad), AMMO_S(1, cArpDiv, kDiv16), AMMO_S(1, cArpLength, 7),
    AMMO_S(1, cArpDensity, 0.571f), AMMO_S(1, cArpRange, 2), AMMO_S(1, cOctave, 1), AMMO_S(1, cDegree, 2), AMMO_S(1, cShape, kSquare),
    AMMO_S(1, cLevel, 0.7f), AMMO_S(1, cAmpDecay, kDec150), AMMO_S(1, cAmpRelease, kDec150), AMMO_S(1, cAmpSustain, 0.f),
    AMMO_S(1, cCutoff, 0.45f), AMMO_S(1, cReso, 0.5f), AMMO_S(1, cFiltEnvAmount, 0.5f), AMMO_S(1, cFiltDecay, kDec150), AMMO_S(1, cPan, 0.35f),
    AMMO_S(2, cGen, 1), AMMO_S(2, cArpMode, kArpUpDown), AMMO_S(2, cArpPool, kPoolKey), AMMO_S(2, cArpDiv, kDiv16), AMMO_S(2, cArpLength, 5),
    AMMO_S(2, cArpDensity, 0.6f), AMMO_S(2, cArpRange, 2), AMMO_S(2, cOctave, 2), AMMO_S(2, cDegree, 4), AMMO_S(2, cShape, kSoft),
    AMMO_S(2, cLevel, 0.6f), AMMO_S(2, cAmpDecay, kDec150), AMMO_S(2, cAmpRelease, kDec150), AMMO_S(2, cAmpSustain, 0.f),
    AMMO_S(2, cCutoff, 0.45f), AMMO_S(2, cReso, 0.5f), AMMO_S(2, cFiltEnvAmount, 0.5f), AMMO_S(2, cFiltDecay, kDec150),
    AMMO_S(1, cDelayDiv, kDelay4), AMMO_S(1, cDelayMix, 0.35f), // the recipe's delay send on OSC 2
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2), AMMO_MOTION(0, 1), AMMO_MOTION(1, 1), AMMO_MOTION(2, 1), AMMO_GRIT(0, 2),
};

inline const PresetEntry kPresetDub[] = {
    // a sparse resonant stab over a sine bass following the chords; Vamp I IV every 2 bars, G, 90 BPM
    AMMO_G(gTempo, 90.f), AMMO_G(gRoot, 7), AMMO_G(gProg, 6), AMMO_G(gChange, 2),
    AMMO_S(0, cGen, 1), AMMO_S(0, cArpMode, kArpRandom), AMMO_S(0, cArpPool, kPoolFifths), AMMO_S(0, cArpDiv, kDiv8), AMMO_S(0, cArpLength, 8),
    AMMO_S(0, cArpGate, 0.2f), AMMO_S(0, cArpDensity, 0.375f), AMMO_S(0, cOctave, 1), AMMO_S(0, cShape, kSquare), AMMO_S(0, cLevel, 0.8f),
    AMMO_S(0, cAmpDecay, kDec200), AMMO_S(0, cAmpRelease, kDec200), AMMO_S(0, cAmpSustain, 0.f), AMMO_S(0, cCutoff, 0.6f),
    AMMO_S(0, cReso, 0.55f), AMMO_S(0, cFilterType, 0.5f),
    AMMO_DRONE(2), AMMO_S(2, cShape, kSine), AMMO_S(2, cOctave, -1), AMMO_S(2, cDegree, 0), AMMO_S(2, cLevel, 0.55f),
    AMMO_S(2, cGlide, 0.5f), AMMO_S(2, cAmpAttack, 0.3f), AMMO_S(2, cCutoff, 0.6f),
    // the echo: dotted eighths, feedback 70, send 60, tone 1.5 kHz, width 100 (ping-pong); rev send 30
    AMMO_S(0, cDelayDiv, kDelay8Dot), AMMO_S(0, cDelayFeedback, 0.7f), AMMO_S(0, cDelayMix, 0.6f), AMMO_S(0, cDelayDamping, 0.654f),
    AMMO_S(0, cDelayPingPong, 1), AMMO_S(0, cRevSend, 0.3f),
    // the stab's filter breathes over two bars
    AMMO_LFO_TEMPO(1, kLfoTriangle, kLfo2Bars, 0.f), AMMO_ROUTE(0, 1, AMMO_LFO(1), cCutoff, 0.08f),
    // the stab is a band-pass into a dark echo: its Bloom also lifts the echo's damping (the echoes come back brighter)
    AMMO_MACROS(0), AMMO_ROUTE(0, 2, kSrcMacro1, cDelayDamping, -0.55f), AMMO_MACROS(2),
};

inline const PresetEntry kPresetJazz[] = {
    // a swung ii V I in C: a gliding bass, an arp through each seventh chord, a looser triplet line; 96 BPM
    AMMO_G(gTempo, 96.f), AMMO_G(gRoot, 0), AMMO_G(gScale, 0), AMMO_G(gSwing, 0.5f), AMMO_G(gProg, 5),
    AMMO_DRONE(0), AMMO_S(0, cShape, kSine), AMMO_S(0, cOctave, -1), AMMO_S(0, cLevel, 0.8f), AMMO_S(0, cGlide, 0.5f),
    AMMO_S(0, cAmpAttack, 0.3f), AMMO_S(0, cCutoff, 0.6f),
    AMMO_S(1, cGen, 1), AMMO_S(1, cArpMode, kArpUp), AMMO_S(1, cArpPool, kPoolSeventh), AMMO_S(1, cArpDiv, kDiv8), AMMO_S(1, cArpLength, 8),
    AMMO_S(1, cArpRange, 2), AMMO_S(1, cOctave, 1), AMMO_S(1, cDegree, 0), AMMO_S(1, cShape, 0.5f), AMMO_S(1, cLevel, 0.7f),
    AMMO_S(1, cAmpDecay, kDec300), AMMO_S(1, cAmpRelease, kDec300), AMMO_S(1, cAmpSustain, 0.2f), AMMO_S(1, cPan, -0.3f),
    AMMO_S(2, cGen, 1), AMMO_S(2, cArpMode, kArpRandom), AMMO_S(2, cArpPool, kPoolSeventh), AMMO_S(2, cArpDiv, kDiv4T), AMMO_S(2, cArpLength, 6),
    AMMO_S(2, cArpRange, 2), AMMO_S(2, cArpVary, 0.3f), AMMO_S(2, cOctave, 2), AMMO_S(2, cDegree, 0), AMMO_S(2, cShape, 0.4f),
    AMMO_S(2, cLevel, 0.6f), AMMO_S(2, cAmpDecay, 0.536f), AMMO_S(2, cAmpRelease, 0.536f), AMMO_S(2, cAmpSustain, 0.f), AMMO_S(2, cPan, 0.3f),
    AMMO_S(1, cRevSend, 0.35f), AMMO_S(2, cRevSend, 0.35f),
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2), AMMO_MOTION(2, 1),
};

inline const PresetEntry kPresetBells[] = {
    // Pachelbel's chords in D major on sine bells that start an octave high and drop into place; 80 BPM
    AMMO_G(gTempo, 80.f), AMMO_G(gRoot, 2), AMMO_G(gScale, 0), AMMO_G(gProg, 7), AMMO_G(gChange, 0), AMMO_G(gRestart, kRestartChord),
    AMMO_DRONE(0), AMMO_S(0, cShape, 0.4f), AMMO_S(0, cOctave, 0), AMMO_S(0, cLevel, 0.6f), AMMO_S(0, cCutoff, 0.5f),
    AMMO_S(1, cGen, 1), AMMO_S(1, cArpMode, kArpUp), AMMO_S(1, cArpPool, kPoolTriad), AMMO_S(1, cArpDiv, kDiv8), AMMO_S(1, cArpLength, 8),
    AMMO_S(1, cArpRange, 2), AMMO_S(1, cOctave, 1), AMMO_S(1, cDegree, 2), AMMO_S(1, cShape, kSine), AMMO_S(1, cLevel, 0.7f),
    AMMO_S(1, cAmpDecay, kDec900), AMMO_S(1, cAmpRelease, kDec900), AMMO_S(1, cAmpSustain, 0.f), AMMO_S(1, cPitchEnvAmount, 1.f),
    AMMO_S(1, cPitchDecay, 0.1f), AMMO_S(1, cPan, -0.3f),
    AMMO_S(2, cGen, 1), AMMO_S(2, cArpMode, kArpDown), AMMO_S(2, cArpPool, kPoolKey), AMMO_S(2, cArpDiv, kDiv16), AMMO_S(2, cArpLength, 6),
    AMMO_S(2, cArpDensity, 0.75f), AMMO_S(2, cArpRange, 2), AMMO_S(2, cOctave, 2), AMMO_S(2, cDegree, 4), AMMO_S(2, cShape, kSine),
    AMMO_S(2, cLevel, 0.6f), AMMO_S(2, cAmpDecay, kDec900), AMMO_S(2, cAmpRelease, kDec900), AMMO_S(2, cAmpSustain, 0.f),
    AMMO_S(2, cPitchEnvAmount, 1.f), AMMO_S(2, cPitchDecay, 0.1f), AMMO_S(2, cPan, 0.3f),
    AMMO_S(2, cDelayDiv, kDelay8T), AMMO_S(2, cDelayMix, 0.3f), AMMO_S(1, cRevSend, 0.45f), AMMO_S(2, cRevSend, 0.45f),
    AMMO_G(gRevDecay, 0.55f),
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2), AMMO_MOTION(2, 1),
};

inline const PresetEntry kPresetBand[] = {
    // phase 4's generators together: an open seventh-chord pad held and strummed, a walking bass leading into
    // each chord, a soft arp on top; Pop I V vi IV in F, a bar per chord, 92 BPM
    AMMO_G(gTempo, 92.f), AMMO_G(gRoot, 5), AMMO_G(gScale, 0), AMMO_G(gProg, 1), AMMO_G(gChange, 1),
    AMMO_S(0, cGen, kGenChord), AMMO_S(0, cChordNotes, 1), AMMO_S(0, cChordVoicing, kVoiceOpen), AMMO_S(0, cChordLead, 1),
    AMMO_S(0, cChordRhythm, 0), AMMO_S(0, cChordStrum, 0.25f), AMMO_S(0, cShape, 0.5f), AMMO_S(0, cDetune, 10.f),
    AMMO_S(0, cCutoff, 0.5f), AMMO_S(0, cAmpAttack, kAtt300), AMMO_S(0, cAmpSustain, 0.8f), AMMO_S(0, cAmpRelease, kDec900),
    AMMO_S(0, cLevel, 0.6f), AMMO_S(0, cPan, -0.15f),
    AMMO_S(1, cGen, kGenBass), AMMO_S(1, cBassPattern, kBassWalk), AMMO_S(1, cArpDiv, kDiv4), AMMO_S(1, cArpLength, 4),
    AMMO_S(1, cArpGate, 0.8f), AMMO_S(1, cOctave, 0), AMMO_S(1, cShape, kSoft), AMMO_S(1, cCutoff, 0.4f), AMMO_S(1, cAmpSustain, 0.6f),
    AMMO_S(1, cAmpDecay, kDec300), AMMO_S(1, cAmpRelease, kDec150), AMMO_S(1, cGlide, 0.2f), AMMO_S(1, cLevel, 0.85f),
    AMMO_S(2, cGen, kGenArp), AMMO_S(2, cArpMode, kArpUpDown), AMMO_S(2, cArpPool, kPoolTriad), AMMO_S(2, cArpDiv, kDiv8),
    AMMO_S(2, cArpLength, 8), AMMO_S(2, cArpDensity, 0.75f), AMMO_S(2, cArpRange, 2), AMMO_S(2, cOctave, 2), AMMO_S(2, cShape, kSine),
    AMMO_S(2, cAmpDecay, kDec300), AMMO_S(2, cAmpRelease, kDec300), AMMO_S(2, cAmpSustain, 0.f), AMMO_S(2, cLevel, 0.5f),
    AMMO_S(2, cPan, 0.3f),
    AMMO_S(0, cRevSend, 0.3f), AMMO_S(2, cRevSend, 0.35f), AMMO_S(2, cDelayDiv, kDelay8Dot), AMMO_S(2, cDelayMix, 0.25f),
    AMMO_S(0, cChorusOn, 1), AMMO_S(0, cChorusMix, 0.4f),
    // the pad opens and closes over four bars (version 2's global LFO 1 as it came: a sine four bars long)
    AMMO_LFO_TEMPO(1, kLfoSine, kLfo4Bars, 0.f), AMMO_ROUTE(0, 1, AMMO_LFO(1), cCutoff, 0.07f),
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2), AMMO_GRIT(1, 2), AMMO_MOTION(2, 1),
};

/** A drum channel: the Rhythm generator on the drum voice. */
#define AMMO_DRUM(slot, model, div, len, density)                                                                  AMMO_S(slot, cGen, kGenRhythm), AMMO_S(slot, cInst, kInstDrum), AMMO_S(slot, cDrumModel, model),                      AMMO_S(slot, cArpDiv, div), AMMO_S(slot, cArpLength, len), AMMO_S(slot, cArpDensity, density),                  AMMO_S(slot, cOctave, 0)

inline const PresetEntry kPresetGroove[] = {
    // phase 4's rhythm and drums: four on the floor, a clap on 2 and 4, sixteenth hats that breathe, open hats on
    // the offbeat, a Root-5th bass and syncopated seventh-chord stabs; Epic i VI III VII in A minor, 118 BPM
    AMMO_G(gTempo, 118.f), AMMO_G(gRoot, 9), AMMO_G(gScale, 1), AMMO_G(gProg, 4), AMMO_G(gChange, 1),
    AMMO_DRUM(0, kDrumKick, kDiv4, 4, 1.f), AMMO_S(0, cDrumDecay, 0.45f), AMMO_S(0, cDrumSnap, 0.6f), AMMO_S(0, cDrumTone, 0.4f),
    AMMO_S(0, cLevel, 0.9f),
    AMMO_DRUM(1, kDrumClap, kDiv4, 4, 0.5f), AMMO_S(1, cRhythmRotate, 1), AMMO_S(1, cLevel, 0.7f), AMMO_S(1, cPan, -0.05f),
    AMMO_DRUM(2, kDrumHat, kDiv16, 16, 1.f), AMMO_S(2, cRhythmChance, 0.85f), AMMO_S(2, cArpVary, 0.5f), AMMO_S(2, cDrumTone, 0.7f),
    AMMO_S(2, cLevel, 0.5f), AMMO_S(2, cPan, 0.25f),
    AMMO_DRUM(3, kDrumOpenHat, kDiv8, 8, 0.25f), AMMO_S(3, cRhythmRotate, 1), AMMO_S(3, cDrumDecay, 0.35f), AMMO_S(3, cLevel, 0.45f),
    AMMO_S(3, cPan, -0.2f),
    AMMO_S(4, cGen, kGenBass), AMMO_S(4, cBassPattern, kBassRootFifth), AMMO_S(4, cArpDiv, kDiv8), AMMO_S(4, cArpLength, 8),
    AMMO_S(4, cArpDensity, 0.75f), AMMO_S(4, cArpGate, 0.5f), AMMO_S(4, cOctave, 0), AMMO_S(4, cShape, kSaw), AMMO_S(4, cCutoff, 0.35f),
    AMMO_S(4, cReso, 0.4f), AMMO_S(4, cFiltEnvAmount, 0.4f), AMMO_S(4, cFiltDecay, kDec200), AMMO_S(4, cAmpSustain, 0.f),
    AMMO_S(4, cAmpDecay, kDec200), AMMO_S(4, cAmpRelease, kDec150), AMMO_S(4, cLevel, 0.75f),
    AMMO_S(5, cGen, kGenChord), AMMO_S(5, cChordNotes, 1), AMMO_S(5, cChordVoicing, kVoiceDrop2), AMMO_S(5, cChordLead, 1),
    AMMO_S(5, cChordRhythm, 1), AMMO_S(5, cArpDiv, kDiv8), AMMO_S(5, cArpLength, 8), AMMO_S(5, cArpDensity, 0.375f), AMMO_S(5, cArpGate, 0.2f),
    AMMO_S(5, cShape, 0.6f), AMMO_S(5, cCutoff, 0.5f), AMMO_S(5, cAmpSustain, 0.1f), AMMO_S(5, cAmpDecay, kDec300),
    AMMO_S(5, cAmpRelease, kDec300), AMMO_S(5, cLevel, 0.5f), AMMO_S(5, cPan, 0.15f),
    // the kick ducks the bass and the stabs; the stabs and the clap in a short room
    AMMO_S(4, cDuckSource, 1), AMMO_S(4, cDuckAmount, 0.5f), AMMO_S(5, cDuckSource, 1), AMMO_S(5, cDuckAmount, 0.6f),
    AMMO_S(5, cRevSend, 0.3f), AMMO_S(1, cRevSend, 0.2f), AMMO_G(gRevDecay, 0.35f), AMMO_G(gRevSize, 0.35f),
    // the hats breathe: their tone rises and falls over two bars
    AMMO_LFO_TEMPO(1, kLfoSine, kLfo2Bars, 0.75f), AMMO_ROUTE(2, 1, AMMO_LFO(1), cDrumTone, 0.15f),
    AMMO_MACROS_DRUM(0), AMMO_MACROS_DRUM(1), AMMO_MACROS_DRUM(2), AMMO_MACROS_DRUM(3), AMMO_MACROS(4), AMMO_MACROS(5),
    AMMO_GRIT(4, 1), AMMO_MOTION(2, 2), AMMO_MOTION(5, 1),
};
#undef AMMO_DRUM

inline const PresetEntry kPresetBloom[] = {
    // the slow, long-echo space: a seventh chord arpeggiated in eighths at 36 BPM, every note left ringing until
    // the chord changes, through the Ensemble into a half-note ping-pong echo (3.3 s) and a large room; a sine
    // drone under it and a few high notes thrown into a longer echo; Fall i VII VI v in D minor, a bar per chord
    AMMO_G(gTempo, 36.f), AMMO_G(gRoot, 2), AMMO_G(gScale, 1), AMMO_G(gProg, 8), AMMO_G(gChange, 1),
    AMMO_S(0, cGen, kGenChord), AMMO_S(0, cChordNotes, 1), AMMO_S(0, cChordVoicing, kVoiceOpen), AMMO_S(0, cChordLead, 1),
    AMMO_S(0, cChordRhythm, kChordArp), AMMO_S(0, cChordLetRing, 1), AMMO_S(0, cArpMode, kArpUp), AMMO_S(0, cArpDiv, kDiv8),
    AMMO_S(0, cArpLength, 8), AMMO_S(0, cArpDensity, 0.75f), AMMO_S(0, cShape, 0.4f), AMMO_S(0, cDetune, 8.f),
    AMMO_S(0, cCutoff, 0.5f), AMMO_S(0, cAmpAttack, 0.36f), AMMO_S(0, cAmpDecay, kDec2s), AMMO_S(0, cAmpSustain, 0.6f),
    AMMO_S(0, cAmpRelease, kDec2s), AMMO_S(0, cLevel, 0.75f), AMMO_S(0, cPan, -0.1f),
    AMMO_S(0, cChorusOn, 1), AMMO_S(0, cChorusMode, kChorusEnsemble), AMMO_S(0, cChorusRate, 0.3f), AMMO_S(0, cChorusDepth, 0.7f),
    AMMO_S(0, cChorusMix, 0.5f), AMMO_S(0, cDelayDiv, kDelay2), AMMO_S(0, cDelayFeedback, 0.55f), AMMO_S(0, cDelayDamping, 0.5f),
    AMMO_S(0, cDelayPingPong, 1), AMMO_S(0, cDelayMix, 0.35f), AMMO_S(0, cRevSend, 0.4f),
    AMMO_DRONE(1), AMMO_S(1, cShape, kSine), AMMO_S(1, cOctave, -1), AMMO_S(1, cCutoff, 0.5f), AMMO_S(1, cAmpAttack, kAtt1s),
    AMMO_S(1, cLevel, 0.55f), AMMO_S(1, cRevSend, 0.25f),
    AMMO_S(2, cGen, kGenArp), AMMO_S(2, cArpMode, kArpRandom), AMMO_S(2, cArpPool, kPoolSeventh), AMMO_S(2, cArpDiv, kDiv8),
    AMMO_S(2, cArpLength, 8), AMMO_S(2, cArpDensity, 0.25f), AMMO_S(2, cArpRange, 2), AMMO_S(2, cOctave, 2), AMMO_S(2, cShape, kSine),
    AMMO_S(2, cAmpDecay, kDec900), AMMO_S(2, cAmpRelease, kDec900), AMMO_S(2, cAmpSustain, 0.f), AMMO_S(2, cLevel, 0.5f),
    AMMO_S(2, cPan, 0.3f), AMMO_S(2, cDelayDiv, kDelay4Dot), AMMO_S(2, cDelayFeedback, 0.7f), AMMO_S(2, cDelayDamping, 0.6f),
    AMMO_S(2, cDelayPingPong, 1), AMMO_S(2, cDelayMix, 0.5f), AMMO_S(2, cRevSend, 0.5f),
    AMMO_G(gRevSize, 0.8f), AMMO_G(gRevDecay, 0.7f), AMMO_G(gRevDamping, 0.4f),
    // the chord's filter drifts slowly (a Drift LFO, a new turn every two bars); the high notes wander side to side
    // (two LFOs: Drift draws from LFO 1's seed, which is version 2's channel 1 LFO 1's, so it drifts as it did)
    AMMO_LFO_TEMPO(1, kLfoDrift, kLfo2Bars, 0.f), AMMO_ROUTE(0, 1, AMMO_LFO(1), cCutoff, 0.08f),
    AMMO_LFO_TEMPO(2, kLfoSine, kLfo4Bars, 0.25f), AMMO_ROUTE(2, 1, AMMO_LFO(2), cPan, 0.3f),
    AMMO_MACROS(0), AMMO_MACROS(1), AMMO_MACROS(2), AMMO_MOTION(2, 2),
};

#define AMMO_PRESET(name, blurb, arr, trim) {name, blurb, arr, (int)(sizeof(arr) / sizeof(arr[0])), trim}
inline const Preset kPresets[] = {
    AMMO_PRESET("Init", "Every slot off: a blank page.", kPresetInit, 1.f),
    AMMO_PRESET("First light", "The three default arps in A minor: a saw pluck, a square an octave up, a soft sparkle.", kPresetFirstLight, kTrimFirst),
    AMMO_PRESET("Slow tide", "Three drones that slide from chord to chord under a slow filter. The progression does the moving.", kPresetTide, kTrimTide),
    AMMO_PRESET("Clockwork", "Sixteenth patterns of 8, 7 and 5 steps with steps left out: they line up once every 280 steps.", kPresetClockwork, kTrimClockwork),
    AMMO_PRESET("Dub echoes", "A sparse, resonant stab thrown into a dark ping-pong echo that sinks away, over a sine bass that follows the chords.", kPresetDub, kTrimDub),
    AMMO_PRESET("Late set", "A swung ii V I in C: a gliding bass, an arp through each seventh chord, a looser triplet line on top.", kPresetJazz, kTrimJazz),
    AMMO_PRESET("Evening band", "A held seventh-chord pad, a bass that walks into every chord, a soft arp on top: Pop in F.",
                kPresetBand, kTrimBand),
    AMMO_PRESET("Night groove", "Four on the floor, claps on 2 and 4, breathing hats, a Root-5th bass and chord stabs: Epic in A minor.",
                kPresetGroove, kTrimGroove),
    AMMO_PRESET("Bells in D", "Pachelbel's chords on sine bells: every note starts an octave high and drops into place.", kPresetBells, kTrimBells),
    AMMO_PRESET("Slow bloom", "36 BPM: a seventh chord arpeggiated note by note, each left ringing until the chord changes, through an ensemble chorus into a long echo and a large room.",
                kPresetBloom, kTrimBloom),
};
#undef AMMO_PRESET
inline constexpr int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

/** The value of parameter `id` in preset `k`: its entry, else the table default (a level times the trim). */
inline float PresetValue(int k, int id)
{
    const Preset& p = kPresets[k];
    float         v = ParamInfo(id).def;
    for(int i = p.count - 1; i >= 0; i--)
        if(p.entries[i].id == id)
        {
            v = p.entries[i].value;
            break;
        }
    int slot, sp;
    if(IsSlotId(id, slot, sp) && sp == cLevel)
        v = ClampParam(id, std::round(v * p.trim * 10000.f) / 10000.f); // 4 decimals: the saved text gives it back exactly
    return v;
}

#undef AMMO_S
#undef AMMO_G
#undef AMMO_DRONE
#undef AMMO_ROUTE
#undef AMMO_LFO_FREE
#undef AMMO_LFO_TEMPO
#undef AMMO_LFO
#undef AMMO_MACRO_ON
#undef AMMO_MACROS
#undef AMMO_MACROS_DRUM
#undef AMMO_GRIT
#undef AMMO_MOTION

} // namespace ammo
