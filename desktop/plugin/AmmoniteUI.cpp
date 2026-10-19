/** Ammonite Desktop's UI: the WebView2 page (plugin/web/, bundled into
 *  the binary; AMMO_WEB_DIR=<dir> loads <dir>/index.html from disk) over a
 *  DPF window. The phase 1 pattern, grown: on `ready` the page gets the
 *  parameter table, every value and the preset list; telemetry frames are
 *  pushed the moment the audio thread publishes them (Pacer), waves as
 *  base64 signed bytes; the page sends one-line strings back.
 *
 *  Patch files (.ammo): the engine's
 *  chunk plus a `page {json}` line with the page's own state. Save and
 *  Open show the native dialogs from uiIdle, never inside the web view's
 *  message handler. Recording: the engine's RecordTap drained into a WAV
 *  on every tick, in Documents\Ammonite\Recordings.
 *
 *  The page has every saved parameter: the host's, and the modulation routes,
 *  which are not host parameters (engine/Params.hpp pfNoHost): the page sets
 *  those in the engine directly and the plugin's "routes" state follows
 *  (setState at the next idle), so the host saves them with its project.
 *
 *  Page -> C++: ready | set <id> <v> | edit <id> <0|1> | preset <k> |
 *               save <json> | open | record <0|1> |
 *               key <down> <vk> <repeat> | gl ok ... | gl fail ... | log ...
 *  C++ -> page: ammo.params([...]) ammo.values([[id,v],...]) ammo.param(id,v)
 *               ammo.presets([{name,blurb}], k, file) ammo.page("json") ammo.rec({...})
 *               ammo.toast("text") ammo.info({}) ammo.tick([...], host) */
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#include "DistrhoUI.hpp"
#include "DistrhoPluginUtils.hpp"
#include "extra/WebView.hpp"
#include "Engine.hpp"
#include "IndexHtml.hpp"
#include "Presets.hpp"
#include "UICommon.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

ammo::Engine* AmmoniteEngineOf(void* pluginInstance);

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
        if(dir[0] != 0)
        {
            std::string p(dir);
            for(char& c : p)
                if(c == '\\')
                    c = '/';
            return "file:///" + p + "/index.html";
        }
    return "data:text/html;charset=utf-8;base64," + Base64(kIndexHtml, kIndexHtmlLen);
}

void Num(std::string& s, double v, const char* fmt = "%.4g")
{
    char b[32];
    std::snprintf(b, sizeof b, fmt, v);
    s += b;
}

void Str(std::string& s, const char* t)
{
    s += '"';
    for(; *t; t++)
    {
        if((unsigned char)*t < 0x20)
        {
            char u[8];
            std::snprintf(u, sizeof u, "\\u%04x", (unsigned)*t);
            s += u;
            continue;
        }
        if(*t == '"' || *t == '\\')
            s += '\\';
        s += *t;
    }
    s += '"';
}

std::string Utf8(const std::wstring& w)
{
    if(w.empty())
        return std::string();
    const int   n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

/** Documents\Ammonite\<sub>, created if missing; empty if there is no Documents. */
std::wstring AmmoniteFolder(const wchar_t* sub)
{
    PWSTR docs = nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
        return std::wstring();
    std::wstring dir = std::wstring(docs) + L"\\Ammonite\\" + sub;
    CoTaskMemFree(docs);
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

/** A name that is safe as a file name. */
std::wstring FileSafe(std::wstring s)
{
    for(wchar_t& c : s)
        if(c < 32 || std::wcschr(L"\\/:*?\"<>|", c) != nullptr)
            c = L'-';
    return s;
}

/** The file name without its folder and extension. */
std::wstring Stem(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name  = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot   = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

/** The native Save or Open dialog for .ammo files: the chosen path, or empty. */
std::wstring PatchDialog(HWND owner, bool save, const std::wstring& name)
{
    const HRESULT co  = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring  path;
    IFileDialog*  dlg = nullptr;
    if(SUCCEEDED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  save ? IID_IFileSaveDialog : IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg))))
    {
        const COMDLG_FILTERSPEC types[] = {{L"Ammonite patch (*.ammo)", L"*.ammo"}, {L"All files (*.*)", L"*.*"}};
        dlg->SetFileTypes(2, types);
        dlg->SetDefaultExtension(L"ammo");
        dlg->SetTitle(save ? L"Save the patch" : L"Open a patch");
        const std::wstring dir    = AmmoniteFolder(L"Patches");
        IShellItem*        folder = nullptr;
        if(!dir.empty() && SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder))))
        {
            dlg->SetDefaultFolder(folder);
            folder->Release();
        }
        if(save && !name.empty())
            dlg->SetFileName(FileSafe(name).c_str());
        IShellItem* item = nullptr;
        if(SUCCEEDED(dlg->Show(owner)) && SUCCEEDED(dlg->GetResult(&item)))
        {
            PWSTR p = nullptr;
            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)))
            {
                path = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
        dlg->Release();
    }
    if(SUCCEEDED(co))
        CoUninitialize();
    return path;
}

