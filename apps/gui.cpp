// speak_robotalker.exe -- Accessible Win32 GUI for RoboTalker.
//
// Design priorities (in order):
//   1. Screen-reader compatibility. Uses standard Win32 controls
//      (EDIT, COMBOBOX, BUTTON, STATIC). MSAA/UIA see these as their
//      proper roles (text, combo, button, label) without extra work.
//      Tab order is logical, mnemonics on every label.
//   2. Keyboard-first. Every action is reachable via Alt+key or Tab.
//   3. No external dependencies beyond Win32 + winmm + comctl32.
//
// Layout:
//   [Voice:  combo ]   [Language: combo ]   [Speak (Alt+S)]
//   [Text to speak (Alt+T):                                 ]
//   [             multi-line edit                           ]
//   [             ...                                       ]
//   [Status: ...                                            ]
//
// The Speak button synthesizes the EDIT contents and plays the audio
// via waveOut. F0 / sample rate are kept at defaults. Status line
// announces progress so screen readers report it via WM_NCALCSIZE
// updates on the STATIC control (live-region equivalent).

#define WIN32_LEAN_AND_MEAN
// _UNICODE / UNICODE come from -municode at compile time.
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#include "klattalker/synth.hpp"

#include <string>
#include <thread>
#include <vector>
#include <atomic>

// Control IDs (used both as IDs and tab order).
constexpr int IDC_VOICE_LABEL = 1001;
constexpr int IDC_VOICE_COMBO = 1002;
constexpr int IDC_LANG_LABEL  = 1003;
constexpr int IDC_LANG_COMBO  = 1004;
constexpr int IDC_SPEAK_BTN   = 1005;
constexpr int IDC_STOP_BTN    = 1006;
constexpr int IDC_TEXT_LABEL  = 1007;
constexpr int IDC_TEXT_EDIT   = 1008;
constexpr int IDC_STATUS      = 1009;

struct Controls {
    HWND root = nullptr;
    HWND voice = nullptr;
    HWND lang = nullptr;
    HWND speak = nullptr;
    HWND stop = nullptr;
    HWND text = nullptr;
    HWND status = nullptr;
};
Controls g;

std::atomic<bool> g_speaking{false};
HWAVEOUT g_waveout = nullptr;
std::vector<int16_t> g_audio_i16;
WAVEHDR g_hdr{};

// ---- helpers --------------------------------------------------------------

std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

void set_status(const wchar_t* text) {
    if (g.status) {
        SetWindowTextW(g.status, text);
        // Screen readers monitoring this control are notified by the
        // SetWindowText path -- no extra calls needed.
    }
}

// ---- voice selection ------------------------------------------------------

struct VoiceEntry { const wchar_t* label; const char* id; };
const VoiceEntry kVoices[] = {
    {L"Dan -- default male",     "dan"},
    {L"Lily -- default female",  "lily"},
    {L"John -- deep male",       "john"},
    {L"Kate -- breathy female",  "kate"},
    {L"Josh -- child",           "josh"},
    {L"Frank -- elderly male",   "frank"},
    {L"Doris -- elderly female", "doris"},
};

struct LangEntry { const wchar_t* label; const char* id;
                   klattalker::Variant v; };
const LangEntry kLangs[] = {
    {L"US English", "us",    klattalker::Variant::USEnglish},
    {L"UK English", "uk",    klattalker::Variant::UKEnglish},
    {L"German",     "de",    klattalker::Variant::German},
    {L"Polish",     "pl",    klattalker::Variant::Polish},
    {L"Italian",    "it",    klattalker::Variant::Italian},
    {L"French",     "fr",    klattalker::Variant::French},
    {L"Mandarin",   "zh",    klattalker::Variant::Mandarin},
    {L"Japanese",   "ja",    klattalker::Variant::Japanese},
};

// ---- synthesis + playback --------------------------------------------------

klattalker::KlattSynth build_synth(int voice_idx, int lang_idx) {
    using namespace klattalker;
    const std::string voice = (voice_idx >= 0 && voice_idx < (int)std::size(kVoices))
                              ? kVoices[voice_idx].id : "dan";
    const Variant variant = (lang_idx >= 0 && lang_idx < (int)std::size(kLangs))
                            ? kLangs[lang_idx].v : Variant::USEnglish;
    constexpr int sr = 22050;
    constexpr auto eng = EngineKind::Klsyn88;
    constexpr auto inf = InflectionKind::Fujisaki;
    if (voice == "john")  return KlattSynth::make_john(eng, sr, inf, variant);
    if (voice == "kate")  return KlattSynth::make_kate(eng, sr, inf, variant);
    if (voice == "josh")  return KlattSynth::make_josh(eng, sr, inf, variant);
    if (voice == "frank") return KlattSynth::make_frank(eng, sr, inf, variant);
    if (voice == "doris") return KlattSynth::make_doris(eng, sr, inf, variant);
    if (voice == "lily")  return KlattSynth::make_female(200.0, eng, sr, inf, variant);
    return KlattSynth::make_male(90.0, eng, sr, inf, variant);
}

