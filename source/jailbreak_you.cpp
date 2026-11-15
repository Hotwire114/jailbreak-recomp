/* Jail Break "YOU" alias, LGPL-2.1-or-later. Independently authored; it moves
 * the game's own menu data and one rider byte, and contains no game code or assets.
 *
 * The Jail Break page (frontend page struct 0x800990DC: +4 the lit record, +8
 * the record count, +0x10 the record table, stock 0x8008AD84 with 19 records
 * of 120 bytes) has the rows Continue / Gang / Alias (records 2-4). The Alias
 * row is a menu list per gang: descriptor {count 3, action, handler, selected}
 * + list (DeSades 0x80081794 / 0x80081770, Kaffe 0x800817C0 / 0x8008179C) of
 * 12-byte entries {2, alias value, flags, 0x3F, 01 00 07 00, widget}. The
 * handler stores the value in Player 1's settings (0x800D81D8: +9 gang, +0x0A
 * alias) at once.
 *
 * YOU is a fourth entry in a copy of each list. Its value is 0 (Switch /
 * Chops), so the memory card only ever holds a stock alias; that the campaign
 * is the YOU one is kept on the PC ([jailbreak] in launcher.ini: you_desades,
 * you_kaffe, weapon). Off the page the Load list and Rap Sheet name it after
 * the player's rider (lend_names). Its widget is a copy of Switch's, whose six strings name our text while YOU is shown (lend_strings), and whose
 * picture is the chosen gang's own (GNGD DeSades / GNGK Kaffe Boys, the Gang
 * row's pictures).
 *
 * With YOU selected the page uses a 20-record copy of its table: a Weapon row
 * (a copy of the Alias record 20 lower; the game's own cursor reaches it, lights
 * it and draws its arrows, test 15 Nov) and the description boxes 14 lower.
 * Left / Right there pick the starting weapon (jail_pad keeps them from the
 * game). Any stock alias: the stock table, nothing else changes.
 *
 * The chosen weapon (0-8 by HUD icon, 9 none) joins the campaign's own weapon
 * list (Player 1 settings +0x0C, a bit per weapon: the Rap Sheet's Current
 * Weapons, saved on the card) once, as a new YOU campaign leaves the page by
 * Continue for the next page (0x800990F0) with that list still empty. From then
 * on it is the game's: fists first in a race, Triangle switches, weapons taken
 * from riders are kept and saved. Names, Rider look and taunt voice apply only to that rider
 * (jail_stock_seat: rider_enhancements.cpp, taunt_voices.cpp).
 *
 * Guest RAM 0x801FB440-0x801FC000 holds the table, lists, widget and strings:
 * zero in menus and races (RAM scan, 14 Nov), below the demo courses' block.
 * Everything is put back as the page is left.
 */
