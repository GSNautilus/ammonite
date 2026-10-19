/** Harmony, ported from the hardware core (core/synth_core.cpp): the key
 *  (root + one of 10 scales or 10 chord types), the current chord as a scale
 *  degree, degree -> semitones, in-key fifths, note pools for the arp, the
 *  progression library. Pure functions and one small state struct. */
#pragma once

#include <cmath>

namespace ammo {

constexpr int kNumKeySets = 10; // scales, and as many chord keys
constexpr int kMidiBase   = 36; // C2: the base octave of Octave 0
constexpr int kNoteLo     = 12; // C0 (16 Hz): a channel's Octave plus the master's can reach past either end;
constexpr int kNoteHi     = 120; // C9 (8.4 kHz): notes beyond are folded back in by whole octaves

/** The octave shift (a multiple of 12) that brings [lo, hi] inside
 *  [kNoteLo, kNoteHi] as far as it fits: a note, or a whole chord kept in
 *  its voicing. */
inline int FoldShift(int lo, int hi)
{
    int s = 0;
    while(lo + s < kNoteLo)
        s += 12;
    while(hi + s > kNoteHi && lo + s - 12 >= kNoteLo)
        s -= 12;
    return s;
}
inline float FoldNote(float note) { return note + (float)FoldShift((int)std::floor(note), (int)std::floor(note)); }

inline const int kScaleMask[kNumKeySets] = {
    0xAB5, // Major     0 2 4 5 7 9 11
    0x5AD, // Minor     0 2 3 5 7 8 10
    0x6AD, // Dorian    0 2 3 5 7 9 10
    0x5AB, // Phrygian  0 1 3 5 7 8 10
    0xAD5, // Lydian    0 2 4 6 7 9 11
    0x6B5, // Mixo      0 2 4 5 7 9 10
    0x9AD, // Harm min  0 2 3 5 7 8 11
    0x295, // Penta     0 2 4 7 9
    0x4A9, // Penta mi  0 3 5 7 10
    0x4E9, // Blues     0 3 5 6 7 10
};
inline const int kChordMask[kNumKeySets] = {
    0x091, // Maj   0 4 7
    0x089, // Min   0 3 7
    0x085, // Sus2  0 2 7
    0x0A1, // Sus4  0 5 7
    0x891, // Maj7  0 4 7 11
    0x489, // Min7  0 3 7 10
    0x491, // Dom7  0 4 7 10
    0x049, // Dim   0 3 6
    0x111, // Aug   0 4 8
    0x095, // Add9  0 2 4 7
};

enum ArpPool
{
    kPoolKey = 0,
    kPoolTriad,
    kPoolSeventh,
    kPoolFifths,
    kPoolRoot
};
constexpr int kPoolMax = 8 * 3;

enum TriadQuality
{
    kQualMaj = 0,
    kQualMin,
    kQualDim,
    kQualAug,
    kQualSus
};

inline int MaskIntervals(int mask, int* out)
{
    int n = 0;
    for(int i = 0; i < 12; i++)
        if((mask >> i) & 1)
            out[n++] = i;
    return n;
}

/** The key and the chord it is on right now. */
struct Harmony
{
    int  keyIv[12] = {0, 2, 3, 5, 7, 8, 10};
    int  keyN      = 7;
    int  chordIv[12] = {};
    int  chordN    = 3;
    int  chordDeg  = 0;
    bool chordKey  = false;
    int  chordRootSemis = 0;

    /** key: 0..9 a scale, 10..19 a chord type; deg: the chord's scale degree. */
    void Update(int key, int deg)
    {
        chordDeg = deg;
        chordKey = key >= kNumKeySets;
        if(!chordKey)
        {
            keyN           = MaskIntervals(kScaleMask[key], keyIv);
            chordRootSemis = keyIv[deg % keyN] + 12 * (deg / keyN);
            return;
        }
        const int  cm     = kChordMask[key - kNumKeySets];
        const bool minor3 = ((cm >> 3) & 1) && !((cm >> 4) & 1);
        keyN           = MaskIntervals(kScaleMask[minor3 ? 1 : 0], keyIv); // the parent scale
        chordRootSemis = keyIv[deg % keyN] + 12 * (deg / keyN);
        chordN         = MaskIntervals(cm, chordIv);
    }

    /** Semitones above the tonic of the note `d` steps up from the chord
     *  root: scale steps in a scale key, chord tones in a chord key. */
    int DegreeSemis(int d) const
    {
        if(chordKey)
            return chordRootSemis + chordIv[d % chordN] + 12 * (d / chordN);
        const int k = chordDeg + d;
        return keyIv[k % keyN] + 12 * (k / keyN);
    }

    /** In-key fifth above the note `d` steps up: the key's own interval
     *  closest to a perfect fifth. */
    int KeyFifth(int d) const
    {
        const int        r       = DegreeSemis(d);
        const int        n       = chordKey ? chordN : keyN;
        static const int kPref[] = {7, 8, 6, 5, 9, 4, 10};
        for(int k = 0; k < 7; k++)
            for(int j = 1; j < n; j++)
                if(DegreeSemis(d + j) - r == kPref[k])
                    return kPref[k];
        return 12;
    }