void stop_playback() {
    if (g_waveout) {
        waveOutReset(g_waveout);
        if (g_hdr.dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(g_waveout, &g_hdr, sizeof(g_hdr));
        }
        waveOutClose(g_waveout);
        g_waveout = nullptr;
    }
    g_speaking = false;
    g_audio_i16.clear();
    EnableWindow(g.speak, TRUE);
    EnableWindow(g.stop, FALSE);
    set_status(L"Ready.");
}

void play_pcm(const std::vector<float>& samples, int sample_rate) {
    g_audio_i16.resize(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float v = samples[i];
        if (v > 1.f) v = 1.f; else if (v < -1.f) v = -1.f;
        g_audio_i16[i] = static_cast<int16_t>(v * 32767);
    }
    WAVEFORMATEX wf{};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = static_cast<DWORD>(sample_rate);
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 2;
    wf.nAvgBytesPerSec = static_cast<DWORD>(sample_rate * 2);
    waveOutOpen(&g_waveout, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    g_hdr = {};
    g_hdr.lpData         = reinterpret_cast<LPSTR>(g_audio_i16.data());
    g_hdr.dwBufferLength = static_cast<DWORD>(g_audio_i16.size() * sizeof(int16_t));
    waveOutPrepareHeader(g_waveout, &g_hdr, sizeof(g_hdr));
    waveOutWrite(g_waveout, &g_hdr, sizeof(g_hdr));
}

void do_speak() {
    if (g_speaking) return;

    // Read user input.
    int len = GetWindowTextLengthW(g.text);
    if (len == 0) { set_status(L"Type some text to speak."); return; }
    std::wstring wtext(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(g.text, wtext.data(), len + 1);
    wtext.resize(static_cast<size_t>(len));
    const std::string text = to_utf8(wtext);

    int voice_idx = static_cast<int>(SendMessageW(g.voice, CB_GETCURSEL, 0, 0));
    int lang_idx  = static_cast<int>(SendMessageW(g.lang,  CB_GETCURSEL, 0, 0));

    g_speaking = true;
    EnableWindow(g.speak, FALSE);
    EnableWindow(g.stop, TRUE);
    set_status(L"Synthesizing...");

    std::thread([text, voice_idx, lang_idx]() {
        try {
            auto synth = build_synth(voice_idx, lang_idx);
            auto audio = synth.synthesize_text(text);
            // Switch back to UI thread to start playback.
            PostMessageW(g.root, WM_APP + 1, 0, reinterpret_cast<LPARAM>(
                new std::pair<std::vector<float>, int>(std::move(audio),
                                                       synth.sample_rate())));
        } catch (const std::exception& e) {
            std::string msg = "Synthesis failed: ";
            msg += e.what();
            auto* wmsg = new std::wstring(from_utf8(msg));
            PostMessageW(g.root, WM_APP + 2, 0, reinterpret_cast<LPARAM>(wmsg));
        }
    }).detach();
}

// ---- window procedure -----------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDC_SPEAK_BTN && code == BN_CLICKED) {
                do_speak();
                return 0;
            }
            if (id == IDC_STOP_BTN && code == BN_CLICKED) {
                stop_playback();
                return 0;
            }
            break;
        }
        case WM_APP + 1: {
            // Synthesis ready -- play it.
            auto* p = reinterpret_cast<std::pair<std::vector<float>, int>*>(lp);
            set_status(L"Speaking.");
            play_pcm(p->first, p->second);
            delete p;
            // Auto-stop after a generous delay (PCM playback completes
            // on its own; we just reset state).
            std::thread([secs = (int)(g_audio_i16.size() / 22050) + 1]() {
                Sleep(static_cast<DWORD>(secs * 1000));
                PostMessageW(g.root, WM_APP + 3, 0, 0);
            }).detach();
            return 0;
        }
        case WM_APP + 2: {
            auto* p = reinterpret_cast<std::wstring*>(lp);
            set_status(p->c_str());
            delete p;
            g_speaking = false;
            EnableWindow(g.speak, TRUE);
            EnableWindow(g.stop, FALSE);
            return 0;
        }
        case WM_APP + 3:
            stop_playback();
            return 0;
        case WM_CLOSE:
            stop_playback();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Subclass the text edit so:
