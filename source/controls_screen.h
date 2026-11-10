#pragma once
#include <cstdint>
namespace rrpc {
// Pad hook (both ports, every poll): presents controller seats as a DualShock
// whose ANALOG button the game's Controllers screen operates, and keeps each
// player's chosen configuration (launcher.ini). Returns the buttons to deliver.
uint16_t controls_pad(int slot, uint16_t buttons);
// Present hook (render thread): the PC bindings over the Controllers screen's
// button column, as one window-space image (rr_ui_preview's interface).
int controls_overlay(int ww, int wh, const uint32_t** pixels, int* w, int* h, int* x, int* y, int* dw, int* dh);
// SDL event hook (every event the game's window sees): Player 1's last-used
// device. A controller seat also takes the keyboard (keybinds are always live),
// so its menu prompts follow whichever the player touched last.
void controls_event(const void* sdl_event);
// Text routine hook (func_8002CDC8 entry, a1 = the string): rewrites the game's
// prompts that name a PlayStation button in words ("Press START To Continue.",
// "Press X To Continue", "Press Circle To Format") in place, within the
// original's length, with Player 1's key or controller button.
void controls_prompt_text(uint32_t text);
// Player 1's current name for a game button (the game's button list: 4 Square,
// 5 Circle, 6 Triangle, 7 Cross, 12 Start, 13 Select...), e.g. "Enter", "A",
// "View"; for our own prompt lines.
const char* controls_button_name(int button);
}
