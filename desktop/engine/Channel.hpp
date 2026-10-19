/** One channel slot: generator -> instrument -> DRIVE -> pan -> CHORUS ->
 *  DELAY -> level / mute, plus what the telemetry needs (envelope, notes, a
 *  phase-aligned wave window, hits). Render runs the generator and the
 *  voices (mono); Mix runs the strip (stereo from the pan on, Fx.hpp).
 *
 *  Generators: Arp, Chord, Bass, Rhythm. Each turns the step that begins
 *  in a block into note events; the channel schedules them on its voices,
 *  carrying events that fall past the block (a strum, a ratchet, a roll)
 *  into the next. Instruments: the synth voice, kPoly of them (Arp, Bass
 *  and Rhythm play the first, a chord one voice per note), or the drum
 *  voice (every event retriggers it).
 *
 *  The telemetry also carries the notes of the pattern playing (stepNote),
 *  foreseen from the generator's settings and the chords to come, worked out
 *  again when a step begins or something they depend on changes; a note the
 *  Free dice pick cannot be foreseen, so such a step shows the note it last
 *  played.
 *
 *  Mute is a mixer mute: a gain that takes the output to 0 in kMuteSec and
 *  back, while the generator and the voices keep running (the pattern keeps
 *  its place, a tail is cut, not left to ring). */
#pragma once

#include "Arp.hpp"
#include "Bass.hpp"
#include "Chord.hpp"
#include "Conductor.hpp"
#include "DrumVoice.hpp"
#include "Dsp.hpp"
#include "Fx.hpp"
#include "Rhythm.hpp"
#include "Steps.hpp"
#include "SynthVoice.hpp"
#include "api.h"

#include <cmath>
#include <cstring>

namespace ammo {

enum GenType
{
    kGenOff = 0,
    kGenArp,
    kGenChord,
    kGenBass,
    kGenRhythm
};

enum InstType
{
    kInstSynth = 0,
    kInstDrum
};

struct ChannelParams
{
    int         gen = kGenOff, inst = 0;
    float       level = 0.7f, pan = 0.f;
    bool        mute = false, show = true;
    ArpParams   arp; // the arp's, and the step grid every generator reads (division, length, density, gate, vary, ratchet, degree, octave)
    ChordParams chord;
    int          bassPattern = kBassRoot;
    RhythmParams rhythm;
    VoiceParams  voice;
    DrumParams   drum;
    FxParams     fx; // the inserts
};

static_assert(kStepNotes == AMMO_STEPS, "the telemetry holds a whole pattern");

class Channel
{
  public:
    static constexpr int kHist = 8192; // > one period of MIDI 24 (32.7 Hz) at 192 kHz (5870)

    void Init(float sampleRate, int index)
    {
        sr_    = sampleRate;
        index_ = index;
        for(int v = 0; v < kPoly; v++)
            voices_[v].Init(sampleRate, 0x1234567u + 7919u * (uint32_t)index + 104723u * (uint32_t)v);
        arp_.Init(0xA5A5A5A5u + 104729u * (uint32_t)index);
        chord_.Init(sampleRate, 0xC3C3C3C3u + 104729u * (uint32_t)index);
        bass_.Init(0x5A5A5A5Au + 104729u * (uint32_t)index);
        rhythm_.Init(0x3C3C3C3Cu + 104729u * (uint32_t)index);
        drum_.Init(sampleRate, 0xD2D2D2D2u + 7919u * (uint32_t)index);
        lastInst_ = kInstSynth;
        level_.SetTime(0.02f, sampleRate, 1);
        pan_.SetTime(0.02f, sampleRate, 1);
        level_.Set(0.7f);
        pan_.Set(0.f);
        muteGain_ = 1.f;
        muteStep_ = 1.f / (kMuteSec * sampleRate);
        std::memset(hist_, 0, sizeof hist_);
        histPos_ = wrapPos_ = 0;
        wasOn_   = false;
        wasPlaying_ = true;
        lastGen_ = kGenOff;
        fresh_   = true;
        hitSeed_ = 0;
        noteSig_  = 0;
        std::memset(notes_, kStepRest, sizeof notes_);
        std::memset(played_, kStepUnknown, sizeof played_);
        nPending_ = 0;
        used_     = 1;
        std::memset(mono_, 0, sizeof mono_);
        drive_.Init(sampleRate);
        chorus_.Init(sampleRate);
        delay_.Init(sampleRate);
        silent_ = false;
        quiet_  = 0;
        ducker_.Init(sampleRate);
        duckAmt_.SetTime(0.02f, sampleRate, 1);
        revSend_.SetTime(0.02f, sampleRate, 1);
        duckAmt_.Set(0.f);
        revSend_.Set(0.f);
        wantEnv_ = false;
        std::memset(env_, 0, sizeof env_);
    }

