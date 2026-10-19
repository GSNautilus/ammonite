/** Ammonite Test Host: the test plugin without a DAW.
 *
 *  A small Windows CLAP host: loads a .clap, creates N instances, gives each
 *  its own top-level window with the plugin's UI inside, plays them through
 *  an output device (WASAPI, shared mode: Windows' default, or the one chosen
 *  in the plugin's own page, switched while playing and remembered: see
 *  ammo_host_outputs below) and runs a transport the plugin follows. Keys (while a host window has focus): Space play / stop,
 *  + / - tempo, Esc close. Whether Space still works when the plugin's UI
 *  has the focus is the phase 1 keyboard question, so transport toggles are
 *  logged (AMMO_TEST_LOG=<file>, the same file the plugin UIs append to).
 *
 *      AmmoniteTestHost.exe [path\to\plugin.clap] [-n instances] [-t bpm] [-out name]
 *
 *  -out: the output device whose name contains `name` (e.g. -out CABLE).
 *
 *  Default: AmmoniteDesktop.clap next to the exe (else AmmoniteTestWeb.clap),
 *  one instance, 120 BPM. */
#ifndef NOMINMAX
# define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <initguid.h> // PKEY_Device_FriendlyName defined here
#include <functiondiscoverykeys_devpkey.h>

#include "clap/entry.h"
#include "clap/events.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/gui.h"
#include "clap/ext/params.h"
#include "clap/ext/thread-check.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/process.h"

#include <timeapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{
constexpr uint32_t kMaxBlock   = 128; // small blocks: a telemetry frame every 2.7 ms at 48 kHz
constexpr UINT     kMsgMainCb  = WM_APP + 1; // plugin->on_main_thread requested
const wchar_t*     kClassName  = L"AmmoniteTestHostWindow";

double NowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void Log(const char* fmt, ...)
{
    static const char* path = std::getenv("AMMO_TEST_LOG");
    char               buf[1024];
    va_list            ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    if(path != nullptr && path[0] != 0)
        if(FILE* f = std::fopen(path, "a"))
        {
            std::fprintf(f, "%.0f host: %s\n", NowMs(), buf);
            std::fclose(f);
        }
}

