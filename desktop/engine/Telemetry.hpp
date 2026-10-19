/** The visual telemetry stream: one AmmoFrame per audio block (api.h),
 *  written by the audio thread, read by the UI thread, through a lock-free
 *  single-producer single-consumer ring (the phase 1 design, generic). */
#pragma once

#include "api.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ammo {

template <typename F, uint32_t N>
class Ring
{
    static_assert((N & (N - 1)) == 0, "power of two");

  public:
    Ring() : w_(0), r_(0) {}

    /** Audio thread: the slot to fill, then publish(). */
    F&   beginWrite() { return ring_[w_.load(std::memory_order_relaxed) & (N - 1)]; }
    void publish() { w_.store(w_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    /** Any thread: frames the reader has not taken yet. */
    uint32_t pending() const
    {
        return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire);
    }
    bool hasNew() const { return pending() != 0; }

    /** Reader: the next frame copied into `out`; false when none waits.
     *  Skips frames the writer has already overwritten (torn copies). */
    bool read(F& out)
    {
        for(int attempt = 0; attempt < 2; attempt++)
        {
            const uint32_t w = w_.load(std::memory_order_acquire);
            uint32_t       r = r_.load(std::memory_order_relaxed);
            if(w - r > N)
                r = w - N;
            if(r == w)
                return false;
            std::memcpy(&out, &ring_[r & (N - 1)], sizeof(F));
            r_.store(r + 1, std::memory_order_release);
            if(w_.load(std::memory_order_acquire) - r <= N)
                return true;
        }
        return false;
    }

  private:
    F                     ring_[N];
    std::atomic<uint32_t> w_, r_;
};

using Telemetry = Ring<AmmoFrame, 32>;

} // namespace ammo