    /** Smoothed-parameter targets come in already resolved per block. */
    void SetParams(const ChannelParams& p)
    {
        p_ = p;
        if(fresh_)
        {
            // the first block after Init: start on the values, no ramp from the table defaults
            level_.Set(p.level);
            pan_.Set(p.pan);
            muteGain_ = p.mute ? 0.f : 1.f;
            revSend_.Set(p.fx.revSend);
            fresh_    = false;
        }
        level_.Target(p.level);
        pan_.Target(p.pan);
        for(int v = 0; v < kPoly; v++)
            voices_[v].SetParams(p.voice);
        drum_.SetParams(p.drum);
    }

    /** Playing: a generator is on (muted or not). */
    bool Active() const { return p_.gen != kGenOff; }

    /** Another channel ducks from this one: Render also keeps the amp
     *  envelope per sample (Env()). */
    void SetWantEnv(bool want) { wantEnv_ = want; }
    const float* Env() const { return env_; }

    /** The generator and the instrument: this block's mono voice output
     *  (mono_), the amp envelope when wanted (env_: the loudest voice's, 0
     *  while muted), and the telemetry that comes from them. */
    void Render(const Conductor& cond, int n, AmmoChannelFrame& tf)
    {
        silent_ = false;
        RenderVoices(cond, n, tf);
        if(wantEnv_ && (silent_ || p_.mute))
            std::memset(env_, 0, sizeof(float) * (size_t)n); // a muted source ducks nothing
    }

