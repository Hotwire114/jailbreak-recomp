#pragma once
#include <cstdint>
struct CPUState;
namespace rrpc {
struct NativeMenuView {bool open=false;int page=0,count=0,selected=0,art=0;char title[64]{},labels[8][32]{},help[4][32]{};};
void native_ui_config(bool enabled,bool hd);
void native_ui_menu(const NativeMenuView&);
// The Rider look preview (outfit sheet in the chosen colours, owner_assets.c) as a menu art index (9), or 2 if unavailable.
int look_preview_art(int outfit,int colour,int paint);
void native_ui_font(CPUState*);
void native_ui_return(CPUState*);
void native_ui_observe(uint32_t);
void native_ui_scene(bool racing,bool frontend,int aspect);
}
