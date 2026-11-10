/* In-game editor page "Gameplay": bike and rider values as a percentage of the
 * game's own. LGPL-2.1-or-later. Original author code; no game code or data.
 *
 * What the values change (found and checked in PROGRESS-0.15-gameplay-values.md):
 *  - Bike physics block, *(actor +0x22C): +0x3C..+0xAC is the engine curve
 *    (29 values; scaling it changes top speed), +0xE4 steering (handling).
 *    Player 1 has a copy of its bike's file; rivals and cops share blocks.
 *  - Rider record, *(actor +0x43C): +0x0D maximum health, +0x0F health,
 *    +0x24 / +0x25 bike condition maximum / current.
 * "You" is every human rider (rider index actor +0xAC < human count, race
 * context +0x30); "rivals" are all other riders, cops included.
 *
 * The tick runs while racing (race flag 1, not paused) and never online.
 * Blocks and maximum health are scaled from the game's value, which is read the
 * first time a block or record is seen and again whenever it no longer holds
 * the value written here (a new race loaded it afresh). So a value back at 100%
 * writes the game's value back, and changes show at once (health keeps its
 * share of the bar). Damage taken scales each loss of health / bike condition as it
 * happens; a loss to 0 (wrecked / bike totalled) is left to the game, and a
 * bigger loss never goes below 1 on its own. In Five-O (race mode 1) nothing
 * is changed unless "Also in Five-O" is on. Any value off 100% keeps
 * achievements locked (pc_services.cpp award). */
