#pragma once
// A page of the in-game editor (F2, game_editor.cpp). Each page lives in its
// own source file and keeps its own state and lock; game_editor.cpp owns the
// window, the page selector, game-edits.ini, Save / Discard and the change log.
// LGPL-2.1-or-later.
#include <windows.h>
#include <cstdio>
#include <string>
namespace rrpc::editor {
struct Page {
    const wchar_t* title;                       // in the page selector
    const char* section;                        // its game-edits.ini section, e.g. "[race hud]"
    HWND (*create)(HWND parent, HFONT font);    // the page's child window (hidden until selected)
    void (*load_line)(const std::string& line); // a trimmed line of its section, while the file loads
    void (*loaded)();                           // file read: draft = saved
    int  (*unsaved)();                          // draft entries that differ from the saved file
    unsigned (*serial)();                       // bumps on every change the window should show
    void (*write)(FILE* f);                     // its section (header and draft lines)
    void (*log)(FILE* f, const char* stamp);    // one change-log line per unsaved change
    void (*commit)();                           // after a successful save: saved = draft
    void (*discard)();                          // draft = saved
};
const Page& race_hud_page();                     // hud_editor.cpp
const Page& gameplay_page();                     // gameplay_editor.cpp
// Reads game-edits.ini once (any thread). Pages whose values the game needs
// before the editor was ever opened call it.
void game_edits_ensure_loaded();
}