#include "jailbreak_you.h"
#include "rider_enhancements.h"
#include "netplay_session.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
extern "C" uint8_t* memory_get_ram_ptr(void);
namespace {
constexpr uint32_t PAGE = 0x800990DC, PAGE_SEL = PAGE + 4, PAGE_COUNT = PAGE + 8, PAGE_TABLE = PAGE + 0x10, CURRENT_PAGE = 0x8009C5C8;
constexpr uint32_t STOCK_TABLE = 0x8008AD84, REC = 120;
constexpr unsigned STOCK_RECS = 19, OUR_RECS = 20, WEAPON_REC = 5;
constexpr uint32_t DESC[2] = {0x80081794, 0x800817C0}, LIST3[2] = {0x80081770, 0x8008179C};
constexpr uint32_t SWITCH_WIDGET = 0x800812D8, WIDGET_SIZE = 0xC4;
constexpr uint32_t SETTINGS = 0x800D81D8, CFG = 0x8005B2F8, TABLE_PTR = 0x8005AC8C + 0x8B8, VBL = 0x8005B46C;
constexpr uint32_t WEAPONS = SETTINGS + 0x0C, NEXT_PAGE = 0x800990F0;   // Player 1's weapon bits; Race / Course / Bike / Rap Sheet
// Player 1's race rider record (actor + 0x43C): +0x2C the weapons carried (a bit
// per weapon; 0x200 fists, set by the race's rider setup), +0x2E the one in hand.
constexpr uint32_t P1_CARRIED = 0x800D5758 + 0x2C;
constexpr uint32_t TABLE = 0x801FB440, LISTS = TABLE + OUR_RECS * REC, WIDGET = LISTS + 96, STRINGS = WIDGET + WIDGET_SIZE, STRINGS_END = 0x801FC000;
constexpr int ROW_Y = 110;   // the Weapon row's label (Alias 90 + 20)
constexpr uint16_t UP = 1 << 4, RIGHT = 1 << 5, DOWN = 1 << 6, LEFT = 1 << 7, CROSS = 1 << 14;
// Starting weapons: the held-weapon values 0-8 (HUD icons), then none (9).
const char* const weapon_names[10] = {"Chain", "Bat", "Billy club", "2x4", "Nunchucks", "Crowbar", "Cattle prod", "Taser", "Mace", "None"};

uint8_t* ram() {return memory_get_ram_ptr();}
bool in_ram(uint32_t p, uint32_t n) {return p >= 0x80000000u && p < 0x80200000u && n <= 0x80200000u - p;}
uint32_t word(uint32_t p) {uint32_t v; std::memcpy(&v, ram() + (p & 0x1fffff), 4); return v;}
uint16_t half(uint32_t p) {uint16_t v; std::memcpy(&v, ram() + (p & 0x1fffff), 2); return v;}
uint8_t byte_at(uint32_t p) {return ram()[p & 0x1fffff];}
void put_bytes(uint32_t p, const void* src, size_t n) {if (std::memcmp(ram() + (p & 0x1fffff), src, n)) std::memcpy(ram() + (p & 0x1fffff), src, n);}
bool frontend_overlay() {return word(0x80069D98) == 0x25058C84u;}   // as rider_enhancements.cpp
bool racing() {uint32_t c = word(CFG); return in_ram(c, 0x40) && byte_at(c) == 1;}
bool jail_mode() {uint32_t c = word(CFG); return in_ram(c, 8) && (byte_at(c + 4) & 0x20);}
bool on_page() {return frontend_overlay() && word(CURRENT_PAGE) == PAGE;}

// PC-side choice: launcher.ini [jailbreak].
// you[g]: the gang's Switch / Chops campaign is the YOU one (one flag per gang,
// so starting another campaign keeps it). 1.2.2 test builds kept you + gang.
struct Choice {bool you[2] = {false, false}; int weapon = 0;};
Choice saved; bool loaded; wchar_t ini[32768];
void load() {
    if (loaded) return; loaded = true;
    static wchar_t path[32768];   // static: this runs on the guest's small stack (VBlank)
    DWORD n = GetModuleFileNameW(nullptr, path, 32768); if (!n || n >= 32768) return;
    wchar_t* slash = wcsrchr(path, L'\\'); if (!slash) return; wcscpy(slash + 1, L"..\\launcher.ini");
    if (!GetFullPathNameW(path, 32768, ini, nullptr)) {ini[0] = 0; return;}
    const bool old = GetPrivateProfileIntW(L"jailbreak", L"you", 0, ini) != 0;
    const int old_gang = GetPrivateProfileIntW(L"jailbreak", L"gang", 0, ini) ? 1 : 0;
    for (int g = 0; g < 2; g++) saved.you[g] = GetPrivateProfileIntW(L"jailbreak", g ? L"you_kaffe" : L"you_desades", old && old_gang == g, ini) != 0;
    saved.weapon = GetPrivateProfileIntW(L"jailbreak", L"weapon", 0, ini);
    if (saved.weapon < 0 || saved.weapon > 9) saved.weapon = 0;
}
void store(const Choice& c) {
    if (!ini[0] || (c.you[0] == saved.you[0] && c.you[1] == saved.you[1] && c.weapon == saved.weapon)) return;
    wchar_t v[8];
    swprintf(v, 8, L"%d", c.you[0] ? 1 : 0); WritePrivateProfileStringW(L"jailbreak", L"you_desades", v, ini);
    swprintf(v, 8, L"%d", c.you[1] ? 1 : 0); WritePrivateProfileStringW(L"jailbreak", L"you_kaffe", v, ini);
    WritePrivateProfileStringW(L"jailbreak", L"you", nullptr, ini); WritePrivateProfileStringW(L"jailbreak", L"gang", nullptr, ini);
    swprintf(v, 8, L"%d", c.weapon); WritePrivateProfileStringW(L"jailbreak", L"weapon", v, ini);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini);
    saved = c; psx_mod_counter_add("rr.jail.saved", 1);
}

