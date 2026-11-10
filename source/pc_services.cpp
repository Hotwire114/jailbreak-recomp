/* Built-in PC graphics, local achievements and optional presence.
 * LGPL-2.1-or-later. No original game code or assets. */
#include "pc_services.h"
#include "model_detail.h"
#include "gfx_options.h"
#include "discord_presence.h"
#include "custom_music.h"
#include "taunt_voices.h"
#include "mod_assets.h"
#include "rider_enhancements.h"
#include "native_ui.h"
#include "cheat_codes.h"
#include "mod_plugins.h"
#include "gpu_gl_renderer.h"
#include "gpu_render.h"
#include "gpu.h"
#include "pgxp.h"
#include "achievements.h"
#include "memcard.h"
#include "game_icon.h"
#include "netplay_session.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" uint8_t*memory_get_ram_ptr(void);
#include <cwchar>
#include <ctime>
extern "C" void gte_geometry_correction_set(int);
extern "C" int psx_debug_display_aspect(int,int,int);
extern "C" void psx_ws_set_native_wide(int);
extern "C" void gpu_texture_correction_set(int);
extern "C" void rr_fast_set_mode(int);
extern "C" void rr_fps120_set(int);
extern "C" void gr_set_texture_filter(int);
extern "C" void gl_renderer_rr_post_set(int,int,int,int);   // gpu_presentation.py post_effects
extern "C" void gl_renderer_rr_post_time(int);
extern "C" void gl_renderer_rr_shadow_set(int);   // gpu_presentation.py shadow_smoothing
// Launcher Display > Movies: Widescreen ([display] fmv_wide). Read once, on the
// first call, from the render thread's movie present (gpu_presentation.py);
// a function-local static is initialised thread-safely.
extern "C" int rr_fmv_wide(void){
    static const int on=[]{wchar_t path[32768],ini[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return 0;
        auto*slash=std::wcsrchr(path,L'\\');if(!slash)return 0;std::wcscpy(slash+1,L"..\\launcher.ini");GetFullPathNameW(path,32768,ini,nullptr);
        return GetPrivateProfileIntW(L"display",L"fmv_wide",0,ini)!=0?1:0;}();
    return on;
}
// Texture correction Full: the generated game code's PGXP value-tracking hooks
// test this before each call (fast_timing.py PGXP_GATED). Off and On leave it 0.
extern "C" int g_rr_pgxp_full=0;
// Full needs the split vertex-path copies (RR_PGXP_SPLIT build); otherwise Off and
// On only, and a saved Full reads as On.
#ifdef RR_PGXP_SPLIT
static constexpr int tc_steps=rr_tc_steps;
#else
static constexpr int tc_steps=2;
#endif
namespace rrpc{bool gameplay_active();}   // gameplay_editor.cpp
namespace {
rrpc::GraphicsOptions options; bool loaded,applied,dirty; wchar_t settings[32768],achievements[32768];char app_id[24];
// Achievements (achievements.h): race_ms counts the first 5 minutes (Road
// regular), total_ms all active race time; finishes, wins and modes as saved.
uint64_t last_tick,last_flush,race_ms,total_ms,unlock[RR_ACH_COUNT];int finishes,wins,modes;
bool write_int(const wchar_t*section,const wchar_t*key,int value,const wchar_t*path){wchar_t text[32];std::swprintf(text,32,L"%d",value);return WritePrivateProfileStringW(section,key,text,path)!=0;}
void flush(){
    if(!loaded||!dirty)return;bool ok=write_int(L"progress",L"active_race_ms",(int)race_ms,achievements);
    ok=write_int(L"progress",L"race_seconds",(int)(total_ms/1000),achievements)&&ok;ok=write_int(L"progress",L"finishes",finishes,achievements)&&ok;
    ok=write_int(L"progress",L"wins",wins,achievements)&&ok;ok=write_int(L"progress",L"modes",modes,achievements)&&ok;
    for(int i=0;i<RR_ACH_COUNT;i++){wchar_t key[16],value[32];std::swprintf(key,16,L"unlock%d",i);std::swprintf(value,32,L"%llu",(unsigned long long)unlock[i]);ok=WritePrivateProfileStringW(L"achievements",key,value,achievements)&&ok;}
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,achievements);if(ok)dirty=false;
}
void shutdown(){flush();rrpc::presence_stop();}
void load(){
    if(loaded)return;wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return;
    auto*slash=std::wcsrchr(path,L'\\');if(!slash)return;std::wcscpy(slash+1,L"..\\launcher.ini");GetFullPathNameW(path,32768,settings,nullptr);
    std::wcscpy(slash+1,L"..\\achievements.ini");GetFullPathNameW(path,32768,achievements,nullptr);
    options.resolution=GetPrivateProfileIntW(L"display",L"resolution",1,settings);if(options.resolution<0||options.resolution>4)options.resolution=1;
    options.aspect=GetPrivateProfileIntW(L"display",L"aspect",0,settings);if(options.aspect<0||options.aspect>2)options.aspect=0;
    options.filter=GetPrivateProfileIntW(L"display",L"filter",0,settings);if(options.filter<0||options.filter>=rr_filter_steps)options.filter=0;   // Sharp, Smooth, xBR
    options.fullscreen=GetPrivateProfileIntW(L"display",L"fullscreen",0,settings)!=0;
    options.discord=GetPrivateProfileIntW(L"discord",L"enabled",0,settings)!=0;
    // Native 60 FPS races (real frames from the game, not interpolation). New key:
    // the old smooth_motion setting meant frame generation and defaulted Off.
    // 0 Original, 1 60 FPS, 2 120 FPS (older builds read 2 as 60).
    options.motion=GetPrivateProfileIntW(L"display",L"native_60fps",1,settings);if(options.motion<0||options.motion>2)options.motion=1;
    options.detail=GetPrivateProfileIntW(L"display",L"model_detail",0,settings);if(options.detail<0||options.detail>=rr_detail_steps)options.detail=0;
    // Renderer options (gfx_options.h); out-of-range values mean the original.
    auto step=[](const wchar_t*key,int steps){int v=GetPrivateProfileIntW(L"display",key,0,settings);return v>=0&&v<steps?v:0;};
    options.texture_correction=rr_tc_load(step(L"texture_correction",rr_tc_steps));if(options.texture_correction>=tc_steps)options.texture_correction=0;   // Precise vertices: 0 Off, 2 On (Full); the old On (1) reads as Off
    options.aa=step(L"antialiasing",rr_aa_steps);options.scanlines=step(L"scanlines",rr_scanline_steps);options.draw=step(L"draw_distance",rr_draw_steps);
    for(int e=0;e<rr_post_effects;e++){wchar_t key[16];std::swprintf(key,16,L"%hs",rr_post_key(e));options.post[e]=step(key,rr_post_steps);}
    options.shadow=step(L"shadow",rr_shadow_steps);
    // Online: the host's settings that change game state (netplay_session.cpp);
    // texture correction (GTE geometry correction) off on both PCs.
    if(rrpc::netplay()){if(const auto*s=rrpc::net_session()){if(s->motion>=0)options.motion=s->motion;if(s->aspect>=0)options.aspect=s->aspect;if(s->draw>=0&&s->draw<rr_draw_steps)options.draw=s->draw;}
        if(options.motion==2)options.motion=1;options.texture_correction=0;}
    wchar_t id[64];GetPrivateProfileStringW(L"discord",L"application_id",L"",id,64,settings);
    size_t len=std::wcslen(id);bool valid=len>=17&&len<=20;for(size_t i=0;i<len;i++)if(id[i]<L'0'||id[i]>L'9')valid=false;
    if(valid)for(size_t i=0;i<len;i++)app_id[i]=(char)id[i];
    race_ms=GetPrivateProfileIntW(L"progress",L"active_race_ms",0,achievements);if(race_ms>300000)race_ms=300000;
    total_ms=(uint64_t)(unsigned)GetPrivateProfileIntW(L"progress",L"race_seconds",0,achievements)*1000;if(total_ms<race_ms)total_ms=race_ms;
    finishes=GetPrivateProfileIntW(L"progress",L"finishes",0,achievements);wins=GetPrivateProfileIntW(L"progress",L"wins",0,achievements);
    modes=GetPrivateProfileIntW(L"progress",L"modes",0,achievements);
    for(int i=0;i<RR_ACH_COUNT;i++){wchar_t key[16],value[32];std::swprintf(key,16,L"unlock%d",i);GetPrivateProfileStringW(L"achievements",key,L"0",value,32,achievements);unlock[i]=std::wcstoull(value,nullptr,10);}
    loaded=true;std::atexit(shutdown);
}
SDL_Window*window(){int count=0;auto**windows=SDL_GetWindows(&count);auto*w=count==1?windows[0]:nullptr;SDL_free(windows);return w;}
// The project icon on the game window (title bar, taskbar, Alt+Tab) instead of
// the default one: big and small images from game_icon.h, sized for the DPI.
void set_window_icon(SDL_Window*w){
    HWND h=w?(HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr):nullptr;if(!h)return;
    // The smallest image at least `want` pixels wide, else the largest.
    auto make=[](int want)->HICON{const rr_icon::Image*best=nullptr;
        for(const auto&i:rr_icon::images)if(i.size>=want&&(!best||i.size<best->size))best=&i;
        if(!best)for(const auto&i:rr_icon::images)if(!best||i.size>best->size)best=&i;
        return best?CreateIconFromResourceEx((PBYTE)best->data,(DWORD)best->bytes,TRUE,0x00030000,want,want,LR_DEFAULTCOLOR):nullptr;};
    HICON big=make(GetSystemMetrics(SM_CXICON)),small=make(GetSystemMetrics(SM_CXSMICON));
    if(big)SendMessageW(h,WM_SETICON,ICON_BIG,(LPARAM)big);if(small)SendMessageW(h,WM_SETICON,ICON_SMALL,(LPARAM)small);
    if(big||small)psx_mod_counter_add("rr.window.icon",1);
}
bool apply_aspect(int aspect){const int nums[]={4,16,21},dens[]={3,9,9};psx_ws_set_native_wide(0);gpu_ws_set_nw_hud_corners(0);gpu_ws_set_full_2d(1);gpu_ws_set_nw_backdrop(0);if(!psx_debug_display_aspect(nums[aspect],dens[aspect],0))return false;if(auto*w=window()){int width,height;SDL_GetWindowSize(w,&width,&height);if(!options.fullscreen)SDL_SetWindowSize(w,width,width*dens[aspect]/nums[aspect]);}return true;}
// Nothing unlocks while a cheat is on (cheats_runtime.h) or a Gameplay value of
// the F2 editor is not 100% (gameplay_editor.cpp).
void award(int id){if(id<0||id>=RR_ACH_COUNT||unlock[id]||rrpc::cheats_active()||rrpc::gameplay_active())return;unlock[id]=(uint64_t)std::time(nullptr);dirty=true;flush();psx_mod_counter_add("rr.achievement.unlocked",1);}
void award_counts(){
    if(finishes>=1)award(RR_A_FINISH1);if(finishes>=10)award(RR_A_FINISH10);if(finishes>=50)award(RR_A_FINISH50);
    if(wins>=1)award(RR_A_WIN1);if(wins>=3)award(RR_A_WIN3);if(wins>=10)award(RR_A_WIN10);
    if(rr_mode_count(modes)>=4)award(RR_A_ALL_MODES);
}
// Race results. The race flag *(0x8005B2F8) is 1 while racing and 6 on the
// results screen; a finish is a results screen after racing. Mode (0x800D5D38
// +4: bit 0x20 Jail Break, else 1 Five-O, 4 Time Trial, 8 / 24 Side Car, 16
// Skull-to-Skull, 17 Cops & Robbers), human players (+0x30) and Player 1's
// place (0x800D5C47, riders ahead of Player 1; one-player races only) are the
// last values seen while racing.
void race_results(){
    auto*r=memory_get_ram_ptr();uint32_t p;std::memcpy(&p,r+(0x8005B2F8&0x1fffff),4);if((p&0xffe00000)!=0x80000000)return;
    static bool raced;static int mode,players,place;const int flag=r[p&0x1fffff];
    if(flag==1){raced=true;mode=r[0x0D5D3C];players=r[0x0D5D68];place=r[0x0D5C47];return;}
    if(flag!=6||!raced)return;raced=false;
    // Player 1's race record (0x800D5758) +0x27: 0xFF wrecked, 0xFE busted
    // (race_end.py); those results screens are not a finish.
    const int status=r[0x0D5758+0x27];if(status==0xFF||status==0xFE){psx_mod_counter_add("rr.achievement.out",1);return;}
    if(rrpc::cheats_active()||rrpc::gameplay_active())return;
    const int m=mode&~2,bit=mode&0x20?RR_MODE_JAILBREAK:m==1?RR_MODE_FIVEO:m==4?RR_MODE_TRIAL:m==8||m==24?RR_MODE_SIDECAR:m==16?RR_MODE_SKULL:m==17?RR_MODE_COPS:0;
    finishes++;modes|=bit;dirty=true;psx_mod_counter_add("rr.achievement.finish",1);
    if(players==1&&bit!=RR_MODE_TRIAL&&place<=2){award(RR_A_PODIUM);if(place==0){wins++;psx_mod_counter_add("rr.achievement.win",1);}}
    if(bit==RR_MODE_JAILBREAK)award(RR_A_JAILBREAK);if(bit==RR_MODE_FIVEO)award(RR_A_FIVEO);if(bit==RR_MODE_SIDECAR)award(RR_A_SIDECAR);
    if(bit==RR_MODE_TRIAL)award(RR_A_TRIAL);if(bit==RR_MODE_SKULL)award(RR_A_SKULL);if(bit==RR_MODE_COPS)award(RR_A_COPS);
    if(players==2)award(RR_A_TWO_PLAYER);
    award_counts();flush();
}
// Career ranks from the saves on both memory cards (the live card images):
// BASLUS-01053ROADRASH, 10 profiles of 0x1E4 bytes from block offset 0x834
// (PROGRESS-0.15-saves.md): +0 0 = used, +0xDC mode (32 Jail Break, 1 Five-O),
// +0xE0 rank (0-1 / 2-3 / 4-5), +0xED qualifications, +0x1DC/+0x1E0 the
// game's checks over bytes 0..475 (only intact profiles count).
void career_check(){
    static uint8_t card[MEMCARD_SIZE];
    for(int c=0;c<2;c++){
        if(!memcard_is_present(c)||memcard_export_raw(c,card)!=0||card[0]!='M'||card[1]!='C')continue;
        for(int f=1;f<16;f++){const uint8_t*d=card+f*128;uint32_t state;std::memcpy(&state,d,4);if(state!=0x51||std::memcmp(d+10,"BASLUS-01053",12))continue;
            const uint8_t*block=card+f*8192;
            for(int k=0;k<10;k++){const uint8_t*s=block+0x834+k*0x1E4;uint32_t used,mode,c1,c2,s1=0,s2=0;std::memcpy(&used,s,4);if(used)continue;
                for(uint32_t i=0;i<0x1DC;i++){s1+=s[i]+i*(uint32_t)(k+2);s2=((s[i]^s2)<<1)+i*(uint32_t)(k+1);}
                std::memcpy(&c1,s+0x1DC,4);std::memcpy(&c2,s+0x1E0,4);if(c1!=s1||c2!=s2)continue;
                std::memcpy(&mode,s+0xDC,4);const int rank=s[0xE0]<6?s[0xE0]/2:0;
                if(mode==32){if(rank>=1)award(RR_A_LIEUTENANT);if(rank>=2)award(RR_A_CAPTAIN);if(s[0xED]>=9)award(RR_A_ALL_QUALIFIED);}
                if(mode==1&&rank>=2)award(RR_A_SERGEANT);
            }
        }
    }
}
// Frame rate option. 60 FPS: the game's own race loop runs every VBlank (fast CPU
// timing during races, psx_cycles.c recipe). 120 FPS: races also get a 120 Hz
// VBlank (race_timing.py), real frames at the original speed. Original: PS1 CPU
// timing. The old reprojection "Smooth" frame generation is no longer used.
bool apply_motion(int rate){gl_renderer_set_frame_generation(false);rr_fast_set_mode(rate?1:0);rr_fps120_set(rate==2);return true;}
// Render resolution with supersampling (gfx_options.h): the internal scale that
// draws Render resolution x 1, 2 or 4 lines. The surface is reserved at start-up
// (__wrap_gl_renderer_init_context); the renderer clamps a larger request to it.
bool apply_render(int resolution,int aa){if(!gl_renderer_step_internal_scale_now(rr_render_scale(resolution,aa)))return false;psx_mod_set_internal_resolution((uint32_t)rr_render_target(resolution,aa));return true;}
// FXAA (post-process pass) and scanlines: renderer state, applied at the next present.
void apply_post_aa(int aa){gl_renderer_set_post_aa(rr_aa_post(aa));}
void apply_scanlines(int s){gl_renderer_set_scanlines(s?1:0,s?rr_scanline_strength(s):.5f);}
// Texture filter: Sharp / Smooth through the mod setter; xBR (renderer mode 3,
// which the mod setter does not accept) straight to the renderer.
void apply_filter(int f){if(rr_filter_mode(f)==3)gr_set_texture_filter(3);else psx_mod_set_texture_filter(rr_filter_mode(f));}
// Texture correction (gfx_options.h rr_tc_*). On: the framework's base tier, the
// GTE's sub-pixel projections matched to GP0 vertices by screen position where
// that is unambiguous. Full: also the PGXP hooks in the generated code, which
// carry each projection through registers and RAM into the packet, with the
// framework's hook-flavour defaults (no position cache, no tolerance clamp)
// and exact projections for near geometry. Visual only: guest state, NCLIP
// culling and timing are unchanged.
void apply_correction(int c){
    const bool on=c>0,full=c==2;
    if((g_rr_pgxp_full!=0)!=full)pgxp_invalidate_all();   // shadows the other step did or did not track
    g_rr_pgxp_full=full;
    pgxp_set_tolerance(full?-1.0f:0.5f);pgxp_set_position_fallback(!full);pgxp_set_preserve_projection(full);
    pgxp_set_enabled(on);pgxp_set_cpu_mode(on);gte_geometry_correction_set(on);gpu_texture_correction_set(on);
}
// Post effects (gpu_presentation.py post_effects): renderer state, next present.
void apply_post(){gl_renderer_rr_post_set(options.post[0],options.post[1],options.post[2],options.post[3]);gl_renderer_rr_shadow_set(options.shadow);}
// Loading speed. The emulated drive reads at the PS1's 2x speed (~300 KB/s,
// with seek delays), so every load took as long as on the console.
// Fast loading: during sustained non-XA disc loads the emulator runs unpaced;
// every guest VBlank, CD deadline and interrupt still happens in order, so
// the game sees the same machine, only sooner (framework host pacing).
// Disc speed divides the emulated sector delay (guest-visible timing; XA
// audio and FMV stay authentic). Test overrides: RR_LOAD_ACCEL (0 = uncapped,
// 1 = authentic) and RR_DISC_SPEED (1 = authentic, N = N x, 0 = instant).
// The framework's mod setters for these are read once at start-up, before the
// PC layer runs, so the switches are set directly: g_turbo_loads_enabled (host
// pacing during loads, uncapped) and the CD operating divisor. pc_tick first
// runs after the game's entry point, when the BIOS boot (always 1x) is done.
}
extern "C" int g_turbo_loads_enabled;
extern "C" void cdrom_set_speed(int divisor);
extern "C" void cdrom_set_game_speed(int divisor);
extern "C" void cdrom_set_instant_rate(int per_frame);
namespace {
bool load_accel;
void apply_loading(){
    // Default: 4x drive ([system] fast_loading=1, launcher Options > System).
    // Measured on all nine Time Trial courses: race loads drop from ~4.4 s to
    // ~0.5 s and races then run normally; 4x already reaches the game's own
    // floor, 16x still works, 32x and above break the game's loader.
    long accel=0,disc=GetPrivateProfileIntW(L"system",L"fast_loading",1,settings)?4:1;
    if(rrpc::netplay())if(const auto*s=rrpc::net_session();s&&s->fast_loading>=0)disc=s->fast_loading?4:1;   // the host's
    if(const char*e=std::getenv("RR_LOAD_ACCEL"))accel=std::strtol(e,nullptr,10);
    if(const char*e=std::getenv("RR_DISC_SPEED"))disc=std::strtol(e,nullptr,10);
    if(disc<0||disc>1024)disc=1;
    load_accel=accel!=0;
    if(disc!=1){if(!disc)cdrom_set_instant_rate(32);cdrom_set_game_speed((int)disc);cdrom_set_speed((int)disc);}
}
// Intro movies ([system] skip_intro, launcher Options > System, default on).
// The start-up movies (EA logo, Road Rash intro) end on Start / Cross. From
// power-on until the main menu first appears (frontend context 0x800990B4,
// page 1), pad 1 taps Start + Cross (100 ms on, 150 ms off) while a movie is
// actually streaming: an MDEC frame decoded within the last 300 ms with XA
// audio playing. The title screen is no movie, so nothing is pressed there or
// in any menu; story cutscenes and the idle attract loop always play. (The
// framework's own auto-skip holds Start from its frame loop, but the host pad
// poll replaced that press in the same frame, so the movies never ended.)
}
extern "C" uint32_t mdec_get_decode_count(void);
extern "C" int cdrom_xa_stream_active(void);
namespace {
int intro_state=-1;   // -1 unset, 1 skipping, 0 done
// A savestate boot (Quick resume RR_QUICK_RESUME, developer PSX_LOAD_SLOT) skips
// the movies and the main menu: no skip then, or race MDEC frames got Start taps.
void intro_init(){if(intro_state<0){load();intro_state=loaded&&GetPrivateProfileIntW(L"system",L"skip_intro",1,settings)&&!std::getenv("RR_QUICK_RESUME")&&!std::getenv("PSX_LOAD_SLOT")?1:0;}}
void intro_skip(uint32_t ctx,uint32_t page){
    intro_init();
    if(intro_state==1&&ctx==0x800990B4u&&page==1){intro_state=0;psx_mod_counter_add("rr.intro.reached_menu",1);}
}
}
namespace rrpc {
uint16_t intro_skip_filter(uint16_t buttons){
    intro_init();
    if(intro_state!=1)return buttons;
    // Off for good once the main menu is up (checked here: the pad poll runs
    // from power-on, before pc_tick does).
    auto*r=memory_get_ram_ptr();uint32_t ctx=0,page=0;std::memcpy(&ctx,r+(0x8009c5c8&0x1fffff),4);std::memcpy(&page,r+(0x8009c5b8&0x1fffff),4);
    if(ctx==0x800990B4u&&page==1){intro_state=0;psx_mod_counter_add("rr.intro.reached_menu",1);return buttons;}
    // Never in a race or on its results (race flag 1 / 6).
    uint32_t race=0;std::memcpy(&race,r+(0x8005b2f8&0x1fffff),4);
    if(race>=0x80010000u&&race<0x801fff00u&&(r[race&0x1fffff]==1||r[race&0x1fffff]==6)){intro_state=0;return buttons;}
    // A movie: the MDEC decoder producing frames steadily (3+ decodes within
    // 500 ms; these movies carry no XA stream). A single still never qualifies.
    static uint32_t last_count;static uint64_t stamps[3];static int n;
    const uint64_t now=SDL_GetTicks();const uint32_t c=mdec_get_decode_count();
    if(c!=last_count){last_count=c;stamps[n++%3]=now;}
    const bool movie=n>=3&&now-stamps[(n)%3]<500&&now-stamps[(n+2)%3]<200;
    if(movie&&now%250<100){psx_mod_counter_add("rr.intro.skip_press",1);return (uint16_t)(buttons&~((1u<<3)|(1u<<14)));}
    return buttons;
}
}
namespace {
// Host pacing may only run while the game sits in its loading state (race
// flag 3, not paused). The game streams track data during races too, and an
// unrestricted turbo ran the race itself at ~2.4x.
void gate_load_accel(bool paused){
    if(!load_accel)return;
    auto*r=memory_get_ram_ptr();uint32_t ctx=0;std::memcpy(&ctx,r+(0x8005B2F8&0x1fffff),4);
    int flag=ctx>=0x80010000u&&ctx<0x80200000u?r[ctx&0x1fffff]:-1;
    g_turbo_loads_enabled=flag==3&&!paused;
}
// Model detail ([display] model_detail). Riders, bikes and the other race
// models carry 3-4 versions in RAM, all loaded for every rider, and the
// per-view selector 0x800667C4 picks one each frame by comparing the camera
// distance (object +0x2C per view) with an {up, down} threshold table in the
// executable's data (object +0x64): scenery 0x80054160 (3 levels), bikes
// 0x80054178, riders 0x80054198, roadside objects 0x800541B8 (3 levels). Stock opponents drop below full
// detail a short way ahead of the player (bikes past 512, riders past 384;
// the player's own bike sits ~260 from the chase camera), and per view, so
// split screen too. Scaling every finite threshold moves the switch points
// out and keeps the game's hysteresis; the end markers stay. pc_tick keeps
// the tables at the chosen scale, so a loaded savestate is corrected too.
// Values not written by this code (another revision, a mod) are left alone.
// Only the bike and rider tables are scaled. The other two belong to scenery
// and roadside objects whose full-detail models are streamed in with the
// road and are not resident at long range: forcing them drew giant garbage
// meshes (a huge dark shape over the road, the stadium sign missing).
constexpr uint32_t lod_tables=0x80054178;constexpr int lod_words=16;
const int32_t lod_stock[lod_words]={512,-2147483647,768,448,960,704,2147483647,832,     // bikes
    384,-2147483647,768,256,1280,640,2147483647,1088};                                  // riders
void lod_scaled(int percent,int32_t*out){for(int i=0;i<lod_words;i++){int32_t v=lod_stock[i];out[i]=v==2147483647||v==-2147483647?v:(int32_t)((int64_t)v*percent/100);}}
int lod_percent(){static int env=[]{const char*e=std::getenv("RR_LOD_SCALE");int p=e?std::atoi(e):0;return p>=100&&p<=10000?p:0;}();
    // Online: the host's switch distance (its tuning.ini steps may differ from ours).
    if(rrpc::netplay())if(const auto*s=rrpc::net_session();s&&s->lod_percent>=100)return s->lod_percent;
    return env?env:rrpc::model_detail_percent(options.detail);}
void apply_detail(){
    static int last=-1;static int32_t target[lod_words];int p=lod_percent();if(p!=last){lod_scaled(p,target);last=p;}
    auto*r=memory_get_ram_ptr()+(lod_tables&0x1fffff);int32_t now[lod_words];std::memcpy(now,r,sizeof now);
    if(!std::memcmp(now,target,sizeof now))return;
    // Only overwrite tables that hold stock values or a scale written here
    // (any menu step, e.g. from a savestate made with another setting).
    // Percent of the scale they hold, from the rider level-0 threshold.
    int held=(int)((int64_t)now[8]*100/lod_stock[8]);int32_t check[lod_words];lod_scaled(held,check);
    if(std::memcmp(now,check,sizeof now)){psx_mod_counter_add("rr.lod.foreign",1);return;}
    std::memcpy(r,target,sizeof target);psx_mod_counter_add("rr.lod.applied",1);
}
// Draw distance ([display] draw_distance). The race overlay's object draw
// 0x80067AC4 (object, view) skips an object whose camera distance (object
// +0x2C per view, as level of detail uses) is beyond its type's cull distance:
// table 0x800CC6A4 (lui 0x800D at 0x80067B64, addiu -0x395C at 0x80067CC8;
// the only reader), indexed by (mesh +0x0E & 0x78) >> 3. Stock: 7680 for
// types 0-2, 4 and 5, 12480 for type 3, 6400 for type 6 (32000 further when
// object +8 is 0). From the object census: type 1 riders, 2 bikes, 3 traffic
// (single-model vehicles, no level-of-detail table), 4 roadside objects. Only
// riders, bikes and traffic are scaled; scenery and roadside models stream in
// with the road (see level of detail). The table is race-overlay data (zeros
// in the menus), so it is only written while it holds the stock values or a
// step written here. RR_DRAW_SCALE=<percent> overrides the setting for tests.
constexpr uint32_t cull_table=0x800CC6A4;constexpr int cull_words=7;
const int32_t cull_stock[cull_words]={7680,7680,7680,12480,7680,7680,6400};
void cull_scaled(int percent,int32_t*out){for(int i=0;i<cull_words;i++)out[i]=i>=1&&i<=3?cull_stock[i]*percent/100:cull_stock[i];}
int draw_percent(){static int env=[]{const char*e=std::getenv("RR_DRAW_SCALE");int p=e?std::atoi(e):0;return p>=100&&p<=1000?p:0;}();
    return env?env:rr_draw_percent(options.draw);}
void apply_draw(){
    static int last=-1;static int32_t target[cull_words];int p=draw_percent();if(p!=last){cull_scaled(p,target);last=p;}
    auto*r=memory_get_ram_ptr()+(cull_table&0x1fffff);int32_t now[cull_words];std::memcpy(now,r,sizeof now);
    if(!std::memcmp(now,target,sizeof now))return;
    // Stock values or another menu step (e.g. a savestate made with another setting).
    bool ours=false;for(int s=0;s<rr_draw_steps&&!ours;s++){int32_t check[cull_words];cull_scaled(rr_draw_percent(s),check);ours=!std::memcmp(now,check,sizeof now);}
    if(!ours)return;   // not the race overlay's table (menus, another revision, a mod)
    std::memcpy(r,target,sizeof target);psx_mod_counter_add("rr.draw.applied",1);
}
}
namespace rrpc {
int model_detail_percent(int step){return rr_detail_percent[step>=0&&step<rr_detail_steps?step:0];}
GraphicsOptions& graphics_options(){load();return options;}
bool change_graphics(int item,int dir){
    load();auto cycle=[dir](int v,int n){return (v+(dir<0?n-1:1))%n;};
    // Online these are the session's (texture correction, aspect, frame rate, draw distance).
    if(rrpc::netplay()&&(item==3||item==4||item==5||item==8))return false;
    if(item==0){int next=cycle(options.resolution,5);
        if(!apply_render(next,options.aa))return false;
        if(!write_int(L"display",L"resolution",next,settings)){apply_render(options.resolution,options.aa);return false;}
        options.resolution=next;
    }else if(item==6){int next=cycle(options.aa,rr_aa_steps);
        if(!apply_render(options.resolution,next))return false;
        if(!write_int(L"display",L"antialiasing",next,settings)){apply_render(options.resolution,options.aa);return false;}
        apply_post_aa(next);options.aa=next;
    }else if(item==7){int next=cycle(options.scanlines,rr_scanline_steps);if(!write_int(L"display",L"scanlines",next,settings))return false;apply_scanlines(next);options.scanlines=next;
    }else if(item==8){int next=cycle(options.draw,rr_draw_steps);if(!write_int(L"display",L"draw_distance",next,settings))return false;options.draw=next;   // apply_draw, next VBlank
    }else if(item==13){int next=cycle(options.shadow,rr_shadow_steps);if(!write_int(L"display",L"shadow",next,settings))return false;options.shadow=next;apply_post();
    }else if(item>=9&&item<9+rr_post_effects){const int e=item-9;int next=cycle(options.post[e],rr_post_steps);wchar_t key[16];std::swprintf(key,16,L"%hs",rr_post_key(e));
        if(!write_int(L"display",key,next,settings))return false;options.post[e]=next;apply_post();
    }else if(item==1){int next=cycle(options.filter,rr_filter_steps);if(!write_int(L"display",L"filter",next,settings))return false;options.filter=next;apply_filter(next);
    }else if(item==2){auto*w=window();if(!w||!SDL_SetWindowFullscreen(w,!options.fullscreen))return false;
        int next=!options.fullscreen;if(!write_int(L"display",L"fullscreen",next,settings)){SDL_SetWindowFullscreen(w,options.fullscreen);return false;}options.fullscreen=next;
    }else if(item==3){int next=rr_tc_step(options.texture_correction);if(next>=tc_steps)return false;if(!write_int(L"display",L"texture_correction",next,settings))return false;
        options.texture_correction=next;apply_correction(next);
    }else if(item==4){int next=(options.aspect+1)%3;if(!apply_aspect(next))return false;if(!write_int(L"display",L"aspect",next,settings)){apply_aspect(options.aspect);return false;}options.aspect=next;
    }else if(item==5){int next=(options.motion+1)%3;if(!apply_motion(next))return false;if(!write_int(L"display",L"native_60fps",next,settings)){apply_motion(options.motion);return false;}options.motion=next;}else return false;
    psx_mod_counter_add("rr.graphics.changed",1);return true;
}
// Test-only stress hook (RR_TEST_PILEUP set): when rr_pileup.trigger appears
// beside the runtime, copy rider 0's position/orientation onto riders 1..15 so
// the whole pack piles into the same wall at once. Inert without the
// environment flag. pc_tick runs at the VBlank, which can interrupt the game
// half way through updating the riders, so the copy waits for a VBlank taken in
// the game's own wait-for-VBlank loop (0x8001C408..0x8001C428), between frames.
// The pack is laid out like a real bunch: four abreast (150 units apart; the
// race start has pairs 150-500 apart) in rows 200 apart, 300-900 units AHEAD
// of the player along the road (the AI riders' direction of travel; short
// enough to stay on the road through a curve). The old layout (+-80 / 0..180, and only the
// coarse position moved; the precise copies stayed on the player) stacked all
// 16 riders on the camera: one render pass then wrapped the game's whole
// ~120 KB primitive ring (allocator 0x80021C98) and overwrote the background
// list queued for the VBlank callback (0x8001B808), corrupting the display list
// under fast timing (GP0 unknown-command fatals, 2-5 s GPUSTAT stalls).
extern "C" uint32_t psx_compiled_irq_resume_pc(void);
static void test_pileup(bool racing){
    static int on=-1;static wchar_t trigger[32768];
    if(on<0){on=std::getenv("RR_TEST_PILEUP")!=nullptr;if(on){DWORD n=GetModuleFileNameW(nullptr,trigger,32768);auto*s=n?std::wcsrchr(trigger,L'\\'):nullptr;if(s)std::wcscpy(s+1,L"rr_pileup.trigger");else on=0;}}
    if(!on||!racing)return;
    auto*r=memory_get_ram_ptr();uint32_t base=0;std::memcpy(&base,r+(0x8005B3A0&0x1fffff),4);
    if(base<0x80010000u||base+16*0x448>0x80200000u)return;
    uint8_t*me=r+(base&0x1fffff);
    // Track direction: the AI riders' mean motion (coarse x/z) per VBlank, kept
    // from the last VBlank the pack was moving. The player itself is steering
    // into a wall, so its own motion is not along the road.
    static int32_t px[16],pz[16];static bool have;static double hx,hz=1.0;
    {double sx=0,sz=0;int n=0;
     for(int i=1;i<16;i++){int32_t x,z;std::memcpy(&x,me+i*0x448+0x0C,4);std::memcpy(&z,me+i*0x448+0x14,4);
         double dx=x-px[i],dz=z-pz[i];if(have&&dx*dx+dz*dz<2000.0*2000.0){sx+=dx;sz+=dz;n++;}px[i]=x;pz[i]=z;}
     double len=std::sqrt(sx*sx+sz*sz);if(n>=4&&len>5.0*n){hx=sx/len;hz=sz/len;}have=true;}
    if(GetFileAttributesW(trigger)==INVALID_FILE_ATTRIBUTES)return;
    uint32_t pc=psx_compiled_irq_resume_pc();if(pc<0x8001C408u||pc>=0x8001C428u)return;   // not between frames yet
    DeleteFileW(trigger);
    static const int blocks[][2]={{0x0C,0x18},{0xB8,0x124},{0x1B0,0x1C8},{0x1D4,0x1E0},{0x1F8,0x204},{0x310,0x31C},{0x32C,0x334},{0x370,0x37C}};
    // Per-rider world positions in the copied blocks: x,y,z triples, precise (x1024) except 0x0C.
    static const int precise[]={0xB8,0xC4,0xD0,0xDC,0xE8,0xF4,0x1D4,0x1F8,0x310};
    auto shift=[](uint8_t*p,int32_t dx,int32_t dz){int32_t x,z;std::memcpy(&x,p,4);std::memcpy(&z,p+8,4);x+=dx;z+=dz;std::memcpy(p,&x,4);std::memcpy(p+8,&z,4);};
    for(int i=1;i<16;i++){uint8_t*a=me+i*0x448;
        for(auto&b:blocks)std::memcpy(a+b[0],me+b[0],b[1]-b[0]);
        double fwd=300.0+200.0*((i-1)/4),side=150.0*((i-1)%4-1.5);
        int32_t dx=(int32_t)(hx*fwd-hz*side),dz=(int32_t)(hz*fwd+hx*side);
        shift(a+0x0C,dx,dz);for(int o:precise)shift(a+o,dx*1024,dz*1024);}
    psx_mod_counter_add("rr.test.pileup",1);
}
// Test-only display hook (RR_TEST_GFX set): rr_gfx.trigger beside the runtime
// holds commands, run at the next VBlank and then deleted: "<row> <dir>" steps
// a Display row through change_graphics (the in-game menu's own path), "depth
// <0|1>" switches the renderer's PGXP depth buffer. Inert without the flag.
static void test_gfx(){
    static int on=-1;static wchar_t trigger[32768];
    if(on<0){on=std::getenv("RR_TEST_GFX")!=nullptr;if(on){DWORD n=GetModuleFileNameW(nullptr,trigger,32768);auto*s=n?std::wcsrchr(trigger,L'\\'):nullptr;if(s)std::wcscpy(s+1,L"rr_gfx.trigger");else on=0;}}
    if(!on)return;
    FILE*f=_wfopen(trigger,L"rb");if(!f)return;
    char line[64];while(std::fgets(line,sizeof line,f)){int a=0,b=1;
        if(std::sscanf(line,"depth %d",&a)==1)gl_renderer_set_pgxp_depth(a);
        else if(std::sscanf(line,"posttime %d",&a)==1)gl_renderer_rr_post_time(a);   // post-effect pass timing (runtime.log)
        else if(std::sscanf(line,"%d %d",&a,&b)>=1&&!change_graphics(a,b))psx_mod_counter_add("rr.test.gfx_failed",1);
        psx_mod_counter_add("rr.test.gfx",1);}
    std::fclose(f);DeleteFileW(trigger);
}
// Hybrid CPUs (Intel 12th gen and later): keep the game thread on performance
// cores and out of EcoQoS. Windows otherwise moves it to efficiency cores,
// especially when the window is not focused: a 16-rider pile-up then drops to
// ~43 Hz instead of holding 60. The process opts out of execution-speed
// throttling and this (game) thread selects the CPU sets of the highest
// efficiency class. A no-op when all cores share one class. RR_CPU_SETS=0 (or
// RR_PERF=0, the A/B switch for all 0.15 pile-up work) skips it.
// Entry points are looked up at run time (Windows 10+; older headers lack them).
static void prefer_performance_cores(){
    static bool done;if(done)return;done=true;
    static const char*const opt_out[]={"RR_CPU_SETS","RR_PERF"};
    for(const char*name:opt_out)if(auto*e=std::getenv(name);e&&e[0]=='0')return;
    HMODULE k=GetModuleHandleW(L"kernel32.dll");if(!k)return;
    struct Throttle{ULONG version,mask,state;}speed{1,1,0};   // *_POWER_THROTTLING_EXECUTION_SPEED, not throttled
    using SetInfo=BOOL(WINAPI*)(HANDLE,int,void*,DWORD);
    if(auto f=(SetInfo)(void*)GetProcAddress(k,"SetProcessInformation"))f(GetCurrentProcess(),4/*ProcessPowerThrottling*/,&speed,sizeof speed);
    if(auto f=(SetInfo)(void*)GetProcAddress(k,"SetThreadInformation"))f(GetCurrentThread(),3/*ThreadPowerThrottling*/,&speed,sizeof speed);
    using GetSets=BOOL(WINAPI*)(void*,ULONG,ULONG*,HANDLE,ULONG);using SelectSets=BOOL(WINAPI*)(HANDLE,const ULONG*,ULONG);
    auto get=(GetSets)(void*)GetProcAddress(k,"GetSystemCpuSetInformation");auto select=(SelectSets)(void*)GetProcAddress(k,"SetThreadSelectedCpuSets");
    if(!get||!select)return;
    static unsigned char info[64*1024];ULONG len=0;if(!get(info,sizeof info,&len,GetCurrentProcess(),0))return;
    // SYSTEM_CPU_SET_INFORMATION: Size, Type, then CpuSet {Id, Group, LogicalProcessorIndex, CoreIndex,
    // LastLevelCacheIndex, NumaNodeIndex, EfficiencyClass, ...}.
    // Only cores the process may run on (a user-set affinity wins): selecting
    // sets outside it made Windows schedule the thread badly (-7% on E-cores).
    DWORD_PTR allowed=0,system=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system))return;
    ULONG ids[256];int n=0;unsigned char top=0,low=255;
    for(int pass=0;pass<2;pass++)for(ULONG off=0;off+20<=len;){
        ULONG size,type,id;std::memcpy(&size,info+off,4);std::memcpy(&type,info+off+4,4);std::memcpy(&id,info+off+8,4);
        if(size<20)break;
        unsigned short group;std::memcpy(&group,info+off+12,2);unsigned char lp=info[off+14],eff=info[off+18];
        bool usable=group==0&&lp<sizeof(DWORD_PTR)*8&&(allowed>>lp&1);
        if(type==0/*CpuSetInformation*/){if(pass==0){if(eff>top)top=eff;if(eff<low)low=eff;}else if(eff==top&&usable&&n<256)ids[n++]=id;}
        off+=size;
    }
    if(top!=low&&n>0&&select(GetCurrentThread(),ids,(ULONG)n))psx_mod_counter_add("rr.cpu.performance_cores",n);
}
void hud_edit_scene(bool paused);   // hud_editor.cpp: the centre of the HUD is the pause menu now
void gameplay_tick(bool racing,bool paused);   // gameplay_editor.cpp: the F2 editor's Gameplay values
void pc_tick(bool racing,bool paused){
    prefer_performance_cores();
    load();if(!loaded)return;
    // Online the game-state writes below run from the emulated VBlank instead
    // (pc_guest_start / pc_guest_tick, netplay_session.cpp); cheats are off.
    const bool online=netplay();
    {static bool loading_set;if(!loading_set&&!online){loading_set=true;apply_loading();}}
    gate_load_accel(paused);
    load();if(!loaded)return;auto now=SDL_GetTicks();
    test_pileup(racing&&!paused);
    test_gfx();
    if(!online){apply_detail();apply_draw();cheats_tick(racing);gameplay_tick(racing,paused);}
    auto*r=memory_get_ram_ptr();uint32_t ctx=0,page=0;std::memcpy(&ctx,r+(0x8009c5c8&0x1fffff),4);std::memcpy(&page,r+(0x8009c5b8&0x1fffff),4);
    intro_skip(ctx,page);
    // The pause menu sets the race flag to 3 (as loading does) but the race
    // frame and its HUD stay on screen behind it: keep the race HUD layout.
    native_ui_scene(racing||paused,page==1&&ctx>=0x80099000&&ctx<=0x80099500,options.aspect);
    hud_edit_scene(paused);
    voice_tick();
    mods_tick(racing);   // texture / sound / model packs (mod_assets.cpp)
    music_tick(racing,paused);
    GlScaleInfo info{};
    if(!applied&&window()&&gl_renderer_scale_info(&info)){
        set_window_icon(window());
        apply_render(options.resolution,options.aa);
        // game.toml carries the same values; set here too so both always agree.
        if(options.aa)apply_post_aa(options.aa);if(options.scanlines)apply_scanlines(options.scanlines);
        apply_filter(options.filter);apply_post();
        apply_correction(options.texture_correction);
        if(!online){apply_aspect(options.aspect);apply_motion(options.motion);}   // online: pc_guest_start
        if(options.fullscreen)SDL_SetWindowFullscreen(window(),true);
        if(options.discord&&app_id[0])presence_start(app_id);applied=true;
    }
    if(last_tick&&racing&&!paused&&!cheats_active()&&!gameplay_active()){auto delta=now-last_tick;if(delta<=250){race_ms+=delta;if(race_ms>300000)race_ms=300000;total_ms+=delta;dirty=true;}}
    last_tick=now;
    if(racing){award(RR_A_FIRST_RIDE);if(race_ms>=300000)award(RR_A_REGULAR);if(total_ms>=3600000)award(RR_A_HOUR1);if(total_ms>=18000000)award(RR_A_HOUR5);
        if(!paused&&options.motion==2)award(RR_A_FPS120);if(!paused&&options.aspect!=0)award(RR_A_WIDE);}
    race_results();
    {static bool counted;if(!counted){counted=true;award_counts();}}   // earlier progress, also after an update
    if(now-last_flush>=15000){if(!racing)career_check();flush();last_flush=now;}
    presence_context(racing?(paused?2:1):0);
}
// Reserve live scale capacity once, before context creation. The backend owns
// allocation limits, VRAM resampling and GL-thread synchronization. Original
// never enters this branch. No guest clock, timing or machine state is changed.
extern "C" int __real_gl_renderer_init_context(SDL_Window*);
extern "C" int __wrap_gl_renderer_init_context(SDL_Window*w){
    // Scale 9 (4K) covers every Render resolution step; supersampling chosen
    // before the start reserves what it draws, up to 3840 lines (the renderer
    // still clamps to the GPU's size and memory limits). Supersampling picked
    // later in game is limited to the surface reserved here.
    if(rrpc::rider_options().menu){auto&o=rrpc::graphics_options();int s=rr_render_scale(o.resolution,o.aa);if(s<9)s=9;
        if(const char*e=std::getenv("RR_ALLOC_SCALE")){int v=std::atoi(e);if(v>=1&&v<=rr_ss_max_scale)s=v;}
        gr_set_scale(s);gl_renderer_set_dynamic_resolution(1);}
    return __real_gl_renderer_init_context(w);
}
// Online: first-frame settings once the game's VBlank handler runs, and the
// level-of-detail / draw-distance tables five times a second, from the
// emulated VBlank (netplay_session.cpp net_guest_tick), so both PCs write
// them on the same guest frame.
void pc_guest_start(){load();apply_loading();apply_motion(options.motion);apply_aspect(options.aspect);}
void pc_guest_tick(){load();apply_detail();apply_draw();}
void personal_touch(){load();award(RR_A_NAME);}
int achievement_count(){return RR_ACH_COUNT;}
const char* achievement_name(int i){return i>=0&&i<RR_ACH_COUNT?rr_achievements[i].name:"";}
const char* achievement_description(int i){return i>=0&&i<RR_ACH_COUNT?rr_achievements[i].help:"";}
bool achievement_unlocked(int i){load();return i>=0&&i<RR_ACH_COUNT&&unlock[i]!=0;}
int achievements_unlocked(){load();int n=0;for(int i=0;i<RR_ACH_COUNT;i++)n+=unlock[i]!=0;return n;}
void achievement_progress(int i,char*out,int cap){
    load();out[0]=0;if(i<0||i>=RR_ACH_COUNT)return;const auto&a=rr_achievements[i];
    if(i==RR_A_REGULAR)std::snprintf(out,cap,"Riding: %u/300 sec",(unsigned)(race_ms/1000));
    else if(a.progress==RR_P_FINISHES)std::snprintf(out,cap,"Finished: %d/%d",finishes<a.goal?finishes:a.goal,a.goal);
    else if(a.progress==RR_P_WINS)std::snprintf(out,cap,"Wins: %d/%d",wins<a.goal?wins:a.goal,a.goal);
    else if(a.progress==RR_P_MODES)std::snprintf(out,cap,"Modes: %d/%d",rr_mode_count(modes),a.goal);
    else if(a.progress==RR_P_HOURS){unsigned m=(unsigned)(total_ms/60000),g=(unsigned)a.goal*60;std::snprintf(out,cap,"Riding: %u/%u min",m<g?m:g,g);}
}
// Locks every achievement and clears all progress (the PC menu's Reset, the
// launcher's own reset writes the same file while the game is closed). The
// file is first copied to achievements.ini.bak.
bool reset_achievements(){
    load();if(!loaded)return false;wchar_t backup[32768];std::swprintf(backup,32768,L"%ls.bak",achievements);CopyFileW(achievements,backup,FALSE);
    race_ms=total_ms=0;finishes=wins=modes=0;for(auto&u:unlock)u=0;dirty=true;flush();psx_mod_counter_add("rr.achievement.reset",1);return !dirty;
}
unsigned racing_seconds(){load();return (unsigned)(race_ms/1000);}
bool discord_configured(){load();return app_id[0]!=0;}
const char* discord_status(){load();return !app_id[0]?"Needs application ID":presence_status();}
bool toggle_discord(){load();if(!app_id[0])return false;bool next=!options.discord;if(!write_int(L"discord",L"enabled",next,settings))return false;
    if(next&&!presence_start(app_id)){write_int(L"discord",L"enabled",0,settings);return false;}if(!next)presence_stop();options.discord=next;return true;}
}

extern "C" SDL_Window*SDLCALL __real_SDL_CreateWindow(const char*,int,int,SDL_WindowFlags);
extern "C" SDL_Window*SDLCALL __wrap_SDL_CreateWindow(const char*title,int width,int height,SDL_WindowFlags flags){
 if(rrpc::rider_options().menu){int a=rrpc::graphics_options().aspect;const unsigned n[]={4,16,21},d[]={3,9,9};psx_mod_set_fixed_display_aspect(n[a],d[a]);height=width*d[a]/n[a];}
 return __real_SDL_CreateWindow(title,width,height,flags);
}
// Cheats (GameShark codes, unlock flags); cheats_tick runs from pc_tick.
#include "cheats_runtime.h"
