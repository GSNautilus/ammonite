/** Shared by both test UIs: timing, the frame-time histogram, the test log,
 *  colors, and the knob geometry (the same in both variants so one UI check
 *  drives either). Logical layout: 720 x 480, scaled to the window. */
#pragma once

#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ammo {

constexpr float kLayoutW = 720.f, kLayoutH = 480.f;

/** The two knobs: centre and radius in layout units; the value text sits
 *  below. Shape is a standard knob (74 px), Level a compact one (50 px). */
struct KnobSpec
{
    int         param;
    float       cx, cy, r;
    float       def;
    const char* name;
};
constexpr KnobSpec kKnobs[2] = {
    {0, 70.f, 410.f, 37.f, 0.25f, "Shape"},
    {1, 160.f, 422.f, 25.f, 0.5f, "Level"},
};
constexpr float kDragSpan = 180.f, kFineSpan = 720.f; // pixels for the full range

inline double NowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/** Appends a line to $AMMO_TEST_LOG when that is set (the headless UI check
 *  reads it); silent otherwise. */
inline void TestLog(const char* fmt, ...)
{
    static const char* path = std::getenv("AMMO_TEST_LOG");
    if(path == nullptr || path[0] == 0)
        return;
    if(FILE* f = std::fopen(path, "a"))
    {
        char    buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        std::fprintf(f, "%.0f %s\n", NowMs(), buf);
        std::fclose(f);
    }
}

/** Frame times over the last 10 s: how many, how many over 20 ms, the worst. */
class FrameStats
{
  public:
    void Mark()
    {
        const double now = NowMs();
        if(last_ > 0.0)
        {
            t_[head_]  = now;
            dt_[head_] = now - last_;
            head_      = (head_ + 1) % kN;
            if(count_ < kN)
                count_++;
        }
        last_ = now;
    }
    struct Summary
    {
        int    frames = 0, slow = 0;
        double maxMs = 0.0, meanMs = 0.0;
    };
    Summary Last10s() const
    {
        Summary     s;
        const double now = NowMs();
        double       sum = 0.0;
        for(int i = 0; i < count_; i++)
        {
            const int k = (head_ - 1 - i + kN) % kN;
            if(now - t_[k] > 10000.0)
                break;
            s.frames++;
            sum += dt_[k];
            if(dt_[k] > 20.0)
                s.slow++;
            if(dt_[k] > s.maxMs)
                s.maxMs = dt_[k];
        }
        s.meanMs = s.frames ? sum / s.frames : 0.0;
        return s;
    }

  private:
    static constexpr int kN = 2048;
    double               t_[kN] = {}, dt_[kN] = {};
    int                  head_ = 0, count_ = 0;
    double               last_ = 0.0;
};

/** Keys the plugin UI does not use go to the host: posted as WM_KEYDOWN /
 *  WM_KEYUP to the top-level window the plugin is embedded in (our own
 *  standalone host, or a DAW's plugin window). Both UIs take the keyboard
 *  focus when clicked (the WebView2 even on creation) and would otherwise
 *  swallow Space, the transport key. */
inline void ForwardKeyToHost(void* nativeWindow, unsigned vk, bool down, bool repeat)
{
    const HWND own = static_cast<HWND>(nativeWindow);
    if(own == nullptr || vk == 0 || vk > 0xFE)
        return;
    const HWND root = GetAncestor(own, GA_ROOT);
    if(root == nullptr || root == own)
        return;
    const unsigned scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM         lp   = 1 | (LPARAM)(scan & 0xFF) << 16;
    if(repeat)
        lp |= (LPARAM)1 << 30;
    if(!down)
        lp |= (LPARAM)1 << 30 | (LPARAM)1 << 31;
    PostMessageW(root, down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)vk, lp);
}

/** Wakes the UI thread as soon as the audio thread has published a new
 *  telemetry frame, instead of waiting for DPF's idle timer (a WM_TIMER at
 *  16 ms that arrives every ~26 ms in practice). A small thread polls the
 *  triple buffer every millisecond and posts a message to a message-only
 *  window owned by the UI thread; the host's message loop dispatches it at
 *  once. At most one wake-up is in flight, and no more than one per
 *  kMinIntervalMs. */
class Pacer
{
  public:
    static constexpr double kMinIntervalMs = 4.0; // <= 250 pushes per second

    Pacer(void (*cb)(void*), void* cbArg) : cb_(cb), cbArg_(cbArg) {}

    template <typename HasNewFn>
    bool Start(HasNewFn hasNew)
    {
        static const wchar_t* kClass = L"AmmoniteTestPacer";
        WNDCLASSW             wc{};
        wc.lpfnWndProc   = &Pacer::WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        RegisterClassW(&wc); // fails harmlessly the second time
        hwnd_ = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        if(hwnd_ == nullptr)
            return false;
        timeBeginPeriod(1);
        thread_ = std::thread([this, hasNew]() {
            double lastPost = 0.0;
            while(!quit_.load(std::memory_order_relaxed))
            {
                Sleep(1);
                if(pending_.load(std::memory_order_acquire) || !hasNew())
                    continue;
                const double now = NowMs();
                if(now - lastPost < kMinIntervalMs)
                    continue;
                lastPost = now;
                pending_.store(true, std::memory_order_release);
                PostMessageW(hwnd_, WM_APP, 0, 0);
            }
        });
        return true;
    }

    ~Pacer()
    {
        quit_.store(true);
        if(thread_.joinable())
            thread_.join();
        if(hwnd_ != nullptr)
            DestroyWindow(hwnd_);
        timeEndPeriod(1);
    }

  private:
    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        if(msg == WM_CREATE)
        {
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
            return 0;
        }
        if(msg == WM_APP)
        {
            if(Pacer* self = reinterpret_cast<Pacer*>(GetWindowLongPtrW(h, GWLP_USERDATA)))
            {
                self->pending_.store(false, std::memory_order_release);
                self->cb_(self->cbArg_);
            }
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    void (*cb_)(void*);
    void*                   cbArg_;
    HWND                    hwnd_ = nullptr;
    std::thread             thread_;
    std::atomic<bool>       quit_{false}, pending_{false};
};

/** The hue a Shape value shows for now (the product uses pitch hues). */
inline float HueOfShape(float shape) { return 20.f + shape * 300.f; }

inline void HslToRgb(float h, float s, float l, float* rgb)
{
    h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f) / 360.f;
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s, p = 2.f * l - q;
    for(int i = 0; i < 3; i++)
    {
        float t = h + (1.f - (float)i) / 3.f;
        t -= std::floor(t);
        rgb[i] = t < 1.f / 6.f ? p + (q - p) * 6.f * t
               : t < 0.5f      ? q
               : t < 2.f / 3.f ? p + (q - p) * (2.f / 3.f - t) * 6.f
                               : p;
    }
}

/** Column-major 3x3 for glUniformMatrix3fv: Rx(ax) * Ry(ay). */
inline void RotationMatrix(float ax, float ay, float out[9])
{
    const float cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    // row-major M = Rx * Ry
    const float m[3][3] = {
        {cy, 0.f, sy},
        {sx * sy, cx, -sx * cy},
        {-cx * sy, sx, cx * cy},
    };
    for(int c = 0; c < 3; c++)
        for(int r = 0; r < 3; r++)
            out[c * 3 + r] = m[r][c];
}

} // namespace ammo
