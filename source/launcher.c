/* Road Rash Jailbreak launcher. LGPL-2.1-or-later. Original author code.
 * No original/translated game code or game artwork is linked into this binary.
 * Game data is installed by the user; the native backend is a private local build. */
#define COBJMACROS
#include "disc_reader.h"
#include "mdec_image.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <process.h>
#include <time.h>
#include <wincodec.h>
#include <math.h>
#include <mmsystem.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "owner_assets.h"
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#define APP_TITLE L"Road Rash: Jailbreak \u2014 PC Recomp"
static wchar_t appdir[32768],store[32768],ini[32768],discfile[32768],cuefile[32768],workspace[32768];
static HWND mainwindow;static Image project_logo;static int installed,page,selected,binding=-1,resolution=1,device,filter;
static volatile LONG busy,progress;static HANDLE builder,game_process;static int gamepad_map;
/* Player 2 seat: 0 off, 1 controller, 2 keyboard. Keyboard only when Player 1
 * is on a controller (one keyboard map per seat would collide otherwise). With
 * Player 1 on a controller too, the runtime gives Player 2 the next free pad. */
static int p2_device;
/* Players 3 and 4: 0 off, 1 controller, on the multitap (Side Car versus takes
 * up to four). Controller seats take pads in the order they were plugged in. */
static int p3_device,p4_device;
/* Saves browser (save_browser.h): quick resume slot for the next start, card backups before every launch. */
static int resume_slot;static void sb_backup_cards(void);static void sb_action(void);static int sb_delete(void);static void sb_pad_poll(HWND);
static void p2_cycle(int dir){do p2_device=(p2_device+(dir>0?1:2))%3;while(p2_device==2&&!device);}
static wchar_t status_text[256]=L"IMPORT YOUR GAME TO GET STARTED";static DWORD status_tick;
static int preview_only;
static int slowdown_mode,fullscreen_mode,discord_enabled,texture_correction,aspect=0,smooth_motion;
/* Original game options (launcher.ini [original], page 23): opt-in, 0 = the
 * unmodified game's own. Render resolution, texture filter, fullscreen, aspect
 * ratio, frame rate (0 original, 1 60 FPS, 2 120 FPS) and Esc closes the game. */
enum {ORIG_RES,ORIG_FILTER,ORIG_FULL,ORIG_ASPECT,ORIG_RATE,ORIG_ESC,ORIG_COUNT};
static int orig[ORIG_COUNT];
static const wchar_t*orig_keys[ORIG_COUNT]={L"resolution",L"filter",L"fullscreen",L"aspect",L"frame_rate",L"esc_quits"};
static wchar_t discord_app_id[64];
static int enhanced_mode=1,mode_override=-1,preset_chosen;
/* hd_ui: HD UI on = smoothed UI artwork and HD lettering, off = the original
 * look (the runtime no longer reads a separate hd_font key). */
static int larger_text=1,rename_riders,hd_ui=1,fmv_wide,voices[2];
/* Rider look per player ([riders] paint1/2, tag1/2, outfit1/2, colour1/2; rr_look_* in
 * owner_assets.c, 0 = original). Rider look page rows: 0/1 paint, 2/3 name tag,
 * 4/5 outfit, 6/7 colours (Player 1, Player 2). */
static int paints[2],tags[2],outfits[2],colours[2];
static int*look_setting(int row){int*kinds[]={paints,tags,outfits,colours};return kinds[(row/2)%RR_LOOK_KINDS]+row%2;}
#include "model_detail.h"
static int model_detail;   /* level of detail: step index into the tuned steps (tn_live.lod); [display] model_detail */
/* Renderer options (gfx_options.h), [display] antialiasing / vsync /
 * low_latency / scanlines / draw_distance. Anti-aliasing, scanlines and draw
 * distance also change live in game. */
#include "gfx_options.h"
static int aa_mode,vsync_on=1,low_latency=1,scanline_mode,draw_mode,post_fx[4],shadow_mode;   /* post_fx: bloom, sharpen, colour, vignette (gfx_options.h) */
static int orig_steps(int k){return k==ORIG_RES?5:k==ORIG_FILTER?rr_filter_steps:k==ORIG_ASPECT||k==ORIG_RATE?3:2;}
/* tuning.ini (tuning.h): the saved menus, labels and level-of-detail steps.
 * The Developer page (dev_editor.h) edits a draft and saves on request. */
#include "tuning.h"
#include "achievements.h"
#include "cheat_codes.h"
static RrTuning tn_live;static wchar_t tuning_path[32768];static FILETIME tuning_stamp;
static int dev_unlocked;   /* launcher.ini [developer] unlocked: Ctrl+Shift+D shows the Developer page */
static int row_count(void);static void tn_refresh(void);static void dev_tuning_reloaded(void);
/* Enhanced always skips the OpenBIOS boot animation (game.toml fast_boot):
 * only the BIOS shell (the spinning cube) is bypassed; kernel init and the
 * game's disc boot still run in the recompiled BIOS. Original always shows it. */
/* [system] fast_loading: 1 = 4x emulated disc drive (Enhanced default; the
 * runtime reads it in pc_services.cpp), 0 = the PS1's own 2x drive timing. */
static int fast_loading=1;
/* [system] skip_intro: 1 = skip the start-up movies (Enhanced default; the
 * runtime's pad hook in pc_services.cpp), 0 = play them. */
static int skip_intro=1;
/* [launcher] start_game: 1 = opening the launcher starts the game straight
 * away with the window hidden, and the launcher closes with the game. Shift
 * held while opening (or --launcher) shows it; so does any launch problem or
 * a game that stops with an error. quick_start: 1 launching, 2 running. */
static int start_game_directly,quick_start;
static int music_custom,music_count,music_index;
static wchar_t music_files[128][260];
static void music_reload(void);
static void music_add(void);
static void notice(const wchar_t*);
static wchar_t rider_names[2][9]={L"Player1",L"Player2"};
static const wchar_t *name_overrides[2];static int rename_override=-1,text_override=-1;
/* Multiplayer > Online ([online] address / port / delay). online_ready: the
 * installed runtime was built with the framework's netplay
 * (runtime\RoadRashNativeResearch.netplay.ini netplay=1, written by the build
 * with its fingerprint). online_launch: the next game start is the online one
 * the handshake prepared (seat online_slot, peer, session file). */
/* Multiplayer > Play online (lobby_page.h): rooms of up to 8 by room code. lobby_ready: an
 * online runtime is installed. room_slots / room_session: set for an online start from a
 * room (session seats, per-match id); 0 for the two-PC Direct connection. */
static int lobby_ready,room_slots;static unsigned room_session;
static int online_ready,online_port=7000,online_delay=2,online_slot,online_launch;static wchar_t online_address[64];static char online_fp[65];
/* The runtime online games start: runtime-netplay (build_rr.py --netplay) when
 * it has netplay, else runtime when that one has it. Offline play keeps runtime. */
static const wchar_t*online_runtime=L"runtime";
static wchar_t online_peer[64],online_session[32768];static int online_peer_port,online_host_aspect,online_host_lod,online_host_delay;
static int mod_count,mod_enabled[8],mod_dump,play_after_mod_check,mod_checked;
static wchar_t mod_names[8][96],mod_ids[8][64],mod_contents[8][96];
static void play_game(void);
static void ol_attach(DWORD pid);static void ol_detach(void);   /* online_link_host.h */
static int game_was_online;static void lb_mmr_apply(void);       /* lobby_page.h: ratings from the online game's races */
static void join(wchar_t *dest,const wchar_t *base,const wchar_t *name);
static int load_project_logo(void){
    HRSRC resource=FindResourceW(NULL,MAKEINTRESOURCEW(102),RT_RCDATA);if(!resource)return 0;
    DWORD bytes=SizeofResource(NULL,resource);BYTE*data=LockResource(LoadResource(NULL,resource));if(!data||!bytes)return 0;
    HRESULT init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);if(FAILED(init))return 0;
    IWICImagingFactory*factory=NULL;IWICStream*stream=NULL;IWICBitmapDecoder*decoder=NULL;IWICBitmapFrameDecode*frame=NULL;IWICFormatConverter*convert=NULL;int ok=0;UINT w=0,h=0;
    if(FAILED(CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&factory)))goto done;
    if(FAILED(IWICImagingFactory_CreateStream(factory,&stream))||FAILED(IWICStream_InitializeFromMemory(stream,data,bytes)))goto done;
    if(FAILED(IWICImagingFactory_CreateDecoderFromStream(factory,(IStream*)stream,NULL,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame)))goto done;
    if(FAILED(IWICBitmapFrameDecode_GetSize(frame,&w,&h))||!w||!h||w>4096||h>4096)goto done;
    if(FAILED(IWICImagingFactory_CreateFormatConverter(factory,&convert))||FAILED(IWICFormatConverter_Initialize(convert,(IWICBitmapSource*)frame,&GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom)))goto done;
    project_logo.pixels=malloc((size_t)w*h*4);if(!project_logo.pixels)goto done;
    if(FAILED(IWICFormatConverter_CopyPixels(convert,NULL,w*4,w*h*4,(BYTE*)project_logo.pixels))){free(project_logo.pixels);project_logo.pixels=NULL;goto done;}project_logo.w=w;project_logo.h=h;ok=1;
done:
    if(convert)IWICFormatConverter_Release(convert);if(frame)IWICBitmapFrameDecode_Release(frame);if(decoder)IWICBitmapDecoder_Release(decoder);if(stream)IWICStream_Release(stream);if(factory)IWICImagingFactory_Release(factory);CoUninitialize();return ok;
}
static void mod_reload(void){
    wchar_t path[32768];join(path,store,L"mods\\catalog.ini");mod_count=GetPrivateProfileIntW(L"catalog",L"count",0,path);if(mod_count<0||mod_count>8)mod_count=0;mod_dump=GetPrivateProfileIntW(L"catalog",L"dump",0,path)!=0;
    for(int i=0;i<mod_count;i++){wchar_t section[16];swprintf(section,16,L"mod%d",i);GetPrivateProfileStringW(section,L"name",L"Mod",mod_names[i],96,path);GetPrivateProfileStringW(section,L"id",L"",mod_ids[i],64,path);GetPrivateProfileStringW(section,L"contents",L"",mod_contents[i],96,path);mod_enabled[i]=GetPrivateProfileIntW(section,L"enabled",0,path)!=0;}
    if(page==3){int n=row_count();if(selected>=n)selected=n?n-1:0;}
}
static const wchar_t *control_names[]={L"Accelerate",L"Brake",L"Steer left",L"Steer right",L"Primary attack",L"Alternate attack",L"Kick",L"Pause",L"Change camera"};
static wchar_t controls[9][40]={L"X",L"Z",L"Left",L"Right",L"W",L"Q",L"R",L"Return",L"Right Shift"};
static wchar_t padcontrols[9][40]={L"a",L"x",L"dpleft",L"dpright",L"rightshoulder",L"leftshoulder",L"righttrigger",L"start",L"back"};
static const wchar_t *resolution_names[]={L"Native",L"720p",L"1080p",L"1440p",L"4K"};
static const char *resolution_values[]={"native","720p","1080p","1440p","4k"};
static const wchar_t *pad_sources[]={L"a",L"b",L"x",L"y",L"leftshoulder",L"rightshoulder",L"lefttrigger",L"righttrigger",L"dpleft",L"dpright",L"dpup",L"dpdown",L"start",L"back",L"leftstick",L"rightstick"};
/* Xbox-style names; launcher.ini / input.ini keep SDL's names (start = Menu, back = View). */
static const wchar_t *pad_labels[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"LT",L"RT",L"D-Pad Left",L"D-Pad Right",L"D-Pad Up",L"D-Pad Down",L"Menu",L"View",L"L-stick click",L"R-stick click"};
/* The same buttons on a PlayStation pad (SDL's positions: a = south = Cross). */
static const wchar_t *pad_labels_ps[]={L"Cross",L"Circle",L"Square",L"Triangle",L"L1",L"R1",L"L2",L"R2",L"D-Pad Left",L"D-Pad Right",L"D-Pad Up",L"D-Pad Down",L"Options",L"Share / Create",L"L3",L"R3"};
/* The kind of the last pad that pressed something: 0 Xbox / XInput, 1 PlayStation
 * (sony_pad.h). Kept as launcher.ini [controls] pad_style for the next start; the
 * stored mappings are the same for both. */
