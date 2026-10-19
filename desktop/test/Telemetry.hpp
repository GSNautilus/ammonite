/** The visual telemetry stream, phase 1 shape: one frame per audio block,
 *  written by the audio thread, read by the UI thread, through a lock-free
 *  single-producer single-consumer ring. No locks, no allocation, never
 *  blocks either side. Audio devices deliver blocks in bursts (four 128
 *  sample blocks every 10 ms here), so a queue, not a latest-value slot:
 *  the UI takes every frame and spreads a burst over time. The
 *  engine publishes the same kind of frames, per channel. */
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ammo {

constexpr int kWaveLen = 256;

struct Frame
{
    uint32_t seq = 0;
    float    shape = 0.f, level = 0.f; // the smoothed parameter values
    float    rms = 0.f, peak = 0.f;    // of this block, mono
    float    env = 0.f;                // the beat envelope, 0..1
    float    wave[kWaveLen] = {};      // one period of the output, phase-aligned (tiles seamlessly)
    double   beat = 0.0, bpm = 0.0;    // the clock the envelope follows
    bool     playing = false;          // host transport running
    bool     hostClock = false;        // the host ever gave a tempo
    int      instance = 0;             // 1, 2, 3... per plugin instance in this process
    double   sampleRate = 0.0;
    uint32_t blockSize = 0;
};

class Telemetry
{
  public:
    static constexpr uint32_t kN = 32; // frames in flight: 85 ms of 128-sample blocks at 48 kHz

    Telemetry() : w_(0), r_(0) {}

    /** Audio thread: the slot to fill, then publish(). Fill every field.
     *  A reader that is more than kN frames behind loses the oldest. */
    Frame& beginWrite() { return ring_[w_.load(std::memory_order_relaxed) & (kN - 1)]; }
    void   publish() { w_.store(w_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    /** Any thread: frames the reader has not taken yet. */
    uint32_t pending() const
    {
        const uint32_t w = w_.load(std::memory_order_acquire), r = r_.load(std::memory_order_acquire);
        return w - r;
    }
    bool hasNew() const { return pending() != 0; }

    /** UI thread: the next frame, copied into `out`; false when none is
     *  waiting. Skips frames the writer has already overwritten. */
    bool read(Frame& out)
    {
        for(int attempt = 0; attempt < 2; attempt++)
        {
            const uint32_t w = w_.load(std::memory_order_acquire);
            uint32_t       r = r_.load(std::memory_order_relaxed);
            if(w - r > kN)
                r = w - kN; // lagging: drop what is gone
            if(r == w)
                return false;
            std::memcpy(&out, &ring_[r & (kN - 1)], sizeof(Frame));
            // the writer may have come round while we copied: then it is torn
            if(w_.load(std::memory_order_acquire) - r <= kN)
            {
                r_.store(r + 1, std::memory_order_release);
                last_ = out;
                return true;
            }
            r_.store(r + 1, std::memory_order_release);
        }
        return false;
    }

    /** UI thread: the last frame read (a copy the writer cannot touch). */
    const Frame& latest() const { return last_; }

  private:
    Frame                 ring_[kN];
    Frame                 last_;
    std::atomic<uint32_t> w_, r_;
};

/** The telemetry of a plugin instance (DPF direct access pointer). */
Telemetry* TelemetryOf(void* pluginInstance);

} // namespace ammo
