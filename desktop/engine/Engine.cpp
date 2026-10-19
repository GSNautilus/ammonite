#include "Engine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(__aarch64__)
# include <cstdint>
#else
# include <xmmintrin.h>
#endif

namespace ammo {

namespace
{
std::atomic<int> gInstances{0};

#if defined(__aarch64__)
// Ammonite Mini (Android, ARM64): denormals flushed by FPCR.FZ (bit 24), as FTZ/DAZ do on x86
struct ScopedFlushToZero
{
    uint64_t saved;
    ScopedFlushToZero()
    {
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(saved));
        const uint64_t fz = saved | (1ull << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fz));
    }
    ~ScopedFlushToZero() { __asm__ __volatile__("msr fpcr, %0" : : "r"(saved)); }
};
#else
struct ScopedFlushToZero
{
    unsigned int saved;
    ScopedFlushToZero() : saved(_mm_getcsr()) { _mm_setcsr(saved | 0x8040); }
    ~ScopedFlushToZero() { _mm_setcsr(saved); }
};
#endif

inline int   I(float v) { return (int)std::lround(v); }
constexpr float kBusGain = 2.f;

/** Version 2's modulation ids (retired, hidden): read from a version 2 chunk only, to convert it. */
bool IsRetiredId(int id)
{
    int slot, p;
    if(IsGlobalId(id))
        return id >= gLfo1Shape && id < kNumGlobalParams;
    return IsSlotId(id, slot, p) && p >= kFirstRetiredSlotParam && p < kEndRetiredSlotParam;
}

/** A version 2 chunk's modulation into the pool: every live route becomes a
 *  pool route (bipolar, on the same slot parameter), in the order slot 1 route 1 .. slot 16 route 4; each LFO such
 *  a route takes becomes a pool LFO the first time it is met (a channel's LFO with Restart Note restarts on that
 *  channel's notes; a global one runs free of notes); a routed macro is marked in use. LFOs no live route takes
 *  are dropped (they moved nothing). Past 32 LFOs (16 channels with both and both global ones), the routes of the
 *  extra ones are dropped. Then the retired ids go back to their defaults. */
void ConvertV2(float* v)
{
    int lfoOf[kSlots][kSlotLfos], glfoOf[2] = {-1, -1}, nl = 0, nr = 0;
    for(int s = 0; s < kSlots; s++)
        for(int l = 0; l < kSlotLfos; l++)
            lfoOf[s][l] = -1;
    // a retired LFO's five settings (shape, sync, rate, division, phase) into pool LFO `l`
    const auto newLfo = [&](const float* p, int restart) {
        if(nl >= kPoolLfos)
            return -1;
        const int l = nl++;
        v[LfoId(l, lOn)]      = 1.f;
        v[LfoId(l, lShape)]   = p[0];
        v[LfoId(l, lSync)]    = p[1];
        v[LfoId(l, lRate)]    = p[2];
        v[LfoId(l, lDiv)]     = p[3];
        v[LfoId(l, lPhase)]   = p[4];
        v[LfoId(l, lRestart)] = (float)restart;
        return l;
    };
    for(int s = 0; s < kSlots; s++)
        for(int r = 0; r < kRoutes; r++)
        {
            const float* sv  = v + SlotId(s, 0);
            const int    b   = cMod1Src + kRouteParams * r;
            const int    src = I(sv[b]), dest = I(sv[b + 1]);
            const float  amt = sv[b + 2];
            if(src < 1 || src > 8 || dest < 1 || dest >= kNumModDests || amt == 0.f || nr >= kPoolRoutes)
                continue;
            int ps;
            if(src <= 2) // the channel's LFO 1 or 2
            {
                const int    l  = src - 1;
                const float* lp = sv + cLfo1Shape + kLfoParams * l;
                if(lfoOf[s][l] < 0)
                    lfoOf[s][l] = newLfo(lp, I(lp[5]) == 1 ? s + 1 : 0);
                if(lfoOf[s][l] < 0)
                    continue;
                ps = kSrcLfo1 + lfoOf[s][l];
            }
            else if(src <= 4) // a global LFO
            {
                const int l = src - 3;
                if(glfoOf[l] < 0)
                    glfoOf[l] = newLfo(v + gLfo1Shape + kGlobalLfoParams * l, 0);
                if(glfoOf[l] < 0)
                    continue;
                ps = kSrcLfo1 + glfoOf[l];
            }
            else // Macro 1..4
            {
                ps                     = kSrcMacro1 + src - 5;
                v[MacroOnId(src - 5)] = 1.f;
            }
            const int q            = nr++;
            v[RouteId(q, rSrc)]    = (float)ps;
            v[RouteId(q, rTarget)] = (float)SlotId(s, kModDests[dest]);
            v[RouteId(q, rAmt)]    = amt;
            v[RouteId(q, rPol)]    = 1.f;
        }
    for(int id = 0; id < kNumIds; id++)
        if(IsRetiredId(id))
            v[id] = ParamInfo(id).def;
}