static int pad_ps,preview_pad=-1;   /* preview_pad: --preview-pad, the page drawn as if that pad had just been used */
static const wchar_t *pad_label(const wchar_t *source){for(int i=0;i<16;i++)if(!wcscmp(source,pad_sources[i]))return ((preview_pad>=0?preview_pad:pad_ps)?pad_labels_ps:pad_labels)[i];return source;}
static void pad_kind_used(int playstation){
    if(pad_ps==!!playstation)return;
    pad_ps=!!playstation;WritePrivateProfileStringW(L"controls",L"pad_style",pad_ps?L"playstation":L"xbox",ini);InvalidateRect(mainwindow,NULL,FALSE);
}
static void join(wchar_t *dest,const wchar_t *base,const wchar_t *name){swprintf(dest,32768,L"%ls\\%ls",base,name);}
static int exists(const wchar_t *path){DWORD a=GetFileAttributesW(path);return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
static void ensure(const wchar_t *path){SHCreateDirectoryExW(NULL,path,NULL);}
static int compare_music(const void*a,const void*b){return _wcsicmp(a,b);}
static void music_reload(void){
    wchar_t path[32768],saved[260];join(path,store,L"music\\*.wav");WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(path,&data);music_count=music_index=0;
    if(search!=INVALID_HANDLE_VALUE){do{if(!(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&data.nFileSizeHigh==0&&data.nFileSizeLow>44&&data.nFileSizeLow<=64*1024*1024)wcscpy(music_files[music_count++],data.cFileName);}while(music_count<128&&FindNextFileW(search,&data));FindClose(search);}
    qsort(music_files,music_count,sizeof music_files[0],compare_music);
    GetPrivateProfileStringW(L"music",L"track",L"",saved,260,ini);for(int i=0;i<music_count;i++)if(!wcscmp(saved,music_files[i]))music_index=i;
    wchar_t source[16];GetPrivateProfileStringW(L"music",L"source",L"original",source,16,ini);music_custom=!wcscmp(source,L"custom");
}
static void music_add(void){
    if(!installed){notice(L"INSTALL YOUR GAME BEFORE ADDING MUSIC");return;}
    wchar_t source[32768]=L"",dest[32768],folder[32768];OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=mainwindow;dialog.lpstrFilter=L"Wave audio (*.wav)\0*.wav\0\0";dialog.lpstrFile=source;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;dialog.lpstrTitle=L"Add your own music";
    if(!GetOpenFileNameW(&dialog))return;FILE*f=_wfopen(source,L"rb");char header[12];int valid=f&&fread(header,1,12,f)==12&&!memcmp(header,"RIFF",4)&&!memcmp(header+8,"WAVE",4);if(f)fclose(f);
    WIN32_FILE_ATTRIBUTE_DATA attr;if(!valid||!GetFileAttributesExW(source,GetFileExInfoStandard,&attr)||attr.nFileSizeHigh||attr.nFileSizeLow>64*1024*1024){notice(L"SELECT A VALID WAV UP TO 64 MB");return;}
    join(folder,store,L"music");ensure(folder);join(dest,folder,PathFindFileNameW(source));if(!CopyFileW(source,dest,TRUE)){notice(L"TRACK ALREADY EXISTS OR COULD NOT BE COPIED");return;}music_reload();notice(L"MUSIC ADDED \u2014 SELECT IT IN OPTIONS > AUDIO");
}
static void notice(const wchar_t *text){wcsncpy(status_text,text,255);status_text[255]=0;status_tick=GetTickCount();if(!status_tick)status_tick=1;InvalidateRect(mainwindow,NULL,FALSE);}
static void quick_reveal(void){quick_start=0;ShowWindow(mainwindow,SW_SHOW);SetForegroundWindow(mainwindow);}
static int rider_name_valid(const wchar_t*s){size_t n=wcslen(s);int nonspace=0;if(!n||n>8)return 0;for(size_t i=0;i<n;i++){wchar_t c=s[i];if(c!=L' ')nonspace=1;if(!((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||wcschr(L" '-_.",c)))return 0;}return nonspace;}
static FILETIME file_stamp(const wchar_t*path){WIN32_FILE_ATTRIBUTE_DATA a;FILETIME none={0,0};return GetFileAttributesExW(path,GetFileExInfoStandard,&a)?a.ftLastWriteTime:none;}
/* Reads tuning.ini (missing = built-in values). Problems are listed in
 * tuning-problems.txt and, when `announce`, shown in the toast. */
static void tuning_load(int announce){
    join(tuning_path,store,L"tuning.ini");TniLog log;memset(&log,0,sizeof log);
    tn_load(tuning_path,&tn_live,&log);tuning_stamp=file_stamp(tuning_path);tn_refresh();
    wchar_t problems[32768];join(problems,store,L"tuning-problems.txt");
    if(log.count){FILE*f=_wfopen(problems,L"wb");if(f){fputs("tuning.ini could not use these lines; their built-in values are used instead.\r\n\r\n",f);fputs(log.text,f);fclose(f);}
        if(announce){wchar_t text[160];swprintf(text,160,L"TUNING.INI: %d LINE%ls COULD NOT BE USED — BUILT-IN VALUES KEPT (SEE tuning-problems.txt)",log.count,log.count==1?L"":L"S");notice(text);}}
    else DeleteFileW(problems);
    dev_tuning_reloaded();
}
static void settings(void){
    music_reload();tuning_load(0);
    /* Play always starts Enhanced; the unmodified game is Game installation >
       Play the original game (one launch, play_original). --mode original
       still renders the launcher as Original for tests. */
    enhanced_mode=1;if(mode_override>=0)enhanced_mode=mode_override;
    preset_chosen=GetPrivateProfileIntW(L"launcher",L"preset_chosen",0,ini)!=0;
    hd_ui=GetPrivateProfileIntW(L"display",L"hd_ui",1,ini)!=0;fmv_wide=GetPrivateProfileIntW(L"display",L"fmv_wide",0,ini)!=0;
    for(int i=0;i<2;i++){wchar_t key[12];swprintf(key,12,L"voice%d",i+1);voices[i]=GetPrivateProfileIntW(L"audio",key,0,ini);if(voices[i]<0||voices[i]>27)voices[i]=0;}
    for(int r=0;r<2*RR_LOOK_KINDS;r++){const wchar_t*keys[]={L"paint",L"tag",L"outfit",L"colour"};wchar_t key[12];swprintf(key,12,L"%ls%d",keys[r/2],r%2+1);int v=GetPrivateProfileIntW(L"riders",key,0,ini);*look_setting(r)=v>0&&v<rr_look_count(r/2)?v:0;}
    larger_text=GetPrivateProfileIntW(L"hud",L"larger_text",1,ini)!=0;rename_riders=GetPrivateProfileIntW(L"riders",L"rename",0,ini)!=0;
    if(text_override>=0)larger_text=text_override;if(rename_override>=0)rename_riders=rename_override;
    for(int i=0;i<2;i++){wchar_t key[8];swprintf(key,8,L"name%d",i+1);wchar_t value[64];GetPrivateProfileStringW(L"riders",key,rider_names[i],value,64,ini);if(name_overrides[i])wcscpy(rider_names[i],name_overrides[i]);else if(rider_name_valid(value))wcscpy(rider_names[i],value);}
    smooth_motion=GetPrivateProfileIntW(L"display",L"native_60fps",1,ini);if(smooth_motion<0||smooth_motion>2)smooth_motion=1;   /* Frame rate: 0 Original, 1 60 FPS, 2 120 FPS */
    model_detail=GetPrivateProfileIntW(L"display",L"model_detail",0,ini);if(model_detail<0)model_detail=0;if(model_detail>=tn_live.lod_n)model_detail=tn_live.lod_n-1;
    {const wchar_t*keys[]={L"antialiasing",L"scanlines",L"draw_distance"};int*vals[]={&aa_mode,&scanline_mode,&draw_mode};const int steps[]={rr_aa_steps,rr_scanline_steps,rr_draw_steps};
     for(int i=0;i<3;i++){*vals[i]=GetPrivateProfileIntW(L"display",keys[i],0,ini);if(*vals[i]<0||*vals[i]>=steps[i])*vals[i]=0;}}
    for(int e=0;e<rr_post_effects;e++){wchar_t key[16];swprintf(key,16,L"%hs",rr_post_key(e));post_fx[e]=GetPrivateProfileIntW(L"display",key,0,ini);if(post_fx[e]<0||post_fx[e]>=rr_post_steps)post_fx[e]=0;}
    shadow_mode=GetPrivateProfileIntW(L"display",L"shadow",0,ini);if(shadow_mode<0||shadow_mode>=rr_shadow_steps)shadow_mode=0;
    vsync_on=GetPrivateProfileIntW(L"display",L"vsync",1,ini)!=0;low_latency=GetPrivateProfileIntW(L"display",L"low_latency",1,ini)!=0;
#ifdef RR_PUBLIC_RELEASE
    dev_unlocked=0;
#else
    dev_unlocked=GetPrivateProfileIntW(L"developer",L"unlocked",0,ini)!=0;
#endif
    aspect=GetPrivateProfileIntW(L"display",L"aspect",0,ini);if(aspect<0||aspect>2)aspect=0;
    resolution=GetPrivateProfileIntW(L"display",L"resolution",1,ini);if(resolution<0||resolution>4)resolution=1;
    fullscreen_mode=GetPrivateProfileIntW(L"display",L"fullscreen",0,ini)!=0;
    texture_correction=rr_tc_load(GetPrivateProfileIntW(L"display",L"texture_correction",0,ini));   /* Precise vertices: 0 Off, 2 On (the old On, 1, reads as Off) */
    discord_enabled=GetPrivateProfileIntW(L"discord",L"enabled",0,ini)!=0;
    fast_loading=GetPrivateProfileIntW(L"system",L"fast_loading",1,ini)!=0;
    skip_intro=GetPrivateProfileIntW(L"system",L"skip_intro",1,ini)!=0;
    start_game_directly=GetPrivateProfileIntW(L"launcher",L"start_game",0,ini)!=0;
    GetPrivateProfileStringW(L"discord",L"application_id",L"",discord_app_id,64,ini);
    if(wcslen(discord_app_id)<17||wcslen(discord_app_id)>20)discord_app_id[0]=0;
    for(size_t i=0;discord_app_id[i];i++)if(discord_app_id[i]<L'0'||discord_app_id[i]>L'9'){discord_app_id[0]=0;break;}
    device=GetPrivateProfileIntW(L"controls",L"device",0,ini)!=0;
    {wchar_t style[16];GetPrivateProfileStringW(L"controls",L"pad_style",L"xbox",style,16,ini);pad_ps=!wcscmp(style,L"playstation");}
    p2_device=GetPrivateProfileIntW(L"controls",L"p2_device",0,ini);if(p2_device<0||p2_device>2||(p2_device==2&&!device))p2_device=0;
    p3_device=GetPrivateProfileIntW(L"controls",L"p3_device",0,ini)==1;p4_device=GetPrivateProfileIntW(L"controls",L"p4_device",0,ini)==1;
    filter=GetPrivateProfileIntW(L"display",L"filter",0,ini);if(filter<0||filter>=rr_filter_steps)filter=0;   /* Sharp, Smooth, xBR */
    GetPrivateProfileStringW(L"online",L"address",L"",online_address,64,ini);
    online_port=GetPrivateProfileIntW(L"online",L"port",7000,ini);if(online_port<1024||online_port>65534)online_port=7000;
    online_delay=GetPrivateProfileIntW(L"online",L"delay",2,ini);if(online_delay<1||online_delay>8)online_delay=2;
    online_ready=lobby_ready=0;online_fp[0]=0;online_runtime=L"runtime";
    for(int i=0;i<2&&!online_ready;i++){const wchar_t*dir=i?L"runtime":L"runtime-netplay";wchar_t path[32768],exe[32768],fp[80];
     swprintf(path,32768,L"%ls\\%ls\\RoadRashNativeResearch.netplay.ini",store,dir);swprintf(exe,32768,L"%ls\\%ls\\RoadRashNativeResearch.exe",store,dir);
     GetPrivateProfileStringW(L"build",L"fingerprint",L"",fp,80,path);
     if(wcslen(fp)!=64||GetPrivateProfileIntW(L"build",L"netplay",0,path)!=1||!exists(exe))continue;
     WideCharToMultiByte(CP_UTF8,0,fp,-1,online_fp,65,NULL,NULL);online_runtime=dir;online_ready=1;lobby_ready=1;}
    slowdown_mode=GetPrivateProfileIntW(L"performance",L"slowdown_reduction",0,ini);if(slowdown_mode<0||slowdown_mode>1)slowdown_mode=0;
    for(int k=0;k<ORIG_COUNT;k++){orig[k]=GetPrivateProfileIntW(L"original",orig_keys[k],0,ini);if(orig[k]<0||orig[k]>=orig_steps(k))orig[k]=0;}
    for(int i=0;i<9;i++){wchar_t key[12];swprintf(key,12,L"key%d",i);GetPrivateProfileStringW(L"controls",key,controls[i],controls[i],40,ini);swprintf(key,12,L"pad%d",i);GetPrivateProfileStringW(L"controls",key,padcontrols[i],padcontrols[i],40,ini);}
}
static void save_settings(void){
    WritePrivateProfileStringW(L"display",L"native_60fps",smooth_motion==2?L"2":smooth_motion?L"1":L"0",ini);
    {wchar_t value[8];swprintf(value,8,L"%d",model_detail);WritePrivateProfileStringW(L"display",L"model_detail",value,ini);}
    {const wchar_t*keys[]={L"antialiasing",L"scanlines",L"draw_distance",L"vsync",L"low_latency"};const int vals[]={aa_mode,scanline_mode,draw_mode,vsync_on,low_latency};
     for(int i=0;i<5;i++){wchar_t value[8];swprintf(value,8,L"%d",vals[i]);WritePrivateProfileStringW(L"display",keys[i],value,ini);}}
    for(int e=0;e<rr_post_effects;e++){wchar_t key[16],value[8];swprintf(key,16,L"%hs",rr_post_key(e));swprintf(value,8,L"%d",post_fx[e]);WritePrivateProfileStringW(L"display",key,value,ini);}
    {wchar_t value[8];swprintf(value,8,L"%d",shadow_mode);WritePrivateProfileStringW(L"display",L"shadow",value,ini);}
    {wchar_t value[8];swprintf(value,8,L"%d",aspect);WritePrivateProfileStringW(L"display",L"aspect",value,ini);}
    WritePrivateProfileStringW(L"display",L"hd_ui",hd_ui?L"1":L"0",ini);WritePrivateProfileStringW(L"display",L"fmv_wide",fmv_wide?L"1":L"0",ini);
    /* Retired keys: HD text follows HD UI; Enhanced always skips the boot animation. */
    WritePrivateProfileStringW(L"display",L"hd_font",NULL,ini);WritePrivateProfileStringW(L"system",L"bios_intro",NULL,ini);
    for(int i=0;i<2;i++){wchar_t key[12],value[12];swprintf(key,12,L"voice%d",i+1);swprintf(value,12,L"%d",voices[i]);WritePrivateProfileStringW(L"audio",key,value,ini);}
    for(int r=0;r<2*RR_LOOK_KINDS;r++){const wchar_t*keys[]={L"paint",L"tag",L"outfit",L"colour"};wchar_t key[12],value[12];swprintf(key,12,L"%ls%d",keys[r/2],r%2+1);swprintf(value,12,L"%d",*look_setting(r));WritePrivateProfileStringW(L"riders",key,value,ini);}
    WritePrivateProfileStringW(L"music",L"source",music_custom?L"custom":L"original",ini);
    WritePrivateProfileStringW(L"music",L"track",music_count?music_files[music_index]:L"",ini);
    WritePrivateProfileStringW(L"display",L"fullscreen",fullscreen_mode?L"1":L"0",ini);
    {wchar_t tc[4];swprintf(tc,4,L"%d",texture_correction);WritePrivateProfileStringW(L"display",L"texture_correction",tc,ini);}
    WritePrivateProfileStringW(L"discord",L"enabled",discord_enabled?L"1":L"0",ini);
    WritePrivateProfileStringW(L"system",L"fast_loading",fast_loading?L"1":L"0",ini);
    WritePrivateProfileStringW(L"system",L"skip_intro",skip_intro?L"1":L"0",ini);
    WritePrivateProfileStringW(L"launcher",L"start_game",start_game_directly?L"1":L"0",ini);
    WritePrivateProfileStringW(L"hud",L"larger_text",larger_text?L"1":L"0",ini);WritePrivateProfileStringW(L"riders",L"rename",rename_riders?L"1":L"0",ini);
    for(int i=0;i<2;i++){wchar_t key[8];swprintf(key,8,L"name%d",i+1);WritePrivateProfileStringW(L"riders",key,rider_names[i],ini);}
    wchar_t value[12];swprintf(value,12,L"%d",resolution);WritePrivateProfileStringW(L"display",L"resolution",value,ini);
    swprintf(value,12,L"%d",filter);WritePrivateProfileStringW(L"display",L"filter",value,ini);WritePrivateProfileStringW(L"controls",L"device",device?L"1":L"0",ini);
    swprintf(value,12,L"%d",p2_device);WritePrivateProfileStringW(L"controls",L"p2_device",value,ini);
    WritePrivateProfileStringW(L"controls",L"p3_device",p3_device?L"1":L"0",ini);WritePrivateProfileStringW(L"controls",L"p4_device",p4_device?L"1":L"0",ini);
    swprintf(value,12,L"%d",slowdown_mode);WritePrivateProfileStringW(L"performance",L"slowdown_reduction",value,ini);
    for(int k=0;k<ORIG_COUNT;k++){swprintf(value,12,L"%d",orig[k]);WritePrivateProfileStringW(L"original",orig_keys[k],value,ini);}
    WritePrivateProfileStringW(L"online",L"address",online_address,ini);swprintf(value,12,L"%d",online_port);WritePrivateProfileStringW(L"online",L"port",value,ini);
    swprintf(value,12,L"%d",online_delay);WritePrivateProfileStringW(L"online",L"delay",value,ini);
    for(int i=0;i<9;i++){wchar_t key[12];swprintf(key,12,L"key%d",i);WritePrivateProfileStringW(L"controls",key,controls[i],ini);swprintf(key,12,L"pad%d",i);WritePrivateProfileStringW(L"controls",key,padcontrols[i],ini);}
}
/* Display presets (Options > Display > Preset, and the first-run page): each
 * sets the picture and performance settings below; the window choice, audio,
 * riders and controls are left as they are. Any other combination is Custom.
 * launcher.ini [launcher] preset_chosen: the first-run page was shown. */
enum {PRESET_RECOMMENDED,PRESET_CLASSIC,PRESET_HIGH,PRESET_CUSTOM,PRESET_FIELDS=19};
static const wchar_t*preset_names[]={L"Recommended",L"Classic look",L"High end",L"Custom"};
/* Values: tuning.ini [preset ...] (tuning.h tn_preset_fields, built-in
 * tn_preset_builtin), editable on the Developer page. Fields: resolution,
 * filter, texture correction, HD UI, larger labels, aspect, frame rate, movies
 * wide, level of detail (step; -1 the recommended maximum), anti-aliasing,
 * scanlines, draw distance, vsync, low latency, bloom, sharpening, colour,
 * vignette, shadow smoothing. */
static int*preset_field(int k){int*f[PRESET_FIELDS]={&resolution,&filter,&texture_correction,&hd_ui,&larger_text,&aspect,&smooth_motion,&fmv_wide,&model_detail,&aa_mode,&scanline_mode,&draw_mode,&vsync_on,&low_latency,&post_fx[0],&post_fx[1],&post_fx[2],&post_fx[3],&shadow_mode};return f[k];}
static int preset_value(int p,int k){int v=tn_live.preset[p][k];if(k!=8){const int steps=tn_preset_fields[k].steps;if(v<0||v>=steps)v=0;}if(k==8){if(v<0)v=tn_live.lod_safe;if(v>=tn_live.lod_n)v=tn_live.lod_n-1;if(v<0)v=0;}return v;}
static int current_preset(void){for(int p=0;p<3;p++){int same=1;for(int k=0;k<PRESET_FIELDS&&same;k++)same=*preset_field(k)==preset_value(p,k);if(same)return p;}return PRESET_CUSTOM;}
static void save_settings(void);
static void apply_preset(int p){if(p<0||p>2)return;for(int k=0;k<PRESET_FIELDS;k++)*preset_field(k)=preset_value(p,k);save_settings();}
static void mark_preset_chosen(void){preset_chosen=1;WritePrivateProfileStringW(L"launcher",L"preset_chosen",L"1",ini);}
/* Why a file is not the supported disc, in the player's words. */
static void not_a_disc(const wchar_t*path){
    unsigned char m[8]={0};FILE*f=_wfopen(path,L"rb");
    if(!f){fail("The file could not be opened. If you picked a .cue, keep its .bin in the same folder.");return;}
    size_t n=fread(m,1,8,f);fclose(f);
    if(n>=4&&!memcmp(m,"PK\3\4",4))fail("This is a zip file. Extract it first, then pick the .bin or .cue inside.");
    else if(n>=6&&!memcmp(m,"7z\xBC\xAF\x27\x1C",6))fail("This is a 7z archive. Extract it first, then pick the .bin or .cue inside.");
    else if(n>=4&&!memcmp(m,"Rar!",4))fail("This is a RAR archive. Extract it first, then pick the .bin or .cue inside.");
    else if(n>=8&&!memcmp(m,"MComprHD",8))fail("CHD images aren't supported. Pick a .bin/.cue rip of your disc instead.");
    else fail("This file isn't a PlayStation disc image. Pick the .bin (or .cue) of your Road Rash: Jailbreak disc.");
}
static void other_game(Disc*d,Entry root){
    Entry e;unsigned char*cnf=NULL;char id[16]={0},msg[200];
    if(find_file(d,root,"/SYSTEM.CNF",&e)&&e.size<4096&&(cnf=read_entry(d,e))!=NULL){
        char text[4097];memcpy(text,cnf,e.size);text[e.size]=0;char*b=strstr(text,"cdrom:");
        if(b){b+=6;while(*b=='\\'||*b=='/')b++;int i=0;while(i<15&&b[i]&&b[i]!=';'&&b[i]!='\r'&&b[i]!='\n'&&b[i]!=' ')id[i]=b[i],i++;}}
    free(cnf);
    if(id[0])snprintf(msg,sizeof msg,"This is a different game or another region's version (%s). Only the USA disc, SLUS-01053, is supported.",id);
    else snprintf(msg,sizeof msg,"This disc image has no PlayStation game on it. Pick the .bin (or .cue) of your Road Rash: Jailbreak disc.");
    fail(msg);
}
static int validate_disc(const wchar_t *path){
    Disc d={0};Entry root,entry;unsigned char *bytes=NULL;char hash[65];int ok=0;
    if(!open_disc(path,&d,&root))not_a_disc(path);
    else if(!find_file(&d,root,"/SLUS_010.53",&entry))other_game(&d,root);
    else{bytes=read_entry(&d,entry);if(bytes&&entry.size>=2048&&!memcmp(bytes,"PS-X EXE",8)&&sha256(bytes,entry.size,hash)&&!strcmp(hash,"52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b"))ok=1;
        else fail("This is Road Rash: Jailbreak (USA), but its game file doesn't match the original disc: the copy is modified, patched or damaged. Rip it again from your disc.");}
    free(bytes);if(d.fp)fclose(d.fp);return ok;
}
/* Background, bar and preview artwork come from rr_art_open (owner_assets.c),
 * which validates the installed executable before decoding anything. Text
 * uses our own HD font (hd_font_data.h), so it needs nothing from the disc. */
static int art_generation;
static void artwork(void){rr_art_open(discfile);art_generation++;}
static void forget_artwork(void){rr_art_close();art_generation++;}
static void utf8(FILE*f,const wchar_t*text){int n=WideCharToMultiByte(CP_UTF8,0,text,-1,NULL,0,NULL,NULL);if(!n)return;char *out=malloc(n);if(!out)return;if(WideCharToMultiByte(CP_UTF8,0,text,-1,out,n,NULL,NULL))fputs(out,f);free(out);}
static void toml_path(FILE*f,const wchar_t*path){int n=WideCharToMultiByte(CP_UTF8,0,path,-1,NULL,0,NULL,NULL);char*out=malloc(n?n:1);if(!out)return;if(n&&WideCharToMultiByte(CP_UTF8,0,path,-1,out,n,NULL,NULL)){fputc('"',f);for(char*p=out;*p;p++){if(*p=='\\')fputc('/',f);else{if(*p=='"')fputc('\\',f);fputc(*p,f);}}fputc('"',f);}free(out);}
static int write_runtime_settings(void){
    wchar_t path[32768],runtime[32768],exe[32768],save[32768];join(runtime,store,online_launch?online_runtime:enhanced_mode?L"runtime":L"runtime-original");ensure(runtime);join(exe,store,L"inputs\\SLUS_010.53");join(path,store,enhanced_mode?L"game.toml":L"game-original.toml");join(save,store,L"saves");ensure(save);
    FILE*f=_wfopen(path,L"wb");if(!f)return 0;
    fputs("[game]\nname=\"Road Rash Jailbreak\"\nid=\"SLUS-01053\"\nexe=",f);toml_path(f,exe);fputs("\ndisc=",f);toml_path(f,exists(cuefile)?cuefile:discfile);
    /* Players 3 / 4 on: four seats, which arms the runtime's multitap on port 1
       when the game starts (pads 1-4 on it, all digital: the runtime's
       DualShock-on-tap option leaves Player 1 unread in this game). Off: no
       multitap, as before. */
    if(enhanced_mode&&!online_launch&&(p3_device||p4_device))fputs("\nplayers=4",f);
    fputs("\nload_address=\"0x80010000\"\nentry_pc=\"0x8004032C\"\ntext_size=\"0x4B800\"\nstack_base=\"0x801FFFF0\"\n[recompiler]\nseeds=\"inputs/seeds.txt\"\nstrict=true\nout_dir=\"compile/framework-generated\"\n[runtime]\nwindow_title=\"Road Rash Jailbreak\"\nbios_hle=false\nopenbios=true\n",f);if(enhanced_mode)fputs("idle_skip=true\nfast_boot=true\n",f);fputs("[controller]\n",f);
    /* Enhanced: a controller seat is a DualShock ("analog"); the game's own
       Controllers screen chooses Analog 1/2 or, through the pad's emulated
       ANALOG button, Digital 1-3 (controls_screen.cpp). Keyboards and Original
       keep the plain digital pad. lock_mode pins these modes per seat. */
    const char*p1_mode=enhanced_mode&&device?"analog":"digital",*p2_mode=enhanced_mode&&p2_device==1?"analog":"digital";
    fprintf(f,"p1_device=\"%s\"\np1_mode=\"%s\"\nlock_mode=true\np2_device=\"%s\"\np2_mode=\"%s\"\n[video]\nrenderer=\"opengl\"\nwindow_width=1280\ninternal_resolution=\"%s\"\ndynamic_resolution=false\ntexture_filtering=\"%s\"\n",device?"gamepad":"keyboard",p1_mode,(const char*[]){"none","gamepad","keyboard"}[p2_device],p2_mode,resolution_values[enhanced_mode?resolution:orig[ORIG_RES]],rr_filter_toml(enhanced_mode?filter:orig[ORIG_FILTER]));
    if(enhanced_mode)fprintf(f,"aspect_ratio=\"%s\"\n",(const char*[]){"4:3","16:9","21:9"}[online_launch?online_host_aspect:aspect]);   /* online: the host's (it changes VRAM) */
    if(enhanced_mode)fputs("render_thread=true\nframe_generation=false\n",f);
    /* Display > Anti-aliasing (FXAA; supersampling is the runtime's render scale,
       pc_services.cpp), VSync, Low-latency input, Scanlines. Draw distance is
       read by the runtime from launcher.ini. */
    if(enhanced_mode)fprintf(f,"antialiasing_mode=\"%s\"\nvsync=\"%s\"\nlow_latency_input=%s\nscanlines=%s\nscanline_strength=%.2f\n",
        (const char*[]){"off","fxaa","fxaa_hq"}[rr_aa_post(aa_mode)],vsync_on?"on":"off",low_latency?"true":"false",scanline_mode?"true":"false",scanline_mode?rr_scanline_strength(scanline_mode):.5);
    /* Original: the Original game page's choices (all off: the stock game). */
    if(!enhanced_mode)fprintf(f,"aspect_ratio=\"%s\"\n",(const char*[]){"4:3","16:9","21:9"}[orig[ORIG_ASPECT]]);
    if(!enhanced_mode)fputs("fov_scale=1.0\ngeometry_correction=false\nperspective_texturing=false\nframe_generation=false\nframe_interpolation=false\nsupersample=1.0\nantialiasing_mode=\"off\"\n[timing]\nguest_cycle_scale=1\n",f);
    /* 0.15: the 0.12 "slowdown reduction" (guest_cycle_scale=2 while the race
       overlay was resident) is gone. Native 60 FPS races use the runtime's own
       race-gated fast timing, which a second cycle scale would distort. */
    int ok=!ferror(f);if(fclose(f))ok=0;
    join(path,runtime,L"keybinds.ini");f=_wfopen(path,L"wb");if(f){
        /* A keyboard Player 2 takes the launcher's key map (Player 1 is then on
           a controller); without a [player2] section the runtime would fall
           back to its stock layout. */
        const char *buttons[]={"cross","square","left","right","r1","l1","r2","start","select"};
        for(int p=1;p<=(p2_device==2?2:1);p++){fprintf(f,"[player%d]\nup=Up\ndown=Down\ncircle=S\ntriangle=A\nl2=E\nl3=T\nr3=Y\n",p);
        for(int i=0;i<9;i++){fprintf(f,"%s=",buttons[i]);utf8(f,controls[i]);fputc('\n',f);}const char*axes[]={"rs_up","rs_down","ls_left","ls_right"};for(int i=0;i<4;i++){fprintf(f,"%s=",axes[i]);utf8(f,controls[i]);fputc('\n',f);}fputs("ls_up=Up\nls_down=Down\n",f);}
        if(ferror(f))ok=0;if(fclose(f))ok=0;
    }else ok=0;
    join(path,runtime,L"input.ini");f=_wfopen(path,L"wb");if(f){const char *buttons[]={"cross","square","left","right","r1","l1","r2","start","select"};fputs("[controller]\nenabled=true\ndevice=0\ndeadzone=3277\n[mapping]\nup=dpup\ndown=dpdown\ncircle=b\ntriangle=y\nl2=lefttrigger\nl3=leftstick\nr3=rightstick\n",f);for(int i=0;i<9;i++){fprintf(f,"%s=",buttons[i]);utf8(f,padcontrols[i]);fputc('\n',f);}fputs("ls_left=leftx-\nls_right=leftx+\nls_up=lefty-\nls_down=lefty+\nrs_up=righty-\nrs_down=righty+\nrs_left=rightx-\nrs_right=rightx+\n",f);if(ferror(f))ok=0;if(fclose(f))ok=0;}else ok=0;
    /* Players 3 and 4 live in the runtime's settings.toml (game.toml takes 1 and 2),
       rewritten on every Enhanced launch so an earlier choice never lingers. */
    if(enhanced_mode&&!online_launch){join(path,runtime,L"settings.toml");f=_wfopen(path,L"wb");if(f){fputs("# Written by the launcher on every launch: Players 3 and 4 (controllers on the multitap).\n[controller]\n",f);
        if(p3_device)fputs("p3_device=\"gamepad\"\np3_mode=\"digital\"\n",f);if(p4_device)fputs("p4_device=\"gamepad\"\np4_mode=\"digital\"\n",f);if(ferror(f))ok=0;if(fclose(f))ok=0;}else ok=0;}
    if(enhanced_mode){join(path,runtime,L"rider-options.ini");f=_wfopen(path,L"wb");if(f){fprintf(f,"[pc]\nin_game_menu=1\n[hud]\nlarger_text=%d\n[riders]\nrename=%d\nname1=",larger_text,rename_riders);utf8(f,rider_names[0]);fputs("\nname2=",f);utf8(f,rider_names[1]);fputc('\n',f);if(ferror(f))ok=0;if(fclose(f))ok=0;}else ok=0;}
#ifdef RR_PUBLIC_RELEASE
    /* Public release: the runtime's developer hotkeys are unbound (config.ini
       [KeyMap], read next to the game's exe): Turbo (Tab, F9), the savestate
       menu (F7), the performance overlay (F), rewind, the debug capture mark and
       the scanlines toggle. Alt+Enter fullscreen and keypad volume stay. */
    join(path,runtime,L"config.ini");f=_wfopen(path,L"wb");if(f){fputs("; Written by the launcher on every launch.\n[KeyMap]\nTurbo=None\nTurboToggle=None\nSaveStateMenu=None\nDisplayPerf=None\nRewind=None\nCaptureMark=None\nScanlines=None\n",f);if(ferror(f))ok=0;if(fclose(f))ok=0;}else ok=0;
#endif
    /* Original: no PC menu; the frame rate and Esc choices (rider_menu.cpp), and
       the window mode in the runtime's own settings.toml (rewritten each launch,
       so nothing the runtime saved there outlives the launcher's choices). */
    if(!enhanced_mode){join(path,runtime,L"rider-options.ini");f=_wfopen(path,L"wb");if(f){fprintf(f,"[hud]\nlarger_text=0\n[riders]\nrename=0\n[original]\nframe_rate=%d\nesc_quits=%d\n",orig[ORIG_RATE],orig[ORIG_ESC]);if(ferror(f))ok=0;if(fclose(f))ok=0;}else ok=0;
        join(path,runtime,L"settings.toml");f=_wfopen(path,L"wb");if(f){fprintf(f,"# Original mode uses game-original.toml; the launcher's Original game page sets this.\n[video]\nfullscreen=%d\n",orig[ORIG_FULL]);if(ferror(f))ok=0;if(fclose(f))ok=0;}else ok=0;}
    return ok;
}
/* Each game gets a private environment. Stray developer/engine overrides must
 * not enable an enhancement in Original, or override saved launcher choices.
 * The explicit local test save remains supported; the parent is unchanged. */
static wchar_t *game_environment(void){
    wchar_t *old=GetEnvironmentStringsW();if(!old)return NULL;size_t n=0;
    for(wchar_t*p=old;*p;p+=wcslen(p)+1)n+=wcslen(p)+1;
    wchar_t *out=malloc((n+1024+32768)*sizeof(wchar_t)),*dest=out;
    if(out){const wchar_t owned[]=L"PSX_DYNRES=0";int added=0;for(wchar_t*p=old;*p;p+=wcslen(p)+1){
        if((!_wcsnicmp(p,L"PSX_",4)||!_wcsnicmp(p,L"RR_",3))&&_wcsnicmp(p,L"PSX_LOAD_SLOT=",14))continue;
        if(!added&&_wcsicmp(p,owned)>0){memcpy(dest,owned,sizeof(owned));dest+=sizeof(owned)/sizeof(wchar_t);added=1;}
        size_t len=wcslen(p)+1;memcpy(dest,p,len*sizeof(wchar_t));dest+=len;
    }if(!added){memcpy(dest,owned,sizeof(owned));dest+=sizeof(owned)/sizeof(wchar_t);}
    /* Saves > Quick resume: the runtime loads this player slot once it starts. */
    if(resume_slot>0&&!online_launch){dest+=swprintf(dest,24,L"RR_QUICK_RESUME=%d",resume_slot)+1;resume_slot=0;}
    /* Level of detail: the chosen step of tuning.ini's steps, as the switch
       distance in percent (pc_services.cpp lod_percent reads RR_LOD_SCALE). */
    if(enhanced_mode&&!online_launch&&model_detail>0&&model_detail<tn_live.lod_n)dest+=swprintf(dest,32,L"RR_LOD_SCALE=%d",tn_live.lod[model_detail])+1;
    /* Multiplayer > Online: the framework's LAN delay-sync session (PSX_NET_*)
       and the session setup both PCs agreed on (netplay_session.cpp). The host
       is seat 0 (Player 1); the host's level of detail applies to both. */
    if(online_launch){
        const wchar_t*v[]={L"PSX_NETPLAY=1",L"PSX_NET_TRANSPORT=lan",L"PSX_NET_MODE=delay"};
        for(int i=0;i<3;i++){size_t len=wcslen(v[i])+1;memcpy(dest,v[i],len*sizeof(wchar_t));dest+=len;}
        dest+=swprintf(dest,32,L"PSX_NET_SLOT=%d",online_slot)+1;
        dest+=swprintf(dest,32,L"PSX_NET_SLOTS=%d",room_slots?room_slots:2)+1;
        if(room_slots){   /* a room: the host carries the match on its port (hub), every guest dials it */
            dest+=swprintf(dest,32,L"PSX_NET_SESSION_ID=%u",room_session)+1;
            if(online_slot)dest+=swprintf(dest,48,L"PSX_NET_BIND=0.0.0.0:0")+1;else dest+=swprintf(dest,48,L"PSX_NET_BIND=0.0.0.0:%d",online_port)+1;
            if(online_slot)dest+=swprintf(dest,128,L"PSX_NET_PEER=%ls:%d",online_peer,online_peer_port)+1;
        }else{
        dest+=swprintf(dest,48,L"PSX_NET_BIND=0.0.0.0:%d",online_port+online_slot)+1;
        dest+=swprintf(dest,128,L"PSX_NET_PEER=%ls:%d",online_peer,online_peer_port)+1;}
        dest+=swprintf(dest,32,L"PSX_NET_DELAY=%d",online_host_delay)+1;
        dest+=swprintf(dest,32,L"RR_LOD_SCALE=%d",online_host_lod)+1;
        dest+=swprintf(dest,32768+20,L"RR_NET_SESSION=%ls",online_session)+1;
    }
    *dest=0;}
    FreeEnvironmentStringsW(old);return out;
}
static int run_process(const wchar_t *command,const wchar_t *cwd,HANDLE *process,int hidden){
    wchar_t cmd[65536];wcsncpy(cmd,command,65535);cmd[65535]=0;STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
    HANDLE log=INVALID_HANDLE_VALUE;
    if(hidden==2){wchar_t path[32768];wchar_t name[40];swprintf(name,40,L"runtime-online-p%d.log",online_slot+1);join(path,store,online_launch?name:L"runtime.log");SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        /* A game that never exited (e.g. held by antivirus) can keep its log open: use a second name. */
        if(log==INVALID_HANDLE_VALUE){wcscat(path,L".2.log");log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);}if(log==INVALID_HANDLE_VALUE)return 0;si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=NULL;si.hStdOutput=log;si.hStdError=log;}
    wchar_t *environment=hidden==2?game_environment():NULL;
    if(hidden==2&&!environment){if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);return 0;}
    BOOL ok=CreateProcessW(NULL,cmd,NULL,NULL,hidden==2,(hidden?CREATE_NO_WINDOW:0)|(environment?CREATE_UNICODE_ENVIRONMENT:0),environment,cwd,&si,&pi);
    free(environment);
    if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(!ok)return 0;
    CloseHandle(pi.hThread);*process=pi.hProcess;
    if(hidden==2){wchar_t path[32768];join(path,store,L"runtime.pid");FILE*f=_wfopen(path,L"wb");if(f){fprintf(f,"%lu\n",(unsigned long)pi.dwProcessId);fclose(f);}}
    return 1;
}
/* ---- Setup, mods and Original mode without PowerShell --------------------
 * The game's tools are the verified toolkit (recipe/toolkit.json): its portable
 * Python runs tools/setup_game.py (first-run setup and rebuilds),
 * mod_manager.py and prepare_original.py directly. Only the first run, before
 * that Python exists, is the launcher's own work: download the toolchain
 * archive (WinHTTP), check its size and SHA-256 (BCrypt), unpack it
 * (zip_extract.h). Nothing here needs PowerShell, the Windows shell or
 * tar.exe, so it runs the same under Wine. */
#include <winhttp.h>
#include "zip_extract.h"
static int builder_thread;   /* builder is the setup thread (else a process) */
static int builder_exit(DWORD*code){return builder_thread?GetExitCodeThread(builder,code):GetExitCodeProcess(builder,code);}
static void toolkit_python(wchar_t*out){wchar_t ws[32768];if(workspace[0])wcscpy(ws,workspace);else{
    /* toolkit id: recipe/toolkit.json "id" */
    char id[64]="rr-toolkit";wchar_t path[32768];join(path,appdir,L"recipe\\toolkit.json");FILE*f=_wfopen(path,L"rb");
    if(f){char text[8192]={0};fread(text,1,sizeof text-1,f);fclose(f);char*p=strstr(text,"\"id\"");if(p&&(p=strchr(p+4,'"'))){char*e=strchr(p+1,'"');if(e&&e-p-1<63){memcpy(id,p+1,(size_t)(e-p-1));id[e-p-1]=0;}}}
    wchar_t wid[64];MultiByteToWideChar(CP_UTF8,0,id,-1,wid,64);swprintf(ws,32768,L"%ls\\toolkits\\%ls",store,wid);}
    swprintf(out,32768,L"%ls\\work\\tooling\\toolchain\\python\\python.exe",ws);}
/* A hidden process with its output in store\<log> (NULL: no log). */
static int run_logged(wchar_t*cmd,const wchar_t*logname,HANDLE*process){
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof si;HANDLE log=INVALID_HANDLE_VALUE;
    if(logname){wchar_t path[32768];join(path,store,logname);SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE};
        log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(log!=INVALID_HANDLE_VALUE){si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=si.hStdError=log;}}
    BOOL ok=CreateProcessW(NULL,cmd,NULL,NULL,log!=INVALID_HANDLE_VALUE,CREATE_NO_WINDOW,NULL,appdir,&si,&pi);
    if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(!ok)return 0;CloseHandle(pi.hThread);*process=pi.hProcess;return 1;
}
/* toolkit.json: a field of one archive pin ("toolchain"), as text. */
static int pin_field(const char*json,const char*pin,const char*key,char*out,size_t cap){
    char want[64];snprintf(want,sizeof want,"\"%s\"",pin);const char*p=strstr(json,want);if(!p||!(p=strchr(p,'{')))return 0;const char*end=strchr(p,'}');if(!end)return 0;
    snprintf(want,sizeof want,"\"%s\"",key);const char*k=strstr(p,want);if(!k||k>end||!(k=strchr(k+strlen(want),':')))return 0;k++;while(*k==' ')k++;
    size_t n=0;if(*k=='"'){k++;while(k[n]&&k[n]!='"')n++;}else while(k[n]>='0'&&k[n]<='9')n++;if(!n||n>=cap)return 0;memcpy(out,k,n);out[n]=0;return 1;
}
static void setup_status(const char*message){
    wchar_t path[32768];join(path,store,L"build-status.txt");FILE*f=_wfopen(path,L"wb");if(f){fputs(message,f);fclose(f);}
    join(path,store,L"setup.log");f=_wfopen(path,L"ab");if(f){fprintf(f,"%s\n",message);fclose(f);}
}
static void setup_unpack_progress(unsigned done,unsigned total){char t[96];snprintf(t,sizeof t,"Installing the verified compiler and Python (%u%%)",total?done*100/total:100);
    wchar_t path[32768];join(path,store,L"build-status.txt");FILE*f=_wfopen(path,L"wb");if(f){fputs(t,f);fclose(f);}}
