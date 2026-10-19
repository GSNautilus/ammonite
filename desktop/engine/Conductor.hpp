/** The conductor: the one master clock (host or internal), swing, the key,
 *  the chord now and the progression. Resolves each block's clock and
 *  harmony for the channels. */
#pragma once

#include "Clock.hpp"
#include "Dsp.hpp"
#include "Harmony.hpp"

namespace ammo {

struct ConductorParams
{
    float tempo = 110.f;
    int   sync = 0; // 0 host, 1 internal
    float swing = 0.f;
    int   root = 9, scale = 1, prog = 0, change = 1, chord = 0, restart = 0, octave = 0;
    int   progLen = 4, progStep[kProgSteps] = {0, 3, 4, 0}; // Progression = Custom
};

class Conductor
{
  public:
    void Init(float sampleRate)
    {
        sr_   = sampleRate;
        beat_ = 0.0;
        haveHost_ = false;
        bpm_      = 110.0;
    }

    /** Per block: resolve the clock and the harmony at the block's end. */
    void Begin(const ConductorParams& p, const HostClock* host, int frames, BlockClock& clk)
    {
        p_ = p;
        const bool useHost = p.sync == 0 && host != nullptr && host->valid;
        if(useHost)
        {
            bpm_      = host->bpm;
            haveHost_ = true;
        }
        else if(p.sync == 1 || !haveHost_)
            bpm_ = p.tempo;
        // playing: the host's transport, in either sync (Host takes its beat too, Internal keeps its own beat and
        // tempo). Stopped, the clock holds where it is (Play goes on from there) and the channels start no notes.
        // No clock at all (a test, the bench): the engine plays itself. A clock that is not valid still says play /
        // stop (Mini's: its own transport, no tempo or position).
        const bool playing = host == nullptr || host->playing;
        clk.bpm     = bpm_;
        clk.bps     = bpm_ / 60.0 / (double)sr_;
        clk.beat0   = useHost && host->playing ? host->beat : beat_;
        clk.frames  = frames;
        clk.playing = playing;
        clk.swing   = (double)Clamp01(p.swing) / 3.0; // not smoothed: timing stays identical for any block size
        beat_       = playing ? clk.beat0 + clk.bps * (double)frames : clk.beat0;
        clk_      = clk;

        prog_     = ProgOf(p.prog, p.progLen, p.progStep);
        chordNow_ = ChordAt(clk.lastBeat(), prog_, p.change, p.chord, &progPos_);
        harmony_.Update(p.scale, chordNow_);
    }

    const Harmony& harmony() const { return harmony_; }
    const BlockClock& clock() const { return clk_; }
    int    chordNow() const { return chordNow_; }
    int    progPos() const { return progPos_; }
    int    progLen() const { return prog_.n; }
    bool   hasHostClock() const { return haveHost_; }
    double beat() const { return beat_; }
    const ConductorParams& params() const { return p_; }

    StepContext stepContext(const Harmony* h) const
    {
        StepContext cx;
        cx.harmony      = h;
        cx.key          = p_.scale;
        cx.tonic        = p_.root;
        cx.prog         = prog_;
        cx.change       = p_.change;
        cx.manualChord  = p_.chord;
        cx.restart      = p_.restart;
        cx.masterOctave = p_.octave;
        return cx;
    }

  private:
    float           sr_ = 48000.f;
    ConductorParams p_;
    double          beat_ = 0.0, bpm_ = 110.0;
    bool            haveHost_ = false;
    BlockClock      clk_;
    Harmony         harmony_;
    Prog            prog_ = kProgs[0];
    int             chordNow_ = 0, progPos_ = -1;
};

} // namespace ammo