  private:
    void RenderVoices(const Conductor& cond, int n, AmmoChannelFrame& tf)
    {
        const bool on = Active();
        if(on && (!wasOn_ || p_.gen != lastGen_))
        {
            std::memset(played_, kStepUnknown, sizeof played_); // another pattern: nothing has played in it
            arp_.Rearm();
            chord_.Rearm();
            bass_.Rearm();
            rhythm_.Rearm();
            if(wasOn_)
                Release(0); // a new generator: the old one's notes end
        }
        wasOn_   = on;
        lastGen_ = p_.gen;
        // the transport: stopped, the notes end (their releases and the effects ring out) and no new ones start;
        // Play arms the generators as when switched on (an arp waits for its next step, a held chord comes in)
        const bool play = cond.clock().playing;
        if(play != wasPlaying_)
        {
            if(play)
            {
                arp_.Rearm();
                chord_.Rearm();
                bass_.Rearm();
                rhythm_.Rearm();
            }
            else
                Release(0);
            wasPlaying_ = play;
        }
        const bool drum = p_.inst == kInstDrum;
        if(drum != (lastInst_ == kInstDrum))
            Release(0); // a new instrument: the synth's notes end (the drum just rings out)
        lastInst_ = p_.inst;

        tf.active = on ? 1 : 0;
        tf.muted  = p_.mute ? 1 : 0;
        tf.gen    = p_.gen;
        tf.hit    = 0;
        StepInfo(tf);
        if(!on)
        {
            noteSig_ = 0;
            std::memset(notes_, kStepRest, sizeof notes_);
            std::memset(played_, kStepUnknown, sizeof played_);
            std::memcpy(tf.stepNote, notes_, sizeof notes_);
            Release(0);
            if(!AnyActive())
            {
                // nothing sounds: the voices are skipped; the effects may still ring (Mix)
                tf.env = tf.rms = tf.peak = 0.f;
                tf.note       = drum ? lastDrumNote_ : voices_[0].Note();
                tf.pitchClass = ((int)std::lround(tf.note) % 12 + 12) % 12;
                tf.voices     = 0;
                std::memset(tf.wave, 0, sizeof tf.wave);
                std::memset(mono_, 0, sizeof(float) * (size_t)n);
                silent_ = true;
                return;
            }
        }

        // the generator: this block's events, after the ones carried over
        NoteEvent ev[kMaxEvents];
        int       ne = 0;
        for(int i = 0; i < nPending_; i++)
            ev[ne++] = pending_[i];
        nPending_ = 0;
        if(on)
        {
            Harmony           scratch = cond.harmony();
            const StepContext cx      = cond.stepContext(&scratch);
            const int         ne0     = ne; // the generator's own events from here
            switch(play ? p_.gen : kGenOff)
            {
                case kGenArp:
                    if(p_.arp.mode != kArpOff)
                    {
                        ArpEvent a;
                        if(arp_.Step(cond.clock(), p_.arp, cx, scratch, a))
                            for(int j = 0; j < a.ratchets && ne < kMaxEvents; j++)
                                ev[ne++] = NoteEvent{a.at + j * (a.gap > 0 ? a.gap : 1), 0, a.note, a.gate};
                    }
                    used_ = 1;
                    break;
                case kGenChord:
                {
                    NoteEvent c[kPoly];
                    const int k = chord_.Step(cond.clock(), p_.arp, p_.chord, cx, scratch, c);
                    for(int j = 0; j < k && ne < kMaxEvents; j++)
                        ev[ne++] = c[j];
                    if(p_.chord.rhythm == kChordArp)
                        used_ = p_.chord.letRing ? ChordGen::Notes(p_.chord) : 1; // a rung chord as loud as a held one
                    else if(k > 0)
                    {
                        // fewer notes than the last chord: the extra voices end where the new chord starts
                        for(int v = k; v < kPoly && ne < kMaxEvents; v++)
                            ev[ne++] = NoteEvent{c[0].at, v, -1.f, 0};
                        used_ = k;
                    }
                    break;
                }
                case kGenBass:
                {
                    NoteEvent b[4];
                    const int k = bass_.Step(cond.clock(), p_.arp, p_.bassPattern, cx, scratch, b);
                    for(int j = 0; j < k && ne < kMaxEvents; j++)
                        ev[ne++] = b[j];
                    used_ = 1;
                    break;
                }
                case kGenRhythm:
                {
                    NoteEvent r[4];
                    const int k = rhythm_.Step(cond.clock(), p_.arp, p_.rhythm, cx, scratch, r);
                    for(int j = 0; j < k && ne < kMaxEvents; j++)
                        ev[ne++] = r[j];
                    used_ = 1;
                    break;
                }
                default: break;
            }
            if(drum)
                used_ = 1; // one drum voice, whatever plays it
            StepInfo(tf);
            if(ne > ne0 && ev[ne0].note >= 0.f && tf.step >= 0 && tf.step < kStepNotes)
            {
                played_[tf.step] = StepNote(ev[ne0].note); // a step's note as it played (a chord: its lowest)
                noteSig_         = 0;                      // into stepNote where it was not foreseen
            }
            PatternNotes(cond, cx);
        }
        std::memcpy(tf.stepNote, notes_, sizeof notes_);
        // events past this block wait for the next
        int now = 0;
        for(int i = 0; i < ne; i++)
        {
            if(ev[i].at >= n)
            {
                if(nPending_ < kMaxEvents)
                {
                    pending_[nPending_]    = ev[i];
                    pending_[nPending_].at -= n;
                    nPending_++;
                }
            }
            else
                ev[now++] = ev[i];
        }
        ne = now;

        // the drum takes every event, in time order (it runs every sample, sounding or not)
        if(drum)
        {
            int i = 0;
            for(;;)
            {
                int next = -1;
                for(int e = 0; e < ne; e++)
                    if(ev[e].voice >= 0 && ev[e].note >= 0.f && (next < 0 || ev[e].at < ev[next].at))
                        next = e;
                if(next < 0)
                    break;
                if(ev[next].at > i)
                {
                    drum_.Process(drumOut_ + i, ev[next].at - i, wantEnv_ ? drumEnv_ + i : nullptr);
                    i = ev[next].at;
                }
                drum_.Hit(ev[next].note, ev[next].vel);
                lastDrumNote_ = ev[next].note;
                tf.hit        = 1;
                hitSeed_      = hitSeed_ * 1664525u + 1013904223u;
                ev[next].voice = -1;
            }
            if(i < n)
                drum_.Process(drumOut_ + i, n - i, wantEnv_ ? drumEnv_ + i : nullptr);
            ne = 0; // the synth voices get none
        }

        // the synth voices, each split at its own events; the first writes, the others add
        for(int v = 0; v < kPoly; v++)
        {
            bool mine = false;
            for(int i = 0; i < ne && !mine; i++)
                mine = ev[i].voice == v;
            // a chord's voices run all the time, so a voice's phase never depends on where blocks
            // begin; other generators only use the first, and the rest only to finish a release
            if(v > 0 && p_.gen != kGenChord && !mine && !voices_[v].Active())
                continue;
            float* dst = v == 0 ? mono_ : tmp_;
            float* den = !wantEnv_ ? nullptr : (v == 0 ? env_ : envTmp_);
            int    i   = 0;
            for(int at = 0; at < n && mine;)
            {
                // this voice's next event at or after `at`
                int next = -1;
                for(int e = 0; e < ne; e++)
                    if(ev[e].voice == v && ev[e].at >= at && (next < 0 || ev[e].at < ev[next].at))
                        next = e;
                if(next < 0)
                    break;
                if(ev[next].at > i)
                {
                    voices_[v].Process(dst + i, ev[next].at - i, den ? den + i : nullptr);
                    i = ev[next].at;
                }
                if(ev[next].note < 0.f)
                    voices_[v].NoteOff();
                else
                {
                    voices_[v].NoteOn(ev[next].note, ev[next].gate);
                    tf.hit   = 1;
                    hitSeed_ = hitSeed_ * 1664525u + 1013904223u;
                }
                ev[next].voice = -1; // done
                at             = i;
            }
            if(i < n)
                voices_[v].Process(dst + i, n - i, den ? den + i : nullptr);
            if(v > 0)
            {
                for(int k = 0; k < n; k++)
                    mono_[k] += tmp_[k];
                if(den)
                    for(int k = 0; k < n; k++)
                        env_[k] = std::fmax(env_[k], envTmp_[k]);
            }
        }
        if(drum)
        {
            for(int k = 0; k < n; k++)
                mono_[k] += drumOut_[k];
            if(wantEnv_)
                std::memcpy(env_, drumEnv_, sizeof(float) * (size_t)n);
        }
        if(used_ > 1)
        {
            const float g = 1.f / std::sqrt((float)used_); // a chord as loud as one note, roughly
            for(int k = 0; k < n; k++)
                mono_[k] *= g;
        }

        // the voices' telemetry
        float env = voices_[0].Level();
        int   nv  = 0;
        for(int v = 0; v < kPoly; v++)
            if(voices_[v].Active() || v == 0)
            {
                if(v > 0 && voices_[v].Level() > env)
                    env = voices_[v].Level();
                if(voices_[v].Active() && nv < AMMO_POLY)
                    tf.voiceNote[nv++] = voices_[v].Target();
            }
        if(drum)
        {
            env = drum_.Level();
            nv  = drum_.Active() ? 1 : 0;
            tf.voiceNote[0] = lastDrumNote_;
        }
        tf.voices     = nv;
        tf.env        = env;
        tf.note       = drum ? lastDrumNote_ : voices_[0].Target();
        tf.pitchClass = ((int)std::lround(tf.note) % 12 + 12) % 12;
        tf.hitSeed    = hitSeed_;
    }

