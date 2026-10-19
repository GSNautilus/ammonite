/** Recording the master output: the engine's RecordTap (written at the end
 *  of every block while armed, by the audio thread) and a WavWriter that a
 *  reader on another thread drains it into (the plugin's UI tick, or the C
 *  API's drain thread for the tests). */
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ammo {

/** A single-producer single-consumer ring of interleaved stereo float
 *  frames, about 2 s at the engine's rate. The writer drops a whole block
 *  that does not fit (and counts its frames) rather than tear it. */
class RecordTap
{
  public:
    /** Not the audio thread: sizes the ring for the rate (not while armed). */
    void Init(double sampleRate)
    {
        uint32_t cap = 1;
        while(cap < (uint32_t)(sampleRate * 2.0))
            cap <<= 1;
        if(cap != cap_)
        {
            buf_.assign((size_t)cap * 2, 0.f);
            cap_ = cap;
        }
        w_.store(0);
        r_.store(0);
    }

    /** The reader: drop whatever waits, then arm. */
    void Start()
    {
        r_.store(w_.load(std::memory_order_acquire), std::memory_order_release);
        dropped_.store(0);
        armed_.store(true, std::memory_order_release);
    }
    void Stop() { armed_.store(false, std::memory_order_release); }
    bool Armed() const { return armed_.load(std::memory_order_acquire); }
    /** Frames the writer could not fit since Start. */
    uint32_t Dropped() const { return dropped_.load(std::memory_order_relaxed); }
    uint32_t Capacity() const { return cap_; }

    /** The audio thread, at a block's end. */
    void Write(const float* L, const float* R, int n)
    {
        if(!armed_.load(std::memory_order_acquire) || cap_ == 0 || n <= 0)
            return;
        const uint32_t w = w_.load(std::memory_order_relaxed);
        if(w - r_.load(std::memory_order_acquire) + (uint32_t)n > cap_)
        {
            dropped_.fetch_add((uint32_t)n, std::memory_order_relaxed);
            return;
        }
        float* const b = buf_.data();
        for(int i = 0; i < n; i++)
        {
            const uint32_t k = ((w + (uint32_t)i) & (cap_ - 1)) * 2;
            b[k]             = L[i];
            b[k + 1]         = R[i];
        }
        w_.store(w + (uint32_t)n, std::memory_order_release);
    }

    /** The reader: up to maxFrames interleaved frames into dst; how many. */
    int Read(float* dst, int maxFrames)
    {
        const uint32_t w     = w_.load(std::memory_order_acquire);
        const uint32_t r     = r_.load(std::memory_order_relaxed);
        uint32_t       avail = w - r;
        if(avail > (uint32_t)maxFrames)
            avail = (uint32_t)maxFrames;
        const float* const b = buf_.data();
        for(uint32_t i = 0; i < avail; i++)
        {
            const uint32_t k = ((r + i) & (cap_ - 1)) * 2;
            dst[i * 2]       = b[k];
            dst[i * 2 + 1]   = b[k + 1];
        }
        r_.store(r + avail, std::memory_order_release);
        return (int)avail;
    }

  private:
    std::vector<float>    buf_;
    uint32_t              cap_ = 0;
    std::atomic<uint32_t> w_{0}, r_{0}, dropped_{0};
    std::atomic<bool>     armed_{false};
};

/** A 32-bit float stereo WAV, written as it comes; Close patches the
 *  header's sizes. The caller opens the file (a wide path on Windows). */
class WavWriter
{
  public:
    ~WavWriter() { Close(); }

    bool Open(FILE* f, int sampleRate)
    {
        Close();
        if(f == nullptr)
            return false;
        f_      = f;
        frames_ = 0;
        full_   = false;
        sr_     = sampleRate;
        Header();
        return std::ferror(f_) == 0;
    }
    bool     IsOpen() const { return f_ != nullptr; }
    uint64_t Frames() const { return frames_; }

    void Write(const float* interleaved, int frames)
    {
        if(f_ == nullptr || frames <= 0 || full_)
            return;
        // a RIFF size is 32 bits: stop short of 4 GB (about 3 hours at 48 kHz)
        if((frames_ + (uint64_t)frames) * 8u > 0xFFFFFF00u - kHeader)
        {
            full_ = true;
            return;
        }
        std::fwrite(interleaved, sizeof(float) * 2, (size_t)frames, f_);
        frames_ += (uint64_t)frames;
    }

    /** Patches the sizes and closes; false if anything failed to write. */
    bool Close()
    {
        if(f_ == nullptr)
            return true;
        std::fseek(f_, 0, SEEK_SET);
        Header();
        const bool ok = std::ferror(f_) == 0;
        std::fclose(f_);
        f_ = nullptr;
        return ok;
    }

  private:
    static constexpr uint32_t kHeader = 58; // RIFF + fmt (18) + fact + data headers

    void U32(uint32_t v) { std::fwrite(&v, 4, 1, f_); }
    void U16(uint16_t v) { std::fwrite(&v, 2, 1, f_); }
    void Header()
    {
        const uint32_t data = (uint32_t)(frames_ * 8u);
        std::fwrite("RIFF", 1, 4, f_);
        U32(kHeader - 8 + data);
        std::fwrite("WAVEfmt ", 1, 8, f_);
        U32(18);
        U16(3); // IEEE float
        U16(2);
        U32((uint32_t)sr_);
        U32((uint32_t)sr_ * 8u);
        U16(8);
        U16(32);
        U16(0); // cbSize
        std::fwrite("fact", 1, 4, f_);
        U32(4);
        U32((uint32_t)frames_);
        std::fwrite("data", 1, 4, f_);
        U32(data);
    }

    FILE*    f_      = nullptr;
    uint64_t frames_ = 0;
    int      sr_     = 48000;
    bool     full_   = false;
};

/** Everything waiting in the tap into the file. */
inline void DrainTap(RecordTap& tap, WavWriter& wav)
{
    float buf[4096 * 2];
    for(;;)
    {
        const int n = tap.Read(buf, 4096);
        if(n <= 0)
            break;
        wav.Write(buf, n);
    }
}

} // namespace ammo
