/** Ammonite Test, the native UI: NanoVG knobs (face = a screen showing the
 *  live wave, value ring, value text under the knob) over a shell drawn with
 *  raw OpenGL 2 (GLSL 1.20: DPF's default context is a legacy 2.x one), the
 *  same mesh, shaders and blending as the web variant's WebGL shell. The shell's
 *  outline is the telemetry wave, i.e. the real audio.
 *
 *  Overlay: instance, format, sample rate, tempo, frame-time histogram over
 *  the last 10 s (frames, how many over 20 ms, the worst), idle cadence. */
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>

#include "DistrhoUI.hpp"
#include "DistrhoPluginUtils.hpp"
#include "src/nanovg/nanovg.h"
#include "Telemetry.hpp"
#include "TestUICommon.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

START_NAMESPACE_DISTRHO

namespace
{
using namespace ammo;

/* ---------------------------------------------------- GL 2 entry points */
// opengl32.dll exports GL 1.1 only; the rest comes from the driver.
#define AMMO_GL_FUNCS(X)                                              \
    X(PFNGLCREATESHADERPROC, glCreateShader)                          \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                          \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                        \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                            \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                  \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                        \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                          \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                            \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                          \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                              \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)              \
    X(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation)                \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                              \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                  \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                              \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                              \
    X(PFNGLBUFFERDATAPROC, glBufferData)                              \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)    \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)  \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)            \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                          \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                        \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)

#define AMMO_DECL(T, n) static T n = nullptr;
AMMO_GL_FUNCS(AMMO_DECL)
#undef AMMO_DECL

bool LoadGL()
{
    static int state = 0; // 0 untried, 1 ok, -1 failed
    if(state == 0)
    {
        state = 1;
#define AMMO_LOAD(T, n)                              \
    n = (T)wglGetProcAddress(#n);                    \
    if(n == nullptr)                                 \
    {                                                \
        TestLog("gl missing function %s", #n);       \
        state = -1;                                  \
    }
        AMMO_GL_FUNCS(AMMO_LOAD)
#undef AMMO_LOAD
    }
    return state > 0;
}

/* -------------------------------------------------------- the shaders */
// The web variant's ORB_VS / ORB_FS with one change: the wave comes from the
// telemetry (uWave, 256 samples of one period) instead of an oscillator.
const char* kShellVS = R"GLSL(#version 120
attribute vec2 aUV;
uniform float uRad, uAspect, uScale, uCyc, uDepth, uPhase, uBreath;
uniform mat3 uRot;
uniform vec4 uWave[64];
varying vec3 vN; varying float vW;
const float PI = 3.14159265;
float wv(float f) {
  float x = fract(f) * 256.0;
  float i0 = floor(x), t = x - i0;
  float i1 = i0 + 1.0; if (i1 >= 256.0) i1 = 0.0;
  int q0 = int(floor(i0 / 4.0)); float r0 = i0 - 4.0 * float(q0);
  int q1 = int(floor(i1 / 4.0)); float r1 = i1 - 4.0 * float(q1);
  vec4 a = uWave[q0]; vec4 b = uWave[q1];
  float s0 = r0 < 0.5 ? a.x : (r0 < 1.5 ? a.y : (r0 < 2.5 ? a.z : a.w));
  float s1 = r1 < 0.5 ? b.x : (r1 < 1.5 ? b.y : (r1 < 2.5 ? b.z : b.w));
  return mix(s0, s1, t);
}
vec3 pos(float u, float v, out float wn) {
  float phi = -PI * 0.5 + PI * u, lam = 2.0 * PI * v, c = cos(phi);
  vec3 n0 = vec3(c * sin(lam), -sin(phi), c * cos(lam));
  float d = wv(u * uCyc + uPhase) * uDepth;
  wn = clamp(d / max(uDepth * 0.25, 1e-4), -1.0, 1.0);
  vec3 p = n0 * uRad * (1.0 + d);
  p.y *= 1.0 + uBreath; p.xz *= 1.0 + 0.5 * uBreath;
  return p;
}
void main() {
  float u = aUV.x, v = aUV.y, e = 0.003, w0, wd;
  vec3 p = pos(u, v, w0);
  vec3 pu = pos(clamp(u + e, 0.0, 1.0), v, wd) - pos(clamp(u - e, 0.0, 1.0), v, wd);
  vec3 pv = pos(u, v + e, wd) - pos(u, v - e, wd);
  vec3 n = cross(pu, pv); n = length(n) > 1e-7 ? normalize(n) : normalize(p + 1e-6);
  p = uRot * p; vN = uRot * n; vW = w0;
  float w = 1.0 - p.z * 0.22;
  gl_Position = vec4(p.x * uScale / uAspect, p.y * uScale, 0.0, w);
}
)GLSL";

const char* kShellFS = R"GLSL(#version 120
uniform float uInt, uSat; uniform vec3 uCol;
varying vec3 vN; varying float vW;
void main() {
  vec3 n = normalize(vN); float fres = pow(1.0 - abs(n.z), 2.2);
  vec3 c = uCol; float g = dot(c, vec3(0.333)); c = clamp(mix(vec3(g), c, uSat), 0.0, 1.0);
  float crest = 0.6 + 0.5 * vW, front = gl_FrontFacing ? 1.0 : 0.55;
  vec3 col = c * (0.08 + 1.35 * fres) * crest + vec3(1.0) * pow(fres, 7.0) * 0.4;
  gl_FragColor = vec4(col, uInt * (0.08 + 0.8 * fres) * front);
}
)GLSL";

