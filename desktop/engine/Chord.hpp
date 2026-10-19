/** The chord generator: the conductor's chord as several notes at once, for
 *  pads and stabs.
 *
 *  NOTES: triad, 7th or 9th, stacked in thirds from DEGREE on the chord
 *  (chord tones, in a chord key). VOICING spreads them: Close (as stacked),
 *  Open (every second note an octave up), Drop 2 (the second highest an
 *  octave down), Spread (the root an octave down, the fifth an octave up).
 *  VOICE LEADING Smooth picks, at every chord, the inversion and octave
 *  whose notes move least from the last chord's, held near home so the
 *  register does not drift; Fixed always plays the root position.
 *  RHYTHM Hold plays each chord once, on the chord change (every bar while
 *  the progression is off), held until the next, and comes in at once when
 *  the channel starts mid-chord; Steps strikes it on the step grid
 *  (DIVISION, LENGTH, DENSITY, ROTATE, GATE as the arp). STRUM rolls the notes up
 *  from the lowest, up to 40 ms apart, all released together.
 *  RHYTHM Arp plays the voiced chord one note per step of the grid (every
 *  rhythm parameter as the arp's: DIVISION, LENGTH, DENSITY, ROTATE, GATE,
 *  VARY, RATCHET, SEED), counting the hits from each chord change (COUNT
 *  Hits, and Auto; Steps: the step's place in the pattern instead): Up
 *  starts on the root, wherever the voicing put it, and climbs, wrapping
 *  round to the voicing's lowest note; Down starts on the root and descends
 *  the same way round; Up-down climbs and comes back; Random picks any.
 *  SHIFT starts that many notes further round. The arp's
 *  Off and Loop play as Up. LET RING holds every note to the chord change
 *  (GATE does nothing then), so the chord builds up across the steps and
 *  all of it is released at the change, as Hold's is. STRUM does nothing.
 *
 *  A chord's window, for Hold and for Arp, is from one change to the next
 *  (a bar while the progression is off).
 *
 *  Octave 0 of a chord sits an octave above the arp's (C3), where pads
 *  live. Each note goes to its own voice, lowest first. */
#pragma once

#include "Arp.hpp"
#include "Steps.hpp"

#include <algorithm>
#include <cstdlib>

namespace ammo {

enum ChordVoicing
{
    kVoiceClose = 0,
    kVoiceOpen,
    kVoiceDrop2,
    kVoiceSpread
};

enum ChordRhythm
{
    kChordHold = 0,
    kChordSteps,
    kChordArp
};

struct ChordParams
{
    int   notes = 0, voicing = kVoiceClose, rhythm = kChordHold;
    bool  smooth = true, letRing = false;
    float strum  = 0.f;
};

class ChordGen
{
  public:
    void Init(float sampleRate, uint32_t seed)
    {
        sr_ = sampleRate;
        rng_ = Rng(seed);
        grid_.Reset();
        prevN_ = 0;
        pos_ = len_ = 0;
        win_   = kNoWin;
        count_ = 0;
    }
    void Rearm()
    {
        grid_.Reset();
        win_ = kNoWin;
    }
    int  StepPos() const { return pos_; }
    int  StepLen() const { return len_; }
    bool LastStep(long long& k) const { return grid_.Last(k); }

    /** The chord's notes for NOTES (3..5): how many voices an arpeggio can hold. */
    static int Notes(const ChordParams& c) { return 3 + (c.notes < 0 ? 0 : (c.notes > 2 ? 2 : c.notes)); }

