#pragma once
#include <cstdint>
union SDL_Event;
namespace rrpc {
// Mouse in the game's menus (mouse_menus.cpp): pointing at a row moves the
// game's own cursor there with Up / Down presses, a left click selects it
// (Cross), a right click goes back (Triangle), the wheel steps Up / Down. The
// pointer is drawn in the menus' gold and hidden in races and when idle.
struct MouseRow{int x,y,w,h,id;};          // native screen pixels; id = the selection that lights the row
struct MouseRows{MouseRow row[16];int count=0,current=-1;bool centred=false;};   // centred: x from the 4:3 centre (pause panel)
bool menu_mouse_rows(MouseRows&);          // rider_menu.cpp: PC pages, main menu Quit row, pause menu
void mouse_event(const SDL_Event&);        // every SDL event (main thread)
void mouse_tick();                         // each event poll: pointer shown / hidden
uint16_t mouse_pad(uint16_t buttons);      // Player 1's pad, each poll: adds the mouse's presses
void mouse_menu_shown(bool over_row);      // another screen (online_screen.cpp) has the pointer: shown, lit over a row
}
