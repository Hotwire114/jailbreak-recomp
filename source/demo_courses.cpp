/* Restored demo courses, LGPL-2.1-or-later. Independently authored; it moves
 * the game's own menu data and race ids, and contains no game code or assets.
 *
 * The owner's disc carries six complete 1P courses that no menu offers: race
 * ids 32-37 (route blocks in ROADGRF1.TXT, RACE1_32..37.STP, CAR32A..37A.GEO,
 * GAMEBIN1 entries, STARTDFA grids, and beta "polaroid" loading cards in
 * FSLOAD.TCM). UNUSED-CONTENT.md has the evidence.
 *
 * Time Trial's Course row is a menu list descriptor at 0x80086FF8 (frontend
 * overlay data): {count 9, action 4, handler 8, selected index, list}. The
 * list holds 12-byte entries {4, race id, flags, rank mask, 01 00 07 00,
 * widget}; the handler stores entry[1] in the frontend's race id (0x800D80E0),
 * and the widget (0xC4 bytes) draws the course's six strings and picture.
 * Here the descriptor points at a 15-entry copy: the nine courses, then the six
 * demo courses (rank mask 0x3F: every rank). A demo entry carries White
 * Knuckle's id (56), because the game indexes its Time Trial record table by
 * race id - 56 without a bounds check; the demo course is recognised by the
 * selected index (descriptor +3 >= 9) and its id goes into the race config
 * (+0x40) only when the race is set up.
 *
 * Records: each demo course keeps its own eight-entry table (demo-records.bin
 * beside the runtime). It is swapped into White Knuckle's slot (0x80053A88,
 * 0xB0 bytes) while a demo race is set up, raced, finished (results and the
 * record-name keyboard) and while the Time Trial page shows a demo course, and
 * swapped out as soon as the page is left. Neither table ever replaces the other.
 *
 * Guest RAM 0x801FC000-0x801FCE00 holds the list, widgets and strings: zero in
 * the frontend (RAM scan, 17 Oct), below the stack's deepest use and the
 * game-text loans (sp - 8 KB). It is rebuilt whenever the frontend overlay is
 * loaded again; races may reuse it.
 */