/** A chunk before version 4: ROTATE was the Rhythm generator's alone, so a value another generator's channel kept
 *  (set while it was a Rhythm) goes back to 0, and the channel plays as it did. */
void ConvertV3(float* v)
{
    for(int s = 0; s < kSlots; s++)
        if((int)std::lround(v[SlotId(s, cGen)]) != kGenRhythm)
            v[SlotId(s, cRhythmRotate)] = 0.f;
}

void AppendLine(std::string& out, int id, float v)
{
    char line[64];
    std::snprintf(line, sizeof line, "%d %.7g\n", id, (double)v);
    out += line;
}
} // namespace

Engine::Engine(double sampleRate, int maxBlock) : instance_(++gInstances)
{
    maxBlock_ = maxBlock > 0 && maxBlock <= kMaxBlock ? maxBlock : kMaxBlock;
    for(int l = 0; l < kPoolLfos; l++)
        lfo_[l].Init(0x6C8E9CF5u + 7919u * (uint32_t)l); // LFO 1's seed is version 2's channel 1 LFO 1's
    for(int id = 0; id < kNumIds; id++)
        params_[id].store(ParamInfo(id).def, std::memory_order_relaxed);
    ResetToDefaults();
    SetSampleRate(sampleRate);
}

void Engine::SetSampleRate(double sampleRate)
{
    sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    cond_.Init((float)sr_);
    for(int s = 0; s < kSlots; s++)
        ch_[s].Init((float)sr_, s);
    volume_.SetTime(0.02f, (float)sr_, 1);
    volume_.Set(params_[gVolume].load());
    tap_.Init(sr_);
    glue_.Init((float)sr_);
    limiter_.Init((float)sr_);
    volFresh_ = true;
    reverb_.Init((float)sr_);
}

void Engine::ResetToDefaults()
{
    for(int id = 0; id < kNumIds; id++)
        params_[id].store(ParamInfo(id).def, std::memory_order_relaxed);
    for(int i = 0; i < kDefaultPatchSize; i++)
        params_[SlotId(kDefaultPatch[i].slot, kDefaultPatch[i].param)].store(kDefaultPatch[i].value,
                                                                            std::memory_order_relaxed);
}

void Engine::SetParam(int id, float value)
{
    if(id < 0 || id >= kNumIds)
        return;
    if(ParamInfo(id).flags & pfHidden)
        return;
    params_[id].store(ClampParam(id, value), std::memory_order_relaxed);
}

float Engine::GetParam(int id) const
{
    return id >= 0 && id < kNumIds ? params_[id].load(std::memory_order_relaxed) : 0.f;
}

void Engine::Process(float* outL, float* outR, int frames, const HostClock* clock)
{
    ScopedFlushToZero ftz;
    int       done = 0;
    HostClock sub;
    if(clock)
        sub = *clock;
    while(done < frames)
    {
        const int n = frames - done < maxBlock_ ? frames - done : maxBlock_;
        ProcessBlock(outL + done, outR + done, n, clock ? &sub : nullptr);
        if(clock && sub.playing)
            sub.beat += sub.bpm / 60.0 / sr_ * (double)n;
        done += n;
    }
}