// The frontend's string table: id 52 "Alias" proves it.
uint32_t string_table() {
    uint32_t t = word(TABLE_PTR);
    if (!in_ram(t, 4 * 300)) return 0;
    uint32_t a = word(t + 4 * 52);
    return in_ram(a, 6) && !std::memcmp(ram() + (a & 0x1fffff), "Alias", 6) ? t : 0;
}

// Menu data.
bool lists_ours[2], table_ours;
uint32_t text_at[7];          // our strings: name, description, four panel lines, the Weapon label
uint32_t alias_text, switch_text[6];   // the game's: "Alias", Switch's six strings
uint32_t lent_table;          // the string table whose ids 258-263 point at our text, or 0
uint32_t last_page_vbl; bool page_was;   // page entry
int weapon;                   // 0-9, as weapon_names
uint16_t mask;

int gang_now() {return byte_at(SETTINGS + 9) ? 1 : 0;}
bool lists_pristine(int g) {return (word(DESC[g]) & 0x00FFFFFFu) == (0x00000203u | (uint32_t)(g + 1) << 16) && word(DESC[g] + 4) == LIST3[g];}
bool lists_patched(int g) {return (word(DESC[g]) & 0x00FFFFFFu) == (0x00000204u | (uint32_t)(g + 1) << 16) && word(DESC[g] + 4) == LISTS + 48 * g;}
int alias_sel(int g) {return byte_at(DESC[g] + 3);}
bool you_selected() {const int g = gang_now(); return lists_patched(g) && alias_sel(g) == 3;}