  public:
    /** The channel strip on Render's output: DRIVE -> pan -> CHORUS -> DELAY
     *  -> level, duck and mute, added into outL / outR, and the REVERB SEND
     *  from there into sendL / sendR; the history and the level telemetry.
     *  `duckEnv`: the DUCK source's amp envelope this block (Render of that
     *  channel), or null. A channel with nothing sounding is skipped once
     *  its effects have rung out. */
    void Mix(const Conductor& cond, float* outL, float* outR, float* sendL, float* sendR, int n, const float* duckEnv,
             AmmoChannelFrame& tf)
    {
        tf.duck = 0.f;
        if(silent_ && !Ringing())
        {
            tf.rms = tf.peak = 0.f;
            return;
        }
        const FxParams& fx = p_.fx;

        // level and pan, per sample; the history (the instrument's own wave, at the channel's level, before the
        // effects smear it and before the mute: a muted channel still shows its wave)
        for(int k = 0; k < n; k++)
        {
            const float lv = level_.Next();
            gain_[k]       = lv * lv; // audio taper
            panA_[k]       = (pan_.Next() + 1.f) * 0.25f * kPi;
        }
        if(!silent_)
            for(int k = 0; k < n; k++)
            {
                hist_[histPos_] = mono_[k] * gain_[k];
                histPos_        = (histPos_ + 1) & (kHist - 1);
            }

        drive_.Process(mono_, n, fx.drive, fx.tone);
        for(int k = 0; k < n; k++)
        {
            L_[k] = mono_[k] * std::cos(panA_[k]);
            R_[k] = mono_[k] * std::sin(panA_[k]);
        }
        chorus_.Process(L_, R_, n, fx.chorusOn, fx.chorusMode, fx.chorusRate, fx.chorusDepth, fx.chorusMix);
        const double bps = cond.clock().bps;
        delay_.Process(L_, R_, n, bps > 0.0 ? kDelayDivBeats[fx.delayDiv] / bps : 1.0, fx.delayFb, fx.delayDamp,
                       fx.delayPingPong, fx.delayMix);

        // level, duck, mute, out, send; how long the effects have been quiet (before the level), for Ringing()
        double      sumSq = 0.0;
        float       pk2 = 0.f, fxPk = 0.f, dkMax = 0.f;
        const float muteTo = p_.mute ? 0.f : 1.f;
        duckAmt_.Target(duckEnv ? fx.duckAmt : 0.f);
        revSend_.Target(fx.revSend);
        const bool ducking = duckEnv || ducker_.d > 1e-6f || !duckAmt_.Settled();
        const bool sending = fx.revSend > 0.f || revSend_.value > 0.f;
        for(int k = 0; k < n; k++)
        {
            if(muteGain_ != muteTo)
                muteGain_ = muteTo > muteGain_ ? std::fmin(muteTo, muteGain_ + muteStep_) : std::fmax(muteTo, muteGain_ - muteStep_);
            float g = gain_[k] * muteGain_;
            if(ducking)
            {
                const float dk = duckAmt_.Next() * std::fmin(1.f, ducker_.Next(duckEnv ? duckEnv[k] : 0.f));
                g *= 1.f - dk;
                if(dk > dkMax)
                    dkMax = dk;
            }
            const float l = L_[k] * g, r = R_[k] * g;
            outL[k] += l;
            outR[k] += r;
            if(sending)
            {
                const float s = revSend_.Next();
                if(s < 1e-6f && fx.revSend <= 0.f)
                    revSend_.value = 0.f;
                sendL[k] += l * s;
                sendR[k] += r * s;
            }
            const float e2 = l * l + r * r; // the mono level the pan law keeps
            sumSq += (double)e2;
            if(e2 > pk2)
                pk2 = e2;
            const float a = std::fmax(std::fabs(L_[k]), std::fabs(R_[k]));
            if(a > fxPk)
                fxPk = a;
        }
        quiet_ = silent_ && fxPk < 1e-6f ? quiet_ + n : 0;

        tf.rms  = (float)std::sqrt(sumSq / (n > 0 ? n : 1));
        tf.peak = std::sqrt(pk2);
        tf.duck = dkMax;
        if(silent_)
            return;
        // one period of the output ending at the most recent cycle start of
        // the first voice (the voice and the history count the same samples,
        // so its sample number is a history index)
        const bool drum   = p_.inst == kInstDrum;
        const int  period = (int)(drum ? drum_.PeriodSamples() : voices_[0].PeriodSamples());
        wrapPos_          = (int)((drum ? drum_.LastWrapSample() : voices_[0].LastWrapSample()) & (uint32_t)(kHist - 1));
        for(int k = 0; k < AMMO_WAVE_LEN; k++)
        {
            const int idx = (wrapPos_ - period + (period * k) / AMMO_WAVE_LEN + kHist) & (kHist - 1);
            tf.wave[k]    = hist_[idx];
        }
    }

