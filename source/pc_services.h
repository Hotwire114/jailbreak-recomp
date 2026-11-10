#pragma once
#include <cstdint>
namespace rrpc {
struct GraphicsOptions { int resolution=1, filter=0, fullscreen=0,aspect=0,detail=0; bool discord=false; int texture_correction=0;   // gfx_options.h rr_tc_*: Off, On, Full
    int motion=0;   // motion: Frame rate 0 Original, 1 60 FPS, 2 120 FPS
    int aa=0, scanlines=0, draw=0;   // steps of gfx_options.h: anti-aliasing, scanlines, draw distance
    int post[4]={0,0,0,0};           // post effects (gfx_options.h rr_post_*): bloom, sharpen, colour, vignette
    int shadow=0; };                 // Shadow smoothing (gfx_options.h rr_shadow_*)
GraphicsOptions& graphics_options();
// Model detail steps ([display] model_detail): LOD switch distance in percent
// of the original game's. Shared with the launcher (launcher_skin.h).
int model_detail_percent(int step);
// Start-up movie skip: pad 1's buttons with Start + Cross taps while an intro
// movie streams (until the main menu first appears). Called by the pad hook.
uint16_t intro_skip_filter(uint16_t buttons);
// Display rows: 0 render, 1 filter, 2 window, 3 texture correction, 4 aspect,
// 5 frame rate, 6 anti-aliasing, 7 scanlines, 8 draw distance, 9-12 post
// effects (bloom, sharpening, colour, vignette), 13 shadow smoothing.
// dir: +1 next value, -1 previous (multi-value rows).
bool change_graphics(int item, int dir=1);
void pc_tick(bool racing, bool paused);
void personal_touch();
// Achievements (achievements.h). Description: up to two '\n'-separated lines;
// progress: one line such as "Wins: 1/3", empty when there is none.
int achievement_count();
const char* achievement_name(int index);
const char* achievement_description(int index);
bool achievement_unlocked(int index);
int achievements_unlocked();
void achievement_progress(int index,char*out,int cap);
bool reset_achievements();
unsigned racing_seconds();
bool discord_configured();
const char* discord_status();
bool toggle_discord();
}
