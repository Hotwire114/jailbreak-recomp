/* In-game editor page "Race HUD": move and resize the race HUD widgets.
 * LGPL-2.1-or-later. Original author code; no game code or data.
 *
 * Six widgets, told apart by where the game draws them (gpu_geometry.py
 * HUD_EDIT): the speed/distance box (top left), the position/gap box (top
 * right), the minimap (inside its clip), your bars and name (bottom left), the
 * opponent's bars and name (bottom right) and the centre (countdown, messages,
 * Time Trial clock). Each has an X/Y offset in screen pixels of the game's
 * 240-line picture and a size in percent, about the widget's corner at its
 * screen edge (the centre group about its middle). The draft shows in the race
 * at once; Save keeps it in game-edits.ini [race hud] for every race. In split
 * screen each view's HUD takes the same edit. Off online. */
#include "editor_page.h"
#include "netplay_session.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
namespace {
using rrpc::editor::Page;
constexpr int N = 6;
struct Widget {const char* key; const wchar_t* title; const char* log_title;};
const Widget widgets[N] = {
    {"speed",    L"Speed and distance box (top left)",          "Speed and distance box"},
    {"position", L"Position and gap box (top right)",           "Position and gap box"},
    {"map",      L"Minimap (top right)",                        "Minimap"},
    {"you",      L"Your health, bike damage and name (bottom left)",   "Your bars and name"},
    {"opponent", L"Opponent's health, bike damage and name (bottom right)", "Opponent's bars and name"},
    {"centre",   L"Centre: countdown, messages, Time Trial clock", "Centre messages"},
};
constexpr int kMinX = -240, kMaxX = 240, kMinY = -160, kMaxY = 160, kMinSize = 40, kMaxSize = 300;
struct Value {int x = 0, y = 0, size = 100;
    bool plain() const {return x == 0 && y == 0 && size == 100;}
    bool operator!=(const Value& o) const {return x != o.x || y != o.y || size != o.size;}};
std::mutex hmu;
Value saved[N], draft[N];
unsigned serial_;
// What the GPU thread reads (no lock): the draft.
std::atomic<int> live_x[N], live_y[N], live_size[N] = {100, 100, 100, 100, 100, 100};
std::atomic<bool> live_any{false}, paused{false};
std::atomic<bool> file_loaded{false};

int clampi(int v, int lo, int hi) {return v < lo ? lo : v > hi ? hi : v;}
void publish_locked() {
    bool any = false;
    for (int i = 0; i < N; i++) {
        live_x[i] = draft[i].x; live_y[i] = draft[i].y; live_size[i] = draft[i].size;
        any = any || !draft[i].plain();
    }
    live_any = any; serial_++;
}
std::string text(const Value& v) {
    char b[64]; std::snprintf(b, sizeof b, "x %+d, y %+d, size %d%%", v.x, v.y, v.size); return b;
}

// ---- Page callbacks (file side) -----------------------------------------------
// "[race hud]" lines: key = x y size   (x, y in pixels of the 240-line picture; size in %)
void load_line(const std::string& s) {
    size_t eq = s.find('='); if (eq == std::string::npos) return;
    std::string k = s.substr(0, eq); while (!k.empty() && k.back() == ' ') k.pop_back();
    int x, y, size;
    if (std::sscanf(s.c_str() + eq + 1, "%d %d %d", &x, &y, &size) != 3) return;
    std::lock_guard<std::mutex> lock(hmu);
    for (int i = 0; i < N; i++) if (k == widgets[i].key)
        saved[i] = {clampi(x, kMinX, kMaxX), clampi(y, kMinY, kMaxY), clampi(size, kMinSize, kMaxSize)};
}
void loaded() {std::lock_guard<std::mutex> lock(hmu); for (int i = 0; i < N; i++) draft[i] = saved[i]; publish_locked(); file_loaded = true;}
int unsaved() {std::lock_guard<std::mutex> lock(hmu); int n = 0; for (int i = 0; i < N; i++) n += draft[i] != saved[i]; return n;}
unsigned serial() {std::lock_guard<std::mutex> lock(hmu); return serial_;}
void write(FILE* f) {
    std::lock_guard<std::mutex> lock(hmu);
    std::fputs("\r\n[race hud]\r\n; widget = x y size  (pixels of the 240-line picture, size in %; Race HUD page)\r\n", f);
    for (int i = 0; i < N; i++) if (!draft[i].plain())
        std::fprintf(f, "%s = %d %d %d\r\n", widgets[i].key, draft[i].x, draft[i].y, draft[i].size);
}
void log(FILE* f, const char* stamp) {
    std::lock_guard<std::mutex> lock(hmu);
    for (int i = 0; i < N; i++) if (draft[i] != saved[i])
        std::fprintf(f, "%s  Race HUD > %s: %s -> %s%s\r\n", stamp, widgets[i].log_title, text(saved[i]).c_str(),
                     text(draft[i]).c_str(), draft[i].plain() ? " (built-in)" : "");
}
void commit() {std::lock_guard<std::mutex> lock(hmu); for (int i = 0; i < N; i++) saved[i] = draft[i]; serial_++;}
void discard() {std::lock_guard<std::mutex> lock(hmu); for (int i = 0; i < N; i++) draft[i] = saved[i]; publish_locked();}

// ---- Page window ----------------------------------------------------------------
enum {ID_HINT = 300, ID_LIST, ID_LEFT, ID_RIGHT, ID_UP, ID_DOWN, ID_SMALLER, ID_BIGGER, ID_BIG_STEPS,
      ID_RESET_ONE, ID_RESET_ALL, ID_VALUE, ID_LBL_MOVE, ID_LBL_SIZE};
HWND page, list, value_label; HFONT font_; unsigned shown = ~0u;
int dpi_() {HDC dc = GetDC(nullptr); int d = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96; if (dc) ReleaseDC(nullptr, dc); return d > 0 ? d : 96;}
std::wstring wide(const std::string& s) {std::wstring w; for (char c : s) w += (wchar_t)(unsigned char)c; return w;}
HWND add(HWND p, const wchar_t* cls, const wchar_t* t, DWORD style, int id) {
    HWND h = CreateWindowExW(std::wcscmp(cls, L"LISTBOX") == 0 ? WS_EX_CLIENTEDGE : 0, cls, t, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, p, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font_, TRUE); return h;
}
void refresh(bool force) {
    unsigned s; Value v[N];
    {std::lock_guard<std::mutex> lock(hmu); s = serial_; for (int i = 0; i < N; i++) v[i] = draft[i];}
    if (!force && s == shown) return; shown = s;
    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0); if (sel < 0) sel = 0;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0); SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < N; i++) {
        std::wstring line = std::wstring(widgets[i].title) + L"\t" + (v[i].plain() ? L"(built-in)" : wide(text(v[i])));
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    SendMessageW(list, LB_SETCURSEL, sel, 0); SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE);
    SetWindowTextW(value_label, (std::wstring(widgets[sel].title) + L":  " + wide(text(v[sel]))).c_str());
}
void change(int id) {
    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0); if (sel < 0 || sel >= N) sel = 0;
    const bool big = SendMessageW(GetDlgItem(page, ID_BIG_STEPS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int step = big ? 5 : 1, sstep = big ? 10 : 2;
    std::lock_guard<std::mutex> lock(hmu);
    Value& v = draft[sel];
    switch (id) {
    case ID_LEFT: v.x = clampi(v.x - step, kMinX, kMaxX); break;
    case ID_RIGHT: v.x = clampi(v.x + step, kMinX, kMaxX); break;
    case ID_UP: v.y = clampi(v.y - step, kMinY, kMaxY); break;
    case ID_DOWN: v.y = clampi(v.y + step, kMinY, kMaxY); break;
    case ID_SMALLER: v.size = clampi(v.size - sstep, kMinSize, kMaxSize); break;
    case ID_BIGGER: v.size = clampi(v.size + sstep, kMinSize, kMaxSize); break;
    case ID_RESET_ONE: v = Value{}; break;
    case ID_RESET_ALL: for (auto& d : draft) d = Value{}; break;
    }
    publish_locked();
}
void layout(HWND w) {
    RECT r; GetClientRect(w, &r); const int W = r.right, H = r.bottom; auto D = [](int v) {return MulDiv(v, dpi_(), 96);};
    MoveWindow(GetDlgItem(w, ID_HINT), 0, 0, W, D(40), TRUE);
    int y = D(44), lh = H - y - D(118); if (lh < D(60)) lh = D(60);
    MoveWindow(list, 0, y, W, lh, TRUE); y += lh + D(8);
    MoveWindow(value_label, 0, y, W, D(22), TRUE); y += D(28);
    int x = 0; auto put = [&](int id, int wd) {MoveWindow(GetDlgItem(w, id), x, y, D(wd), D(30), TRUE); x += D(wd) + D(6);};
    put(ID_LBL_MOVE, 44); put(ID_LEFT, 40); put(ID_RIGHT, 40); put(ID_UP, 40); put(ID_DOWN, 40); x += D(14);
    put(ID_LBL_SIZE, 34); put(ID_SMALLER, 40); put(ID_BIGGER, 40); x += D(14); put(ID_BIG_STEPS, 100);
    y += D(38); x = 0; put(ID_RESET_ONE, 140); put(ID_RESET_ALL, 170);
    int tabs[1] = {D(250)}; SendMessageW(list, LB_SETTABSTOPS, 1, (LPARAM)tabs);
}
LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        page = w;
        add(w, L"STATIC", L"Race HUD: pick a widget, then move it or change its size. A race shows the change at once "
                          L"(open a race, F2 works while it runs). Save keeps it for every race; Reset gives the built-in layout back.", 0, ID_HINT);
        list = add(w, L"LISTBOX", L"", WS_VSCROLL | LBS_NOTIFY | LBS_USETABSTOPS | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, ID_LIST);
        value_label = add(w, L"STATIC", L"", 0, ID_VALUE);
        add(w, L"STATIC", L"Move", SS_CENTERIMAGE, ID_LBL_MOVE);
        add(w, L"BUTTON", L"◀", WS_TABSTOP, ID_LEFT); add(w, L"BUTTON", L"▶", WS_TABSTOP, ID_RIGHT);
        add(w, L"BUTTON", L"▲", WS_TABSTOP, ID_UP); add(w, L"BUTTON", L"▼", WS_TABSTOP, ID_DOWN);
        add(w, L"STATIC", L"Size", SS_CENTERIMAGE, ID_LBL_SIZE);
        add(w, L"BUTTON", L"−", WS_TABSTOP, ID_SMALLER); add(w, L"BUTTON", L"+", WS_TABSTOP, ID_BIGGER);
        add(w, L"BUTTON", L"Big steps", BS_AUTOCHECKBOX | WS_TABSTOP, ID_BIG_STEPS);
        add(w, L"BUTTON", L"Reset this widget", WS_TABSTOP, ID_RESET_ONE); add(w, L"BUTTON", L"Reset whole HUD", WS_TABSTOP, ID_RESET_ALL);
        layout(w); SendMessageW(list, LB_SETCURSEL, 0, 0); refresh(true); SetTimer(w, 1, 300, nullptr); return 0;
    case WM_SIZE: layout(w); return 0;
    case WM_TIMER: refresh(false); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_LIST) {if (HIWORD(wp) == LBN_SELCHANGE) refresh(true); return 0;}
        if (LOWORD(wp) >= ID_LEFT && LOWORD(wp) <= ID_RESET_ALL && LOWORD(wp) != ID_BIG_STEPS) {change(LOWORD(wp)); refresh(true); return 0;}
        break;
    case WM_DESTROY: KillTimer(w, 1); page = nullptr; return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
