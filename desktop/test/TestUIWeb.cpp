/** Ammonite Test, the web UI: a DPF UI whose window is covered by a CHOC
 *  WebView2 child (distrho/extra/WebView.hpp, the low-level API). The page
 *  (test/web/index.html, embedded as a data: URL; AMMO_WEB_DIR=<dir> loads
 *  <dir>/index.html from disk instead) draws the knobs on Canvas 2D and the
 *  shell in WebGL.
 *
 *  C++ -> page: every idle call, `ammo.tick({...})` with the newest telemetry
 *  frame (the wave as 256 numbers), plus `ammo.param(i, v)` on host changes
 *  and `ammo.info({...})` once the page is ready. The time spent in each
 *  evaluateJS call is measured and sent along: it is part of the test.
 *  Page -> C++ (the bind callback, one line each): `ready`, `edit <id> <0|1>`,
 *  `set <id> <value>`, `gl ok <renderer>` | `gl fail <why>`, `log <text>`. */
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>

#include "DistrhoUI.hpp"
#include "DistrhoPluginUtils.hpp"
#include "extra/WebView.hpp"
#include "IndexHtml.hpp"
#include "Telemetry.hpp"
#include "TestUICommon.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace
{
using namespace ammo;

std::string Base64(const unsigned char* data, size_t len)
{
    static const char* const T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string              out;
    out.reserve((len + 2) / 3 * 4);
    for(size_t i = 0; i < len; i += 3)
    {
        const unsigned v = (unsigned)data[i] << 16 | (i + 1 < len ? (unsigned)data[i + 1] << 8 : 0u)
                           | (i + 2 < len ? (unsigned)data[i + 2] : 0u);
        out.push_back(T[(v >> 18) & 63]);
        out.push_back(T[(v >> 12) & 63]);
        out.push_back(i + 1 < len ? T[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? T[v & 63] : '=');
    }
    return out;
}

std::string PageUrl()
{
    if(const char* dir = std::getenv("AMMO_WEB_DIR"))
    {
        if(dir[0] != 0)
        {
            std::string p(dir);
            for(char& c : p)
                if(c == '\\')
                    c = '/';
            return "file:///" + p + "/index.html";
        }
    }
    return "data:text/html;charset=utf-8;base64," + Base64(kIndexHtml, kIndexHtmlLen);
}

void AppendNum(std::string& s, double v, const char* fmt = "%.4g")
{
    char b[32];
    std::snprintf(b, sizeof b, fmt, v);
    s += b;
}
} // namespace

class TestUIWeb : public UI
{
  public:
    TestUIWeb()
    : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true),
      tele_(TelemetryOf(getPluginInstancePointer()))
    {
        const double sf = getScaleFactor();
        setGeometryConstraints((uint)(480 * sf), (uint)(320 * sf), true, false);
        loadSharedResources();
        host_[0] = kKnobs[0].def;
        host_[1] = kKnobs[1].def;

        const std::string url = PageUrl();
        WebViewOptions    opts(&MessageCallback, this);
        const double      t0 = NowMs();
        web_                 = webViewCreate(url.c_str(), getWindow().getNativeWindowHandle(), getWidth(), getHeight(), sf, opts);
        createMs_            = NowMs() - t0;
        TestLog("ui created (Web) %u x %u scale %.2f; webview %s in %.0f ms (%s)", getWidth(), getHeight(), sf,
                web_ != nullptr ? "created" : "FAILED", createMs_, url.compare(0, 5, "file:") == 0 ? "file" : "data");
        // Push telemetry the moment a frame is published, not on DPF's idle.
        Telemetry* const t = tele_;
        paced_ = web_ != nullptr && pacer_.Start([t]() { return t != nullptr && t->hasNew(); });
        TestLog("pacer %s", paced_ ? "running" : "unavailable, falling back to idle");
    }

    ~TestUIWeb() override
    {
        if(web_ != nullptr)
            webViewDestroy(web_);
    }

  protected:
    /* ------------------------------------------------------ host side */
    void parameterChanged(uint32_t index, float value) override
    {
        if(index >= 2)
            return;
        host_[index] = value;
        if(ready_)
        {
            std::string js = "ammo.param(";
            AppendNum(js, index, "%.0f");
            js += ",";
            AppendNum(js, value, "%.5f");
            js += ")";
            Eval(js);
        }
    }

    void uiIdle() override
    {
        idle_.Mark();
        if(web_ == nullptr)
            return;
        webViewIdle(web_);
        if(!paced_)
            Tick();
    }

    void onResize(const ResizeEvent& ev) override
    {
        UI::onResize(ev);
        if(web_ != nullptr)
            webViewResize(web_, ev.size.getWidth(), ev.size.getHeight(), getScaleFactor());
    }

    void onNanoDisplay() override
    {
        // Under the web view; only ever seen when it could not be created.
        beginPath();
        rect(0, 0, getWidth(), getHeight());
        fillColor(Color(14 / 255.f, 14 / 255.f, 16 / 255.f));
        fill();
        if(web_ == nullptr)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(16 * (float)getScaleFactor());
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(Color(1.f, 0.5f, 0.4f));
            text(getWidth() / 2.f, getHeight() / 2.f, "WebView2 could not be created (is the Edge WebView2 runtime installed?)",
                 nullptr);
        }
    }

  private:
    /* ---- page -> C++ */
    static void MessageCallback(void* arg, char* msg) { static_cast<TestUIWeb*>(arg)->OnMessage(msg); }

    void OnMessage(const char* msg)
    {
        if(std::strcmp(msg, "ready") == 0)
        {
            ready_ = true;
            TestLog("page ready");
            std::string js = "ammo.info({fmt:\"";
            js += getPluginFormatName();
            js += "\",variant:\"Web\",createMs:";
            AppendNum(js, createMs_);
            js += ",params:[";
            AppendNum(js, host_[0], "%.5f");
            js += ",";
            AppendNum(js, host_[1], "%.5f");
            js += "]})";
            Eval(js);
            return;
        }
        if(std::strncmp(msg, "edit ", 5) == 0)
        {
            int id = -1, started = 0;
            if(std::sscanf(msg + 5, "%d %d", &id, &started) == 2 && id >= 0 && id < 2)
                editParameter((uint32_t)id, started != 0);
            return;
        }
        if(std::strncmp(msg, "set ", 4) == 0)
        {
            int   id = -1;
            float v  = 0.f;
            if(std::sscanf(msg + 4, "%d %f", &id, &v) == 2 && id >= 0 && id < 2)
            {
                v        = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
                host_[id] = v;
                setParameterValue((uint32_t)id, v);
            }
            return;
        }
        if(std::strncmp(msg, "key ", 4) == 0)
        {
            // "key <1 down|0 up> <virtual key> <1 repeat|0>" from the page
            int down = 0, vk = 0, repeat = 0;
            if(std::sscanf(msg + 4, "%d %d %d", &down, &vk, &repeat) >= 2)
                ForwardKeyToHost(reinterpret_cast<void*>(getWindow().getNativeWindowHandle()), (unsigned)vk, down != 0,
                                 repeat != 0);
            return;
        }
        if(std::strncmp(msg, "gl ", 3) == 0 || std::strncmp(msg, "log ", 4) == 0)
        {
            TestLog("%s", msg);
            return;
        }
        TestLog("page said: %s", msg);
    }

    /* ---- C++ -> page */
    void Eval(const std::string& js)
    {
        const double t0 = NowMs();
        webViewEvaluateJS(web_, js.c_str());
        lastEvalMs_ = NowMs() - t0;
    }

    /** Every frame published since the last push goes to the page in one
     *  call: `ammo.tick([{...}, {...}])`. Audio arrives in bursts (four
     *  128-sample blocks per 10 ms device period here); the page spreads
     *  them over time and blends between neighbours at its own frame rate. */
    void Tick()
    {
        if(tele_ == nullptr)
            return;
        Frame    frames[8];
        uint32_t n = 0;
        while(n < 8 && tele_->read(frames[n]))
            n++;
        if(n > 0 && !firstTick_)
        {
            firstTick_ = true;
            TestLog("first tick seq %u sr %.0f inst %d", frames[0].seq, frames[0].sampleRate, frames[0].instance);
        }
        if(!ready_ || n == 0)
            return;
        const FrameStats::Summary id = idle_.Last10s();

        std::string js;
        js.reserve(2200 * n + 200);
        js += "ammo.tick([";
        char b[16];
        for(uint32_t i = 0; i < n; i++)
        {
            const Frame& f = frames[i];
            js += i ? ",{seq:" : "{seq:";
            AppendNum(js, f.seq, "%.0f");
            js += ",shape:";
            AppendNum(js, f.shape, "%.4f");
            js += ",level:";
            AppendNum(js, f.level, "%.4f");
            js += ",rms:";
            AppendNum(js, f.rms);
            js += ",peak:";
            AppendNum(js, f.peak);
            js += ",env:";
            AppendNum(js, f.env, "%.4f");
            js += ",beat:";
            AppendNum(js, f.beat, "%.3f");
            js += ",bpm:";
            AppendNum(js, f.bpm, "%.2f");
            js += f.playing ? ",playing:true" : ",playing:false";
            js += f.hostClock ? ",hostClock:true" : ",hostClock:false";
            js += ",inst:";
            AppendNum(js, f.instance, "%.0f");
            js += ",sr:";
            AppendNum(js, f.sampleRate, "%.0f");
            js += ",block:";
            AppendNum(js, f.blockSize, "%.0f");
            js += ",wave:[";
            for(int k = 0; k < kWaveLen; k++)
            {
                std::snprintf(b, sizeof b, k ? ",%.3f" : "%.3f", f.wave[k]);
                js += b;
            }
            js += "]}";
        }
        js += "],{idleMs:";
        AppendNum(js, id.meanMs, "%.2f");
        js += ",idleMax:";
        AppendNum(js, id.maxMs, "%.1f");
        js += ",evalMs:";
        AppendNum(js, lastEvalMs_, "%.3f");
        js += "})";
        Eval(js);
        pushes_.Mark();
        framesSent_ += n;
        if(++ticks_ == 60 || ticks_ % 600 == 0)
        {
            const FrameStats::Summary p = pushes_.Last10s();
            TestLog("%u ticks, %u frames; %.0f pushes/s (mean %.1f ms, max %.1f), last eval %.3f ms for %u frames, js %u bytes, idle mean %.1f ms",
                    ticks_, framesSent_, p.meanMs > 0 ? 1000.0 / p.meanMs : 0.0, p.meanMs, p.maxMs, lastEvalMs_, n,
                    (unsigned)js.size(), id.meanMs);
        }
    }

    static void OnPace(void* arg) { static_cast<TestUIWeb*>(arg)->Tick(); }

    Telemetry* const tele_;
    WebViewHandle    web_ = nullptr;
    float            host_[2] = {0.25f, 0.5f};
    FrameStats       idle_, pushes_;
    Pacer            pacer_{&TestUIWeb::OnPace, this};
    bool             paced_ = false;
    bool             ready_ = false, firstTick_ = false;
    double           lastEvalMs_ = 0.0, createMs_ = 0.0;
    uint32_t         ticks_ = 0, framesSent_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestUIWeb)
};

UI* createUI()
{
    return new TestUIWeb();
}

END_NAMESPACE_DISTRHO