bool Engine::PrepareMod()
{
    const auto get = [this](int id) { return params_[id].load(std::memory_order_relaxed); };
    // the live routes, sorted by target (an insertion: routes on one target stay in route order, so they add in it)
    bool     grid = false;
    uint32_t taken = 0;
    nLive_         = 0;
    for(int r = 0; r < kPoolRoutes; r++)
    {
        const PoolRoute rt = ReadRoute(get, r);
        if(!Live(rt))
            continue;
        int k = nLive_++;
        for(; k > 0 && live_[k - 1].target > rt.target; k--)
        {
            live_[k]    = live_[k - 1];
            liveIdx_[k] = liveIdx_[k - 1];
        }
        live_[k]    = rt;
        liveIdx_[k] = r;
        if(rt.src < kSrcMacro1)
            taken |= 1u << (rt.src - kSrcLfo1);
        int slot, p;
        if(!grid)
            grid = !IsSlotId(rt.target, slot, p) || I(get(SlotId(slot, cGen))) != kGenOff;
    }
    // the LFOs that run: in use, or taken by a live route; and per slot the ones its notes restart
    lfoRun_ = taken;
    for(int s = 0; s < kSlots; s++)
        restartOn_[s] = 0;
    for(int l = 0; l < kPoolLfos; l++)
    {
        if(get(LfoId(l, lOn)) >= 0.5f)
            lfoRun_ |= 1u << l;
        if(!(lfoRun_ >> l & 1u))
            continue;
        lfoP_[l] = ReadLfo(get, l);
        if(lfoP_[l].restart >= 1 && lfoP_[l].restart <= kSlots)
            restartOn_[lfoP_[l].restart - 1] |= 1u << l;
    }
    return grid;
}

void Engine::ProcessBlock(float* outL, float* outR, int n, const HostClock* clock)
{
    // the CPU meter: this block's render time (steady_clock: the performance counter, no allocation) over its length
    const auto t0 = std::chrono::steady_clock::now();
    std::memset(outL, 0, sizeof(float) * n);
    std::memset(outR, 0, sizeof(float) * n);

    // one telemetry frame per block; with a live route the block runs in stretches on the modulation grid
    // (kModBlock samples counted from the start, so any block size gives the same audio) and the frame takes
    // in each: hits and the deepest values over all of them, levels as the block's RMS, the rest from the last
    AmmoFrame& f    = tele_.beginWrite();
    const bool grid = PrepareMod();
    for(int r = 0; r < kPoolRoutes; r++)
        f.route[r] = -1.f;
    double     sq[kSlots] = {}, masterSq = 0.0;
    float      pk[kSlots] = {}, dk[kSlots] = {}, masterPk = 0.f, glue = 0.f, lim = 0.f;
    int        hit[kSlots] = {};
    BlockClock clk;
    HostClock  sub;
    if(clock)
        sub = *clock;
    for(int done = 0; done < n;)
    {
        int m = n - done;
        if(grid)
        {
            const int left = kModBlock - (int)(pos_ % (uint64_t)kModBlock);
            m              = m < left ? m : left;
        }
        SubBlock(outL + done, outR + done, m, clock ? &sub : nullptr, grid, f, clk);
        for(int s = 0; s < kSlots; s++)
        {
            const AmmoChannelFrame& c = f.ch[s];
            sq[s] += (double)c.rms * c.rms * m;
            pk[s] = std::fmax(pk[s], c.peak);
            dk[s] = std::fmax(dk[s], c.duck);
            hit[s] |= c.hit;
        }
        masterSq += (double)f.masterRms * f.masterRms * m;
        masterPk = std::fmax(masterPk, f.masterPeak);
        glue     = std::fmax(glue, f.glueGr);
        lim      = std::fmax(lim, f.limitGr);
        if(clock && sub.playing)
            sub.beat += sub.bpm / 60.0 / sr_ * (double)m;
        done += m;
        pos_ += (uint64_t)m;
    }
    for(int s = 0; s < kSlots; s++)
    {
        AmmoChannelFrame& c = f.ch[s];
        c.rms               = (float)std::sqrt(sq[s] / n);
        c.peak              = pk[s];
        c.duck              = dk[s];
        c.hit               = hit[s];
    }
    f.masterRms  = (float)std::sqrt(masterSq / n);
    f.masterPeak = masterPk;
    f.glueGr     = glue;
    f.limitGr    = lim;

    // the frame
    const ConductorParams& cp = cond_.params();
    f.seq          = ++seq_;
    f.beat         = clk.playing ? clk.lastBeat() : clk.beat0; // stopped: the beat the clock holds
    f.bpm          = clk.bpm;
    f.playing      = clk.playing ? 1 : 0;
    f.hostClock    = cond_.hasHostClock() ? 1 : 0;
    f.chordRootPc  = (cp.root + cond_.harmony().chordRootSemis) % 12;
    f.chordDeg     = cond_.chordNow() % 7;
    f.chordQuality = cond_.harmony().Quality();
    f.progPos      = cond_.progPos();
    f.progLen      = cond_.progLen();
    cond_.harmony().Masks(cp.root, f.keyMask, f.chordMask);
    f.instance   = instance_;
    f.sampleRate = sr_;
    f.block      = (uint32_t)n;
    f.cpu        = (float)(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * sr_ / n);
    tele_.publish();
}