#include "editor_page.h"
#include "netplay_session.h"
#include "mod_plugins.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
extern "C" uint8_t* memory_get_ram_ptr(void);
namespace {
using rrpc::editor::Page;
enum {YOU_SPEED, YOU_HANDLING, YOU_HEALTH, YOU_DAMAGE, YOU_BIKE_DAMAGE, RIVAL_SPEED, RIVAL_HANDLING, RIVAL_HEALTH, N};
struct Setting {const char* key; const wchar_t* title; const char* log_title; int lo, hi; const wchar_t* help;};
const Setting settings[N] = {
    {"you_top_speed", L"Your top speed (engine power)", "Your top speed", 50, 200,
     L"Scales your bike's engine curve. 125% gave about 9% more top speed in the test; above 100% the launch is a little slower, below it a little quicker."},
    {"you_handling", L"Your handling (steering)", "Your handling", 50, 200,
     L"Scales how hard your bike steers. 150% turned about 30% tighter in the test, 50% about half as tight."},
    {"you_health", L"Your health", "Your health", 50, 250,
     L"Your maximum health at the start of a race (the game's is 90; at most 255). The bar shows the same share."},
    {"you_damage_taken", L"Damage you take", "Damage you take", 0, 300,
     L"Each loss of your health (hits, crashes) is scaled. 0% = no damage. A hit that wrecks you still wrecks you."},
    {"you_bike_damage", L"Damage your bike takes", "Damage your bike takes", 0, 300,
     L"Each loss of your bike's condition (a crash takes 1/16 when new) is scaled. 0% = the bike never wears."},
    {"rival_top_speed", L"Rivals' top speed (engine power)", "Rivals' top speed", 50, 200,
     L"Scales every rival's and cop's engine curve, like yours."},
    {"rival_handling", L"Rivals' handling (steering)", "Rivals' handling", 50, 200,
     L"Scales every rival's and cop's steering, like yours."},
    {"rival_health", L"Rivals' health (toughness)", "Rivals' health", 25, 250,
     L"Every rival's and cop's maximum health at the start of a race (theirs are 48 to 96; at most 255). Lower = easier to knock off."},
};
std::mutex gmu;
int saved[N], draft[N]; bool saved_five_o, draft_five_o;
unsigned serial_;
std::atomic<int> live[N]; std::atomic<bool> live_five_o{false}, live_any{false}, file_loaded{false};
// What the last race showed (for the page): the game's own values for Player 1 and the first rival.
std::atomic<int> seen_health{-1}, seen_rival_health{-1}, seen_curve{-1}, seen_steer{-1};

int clampi(int v, int lo, int hi) {return v < lo ? lo : v > hi ? hi : v;}
void reset_all(int* v) {for (int i = 0; i < N; i++) v[i] = 100;}
void publish_locked() {
    bool any = false;
    for (int i = 0; i < N; i++) {live[i] = draft[i]; any = any || draft[i] != 100;}
    live_five_o = draft_five_o; live_any = any; serial_++;
}

// ---- Page callbacks (file side) -----------------------------------------------
// "[gameplay]" lines: key = percent; also_in_five_o = 0/1. Missing keys are 100%.
void load_line(const std::string& s) {
    size_t eq = s.find('='); if (eq == std::string::npos) return;
    std::string k = s.substr(0, eq); while (!k.empty() && k.back() == ' ') k.pop_back();
    int v; if (std::sscanf(s.c_str() + eq + 1, "%d", &v) != 1) return;
    std::lock_guard<std::mutex> lock(gmu);
    if (k == "also_in_five_o") {saved_five_o = v != 0; return;}
    for (int i = 0; i < N; i++) if (k == settings[i].key) saved[i] = clampi(v, settings[i].lo, settings[i].hi);
}
struct Init {Init() {reset_all(saved); reset_all(draft); for (auto& l : live) l = 100;}} init_;
void loaded() {std::lock_guard<std::mutex> lock(gmu); for (int i = 0; i < N; i++) draft[i] = saved[i]; draft_five_o = saved_five_o; publish_locked(); file_loaded = true;}
int unsaved() {std::lock_guard<std::mutex> lock(gmu); int n = draft_five_o != saved_five_o; for (int i = 0; i < N; i++) n += draft[i] != saved[i]; return n;}
unsigned serial() {std::lock_guard<std::mutex> lock(gmu); return serial_;}
void write(FILE* f) {
    std::lock_guard<std::mutex> lock(gmu);
    std::fputs("\r\n[gameplay]\r\n; value = percent of the game's own (100 = unchanged; Gameplay page). Achievements stay locked while any differs.\r\n", f);
    for (int i = 0; i < N; i++) if (draft[i] != 100) std::fprintf(f, "%s = %d\r\n", settings[i].key, draft[i]);
    if (draft_five_o) std::fputs("also_in_five_o = 1\r\n", f);
}
void log(FILE* f, const char* stamp) {
    std::lock_guard<std::mutex> lock(gmu);
    for (int i = 0; i < N; i++) if (draft[i] != saved[i])
        std::fprintf(f, "%s  Gameplay > %s: %d%% -> %d%%%s\r\n", stamp, settings[i].log_title, saved[i], draft[i], draft[i] == 100 ? " (built-in)" : "");
    if (draft_five_o != saved_five_o) std::fprintf(f, "%s  Gameplay > Also in Five-O: %s -> %s\r\n", stamp, saved_five_o ? "on" : "off", draft_five_o ? "on" : "off");
}
void commit() {std::lock_guard<std::mutex> lock(gmu); for (int i = 0; i < N; i++) saved[i] = draft[i]; saved_five_o = draft_five_o; serial_++;}
void discard() {std::lock_guard<std::mutex> lock(gmu); for (int i = 0; i < N; i++) draft[i] = saved[i]; draft_five_o = saved_five_o; publish_locked();}

// ---- Game side (pc_tick) ----------------------------------------------------------
uint8_t* ram() {return memory_get_ram_ptr();}
bool in_ram(uint32_t a, uint32_t n) {return a >= 0x80010000u && a < 0x80200000u && n <= 0x80200000u - a;}
uint32_t word(uint32_t a) {uint32_t v; std::memcpy(&v, ram() + (a & 0x1ffffc), 4); return v;}
void store(uint32_t a, const void* v, uint32_t n) {
    if (std::memcmp(ram() + (a & 0x1fffff), v, n)) {psx_mod_host_write_ram(a, v, n); psx_mod_counter_add("rr.gameplay.writes", 1);}
}
void store8(uint32_t a, int v) {uint8_t b = (uint8_t)clampi(v, 0, 255); store(a, &b, 1);}
constexpr uint32_t kCurve = 0x3C, kSteer = 0xE4; constexpr int kCurveN = 29;
struct Block {int32_t orig[kCurveN + 1], wrote[kCurveN + 1];};
struct Record {int max_orig = -1, max_wrote = -1, last_health = -1, last_bike = -1;};
std::map<uint32_t, Block> blocks; std::map<uint32_t, Record> records;
void read_block(uint32_t ph, int32_t* v) {std::memcpy(v, ram() + (ph & 0x1fffff) + kCurve, 4 * kCurveN); std::memcpy(v + kCurveN, ram() + (ph & 0x1fffff) + kSteer, 4);}
void apply_block(uint32_t ph, int speed, int steer, bool you) {
    int32_t cur[kCurveN + 1]; read_block(ph, cur);
    auto it = blocks.find(ph);
    const bool fresh = it == blocks.end() || std::memcmp(cur, it->second.wrote, sizeof cur);
    if (fresh && speed == 100 && steer == 100) {blocks.erase(ph); return;}   // the game's own, nothing to do
    Block& b = blocks[ph]; if (fresh) std::memcpy(b.orig, cur, sizeof cur);
    for (int i = 0; i < kCurveN; i++) b.wrote[i] = (int32_t)((int64_t)b.orig[i] * speed / 100);
    b.wrote[kCurveN] = (int32_t)((int64_t)b.orig[kCurveN] * steer / 100);
    if (you) {int peak = 0; for (int i = 0; i < kCurveN; i++) if (b.orig[i] > peak) peak = b.orig[i]; seen_curve = peak; seen_steer = b.orig[kCurveN];}
    if (std::memcmp(cur, b.wrote, sizeof cur)) {store(ph + kCurve, b.wrote, 4 * kCurveN); store(ph + kSteer, &b.wrote[kCurveN], 4);}
}
// Maximum health from the game's value; health keeps its share of the bar.
void apply_health(uint32_t rec, Record& r, int pct) {
    const int mx = ram()[(rec + 0x0D) & 0x1fffff], hp = ram()[(rec + 0x0F) & 0x1fffff];
    if (r.max_wrote < 0 || mx != r.max_wrote) r.max_orig = mx;   // first sight, or a new race reset it
    if (r.max_orig <= 0) return;
    const int want = clampi(r.max_orig * pct / 100, 1, 255);
    if (mx != want) {store8(rec + 0x0D, want); if (hp > 0) store8(rec + 0x0F, clampi((hp * want + mx / 2) / mx, 1, 255));}
    r.max_wrote = want;
}
// Scale a loss since the last frame; leave a loss to 0 and any gain to the game.
void scale_loss(uint32_t at, int& last, int pct) {
    const int now = ram()[at & 0x1fffff];
    if (last >= 0 && now < last && now > 0 && pct != 100) {
        const int loss = last - now, want = clampi(last - (loss * pct + 50) / 100, 1, 255);
        if (want != now) store8(at, want);
        last = want; return;
    }
    last = now;
}
}