HWND create(HWND parent, HFONT font) {
    font_ = font; shown = ~0u;
    static bool reg; if (!reg) {WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"JailbreakEditorRaceHud";
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;}
    return CreateWindowExW(WS_EX_CONTROLPARENT, L"JailbreakEditorRaceHud", L"", WS_CHILD, 0, 0, 10, 10, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}
const Page page_def = {L"Race HUD", "[race hud]", create, load_line, loaded, unsaved, serial, write, log, commit, discard};
}

namespace rrpc {
const editor::Page& editor::race_hud_page() {return page_def;}
void hud_edit_scene(bool is_paused) {paused = is_paused;}
}

// GPU side (gpu_geometry.py HUD_EDIT). Groups: 0 speed, 1 position, 2 map, 3 you, 4 opponent, 5 centre.
extern "C" int rr_hud_edit_live(void) {
    if (!file_loaded.load(std::memory_order_relaxed)) rrpc::editor::game_edits_ensure_loaded();
    return live_any.load(std::memory_order_relaxed) && !rrpc::netplay();
}
extern "C" int rr_hud_edit_get(int g, double* dx, double* dy, double* scale) {
    if (g < 0 || g >= N) return 0;
    const int x = live_x[g].load(std::memory_order_relaxed), y = live_y[g].load(std::memory_order_relaxed),
              s = live_size[g].load(std::memory_order_relaxed);
    if (x == 0 && y == 0 && s == 100) return 0;
    *dx = x; *dy = y; *scale = s / 100.0; return 1;
}
extern "C" int rr_hud_edit_paused(void) {return paused.load(std::memory_order_relaxed);}
