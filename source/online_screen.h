#pragma once
#include <cstdint>
union SDL_Event;
namespace rrpc {
// Online from the game's main menu (online_screen.cpp): the launcher's Online
// pages (rooms, codes, public lobby, chat) shown full screen in the game's
// style, through the link to the launcher that started the game (online_link.h).
void online_begin();                      // the main menu's Online row
bool online_open();                       // the screen is up: the game's pads are held released
bool online_event(const SDL_Event&);      // true: the event was the screen's
uint16_t online_pad(uint16_t buttons);    // Player 1's pad while open; returns what the game gets
void online_tick();                       // each event poll: the launcher's page, the picture
int online_overlay(int ww,int wh,const uint32_t**pixels,int*w,int*h,int*x,int*y,int*dw,int*dh);
}