namespace rrpc {
bool gameplay_active() {
    if (!file_loaded.load(std::memory_order_relaxed)) editor::game_edits_ensure_loaded();
    if (live_any.load(std::memory_order_relaxed)) return true;
    std::lock_guard<std::mutex> lock(gmu); for (int i = 0; i < N; i++) if (saved[i] != 100) return true; return false;
}
void gameplay_tick(bool racing, bool paused) {
    if (!file_loaded.load(std::memory_order_relaxed)) editor::game_edits_ensure_loaded();
    // State is kept across pauses and races: a value that no longer matches what
    // was written here means the game loaded it afresh (see the top of the file).
    if (netplay() || !racing || paused) return;
    const uint32_t ctx = word(0x8005B2F8), base = word(0x8005B3A0);
    if (!in_ram(ctx, 0x40) || !in_ram(base, 0x448 * 24) || ram()[ctx & 0x1fffff] != 1) return;
    if ((ram()[(ctx + 4) & 0x1fffff] & ~2) == 1 && !live_five_o.load()) {   // race mode 1 (or 3) = Five-O, as pc_services.cpp race_results
        // Five-O: give the game's values back (a block of ours from before), change nothing else.
        for (auto& [ph, b] : blocks) {int32_t cur[kCurveN + 1]; read_block(ph, cur); if (!std::memcmp(cur, b.wrote, sizeof cur)) {store(ph + kCurve, b.orig, 4 * kCurveN); store(ph + kSteer, &b.orig[kCurveN], 4);}}
        blocks.clear(); records.clear(); return;
    }
    int v[N]; for (int i = 0; i < N; i++) v[i] = live[i].load(std::memory_order_relaxed);
    const uint32_t humans = word(ctx + 0x30);
    // Blocks: one shared block is "yours" if any human rides it.
    std::map<uint32_t, bool> ph_you;
    for (int k = 0; k < 24; k++) {
        const uint32_t a = base + k * 0x448, rec = word(a + 0x43C), ph = word(a + 0x22C);
        if (rec < 0x800D5700u || rec >= 0x800D6000u) continue;
        const bool you = (word(a + 0xAC) & 0xffff) < humans;
        if (in_ram(ph, 0x1C0)) ph_you[ph] = ph_you[ph] || you;
        Record& r = records[rec];
        if (you) {
            apply_health(rec, r, v[YOU_HEALTH]); if (k == 0) seen_health = r.max_orig;
            scale_loss(rec + 0x0F, r.last_health, v[YOU_DAMAGE]);
            scale_loss(rec + 0x25, r.last_bike, v[YOU_BIKE_DAMAGE]);
        } else {
            apply_health(rec, r, v[RIVAL_HEALTH]); if (k == 1) seen_rival_health = r.max_orig;
        }
    }
    for (auto& [ph, you] : ph_you)
        apply_block(ph, you ? v[YOU_SPEED] : v[RIVAL_SPEED], you ? v[YOU_HANDLING] : v[RIVAL_HANDLING], you);
    psx_mod_counter_add("rr.gameplay.race_frames", 1);
}
}