#include "demo_courses.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include "netplay_session.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
extern "C" uint8_t* memory_get_ram_ptr(void);
namespace {
constexpr uint32_t DESC = 0x80086FF8, LIST9 = 0x80086F8C, MIRAGE_WIDGET = 0x800842A8, FE_RACE = 0x800D80E0;
constexpr uint32_t CFG = 0x8005B2F8, TABLE_PTR = 0x8005AC8C + 0x8B8, VBL = 0x8005B46C;
constexpr uint32_t LIST = 0x801FC000, WIDGETS = 0x801FC0C0, STRINGS = 0x801FC600, STRINGS_END = 0x801FCE00;
constexpr uint32_t RECORDS = 0x80053A88, RECORD_SIZE = 0xB0;   // White Knuckle's (race 56) table
constexpr int ALIAS = 56, FIRST = 32, COUNT = 6, WIDGET_SIZE = 0xC4;
// Widget elements carrying a string id: name, description, route, traffic, curves, miles.
constexpr unsigned STRING_SLOTS[6] = {0x04, 0x20, 0x58, 0x74, 0x90, 0xAC};
struct Course {const char* name; const char* text[5];};
// Names as written on the polaroid loading cards, marked (beta). The game's own strings for
// these ids are "Demo Course" and five empty lines; the unknown figures follow
// its own "???" style (Jailbreak, string 581-584).
const Course courses[COUNT] = {
    {"Meadows (beta)",      {"A beta course the original game never offered. One long straight run - open the throttle.", "??? to ???", "Traffic Density:  ???", "Curve Factor:  ???", "Miles:  ???"}},
    {"Countryside (beta)",  {"A beta course the original game never offered. Rolling farm roads, restored from the disc.", "??? to ???", "Traffic Density:  ???", "Curve Factor:  ???", "Miles:  ???"}},
    {"Alpine (beta)",       {"A beta course the original game never offered. Mountain roads, restored from the disc.", "??? to ???", "Traffic Density:  ???", "Curve Factor:  ???", "Miles:  ???"}},
    {"Rash City (beta)",    {"A beta course the original game never offered. City streets, restored from the disc.", "??? to ???", "Traffic Density:  ???", "Curve Factor:  ???", "Miles:  ???"}},
    {"Cactus Hills (beta)", {"A beta course the original game never offered. Desert heat, restored from the disc.", "??? to ???", "Traffic Density:  ???", "Curve Factor:  ???", "Miles:  ???"}},
    {"Coast (beta)",        {"A beta course the original game never offered. Down by the water, restored from the disc.", "??? to ???", "Traffic Density:  ???", "Curve Factor:  ???", "Miles:  ???"}},
};
uint8_t* ram() {return memory_get_ram_ptr();}
bool in_ram(uint32_t p, uint32_t n) {return p >= 0x80000000u && p < 0x80200000u && n <= 0x80200000u - p;}
uint32_t word(uint32_t p) {uint32_t v; std::memcpy(&v, ram() + (p & 0x1fffff), 4); return v;}
uint8_t byte_at(uint32_t p) {return ram()[p & 0x1fffff];}
void put_bytes(uint32_t p, const void* src, size_t n) {std::memcpy(ram() + (p & 0x1fffff), src, n);}
void put_word(uint32_t p, uint32_t v) {psx_mod_write_word(p, v);}
bool frontend_overlay() {return word(0x80069D98) == 0x25058C84u;}   // as rider_enhancements.cpp
bool race_overlay() {return word(0x800C6E98) == 0x24A500A2u && word(0x800C5F9C) == 0x240500A9u;}

// The frontend's string table (FESTRING.LOC): id 405 "Rush Hour" proves it.
uint32_t string_table() {
    uint32_t t = word(TABLE_PTR);
    if (!in_ram(t, 4 * 800)) return 0;
    uint32_t p = word(t + 4 * 405);
    return in_ram(p, 10) && !std::memcmp(ram() + (p & 0x1fffff), "Rush Hour", 10) ? t : 0;
}
uint32_t string_id(int demo, int slot) {return 405 + 6 * (FIRST + demo - 1) + slot;}
uint32_t string_addr[COUNT][6], alias_name;   // our copies in guest RAM, White Knuckle's name

// Records: the six tables, kept beside the runtime.
uint8_t records[COUNT][RECORD_SIZE], saved_slot[RECORD_SIZE];
bool records_loaded; int swapped = -1;
wchar_t records_path[MAX_PATH];
void default_records(uint8_t* r) {
    // Eight entries of {name[12], time (1/300 s), flags}; the first 16 bytes are
    // the best run's four splits. Generous times, so any finish sets a record.
    std::memset(r, 0, RECORD_SIZE);
    static const char* names[8] = {"Draco", "Helmut", "Natasha", "Zed", "Kat", "Bull", "Mongo", "Jork"};
    for (int i = 0; i < 8; i++) {
        uint8_t* e = r + 0x10 + i * 0x14; std::snprintf((char*)e, 12, "%s", names[i]);
        uint32_t t = (uint32_t)(300 * (240 + 20 * i)), flags = 0x00010101; std::memcpy(e + 12, &t, 4); std::memcpy(e + 16, &flags, 4);
    }
    uint32_t best; std::memcpy(&best, r + 0x1C, 4);
    for (int k = 0; k < 4; k++) {uint32_t s = best * (k + 1) / 4; std::memcpy(r + 4 * k, &s, 4);}
}
void load_records() {
    if (records_loaded) return; records_loaded = true;
    if (GetModuleFileNameW(nullptr, records_path, MAX_PATH)) {wchar_t* s = wcsrchr(records_path, L'\\'); if (s) wcscpy(s + 1, L"demo-records.bin"); else records_path[0] = 0;}
    for (auto& r : records) default_records(r);
    if (FILE* f = records_path[0] ? _wfopen(records_path, L"rb") : nullptr) {
        uint8_t buf[sizeof records]; if (std::fread(buf, 1, sizeof buf, f) == sizeof buf) std::memcpy(records, buf, sizeof buf); std::fclose(f);
    }
}
void save_records() {
    if (!records_path[0]) return;
    wchar_t tmp[MAX_PATH + 8]; wcscpy(tmp, records_path); wcscat(tmp, L".tmp");
    if (FILE* f = _wfopen(tmp, L"wb")) {
        bool ok = std::fwrite(records, 1, sizeof records, f) == sizeof records; ok = std::fclose(f) == 0 && ok;
        if (ok) MoveFileExW(tmp, records_path, MOVEFILE_REPLACE_EXISTING);
    }
}
// Put demo course d's table (or -1: White Knuckle's own) in the record slot.
void swap_to(int d) {
    if (d == swapped) return;
    if (swapped >= 0) {   // the demo table back out, with any new record
        std::memcpy(records[swapped], ram() + (RECORDS & 0x1fffff), RECORD_SIZE);
        put_bytes(RECORDS, saved_slot, RECORD_SIZE); save_records(); psx_mod_counter_add("rr.demo.records_out", 1);
    }
    swapped = -1;
    if (d >= 0) {
        std::memcpy(saved_slot, ram() + (RECORDS & 0x1fffff), RECORD_SIZE);
        put_bytes(RECORDS, records[d], RECORD_SIZE); swapped = d; psx_mod_counter_add("rr.demo.records_in", 1);
    }
}

// Menu data in guest RAM.
uint32_t page_seen;            // VBlank when the Time Trial page last drew its course name
uint32_t desc_seen;            // VBlank when a demo course's description last drew
std::atomic<int> preview{-1};
int came_from = -1;            // demo course of the race that ran, until the page is back
uint32_t trophy_seen;          // VBlank when a record name was drawn from the record slot
int chosen = -1;               // the demo course selected when the frontend was last seen
int forced = -1;               // the Rider look showroom's course (demo_showroom), whatever Time Trial shows
bool showroom_race;            // the race that ran was the showroom's: the Time Trial cursor stays put

// Descriptor bytes {count, action, handler, selected index}: the index is the top byte.
bool pristine() {return (word(DESC) & 0x00FFFFFFu) == 0x00080409u && word(DESC + 4) == LIST9;}
bool patched() {return (word(DESC) & 0x00FFFFFFu) == (0x00080400u | (9 + COUNT)) && word(DESC + 4) == LIST;}
void build_menu() {
    for (uint32_t a = LIST; a < STRINGS_END; a += 4) put_word(a, 0);
    put_bytes(LIST, ram() + (LIST9 & 0x1fffff), 9 * 12);
    for (int d = 0; d < COUNT; d++) {
        const uint32_t w = WIDGETS + d * WIDGET_SIZE, e = LIST + (9 + d) * 12;
        put_bytes(w, ram() + (MIRAGE_WIDGET & 0x1fffff), WIDGET_SIZE);
        for (int s = 0; s < 6; s++) {uint16_t id = (uint16_t)string_id(d, s); put_bytes(w + STRING_SLOTS[s], &id, 2);}
        const uint8_t head[8] = {4, (uint8_t)ALIAS, 0, 0x3F, 1, 0, 7, 0}; put_bytes(e, head, 8); put_word(e + 8, w);
    }
    const uint8_t count = 9 + COUNT; put_word(DESC + 4, LIST); put_bytes(DESC, &count, 1);
    psx_mod_counter_add("rr.demo.menu_built", 1);
}
// The six strings of each demo course, and the table pointing at them.
void place_strings(uint32_t table) {
    uint32_t at = STRINGS;
    for (int d = 0; d < COUNT; d++) for (int s = 0; s < 6; s++) {
        const char* text = s ? courses[d].text[s - 1] : courses[d].name; size_t n = std::strlen(text) + 1;
        if (at + n > STRINGS_END) return;
        if (std::memcmp(ram() + (at & 0x1fffff), text, n)) put_bytes(at, text, n);
        string_addr[d][s] = at;
        const uint32_t slot = table + 4 * string_id(d, s);
        if (word(slot) != at) put_word(slot, at);
        at = (at + (uint32_t)n + 3) & ~3u;
    }
    alias_name = word(table + 4 * (405 + 6 * (ALIAS - 1)));
}
int selected_demo() {
    if (!patched()) return -1;
    const int i = byte_at(DESC + 3);
    return i >= 9 && i < 9 + COUNT && (int)byte_at(FE_RACE) == ALIAS ? i - 9 : -1;
}

// The best run's splits are read by race id - 56 straight from the race
// config, not the frontend: three reads in the race-init overlay (the time to
// beat and first split, block 0x80060468/0x80060470 to the return 0x8006056c)
// and one per checkpoint in the race overlay (block 0x80063824 to the return
// 0x80063888). For a demo race the config shows 56 just for those blocks, so
// they read the demo course's table in White Knuckle's slot.
bool flipped; int flip_ticks;
bool init_reads() {return word(0x80060470) == 0x8C830040u && word(0x80060478) == 0x2463FFC8u && word(0x80060524) == 0x2463FFC8u;}
bool split_read() {return word(0x80063834) == 0x8C440040u && word(0x8006383C) == 0x2484FFC8u;}
void flip(bool on) {
    const uint32_t cfg = word(CFG);
    if (!in_ram(cfg, 0x44) || came_from < 0) {flipped = false; return;}
    if (on && word(cfg + 0x40) == (uint32_t)(FIRST + came_from)) {put_word(cfg + 0x40, ALIAS); flipped = true; flip_ticks = 0; psx_mod_counter_add("rr.demo.split_reads", 1);}
    else if (!on && flipped) {if (word(cfg + 0x40) == (uint32_t)ALIAS) put_word(cfg + 0x40, FIRST + came_from); flipped = false;}
}
}  // namespace

