/** The engine: 16 channel slots, the conductor, the shared reverb, the master bus, parameters
 *  by stable id, the state chunk and the telemetry ring. No plugin or UI
 *  knowledge. */
#pragma once

#include "Channel.hpp"
#include "Conductor.hpp"
#include "Mod.hpp"
#include "Params.hpp"
#include "Record.hpp"
#include "Telemetry.hpp"

#include <atomic>
#include <string>

namespace ammo {

class Engine
{
  public:
    static constexpr int kMaxBlock = 4096;

    Engine(double sampleRate, int maxBlock);

    void   SetSampleRate(double sampleRate);
    double SampleRate() const { return sr_; }

    /** Any thread: the host-side value of a parameter (clamped, stepped). */
    void  SetParam(int id, float value);
    float GetParam(int id) const;
    /** Audio thread, at a block start: the engine-side copy of all params. */

    /** Audio thread: render a block (any size up to kMaxBlock; larger blocks
     *  are split). Clock null = internal. */
    void Process(float* outL, float* outR, int frames, const HostClock* clock);

    Telemetry& telemetry() { return tele_; }
    /** The master output, after the master bus, while armed (Record.hpp). */
    RecordTap& recordTap() { return tap_; }
    int        instance() const { return instance_; }
    /** Samples the output lags the music: the limiter's look-ahead. */
    int latency() const { return limiter_.Latency(); }

    /** The state chunk: a versioned text, every non-default parameter
     *  (IsSaved: the routes too). Lines Load cannot parse are skipped (a
     *  patch file's `page` line). */
    std::string Save() const;
    bool        Load(const char* text, size_t len);
    /** A chunk into a value per id (kNumIds): table defaults, then the
     *  chunk's values, clamped; false if it is not a chunk. A version 2
     *  chunk's LFOs and routes become the pool's (ConvertV2). */
    static bool ParseState(const char* text, size_t len, float* values);
    /** The routes alone, as a chunk: what a plugin keeps as its state (the routes are not host parameters).
     *  RoutesText: the same from a value per id. LoadRoutes takes a chunk's routes and nothing else. */
    std::string        SaveRoutes() const;
    static std::string RoutesText(const float* values);
    bool               LoadRoutes(const char* text, size_t len);

    /** Reset every parameter to the table defaults plus the default patch. */
    void ResetToDefaults();

  private:
    void ProcessBlock(float* outL, float* outR, int frames, const HostClock* clock);
    /** One stretch of a block with the modulation as it stands at its start (ProcessBlock's loop). */
    void SubBlock(float* outL, float* outR, int frames, const HostClock* clock, bool grid, AmmoFrame& f, BlockClock& clk);
    /** The routes and LFOs for this block, from the parameters; true if modulation runs on the kModBlock grid
     *  (a live route on a playing channel or a global target). */
    bool PrepareMod();
    /** Glue, the limiter at `ceiling`, the volume (as modulated). */
    void MasterBus(float* outL, float* outR, int frames, AmmoFrame& f, float glue, float ceiling, float volume);

    double             sr_ = 48000.0;
    int                maxBlock_ = 512;
    int                instance_ = 0;
    std::atomic<float> params_[kNumIds];
    Conductor          cond_;
    Channel            ch_[kSlots];
    Reverb             reverb_;
    float              revL_[kMaxBlock], revR_[kMaxBlock]; // the reverb send bus
    Glue               glue_;
    Limiter            limiter_;
    Smoother           volume_;
    bool               volFresh_ = true;
    Telemetry          tele_;
    RecordTap          tap_;
    uint32_t           seq_ = 0;
    uint64_t           pos_ = 0; // samples rendered since the start: the modulation grid
    // the modulation pool: its LFOs, and per block (PrepareMod) the LFOs that run and the live routes grouped by
    // target (in route order, so routes on one target add in order)
    Lfo                lfo_[kPoolLfos];
    LfoParams          lfoP_[kPoolLfos];
    uint32_t           lfoRun_ = 0, restartOn_[kSlots] = {}; // per slot: the running LFOs its notes restart
    PoolRoute          live_[kPoolRoutes];
    int                liveIdx_[kPoolRoutes] = {}, nLive_ = 0; // each live route's index; sorted by target
    // the sources as taken at the last grid point (a stretch a host block cut short keeps them: any block size, the
    // same values), and the restarts a note asked for, done at the next grid point
    float              src_[kNumModSources] = {};
    bool               srcFresh_ = false, retrig_[kPoolLfos] = {};
    bool               hostClockSeen_ = false;
};

} // namespace ammo