    /** The notes of the step that begins in this block, if one does: returns
     *  how many events (0 for none) and writes them into `out` (up to kPoly):
     *  the chord voice by voice, or (Arp) one note, ratcheted, on its voice. */
    int Step(const BlockClock& clk, const ArpParams& a, const ChordParams& c, const StepContext& cx, Harmony& scratch,
             NoteEvent* out)
    {
        if(c.rhythm == kChordArp)
            return Arpeggiate(clk, a, c, cx, scratch, out);
        const bool   hold = c.rhythm == kChordHold;
        const double d    = hold ? WindowBeats(cx) : kArpDivBeats[a.div];
        StepHit      h;
        if(!grid_.Begin(clk, d, hold ? 1 : a.length, hold ? 1.f : a.density, hold ? 0 : a.rotate, cx.restart, cx.change,
                        hold, h))
            return 0;
        pos_ = h.pos;
        len_ = h.len;
        if(!h.play)
            return 0;

        scratch.Update(cx.key, ChordAt(h.start, cx.prog, cx.change, cx.manualChord));
        int       v[kPoly];
        const int n = Voice(scratch, a.degree, c, v);

        int base = kMidiBase + 12 + cx.tonic + 12 * (a.octave + cx.masterOctave);
        if(n > 0)
            base += FoldShift(base + v[0], base + v[n - 1]); // the whole chord, in its voicing
        const int gate = hold ? (int)h.stepSmp : (int)((0.05f + 0.95f * Clamp01(a.gate)) * h.stepSmp);
        const int gap  = (int)(c.strum * c.strum * 0.04f * sr_);
        for(int i = 0; i < n; i++)
        {
            out[i].at    = h.at + i * gap;
            out[i].voice = i;
            out[i].note  = (float)(base + v[i]);
            out[i].gate  = std::max(1, gate - i * gap); // strummed up, released together
        }
        return n;
    }

    /** The notes of the pattern playing (as Arp::Predict): Hold and Steps report each strike's chord root, Arp its
     *  note. Changes nothing (the voice leading runs on a copy). */
    bool Predict(double swing, const ArpParams& a, const ChordParams& c, const StepContext& cx, Harmony& scratch,
                 int8_t* out) const
    {
        long long k;
        if(!grid_.Last(k))
            return false;
        const bool   hold = c.rhythm == kChordHold, arp = c.rhythm == kChordArp;
        const double d    = hold ? WindowBeats(cx) : kArpDivBeats[a.div];
        const int    len  = hold ? 1 : a.length;
        const float  den  = hold ? 1.f : a.density;
        const int    rot  = hold ? 0 : a.rotate;
        StepHit      h;
        StepGrid::Locate(k, d, len, den, rot, cx.restart, cx.change, swing, h);
        const long long k0   = PatternFirst(k, h.pos);
        const bool      free = arp && a.seed <= 0 && (a.mode == kArpRandom || a.vary > 0.f);
        // Arp counting from the chord change: the hits in it before the pattern's first step
        const double wb    = WindowBeats(cx);
        long long    win   = kNoWin;
        int          count = 0;
        if(arp && a.count != kCountSteps)
        {
            StepHit f;
            StepGrid::Locate(k0, d, len, den, rot, cx.restart, cx.change, swing, f);
            win = (long long)std::floor(f.start / wb);
            for(long long q = k0 - 1;; q--)
            {
                StepHit b;
                StepGrid::Locate(q, d, len, den, rot, cx.restart, cx.change, swing, b);
                if((long long)std::floor(b.start / wb) != win)
                    break;
                count += b.play ? 1 : 0;
            }
        }
        ChordGen g = *this; // the voice leading's memory, to run on
        for(int i = 0; i < h.len; i++)
        {
            StepHit s;
            StepGrid::Locate(k0 + i, d, len, den, rot, cx.restart, cx.change, swing, s);
            const long long w = (long long)std::floor(s.start / wb);
            if(w != win)
            {
                win   = w;
                count = 0;
            }
            if(!StepGrid::Plays(i, s.hits, s.len, s.rot))
            {
                out[i] = (int8_t)kStepRest;
                continue;
            }
            scratch.Update(cx.key, ChordAt(s.start, cx.prog, cx.change, cx.manualChord));
            if(!arp)
            {
                out[i] = StepNote((float)(kMidiBase + 12 + cx.tonic + 12 * (a.octave + cx.masterOctave) + scratch.DegreeSemis(a.degree)));
                continue;
            }
            if(free)
            {
                out[i] = (int8_t)kStepUnknown;
                count++;
                continue;
            }
            int       v[kPoly];
            const int n = g.Voice(scratch, a.degree, c, v);
            Rng       none; // never rolled: the seed is set, or nothing rolls
            Dice      dice = {&none, a.seed, i};
            int       j    = Turn(a, n, a.count == kCountSteps ? i : count, dice);
            if(a.seed > 0 && dice.Uniform(kRollVary) < a.vary)
                j = (int)(dice.Next(kRollVaryPick) % (uint32_t)n);
            count++;
            out[i] = StepNote((float)(ArpBase(a, cx, v, n) + v[(RootPlace(scratch, a.degree, v, n) + j) % n]));
        }
        return true;
    }

