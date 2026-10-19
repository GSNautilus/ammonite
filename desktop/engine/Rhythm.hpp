/** The rhythm generator: hits with no melody, for drums (or a pitched
 *  sound struck on a pattern).
 *
 *  The step grid as every generator (DIVISION, LENGTH, DENSITY = how many
 *  hits, spread evenly: a Euclidean rhythm, ROTATE: the pattern turned by
 *  that many steps, so a kick and a snare of the same length can
 *  interlock), plus CHANCE (each hit plays at that probability), RATCHET (a
 *  hit becomes a roll of 2-4) and VARY (each hit's velocity drops by up to
 *  60 % at random, for a human feel). The three roll the step's dice
 *  (Steps.hpp: SEED), so a seeded rhythm drops, rolls and accents the same
 *  hits every LENGTH.
 *
 *  PITCH: Fixed plays C4 (a drum untransposed); Root and Fifth follow the
 *  chord's root or in-key fifth, folded to within a tritone of C4, for
 *  tuned drums and percussion that move with the harmony. OCTAVE shifts. */
#pragma once

#include "Arp.hpp"
#include "Steps.hpp"

#include <algorithm>

namespace ammo {

enum RhythmPitch
{
    kRhythmFixed = 0,
    kRhythmRoot,
    kRhythmFifth
};

struct RhythmParams
{
    int   pitch  = kRhythmFixed; // (ROTATE is every generator's: ArpParams)
    float chance = 1.f;
};

class RhythmGen
{
  public:
    void Init(uint32_t seed)
    {
        rng_ = Rng(seed);
        grid_.Reset();
        pos_ = len_ = 0;
    }
    void Rearm() { grid_.Reset(); }
    int  StepPos() const { return pos_; }
    int  StepLen() const { return len_; }
    bool LastStep(long long& k) const { return grid_.Last(k); }

    /** The hits of the step that begins in this block: returns how many (a
     *  roll: 2-4) and writes them into `out`. */
    int Step(const BlockClock& clk, const ArpParams& a, const RhythmParams& r, const StepContext& cx, Harmony& scratch,
             NoteEvent* out)
    {
        StepHit h;
        if(!grid_.Begin(clk, kArpDivBeats[a.div], a.length, a.density, a.rotate, cx.restart, cx.change, false, h))
            return 0;
        pos_ = h.pos;
        len_ = h.len;
        if(!h.play)
            return 0;
        Dice dice = {&rng_, a.seed, h.pos};
        if(dice.Uniform(kRollChance) >= r.chance)
            return 0;

        const float note     = Note(a, r, cx, scratch, h.start);
        const float vel      = 1.f - 0.6f * Clamp01(a.vary) * dice.Uniform(kRollVelocity);
        const int   rolls    = Ratchets(dice, a.ratchet);
        const float gateFrac = 0.05f + 0.95f * Clamp01(a.gate);
        const int   gap      = std::max(1, (int)(h.stepSmp / (float)rolls));
        const int   gate     = std::max(1, (int)(gateFrac * h.stepSmp / (float)rolls));
        for(int j = 0; j < rolls; j++)
        {
            out[j].at    = h.at + j * gap;
            out[j].voice = 0;
            out[j].note  = note;
            out[j].gate  = gate;
            out[j].vel   = vel * (j == 0 ? 1.f : 0.75f); // a roll's later hits a little softer
        }
        return rolls;
    }

    /** The notes of the pattern playing (as Arp::Predict; a seeded CHANCE's dropped hits are rests). */
    bool Predict(double swing, const ArpParams& a, const RhythmParams& r, const StepContext& cx, Harmony& scratch,
                 int8_t* out) const
    {
        long long k;
        if(!grid_.Last(k))
            return false;
        const double d = kArpDivBeats[a.div];
        StepHit      h;
        StepGrid::Locate(k, d, a.length, a.density, a.rotate, cx.restart, cx.change, swing, h);
        const long long k0 = PatternFirst(k, h.pos);
        for(int i = 0; i < h.len; i++)
        {
            StepHit s;
            StepGrid::Locate(k0 + i, d, a.length, a.density, a.rotate, cx.restart, cx.change, swing, s);
            Rng  none;
            Dice dice = {&none, a.seed, i};
            if(!StepGrid::Plays(i, s.hits, s.len, s.rot) || (a.seed > 0 && dice.Uniform(kRollChance) >= r.chance))
                out[i] = (int8_t)kStepRest;
            else
                out[i] = StepNote(Note(a, r, cx, scratch, s.start));
        }
        return true;
    }

  private:
    /** PITCH: C4, or the chord's root or fifth near it, for a step starting at beat `sb`. */
    static float Note(const ArpParams& a, const RhythmParams& r, const StepContext& cx, Harmony& scratch, double sb)
    {
        int semis = 0;
        if(r.pitch != kRhythmFixed)
        {
            scratch.Update(cx.key, ChordAt(sb, cx.prog, cx.change, cx.manualChord));
            semis = (cx.tonic + scratch.DegreeSemis(a.degree) + (r.pitch == kRhythmFifth ? scratch.KeyFifth(a.degree) : 0)) % 12;
            if(semis > 6)
                semis -= 12; // within a tritone of C
        }
        return FoldNote((float)(60 + semis + 12 * (a.octave + cx.masterOctave)));
    }

    Rng      rng_;
    StepGrid grid_;
    int      pos_ = 0, len_ = 0;
};

} // namespace ammo