void Engine::SubBlock(float* outL, float* outR, int n, const HostClock* clock, bool grid, AmmoFrame& f, BlockClock& clk)
{
    // the conductor: this stretch's clock and harmony (its swing as modulated, below)
    ConductorParams cp;
    cp.tempo   = params_[gTempo].load();
    cp.sync    = I(params_[gSync].load());
    cp.swing   = params_[gSwing].load();
    cp.root    = I(params_[gRoot].load());
    cp.scale   = I(params_[gScale].load());
    cp.prog    = I(params_[gProg].load());
    cp.change  = I(params_[gChange].load());
    cp.chord   = I(params_[gChord].load());
    cp.restart = I(params_[gRestart].load());
    cp.octave  = I(params_[gOctave].load());
    cp.progLen = I(params_[gProgLen].load());
    for(int i = 0; i < kProgSteps; i++)
        cp.progStep[i] = I(params_[gProgStep1 + i].load());
    cond_.Begin(cp, clock, n, clk);

    // the modulation sources: taken on the grid points (or once per block while no route is live), each LFO then
    // advanced to the next one; a stretch that starts between grid points keeps the last ones
    const int  phase = (int)(pos_ % (uint64_t)kModBlock);
    const bool take  = !grid || phase == 0 || !srcFresh_;
    const int  span  = grid ? kModBlock - phase : n;
    if(take)
    {
        for(int l = 0; l < kPoolLfos; l++)
        {
            if(lfoRun_ >> l & 1u)
            {
                if(retrig_[l])
                    lfo_[l].Retrigger(clk.beat0);
                src_[kSrcLfo1 + l] = lfo_[l].Step(lfoP_[l], clk.beat0, span, (float)sr_);
            }
            retrig_[l] = false;
        }
        for(int m = 0; m < kPoolMacros; m++)
            src_[kSrcMacro1 + m] = params_[MacroId(m)].load(std::memory_order_relaxed);
        srcFresh_ = grid;
    }
    f.lfoRun = lfoRun_;
    for(int l = 0; l < kPoolLfos; l++)
        f.lfo[l] = lfoRun_ >> l & 1u ? src_[kSrcLfo1 + l] : 0.f;

    // this stretch's values, the globals and every slot's, then the live routes on them: per target its position
    // in its range plus each route's offset, clamped (the telemetry: that position for each of its routes)
    float g[kNumGlobalParams], vals[kSlots][kNumSlotParams];
    for(int k = 0; k < kNumGlobalParams; k++)
        g[k] = params_[k].load(std::memory_order_relaxed);
    for(int s = 0; s < kSlots; s++)
        for(int k = 0; k < kNumSlotParams; k++)
            vals[s][k] = params_[SlotId(s, k)].load(std::memory_order_relaxed);
    for(int i = 0; i < nLive_;)
    {
        const int       t = live_[i].target;
        int             slot, sp;
        float* const    v = IsSlotId(t, slot, sp) ? &vals[slot][sp] : &g[t];
        const ParamDef& d = ParamInfo(t);
        const float     range = d.max - d.min;
        float           x     = (*v - d.min) / range;
        int             j     = i;
        for(; j < nLive_ && live_[j].target == t; j++)
            x += RouteOffset(live_[j], src_[live_[j].src]);
        x  = Clamp01(x);
        *v = d.min + x * range;
        for(; i < j; i++)
            f.route[liveIdx_[i]] = x;
    }
    clk.swing = (double)Clamp01(g[gSwing]) / 3.0; // the swing as modulated (Conductor::Begin's rule)

    int duckFrom[kSlots];
    for(int s = 0; s < kSlots; s++)
    {
        ChannelParams p;
        const auto    v = [&](int param) { return vals[s][param]; };
        p.gen           = I(v(cGen));
        p.inst          = I(v(cInst));
        p.level         = v(cLevel);
        p.pan           = v(cPan);
        p.mute          = v(cMute) >= 0.5f;
        p.show          = v(cShow) >= 0.5f;
        p.arp.degree    = I(v(cDegree));
        p.arp.octave    = I(v(cOctave));
        p.arp.mode      = I(v(cArpMode));
        p.arp.pool      = I(v(cArpPool));
        p.arp.range     = I(v(cArpRange));
        p.arp.div       = I(v(cArpDiv));
        p.arp.length    = I(v(cArpLength));
        p.arp.gate      = v(cArpGate);
        p.arp.density   = v(cArpDensity);
        p.arp.vary      = v(cArpVary);
        p.arp.ratchet   = v(cArpRatchet);
        p.arp.rotate    = I(v(cRhythmRotate));
        p.arp.shift     = I(v(cNoteShift));
        p.arp.count     = I(v(cNoteCount));
        p.arp.seed      = I(v(cSeed));
        VoiceParams& vp = p.voice;
        vp.shape        = v(cShape);
        vp.detuneCents  = v(cDetune);
        vp.sub          = v(cSub);
        vp.noise        = v(cNoise);
        vp.cutoff       = v(cCutoff);
        vp.reso         = v(cReso);
        vp.filterType   = v(cFilterType);
        vp.drive        = v(cDrive);
        vp.ampA         = v(cAmpAttack);
        vp.ampD         = v(cAmpDecay);
        vp.ampS         = v(cAmpSustain);
        vp.ampR         = v(cAmpRelease);
        vp.filtAmount   = v(cFiltEnvAmount);
        vp.filtA        = v(cFiltAttack);
        vp.filtD        = v(cFiltDecay);
        vp.pitchAmount  = v(cPitchEnvAmount);
        vp.pitchD       = v(cPitchDecay);
        vp.glide        = v(cGlide);
        vp.tuneCents    = v(cTune);
        p.chord.notes   = I(v(cChordNotes));
        p.chord.voicing = I(v(cChordVoicing));
        p.chord.smooth  = I(v(cChordLead)) == 1;
        p.chord.rhythm  = I(v(cChordRhythm));
        p.chord.strum   = v(cChordStrum);
        p.chord.letRing = v(cChordLetRing) >= 0.5f;
        p.bassPattern   = I(v(cBassPattern));
        p.rhythm.chance = v(cRhythmChance);
        p.rhythm.pitch  = I(v(cRhythmPitch));
        p.drum.model    = I(v(cDrumModel));
        p.drum.tune     = v(cDrumTune) + v(cTune) * 0.01f; // FINE TUNE moves the drum too
        p.drum.decay    = v(cDrumDecay);
        p.drum.tone     = v(cDrumTone);
        p.drum.snap     = v(cDrumSnap);
        FxParams& fx       = p.fx;
        fx.drive           = v(cFxDrive);
        fx.tone            = v(cFxTone);
        fx.chorusOn        = v(cChorusOn) >= 0.5f;
        fx.chorusMode      = I(v(cChorusMode));
        fx.chorusRate      = v(cChorusRate);
        fx.chorusDepth     = v(cChorusDepth);
        fx.chorusMix       = v(cChorusMix);
        fx.delayDiv        = I(v(cDelayDiv));
        fx.delayFb         = v(cDelayFeedback);
        fx.delayPingPong   = v(cDelayPingPong) >= 0.5f;
        fx.delayDamp       = v(cDelayDamping);
        fx.delayMix        = v(cDelayMix);
        fx.revSend         = v(cRevSend);
        fx.duckSrc         = I(v(cDuckSource));
        fx.duckAmt         = v(cDuckAmount);
        ch_[s].SetParams(p);
        duckFrom[s] = fx.duckSrc - 1 != s && fx.duckAmt > 0.f ? fx.duckSrc - 1 : -1; // a slot does not duck itself
    }

    // every channel's voices first (a ducked channel needs its source's envelope for this block), then the strips
    for(int s = 0; s < kSlots; s++)
    {
        bool want = false;
        for(int t = 0; t < kSlots && !want; t++)
            want = duckFrom[t] == s;
        ch_[s].SetWantEnv(want);
        ch_[s].Render(cond_, n, f.ch[s]);
        // RESTART on this channel's notes: a note in this stretch starts the LFO's cycle again at the next grid point
        if(f.ch[s].hit && restartOn_[s])
            for(int l = 0; l < kPoolLfos; l++)
                if(restartOn_[s] >> l & 1u)
                    retrig_[l] = true;
    }
    std::memset(revL_, 0, sizeof(float) * (size_t)n);
    std::memset(revR_, 0, sizeof(float) * (size_t)n);
    for(int s = 0; s < kSlots; s++)
        ch_[s].Mix(cond_, outL, outR, revL_, revR_, n, duckFrom[s] >= 0 ? ch_[duckFrom[s]].Env() : nullptr, f.ch[s]);

    // the shared reverb, on the sends (fed: something was sent; else it runs only while its tail rings)
    bool fed = false;
    for(int i = 0; i < n && !fed; i++)
        fed = revL_[i] != 0.f || revR_[i] != 0.f;
    ReverbParams rp;
    rp.size  = g[gRevSize];
    rp.decay = g[gRevDecay];
    rp.damp  = g[gRevDamping];
    rp.pre   = g[gRevPreDelay];
    rp.width = g[gRevWidth];
    rp.ret   = g[gRevReturn];
    reverb_.Process(revL_, revR_, outL, outR, n, rp, fed);

    MasterBus(outL, outR, n, f, g[gGlue], g[gCeiling], g[gVolume]);
    tap_.Write(outL, outR, n);
}