    /** The chord's notes in semitones above the tonic, ascending: stacked,
     *  voiced, and (Smooth) led from the last chord. Public for the tests. */
    int Voice(const Harmony& hm, int degree, const ChordParams& c, int* out)
    {
        const int n  = Notes(c);
        const int th = hm.chordKey ? 1 : 2; // a third: two scale steps, one chord tone
        int       stack[kPoly];
        for(int i = 0; i < n; i++)
            stack[i] = hm.DegreeSemis(degree + i * th);

        int best[kPoly];
        Arrange(stack, n, 0, c.voicing, best);
        if(c.smooth && prevN_ == n)
        {
            // home: the root position's centre, so a long progression cannot climb away
            const float home = Centre(best, n);
            float       bestCost = 1e9f;
            int         cand[kPoly];
            for(int inv = 0; inv < n; inv++)
                for(int sh = -12; sh <= 12; sh += 12)
                {
                    Arrange(stack, n, inv, c.voicing, cand);
                    float cost = 0.f;
                    for(int i = 0; i < n; i++)
                    {
                        cand[i] += sh;
                        cost += (float)std::abs(cand[i] - prev_[i]);
                    }
                    cost += 0.5f * std::fabs(Centre(cand, n) - home);
                    if(cost < bestCost - 1e-4f)
                    {
                        bestCost = cost;
                        std::copy(cand, cand + n, best);
                    }
                }
        }
        std::copy(best, best + n, prev_);
        prevN_ = n;
        std::copy(best, best + n, out);
        return n;
    }

  private:
    static constexpr long long kNoWin = (-9223372036854775807LL - 1);

    /** The chord's window in beats: the change, or a bar while the progression is off. */
    static double WindowBeats(const StepContext& cx) { return cx.prog.n > 0 ? kChangeBeats[cx.change] : 4.0; }

    /** RHYTHM Arp: the step's note of the voiced chord (see the top). */
    int Arpeggiate(const BlockClock& clk, const ArpParams& a, const ChordParams& c, const StepContext& cx,
                   Harmony& scratch, NoteEvent* out)
    {
        StepHit h;
        if(!grid_.Begin(clk, kArpDivBeats[a.div], a.length, a.density, a.rotate, cx.restart, cx.change, false, h))
            return 0;
        pos_ = h.pos;
        len_ = h.len;
        // the hits count from the chord change: its first is the root
        const double    wb  = WindowBeats(cx);
        const long long win = (long long)std::floor(h.start / wb);
        if(win != win_)
        {
            win_   = win;
            count_ = 0;
        }
        if(!h.play)
            return 0;

        scratch.Update(cx.key, ChordAt(h.start, cx.prog, cx.change, cx.manualChord));
        int       v[kPoly];
        const int n    = Voice(scratch, a.degree, c, v);
        Dice      dice = {&rng_, a.seed, h.pos};
        int       j    = Turn(a, n, a.count == kCountSteps ? h.pos : count_, dice); // places round the voicing from the root
        if(dice.Uniform(kRollVary) < a.vary)
            j = (int)(dice.Next(kRollVaryPick) % (uint32_t)n);
        count_++;
        const int vi = (RootPlace(scratch, a.degree, v, n) + j) % n;

        const int base = ArpBase(a, cx, v, n);
        const int r    = Ratchets(dice, a.ratchet);
        // rounded, not cut: a step of a whole number of samples comes out a hair over or under it, depending on
        // where the block began, and a cut would make the ratchet's gap one sample shorter for some block sizes
        const int gap = (int)(h.stepSmp / (float)r + 0.5f);
        // LET RING: to the sample the next window's first step would take (the grid's own rounding)
        const int end  = (int)std::ceil(((double)(win + 1) * wb - clk.beat0) / clk.bps - 1e-6);
        const int gate = std::max(1, (int)((0.05f + 0.95f * Clamp01(a.gate)) * h.stepSmp / (float)r + 0.5f));
        for(int i = 0; i < r; i++)
        {
            out[i].at    = h.at + i * gap;
            out[i].voice = vi;
            out[i].note  = (float)(base + v[vi]);
            out[i].gate  = c.letRing ? std::max(1, end - out[i].at) : gate;
        }
        return r;
    }

