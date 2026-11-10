#pragma once
#include <cstdint>
namespace rrpc {void rider_menu_observe(uint32_t);}
struct CPUState;
namespace rrpc {
// paint / tag / outfit / colour: rider look presets per seat (owner_assets.h rr_look_*), 0 = original.
// original_rate / original_quit: Original game options (rider-options.ini
// [original], written by the launcher only for Original): frame rate 0 the
// game's own, 1 60 FPS, 2 120 FPS; Esc closes the game.
struct RiderOptions {bool names=false,large=false,menu=false,hd=true;int voice[2]{},paint[2]{},tag[2]{},outfit[2]{},colour[2]{};char name[2][9]{};int original_rate=0;bool original_quit=false;};
RiderOptions& rider_options();
bool save_rider_options(const RiderOptions&);
void look_tick();   // rider look in races (bike paint, outfit); call once per host frame
void unblink_players();   // every VBlank: no blue blink on the human riders in multiplayer races
void grid_tick();         // every VBlank: multiplayer races start from a shuffled grid
bool rider_menu_font(CPUState*);
bool rider_menu_drawing();
}