static int file_sha256(const wchar_t*path,char*hex,unsigned long long*bytes){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(f==INVALID_HANDLE_VALUE)return 0;
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE h=NULL;unsigned char digest[32],*buf=malloc(1<<20);int ok=0;*bytes=0;
    if(buf&&BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0&&BCryptCreateHash(alg,&h,NULL,0,NULL,0,0)>=0){DWORD got;ok=1;
        while(ReadFile(f,buf,1<<20,&got,NULL)&&got){if(BCryptHashData(h,buf,got,0)<0){ok=0;break;}*bytes+=got;}
        if(ok&&BCryptFinishHash(h,digest,32,0)<0)ok=0;}
    if(h)BCryptDestroyHash(h);if(alg)BCryptCloseAlgorithmProvider(alg,0);free(buf);CloseHandle(f);
    if(ok)for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",digest[i]);return ok;
}
static int archive_ok(const wchar_t*path,const char*size,const char*sha){char hex[65];unsigned long long n;return file_sha256(path,hex,&n)&&n==_strtoui64(size,NULL,10)&&!strcmp(hex,sha);}
/* HTTPS GET (redirects followed) into a file. */
static int http_download(const char*url,const wchar_t*out){
    wchar_t wurl[2048],host[256],pathq[2048];MultiByteToWideChar(CP_UTF8,0,url,-1,wurl,2048);
    URL_COMPONENTS u;memset(&u,0,sizeof u);u.dwStructSize=sizeof u;u.lpszHostName=host;u.dwHostNameLength=256;u.lpszUrlPath=pathq;u.dwUrlPathLength=2048;
    if(!WinHttpCrackUrl(wurl,0,0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS)return 0;
    int ok=0;HINTERNET s=WinHttpOpen(L"JailbreakRecomp-setup",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0),c=NULL,r=NULL;
    if(!s)s=WinHttpOpen(L"JailbreakRecomp-setup",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(s){WinHttpSetTimeouts(s,30000,30000,60000,600000);c=WinHttpConnect(s,host,u.nPort,0);}
    if(c)r=WinHttpOpenRequest(c,L"GET",pathq,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(r&&WinHttpSendRequest(r,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(r,NULL)){
        DWORD code=0,len=sizeof code;WinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&len,WINHTTP_NO_HEADER_INDEX);
        HANDLE f=code==200?CreateFileW(out,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
        if(f!=INVALID_HANDLE_VALUE){static char buf[1<<16];DWORD got,wrote;ok=1;
            while(WinHttpReadData(r,buf,sizeof buf,&got)){if(!got)break;if(!WriteFile(f,buf,got,&wrote,NULL)||wrote!=got){ok=0;break;}}
            CloseHandle(f);}}
    if(r)WinHttpCloseHandle(r);if(c)WinHttpCloseHandle(c);if(s)WinHttpCloseHandle(s);return ok;
}
static void remove_tree(const wchar_t*dir_in){
    /* extended-length form: toolchain files nest past 260 characters */
    wchar_t dir[32768];if(wcsncmp(dir_in,L"\\\\?\\",4))swprintf(dir,32768,L"\\\\?\\%ls",dir_in);else wcscpy(dir,dir_in);
    WIN32_FIND_DATAW d;wchar_t pat[32768],p[32768];swprintf(pat,32768,L"%ls\\*",dir);HANDLE h=FindFirstFileW(pat,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;join(p,dir,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)remove_tree(p);else{SetFileAttributesW(p,FILE_ATTRIBUTE_NORMAL);DeleteFileW(p);}}while(FindNextFileW(h,&d));FindClose(h);}
    RemoveDirectoryW(dir);
}
/* First-run bootstrap (the toolchain, when its Python is not there yet), then setup_game.py. */
static unsigned __stdcall setup_worker(void*arg){
    (void)arg;wchar_t python[32768],path[32768];toolkit_python(python);
    join(path,store,L"setup.log");FILE*lf=_wfopen(path,L"wb");if(lf)fclose(lf);
    if(!exists(python)){
        if(workspace[0]){setup_status("SETUP FAILED: the selected toolkit has no portable Python.");return 1;}
        char json[8192]={0},file[128],url[1024],size[32],sha[80];join(path,appdir,L"recipe\\toolkit.json");FILE*f=_wfopen(path,L"rb");if(f){fread(json,1,sizeof json-1,f);fclose(f);}
        if(!pin_field(json,"toolchain","file",file,sizeof file)||!pin_field(json,"toolchain","url",url,sizeof url)||!pin_field(json,"toolchain","size",size,sizeof size)||!pin_field(json,"toolchain","sha256",sha,sizeof sha)){setup_status("SETUP FAILED: recipe/toolkit.json is damaged. Re-download this release.");return 1;}
        /* python path = <ws>\work\tooling\toolchain\python\python.exe */
        wchar_t ws[32768];wcscpy(ws,python);for(int up=0;up<4;up++){wchar_t*s=wcsrchr(ws,L'\\');if(s)*s=0;}
        { wchar_t*s=wcsrchr(ws,L'\\');if(s)*s=0; }   /* drop "work" */
        wchar_t receipt[32768];join(receipt,ws,L"toolkit-ready.json");
        if(exists(receipt)){setup_status("SETUP FAILED: Installed build tool is damaged. Remove only the toolkits folder to prepare it again.");return 1;}
        if(GetFileAttributesW(ws)!=INVALID_FILE_ATTRIBUTES)remove_tree(ws);   /* an unfinished earlier attempt */
        wchar_t cache[32768],zip[32768],partial[32768],wfile[128];join(cache,store,L"downloads");ensure(cache);MultiByteToWideChar(CP_UTF8,0,file,-1,wfile,128);join(zip,cache,wfile);swprintf(partial,32768,L"%ls.partial",zip);
        if(archive_ok(zip,size,sha)){char t[200];snprintf(t,sizeof t,"Verified cached download: %s",file);setup_status(t);}
        else{char t[200];snprintf(t,sizeof t,"Downloading build tools: %s (first installation only)",file);setup_status(t);int got=0;
            for(int attempt=1;attempt<=3&&!got;attempt++){got=http_download(url,partial)&&archive_ok(partial,size,sha);if(!got&&attempt<3){snprintf(t,sizeof t,"Retrying download %s",file);setup_status(t);Sleep(3000);}}
            if(!got||!MoveFileExW(partial,zip,MOVEFILE_REPLACE_EXISTING)){DeleteFileW(partial);setup_status("SETUP FAILED: the build tools could not be downloaded (or failed their SHA-256 check). Check your internet connection and try again.");return 1;}}
        setup_status("Installing the verified compiler and Python");
        wchar_t stage[32768],dest[32768];swprintf(stage,32768,L"%ls\\toolkits\\new-%llu",store,(unsigned long long)GetTickCount64()%1000000);join(dest,stage,L"work\\tooling\\toolchain");ensure(dest);
        char why[300];if(!zip_extract(zip,dest,setup_unpack_progress,why,sizeof why)){remove_tree(stage);char t[400];snprintf(t,sizeof t,"SETUP FAILED: the build tools could not be unpacked: %s",why);setup_status(t);return 1;}
        wchar_t parent[32768];wcscpy(parent,ws);{wchar_t*s=wcsrchr(parent,L'\\');if(s)*s=0;}ensure(parent);
        if(!MoveFileW(stage,ws)){remove_tree(stage);setup_status("SETUP FAILED: the build tools could not be put in place.");return 1;}
        if(!exists(python)){setup_status("SETUP FAILED: the build tools have no portable Python.");return 1;}
    }
    wchar_t script[32768],cmd[65536];join(script,appdir,L"tools\\setup_game.py");
    swprintf(cmd,65536,L"\"%ls\" \"%ls\" --data-root \"%ls\"",python,script,store);
    if(workspace[0]){size_t n=wcslen(cmd);swprintf(cmd+n,65536-n,L" --workspace \"%ls\"",workspace);}
    HANDLE p;if(!run_logged(cmd,NULL,&p)){setup_status("SETUP FAILED: the build tools' Python could not start.");return 1;}
    WaitForSingleObject(p,INFINITE);DWORD code=1;GetExitCodeProcess(p,&code);CloseHandle(p);return code;
}
static void prepare_original(void){
    wchar_t python[32768],script[32768],cmd[65536],status[32768];toolkit_python(python);join(script,appdir,L"tools\\prepare_original.py");join(status,store,L"launch-status.txt");DeleteFileW(status);
    if(!exists(python)){play_after_mod_check=0;notice(L"Prepare the local game build first (Game installation).");return;}
    swprintf(cmd,65536,L"\"%ls\" \"%ls\" --data-root \"%ls\"",python,script,store);
    if(!run_logged(cmd,L"launch.log",&builder)){play_after_mod_check=0;notice(L"Original mode could not be prepared. See launch.log.");return;}
    builder_thread=0;busy=4;notice(L"PREPARING ORIGINAL MODE \u2014 YOUR ENHANCED SETTINGS ARE KEPT");
}
static void build_game(void){
    wchar_t status[32768];join(status,store,L"build-status.txt");DeleteFileW(status);
    uintptr_t t=_beginthreadex(NULL,0,setup_worker,NULL,0,NULL);if(!t){notice(L"Local build tools are unavailable. See the setup guide.");return;}
    builder=(HANDLE)t;builder_thread=1;busy=2;notice(L"PREPARING YOUR LOCAL NATIVE BUILD \u2014 THIS MAY TAKE A FEW MINUTES");
}
static void run_mod(const wchar_t*operation,const wchar_t*value){
    if(busy)return;
    if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"Close your game before changing mods.");play_after_mod_check=0;return;}}
    wchar_t python[32768],script[32768],cmd[65536],status[32768];join(status,store,L"mods\\status.txt");DeleteFileW(status);toolkit_python(python);join(script,appdir,L"tools\\mod_manager.py");
    if(!exists(python)){notice(L"Prepare the local game build first (Game installation).");play_after_mod_check=0;return;}
    swprintf(cmd,65536,L"\"%ls\" \"%ls\" --data-root \"%ls\" --operation %ls",python,script,store,operation);
    size_t n=wcslen(cmd);if(value)swprintf(cmd+n,65536-n,L" %ls \"%ls\"",!wcscmp(operation,L"install")?L"--archive":L"--id",value);
    n=wcslen(cmd);swprintf(cmd+n,65536-n,L" --version 1.0.0");
    if(!run_logged(cmd,L"mods.log",&builder)){notice(L"The mod manager could not start. See mods.log.");play_after_mod_check=0;return;}builder_thread=0;busy=3;notice(L"CHECKING YOUR MODS \u2014 YOUR INSTALLED DISC STAYS UNCHANGED");
}
static void import_mod(void){
    if(!installed){notice(L"Install your original game and prepare its local build first.");return;}wchar_t path[32768]={0};OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=mainwindow;dialog.lpstrFilter=L"Jailbreak mod (*.rrmod)\0*.rrmod\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrTitle=L"Install a mod";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&dialog))run_mod(L"install",path);
}
/* Game installation > Play the original game: one launch of the stock game
 * (runtime-original, no mods, no PC features); Play stays Enhanced. The mode
 * is held through the asynchronous Original preparation and put back after. */