    /** RHYTHM Arp: places round the voicing from the root at count `cnt` (MODE, then SHIFT). */
    static int Turn(const ArpParams& a, int n, int cnt, Dice& dice)
    {
        int j;
        switch(a.mode)
        {
            case kArpDown: j = (n - cnt % n) % n; break;
            case kArpUpDown:
            {
                const int period = n > 1 ? 2 * n - 2 : 1;
                const int m      = cnt % period;
                j                = m < n ? m : period - m;
                break;
            }
            case kArpRandom: j = (int)(dice.Next(kRollNote) % (uint32_t)n); break;
            default: j = cnt % n; break; // Up (and Off, Loop)
        }
        return a.shift > 0 ? (j + a.shift) % n : j;
    }
    /** The root's place in the voicing `v` (its pitch class; voicings move notes by octaves only). */
    static int RootPlace(const Harmony& hm, int degree, const int* v, int n)
    {
        const int rpc = ((hm.DegreeSemis(degree) % 12) + 12) % 12;
        for(int i = 0; i < n; i++)
            if(((v[i] % 12) + 12) % 12 == rpc)
                return i;
        return 0;
    }
    /** RHYTHM Arp: the MIDI note the voicing's semitones sit on (the whole chord folded, so it keeps the voicing). */
    static int ArpBase(const ArpParams& a, const StepContext& cx, const int* v, int n)
    {
        const int base = kMidiBase + 12 + cx.tonic + 12 * (a.octave + cx.masterOctave);
        return base + FoldShift(base + v[0], base + v[n - 1]);
    }

    /** Inversion `inv` (the lowest `inv` notes up an octave), then the voicing, sorted. */
    static void Arrange(const int* stack, int n, int inv, int voicing, int* out)
    {
        for(int i = 0; i < n; i++)
            out[i] = stack[i] + (i < inv ? 12 : 0);
        std::sort(out, out + n);
        switch(voicing)
        {
            case kVoiceOpen:
                for(int i = 1; i < n; i += 2)
                    out[i] += 12;
                break;
            case kVoiceDrop2: out[n - 2] -= 12; break;
            case kVoiceSpread:
                out[0] -= 12;
                out[2] += 12;
                break;
            default: break;
        }
        std::sort(out, out + n);
    }
    static float Centre(const int* v, int n)
    {
        float s = 0.f;
        for(int i = 0; i < n; i++)
            s += (float)v[i];
        return s / (float)n;
    }

    float     sr_ = 48000.f;
    Rng       rng_;
    StepGrid  grid_;
    int       prev_[kPoly] = {};
    int       prevN_ = 0, pos_ = 0, len_ = 0;
    long long win_   = kNoWin; // Arp: the chord window of the last step, and the hits in it so far
    int       count_ = 0;
};

} // namespace ammo