  private:
    static constexpr int   kMaxBlock  = 4096;
    static constexpr int   kMaxEvents = 32;
    static constexpr float kMuteSec   = 0.01f;

    /** The inserts still sound after the voices stopped: an engaged effect
     *  has not yet been quiet for longer than it can hold a sound. */
    bool Ringing() const
    {
        if(!drive_.Engaged() && !chorus_.Engaged() && !delay_.Engaged())
            return false;
        const int span = Drive::kLatency + 64 + (chorus_.Engaged() ? chorus_.Span() : 0) +
                         (delay_.Engaged() ? (int)delay_.Samples() + 4 : 0);
        return quiet_ <= span;
    }

    void StepInfo(AmmoChannelFrame& tf) const
    {
        switch(p_.gen)
        {
            case kGenChord:
                tf.step   = chord_.StepPos();
                tf.length = chord_.StepLen();
                break;
            case kGenBass:
                tf.step   = bass_.StepPos();
                tf.length = bass_.StepLen();
                break;
            case kGenRhythm:
                tf.step   = rhythm_.StepPos();
                tf.length = rhythm_.StepLen();
                break;
            default:
                tf.step   = arp_.StepPos();
                tf.length = arp_.StepLen();
                break;
        }
    }
    /** The notes of the pattern playing into notes_ (the generator's Predict), when a step has begun or anything
     *  they depend on changed since the last time: a signature of all of it. */
    void PatternNotes(const Conductor& cond, const StepContext& cx)
    {
        long long k   = 0;
        bool      any = false;
        switch(p_.gen)
        {
            case kGenArp: any = arp_.LastStep(k); break;
            case kGenChord: any = chord_.LastStep(k); break;
            case kGenBass: any = bass_.LastStep(k); break;
            case kGenRhythm: any = rhythm_.LastStep(k); break;
            default: break;
        }
        const ArpParams& a     = p_.arp;
        const double     swing = cond.clock().swing;
        uint32_t         h     = 2166136261u; // FNV-1a
        const auto       mix   = [&h](uint32_t x) {
            for(int b = 0; b < 4; b++, x >>= 8)
                h = (h ^ (x & 0xFFu)) * 16777619u;
        };
        float vary = a.vary > 0.f ? 1.f : 0.f; // only whether it varies: the note is then unknown, or seeded
        if(a.seed > 0)
            vary = a.vary;
        const int ints[] = {any ? 1 : 0, (int)k, (int)(k >> 32), p_.gen, a.mode, a.pool, a.range, a.div, a.length,
                            a.degree, a.octave, a.rotate, a.shift, a.count, a.seed, p_.chord.notes, p_.chord.voicing,
                            p_.chord.rhythm, p_.chord.smooth ? 1 : 0, p_.bassPattern, p_.rhythm.pitch, cx.key, cx.tonic,
                            cx.change, cx.manualChord, cx.restart, cx.masterOctave, cx.prog.n};
        for(int x : ints)
            mix((uint32_t)x);
        for(int i = 0; i < cx.prog.n; i++)
            mix((uint32_t)cx.prog.deg[i]);
        uint32_t fb;
        const float fs[] = {a.density, vary, a.seed > 0 ? p_.rhythm.chance : 1.f, (float)swing};
        for(float f : fs)
        {
            std::memcpy(&fb, &f, 4);
            mix(fb);
        }
        h |= 1u; // never 0: 0 is "not worked out"
        if(h == noteSig_)
            return;
        noteSig_ = h;
        std::memset(notes_, kStepRest, sizeof notes_);
        if(!any)
            return;
        Harmony scratch = cond.harmony();
        switch(p_.gen)
        {
            case kGenArp:
                if(a.mode != kArpOff)
                    arp_.Predict(swing, a, cx, scratch, notes_);
                break;
            case kGenChord: chord_.Predict(swing, a, p_.chord, cx, scratch, notes_); break;
            case kGenBass: bass_.Predict(swing, a, p_.bassPattern, cx, scratch, notes_); break;
            case kGenRhythm: rhythm_.Predict(swing, a, p_.rhythm, cx, scratch, notes_); break;
            default: break;
        }
        for(int i = 0; i < kStepNotes; i++)
            if(notes_[i] == kStepUnknown)
                notes_[i] = played_[i]; // not foreseen: what it last played (kStepUnknown until it has)
    }

