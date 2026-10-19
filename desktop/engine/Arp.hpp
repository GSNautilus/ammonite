/** The arp generator, the hardware's arp grown into a channel generator:
 *  on every new step of its division (derived from the master clock, with
 *  swing) it decides whether the step plays (Euclidean DENSITY over LENGTH,
 *  counted from the RESTART point, turned by ROTATE), which pool note (MODE,
 *  COUNT, SHIFT, VARY, SEED) and how (GATE, RATCHET), and reports the event
 *  with its sample offset inside the block. The step timing is the shared
 *  StepGrid (Steps.hpp).
 *
 *  Which note: COUNT Steps (Auto) takes the step's place in the pattern, so
 *  Up plays pool[pos % n] and ROTATE changes the notes as well as the
 *  rhythm; Hits counts the hits since the pattern began, so a run stays
 *  whole (R 3 5 R ...) at any density and ROTATE moves only the rhythm.
 *  Down and Up-down follow the same count. SHIFT then moves the note that
 *  many places up the pool (mod its size). Random picks any; VARY swaps a
 *  hit for any pool note at that chance; both, and RATCHET's, roll the
 *  step's dice (Steps.hpp: SEED). */
#pragma once

#include "Clock.hpp"
#include "Dsp.hpp"
#include "Harmony.hpp"
#include "Steps.hpp"

#include <cstdint>

namespace ammo {

enum ArpMode
{
    kArpOff = 0,
    kArpLoop,
    kArpUp,
    kArpDown,
    kArpUpDown,
    kArpRandom
};

struct ArpParams
{
    int   mode = kArpUp, pool = kPoolFifths, range = 1, div = 2, length = 8;
    float gate = 0.5f, density = 1.f, vary = 0.f, ratchet = 0.f;
    int   degree = 0, octave = 0;
    int   rotate = 0, shift = 0, count = kCountAuto, seed = 0; // every generator's ROTATE and SEED; SHIFT, COUNT
};

/** What a step asks the voice to do. */
struct ArpEvent
{
    int   at       = 0;   // sample offset in the block
    float note     = 60.f; // MIDI
    int   gate     = 1;   // samples per hit
    int   ratchets = 1;   // hits in the step (1 = plain)
    int   gap      = 0;   // samples between hits
};

/** The conductor's view a step needs: the chord at any beat and the
 *  restart point of any beat. */
struct StepContext
{
    const Harmony* harmony; // the harmony object to update for the step's chord (scratch)
    Prog           prog;    // the progression playing (none: the manual chord)
    int            key, tonic, change, manualChord, restart, masterOctave;
};

/** Up, Down, Up-down over `n` notes at count `c`, SHIFT places up: the pool index (Loop: the first, shifted). */
inline int RunIndex(int mode, int n, int c, int shift)
{
    int idx;
    switch(mode)
    {
        case kArpUp: idx = c % n; break;
        case kArpDown: idx = n - 1 - c % n; break;
        case kArpUpDown:
        {
            const int period = n > 1 ? 2 * n - 2 : 1;
            const int m      = c % period;
            idx              = m < n ? m : period - m;
            break;
        }
        default: idx = 0; break; // Loop: the pool's first note again
    }
    return shift > 0 ? (idx + shift) % n : idx;
}

/** A step's RATCHET: hits in the step (1 = plain). */
inline int Ratchets(Dice& dice, float ratchet)
{
    return dice.Uniform(kRollRatchet) < ratchet ? 2 + (int)(dice.Next(kRollRatchetN) % 3u) : 1;
}

/** A MIDI note for the telemetry's step notes (Steps.hpp). */
inline int8_t StepNote(float note)
{
    const int m = (int)std::lround(note);
    return (int8_t)(m < 0 ? 0 : (m > 127 ? 127 : m));
}

/** The steps of the pattern the step `k` (at place `pos`) belongs to: k - pos .. k - pos + LENGTH - 1. */
inline long long PatternFirst(long long k, int pos) { return k - pos; }

class Arp
{
  public:
    void Init(uint32_t seed)
    {
        rng_ = Rng(seed);
        grid_.Reset();
        pos_ = len_ = 0;
    }

    /** Resets the step memory so the next block starts a step (e.g. when
     *  the generator is switched on). */
    void Rearm() { grid_.Reset(); }

    int  StepPos() const { return pos_; }
    int  StepLen() const { return len_; }
    bool LastStep(long long& k) const { return grid_.Last(k); }

