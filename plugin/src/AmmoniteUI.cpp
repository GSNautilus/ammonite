/** Ammonite's panel as the plugin UI: the simulator's layout (sim/panel_sim.py),
 *  12 knobs around the round screen, drawn with NanoVG.
 *
 *  The screen is the engine's own RenderScreen (the pixels the hardware
 *  display gets), about every 38 ms like the display (its fades count
 *  frames). A knob edits the host parameter it drives on the current page
 *  (begin / change / end, so hosts record automation); with a mouse there
 *  is no pickup: a knob always shows the stored value. Knob 11 picks the
 *  page and knob 10 the section, as on the panel; both are UI state kept in
 *  the engine (so the screen shows the maps), not host parameters.
 *
 *  Drag vertically (Shift: fine), scroll, double-click: back to the default. */
#include "DistrhoUI.hpp"
#include "AmmoniteParams.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

START_NAMESPACE_DISTRHO

using ammonite::Engine;

namespace
{
// The panel in the simulator's coordinates; the window scales it.
constexpr float kW = 1400.f, kH = 860.f;
// Knob centres, normalized, measured off the printed panel (as the simulator).
constexpr float kKnobs[12][2] = {
    {0.130f, 0.372f}, {0.240f, 0.366f}, {0.352f, 0.362f}, // 1-3  left top
    {0.130f, 0.610f}, {0.240f, 0.603f}, {0.352f, 0.597f}, // 4-6  left bottom
    {0.660f, 0.358f}, {0.766f, 0.353f}, {0.873f, 0.350f}, // 7-9  right top
    {0.663f, 0.592f}, {0.768f, 0.587f}, {0.874f, 0.582f}, // 10-12 right bottom
};
constexpr float kScreenX = 0.502f * kW, kScreenY = 0.478f * kH;
constexpr float kScreenR = 140.f; // the active area, 240 px shown at 280
constexpr float kKnobR   = 40.f;
constexpr int   kFrameMs = 38;    // the display's frame time (~26 fps)
constexpr float kDragPerPx = 0.005f, kFinePerPx = 0.001f;
constexpr float kPi = 3.14159265f;

// sync toggle, top right of the plate
constexpr float kSyncX = 1215.f, kSyncY = 58.f, kSyncW = 130.f, kSyncH = 30.f;

inline float Clamp01(float v)
{
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

uint32_t NowMs()
{
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

class AmmoniteUI : public UI
{
  public:
    AmmoniteUI()
    : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true),
      engine_(ammonite::EngineOf(getPluginInstancePointer()))
    {
        const double sf = getScaleFactor();
        setGeometryConstraints((uint)(700 * sf), (uint)(430 * sf), true, false);
        loadSharedResources();

        const std::vector<ammonite::ParamRef>& ps = ammonite::Params();
        host_.resize(ps.size());
        for(size_t i = 0; i < ps.size(); i++)
            host_[i] = ammonite::EngineToHost(ps[i].func, engine_->GetParam(ps[i].func, ps[i].osc));
        for(int f = 0; f < Engine::NumFuncs(); f++)
            if(std::strcmp(Engine::FuncName(f), "SYNC") == 0)
                syncIdx_ = ammonite::ParamIndex(f, 0);
        if(engine_->GetPage() < 0)
            engine_->SetPanelPage(0, 0);
        std::memset(rgba_, 0, sizeof rgba_);
        RenderFrame();
    }

  protected:
    /* ------------------------------------------------------ host side */
    void parameterChanged(uint32_t index, float value) override
    {
        if(index < host_.size() && host_[index] != value)
        {
            host_[index] = value;
            repaint();
        }
    }

    void uiIdle() override { MaybeFrame(); }

    /* ---------------------------------------------------------- drawing */
    void onNanoDisplay() override
    {
        const float s = (float)getWidth() / kW;
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        // plate
        beginPath();
        rect(0, 0, kW, kH);
        fillColor(Color(24, 26, 30));
        fill();
        beginPath();
        roundedRect(30, 40, kW - 60, kH - 80, 14);
        fillColor(Color(46, 50, 56));
        fill();
        strokeColor(Color(70, 76, 84));
        strokeWidth(2);
        stroke();

        DrawScreen();
        DrawHeader();
        for(int i = 0; i < 12; i++)
            DrawKnob(i);
    }

    void DrawScreen()
    {
        if(!image_.isValid())
            image_ = createImageFromRGBA(240, 240, rgba_, IMAGE_PREMULTIPLIED);
        else if(frameDirty_)
            image_.update(rgba_);
        frameDirty_ = false;

        beginPath(); // bezel
        circle(kScreenX, kScreenY, kScreenR + 14);
        fillColor(Color(12, 12, 14));
        fill();
        strokeColor(Color(70, 76, 84));
        strokeWidth(2);
        stroke();
        beginPath(); // the frame, masked round like the GC9A01
        circle(kScreenX, kScreenY, kScreenR);
        fillPaint(imagePattern(kScreenX - kScreenR, kScreenY - kScreenR, 2 * kScreenR, 2 * kScreenR, 0.f,
                               image_, 1.f));
        fill();
    }

    void DrawHeader()
    {
        const int page = PageNow();
        const int sub  = engine_->GetSub(page);
        char      label[64];
        const char* sn = engine_->GetSubName(page, sub);
        std::snprintf(label, sizeof label, sn[0] ? "%d  %s > %s" : "%d  %s", page + 1,
                      engine_->GetPageName(page), sn);
        fontSize(22);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(Color(235, 238, 242));
        text(kScreenX, 54, label, nullptr);

        fontSize(15);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(Color(150, 156, 164));
        text(52, 56, "AMMONITE", nullptr);

        // SYNC: DAW / FREE (a host parameter, not on a panel page)
        const bool daw = syncIdx_ >= 0 && host_[syncIdx_] >= 0.5f;
        beginPath();
        roundedRect(kSyncX, kSyncY, kSyncW, kSyncH, 6);
        fillColor(daw ? Color(62, 90, 120) : Color(34, 37, 42));
        fill();
        strokeColor(Color(70, 76, 84));
        strokeWidth(1.5f);
        stroke();
        fontSize(15);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(235, 238, 242));
        text(kSyncX + kSyncW / 2, kSyncY + kSyncH / 2 + 1, daw ? "SYNC  DAW" : "SYNC  FREE", nullptr);
    }

    void DrawKnob(int i)
    {
        const float x = kKnobs[i][0] * kW, y = kKnobs[i][1] * kH;
        const float v = KnobValue(i);

        beginPath();
        circle(x, y, kKnobR);
        fillColor(Color(28, 30, 34));
        fill();
        strokeColor(Color(110, 118, 128));
        strokeWidth(2);
        stroke();

        // value arc over the 270-degree travel (7:30 .. 4:30)
        const float a0 = kPi * 0.75f, a1 = a0 + v * kPi * 1.5f;
        beginPath();
        arc(x, y, kKnobR + 7, a0, a0 + kPi * 1.5f, CW);
        strokeColor(Color(34, 37, 42));
        strokeWidth(4);
        stroke();
        if(v > 0.002f)
        {
            beginPath();
            arc(x, y, kKnobR + 7, a0, a1, CW);
            strokeColor(i == dragPot_ ? Color(235, 190, 110) : Color(150, 170, 190));
            strokeWidth(4);
            stroke();
        }
        // pointer
        const float ang = -2.356194f + v * 4.712389f;
        beginPath();
        moveTo(x, y);
        lineTo(x + kKnobR * 0.78f * std::sin(ang), y - kKnobR * 0.78f * std::cos(ang));
        strokeColor(Color(235, 238, 242));
        strokeWidth(4);
        lineCap(ROUND);
        stroke();

        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        fontSize(15);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fillColor(Color(150, 156, 164));
        text(x, y - kKnobR - 12, num, nullptr);

        // value (the readout's text) and what the knob does
        char val[32], cap[32];
        if(i == ammonite::kPagePot)
            std::snprintf(val, sizeof val, "%s", engine_->GetPageName(PageNow()));
        else
            std::snprintf(val, sizeof val, "%s", engine_->GetValueText(i));
        const int osc = engine_->GetSlotOsc(i);
        if(osc > 0)
            std::snprintf(cap, sizeof cap, "%s %d", engine_->GetSlotName(i), osc);
        else
            std::snprintf(cap, sizeof cap, "%s", engine_->GetSlotName(i));
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(16);
        fillColor(Color(235, 238, 242));
        text(x, y + kKnobR + 14, val, nullptr);
        fontSize(13);
        fillColor(Color(150, 156, 164));
        text(x, y + kKnobR + 34, cap, nullptr);
    }

    /* ------------------------------------------------------------ input */
    bool onMouse(const MouseEvent& ev) override
    {
        if(ev.button != kMouseButtonLeft)
            return false;
        const float mx = (float)ev.pos.getX() / Scale(), my = (float)ev.pos.getY() / Scale();
        if(!ev.press)
        {
            if(dragPot_ >= 0)
            {
                if(dragParam_ >= 0)
                    editParameter((uint32_t)dragParam_, false);
                dragPot_   = -1;
                dragParam_ = -1;
                repaint();
                return true;
            }
            return false;
        }
        if(mx >= kSyncX && mx <= kSyncX + kSyncW && my >= kSyncY && my <= kSyncY + kSyncH && syncIdx_ >= 0)
        {
            SetHost(syncIdx_, host_[syncIdx_] >= 0.5f ? 0.f : 1.f, true);
            return true;
        }
        const int pot = KnobAt(mx, my);
        if(pot < 0)
            return false;

        const bool dbl = pot == lastClickPot_ && ev.time - lastClickTime_ < 350;
        lastClickPot_  = pot;
        lastClickTime_ = ev.time;
        const int idx  = ParamOf(pot);
        if(dbl && idx >= 0)
        {
            const ammonite::ParamRef& r = ammonite::Params()[idx];
            SetHost(idx, ammonite::EngineToHost(r.func, Engine::FuncDefault(r.func, r.osc)), true);
            engine_->ShowPot(pot);
            return true;
        }
        dragPot_   = pot;
        dragParam_ = idx;
        dragY_     = my;
        dragV_     = KnobValue(pot);
        if(idx >= 0)
            editParameter((uint32_t)idx, true);
        engine_->ShowPot(pot);
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if(dragPot_ < 0)
            return false;
        const float my  = (float)ev.pos.getY() / Scale();
        const float per = (ev.mod & kModifierShift) ? kFinePerPx : kDragPerPx;
        dragV_          = Clamp01(dragV_ + (dragY_ - my) * per);
        dragY_          = my;
        SetKnob(dragPot_, dragV_, false);
        MaybeFrame();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = (float)ev.pos.getX() / Scale(), my = (float)ev.pos.getY() / Scale();
        const int   pot = KnobAt(mx, my);
        if(pot < 0 || ev.delta.getY() == 0.0)
            return false;
        const float dir   = ev.delta.getY() > 0.0 ? 1.f : -1.f;
        const int   steps = StepsOf(pot);
        float       v     = KnobValue(pot);
        if(steps > 0)
            v = Clamp01(v + dir / (float)steps);
        else
            v = Clamp01(v + dir * ((ev.mod & kModifierShift) ? 0.005f : 0.02f));
        SetKnob(pot, v, true);
        MaybeFrame();
        return true;
    }

  private:
    /** The next screen frame when one is due (every kFrameMs, the display's
     *  rate: the engine's fades count frames). Called from the idle timer
     *  AND from mouse input: Windows holds timer messages back while mouse
     *  moves keep coming, so a drag alone must keep the screen running. */
    void MaybeFrame()
    {
        const uint32_t now = NowMs();
        if(now - lastFrame_ < (uint32_t)kFrameMs)
            return;
        lastFrame_ = now;
        RenderFrame();
        repaint();
    }

    float Scale() const { return (float)getWidth() / kW; }

    int PageNow() const
    {
        const int p = engine_->GetPage();
        return p < 0 ? 0 : p;
    }

    int KnobAt(float x, float y) const
    {
        for(int i = 0; i < 12; i++)
        {
            const float dx = x - kKnobs[i][0] * kW, dy = y - kKnobs[i][1] * kH;
            if(dx * dx + dy * dy <= (kKnobR + 8) * (kKnobR + 8))
                return i;
        }
        return -1;
    }

    /** The host parameter a knob edits on this page, -1 for PAGE and the
     *  section selector. */
    int ParamOf(int pot) const
    {
        int f, o;
        return engine_->GetSlotFunc(pot, &f, &o) ? ammonite::ParamIndex(f, o) : -1;
    }

    /** Positions of a stepped knob (0 = continuous). */
    int StepsOf(int pot) const
    {
        if(pot == ammonite::kPagePot)
            return ammonite::kNumPages;
        const int idx = ParamOf(pot);
        if(idx < 0)
            return engine_->GetNumSubs(PageNow()); // the section selector
        return Engine::FuncSteps(ammonite::Params()[idx].func);
    }

    /** Where a knob points, 0..1 (a step: its centre). */
    float KnobValue(int pot) const
    {
        const int page = PageNow();
        if(pot == ammonite::kPagePot)
            return ((float)page + 0.5f) / (float)ammonite::kNumPages;
        const int idx = ParamOf(pot);
        if(idx < 0)
        {
            const int n = engine_->GetNumSubs(page);
            return ((float)engine_->GetSub(page) + 0.5f) / (float)(n > 0 ? n : 1);
        }
        return ammonite::HostToEngine(ammonite::Params()[idx].func, host_[idx]);
    }

    /** Turn a knob to v (0..1): a page, a section or a host parameter. */
    void SetKnob(int pot, float v, bool gesture)
    {
        const int page = PageNow();
        if(pot == ammonite::kPagePot)
        {
            const int p = ammonite::StepOfValue(v, ammonite::kNumPages);
            if(p != page)
                engine_->SetPanelPage(p, engine_->GetSub(p));
        }
        else if(ParamOf(pot) < 0)
        {
            const int s = ammonite::StepOfValue(v, engine_->GetNumSubs(page));
            if(s != engine_->GetSub(page))
                engine_->SetPanelPage(page, s);
        }
        else
        {
            const int idx = ParamOf(pot);
            SetHost(idx, ammonite::EngineToHost(ammonite::Params()[idx].func, v), gesture);
        }
        engine_->ShowPot(pot);
        repaint();
    }

    /** A host parameter edit; `gesture` wraps it in begin / end (a click or
     *  a scroll step; a drag is wrapped by the mouse press and release). */
    void SetHost(int idx, float value, bool gesture)
    {
        if(host_[idx] == value)
            return;
        if(gesture)
            editParameter((uint32_t)idx, true);
        host_[idx] = value;
        // Straight into the engine too (its parameters are atomics): the
        // sound and the screen answer now, not after the host has passed
        // the change to the audio thread at its next buffer.
        const ammonite::ParamRef& r = ammonite::Params()[idx];
        engine_->SetParam(r.func, r.osc, ammonite::HostToEngine(r.func, value));
        setParameterValue((uint32_t)idx, value);
        if(gesture)
            editParameter((uint32_t)idx, false);
        repaint();
    }

    /** The engine's next screen frame, RGB565 in panel byte order -> RGBA. */
    void RenderFrame()
    {
        engine_->RenderScreen(fb_);
        for(int i = 0; i < 240 * 240; i++)
        {
            const uint16_t v = (uint16_t)((fb_[i] >> 8) | (fb_[i] << 8)); // panel byte order
            const uint8_t  r = (uint8_t)((v >> 11) & 0x1F), g = (uint8_t)((v >> 5) & 0x3F),
                          b = (uint8_t)(v & 0x1F);
            rgba_[4 * i]     = (uint8_t)((r << 3) | (r >> 2));
            rgba_[4 * i + 1] = (uint8_t)((g << 2) | (g >> 4));
            rgba_[4 * i + 2] = (uint8_t)((b << 3) | (b >> 2));
            rgba_[4 * i + 3] = 255;
        }
        frameDirty_ = true;
    }

    Engine* const      engine_;
    std::vector<float> host_; // host values of every parameter
    int                syncIdx_ = -1;

    uint16_t  fb_[240 * 240];
    uint8_t   rgba_[240 * 240 * 4];
    NanoImage image_;
    bool      frameDirty_ = false;
    uint32_t  lastFrame_  = 0;

    int      dragPot_ = -1, dragParam_ = -1;
    float    dragY_ = 0.f, dragV_ = 0.f;
    int      lastClickPot_  = -1;
    uint32_t lastClickTime_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmmoniteUI)
};

UI* createUI()
{
    return new AmmoniteUI();
}

END_NAMESPACE_DISTRHO