    /** Release voices from `from` up (and drop their pending notes). */
    void Release(int from)
    {
        for(int v = from; v < kPoly; v++)
            if(voices_[v].Active())
                voices_[v].NoteOff();
        int k = 0;
        for(int i = 0; i < nPending_; i++)
            if(pending_[i].voice < from)
                pending_[k++] = pending_[i];
        nPending_ = k;
    }
    bool AnyActive() const
    {
        if(p_.inst == kInstDrum && drum_.Active())
            return true;
        for(int v = 0; v < kPoly; v++)
            if(voices_[v].Active())
                return true;
        return false;
    }

    float         sr_ = 48000.f;
    int           index_ = 0;
    ChannelParams p_;
    SynthVoice    voices_[kPoly];
    Arp           arp_;
    ChordGen      chord_;
    BassGen       bass_;
    RhythmGen     rhythm_;
    DrumVoice     drum_;
    float         drumOut_[4096];
    float         lastDrumNote_ = 60.f;
    int           lastInst_ = kInstSynth;
    Smoother      level_, pan_;
    float         muteGain_ = 1.f, muteStep_ = 1.f / 480.f;
    float         mono_[kMaxBlock], tmp_[kMaxBlock];
    float         gain_[kMaxBlock], panA_[kMaxBlock], L_[kMaxBlock], R_[kMaxBlock]; // the strip, per sample
    Drive         drive_;
    Chorus        chorus_;
    Delay         delay_;
    bool          silent_ = false; // Render had nothing sounding (mono_ is 0)
    int           quiet_  = 0;     // samples the inserts have been silent since
    float         env_[kMaxBlock], envTmp_[kMaxBlock], drumEnv_[kMaxBlock]; // the amp envelope, for a ducked channel
    bool          wantEnv_ = false;
    Ducker        ducker_;
    Smoother      duckAmt_, revSend_;
    float         hist_[kHist];
    int           histPos_ = 0, wrapPos_ = 0;
    bool          wasOn_ = false, wasPlaying_ = true, fresh_ = true;
    int           lastGen_ = kGenOff, used_ = 1;
    NoteEvent     pending_[kMaxEvents];
    int           nPending_ = 0;
    uint32_t      hitSeed_ = 0;
    int8_t        notes_[kStepNotes];  // the pattern's notes (PatternNotes), for the telemetry
    int8_t        played_[kStepNotes]; // the note each step last played (kStepUnknown: none since the pattern began)
    uint32_t      noteSig_ = 0;       // what they were worked out from (0: not yet)
};

} // namespace ammo