constexpr int kMeshL = 220, kMeshO = 72; // rows pole to pole, columns around

GLuint CompileShader(GLenum type, const char* src)
{
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok)
    {
        char log[2048] = "";
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        TestLog("gl shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

struct Shell
{
    GLuint prog = 0, vb = 0, ib = 0;
    GLint  aUV = -1, uRad, uAspect, uScale, uCyc, uDepth, uPhase, uBreath, uRot, uWave, uInt, uSat, uCol;
    int    nIdx   = 0;
    bool   failed = false;

    bool Init()
    {
        if(!LoadGL())
            return false;
        const GLuint vs = CompileShader(GL_VERTEX_SHADER, kShellVS);
        const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kShellFS);
        if(!vs || !fs)
            return false;
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if(!ok)
        {
            char log[2048] = "";
            glGetProgramInfoLog(prog, sizeof log, nullptr, log);
            TestLog("gl program link failed: %s", log);
            return false;
        }
        aUV     = glGetAttribLocation(prog, "aUV");
        uRad    = glGetUniformLocation(prog, "uRad");
        uAspect = glGetUniformLocation(prog, "uAspect");
        uScale  = glGetUniformLocation(prog, "uScale");
        uCyc    = glGetUniformLocation(prog, "uCyc");
        uDepth  = glGetUniformLocation(prog, "uDepth");
        uPhase  = glGetUniformLocation(prog, "uPhase");
        uBreath = glGetUniformLocation(prog, "uBreath");
        uRot    = glGetUniformLocation(prog, "uRot");
        uWave   = glGetUniformLocation(prog, "uWave");
        uInt    = glGetUniformLocation(prog, "uInt");
        uSat    = glGetUniformLocation(prog, "uSat");
        uCol    = glGetUniformLocation(prog, "uCol");

        std::vector<float>    uv;
        std::vector<uint16_t> idx;
        uv.reserve((kMeshL + 1) * (kMeshO + 1) * 2);
        for(int i = 0; i <= kMeshL; i++)
            for(int j = 0; j <= kMeshO; j++)
            {
                uv.push_back((float)i / kMeshL);
                uv.push_back((float)j / kMeshO);
            }
        for(int i = 0; i < kMeshL; i++)
            for(int j = 0; j < kMeshO; j++)
            {
                const uint16_t a = (uint16_t)(i * (kMeshO + 1) + j), b = (uint16_t)(a + kMeshO + 1);
                idx.push_back(a);
                idx.push_back(b);
                idx.push_back(a + 1);
                idx.push_back(a + 1);
                idx.push_back(b);
                idx.push_back(b + 1);
            }
        nIdx = (int)idx.size();
        glGenBuffers(1, &vb);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(uv.size() * sizeof(float)), uv.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &ib);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * sizeof(uint16_t)), idx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        TestLog("gl program linked (%d vertices, %d indices)", (kMeshL + 1) * (kMeshO + 1), nIdx);
        return true;
    }

    void Draw(const Frame& f, float aspect, double timeSec)
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glUseProgram(prog);
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glEnableVertexAttribArray((GLuint)aUV);
        glVertexAttribPointer((GLuint)aUV, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);

        const float env = f.env > 1.f ? 1.f : f.env;
        float       rgb[3], rot[9];
        HslToRgb(HueOfShape(f.shape), 0.95f, 0.58f, rgb);
        RotationMatrix(-0.3f, (float)(timeSec * 0.1), rot);
        glUniform1f(uRad, 0.5f * (0.9f + 0.15f * env));
        glUniform1f(uAspect, aspect);
        glUniform1f(uScale, 1.0f);
        glUniform1f(uCyc, 4.f);
        glUniform1f(uDepth, 1.5f);
        glUniform1f(uPhase, (float)(-timeSec * 0.25));
        glUniform1f(uBreath, 0.06f * env);
        glUniformMatrix3fv(uRot, 1, GL_FALSE, rot);
        glUniform4fv(uWave, kWaveLen / 4, f.wave);
        glUniform1f(uInt, 0.5f + 0.5f * env);
        glUniform1f(uSat, 1.15f);
        glUniform3f(uCol, rgb[0], rgb[1], rgb[2]);
        glDrawElements(GL_TRIANGLES, nIdx, GL_UNSIGNED_SHORT, nullptr);

        glDisableVertexAttribArray((GLuint)aUV);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glDisable(GL_BLEND);
    }
};

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
} // namespace