namespace {
// ---- Page window ----------------------------------------------------------------
enum {ID_HINT = 400, ID_LIST, ID_VALUE, ID_HELP, ID_SLIDER, ID_MINUS, ID_PLUS, ID_RESET_ONE, ID_RESET_ALL, ID_FIVE_O};
HWND page, list, value_label, help_label, slider; HFONT font_; unsigned shown = ~0u;
int dpi_() {HDC dc = GetDC(nullptr); int d = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96; if (dc) ReleaseDC(nullptr, dc); return d > 0 ? d : 96;}
HWND add(HWND p, const wchar_t* cls, const wchar_t* t, DWORD style, int id) {
    HWND h = CreateWindowExW(std::wcscmp(cls, L"LISTBOX") == 0 ? WS_EX_CLIENTEDGE : 0, cls, t, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, p, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font_, TRUE); return h;
}
int selected() {int s = (int)SendMessageW(list, LB_GETCURSEL, 0, 0); return s >= 0 && s < N ? s : 0;}
// The game's own value behind a setting, as last seen in a race (or a plain note).
std::wstring original(int i) {
    wchar_t b[96]; int g = -1;
    switch (i) {
    case YOU_HEALTH: g = seen_health; if (g >= 0) {std::swprintf(b, 96, L"game's value %d", g); return b;} return L"game's value 90";
    case RIVAL_HEALTH: g = seen_rival_health; if (g >= 0) {std::swprintf(b, 96, L"game's value %d (rider 2)", g); return b;} return L"game's values 48-96";
    case YOU_SPEED: g = seen_curve; if (g >= 0) {std::swprintf(b, 96, L"game's engine peak %d", g); return b;} break;
    case YOU_HANDLING: g = seen_steer; if (g >= 0) {std::swprintf(b, 96, L"game's steering %d", g); return b;} break;
    }
    return L"game's value = 100%";
}
void show_value(int sel, int v) {
    wchar_t t[200]; std::swprintf(t, 200, L"%ls:  %d%%   (100%% = %ls)", settings[sel].title, v, original(sel).c_str());
    SetWindowTextW(value_label, t);
}
void refresh(bool force) {
    unsigned s; int v[N]; bool five;
    {std::lock_guard<std::mutex> lock(gmu); s = serial_; for (int i = 0; i < N; i++) v[i] = draft[i]; five = draft_five_o;}
    if (!force && s == shown) return; shown = s;
    const int sel = selected();
    SendMessageW(list, WM_SETREDRAW, FALSE, 0); SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < N; i++) {
        wchar_t line[160]; std::swprintf(line, 160, L"%ls\t%d%%%ls", settings[i].title, v[i], v[i] == 100 ? L"  (built-in)" : L"");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageW(list, LB_SETCURSEL, sel, 0); SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE);
    show_value(sel, v[sel]); SetWindowTextW(help_label, settings[sel].help);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_POS | SIF_PAGE, settings[sel].lo, settings[sel].hi, 1, v[sel]}; SetScrollInfo(slider, SB_CTL, &si, TRUE);
    SendMessageW(GetDlgItem(page, ID_FIVE_O), BM_SETCHECK, five ? BST_CHECKED : BST_UNCHECKED, 0);
}
void set_value(int i, int v) {std::lock_guard<std::mutex> lock(gmu); draft[i] = clampi(v, settings[i].lo, settings[i].hi); publish_locked();}
int value(int i) {std::lock_guard<std::mutex> lock(gmu); return draft[i];}
void layout(HWND w) {
    RECT r; GetClientRect(w, &r); const int W = r.right, H = r.bottom; auto D = [](int v) {return MulDiv(v, dpi_(), 96);};
    MoveWindow(GetDlgItem(w, ID_HINT), 0, 0, W, D(40), TRUE);
    int y = D(44), lh = H - y - D(176); if (lh < D(80)) lh = D(80);
    MoveWindow(list, 0, y, W, lh, TRUE); y += lh + D(8);
    MoveWindow(value_label, 0, y, W, D(22), TRUE); y += D(24);
    MoveWindow(help_label, 0, y, W, D(40), TRUE); y += D(44);
    MoveWindow(GetDlgItem(w, ID_MINUS), 0, y, D(36), D(26), TRUE); MoveWindow(slider, D(42), y + D(3), W - D(126), D(20), TRUE);
    MoveWindow(GetDlgItem(w, ID_PLUS), W - D(78), y, D(36), D(26), TRUE); y += D(34);
    MoveWindow(GetDlgItem(w, ID_RESET_ONE), 0, y, D(130), D(28), TRUE); MoveWindow(GetDlgItem(w, ID_RESET_ALL), D(136), y, D(130), D(28), TRUE);
    MoveWindow(GetDlgItem(w, ID_FIVE_O), D(282), y, W - D(282), D(28), TRUE);
    int tabs[1] = {D(260)}; SendMessageW(list, LB_SETTABSTOPS, 1, (LPARAM)tabs);
}
LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        page = w;
        add(w, L"STATIC", L"Gameplay: percent of the game's own value (100% = unchanged). A race uses the change at once. "
                          L"Save keeps it. Achievements stay locked while any value is not 100%. Off online.", 0, ID_HINT);
        list = add(w, L"LISTBOX", L"", WS_VSCROLL | LBS_NOTIFY | LBS_USETABSTOPS | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, ID_LIST);
        value_label = add(w, L"STATIC", L"", 0, ID_VALUE); help_label = add(w, L"STATIC", L"", 0, ID_HELP);
        add(w, L"BUTTON", L"−", WS_TABSTOP, ID_MINUS); slider = add(w, L"SCROLLBAR", L"", SBS_HORZ | WS_TABSTOP, ID_SLIDER); add(w, L"BUTTON", L"+", WS_TABSTOP, ID_PLUS);
        add(w, L"BUTTON", L"Reset this value", WS_TABSTOP, ID_RESET_ONE); add(w, L"BUTTON", L"Reset all", WS_TABSTOP, ID_RESET_ALL);
        add(w, L"BUTTON", L"Also in Five-O (when you play the cop)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_FIVE_O);
        layout(w); SendMessageW(list, LB_SETCURSEL, 0, 0); refresh(true); SetTimer(w, 1, 500, nullptr); return 0;
    case WM_SIZE: layout(w); return 0;
    case WM_TIMER: refresh(false); {const int i = selected(); show_value(i, value(i));} return 0;   // the label also shows the race's game values
    case WM_HSCROLL: if ((HWND)lp == slider) {
            const int i = selected(); int v = value(i);
            switch (LOWORD(wp)) {
            case SB_LINELEFT: v -= 1; break; case SB_LINERIGHT: v += 1; break;
            case SB_PAGELEFT: v -= 10; break; case SB_PAGERIGHT: v += 10; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: v = (short)HIWORD(wp); break;
            case SB_LEFT: v = settings[i].lo; break; case SB_RIGHT: v = settings[i].hi; break;
            default: return 0;
            }
            set_value(i, v); refresh(true); return 0;}
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_LIST: if (HIWORD(wp) == LBN_SELCHANGE) refresh(true); return 0;
        case ID_MINUS: case ID_PLUS: {const int i = selected(); set_value(i, (value(i) / 5 + (LOWORD(wp) == ID_PLUS ? 1 : (value(i) % 5 ? 0 : -1))) * 5); refresh(true); return 0;}
        case ID_RESET_ONE: set_value(selected(), 100); refresh(true); return 0;
        case ID_RESET_ALL: {std::lock_guard<std::mutex> lock(gmu); reset_all(draft); draft_five_o = false; publish_locked();} refresh(true); return 0;
        case ID_FIVE_O: {std::lock_guard<std::mutex> lock(gmu); draft_five_o = SendMessageW(GetDlgItem(w, ID_FIVE_O), BM_GETCHECK, 0, 0) == BST_CHECKED; publish_locked();} return 0;
        }
        break;
    case WM_DESTROY: KillTimer(w, 1); page = nullptr; return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
HWND create(HWND parent, HFONT font) {
    font_ = font; shown = ~0u;
    static bool reg; if (!reg) {WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"JailbreakEditorGameplay";
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;}
    return CreateWindowExW(WS_EX_CONTROLPARENT, L"JailbreakEditorGameplay", L"", WS_CHILD, 0, 0, 10, 10, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}
const Page page_def = {L"Gameplay", "[gameplay]", create, load_line, loaded, unsaved, serial, write, log, commit, discard};
}
const rrpc::editor::Page& rrpc::editor::gameplay_page() {return page_def;}