bool ReadWholeFile(const std::wstring& path, std::string& out)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if(f == nullptr)
        return false;
    char   buf[16384];
    size_t n;
    out.clear();
    while(out.size() < (4u << 20) && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    return true;
}

/** The text after "page " on its own line, or empty. */
std::string PageLine(const std::string& text)
{
    size_t at = 0;
    while(at < text.size())
    {
        size_t end = text.find('\n', at);
        if(end == std::string::npos)
            end = text.size();
        if(text.compare(at, 5, "page ") == 0)
        {
            size_t e = end;
            while(e > at + 5 && (text[e - 1] == '\r' || text[e - 1] == ' '))
                e--;
            return text.substr(at + 5, e - at - 5);
        }
        at = end + 1;
    }
    return std::string();
}
} // namespace

class AmmoniteUI : public UI
{
  public:
    AmmoniteUI()
    : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, false), engine_(AmmoniteEngineOf(getPluginInstancePointer()))
    {
        const double sf = getScaleFactor();
        // any shape (a window snapped to half a screen is tall): the page scales its layout to the window and keeps
        // the panel docked beside the stage; small enough for half of a 1080p screen at 150 %
        setGeometryConstraints((uint)(640 * sf), (uint)(360 * sf), false, false);
        loadSharedResources();

        const std::string url = PageUrl();
        WebViewOptions    opts(&MessageCallback, this);
        const double      t0 = NowMs();
        web_      = webViewCreate(url.c_str(), getWindow().getNativeWindowHandle(), getWidth(), getHeight(), sf, opts);
        createMs_ = NowMs() - t0;
        TestLog("ui created (Ammonite) %u x %u scale %.2f; webview %s in %.0f ms (%s)", getWidth(), getHeight(), sf,
                web_ != nullptr ? "created" : "FAILED", createMs_, url.compare(0, 5, "file:") == 0 ? "file" : "data");
        Engine* const e = engine_;
        paced_          = web_ != nullptr && pacer_.Start([e]() { return e != nullptr && e->telemetry().hasNew(); });
    }

    ~AmmoniteUI() override
    {
        StopRecording(false); // a recording lives with the UI: closing it ends the file properly
        if(web_ != nullptr)
            webViewDestroy(web_);
    }

  protected:
    /** The host restored its project: the routes are in the engine (the plugin's setState), the page wants them. */
    void stateChanged(const char* key, const char*) override
    {
        if(ready_ && std::strcmp(key, "routes") == 0)
            SendValues();
    }

    void parameterChanged(uint32_t index, float value) override
    {
        if(!ready_)
            return;
        std::string js = "ammo.param(";
        Num(js, VisibleId((int)index), "%.0f");
        js += ",";
        Num(js, value, "%.6g");
        js += ")";
        Eval(js);
    }

    void uiIdle() override
    {
        idle_.Mark();
        if(web_ == nullptr)
            return;
        webViewIdle(web_);
        if(!paced_)
            Tick();
        DrainRecording();
        // the routes the page set since the last idle, into the plugin's state (the host sees it change and saves it)
        if(routesDirty_ && engine_ != nullptr)
        {
            routesDirty_ = false;
            setState("routes", engine_->SaveRoutes().c_str());
        }
        // the dialogs the page asked for, outside its message handler (the dialog runs a message loop of its own,
        // where this is called again: inDialog_ keeps it to one)
        // a switch of the host's output, outside the page's message handler (it stops the audio thread, and may
        // activate the plugin again at another rate; a failure shows a message box)
        if(!inDialog_ && haveWantOutput_)
        {
            inDialog_       = true;
            haveWantOutput_ = false;
            hostOutput_(wantOutput_.c_str());
            inDialog_ = false;
            SendOutputs();
        }
        if(!inDialog_ && (wantSave_ || wantOpen_))
        {
            inDialog_ = true;
            if(wantSave_)
            {
                wantSave_ = false;
                SavePatch();
            }
            else
            {
                wantOpen_ = false;
                OpenPatch();
            }
            inDialog_ = false;
        }
    }

    void onResize(const ResizeEvent& ev) override
    {
        UI::onResize(ev);
        if(web_ != nullptr)
            webViewResize(web_, ev.size.getWidth(), ev.size.getHeight(), getScaleFactor());
    }

    void onNanoDisplay() override
    {
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
    static void MessageCallback(void* arg, char* msg) { static_cast<AmmoniteUI*>(arg)->OnMessage(msg); }

    void OnMessage(const char* msg)
    {
        if(std::strcmp(msg, "ready") == 0)
        {
            ready_ = true;
            TestLog("page ready");
            SendParams();
            SendValues();
            SendPresets();
            if(wav_.IsOpen()) // the page reloaded while recording
            {
                std::string rj = "ammo.rec({on:1,secs:";
                Num(rj, (double)wav_.Frames() / recRate_, "%.2f");
                Eval(rj + "})");
            }
            std::string js = "ammo.info({fmt:";
            Str(js, getPluginFormatName());
            js += ",createMs:";
            Num(js, createMs_);
            js += ",inst:";
            Num(js, engine_ ? engine_->instance() : 0, "%.0f");
            js += "})";
            Eval(js);
            SendOutputs();
            return;
        }
        // the output device: our standalone host's (in a DAW the page never hears of one)
        if(std::strcmp(msg, "outputs") == 0)
        {
            SendOutputs();
            return;
        }
        if(std::strncmp(msg, "output ", 7) == 0 || std::strcmp(msg, "output") == 0)
        {
            wantOutput_    = msg[6] == ' ' ? msg + 7 : "";
            haveWantOutput_ = hostOutput_ != nullptr;
            return;
        }
        if(std::strncmp(msg, "set ", 4) == 0)
        {
            int   id = -1;
            float v  = 0.f;
            if(std::sscanf(msg + 4, "%d %f", &id, &v) == 2)
            {
                const int idx = VisibleIndexOf(id);
                if(idx >= 0)
                    setParameterValue((uint32_t)idx, ClampParam(id, v));
                else if(IsRouteId(id) && engine_ != nullptr) // not a host parameter: the engine's, then the state
                {
                    engine_->SetParam(id, v);
                    routesDirty_ = true;
                }
            }
            return;
        }
        if(std::strncmp(msg, "edit ", 5) == 0)
        {
            int id = -1, started = 0;
            if(std::sscanf(msg + 5, "%d %d", &id, &started) == 2)
            {
                const int idx = VisibleIndexOf(id);
                if(idx >= 0)
                    editParameter((uint32_t)idx, started != 0);
            }
            return;
        }
        if(std::strncmp(msg, "preset ", 7) == 0)
        {
            const int k = std::atoi(msg + 7);
            if(k >= 0 && k < kNumPresets)
            {
                std::vector<float> values(kNumIds);
                for(int id = 0; id < kNumIds; id++)
                    values[id] = PresetValue(k, id);
                ApplyValues(values.data());
                preset_ = k;
                file_.clear();
                SendPresets();
                TestLog("preset %d applied", k);
            }
            return;
        }
        if(std::strncmp(msg, "save ", 5) == 0 || std::strcmp(msg, "save") == 0)
        {
            pageJson_ = msg[4] == ' ' ? msg + 5 : "";
            for(char& c : pageJson_)
                if(c == '\n' || c == '\r')
                    c = ' ';
            wantSave_ = true;
            return;
        }
        if(std::strcmp(msg, "open") == 0)
        {
            wantOpen_ = true;
            return;
        }
        if(std::strncmp(msg, "record ", 7) == 0)
        {
            if(std::atoi(msg + 7) != 0)
                StartRecording();
            else
                StopRecording(true);
            return;
        }
        if(std::strncmp(msg, "key ", 4) == 0)
        {
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
    /** The host's output devices to the page: ammo.outputs({cur, name, rate, list: [{id, name}]}). */
    void SendOutputs()
    {
        if(hostOutputs_ != nullptr && ready_)
            Eval(std::string("ammo.outputs(") + hostOutputs_() + ")");
    }

    void Eval(const std::string& js)
    {
        const double t0 = NowMs();
        webViewEvaluateJS(web_, js.c_str());
        lastEvalMs_ = NowMs() - t0;
    }

    /** The parameter table: id, name, symbol, slot (-1 global or the pool), min, max, def, steps, flags, names;
     *  every saved parameter (the host's and the routes). Once, at ready. */
    void SendParams()
    {
        std::string js;
        js.reserve(160000);
        js += "ammo.params([";
        bool first = true;
        for(int id = 0; id < kNumIds; id++)
        {
            if(!IsSaved(id))
                continue;
            const ParamDef& d = ParamInfo(id);
            int             slot = -1, sp = 0;
            IsSlotId(id, slot, sp);
            js += first ? "{id:" : ",{id:";
            first = false;
            Num(js, id, "%.0f");
            js += ",n:";
            Str(js, d.name);
            js += ",s:";
            Str(js, d.symbol);
            js += ",slot:";
            Num(js, slot, "%.0f");
            js += ",min:";
            Num(js, d.min, "%.6g");
            js += ",max:";
            Num(js, d.max, "%.6g");
            js += ",def:";
            Num(js, d.def, "%.6g");
            js += ",steps:";
            Num(js, d.steps, "%.0f");
            js += ",flags:";
            Num(js, d.flags, "%.0f");
            if(d.steps > 0 && d.names != nullptr)
            {
                js += ",names:[";
                for(int k = 0; k < d.steps; k++)
                {
                    if(k)
                        js += ",";
                    Str(js, d.names[k]);
                }
                js += "]";
            }
            js += "}";
        }
        js += "])";
        Eval(js);
    }

    /** Every saved value to the page: the engine's, or `byId` (values just pushed through the host, which reach
     *  the engine only at its next block). */
    void SendValues(const float* byId = nullptr)
    {
        if(engine_ == nullptr && byId == nullptr)
            return;
        std::string js = "ammo.values([";
        bool        first = true;
        for(int id = 0; id < kNumIds; id++)
        {
            if(!IsSaved(id))
                continue;
            js += first ? "[" : ",[";
            first = false;
            Num(js, id, "%.0f");
            js += ",";
            Num(js, byId ? byId[id] : engine_->GetParam(id), "%.6g");
            js += "]";
        }
        js += "])";
        Eval(js);
    }

    /** A whole patch (a value per id): every host parameter through the host, so host and engine agree; the
     *  routes into the engine and the plugin's state. */
    void ApplyValues(const float* byId)
    {
        for(int i = 0; i < VisibleParamCount(); i++)
        {
            editParameter((uint32_t)i, true);
            setParameterValue((uint32_t)i, byId[VisibleId(i)]);
            editParameter((uint32_t)i, false);
        }
        if(engine_ != nullptr)
            for(int id = kRouteBase; id < kPoolEnd; id++)
                engine_->SetParam(id, byId[id]);
        setState("routes", Engine::RoutesText(byId).c_str());
        routesDirty_ = false;
        SendValues(byId);
    }

    /** The preset list, the current one (-1: a patch file) and the file's name. */
    void SendPresets()
    {
        std::string js = "ammo.presets([";
        for(int k = 0; k < kNumPresets; k++)
        {
            js += k ? ",{n:" : "{n:";
            Str(js, kPresets[k].name);
            js += ",b:";
            Str(js, kPresets[k].blurb);
            js += "}";
        }
        js += "],";
        Num(js, preset_, "%.0f");
        js += ",";
        Str(js, Utf8(file_).c_str());
        js += ")";
        Eval(js);
    }

    void Toast(const std::string& text)
    {
        std::string js = "ammo.toast(";
        Str(js, text.c_str());
        js += ")";
        Eval(js);
    }

    /** What the patch is called now: the file's name, or the preset's. */
    std::wstring PatchName() const
    {
        if(!file_.empty())
            return file_;
        if(preset_ >= 0 && preset_ < kNumPresets)
        {
            const char* n = kPresets[preset_].name;
            return std::wstring(n, n + std::strlen(n)); // ASCII
        }
        return L"Untitled";
    }

    HWND Owner() const
    {
        const HWND own  = reinterpret_cast<HWND>(getWindow().getNativeWindowHandle());
        const HWND root = own != nullptr ? GetAncestor(own, GA_ROOT) : nullptr;
        return root != nullptr ? root : own;
    }

    /* ---- patch files */
    void SavePatch()
    {
        if(engine_ == nullptr)
            return;
        const std::wstring path = PatchDialog(Owner(), true, PatchName());
        if(path.empty())
            return;
        std::string text = engine_->Save();
        if(!pageJson_.empty())
            text += "page " + pageJson_ + "\n";
        FILE*      f  = _wfopen(path.c_str(), L"wb");
        bool       ok = f != nullptr && std::fwrite(text.data(), 1, text.size(), f) == text.size();
        if(f != nullptr)
            ok &= std::fclose(f) == 0;
        if(!ok)
        {
            Toast("Could not write " + Utf8(path));
            return;
        }
        file_ = Stem(path);
        SendPresets();
        Toast("Saved " + Utf8(path));
        TestLog("patch saved: %s (%u bytes)", Utf8(path).c_str(), (unsigned)text.size());
    }

    void OpenPatch()
    {
        const std::wstring path = PatchDialog(Owner(), false, std::wstring());
        if(path.empty())
            return;
        std::string        text;
        std::vector<float> values(kNumIds);
        if(!ReadWholeFile(path, text) || !Engine::ParseState(text.data(), text.size(), values.data()))
        {
            Toast("Not an Ammonite patch: " + Utf8(path));
            return;
        }
        ApplyValues(values.data());
        const std::string page = PageLine(text);
        if(!page.empty())
        {
            std::string js = "ammo.page(";
            Str(js, page.c_str());
            js += ")";
            Eval(js);
        }
        preset_ = -1;
        file_   = Stem(path);
        SendPresets();
        Toast("Opened " + Utf8(path));
        TestLog("patch opened: %s", Utf8(path).c_str());
    }

    /* ---- recording */
    void StartRecording()
    {
        if(engine_ == nullptr || wav_.IsOpen())
            return;
        const std::wstring dir = AmmoniteFolder(L"Recordings");
        SYSTEMTIME         t;
        GetLocalTime(&t);
        wchar_t stamp[64];
        std::swprintf(stamp, 64, L"Ammonite %04u-%02u-%02u %02u-%02u-%02u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                      t.wSecond);
        recPath_ = dir + L"\\" + FileSafe(stamp + PatchName()) + L".wav";
        if(dir.empty() || !wav_.Open(_wfopen(recPath_.c_str(), L"wb"), (int)std::lround(engine_->SampleRate())))
        {
            Eval("ammo.rec({on:0})");
            Toast("Could not record to " + Utf8(recPath_));
            return;
        }
        recRate_ = engine_->SampleRate();
        engine_->recordTap().Start();
        Eval("ammo.rec({on:1})");
        TestLog("recording %s", Utf8(recPath_).c_str());
    }

    void DrainRecording()
    {
        if(engine_ != nullptr && wav_.IsOpen())
            DrainTap(engine_->recordTap(), wav_);
    }

    void StopRecording(bool tell)
    {
        if(engine_ == nullptr || !wav_.IsOpen())
            return;
        RecordTap& tap = engine_->recordTap();
        tap.Stop();
        DrainTap(tap, wav_);
        const double   secs    = (double)wav_.Frames() / (recRate_ > 0 ? recRate_ : 48000.0);
        const uint32_t dropped = tap.Dropped();
        const bool     ok      = wav_.Close();
        TestLog("recording stopped: %.2f s, %u frames dropped, %s", secs, dropped, ok ? "ok" : "write failed");
        if(!tell)
            return;
        std::string js = "ammo.rec({on:0,secs:";
        Num(js, secs, "%.2f");
        js += ",dropped:";
        Num(js, dropped, "%.0f");
        js += ",ok:";
        js += ok ? "1" : "0";
        js += ",path:";
        Str(js, Utf8(recPath_).c_str());
        js += "})";
        Eval(js);
    }

    /** Every frame published since the last push, in one call. */
    void Tick()
    {
        if(engine_ == nullptr)
            return;
        DrainRecording();
        AmmoFrame frames[6];
        uint32_t  n = 0;
        while(n < 6 && engine_->telemetry().read(frames[n]))
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
        js.reserve(4096 * n);
        js += "ammo.tick([";
        unsigned char bytes[AMMO_WAVE_LEN];
        for(uint32_t i = 0; i < n; i++)
        {
            const AmmoFrame& f = frames[i];
            js += i ? ",{s:" : "{s:";
            Num(js, f.seq, "%.0f");
            js += ",b:";
            Num(js, f.beat, "%.4f");
            js += ",t:";
            Num(js, f.bpm, "%.2f");
            js += f.playing ? ",p:1" : ",p:0";
            js += f.hostClock ? ",h:1" : ",h:0";
            js += ",cr:";
            Num(js, f.chordRootPc, "%.0f");
            js += ",cd:";
            Num(js, f.chordDeg, "%.0f");
            js += ",cq:";
            Num(js, f.chordQuality, "%.0f");
            js += ",pp:";
            Num(js, f.progPos, "%.0f");
            js += ",pl:";
            Num(js, f.progLen, "%.0f");
            js += ",km:";
            Num(js, f.keyMask, "%.0f");
            js += ",cm:";
            Num(js, f.chordMask, "%.0f");
            js += ",sr:";
            Num(js, f.sampleRate, "%.0f");
            js += ",bl:";
            Num(js, f.block, "%.0f");
            js += ",mr:";
            Num(js, f.masterRms);
            js += ",mp:";
            Num(js, f.masterPeak);
            js += ",gg:";
            Num(js, f.glueGr, "%.2f");
            js += ",lg:";
            Num(js, f.limitGr, "%.2f");
            // the modulation pool: the LFOs that ran ({index: value}), the live routes' target positions ({route: 0..1})
            js += ",lf:{";
            bool firstL = true;
            for(int l = 0; l < AMMO_LFOS; l++)
            {
                if(!(f.lfoRun >> l & 1u))
                    continue;
                js += firstL ? "\"" : ",\"";
                firstL = false;
                Num(js, l, "%.0f");
                js += "\":";
                Num(js, f.lfo[l], "%.3f");
            }
            js += "},rt:{";
            bool firstR = true;
            for(int r = 0; r < AMMO_ROUTES; r++)
            {
                if(f.route[r] < 0.f)
                    continue;
                js += firstR ? "\"" : ",\"";
                firstR = false;
                Num(js, r, "%.0f");
                js += "\":";
                Num(js, f.route[r], "%.4f");
            }
            js += "},cpu:";
            Num(js, f.cpu, "%.4f");
            js += ",ch:{";
            bool firstCh = true;
            for(int s = 0; s < AMMO_SLOTS; s++)
            {
                const AmmoChannelFrame& c = f.ch[s];
                if(!c.active && c.env <= 0.f)
                    continue;
                if(!firstCh)
                    js += ",";
                firstCh = false;
                js += "\"";
                Num(js, s, "%.0f");
                js += "\":{a:";
                js += c.active ? "1" : "0";
                if(c.muted)
                    js += ",m:1";
                js += ",n:";
                Num(js, c.note, "%.2f");
                js += ",pc:";
                Num(js, c.pitchClass, "%.0f");
                js += ",e:";
                Num(js, c.env, "%.4f");
                js += ",r:";
                Num(js, c.rms);
                js += ",k:";
                Num(js, c.peak);
                js += c.hit ? ",h:1" : ",h:0";
                js += ",sd:";
                Num(js, c.hitSeed, "%.0f");
                js += ",st:";
                Num(js, c.step, "%.0f");
                js += ",ln:";
                Num(js, c.length, "%.0f");
                if(c.active)
                {
                    // the pattern's notes, LENGTH of them (api.h: -1 a rest, -2 not known before it plays)
                    js += ",sn:[";
                    for(int k = 0; k < c.length && k < AMMO_STEPS; k++)
                    {
                        if(k)
                            js += ",";
                        Num(js, c.stepNote[k], "%.0f");
                    }
                    js += "]";
                }
                js += ",v:[";
                for(int k = 0; k < c.voices && k < AMMO_POLY; k++)
                {
                    if(k)
                        js += ",";
                    Num(js, c.voiceNote[k], "%.2f");
                }
                js += "]";
                if(c.duck > 0.f)
                {
                    js += ",dk:";
                    Num(js, c.duck, "%.3f");
                }
                float peak = 0.f;
                for(int k = 0; k < AMMO_WAVE_LEN; k++)
                    peak = std::fmax(peak, std::fabs(c.wave[k]));
                for(int k = 0; k < AMMO_WAVE_LEN; k++)
                {
                    const int q = peak > 0.f ? (int)std::lround(c.wave[k] / peak * 127.f) : 0;
                    bytes[k]    = (unsigned char)(q < 0 ? q + 256 : q);
                }
                js += ",ws:";
                Num(js, peak);
                js += ",w:\"";
                js += Base64(bytes, AMMO_WAVE_LEN);
                js += "\"}";
            }
            js += "}}";
        }
        js += "],{idleMs:";
        Num(js, id.meanMs, "%.2f");
        js += ",evalMs:";
        Num(js, lastEvalMs_, "%.3f");
        js += "})";
        Eval(js);
        pushes_.Mark();
        if(++ticks_ == 60 || ticks_ % 600 == 0)
        {
            const FrameStats::Summary p = pushes_.Last10s();
            TestLog("%u ticks; %.0f pushes/s, last eval %.3f ms for %u frames, js %u bytes", ticks_,
                    p.meanMs > 0 ? 1000.0 / p.meanMs : 0.0, lastEvalMs_, n, (unsigned)js.size());
        }
    }

    static void OnPace(void* arg) { static_cast<AmmoniteUI*>(arg)->Tick(); }

    Engine* const engine_;
    WebViewHandle web_ = nullptr;
    FrameStats    idle_, pushes_;
    Pacer         pacer_{&AmmoniteUI::OnPace, this};
    bool          paced_ = false, ready_ = false, firstTick_ = false;
    double        lastEvalMs_ = 0.0, createMs_ = 0.0;
    uint32_t      ticks_  = 0;
    int           preset_ = 1; // First light, the default patch; -1: a patch file
    std::wstring  file_;       // that file's name, when one is open (or was just saved)
    std::string   pageJson_;   // the page's state for the next save
    bool          wantSave_ = false, wantOpen_ = false, inDialog_ = false;
    bool          routesDirty_ = false; // the page set a route: the plugin's state follows at the next idle
    // our standalone host's output devices (ammo_host_outputs / ammo_host_output in its exe; null in a DAW)
    using HostOutputsFn = const char* (*)();
    using HostOutputFn  = int (*)(const char*);
    HostOutputsFn hostOutputs_ = reinterpret_cast<HostOutputsFn>(GetProcAddress(GetModuleHandleW(nullptr), "ammo_host_outputs"));
    HostOutputFn  hostOutput_  = reinterpret_cast<HostOutputFn>(GetProcAddress(GetModuleHandleW(nullptr), "ammo_host_output"));
    std::string   wantOutput_;
    bool          haveWantOutput_ = false;
    WavWriter     wav_;
    std::wstring  recPath_;
    double        recRate_ = 48000.0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmmoniteUI)
};

UI* createUI()
{
    return new AmmoniteUI();
}

END_NAMESPACE_DISTRHO