static int original_once;
static void original_done(void){if(original_once){original_once=0;enhanced_mode=1;}}
static void play_game_now(void);
static void play_game(void){play_game_now();if(original_once&&!play_after_mod_check)original_done();}
static void play_original(void){if(busy)return;original_once=1;enhanced_mode=0;mod_checked=0;play_game();}
static void play_game_now(void){
    /* An online start holds through the mod check (asynchronous) only. */
    const int online=online_launch;online_launch=0;
    if(!installed){notice(L"Import your BIN/CUE or ISO once to install the game.");return;}if(busy)return;
    if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"Your game is already running. Return to its window.");return;}CloseHandle(game_process);game_process=NULL;}
    if(!validate_disc(discfile)){installed=0;forget_artwork();notice(L"Installed game data is missing or damaged. Import your game again.");return;}
    wchar_t exe[32768],cwd[32768],config[32768],saves[32768],cmd[65536];join(cwd,store,L"runtime");join(exe,cwd,L"RoadRashNativeResearch.exe");
    wchar_t receipt[32768];join(receipt,store,L"local-build.json");
    if(!exists(exe)||!exists(receipt)){build_game();return;}
    if(enhanced_mode){FILE*f=_wfopen(receipt,L"rb");char data[4096]={0};if(f){fread(data,1,sizeof(data)-1,f);fclose(f);}if(!strstr(data,"rr-pc-1.1")){build_game();return;}}   /* a game built by an older release: rebuild (1.1 adds the online game) */
    if(!mod_checked){play_after_mod_check=1;if(enhanced_mode)run_mod(L"sync",NULL);else prepare_original();online_launch=online&&play_after_mod_check;return;}mod_checked=0;
    online_launch=online;
    join(cwd,store,online?online_runtime:enhanced_mode?L"runtime":L"runtime-original");join(exe,cwd,L"RoadRashNativeResearch.exe");if(!write_runtime_settings()){online_launch=0;notice(L"Game settings could not be written. Check your installation folder and free disk space.");return;}join(config,store,enhanced_mode?L"game.toml":L"game-original.toml");join(saves,store,L"saves");
    /* Online: a debug port per seat, so two instances can share one PC for tests. */
    swprintf(cmd,65536,L"\"%ls\" --game \"%ls\" --debug-port %d --memcard-dir \"%ls\"",exe,config,online_launch?4380+online_slot:4376,saves);
    sb_backup_cards();
    int started=run_process(cmd,cwd,&game_process,2);
    if(started){if(online_launch)ol_detach();else ol_attach(GetProcessId(game_process));game_was_online=online_launch;}   /* Online from the game's menus; ratings after an online game */
    if(started&&quick_start)quick_start=2;
    if(started)notice(online_launch?L"ONLINE GAME RUNNING \u2014 CLOSE ITS WINDOW TO RETURN HERE":enhanced_mode?L"ENHANCED GAME RUNNING \u2014 CLOSE ITS WINDOW TO RETURN HERE":L"ORIGINAL GAME RUNNING \u2014 CLOSE ITS WINDOW TO RETURN HERE");else notice(L"The local game build could not start. See the setup guide.");
    online_launch=0;room_slots=0;
}
static DWORD CALLBACK copy_progress(LARGE_INTEGER total,LARGE_INTEGER done,LARGE_INTEGER stream,LARGE_INTEGER streamdone,DWORD number,DWORD reason,HANDLE src,HANDLE dst,LPVOID context){(void)stream;(void)streamdone;(void)number;(void)reason;(void)src;(void)dst;(void)context;if(total.QuadPart)InterlockedExchange(&progress,(LONG)(done.QuadPart*100/total.QuadPart));return PROGRESS_CONTINUE;}
static unsigned __stdcall import_worker(void *argument){
    wchar_t *source=argument,temp[32768];join(temp,store,L"installed-disc.importing");BOOL cancel=FALSE;
    error_text[0]=0;int ok=validate_disc(source);
    if(ok&&!(CopyFileExW(source,temp,copy_progress,NULL,&cancel,0)&&validate_disc(temp)&&MoveFileExW(temp,discfile,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))){
        ok=0;DeleteFileW(temp);fail("Your game file is fine, but it couldn't be copied into the launcher's folder. Check your free disk space and try again.");}
    free(source);PostMessageW(mainwindow,WM_APP+1,ok,0);return 0;
}
static int resolve_cue(wchar_t *path){
    if(_wcsicmp(PathFindExtensionW(path),L".cue"))return 1;
    FILE*f=_wfopen(path,L"rb");if(!f)return 0;char bytes[8193];size_t n=fread(bytes,1,8192,f);fclose(f);bytes[n]=0;
    if(n==8192||!strstr(bytes,"INDEX 01 00:00:00")||strstr(bytes,"TRACK 02"))return fail("This .cue describes more than one track, which isn't the Road Rash: Jailbreak (USA) layout. Pick the game's .bin file directly.");
    char *first=strstr(bytes,"FILE \"");if(!first)return fail("This .cue doesn't name a .bin file. Pick the .bin file directly.");first+=6;char*last=strchr(first,'\"');if(!last||last-first>2000)return fail("This .cue's .bin file name couldn't be read. Pick the .bin file directly.");*last=0;wchar_t name[2048];if(!MultiByteToWideChar(CP_UTF8,0,first,-1,name,2048))return 0;
    PathRemoveFileSpecW(path);wcscat(path,L"\\");wcscat(path,name);return 1;
}
static void import_disc(void){
    if(busy)return;wchar_t chosen[32768]={0};OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=mainwindow;dialog.lpstrFilter=L"Road Rash game disc (*.bin;*.iso;*.cue)\0*.bin;*.iso;*.cue\0";dialog.lpstrFile=chosen;dialog.nMaxFile=32768;dialog.lpstrTitle=L"Import your original Road Rash: Jailbreak (USA)";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog))return;
    if(!resolve_cue(chosen)){wchar_t err[256];MultiByteToWideChar(CP_UTF8,0,error_text,-1,err,256);MessageBoxW(mainwindow,err,L"Game import",MB_ICONWARNING);return;}
    busy=1;progress=0;notice(L"INSTALLING YOUR GAME DATA \u2014 YOU ONLY NEED TO IMPORT ONCE");
    wchar_t *copy=_wcsdup(chosen);uintptr_t thread=_beginthreadex(NULL,0,import_worker,copy,0,NULL);if(!thread){free(copy);busy=0;notice(L"Could not start the game import.");}else CloseHandle((HANDLE)thread);
}
#include "launcher_skin.h"

