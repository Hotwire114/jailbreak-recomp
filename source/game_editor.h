#pragma once
#include <cstddef>
#include <cstdint>
// In-game editor (F2): a window beside the game, editing game-edits.ini next to
// launcher.ini. Changes show in the game at once; Save keeps them and adds one
// dated line per change to game-edits-changes.log. LGPL-2.1-or-later.
struct CPUState;
namespace rrpc {
// PC menu text: replaces `text` (a built-in label, label prefix or help line,
// up to cap-1 characters) with the owner's text, and lists it in the editor
// under `where` (the menu page). Called by the PC menu (rider_menu.cpp).
void menu_text(char* text, size_t cap, const char* where);
// Game text, at the entry of the game's text routine 0x8002CDC8 (string in
// a1): lists the string under `where` (the screen; nullptr: leave this draw
// alone) and, if the owner changed it, points a1 at a copy of their text for
// this call. game_text_return(sp at the routine's entry) once it has drawn.
bool game_text_draw(CPUState* c, const char* where);
void game_text_return(uint32_t sp);
// A menu title's words for the HD banner (native_ui.cpp rr_banner_text): the
// owner's text, or `text`. The pointer stays valid.
const char* game_text_title(const char* text);
// F2: opens the editor window, or brings it to the front.
void game_editor_toggle();
}
