/** The step clock every generator shares, and the note event they all
 *  produce. A generator asks the grid whether one of its steps begins in
 *  this block (its division on the master clock, with swing); the grid
 *  answers with the step's sample, its place in the pattern (counted from
 *  the RESTART point, mod LENGTH) and whether it is a Euclidean hit
 *  (DENSITY spreads the hits evenly over LENGTH, ROTATE turns them right by
 *  that many steps). The generator then decides what plays: one note (Arp,
 *  Bass) or several (Chord).
 *
 *  SEED (every generator's dice): Free rolls the generator's own free-running
 *  dice, so a pattern never repeats; a seed n takes each roll from a hash of
 *  (n, the step's place, which roll), so the pattern repeats every LENGTH and
 *  another seed gives another one. */
#pragma once

#include "Clock.hpp"
#include "Dsp.hpp"
#include "Harmony.hpp"

namespace ammo {

constexpr int kPoly = 6; // voices per channel: a chord's notes; Arp and Bass use the first (= AMMO_POLY)

/** What a generator asks a voice to do: start `note` at sample `at` of the
 *  block (may lie past the block: the channel carries it over), hold it for
 *  `gate` samples, at velocity `vel` (0..1; the drum voice uses it). A
 *  negative note releases the voice at `at` instead. */
struct NoteEvent
{
    int   at    = 0;
    int   voice = 0;
    float note  = 60.f;
    int   gate  = 1;
    float vel   = 1.f;
};

/** A step that begins in this block. */
struct StepHit
{
    long long k       = 0;     // the step's index on the master clock
    int       at      = 0;     // its sample in the block
    int       pos     = 0;     // its place in the pattern (from the restart point)
    int       len     = 1;     // LENGTH
    int       hits    = 1;     // Euclidean hits over LENGTH
    int       rot     = 0;     // ROTATE, 0..LENGTH-1
    bool      play    = false; // this step is one of them
    bool      joined  = false; // the generator came in mid-step and plays it from sample 0
    double    start   = 0.0;   // the step's beat (a hair inside it)
    float     stepSmp = 0.f;   // its length in samples (what is left of it, when joined)
};

class StepGrid
{
  public:
    /** The next block starts a step (e.g. when the generator is switched on). */
    void Reset() { last_ = kNone; }

    /** The last step begun (Begin), or false before the first. */
    bool Last(long long& k) const
    {
        k = last_;
        return last_ != kNone;
    }

    /** Whether a step of `d` beats begins in this block. Arriving mid-step
     *  (the first block after a Reset), the step is skipped, or with
     *  `joinMidStep` played from the block's first sample for what is left
     *  of it (a held chord should not wait for the next change). */
    bool Begin(const BlockClock& clk, double d, int length, float density, int rotate, int restart, int change,
               bool joinMidStep, StepHit& h)
    {
        // a step whose sample (rounded as below) is the block's last belongs to this block: without the same
        // epsilon here, a start a hair past the last sample's beat went to the next block, one sample late
        const long long k = StepIndex(clk.lastBeat() + 1e-6 * clk.bps, d, clk.swing);
        if(k == last_)
            return false;
        const bool first = last_ == kNone;
        last_            = k;
        const double st  = StepStart(k, d, clk.swing);
        h.joined         = false;
        if(first && st < clk.beat0 - kBeatEps)
        {
            if(!joinMidStep)
                return false; // we arrived mid-step: wait for the next one
            h.joined = true;
        }

        // the step's sample: a start exactly on a sample boundary must round the
        // same way whatever the block size, hence the epsilon before the ceil
        int at = (int)std::ceil((st - clk.beat0) / clk.bps - 1e-6);
        at     = at < 0 ? 0 : (at >= clk.frames ? clk.frames - 1 : at);

        Locate(k, d, length, density, rotate, restart, change, clk.swing, h);
        h.at      = h.joined ? 0 : at;
        h.stepSmp = h.joined ? (float)((st + StepBeats(k, d, clk.swing) - clk.beat0) / clk.bps)
                             : (float)(StepBeats(k, d, clk.swing) / clk.bps);
        return true;
    }

    /** Step `k`'s place in the pattern and whether it plays (k, pos, len, hits, rot, play, start): what Begin
     *  reports, for any step (the telemetry's look at a whole pattern). */
    static void Locate(long long k, double d, int length, float density, int rotate, int restart, int change,
                       double swing, StepHit& h)
    {
        // pattern position: steps since the restart point, mod LENGTH
        const double    sb  = StepStart(k, d, swing) + kBeatEps;
        // (a step as long as the restart period or longer counts freely: every one would be the first)
        const bool      fre = d >= RestartPeriod(restart, change);
        const long long k0  = fre ? 0 : StepIndex(RestartBeat(sb, restart, change) + kBeatEps, d, swing);
        const int       len = length < 1 ? 1 : length;
        h.k                 = k;
        h.pos               = (int)(((k - k0) % len + len) % len);
        h.len               = len;
        h.hits              = (int)(Clamp01(density) * (float)len + 0.5f);
        h.rot               = ((rotate % len) + len) % len;
        h.play              = Plays(h.pos, h.hits, len, h.rot);
        h.start             = sb;
    }

    /** Euclidean: `hits` spread evenly over `len`, the pattern turned right by `rot`. */
    static bool Plays(int pos, int hits, int len, int rot)
    {
        const int p = (pos + len - rot) % len;
        return (p * hits) % len < hits;
    }

    /** The hits before `pos` in the pattern (from its start): which hit of the pattern a hit at `pos` is. */
    static int HitsBefore(int pos, int hits, int len, int rot)
    {
        int c = 0;
        for(int q = 0; q < pos; q++)
            c += Plays(q, hits, len, rot) ? 1 : 0;
        return c;
    }

  private:
    static constexpr long long kNone = (-9223372036854775807LL - 1);
    long long last_ = kNone;
};

/** A step's dice. SEED 0 (Free): the generator's own free-running dice, `rng`; a seed: roll `roll` of the step
 *  at `pos`, from a hash of the three. */
struct Dice
{
    Rng* rng;
    int  seed, pos;
    uint32_t Next(uint32_t roll)
    {
        if(seed <= 0)
            return rng->Next();
        uint32_t x = (uint32_t)seed * 0x9E3779B1u ^ (uint32_t)pos * 0x85EBCA77u ^ roll * 0xC2B2AE3Du;
        x ^= x >> 16; // murmur3's finaliser: every input bit reaches every output bit
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }
    float Uniform(uint32_t roll) { return (float)(Next(roll) >> 8) * (1.f / 16777216.f); } // [0, 1), as Rng's
};
/** Which roll of a step (the hash keeps them apart). */
enum DiceRoll : uint32_t
{
    kRollNote = 1, // Random's note
    kRollVary,     // VARY: whether
    kRollVaryPick, // VARY: which
    kRollRatchet,  // RATCHET: whether
    kRollRatchetN, // RATCHET: how many
    kRollChance,   // Rhythm's CHANCE
    kRollVelocity  // Rhythm's VARY (the velocity)
};

/** COUNT: what moves a pattern's notes on. Auto: the generator's own way (the arp: Steps; Chord's Arp: its
 *  hits since the chord change); Steps: the step's place in the pattern; Hits: the hits since the pattern began. */
enum NoteCount
{
    kCountAuto = 0,
    kCountSteps,
    kCountHits
};

/** The telemetry's notes of a pattern (AmmoChannelFrame::stepNote): a rest, or a hit whose note is not known
 *  before it plays (Free dice). */
constexpr int kStepRest = -1, kStepUnknown = -2, kStepNotes = 16;

} // namespace ammo