/* Per-monitor DPI awareness where available (Windows 10 1703+), otherwise
 * system DPI. Rendering is resolution independent; DPI only sizes windows. */
static UINT (WINAPI*dpi_for_window)(HWND);
static BOOL (WINAPI*adjust_rect_for_dpi)(LPRECT,DWORD,BOOL,DWORD,UINT);
static void dpi_setup(void){
    HMODULE user=GetModuleHandleW(L"user32.dll");BOOL (WINAPI*set_context)(HANDLE)=NULL;
    if(user){*(FARPROC*)&set_context=GetProcAddress(user,"SetProcessDpiAwarenessContext");*(FARPROC*)&dpi_for_window=GetProcAddress(user,"GetDpiForWindow");*(FARPROC*)&adjust_rect_for_dpi=GetProcAddress(user,"AdjustWindowRectExForDpi");}
    if(!set_context||!set_context((HANDLE)(INT_PTR)-4))SetProcessDPIAware();
}
static UINT window_dpi(HWND window){if(dpi_for_window&&window){UINT dpi=dpi_for_window(window);if(dpi)return dpi;}HDC dc=GetDC(NULL);int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);return dpi>0?(UINT)dpi:96;}
static SIZE window_size_for_client(int width,int height,UINT dpi){RECT r={0,0,width,height};if(adjust_rect_for_dpi)adjust_rect_for_dpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);else AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0);return (SIZE){r.right-r.left,r.bottom-r.top};}
/* Opens at 1280x720 client pixels scaled for DPI, shrunk to fit the monitor. */
static void place_main_window(void){
    UINT dpi=window_dpi(mainwindow);MONITORINFO mi={0};mi.cbSize=sizeof mi;GetMonitorInfoW(MonitorFromWindow(mainwindow,MONITOR_DEFAULTTONEAREST),&mi);
    int cw=MulDiv(1280,dpi,96),ch=MulDiv(720,dpi,96);SIZE frame=window_size_for_client(cw,ch,dpi);frame.cx-=cw;frame.cy-=ch;
    int aw=mi.rcWork.right-mi.rcWork.left,ah=mi.rcWork.bottom-mi.rcWork.top;double fit=fmin(1.0,fmin((aw*.92-frame.cx)/cw,(ah*.92-frame.cy)/ch));
    cw=(int)(cw*fit);ch=(int)(ch*fit);SetWindowPos(mainwindow,NULL,mi.rcWork.left+(aw-cw-frame.cx)/2,mi.rcWork.top+(ah-ch-frame.cy)/2,cw+frame.cx,ch+frame.cy,SWP_NOZORDER|SWP_NOACTIVATE);
}
/* Navigation keeps a short history so Back returns to the row you came from. */
static int nav_depth,nav_pages[16],nav_rows[16];
/* History holds the page and row being viewed (also from a native handler). */
static void open_page(int target){if(target==SETUP_PAGE&&!preview_only)mark_preset_chosen();if(nav_depth<16){nav_pages[nav_depth]=nat_vp>=0?nat_vp:page;nav_rows[nav_depth]=nat_vp>=0?nat_vs:selected;nav_depth++;}page=target;selected=0;list_first=0;binding=-1;InvalidateRect(mainwindow,NULL,FALSE);}
static int dev_leave_ok(void);
static int lb_back(void);
static void go_back(void){
    if(lb_back())return;   /* Find a game: back from the room leaves it (the list follows) */
    int parent=parent_page();if(parent==page)return;binding=-1;
    if(page==DEV_PAGE&&!dev_leave_ok())return;
    if(nav_depth&&nav_pages[nav_depth-1]==parent)selected=nav_rows[--nav_depth];else{nav_depth=0;selected=0;}
    page=parent;list_first=0;InvalidateRect(mainwindow,NULL,FALSE);
}