void Engine::MasterBus(float* outL, float* outR, int n, AmmoFrame& f, float glue, float ceiling, float vt)
{
    // +6 dB into the bus (the voices sum low: 0.3 each, sized for the soft clip this bus had before the limiter), GLUE,
    // the limiter, then the volume (so the ceiling holds at any volume)
    for(int i = 0; i < n; i++)
    {
        outL[i] *= kBusGain;
        outR[i] *= kBusGain;
    }
    f.glueGr  = glue_.Process(outL, outR, n, glue);
    f.limitGr = limiter_.Process(outL, outR, n, ceiling);
    if(volFresh_)
    {
        volume_.Set(vt); // the first block starts on the value, no glide from the table default
        volFresh_ = false;
    }
    volume_.Target(vt);
    double sumSq = 0.0;
    float  peak  = 0.f;
    for(int i = 0; i < n; i++)
    {
        const float vol = volume_.Next();
        const float l   = outL[i] * vol;
        const float r   = outR[i] * vol;
        outL[i]         = l;
        outR[i]         = r;
        sumSq += 0.5 * ((double)l * l + (double)r * r);
        const float m = std::fmax(l < 0.f ? -l : l, r < 0.f ? -r : r);
        if(m > peak)
            peak = m;
    }
    f.masterRms  = (float)std::sqrt(sumSq / (n > 0 ? n : 1));
    f.masterPeak = peak;
}