    int Quality() const
    {
        const int r = DegreeSemis(0), t3 = DegreeSemis(2) - r, t5 = DegreeSemis(4) - r;
        if(t3 == 4)
            return t5 == 8 ? kQualAug : kQualMaj;
        if(t3 == 3)
            return t5 == 6 ? kQualDim : kQualMin;
        return kQualSus;
    }

    /** An arp pool: POOL type from `degree` on the current chord, repeated
     *  over `range` octaves, as semitones above the tonic, ascending. */
    int BuildPool(int degree, int pool, int range, int* out) const
    {
        const int n  = chordKey ? chordN : keyN;
        const int th = chordKey ? 1 : 2; // a third: two scale steps, one chord tone
        int       base[12], nb = 0;
        switch(pool)
        {
            case kPoolTriad:
            case kPoolSeventh:
            {
                const int cnt = pool == kPoolTriad ? 3 : 4;
                for(int i = 0; i < cnt; i++)
                    base[nb++] = DegreeSemis(degree + i * th);
                break;
            }
            case kPoolFifths:
                base[nb++] = DegreeSemis(degree);
                base[nb++] = base[0] + KeyFifth(degree);
                break;
            case kPoolRoot: base[nb++] = DegreeSemis(degree); break;
            default:
                for(int i = 0; i < n; i++)
                    base[nb++] = DegreeSemis(degree + i);
                break;
        }
        int k = 0;
        for(int oc = 0; oc < range; oc++)
            for(int i = 0; i < nb && k < kPoolMax; i++)
                out[k++] = base[i] + 12 * oc;
        return k;
    }

    /** Pitch-class masks of the key and the chord, given the tonic. */
    void Masks(int tonic, int& keyMask, int& chordMask) const
    {
        int km = 0, cm = 0;
        const int n = chordKey ? chordN : keyN;
        for(int d = 0; d < n; d++)
            km |= 1 << ((tonic + DegreeSemis(d)) % 12);
        for(int d = 0; d < (chordKey ? chordN : 3); d++)
            cm |= 1 << ((tonic + DegreeSemis(chordKey ? d : 2 * d)) % 12);
        keyMask   = chordKey ? cm : km;
        chordMask = cm;
    }
};

/* ------------------------------------------------------------ progression */
constexpr int kProgSteps  = 8; // the most chords a progression holds
constexpr int kProgCustom = 9; // Progression = Custom: the eight step parameters
struct Prog
{
    int n;
    int deg[kProgSteps];
};
inline const Prog kProgs[kProgCustom] = {
    {0, {0}},                      // Off: the Chord parameter
    {4, {0, 4, 5, 3}},             // Pop    I V vi IV
    {4, {0, 5, 3, 4}},             // 50s    I vi IV V
    {4, {5, 3, 0, 4}},             // Sad    vi IV I V
    {4, {0, 5, 2, 6}},             // Epic   i VI III VII (in minor)
    {4, {1, 4, 0, 0}},             // Jazz   ii V I I
    {2, {0, 3}},                   // Vamp   I IV
    {8, {0, 4, 5, 2, 3, 0, 3, 4}}, // Canon  I V vi iii IV I IV V
    {4, {0, 6, 5, 4}},             // Fall   i VII VI v (in minor)
};

/** The progression PROG plays: a library one, or (Custom) the first `len`
 *  of the user's degrees `steps`. */
inline Prog ProgOf(int prog, int len, const int* steps)
{
    if(prog != kProgCustom)
        return kProgs[prog < 0 || prog >= kProgCustom ? 0 : prog];
    Prog p;
    p.n = len < 1 ? 1 : (len > kProgSteps ? kProgSteps : len);
    for(int i = 0; i < kProgSteps; i++)
        p.deg[i] = steps[i] < 0 ? 0 : (steps[i] > 6 ? 6 : steps[i]);
    return p;
}
inline const double kChangeBeats[4] = {2.0, 4.0, 8.0, 16.0}; // 1/2, 1, 2, 4 bars
constexpr double    kBeatEps        = 1e-7;                   // a step on a bar line belongs to it

enum Restart
{
    kRestartFree = 0,
    kRestartBar,
    kRestartChord
};

/** The chord degree at master-clock position `beat`. */
inline int ChordAt(double beat, const Prog& pr, int change, int manualChord, int* progPos = nullptr)
{
    if(pr.n == 0)
    {
        if(progPos)
            *progPos = -1;
        return manualChord;
    }
    const double    cb  = kChangeBeats[change];
    const long long idx = (long long)std::floor(beat / cb) % pr.n;
    if(progPos)
        *progPos = (int)idx;
    return pr.deg[idx];
}

/** Beat where the pattern of a step starting at `beat` began: 0 (Free),
 *  the bar line (Bar) or the chord change (Chord). */
/** How often RESTART comes round, in beats (Free: never). */
inline double RestartPeriod(int restart, int change)
{
    return restart == kRestartBar ? 4.0 : restart == kRestartChord ? kChangeBeats[change] : 1e300;
}
inline double RestartBeat(double beat, int restart, int change)
{
    switch(restart)
    {
        case kRestartBar: return std::floor(beat / 4.0) * 4.0;
        case kRestartChord:
        {
            const double cb = kChangeBeats[change];
            return std::floor(beat / cb) * cb;
        }
        default: return 0.0;
    }
}

} // namespace ammo
