/** ammonite_engine.dll: the C API over ammo::Engine (api.h). */
#define AMMO_BUILD_DLL
#include "api.h"

#include "Engine.hpp"
#include "Presets.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#endif

using ammo::Engine;

namespace
{
Engine* E(void* p) { return static_cast<Engine*>(p); }

/** A recording through the C API: the tap drained into the file every
 *  millisecond by a thread of its own. */
struct ApiRecording
{
    ammo::WavWriter   wav;
    std::thread       drain;
    std::atomic<bool> run{true};
};
std::mutex                                     gRecLock;
std::map<void*, std::unique_ptr<ApiRecording>> gRecs;
std::map<void*, unsigned>                      gDropped;

FILE* OpenUtf8(const char* path)
{
#ifdef _WIN32
    wchar_t   w[1024];
    const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024);
    return n > 0 ? _wfopen(w, L"wb") : nullptr;
#else
    return std::fopen(path, "wb");
#endif
}

long long StopRecording(void* engine)
{
    std::unique_ptr<ApiRecording> rec;
    {
        std::lock_guard<std::mutex> lk(gRecLock);
        auto                        it = gRecs.find(engine);
        if(it == gRecs.end())
            return -1;
        rec = std::move(it->second);
        gRecs.erase(it);
    }
    ammo::RecordTap& tap = E(engine)->recordTap();
    tap.Stop();
    rec->run.store(false);
    rec->drain.join();
    ammo::DrainTap(tap, rec->wav);
    {
        std::lock_guard<std::mutex> lk(gRecLock);
        gDropped[engine] = tap.Dropped();
    }
    const long long frames = (long long)rec->wav.Frames();
    return rec->wav.Close() ? frames : -1;
}

void CopyStr(char* dst, int cap, const char* src)
{
    std::snprintf(dst, (size_t)cap, "%s", src ? src : "");
}

using ammo::VisibleId;
} // namespace

