/* PC controls for the original CONTROLLERS screen. LGPL-2.1-or-later.
 * Independently authored. No game code, text or artwork is included: the
 * button table is read from the running game, the drawn key caps and pad
 * buttons are our own shapes, the lettering is Roboto Flex (hd_font_data.h).
 *
 * Analog control. The game has its own analog configurations: with a DualShock
 * in analog mode (pad id 0x73) its Configuration row offers Analog 1 / Analog 2
 * (left stick steers, right stick is a proportional throttle / brake); with a
 * digital pad (0x41) it offers Digital 1-3. When the pad type changes, the game
 * itself resets the player's configuration to Analog 1 / Digital 1. A real
 * DualShock switches between the two with its ANALOG button.
 *  - A controller seat (game.toml pN_device "gamepad", pN_mode "analog", written
 *    by the launcher) is presented through the framework's game-owned
 *    presentation policy (psx_mod_set_controller_presentation_policy): a
 *    DualShock whose ANALOG button is ours. Keyboard seats keep the plain
 *    digital pad the launcher configures.
 *  - The ANALOG button is pressed from the game's own Configuration row: Right
 *    past the last configuration of one kind, or Left past the first, switches
 *    the pad's mode, and the game then lists the other kind. Right runs Analog 1,
 *    Analog 2, Digital 1, 2, 3, Analog 1; Left the other way.
 *  - The game resets the configuration whenever it re-initialises the pads,
 *    which it does on every memory card check (Load, Save, Jail Break). The
 *    choice made on the Controllers screen is remembered in launcher.ini
 *    [controls] pN_pad_config / pN_key_config (the game's index, 0-4) and put
 *    back while the menus are up. A race takes the configuration from the menu
 *    when it starts (0x8007F37C), so races always get the player's choice.
 *    The screen's own rules decide what that choice is: Cross ("Accept
 *    Selection") keeps it, Triangle backs out and the game reverts it; what
 *    the game holds when the player leaves is what is remembered.
 * Game state (SLUS-01053 with its frontend overlay):
 *  - per player settings 0x800D81D8 + 36n, +8 configuration (0-2 digital, 3-4
 *    analog); controller records 0x800D6DE0 + 0xC0n, +0xC pad id the game saw;
 *  - button table 0x800526A4 + 60 * configuration: 15 words, one per action,
 *    each an index into the game's button list (0-14: Left, Right, Up, Down,
 *    Square, Circle, Triangle, Cross, L1, L2, R1, R2, Start, Select, L3; 15-18:
 *    right stick up / down, left stick up / down);
 *  - the CONTROLLERS menu is record 0x8009944C (current menu 0x8009C5C8, page
 *    0x8009C5B8 = 1, depth 0x8009C5BC = 3), its selected row at +4 (2
 *    Controller, 3 Configuration, 4 Vibration); 0x8009C5E6 = the controller
 *    shown (0 = Controller 1). On rows 3 and 4 the game shows a picture of the
 *    configuration (PlayStation buttons beside each action).
 * The overlay covers that picture's button column with the player's PC
 * bindings (keybinds.ini for a keyboard seat, input.ini [mapping] for a
 * controller). Its background is the screen itself, read back from the
 * presented frame while the Controller row is selected (no picture then), so
 * the column blends in at any size and aspect; key caps keep their shape in
 * 4:3, 16:9 and 21:9. The action names stay the game's own. The bindings come
 * from the game's button table, not from the picture: Analog 2's picture swaps
 * Kick and Alternate Attack, the table is what the game uses.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "controls_screen.h"
#include "pc_services.h"
#include "mod_plugins.h"
#include "hd_font_data.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
extern "C" uint8_t* memory_get_ram_ptr(void);
namespace {
constexpr uint32_t kMenuPtr = 0x8009C5C8, kMenuPage = 0x8009C5B8, kMenuDepth = 0x8009C5BC;
constexpr uint32_t kControllersMenu = 0x8009944C, kShown = 0x8009C5E6;
constexpr uint32_t kPlayer = 0x800D81D8, kRecord = 0x800D6DE0, kButtons = 0x800526A4;
constexpr uint16_t kRight = 1u << 5, kLeft = 1u << 7;
enum Device { NONE, KEYBOARD, GAMEPAD };
struct Seat {
    Device device = NONE;
    bool policy = false;                 // DualShock with our ANALOG button
    std::atomic<bool> analog{false};     // its mode (read by the policy callback)
    int chosen = -1;                     // remembered configuration
    int left_slot = -1, wait = 0, settle = 0, press = 0, release = 0;   // Left to deliver after a switch (see controls_pad)
    uint32_t last_sampled = 0; bool live = false;   // the policy saw this seat's controller since the last frame
};
constexpr int kSeats = 4;   // Players 1-4 (3 and 4 on the multitap, Side Car)
Seat seats[kSeats];
uint16_t last[kSeats] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, masked[kSeats];
bool ready;
std::atomic<bool> inited{false};          // seats known (the render thread asks for prompts)
// Player 1's prompts: the device shown (a controller seat's last-used device)
// and a serial the renderer compares to rebuild the prompt icons.
std::atomic<int> prompt_device{NONE};
// Each controller seat's kind of pad (SDL's gamepad type): Xbox-style glyphs, or
// PlayStation ones for a DualShock 3/4 or DualSense. RR_PAD_STYLE=xbox|ps3|ps4|ps5
// pins it (tests without the pad).
enum PadStyle { STYLE_XBOX, STYLE_PS3, STYLE_PS4, STYLE_PS5 };
std::atomic<int> pad_style[4] = {STYLE_XBOX, STYLE_XBOX, STYLE_XBOX, STYLE_XBOX};
int forced_style = -1;
int style_of(SDL_JoystickID id) {
    switch (SDL_GetGamepadTypeForID(id)) {
    case SDL_GAMEPAD_TYPE_PS3: return STYLE_PS3;
    case SDL_GAMEPAD_TYPE_PS4: return STYLE_PS4;
    case SDL_GAMEPAD_TYPE_PS5: return STYLE_PS5;
    default: return STYLE_XBOX;   // Xbox, generic XInput and every other pad keep the Xbox-style glyphs
    }
}
std::atomic<unsigned> prompt_serial{1};
// A seat's pad kind changed: Player 1's prompts are drawn again (the serial).
void set_style(int s, int style) {
    if (forced_style >= 0) style = forced_style;
    if (pad_style[s].exchange(style) == style) return;
    if (s == 0) prompt_serial.fetch_add(1);
    psx_mod_counter_add(style == STYLE_XBOX ? "rr.controls.style_xbox" : "rr.controls.style_playstation", 1);
}
// Controller seats take connected pads in SDL's order (the order they were plugged
// in): the first controller seat the first pad, the next one the next, as the
// runtime claims them. -1 when the pad is beyond the controller seats.
int seat_of_pad(int index) {
    for (int s = 0, k = 0; s < kSeats; s++) if (seats[s].device == GAMEPAD && k++ == index) return s;
    return -1;
}
int first_pad_seat() { for (int s = 0; s < kSeats; s++) if (seats[s].device == GAMEPAD) return s; return -1; }
// The pad style_from_use last resolved: stick events arrive many times a second,
// so the same pad is not looked up again (SDL_GetGamepads allocates a list).
SDL_JoystickID last_use_id = 0;
void styles_from_list() {
    last_use_id = 0;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) { const int s = seat_of_pad(i); if (s >= 0) set_style(s, style_of(ids[i])); }
    SDL_free(ids);
}
// The pad just used: the seat that holds it; a spare pad counts for the first controller seat.
void style_from_use(SDL_JoystickID id) {
    if (id == last_use_id) return;
    last_use_id = id;
    int n = 0, index = -1;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) if (ids[i] == id) index = i;
    SDL_free(ids);
    int s = seat_of_pad(index);
    if (s < 0) s = first_pad_seat();
    if (s >= 0) set_style(s, style_of(id));
}
std::wstring ini, keybinds, input;
std::atomic<uint32_t> view{0};           // overlay state: see publish()

uint8_t byte(uint32_t a) { return memory_get_ram_ptr()[a & 0x1FFFFF]; }
uint32_t word(uint32_t a) { uint32_t v; std::memcpy(&v, memory_get_ram_ptr() + (a & 0x1FFFFF), 4); return v; }
uint16_t half(uint32_t a) { uint16_t v; std::memcpy(&v, memory_get_ram_ptr() + (a & 0x1FFFFF), 2); return v; }
bool controllers_screen() { return word(kMenuPtr) == kControllersMenu && word(kMenuPage) == 1 && word(kMenuDepth) == 3; }
bool analog_config(int c) { return c >= 3; }
const wchar_t* config_key(int s) {
    static const wchar_t* keys[4][2] = {{L"p1_key_config", L"p1_pad_config"}, {L"p2_key_config", L"p2_pad_config"},
                                        {L"p3_key_config", L"p3_pad_config"}, {L"p4_key_config", L"p4_pad_config"}};
    return keys[s & 3][seats[s & 3].device == GAMEPAD];
}
void remember(int s, int config) {
    if (seats[s].chosen == config) return;
    seats[s].chosen = config;
    wchar_t value[8]; std::swprintf(value, 8, L"%d", config);
    WritePrivateProfileStringW(L"controls", config_key(s), value, ini.c_str());
    psx_mod_counter_add("rr.controls.remembered", 1);
}

// game.toml [controller] (the launcher's seat devices and modes), from --game.
std::string toml_value(const std::string& t, const char* key) {
    for (size_t p = 0; (p = t.find(key, p)) != std::string::npos; p++) {
        if (p && t[p - 1] != '\n') continue;
        size_t q = p + std::strlen(key);
        while (q < t.size() && t[q] == ' ') q++;
        if (q >= t.size() || t[q] != '=') continue;
        q = t.find('"', q);
        if (q == std::string::npos) return {};
        size_t e = t.find('"', q + 1);
        return e == std::string::npos ? std::string() : t.substr(q + 1, e - q - 1);
    }
    return {};
}
std::string game_toml() {
    const wchar_t* p = std::wcsstr(GetCommandLineW(), L"--game");
    if (!p) return {};
    p += 6;
    while (*p == L' ' || *p == L'=') p++;
    std::wstring path;
    if (*p == L'"') { p++; while (*p && *p != L'"') path += *p++; }
    else while (*p && *p != L' ') path += *p++;
    std::string text;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
        std::fclose(f);
    }
    return text;
}

// Called for every sample of a seat with a device. A seat whose controller is
// missing (Player 2 set to a controller that is not plugged in: the port
// shows an idle pad) is never sampled, so its mode cannot change: frame()
// tracks which seats are sampled and the ANALOG button waits for that.
std::atomic<uint32_t> sampled[kSeats];
uint32_t presentation(const PSXModControllerInput* in) {
    const uint32_t s = in ? in->sio_slot : 0;
    if (s >= kSeats) return PSX_MOD_CONTROLLER_DIGITAL;
    sampled[s].fetch_add(1);
    return seats[s].analog.load() ? PSX_MOD_CONTROLLER_ANALOG : PSX_MOD_CONTROLLER_DIGITAL;
}

#ifndef RR_PUBLIC_RELEASE
// Test pads (private builds only): RR_TEST_VIRTUAL_PADS=n attaches n SDL virtual
// gamepads ("RR Test Pad 1".."n"). The runtime claims them like real pads, in SDL
// order after any real ones, so seats 3 and 4 can be exercised without hardware.
// RR_TEST_PAD_FILE names a text file, read every 50 ms, of "<pad> <hex buttons>"
// lines; the bits are SDL gamepad buttons (0 south, 1 east, 2 west, 3 north,
// 4 back, 6 start, 9 left shoulder, 10 right shoulder, 11-14 D-pad up/down/left/right).
struct TestPads { int n = 0; SDL_Joystick* pad[4]{}; std::string file; uint64_t next = 0; } test_pads;
void test_pads_init() {
    const char* e = std::getenv("RR_TEST_VIRTUAL_PADS");
    if (!e) return;
    if (const char* f = std::getenv("RR_TEST_PAD_FILE")) test_pads.file = f;
    const int n = std::max(0, std::min(4, std::atoi(e)));
    for (int i = 0; i < n; i++) {
        SDL_VirtualJoystickDesc d; SDL_INIT_INTERFACE(&d);
        char name[32]; std::snprintf(name, sizeof name, "RR Test Pad %d", i + 1);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD; d.naxes = SDL_GAMEPAD_AXIS_COUNT; d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.button_mask = (1u << SDL_GAMEPAD_BUTTON_MISC1) - 1; d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1; d.name = name;
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        if (id) test_pads.pad[test_pads.n++] = SDL_OpenJoystick(id);
    }
    std::fprintf(stderr, "rr test pads: %d attached\n", test_pads.n);
}
void test_pads_poll() {
    if (!test_pads.n || test_pads.file.empty()) return;
    const uint64_t now = SDL_GetTicks();
    if (now < test_pads.next) return;
    test_pads.next = now + 50;
    FILE* f = std::fopen(test_pads.file.c_str(), "r");
    if (!f) return;
    int pad; unsigned mask;
    while (std::fscanf(f, "%d %x", &pad, &mask) == 2)
        if (pad >= 1 && pad <= test_pads.n && test_pads.pad[pad - 1])
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_MISC1; b++) SDL_SetJoystickVirtualButton(test_pads.pad[pad - 1], b, (mask >> b) & 1);
    std::fclose(f);
}
#else
void test_pads_init() {}
void test_pads_poll() {}
#endif

void init() {
    ready = true;
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    wchar_t* slash = n ? std::wcsrchr(path, L'\\') : nullptr;
    if (!slash) return;
    slash[1] = 0;
    std::wstring dir = path;
    keybinds = dir + L"keybinds.ini"; input = dir + L"input.ini";
    wchar_t full[32768];
    GetFullPathNameW((dir + L"..\\launcher.ini").c_str(), 32768, full, nullptr); ini = full;
    const std::string toml = game_toml();
    // Players 3 and 4 (controllers on the multitap) are in the runtime's settings.toml,
    // which the launcher writes next to it (game.toml has Players 1 and 2 only).
    std::string settings;
    if (FILE* f = _wfopen((dir + L"settings.toml").c_str(), L"rb")) {
        char buf[4096]; size_t got;
        while ((got = std::fread(buf, 1, sizeof buf, f)) > 0 && settings.size() < (1u << 20)) settings.append(buf, got);
        std::fclose(f);
    }
    // 3-4 players put every seat on the multitap, where the runtime presents plain
    // digital pads (its DualShock-on-tap option, multitap_analog, leaves Slot A =
    // Player 1 stale in this game: 0x73, or a config-capable 0x41, never updates).
    // No presentation policy there: the Controllers screen lists the digital
    // configurations and the remembered analog ones wait for a 1-2 player launch.
    const size_t players_at = toml.find("\nplayers=");
    const bool tap = players_at != std::string::npos && std::atoi(toml.c_str() + players_at + 9) >= 3;
    for (int s = 0; s < kSeats; s++) {
        char dev_key[16], mode_key[16];
        std::snprintf(dev_key, sizeof dev_key, "p%d_device", s + 1); std::snprintf(mode_key, sizeof mode_key, "p%d_mode", s + 1);
        const std::string& source = s < 2 ? toml : settings;
        const std::string dev = toml_value(source, dev_key), mode = toml_value(source, mode_key);
        Seat& seat = seats[s];
        if (s >= 2) seat.device = dev == "gamepad" ? GAMEPAD : NONE;
        else if (toml.empty()) {   // no game.toml: the launcher's own settings
            const int d = GetPrivateProfileIntW(L"controls", s ? L"p2_device" : L"device", 0, ini.c_str());
            seat.device = s ? (d == 1 ? GAMEPAD : d == 2 ? KEYBOARD : NONE) : (d ? GAMEPAD : KEYBOARD);
        } else seat.device = dev == "gamepad" ? GAMEPAD : dev == "keyboard" ? KEYBOARD : NONE;
        if (seat.device == NONE) continue;
        const int c = GetPrivateProfileIntW(L"controls", config_key(s), -1, ini.c_str());
        seat.chosen = c >= 0 && c <= 4 ? c : -1;
        seat.policy = seat.device == GAMEPAD && (toml.empty() || mode == "analog") && !tap;
        if (!seat.policy) {
            if (seat.chosen >= 3) seat.chosen = -1;
            continue;
        }
        // A controller starts in the kind of its remembered configuration (analog by default).
        seat.analog = seat.chosen < 0 || analog_config(seat.chosen);
        if (psx_mod_set_controller_presentation_policy((uint32_t)s, presentation,
                seat.analog ? PSX_MOD_CONTROLLER_ANALOG : PSX_MOD_CONTROLLER_DIGITAL, 1))
            psx_mod_counter_add("rr.controls.policy_seats", 1);
        else seat.policy = false;
    }
    prompt_device.store(seats[0].device);
    if (const char* e = std::getenv("RR_PAD_STYLE"))
        forced_style = !std::strcmp(e, "ps3") ? STYLE_PS3 : !std::strcmp(e, "ps4") ? STYLE_PS4 : !std::strcmp(e, "ps5") ? STYLE_PS5 : STYLE_XBOX;
    if (forced_style >= 0) for (auto& p : pad_style) p = forced_style;
    else styles_from_list();
    test_pads_init();
    inited.store(true);
}

// The player's configuration while the game has settled on the pad's kind:
// -1 while the pad id the game saw or its configuration is of the other kind
// (mode just switched, pad being re-initialised).
int settled_config(int s) {
    const Seat& seat = seats[s];
    const bool analog = seat.policy && seat.analog.load();
    const int config = byte(kPlayer + 36 * s + 8), id = byte(kRecord + 0xC0 * s + 0xC);
    if (config > 4 || id != (analog ? 0x73 : 0x41) || analog_config(config) != analog) return -1;
    return config;
}

// Overlay state for the render thread: bit 0 on the Controllers screen, bits
// 1-3 row, bits 4-5 controller shown, bits 6-8 its configuration, bits 9-10 device.
void publish(bool screen) {
    if (!screen) { view.store(0); return; }
    const int shown = byte(kShown) & 3, config = byte(kPlayer + 36 * shown + 8);
    view.store(1u | (uint32_t)(half(kControllersMenu + 4) & 7) << 1 | (uint32_t)shown << 4 |
               (uint32_t)(config > 4 ? 0 : config) << 6 | (uint32_t)seats[shown].device << 9);
}

void frame() {
    const bool screen = controllers_screen();
    for (int s = 0; s < kSeats; s++) {
        Seat& seat = seats[s];
        const uint32_t n = sampled[s].load();
        seat.live = n != seat.last_sampled; seat.last_sampled = n;
        if (seat.device == NONE) continue;
        const int config = settled_config(s);
        if (config < 0) continue;
        if (screen) {
            if (config != seat.chosen) { remember(s, config); psx_mod_counter_add("rr.controls.chosen", 1); }
        } else if (seat.chosen >= 0 && analog_config(seat.chosen) == analog_config(config) && config != seat.chosen) {
            psx_mod_write_byte(kPlayer + 36 * s + 8, (uint8_t)seat.chosen);
            psx_mod_counter_add("rr.controls.restored", 1);
        }
    }
    publish(screen);
}

// The ANALOG button, pressed past either end of the game's Configuration list.
uint16_t analog_button(int slot, uint16_t pressed) {
    if (!(pressed & (kLeft | kRight)) || !controllers_screen() || half(kControllersMenu + 4) != 3) return 0;
    const int s = byte(kShown) & 3;
    Seat& seat = seats[s];
    if (!seat.policy || !seat.live || seat.left_slot >= 0) return 0;
    const int config = settled_config(s);
    if (config < 0) return 0;
    const bool analog = seat.analog.load();
    const uint16_t press = (pressed & kRight) && config == (analog ? 4 : 2) ? kRight
                         : (pressed & kLeft) && config == (analog ? 3 : 0) ? kLeft : 0;
    if (!press) return 0;
    // The game answers the new pad with the other kind's first configuration
    // (Right: done). Left continues at its last one: the game gets one Left.
    if (press == kLeft) { seat.left_slot = slot; seat.wait = seat.settle = seat.press = seat.release = 0; }
    seat.analog = !analog;
    psx_mod_counter_add(analog ? "rr.controls.to_digital" : "rr.controls.to_analog", 1);
    return press;
}
// That Left: half a second after the game lists the new kind from its first
// configuration (the screen reloads its picture first), pressed for 8 polls
// (the menu reads its pad at 15 Hz), then released for 8. Abandoned when the
// screen or row changes or the switch takes over 3 s.
uint16_t deliver_left(int slot, uint16_t out) {
    for (int s = 0; s < kSeats; s++) {
        Seat& seat = seats[s];
        if (seat.left_slot != slot) continue;
        if (!controllers_screen() || half(kControllersMenu + 4) != 3 || (byte(kShown) & 3) != s) { seat.left_slot = -1; continue; }
        if (seat.press) { out &= (uint16_t)~kLeft; if (!--seat.press) seat.release = 8; }
        else if (seat.release) { out |= kLeft; if (!--seat.release) seat.left_slot = -1; }
        else if (settled_config(s) != (seat.analog.load() ? 3 : 0)) { if (++seat.wait > 180) seat.left_slot = -1; }
        else if (++seat.settle >= 30) { seat.press = 8; psx_mod_counter_add("rr.controls.left_delivered", 1); }
    }
    return out;
}
}

namespace rrpc {
uint16_t controls_pad(int slot, uint16_t buttons) {
    if (!ready) init();
    if (slot < 0 || slot >= kSeats) return buttons;
    if (slot == 0) test_pads_poll();
    const uint16_t pressed = (uint16_t)(last[slot] & ~buttons);
    last[slot] = buttons;
    masked[slot] &= (uint16_t)~buttons;   // until released
    if (slot == 0) frame();
    masked[slot] |= analog_button(slot, pressed);
    return deliver_left(slot, (uint16_t)(buttons | masked[slot]));
}
}

// ---------------------------------------------------------------------------
// Overlay drawing (render thread).
namespace {
constexpr double kPi = 3.14159265358979323846;
struct Pt { float x, y; };
using Path = std::vector<std::vector<Pt>>;
struct Canvas {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // 0xAARRGGBB (GL_BGRA bytes), premultiplied
    void blend(int x, int y, uint32_t rgb, float a) {   // rgb over the pixel with coverage a
        if (a <= 0.0f) return;
        if (a > 1.0f) a = 1.0f;
        uint32_t& d = px[(size_t)y * w + x];
        const float ia = 1.0f - a;
        auto mix = [&](int sh) { return (uint32_t)(((rgb >> sh) & 255) * a + ((d >> sh) & 255) * ia + 0.5f) << sh; };
        d = (uint32_t)(255 * a + (d >> 24) * ia + 0.5f) << 24 | mix(16) | mix(8) | mix(0);
    }
};
// Non-zero fill, 4 sub-rows per pixel, exact horizontal coverage.
void fill(Canvas& c, const Path& path, uint32_t rgb, float alpha = 1.0f) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto& ct : path) for (auto& p : ct) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    const int ix0 = std::max(0, (int)std::floor(x0)), ix1 = std::min(c.w, (int)std::ceil(x1) + 1);
    const int iy0 = std::max(0, (int)std::floor(y0)), iy1 = std::min(c.h, (int)std::ceil(y1) + 1);
    if (ix0 >= ix1 || iy0 >= iy1) return;
    std::vector<float> cov(ix1 - ix0);
    struct Cross { float x; int d; };
    std::vector<Cross> xs;
    auto span = [&](float a, float b) {
        a = std::max(a, (float)ix0); b = std::min(b, (float)ix1);
        if (b <= a) return;
        const int ia = (int)a, ib = (int)b;
        if (ia == ib) { cov[ia - ix0] += 0.25f * (b - a); return; }
        cov[ia - ix0] += 0.25f * (ia + 1 - a);
        for (int x = ia + 1; x < ib; x++) cov[x - ix0] += 0.25f;
        if (ib < ix1) cov[ib - ix0] += 0.25f * (b - ib);
    };
    for (int py = iy0; py < iy1; py++) {
        std::fill(cov.begin(), cov.end(), 0.0f);
        for (int k = 0; k < 4; k++) {
            const float yc = py + (k + 0.5f) * 0.25f;
            xs.clear();
            for (auto& ct : path)
                for (size_t i = 0, n = ct.size(); i < n; i++) {
                    const Pt a = ct[i], b = ct[(i + 1) % n];
                    if ((a.y <= yc) != (b.y <= yc)) xs.push_back({a.x + (yc - a.y) / (b.y - a.y) * (b.x - a.x), b.y > a.y ? 1 : -1});
                }
            std::sort(xs.begin(), xs.end(), [](const Cross& a, const Cross& b) { return a.x < b.x; });
            int wind = 0; float start = 0;
            for (auto& x : xs) {
                const int before = wind; wind += x.d;
                if (!before && wind) start = x.x;
                else if (before && !wind) span(start, x.x);
            }
        }
        for (int x = ix0; x < ix1; x++) c.blend(x, py, rgb, cov[x - ix0] * alpha);
    }
}
std::vector<Pt> rrect(float x, float y, float w, float h, float r) {
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    std::vector<Pt> p;
    const float cx[4] = {x + w - r, x + w - r, x + r, x + r}, cy[4] = {y + r, y + h - r, y + h - r, y + r};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= 6; i++) {
            const float a = (float)(kPi * 0.5 * (k - 1 + i / 6.0));
            p.push_back({cx[k] + r * std::cos(a), cy[k] + r * std::sin(a)});
        }
    return p;
}
std::vector<Pt> circle(float cx, float cy, float r, bool reverse = false) {
    std::vector<Pt> p;
    for (int i = 0; i < 40; i++) {
        const float a = (float)(2 * kPi * (reverse ? 40 - i : i) / 40);
        p.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return p;
}
// Arrow head pointing dir (0 left, 1 right, 2 up, 3 down), centred, size s.
std::vector<Pt> arrow(float cx, float cy, float s, int dir) {
    const float a = s * 0.5f, b = s * 0.42f;
    switch (dir) {
    case 0: return {{cx - b, cy}, {cx + b, cy - a}, {cx + b, cy + a}};
    case 1: return {{cx + b, cy}, {cx - b, cy + a}, {cx - b, cy - a}};
    case 2: return {{cx, cy - b}, {cx + a, cy + b}, {cx - a, cy + b}};
    default: return {{cx, cy + b}, {cx - a, cy - b}, {cx + a, cy - b}};
    }
}
constexpr int kBlack = 2;   // Roboto Flex style
float text_width(const std::string& s, float em) {
    float w = 0;
    for (unsigned char ch : s) if (ch >= 32 && ch <= RR_HD_LAST) w += rr_hd_advance[kBlack][ch - 32];
    return w * em / 1024.0f;
}
// Text centred on (cx, cy): cap height 0.71 em.
void text(Canvas& c, const std::string& s, float cx, float cy, float em, uint32_t rgb, float alpha = 1.0f) {
    Path path;
    float pen = cx - text_width(s, em) * 0.5f;
    const float base = cy + 0.355f * em;
    for (unsigned char ch : s) {
        if (ch >= RR_HD_FIRST && ch <= RR_HD_LAST) {
            const int16_t* d = rr_hd_data + rr_hd_index[kBlack][ch - RR_HD_FIRST];
            const int nc = *d++;
            for (int k = 0; k < nc; k++) {
                const int np = *d++;
                std::vector<Pt> ct;
                for (int i = 0; i < np; i++, d += 2) ct.push_back({pen + d[0] * em / 1024.0f, base + d[1] * em / 1024.0f});
                path.push_back(std::move(ct));
            }
        }
        if (ch >= 32 && ch <= RR_HD_LAST) pen += rr_hd_advance[kBlack][ch - 32] * em / 1024.0f;
    }
    fill(c, path, rgb, alpha);
}

// One binding, as drawn.
enum Kind { KEY, KEY_ARROW, FACE, BUMPER, TRIGGER, MENU, DPAD, STICK, SEP };
struct Token { Kind kind; std::string label; int dir = 0; uint32_t color = 0; };
constexpr uint32_t kCapFace = 0xE7EBF3, kCapSide = 0x7C879F, kCapText = 0x121A34, kDark = 0x1A2030, kDarkEdge = 0xB9C3D6, kWhite = 0xFFFFFF;

float token_width(const Token& t, float H) {
    switch (t.kind) {
    case KEY: return std::max(H, text_width(t.label, 0.62f * H) + 0.55f * H);
    case KEY_ARROW: case FACE: case DPAD: return H;
    case BUMPER: return 1.5f * H;
    case TRIGGER: return 1.35f * H;
    case MENU: return text_width(t.label, 0.5f * H) + 0.6f * H;
    case STICK: return H + (t.dir == 1 ? 0.9f * H : t.dir ? 0.5f * H : 0.0f);
    case SEP: return text_width(t.label, 0.7f * H) + 0.12f * H;
    }
    return H;
}
void shadow(Canvas& c, float x, float y, float w, float h, float r, float H) {
    fill(c, {rrect(x + 0.05f * H, y + 0.08f * H, w, h, r)}, 0x000000, 0.45f);
}
void draw_token(Canvas& c, const Token& t, float x, float cy, float H) {
    const float w = token_width(t, H), top = cy - 0.5f * H, edge = std::max(1.0f, 0.06f * H);
    switch (t.kind) {
    case KEY: case KEY_ARROW: {
        // A key cap: light top over a darker base.
        shadow(c, x, top, w, H, 0.2f * H, H);
        fill(c, {rrect(x, top, w, H, 0.2f * H)}, kCapSide);
        fill(c, {rrect(x + edge, top + edge, w - 2 * edge, H - 2 * edge - 0.12f * H, 0.16f * H)}, kCapFace);
        const float fy = cy - 0.06f * H;
        if (t.kind == KEY) text(c, t.label, x + w * 0.5f, fy, 0.62f * H, kCapText);
        else fill(c, {arrow(x + w * 0.5f, fy, 0.5f * H, t.dir)}, kCapText);
        break;
    }
    case FACE: {
        const float r = 0.5f * H, cx = x + r;
        fill(c, {circle(cx + 0.05f * H, cy + 0.08f * H, r)}, 0x000000, 0.45f);
        fill(c, {circle(cx, cy, r)}, t.dir ? kDarkEdge : t.color);   // PlayStation symbols: a neutral rim, the colour is the symbol's
        fill(c, {circle(cx, cy, r - (t.dir ? 1.0f : 1.6f) * edge)}, kDark);
        if (!t.dir) { text(c, t.label, cx, cy, 0.66f * H, t.color); break; }
        // PlayStation-style symbols, our own outlines: 1 Cross, 2 Circle, 3 Square, 4 Triangle.
        const float s = 0.25f * H, sw = std::max(1.0f, 0.085f * H);
        auto rev = [](std::vector<Pt> v) { std::reverse(v.begin(), v.end()); return v; };
        auto bar = [&](float ax, float ay, float bx, float by) {
            const float dx = bx - ax, dy = by - ay, l = std::max(1e-3f, std::sqrt(dx * dx + dy * dy)), nx = -dy / l * 0.5f * sw, ny = dx / l * 0.5f * sw;
            return std::vector<Pt>{{ax + nx, ay + ny}, {bx + nx, by + ny}, {bx - nx, by - ny}, {ax - nx, ay - ny}};
        };
        Path sym;
        if (t.dir == 1) { sym = {bar(cx - s, cy - s, cx + s, cy + s), bar(cx - s, cy + s, cx + s, cy - s)}; }
        else if (t.dir == 2) { sym = {circle(cx, cy, 1.1f * s + 0.5f * sw), circle(cx, cy, 1.1f * s - 0.5f * sw, true)}; }
        else if (t.dir == 3) { const float a = 0.95f * s; sym = {rrect(cx - a - 0.5f * sw, cy - a - 0.5f * sw, 2 * a + sw, 2 * a + sw, 0.08f * H), rev(rrect(cx - a + 0.5f * sw, cy - a + 0.5f * sw, 2 * a - sw, 2 * a - sw, 0.02f * H))}; }
        else {
            // A ring between two concentric triangles (apex up); the inner one sits a stroke width inside.
            const float R = 1.25f * s, Ri = R - 2.0f * sw, yc = cy + 0.12f * s;
            auto tri = [&](float rad) { std::vector<Pt> v; for (int k = 0; k < 3; k++) { const float a = (float)(-kPi / 2 + k * 2 * kPi / 3); v.push_back({cx + rad * std::cos(a), yc + rad * std::sin(a)}); } return v; };
            sym = {tri(R), rev(tri(std::max(0.0f, Ri)))};
        }
        fill(c, sym, t.color);
        break;
    }
    case BUMPER: case TRIGGER: {
        const float h = t.kind == BUMPER ? 0.72f * H : H, y = cy - 0.5f * h, r = t.kind == BUMPER ? 0.36f * h : 0.3f * H;
        shadow(c, x, y, w, h, r, H);
        fill(c, {rrect(x, y, w, h, r)}, kDarkEdge);
        fill(c, {rrect(x + edge, y + edge, w - 2 * edge, h - 2 * edge, r - edge)}, kDark);
        text(c, t.label, x + w * 0.5f, cy, 0.52f * H, kWhite);
        break;
    }
    case MENU: {
        const float h = 0.72f * H, y = cy - 0.5f * h;
        shadow(c, x, y, w, h, 0.5f * h, H);
        fill(c, {rrect(x, y, w, h, 0.5f * h)}, kDarkEdge);
        fill(c, {rrect(x + edge, y + edge, w - 2 * edge, h - 2 * edge, 0.5f * h - edge)}, kDark);
        text(c, t.label, x + w * 0.5f, cy, 0.5f * H, kWhite);
        break;
    }
    case DPAD: {
        // A dark cross; the pressed arm is lit.
        const float a = 0.38f * H, cx = x + 0.5f * H, r = 0.1f * H;
        Path cross = {rrect(cx - 0.5f * a, top, a, H, r), rrect(x, cy - 0.5f * a, H, a, r)};
        for (auto& ct : cross) for (auto& p : ct) { p.x += 0.05f * H; p.y += 0.08f * H; }
        fill(c, cross, 0x000000, 0.45f);
        fill(c, {rrect(cx - 0.5f * a, top, a, H, r), rrect(x, cy - 0.5f * a, H, a, r)}, kDarkEdge);
        fill(c, {rrect(cx - 0.5f * a + edge, top + edge, a - 2 * edge, H - 2 * edge, r), rrect(x + edge, cy - 0.5f * a + edge, H - 2 * edge, a - 2 * edge, r)}, kDark);
        const float arm = 0.5f * H - 0.5f * a - edge;   // from the hub to the arm's end
        auto light = [&](int dir) {
            const float lx = dir == 0 ? x + edge : dir == 1 ? cx + 0.5f * a : cx - 0.5f * a + edge;
            const float ly = dir == 2 ? top + edge : dir == 3 ? cy + 0.5f * a : cy - 0.5f * a + edge;
            const bool across = dir < 2;
            fill(c, {rrect(lx, ly, across ? arm : a - 2 * edge, across ? a - 2 * edge : arm, 0.06f * H)}, kWhite);
        };
        if (t.dir == 4) { light(2); light(3); }        // up / down
        else if (t.dir == 5) { light(0); light(1); }   // left / right
        else light(t.dir);
        break;
    }
    case STICK: {
        // A thumbstick seen from above: the letter on its cap; arrows beside it.
        const float r = 0.5f * H, cx = x + (t.dir == 1 ? 0.45f * H : 0) + r;
        fill(c, {circle(cx + 0.05f * H, cy + 0.08f * H, r)}, 0x000000, 0.45f);
        fill(c, {circle(cx, cy, r)}, kDarkEdge);
        fill(c, {circle(cx, cy, r - edge)}, kDark);
        fill(c, {circle(cx, cy, 0.34f * H)}, 0x394158);
        text(c, t.label, cx, cy, 0.5f * H, kWhite);
        auto head = [&](float ax, float ay, int d) { fill(c, {arrow(ax + 0.03f * H, ay + 0.05f * H, 0.4f * H, d)}, 0x000000, 0.5f); fill(c, {arrow(ax, ay, 0.38f * H, d)}, kWhite); };
        if (t.dir == 1) { head(x + 0.2f * H, cy, 0); head(cx + r + 0.25f * H, cy, 1); }
        else if (t.dir == 2) { head(cx + r + 0.25f * H, cy - 0.24f * H, 2); head(cx + r + 0.25f * H, cy + 0.24f * H, 3); }
        else if (t.dir == 3) head(cx + r + 0.25f * H, cy, 2);
        else if (t.dir == 4) head(cx + r + 0.25f * H, cy, 3);
        break;
    }
    case SEP:
        text(c, t.label, x + w * 0.5f + 0.04f * H, cy + 0.06f * H, 0.7f * H, 0x000000, 0.6f);
        text(c, t.label, x + w * 0.5f, cy, 0.7f * H, kWhite);
        break;
    }
}

// Bindings. Button names as keybinds.ini / input.ini spell them, in the
// game's button-list order.
const char* const kButtonNames[15] = {"left", "right", "up", "down", "square", "circle", "triangle", "cross",
                                      "l1", "l2", "r1", "r2", "start", "select", "l3"};
std::string ini_string(const std::wstring& file, const wchar_t* section, const char* key) {
    wchar_t k[32], v[64];
    std::swprintf(k, 32, L"%hs", key);
    GetPrivateProfileStringW(section, k, L"", v, 64, file.c_str());
    std::string out;
    for (const wchar_t* p = v; *p; p++) out += *p < 128 ? (char)*p : '?';
    return out;
}
Token key_token(const std::string& name) {
    if (name == "Left") return {KEY_ARROW, "", 0};
    if (name == "Right") return {KEY_ARROW, "", 1};
    if (name == "Up") return {KEY_ARROW, "", 2};
    if (name == "Down") return {KEY_ARROW, "", 3};
    static const char* const names[][2] = {{"Return", "Enter"}, {"Escape", "Esc"}, {"Backspace", "Bksp"}, {"Keypad ", "Num "},
                                           {"Right ", "R "}, {"Left ", "L "}, {"PageUp", "PgUp"}, {"PageDown", "PgDn"}};
    std::string s = name.empty() ? "?" : name;
    for (auto& n : names) if (s.rfind(n[0], 0) == 0) s = n[1] + s.substr(std::strlen(n[0]));
    return {KEY, s};
}
// input.ini names buttons by SDL's positions (a/b/x/y = south/east/west/north),
// the same for every pad; only the drawing follows the pad's kind (pad_style).
Token pad_token(const std::string& source, int style) {
    static const struct { const char* source; Kind kind; const char* label; int dir; uint32_t color; } xbox[] = {
        {"a", FACE, "A", 0, 0x5DBB4A}, {"b", FACE, "B", 0, 0xE24B3D}, {"x", FACE, "X", 0, 0x3B86E0}, {"y", FACE, "Y", 0, 0xF2BE2E},
        {"leftshoulder", BUMPER, "LB"}, {"rightshoulder", BUMPER, "RB"}, {"lefttrigger", TRIGGER, "LT"}, {"righttrigger", TRIGGER, "RT"},
        {"dpleft", DPAD, "", 0}, {"dpright", DPAD, "", 1}, {"dpup", DPAD, "", 2}, {"dpdown", DPAD, "", 3},
        {"start", MENU, "Menu"}, {"back", MENU, "View"}, {"guide", MENU, "Guide"},
        {"leftstick", STICK, "L", 0}, {"rightstick", STICK, "R", 0}};
    // PlayStation pads: Cross / Circle / Square / Triangle in their usual colours, L1-R2,
    // and the centre buttons as each model names them (PS3 Select/Start, PS4 Share/Options,
    // PS5 Create/Options).
    static const struct { const char* source; Kind kind; const char* label; int dir; uint32_t color; } ps[] = {
        {"a", FACE, "Cross", 1, 0x86B8EE}, {"b", FACE, "Circle", 2, 0xF26C6C}, {"x", FACE, "Square", 3, 0xE995CF}, {"y", FACE, "Triangle", 4, 0x45D2AE},
        {"leftshoulder", BUMPER, "L1"}, {"rightshoulder", BUMPER, "R1"}, {"lefttrigger", TRIGGER, "L2"}, {"righttrigger", TRIGGER, "R2"},
        {"dpleft", DPAD, "", 0}, {"dpright", DPAD, "", 1}, {"dpup", DPAD, "", 2}, {"dpdown", DPAD, "", 3},
        {"guide", MENU, "PS"}, {"leftstick", STICK, "L", 0}, {"rightstick", STICK, "R", 0}};
    if (style != STYLE_XBOX) {
        if (source == "start") return {MENU, style == STYLE_PS3 ? "Start" : "Options"};
        if (source == "back") return {MENU, style == STYLE_PS3 ? "Select" : style == STYLE_PS4 ? "Share" : "Create"};
        for (auto& p : ps) if (source == p.source) return {p.kind, p.label, p.dir, p.color};
    } else for (auto& p : xbox) if (source == p.source) return {p.kind, p.label, p.dir, p.color};
    return {KEY, source.empty() ? "?" : source};
}
Token binding(Device device, int seat, int button) {
    if (button >= 15 && button <= 18) return {STICK, button < 17 ? "R" : "L", button & 1 ? 3 : 4};   // stick up / down
    if (button < 0 || button > 14) return {SEP, "-"};
    if (device == KEYBOARD) return key_token(ini_string(keybinds, seat ? L"player2" : L"player1", kButtonNames[button]));
    return pad_token(ini_string(input, L"mapping", kButtonNames[button]), pad_style[seat & 3].load());
}

// The pictures' rows: guest y (of 240) and contents. 0-14 = the action with
// that index in the button table; the rest are fixed marks.
enum { PLUS = 100, SLASH, LS_H, LS_DOWN, RS_V, RS_UP, LS_V };
struct Row { float y; bool small; std::vector<int> items; };
const std::vector<Row>& rows(int config) {
    static const std::vector<Row> digital12 = {
        {23.3f, 0, {3}}, {38.3f, 0, {3, PLUS, 13}}, {52.7f, 0, {4}}, {69.0f, 0, {12}}, {84.3f, 0, {2}}, {99.3f, 0, {0}}, {114.0f, 0, {1}},
        {128.7f, 0, {5, 6}}, {143.7f, 0, {9}}, {158.7f, 0, {11}}, {173.7f, 0, {10}}, {188.0f, 0, {8}}, {202.0f, 0, {PLUS, 14, SLASH, 13}},
        {217.3f, 0, {9, SLASH, 11, SLASH, 10}}};
    static const std::vector<Row> digital3 = {
        {23.3f, 0, {3}}, {38.3f, 0, {3, PLUS, 13}}, {52.7f, 0, {4}}, {69.0f, 0, {8}}, {84.3f, 0, {12}}, {99.3f, 0, {0}}, {114.0f, 0, {1}},
        {128.7f, 0, {5, 6}}, {143.7f, 0, {11}}, {158.7f, 0, {10}}, {173.7f, 0, {9}}, {188.0f, 0, {2}}, {202.0f, 0, {PLUS, 14, SLASH, 13}},
        {217.3f, 0, {11, SLASH, 10, SLASH, 9}}};
    static const std::vector<Row> analog1 = {
        {22.7f, 0, {LS_H}}, {32.3f, 1, {LS_DOWN}}, {41.0f, 1, {8}}, {56.0f, 0, {RS_V}}, {66.0f, 1, {RS_UP}},
        {86.7f, 0, {0}}, {101.7f, 0, {1}}, {115.7f, 0, {2}}, {129.7f, 0, {12}}, {144.0f, 0, {9}}, {158.7f, 0, {11}}, {174.0f, 0, {10}},
        {196.0f, 0, {PLUS, LS_V}}, {217.3f, 0, {9, SLASH, 11, SLASH, 10}}};
    // Analog 2's picture lists Kick, Alternate, Primary (and calls Kick R1).
    static const std::vector<Row> analog2 = {
        {22.7f, 0, {LS_H}}, {32.3f, 1, {LS_DOWN}}, {41.0f, 1, {8}}, {56.0f, 0, {RS_V}}, {66.0f, 1, {RS_UP}},
        {86.7f, 0, {0}}, {101.7f, 0, {1}}, {115.7f, 0, {2}}, {129.7f, 0, {12}}, {144.0f, 0, {11}}, {158.7f, 0, {10}}, {174.0f, 0, {9}},
        {196.0f, 0, {PLUS, LS_V}}, {217.3f, 0, {11, SLASH, 10, SLASH, 9}}};
    return config == 2 ? digital3 : config == 3 ? analog1 : config == 4 ? analog2 : digital12;
}

// The button column the overlay covers, guest coordinates of the 512x240 frontend.
constexpr float kColLeft = 248, kColRight = 365, kColTop = 13, kColBottom = 229, kAlign = 357;
struct Rect { int x, y, w, h; bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; } };
struct Frame { int ox, oy, vw, vh; };
Frame viewport(int ww, int wh) {
    const int a = rrpc::graphics_options().aspect;
    const double ratio = a == 2 ? 21.0 / 9 : a == 1 ? 16.0 / 9 : 4.0 / 3;
    int vw = ww, vh = (int)(ww / ratio);
    if (vh > wh) { vh = wh; vw = (int)(wh * ratio); }
    return {(ww - vw) / 2, (wh - vh) / 2, vw, vh};
}
Rect column(const Frame& f) {
    const int x0 = f.ox + (int)std::floor(f.vw * kColLeft / 512), x1 = f.ox + (int)std::ceil(f.vw * kColRight / 512);
    const int y0 = f.oy + (int)std::floor(f.vh * kColTop / 240), y1 = f.oy + (int)std::ceil(f.vh * kColBottom / 240);
    return {x0, y0, x1 - x0, y1 - y0};
}

// The column's background, read back from the presented frame.
using ReadPixels = void(APIENTRY*)(int, int, int, int, unsigned, unsigned, void*);
using GetIntegerv = void(APIENTRY*)(unsigned, int*);
using BindFramebuffer = void(APIENTRY*)(unsigned, unsigned);
using BindBuffer = void(APIENTRY*)(unsigned, unsigned);
using ReadBuffer = void(APIENTRY*)(unsigned);
using PixelStorei = void(APIENTRY*)(unsigned, int);
std::vector<uint32_t> backdrop;
Rect backdrop_rect{};
uint32_t backdrop_serial;
bool capture(const Rect& r, int wh) {
    static ReadPixels read_pixels; static GetIntegerv get; static BindFramebuffer bind_fb; static BindBuffer bind_buf; static ReadBuffer read_buffer; static PixelStorei store;
    if (!read_pixels) {
        read_pixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels"); get = (GetIntegerv)SDL_GL_GetProcAddress("glGetIntegerv");
        bind_fb = (BindFramebuffer)SDL_GL_GetProcAddress("glBindFramebuffer"); bind_buf = (BindBuffer)SDL_GL_GetProcAddress("glBindBuffer");
        read_buffer = (ReadBuffer)SDL_GL_GetProcAddress("glReadBuffer"); store = (PixelStorei)SDL_GL_GetProcAddress("glPixelStorei");
    }
    if (!read_pixels || !get || !bind_fb || !bind_buf || !read_buffer || !store || r.w <= 0 || r.h <= 0) return false;
    enum : unsigned { READ_FB = 0x8CA8, READ_FB_BINDING = 0x8CAA, PACK_BUFFER = 0x88EB, PACK_BUFFER_BINDING = 0x88ED, READ_BUF = 0x0C02,
                      BACK = 0x0405, PACK_ALIGN = 0x0D05, PACK_ROW = 0x0D02, BGRA = 0x80E1, UBYTE = 0x1401 };
    int fb = 0, buf = 0, rb = 0, align = 4, row = 0;
    get(READ_FB_BINDING, &fb); get(PACK_BUFFER_BINDING, &buf); get(PACK_ALIGN, &align); get(PACK_ROW, &row);
    bind_fb(READ_FB, 0); get(READ_BUF, &rb); bind_buf(PACK_BUFFER, 0); store(PACK_ALIGN, 4); store(PACK_ROW, 0); read_buffer(BACK);
    std::vector<uint32_t> img((size_t)r.w * r.h);
    read_pixels(r.x, wh - r.y - r.h, r.w, r.h, BGRA, UBYTE, img.data());
    read_buffer((unsigned)rb); store(PACK_ALIGN, align); store(PACK_ROW, row); bind_buf(PACK_BUFFER, (unsigned)buf); bind_fb(READ_FB, (unsigned)fb);
    backdrop.resize(img.size());
    for (int y = 0; y < r.h; y++) std::memcpy(&backdrop[(size_t)y * r.w], &img[(size_t)(r.h - 1 - y) * r.w], (size_t)r.w * 4);
    for (auto& p : backdrop) p |= 0xFF000000u;
    backdrop_rect = r; backdrop_serial++;
    psx_mod_counter_add("rr.controls.backdrops", 1);
    return true;
}

Canvas image;
struct ImageKey { uint32_t view, serial; int ww, wh, aspect, style; bool operator!=(const ImageKey& o) const { return std::memcmp(this, &o, sizeof o) != 0; } };
ImageKey image_key{~0u};
void build(uint32_t v, const Frame& f, const Rect& r) {
    image.w = r.w; image.h = r.h;
    if (backdrop_rect == r && backdrop.size() == (size_t)r.w * r.h) image.px = backdrop;
    else {   // not read yet (e.g. a savestate taken on this screen): a plain panel
        image.px.assign((size_t)r.w * r.h, 0);
        for (int y = 0; y < r.h; y++) {
            const float t = (float)y / std::max(1, r.h - 1);
            const uint32_t g = 0xFF000000u | (uint32_t)(20 + 6 * t) << 16 | (uint32_t)(40 - 14 * t) << 8 | (uint32_t)(96 - 30 * t);
            std::fill(image.px.begin() + (size_t)y * r.w, image.px.begin() + (size_t)(y + 1) * r.w, g);
        }
    }
    const int seat = v >> 4 & 3, config = v >> 6 & 7;
    const Device device = (Device)(v >> 9 & 3);
    const float sy = f.vh / 240.0f, H = 11.6f * sy;
    auto X = [&](float gx) { return f.ox + f.vw * gx / 512.0f - r.x; };
    auto Y = [&](float gy) { return f.oy + gy * sy - r.y; };
    if (device == NONE) {
        text(image, "Not connected", X((kColLeft + kColRight) / 2), Y(120), 0.75f * H, 0x000000, 0.6f);
        text(image, "Not connected", X((kColLeft + kColRight) / 2) - 1, Y(120) - 1, 0.75f * H, 0xD6DCE8);
        return;
    }
    uint32_t table[15];
    std::memcpy(table, memory_get_ram_ptr() + ((kButtons + 60u * (uint32_t)config) & 0x1FFFFF), sizeof table);
    const float right = X(kAlign), avail = right - X(kColLeft + 4);
    for (const Row& row : rows(config)) {
        std::vector<Token> tokens;
        for (int item : row.items) {
            if (item == PLUS) tokens.push_back({SEP, "+"});
            else if (item == SLASH) tokens.push_back({SEP, "/"});
            else if (item == LS_H) tokens.push_back({STICK, "L", 1});
            else if (item == LS_DOWN) tokens.push_back({STICK, "L", 4});
            else if (item == RS_V) tokens.push_back({STICK, "R", 2});
            else if (item == RS_UP) tokens.push_back({STICK, "R", 3});
            else if (item == LS_V) tokens.push_back({STICK, "L", 2});
            else tokens.push_back(binding(device, seat, (int)table[item]));
        }
        float h = row.small ? 0.72f * H : H, gap = 0.16f * h, total = 0;
        for (auto& t : tokens) total += token_width(t, h) + gap;
        total -= gap;
        if (total > avail) { h *= avail / total; gap = 0.16f * h; total = avail; }
        float x = right - total;
        for (auto& t : tokens) { draw_token(image, t, x, Y(row.y), h); x += token_width(t, h) + gap; }
    }
    psx_mod_counter_add("rr.controls.overlays", 1);
}
}

namespace rrpc {
int controls_overlay(int ww, int wh, const uint32_t** pixels, int* w, int* h, int* x, int* y, int* dw, int* dh) {
    static int steady;
    const uint32_t v = view.load();
    const int row = v >> 1 & 7;
    if (!(v & 1) || ww <= 0 || wh <= 0) { steady = 0; return 0; }
    const Frame f = viewport(ww, wh);
    const Rect r = column(f);
    if (r.x < 0 || r.y < 0 || r.x + r.w > ww || r.y + r.h > wh) return 0;
    if (row == 2) {
        // No picture on the Controller row: the column shows the menu backdrop.
        // Read it once the screen has settled, and again every half second.
        if (++steady >= 20 && (steady - 20) % 30 == 0) capture(r, wh);
        return 0;
    }
    steady = 0;
    if (row != 3 && row != 4) return 0;
    const ImageKey key{v & ~0xFu, backdrop_serial, ww, wh, rrpc::graphics_options().aspect, pad_style[v >> 4 & 3].load()};
    if (key != image_key || image.w != r.w || image.h != r.h) { build(v, f, r); image_key = key; }
    *pixels = image.px.data(); *w = image.w; *h = image.h; *x = r.x; *y = r.y; *dw = image.w; *dh = image.h;
    return 1;
}
}

// Menu prompts ("X : Select", "O : Help", "/\ : Backup"): the game draws each
// button as a sprite from one 15-bit strip at VRAM 512,368: Square, Triangle,
// Circle, Cross, Up/Down, Left/Right, 26 texels apart, each 14x10 (Up/Down
// 14x12). The renderer (gpu_presentation.py prompt_glyphs) asks for an HD image
// of the strip and draws the sprites of recognised icons from it: here,
// Player 1's keys or controller buttons for the same buttons, drawn like the
// Controllers screen's. Each icon is recognised on its own (its fingerprint,
// SLUS-01053; no image data stored): after a race the gaps between them hold
// leftover race textures and Left/Right is partly overwritten, while the other
// icons are intact (the WRECKED! / results screens' "X : Continue").
namespace {
const uint64_t kPromptCells[6] = {0x47597d134b0256bdull, 0x5030ea7a5f66fa3full, 0xeffe93c70ae8bbcbull,
                                  0x11125bd9605acab0ull, 0xfdd097cd9226f81dull, 0x283aae19edc80257ull};
int prompt_cells(const uint16_t* vram, int x, int y) {
    int mask = 0;
    for (int i = 0; i < 6; i++) {
        uint64_t hash = 14695981039346656037ull;
        for (int r = 0; r < (i == 4 ? 12 : 10); r++)
            for (int c = 0; c < 14; c++) { hash ^= vram[(size_t)(y + r) * 1024 + x + 26 * i + c]; hash *= 1099511628211ull; }
        if (hash == kPromptCells[i]) mask |= 1 << i;
        else if (std::getenv("RR_PROMPT_TRACE")) {
            static uint64_t reported[6];   // looked at again while unknown: report each new one once
            if (reported[i] != hash) std::fprintf(stderr, "rr prompts: unknown icon %d %016llx\n", i, (unsigned long long)hash);
            reported[i] = hash;
        }
    }
    return mask;
}
}
extern "C" unsigned rr_prompt_serial(void) { return prompt_serial.load(); }
// Which icons (bit per cell) are the game's own right now; cheap, for the
// renderer's periodic look while some are not.
extern "C" int rr_prompt_cells(const uint16_t* vram, int x, int y) { return prompt_cells(vram, x, y); }
// Returns the recognised icons (bit per cell) with out filled (w*s x h*s
// premultiplied RGBA, those cells drawn), 0 to keep the game's own icons, -1 to
// be asked again (pads not set up yet).
extern "C" int rr_prompt_build(const uint16_t* vram, int x, int y, int w, int h, uint8_t* out, int s) {
    if (!inited.load()) return -1;
    if (seats[0].device == NONE || w != 146 || h != 12 || s < 1) return 0;
    const int mask = prompt_cells(vram, x, y);
    if (!mask) return 0;
    Canvas c; c.w = w * s; c.h = h * s; c.px.assign((size_t)c.w * c.h, 0);
    const Device device = (Device)prompt_device.load();   // a controller seat: its last-used device
    const int face[4] = {4, 6, 5, 7};   // the game's button list: Square, Triangle, Circle, Cross
    for (int i = 0; i < 6; i++) {
        if (!(mask >> i & 1)) continue;
        std::vector<Token> tokens;
        if (i < 4) tokens.push_back(binding(device, 0, face[i]));
        else if (device == GAMEPAD) tokens.push_back({DPAD, "", i == 4 ? 4 : 5});
        else { tokens.push_back(binding(device, 0, i == 4 ? 2 : 0)); tokens.push_back(binding(device, 0, i == 4 ? 3 : 1)); }
        const float cell = 14.0f * s;
        float H = 9.6f * s, gap = 0.1f * H, total = 0;
        for (auto& t : tokens) total += token_width(t, H) + gap;
        total -= gap;
        if (total > cell) { H *= cell / total; gap = 0.1f * H; total = cell; }
        float px = 26.0f * s * i + (cell - total) * 0.5f;
        const float cy = i == 4 ? 6.0f * s - 0.5f * s : 5.0f * s - 0.4f * s;   // centred on the game's sprite
        for (auto& t : tokens) { draw_token(c, t, px, cy, H); px += token_width(t, H) + gap; }
    }
    for (size_t i = 0; i < c.px.size(); i++) {
        const uint32_t p = c.px[i];
        out[i * 4] = (uint8_t)(p >> 16); out[i * 4 + 1] = (uint8_t)(p >> 8); out[i * 4 + 2] = (uint8_t)p; out[i * 4 + 3] = (uint8_t)(p >> 24);
    }
    psx_mod_counter_add("rr.controls.prompts_built", 1);
    return mask;
}
// ---------------------------------------------------------------------------
// Last-used device, and the prompts that name a button in words. The game's
// own lines are rewritten in RAM where they lie, never longer than the original
// (so the text after them is untouched); a new device rewrites them again.
namespace {
void use_device(Device d) {
    if (prompt_device.exchange(d) == d) return;
    prompt_serial.fetch_add(1);
    psx_mod_counter_add(d == KEYBOARD ? "rr.controls.prompts_to_keyboard" : "rr.controls.prompts_to_controller", 1);
}
std::string token_name(const Token& t, int style) {
    static const char* const arrows[] = {"Left", "Right", "Up", "Down"};
    static const char* const dpad[] = {"D-pad Left", "D-pad Right", "D-pad Up", "D-pad Down"};
    if (t.kind == KEY_ARROW) return arrows[t.dir & 3];
    if (t.kind == DPAD) return dpad[t.dir & 3];
    if (t.kind == STICK) return style != STYLE_XBOX ? (t.label == "L" ? "L3" : "R3") : (t.label == "L" ? "LS" : "RS");
    return t.label;
}
std::string button_name(int button) {
    const Device d = (Device)prompt_device.load();
    if (d == NONE || button < 0 || button > 14) return {};
    return token_name(binding(d, 0, button), pad_style[0].load());
}
// The game's lines, as the frontend (memory card, controller test) and the race
// results hold them, with the button each names (the game's button list).
struct PromptString { const char* text; int button; };
const PromptString kPromptStrings[] = {
    {"Press START To Continue.", 12}, {"Press X To Continue", 7}, {"Press X to Continue", 7}, {"Press X to Create File", 7},
    {"Press X To Overwrite", 7}, {"Press X to Begin", 7}, {"Press X to Select", 7}, {"Press Circle To Format", 5},
    {"Press Triangle To Cancel", 6}};
constexpr size_t kPromptMax = 32;
struct PromptSlot { uint32_t addr; int index; unsigned serial; char written[kPromptMax]; };
PromptSlot prompt_slots[16];
// "Press <name> <rest>" within the original's length, else "<name> <rest>".
// START is the game's only capitalised button word; key names follow it there.
std::string compose(const PromptString& p, std::string name) {
    const std::string text = p.text;
    const size_t end = text.find(' ', 6);
    const std::string word = text.substr(6, end - 6), rest = text.substr(end);
    if (word.size() > 1 && word.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos)
        for (char& ch : name) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    std::string s = "Press " + name + rest;
    if (s.size() > text.size()) s = name + rest;
    if (s.size() > text.size()) s.resize(text.size());
    return s;
}
bool prompt_ram(uint32_t a, size_t n) {
    const uint32_t o = a & 0x1FFFFFFFu;
    return ((a >= 0x80000000u && a < 0x80200000u) || a < 0x200000u) && o + n <= 0x200000u;
}
void prompt_write(PromptSlot& slot) {
    const PromptString& p = kPromptStrings[slot.index];
    const std::string s = compose(p, button_name(p.button));
    char* r = (char*)memory_get_ram_ptr() + (slot.addr & 0x1FFFFF);
    const size_t room = std::strlen(p.text);
    std::memset(r, 0, room + 1);
    std::memcpy(r, s.data(), std::min(room, s.size()));
    std::memset(slot.written, 0, sizeof slot.written);
    std::memcpy(slot.written, r, room + 1);
    slot.serial = prompt_serial.load();
    psx_mod_counter_add("rr.controls.prompt_texts", 1);
}
}

namespace rrpc {
void controls_event(const void* e) {
    if (!e || !inited.load()) return;
    const SDL_Event& ev = *(const SDL_Event*)e;
    // The kind of pad each seat holds: on (un)plugging, and the pad last used.
    if (ev.type == SDL_EVENT_GAMEPAD_ADDED || ev.type == SDL_EVENT_GAMEPAD_REMOVED) { styles_from_list(); return; }
    if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) style_from_use(ev.gbutton.which);
    else if (ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (ev.gaxis.value > 16000 || ev.gaxis.value < -16000)) style_from_use(ev.gaxis.which);
    if (seats[0].device != GAMEPAD) return;   // a keyboard seat has only the keyboard
    switch (ev.type) {
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = ev.key.key;
        // Window switching (Alt, Windows key, Alt+Tab) is not playing on the keyboard.
        if (ev.key.repeat || k == SDLK_LALT || k == SDLK_RALT || k == SDLK_LGUI || k == SDLK_RGUI || (ev.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI))) return;
        use_device(KEYBOARD);
        return;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: use_device(GAMEPAD); return;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: if (ev.gaxis.value > 16000 || ev.gaxis.value < -16000) use_device(GAMEPAD); return;
    default: return;
    }
}
void controls_prompt_text(uint32_t text) {
    if (!inited.load() || seats[0].device == NONE || !prompt_ram(text, kPromptMax)) return;
    const char* r = (const char*)memory_get_ram_ptr() + (text & 0x1FFFFF);
    for (PromptSlot& slot : prompt_slots) {
        if (!slot.addr || slot.addr != text) continue;
        const PromptString& p = kPromptStrings[slot.index];
        const size_t room = std::strlen(p.text);
        if (!std::memcmp(r, slot.written, room + 1)) { if (slot.serial != prompt_serial.load()) prompt_write(slot); return; }
        if (!std::memcmp(r, p.text, room + 1)) { prompt_write(slot); return; }   // reloaded with the overlay
        slot.addr = 0;   // something else lives here now
    }
    if (std::memcmp(r, "Press ", 6)) return;
    for (int i = 0; i < (int)(sizeof kPromptStrings / sizeof kPromptStrings[0]); i++) {
        const size_t room = std::strlen(kPromptStrings[i].text);
        if (std::memcmp(r, kPromptStrings[i].text, room + 1)) continue;
        PromptSlot* slot = nullptr;
        for (PromptSlot& s : prompt_slots) if (!s.addr) { slot = &s; break; }
        if (!slot) slot = &prompt_slots[text >> 4 & 15];
        slot->addr = text; slot->index = i;
        prompt_write(*slot);
        return;
    }
}
const char* controls_button_name(int button) {
    static thread_local std::string name;
    name = inited.load() ? button_name(button) : std::string();
    if (name.empty()) {
        static const char* const ps[] = {"Left", "Right", "Up", "Down", "Square", "Circle", "Triangle", "X", "L1", "L2", "R1", "R2", "Start", "Select", "L3"};
        name = button >= 0 && button <= 14 ? ps[button] : "?";
    }
    return name.c_str();
}
}