std::string Engine::Save() const
{
    // 4: ROTATE for every generator, SHIFT, COUNT, SEED (3: the modulation pool; 2: the arp divisions gained 4/1
    // 2/1 and the dotted ones)
    std::string out = "ammonite-desktop 4\n";
    for(int id = 0; id < kNumIds; id++)
    {
        if(!IsSaved(id))
            continue;
        const float v = params_[id].load(std::memory_order_relaxed);
        if(std::fabs(v - ParamInfo(id).def) >= 1e-7f)
            AppendLine(out, id, v);
    }
    return out;
}

std::string Engine::RoutesText(const float* values)
{
    std::string out = "ammonite-desktop 4\n";
    for(int id = kRouteBase; id < kPoolEnd; id++)
        if(std::fabs(values[id] - ParamInfo(id).def) >= 1e-7f)
            AppendLine(out, id, values[id]);
    return out;
}

std::string Engine::SaveRoutes() const
{
    std::vector<float> values(kNumIds);
    for(int id = kRouteBase; id < kPoolEnd; id++)
        values[id] = params_[id].load(std::memory_order_relaxed);
    return RoutesText(values.data());
}

bool Engine::LoadRoutes(const char* text, size_t len)
{
    std::vector<float> values(kNumIds);
    if(!ParseState(text, len, values.data()))
        return false;
    for(int id = kRouteBase; id < kPoolEnd; id++)
        if(params_[id].load(std::memory_order_relaxed) != values[id])
            params_[id].store(values[id], std::memory_order_relaxed);
    return true;
}