namespace rrpc {
void demo_observe(uint32_t pc) {
    if (came_from < 0) return;
    switch (pc) {
    case 0x80060468: case 0x80060470: if (init_reads()) flip(true); break;
    case 0x8006056C: if (init_reads()) flip(false); break;
    case 0x80063824: if (split_read()) flip(true); break;
    case 0x80063888: if (split_read()) flip(false); break;
    }
}
}  // namespace rrpc

namespace rrpc {
int demo_preview() {return preview.load();}
// The showroom's stage: the next Time Trial set up runs demo course d; -1 when
// it is over (White Knuckle's own records back, Time Trial as the player left it).
void demo_showroom(int d) {
    forced = d;
    if (d < 0 && showroom_race) {came_from = -1; showroom_race = false; swap_to(-1);}
}

void demo_tick() {
    if (netplay()) {preview.store(-1); return;}
    load_records();
    const uint32_t now = word(VBL), cfg = word(CFG);
    const bool cfg_ok = in_ram(cfg, 0x44);
    const uint32_t race_id = cfg_ok ? word(cfg + 0x40) : 0;
    const int demo_id = race_id >= (uint32_t)FIRST && race_id < (uint32_t)(FIRST + COUNT) ? (int)(race_id - FIRST) : -1;
    // The frontend's code decides: after a race, the race overlay's code is
    // still in RAM below the frontend (race_overlay() stays true in the menus).
    if (!frontend_overlay()) {
        // Race set-up, race, results: the demo course's own table in the slot.
        preview.store(-1);
        // The overlay holding the menu is replaced as soon as the frontend has
        // copied its race id (56): the id is set here, while the race overlays load.
        if (forced >= 0 && race_id >= (uint32_t)ALIAS && race_id < (uint32_t)(ALIAS + 9)) {put_word(cfg + 0x40, FIRST + forced); psx_mod_counter_add("rr.demo.showroom_ids", 1); came_from = forced; showroom_race = true; chosen = -1; swap_to(came_from); return;}
        if (race_id == (uint32_t)ALIAS && chosen >= 0 && !race_overlay()) {put_word(cfg + 0x40, FIRST + chosen); psx_mod_counter_add("rr.demo.race_ids", 1); came_from = chosen; chosen = -1; swap_to(came_from); return;}
        // (The config shows 56 inside the split-reading blocks, see flip.)
        if (flipped && ++flip_ticks > 2) flip(false);
        if (demo_id >= 0) came_from = demo_id;
        swap_to(came_from);
        return;
    }
    if (pristine()) build_menu();
    if (patched() && came_from >= 0 && !showroom_race && (int)byte_at(FE_RACE) == ALIAS && byte_at(DESC + 3) < 9) {
        // Back from a demo race: the cursor stays on that course.
        const uint8_t index = (uint8_t)(9 + came_from); put_bytes(DESC + 3, &index, 1); psx_mod_counter_add("rr.demo.reselected", 1);
    }
    if (uint32_t t = string_table()) place_strings(t);
    const int sel = selected_demo();
    if (patched()) chosen = sel;
    const bool page = now - page_seen < 30u, trophy = now - trophy_seen < 30u;
    // The frontend copies its race id (56) into the config when a race is set
    // up; the demo course's id replaces it (polled every VBlank; the race
    // overlay takes many frames to read, and only then uses the id).
    if (cfg_ok && sel >= 0 && race_id == (uint32_t)ALIAS) {put_word(cfg + 0x40, FIRST + sel); psx_mod_counter_add("rr.demo.race_ids", 1);}
    // The showroom's course the same way, whatever Time Trial course the config
    // holds (the set-up branch above may not see the frontend leave: after a race
    // the race overlay's code is still in RAM, so race_overlay() reads true).
    if (cfg_ok && forced >= 0 && race_id >= (uint32_t)ALIAS && race_id < (uint32_t)(ALIAS + 9)) {
        put_word(cfg + 0x40, FIRST + forced); psx_mod_counter_add("rr.demo.showroom_ids", 1); came_from = forced; showroom_race = true;
    }
    // Records in the frontend: after a demo race until the Time Trial page is
    // back (results, record-name keyboard), and while a record list of the
    // selected demo course is on screen (Trophy Room). Never anywhere else, so
    // a memory-card save always finds White Knuckle's own table.
    if (page) came_from = -1;
    swap_to(came_from >= 0 ? came_from : sel >= 0 && trophy ? sel : -1);
    preview.store(sel >= 0 && now - desc_seen < 8u ? sel : -1);
}
void demo_text(CPUState* c) {
    if (!c || netplay() || !frontend_overlay()) return;
    const uint32_t a1 = c->gpr[5], now = word(VBL);
    const int sel = selected_demo();
    if (alias_name && a1 == alias_name) {
        page_seen = now;
        // The page's summary line names the race id (White Knuckle for a demo entry).
        if (sel >= 0 && string_addr[sel][0]) c->gpr[5] = string_addr[sel][0];
        return;
    }
    if (a1 >= RECORDS + 0x10 && a1 < RECORDS + RECORD_SIZE) trophy_seen = now;   // a record name read from the slot
    for (int d = 0; d < COUNT; d++) {
        if (a1 == string_addr[d][0] || a1 == string_addr[d][1]) {desc_seen = now; page_seen = now;}   // the Course row's widget
    }
}
}  // namespace rrpc
