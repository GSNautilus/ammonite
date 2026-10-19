/** The bass generator: the chord's root on the step grid (DIVISION, LENGTH,
 *  DENSITY, GATE, VARY, RATCHET as the arp), one note at a time.
 *
 *  PATTERN, counted over the hits since the pattern began (from the restart
 *  point, every LENGTH; ROTATE turns the rhythm, the pattern starting on its
 *  first hit):
 *  Root (the root every hit), Root-5th (root, in-key fifth, ...), Octaves
 *  (root, octave up, ...), Walk (root, third, fifth, sixth, ... and on the
 *  last step before a chord change, a half step next to the next chord's
 *  root, on the side the line comes from, leading into it). VARY swaps a
 *  hit, at that chance, for the root, fifth or octave (VARY and RATCHET
 *  roll the step's dice: Steps.hpp, SEED). DEGREE moves the
 *  "root" up the chord as for the arp.
 *  Octave 0 is the arp's (C2). */
#pragma once

#include "Arp.hpp"
#include "Steps.hpp"

#include <algorithm>

namespace ammo {

enum BassPattern
{
    kBassRoot = 0,
    kBassRootFifth,
    kBassOctaves,
    kBassWalk
};

class BassGen
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

    /** The hits of the step that begins in this block (one, or a ratchet's
     *  2-4): returns how many and writes them into `out`. */
    int Step(const BlockClock& clk, const ArpParams& a, int pattern, const StepContext& cx, Harmony& scratch, NoteEvent* out)
    {
        StepHit h;
        if(!grid_.Begin(clk, kArpDivBeats[a.div], a.length, a.density, a.rotate, cx.restart, cx.change, false, h))
            return 0;
        pos_ = h.pos;
        len_ = h.len;
        if(!h.play)
            return 0;

        scratch.Update(cx.key, ChordAt(h.start, cx.prog, cx.change, cx.manualChord));
        const int root  = scratch.DegreeSemis(a.degree);
        int       semis = Semis(a, pattern, cx, scratch, h, h.stepSmp * clk.bps);
        Dice      dice  = {&rng_, a.seed, h.pos};
        if(dice.Uniform(kRollVary) < a.vary)
        {
            const int pick = (int)(dice.Next(kRollVaryPick) % 3u);
            semis          = pick == 0 ? root : (pick == 1 ? root + scratch.KeyFifth(a.degree) : root + 12);
        }

        const int   r        = Ratchets(dice, a.ratchet);
        const float gateFrac = 0.05f + 0.95f * Clamp01(a.gate);
        const int   gap      = std::max(1, (int)(h.stepSmp / (float)r));
        const int   gate     = std::max(1, (int)(gateFrac * h.stepSmp / (float)r));
        const float note     = FoldNote((float)(kMidiBase + cx.tonic + 12 * (a.octave + cx.masterOctave) + semis));
        for(int j = 0; j < r; j++)
        {
            out[j].at    = h.at + j * gap;
            out[j].voice = 0;
            out[j].note  = note;
            out[j].gate  = gate;
        }
        return r;
    }

    /** The notes of the pattern playing (as Arp::Predict). Changes nothing. */
    bool Predict(double swing, const ArpParams& a, int pattern, const StepContext& cx, Harmony& scratch, int8_t* out) const
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
            s.pos  = i;
            s.play = StepGrid::Plays(i, s.hits, s.len, s.rot);
            if(!s.play || (a.seed <= 0 && a.vary > 0.f))
            {
                out[i] = (int8_t)(s.play ? kStepUnknown : kStepRest);
                continue;
            }
            scratch.Update(cx.key, ChordAt(s.start, cx.prog, cx.change, cx.manualChord));
            const int root  = scratch.DegreeSemis(a.degree);
            int       semis = Semis(a, pattern, cx, scratch, s, StepBeats(s.k, d, swing));
            Rng       none;
            Dice      dice = {&none, a.seed, i};
            if(a.seed > 0 && dice.Uniform(kRollVary) < a.vary)
            {
                const int pick = (int)(dice.Next(kRollVaryPick) % 3u);
                semis          = pick == 0 ? root : (pick == 1 ? root + scratch.KeyFifth(a.degree) : root + 12);
            }
            out[i] = StepNote(FoldNote((float)(kMidiBase + cx.tonic + 12 * (a.octave + cx.masterOctave) + semis)));
        }
        return true;
    }

  private:
    /** PATTERN's note for the step `h` (`scratch` on its chord) lasting `stepBeats`: semitones above the tonic. */
    static int Semis(const ArpParams& a, int pattern, const StepContext& cx, Harmony& scratch, const StepHit& h,
                     double stepBeats)
    {
        const int root = scratch.DegreeSemis(a.degree);
        const int hit  = StepGrid::HitsBefore(h.pos, h.hits, h.len, h.rot); // which hit of the pattern this is
        int       semis = root;
        switch(pattern)
        {
            case kBassRootFifth: semis = (hit & 1) ? root + scratch.KeyFifth(a.degree) : root; break;
            case kBassOctaves: semis = (hit & 1) ? root + 12 : root; break;
            case kBassWalk:
            {
                // the change this step leads into: the progression's, or the bar's while it is off
                const double cb   = cx.prog.n > 0 ? kChangeBeats[cx.change] : 4.0;
                const double next = (std::floor(h.start / cb) + 1.0) * cb;
                if(h.start + stepBeats >= next - 1e-6)
                {
                    Harmony nx = scratch;
                    nx.Update(cx.key, ChordAt(next + kBeatEps, cx.prog, cx.change, cx.manualChord));
                    const int target = nx.DegreeSemis(a.degree); // where the next bar's root will sound
                    semis            = target >= root ? target - 1 : target + 1; // a half step short of it: it leads in
                }
                else
                {
                    static const int kScaleWalk[4] = {0, 2, 4, 5}, kChordWalk[4] = {0, 1, 2, 1};
                    semis = scratch.DegreeSemis(a.degree + (scratch.chordKey ? kChordWalk : kScaleWalk)[hit % 4]);
                }
                break;
            }
            default: break;
        }
        return semis;
    }

    Rng      rng_;
    StepGrid grid_;
    int      pos_ = 0, len_ = 0;
};

} // namespace ammo