void build_lists() {
    put_bytes(WIDGET, ram() + (SWITCH_WIDGET & 0x1fffff), WIDGET_SIZE);
    for (int g = 0; g < 2; g++) {
        if (lists_patched(g)) {lists_ours[g] = true; continue;}
        if (!lists_pristine(g)) continue;
        const uint32_t l = LISTS + 48 * g;
        put_bytes(l, ram() + (LIST3[g] & 0x1fffff), 36);
        uint8_t e[12]; std::memcpy(e, ram() + (LIST3[g] & 0x1fffff), 12);
        e[1] = 0; e[2] = 0; const uint32_t w = WIDGET; std::memcpy(e + 8, &w, 4);
        put_bytes(l + 36, e, 12);
        psx_mod_write_word(DESC[g] + 4, l); psx_mod_write_byte(DESC[g], 4);
        lists_ours[g] = true; psx_mod_counter_add("rr.jail.lists_built", 1);
    }
}
void drop_lists() {
    for (int g = 0; g < 2; g++) {
        if (!lists_ours[g]) continue; lists_ours[g] = false;
        if (!lists_patched(g)) continue;
        if (alias_sel(g) == 3) psx_mod_write_byte(DESC[g] + 3, 0);   // YOU is Switch / Chops to the game
        psx_mod_write_word(DESC[g] + 4, LIST3[g]); psx_mod_write_byte(DESC[g], 3);
    }
}
// The stock table with its row records as the game lit them.
bool stock_table_ok() {return half(STOCK_TABLE + 4 * REC + 8) == 0x0D && half(STOCK_TABLE + 4 * REC + 20) == 52 && half(STOCK_TABLE + 2 * REC + 20) == 49;}
void use_table(bool ours) {
    const uint32_t now = word(PAGE_TABLE);
    if (ours) {
        if (now == TABLE && (half(PAGE_COUNT)) == OUR_RECS) return;
        if (now != STOCK_TABLE || half(PAGE_COUNT) != STOCK_RECS || !stock_table_ok()) return;
        static uint8_t t[OUR_RECS * REC]; const uint8_t* s = ram() + (STOCK_TABLE & 0x1fffff);
        std::memcpy(t, s, 5 * REC); std::memcpy(t + 6 * REC, s + 5 * REC, (STOCK_RECS - 5) * REC);
        uint8_t* row = t + WEAPON_REC * REC; std::memcpy(row, s + 4 * REC, REC);
        auto add = [](uint8_t* r, unsigned off, int d) {int16_t v; std::memcpy(&v, r + off, 2); v = (int16_t)(v + d); std::memcpy(r + off, &v, 2);};
        for (unsigned off : {38u, 50u, 62u, 86u}) add(row, off, 20);   // label, bar, both arrows
        std::memcpy(row + 40, "BTNN", 4);
        for (unsigned rec : {5u, 6u, 8u}) add(t + (rec + 1) * REC, 18, 14);   // the descriptions under the rows
        put_bytes(TABLE, t, sizeof t);
        psx_mod_write_word(PAGE_TABLE, TABLE); psx_mod_write_half(PAGE_COUNT, OUR_RECS); table_ours = true;
        psx_mod_counter_add("rr.jail.table_in", 1);
    } else {
        if (!table_ours) return; table_ours = false;
        if (now != TABLE) return;
        for (unsigned rec = 2; rec < 5; rec++) put_bytes(STOCK_TABLE + rec * REC + 40, ram() + ((TABLE + rec * REC + 40) & 0x1fffff), 4);
        if (half(PAGE_SEL) == WEAPON_REC) psx_mod_write_half(PAGE_SEL, 4);
        psx_mod_write_word(PAGE_TABLE, STOCK_TABLE); psx_mod_write_half(PAGE_COUNT, STOCK_RECS);
    }
}
// Switch's six string ids (258-263) name YOU's text while the YOU panel is
// shown: the game word-wraps the description from the table entry itself.
void lend_strings(uint32_t t, bool on) {
    if (lent_table && (!on || lent_table != t)) {
        for (int i = 0; i < 6; i++) if (in_ram(lent_table + 4 * (258 + i), 4) && word(lent_table + 4 * (258 + i)) == text_at[i]) psx_mod_write_word(lent_table + 4 * (258 + i), switch_text[i]);
        lent_table = 0;
    }
    if (!on || !t) return;
    if (!lent_table) {
        const uint32_t s = word(t + 4 * 258);
        if (!in_ram(s, 7) || std::memcmp(ram() + (s & 0x1fffff), "Switch", 7)) return;   // only from the game's own entries
        for (int i = 0; i < 6; i++) switch_text[i] = word(t + 4 * (258 + i));
        lent_table = t; psx_mod_counter_add("rr.jail.strings_lent", 1);
    }
    for (int i = 0; i < 6; i++) if (text_at[i] && word(t + 4 * (258 + i)) != text_at[i]) psx_mod_write_word(t + 4 * (258 + i), text_at[i]);
}
// Off the Jail Break page (the Load list, the Rap Sheet) Switch's / Chops's
// name (ids 258 / 276) reads as the player's rider name for a gang whose
// campaign is the YOU one. Its text sits at STRINGS, which the page's own
// strings use only on the page (the names are put back before them).
uint32_t name_lent[2], name_stock[2];   // the table lent in, the game's pointer
void lend_names(uint32_t t, bool on) {
    static const char* const stock[2] = {"Switch", "Chops"};
    for (int g = 0; g < 2; g++) {
        const uint32_t id = g ? 276 : 258, mine = STRINGS + 32 * g;
        if (name_lent[g] && (!on || !saved.you[g] || name_lent[g] != t)) {
            if (in_ram(name_lent[g] + 4 * id, 4) && word(name_lent[g] + 4 * id) == mine) psx_mod_write_word(name_lent[g] + 4 * id, name_stock[g]);
            name_lent[g] = 0;
        }
        if (!on || !t || !saved.you[g]) continue;
        if (!name_lent[g]) {
            const uint32_t s = word(t + 4 * id); const size_t n = std::strlen(stock[g]) + 1;
            if (!in_ram(s, (uint32_t)n) || std::memcmp(ram() + (s & 0x1fffff), stock[g], n)) continue;
            name_stock[g] = s; name_lent[g] = t; psx_mod_counter_add("rr.jail.names_lent", 1);
        }
        const auto& o = rrpc::rider_options();
        char name[32]; std::snprintf(name, sizeof name, "%s", o.name[0][0] ? o.name[0] : "YOU");
        put_bytes(mine, name, std::strlen(name) + 1);
        if (word(t + 4 * id) != mine) psx_mod_write_word(t + 4 * id, mine);
    }
}
void place_strings() {
    const auto& o = rrpc::rider_options();
    char lines[7][128];
    if (o.name[0][0]) std::snprintf(lines[0], 128, "YOU - %s", o.name[0]); else std::strcpy(lines[0], "YOU");
    std::strcpy(lines[1], "Fresh outta lockup with no rep. Change your look in Options > User settings > Rider look.");
    std::strcpy(lines[2], "Height:       ???");
    std::strcpy(lines[3], "Weight:      ???");
    std::snprintf(lines[4], 128, "Weapon:    %s", weapon_names[weapon]);
    std::strcpy(lines[5], "Colors:       Rider look");
    std::strcpy(lines[6], "Weapon");
    uint32_t at = STRINGS;
    for (int i = 0; i < 7; i++) {
        const size_t n = std::strlen(lines[i]) + 1;
        if (at + n > STRINGS_END) {text_at[i] = 0; continue;}
        put_bytes(at, lines[i], n); text_at[i] = at; at = (at + (uint32_t)n + 3) & ~3u;
    }
}
}  // namespace

