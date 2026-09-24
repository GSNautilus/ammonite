/** The host parameters, shared by the DSP and the UI side of the plugin.
 *
 *  IDs: one per (func, osc) in enum Func order, osc 1..3 for a
 *  per-oscillator function. New functions only go at the END of enum Func,
 *  so an ID never changes meaning (hosts store automation and state by ID).
 *
 *  Values: a continuous function is 0..100 (the engine's 0..1), a stepped
 *  one is its step 0..n-1 with the step names the panel shows. */
#pragma once
#include "engine.h"

#include <cmath>
#include <vector>

namespace ammonite
{
struct ParamRef
{
    int func;
    int osc;
};

inline const std::vector<ParamRef>& Params()
{
    static const std::vector<ParamRef> list = [] {
        std::vector<ParamRef> v;
        for(int f = 0; f < Engine::NumFuncs(); f++)
            for(int o = 0; o < (Engine::FuncPerOsc(f) ? kOscs : 1); o++)
                v.push_back({f, o});
        return v;
    }();
    return list;
}

/** The parameter index of (func, osc), -1 if there is none. */
inline int ParamIndex(int func, int osc)
{
    const std::vector<ParamRef>& ps = Params();
    for(size_t i = 0; i < ps.size(); i++)
        if(ps[i].func == func && ps[i].osc == (Engine::FuncPerOsc(func) ? osc : 0))
            return (int)i;
    return -1;
}

/** The step of a stepped function's engine value (0..1). */
inline int StepOfValue(float v, int steps)
{
    int s = (int)(v * (float)steps);
    return s < 0 ? 0 : (s > steps - 1 ? steps - 1 : s);
}

/** Host value -> engine value (0..1): a step lands on its centre. */
inline float HostToEngine(int func, float host)
{
    const int steps = Engine::FuncSteps(func);
    if(steps <= 0)
        return host < 0.f ? 0.f : (host > 100.f ? 1.f : host * 0.01f);
    int s = (int)std::lround(host);
    s     = s < 0 ? 0 : (s > steps - 1 ? steps - 1 : s);
    return ((float)s + 0.5f) / (float)steps;
}

/** Engine value (0..1) -> host value. */
inline float EngineToHost(int func, float v)
{
    const int steps = Engine::FuncSteps(func);
    return steps > 0 ? (float)StepOfValue(v, steps) : v * 100.f;
}

/** The engine of a plugin instance (DPF's getPluginInstancePointer(), UI
 *  side). Defined in AmmonitePlugin.cpp: the DSP and the UI are one module. */
Engine* EngineOf(void* pluginInstance);
} // namespace ammonite