bool Engine::Load(const char* text, size_t len)
{
    float values[kNumIds];
    if(!ParseState(text, len, values))
        return false;
    for(int id = 0; id < kNumIds; id++)
        params_[id].store(values[id], std::memory_order_relaxed);
    return true;
}

bool Engine::ParseState(const char* text, size_t len, float* values)
{
    if(text == nullptr || len < 18 || std::strncmp(text, "ammonite-desktop ", 17) != 0)
        return false;
    // defaults first (table defaults, not the patch: the chunk holds every
    // non-default value, so a saved default patch restores itself)
    for(int id = 0; id < kNumIds; id++)
        values[id] = ParamInfo(id).def;
    const char* p       = text;
    const char* end     = text + len;
    const int   version = std::atoi(text + 17);
    // skip the header line
    while(p < end && *p != '\n')
        p++;
    while(p < end)
    {
        char* next = nullptr;
        const long id = std::strtol(p, &next, 10);
        if(next == p)
        {
            while(p < end && *p != '\n')
                p++;
            if(p < end)
                p++;
            continue;
        }
        float v = std::strtof(next, &next);
        int   slot, sp;
        if(version < 2 && IsSlotId((int)id, slot, sp) && sp == cArpDiv)
            v = (float)kArpDivFromV1[(int)std::lround(std::fmin(std::fmax(v, 0.f), 8.f))];
        if(IsSaved((int)id) || (version < 3 && IsRetiredId((int)id)))
            values[id] = ClampParam((int)id, v);
        p = next;
        while(p < end && *p != '\n')
            p++;
        if(p < end)
            p++;
    }
    if(version < 3)
        ConvertV2(values);
    if(version < 4)
        ConvertV3(values);
    return true;
}

} // namespace ammo