    /** The event of the step that begins in this block, if one does.
     *  `scratch` is a Harmony the arp may update to the step's chord. */
    bool Step(const BlockClock& clk, const ArpParams& p, const StepContext& cx, Harmony& scratch, ArpEvent& ev)
    {
        StepHit h;
        if(!grid_.Begin(clk, kArpDivBeats[p.div], p.length, p.density, p.rotate, cx.restart, cx.change, false, h))
            return false;
        pos_ = h.pos;
        len_ = h.len;
        if(!h.play)
            return false;

        int       pool[kPoolMax];
        const int n    = Pool(p, cx, h.start, scratch, pool);
        Dice      dice = {&rng_, p.seed, h.pos};
        int       idx  = Index(p, n, h, dice);
        if(dice.Uniform(kRollVary) < p.vary)
            idx = (int)(dice.Next(kRollVaryPick) % (uint32_t)n);

        const float stepSmp  = h.stepSmp;
        const int   r        = Ratchets(dice, p.ratchet);
        const float gateFrac = 0.05f + 0.95f * Clamp01(p.gate);
        ev.at                = h.at;
        ev.note              = FoldNote(Base(p, cx) + (float)pool[idx]);
        ev.ratchets          = r;
        ev.gap               = (int)(stepSmp / (float)r);
        ev.gate              = (int)(gateFrac * stepSmp / (float)r);
        if(ev.gate < 1)
            ev.gate = 1;
        return true;
    }

    /** The notes of the pattern playing (Steps.hpp: kStepRest, kStepUnknown), LENGTH of them, from the last step
     *  begun; false before the first. Changes nothing. */
    bool Predict(double swing, const ArpParams& p, const StepContext& cx, Harmony& scratch, int8_t* out) const
    {
        long long k;
        if(!grid_.Last(k))
            return false;
        const double d = kArpDivBeats[p.div];
        StepHit      h;
        StepGrid::Locate(k, d, p.length, p.density, p.rotate, cx.restart, cx.change, swing, h);
        const long long k0   = PatternFirst(k, h.pos);
        const bool      free = p.seed <= 0 && (p.mode == kArpRandom || p.vary > 0.f);
        for(int i = 0; i < h.len; i++)
        {
            StepHit s;
            StepGrid::Locate(k0 + i, d, p.length, p.density, p.rotate, cx.restart, cx.change, swing, s);
            s.pos = i; // the pattern as it runs from its first step (a restart inside it is not foreseen)
            s.play = StepGrid::Plays(i, s.hits, s.len, s.rot);
            if(!s.play || free)
            {
                out[i] = (int8_t)(s.play ? kStepUnknown : kStepRest);
                continue;
            }
            int       pool[kPoolMax];
            const int n = Pool(p, cx, s.start, scratch, pool);
            Rng       none; // never rolled: the seed is set, or nothing rolls
            Dice      dice = {&none, p.seed, i};
            int       idx  = Index(p, n, s, dice);
            if(p.seed > 0 && dice.Uniform(kRollVary) < p.vary)
                idx = (int)(dice.Next(kRollVaryPick) % (uint32_t)n);
            out[i] = StepNote(FoldNote(Base(p, cx) + (float)pool[idx]));
        }
        return true;
    }

  private:
    /** MODE's pool index for the step (Random: a roll of the dice; a seeded one is a pattern, so SHIFT moves it). */
    static int Index(const ArpParams& p, int n, const StepHit& h, Dice& dice)
    {
        if(p.mode != kArpRandom)
            return RunIndex(p.mode, n, Count(p, h), p.shift);
        const int idx = (int)(dice.Next(kRollNote) % (uint32_t)n);
        return p.shift > 0 ? (idx + p.shift) % n : idx;
    }
    /** COUNT: the step's place, or (Hits) the hits before it in the pattern. */
    static int Count(const ArpParams& p, const StepHit& h)
    {
        return p.count == kCountHits ? StepGrid::HitsBefore(h.pos, h.hits, h.len, h.rot) : h.pos;
    }
    /** The pool of the chord the step at beat `sb` starts in. */
    static int Pool(const ArpParams& p, const StepContext& cx, double sb, Harmony& scratch, int* pool)
    {
        scratch.Update(cx.key, ChordAt(sb, cx.prog, cx.change, cx.manualChord));
        return scratch.BuildPool(p.degree, p.pool, p.range < 1 ? 1 : p.range, pool);
    }
    static float Base(const ArpParams& p, const StepContext& cx)
    {
        return (float)(kMidiBase + cx.tonic + 12 * (p.octave + cx.masterOctave));
    }

    Rng      rng_;
    StepGrid grid_;
    int      pos_ = 0, len_ = 0;
};

} // namespace ammo
