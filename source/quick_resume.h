#pragma once
#include <cstddef>
namespace rrpc {
// Quick resume: player savestates taken from the in-game pause menu. They live
// in <savestate dir>/quick (saves/openbios/quick), apart from the developer
// slots in saves/openbios, and are loaded through the framework's own
// savestate path. Slots are 0-based here and shown as 1..quick_slots.
constexpr int quick_slots = 4;
// Stage a save / load of a slot. nullptr when staged; otherwise a short reason
// for the pause menu (refused in online play, outside a race, empty slot...).
// The caller closes the PC menu first so the snapshot holds the game's own
// pause-menu text.
const char* quick_save(int slot);
const char* quick_load(int slot);
// True while a quick save / load is staged or running.
bool quick_busy();
// One-line slot summary for the pause menu ("Downtown 14:05" / "Empty").
void quick_label(int slot, char* out, size_t cap);
// Every VBlank (pc_tick): runs staged requests, restores the developer slot
// folder afterwards, writes the slot's info file, and applies a launcher
// resume request (RR_QUICK_RESUME=<1..quick_slots>) once at start-up.
void quick_tick();
}
