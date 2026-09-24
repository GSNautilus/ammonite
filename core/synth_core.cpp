/**
 * Arp synth core: parameters, panel pages, audio engine and screen.
 *
 *   functions (enum Func) are what the audio code reads. A function is
 *   either GLOBAL or PER-OSC (three values, one per oscillator); both live
 *   in params_[func][osc]. kPages says which (func, osc) every (page,
 *   sub-page, pot) slot drives. To add a function: add a Func, its entry in
 *   kFuncs, use it in ProcessAudio, then place it in kPages.
 *
 *   3 mono oscillators, each: 2 detuned MorphOsc (SHAPE, DETUNE; note from
 *     ROOT / SCALE / CHORD + OCTAVE + DEGREE) -> drive (DRIVE) -> two SVFs,
 *     LP..BP..HP morph (CUTOFF, RESO, TYPE, master CUTOFF) -> LEVEL
 *     -> the two oscillators panned apart around PAN (WIDTH)
 *     -> its own stereo delay (DIVISION, FEEDBACK, SEND x master DELAY,
 *        TONE, WOBBLE, WIDTH)
 *   -> dry + repeats summed, and sent (REV SEND x master REVERB) through a
 *      pre-delay into ReverbSc (SIZE, DAMPING, PREDELAY)
 *   -> soft limiter -> VOLUME
 *
 *   one master clock (TEMPO, SWING) -> per osc: an arp step on its own
 *     DIVISION grid (MODE, POOL, RANGE, LENGTH, DENSITY, VARY, RATCHET,
 *     GATE) -> amp envelope (ATTACK, DECAY, SUSTAIN; release = DECAY),
 *     applied after the filter. MODE OFF = drone, envelope bypassed.
 *
 *   harmony: ROOT + SCALE, the chord degree from PROG (changing every
 *     CHANGE on the master clock) or KEY CHORD; patterns RESTART from 0,
 *     each bar or each chord change.
 *
 *   every kCtrl (16) samples: 15 LFOs (PITCH CUTOFF AMP PAN SHAPE per osc;
 *     free or synced to the clock), GLIDE, PITCH env (AMOUNT, DECAY) on the
 *     oscillators' pitch, FILTER env (ATTACK, DECAY, AMOUNT) on CUTOFF.
 *
 *   screen (KEY VIEW): RINGS (each osc's waveform as a ring of step arcs
 *     in the notes' fifths hues), WHEEL (the chord on the circle of
 *     fifths) or SCOPE; the chord in the centre; readouts and page maps.
 *
 * Build steps 1-7 of ENGINE_PLAN.md are in; step 8 (hardware) is the user's.
 */
#include <atomic>
#include <math.h>
#include "daisysp.h"
#include "canvas.h"
#include "synth_core.h"