namespace rrpc {
bool jail_you_active() {
    if (netplay()) return false;
    load();
    return saved.you[gang_now()] && byte_at(SETTINGS + 0x0A) == 0;
}
bool jail_stock_seat(unsigned seat) {
    if (netplay()) return false;
    return seat != 0 || !jail_you_active();
}

void jail_tick() {
    if (netplay()) return;
    load();
    // Continue with YOU lit arms the starting weapon; the next page takes it.
    static bool armed; static int armed_weapon; static uint32_t left_at;
    const uint32_t now = word(VBL);
    // The race: the game copies the carried weapons back into the list as a race
    // ends (frontend 0x8007BC20) and from it as a Jail Break race starts
    // (0x80063FA0), but the first race of a new campaign starts from its rider
    // setup alone (fists + the alias's own weapon), which would drop the list's
    // weapons at its end. So a YOU campaign's rider carries exactly the list's
    // weapons (not Switch's / Chops's own), once per race; the game does the rest
    // (fists in hand, Triangle switches, finish with a weapon and it is kept).
    static bool was_racing, carried; static uint32_t race_start;
    const bool r = racing();
    if (r && !was_racing) {carried = false; race_start = now;}
    was_racing = r;
    if (r && !carried && jail_mode() && now - race_start < 600u) {
        const uint16_t have = half(P1_CARRIED);
        if (!jail_you_active()) carried = true;
        else if ((have & 0x200) && now - race_start > 30u) {   // the rider setup has run
            const uint16_t want = (uint16_t)((have & ~0x1FFu) | (half(WEAPONS) & 0x1FFu));
            if (want != have) {psx_mod_write_half(P1_CARRIED, want); psx_mod_counter_add("rr.jail.weapons_carried", 1);}
            carried = true;
        }
    }
    if (!on_page() && armed) {
        if (frontend_overlay() && word(CURRENT_PAGE) == NEXT_PAGE) {
            armed = false;
            if (armed_weapon < 9 && half(WEAPONS) == 0 && jail_you_active()) {psx_mod_write_half(WEAPONS, (uint16_t)(1u << armed_weapon)); psx_mod_counter_add("rr.jail.weapon_given", 1);}
        } else if (!frontend_overlay() || now - left_at > 300u) armed = false;
    }
    if (!on_page()) {
        if (page_was && frontend_overlay()) {use_table(false); drop_lists(); lend_strings(0, false);}
        else if (!frontend_overlay()) {table_ours = false; lists_ours[0] = lists_ours[1] = false; lent_table = 0; name_lent[0] = name_lent[1] = 0;}   // overlay gone: its data with it
        page_was = false; mask = 0;
        if (frontend_overlay()) lend_names(string_table(), true);
        return;
    }
    const uint32_t t = string_table();
    if (!t) return;
    lend_names(t, false);   // the page shows the game's own names
    alias_text = word(t + 4 * 52);
    const bool entering = !page_was; page_was = true;
    static bool seen[2];
    if (entering) {weapon = saved.weapon; seen[0] = seen[1] = false;}
    build_lists();
    const int g = gang_now();
    put_bytes(WIDGET + 0x3C, g ? "GNGK" : "GNGD", 4);   // the picture: the gang's own
    // A gang with a YOU campaign shows YOU selected, as it was, the first time
    // it is shown on this visit (so moving to Switch / Chops is a choice).
    if (!seen[g] && lists_patched(g)) {
        seen[g] = true;
        if (saved.you[g] && alias_sel(g) == 0) {psx_mod_write_byte(DESC[g] + 3, 3); psx_mod_counter_add("rr.jail.reselected", 1);}
    }
    const bool you = you_selected();
    use_table(you);
    place_strings();
    const unsigned lit = half(PAGE_SEL);
    armed = you && lit == 2; armed_weapon = weapon; left_at = now;
    lend_strings(t, you && (lit == 4 || lit == WEAPON_REC));   // the Continue / Gang panels name the saved campaign
    if (!entering && seen[g] && lists_patched(g)) {
        // YOU sets the gang's flag, Switch / Chops clears it, Stacks etc. leave it.
        Choice c = saved; c.weapon = weapon;
        if (you) c.you[g] = true; else if (alias_sel(g) == 0) c.you[g] = false;
        store(c);
    }
}

void jail_text(CPUState* c) {
    if (!c || netplay() || !on_page() || !you_selected()) return;
    const uint32_t a1 = c->gpr[5];
    if (a1 == alias_text && table_ours && c->gpr[7] == (uint32_t)ROW_Y && text_at[6]) {c->gpr[5] = text_at[6]; return;}
}

uint16_t jail_pad(uint16_t buttons) {
    static uint16_t last = 0xffff;
    const uint16_t pressed = (uint16_t)(last & ~buttons); last = buttons;
    if (netplay() || !on_page() || !table_ours || half(PAGE_SEL) != WEAPON_REC) {mask &= (uint16_t)~buttons; return (uint16_t)(buttons | mask);}
    if (pressed & (LEFT | RIGHT)) {weapon = (weapon + ((pressed & RIGHT) ? 1 : 9)) % 10; psx_mod_counter_add("rr.jail.weapon_steps", 1);}
    mask |= (uint16_t)(pressed & (LEFT | RIGHT | CROSS));
    mask &= (uint16_t)~buttons;   // held until released
    (void)UP; (void)DOWN;
    return (uint16_t)(buttons | mask);
}
}  // namespace rrpc