[[noreturn]] void Fatal(const char* fmt, ...)
{
    char    buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Log("FATAL %s", buf);
    MessageBoxA(nullptr, buf, "Ammonite Test Host", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

std::string Utf8(const std::wstring& w)
{
    if(w.empty())
        return {};
    const int   n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

/* ------------------------------------------------------------ transport */
struct Transport
{
    std::atomic<bool>   playing{false};
    std::atomic<double> bpm{120.0};
    double              beat = 0.0; // audio thread only
    double              sampleRate = 48000.0;

    void Fill(clap_event_transport_t& t, uint32_t /*frames*/) const
    {
        std::memset(&t, 0, sizeof t);
        t.header.size   = sizeof t;
        t.header.type   = CLAP_EVENT_TRANSPORT;
        t.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        t.flags         = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE
                          | (playing.load() ? CLAP_TRANSPORT_IS_PLAYING : 0);
        t.tempo          = bpm.load();
        t.song_pos_beats = (clap_beattime)std::llround(beat * CLAP_BEATTIME_FACTOR);
        const long long bar = (long long)std::floor(beat / 4.0);
        t.bar_start      = (clap_beattime)(bar * 4 * CLAP_BEATTIME_FACTOR);
        t.bar_number     = (int32_t)bar;
        t.tsig_num       = 4;
        t.tsig_denom     = 4;
    }

    void Advance(uint32_t frames)
    {
        if(playing.load())
            beat += bpm.load() / 60.0 / sampleRate * (double)frames;
    }
};

Transport gTransport;
DWORD     gMainThread = 0, gAudioThread = 0;

/* ------------------------------------------------------------- instance */
struct Instance
{
    int                        index = 0;
    clap_host_t                host{};
    const clap_plugin_t*       plugin = nullptr;
    const clap_plugin_gui_t*   gui    = nullptr;
    HWND                       hwnd   = nullptr;
    bool                       uiOpen = false;
    std::vector<float>         outL, outR;

    // --- host callbacks
    static Instance* Of(const clap_host_t* h) { return static_cast<Instance*>(h->host_data); }

    static const void* CLAP_ABI GetExtension(const clap_host_t* h, const char* id)
    {
        static const clap_host_gui_t gui = {
            [](const clap_host_t*) {},                                        // resize_hints_changed
            [](const clap_host_t* h, uint32_t w, uint32_t hh) -> bool {       // request_resize
                Instance* const self = Of(h);
                if(self->hwnd == nullptr)
                    return false;
                RECT r = {0, 0, (LONG)w, (LONG)hh};
                AdjustWindowRect(&r, (DWORD)GetWindowLongPtr(self->hwnd, GWL_STYLE), GetMenu(self->hwnd) != nullptr);
                SetWindowPos(self->hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                return true;
            },
            [](const clap_host_t*) -> bool { return true; }, // request_show
            [](const clap_host_t*) -> bool { return true; }, // request_hide
            [](const clap_host_t*, bool) {},                 // closed
        };
        static const clap_host_params_t params = {
            [](const clap_host_t*, clap_param_rescan_flags) {},  // rescan
            [](const clap_host_t*, clap_id, clap_param_clear_flags) {},
            [](const clap_host_t*) {},                           // request_flush: we process all the time
        };
        static const clap_host_thread_check_t threads = {
            [](const clap_host_t*) -> bool { return GetCurrentThreadId() == gMainThread; },
            [](const clap_host_t*) -> bool { return GetCurrentThreadId() == gAudioThread; },
        };
        static const clap_host_audio_ports_t audioPorts = {
            [](const clap_host_t*, uint32_t) -> bool { return false; },
            [](const clap_host_t*, uint32_t) {},
        };
        if(std::strcmp(id, CLAP_EXT_GUI) == 0)
            return &gui;
        if(std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &params;
        if(std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0)
            return &threads;
        if(std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
            return &audioPorts;
        return nullptr;
    }
    static void CLAP_ABI RequestRestart(const clap_host_t* h) { Log("plugin #%d requested a restart (ignored)", Of(h)->index); }
    static void CLAP_ABI RequestProcess(const clap_host_t*) {}
    static void CLAP_ABI RequestCallback(const clap_host_t* h)
    {
        if(HWND w = Of(h)->hwnd)
            PostMessageW(w, kMsgMainCb, 0, 0);
    }

    void InitHost()
    {
        host.clap_version     = CLAP_VERSION;
        host.host_data        = this;
        host.name             = "Ammonite Test Host";
        host.vendor           = "GSNautilus";
        host.url              = "https://github.com/GSNautilus/ammonite";
        host.version          = "0.1.0";
        host.get_extension    = &GetExtension;
        host.request_restart  = &RequestRestart;
        host.request_process  = &RequestProcess;
        host.request_callback = &RequestCallback;
    }
};

std::vector<Instance*>   gInstances;
HMODULE                  gModule = nullptr;
const clap_plugin_entry_t* gEntry = nullptr;
std::atomic<bool>        gQuit{false};
std::string              gTitleBase;

/* --------------------------------------------------------------- output
   The playback device: Windows' default, or one chosen in the plugin's page
   (or -out <part of its name>), remembered as its endpoint id under
   HKCU\Software\Ammonite\Desktop, value Output (empty: the default). A
   switch stops the audio thread, opens the new device and, at another
   sample rate, activates the plugins again at it. */
struct OutputDevice
{
    std::wstring id, name;
};
struct Output
{
    IMMDevice*          device = nullptr;
    IAudioClient*       client = nullptr;
    IAudioRenderClient* render = nullptr;
    HANDLE              event  = nullptr;
    uint32_t            bufferFrames = 0, channels = 2;
    double              rate = 0.0;
    std::wstring        id, name; // id empty: Windows' default
    std::string         mode;
    std::thread         thread;
    std::atomic<bool>   stop{false};
};
Output                    gOut;
IMMDeviceEnumerator*      gEnum = nullptr;
double                    gActiveRate = 0.0;    // the rate the plugins are activated at
int64_t                   gSteady     = 0;      // CLAP steady time, on across devices (audio thread)
const wchar_t*            kRegKey      = L"Software\\Ammonite\\Desktop";

/* --------------------------------------------------------------- audio */
struct ClapIn
{
    clap_input_events_t ev;
    static uint32_t CLAP_ABI Size(const clap_input_events_t*) { return 0; }
    static const clap_event_header_t* CLAP_ABI Get(const clap_input_events_t*, uint32_t) { return nullptr; }
};
struct ClapOut
{
    clap_output_events_t ev;
    static bool CLAP_ABI TryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
};

void AudioThread(IAudioClient* client, IAudioRenderClient* render, HANDLE event, uint32_t bufferFrames, uint32_t channels)
{
    gAudioThread = GetCurrentThreadId();
    DWORD  taskIndex = 0;
    HANDLE task      = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    ClapIn  in;
    ClapOut out;
    in.ev.ctx = nullptr; in.ev.size = &ClapIn::Size; in.ev.get = &ClapIn::Get;
    out.ev.ctx = nullptr; out.ev.try_push = &ClapOut::TryPush;

    for(Instance* inst : gInstances)
    {
        inst->outL.assign(kMaxBlock, 0.f);
        inst->outR.assign(kMaxBlock, 0.f);
        if(!inst->plugin->start_processing(inst->plugin))
            Log("plugin #%d start_processing failed", inst->index);
    }

    // Once a second, for the log: how near the device came to running dry
    // (the fewest frames still queued at a wake-up; 0 = an underrun, a click)
    // and the longest a wake-up's rendering took.
    uint32_t statWakes = 0, statDry = 0, statMinPad = UINT32_MAX;
    double   statMaxMs = 0.0, statStart = NowMs();

    const float gain = gInstances.size() > 1 ? 1.f / (float)gInstances.size() : 1.f;
    while(!gQuit.load() && !gOut.stop.load())
    {
        if(WaitForSingleObject(event, 1000) != WAIT_OBJECT_0)
            continue;
        UINT32 padding = 0;
        if(FAILED(client->GetCurrentPadding(&padding)))
            continue;
        const double wakeMs = NowMs();
        statWakes++;
        statDry += padding == 0;
        statMinPad = padding < statMinPad ? padding : statMinPad;
        uint32_t avail = bufferFrames - padding;
        while(avail > 0 && !gQuit.load() && !gOut.stop.load())
        {
            const uint32_t n = avail < kMaxBlock ? avail : kMaxBlock;
            BYTE* data = nullptr;
            if(FAILED(render->GetBuffer(n, &data)))
                break;
            float* outp = reinterpret_cast<float*>(data);
            std::memset(outp, 0, sizeof(float) * n * channels);

            clap_event_transport_t tr;
            gTransport.Fill(tr, n);
            for(Instance* inst : gInstances)
            {
                float* chans[2] = {inst->outL.data(), inst->outR.data()};
                clap_audio_buffer_t ob{};
                ob.data32        = chans;
                ob.channel_count = 2;
                clap_process_t p{};
                p.steady_time         = gSteady;
                p.frames_count        = n;
                p.transport           = &tr;
                p.audio_inputs        = nullptr;
                p.audio_inputs_count  = 0;
                p.audio_outputs       = &ob;
                p.audio_outputs_count = 1;
                p.in_events           = &in.ev;
                p.out_events          = &out.ev;
                inst->plugin->process(inst->plugin, &p);
                for(uint32_t i = 0; i < n; i++)
                {
                    outp[i * channels]     += inst->outL[i] * gain;
                    outp[i * channels + 1] += inst->outR[i] * gain;
                }
            }
            render->ReleaseBuffer(n, 0);
            gTransport.Advance(n);
            gSteady += n;
            avail -= n;
        }
        const double doneMs = NowMs();
        statMaxMs = doneMs - wakeMs > statMaxMs ? doneMs - wakeMs : statMaxMs;
        if(doneMs - statStart >= 1000.0)
        {
            Log("audio stats: %u wake-ups, %u ran dry, fewest queued %u of %u frames, slowest render %.2f ms", statWakes, statDry,
                statMinPad, bufferFrames, statMaxMs);
            statWakes = statDry = 0;
            statMinPad = UINT32_MAX;
            statMaxMs  = 0.0;
            statStart  = doneMs;
        }
    }
    for(Instance* inst : gInstances)
        inst->plugin->stop_processing(inst->plugin);
    if(task)
        AvRevertMmThreadCharacteristics(task);
}

std::wstring DeviceName(IMMDevice* d)
{
    std::wstring    out;
    IPropertyStore* ps = nullptr;
    if(SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)))
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if(SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            out = v.pwszVal;
        PropVariantClear(&v);
        ps->Release();
    }
    return out;
}

/** The active playback devices, by name. */
std::vector<OutputDevice> ListOutputs()
{
    std::vector<OutputDevice> out;
    IMMDeviceCollection*      col = nullptr;
    if(FAILED(gEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
        return out;
    UINT n = 0;
    col->GetCount(&n);
    for(UINT i = 0; i < n; i++)
    {
        IMMDevice* d  = nullptr;
        LPWSTR     id = nullptr;
        if(FAILED(col->Item(i, &d)))
            continue;
        if(SUCCEEDED(d->GetId(&id)))
        {
            out.push_back({id, DeviceName(d)});
            CoTaskMemFree(id);
        }
        d->Release();
    }
    col->Release();
    std::sort(out.begin(), out.end(), [](const OutputDevice& a, const OutputDevice& b) { return a.name < b.name; });
    return out;
}

std::wstring SavedOutput()
{
    wchar_t buf[512] = L"";
    DWORD   size     = sizeof buf;
    return RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Output", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS ? buf : L"";
}
void SaveOutput(const std::wstring& id)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"Output", REG_SZ, id.c_str(), (DWORD)((id.size() + 1) * sizeof(wchar_t)));
}

void ReleaseOutput()
{
    if(gOut.render) gOut.render->Release();
    if(gOut.client) gOut.client->Release();
    if(gOut.device) gOut.device->Release();
    if(gOut.event) CloseHandle(gOut.event);
    gOut.render = nullptr; gOut.client = nullptr; gOut.device = nullptr; gOut.event = nullptr;
}

/** Open a device (id empty: Windows' default) in shared mode, float32 at its mix rate; false if it cannot be. */
bool OpenOutput(const std::wstring& id)
{
    Output& o  = gOut;
    HRESULT hr = id.empty() ? gEnum->GetDefaultAudioEndpoint(eRender, eConsole, &o.device) : gEnum->GetDevice(id.c_str(), &o.device);
    if(FAILED(hr) || FAILED(o.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&o.client)))
    {
        ReleaseOutput();
        return false;
    }
    WAVEFORMATEX* mix = nullptr;
    o.client->GetMixFormat(&mix);
    o.rate     = mix->nSamplesPerSec;
    o.channels = 2;
    hr         = E_FAIL;

    // First choice: the engine's shortest shared-mode period (IAudioClient3,
    // ~3 ms here) in the mix format, so the plugin renders one small block
    // per wake-up and publishes telemetry frames evenly instead of in
    // bursts every 10 ms. Needs a float mix format (the usual case).
    IAudioClient3* client3 = nullptr;
    const bool mixIsFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                            || (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                                && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    if(mixIsFloat && mix->nChannels >= 2 && SUCCEEDED(o.client->QueryInterface(__uuidof(IAudioClient3), (void**)&client3)))
    {
        UINT32 defP = 0, fundP = 0, minP = 0, maxP = 0;
        if(SUCCEEDED(client3->GetSharedModeEnginePeriod(mix, &defP, &fundP, &minP, &maxP)))
        {
            hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minP, mix, nullptr);
            if(SUCCEEDED(hr))
            {
                o.channels = mix->nChannels;
                char modeBuf[64];
                std::snprintf(modeBuf, sizeof modeBuf, "low-latency period %u frames (default %u)", minP, defP);
                o.mode = modeBuf;
            }
        }
        client3->Release();
    }
    if(FAILED(hr))
    {
        // Fallback: a 20 ms buffer, float32 stereo, WASAPI converts.
        WAVEFORMATEXTENSIBLE fmt{};
        fmt.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
        fmt.Format.nChannels       = 2;
        fmt.Format.nSamplesPerSec  = (DWORD)o.rate;
        fmt.Format.wBitsPerSample  = 32;
        fmt.Format.nBlockAlign     = 8;
        fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
        fmt.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        fmt.Samples.wValidBitsPerSample = 32;
        fmt.dwChannelMask          = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        fmt.SubFormat              = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        o.channels                 = 2;
        hr = o.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                      | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                  200000, 0, &fmt.Format, nullptr);
        o.mode = "default period (10 ms)";
    }
    CoTaskMemFree(mix);
    if(FAILED(hr))
    {
        Log("WASAPI Initialize failed (0x%08lx)", (unsigned long)hr);
        ReleaseOutput();
        return false;
    }
    o.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    o.client->SetEventHandle(o.event);
    o.client->GetBufferSize(&o.bufferFrames);
    if(FAILED(o.client->GetService(__uuidof(IAudioRenderClient), (void**)&o.render)))
    {
        ReleaseOutput();
        return false;
    }
    o.id   = id;
    o.name = DeviceName(o.device);
    Log("audio: %s%s, %.0f Hz, %u channels, %u-frame buffer, %s, blocks of up to %u", Utf8(o.name).c_str(), id.empty() ? " (default)" : "",
        o.rate, o.channels, o.bufferFrames, o.mode.c_str(), kMaxBlock);
    return true;
}

/** The plugins activated at `rate` (again, if they were at another). Not while processing. */
void ActivateAt(double rate)
{
    for(Instance* inst : gInstances)
    {
        if(gActiveRate > 0.0)
            inst->plugin->deactivate(inst->plugin);
        if(!inst->plugin->activate(inst->plugin, rate, 1, kMaxBlock))
            Fatal("activate failed at %.0f Hz", rate);
    }
    gActiveRate           = rate;
    gTransport.sampleRate = rate;
}

void StartOutput()
{
    gOut.stop.store(false);
    gOut.thread = std::thread(AudioThread, gOut.client, gOut.render, gOut.event, gOut.bufferFrames, gOut.channels);
    gOut.client->Start();
}
void StopOutput()
{
    gOut.stop.store(true);
    if(gOut.event)
        SetEvent(gOut.event);
    if(gOut.thread.joinable())
        gOut.thread.join();
    if(gOut.client)
        gOut.client->Stop();
}

void UpdateTitles();

/** Play through another device now (the menu); back to the default if it cannot be opened. */
void SwitchOutput(const std::wstring& id)
{
    if(id == gOut.id && gOut.client)
        return;
    StopOutput();
    ReleaseOutput();
    if(!OpenOutput(id))
    {
        Log("output %s could not be opened: the default instead", Utf8(id).c_str());
        MessageBoxW(nullptr, L"That output device could not be opened; playing through Windows' default instead.", L"Ammonite", MB_ICONWARNING | MB_OK);
        if(!OpenOutput(L""))
            Fatal("no audio output device");
    }
    if(gOut.rate != gActiveRate)
        ActivateAt(gOut.rate);
    StartOutput();
    SaveOutput(gOut.id);
    UpdateTitles();
}

/* -------------------------------------------------------------- window */
void UpdateTitle(Instance* inst)
{
    char t[256];
    std::snprintf(t, sizeof t, "%s #%d   %s  %.0f BPM   -> %s   [Space] play/stop  [+/-] tempo  [Esc] close", gTitleBase.c_str(),
                  inst->index, gTransport.playing.load() ? "playing" : "stopped", gTransport.bpm.load(), Utf8(gOut.name).c_str());
    SetWindowTextA(inst->hwnd, t);
}

void UpdateTitles()
{
    for(Instance* inst : gInstances)
        if(inst->hwnd)
            UpdateTitle(inst);
}

void TogglePlay(const char* source)
{
    const bool now = !gTransport.playing.load();
    gTransport.playing.store(now);
    Log("transport %s (%s)", now ? "play" : "stop", source);
    UpdateTitles();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Instance* inst = reinterpret_cast<Instance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch(msg)
    {
        case WM_CREATE:
        {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case kMsgMainCb:
            if(inst && inst->plugin)
                inst->plugin->on_main_thread(inst->plugin);
            return 0;
        case WM_SIZE:
            if(inst && inst->uiOpen && wp != SIZE_MINIMIZED)
            {
                uint32_t w = LOWORD(lp), h = HIWORD(lp);
                if(inst->gui->can_resize(inst->plugin))
                {
                    inst->gui->adjust_size(inst->plugin, &w, &h);
                    inst->gui->set_size(inst->plugin, w, h);
                }
            }
            return 0;
        case WM_ACTIVATE:
            Log("window #%d %s", inst ? inst->index : 0, LOWORD(wp) == WA_INACTIVE ? "deactivated" : "activated");
            break;
        case WM_SETFOCUS:
            Log("window #%d got focus", inst ? inst->index : 0);
            break;
        case WM_KILLFOCUS:
        {
            char cls[64] = "";
            if(HWND to = reinterpret_cast<HWND>(wp))
                GetClassNameA(to, cls, sizeof cls);
            Log("window #%d lost focus to %s", inst ? inst->index : 0, cls);
            break;
        }
        case WM_KEYDOWN:
            Log("window #%d key 0x%02X", inst ? inst->index : 0, (unsigned)wp);
            switch(wp)
            {
                case VK_SPACE: TogglePlay("host window key"); return 0;
                case VK_ESCAPE: PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
                case VK_OEM_PLUS: case VK_ADD: gTransport.bpm.store(std::fmin(300.0, gTransport.bpm.load() + 5.0)); UpdateTitles(); return 0;
                case VK_OEM_MINUS: case VK_SUBTRACT: gTransport.bpm.store(std::fmax(20.0, gTransport.bpm.load() - 5.0)); UpdateTitles(); return 0;
            }
            break;
        case WM_CLOSE:
            if(inst)
            {
                if(inst->uiOpen)
                {
                    inst->gui->hide(inst->plugin);
                    inst->gui->destroy(inst->plugin);
                    inst->uiOpen = false;
                    Log("plugin #%d UI closed", inst->index);
                }
                inst->hwnd = nullptr;
            }
            DestroyWindow(hwnd);
            {
                bool any = false;
                for(Instance* i : gInstances)
                    any = any || i->hwnd != nullptr;
                if(!any)
                    PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenWindow(Instance* inst, int order)
{
    inst->gui = static_cast<const clap_plugin_gui_t*>(inst->plugin->get_extension(inst->plugin, CLAP_EXT_GUI));
    if(inst->gui == nullptr || !inst->gui->is_api_supported(inst->plugin, CLAP_WINDOW_API_WIN32, false))
        Fatal("the plugin has no embeddable win32 UI");

    inst->hwnd = CreateWindowExW(0, kClassName, L"Ammonite Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 560,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), inst);
    if(inst->hwnd == nullptr)
        Fatal("CreateWindow failed");

    const double t0 = NowMs();
    if(!inst->gui->create(inst->plugin, CLAP_WINDOW_API_WIN32, false))
        Fatal("the plugin UI could not be created");
    uint32_t w = 720, h = 480;
    inst->gui->get_size(inst->plugin, &w, &h);
    // fit the work area (the UI may ask for nearly the whole monitor), keeping its aspect
    RECT work = {0, 0, 1920, 1080};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT frame = {0, 0, 0, 0};
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    const uint32_t maxW = (uint32_t)((work.right - work.left) - (frame.right - frame.left) - 2 * (order ? 40 : 0));
    const uint32_t maxH = (uint32_t)((work.bottom - work.top) - (frame.bottom - frame.top) - 2 * (order ? 40 : 0));
    if(w > maxW || h > maxH)
    {
        const double k = std::fmin((double)maxW / w, (double)maxH / h);
        w              = (uint32_t)(w * k);
        h              = (uint32_t)(h * k);
        if(inst->gui->can_resize(inst->plugin))
        {
            inst->gui->adjust_size(inst->plugin, &w, &h);
            inst->gui->set_size(inst->plugin, w, h);
        }
    }
    RECT r = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    const int winW = r.right - r.left, winH = r.bottom - r.top;
    const int x = work.left + std::max(0, (int)((work.right - work.left) - winW) / 2) + 40 * order;
    const int y = work.top + std::max(0, (int)((work.bottom - work.top) - winH) / 2) + 40 * order;
    SetWindowPos(inst->hwnd, nullptr, x, y, winW, winH, SWP_NOZORDER);

    clap_window_t win{};
    win.api   = CLAP_WINDOW_API_WIN32;
    win.win32 = inst->hwnd;
    if(!inst->gui->set_parent(inst->plugin, &win))
        Fatal("set_parent failed");
    inst->gui->show(inst->plugin);
    inst->uiOpen = true;
    UpdateTitle(inst);
    ShowWindow(inst->hwnd, SW_SHOW);
    UpdateWindow(inst->hwnd);
    Log("plugin #%d UI open %u x %u in %.0f ms", inst->index, w, h, NowMs() - t0);
}
std::wstring Wide(const char* s)
{
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? (size_t)n - 1 : 0, 0);
    if(n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}
void JsonStr(std::string& out, const std::wstring& w)
{
    out += '"';
    for(char c : Utf8(w))
    {
        if(c == '"' || c == '\\')
            out += '\\';
        if((unsigned char)c >= 0x20)
            out += c;
    }
    out += '"';
}
} // namespace

/* ------------------------------------------------------- for the plugin's page
   The plugin's UI looks these up in the process's exe (GetProcAddress on the
   exe's module): only this host has them, so the page shows its output
   control here and nowhere else. Main thread (the UI's idle). */
extern "C" __declspec(dllexport) const char* ammo_host_outputs()
{
    static std::string js;
    js = "{\"cur\":";
    JsonStr(js, gOut.id);
    js += ",\"name\":";
    JsonStr(js, gOut.name);
    js += ",\"rate\":" + std::to_string((long)gOut.rate) + ",\"list\":[";
    bool first = true;
    for(const OutputDevice& d : ListOutputs())
    {
        js += first ? "{\"id\":" : ",{\"id\":";
        JsonStr(js, d.id);
        js += ",\"name\":";
        JsonStr(js, d.name);
        js += "}";
        first = false;
    }
    js += "]}";
    return js.c_str();
}
extern "C" __declspec(dllexport) int ammo_host_output(const char* id)
{
    const std::wstring want = Wide(id ? id : "");
    SwitchOutput(want);
    return gOut.id == want ? 1 : 0;
}

/* ---------------------------------------------------------------- main */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    gMainThread = GetCurrentThreadId();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    timeBeginPeriod(1); // 1 ms timer resolution, as DAWs set: DPF's 16 ms UI idle timer fires on time
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ---- arguments
    int       argc = 0;
    LPWSTR*   argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring clapPath;
    int       instances = 1;
    std::wstring outArg;
    for(int i = 1; i < argc; i++)
    {
        if(std::wcscmp(argv[i], L"-n") == 0 && i + 1 < argc)
            instances = std::max(1, std::min(16, _wtoi(argv[++i])));
        else if(std::wcscmp(argv[i], L"-t") == 0 && i + 1 < argc)
            gTransport.bpm.store(std::max(20.0, std::min(300.0, _wtof(argv[++i]))));
        else if(std::wcscmp(argv[i], L"-out") == 0 && i + 1 < argc)
            outArg = argv[++i];
        else if(std::wcscmp(argv[i], L"-play") == 0)
            gTransport.playing.store(true);
        else
            clapPath = argv[i];
    }
    if(clapPath.empty())
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe);
        dir      = dir.substr(0, dir.find_last_of(L'\\') + 1);
        // the release folder: Ammonite Desktop.exe beside AmmoniteDesktop.clap, so a double-click plays it
        clapPath = dir + L"AmmoniteDesktop.clap";
        if(GetFileAttributesW(clapPath.c_str()) == INVALID_FILE_ATTRIBUTES)
            clapPath = dir + L"AmmoniteTestWeb.clap";
    }
    Log("starting: %s, %d instance(s), %.0f BPM", Utf8(clapPath).c_str(), instances, gTransport.bpm.load());

    // ---- the plugin
    gModule = LoadLibraryW(clapPath.c_str());
    if(gModule == nullptr)
        Fatal("could not load %s (error %lu)", Utf8(clapPath).c_str(), GetLastError());
    gEntry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(gModule, "clap_entry"));
    if(gEntry == nullptr || !gEntry->init(Utf8(clapPath).c_str()))
        Fatal("not a CLAP plugin: %s", Utf8(clapPath).c_str());
    const clap_plugin_factory_t* factory =
        static_cast<const clap_plugin_factory_t*>(gEntry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if(factory == nullptr || factory->get_plugin_count(factory) == 0)
        Fatal("no plugin in the factory");
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    gTitleBase                           = desc->name;
    Log("plugin: %s (%s) by %s", desc->name, desc->id, desc->vendor);

    // ---- audio device: -out <name>, else the one chosen last time, else Windows' default
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&gEnum)))
        Fatal("no audio device enumerator");
    std::wstring want = SavedOutput();
    if(!outArg.empty())
    {
        auto lower = [](std::wstring w) { for(auto& c : w) c = (wchar_t)towlower(c); return w; };
        bool found = false;
        for(const OutputDevice& d : ListOutputs())
            if(lower(d.name).find(lower(outArg)) != std::wstring::npos)
            {
                want  = d.id;
                found = true;
                break;
            }
        if(!found)
            Log("-out %s: no such output device", Utf8(outArg).c_str());
    }
    if(!OpenOutput(want) && (want.empty() || !OpenOutput(L"")))
        Fatal("no audio output device");
    SaveOutput(gOut.id);

    // ---- instances
    for(int i = 0; i < instances; i++)
    {
        Instance* inst = new Instance();
        inst->index    = i + 1;
        inst->InitHost();
        inst->plugin = factory->create_plugin(factory, &inst->host, desc->id);
        if(inst->plugin == nullptr || !inst->plugin->init(inst->plugin))
            Fatal("create_plugin / init failed");
        gInstances.push_back(inst);
    }
    ActivateAt(gOut.rate);

    // ---- windows
    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(14, 14, 16));
    RegisterClassW(&wc);
    for(size_t i = 0; i < gInstances.size(); i++)
        OpenWindow(gInstances[i], (int)i);

    // ---- go
    StartOutput();
    Log("running");

    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ---- shutdown
    gQuit.store(true);
    StopOutput();
    for(Instance* inst : gInstances)
    {
        inst->plugin->deactivate(inst->plugin);
        inst->plugin->destroy(inst->plugin);
        delete inst;
    }
    gInstances.clear();
    ReleaseOutput();
    gEnum->Release();
    gEntry->deinit();
    FreeLibrary(gModule);
    Log("exit");
    CoUninitialize();
    return 0;
}