extern "C" {

AMMO_API void* ammo_create(double sampleRate, int maxBlock) { return new Engine(sampleRate, maxBlock); }
AMMO_API void ammo_destroy(void* engine)
{
    StopRecording(engine);
    {
        std::lock_guard<std::mutex> lk(gRecLock);
        gDropped.erase(engine);
    }
    delete E(engine);
}

AMMO_API int ammo_record_start(void* engine, const char* pathUtf8)
{
    if(engine == nullptr || pathUtf8 == nullptr)
        return 0;
    StopRecording(engine);
    auto rec = std::make_unique<ApiRecording>();
    if(!rec->wav.Open(OpenUtf8(pathUtf8), (int)E(engine)->SampleRate()))
        return 0;
    ammo::RecordTap& tap = E(engine)->recordTap();
    tap.Start();
    ApiRecording* const r = rec.get();
    rec->drain            = std::thread([r, &tap]() {
        while(r->run.load())
        {
            ammo::DrainTap(tap, r->wav);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::lock_guard<std::mutex> lk(gRecLock);
    gRecs[engine] = std::move(rec);
    return 1;
}

AMMO_API long long ammo_record_stop(void* engine) { return engine != nullptr ? StopRecording(engine) : -1; }

AMMO_API int ammo_latency(void* engine) { return engine != nullptr ? E(engine)->latency() : 0; }

AMMO_API unsigned ammo_record_dropped(void* engine)
{
    std::lock_guard<std::mutex> lk(gRecLock);
    auto                        it = gDropped.find(engine);
    if(it != gDropped.end())
        return it->second;
    return engine != nullptr ? E(engine)->recordTap().Dropped() : 0u;
}

AMMO_API int ammo_param_count(void) { return ammo::VisibleParamCount(); }

AMMO_API int ammo_preset_count(void) { return ammo::kNumPresets; }
AMMO_API const char* ammo_preset_name(int k) { return k >= 0 && k < ammo::kNumPresets ? ammo::kPresets[k].name : ""; }
AMMO_API const char* ammo_preset_blurb(int k) { return k >= 0 && k < ammo::kNumPresets ? ammo::kPresets[k].blurb : ""; }
AMMO_API int ammo_apply_preset(void* engine, int k)
{
    if(k < 0 || k >= ammo::kNumPresets)
        return 0;
    for(int id = 0; id < ammo::kNumIds; id++) // every saved parameter, the routes too
        if(ammo::IsSaved(id))
            E(engine)->SetParam(id, ammo::PresetValue(k, id));
    return 1;
}

AMMO_API int ammo_param_info(int index, AmmoParamInfo* out) { return ammo_param_info_id(VisibleId(index), out); }

AMMO_API int ammo_param_info_id(int id, AmmoParamInfo* out)
{
    if(id < 0 || id >= ammo::kNumIds || out == nullptr)
        return 0;
    const ammo::ParamDef& d = ammo::ParamInfo(id);
    std::memset(out, 0, sizeof *out);
    out->id = id;
    int slot, p;
    if(ammo::IsSlotId(id, slot, p))
    {
        char name[48], sym[32], grp[16];
        std::snprintf(name, sizeof name, "%d %s", slot + 1, d.name);
        std::snprintf(sym, sizeof sym, "s%d_%s", slot + 1, d.symbol);
        std::snprintf(grp, sizeof grp, "slot %d", slot + 1);
        CopyStr(out->name, sizeof out->name, name);
        CopyStr(out->symbol, sizeof out->symbol, sym);
        CopyStr(out->group, sizeof out->group, grp);
    }
    else
    {
        CopyStr(out->name, sizeof out->name, d.name);
        CopyStr(out->symbol, sizeof out->symbol, d.symbol);
        CopyStr(out->group, sizeof out->group, ammo::IsPoolId(id) ? "pool" : "global");
    }
    out->min   = d.min;
    out->max   = d.max;
    out->def   = d.def;
    out->steps = d.steps;
    out->flags = d.flags;
    return 1;
}

AMMO_API int ammo_param_step_name(int id, int step, char* buf, int cap)
{
    const ammo::ParamDef& d = ammo::ParamInfo(id);
    if(d.steps <= 0 || buf == nullptr || cap <= 0)
        return 0;
    const int k = step - (int)d.min;
    if(k < 0 || k >= d.steps)
        return 0;
    if(d.names)
        CopyStr(buf, cap, d.names[k]);
    else
        std::snprintf(buf, (size_t)cap, "%+d", step);
    return 1;
}

AMMO_API void  ammo_set_param(void* engine, int id, float value) { E(engine)->SetParam(id, value); }
AMMO_API float ammo_get_param(void* engine, int id) { return E(engine)->GetParam(id); }

AMMO_API void ammo_process(void* engine, float* outL, float* outR, int frames, const AmmoClock* clock)
{
    ammo::HostClock hc;
    if(clock)
    {
        hc.valid   = clock->valid != 0;
        hc.playing = clock->playing != 0;
        hc.bpm     = clock->bpm;
        hc.beat    = clock->beat;
    }
    E(engine)->Process(outL, outR, frames, clock ? &hc : nullptr);
}

AMMO_API int ammo_read_frame(void* engine, AmmoFrame* out)
{
    return out != nullptr && E(engine)->telemetry().read(*out) ? 1 : 0;
}

AMMO_API int ammo_save(void* engine, char* buf, int cap)
{
    const std::string s = E(engine)->Save();
    if(buf != nullptr && cap > 0)
        std::snprintf(buf, (size_t)cap, "%s", s.c_str());
    return (int)s.size();
}

AMMO_API int ammo_load(void* engine, const char* buf, int len) { return E(engine)->Load(buf, (size_t)len) ? 1 : 0; }

AMMO_API const char* ammo_version(void) { return "0.2.0-phase2"; }

} // extern "C"