typedef struct NamePrompt {HWND edit;wchar_t name[9];int accepted;} NamePrompt;
static LRESULT CALLBACK name_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return DefWindowProcW(window,msg,wp,lp);}
    NamePrompt*p=(NamePrompt*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(msg==WM_COMMAND){if(LOWORD(wp)==IDOK){wchar_t text[32];GetWindowTextW(p->edit,text,32);if(!rider_name_valid(text)){MessageBoxW(window,L"Use 1\u20138 letters, numbers, spaces or ' - _ .",L"Choose your rider name",MB_OK);return 0;}wcscpy(p->name,text);p->accepted=1;DestroyWindow(window);return 0;}if(LOWORD(wp)==IDCANCEL){DestroyWindow(window);return 0;}}
    if(msg==WM_CLOSE){DestroyWindow(window);return 0;}return DefWindowProcW(window,msg,wp,lp);
}
static void edit_rider_name(int seat){
    WNDCLASSW wc={0};wc.lpfnWndProc=name_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"JailbreakRiderName";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    NamePrompt state={0};wchar_t title[48];swprintf(title,48,L"Player %d rider name",seat+1);RECT r;GetWindowRect(mainwindow,&r);UINT dpi=window_dpi(mainwindow);
#define D(v) MulDiv(v,dpi,96)
    HWND dialog=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,title,WS_CAPTION|WS_SYSMENU, r.left+(r.right-r.left-D(430))/2,r.top+(r.bottom-r.top-D(220))/2,D(430),D(220),mainwindow,NULL,wc.hInstance,&state);if(!dialog)return;
    HWND label=CreateWindowW(L"STATIC",L"Choose a name (up to 8 characters)",WS_CHILD|WS_VISIBLE,D(22),D(19),D(382),D(35),dialog,NULL,wc.hInstance,NULL);
    state.edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",rider_names[seat],WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,D(22),D(62),D(380),D(35),dialog,(HMENU)100,wc.hInstance,NULL);SendMessageW(state.edit,EM_LIMITTEXT,8,0);
    HWND ok=CreateWindowW(L"BUTTON",L"Save name",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,D(198),D(123),D(113),D(34),dialog,(HMENU)IDOK,wc.hInstance,NULL),cancel=CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,D(318),D(123),D(83),D(34),dialog,(HMENU)IDCANCEL,wc.hInstance,NULL);
    HFONT font=CreateFontW(-D(20),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
#undef D
    HWND widgets[]={label,state.edit,ok,cancel};for(int i=0;i<4;i++)SendMessageW(widgets[i],WM_SETFONT,(WPARAM)font,TRUE);
    EnableWindow(mainwindow,FALSE);ShowWindow(dialog,SW_SHOW);SetFocus(state.edit);SendMessageW(state.edit,EM_SETSEL,0,-1);MSG msg;
    while(IsWindow(dialog)&&GetMessageW(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN){SendMessageW(dialog,WM_COMMAND,IDOK,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_TAB){HWND next=GetNextDlgTabItem(dialog,GetFocus(),GetKeyState(VK_SHIFT)<0);if(next)SetFocus(next);continue;}
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    EnableWindow(mainwindow,TRUE);SetForegroundWindow(mainwindow);DeleteObject(font);if(state.accepted){wcscpy(rider_names[seat],state.name);save_settings();notice(L"RIDER NAME SAVED \u2014 APPLIES ON YOUR NEXT ENHANCED LAUNCH");}
}
#include "cheats_page.h"
#include "dev_editor.h"
/* ---- Multiplayer > Online: session setup ------------------------------------
 * Before the game starts, the two launchers exchange one short text message
 * over TCP (host listens on its port; the joining PC connects): the build
 * fingerprint, each player's rider name, custom-names switch, taunt voice and
 * rider look (their Player 1 choices), each side's UDP port and, from the host,
 * the settings that change game state (frame rate, aspect, level of detail,
 * draw distance, loading speed, larger labels) and the input delay. Different
 * builds refuse to play. Both write the same session file (seat 1 = host), the
 * runtime applies both seats on both PCs (netplay_session.cpp), and the game
 * starts with the framework's LAN delay-sync session (PSX_NET_*). Ports: the
 * host's port (TCP + UDP) and the next one on the joining PC (UDP). */
static SOCKET net_listen=INVALID_SOCKET,net_conn=INVALID_SOCKET;static volatile LONG net_cancelled;
static wchar_t net_error[200];
static int mmr_get(void);static int lb_bike(int r);static int lb_voted_race;static unsigned lb_seed;   /* lobby_page.h: rating, bikes per rank, the room's vote */
static void net_message(char*out,size_t cap,int slot){
    char name[40]={0};WideCharToMultiByte(CP_UTF8,0,rider_names[0],-1,name,sizeof name,NULL,NULL);
    int n=snprintf(out,cap,"RRJB-NET 1\nfingerprint=%s\nslot=%d\nudp=%d\nname=%s\nrename=%d\nvoice=%d\npaint=%d\ntag=%d\noutfit=%d\ncolour=%d\nmmr=%d\nbike_r0=%d\nbike_r1=%d\nbike_r2=%d\n",
        online_fp,slot,online_port+slot,name,rename_riders,voices[0],paints[0],tags[0],outfits[0],colours[0],mmr_get(),lb_bike(0),lb_bike(1),lb_bike(2));
    if(!slot&&n>0&&(size_t)n<cap)n+=snprintf(out+n,cap-n,"delay=%d\nnative_60fps=%d\naspect=%d\nlod_percent=%d\ndraw_distance=%d\nfast_loading=%d\nlarger_text=%d\nrace=%d\nseed=%u\n",
        online_delay,smooth_motion,aspect,det_percent(model_detail),draw_mode,fast_loading,larger_text,lb_voted_race,lb_seed);
    if(n>0&&(size_t)n<cap)snprintf(out+n,cap-n,"end\n");
}
/* One "key=value" line of a message (up to cap-1 characters). */
static int net_get(const char*msg,const char*key,char*out,size_t cap){
    size_t k=strlen(key);out[0]=0;
    for(const char*p=msg;p&&*p;p=strchr(p,'\n'),p=p?p+1:NULL)if(!strncmp(p,key,k)&&p[k]=='='){const char*v=p+k+1;size_t n=strcspn(v,"\r\n");if(n>=cap)n=cap-1;memcpy(out,v,n);out[n]=0;return 1;}
    return 0;
}
static int net_int(const char*msg,const char*key,int fallback){char v[16];return net_get(msg,key,v,sizeof v)&&v[0]?atoi(v):fallback;}
static int net_exchange(SOCKET s,const char*mine,char*theirs,size_t cap){
    DWORD t=15000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&t,sizeof t);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&t,sizeof t);
    int n=(int)strlen(mine);if(send(s,mine,n,0)!=n)return 0;
    size_t got=0;theirs[0]=0;
    while(got<cap-1){int r=recv(s,theirs+got,(int)(cap-1-got),0);if(r<=0)break;got+=(size_t)r;theirs[got]=0;if(strstr(theirs,"\nend\n"))return !strncmp(theirs,"RRJB-NET 1\n",11);}
    return 0;
}
/* Session file for this seat (two launchers on one PC write their own). */
static int net_write_session(int slot,const char*host,const char*guest){
    join(online_session,store,slot?L"netplay-session-p2.ini":L"netplay-session-p1.ini");DeleteFileW(online_session);int ok=1;
    for(int seat=0;seat<2;seat++){const char*m=seat?guest:host;wchar_t sec[8];swprintf(sec,8,L"seat%d",seat+1);
        const char*keys[]={"name","rename","voice","paint","tag","outfit","colour","bike_r0","bike_r1","bike_r2"};
        for(int k=0;k<10;k++){char v[96];wchar_t key[16],w[96];net_get(m,keys[k],v,sizeof v);MultiByteToWideChar(CP_UTF8,0,keys[k],-1,key,16);if(!MultiByteToWideChar(CP_UTF8,0,v,-1,w,96))w[0]=0;ok=WritePrivateProfileStringW(sec,key,w,online_session)&&ok;}}
    const char*host_keys[]={"native_60fps","aspect","lod_percent","draw_distance","fast_loading","larger_text","delay","race","seed"};
    for(int k=0;k<9;k++){wchar_t key[24],w[16];MultiByteToWideChar(CP_UTF8,0,host_keys[k],-1,key,24);swprintf(w,16,L"%d",net_int(host,host_keys[k],-1));ok=WritePrivateProfileStringW(L"host",key,w,online_session)&&ok;}
    wchar_t w[160];swprintf(w,160,L"%d",slot);ok=WritePrivateProfileStringW(L"session",L"slot",w,online_session)&&ok;
    swprintf(w,160,L"%hs",online_fp);ok=WritePrivateProfileStringW(L"session",L"fingerprint",w,online_session)&&ok;
    /* For reference (the launcher passes these as PSX_NET_*). */
    swprintf(w,160,L"0.0.0.0:%d",online_port+slot);ok=WritePrivateProfileStringW(L"launch",L"bind",w,online_session)&&ok;
    swprintf(w,160,L"%ls:%d",online_peer,online_peer_port);ok=WritePrivateProfileStringW(L"launch",L"peer",w,online_session)&&ok;
    swprintf(w,160,L"%d",online_host_delay);ok=WritePrivateProfileStringW(L"launch",L"delay",w,online_session)&&ok;
    WritePrivateProfileStringW(NULL,NULL,NULL,online_session);return ok;
}
/* Host (slot 0) or join (slot 1). 1 = session file written, else net_error. */
static int net_handshake(int slot){
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)){wcscpy(net_error,L"NETWORK UNAVAILABLE");return 0;}
    char mine[2048],theirs[2048];net_message(mine,sizeof mine,slot);int ok=0;net_error[0]=0;
    if(!slot){
        SOCKET l=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons((u_short)online_port);a.sin_addr.s_addr=htonl(INADDR_ANY);
        if(l==INVALID_SOCKET||bind(l,(struct sockaddr*)&a,sizeof a)||listen(l,1)){swprintf(net_error,200,L"PORT %d IS IN USE — CHOOSE ANOTHER",online_port);if(l!=INVALID_SOCKET)closesocket(l);WSACleanup();return 0;}
        net_listen=l;struct sockaddr_in peer;int len=sizeof peer;
        for(int waited=0;waited<300&&!net_cancelled;waited++){fd_set r;FD_ZERO(&r);FD_SET(l,&r);struct timeval tv={0,500000};int s=select(0,&r,NULL,NULL,&tv);if(s<0)break;if(s==0)continue;
            net_conn=accept(l,(struct sockaddr*)&peer,&len);break;}
        net_listen=INVALID_SOCKET;closesocket(l);
        if(net_conn==INVALID_SOCKET){wcscpy(net_error,net_cancelled?L"HOSTING CANCELLED":L"NOBODY JOINED WITHIN 2.5 MINUTES");WSACleanup();return 0;}
        char ip[64]={0};inet_ntop(AF_INET,&peer.sin_addr,ip,sizeof ip);MultiByteToWideChar(CP_UTF8,0,ip,-1,online_peer,64);
    }else{
        char host[160]={0},port[16];WideCharToMultiByte(CP_UTF8,0,online_address,-1,host,sizeof host,NULL,NULL);snprintf(port,16,"%d",online_port);
        struct addrinfo hints={0},*res=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
        if(!host[0]||getaddrinfo(host,port,&hints,&res)||!res){wcscpy(net_error,L"SET THE HOST ADDRESS FIRST (E.G. 192.168.1.20)");WSACleanup();return 0;}
        for(int tries=0;tries<60&&!net_cancelled;tries++){SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)break;net_conn=s;
            if(!connect(s,res->ai_addr,(int)res->ai_addrlen))break;net_conn=INVALID_SOCKET;closesocket(s);Sleep(500);}
        char ip[64]={0};inet_ntop(AF_INET,&((struct sockaddr_in*)res->ai_addr)->sin_addr,ip,sizeof ip);MultiByteToWideChar(CP_UTF8,0,ip,-1,online_peer,64);freeaddrinfo(res);
        if(net_conn==INVALID_SOCKET){wcscpy(net_error,net_cancelled?L"JOINING CANCELLED":L"NO HOST ANSWERED — CHECK THE ADDRESS, PORT AND FIREWALL");WSACleanup();return 0;}
    }
    if(!net_exchange(net_conn,mine,theirs,sizeof theirs))wcscpy(net_error,L"THE OTHER PC DID NOT ANSWER AS A JAILBREAK LAUNCHER");
    else{char fp[80];net_get(theirs,"fingerprint",fp,sizeof fp);
        if(strcmp(fp,online_fp))wcscpy(net_error,L"THE OTHER PC HAS A DIFFERENT GAME BUILD — BOTH NEED THE SAME ONE");
        else if(net_int(theirs,"slot",-1)!=!slot)wcscpy(net_error,L"BOTH PCS CHOSE THE SAME ROLE — ONE HOSTS, ONE JOINS");
        else{const char*host=slot?theirs:mine;online_peer_port=net_int(theirs,"udp",online_port+!slot);
            online_host_aspect=net_int(host,"aspect",0);if(online_host_aspect<0||online_host_aspect>2)online_host_aspect=0;
            online_host_lod=net_int(host,"lod_percent",100);online_host_delay=net_int(host,"delay",2);if(online_host_delay<1||online_host_delay>8)online_host_delay=2;
            ok=net_write_session(slot,host,slot?mine:theirs);if(!ok)wcscpy(net_error,L"THE SESSION FILE COULD NOT BE WRITTEN");}}
    closesocket(net_conn);net_conn=INVALID_SOCKET;WSACleanup();return ok;
}
static unsigned __stdcall net_worker(void*arg){int slot=(int)(intptr_t)arg;int ok=net_handshake(slot);PostMessageW(mainwindow,WM_APP+2,(WPARAM)ok,(LPARAM)slot);return 0;}
static void online_cancel(void){InterlockedExchange(&net_cancelled,1);SOCKET l=net_listen,c=net_conn;if(l!=INVALID_SOCKET)closesocket(l);if(c!=INVALID_SOCKET)closesocket(c);}
static void online_start(int slot){
    if(!online_ready){notice(L"ONLINE PLAY IS NOT READY YET — COMING IN A LATER RELEASE");return;}
    if(mods_on()||cp_active()){notice(L"SWITCH OFF YOUR MODS AND CHEATS TO PLAY ONLINE");return;}
    if(slot&&!online_address[0]){notice(L"SET THE HOST ADDRESS FIRST");return;}
    if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"Your game is already running. Return to its window.");return;}}
    online_slot=slot;net_cancelled=0;busy=5;
    uintptr_t t=_beginthreadex(NULL,0,net_worker,(void*)(intptr_t)slot,0,NULL);if(!t){busy=0;notice(L"Could not start the network check.");return;}CloseHandle((HANDLE)t);
    wchar_t text[160];if(slot)swprintf(text,160,L"JOINING %ls:%d — ESC CANCELS",online_address,online_port);else swprintf(text,160,L"HOSTING ON PORT %d — WAITING FOR PLAYER 2, ESC CANCELS",online_port);notice(text);
}
static void online_ready_to_play(int ok,int slot){
    busy=0;if(!ok){notice(net_error[0]?net_error:L"THE ONLINE SESSION COULD NOT BE SET UP");return;}
    online_slot=slot;online_launch=1;mod_checked=0;play_game();
}
static int address_valid(const wchar_t*s){size_t n=wcslen(s);if(!n||n>63)return 0;for(size_t i=0;i<n;i++){wchar_t c=s[i];if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||c==L'.'||c==L'-'))return 0;}return 1;}
static void online_action(void){
    if(selected==0)online_start(0);else if(selected==1)online_start(1);
    else if(selected==2){wchar_t t[64];wcscpy(t,online_address);if(prompt_text(L"Host address",L"The host PC's IP address or name on your network:",t,64,0)){if(!t[0]||address_valid(t)){wcscpy(online_address,t);save_settings();}else notice(L"USE AN IP ADDRESS SUCH AS 192.168.1.20");}}
    else if(selected==3){wchar_t t[16];swprintf(t,16,L"%d",online_port);if(prompt_text(L"Port",L"Port 1024-65534 (the joining PC uses the next one too):",t,8,0)){int p=_wtoi(t);if(p>=1024&&p<=65534){online_port=p;save_settings();}else notice(L"CHOOSE A PORT FROM 1024 TO 65534");}}
    else if(selected==4){online_delay=online_delay%8+1;save_settings();}
}
static void online_adjust(int dir){
    if(selected==3){int p=online_port+dir;if(p>=1024&&p<=65534){online_port=p;save_settings();}}
    else if(selected==4){int d=online_delay+dir;if(d>=1&&d<=8){online_delay=d;save_settings();}}
}
#include "lobby_page.h"
#include "online_link_host.h"
/* Voice preview: choosing a voice plays one of that character's taunt clips,
 * decoded from the owner's disc (DATA/AUDTAUNT.STR, read by rr_owner_taunts)
 * and played through waveOut at the clip's own pitch (0x1000 = 44100 Hz).
 * winmm is loaded on first use; a new preview replaces the one playing. */