//   * Tab / Shift+Tab move focus OUT of the multiline edit (to the
//     next / previous control in tab order) rather than inserting a
//     tab character. The WM_GETDLGCODE+IsDialogMessage approach was
//     unreliable for multi-line EDIT; intercepting WM_KEYDOWN directly
//     is bulletproof.
//   * Ctrl+Enter triggers Speak (common power-user shortcut).
WNDPROC g_orig_edit_proc = nullptr;
LRESULT CALLBACK EditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_TAB) {
        const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        HWND parent = GetParent(hwnd);
        HWND next = GetNextDlgTabItem(parent, hwnd, shift);
        if (next && next != hwnd) {
            SetFocus(next);
            // Eat the key so the EDIT doesn't also insert '\t'.
            return 0;
        }
    }
    if (msg == WM_CHAR && wp == VK_TAB) {
        // Suppress the character that would otherwise sneak in from
        // the TranslateMessage path.
        return 0;
    }
    if (msg == WM_GETDLGCODE) {
        LRESULT r = CallWindowProcW(g_orig_edit_proc, hwnd, msg, wp, lp);
        return r & ~DLGC_WANTTAB;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN
        && (GetKeyState(VK_CONTROL) & 0x8000)) {
        do_speak();
        return 0;
    }
    return CallWindowProcW(g_orig_edit_proc, hwnd, msg, wp, lp);
}

// ---- entry ----------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SpeakRoboTalker";
    RegisterClassW(&wc);

    g.root = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"SpeakRoboTalker", L"Speak RoboTalker",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
        nullptr, nullptr, hInst, nullptr);

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    auto add_control = [&](const wchar_t* cls, const wchar_t* text,
                           DWORD style, int x, int y, int w, int h,
                           int id) -> HWND {
        HWND h_ = CreateWindowExW(0, cls, text,
            WS_CHILD | WS_VISIBLE | style,
            x, y, w, h, g.root, reinterpret_cast<HMENU>(id),
            hInst, nullptr);
        SendMessageW(h_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h_;
    };

    // Row 1: Voice combo, Language combo, Speak / Stop buttons
    add_control(L"STATIC", L"&Voice:", SS_LEFT,
                10, 14, 50, 20, IDC_VOICE_LABEL);
    g.voice = add_control(L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
        60, 10, 200, 240, IDC_VOICE_COMBO);
    for (auto& v : kVoices)
        SendMessageW(g.voice, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(v.label));
    SendMessageW(g.voice, CB_SETCURSEL, 0, 0);

    add_control(L"STATIC", L"&Language:", SS_LEFT,
                275, 14, 70, 20, IDC_LANG_LABEL);
    g.lang = add_control(L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
        350, 10, 160, 240, IDC_LANG_COMBO);
    for (auto& v : kLangs)
        SendMessageW(g.lang, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(v.label));
    SendMessageW(g.lang, CB_SETCURSEL, 0, 0);

    g.speak = add_control(L"BUTTON", L"&Speak",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        525, 8, 80, 28, IDC_SPEAK_BTN);
    g.stop = add_control(L"BUTTON", L"S&top",
        BS_PUSHBUTTON | WS_TABSTOP,
        610, 8, 80, 28, IDC_STOP_BTN);
    EnableWindow(g.stop, FALSE);

    // Row 2: Text label + multi-line edit
    add_control(L"STATIC", L"&Text to speak:", SS_LEFT,
                10, 50, 200, 20, IDC_TEXT_LABEL);
    g.text = add_control(L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL
        | WS_TABSTOP | WS_BORDER,
        10, 75, 680, 320, IDC_TEXT_EDIT);

    // Bottom: status bar (acts as live region)
    g.status = add_control(L"STATIC", L"Ready. Type text and press Speak (Alt+S).",
        SS_LEFT | SS_NOTIFY,
        10, 405, 680, 30, IDC_STATUS);

    // Subclass the edit to catch Ctrl+Enter
    g_orig_edit_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g.text, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EditSubclass)));

    SetFocus(g.text);

    ShowWindow(g.root, nShow);
    UpdateWindow(g.root);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g.root, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