namespace synth
{
/* ------------------------------------------------------------- functions */
enum Func
{
    LEVEL,    // MAIN (per osc)
    ROOT,     //   key tonic C..B
    SCALE,    //   10 scales then 10 chords
    TEMPO,    //   40..240 BPM
    MCUTOFF,  //   master: +-3 oct on every CUTOFF
    MDELAY,   //   master: 0..2x every delay SEND
    MREVERB,  //   master: 0..2x every REV SEND
    SWING,
    SHAPE,    // OSC (per osc): sine .. square .. saw
    OCTAVE,   //   -1..+3 above base octave 2
    DEGREE,   //   0..7 scale steps above the chord root
    DETUNE,   //   shared: 0..20 cents between an osc's two oscillators
    AMODE,    // ARP NOTES (per osc)
    APOOL,
    ARANGE,
    ADIV,     // ARP RHYTHM
    ALENGTH,
    AGATE,
    ADENSITY, // ARP CHANCE
    AVARY,
    ARATCHET,
    EAATT,    // ENVELOPE AMP (per osc)
    EADEC,    //   also the release time
    EASUS,
    EFATT,    // ENVELOPE FILTER
    EFDEC,
    EFAMT,
    EPAMT,    // ENVELOPE PITCH
    EPDEC,
    GLIDE,
    CUTOFF,   // FILTER (per osc)
    RESO,
    FTYPE,    //   LP -> BP -> HP
    DRIVE,    //   shared
    LPITCH_R, // LFO (per osc): one LFO per target, RATE / DEPTH / WAVE each
    LPITCH_D,
    LPITCH_W,
    LCUT_R,
    LCUT_D,
    LCUT_W,
    LAMP_R,
    LAMP_D,
    LAMP_W,
    LPAN_R,
    LPAN_D,
    LPAN_W,
    LSHAPE_R,
    LSHAPE_D,
    LSHAPE_W,
    DDIV,     // DELAY TIME (per osc)
    DFEED,
    DSEND,
    DTONE,    // DELAY COLOR
    DWOBBLE,
    DWIDTH,
    PAN,      // MIX (per osc)
    WIDTH,
    RSEND,
    RSIZE,    //   shared: reverb size
    PROG,     // KEY (global)
    CHANGE,
    CHORD,
    RESTART,
    KOCT,
    FINE,
    LFOSYNC,
    RDAMP,
    RPRE,
    VIEW,
    VOLUME,
    kNumFuncs
};
constexpr int kLfoTargets = 5; // PITCH CUTOFF AMP PAN SHAPE, 3 funcs each from LPITCH_R

/* --------------------------------------------------------- stepped names */
static const char* const kNoteNames[12]
    = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr int kNumKeySets = 10; // scales, and as many chords
static const int kScaleMask[kNumKeySets] = {
    0xAB5, // MAJOR     0 2 4 5 7 9 11
    0x5AD, // MINOR     0 2 3 5 7 8 10
    0x6AD, // DORIAN    0 2 3 5 7 9 10
    0x5AB, // PHRYGIAN  0 1 3 5 7 8 10
    0xAD5, // LYDIAN    0 2 4 6 7 9 11
    0x6B5, // MIXO      0 2 4 5 7 9 10
    0x9AD, // HARM MIN  0 2 3 5 7 8 11
    0x295, // PENTA     0 2 4 7 9
    0x4A9, // PENTA MI  0 3 5 7 10
    0x4E9, // BLUES     0 3 5 6 7 10
};
static const int kChordMask[kNumKeySets] = {
    0x091, // MAJ   0 4 7
    0x089, // MIN   0 3 7
    0x085, // SUS2  0 2 7
    0x0A1, // SUS4  0 5 7
    0x891, // MAJ7  0 4 7 11
    0x489, // MIN7  0 3 7 10
    0x491, // DOM7  0 4 7 10
    0x049, // DIM   0 3 6
    0x111, // AUG   0 4 8
    0x095, // ADD9  0 2 4 7
};
static const char* const kKeyNames[2 * kNumKeySets] = {
    "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXO", "HARM MIN", "PENTA", "PENTA MI", "BLUES",
    "MAJ",   "MIN",   "SUS2",   "SUS4",     "MAJ7",   "MIN7", "DOM7",     "DIM",   "AUG",      "ADD9"};
static const char* const kModeNames[] = {"OFF", "LOOP", "UP", "DOWN", "UP-DN", "RANDOM"};
static const char* const kPoolNames[] = {"KEY", "TRIAD", "7TH", "FIFTHS", "ROOT"};
static const char* const kRangeNames[] = {"1 OCT", "2 OCT", "3 OCT"};
static const char* const kArpDivNames[]
    = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char* const kWaveNames[] = {"SINE", "TRI", "SAW", "SQUARE", "S+H", "DRIFT"};
static const char* const kDelDivNames[]
    = {"1/32", "1/16", "1/16T", "1/8", "1/8T", "1/8.", "1/4", "1/4T", "1/4.", "1/2", "1/1"};
static const char* const kProgNames[]
    = {"OFF", "POP", "50S", "SAD", "EPIC", "JAZZ", "VAMP", "CANON", "FALL"};
static const char* const kChangeNames[] = {"1/2 BAR", "1 BAR", "2 BARS", "4 BARS"};
static const char* const kRoman[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
static const char* const kRestartNames[] = {"FREE", "BAR", "CHORD"};
static const char* const kSyncNames[] = {"FREE", "TEMPO"};
static const char* const kViewNames[] = {"RINGS", "PITCH", "WHEEL", "SCOPE"};
enum
{
    kViewRings = 0, // step arcs in their notes' hues
    kViewPitch,     // the same rings, each wave one hue: the note it is on
    kViewWheel,
    kViewScope
};

/** Default of a stepped function: the centre of step k of n. */
constexpr float St(int k, int n)
{
    return ((float)k + 0.5f) / (float)n;
}

/* ------------------------------------------------------------ func table */
// steps 0 = continuous. A stepped function shows names[k], or base + k as
// a number (with a sign when `sign`). def[] per osc (global: def[0]).
struct FuncInfo
{
    const char*        name;
    bool               perOsc;
    int                steps;
    const char* const* names;
    int                base;
    bool               sign;
    float              def[kOscs];
};
// Defaults (user 2026-09-23): start as simple as possible - three plain
// oscillators and their arps; every effect and modulation off (delay and
// reverb sends, LFO depths, FILTER / PITCH env amounts, DRIVE, WIDTH).
// The masters (MAIN DELAY / REVERB) stay at 1x so a send works when raised.
static const FuncInfo kFuncs[kNumFuncs] = {
    {"LEVEL", true, 0, nullptr, 0, false, {0.8f, 0.7f, 0.6f}},
    {"ROOT", false, 12, kNoteNames, 0, false, {St(9, 12)}},   // A
    {"SCALE", false, 20, kKeyNames, 0, false, {St(1, 20)}},   // MINOR
    {"TEMPO", false, 0, nullptr, 0, false, {0.35f}},          // 110 BPM
    {"CUTOFF", false, 0, nullptr, 0, false, {0.5f}},          // master, centre = as set
    {"DELAY", false, 0, nullptr, 0, false, {0.5f}},           // master, centre = 1x
    {"REVERB", false, 0, nullptr, 0, false, {0.5f}},          // master, centre = 1x
    {"SWING", false, 0, nullptr, 0, false, {0.f}},
    {"SHAPE", true, 0, nullptr, 0, false, {1.f, 0.5f, 0.3f}},
    {"OCTAVE", true, 5, nullptr, -1, true, {St(1, 5), St(2, 5), St(3, 5)}}, // 0 +1 +2
    {"DEGREE", true, 8, nullptr, 0, true, {St(0, 8), St(2, 8), St(4, 8)}},  // the triad
    {"DETUNE", false, 0, nullptr, 0, false, {0.4f}},                        // 8 cents
    {"MODE", true, 6, kModeNames, 0, false, {St(2, 6), St(4, 6), St(5, 6)}},   // UP UP-DN RANDOM
    {"POOL", true, 5, kPoolNames, 0, false, {St(3, 5), St(1, 5), St(0, 5)}},   // FIFTHS TRIAD KEY
    {"RANGE", true, 3, kRangeNames, 0, false, {St(0, 3), St(1, 3), St(1, 3)}},
    {"DIVISION", true, 9, kArpDivNames, 0, false, {St(2, 9), St(4, 9), St(6, 9)}}, // 1/4 1/8 1/16
    {"LENGTH", true, 16, nullptr, 1, false, {St(7, 16), St(7, 16), St(5, 16)}},    // 8 8 6
    {"GATE", true, 0, nullptr, 0, false, {0.5f, 0.5f, 0.5f}},
    {"DENSITY", true, 0, nullptr, 0, false, {1.f, 1.f, 0.6f}},
    {"VARY", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"RATCHET", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"ATTACK", true, 0, nullptr, 0, false, {0.083f, 0.083f, 0.083f}},  // 2 ms
    {"DECAY", true, 0, nullptr, 0, false, {0.656f, 0.585f, 0.536f}},   // 400 250 180 ms
    {"SUSTAIN", true, 0, nullptr, 0, false, {0.4f, 0.f, 0.f}},
    {"ATTACK", true, 0, nullptr, 0, false, {0.083f, 0.083f, 0.083f}},
    {"DECAY", true, 0, nullptr, 0, false, {0.585f, 0.585f, 0.585f}},
    {"AMOUNT", true, 0, nullptr, 0, false, {0.5f, 0.5f, 0.5f}},          // filter env off (simple start)
    {"AMOUNT", true, 0, nullptr, 0, false, {0.5f, 0.5f, 0.5f}},          // pitch env off
    {"DECAY", true, 0, nullptr, 0, false, {0.3f, 0.3f, 0.3f}},
    {"GLIDE", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"CUTOFF", true, 0, nullptr, 0, false, {0.54f, 0.642f, 0.72f}},  // 1.2 2 3 kHz
    {"RESO", true, 0, nullptr, 0, false, {0.3f, 0.2f, 0.2f}},
    {"TYPE", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},           // lowpass
    {"DRIVE", false, 0, nullptr, 0, false, {0.f}},                       // clean (simple start)
    {"RATE", true, 0, nullptr, 0, false, {0.4f, 0.4f, 0.4f}},        // LFO PITCH
    {"DEPTH", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"WAVE", true, 6, kWaveNames, 0, false, {St(0, 6), St(0, 6), St(0, 6)}},
    {"RATE", true, 0, nullptr, 0, false, {0.218f, 0.271f, 0.31f}},   // LFO CUTOFF: 0.09 0.13 0.17 Hz
    {"DEPTH", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},              // off (simple start)
    {"WAVE", true, 6, kWaveNames, 0, false, {St(0, 6), St(0, 6), St(0, 6)}},
    {"RATE", true, 0, nullptr, 0, false, {0.4f, 0.4f, 0.4f}},        // LFO AMP
    {"DEPTH", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"WAVE", true, 6, kWaveNames, 0, false, {St(0, 6), St(0, 6), St(0, 6)}},
    {"RATE", true, 0, nullptr, 0, false, {0.4f, 0.4f, 0.4f}},        // LFO PAN
    {"DEPTH", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"WAVE", true, 6, kWaveNames, 0, false, {St(0, 6), St(0, 6), St(0, 6)}},
    {"RATE", true, 0, nullptr, 0, false, {0.4f, 0.4f, 0.4f}},        // LFO SHAPE
    {"DEPTH", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},
    {"WAVE", true, 6, kWaveNames, 0, false, {St(0, 6), St(0, 6), St(0, 6)}},
    {"DIVISION", true, 11, kDelDivNames, 0, false, {St(5, 11), St(6, 11), St(4, 11)}}, // 1/8. 1/4 1/8T
    {"FEEDBACK", true, 0, nullptr, 0, false, {0.356f, 0.356f, 0.356f}},  // gain 0.4
    {"SEND", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},               // delays off (simple start)
    {"TONE", true, 0, nullptr, 0, false, {0.654f, 0.654f, 0.654f}},      // 4 kHz
    {"WOBBLE", true, 0, nullptr, 0, false, {0.3f, 0.3f, 0.3f}},
    {"WIDTH", true, 0, nullptr, 0, false, {1.f, 1.f, 1.f}},              // ping-pong
    {"PAN", true, 0, nullptr, 0, false, {0.5f, 0.3f, 0.7f}},
    {"WIDTH", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},              // mono per osc (simple start)
    {"REV SEND", true, 0, nullptr, 0, false, {0.f, 0.f, 0.f}},           // reverb off (simple start)
    {"SIZE", false, 0, nullptr, 0, false, {0.75f}},                      // feedback 0.91
    {"PROG", false, 9, kProgNames, 0, false, {St(0, 9)}},
    {"CHANGE", false, 4, kChangeNames, 0, false, {St(1, 4)}},
    {"CHORD", false, 7, kRoman, 0, false, {St(0, 7)}},
    {"RESTART", false, 3, kRestartNames, 0, false, {St(0, 3)}},
    {"OCTAVE", false, 5, nullptr, -2, true, {St(2, 5)}},
    {"FINE", false, 0, nullptr, 0, false, {0.5f}},
    {"LFO SYNC", false, 2, kSyncNames, 0, false, {St(0, 2)}},
    {"DAMPING", false, 0, nullptr, 0, false, {0.22f}},                   // 6.9 kHz
    {"PREDELAY", false, 0, nullptr, 0, false, {0.f}},
    {"VIEW", false, 4, kViewNames, 0, false, {St(1, 4)}}, // PITCH (user 2026-09-24)
    {"VOLUME", false, 0, nullptr, 0, false, {0.5f}},
}; // names <= 8 chars: the page maps list them at scale 2

/* ----------------------------------------------------------------- pages */
// A slot is (func, osc) packed as func | osc << 8. On a column page the
// three columns are the three oscillators (COLS expands one function per
// row). Knob 10 is a function, or kSubSel: the sub-page selector.
constexpr int kMaxSubs = 5;
constexpr int kSubSel  = -2;
#define O(f, o) ((f) | ((o) << 8))
#define COLS(a, b, c) O(a, 0), O(a, 1), O(a, 2), O(b, 0), O(b, 1), O(b, 2), O(c, 0), O(c, 1), O(c, 2)
struct PageDef
{
    const char* name;
    const char* selName; // knob 10's name when it selects sub-pages
    int         numSubs;
    const char* subNames[kMaxSubs];
    int         slots[kMaxSubs][9]; // pots 0..8
    int         knob10;             // a Func, or kSubSel
    bool        columns;
};
enum
{
    kPgMain = 0,
    kPgOsc,
    kPgArp,
    kPgEnv,
    kPgFilter,
    kPgLfo,
    kPgDelay,
    kPgMix,
    kPgKey
};
static const PageDef kPages[kNumPages] = {
    {"MAIN", "", 1, {""},
     {{O(LEVEL, 0), O(LEVEL, 1), O(LEVEL, 2), ROOT, SCALE, TEMPO, MCUTOFF, MDELAY, MREVERB}},
     SWING, false},
    {"OSC", "", 1, {""}, {{COLS(SHAPE, OCTAVE, DEGREE)}}, DETUNE, true},
    {"ARP", "SECTION", 3, {"NOTES", "RHYTHM", "CHANCE"},
     {{COLS(AMODE, APOOL, ARANGE)}, {COLS(ADIV, ALENGTH, AGATE)}, {COLS(ADENSITY, AVARY, ARATCHET)}},
     kSubSel, true},
    {"ENVELOPE", "SECTION", 3, {"AMP", "FILTER", "PITCH"},
     {{COLS(EAATT, EADEC, EASUS)}, {COLS(EFATT, EFDEC, EFAMT)}, {COLS(EPAMT, EPDEC, GLIDE)}},
     kSubSel, true},
    {"FILTER", "", 1, {""}, {{COLS(CUTOFF, RESO, FTYPE)}}, DRIVE, true},
    {"LFO", "TARGET", kLfoTargets, {"PITCH", "CUTOFF", "AMP", "PAN", "SHAPE"},
     {{COLS(LPITCH_R, LPITCH_D, LPITCH_W)},
      {COLS(LCUT_R, LCUT_D, LCUT_W)},
      {COLS(LAMP_R, LAMP_D, LAMP_W)},
      {COLS(LPAN_R, LPAN_D, LPAN_W)},
      {COLS(LSHAPE_R, LSHAPE_D, LSHAPE_W)}},
     kSubSel, true},
    {"DELAY", "SECTION", 2, {"TIME", "COLOR"},
     {{COLS(DDIV, DFEED, DSEND)}, {COLS(DTONE, DWOBBLE, DWIDTH)}}, kSubSel, true},
    {"MIX", "", 1, {""}, {{COLS(PAN, WIDTH, RSEND)}}, RSIZE, true},
    {"KEY", "", 1, {""}, {{PROG, CHANGE, CHORD, RESTART, KOCT, FINE, LFOSYNC, RDAMP, RPRE}}, VIEW,
     false},
};
#undef COLS
#undef O

static inline int SlotFunc(int code)
{
    return code & 0xFF;
}
static inline int SlotOsc(int code)
{
    return code >> 8;
}

/* ------------------------------------------------------------ parameters */
static std::atomic<float> params_[kNumFuncs][kOscs]; // engine inputs (any thread)
static float              smooth_[kNumFuncs][kOscs]; // audio-thread smoothed copy
static int                step_[kNumFuncs][kOscs];   // stepped functions, audio thread
static float              samplerate_ = 48000.f;

// UI-thread state of the panel. A pot's reading only reaches its slot once
// the pot has been MOVED since its page / sub-page was chosen (pickup),
// except the very first reading after Init, which is adopted so the panel
// defines the boot state.
constexpr float kPickup   = 0.02f;  // move needed to pick a slot up after a page change
constexpr float kDeadband = 0.006f; // move needed to count as a real move (ADC noise)
static float            pot_[kNumPots];     // last reading, -1 = none yet
static float            potRef_[kNumPots];  // reading when the page was chosen
static bool             picked_[kNumPots];  // moved since the page was chosen
static int              page_ = -1;         // active page, -1 until pot 10 reads
static int              sub_[kNumPages];    // chosen sub-page per page
static float            subSel_[kNumPages]; // knob 10 reading that chose it
static std::atomic<int> activePot_;         // last pot that really moved
static float            lastShown_[kNumPots]; // readout deadband memory

/** Stepped pot: p (0..1) into `steps` positions, with hysteresis around the
 *  boundaries so noise never flickers between two steps. `cur` is the
 *  current step (-1 = none yet). `hystFrac` is the margin as a fraction of
 *  one step's travel. */
static int StepWithHyst(float p, int steps, int cur, float hystFrac = 0.12f)
{
    int cand = (int)(p * (steps - 0.001f));
    if(cand > steps - 1)
        cand = steps - 1;
    if(cand < 0)
        cand = 0;
    if(cur < 0 || cand == cur)
        return cand;
    // Hysteresis: cross the boundary by a margin before switching.
    const float kHyst    = hystFrac / (float)steps;
    float       boundary = (cand > cur ? (float)cand : (float)(cand + 1)) / steps;
    if(cand > cur && p < boundary + kHyst)
        return cur;
    if(cand < cur && p > boundary - kHyst)
        return cur;
    return cand;
}

/* ---------------------------------------------------------------- voices */
/** One phase, three waveforms: SHAPE 0 = sine, 0.5 = square, 1 = saw
 *  (PolyBLEP on the square and saw edges, same formula as DaisySP's
 *  WAVE_POLYBLEP_SAW). */
struct MorphOsc
{
    float phase = 0.f; // 0..1
    float inc   = 0.f; // cycles per sample

    void SetFreq(float f, float samplerate) { inc = f / samplerate; }

    static inline float Blep(float t, float dt)
    {
        if(t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.f;
        }
        if(t > 1.f - dt)
        {
            t = (t - 1.f) / dt;
            return t * t + t + t + 1.f;
        }
        return 0.f;
    }

    inline float Process(float shape)
    {
        const float t = phase;
        phase += inc;
        if(phase >= 1.f)
            phase -= 1.f;
        float sq = (t < 0.5f ? 1.f : -1.f) + Blep(t, inc)
                   - Blep(t + (t < 0.5f ? 0.5f : -0.5f), inc);
        if(shape < 0.5f)
        {
            // Sine: parabola + correction (max error ~0.001), no sinf.
            float x  = t < 0.5f ? t : t - 1.f;                 // -0.5..0.5
            float p  = 16.f * x * (0.5f - (x < 0.f ? -x : x)); // +-1 at +-0.25
            float sn = p + 0.225f * (p * (p < 0.f ? -p : p) - p);
            float w  = shape * 2.f;
            return sn + (sq - sn) * w;
        }
        float saw = 2.f * t - 1.f - Blep(t, inc);
        float w   = (shape - 0.5f) * 2.f;
        return sq + (saw - sq) * w;
    }
};

enum
{
    kEnvRel = 0,
    kEnvAtt,
    kEnvDec,
    kEnvHold
};

/** One oscillator of the panel: two detuned MorphOscs, each with its own
 *  filter so WIDTH can pan them apart, an amp envelope and an arp. */
struct Voice
{
    MorphOsc     a, b;
    daisysp::Svf fa, fb;
    float        detuneScale; // per-voice detune weight
    float        det;         // detune ratio this block
    float        midi;        // current pitch, MIDI note (glides toward target)
    float        target;      // note it is heading for (the struck / drone note)
    // control-rate modulation (every kCtrl samples)
    float        glideSec;    // this block's glide time (0 = jump)
    float        pAmt;        // PITCH env amount, semitones
    float        penv;        // PITCH env level: 1 at a hit, decays to 0
    float        fenv;        // FILTER env level 0..1
    float        lfoPitch;    // PITCH LFO, semitones
    float        ampMul;      // AMP LFO gain (<= 1), ramped per sample across a tick
    float        ampStep;
    bool         fAttack;     // FILTER env rising
    float        gLA, gRA, gLB, gRB; // pan gains of the two oscillators
    // amp envelope (release = decay)
    float        env;
    int          envStage;
    bool         arping;      // MODE is not OFF (else it drones, envelope bypassed)
    int          gateLeft;    // samples until the release, <= 0 = none
    // arp
    long long    lastStep;    // master-clock step index last handled
    float        pendNote;    // note of the step starting in this block
    int          pendGate;    // its gate (samples, per hit when ratcheted)
    int          pendRatchet; // hits in the step (1 = no ratchet)
    int          pendGap;     // samples between ratchet hits
    int          ratchetCount; // ratchet hits still to come
    int          ratchetIn;   // samples until the next one
    uint32_t     rng;
};
static Voice voices_[kOscs];
static std::atomic<float> oscNote_[kOscs]; // current pitch, for tests and colors

constexpr float kOscAmp     = 0.25f; // an oscillator into the drive
constexpr float kMixGain    = 1.2f;
constexpr float kDroneGlideSec = 0.37f; // drones glide at least this long (99 %) on chord changes
constexpr int   kCtrl          = 16;    // samples per control tick: pitch, filter, envelopes

/* ------------------------------------------------------------------- arp */
// One master clock (beat_, in quarter notes since Init) drives all three
// arps. A step index is derived from the beat position and the osc's
// DIVISION, with SWING stretching even steps and shrinking odd ones, so
// the three grids can never drift apart. The pattern position is the step
// index mod LENGTH; DENSITY spreads its hits evenly over LENGTH (Euclidean).
static double beat_ = 0.0;
static const float kArpDivBeats[9]
    = {4.f, 2.f, 1.f, 2.f / 3.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f};
enum
{
    kArpOff = 0,
    kArpLoop,
    kArpUp,
    kArpDown,
    kArpUpDown,
    kArpRandom
};
enum
{
    kPoolKey = 0,
    kPoolTriad,
    kPoolSeventh,
    kPoolFifths,
    kPoolRoot
};
constexpr int kPoolMax = 8 * 3;
constexpr float kEnvAttMin = 0.001f, kEnvAttOct = 12.f;  // 1 ms .. 4 s
constexpr float kEnvDecMin = 0.005f, kEnvDecOct = 9.64f; // 5 ms .. 4 s
constexpr float kPitchDecOct = 7.64f;                    // PITCH env decay 5 ms .. 1 s
constexpr float kEnvFiltOct  = 4.f;                      // FILTER env AMOUNT: +-4 octaves

/** Step index of master-clock position `beat` for a step of `d` beats with
 *  swing `s` (0..1/3): even steps last d(1+s), odd ones d(1-s). */
static long long StepIndex(double beat, double d, double s)
{
    const long long pair = (long long)floor(beat / (2.0 * d));
    const double    t    = beat - (double)pair * 2.0 * d;
    return 2 * pair + (t >= d * (1.0 + s) ? 1 : 0);
}
static double StepStart(long long k, double d, double s)
{
    return (double)(k / 2) * 2.0 * d + ((k & 1) ? d * (1.0 + s) : 0.0);
}
static double StepBeats(long long k, double d, double s)
{
    return (k & 1) ? d * (1.0 - s) : d * (1.0 + s);
}

static inline uint32_t NextRand(uint32_t& r)
{
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r;
}
static inline float Rand01(uint32_t& r)
{
    return (float)(NextRand(r) >> 8) * (1.f / 16777216.f);
}

/* ------------------------------------------------------------------- lfo */
// One LFO per (target, osc): PITCH CUTOFF AMP PAN SHAPE x 3, each with
// RATE, DEPTH and WAVE, run on the control tick. FREE: RATE is 0.02..20 Hz
// and the phase runs on its own. TEMPO (KEY LFO SYNC): RATE picks a
// division and the phase is read off the master clock, so LFOs line up
// with the bar and with each other.
enum
{
    kLfoPitch = 0,
    kLfoCut,
    kLfoAmp,
    kLfoPan,
    kLfoShape
};
enum
{
    kWaveSine = 0,
    kWaveTri,
    kWaveSaw,
    kWaveSquare,
    kWaveSH,
    kWaveDrift
};
constexpr int kNumLfoDivs = 9;
static const float kLfoDivBeats[kNumLfoDivs]
    = {16.f, 8.f, 4.f, 2.f, 1.f, 2.f / 3.f, 0.5f, 1.f / 3.f, 0.25f};
static const char* const kLfoDivNames[kNumLfoDivs]
    = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16"};
constexpr float kLfoRateOct = 9.966f; // FREE RATE: 0.02 Hz * 2^(p * 9.966) = 0.02..20 Hz
constexpr float kLfoPitchSt = 1.f;   // PITCH: +-1 semitone at DEPTH 1
constexpr float kLfoCutOct  = 3.f;   // CUTOFF: +-3 octaves
constexpr float kLfoPanAmt   = 0.5f;  // PAN: +-0.5 of the position (full L..R from centre)
constexpr float kLfoShapeAmt = 0.5f;  // SHAPE: +-0.5
struct Lfo
{
    float    phase; // 0..1
    float    cur;   // S+H / DRIFT: this cycle's random value
    float    prev;  // DRIFT: the last one (it glides from prev to cur)
    uint32_t rng;
};
static Lfo lfo_[kLfoTargets][kOscs];
static int lfoDiv_[kLfoTargets][kOscs]; // TEMPO: division step (hysteresis)

/** Sine without trig (the MorphOsc parabola), phase 0..1, +-1. */
static inline float ParaSine(float t)
{
    const float x = t < 0.5f ? t : t - 1.f;
    const float p = 16.f * x * (0.5f - (x < 0.f ? -x : x));
    return p + 0.225f * (p * (p < 0.f ? -p : p) - p);
}

/** Move an LFO to `phase`; a wrap starts a new random cycle. Returns -1..1. */
static float LfoAt(Lfo& l, float phase, int wave)
{
    if(phase < l.phase) // wrapped
    {
        l.prev = l.cur;
        l.cur  = Rand01(l.rng) * 2.f - 1.f;
    }
    l.phase       = phase;
    const float p = phase;
    switch(wave)
    {
        case kWaveTri: return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p;
        case kWaveSaw: return 2.f * p - 1.f;
        case kWaveSquare: return p < 0.5f ? 1.f : -1.f;
        case kWaveSH: return l.cur;
        case kWaveDrift: return l.prev + (l.cur - l.prev) * p * p * (3.f - 2.f * p);
        default: return ParaSine(p);
    }
}
constexpr float kCutoffOct  = 7.23f; // CUTOFF: 80 Hz .. 12 kHz
constexpr float kMasterCutOct = 3.f; // master CUTOFF: +-3 octaves
constexpr float kDetuneCents  = 20.f;

/* ----------------------------------------------------------------- scope */
// The audio thread drops each oscillator's output (post filter and level,
// pre pan) into its own ring; the scope view draws the newest 25 ms of
// each, colored by that oscillator's note.
constexpr int   kScopeSize   = 4096; // power of two, ~85 ms at 48 kHz
constexpr int   kScopeMask   = kScopeSize - 1;
constexpr int   kScopeWindow = 1200; // samples shown across the 240 px
constexpr float kScopeGainPx = 90.f; // px per unit
constexpr int   kScopeTraces = kOscs + 1; // + the final output (mono)
constexpr int   kScopeOutput = kOscs;
static float            scope_[kScopeTraces][kScopeSize];
static std::atomic<int> scopeWrite_;
static std::atomic<int> moveCount_; // bumped by SetPot on real moves
static int              lastMove_   = 0;
static int              idleFrames_ = 0;
constexpr int kIdleStart  = 66; // frames (~2.5 s at 26 fps, FAST_DISPLAY_SPI) before the readout fades
constexpr int kFadeFrames = 20; // crossfade length in frames (~0.75 s)

/* ---------------------------------------------------------------- reverb */
static Buffers  fx_;
constexpr float kReverbReturn  = 0.5f;    // wet level at send 1
constexpr float kReverbFbMin   = 0.7f;    // SIZE 0
constexpr float kReverbFbMax   = 0.98f;   // SIZE 1
constexpr float kReverbLpHiHz  = 12000.f; // DAMPING 0
constexpr float kReverbLpLoHz  = 1000.f;  // DAMPING 1
constexpr float kReverbPreMax  = 0.25f;   // s at PREDELAY 1
static float    preTime_       = 1.f;     // pre-delay read tap (samples), slewed

/* ----------------------------------------------------------------- delays */
// One stereo delay per oscillator (the reference loop, after its rework):
// the loop runs at half level (kDelaySend) into a soft clip and the return
// is made up 2x; a one-pole lowpass (TONE) and a 20 Hz DC blocker sit in
// the loop. WIDTH 0 keeps the oscillator's own stereo image in parallel
// L / R lines; WIDTH 1 is ping-pong (mono in on L, L feeds R, R feeds L).
// The time is a TEMPO division, slewed in the log domain so tempo changes
// bend like tape; WOBBLE adds a slow random wander per side.
static const float kDelDivBeats[11]
    = {0.125f, 0.25f, 1.f / 6.f, 0.5f, 1.f / 3.f, 0.75f, 1.f, 2.f / 3.f, 1.5f, 2.f, 4.f};
constexpr float kDelaySend     = 0.5f;  // loop level: 2x headroom before the clip
constexpr float kDelayHpHz     = 20.f;  // DC blocker only
constexpr float kDelaySlewSec  = 0.08f;
constexpr float kDelayToneLoHz = 500.f, kDelayToneOct = 4.585f; // 500 Hz .. 12 kHz
constexpr float kWobbleMs      = 3.f;
constexpr float kWobbleRevertSec = 4.f, kWobbleSmoothSec = 0.3f;
constexpr float kDelayMinSec   = 0.02f;
constexpr float kDelayMaxSec   = (float)kDelayMaxSamples / 48000.f - 0.01f;
struct Delay
{
    float    logT;         // slewed log(delay seconds)
    float    tL, tR;       // read taps (samples), ramped per sample
    float    dampL, dampR; // TONE lowpass state
    float    dcL, dcR;     // DC blocker state
    float    walk[2];      // WOBBLE random walks L / R (-1..1)
    float    wob[2];       // smoothed walks
    uint32_t rng;
};
static Delay delays_[kOscs];

/** FEEDBACK knob -> loop gain: 0..0.8 of travel covers 0..0.9, the last
 *  fifth is stretched over 0.9..1.0 for fine control near endless. */
static inline float FeedbackGain(float p)
{
    return p < 0.8f ? p * (0.9f / 0.8f) : 0.9f + (p - 0.8f) * 0.5f;
}

/* ----------------------------------------------------------------- harmony */
// The key is ROOT (tonic) + SCALE (a scale or a chord as a 12-bit
// pitch-class mask, bit 0 = tonic). The current chord is a degree of the
// scale (KEY page CHORD; the progression drives it from step 3). Every
// oscillator's DEGREE counts scale steps up from the chord root. For a
// chord key the chord keeps its quality and moves along its parent scale
// (natural minor if it has a minor third, else major); DEGREE then counts
// chord tones.
static int keyIv_[12]; // scale key: the scale as intervals above the tonic
static int keyN_ = 7;
static int chordDeg_ = 0;
static bool chordKey_ = false;
static int chordRootSemis_ = 0; // chord root, semitones above the tonic
static int chordIv_[12];        // chord key: the chord's intervals
static int chordN_ = 3;

static int MaskIntervals(int mask, int* out)
{
    int n = 0;
    for(int i = 0; i < 12; i++)
        if((mask >> i) & 1)
            out[n++] = i;
    return n;
}

static void UpdateHarmony(int key, int deg)
{
    chordDeg_ = deg;
    chordKey_ = key >= kNumKeySets;
    if(!chordKey_)
    {
        keyN_           = MaskIntervals(kScaleMask[key], keyIv_);
        chordRootSemis_ = keyIv_[deg % keyN_] + 12 * (deg / keyN_);
        return;
    }
    const int cm    = kChordMask[key - kNumKeySets];
    const bool minor3 = ((cm >> 3) & 1) && !((cm >> 4) & 1);
    keyN_           = MaskIntervals(kScaleMask[minor3 ? 1 : 0], keyIv_); // parent scale
    chordRootSemis_ = keyIv_[deg % keyN_] + 12 * (deg / keyN_);
    chordN_         = MaskIntervals(cm, chordIv_);
}

/** Semitones above the tonic of the note `d` steps up from the chord root:
 *  scale steps in a scale key, chord tones in a chord key. */
static int DegreeSemis(int d)
{
    if(chordKey_)
        return chordRootSemis_ + chordIv_[d % chordN_] + 12 * (d / chordN_);
    const int k = chordDeg_ + d;
    return keyIv_[k % keyN_] + 12 * (k / keyN_);
}

constexpr int kMidiBase = 36; // C2: base octave of OCTAVE 0

/* ------------------------------------------------------------ progression */
// PROG presets as scale degrees (0 = I), so each works in any ROOT and
// SCALE. The chord changes every CHANGE (in beats) on the master clock,
// counted from beat 0, so changes land on bar lines.
struct Prog
{
    int n;
    int deg[8];
};
static const Prog kProgs[9] = {
    {0, {0}},                      // OFF: KEY page CHORD
    {4, {0, 4, 5, 3}},             // POP    I V vi IV
    {4, {0, 5, 3, 4}},             // 50S    I vi IV V
    {4, {5, 3, 0, 4}},             // SAD    vi IV I V
    {4, {0, 5, 2, 6}},             // EPIC   i VI III VII (in minor)
    {4, {1, 4, 0, 0}},             // JAZZ   ii V I I
    {2, {0, 3}},                   // VAMP   I IV
    {8, {0, 4, 5, 2, 3, 0, 3, 4}}, // CANON  I V vi iii IV I IV V
    {4, {0, 6, 5, 4}},             // FALL   i VII VI v (in minor)
};
static const float kChangeBeats[4] = {2.f, 4.f, 8.f, 16.f}; // 1/2, 1, 2, 4 bars
constexpr double kBeatEps = 1e-7; // a step starting exactly on a bar line belongs to it
enum
{
    kRestartFree = 0,
    kRestartBar,
    kRestartChord
};

/** The chord degree at master-clock position `beat` (PROG, else CHORD). */
static int ChordAt(double beat, int* progPos = nullptr)
{
    const Prog& pr = kProgs[step_[PROG][0]];
    if(pr.n == 0)
    {
        if(progPos)
            *progPos = -1;
        return step_[CHORD][0];
    }
    const double    cb  = (double)kChangeBeats[step_[CHANGE][0]];
    const long long idx = (long long)floor(beat / cb) % pr.n;
    if(progPos)
        *progPos = (int)idx;
    return pr.deg[idx];
}

/** Beat where the pattern of a step starting at `beat` began: 0 (FREE),
 *  the bar line (BAR) or the chord change (CHORD, every CHANGE). */
static double RestartBeat(double beat)
{
    switch(step_[RESTART][0])
    {
        case kRestartBar: return floor(beat / 4.0) * 4.0;
        case kRestartChord:
        {
            const double cb = (double)kChangeBeats[step_[CHANGE][0]];
            return floor(beat / cb) * cb;
        }
        default: return 0.0;
    }
}

// What the centre of the screen shows (written by the audio thread).
enum
{
    kQualMaj = 0,
    kQualMin,
    kQualDim,
    kQualAug,
    kQualSus
};
static std::atomic<int> dispRootPc_;  // chord root pitch class
static std::atomic<int> dispDeg_;     // chord degree 0..6
static std::atomic<int> dispQual_;    // scale key: kQual*, chord key: 100 + chord index
static std::atomic<int> dispProgPos_; // -1 = PROG OFF
static std::atomic<int> dispProgLen_;
static std::atomic<int> dispBeat_;    // beat in the bar, 0..3
static std::atomic<int> dispKeyMask_;   // key notes, absolute pitch classes (bit 0 = C)
static std::atomic<int> dispChordMask_; // current chord's tones, absolute
// Step rings: what each oscillator's pattern played, per position.
constexpr int kRingMax = 16;
enum
{
    kRingUnplayed = -2,
    kRingRest     = -1 // DENSITY skipped the step
};
static std::atomic<int>   ringLen_[kOscs]; // LENGTH, 0 = droning
static std::atomic<int>   ringPos_[kOscs]; // pattern position playing now
static int8_t             ringNote_[kOscs][kRingMax];    // MIDI note or kRing*
static uint8_t            ringRatchet_[kOscs][kRingMax]; // hit more than once
static std::atomic<float> oscHz_[kOscs];                 // sounding frequency (scope period)
static std::atomic<float> oscLvl_[kOscs];                // LEVEL gain (the rings divide it out)

/** Triad quality on the current chord root of a scale key. */
static int TriadQuality()
{
    const int r  = DegreeSemis(0);
    const int t3 = DegreeSemis(2) - r;
    const int t5 = DegreeSemis(4) - r;
    if(t3 == 4)
        return t5 == 8 ? kQualAug : kQualMaj;
    if(t3 == 3)
        return t5 == 6 ? kQualDim : kQualMin;
    return kQualSus; // a gapped scale (penta, blues): no third on this degree
}

/** In-key fifth above the note `d` steps up from the chord root: the key
 *  note whose distance is closest to a perfect fifth (the diatonic fifth of
 *  a 7-note scale; B -> F in C major). Semitones above that note. */
static int KeyFifth(int d)
{
    const int r = DegreeSemis(d);
    const int n = chordKey_ ? chordN_ : keyN_;
    static const int kPref[] = {7, 8, 6, 5, 9, 4, 10};
    for(int k = 0; k < 7; k++)
        for(int j = 1; j < n; j++)
            if(DegreeSemis(d + j) - r == kPref[k])
                return kPref[k];
    return 12;
}

/** Oscillator o's arp pool: POOL type from its DEGREE on the current chord,
 *  repeated over RANGE octaves, as semitones above the tonic, ascending.
 *  KEY = every key note; TRIAD / 7TH = stacked diatonic thirds (chord
 *  tones in a chord key); FIFTHS = root + in-key fifth; ROOT = octaves. */
static int BuildPool(int o, int* out)
{
    const int d0 = step_[DEGREE][o];
    const int n  = chordKey_ ? chordN_ : keyN_;
    const int th = chordKey_ ? 1 : 2; // a third: two scale steps, one chord tone
    int       base[12], nb = 0;
    switch(step_[APOOL][o])
    {
        case kPoolTriad:
        case kPoolSeventh:
        {
            const int cnt = step_[APOOL][o] == kPoolTriad ? 3 : 4;
            for(int i = 0; i < cnt; i++)
                base[nb++] = DegreeSemis(d0 + i * th);
            break;
        }
        case kPoolFifths:
            base[nb++] = DegreeSemis(d0);
            base[nb++] = base[0] + KeyFifth(d0);
            break;
        case kPoolRoot: base[nb++] = DegreeSemis(d0); break;
        default:
            for(int i = 0; i < n; i++)
                base[nb++] = DegreeSemis(d0 + i);
            break;
    }
    const int range = step_[ARANGE][o] + 1;
    int       k     = 0;
    for(int oc = 0; oc < range; oc++)
        for(int i = 0; i < nb && k < kPoolMax; i++)
            out[k++] = base[i] + 12 * oc;
    return k;
}

/* ------------------------------------------------------------------ init */
void Init(float samplerate, const Buffers& buffers)
{
    samplerate_ = samplerate;
    fx_         = buffers;
    for(int f = 0; f < kNumFuncs; f++)
        for(int o = 0; o < kOscs; o++)
        {
            const float d = kFuncs[f].def[kFuncs[f].perOsc ? o : 0];
            params_[f][o].store(d);
            smooth_[f][o] = d;
            step_[f][o]   = -1;
        }
    for(int i = 0; i < kNumPots; i++)
    {
        pot_[i]       = -1.f;
        potRef_[i]    = -1.f;
        picked_[i]    = false;
        lastShown_[i] = -1.f;
    }
    page_ = -1;
    for(int p = 0; p < kNumPages; p++)
    {
        sub_[p]    = 0;
        subSel_[p] = St(0, kPages[p].numSubs);
    }
    activePot_.store(kPagePot);
    for(int t = 0; t < kScopeTraces; t++)
        for(int i = 0; i < kScopeSize; i++)
            scope_[t][i] = 0.f;
    scopeWrite_.store(0);
    moveCount_.store(0);
    lastMove_   = 0;
    idleFrames_ = 0;

    const float kDetScale[kOscs] = {1.0f, 0.8f, 1.25f};
    for(int o = 0; o < kOscs; o++)
    {
        Voice& vc = voices_[o];
        // Start phases a quarter cycle apart (never a half: two sines half
        // a cycle apart cancel at DETUNE 0), different per oscillator.
        vc.a.phase     = 0.37f * (float)o;
        vc.b.phase     = vc.a.phase + 0.23f;
        vc.detuneScale = kDetScale[o];
        vc.midi        = -1.f; // jump to the first note
        vc.target      = -1.f;
        vc.det         = 1.f;
        vc.glideSec    = 0.f;
        vc.pAmt        = 0.f;
        vc.penv        = 0.f;
        vc.fenv        = 0.f;
        vc.fAttack     = false;
        vc.lfoPitch    = 0.f;
        vc.ampMul      = 1.f;
        vc.ampStep     = 0.f;
        for(int t = 0; t < kLfoTargets; t++)
        {
            Lfo& l = lfo_[t][o];
            l.phase = (float)o / 3.f; // the oscillators start a third apart
            l.cur = l.prev = 0.f;
            l.rng  = 0x2545F491u * (uint32_t)(3 * t + o + 1);
            lfoDiv_[t][o] = -1;
        }
        vc.gLA = vc.gRA = vc.gLB = vc.gRB = 0.7071f;
        vc.env          = 0.f;
        vc.envStage     = kEnvRel; // an arp waits silent for its first hit
        vc.arping       = false;
        vc.gateLeft     = 0;
        vc.lastStep     = -1; // the first block starts step 0
        vc.pendNote     = 0.f;
        vc.pendGate     = 1;
        vc.pendRatchet  = 1;
        vc.pendGap      = 1;
        vc.ratchetCount = 0;
        vc.ratchetIn    = 0;
        vc.rng          = 0x9E3779B9u * (uint32_t)(o + 1);
        vc.fa.Init(samplerate);
        vc.fb.Init(samplerate);
        vc.fa.SetDrive(0.f);
        vc.fb.SetDrive(0.f);
        oscNote_[o].store(0.f);
        fx_.delayL[o]->Init();
        fx_.delayR[o]->Init();
        Delay& dl = delays_[o];
        dl.logT   = -1e9f; // snap to the first block's time
        dl.tL = dl.tR = 1.f;
        dl.dampL = dl.dampR = dl.dcL = dl.dcR = 0.f;
        dl.walk[0] = dl.walk[1] = dl.wob[0] = dl.wob[1] = 0.f;
        dl.rng = 0x68E31DA4u * (uint32_t)(o + 1);
    }
    UpdateHarmony(1, 0);
    beat_ = 0.0;
    dispRootPc_.store(0);
    dispDeg_.store(0);
    dispQual_.store(kQualMaj);
    dispProgPos_.store(-1);
    dispProgLen_.store(0);
    dispBeat_.store(0);
    dispKeyMask_.store(0);
    dispChordMask_.store(0);
    for(int o = 0; o < kOscs; o++)
    {
        ringLen_[o].store(0);
        ringPos_[o].store(0);
        oscHz_[o].store(110.f);
        oscLvl_[o].store(1.f);
        for(int i = 0; i < kRingMax; i++)
        {
            ringNote_[o][i]    = kRingUnplayed;
            ringRatchet_[o][i] = 0;
        }
    }

    fx_.reverb->Init(samplerate);
    fx_.reverb->SetFeedback(kReverbFbMin);
    fx_.reverb->SetLpFreq(kReverbLpHiHz);
    fx_.preL->Init();
    fx_.preR->Init();
    preTime_ = 1.f;
}

/* ----------------------------------------------------------------- panel */
static void NoteMove(int pot, float v)
{
    // Deadband beats ADC noise: only a real move claims the screen. The
    // first reading after Init just sets the reference.
    if(lastShown_[pot] < 0.f)
    {
        lastShown_[pot] = v;
        return;
    }
    if(fabsf(v - lastShown_[pot]) > kDeadband)
    {
        lastShown_[pot] = v;
        activePot_.store(pot);
        moveCount_.fetch_add(1);
    }
}

static int PageIndex()
{
    return page_ < 0 ? 0 : page_;
}

static bool HasSubs(int page)
{
    return kPages[page].numSubs > 1;
}

/** Nothing changes until these pots move again (pickup re-armed). */
static void Rearm(int count)
{
    for(int i = 0; i < count; i++)
    {
        potRef_[i] = pot_[i];
        picked_[i] = false;
    }
}

/** The (func, osc) code a pot drives right now, or kSubSel / -1. */
static int SlotCode(int pot)
{
    const int pg = PageIndex();
    if(pot == kSubPot)
        return kPages[pg].knob10;
    if(pot < 0 || pot > 8)
        return -1;
    return kPages[pg].slots[sub_[pg]][pot];
}

void SetPot(int pot, float v)
{
    if(pot < 0 || pot >= kNumPots)
        return;
    if(v < 0.f)
        v = 0.f;
    if(v > 1.f)
        v = 1.f;
    const bool first = pot_[pot] < 0.f;
    pot_[pot]        = v;

    if(pot == kPagePot)
    {
        int s = StepWithHyst(v, kNumPages, page_);
        if(s != page_)
        {
            page_ = s; // new page: nothing changes until a pot moves again
            Rearm(kNumPots);
        }
        NoteMove(pot, v);
        return;
    }
    if(pot == kVolumePot)
    {
        params_[VOLUME][0].store(v);
        NoteMove(pot, v);
        return;
    }

    // Pots 0..9: pickup. The first reading ever is adopted (boot); after
    // a page or sub-page change the pot must move kPickup from where it was.
    if(!first && !picked_[pot])
    {
        if(potRef_[pot] >= 0.f && fabsf(v - potRef_[pot]) <= kPickup)
            return;
    }
    picked_[pot] = true;
    const int pg = PageIndex();
    const int code = SlotCode(pot);
    if(code == kSubSel)
    {
        subSel_[pg] = v;
        int s       = StepWithHyst(v, kPages[pg].numSubs, sub_[pg]);
        if(s != sub_[pg])
        {
            sub_[pg] = s; // new sub-page: re-arm knobs 1-9
            Rearm(kSubPot);
        }
    }
    else if(code >= 0)
        params_[SlotFunc(code)][SlotOsc(code)].store(v);
    NoteMove(pot, v);
}

float GetPot(int pot)
{
    if(pot < 0 || pot >= kNumPots)
        return 0.f;
    if(pot_[pot] >= 0.f)
        return pot_[pot];
    return GetSlotValue(pot); // not read yet: where it "should" be
}

int GetPage()
{
    return PageIndex();
}

const char* GetPageName(int page)
{
    return page >= 0 && page < kNumPages ? kPages[page].name : "";
}

int GetNumSubs(int page)
{
    return page >= 0 && page < kNumPages ? kPages[page].numSubs : 1;
}

int GetSub(int page)
{
    return page >= 0 && page < kNumPages ? sub_[page] : 0;
}

const char* GetSubName(int page, int sub)
{
    if(page < 0 || page >= kNumPages || !HasSubs(page) || sub < 0 || sub >= kPages[page].numSubs)
        return "";
    return kPages[page].subNames[sub];
}

float GetSlotValue(int pot)
{
    if(pot == kPagePot)
        return pot_[pot] >= 0.f ? pot_[pot] : 0.f;
    if(pot == kVolumePot)
        return params_[VOLUME][0].load();
    const int code = SlotCode(pot);
    if(code == kSubSel)
        return subSel_[PageIndex()];
    if(code < 0)
        return 0.f;
    return params_[SlotFunc(code)][SlotOsc(code)].load();
}

const char* GetSlotName(int pot)
{
    if(pot == kPagePot)
        return "PAGE";
    if(pot == kVolumePot)
        return kFuncs[VOLUME].name;
    const int code = SlotCode(pot);
    if(code == kSubSel)
        return kPages[PageIndex()].selName;
    return code < 0 ? "" : kFuncs[SlotFunc(code)].name;
}

int GetSlotOsc(int pot)
{
    const int code = (pot == kPagePot || pot == kVolumePot) ? -1 : SlotCode(pot);
    if(code < 0 || !kFuncs[SlotFunc(code)].perOsc)
        return 0;
    return SlotOsc(code) + 1;
}

void SetEngineParam(int func, int osc, float v)
{
    if(func < 0 || func >= kNumFuncs || osc < 0 || osc >= kOscs)
        return;
    v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    params_[func][osc].store(v);
    smooth_[func][osc] = v; // tests: no 20 ms slide from the old value
}

float GetOscNote(int osc)
{
    return osc >= 0 && osc < kOscs ? oscNote_[osc].load() : 0.f;
}

/* ----------------------------------------------------------------- audio */
static inline float SoftClip(float x)
{
    // Cheap symmetric saturator, unity slope at 0, limits to +-1.
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

static inline float Clamp01(float x)
{
    return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

/** Oscillator pitch = the (glided) note + the PITCH envelope + PITCH LFO. */
static inline void SetPitch(Voice& vc)
{
    const float f = 440.f * exp2f((vc.midi + vc.pAmt * vc.penv + vc.lfoPitch - 69.f) / 12.f);
    vc.a.SetFreq(f * vc.det, samplerate_);
    vc.b.SetFreq(f / vc.det, samplerate_);
}

/** An arp hit: head for the note (on this sample when GLIDE is 0),
 *  restart the amp envelope's attack and the FILTER envelope from their
 *  current levels (no click), kick the PITCH envelope, arm the gate. */
static void Strike(Voice& vc, float midi, int gate)
{
    vc.target = midi;
    if(vc.glideSec <= 0.f)
        vc.midi = midi;
    vc.penv    = 1.f;
    vc.fAttack = true;
    SetPitch(vc);
    vc.envStage = kEnvAtt;
    vc.gateLeft = gate;
}

/** One control tick of `dn` samples: glide, pitch, cutoff, then advance the
 *  PITCH and FILTER envelopes. */
static inline void ControlTick(Voice& vc, float dn, float cutOct, float fAmt, float fAttInc,
                               float fDecSec, float pDecSec, float fcMax)
{
    if(vc.glideSec <= 0.f)
        vc.midi = vc.target;
    else
        vc.midi += (1.f - expf(-4.6f * dn / (vc.glideSec * samplerate_))) * (vc.target - vc.midi);
    SetPitch(vc);
    float cut = 80.f * exp2f(cutOct + fAmt * vc.fenv);
    cut       = cut < 20.f ? 20.f : (cut > fcMax ? fcMax : cut);
    vc.fa.SetFreq(cut);
    vc.fb.SetFreq(cut);
    vc.penv *= expf(-4.6f * dn / (pDecSec * samplerate_));
    if(vc.fAttack)
    {
        vc.fenv += fAttInc * dn;
        if(vc.fenv >= 1.f)
        {
            vc.fenv    = 1.f;
            vc.fAttack = false;
        }
    }
    else
        vc.fenv *= expf(-4.6f * dn / (fDecSec * samplerate_));
}

void ProcessAudio(float* out, int nframes)
{
    // Block-rate smoothing with a fixed 20 ms time constant, so the feel is
    // identical at the sim's 256-frame blocks and the firmware's 48. Stepped
    // functions go through hysteresis here; the screen only reads them.
    const float coef = 1.f - expf(-(float)nframes / (samplerate_ * 0.02f));
    for(int f = 0; f < kNumFuncs; f++)
    {
        const int n = kFuncs[f].perOsc ? kOscs : 1;
        for(int o = 0; o < n; o++)
        {
            const float p = params_[f][o].load();
            smooth_[f][o] += coef * (p - smooth_[f][o]);
            if(kFuncs[f].steps > 0)
                step_[f][o] = StepWithHyst(p, kFuncs[f].steps, step_[f][o]);
        }
    }

    // --- master clock: beats since Init; each osc's grid is derived from it
    const float  bpm      = 40.f + 200.f * params_[TEMPO][0].load();
    const double bps      = (double)bpm / 60.0 / (double)samplerate_; // beats per sample
    const double beat0    = beat_;
    const double lastBeat = beat0 + bps * (double)(nframes - 1);
    beat_ += bps * (double)nframes;
    const double swing = (double)smooth_[SWING][0] / 3.0;

    // --- harmony: the chord now (drones, screen); an arp step asks for the
    //     chord at its own start, so a step on a bar line gets the new chord
    const int tonic = step_[ROOT][0];
    const int key   = step_[SCALE][0];
    int       progPos;
    const int chordNow = ChordAt(lastBeat, &progPos);
    UpdateHarmony(key, chordNow);
    dispRootPc_.store((tonic + chordRootSemis_) % 12);
    dispDeg_.store(chordNow % 7);
    dispQual_.store(chordKey_ ? 100 + key - kNumKeySets : TriadQuality());
    dispProgPos_.store(progPos);
    dispProgLen_.store(kProgs[step_[PROG][0]].n);
    dispBeat_.store((int)(lastBeat - floor(lastBeat / 4.0) * 4.0));
    {
        // Key and chord as absolute pitch-class sets (the WHEEL view).
        int km = 0, cm = 0;
        const int n = chordKey_ ? chordN_ : keyN_;
        for(int d = 0; d < n; d++)
            km |= 1 << ((tonic + DegreeSemis(d)) % 12);
        for(int d = 0; d < (chordKey_ ? chordN_ : 3); d++)
            cm |= 1 << ((tonic + DegreeSemis(chordKey_ ? d : 2 * d)) % 12);
        dispKeyMask_.store(chordKey_ ? cm : km);
        dispChordMask_.store(cm);
    }
    const float masterOct = (float)(step_[KOCT][0] - 2) * 12.f
                            + (smooth_[FINE][0] - 0.5f); // FINE: +-50 cents
    const float cents     = smooth_[DETUNE][0] * kDetuneCents;

    // --- per oscillator: drone (MODE OFF) or arp step; pitch
    int onsetAt[kOscs] = {-1, -1, -1};
    for(int o = 0; o < kOscs; o++)
    {
        Voice&      vc   = voices_[o];
        const int   mode = step_[AMODE][o];
        const float base = (float)(kMidiBase + tonic + 12 * (step_[OCTAVE][o] - 1)) + masterOct;
        const double d   = (double)kArpDivBeats[step_[ADIV][o]];
        const long long k = StepIndex(lastBeat, d, swing); // step holding the block's last sample
        if(mode != kArpOff && !vc.arping)
            vc.envStage = kEnvRel; // drone -> arp: fade at DECAY until the next step
        vc.arping = mode != kArpOff;
        // GLIDE: arp notes glide for GLIDE (99 %); drones glide at least
        // kDroneGlideSec on a chord change.
        vc.glideSec = smooth_[GLIDE][o];
        if(!vc.arping)
        {
            // Drone: the pool's first note (DEGREE on the chord), glided.
            const float target = base + (float)DegreeSemis(step_[DEGREE][o]);
            vc.target          = target;
            if(vc.glideSec < kDroneGlideSec)
                vc.glideSec = kDroneGlideSec;
            oscNote_[o].store(target);
            vc.gateLeft        = 0;
            vc.ratchetCount    = 0;
            vc.lastStep        = k; // switching the arp on starts at the next step
            ringLen_[o].store(0);
        }
        else if(k != vc.lastStep)
        {
            // A new step begins inside this block.
            vc.lastStep       = k;
            int at            = (int)ceil((StepStart(k, d, swing) - beat0) / bps);
            at                = at < 0 ? 0 : (at >= nframes ? nframes - 1 : at);
            // Pattern position: steps since the RESTART point (0, the bar
            // line or the chord change), mod LENGTH.
            const double    sb   = StepStart(k, d, swing) + kBeatEps;
            const long long k0   = StepIndex(RestartBeat(sb) + kBeatEps, d, swing);
            const int       len  = step_[ALENGTH][o] + 1;
            const int       pos  = (int)((k - k0) % len);
            const int       hits = (int)(smooth_[ADENSITY][o] * (float)len + 0.5f);
            ringLen_[o].store(len);
            ringPos_[o].store(pos);
            ringNote_[o][pos]    = kRingRest;
            ringRatchet_[o][pos] = 0;
            if((pos * hits) % len < hits) // Euclidean: hits spread evenly over LENGTH
            {
                int pool[kPoolMax];
                UpdateHarmony(key, ChordAt(sb)); // the chord this step starts in
                const int n = BuildPool(o, pool);
                UpdateHarmony(key, chordNow);
                int       idx;
                switch(mode)
                {
                    case kArpUp: idx = pos % n; break;
                    case kArpDown: idx = n - 1 - pos % n; break;
                    case kArpUpDown:
                    {
                        const int period = n > 1 ? 2 * n - 2 : 1;
                        const int m      = pos % period;
                        idx              = m < n ? m : period - m;
                        break;
                    }
                    case kArpRandom: idx = (int)(NextRand(vc.rng) % (uint32_t)n); break;
                    default: idx = 0; break; // LOOP: retrigger the pool's first note
                }
                if(Rand01(vc.rng) < smooth_[AVARY][o]) // VARY: a random pool note
                    idx = (int)(NextRand(vc.rng) % (uint32_t)n);
                const float stepSmp = (float)(StepBeats(k, d, swing) / bps);
                const int   r = Rand01(vc.rng) < smooth_[ARATCHET][o]
                                    ? 2 + (int)(NextRand(vc.rng) % 3u) // 2..4 hits
                                    : 1;
                const float gateFrac = 0.05f + 0.95f * smooth_[AGATE][o];
                vc.pendNote          = base + (float)pool[idx];
                vc.pendRatchet       = r;
                vc.pendGap           = (int)(stepSmp / (float)r);
                vc.pendGate          = (int)(gateFrac * stepSmp / (float)r);
                if(vc.pendGate < 1)
                    vc.pendGate = 1;
                onsetAt[o]           = at;
                ringNote_[o][pos]    = (int8_t)(int)floorf(vc.pendNote + 0.5f);
                ringRatchet_[o][pos] = r > 1;
            }
        }
        if(vc.midi < 0.f) // first block: start on the note, no glide in
            vc.midi = vc.target = base + (float)DegreeSemis(step_[DEGREE][o]);
        if(vc.arping) // (a drone reports the note it glides to, above)
            oscNote_[o].store(onsetAt[o] >= 0 ? vc.pendNote : vc.target);
        vc.det  = exp2f(cents * vc.detuneScale / 1200.f);
        vc.pAmt = floorf((smooth_[EPAMT][o] - 0.5f) * 24.f + 0.5f); // whole semitones
    }

    // --- amp envelope coefficients (release = decay)
    float envAtt[kOscs], envDec[kOscs], envSus[kOscs];
    for(int o = 0; o < kOscs; o++)
    {
        envAtt[o] = 1.f / (kEnvAttMin * exp2f(smooth_[EAATT][o] * kEnvAttOct) * samplerate_);
        envDec[o] = 1.f - expf(-4.6f / (kEnvDecMin * exp2f(smooth_[EADEC][o] * kEnvDecOct) * samplerate_));
        envSus[o] = smooth_[EASUS][o];
    }

    // --- tone: drive (level-compensated) -> two SVFs per oscillator
    const float gain   = 1.f + 7.f * smooth_[DRIVE][0]; // 1x..8x
    const float norm   = kOscAmp / SoftClip(gain * kOscAmp);
    const float master = (smooth_[MCUTOFF][0] - 0.5f) * 2.f * kMasterCutOct;
    const float fcMax  = samplerate_ * 0.33f;
    float       shape[kOscs], wL[kOscs], wB[kOscs], wH[kOscs], lvl[kOscs], send[kOscs];
    float       shapeNow[kOscs]; // SHAPE + SHAPE LFO, set on the control tick
    float       cutOct[kOscs], fAmt[kOscs], fAtt[kOscs], fDec[kOscs], pDec[kOscs];
    const float revMaster = smooth_[MREVERB][0] * 2.f;
    for(int o = 0; o < kOscs; o++)
    {
        Voice& vc = voices_[o];
        // FILTER env: AD, attack 1 ms..4 s linear, decay 5 ms..4 s to 0,
        // AMOUNT +-4 octaves on CUTOFF. PITCH env: DECAY 5 ms..1 s.
        cutOct[o] = smooth_[CUTOFF][o] * kCutoffOct + master;
        fAmt[o]   = (smooth_[EFAMT][o] - 0.5f) * 2.f * kEnvFiltOct;
        fAtt[o]   = 1.f / (kEnvAttMin * exp2f(smooth_[EFATT][o] * kEnvAttOct) * samplerate_);
        fDec[o]   = kEnvDecMin * exp2f(smooth_[EFDEC][o] * kEnvDecOct);
        pDec[o]   = kEnvDecMin * exp2f(smooth_[EPDEC][o] * kPitchDecOct);
        vc.fa.SetRes(smooth_[RESO][o] * 0.85f);
        vc.fb.SetRes(smooth_[RESO][o] * 0.85f);
        shape[o]      = Clamp01(smooth_[SHAPE][o]);
        const float t = Clamp01(smooth_[FTYPE][o]); // LP 0 .. BP 0.5 .. HP 1
        wL[o]         = t < 0.5f ? 1.f - 2.f * t : 0.f;
        wH[o]         = t > 0.5f ? 2.f * t - 1.f : 0.f;
        wB[o]         = 1.f - wL[o] - wH[o];
        lvl[o]        = smooth_[LEVEL][o] * smooth_[LEVEL][o];
        send[o]       = smooth_[RSEND][o] * revMaster;
    }

    // --- LFOs: per (target, osc) rate (Hz per sample, or a TEMPO division)
    //     and depth; they run on the control tick below
    const bool lfoSync = step_[LFOSYNC][0] == 1;
    float      lfoInc[kLfoTargets][kOscs], lfoDepth[kLfoTargets][kOscs];
    float      lfoBeats[kLfoTargets][kOscs];
    int        lfoWave[kLfoTargets][kOscs];
    for(int t = 0; t < kLfoTargets; t++)
        for(int o = 0; o < kOscs; o++)
        {
            const int f      = LPITCH_R + 3 * t;
            lfoDepth[t][o]   = smooth_[f + 1][o];
            lfoWave[t][o]    = step_[f + 2][o];
            lfoInc[t][o]     = 0.02f * exp2f(smooth_[f][o] * kLfoRateOct) / samplerate_;
            lfoDiv_[t][o]    = StepWithHyst(params_[f][o].load(), kNumLfoDivs, lfoDiv_[t][o]);
            lfoBeats[t][o]   = kLfoDivBeats[lfoDiv_[t][o]];
        }

    // --- delays: time from DIVISION and TEMPO (log slew + wobble, ramped
    //     per sample), feedback, send (x master DELAY), tone, width
    const float dtBlk     = (float)nframes / samplerate_;
    const float delMaster = smooth_[MDELAY][0] * 2.f;
    const float dcCoef    = 1.f - expf(-6.2831853f * kDelayHpHz / samplerate_);
    float       dStepL[kOscs], dStepR[kOscs], dFb[kOscs], dSend[kOscs], dTone[kOscs], dWidth[kOscs];
    for(int o = 0; o < kOscs; o++)
    {
        Delay& dl  = delays_[o];
        float  sec = 60.f / bpm * kDelDivBeats[step_[DDIV][o]];
        sec        = sec < kDelayMinSec ? kDelayMinSec : (sec > kDelayMaxSec ? kDelayMaxSec : sec);
        const bool first = dl.logT < -1e8f;
        dl.logT = first ? logf(sec) : dl.logT + (1.f - expf(-dtBlk / kDelaySlewSec)) * (logf(sec) - dl.logT);
        // WOBBLE: Ornstein-Uhlenbeck walk per side (4 s memory), smoothed
        const float kick = sqrtf(1.5f / kWobbleRevertSec) * sqrtf(dtBlk);
        const float sm   = 1.f - expf(-dtBlk / kWobbleSmoothSec);
        for(int s = 0; s < 2; s++)
        {
            float& w = dl.walk[s];
            w += -w * (dtBlk / kWobbleRevertSec) + kick * (Rand01(dl.rng) * 2.f - 1.f);
            w = w > 1.f ? 1.f : (w < -1.f ? -1.f : w);
            dl.wob[s] += sm * (w - dl.wob[s]);
        }
        const float base = expf(dl.logT) * samplerate_;
        const float wob  = smooth_[DWOBBLE][o] * kWobbleMs * 0.001f * samplerate_;
        const float tgtL = base + wob * dl.wob[0], tgtR = base + wob * dl.wob[1];
        if(first)
            dl.tL = tgtL, dl.tR = tgtR;
        dStepL[o] = (tgtL - dl.tL) / (float)nframes;
        dStepR[o] = (tgtR - dl.tR) / (float)nframes;
        dFb[o]    = FeedbackGain(smooth_[DFEED][o]);
        dSend[o]  = smooth_[DSEND][o] * delMaster * kDelaySend;
        dTone[o]  = 1.f - expf(-6.2831853f * kDelayToneLoHz * exp2f(smooth_[DTONE][o] * kDelayToneOct)
                              / samplerate_);
        dWidth[o] = smooth_[DWIDTH][o];
    }

    // --- reverb settings
    fx_.reverb->SetFeedback(kReverbFbMin + smooth_[RSIZE][0] * (kReverbFbMax - kReverbFbMin));
    fx_.reverb->SetLpFreq(kReverbLpHiHz
                          * exp2f(smooth_[RDAMP][0] * log2f(kReverbLpLoHz / kReverbLpHiHz)));
    // Pre-delay tap (>= 1 sample: Read(0) of a DelayLine is the oldest slot).
    const float preTgt  = 1.f + smooth_[RPRE][0] * kReverbPreMax * samplerate_;
    const float preStep = (preTgt - preTime_) / (float)nframes;

    const float vol    = smooth_[VOLUME][0] * smooth_[VOLUME][0];
    const int   scopeW = scopeWrite_.load();

    for(int n = 0; n < nframes; n++)
    {
        if(n % kCtrl == 0) // control tick: LFOs, glide, pitch env, filter env
        {
            const float  dn   = (float)(nframes - n < kCtrl ? nframes - n : kCtrl);
            const double beat = beat0 + bps * (double)n;
            for(int o = 0; o < kOscs; o++)
            {
                Voice& vc = voices_[o];
                float  m[kLfoTargets]; // raw LFO values, -1..1
                for(int t = 0; t < kLfoTargets; t++)
                {
                    Lfo&  lf = lfo_[t][o];
                    float ph;
                    if(lfoSync)
                    {
                        const double c = beat / (double)lfoBeats[t][o];
                        ph             = (float)(c - floor(c));
                    }
                    else
                    {
                        ph = lf.phase + lfoInc[t][o] * dn;
                        ph -= (float)(int)ph;
                    }
                    m[t] = LfoAt(lf, ph, lfoWave[t][o]);
                }
                vc.lfoPitch = m[kLfoPitch] * lfoDepth[kLfoPitch][o] * kLfoPitchSt;
                const float cut = cutOct[o] + m[kLfoCut] * lfoDepth[kLfoCut][o] * kLfoCutOct;
                ControlTick(vc, dn, cut, fAmt[o], fAtt[o], fDec[o], pDec[o], fcMax);
                // AMP only dips (1 at the top of the wave, 1 - DEPTH at the
                // bottom), ramped across the tick so fast waves do not click.
                const float amp = 1.f - lfoDepth[kLfoAmp][o] * 0.5f * (1.f - m[kLfoAmp]);
                vc.ampStep      = (amp - vc.ampMul) / dn;
                shapeNow[o]     = Clamp01(shape[o] + m[kLfoShape] * lfoDepth[kLfoShape][o] * kLfoShapeAmt);
                // PAN LFO swings the position; WIDTH pans the two
                // oscillators apart around it (constant power).
                const float pan = smooth_[PAN][o] + m[kLfoPan] * lfoDepth[kLfoPan][o] * kLfoPanAmt;
                const float w   = smooth_[WIDTH][o] * 0.5f;
                const float pa  = Clamp01(pan - w) * 1.5707963f;
                const float pb  = Clamp01(pan + w) * 1.5707963f;
                vc.gLA          = cosf(pa);
                vc.gRA          = sinf(pa);
                vc.gLB          = cosf(pb);
                vc.gRB          = sinf(pb);
            }
        }
        float l = 0.f, r = 0.f, rl = 0.f, rr = 0.f;
        for(int o = 0; o < kOscs; o++)
        {
            Voice& vc = voices_[o];
            // Arp events land on their sample: the step's note, then any
            // ratchet hits on the same note, each with its own gate.
            if(n == onsetAt[o])
            {
                Strike(vc, vc.pendNote, vc.pendGate);
                vc.ratchetCount = vc.pendRatchet - 1;
                vc.ratchetIn    = vc.pendGap;
            }
            else if(vc.ratchetCount > 0 && --vc.ratchetIn <= 0)
            {
                Strike(vc, vc.target, vc.pendGate);
                vc.ratchetCount--;
                vc.ratchetIn = vc.pendGap;
            }
            if(vc.gateLeft > 0 && --vc.gateLeft == 0)
                vc.envStage = kEnvRel;
            // Envelope: droning oscillators bypass it (rise at the attack
            // rate, hold at 1); arp notes run A, D to SUSTAIN, and release
            // at the decay rate.
            if(!vc.arping)
            {
                vc.env      = vc.env < 1.f ? (vc.env + envAtt[o] > 1.f ? 1.f : vc.env + envAtt[o]) : 1.f;
                vc.envStage = kEnvHold;
            }
            else
                switch(vc.envStage)
                {
                    case kEnvAtt:
                        vc.env += envAtt[o];
                        if(vc.env >= 1.f)
                        {
                            vc.env      = 1.f;
                            vc.envStage = kEnvDec;
                        }
                        break;
                    case kEnvDec: vc.env += envDec[o] * (envSus[o] - vc.env); break;
                    case kEnvRel: vc.env += envDec[o] * (0.f - vc.env); break;
                    default: break;
                }
            vc.ampMul += vc.ampStep;
            const float g  = lvl[o] * vc.env * vc.ampMul;
            float       sa = SoftClip(gain * vc.a.Process(shapeNow[o]) * kOscAmp) * norm;
            float       sb = SoftClip(gain * vc.b.Process(shapeNow[o]) * kOscAmp) * norm;
            vc.fa.Process(sa);
            vc.fb.Process(sb);
            sa = (wL[o] * vc.fa.Low() + wB[o] * vc.fa.Band() + wH[o] * vc.fa.High()) * g;
            sb = (wL[o] * vc.fb.Low() + wB[o] * vc.fb.Band() + wH[o] * vc.fb.High()) * g;
            scope_[o][(scopeW + n) & kScopeMask] = sa + sb;
            float vl = sa * vc.gLA + sb * vc.gLB;
            float vr = sa * vc.gRA + sb * vc.gRB;
            // This oscillator's delay: taps -> TONE lowpass -> DC blocker;
            // WIDTH blends parallel stereo lines into ping-pong.
            {
                Delay&      dl = delays_[o];
                DelayLineT& lL = *fx_.delayL[o];
                DelayLineT& lR = *fx_.delayR[o];
                dl.tL += dStepL[o];
                dl.tR += dStepR[o];
                dl.dampL += dTone[o] * (lL.Read(dl.tL) - dl.dampL);
                dl.dampR += dTone[o] * (lR.Read(dl.tR) - dl.dampR);
                dl.dcL += dcCoef * (dl.dampL - dl.dcL);
                dl.dcR += dcCoef * (dl.dampR - dl.dcR);
                const float tapL = dl.dampL - dl.dcL, tapR = dl.dampR - dl.dcR;
                const float w    = dWidth[o];
                const float inL = vl * dSend[o], inR = vr * dSend[o];
                const float fbL = dFb[o] * tapL, fbR = dFb[o] * tapR;
                lL.Write(SoftClip((1.f - w) * (inL + fbL) + w * (0.5f * (inL + inR) + fbR)));
                lR.Write(SoftClip((1.f - w) * (inR + fbR) + w * fbL));
                vl += tapL * (1.f / kDelaySend); // the repeats, made up to unity
                vr += tapR * (1.f / kDelaySend);
            }
            l += vl;
            r += vr;
            rl += vl * send[o]; // (dry + repeats) into the reverb
            rr += vr * send[o];
        }
        l *= kMixGain;
        r *= kMixGain;

        // Reverb: the oscillators' sends through the pre-delay into the tank.
        preTime_ += preStep;
        fx_.preL->Write(rl * kMixGain);
        fx_.preR->Write(rr * kMixGain);
        float wl, wr;
        fx_.reverb->Process(fx_.preL->Read(preTime_), fx_.preR->Read(preTime_), &wl, &wr);
        l += wl * kReverbReturn;
        r += wr * kReverbReturn;

        l = SoftClip(l);
        r = SoftClip(r);
        scope_[kScopeOutput][(scopeW + n) & kScopeMask] = 0.5f * (l + r);
        out[2 * n]     = l * vol;
        out[2 * n + 1] = r * vol;
    }
    scopeWrite_.store((scopeW + nframes) & kScopeMask);
    for(int o = 0; o < kOscs; o++) // the pitch sounding now (the rings' period)
    {
        oscHz_[o].store(voices_[o].a.inc * samplerate_ / voices_[o].det);
        oscLvl_[o].store(lvl[o]);
    }
}

/* ---------------------------------------------------------------- screen */
// Everything below runs on the UI thread at ~13 fps and is small next to
// the SPI push, so it is compiled for size: flash is the tight budget.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif
static char* PutInt(char* p, int n, bool sign = false)
{
    if(n < 0)
    {
        *p++ = '-';
        n    = -n;
    }
    else if(sign)
        *p++ = '+';
    char tmp[8];
    int  k = 0;
    do
    {
        tmp[k++] = (char)('0' + n % 10);
        n /= 10;
    } while(n > 0);
    while(k > 0)
        *p++ = tmp[--k];
    *p = 0;
    return p;
}

static char* PutStr(char* p, const char* s)
{
    while(*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

/** x with `dec` decimals (1 or 2), optional sign. */
static char* PutFixed(char* p, float x, int dec, bool sign = false)
{
    const int scale = dec == 2 ? 100 : 10;
    int       v     = (int)floorf(fabsf(x) * (float)scale + 0.5f);
    if(x < 0.f && v > 0)
        *p++ = '-';
    else if(sign)
        *p++ = '+';
    p    = PutInt(p, v / scale);
    *p++ = '.';
    if(dec == 2 && v % scale < 10)
        *p++ = '0';
    return PutInt(p, v % scale);
}

/** Milliseconds, or seconds with one decimal from 1 s. */
static char* PutTime(char* p, float sec)
{
    if(sec >= 1.f)
        return PutStr(PutFixed(p, sec, 1), " S");
    return PutStr(PutInt(p, (int)(sec * 1000.f + 0.5f)), " MS");
}

static char* PutHz(char* p, float hz)
{
    if(hz >= 1000.f)
        return PutStr(PutFixed(p, hz / 1000.f, 1), " KHZ");
    return PutStr(PutInt(p, (int)(hz + 0.5f)), " HZ");
}

static int StepOf(int f, int o, float v)
{
    const int s = step_[f][o];
    return s >= 0 ? s : StepWithHyst(v, kFuncs[f].steps, -1);
}


const char* GetValueText(int pot)
{
    static char buf[16];
    char*       p = buf;
    buf[0]        = 0;
    if(pot == kPagePot)
    {
        PutInt(p, PageIndex() + 1);
        return buf;
    }
    if(pot == kVolumePot)
    {
        PutInt(p, (int)(params_[VOLUME][0].load() * 100.f + 0.5f));
        return buf;
    }
    const int code = SlotCode(pot);
    if(code == kSubSel)
    {
        PutStr(p, kPages[PageIndex()].subNames[sub_[PageIndex()]]);
        return buf;
    }
    if(code < 0)
        return buf;
    const int       f  = SlotFunc(code);
    const int       o  = SlotOsc(code);
    const FuncInfo& fi = kFuncs[f];
    const float     v  = params_[f][o].load();
    if(fi.steps > 0)
    {
        const int k = StepOf(f, o, v);
        if(fi.names)
            PutStr(p, fi.names[k]);
        else
            PutInt(p, fi.base + k, fi.sign);
        return buf;
    }
    if(f >= LPITCH_R && f <= LSHAPE_W && (f - LPITCH_R) % 3 == 0) // an LFO RATE
    {
        if(step_[LFOSYNC][0] == 1) // TEMPO: a division of the master clock
        {
            const int t = (f - LPITCH_R) / 3;
            const int k = lfoDiv_[t][o] >= 0 ? lfoDiv_[t][o] : StepWithHyst(v, kNumLfoDivs, -1);
            PutStr(p, kLfoDivNames[k]);
            return buf;
        }
        const float hz = 0.02f * exp2f(v * kLfoRateOct); // 0.02 .. 20 Hz
        PutStr(PutFixed(p, hz, hz < 10.f ? 2 : 1), " HZ");
        return buf;
    }
    switch(f)
    {
        case TEMPO: PutStr(PutInt(p, (int)(40.f + v * 200.f + 0.5f)), " BPM"); break;
        case MCUTOFF:
            PutStr(PutFixed(p, (v - 0.5f) * 2.f * kMasterCutOct, 1, true), " OCT");
            break;
        case MDELAY:
        case MREVERB: PutStr(PutFixed(p, v * 2.f, 1), " X"); break;
        case DETUNE: PutStr(PutFixed(p, v * kDetuneCents, 1), " CT"); break;
        case ADENSITY:
        {
            // hits of LENGTH, as the arp plays them
            const int len = StepOf(ALENGTH, o, params_[ALENGTH][o].load()) + 1;
            PutInt(PutStr(PutInt(p, (int)(v * (float)len + 0.5f)), "/"), len);
            break;
        }
        case EAATT:
        case EFATT: PutTime(p, 0.001f * exp2f(v * 12.f)); break;    // 1 ms .. 4 s
        case EADEC:
        case EFDEC: PutTime(p, 0.005f * exp2f(v * 9.64f)); break;   // 5 ms .. 4 s
        case EPDEC: PutTime(p, 0.005f * exp2f(v * 7.64f)); break;   // 5 ms .. 1 s
        case EFAMT: PutStr(PutFixed(p, (v - 0.5f) * 8.f, 1, true), " OCT"); break;
        case EPAMT:
            PutStr(PutInt(p, (int)floorf((v - 0.5f) * 24.f + 0.5f), true), " ST");
            break;
        case GLIDE: PutTime(p, v); break;
        case CUTOFF: PutHz(p, 80.f * exp2f(v * kCutoffOct)); break;
        case FTYPE:
            PutStr(p, v < 0.1f ? "LP" : (v < 0.4f ? "LP-BP" : (v <= 0.6f ? "BP" : (v < 0.9f ? "BP-HP" : "HP"))));
            break;
        case DFEED: PutInt(p, (int)(FeedbackGain(v) * 100.f + 0.5f)); break;
        case DTONE: PutHz(p, 500.f * exp2f(v * 4.585f)); break; // 500 Hz .. 12 kHz
        case DWOBBLE: PutStr(PutFixed(p, v * 3.f, 1), " MS"); break;
        case PAN:
        {
            const int d = (int)floorf((v - 0.5f) * 200.f + 0.5f);
            if(d == 0)
                PutStr(p, "C");
            else
                PutInt(PutStr(p, d < 0 ? "L" : "R"), d < 0 ? -d : d);
            break;
        }
        case FINE: PutStr(PutInt(p, (int)floorf((v - 0.5f) * 100.f + 0.5f), true), " CT"); break;
        case RDAMP:
            PutHz(p, kReverbLpHiHz * exp2f(v * log2f(kReverbLpLoHz / kReverbLpHiHz)));
            break;
        case RPRE: PutTime(p, v * kReverbPreMax); break;
        default: PutInt(p, (int)(v * 100.f + 0.5f)); break;
    }
    return buf;
}

/* ------------------------------------------------------------ chord text */
static const char* const kQualSuffix[5] = {"", "m", "dim", "+", "sus"};
static const char* const kChordSuffix[kNumKeySets]
    = {"", "m", "sus2", "sus4", "maj7", "m7", "7", "dim", "+", "add9"};

static bool DispMinor()
{
    const int q = dispQual_.load();
    if(q >= 100)
    {
        const int cm = kChordMask[q - 100];
        return ((cm >> 3) & 1) && !((cm >> 4) & 1); // a minor third
    }
    return q == kQualMin || q == kQualDim;
}

const char* GetChordName()
{
    static char buf[12];
    const int   q = dispQual_.load();
    PutStr(PutStr(buf, kNoteNames[dispRootPc_.load()]),
           q >= 100 ? kChordSuffix[q - 100] : kQualSuffix[q]);
    return buf;
}

const char* GetChordNumeral()
{
    // Roman numeral of the chord degree, lowercase for a minor chord.
    static char buf[8];
    const char* r     = kRoman[dispDeg_.load()];
    const bool  minor = DispMinor();
    int         i     = 0;
    for(; r[i]; i++)
        buf[i] = minor ? (char)(r[i] == 'I' ? 'i' : 'v') : r[i];
    buf[i] = 0;
    return buf;
}

static const uint16_t kBright = synthui::Color(230, 230, 235);
static const uint16_t kFaint  = synthui::Color(105, 108, 118);

/** Pitch -> color: pitch class in circle-of-fifths order round the hue
 *  circle, so notes a fifth apart get neighbouring hues and a chord's
 *  notes cluster into one color mood. */
static uint16_t NoteColor(float midi)
{
    const int pc = (((int)floorf(midi + 0.5f)) % 12 + 12) % 12;
    return synthui::HueColor((float)((pc * 7) % 12) / 12.f);
}

/** Centred text on a darkened band: the view behind the line is dimmed by
 *  `alpha` (so the band fades with the text), then the text is drawn. The
 *  readout sits on the lightly dimmed view, and this keeps it readable. */
static void TextOnDark(uint16_t* fb, const char* s, int cx, int cy, int scale, uint16_t c, float alpha)
{
    int n = 0;
    while(s[n])
        n++;
    if(n == 0)
        return;
    const int w = (n * 6 - 1) * scale + 8, h = 7 * scale + 6;
    const int x0 = cx - w / 2, y0 = cy - (7 * scale) / 2 - 3;
    for(int y = y0 < 0 ? 0 : y0; y < y0 + h && y < synthui::H; y++)
        for(int x = x0 < 0 ? 0 : x0; x < x0 + w && x < synthui::W; x++)
            fb[y * synthui::W + x] = synthui::Dim(fb[y * synthui::W + x], 1.f - 0.85f * alpha);
    synthui::DrawTextCentered(fb, s, cx, cy, scale, c);
}

/** Knob readout: gauge + name + value of the last-touched pot, with the
 *  oscillator (and sub-page) it belongs to above the name. */
static void DrawReadout(uint16_t* fb, float alpha)
{
    const int p = activePot_.load();
    const float v = GetSlotValue(p);
    uint16_t    c = synthui::Dim(synthui::HueColor(v), alpha);
    synthui::DrawGauge(fb, v, c, synthui::Dim(synthui::kTrack, alpha));
    TextOnDark(fb, GetSlotName(p), 120, 120, 4, c, alpha);
    char       sub[24];
    char*      q   = sub;
    const int  pg  = PageIndex();
    const int  osc = GetSlotOsc(p);
    sub[0]         = 0;
    if(HasSubs(pg) && p < kSubPot)
        q = PutStr(PutStr(q, kPages[pg].subNames[sub_[pg]]), "  ");
    if(osc > 0)
        PutInt(PutStr(q, "OSC "), osc);
    TextOnDark(fb, sub, 120, 86, 2, synthui::Dim(kFaint, alpha), alpha);
    TextOnDark(fb, GetValueText(p), 120, 172, 3, synthui::Dim(kBright, alpha), alpha);
}

/** The rows of a column page (sub-page): "1-3 NAME" .. "10 NAME". */
static void DrawRows(uint16_t* fb, int pg, int sub, int y0, float alpha)
{
    static const char* const kRowLabel[4] = {"1-3", "4-6", "7-9", "10"};
    const uint16_t bright = synthui::Dim(kBright, alpha);
    const uint16_t faint  = synthui::Dim(kFaint, alpha);
    for(int r = 0; r < 4; r++)
    {
        const int   y    = y0 + r * 28;
        const char* name = r < 3 ? kFuncs[SlotFunc(kPages[pg].slots[sub][r * 3])].name
                                 : (kPages[pg].knob10 == kSubSel ? kPages[pg].selName
                                                                 : kFuncs[kPages[pg].knob10].name);
        synthui::DrawText(fb, kRowLabel[r], 36, y, 2, faint);
        synthui::DrawText(fb, name, 84, y, 2, r < 3 ? bright : faint);
    }
}

/** PAGE readout: the page's name, then what the knobs do on it. Column
 *  pages list their rows; MAIN and KEY list all twelve knobs in two
 *  columns mirroring the panel (knobs 1-6 left, 7-12 right). */
static void DrawPageMap(uint16_t* fb, float alpha)
{
    const int pg = PageIndex();
    uint16_t  c  = synthui::Dim(synthui::HueColor(GetSlotValue(kPagePot)), alpha);
    synthui::DrawTextCentered(fb, kPages[pg].name, 120, 32, 3, c);
    const uint16_t faint = synthui::Dim(kFaint, alpha);
    if(kPages[pg].columns)
    {
        int y0 = 78;
        if(HasSubs(pg))
        {
            synthui::DrawTextCentered(fb, kPages[pg].subNames[sub_[pg]], 120, 62, 2, faint);
            y0 = 86;
        }
        DrawRows(fb, pg, sub_[pg], y0, alpha);
    }
    else
    {
        const uint16_t bright = synthui::Dim(kBright, alpha);
        for(int pot = 0; pot < kNumPots; pot++)
        {
            const int col = pot / 6;
            const int row = pot % 6;
            char      name[16];
            char*     q = PutStr(name, GetSlotName(pot));
            const int o = GetSlotOsc(pot);
            if(o > 0)
                PutInt(PutStr(q, " "), o);
            const bool fixd = pot == kPagePot || pot == kVolumePot;
            synthui::DrawText(fb, name, col == 0 ? 20 : 128, 60 + row * 22, 2, fixd ? faint : bright);
        }
    }
    char txt[8] = "PAGE ";
    PutInt(txt + 5, pg + 1);
    synthui::DrawTextCentered(fb, txt, 120, 206, 2, faint);
}

/** Does a sub-page do anything right now? (LFO: any DEPTH above zero.) */
static bool SubInUse(int pg, int sub)
{
    if(pg != kPgLfo)
        return true;
    const int d = LPITCH_D + 3 * sub;
    for(int o = 0; o < kOscs; o++)
        if(params_[d][o].load() > 0.005f)
            return true;
    return false;
}

/** Knob 10 on a sub-page page: the chosen sub-page and its rows, then one
 *  dot per sub-page (bright = chosen, dim color = in use, grey = idle). */
static void DrawSubMap(uint16_t* fb, float alpha)
{
    const int pg = PageIndex();
    const int sb = sub_[pg];
    uint16_t  c  = synthui::Dim(synthui::HueColor(subSel_[pg]), alpha);
    synthui::DrawTextCentered(fb, kPages[pg].name, 120, 30, 2, synthui::Dim(kFaint, alpha));
    synthui::DrawTextCentered(fb, kPages[pg].subNames[sb], 120, 60, 3, c);
    DrawRows(fb, pg, sb, 90, alpha);
    const int n = kPages[pg].numSubs;
    for(int i = 0; i < n; i++)
    {
        const int x  = 120 + (2 * i - (n - 1)) * 10;
        uint16_t  dc = i == sb ? c
                      : (SubInUse(pg, i) ? synthui::Dim(synthui::HueColor(St(i, n)), 0.45f * alpha)
                                         : synthui::Dim(synthui::Color(60, 60, 66), alpha));
        synthui::FillCircle(fb, x, 206, i == sb ? 5 : 4, dc);
    }
}

/** Horizontal envelope: flat through the middle, raised-cosine taper over
 *  the outer kScopeTaperPx so every trace pinches to the center line at
 *  both edges of the round screen. */
constexpr int kScopeTaperPx = 70;
static float  ScopeEnvelope(int x)
{
    int edge = x < synthui::W / 2 ? x : synthui::W - 1 - x;
    if(edge >= kScopeTaperPx)
        return 1.f;
    float t = (float)edge / (float)kScopeTaperPx;
    return 0.5f - 0.5f * cosf(3.1415927f * t);
}

static void DrawTrace(uint16_t* fb, const float* ring, int yc, uint16_t c,
                      float gainPx = kScopeGainPx)
{
    // Newest 25 ms of one trace, triggered on a rising zero crossing so the
    // wave holds still. Drawn as a connected line around center row yc.
    int       w     = scopeWrite_.load();
    int       start = (w - kScopeWindow) & kScopeMask;
    const int limit = kScopeSize - kScopeWindow - 2; // how far back we may look
    for(int k = 0; k < limit; k++)
    {
        int i = (start - k) & kScopeMask;
        int j = (i - 1) & kScopeMask;
        if(ring[j] < 0.f && ring[i] >= 0.f)
        {
            start = i;
            break;
        }
    }
    int prevY = -1;
    for(int x = 0; x < synthui::W; x++)
    {
        int   idx = (start + x * kScopeWindow / synthui::W) & kScopeMask;
        float s   = ring[idx] * ScopeEnvelope(x);
        int   y   = yc - (int)(s * gainPx);
        if(y < 0)
            y = 0;
        if(y > synthui::H - 1)
            y = synthui::H - 1;
        if(prevY < 0)
            synthui::Pixel(fb, x, y, c);
        else
            synthui::DrawVLine(fb, x, prevY, y, c);
        prevY = y;
    }
}

static void DrawScope(uint16_t* fb, float alpha)
{
    // Final output first, larger and much dimmer: a background wash the
    // colored oscillators float over; then the oscillators, osc 1 last.
    DrawTrace(fb, scope_[kScopeOutput], 120, synthui::Dim(synthui::Color(70, 70, 80), alpha),
              kScopeGainPx * 1.6f);
    for(int o = kOscs - 1; o >= 0; o--)
        DrawTrace(fb, scope_[o], 120, synthui::Dim(NoteColor(oscNote_[o].load()), alpha));
}

/** Centre of the screen: the current chord (name in its root's color),
 *  its numeral and place in the progression, and the beat of the bar.
 *  Build step 7 puts the step rings around it. */
static void DrawChordCenter(uint16_t* fb, float alpha)
{
    synthui::FillCircle(fb, 120, 120, 48, synthui::kBlack);
    const char* name = GetChordName();
    int         len  = 0;
    while(name[len])
        len++;
    const uint16_t c = synthui::Dim(NoteColor((float)dispRootPc_.load()), alpha);
    synthui::DrawTextCentered(fb, name, 120, 108, len <= 3 ? 4 : 3, c);
    char sub[16];
    char* p = PutStr(sub, GetChordNumeral());
    if(dispProgPos_.load() >= 0)
        PutInt(PutStr(PutInt(PutStr(p, "  "), dispProgPos_.load() + 1), "/"), dispProgLen_.load());
    synthui::DrawTextCentered(fb, sub, 120, 138, 2, synthui::Dim(kFaint, alpha));
    const int beat = dispBeat_.load();
    for(int b = 0; b < 4; b++)
        synthui::FillCircle(fb, 105 + b * 10, 156, 2,
                            synthui::Dim(b == beat ? kBright : synthui::Color(60, 60, 66), alpha));
}

/* -------------------------------------------------------------- the views */
// Points round the screen centre: index 0 = 12 o'clock, clockwise.
constexpr int kCirclePts = 256;
static float  circSin_[kCirclePts], circCos_[kCirclePts];
static bool   circReady_ = false;
static void   UnitCircle()
{
    if(circReady_)
        return;
    for(int i = 0; i < kCirclePts; i++)
    {
        circSin_[i] = sinf(6.2831853f * (float)i / (float)kCirclePts);
        circCos_[i] = cosf(6.2831853f * (float)i / (float)kCirclePts);
    }
    circReady_ = true;
}
static inline int CircX(int i, float r)
{
    return 120 + (int)floorf(r * circSin_[i & (kCirclePts - 1)] + 0.5f);
}
static inline int CircY(int i, float r)
{
    return 120 - (int)floorf(r * circCos_[i & (kCirclePts - 1)] + 0.5f);
}

/** VIEW RINGS: one ring per oscillator (outer = OSC 1), drawn as that
 *  oscillator's real waveform: one period of its captured signal (post
 *  filter and envelope), repeated round the ring, 3 cycles at A2 and
 *  twice as many per octave, height = the signal divided by LEVEL (so it
 *  follows the envelope, and a quiet oscillator still draws a full ring),
 *  soft-limited to +-kMaxPx so the peaks keep their shape, stroked 2 px.
 *  Each step's arc takes the color of the note that step last played
 *  (fifths hue), the playing step at full brightness; DENSITY rests are
 *  grey. Step dots sit on the ring: the playing one large with a halo,
 *  rests hollow, ratchets split. A droning oscillator is one unbroken
 *  ring. `oneHue` (VIEW PITCH, user request): every wave in one color,
 *  the hue of the note its oscillator is on, instead of the step arcs; the
 *  dots stay per step. (User 2026-09-23: the first version, +-7 px at
 *  40 %, was too subtle.) */
static void DrawRings(uint16_t* fb, float alpha, bool oneHue)
{
    static const int kRingR[kOscs] = {102, 80, 58}; // 22 px apart: two +-9 px waves never touch
    constexpr float  kGainPx = 40.f, kMaxPx = 9.f;
    UnitCircle();
    const int      w    = scopeWrite_.load();
    const uint16_t kRest = synthui::Dim(synthui::Color(55, 55, 62), alpha);
    for(int o = 0; o < kOscs; o++)
    {
        const float* ring = scope_[o];
        const float  hz   = oscHz_[o].load();
        float        per  = samplerate_ / (hz > 10.f ? hz : 10.f);
        per               = per < 4.f ? 4.f : (per > 1500.f ? 1500.f : per);
        // Latest rising zero crossing a whole period back: the wave holds still.
        int start = (w - (int)per - 2) & kScopeMask;
        for(int k = 0; k < 2 * (int)per + 4; k++)
        {
            const int i = (w - (int)per - 2 - k) & kScopeMask;
            if(ring[(i - 1) & kScopeMask] < 0.f && ring[i] >= 0.f)
            {
                start = i;
                break;
            }
        }
        int cycles = (int)(3.f * hz / 110.f + 0.5f);
        cycles     = cycles < 2 ? 2 : (cycles > 24 ? 24 : cycles);
        const int len = ringLen_[o].load();
        const int pos = ringPos_[o].load();
        uint16_t  col[kRingMax];
        // A drone, or VIEW PITCH: the whole wave in the hue of the note it is on.
        const uint16_t whole = synthui::Dim(NoteColor(oscNote_[o].load()), alpha);
        for(int k = 0; k < len; k++)
        {
            const int n = ringNote_[o][k];
            col[k]      = n >= 0 ? synthui::Dim(NoteColor((float)n), (k == pos ? 1.f : 0.7f) * alpha)
                                 : (n == kRingRest ? kRest : synthui::Dim(synthui::Color(35, 35, 40), alpha));
        }
        const float R    = (float)kRingR[o];
        const float lv   = oscLvl_[o].load();
        const float gain = kGainPx / (lv > 0.05f ? lv : 0.05f) / kMaxPx;
        int         px = 0, py = 0, qx = 0, qy = 0;
        for(int i = 0; i <= kCirclePts; i++)
        {
            float ph = (float)(i * cycles % kCirclePts) / (float)kCirclePts;
            float v  = kMaxPx * SoftClip(ring[(start + (int)(ph * per)) & kScopeMask] * gain);
            const int x = CircX(i, R + v), y = CircY(i, R + v);
            const int x2 = CircX(i, R + v + 1.f), y2 = CircY(i, R + v + 1.f);
            if(i > 0)
            {
                const int      k = len > 0 ? (int)((float)i * (float)len / kCirclePts + 0.5f) % len : 0;
                const uint16_t c = len > 0 && !oneHue ? col[k] : whole;
                synthui::DrawLine(fb, px, py, x, y, c);
                synthui::DrawLine(fb, qx, qy, x2, y2, c); // 2 px stroke
            }
            px = x;
            py = y;
            qx = x2;
            qy = y2;
        }
        for(int k = 0; k < len; k++) // step dots
        {
            const int i = k * kCirclePts / len;
            const int n = ringNote_[o][k];
            const int x = CircX(i, R), y = CircY(i, R);
            if(n >= 0 && k == pos)
            {
                const uint16_t c = NoteColor((float)n);
                synthui::FillCircle(fb, x, y, 10, synthui::Dim(c, 0.3f * alpha));
                synthui::FillCircle(fb, x, y, 7, synthui::Dim(c, alpha));
            }
            else if(n >= 0 && ringRatchet_[o][k])
            {
                synthui::FillCircle(fb, CircX(i - 4, R), CircY(i - 4, R), 3, col[k]);
                synthui::FillCircle(fb, CircX(i + 4, R), CircY(i + 4, R), 3, col[k]);
            }
            else if(n >= 0)
                synthui::FillCircle(fb, x, y, 4, col[k]);
            else if(n == kRingRest)
            {
                synthui::FillCircle(fb, x, y, 4, kRest);
                synthui::FillCircle(fb, x, y, 2, synthui::kBlack);
            }
            else
                synthui::FillCircle(fb, x, y, 2, col[k]);
        }
    }
}

/** VIEW WHEEL: the 12 notes round the circle in fifths order (C at the
 *  top, so the hue wheel and the note wheel agree). Key notes marked in
 *  their hue, the chord's tones larger and joined into its shape; each
 *  oscillator's note a disc (OSC 1 largest) at its note, nearer the rim
 *  the higher it is, with its previous note left behind dimly. */
static void DrawWheel(uint16_t* fb, float alpha, float centerA)
{
    UnitCircle();
    static float last_[kOscs] = {-1.f, -1.f, -1.f}, prev_[kOscs] = {-1.f, -1.f, -1.f};
    const int    km = dispKeyMask_.load(), cm = dispChordMask_.load();
    auto at = [](int pc) { return (pc * 7 % 12) * kCirclePts / 12; };
    constexpr float kR = 94.f;
    int first = -1, prevPc = -1;
    for(int q = 0; q < 12; q++) // chord shape: its tones joined in wheel order
    {
        const int pc = q * 7 % 12;
        if(!((cm >> pc) & 1))
            continue;
        if(prevPc >= 0)
            synthui::DrawLine(fb, CircX(at(prevPc), kR), CircY(at(prevPc), kR), CircX(at(pc), kR),
                              CircY(at(pc), kR), synthui::Dim(kFaint, alpha));
        else
            first = pc;
        prevPc = pc;
    }
    if(first >= 0 && prevPc != first)
        synthui::DrawLine(fb, CircX(at(prevPc), kR), CircY(at(prevPc), kR), CircX(at(first), kR),
                          CircY(at(first), kR), synthui::Dim(kFaint, alpha));
    for(int pc = 0; pc < 12; pc++)
    {
        const uint16_t c = NoteColor((float)pc);
        const int      x = CircX(at(pc), kR), y = CircY(at(pc), kR);
        if((cm >> pc) & 1)
            synthui::FillCircle(fb, x, y, 6, synthui::Dim(c, alpha));
        else if((km >> pc) & 1)
            synthui::FillCircle(fb, x, y, 3, synthui::Dim(c, 0.45f * alpha));
        else
            synthui::FillCircle(fb, x, y, 1, synthui::Dim(synthui::Color(60, 60, 66), alpha));
    }
    static const int kDot[kOscs] = {7, 5, 4};
    for(int o = kOscs - 1; o >= 0; o--)
    {
        const float m = oscNote_[o].load();
        if(fabsf(m - last_[o]) > 0.5f)
        {
            prev_[o] = last_[o];
            last_[o] = m;
        }
        for(int t = 0; t < 2; t++)
        {
            const float n = t == 0 ? prev_[o] : m;
            if(n < 0.f)
                continue;
            float r = 30.f + (n - 36.f) * (56.f / 48.f);
            r       = r < 26.f ? 26.f : (r > 86.f ? 86.f : r);
            const int pc = ((int)floorf(n + 0.5f) % 12 + 12) % 12;
            synthui::FillCircle(fb, CircX(at(pc), r), CircY(at(pc), r), t == 0 ? 2 : kDot[o],
                                synthui::Dim(NoteColor(n), (t == 0 ? 0.35f : 1.f) * alpha));
        }
    }
    if(centerA > 0.f)
    {
        synthui::DrawTextCentered(fb, GetChordName(), 120, 114, 3,
                                  synthui::Dim(NoteColor((float)dispRootPc_.load()), centerA));
        synthui::DrawTextCentered(fb, GetChordNumeral(), 120, 136, 2, synthui::Dim(kFaint, centerA));
    }
}

void RenderDiagnostics(uint16_t* fb)
{
    // Two columns of six: "N-VVV" per pot (1-12), scale-2 text.
    synthui::Fill(fb, synthui::kBlack);
    synthui::DrawTextCentered(fb, "ADC", 120, 30, 2, synthui::Color(120, 120, 130));
    for(int i = 0; i < kNumPots; i++)
    {
        float raw = pot_[i] < 0.f ? 0.f : pot_[i];
        int   pct = (int)(raw * 100.f + 0.5f);
        char  txt[8];
        int   n    = 0;
        int   knob = i + 1;
        if(knob >= 10)
            txt[n++] = (char)('0' + knob / 10);
        txt[n++] = (char)('0' + knob % 10);
        txt[n++] = '-';
        if(pct >= 100)
            txt[n++] = '1';
        if(pct >= 10)
            txt[n++] = (char)('0' + (pct / 10) % 10);
        txt[n++] = (char)('0' + pct % 10);
        txt[n]   = 0;
        int col  = i / 6;
        int row  = i % 6;
        synthui::DrawTextCentered(fb, txt, 78 + col * 84, 62 + row * 24, 2,
                                  synthui::Color(230, 230, 235));
    }
}

void RenderScreen(uint16_t* fb)
{
    // The scope is always on (the rings view replaces it in build step 7).
    // The knob readout appears on top the moment a knob moves and fades
    // out after kIdleStart quiet frames; the scope dims underneath it.
    // Frame-counted, so sim and firmware behave identically.
    int mc = moveCount_.load();
    if(mc != lastMove_)
    {
        lastMove_   = mc;
        idleFrames_ = 0;
    }
    else if(idleFrames_ < kIdleStart + kFadeFrames)
        idleFrames_++;

    float readoutA = 1.f;
    if(idleFrames_ > kIdleStart)
        readoutA = 1.f - (float)(idleFrames_ - kIdleStart) / (float)kFadeFrames;

    const int  ap     = activePot_.load();
    const bool pageMap = ap == kPagePot;
    const bool subMap  = ap == kSubPot && HasSubs(PageIndex());
    synthui::Fill(fb, synthui::kBlack);
    // The view (KEY page VIEW) dims under the readout and the maps. (User
    // 2026-09-23: the readout dimmed it far too much; it now keeps 75 % and
    // its text lines sit on darkened bands. The maps fill the screen with
    // text, so the view drops to 20 % under them.) The readout and the
    // chord in the centre hand over instead of overlapping: the text fades
    // out in the first half of the fade, the chord fades in in the second.
    const float bg     = 1.f - ((pageMap || subMap) ? 0.8f : 0.25f) * readoutA;
    const float textA  = readoutA >= 1.f ? 1.f : (readoutA > 0.5f ? 2.f * readoutA - 1.f : 0.f);
    const float chordA = readoutA < 0.5f ? 1.f - 2.f * readoutA : 0.f;
    const int   view   = step_[VIEW][0] < 0 ? kViewRings : step_[VIEW][0];
    if(bg > 0.01f)
    {
        if(view == kViewWheel)
            DrawWheel(fb, bg, chordA);
        else if(view == kViewScope)
            DrawScope(fb, bg);
        else
            DrawRings(fb, bg, view == kViewPitch);
    }
    if(chordA > 0.f && view != kViewWheel)
        DrawChordCenter(fb, chordA);
    if(textA > 0.f)
    {
        if(pageMap)
            DrawPageMap(fb, textA);
        else if(subMap)
            DrawSubMap(fb, textA);
        else
            DrawReadout(fb, textA);
    }
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
} // namespace synth
