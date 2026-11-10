#pragma once
#include <cstdint>
union SDL_Event;
namespace rrpc {
// Rider look showroom (showroom.cpp): User settings > Rider look on the main
// menu starts a quiet Time Trial, where the game itself draws the player's
// rider and bike while the camera circles them and a side panel changes the look.
bool showroom_begin();                 // false: not possible now (the menu page opens instead)
bool showroom_active();
bool showroom_mute();                  // behind the loading cover: no game sound starts (taunt_voices.cpp spu_write)
int showroom_seat();                   // the seat whose look the showroom rider wears; -1 when not showing
bool showroom_pad(int slot,uint16_t real,uint16_t&out);   // true: the game gets out instead
bool showroom_event(const SDL_Event&); // true: the key belonged to the showroom
void showroom_observe(uint32_t pc);
int showroom_overlay(int ww,int wh,const uint32_t**pixels,int*w,int*h,int*x,int*y,int*dw,int*dh);
// rider_menu.cpp: the PC menu closed with the game's own menu tables back, and
// opened again on User settings with Rider look selected.
void rider_menu_leave();
bool rider_menu_open_look();
}