class TestUIGL : public UI
{
  public:
    TestUIGL()
    : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true),
      tele_(TelemetryOf(getPluginInstancePointer())),
      t0_(NowMs())
    {
        const double sf = getScaleFactor();
        setGeometryConstraints((uint)(480 * sf), (uint)(320 * sf), true, false);
        loadSharedResources();
        host_[0] = kKnobs[0].def;
        host_[1] = kKnobs[1].def;
        TestLog("ui created (GL) %u x %u scale %.2f", getWidth(), getHeight(), sf);
        // Repaint the moment a telemetry frame is published, not on DPF's idle.
        Telemetry* const t = tele_;
        paced_ = pacer_.Start([t]() { return t != nullptr && t->hasNew(); });
        TestLog("pacer %s", paced_ ? "running" : "unavailable, falling back to idle");
    }

  protected:
    /* ------------------------------------------------------ host side */
    void parameterChanged(uint32_t index, float value) override
    {
        if(index < 2)
        {
            host_[index] = value;
            repaint();
        }
    }

    void uiIdle() override
    {
        idle_.Mark();
        if(!paced_)
        {
            Poll();
            repaint();
        }
    }

    static void OnPace(void* arg)
    {
        TestUIGL* const self = static_cast<TestUIGL*>(arg);
        self->Poll();
        self->repaint();
    }

    /* ---------------------------------------------------------- drawing */
    // Raw GL first: NanoVG only issues its own GL calls at endFrame, after
    // this returns, so the knobs and the overlay land on top of the shell.
    void onNanoDisplay() override
    {
        frames_.Mark();
        if(++paints_ == 60 || paints_ % 600 == 0)
        {
            const FrameStats::Summary p = frames_.Last10s();
            TestLog("%u paints; %.0f paints/s (mean %.1f ms, max %.1f), idle mean %.1f ms", paints_,
                    p.meanMs > 0 ? 1000.0 / p.meanMs : 0.0, p.meanMs, p.maxMs, idle_.Last10s().meanMs);
        }
        glClearColor(14 / 255.f, 14 / 255.f, 16 / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if(!shell_.failed && shell_.prog == 0)
        {
            shell_.failed = !shell_.Init();
            if(shell_.failed)
                TestLog("gl fail");
        }
        if(!shell_.failed && tele_ != nullptr)
            shell_.Draw(tele_->latest(), (float)getWidth() / (float)getHeight(), (NowMs() - t0_) / 1000.0);

        const float s = Scale();
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        const Frame& f = tele_ != nullptr ? tele_->latest() : Frame();
        for(int i = 0; i < 2; i++)
            DrawKnob(kKnobs[i], host_[i], f);
        DrawOverlay(f);
        if(shell_.failed)
        {
            fontSize(16);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(Color(1.f, 0.5f, 0.4f));
            text(kLayoutW / 2, kLayoutH / 2, "OpenGL 2 shaders unavailable (see AMMO_TEST_LOG)", nullptr);
        }
    }

    /* ------------------------------------------------------------ input */
    bool onMouse(const MouseEvent& ev) override
    {
        if(ev.button != kMouseButtonLeft)
            return false;
        const float mx = (float)ev.pos.getX() / Scale(), my = (float)ev.pos.getY() / Scale();
        if(!ev.press)
        {
            if(drag_ >= 0)
            {
                editParameter((uint32_t)kKnobs[drag_].param, false);
                drag_ = -1;
                repaint();
                return true;
            }
            return false;
        }
        const int k = KnobAt(mx, my);
        if(k < 0)
            return false;
        const bool dbl = k == lastClick_ && ev.time - lastClickTime_ < 350;
        lastClick_     = k;
        lastClickTime_ = ev.time;
        if(dbl)
        {
            SetHost(k, kKnobs[k].def, true);
            return true;
        }
        drag_  = k;
        dragY_ = my;
        dragV_ = host_[k];
        editParameter((uint32_t)kKnobs[k].param, true);
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float mx = (float)ev.pos.getX() / Scale(), my = (float)ev.pos.getY() / Scale();
        hover_         = KnobAt(mx, my);
        if(drag_ < 0)
            return false;
        const float span = (ev.mod & kModifierShift) ? kFineSpan : kDragSpan;
        dragV_           = Clamp01(dragV_ + (dragY_ - my) / span);
        dragY_           = my;
        SetHost(drag_, dragV_, false);
        // Windows holds the idle timer back while the mouse moves: keep going.
        if(NowMs() - lastPoll_ > 15.0)
        {
            Poll();
            repaint();
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = (float)ev.pos.getX() / Scale(), my = (float)ev.pos.getY() / Scale();
        const int   k  = KnobAt(mx, my);
        if(k < 0 || ev.delta.getY() == 0.0)
            return false;
        const float step = (ev.mod & kModifierShift) ? 0.005f : 0.02f;
        SetHost(k, Clamp01(host_[k] + (ev.delta.getY() > 0.0 ? step : -step)), true);
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // No key is ours: hand them all to the host (Space = transport).
        // pugl's keycode on Windows is the hardware scan code.
        const unsigned vk = MapVirtualKeyW(ev.keycode, MAPVK_VSC_TO_VK_EX);
        ForwardKeyToHost(reinterpret_cast<void*>(getWindow().getNativeWindowHandle()), vk, ev.press, false);
        return true;
    }

  private:
    float Scale() const { return (float)getWidth() / kLayoutW; }

    void Poll()
    {
        lastPoll_ = NowMs();
        if(tele_ != nullptr)
        {
            // Take everything waiting; the newest is what gets painted.
            Frame f;
            while(tele_->read(f))
            {
                if(!firstTick_)
                {
                    firstTick_ = true;
                    TestLog("first tick seq %u sr %.0f inst %d", f.seq, f.sampleRate, f.instance);
                }
            }
        }
    }

    int KnobAt(float x, float y) const
    {
        for(int i = 0; i < 2; i++)
        {
            const float dx = x - kKnobs[i].cx, dy = y - kKnobs[i].cy, r = kKnobs[i].r + 8.f;
            if(dx * dx + dy * dy <= r * r)
                return i;
        }
        return -1;
    }

    void SetHost(int k, float v, bool gesture)
    {
        if(host_[k] == v)
            return;
        const uint32_t p = (uint32_t)kKnobs[k].param;
        if(gesture)
            editParameter(p, true);
        host_[k] = v;
        setParameterValue(p, v);
        if(gesture)
            editParameter(p, false);
        repaint();
    }

    /* ---- NanoVG painting (a port of the web variant's drawKnob) */
    void Additive(bool on) { nvgGlobalCompositeOperation(getContext(), on ? NVG_LIGHTER : NVG_SOURCE_OVER); }

    Color Hsl(float h, float s, float l, float a = 1.f)
    {
        float rgb[3];
        HslToRgb(h, s / 100.f, l / 100.f, rgb);
        return Color(rgb[0], rgb[1], rgb[2], a);
    }

    void Arc(float cx, float cy, float r, float a0, float a1, const Color& c, float w)
    {
        if(a1 - a0 < 0.002f || r <= 0.f)
            return;
        beginPath();
        arc(cx, cy, r, a0, a1, CW);
        strokeColor(c);
        strokeWidth(w);
        stroke();
    }

    void GlowDot(float x, float y, float r, float h, float s, float k)
    {
        Additive(true);
        const struct { float m, l, a; } L[3] = {{3.f, 55.f, 0.12f}, {1.6f, 60.f, 0.4f}, {0.7f, 92.f, 0.95f}};
        for(int i = 0; i < 3; i++)
        {
            beginPath();
            circle(x, y, r * L[i].m);
            fillColor(Hsl(h, i == 2 ? (s < 50.f ? s : 50.f) : s, L[i].l, L[i].a * k));
            fill();
        }
        Additive(false);
    }

    /** A neon tube along the current path: halo, body, core, added as light.
     *  The path is rebuilt by `trace` three times (NanoVG strokes consume it). */
    template <typename F>
    void Neon(F trace, float h, float s, float w, float k)
    {
        if(k <= 0.01f)
            return;
        Additive(true);
        lineCap(ROUND);
        lineJoin(ROUND);
        const struct { float m, sat, l, a; } L[3] = {{7.f, s, 55.f, 0.06f}, {2.4f, s, 58.f, 0.32f}, {0.75f, s < 60.f ? s : 60.f, 88.f, 0.85f}};
        for(int i = 0; i < 3; i++)
        {
            beginPath();
            trace();
            strokeColor(Hsl(h, L[i].sat, L[i].l, L[i].a * k));
            strokeWidth(w * L[i].m);
            stroke();
        }
        Additive(false);
    }

    void DrawKnob(const KnobSpec& kb, float v, const Frame& f)
    {
        const float cx = kb.cx, cy = kb.cy, R = kb.r;
        const bool  big = R >= 35.f, dragging = drag_ >= 0 && &kKnobs[drag_] == &kb, hov = hover_ >= 0 && &kKnobs[hover_] == &kb;
        const float h = HueOfShape(f.shape), s = 90.f;
        const float A0 = kPiF * 0.75f, SW = kPiF * 1.5f;
        const float tw = std::fmax(2.f, R * 2.f * 0.036f), rv = R - (big ? 7.f : 5.5f), rf = rv - tw / 2 - (big ? 5.f : 3.5f);
        const float amp = f.env > 1.f ? 1.f : f.env;

        // track, value arc with glow, tip
        Arc(cx, cy, rv, A0, A0 + SW, Color(1.f, 1.f, 1.f, 0.07f), tw);
        if(v > 0.003f)
        {
            Additive(true);
            Arc(cx, cy, rv, A0, A0 + SW * v, Hsl(h, s, 58.f, 0.16f + (dragging ? 0.12f : 0.f)), tw * 3.2f);
            Additive(false);
            beginPath();
            arc(cx, cy, rv, A0, A0 + SW * v, CW);
            strokePaint(linearGradient(cx - rv, cy, cx + rv, cy, Hsl(h - 28.f, s, 42.f, 0.55f), Hsl(h, s, 66.f, 1.f)));
            strokeWidth(tw);
            stroke();
        }
        const float ab = A0 + SW * v;
        GlowDot(cx + std::cos(ab) * rv, cy + std::sin(ab) * rv, tw * 0.75f, h, s, 1.f);

        // the face: a dark screen with a faint glow of the voice
        beginPath();
        circle(cx, cy, rf);
        fillPaint(radialGradient(cx - rf * 0.3f, cy - rf * 0.4f, rf * 0.1f, rf, Color(0x22, 0x22, 0x26), Color(0x09, 0x09, 0x0b)));
        fill();
        beginPath();
        circle(cx, cy, rf);
        fillPaint(radialGradient(cx, cy, 0.f, rf, Hsl(h, s, 50.f, 0.05f + 0.13f * amp), Hsl(h, s, 50.f, 0.f)));
        fill();
        Arc(cx, cy, rf, kPiF * 1.1f, kPiF * 1.9f, Color(1.f, 1.f, 1.f, 0.09f), 1.f);

        // the glyph, clipped to the face
        save();
        scissor(cx - rf, cy - rf, 2 * rf, 2 * rf);
        const float gr = rf * 0.8f, lw = std::fmax(1.f, gr / 20.f), k = 0.55f + 0.45f * amp;
        if(kb.param == 0)
        {
            // 'wave': the live telemetry wave, 1.5 periods across the face
            Neon([&]() {
                for(int j = 0; j <= 96; j++)
                {
                    const float u   = (float)j / 96.f;
                    const int   idx = (int)(std::fmod(u * 1.5f + (float)((NowMs() - t0_) * 0.0003), 1.f) * kWaveLen) % kWaveLen;
                    const float y   = cy - f.wave[idx] * 4.f * gr * 0.55f; // peak 0.25 -> full swing
                    if(j)
                        lineTo(cx - gr + 2 * gr * u, y);
                    else
                        moveTo(cx - gr + 2 * gr * u, y);
                }
            }, h, s, lw, k);
        }
        else
        {
            // 'level': a glowing fill up to the value
            const float top = cy + gr - 2 * gr * v * (0.85f + 0.15f * amp);
            beginPath();
            moveTo(cx - gr, top);
            lineTo(cx + gr, top);
            lineTo(cx + gr, cy + gr);
            lineTo(cx - gr, cy + gr);
            closePath();
            fillPaint(linearGradient(cx, top, cx, cy + gr, Hsl(h, s, 60.f, 0.45f * k), Hsl(h - 30.f, s, 40.f, 0.1f)));
            fill();
            Neon([&]() { moveTo(cx - gr, top); lineTo(cx + gr, top); }, h, s, lw * 0.8f, k);
        }
        restore();

        // pointer tick and the value under the knob
        beginPath();
        moveTo(cx + std::cos(ab) * rf * 0.8f, cy + std::sin(ab) * rf * 0.8f);
        lineTo(cx + std::cos(ab) * rf * 0.98f, cy + std::sin(ab) * rf * 0.98f);
        strokeColor(Color(1.f, 1.f, 1.f, 0.85f));
        strokeWidth(1.6f);
        stroke();

        char val[16];
        std::snprintf(val, sizeof val, "%d%%", (int)std::lround(v * 100.f));
        fontSize(big ? 12.f : 11.f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(dragging ? Hsl(h, s < 80.f ? s : 80.f, 74.f) : hov ? Color(0.93f, 0.93f, 0.94f, 0.92f) : Color(0.86f, 0.86f, 0.9f, 0.55f));
        text(cx, cy + R + 9.f, val, nullptr);
        fontSize(10.f);
        fillColor(Color(0.6f, 0.6f, 0.64f, 0.8f));
        text(cx, cy + R + 22.f, kb.name, nullptr);
    }

    void DrawOverlay(const Frame& f)
    {
        const FrameStats::Summary fr = frames_.Last10s(), id = idle_.Last10s();
        char                      line[256];
        fontSize(11.f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(Color(0.86f, 0.86f, 0.9f, 0.75f));
        std::snprintf(line, sizeof line, "Ammonite Test GL   #%d   %s   %.0f Hz   block %u", f.instance,
                      getPluginFormatName(), f.sampleRate, f.blockSize);
        text(12, 10, line, nullptr);
        std::snprintf(line, sizeof line, "%.1f BPM %s   beat %.2f   %s", f.bpm, f.playing ? "playing" : "stopped",
                      f.beat, f.hostClock ? "host clock" : "free clock");
        text(12, 24, line, nullptr);
        std::snprintf(line, sizeof line, "paint 10 s: %d frames  >20 ms: %d  max %.1f ms  mean %.1f ms", fr.frames,
                      fr.slow, fr.maxMs, fr.meanMs);
        text(12, 38, line, nullptr);
        std::snprintf(line, sizeof line, "idle 10 s: %d calls  mean %.1f ms  max %.1f ms   telemetry seq %u", id.frames,
                      id.meanMs, id.maxMs, f.seq);
        text(12, 52, line, nullptr);
        std::snprintf(line, sizeof line, "rms %.3f  peak %.3f  env %.2f  shape %.2f  level %.2f", f.rms, f.peak, f.env,
                      f.shape, f.level);
        text(12, 66, line, nullptr);
    }

    static constexpr float kPiF = 3.14159265f;

    Telemetry* const tele_;
    const double     t0_;
    float            host_[2] = {0.25f, 0.5f};
    Shell            shell_;
    FrameStats       frames_, idle_;
    Pacer            pacer_{&TestUIGL::OnPace, this};
    bool             paced_ = false;
    uint32_t         paints_ = 0;
    bool             firstTick_ = false;
    double           lastPoll_  = 0.0;
    int              drag_ = -1, hover_ = -1, lastClick_ = -1;
    float            dragY_ = 0.f, dragV_ = 0.f;
    uint32_t         lastClickTime_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestUIGL)
};

UI* createUI()
{
    return new TestUIGL();
}

END_NAMESPACE_DISTRHO
