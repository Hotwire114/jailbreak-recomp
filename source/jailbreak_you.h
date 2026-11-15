#pragma once
#include <cstdint>
struct CPUState;
namespace rrpc {
// Jail Break "YOU" alias (jailbreak_you.cpp): a fourth Alias entry that rides as
// the player's own rider (Rider look, name, taunt voice) with a chosen starting
// weapon. Every stock alias plays exactly as the game has it.
void jail_tick();                  // once per VBlank (native_ui.cpp __wrap_gpu_vblank_tick)
void jail_text(CPUState* c);       // at the text routine's entry (rider_enhancements.cpp)
uint16_t jail_pad(uint16_t buttons);   // Player 1's pad (rider_menu.cpp): the Weapon row's Left / Right
bool jail_you_active();            // the Jail Break campaign in play is the YOU one
// True where a seat must stay stock: in a Jail Break race (or its frontend
// profile panels) for any rider but the YOU one. Never online.
bool jail_stock_seat(unsigned seat);
}