typedef MMRESULT (WINAPI*WaveOpen)(LPHWAVEOUT,UINT,LPCWAVEFORMATEX,DWORD_PTR,DWORD_PTR,DWORD);
typedef MMRESULT (WINAPI*WaveHeader)(HWAVEOUT,LPWAVEHDR,UINT);
typedef MMRESULT (WINAPI*WaveHandle)(HWAVEOUT);
static unsigned voice_preview(int voice,int seat){   /* returns the milliseconds queued (0: none) */
    static HMODULE winmm;static WaveOpen open_;static WaveHeader prepare,unprepare,write_;static WaveHandle reset,close_;
    static HWAVEOUT out;static WAVEHDR header;static short*pcm;static unsigned turn;
    unsigned size=0;const unsigned char*catalogue=rr_owner_taunts(discfile,&size),*adpcm;unsigned bytes;uint32_t attr[3];
    if(voice<=0||!rr_voice_clip(catalogue,size,voice,seat,turn++,&adpcm,&bytes,attr))return 0;
    if(!winmm&&(winmm=LoadLibraryW(L"winmm.dll"))!=NULL){open_=(WaveOpen)(void*)GetProcAddress(winmm,"waveOutOpen");prepare=(WaveHeader)(void*)GetProcAddress(winmm,"waveOutPrepareHeader");
        unprepare=(WaveHeader)(void*)GetProcAddress(winmm,"waveOutUnprepareHeader");write_=(WaveHeader)(void*)GetProcAddress(winmm,"waveOutWrite");
        reset=(WaveHandle)(void*)GetProcAddress(winmm,"waveOutReset");close_=(WaveHandle)(void*)GetProcAddress(winmm,"waveOutClose");}
    if(!open_||!prepare||!unprepare||!write_||!reset||!close_)return 0;
    if(out){reset(out);unprepare(out,&header,sizeof header);close_(out);out=NULL;}
    unsigned cap=bytes/16*28,pitch=attr[2]>>16;short*next=realloc(pcm,(size_t)cap*sizeof*pcm+2);if(!next)return 0;pcm=next;
    unsigned samples=rr_adpcm_decode(adpcm,bytes,pcm,cap),rate=pitch&&pitch<=0x4000?pitch*44100u/4096u:11025u;if(!samples)return 0;
    WAVEFORMATEX format={WAVE_FORMAT_PCM,1,rate,rate*2,2,16,0};
    if(open_(&out,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){out=NULL;return 0;}
    memset(&header,0,sizeof header);header.lpData=(LPSTR)pcm;header.dwBufferLength=samples*2;
    if(prepare(out,&header,sizeof header)!=MMSYSERR_NOERROR||write_(out,&header,sizeof header)!=MMSYSERR_NOERROR){close_(out);out=NULL;return 0;}
    return samples*1000u/rate;   /* milliseconds queued */
}
static void native_action(void){
    if(busy&&page!=SETUP_PAGE)return;
    if(!installed){import_disc();return;}
    if(!page){
        if(selected==0)play_game();
        else if(selected==5)PostMessageW(mainwindow,WM_CLOSE,0,0);
        else if(selected==6)open_page(DEV_PAGE);
        else if(selected==7)open_page(CHEAT_PAGE);
        else if(selected==8)open_page(MULTI_PAGE);
        else if(selected==9)open_page(ORIGINAL_PAGE);   /* Original game: play the unmodified game, and its options */
        else open_page((const int[]){0,20,3,7,4}[selected%5]);
        return;
    }
    if(page==MULTI_PAGE){if(selected==1){if(!lobby_ready){notice(L"PLAY ONLINE NEEDS THE ONLINE BUILD OF THE GAME");return;}open_page(LB_ROOMS_PAGE);pub_auth();if(pub_signed_in())pub_request_list();}else open_page(ONLINE_PAGE);return;}   /* the list also brings the players-online count */
    if(LB_PAGE(page)){lb_action();InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==ONLINE_PAGE){online_action();InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==CHEAT_PAGE){cp_action(selected);InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(SB_PAGE(page)){sb_action();InvalidateRect(mainwindow,NULL,FALSE);return;}
    /* Original game page: row 0 plays, 1-6 step a setting, 7 restores them all. */
    if(page==ORIGINAL_PAGE){if(selected==0){play_original();return;}if(selected>=1&&selected<=ORIG_COUNT){int k=selected-1;orig[k]=(orig[k]+1)%orig_steps(k);}else for(int k=0;k<ORIG_COUNT;k++)orig[k]=0;save_settings();InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==7){const int destinations[]={2,8,13,11};open_page(destinations[selected]);return;}
    if(page==8){const int destinations[]={6,14,10,15};open_page(destinations[selected&3]);return;}
    if(page==14){if(selected<2){voices[selected]=rr_owner_voice_next(discfile,voices[selected],selected);voice_preview(voices[selected],selected);}else voices[0]=voices[1]=0;save_settings();InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==15){if(selected<2*RR_LOOK_KINDS){int*v=look_setting(selected);*v=(*v+1)%rr_look_count(selected/2);}else for(int r=0;r<2*RR_LOOK_KINDS;r++)*look_setting(r)=0;save_settings();InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==13){if(selected==0){if(music_count)music_custom=!music_custom;else notice(L"ADD YOUR OWN WAV TRACKS FIRST");}else if(selected==1){if(music_count)music_index=(music_index+1)%music_count;}else music_add();save_settings();InvalidateRect(mainwindow,NULL,FALSE);return;}
    /* System: rows 1 Loading, 2 Intro movies, 3 Controls, 4 Start-up (row numbers as in tuning.h; row 0, Discord, is not shown). */
    if(page==11){if(selected==0)return;if(selected==1){fast_loading=!fast_loading;save_settings();}else if(selected==2){skip_intro=!skip_intro;save_settings();}else if(selected==3)open_page(1);else{start_game_directly=!start_game_directly;save_settings();if(start_game_directly)notice(L"NEXT TIME THE GAME STARTS STRAIGHT AWAY — HOLD SHIFT WHILE OPENING FOR THE LAUNCHER");}InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==SETUP_PAGE){apply_preset(selected);mark_preset_chosen();page=selected=list_first=nav_depth=0;notice(L"PRESET APPLIED — CHANGE IT ANY TIME IN OPTIONS > DISPLAY");InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==10){if(selected>=RR_ACH_COUNT)ach_reset();InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==1){if(selected==0){device=!device;if(!device&&p2_device==2)p2_device=1;}else if(selected==1)p2_cycle(1);else if(selected==2)p3_device=!p3_device;else if(selected==3)p4_device=!p4_device;else if(selected==4)gamepad_map=!gamepad_map;else if(gamepad_map){int row=selected-5,index=0;while(index<16&&wcscmp(pad_sources[index],padcontrols[row]))index++;wcscpy(padcontrols[row],pad_sources[(index+1)%16]);}else binding=selected-5;save_settings();}
    if(page==2&&selected>=21&&selected<=24){open_page(DISPLAY_GROUP_FIRST+selected-21);return;}   /* Screen, Picture, Detail, Effects */
    if(page==2&&selected==15){int p=current_preset();apply_preset(p==PRESET_CUSTOM?0:(p+1)%3);InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==2){if(selected==0)resolution=(resolution+1)%5;else if(selected==1)filter=(filter+1)%rr_filter_steps;else if(selected==2)fullscreen_mode=!fullscreen_mode;else if(selected==3)texture_correction=(texture_correction+1)%rr_tc_offered;else if(selected==4)hd_ui=!hd_ui;else if(selected==5)larger_text=!larger_text;else if(selected==6)aspect=(aspect+1)%3;else if(selected==7)smooth_motion=(smooth_motion+1)%3;else if(selected==8)fmv_wide=!fmv_wide;else if(selected==9)model_detail=(model_detail+1)%tn_live.lod_n;
        else if(selected==10)aa_mode=(aa_mode+1)%rr_aa_steps;else if(selected==11)vsync_on=!vsync_on;else if(selected==12)low_latency=!low_latency;
        else if(selected==13)scanline_mode=(scanline_mode+1)%rr_scanline_steps;else if(selected==14)draw_mode=(draw_mode+1)%rr_draw_steps;
        else if(selected>=16&&selected<20)post_fx[selected-16]=(post_fx[selected-16]+1)%rr_post_steps;else if(selected==20)shadow_mode=(shadow_mode+1)%rr_shadow_steps;save_settings();}
    if(page==3){if(selected==0)import_mod();else if(selected==1){wchar_t path[32768];join(path,store,L"mods");ensure(path);ShellExecuteW(mainwindow,L"open",path,NULL,NULL,SW_SHOWNORMAL);}else if(selected==2)run_mod(L"dump",NULL);else if(selected-3<mod_count)run_mod(L"toggle",mod_ids[selected-3]);}
    if(page==4){if(selected==0)import_disc();else if(selected==1)ShellExecuteW(mainwindow,L"open",store,NULL,NULL,SW_SHOWNORMAL);else if(selected==3)report_problem();else if(installed)build_game();else notice(L"Import your original game first.");}
    if(page==6){if(selected==0){rename_riders=!rename_riders;save_settings();}else edit_rider_name(selected-1);}
    InvalidateRect(mainwindow,NULL,FALSE);
}
/* The selected row's handler, on the item's own page and row (tuning.ini). */
static void action(void){
    if(busy&&page!=SETUP_PAGE)return;if(!installed){import_disc();return;}
    if(IS_DEV(page)){dev_action();InvalidateRect(mainwindow,NULL,FALSE);return;}
    int np;if(DYN_PAGE(page)||!nat_enter(selected,&np)){if(DYN_PAGE(page))native_action();return;}
    native_action();nat_leave(np);InvalidateRect(mainwindow,NULL,FALSE);
}
static void native_remove_mod_prompt(void);
static void remove_mod_prompt(void){int np;if(!nat_enter(selected,&np))return;native_remove_mod_prompt();nat_leave(np);}
/* Removing moves the pack into mods\removed (recoverable), after a prompt. */
static void native_remove_mod_prompt(void){
    if(busy||page!=3||selected<3||selected-3>=mod_count)return;int i=selected-3;wchar_t text[400];
    swprintf(text,400,L"Remove \u201C%ls\u201D?\n\nIts files move to the mods\\removed folder of your installation, so you can recover them later.",mod_names[i]);
    if(MessageBoxW(mainwindow,text,L"Remove mod",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)run_mod(L"remove",mod_ids[i]);
}
/* Left / Right step a setting's value. Multi-value settings step backwards on
 * Left; on/off settings simply switch. Dialogs and mod scripts are not run. */
static void native_adjust(int dir);
static void adjust(int dir){
    if(busy||binding>=0||!row_count())return;
    if(IS_DEV(page)){dev_adjust(dir);InvalidateRect(mainwindow,NULL,FALSE);return;}
    int np;if(DYN_PAGE(page)||!nat_enter(selected,&np)){if(DYN_PAGE(page))native_adjust(dir);return;}
    native_adjust(dir);nat_leave(np);
}
static void native_adjust(int dir){
    if(page==0){}
    else if(page==2&&selected==15){int p=current_preset();p=p==PRESET_CUSTOM?(dir>0?0:2):(p+(dir>0?1:2))%3;apply_preset(p);}
    else if(page==16){}
    else if(page==ONLINE_PAGE)online_adjust(dir);
    else if(LB_PAGE(page))lb_adjust(dir);
    else if(page==ORIGINAL_PAGE&&selected>=1&&selected<=ORIG_COUNT){int k=selected-1,n=orig_steps(k);orig[k]=(orig[k]+(dir>0?1:n-1))%n;save_settings();}
    else if(page==2&&(selected==0||selected==6||selected==7)){if(selected==0)resolution=(resolution+(dir>0?1:4))%5;else if(selected==6)aspect=(aspect+(dir>0?1:2))%3;else smooth_motion=(smooth_motion+(dir>0?1:2))%3;save_settings();}
    else if(page==2&&selected==9){int next=model_detail+(dir>0?1:-1);if(next>=0&&next<tn_live.lod_n){model_detail=next;save_settings();}}
    else if(page==2&&(selected==1||selected==3||selected==10||selected==13||selected==14||(selected>=16&&selected<=20))){int*v=selected==1?&filter:selected==3?&texture_correction:selected==10?&aa_mode:selected==13?&scanline_mode:selected==14?&draw_mode:selected==20?&shadow_mode:&post_fx[selected-16];
        int n=selected==1?rr_filter_steps:selected==3?rr_tc_offered:selected==10?rr_aa_steps:selected==13?rr_scanline_steps:selected==14?rr_draw_steps:selected==20?rr_shadow_steps:rr_post_steps;*v=selected==3?rr_tc_step(*v):(*v+(dir>0?1:n-1))%n;save_settings();}
    else if(page==11&&(selected==1||selected==2||selected==4)){int*v=selected==1?&fast_loading:selected==2?&skip_intro:&start_game_directly;*v=!*v;save_settings();}
    else if(page==1&&selected==1){p2_cycle(dir);save_settings();}
    else if(page==1&&(selected==2||selected==3)){if(selected==2)p3_device=!p3_device;else p4_device=!p4_device;save_settings();}
    else if(page==13&&selected==1){if(music_count){music_index=(music_index+(dir>0?1:music_count-1))%music_count;save_settings();}}
    else if(page==14&&selected<2){voices[selected]=rr_owner_voice_step(discfile,voices[selected],selected,dir);voice_preview(voices[selected],selected);save_settings();}
    else if(page==15&&selected<2*RR_LOOK_KINDS){int*v=look_setting(selected),n=rr_look_count(selected/2);*v=(*v+(dir>0?1:n-1))%n;save_settings();}
    else if(page!=3){wchar_t label[160],value[160];if(row_is_value(launcher_row(selected,label,value,160)))native_action();}
    InvalidateRect(mainwindow,NULL,FALSE);
}
static int mouse_tracking;
static int mouse_hit(LPARAM lp,int*row){RECT r;GetClientRect(mainwindow,&r);ui_layout(r.right,r.bottom);return ui_hit((short)LOWORD(lp),(short)HIWORD(lp),row);}
static void capture_binding(WPARAM key){
    if(key==VK_ESCAPE){binding=-1;return;}wchar_t name[40]={0};
    if((key>='A'&&key<='Z')||(key>='0'&&key<='9')){name[0]=(wchar_t)key;name[1]=0;}
    else if(key>=VK_F1&&key<=VK_F12)swprintf(name,40,L"F%u",(unsigned)key-VK_F1+1);
    else switch(key){case VK_LEFT:wcscpy(name,L"Left");break;case VK_RIGHT:wcscpy(name,L"Right");break;case VK_UP:wcscpy(name,L"Up");break;case VK_DOWN:wcscpy(name,L"Down");break;case VK_RETURN:wcscpy(name,L"Return");break;case VK_SPACE:wcscpy(name,L"Space");break;case VK_TAB:wcscpy(name,L"Tab");break;case VK_SHIFT:wcscpy(name,L"Left Shift");break;case VK_CONTROL:wcscpy(name,L"Left Ctrl");break;}
    if(name[0]){wcscpy(controls[binding],name);binding=-1;save_settings();}
}
static LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_DESTROY){lb_close();lb_unmap();PostQuitMessage(0);return 0;}
    if(msg==WM_CLOSE){if(dev_leave_ok())DestroyWindow(window);return 0;}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_TIMER&&wp==2){sb_pad_poll(window);lb_poll(window);ol_poll();if(busy)InvalidateRect(window,NULL,FALSE);return 0;}
    if(msg==WM_TIMER){
        if(builder){wchar_t path[32768];join(path,store,busy==3?L"mods\\status.txt":busy==4?L"launch-status.txt":L"build-status.txt");FILE*f=_wfopen(path,L"rb");if(f){char line[1024]={0};size_t n=fread(line,1,sizeof(line)-1,f);fclose(f);if(n)MultiByteToWideChar(CP_UTF8,0,line,-1,status_text,256);}}
        if(builder){DWORD code;if(builder_exit(&code)&&code!=STILL_ACTIVE){int was_mod=busy==3,was_original=busy==4;CloseHandle(builder);builder=NULL;busy=0;
            if(was_mod||was_original){
                if(was_mod)mod_reload();
                if(code){wchar_t reason[256]=L"";if(was_mod){wchar_t path[32768];join(path,store,L"mods\\status.txt");FILE*f=_wfopen(path,L"rb");if(f){char line[1024]={0};size_t n=fread(line,1,sizeof(line)-1,f);fclose(f);if(n)MultiByteToWideChar(CP_UTF8,0,line,-1,reason,256);}}
                    notice(was_original?L"Original mode could not be prepared. See launch.log for details.":reason[0]?reason:L"The mod was not applied. See mods.log for details.");play_after_mod_check=0;online_launch=0;original_done();}
                else if(play_after_mod_check){play_after_mod_check=0;mod_checked=1;play_game();}
                else notice(L"MODS UPDATED \u2014 CHANGES APPLY ON YOUR NEXT ENHANCED LAUNCH");
            }else notice(code?L"Local build setup needs attention. Open the installation folder and read build.log.":L"LOCAL GAME BUILD READY \u2014 SELECT PLAY");}}
        /* Straight-to-game: a launch that didn't happen shows the launcher with its reason. */
        if(quick_start==1&&!busy&&!play_after_mod_check&&!game_process)quick_reveal();
        if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code!=STILL_ACTIVE){CloseHandle(game_process);game_process=NULL;
            if(game_was_online){game_was_online=0;lb_mmr_apply();}
            if(ol_game_exited())return 0;   /* an online race from the game's menus (online_link_host.h) */
            if(quick_start==2&&!code&&!IsWindowVisible(window)){DestroyWindow(window);return 0;}if(quick_start)quick_reveal();settings();notice(code?L"The research game stopped here. Details are saved in runtime.log.":L"GAME CLOSED \u2014 READY TO PLAY AGAIN");}}
        /* tuning.ini changed outside the launcher (by hand, an import): use it. */
        {FILETIME s=file_stamp(tuning_path);if(CompareFileTime(&s,&tuning_stamp)){tuning_load(1);InvalidateRect(window,NULL,FALSE);}}
        if(busy||toast_visible()!=toast_painted)InvalidateRect(window,NULL,FALSE);return 0;
    }
    if(msg==WM_APP+2){online_ready_to_play((int)wp,(int)lp);InvalidateRect(window,NULL,FALSE);return 0;}
    if(msg==WM_APP+1){busy=0;if(wp){installed=1;artwork();page=selected=list_first=nav_depth=0;binding=-1;notice(L"GAME DATA INSTALLED \u2014 THE ORIGINAL DISC FILE IS NO LONGER REQUIRED");if(!preset_chosen)open_page(SETUP_PAGE);if(!preview_only)build_game();}else{wchar_t err[256];MultiByteToWideChar(CP_UTF8,0,error_text,-1,err,256);const wchar_t*why=err[0]?err:L"Game import failed. Your previous installation was preserved.";notice(why);MessageBoxW(mainwindow,why,L"Game import",MB_ICONWARNING);}return 0;}
    if(msg==WM_CHAR&&installed&&binding<0&&lb_chat_char(wp)){InvalidateRect(window,NULL,FALSE);return 0;}
    if(msg==WM_KEYDOWN){
        int count=row_count();
        /* Ctrl+Shift+D shows or hides the Developer page (remembered). Not in
           the public release (RR_PUBLIC_RELEASE: no editors). */
#ifndef RR_PUBLIC_RELEASE
        if(installed&&binding<0&&wp=='D'&&GetKeyState(VK_CONTROL)<0&&GetKeyState(VK_SHIFT)<0){
            if(dev_unlocked&&IS_DEV(page)&&!dev_leave_ok())return 0;
            dev_unlocked=!dev_unlocked;WritePrivateProfileStringW(L"developer",L"unlocked",dev_unlocked?L"1":NULL,ini);
            if(!dev_unlocked&&IS_DEV(page)){page=selected=list_first=nav_depth=0;}
            notice(dev_unlocked?L"DEVELOPER PAGE ON — AT THE END OF THE MAIN MENU":L"DEVELOPER PAGE HIDDEN");InvalidateRect(window,NULL,FALSE);return 0;}
#endif
        if(busy==5&&wp==VK_ESCAPE){online_cancel();return 0;}   /* hosting / joining */
        if(installed&&binding<0&&lb_chat_key(wp)){InvalidateRect(window,NULL,FALSE);return 0;}   /* room chat box (lobby_page.h) */
        if(!installed){if(wp==VK_RETURN||wp==VK_SPACE)action();}
        else if(binding>=0)capture_binding(wp);
        else if(wp==VK_ESCAPE||wp==VK_BACK)go_back();
        else if((wp==VK_UP||wp==VK_DOWN)&&count)selected=(selected+(wp==VK_DOWN?1:count-1))%count;
        else if(wp==VK_HOME||wp==VK_END)selected=wp==VK_HOME||!count?0:count-1;
        else if(wp==VK_PRIOR||wp==VK_NEXT){selected+=wp==VK_NEXT?list_visible():-list_visible();if(selected>=count)selected=count-1;if(selected<0)selected=0;}
        else if(wp==VK_LEFT||wp==VK_RIGHT)adjust(wp==VK_RIGHT?1:-1);
        else if(wp==VK_DELETE){if(!sb_delete()&&!cp_delete())remove_mod_prompt();}
        else if(wp==VK_RETURN||wp==VK_SPACE)action();
        InvalidateRect(window,NULL,FALSE);return 0;
    }
    if(msg==WM_MOUSEMOVE){
        if(!mouse_tracking){TRACKMOUSEEVENT t={sizeof t,TME_LEAVE,window,0};mouse_tracking=TrackMouseEvent(&t);}
        // Windows also sends moves without motion (e.g. when a window appears
        // under a resting cursor); only real motion moves the highlight.
        static LPARAM last_lp=-1;int moved=last_lp!=-1&&lp!=last_lp;last_lp=lp;
        int row,hit=mouse_hit(lp,&row),changed=hit!=hover_hit||row!=hover_row;hover_hit=hit;hover_row=row;
        if(moved&&hit==HIT_ROW&&binding<0&&row!=selected){selected=row;changed=1;}
        if(changed)InvalidateRect(window,NULL,FALSE);return 0;
    }
    if(msg==WM_MOUSELEAVE){mouse_tracking=0;if(hover_hit){hover_hit=HIT_NONE;hover_row=-1;InvalidateRect(window,NULL,FALSE);}return 0;}
    if(msg==WM_SETCURSOR&&LOWORD(lp)==HTCLIENT){POINT pt;GetCursorPos(&pt);ScreenToClient(window,&pt);int row;SetCursor(LoadCursorW(NULL,mouse_hit(MAKELPARAM(pt.x,pt.y),&row)!=HIT_NONE?IDC_HAND:IDC_ARROW));return TRUE;}
    if(msg==WM_MOUSEWHEEL){int count=row_count();if(binding<0&&count){selected+=GET_WHEEL_DELTA_WPARAM(wp)<0?1:-1;if(selected<0)selected=0;if(selected>=count)selected=count-1;InvalidateRect(window,NULL,FALSE);}return 0;}
    if(msg==WM_LBUTTONDOWN){
        int row,hit=mouse_hit(lp,&row);
        if(hit==HIT_FIND)action();
        else if(binding>=0){if(hit!=HIT_ROW||row!=selected)binding=-1;}
        else if(hit==HIT_ROW){selected=row;action();}
        else if(hit==HIT_BACK)go_back();
        else if(hit==HIT_PREV||hit==HIT_NEXT)adjust(hit==HIT_NEXT?1:-1);
        else if(hit==HIT_REMOVE){selected=row;InvalidateRect(window,NULL,FALSE);UpdateWindow(window);remove_mod_prompt();}
        InvalidateRect(window,NULL,FALSE);return 0;
    }
    if(msg==WM_XBUTTONDOWN){if(binding<0&&GET_XBUTTON_WPARAM(wp)==XBUTTON1)go_back();return TRUE;}
    if(msg==WM_SIZE){InvalidateRect(window,NULL,FALSE);return 0;}
    if(msg==WM_DPICHANGED){RECT*r=(RECT*)lp;SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    if(msg==WM_GETMINMAXINFO){UINT dpi=window_dpi(window);SIZE s=window_size_for_client(MulDiv(720,dpi,96),MulDiv(405,dpi,96),dpi);((MINMAXINFO*)lp)->ptMinTrackSize.x=s.cx;((MINMAXINFO*)lp)->ptMinTrackSize.y=s.cy;return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);void *pixels;HBITMAP bitmap=render(dc,&pixels);if(bitmap){HDC mem=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(mem,bitmap);BitBlt(dc,0,0,render_width,render_height,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteDC(mem);DeleteObject(bitmap);}EndPaint(window,&ps);return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
static int decoder_test(void){
    unsigned char data[36]={0};memcpy(data,"MDEC",4);data[7]=36;data[9]=16;data[11]=16;data[20]=1;data[22]=2;
    unsigned bit=0;for(unsigned block=0;block<6;block++){bit+=10;size_t offset=(bit/8)^1;data[24+offset]|=1u<<(7-(bit&7));bit+=2;}
    Image im={0};if(!decode_mdec(data,sizeof(data),&im))return 2;
    for(unsigned i=0;i<256;i++)if(im.pixels[i]!=0x808080){free(im.pixels);return 3;}free(im.pixels);
    data[22]=3;if(decode_mdec(data,sizeof(data),&im)){free(im.pixels);return 4;}data[22]=2;
    if(decode_mdec(data,25,&im)){free(im.pixels);return 5;}return 0;
}
static int validate_mdec_file(const wchar_t*path){
    FILE*f=_wfopen(path,L"rb");if(!f)return 16;fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);if(length<24||length>33554432){fclose(f);return 16;}unsigned char*b=malloc(length);if(!b){fclose(f);return 16;}size_t got=fread(b,1,length,f);fclose(f);size_t pos=0;unsigned frames=0;uint64_t pixels=0;int ok=got==(size_t)length;
    while(ok&&pos<(size_t)length){if(b[pos]==0){for(size_t i=pos;i<(size_t)length;i++)if(b[i])ok=0;break;}if((size_t)length-pos<24||memcmp(b+pos,"MDEC",4)){ok=0;break;}unsigned n=be32(b+pos+4);if(n<24||n>(size_t)length-pos||++frames>256){ok=0;break;}Image im={0};if(!decode_mdec(b+pos,n,&im)){ok=0;break;}pixels+=(uint64_t)im.w*im.h;free(im.pixels);if(pixels>16777216){ok=0;break;}pos+=n;}
    free(b);return ok&&frames?0:16;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR line,int show){
    (void)previous;(void)line;dpi_setup();GetModuleFileNameW(NULL,appdir,32768);PathRemoveFileSpecW(appdir);
    workspace[0]=0;
    wchar_t base[32768];GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);join(store,base,L"RoadRashJailbreak");
    int argc;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);const wchar_t *preview_path=NULL,*import_path=NULL,*mod_operation=NULL,*mod_value=NULL;int room_test=0,room_test_n=0;const wchar_t*room_test_code=NULL;int settings_only=0,play_on_start=0,setup_on_start=0,preview_voice=0,net_test=0,net_test_port=0,online_start_on=0,second_instance=0,show_launcher=0;const wchar_t*net_test_address=NULL;
    for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--self-test")){LocalFree(argv);return decoder_test();}
        if(!wcscmp(argv[i],L"--validate-mdec-file")&&i+1<argc){int code=validate_mdec_file(argv[++i]);LocalFree(argv);return code;}
        if(!wcscmp(argv[i],L"--data-root")&&i+1<argc)GetFullPathNameW(argv[++i],32768,store,NULL);
        else if(!wcscmp(argv[i],L"--workspace")&&i+1<argc)GetFullPathNameW(argv[++i],32768,workspace,NULL);
        else if(!wcscmp(argv[i],L"--render-preview")&&i+1<argc){preview_path=argv[++i];preview_only=1;}
        else if(!wcscmp(argv[i],L"--preview-size")&&i+1<argc){int w=0,h=0;if(swscanf(argv[++i],L"%dx%d",&w,&h)!=2||w<320||h<180||w>7680||h>4320){LocalFree(argv);return 22;}render_width=w;render_height=h;}
        else if(!wcscmp(argv[i],L"--preview-pad")&&i+1<argc){i++;preview_pad=!wcscmp(argv[i],L"playstation");}
        else if(!wcscmp(argv[i],L"--import")&&i+1<argc)import_path=argv[++i];
        else if(!wcscmp(argv[i],L"--page")&&i+1<argc)page=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--selection")&&i+1<argc)selected=_wtoi(argv[++i]);
        else if((!wcscmp(argv[i],L"--rider-name1")||!wcscmp(argv[i],L"--rider-name2"))&&i+1<argc){int seat=!wcscmp(argv[i],L"--rider-name2");i++;if(!rider_name_valid(argv[i])){LocalFree(argv);return 21;}name_overrides[seat]=argv[i];}
        else if((!wcscmp(argv[i],L"--rename-riders")||!wcscmp(argv[i],L"--larger-text"))&&i+1<argc){int text=!wcscmp(argv[i],L"--larger-text");i++;if(wcscmp(argv[i],L"0")&&wcscmp(argv[i],L"1")){LocalFree(argv);return 21;}if(text)text_override=_wtoi(argv[i]);else rename_override=_wtoi(argv[i]);}
        else if(!wcscmp(argv[i],L"--mode")&&i+1<argc){i++;if(!_wcsicmp(argv[i],L"enhanced"))mode_override=1;else if(!_wcsicmp(argv[i],L"original"))mode_override=0;else{LocalFree(argv);return 19;}}
        else if(!wcscmp(argv[i],L"--write-settings-only"))settings_only=1;
        else if(!wcscmp(argv[i],L"--play-on-start"))play_on_start=1;
        else if(!wcscmp(argv[i],L"--setup-on-start"))setup_on_start=1;   /* test: first-run setup as after an import */
        else if(!wcscmp(argv[i],L"--voice-preview")&&i+1<argc)preview_voice=_wtoi(argv[++i]);   /* test: play one clip, exit 0 when it played */
        else if(!wcscmp(argv[i],L"--mod-import")&&i+1<argc){mod_operation=L"install";mod_value=argv[++i];}
        else if(!wcscmp(argv[i],L"--mod-toggle")&&i+1<argc){mod_operation=L"toggle";mod_value=argv[++i];}
        else if(!wcscmp(argv[i],L"--mod-remove")&&i+1<argc){mod_operation=L"remove";mod_value=argv[++i];}
        /* Test: the Online page's session setup without the window (host|join). */
        else if(!wcscmp(argv[i],L"--net-handshake")&&i+1<argc){i++;net_test=!wcscmp(argv[i],L"host")?1:!wcscmp(argv[i],L"join")?2:0;if(!net_test){LocalFree(argv);return 19;}}
        else if(!wcscmp(argv[i],L"--net-address")&&i+1<argc)net_test_address=argv[++i];
        else if(!wcscmp(argv[i],L"--net-port")&&i+1<argc)net_test_port=_wtoi(argv[++i]);
        /* Test: Host / Join from the Online page once the window is up; a second
           window on the same PC (one-PC online tests) skips the single-instance check. */
        else if(!wcscmp(argv[i],L"--online-start")&&i+1<argc){i++;online_start_on=!wcscmp(argv[i],L"host")?1:!wcscmp(argv[i],L"join")?2:0;if(!online_start_on){LocalFree(argv);return 19;}}
        else if(!wcscmp(argv[i],L"--second-instance"))second_instance=1;
        /* Test: Play online rooms without clicking (lobby_page.h). */
        else if(!wcscmp(argv[i],L"--room-host")&&i+1<argc){room_test=1;room_test_n=_wtoi(argv[++i]);}
        else if(!wcscmp(argv[i],L"--room-join")&&i+1<argc){room_test=2;room_test_code=argv[++i];}
        else if(!wcscmp(argv[i],L"--launcher"))show_launcher=1;   /* always show the launcher (overrides Start-up: Straight to game) */
    }
    page=page_live(page);ensure(store);join(ini,store,L"launcher.ini");join(discfile,store,L"game-disc.bin");join(cuefile,store,L"installed-disc.cue");settings();
    if(selected<0||selected>=row_count())selected=0;   /* after settings(): the rows come from tuning.ini */
    if(import_path){wchar_t source[32768];GetFullPathNameW(import_path,32768,source,NULL);if(!resolve_cue(source)||!validate_disc(source)||!CopyFileW(source,discfile,FALSE)){fprintf(stderr,"%s\n",error_text);LocalFree(argv);return 10;}}
    installed=exists(discfile)&&validate_disc(discfile);if(installed)artwork();
    if(preview_voice){unsigned ms=installed?voice_preview(preview_voice,0):0;if(ms)Sleep(ms+300);LocalFree(argv);return ms?0:23;}
    mod_reload();load_project_logo();if(selected>=row_count())selected=0;
    if(net_test){int slot=net_test-1;if(net_test_address)wcsncpy(online_address,net_test_address,63);if(net_test_port>=1024&&net_test_port<=65534)online_port=net_test_port;
        int ok=net_handshake(slot);wchar_t path[32768];join(path,store,slot?L"netplay-handshake-p2.txt":L"netplay-handshake-p1.txt");
        FILE*f=_wfopen(path,L"wb");if(f){fputs(ok?"ok ":"failed ",f);utf8(f,ok?online_session:net_error);fputc('\n',f);fclose(f);}LocalFree(argv);return ok?0:31;}
    if(mod_operation){run_mod(mod_operation,mod_value);if(!builder){LocalFree(argv);return 17;}WaitForSingleObject(builder,120000);DWORD code=1;builder_exit(&code);CloseHandle(builder);LocalFree(argv);return code==STILL_ACTIVE?18:(int)code;}
    if(settings_only){if(!installed){LocalFree(argv);return 14;}int ok=write_runtime_settings();if(ok)save_settings();LocalFree(argv);return ok?0:20;}
    if(preview_path){HDC dc=GetDC(NULL);void *pixels;HBITMAP bmp=render(dc,&pixels);Image im={(unsigned)render_width,(unsigned)render_height,pixels};int ok=bmp&&save_bmp(preview_path,&im);if(bmp)DeleteObject(bmp);ReleaseDC(NULL,dc);LocalFree(argv);return ok?0:11;}
    /* The tuning file with every built-in value, so it can be found and edited;
       a broken one is announced (its problems keep their built-in values). */
    if(installed&&!exists(tuning_path)&&tn_save(tuning_path,&tn_live))tuning_stamp=file_stamp(tuning_path);else tuning_load(1);
    HWND previous_window=second_instance?NULL:FindWindowW(L"RoadRashJailbreakLauncher",NULL);if(previous_window){ShowWindow(previous_window,SW_RESTORE);SetForegroundWindow(previous_window);LocalFree(argv);return 0;}
    WNDCLASSW wc={0};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"RoadRashJailbreakLauncher";wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hCursor=LoadCursorW(NULL,IDC_ARROW);if(!RegisterClassW(&wc)){LocalFree(argv);return 12;}
    if(online_start_on&&!online_address[0])wcscpy(online_address,L"127.0.0.1");
    mainwindow=CreateWindowExW(0,wc.lpszClassName,APP_TITLE,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,760,NULL,NULL,instance,NULL);if(!mainwindow){LocalFree(argv);return 13;}
    /* Start-up: Straight to game, once set up (game imported, preset chosen, game built). */
    {wchar_t built[32768];join(built,store,L"runtime\\RoadRashNativeResearch.exe");
     if(start_game_directly&&installed&&preset_chosen&&exists(built)&&!show_launcher&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)&&!play_on_start&&!setup_on_start&&!online_start_on&&!room_test&&page==0)quick_start=1;}
    place_main_window();SetTimer(mainwindow,1,500,NULL);SetTimer(mainwindow,2,50,NULL);if(!quick_start){ShowWindow(mainwindow,show);UpdateWindow(mainwindow);}
    if(quick_start)play_game();
    if(installed&&!preset_chosen&&!play_on_start&&page==0)open_page(SETUP_PAGE);   /* first run: choose a preset */
    if(setup_on_start&&installed)build_game();
    if(play_on_start)play_game();
    if(online_start_on){if(net_test_port>=1024&&net_test_port<=65534)online_port=net_test_port;if(net_test_address)wcsncpy(online_address,net_test_address,63);page=ONLINE_PAGE;selected=online_start_on-1;online_start(online_start_on-1);}
    if(room_test){if(net_test_port>=1024&&net_test_port<=65534)online_port=net_test_port;page=LB_ROOMS_PAGE;selected=0;
        if(room_test==1){lb_size=room_test_n>=2&&room_test_n<=8?room_test_n:2;lb_test_autostart=lb_size;lb_host();}else{lb_test_autoready=1;lb_join(room_test_code);}}
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}LocalFree(argv);if(builder)CloseHandle(builder);if(game_process)CloseHandle(game_process);return 0;
}
