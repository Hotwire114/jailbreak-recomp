/* Road Rash Jailbreak launcher skin. LGPL-2.1-or-later. Original author code.
 * Page model (rows, values, help, navigation) and the renderer.
 *
 * Asset gate: nothing from the game is used until the owner's disc image has
 * been validated (rr_art_open checks the executable hash). Until then the
 * launcher shows only its own procedural backdrop, the independent Jailbreak
 * logo and one action: find your game's .bin. Once validated, the menus are
 * drawn with the game's own assets, upscaled at the window's native pixel
 * density: the BTN/HDR fonts (smoothly interpolated, re-sharpened edges), the
 * textured bars (edges kept crisp, fills enlarged), the menu background and
 * the preview artwork. No game data is embedded in the launcher. */

/* ---- Page model --------------------------------------------------------- */
/* Pages: 0 main, 1 controls, 2 display, 3 mods, 4 installation, 6 rider
 * names, 7 options, 8 user settings, 10 achievements, 11 system, 13 audio,
 * 14 taunt voices, 15 rider look. Options holds the same four categories as the in-game PC
 * menu (rider_menu.cpp): Display; User settings = everything about a rider
 * (names, taunt voices) and achievements; Audio = music; System = Discord,
 * loading, intro movies and controls. The game mode is chosen on Play.
 * Retired numbers (5 game mode, 9 game options, 12 music) open their new
 * place (page_live). */
enum {ROW_ACTION,ROW_MENU,ROW_VALUE,ROW_INFO,ROW_SLIDER};
/* Developer pages (dev_editor.h): DEV_PAGE and the pages after it. */
#define DEV_PAGE 30
#define DEV_LAST 37
#define IS_DEV(p) ((p)>=DEV_PAGE&&(p)<=DEV_LAST)   /* not just >=: Public rooms (38) is above the range */
/* Cheats page (cheats_page.h, included by launcher.c; codes: cheat_codes.h). */
#define CHEAT_PAGE 19
static int cp_row_count(void);static int cp_custom_count(void);static int cp_row(int,wchar_t*,wchar_t*,size_t);static const wchar_t*cp_help(int);static int cp_active(void);static int cp_on_count(void);
#define SETUP_PAGE 16   /* first-run preset choice (launcher.c) */
/* Multiplayer (17) and its Online page (18): launcher.c online_*. */
#define MULTI_PAGE 17
#define ONLINE_PAGE 18
/* Original game (23): Play original game and its own opt-in options (launcher.c orig_*). */
#define ORIGINAL_PAGE 23
/* Display's groups (24-27): Screen, Picture, Detail, Effects (tuning.h tn_homes);
 * their items stay native page 2 rows. */
#define DISPLAY_GROUP_FIRST 24
#define DISPLAY_GROUP_LAST 27
/* Multiplayer > Play online, pages 28-29 and Public rooms 38 (lobby_page.h, included by launcher.c). */
#define LB_PAGE(p) ((p)==28||(p)==29||(p)==38||(p)==39)
static int page_live(int p){
#ifdef RR_PUBLIC_RELEASE
    if(IS_DEV(p))return 0;   /* no Developer page */
#endif
    return p==5?0:p==9?11:p==12?13:IS_DEV(p)&&p<=DEV_LAST?p:LB_PAGE(p)?p:p<0||p>DISPLAY_GROUP_LAST?0:p;}
/* Saves browser, pages 20-22 (save_browser.h, included at the end). */
#define SB_PAGE(p) ((p)>=20&&(p)<=22)
#define DYN_PAGE(p) (SB_PAGE(p)||LB_PAGE(p))
static int lb_row_count(void);static int lb_row(int,wchar_t*,wchar_t*,size_t);static void lb_draw_chat(double,double,double,double);static const wchar_t*lb_help(void);static const wchar_t*lb_page_title(int);
static int sb_row_count(void);static int sb_row(int,wchar_t*,wchar_t*,size_t);static const wchar_t*sb_help(void);static const wchar_t*sb_page_title(int);
static void sb_draw_art(double,double,double,double);static const wchar_t*sb_key(const wchar_t*,const wchar_t*,const wchar_t*);
/* A slider is a value row (Left / Right, arrows) drawn with a step gauge. */
static int row_is_value(int kind){return kind==ROW_VALUE||kind==ROW_SLIDER;}
static int list_first;
/* ---- tuning.ini (tuning.h) ------------------------------------------------
 * tn_live is the saved file: what every page shows. A page lists items by
 * their native (page, row); the handlers below (native_row, native_help,
 * native_action ...) keep working on native positions, so an item renamed,
 * moved, reordered or hidden behaves as before. While a native handler runs,
 * page/selected hold the item's own page and row and nat_vp the page being
 * viewed (nat_enter / nat_leave). */
static wchar_t tn_titles[TN_PAGES][TN_TITLE];
static int nat_vp=-1,nat_vs;
typedef struct VRow {short page,row,item;} VRow;
static int view_rows(int vp,VRow*out,int cap){
    int p=tn_page_of_num(vp),n=0;if(p<0)return 0;
    for(int k=0;k<tn_live.count[p];k++){int i=tn_live.order[p][k];if(tn_live.hidden[i])continue;const TnItem*it=tn_items+i;
        /* Rider look lives in the game: its showroom (showroom.cpp) shows the real rider. */
        if(it->opens==15||it->page==15)continue;
        /* No Discord in this release. The page's handlers get each row's own number (row 0 is never shown). */
        if(!strcmp(it->id,"discord"))continue;
        if(it->flags&TN_GROUP){int gn=it->page==CHEAT_PAGE?cp_custom_count():it->page==10?RR_ACH_COUNT:mod_count;for(int m=0;m<gn&&n<cap;m++){out[n].page=(short)it->page;out[n].row=(short)(it->row+m);out[n].item=(short)i;n++;}}
        else if(n<cap){out[n].page=(short)it->page;out[n].row=(short)it->row;out[n].item=(short)i;n++;}}
    if(vp==0&&dev_unlocked&&n<cap){out[n].page=0;out[n].row=6;out[n].item=-1;n++;}   /* Developer */
    return n;
}
static int view_at(int r,VRow*v){VRow rows[TN_ROWS+8];int n=view_rows(page,rows,TN_ROWS+8);if(r<0||r>=n)return 0;*v=rows[r];return 1;}
static int nat_enter(int r,int*np){VRow v;if(nat_vp>=0||DYN_PAGE(page)||!view_at(r,&v))return 0;nat_vp=page;nat_vs=selected;page=v.page;selected=v.row;*np=v.page;return 1;}
/* A handler that opened another page leaves it open; otherwise the view returns. */
static void nat_leave(int np){if(page==np){page=nat_vp;selected=nat_vs;}nat_vp=-1;}
static int view_is_mod(int r){VRow v;return !SB_PAGE(page)&&view_at(r,&v)&&v.page==3&&v.row>=3;}
static void tn_refresh(void){for(int p=0;p<TN_PAGES;p++)MultiByteToWideChar(CP_UTF8,0,tn_live.title[p],-1,tn_titles[p],TN_TITLE);}
/* Level of detail steps (tuning.ini [level of detail]). */
static int det_steps(void){return tn_live.lod_n;}
static int det_percent(int i){return tn_live.lod[i>=0&&i<tn_live.lod_n?i:0];}
static int det_safe(void){return tn_live.lod_safe;}
/* Level of detail step label: "Original", then the switch-distance multiple. */
static void detail_name(int step,wchar_t*out,size_t cap){char t[16];tn_lod_name(det_percent(step),t,sizeof t);MultiByteToWideChar(CP_UTF8,0,t,-1,out,(int)cap);}
static int mods_on(void){int n=0;for(int i=0;i<mod_count;i++)n+=mod_enabled[i]!=0;return n;}
static int native_count(void){switch(page){case 0:return 6;case CHEAT_PAGE:return cp_row_count();case 20:case 21:case 22:return sb_row_count();case 28:case 29:case 38:return lb_row_count();case 1:return 14;case 2:return 25;case 3:return 3+mod_count;case 4:return 4;case 7:case 8:return 4;case 11:return 5;case 15:return 2*RR_LOOK_KINDS+1;case MULTI_PAGE:return 2;case ONLINE_PAGE:return 6;case ORIGINAL_PAGE:return ORIG_COUNT+2;default:return 3;}}
static int dev_row_count(void);static int dev_row(int,wchar_t*,wchar_t*,size_t);static const wchar_t*dev_help(void);static int dev_parent(int);static const wchar_t*dev_title(int);static int dev_dirty(void);
static int row_count(void){if(nat_vp>=0||DYN_PAGE(page))return native_count();if(IS_DEV(page))return dev_row_count();VRow v[TN_ROWS+8];return view_rows(page,v,TN_ROWS+8);}
static int static_parent(int p){switch(p){case 3:case 4:case 7:case 20:case CHEAT_PAGE:case DEV_PAGE:case MULTI_PAGE:case ORIGINAL_PAGE:return 0;case ONLINE_PAGE:case 28:return MULTI_PAGE;case 29:case 38:case 39:return 28;case 21:case 22:return 20;case 2:case 8:case 11:case 13:return 7;case 24:case 25:case 26:case 27:return 2;case 6:case 10:case 14:case 15:return 8;case 1:return 11;default:return 0;}}
/* The page holding the visible item that opens p (items can move between pages). */
static int parent_of(int p){
    if(p>DEV_PAGE&&p<=DEV_LAST)return dev_parent(p);if(p==21||p==22)return 20;
    for(int q=0;q<TN_PAGES;q++)for(int k=0;k<tn_live.count[q];k++){int i=tn_live.order[q][k];if(!tn_live.hidden[i]&&tn_items[i].opens==p)return tn_pages[q].num;}
    return static_parent(p);
}
static int parent_page(void){return parent_of(page);}
static const wchar_t*page_title(int p){p=page_live(p);if(IS_DEV(p))return dev_title(p);if(SB_PAGE(p))return sb_page_title(p);if(LB_PAGE(p))return lb_page_title(p);int q=tn_page_of_num(p);return q>=0?tn_titles[q]:L"";}
static const wchar_t*on_off(int v){return v?L"On":L"Off";}
static int native_row(int item,wchar_t*label,wchar_t*value,size_t cap);
/* Achievements (achievements.h): written by the game, read here. */
static int ach_get(const wchar_t*section,const wchar_t*key){wchar_t path[32768];join(path,store,L"achievements.ini");return GetPrivateProfileIntW(section,key,0,path);}
static int ach_unlocked(int i){wchar_t key[16];swprintf(key,16,L"unlock%d",i);return ach_get(L"achievements",key)!=0;}
static int ach_unlocked_count(void){int n=0;for(int i=0;i<RR_ACH_COUNT;i++)n+=ach_unlocked(i);return n;}
static void ach_value(int i,wchar_t*value,size_t cap){
    const RrAchievement*a=rr_achievements+i;if(ach_unlocked(i)){wcsncpy(value,L"Unlocked",cap);return;}
    int have=a->progress==RR_P_FINISHES?ach_get(L"progress",L"finishes"):a->progress==RR_P_WINS?ach_get(L"progress",L"wins"):a->progress==RR_P_MODES?rr_mode_count(ach_get(L"progress",L"modes")):
        a->progress==RR_P_HOURS?ach_get(L"progress",L"race_seconds")/60:0,goal=a->progress==RR_P_HOURS?a->goal*60:a->goal;
    if(have>goal)have=goal;
    if(a->progress==RR_P_HOURS)swprintf(value,cap,L"Locked  %d/%d min",have,goal);else if(a->progress&&goal>1)swprintf(value,cap,L"Locked  %d/%d",have,goal);else wcsncpy(value,L"Locked",cap);
}
/* Reset all achievements: only while the game is closed (it keeps its own copy
 * and would write it back), after a prompt; the file is kept as .bak first. */
static void ach_reset(void){
    DWORD code;if(game_process&&GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"CLOSE THE GAME FIRST, THEN RESET ACHIEVEMENTS");return;}
    if(MessageBoxW(mainwindow,L"Lock every achievement again and clear all progress?\n\nThe current file is kept as achievements.ini.bak.",L"Reset all achievements",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    wchar_t path[32768],backup[32768];join(path,store,L"achievements.ini");swprintf(backup,32768,L"%ls.bak",path);
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES&&!CopyFileW(path,backup,FALSE)){notice(L"COULD NOT BACK UP achievements.ini; NOTHING WAS RESET");return;}
    int ok=WritePrivateProfileStringW(L"achievements",NULL,NULL,path)&&WritePrivateProfileStringW(L"progress",NULL,NULL,path);WritePrivateProfileStringW(NULL,NULL,NULL,path);
    notice(ok?L"ALL ACHIEVEMENTS RESET":L"COULD NOT WRITE achievements.ini");
}
/* A view row: the native row's text, with the file's name for it. */
static int launcher_row(int r,wchar_t*label,wchar_t*value,size_t cap){
    if(nat_vp>=0||DYN_PAGE(page))return native_row(r,label,value,cap);
    if(IS_DEV(page))return dev_row(r,label,value,cap);
    VRow v;label[0]=value[0]=0;if(!view_at(r,&v))return ROW_INFO;
    if(v.item<0){wcsncpy(label,L"Developer",cap);return ROW_MENU;}
    int saved=page;page=v.page;int kind=native_row(v.row,label,value,cap);page=saved;
    const TnItem*it=tn_items+v.item;
    if(!(it->flags&TN_GROUP)&&strcmp(tn_live.label[v.item],it->label))MultiByteToWideChar(CP_UTF8,0,tn_live.label[v.item],-1,label,(int)cap);
    label[cap-1]=0;return kind;
}
/* Fills a row's label and (optional) right-aligned value; returns its kind. */
/* Set by native_row for a value drawn in the warning red: a heavy Display step. */
static int row_warn;
static int native_row(int item,wchar_t*label,wchar_t*value,size_t cap){
    label[0]=value[0]=0;int kind=ROW_VALUE;row_warn=0;
    switch(page){
    case 0:{const wchar_t*names[]={L"Play",L"Saves",L"Mods",L"Options",L"Game installation",L"Quit",L"Developer",L"Cheats",L"Multiplayer",L"Play original game"};wcsncpy(label,names[item>=0&&item<10?item:6],cap);kind=ROW_MENU;
        if(item==7&&enhanced_mode&&cp_active())swprintf(value,cap,L"%d on",cp_on_count());
        if(item==0)kind=ROW_ACTION;
        else if(item==2&&mod_count)swprintf(value,cap,L"%d of %d on",mods_on(),mod_count);
        else if(item==5)kind=ROW_ACTION;
        break;}
    case 20:case 21:case 22:kind=sb_row(item,label,value,cap);break;
    case 28:case 29:case 38:kind=lb_row(item,label,value,cap);break;
    case CHEAT_PAGE:kind=cp_row(item,label,value,cap);break;
    case 1:if(item==0){wcsncpy(label,L"Player 1",cap);wcsncpy(value,device?L"Controller":L"Keyboard",cap);}
        else if(item==1){const wchar_t*p2[]={L"Off",device?L"Second controller":L"Controller",L"Keyboard"};wcsncpy(label,L"Player 2",cap);wcsncpy(value,p2[p2_device],cap);}
        else if(item==2||item==3){swprintf(label,cap,L"Player %d",item+1);wcsncpy(value,(item==2?p3_device:p4_device)?L"Controller":L"Off",cap);}
        else if(item==4){wcsncpy(label,L"Edit bindings for",cap);wcsncpy(value,gamepad_map?L"Controller":L"Keyboard",cap);}
        else{wcsncpy(label,control_names[item-5],cap);wcsncpy(value,binding==item-5?L"Press a key...":gamepad_map?pad_label(padcontrols[item-5]):controls[item-5],cap);kind=gamepad_map?ROW_VALUE:ROW_ACTION;}
        break;
    case 2:if(item==15){wcsncpy(label,L"Preset",cap);wcsncpy(value,preset_names[current_preset()],cap);break;}
        /* Rows 21-24: the groups (pages 24-27). */
        if(item>=21&&item<=24){const wchar_t*names[]={L"Screen",L"Picture",L"Detail",L"Effects"};wcsncpy(label,names[item-21],cap);kind=ROW_MENU;
            if(item==21)swprintf(value,cap,L"%ls, %ls",resolution_names[resolution],smooth_motion==2?L"120 FPS":smooth_motion?L"60 FPS":L"Original");
            else if(item==24){int on=shadow_mode!=0;for(int e=0;e<4;e++)on+=post_fx[e]!=0;if(on)swprintf(value,cap,L"%d on",on);}
            break;}
        /* Rows 16-19: post effects (gfx_options.h rr_post_*). */
        if(item>=16&&item<20){swprintf(label,cap,L"%hs",rr_post_label(item-16));swprintf(value,cap,L"%hs",rr_post_name(post_fx[item-16]));break;}
        if(item==20){wcsncpy(label,L"Shadow smoothing",cap);swprintf(value,cap,L"%hs",rr_shadow_name(shadow_mode));break;}
        {const wchar_t*names[]={L"Render resolution",L"Texture filter",L"Window",L"Precise vertices",L"HD UI",L"HUD labels",L"Aspect ratio",L"Frame rate",L"Movies",L"Level of detail",
            L"Anti-aliasing",L"VSync",L"Low-latency input",L"Scanlines",L"Draw distance"};wcsncpy(label,names[item>=0&&item<15?item:0],cap);
        const wchar_t*aspects[]={L"4:3",L"16:9",L"21:9"};
        const wchar_t*values[]={resolution_names[resolution],L"" /* filter: gfx below */,fullscreen_mode?L"Fullscreen":L"Windowed",L"" /* texture correction: gfx below */,on_off(hd_ui),larger_text?L"Large":L"Original",aspects[aspect],smooth_motion==2?L"120 FPS":smooth_motion?L"60 FPS":L"Original",fmv_wide?L"Widescreen":L"4:3",L""};
        /* Renderer options (gfx_options.h): names shared with the game's menu. */
        const char*gfx=item==1?rr_filter_name(filter):item==3?rr_tc_name(texture_correction):item==10?rr_aa_name(aa_mode):item==13?rr_scanline_name(scanline_mode):item==14?rr_draw_name(draw_mode):NULL;
        row_warn=(item==3&&rr_tc_heavy(texture_correction))||(item==10&&aa_mode>=3)||(item==1&&rr_xbr_ss_heavy(filter,aa_mode))||(item>=16&&item<20&&rr_post_heavy(item-16,post_fx[item-16]));
        if(item==9){detail_name(model_detail,value,cap);kind=ROW_SLIDER;}else if(gfx)swprintf(value,cap,L"%hs",gfx);else if(item==11)wcsncpy(value,on_off(vsync_on),cap);else if(item==12)wcsncpy(value,on_off(low_latency),cap);else wcsncpy(value,values[item>=0&&item<9?item:0],cap);break;}
    case 3:if(item==0){wcsncpy(label,L"Install a mod",cap);wcsncpy(value,L".rrmod",cap);kind=ROW_ACTION;}
        else if(item==1){wcsncpy(label,L"Open mods folder",cap);kind=ROW_ACTION;}
        else if(item==2){wcsncpy(label,L"Dump for modding",cap);wcsncpy(value,on_off(mod_dump),cap);}
        else{wcsncpy(label,mod_names[item-3],cap);wcsncpy(value,on_off(mod_enabled[item-3]),cap);}
        break;
    case 4:{const wchar_t*names[]={L"Repair / re-import game",L"Open installation folder",L"Prepare local game build",L"Report a problem",L"Play the original game"};wcsncpy(label,names[item>=0&&item<5?item:0],cap);kind=ROW_ACTION;break;}
    case SETUP_PAGE:wcsncpy(label,preset_names[item%3],cap);kind=ROW_ACTION;break;
    case 6:if(item==0){wcsncpy(label,L"Custom names",cap);wcsncpy(value,on_off(rename_riders),cap);}else{swprintf(label,cap,L"Player %d name",item);wcsncpy(value,rider_names[item-1],cap);kind=ROW_ACTION;}break;
    case 7:{const wchar_t*names[]={L"Display",L"User settings",L"Audio",L"System"};wcsncpy(label,names[item],cap);kind=ROW_MENU;break;}
    case 8:{const wchar_t*names[]={L"Rider names",L"Taunt voices",L"Achievements",L"Rider look"};wcsncpy(label,names[item&3],cap);kind=ROW_MENU;break;}
    case 10:if(item>=0&&item<RR_ACH_COUNT){swprintf(label,cap,L"%hs",rr_achievements[item].name);ach_value(item,value,cap);kind=ROW_INFO;}
        else{wcsncpy(label,L"Reset all achievements",cap);kind=ROW_ACTION;}break;
    case 11:if(item==0){wcsncpy(label,L"Discord presence",cap);wcsncpy(value,discord_app_id[0]?on_off(discord_enabled):L"Not set up",cap);}
        else if(item==1){wcsncpy(label,L"Loading speed",cap);wcsncpy(value,fast_loading?L"Fast":L"Original",cap);}
        else if(item==2){wcsncpy(label,L"Intro movies",cap);wcsncpy(value,skip_intro?L"Skip":L"Play",cap);}
        else if(item==3){wcsncpy(label,L"Controls",cap);wcsncpy(value,device?L"Controller":L"Keyboard",cap);kind=ROW_MENU;}
        else{wcsncpy(label,L"Start-up",cap);wcsncpy(value,start_game_directly?L"Straight to game":L"Launcher",cap);}break;
    case 13:if(item==0){wcsncpy(label,L"Music",cap);wcsncpy(value,music_custom?L"Custom":L"Original",cap);}
        else if(item==1){wcsncpy(label,L"Track",cap);if(music_count)swprintf(value,cap,L"%d: %ls",music_index+1,music_files[music_index]);else wcsncpy(value,L"None installed",cap);}
        else{wcsncpy(label,L"Add WAV tracks",cap);kind=ROW_ACTION;}
        break;
    case 14:if(item==2){wcsncpy(label,L"Reset voices",cap);kind=ROW_ACTION;}else{swprintf(label,cap,L"Player %d voice",item+1);swprintf(value,cap,L"%hs",rr_voice_name(voices[item]));}break;
    case 15:if(item>=2*RR_LOOK_KINDS){wcsncpy(label,L"Reset rider look",cap);kind=ROW_ACTION;}else{const wchar_t*what[]={L"bike paint",L"name tag",L"outfit",L"colours"};swprintf(label,cap,L"Player %d %ls",item%2+1,what[item/2]);swprintf(value,cap,L"%hs",rr_look_name(item/2,*look_setting(item)));}break;
    case ORIGINAL_PAGE:{const wchar_t*names[]={L"Play original game",L"Resolution",L"Texture filter",L"Window",L"Aspect ratio",L"Frame rate",L"Esc closes the game",L"Back to original settings"};
        wcsncpy(label,names[item>=0&&item<ORIG_COUNT+2?item:0],cap);
        if(item==0||item>ORIG_COUNT){kind=ROW_ACTION;if(item==0){int changed=0;for(int k=0;k<ORIG_COUNT;k++)changed+=orig[k]!=0;if(changed)swprintf(value,cap,L"%d option%ls on",changed,changed==1?L"":L"s");}break;}
        const int k=item-1,v=orig[k];const wchar_t*aspects[]={L"4:3",L"16:9",L"21:9"},*rates[]={L"Original",L"60 FPS",L"120 FPS"};
        if(k==ORIG_RES)wcsncpy(value,resolution_names[v],cap);
        else if(k==ORIG_FILTER)swprintf(value,cap,L"%hs",rr_filter_name(v));
        else if(k==ORIG_FULL)wcsncpy(value,v?L"Fullscreen":L"Windowed",cap);
        else if(k==ORIG_ASPECT)wcsncpy(value,aspects[v],cap);
        else if(k==ORIG_RATE)wcsncpy(value,rates[v],cap);
        else wcsncpy(value,on_off(v),cap);
        row_warn=k==ORIG_RATE&&v!=0;break;}
    case MULTI_PAGE:if(item==1){wcsncpy(label,L"Find a game",cap);wcsncpy(value,lobby_ready?L"Internet, up to 8":L"Unavailable",cap);kind=lobby_ready?ROW_MENU:ROW_INFO;break;}
        wcsncpy(label,L"Direct connection",cap);wcsncpy(value,online_ready?L"Experimental":L"Not built",cap);kind=ROW_MENU;break;
    /* Greyed out (ROW_INFO, dim) while the runtime has no netplay libraries. */
    case ONLINE_PAGE:{const wchar_t*names[]={L"Host a game",L"Join a game",L"Host address",L"Port",L"Input delay",L"Status"};wcsncpy(label,names[item>=0&&item<6?item:5],cap);
        if(item==0){kind=online_ready?ROW_ACTION:ROW_INFO;if(!online_ready)wcsncpy(value,L"Unavailable",cap);else if(busy==5&&!online_slot)wcsncpy(value,L"Waiting...",cap);else swprintf(value,cap,L"Port %d",online_port);}
        else if(item==1){kind=online_ready?ROW_ACTION:ROW_INFO;if(!online_ready)wcsncpy(value,L"Unavailable",cap);else if(busy==5&&online_slot)wcsncpy(value,L"Connecting...",cap);else swprintf(value,cap,L"%ls",online_address[0]?online_address:L"Set the address");}
        else if(item==2){kind=ROW_ACTION;wcsncpy(value,online_address[0]?online_address:L"Not set",cap);}
        else if(item==3)swprintf(value,cap,L"%d",online_port);
        else if(item==4)swprintf(value,cap,L"%d frame%ls",online_delay,online_delay==1?L"":L"s");
        else{kind=ROW_INFO;wcsncpy(value,online_ready?L"Ready (LAN)":L"Not built",cap);}
        break;}
    }
    label[cap-1]=value[cap-1]=0;return kind;
}
static const wchar_t*native_help(void);
/* The selected row's help: the file's text once it differs from the built-in
 * one; until then the launcher's own (with its notes on mode, counts ...). */
static const wchar_t*menu_help(void){
    if(nat_vp>=0||DYN_PAGE(page))return native_help();
    if(IS_DEV(page))return dev_help();
    VRow v;if(!view_at(selected,&v))return L"";
    if(v.item<0)return L"Change the launcher's menus: names, help text, order, pages and the level-of-detail steps.\nNothing changes until you save. Ctrl+Shift+D hides this page again.";
    /* A group (each achievement, mod, custom code) keeps its own row's help. */
    const wchar_t*h;
    if(!(tn_items[v.item].flags&TN_GROUP)&&strcmp(tn_live.help[v.item],tn_items[v.item].help)){static wchar_t t[TN_HELP];MultiByteToWideChar(CP_UTF8,0,tn_live.help[v.item],-1,t,TN_HELP);h=t;}
    else{int sp=page,ss=selected;page=v.page;selected=v.row;h=native_help();page=sp;selected=ss;}
    /* Texture filter and Anti-aliasing rows: the xBR + Supersample warning (gfx_options.h
       rr_xbr_ss_heavy) leads whichever help text is shown, the file's own included. */
    if(v.page==2&&(v.row==1||v.row==10)&&rr_xbr_ss_heavy(filter,aa_mode)){static wchar_t w[TN_HELP+256];
        swprintf(w,TN_HELP+256,L"WARNING: xBR with Supersample is very heavy (xBR runs on every supersampled pixel); at 120 FPS most PCs fall back to 60. For smooth 120 use Sharp or Smooth, or FXAA.\n%ls",h);return w;}
    return h;
}
static const wchar_t*native_help(void){
    static wchar_t text[600];
    if(SB_PAGE(page))return sb_help();
    if(LB_PAGE(page))return lb_help();
    if(page==0){
        if(selected==6)return L"";
        if(selected==7)return enhanced_mode?(cp_active()?L"Cheats are ON: GameShark codes and unlocks apply in Enhanced games.\nAchievements stay locked until every cheat is off.":L"GameShark codes and the game's own unlocks, for Enhanced. All off by default.\nAchievements stay locked while any cheat is on."):L"Cheats apply in Enhanced only.\nOriginal always plays the stock game.";
        if(selected==1)return L"Your memory-card saves and quick resume slots: play, copy, export, import and delete.\nCards are backed up before every launch.";
        if(selected==8)return L"Two players on one PC are in the game's own Multiplayer menu.\nOnline: two PCs, one player each.";
        if(selected==0){swprintf(text,600,L"Start Road Rash: Jailbreak with your settings (preset: %ls), music and enabled mods.\nPlay original game, below, starts the unmodified game.",preset_names[current_preset()]);return text;}
        if(selected==9)return L"The unmodified game: no PC menu, mods, cheats or achievements. Same memory cards.\nA few opt-in options (resolution, window, frame rate ...), all off to start.";
        if(selected==2){if(mod_count){swprintf(text,600,L"%d installed, %d switched on.\nInstall .rrmod packs (art, textures, sounds, models) and switch them on or off. Mods apply in Enhanced.",mod_count,mods_on());return text;}return L"Install .rrmod packs (art, textures, sounds, models) and switch them on or off.\nMods apply in Enhanced; Original always stays stock.";}
        const wchar_t*items[]={L"",L"",L"",L"Display, rider, audio and system settings, including controls.",L"Repair, verify and prepare your own copy of the game.\nNo disc is needed after installation.",L"Close the launcher.\nYour settings are already saved."};return items[selected];
    }
    if(page==1){if(binding>=0)return L"Press a key for this action.\nEsc cancels.";
        if(selected==0)return device?L"A controller plays the game's analog setup: left stick steers, right stick accelerates and brakes. In game, Game options > Controller switches Analog / Digital. Next launch.":L"Choose the device you play with. A controller plays the game's analog setup.\nApplies on your next launch.";
        if(selected==1)return device?L"Plug in a second controller, or give Player 2 the keyboard. Controllers play analog; in game, Game options > Controller > 2 sets Player 2 up. Next launch.":L"Player 2 uses your controller (analog) while you keep the keyboard. In game, Game options > Controller > 2 sets it up. Applies on your next launch.";
        if(selected==2||selected==3)return L"A controller on the game's multitap, for Side Car (Versus) with up to 4 players; all controllers then play digital. Pads go to players in the order they were plugged in, Player 1 first. Next launch.";
        if(selected==4)return L"Choose whether to edit keyboard keys or controller buttons.\nIn game, Game options > Controller shows every action with your keys.";
        return gamepad_map?L"Select to cycle through controller buttons. Names follow the game's Digital 1 setup; in Analog the sticks steer and accelerate. Next launch.":L"Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch.";}
    if(page==SETUP_PAGE){const wchar_t*help[]={L"1080p, 16:9, 60 FPS, HD menus and more detailed riders. Runs well on most PCs.",
            L"The PS1's look: 4:3, its own frame rate and menus, drawn sharp. Saves and achievements still work.",
            L"1440p, 16:9, 120 FPS, supersampling and the most detailed riders. Needs a fast PC and a 120 Hz display."};
        swprintf(text,600,L"%ls\nChange it later in Options > Display.",help[selected%3]);return text;}
    if(page==2&&selected>=21&&selected<=24){const wchar_t*help[]={L"Render resolution, window, aspect ratio and frame rate, plus VSync and low-latency input.",
            L"Texture filter, texture correction, anti-aliasing and scanlines, plus HD UI, HUD labels and movies.",
            L"Level of detail and draw distance: how far away riders, bikes and traffic stay detailed and visible.",
            L"Bloom, sharpening, colour, vignette and shadow smoothing on the race picture. All off to start; races only."};
        return help[selected-21];}
    if(page==2&&selected==15){int p=current_preset();
        if(p==PRESET_CUSTOM)return L"Custom: your own mix. Left / Right picks a preset again.\nThe window setting, audio, riders and controls are not changed.";
        /* Built from the preset's values (editable on the Developer page). */
        const int shown[]={0,5,6,3,9,8,11};char s[400];size_t n=(size_t)snprintf(s,sizeof s,"%s: ",tn_preset_names[p]);
        for(int j=0;j<7&&n<sizeof s-60;j++){int k=shown[j];char v[48];tn_preset_text(&tn_live,k,preset_value(p,k),v,sizeof v);
            n+=(size_t)snprintf(s+n,sizeof s-n,k==3?"%sHD UI %s":k==8?"%sdetail %s":k==9?"%santi-aliasing %s":k==11?"%sdraw distance %s":"%s%s",j?", ":"",v);}
        swprintf(text,600,L"%hs.\nThe window setting, audio, riders and controls are not changed.",s);return text;}
    if(page==2){
        /* Rows 0-7 are also on the in-game Display page, where they apply at once. */
        const wchar_t*help[]={L"The game's internal resolution, in whole multiples of the PS1 picture.",L"Sharp keeps the PS1's crisp texels; Smooth blends them; xBR rounds off blocky edges and curves (sky, signs, HUD art) at high resolution.",L"Play in a window or fullscreen. The picture keeps its aspect ratio.",texture_correction==2?L"WARNING: heavy CPU load (holds 60 FPS, not 120). On: no PS1 \"wobble\", perspective-correct textures on almost all of the road, scenery and riders.":L"Off: the PS1's original wobbling geometry and warping textures. On: precise vertices, no wobble, perspective-correct textures (heavy on the CPU).",L"On: HD lettering, smoothed menus and HUD.\nOff: the game's original look.",L"Larger rider names and race messages. Also in the pause options.",L"Menus and races in 4:3, 16:9 or 21:9: a wider view, original HUD.",L"Real game frames at the original game speed.\n120 FPS needs a 120 Hz+ display and a fast PC.",L"Full-screen movies: original 4:3 frame, or stretched to fill a widescreen display."};
        if(selected==9){
            wchar_t safe[16];detail_name(det_safe(),safe,16);
            if(model_detail>det_safe())swprintf(text,600,L"WARNING: above %ls, frame rate can drop in crowded races (pile-ups, 16 riders, split screen).\nRiders and bikes keep full detail further away.%ls",safe,enhanced_mode?L"":L" Saved for Enhanced.");
            else swprintf(text,600,L"How far away riders and bikes keep full detail. The PS1 swapped in low-detail models almost at once.\n%ls%ls",model_detail==det_safe()?L"Recommended maximum: holds 60 FPS everywhere. ":L"",enhanced_mode?L"Applies on your next launch.":L"Saved for Enhanced; Original ignores it.");
            return text;}
        /* Rows 16-19: post effects on the race picture (gfx_options.h). */
        if(selected==20){swprintf(text,600,L"%lsShadow smoothing: soft edges on the bikes' and riders' shadows instead of the PS1's hard pixel steps.\nRaces only. Also in game: Display > Effects.",rr_shadow_heavy(shadow_mode)?L"WARNING: heavy GPU load. ":L"");return text;}
        if(selected>=16&&selected<20){const int e=selected-16,s=post_fx[e];
            const wchar_t*what[]={L"Bloom: a soft glow around bright areas such as lights and the sky.",L"Sharpening: crisper detail without halos (contrast-adaptive).",
                L"Colour: livelier colour (vibrance) and a little more contrast.",L"Vignette: darker picture corners, drawing the eye to the road."};
            swprintf(text,600,L"%ls%ls\nRaces only; HUD and menus untouched. In game: Display > Effects.",rr_post_heavy(e,s)?L"WARNING: heavy GPU load. ":L"",what[e]);return text;}
        /* Rows 10-14 (gfx_options.h): anti-aliasing, scanlines and draw distance are
           also on the in-game Display page (its second screen). */
        if(selected>=10){
            const wchar_t*aa[]={L"Smooths jagged edges. FXAA is light on the GPU; Supersample is the cleanest but heavy.",
                L"FXAA: a light edge-smoothing filter on the finished picture. Almost no GPU cost.",
                L"FXAA HQ: a stronger edge-smoothing filter on the finished picture. Light GPU cost.",
                L"WARNING: heavy GPU load. Draws 2x your Render resolution's lines, then scales down.",
                L"WARNING: very heavy GPU load. Draws 4x your Render resolution's lines, then scales down."};
            const wchar_t*h=selected==10?aa[aa_mode>=0&&aa_mode<rr_aa_steps?aa_mode:0]
                :selected==11?L"On: no tearing on a 60 Hz display (120 Hz at 120 FPS). Off: least delay, may tear. Game speed is unchanged."
                :selected==12?L"On: controls are read again just before each frame, for about one frame less delay. Off: standard timing."
                :selected==13?L"Dark gaps between the PS1's picture lines, like an old CRT TV: Light, Medium or Strong.\nOff: the clean picture."
                :L"How far away riders, bikes and traffic are drawn. Original: the PS1's, where they pop in.";
            const int live=selected==10||selected==13||selected==14;
            swprintf(text,600,L"%ls%ls%ls",h,live?L"\nAlso in game: Options > Display.":L"",enhanced_mode?L"\nApplies on your next launch.":L"\nSaved for Enhanced; Original ignores it.");return text;}
        swprintf(text,600,L"%ls%ls%ls",help[selected],selected<8?L"\nAlso in game: Options > Display.":L"",enhanced_mode?L"\nApplies on your next launch.":L"\nSaved for Enhanced; Original ignores it.");return text;}
    if(page==3){
        if(selected==0)return enhanced_mode?L"Choose an .rrmod pack to install: artwork, textures, sounds or models.\nNew mods start switched off.":L"Choose an .rrmod pack to install.\nMods apply in Enhanced; Original always uses the stock game data.";
        if(selected==1)return L"Open the mods folder in your installation.\nRemoved mods are kept in mods\\removed.";
        if(selected==2)return mod_dump?L"On: your next Enhanced games save the textures, sounds and models they use to mods\\texture-dump, sound-dump and model-dump. Texture packs pause meanwhile.":L"Off. Switch on to save the textures, sounds and models your Enhanced games use, so you can edit them for your own pack (see MODDING.md).";
        int i=selected-3;if(i>=0&&i<mod_count){swprintf(text,600,L"%ls%ls%ls. Enter or click switches it %ls; changes apply on your next Enhanced launch.\nDel or the X button removes it (moved to mods\\removed).",mod_enabled[i]?L"On":L"Off",mod_contents[i][0]?L": ":L"",mod_contents[i],mod_enabled[i]?L"off":L"on");return text;}
        return L"";
    }
    if(page==4){const wchar_t*help[]={L"Re-import your .bin (or .cue / .iso) to repair the installed game data.",L"Open the folder that holds your installed game, local build and saves.",L"Rebuild the local native game from your installed data.\nThis can take a few minutes.",L"Save a bug report: a picture of this screen, the game's recent logs and your settings, with a short note.\nIt goes into the reports folder of your installation.",
        L"The unmodified game, once: no PC options, mods or achievements. Same memory cards.\nFor comparing, or if something looks wrong. Play stays enhanced."};return help[selected>=0&&selected<5?selected:0];}
    if(page==6)return selected?L"Type a name of up to 8 characters.\nSaved on this PC.":enhanced_mode?L"Custom names appear in supported race HUD labels. Characters and saved profiles stay unchanged.":L"Names apply in Enhanced. Original keeps the game's own rider names.";
    if(page==7){const wchar_t*help[]={L"Resolution, textures, HD UI, aspect ratio, frame rate and level of detail.",L"Everything about your riders: names and taunt voices.\nAlso your achievements.",L"Music: the original soundtrack or your own WAV tracks.",L"Loading speed, intro movies, controls and start-up."};return help[selected];}
    if(page==8){const wchar_t*help[]={L"Both riders' names, shown in races.\nSaved on this PC.",L"Your taunts in another rider's voice.\nPlayer 1 and Player 2 pick separately.",L"Local achievements.\nSaved on this PC.",L"Bike paint, name tag colour and outfit.\nPlayer 1 and Player 2 pick separately."};return help[selected&3];}
    if(page==15){const wchar_t*help[]={L"Your bike in another colour. Original keeps the game's paint.\nNot in Five-O races. Applies to Enhanced races.",L"The colour of your name under your health bars in races.\nNot in Five-O races. Applies to Enhanced races.",L"Ride as a gang member or police: DeSades or Kaffe Boys, Captain, Lieutenant or Grunt. Not in Jail Break, where your chosen character stays, and not in Five-O.\nApplies to Enhanced races.",L"Your rider in another colour scheme: the gangs' and police colours, or schemes no rider wears. Original keeps the outfit's own colours.\nNot in Five-O races. Applies to Enhanced races."};
        return selected>=2*RR_LOOK_KINDS?L"Both players back to their own paint, name tag, rider and colours.":help[(selected/2)%RR_LOOK_KINDS];}
    if(page==10){if(selected>=RR_ACH_COUNT){swprintf(text,600,L"%d of %d unlocked. Locks every achievement again and clears all progress (races, wins, riding time).\nThe old file is kept as achievements.ini.bak. Close the game first.",ach_unlocked_count(),RR_ACH_COUNT);return text;}
        /* The game menu's two short lines, joined into one sentence here. */
        wchar_t h[200];swprintf(h,200,L"%hs",rr_achievements[selected].help);for(wchar_t*c=h;*c;c++)if(*c==L'\n')*c=L' ';
        wchar_t path[32768],key[16];join(path,store,L"achievements.ini");swprintf(key,16,L"unlock%d",selected);unsigned long long t=0;{wchar_t v[32];GetPrivateProfileStringW(L"achievements",key,L"0",v,32,path);t=wcstoull(v,NULL,10);}
        if(t){FILETIME ft;unsigned long long w=(t+11644473600ULL)*10000000ULL;ft.dwLowDateTime=(DWORD)w;ft.dwHighDateTime=(DWORD)(w>>32);wchar_t when[48];FILETIME local;SYSTEMTIME s;FileTimeToLocalFileTime(&ft,&local);FileTimeToSystemTime(&local,&s);
            const wchar_t*mon[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};swprintf(when,48,L"%d %ls %d, %02d:%02d",s.wDay,mon[(s.wMonth+11)%12],s.wYear,s.wHour,s.wMinute);swprintf(text,600,L"%ls\nUnlocked %ls.",h,when);}
        else swprintf(text,600,L"%ls\nNothing unlocks while a cheat is on.",h);return text;}
    if(page==11&&selected==4)return L"Straight to game: once set up, opening Jailbreak Recomp starts the game at once and the launcher closes with it.\nHold Shift while opening it to get the launcher back.";
    if(page==11&&selected==3)return L"Devices, Player 2 and key / button maps. The game's own Analog / Digital setup is in game: Options > Game settings > Controller.";
    if(page==11&&selected==1)return enhanced_mode?L"Fast: a 4x disc drive, so races load in about half a second. Movies keep their own pace.\nOriginal: the PS1's own load times. Applies on your next launch.":L"Fast loading applies in Enhanced.\nOriginal always uses the PS1's load times.";
    if(page==11&&selected==2)return enhanced_mode?L"Skip the start-up movies and go straight to the title screen. Story scenes and the menu's idle movies still play.\nApplies on your next launch.":L"Skipping intro movies applies in Enhanced.\nOriginal always plays them.";
    if(page==13)return selected==0?L"Choose the original soundtrack or your own music.\nCustom tracks replace race music in Enhanced.":selected==1?L"Select an imported track. Left / Right to browse.\nIn game: Left / Right to browse; volume in Game options > Noise.":L"Copy your own WAV into the local music library.\nUp to 128 tracks, 64 MB each. Your source file stays where it is.";
    if(page==CHEAT_PAGE)return cp_help(selected);
    if(page==ORIGINAL_PAGE){const wchar_t*help[]={L"The unmodified game: no PC menu, mods, cheats or achievements. Same memory cards.\nWith every option below off, it is the stock game.",
            L"Draw the game sharper, in whole multiples of the PS1 picture. Native is the PS1's own.\nApplies on your next Original launch.",
            L"Sharp is the PS1's own texels; Smooth blends them; xBR rounds off blocky edges.\nApplies on your next Original launch.",
            L"Start in a window or fullscreen. Alt+Enter switches while you play.\nApplies on your next Original launch.",
            L"16:9 or 21:9 widen the picture. The original HUD and menus are not adjusted and may look stretched.\nNext Original launch.",
            orig[ORIG_RATE]?L"NOT THE ORIGINAL TIMING: races run at 60 or 120 real frames, as in Enhanced. 120 FPS needs a 120 Hz+ display.\nNext Original launch.":
                L"Original: the game's own frame rate and timing. 60 or 120 FPS are opt-in and are not the original timing.\nNext Original launch.",
            L"On: Esc closes the game and returns to the launcher (the original game has no quit option).\nOff: close the window instead.",
            L"Every option on this page back to the stock game's own."};
        return help[selected>=0&&selected<ORIG_COUNT+2?selected:0];}
    if(page==MULTI_PAGE&&selected==1)return lobby_ready?L"Play over the internet through the public lobby: browse rooms or open your own, up to 8 players. Players 3-8 take over AI riders in Skull-to-Skull.\nMods and cheats must be off.":
        L"The online game isn't built in this installation yet. Game installation > Prepare local game build builds it (needs internet).";
    if(page==MULTI_PAGE)return online_ready?L"Two PCs, one player each: Skull-to-Skull, Cops & Robbers and Side Car versus. Up to two players, as in the game.\nExperimental.":
        L"Two PCs, one player each. The online game isn't built in this installation yet: Game installation > Prepare local game build builds it (needs internet).\nLocal two-player is in the game's own Multiplayer menu.";
    if(page==ONLINE_PAGE){
        if(!online_ready&&selected!=2&&selected!=3&&selected!=4)return L"The online game isn't built in this installation yet: Game installation > Prepare local game build builds it (needs internet).\nLocal two-player is in the game's own Multiplayer menu.";
        const wchar_t*help[]={L"Wait for the other player to join on your port. Both PCs use your frame rate, aspect, detail, draw distance and loading speed. Mods and cheats must be off.",
            L"Connect to the host's address and port; you are Player 2. Your rider name, taunt voice and rider look are sent, and both PCs show both riders' choices.",
            L"The host PC's IP address on your network (for Join), e.g. 192.168.1.20.",
            L"The host uses this port, the joining PC the next one (UDP and TCP). Allow both in your firewall. Left / Right to change.",
            L"Frames between a button press and the game seeing it on both PCs. 2 suits a home network; raise it if the game stutters.",
            L"Online plays the two-player modes over your local network (LAN), one player per PC. Experimental."};
        return help[selected>=0&&selected<6?selected:5];}
    if(page==14)return selected==2?L"Both players back to their own voices.":L"Your taunts in another rider's voice.\nPlayer 1 and Player 2 pick separately.\nApplies to Enhanced races.";
    return L"";
}
/* Preview artwork kind for the info card (indices of rr_art_get). */
static int native_art(void);static int dev_art(void);
/* An item's automatic picture: its own row's choice. */
static int item_auto_art(int item){int sp=page,ss=selected;page=tn_items[item].page;selected=tn_items[item].row;int a=native_art();page=sp;selected=ss;return a;}
static int preview_art(void){
    if(nat_vp>=0||DYN_PAGE(page))return native_art();
    if(IS_DEV(page))return dev_art();
    VRow v;if(!view_at(selected,&v))return RR_ART_LOGO;
    if(v.item>=0&&tn_live.picture[v.item]>=0)return tn_live.picture[v.item];   /* chosen on the Developer page */
    int sp=page,ss=selected;page=v.page;selected=v.row;int a=native_art();page=sp;selected=ss;return a;
}
/* Each row shows the owner-disc still that pictures it (RR_ART_*). */
static int preset_art(int p){return p==PRESET_CLASSIC?RR_ART_PAD:p==PRESET_HIGH?RR_ART_RACE_BIKE:RR_ART_LOGO;}
static int native_art(void){
    switch(page){
    case 0:{const int a[]={RR_ART_LOGO,RR_ART_CARD,RR_ART_CHOPPER,RR_ART_SPORT_BIKE,RR_ART_STONE_LOGO,RR_ART_WRECK,RR_ART_MAP,RR_ART_WARRANT,RR_ART_GANG,RR_ART_BETA_MAP};return a[selected>=0&&selected<10?selected:0];}
    case MULTI_PAGE:return RR_ART_RIDERS;
    case 28:case 29:return RR_ART_SKULLS;
    case ONLINE_PAGE:return selected<2?RR_ART_SKULLS:RR_ART_RIDERS;
    case 1:return RR_ART_PAD;
    case 2:{if(selected==15)return preset_art(current_preset());
        if(selected>=21&&selected<=24){const int g[]={RR_ART_RASH_CITY,RR_ART_CHOPPER,RR_ART_ALPINE,RR_ART_CACTUS_HILLS};return g[selected-21];}
        /* post effects (bloom, sharpening, colour, vignette, shadow smoothing): race
           pictures; the beta course cards (unused on the disc) where a scene fits */
        if(selected>=16&&selected<=20){const int p[]={RR_ART_CACTUS_HILLS,RR_ART_RACE_BIKE,RR_ART_MEADOWS,RR_ART_COUNTRYSIDE,RR_ART_RACE_BIKE};return p[selected-16];}
        /* render, filter, window, texture correction, HD UI, HUD labels, aspect, frame rate, movies, detail, AA, vsync, latency, scanlines, draw distance */
        const int a[]={RR_ART_RASH_CITY,RR_ART_CHOPPER,RR_ART_LOGO,RR_ART_CHOPPER,RR_ART_LOGO,RR_ART_RIDERS,RR_ART_COAST,RR_ART_STOPWATCH,RR_ART_JAILED,RR_ART_SPORT_BIKE,RR_ART_CHOPPER,RR_ART_STOPWATCH,RR_ART_PAD,RR_ART_STONE_LOGO,RR_ART_ALPINE};
        return a[selected>=0&&selected<15?selected:0];}
    case 3:return RR_ART_CHOPPER;
    /* repair, open folder, prepare build, report a problem (the cop on the radio), play original */
    case 4:{const int a[]={RR_ART_STONE_LOGO,RR_ART_STONE_LOGO,RR_ART_SPAZ_ESCORT,RR_ART_HELMET_COP,RR_ART_BETA_MAP};return a[selected>=0&&selected<5?selected:0];}
    /* the riders' own portraits where a rider is named or speaks */
    case 6:{const int a[]={RR_ART_GANG,RR_ART_SWITCH,RR_ART_KAT};return a[selected>=0&&selected<3?selected:0];}
    case 14:{const int a[]={RR_ART_BRICK,RR_ART_DRU,RR_ART_TAUNT};return a[selected>=0&&selected<3?selected:0];}
    /* paint, name tag (a wanted-poster name), outfit, colours, back to stock */
    case 15:return selected<2?RR_ART_CHOPPER:selected<4?RR_ART_MUGSHOT:selected<6?RR_ART_RIDERS:selected<8?RR_ART_GANG:RR_ART_FREE_SPAZ;
    case 7:{const int a[]={RR_ART_LOGO,RR_ART_RIDERS,RR_ART_MUSIC,RR_ART_RACE_BIKE};return a[selected&3];}
    case 8:{const int a[]={RR_ART_GANG,RR_ART_TAUNT,RR_ART_TROPHY,RR_ART_CHOPPER};return a[selected&3];}
    case 10:return selected>=RR_ACH_COUNT?RR_ART_CAGED:RR_ART_TROPHY;
    /* Discord (Spaz on the phone), loading, intro movies, controls, start-up (Spaz breaking free) */
    case 11:{const int a[]={RR_ART_SPAZ_CALL,RR_ART_STOPWATCH,RR_ART_JAILED,RR_ART_PAD,RR_ART_FREE_SPAZ};return a[selected%5];}
    case 13:return selected==2?RR_ART_SPAZ_BAR:RR_ART_MUSIC;
    case SETUP_PAGE:return preset_art(selected%3);
    /* codes on the warrant, course unlocks on the map; switching them all off is the law, adding one the donut cop */
    case CHEAT_PAGE:return selected==10?RR_ART_COP:selected==11?RR_ART_DONUT_COP:selected>=6&&selected<=8?RR_ART_MAP:RR_ART_WARRANT;
    case ORIGINAL_PAGE:{const int a[]={RR_ART_BETA_MAP,RR_ART_RASH_CITY,RR_ART_CHOPPER,RR_ART_LOGO,RR_ART_COAST,RR_ART_STOPWATCH,RR_ART_GRABBED,RR_ART_FREE_SPAZ};return a[selected>=0&&selected<ORIG_COUNT+2?selected:0];}
    case 20:case 21:case 22:return RR_ART_CARD;
    }
    return RR_ART_LOGO;
}

/* ---- Pixel surface ------------------------------------------------------ */
#define RGBX(r,g,b) ((uint32_t)(((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b)))
#define C_STEEL RGBX(179,197,220)
#define C_GOLD RGBX(255,190,37)
#define C_WHITE RGBX(240,242,250)
#define C_INK RGBX(1,4,13)
#define C_PANEL RGBX(3,12,40)
#define C_DIM RGBX(128,144,170)
typedef struct Surface {uint32_t*px;int w,h;} Surface;
/* Logical layout: a 1280x720 design frame, uniformly scaled by k and centred.
 * lw/lh are the client size in logical units; cx/by/hy offset the frame. */
static struct {Surface s;HDC dc;double k,lw,lh,cx,by,hy;} ui;
static int toast_painted,hover_hit,hover_row=-1;
static int render_width=1280,render_height=720;
static void ui_layout(int w,int h){ui.k=fmin(w/1280.0,h/720.0);if(!(ui.k>0))ui.k=1;ui.lw=w/ui.k;ui.lh=h/ui.k;ui.cx=(ui.lw-1280)/2;ui.by=(ui.lh-720)/2;ui.hy=ui.by*.6;}
static int P(double v){return (int)floor(v*ui.k+.5);}
static COLORREF cref(uint32_t c){return RGB(c>>16&255,c>>8&255,c&255);}
static uint32_t blend(uint32_t d,uint32_t c,unsigned a){if(a>=255)return c;if(!a)return d;unsigned ia=255-a;return (((d>>16&255)*ia+(c>>16&255)*a)/255)<<16|(((d>>8&255)*ia+(c>>8&255)*a)/255)<<8|((d&255)*ia+(c&255)*a)/255;}
static uint32_t mix(uint32_t a,uint32_t b,double t){if(t<0)t=0;if(t>1)t=1;return blend(a,b,(unsigned)(t*255+.5));}
static int clip_rect(int*x,int*y,int*w,int*h){if(*x<0){*w+=*x;*x=0;}if(*y<0){*h+=*y;*y=0;}if(*x+*w>ui.s.w)*w=ui.s.w-*x;if(*y+*h>ui.s.h)*h=ui.s.h-*y;return *w>0&&*h>0;}
/* Gradient fill in device pixels; colour and opacity both interpolate. */
static void px_grad(int x,int y,int w,int h,uint32_t c0,uint32_t c1,unsigned a0,unsigned a1,int vertical){
    GdiFlush();int ox=x,oy=y,ow=w,oh=h;if(!clip_rect(&x,&y,&w,&h))return;
    for(int j=y;j<y+h;j++){uint32_t*row=ui.s.px+(size_t)j*ui.s.w;
        for(int i=x;i<x+w;i++){double t=vertical?(oh>1?(j-oy)/(double)(oh-1):0):(ow>1?(i-ox)/(double)(ow-1):0);row[i]=blend(row[i],mix(c0,c1,t),(unsigned)(a0+(a1-(double)a0)*t+.5));}}
}
static void px_fill(int x,int y,int w,int h,uint32_t c,unsigned a){px_grad(x,y,w,h,c,c,a,a,1);}
static void fill(double x,double y,double w,double h,uint32_t c,unsigned a){int X=P(x),Y=P(y);px_fill(X,Y,P(x+w)-X,P(y+h)-Y,c,a);}
static void grad(double x,double y,double w,double h,uint32_t c0,uint32_t c1,unsigned a0,unsigned a1,int vertical){int X=P(x),Y=P(y);px_grad(X,Y,P(x+w)-X,P(y+h)-Y,c0,c1,a0,a1,vertical);}
static void outline(double x,double y,double w,double h,uint32_t c,unsigned a){int X=P(x),Y=P(y),W=P(x+w)-X,H=P(y+h)-Y,t=P(1)>1?P(1):1;px_fill(X,Y,W,t,c,a);px_fill(X,Y+H-t,W,t,c,a);px_fill(X,Y+t,t,H-2*t,c,a);px_fill(X+W-t,Y+t,t,H-2*t,c,a);}
/* Bilinear resample of a source rectangle into a device rectangle. Source
 * samples clamp to the rectangle so cropped textures never bleed. premul
 * composites premultiplied BGRA (the WIC logo); otherwise it is opaque. */
static void resample(int dx,int dy,int dw,int dh,const uint32_t*src,int stride,int sx,int sy,int sw,int sh,int premul,unsigned opacity){
    GdiFlush();if(dw<=0||dh<=0||sw<=0||sh<=0||!src)return;
    int x0=dx>0?dx:0,y0=dy>0?dy:0,x1=dx+dw<ui.s.w?dx+dw:ui.s.w,y1=dy+dh<ui.s.h?dy+dh:ui.s.h;if(x0>=x1||y0>=y1)return;
    int n=x1-x0;int*xa=malloc(sizeof(int)*n*3);if(!xa)return;
    for(int i=0;i<n;i++){double f=sx+(x0+i-dx+.5)*sw/dw-.5;if(f<sx)f=sx;if(f>sx+sw-1)f=sx+sw-1;int a=(int)f;xa[i*3]=a;xa[i*3+1]=a+1<sx+sw?a+1:a;xa[i*3+2]=(int)((f-a)*256);}
    for(int y=y0;y<y1;y++){
        double f=sy+(y-dy+.5)*sh/dh-.5;if(f<sy)f=sy;if(f>sy+sh-1)f=sy+sh-1;int r0=(int)f,r1=r0+1<sy+sh?r0+1:r0;unsigned wy=(unsigned)((f-r0)*256);
        const uint32_t*top=src+(size_t)r0*stride,*bot=src+(size_t)r1*stride;uint32_t*out=ui.s.px+(size_t)y*ui.s.w;
        for(int i=0;i<n;i++){
            unsigned wx=(unsigned)xa[i*3+2];uint32_t a=top[xa[i*3]],b=top[xa[i*3+1]],c=bot[xa[i*3]],d=bot[xa[i*3+1]],v=0;
            for(int s=0;s<32;s+=8){unsigned t=(a>>s&255)*(256-wx)+(b>>s&255)*wx,u=(c>>s&255)*(256-wx)+(d>>s&255)*wx;v|=(((t*(256-wy)+u*wy)>>16)&255)<<s;}
            uint32_t*p=out+x0+i;
            if(!premul)*p=opacity>=255?(v&0xFFFFFF):blend(*p,v,opacity);
            else{unsigned sa=(v>>24)*opacity/255;if(!sa)continue;uint32_t r=0;for(int s=0;s<24;s+=8){unsigned sc=(v>>s&255)*opacity/255,dc=*p>>s&255;unsigned o=sc+dc*(255-sa)/255;r|=(o>255?255:o)<<s;}*p=r;}
        }
    }
    free(xa);
}
/* Cover-fit an image into a rectangle, cropping the overflow evenly. */
static void cover(const uint32_t*src,int w,int h,double x,double y,double dw,double dh){
    int X=P(x),Y=P(y),W=P(x+dw)-X,H=P(y+dh)-Y;if(W<=0||H<=0||w<=0||h<=0)return;
    double da=(double)W/H,sa=(double)w/h;int sx=0,sy=0,sw=w,sh=h;
    if(da>sa){sh=(int)(w/da+.5);if(sh<1)sh=1;sy=(h-sh)/2;}else{sw=(int)(h*da+.5);if(sw<1)sw=1;sx=(w-sw)/2;}
    resample(X,Y,W,H,src,w,sx,sy,sw,sh,0,255);
}
/* Anti-aliased filled triangle (4x4 supersampled), for arrow keycaps. */
static void triangle(double x0,double y0,double x1,double y1,double x2,double y2,uint32_t c,unsigned alpha){
    GdiFlush();double X[3]={x0*ui.k,x1*ui.k,x2*ui.k},Y[3]={y0*ui.k,y1*ui.k,y2*ui.k};
    int minx=(int)floor(fmin(X[0],fmin(X[1],X[2]))),maxx=(int)ceil(fmax(X[0],fmax(X[1],X[2]))),miny=(int)floor(fmin(Y[0],fmin(Y[1],Y[2]))),maxy=(int)ceil(fmax(Y[0],fmax(Y[1],Y[2])));
    double area=(X[1]-X[0])*(Y[2]-Y[0])-(X[2]-X[0])*(Y[1]-Y[0]);if(fabs(area)<1e-6)return;
    for(int py=miny;py<maxy;py++){if(py<0||py>=ui.s.h)continue;for(int px=minx;px<maxx;px++){if(px<0||px>=ui.s.w)continue;int in=0;
        for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++){double qx=px+(sx+.5)/4,qy=py+(sy+.5)/4,e0=(X[1]-X[0])*(qy-Y[0])-(Y[1]-Y[0])*(qx-X[0]),e1=(X[2]-X[1])*(qy-Y[1])-(Y[2]-Y[1])*(qx-X[1]),e2=(X[0]-X[2])*(qy-Y[2])-(Y[0]-Y[2])*(qx-X[2]);
            if(area>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0))in++;}
        if(in){uint32_t*p=ui.s.px+(size_t)py*ui.s.w+px;*p=blend(*p,c,alpha*in/16);}}}
}

/* ---- HD lettering (Roboto Flex, our own OFL font: no game asset) -------- */
/* All launcher text matches the game's HD menus: rows and help in Roboto
 * Flex Regular, page titles in Roboto Flex Black (hd_title). Outlines
 * (hd_font_data.h) are filled with even-odd spans, four sub-rows per device
 * pixel, so text is crisp at any window size. */
#include "hd_font_data.h"
enum {HD_REGULAR=0,HD_BLACK=2};
/* Text is reduced to the font's ASCII set; common typography maps across. */
static int ascii_text(const wchar_t*s,wchar_t*out,int cap){
    int n=0;for(;s&&*s&&n<cap-4;s++){wchar_t c=*s;
        if(c==0x2026){out[n++]='.';out[n++]='.';out[n++]='.';continue;}
        if(c==0x2014||c==0x2013)c='-';else if(c==0x2039)c='<';else if(c==0x203A)c='>';else if(c==0x00D7)c='x';else if(c==0x2018||c==0x2019)c='\'';else if(c==0x201C||c==0x201D)c='"';else if(c>126||(c<32&&c!='\n'))c='?';
        out[n++]=c;}
    out[n]=0;return n;
}
typedef struct Mask {unsigned char*a;int x,y,w,h;} Mask;
static void mask_draw(const Mask*m,int dx,int dy,uint32_t color,unsigned opacity){
    GdiFlush();for(int j=0;j<m->h;j++){int Y=m->y+dy+j;if(Y<0||Y>=ui.s.h)continue;uint32_t*row=ui.s.px+(size_t)Y*ui.s.w;const unsigned char*a=m->a+(size_t)j*m->w;
        for(int i=0;i<m->w;i++){if(!a[i])continue;int X=m->x+dx+i;if(X<0||X>=ui.s.w)continue;row[X]=blend(row[X],color,a[i]*opacity/255);}}
}
typedef struct HdEdge {float x0,y0,x1,y1;} HdEdge;
static int hd_fcmp(const void*a,const void*b){float x=*(const float*)a,y=*(const float*)b;return (x>y)-(x<y);}
static double hd_cap(int style){static double cache[RR_HD_STYLES];if(cache[style])return cache[style];
    const int16_t*d=rr_hd_data+rr_hd_index[style]['H'-RR_HD_FIRST];int nc=*d++,top=0;
    for(int c=0;c<nc;c++){int np=*d++;for(int i=0;i<np;i++,d+=2)if(d[1]<top)top=d[1];}return cache[style]=top<0?-top:700;}
static double hd_width(int style,const wchar_t*s,int n,double cap,double spacing){
    double sc=cap/hd_cap(style),w=0;for(int i=0;i<n;i++){int c=s[i];if(c>=32&&c<=RR_HD_LAST)w+=rr_hd_advance[style][c-32]*sc;if(i+1<n)w+=spacing;}return w;
}
/* Rasterises one line (cap top at y) into a device-space coverage mask. */
static int hd_line_mask(int style,const wchar_t*s,int n,double x,double top,double cap,double spacing,int pad,Mask*m){
    double sc=cap*ui.k/hd_cap(style),pen=x*ui.k,base=(top+cap)*ui.k,x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;int ne=0,room=0;HdEdge*e=NULL;
    for(int i=0;i<n;i++){int c=s[i];
        if(c>=RR_HD_FIRST&&c<=RR_HD_LAST){const int16_t*d=rr_hd_data+rr_hd_index[style][c-RR_HD_FIRST];int nc=*d++;
            for(int k=0;k<nc;k++){int np=*d++;for(int j=0;j<np;j++){const int16_t*a=d+j*2,*b=j+1<np?a+2:d;
                if(ne==room){room=room?room*2:2048;HdEdge*g=realloc(e,sizeof*e*room);if(!g){free(e);return 0;}e=g;}
                HdEdge q={(float)(pen+a[0]*sc),(float)(base+a[1]*sc),(float)(pen+b[0]*sc),(float)(base+b[1]*sc)};e[ne++]=q;
                x0=fmin(x0,q.x0);x1=fmax(x1,q.x0);y0=fmin(y0,q.y0);y1=fmax(y1,q.y0);}
                d+=np*2;}}
        if(c>=32&&c<=RR_HD_LAST)pen+=rr_hd_advance[style][c-32]*sc;pen+=spacing*ui.k;}
    if(!ne){free(e);return 0;}
    m->x=(int)floor(x0)-pad;m->y=(int)floor(y0)-pad;m->w=(int)ceil(x1)-m->x+pad+1;m->h=(int)ceil(y1)-m->y+pad+1;
    float*cov=calloc((size_t)m->w*m->h,sizeof(float)),*xs=malloc(sizeof(float)*ne);m->a=calloc((size_t)m->w*m->h,1);
    if(!cov||!xs||!m->a){free(cov);free(xs);free(e);free(m->a);m->a=NULL;return 0;}
    for(int py=0;py<m->h;py++)for(int k=0;k<4;k++){float yc=m->y+py+(k+.5f)*.25f;int nx=0;float*row=cov+(size_t)py*m->w;
        for(int i=0;i<ne;i++)if((e[i].y0<=yc)!=(e[i].y1<=yc))xs[nx++]=e[i].x0+(yc-e[i].y0)/(e[i].y1-e[i].y0)*(e[i].x1-e[i].x0)-m->x;
        qsort(xs,nx,sizeof(float),hd_fcmp);
        for(int i=0;i+1<nx;i+=2){float a=fmaxf(0,xs[i]),b=fminf((float)m->w,xs[i+1]);if(b<=a)continue;int ia=(int)a,ib=(int)b;
            if(ia==ib){row[ia]+=.25f*(b-a);continue;}row[ia]+=.25f*(ia+1-a);for(int px=ia+1;px<ib;px++)row[px]+=.25f;if(ib<m->w)row[ib]+=.25f*(b-ib);}}
    for(size_t i=0;i<(size_t)m->w*m->h;i++){float v=cov[i];m->a[i]=(unsigned char)(v>=1?255:v<=0?0:v*255+.5f);}
    free(cov);free(xs);free(e);return 1;
}
#define GAME_CAP .64 /* cap height per unit of the layout's font sizes */
static void hd_line(const wchar_t*s,int len,double x,double top,double cap,double spacing,uint32_t color,int shadow){
    Mask m={0};if(!len||!hd_line_mask(HD_REGULAR,s,len,x,top,cap,spacing,0,&m))return;
    if(shadow){int d=P(fmax(1.0,cap*.09));if(d<1)d=1;mask_draw(&m,d,d,C_INK,215);}
    mask_draw(&m,0,0,color,255);free(m.a);
}
static void hd_text(const wchar_t*s0,double x,double y,double w,double h,double size,uint32_t color,UINT format,double spacing,int shadow){
    wchar_t s[700];int n=ascii_text(s0,s,700);if(!n||w<=0||h<=0)return;double cap=size*GAME_CAP;
    if(format&DT_SINGLELINE){
        int len=n;double tw=hd_width(HD_REGULAR,s,len,cap,spacing);
        if(tw>w&&(format&DT_END_ELLIPSIS)){double dots=hd_width(HD_REGULAR,L"...",3,cap,spacing)+spacing;while(len>0&&hd_width(HD_REGULAR,s,len,cap,spacing)+dots>w)len--;while(len>0&&s[len-1]==' ')len--;s[len]='.';s[len+1]='.';s[len+2]='.';len+=3;tw=hd_width(HD_REGULAR,s,len,cap,spacing);}
        double lx=format&DT_RIGHT?x+w-tw:format&DT_CENTER?x+(w-tw)/2:x,top=format&DT_VCENTER?y+(h-cap)/2:y;
        hd_line(s,len,lx,top,cap,spacing,color,shadow);return;
    }
    double line=cap*1.9,top=y;int start=0;
    while(start<n&&top+cap<=y+h+.5){
        int end=start,last_break=-1;while(end<n&&s[end]!='\n'){if(s[end]==' ')last_break=end;if(hd_width(HD_REGULAR,s+start,end-start+1,cap,spacing)>w&&last_break>start)break;end++;}
        int stop=end<n&&s[end]!='\n'&&last_break>start?last_break:end;
        double tw=hd_width(HD_REGULAR,s+start,stop-start,cap,spacing),lx=format&DT_RIGHT?x+w-tw:format&DT_CENTER?x+(w-tw)/2:x;
        hd_line(s+start,stop-start,lx,top,cap,spacing,color,shadow);
        start=stop<n?stop+1:stop;top+=line;
    }
}

/* ---- Text front end ----------------------------------------------------- */
/* Text is drawn in the HD font (our own). The setup screen's button and
 * note use a system font. */
static const wchar_t*ui_face=L"Bahnschrift";
static int CALLBACK font_found(const LOGFONTW*lf,const TEXTMETRICW*tm,DWORD type,LPARAM found){(void)lf;(void)tm;(void)type;*(int*)found=1;return 0;}
static void ui_pick_face(void){static int done;if(done)return;done=1;LOGFONTW lf={0};lf.lfCharSet=DEFAULT_CHARSET;wcscpy(lf.lfFaceName,L"Bahnschrift");int found=0;HDC dc=GetDC(NULL);EnumFontFamiliesExW(dc,&lf,font_found,(LPARAM)&found,0);ReleaseDC(NULL,dc);if(!found)ui_face=L"Segoe UI";}
static HFONT ui_font(double size,int weight,DWORD quality){int px=P(size);return CreateFontW(-(px>1?px:1),0,0,0,weight,0,0,0,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,quality,DEFAULT_PITCH|FF_SWISS,ui_face);}
static void system_text(const wchar_t*s,double x,double y,double w,double h,double size,int weight,uint32_t color,UINT format){
    if(!s||!*s||w<=0||h<=0)return;HFONT f=ui_font(size,weight,CLEARTYPE_QUALITY);HGDIOBJ old=SelectObject(ui.dc,f);SetBkMode(ui.dc,TRANSPARENT);SetTextColor(ui.dc,cref(color));
    RECT r={P(x),P(y),P(x+w),P(y+h)};DrawTextW(ui.dc,s,-1,&r,format|DT_NOPREFIX);SelectObject(ui.dc,old);DeleteObject(f);
}
static void text_ex(const wchar_t*s,double x,double y,double w,double h,double size,int weight,uint32_t color,UINT format,double spacing,int shadow){
    (void)weight;hd_text(s,x,y,w,h,size,color,format,spacing,shadow);
}
static void text(const wchar_t*s,double x,double y,double w,double h,double size,int weight,uint32_t color,UINT format){text_ex(s,x,y,w,h,size,weight,color,format,0,0);}
static void text_shadow(const wchar_t*s,double x,double y,double w,double h,double size,int weight,uint32_t color,UINT format){text_ex(s,x,y,w,h,size,weight,color,format,0,1);}
static void spaced(const wchar_t*s,double x,double y,double w,double h,double size,int weight,uint32_t color,UINT format,double spacing){text_ex(s,x,y,w,h,size,weight,color,format,spacing,0);}
static double text_width(const wchar_t*s,double size,int weight,double spacing){
    (void)weight;wchar_t a[700];int n=ascii_text(s,a,700);return hd_width(HD_REGULAR,a,n,size*GAME_CAP,spacing);
}
/* Page titles match the game's HD menu banners (native_ui.cpp
 * rr_banner_build): Roboto Flex Black, a blue-grey gradient at 82% over a
 * soft dark shadow. */
static void hd_title(const wchar_t*s0,double x,double y,double size){
    wchar_t s[128];int n=ascii_text(s0,s,128);double cap=size*GAME_CAP;
    int rad=P(cap*.06),dx=P(cap*.08),dy=P(cap*.11);if(rad<1)rad=1;Mask m={0};
    if(!hd_line_mask(HD_BLACK,s,n,x,y,cap,cap*.02,rad*3+dx+dy+2,&m))return;
    int w=m.w,h=m.h;float*sh=malloc(sizeof(float)*w*h*2),*tmp=sh?sh+(size_t)w*h:NULL;
    if(sh){
        // Soft shadow: the coverage box-blurred three times, offset down-right.
        for(size_t i=0;i<(size_t)w*h;i++)sh[i]=m.a[i]/255.f;
        for(int pass=0;pass<3;pass++){
            for(int j=0;j<h;j++){float acc=0;const float*a=sh+(size_t)j*w;float*t=tmp+(size_t)j*w;
                for(int i=-rad;i<w+rad;i++){if(i+rad<w)acc+=a[i+rad];if(i-rad-1>=0)acc-=a[i-rad-1];if(i>=0&&i<w)t[i]=acc/(2*rad+1);}}
            for(int i=0;i<w;i++){float acc=0;
                for(int j=-rad;j<h+rad;j++){if(j+rad<h)acc+=tmp[(size_t)(j+rad)*w+i];if(j-rad-1>=0)acc-=tmp[(size_t)(j-rad-1)*w+i];if(j>=0&&j<h)sh[(size_t)j*w+i]=acc/(2*rad+1);}}
        }
        GdiFlush();int top=P(y),capd=P(y+cap)-top;if(capd<1)capd=1;
        for(int j=0;j<h;j++){int Y=m.y+j;if(Y<0||Y>=ui.s.h)continue;uint32_t*row=ui.s.px+(size_t)Y*ui.s.w;uint32_t fillc=mix(RGBX(190,200,240),RGBX(120,130,190),(Y+.5-top)/capd);
            for(int i=0;i<w;i++){int X=m.x+i;if(X<0||X>=ui.s.w)continue;
                unsigned at=m.a[(size_t)j*w+i]*82/100,as=i>=dx&&j>=dy?(unsigned)(204*fminf(1,sh[(size_t)(j-dy)*w+i-dx])+.5f):0;
                if(!at&&!as)continue;uint32_t d=row[X];if(as)d=blend(d,RGBX(0,0,20),as);if(at)d=blend(d,fillc,at);row[X]=d;}}
    }
    free(sh);free(m.a);
}

/* ---- Backdrop, bars, logo ----------------------------------------------- */
static uint32_t*bg_px;static int bg_w,bg_h,bg_key=-1;
static void draw_background(void){
    int key=art_generation*2+installed;
    if(!bg_px||bg_w!=ui.s.w||bg_h!=ui.s.h||bg_key!=key){
        free(bg_px);bg_px=malloc((size_t)ui.s.w*ui.s.h*4);if(!bg_px)return;Surface saved=ui.s;ui.s.px=bg_px;bg_w=ui.s.w;bg_h=ui.s.h;bg_key=key;
        const RrArt*a=installed?rr_art_get(0):NULL;int w=ui.s.w,h=ui.s.h;
        if(a&&a->pixels){cover(a->pixels,(int)a->w,(int)a->h,0,0,ui.lw,ui.lh);px_fill(0,0,w,h,RGBX(0,2,10),112);
            // Legibility: darken behind the menu column, the header and the footer.
            px_grad(0,0,(int)(w*.62),h,C_INK,C_INK,150,0,0);px_grad(0,0,w,(int)(h*.24),C_INK,C_INK,120,0,1);px_grad(0,(int)(h*.78),w,h-(int)(h*.78),C_INK,C_INK,0,150,1);}
        else for(int y=0;y<h;y++){uint32_t*row=bg_px+(size_t)y*w;uint32_t base=mix(RGBX(10,24,70),RGBX(3,8,26),y/(double)h);
            for(int x=0;x<w;x++){double dx=(x-.5*w)/(.70*w),dy=(y-.36*h)/(.70*h),g=1-(dx*dx+dy*dy);uint32_t c=base;if(g>0)c=blend(c,RGBX(36,84,190),(unsigned)(g*g*110));
                unsigned n=(unsigned)((x*7+y*13)*2654435761u)>>29;int streak=((x+y*2)/3)%9==0;c=blend(c,streak?RGBX(60,96,170):RGBX(0,0,0),streak?10:n*3);row[x]=c;}}
        ui.s=saved;
    }
    GdiFlush();memcpy(ui.s.px,bg_px,(size_t)ui.s.w*ui.s.h*4);
}
/* The game's 164x18 BTNN/BTNH bars enlarged edge-preservingly: the lit outer
 * frame (row 0, column 0, row 14, column 159) and dark inner bevel (row 1,
 * column 1, row 13, column 158) stretch only along their length, so they stay
 * crisp lines; the textured fill is enlarged smoothly. Before validation the
 * launcher draws its own plain bar instead. Cached by size (bars are opaque
 * inside their rectangle). */
typedef struct BarCache {uint32_t*px;int w,h,active,key;} BarCache;
static BarCache bar_cache[4];static int bar_cache_next;
static void draw_bar(double x,double y,double w,double h,int active){
    int X=P(x),Y=P(y),W=P(x+w)-X,H=P(y+h)-Y,sh=P(4)>2?P(4):2;if(W<6||H<6)return;
    const RrArt*a=installed?rr_art_get(active?10:9):NULL;int game=a&&a->pixels&&a->w>=164&&a->h>=16;
    px_fill(X+sh,Y+sh,W,H,game?a->pixels[(size_t)15*a->w+160]&0xFFFFFF:C_INK,140);GdiFlush();
    int key=art_generation*2+installed,inside=X>=0&&Y>=0&&X+W<=ui.s.w&&Y+H<=ui.s.h;BarCache*hit=NULL;
    for(int i=0;i<4;i++)if(bar_cache[i].px&&bar_cache[i].w==W&&bar_cache[i].h==H&&bar_cache[i].active==active&&bar_cache[i].key==key)hit=bar_cache+i;
    if(hit&&inside){for(int j=0;j<H;j++)memcpy(ui.s.px+(size_t)(Y+j)*ui.s.w+X,hit->px+(size_t)j*W,(size_t)W*4);return;}
    if(game){
        const uint32_t*p=a->pixels;int s=(int)a->w,o=P(1.6)>1?P(1.6):1,i=P(1.2)>1?P(1.2):1,b=o+i;
        resample(X+b,Y+b,W-2*b,H-2*b,p,s,2,2,156,11,0,255);
        resample(X+o,Y+o,W-2*o,i,p,s,1,1,158,1,0,255);resample(X+o,Y+H-o-i,W-2*o,i,p,s,1,13,158,1,0,255);
        resample(X+o,Y+o,i,H-2*o,p,s,1,1,1,13,0,255);resample(X+W-o-i,Y+o,i,H-2*o,p,s,158,1,1,13,0,255);
        resample(X,Y,W,o,p,s,0,0,160,1,0,255);resample(X,Y+H-o,W,o,p,s,0,14,160,1,0,255);
        resample(X,Y,o,H,p,s,0,0,1,15,0,255);resample(X+W-o,Y,o,H,p,s,159,0,1,15,0,255);
    }else{
        int b=P(2)>1?P(2):1;
        if(active){px_grad(X,Y,W,H/2,RGBX(214,112,40),RGBX(176,72,16),255,255,1);px_grad(X,Y+H/2,W,H-H/2,RGBX(150,52,8),RGBX(110,34,6),255,255,1);}
        else px_grad(X,Y,W,H,RGBX(10,36,104),RGBX(3,18,62),255,255,1);
        uint32_t hi=active?RGBX(236,206,170):RGBX(150,180,230),lo=active?RGBX(150,90,50):RGBX(60,96,170);
        px_grad(X,Y,W,b,hi,lo,255,255,0);px_grad(X,Y,b,H,hi,lo,255,255,1);px_fill(X,Y+H-b,W,b,lo,255);px_fill(X+W-b,Y,b,H,lo,255);
    }
    if(inside){BarCache*c=bar_cache+bar_cache_next;bar_cache_next=(bar_cache_next+1)%4;free(c->px);c->px=malloc((size_t)W*H*4);
        if(c->px){c->w=W;c->h=H;c->active=active;c->key=key;for(int j=0;j<H;j++)memcpy(c->px+(size_t)j*W,ui.s.px+(size_t)(Y+j)*ui.s.w+X,(size_t)W*4);}}
}
/* The independent project logo, with a box-filtered mip chain so it stays
 * clean when drawn far below its source size. */
static uint32_t*logo_mip[12];static int logo_mw[12],logo_mh[12],logo_levels,logo_l,logo_t,logo_r,logo_b;
static void logo_prepare(void){
    if(logo_levels||!project_logo.pixels)return;logo_mip[0]=project_logo.pixels;logo_mw[0]=(int)project_logo.w;logo_mh[0]=(int)project_logo.h;logo_levels=1;
    logo_l=logo_mw[0];logo_t=logo_mh[0];logo_r=-1;logo_b=-1;
    for(int y=0;y<logo_mh[0];y++)for(int x=0;x<logo_mw[0];x++)if(logo_mip[0][(size_t)y*logo_mw[0]+x]>>24>24){if(x<logo_l)logo_l=x;if(x>logo_r)logo_r=x;if(y<logo_t)logo_t=y;if(y>logo_b)logo_b=y;}
    while(logo_levels<12&&logo_mw[logo_levels-1]>=64&&logo_mh[logo_levels-1]>=64){
        int pw=logo_mw[logo_levels-1],w=pw/2,h=logo_mh[logo_levels-1]/2;const uint32_t*p=logo_mip[logo_levels-1];uint32_t*q=malloc((size_t)w*h*4);if(!q)break;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){const uint32_t*s=p+(size_t)y*2*pw+x*2;uint32_t v=0;for(int c=0;c<32;c+=8)v|=(((s[0]>>c&255)+(s[1]>>c&255)+(s[pw]>>c&255)+(s[pw+1]>>c&255)+2)/4)<<c;q[(size_t)y*w+x]=v;}
        logo_mip[logo_levels]=q;logo_mw[logo_levels]=w;logo_mh[logo_levels]=h;logo_levels++;
    }
}
static void draw_logo(double x,double y,double w,double h){
    logo_prepare();if(!logo_levels||logo_r<logo_l||logo_b<logo_t)return;
    double cw=logo_r-logo_l+1,ch=logo_b-logo_t+1,k=fmin((P(x+w)-P(x))/cw,(P(y+h)-P(y))/ch);
    int dw=(int)(cw*k),dh=(int)(ch*k),L=0;while(L+1<logo_levels&&(cw/(1<<(L+1)))>=dw)L++;
    int s=1<<L,sx=logo_l/s,sy=logo_t/s,sw=(int)(cw/s),sh=(int)(ch/s);if(sx+sw>logo_mw[L])sw=logo_mw[L]-sx;if(sy+sh>logo_mh[L])sh=logo_mh[L]-sy;
    resample(P(x)+(P(x+w)-P(x)-dw)/2,P(y)+(P(y+h)-P(y)-dh)/2,dw,dh,logo_mip[L],logo_mw[L],sx,sy,sw,sh,1,255);
}

/* ---- Layout ------------------------------------------------------------- */
#define LIST_W 500.0
enum {HIT_NONE,HIT_ROW,HIT_BACK,HIT_REMOVE,HIT_PREV,HIT_NEXT,HIT_FIND};
/* The selected value row's arrow buttons, recorded while drawing. */
static int arrow_row=-1;static double arrow_y,arrow_h,arrow_lx,arrow_rx;
static double list_top(void){return 150;}
static double list_pitch(void){return page==0?66:58;}
static double list_height(void){return page==0?56:50;}
static int list_visible(void){return page==0?7:8;}
static void list_scroll(void){int v=list_visible(),n=row_count();if(selected<list_first)list_first=selected;if(selected>=list_first+v)list_first=selected-v+1;if(list_first>n-v)list_first=n-v;if(list_first<0)list_first=0;}
static double footer_y(void){return ui.lh-58;}
static void back_rect(double*r){r[0]=ui.cx+1224-136;r[1]=footer_y()+11;r[2]=136;r[3]=36;}
static void find_rect(double*r){r[0]=ui.cx+640-230;r[1]=ui.by+458;r[2]=460;r[3]=66;}
static int ui_hit(int px,int py,int*row){
    double x=px/ui.k,y=py/ui.k,b[4];*row=-1;
    if(!installed){find_rect(b);return x>=b[0]&&x<b[0]+b[2]&&y>=b[1]&&y<b[1]+b[3]?HIT_FIND:HIT_NONE;}
    back_rect(b);if(page&&x>=b[0]&&x<b[0]+b[2]&&y>=b[1]&&y<b[1]+b[3])return HIT_BACK;
    if(arrow_row>=0&&arrow_row==selected&&y>=arrow_y&&y<arrow_y+arrow_h){
        if(x>=arrow_lx&&x<arrow_lx+28){*row=arrow_row;return HIT_PREV;}
        if(x>=arrow_rx&&x<arrow_rx+28){*row=arrow_row;return HIT_NEXT;}
    }
    int n=row_count(),v=list_visible();double lx=ui.cx+56;
    for(int i=list_first;i<n&&i<list_first+v;i++){double ry=ui.by+list_top()+(i-list_first)*list_pitch();
        if(y<ry||y>=ry+list_height())continue;
        if(x>=lx&&x<lx+LIST_W){*row=i;return HIT_ROW;}
        if(x>=lx+LIST_W+12&&x<lx+LIST_W+50&&view_is_mod(i)){*row=i;return HIT_REMOVE;}
    }
    return HIT_NONE;
}

/* ---- Page parts --------------------------------------------------------- */
static int toast_visible(void){return busy||(status_tick&&GetTickCount()-status_tick<12000);}
static double chip(double right,double y,const wchar_t*s,uint32_t border,uint32_t color){
    double w=text_width(s,13.5,FW_SEMIBOLD,1.2)+26,x=right-w;fill(x,y,w,26,C_PANEL,200);outline(x,y,w,26,border,230);
    spaced(s,x,y,w,26,13.5,FW_SEMIBOLD,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE,1.2);return x-10;
}
static void draw_header(void){
    double x=ui.cx+56,y=ui.hy;wchar_t trail[160]=L"ROAD RASH: JAILBREAK";int chain[16],n=0,p=page;
    while(p&&n<16){p=parent_of(p);chain[n++]=p;}
    for(int i=n-1;i>=0;i--){wcscat(trail,L"   >   ");wcscat(trail,page_title(chain[i]));}
    spaced(trail,x+2,y+28,900,22,14,FW_SEMIBOLD,C_STEEL,DT_LEFT|DT_VCENTER|DT_SINGLELINE,1.6);
    hd_title(page_title(page),x,y+58,58);
    double right=ui.cx+1224;
    /* The preset in use, on the pages where it matters (everything plays enhanced). */
    if(page==0||page==2||page==7){wchar_t p[40];swprintf(p,40,L"%ls",preset_names[current_preset()]);_wcsupr(p);right=chip(right,y+28,p,C_GOLD,C_GOLD);}
    if(mod_count&&(page==0||page==3)){wchar_t m[48];swprintf(m,48,L"MODS %d/%d ON",mods_on(),mod_count);right=chip(right,y+28,m,C_STEEL,C_WHITE);}
    if(enhanced_mode&&(page==0||page==CHEAT_PAGE)&&cp_active())right=chip(right,y+28,L"CHEATS ACTIVE",C_GOLD,C_GOLD);
    if(IS_DEV(page)&&dev_dirty())right=chip(right,y+28,L"UNSAVED CHANGES",C_GOLD,C_GOLD);
    DWORD code;if(game_process&&GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE)chip(right,y+28,L"GAME RUNNING",C_GOLD,C_WHITE);
}
/* Slider rows: steps, current step and recommended maximum. */
static int dev_slider(int,int*,int*,int*);
static int row_slider(int r,int*steps,int*pos,int*safe){if(IS_DEV(page))return dev_slider(r,steps,pos,safe);*steps=det_steps();*pos=model_detail;*safe=det_safe();return 1;}
static void draw_list(void){
    list_scroll();arrow_row=-1;int n=row_count(),v=list_visible();double x=ui.cx+56,size=page==0?30:25;
    for(int i=list_first;i<n&&i<list_first+v;i++){
        wchar_t label[160],value[160];int kind=launcher_row(i,label,value,160),active=i==selected;
        double y=ui.by+list_top()+(i-list_first)*list_pitch(),h=list_height(),right=x+LIST_W-22,vw=0;
        draw_bar(x,y,LIST_W,h,active);
        uint32_t lc=active?C_WHITE:C_GOLD,vc=active?C_WHITE:C_STEEL;
        if(row_warn)vc=RGBX(226,64,36);   /* heavy step (gfx_options.h): the slider's warning red */
        if(page==ONLINE_PAGE&&!online_ready&&kind==ROW_INFO){lc=vc=active?C_STEEL:C_DIM;}   /* greyed out: no netplay in this build */
        if(kind==ROW_MENU){text_shadow(L">",right-18,y,18,h,size,FW_SEMIBOLD,active?C_WHITE:C_STEEL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);right-=30;}
        if(row_is_value(kind))right-=14;
        if(value[0]){double vs=size*.84;vw=fmin(text_width(value,vs,FW_SEMIBOLD,0)+2,LIST_W*.52);
            /* Slider: one segment per step left of the value; lit up to the
               current step, gold up to the recommended maximum, red beyond. */
            double gw=0;int steps,pos,safe;if(kind==ROW_SLIDER&&row_slider(i,&steps,&pos,&safe)){const double sw=steps>12?6:9,gap=3;gw=steps*(sw+gap);
                double gx=right-vw-12-gw,gy=y+h/2-7;uint32_t warn=RGBX(226,64,36);
                if(pos>safe)vc=warn;
                for(int s=0;s<steps;s++){uint32_t c=s>safe?warn:C_GOLD;double sx=gx+s*(sw+gap);
                    if(s<=pos)fill(sx,gy,sw,14,c,active?255:200);else outline(sx,gy,sw,14,c,active?200:110);}
                vw+=gw+12;}
            if(row_is_value(kind)&&active){arrow_row=i;arrow_y=y;arrow_h=h;arrow_lx=right-vw-30;arrow_rx=right+4;
                text_shadow(L"<",right-vw-26,y,16,h,vs,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE);text_shadow(L">",right+10,y,16,h,vs,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}
            text_shadow(value,right-vw+gw+12*(gw>0),y,vw-gw-12*(gw>0),h,vs,FW_SEMIBOLD,vc,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);vw+=row_is_value(kind)&&active?40:18;}
        text_shadow(label,x+24,y,right-vw-(x+24),h,size,FW_SEMIBOLD,lc,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(view_is_mod(i)){double bx=x+LIST_W+14,by=y+(h-34)/2;int hot=(hover_hit==HIT_REMOVE&&hover_row==i);
            fill(bx+2,by+2,34,34,C_INK,120);fill(bx,by,34,34,hot?RGBX(170,58,12):active?RGBX(26,44,96):C_PANEL,hot?255:220);outline(bx,by,34,34,hot||active?C_WHITE:C_STEEL,active||hot?220:120);
            text(L"X",bx,by,34,34,22,FW_NORMAL,hot||active?C_WHITE:C_STEEL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    }
    if(page==3&&!mod_count){double y=ui.by+list_top()+3*list_pitch()+8;
        text(L"No mods installed yet",x+4,y,LIST_W,26,21,FW_SEMIBOLD,C_STEEL,DT_LEFT|DT_TOP|DT_SINGLELINE);
        text(L"Install an .rrmod pack to get started. Packs work with the USA game; MODDING.md shows how to make one.",x+4,y+32,LIST_W-10,60,17,FW_NORMAL,C_DIM,DT_LEFT|DT_TOP|DT_WORDBREAK);}
    if(n>v){double tx=x+LIST_W+16,ty=ui.by+list_top(),th=(v-1)*list_pitch()+list_height();fill(tx,ty,4,th,C_STEEL,60);
        double thumb=th*v/n,pos=(th-thumb)*list_first/(n-v);fill(tx-1,ty+pos,6,thumb,C_GOLD,230);}
}
static void draw_card(void){
    double x=ui.cx+604,y=ui.by+150,w=620,h=456;
    fill(x+5,y+5,w,h,C_INK,110);fill(x,y,w,h,C_PANEL,200);outline(x,y,w,h,C_STEEL,90);fill(x,y,w,3,C_GOLD,255);
    if(page==29){lb_draw_chat(x,y,w,h);return;}   /* LB_ROOM_PAGE: the room chat instead of art and help */
    double ax=x+16,ay=y+19,aw=w-32,ah=aw*112/256;
    const RrArt*a=rr_art_get(preview_art());fill(ax,ay,aw,ah,RGBX(1,6,24),255);
    if(a&&a->pixels)cover(a->pixels,(int)a->w,(int)a->h,ax,ay,aw,ah);
    grad(ax,ay+ah*.7,aw,ah*.3,C_INK,C_INK,0,90,1);outline(ax,ay,aw,ah,C_STEEL,200);
    if(page>=20&&page!=ORIGINAL_PAGE)sb_draw_art(ax,ay,aw,ah);
    wchar_t label[160],value[160];launcher_row(selected,label,value,160);double ty=ay+ah+18;
    text(label,ax+2,ty,aw-4,32,25,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    fill(ax+2,ty+38,64,2,C_GOLD,200);
    text(menu_help(),ax+2,ty+52,aw-4,y+h-14-(ty+52),18.5,FW_NORMAL,C_WHITE,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_EDITCONTROL);
}
/* PlayStation face symbols, our own outlines (anti-aliased, 4x4 samples per
 * device pixel): 0 Cross, 1 Circle, 2 Square, 3 Triangle, centred on cx,cy. */
static void ps_symbol(double cx,double cy,double size,int which,uint32_t c){
    GdiFlush();const double k=ui.k,X=cx*k,Y=cy*k,s=size*k*.5,w=fmax(1.2,size*k*.13);
    const int x0=(int)floor(X-s-w),x1=(int)ceil(X+s+w),y0=(int)floor(Y-s-w),y1=(int)ceil(Y+s+w);
    for(int py=y0;py<y1;py++){if(py<0||py>=ui.s.h)continue;for(int px=x0;px<x1;px++){if(px<0||px>=ui.s.w)continue;int in=0;
        for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++){const double qx=px+(sx+.5)/4-X,qy=py+(sy+.5)/4-Y;double d;
            if(which==0){const double a=fabs(qx+qy)/sqrt(2.0),b=fabs(qx-qy)/sqrt(2.0),along=fmax(fabs(qx),fabs(qy));d=along>s*.82?1e9:fmin(a,b);}
            else if(which==1)d=fabs(sqrt(qx*qx+qy*qy)-s*.82);
            else if(which==2)d=fabs(fmax(fabs(qx),fabs(qy))-s*.72);
            else{/* triangle (apex up): its outline, the edges 0.43 s from the centre */
                const double yy=qy-s*.12,r=s*.86,e1=yy-r*.5,e2=(sqrt(3.0)*qx-yy)*.5-r*.5,e3=(-sqrt(3.0)*qx-yy)*.5-r*.5;d=fabs(fmax(e1,fmax(e2,e3)));}
            if(d<=w*.5)in++;}
        if(in){uint32_t*p=ui.s.px+(size_t)py*ui.s.w+px;*p=blend(*p,c,255*in/16);}}}
}
/* Keycaps: arrow keys and PlayStation buttons (PS:CROSS ...) are drawn as
 * shapes, other keys as text. */
static double keycap(double x,double y,const wchar_t*key,const wchar_t*what){
    static const wchar_t*const ps[]={L"PS:CROSS",L"PS:CIRCLE",L"PS:SQUARE",L"PS:TRIANGLE"};static const uint32_t ps_colour[]={0x86B8EE,0xF26C6C,0xE995CF,0x45D2AE};
    int symbol=-1;for(int i=0;i<4;i++)if(!wcscmp(key,ps[i]))symbol=i;
    int arrows=!wcscmp(key,L"UPDOWN")||!wcscmp(key,L"LEFTRIGHT");double kw=arrows?44:symbol>=0?30:fmax(30,text_width(key,13,FW_SEMIBOLD,0)+18);
    fill(x,y,kw,24,RGBX(16,32,72),230);outline(x,y,kw,24,C_STEEL,150);
    if(symbol>=0)ps_symbol(x+kw/2,y+12,14,symbol,ps_colour[symbol]);
    else if(!wcscmp(key,L"UPDOWN")){triangle(x+11,y+16,x+19,y+16,x+15,y+8,C_WHITE,255);triangle(x+25,y+8,x+33,y+8,x+29,y+16,C_WHITE,255);}
    else if(!wcscmp(key,L"LEFTRIGHT")){triangle(x+17,y+8,x+17,y+16,x+10,y+12,C_WHITE,255);triangle(x+27,y+8,x+27,y+16,x+34,y+12,C_WHITE,255);}
    else text(key,x,y,kw,24,13,FW_SEMIBOLD,C_WHITE,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    double tw=text_width(what,15,FW_NORMAL,0)+2;text(what,x+kw+8,y,tw,24,15,FW_NORMAL,C_STEEL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);return x+kw+8+tw+22;
}
static void draw_footer(void){
    double fy=footer_y(),x=ui.cx+56;
    fill(0,fy,ui.lw,ui.lh-fy,RGBX(1,3,12),170);fill(0,fy,ui.lw,1,C_STEEL,70);
    wchar_t label[160],value[160];int kind=row_count()?launcher_row(selected,label,value,160):ROW_INFO;double ky=fy+17;
    if(binding>=0){x=keycap(x,ky,L"Any key",L"Bind");keycap(x,ky,L"Esc",L"Cancel");}
    else{x=keycap(x,ky,L"UPDOWN",L"Move");if(row_is_value(kind))x=keycap(x,ky,L"LEFTRIGHT",L"Change");if(kind!=ROW_INFO)x=keycap(x,ky,sb_key(L"Enter",L"A",L"PS:CROSS"),L"Select");
        if(page)x=keycap(x,ky,sb_key(L"Esc",L"B",L"PS:CIRCLE"),L"Back");if(view_is_mod(selected))keycap(x,ky,sb_key(L"Del",L"X",L"PS:SQUARE"),L"Remove");
        if(page==21||page==22)keycap(x,ky,sb_key(L"Del",L"X",L"PS:SQUARE"),L"Delete");}
    if(page){double b[4];back_rect(b);int hot=hover_hit==HIT_BACK;draw_bar(b[0],b[1],b[2],b[3],hot);
        spaced(L"< BACK",b[0],b[1],b[2],b[3],16,FW_SEMIBOLD,hot?C_WHITE:C_GOLD,DT_CENTER|DT_VCENTER|DT_SINGLELINE,1.5);}
}
static void draw_toast(void){
    toast_painted=toast_visible();if(!toast_painted)return;
    double x=ui.cx+56,w=1168,h=36,y=footer_y()-h-12;
    fill(x+3,y+3,w,h,C_INK,120);fill(x,y,w,h,C_PANEL,235);outline(x,y,w,h,C_STEEL,80);fill(x,y,4,h,C_GOLD,255);
    text(status_text,x+20,y,w-40,h,17,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(busy){double by=y+h-3,bw=w-4;fill(x+4,by,bw-4,3,C_STEEL,50);
        if(busy==1)fill(x+4,by,(bw-4)*progress/100.0,3,C_GOLD,255);
        else{double phase=(GetTickCount()%1600)/1600.0,seg=bw*.22,sx=x+4+(bw+seg)*phase-seg,ex=sx+seg;if(sx<x+4)sx=x+4;if(ex>x+bw)ex=x+bw;if(ex>sx)fill(sx,by,ex-sx,3,C_GOLD,255);}}
}
/* Before a validated game exists: our logo and one way forward. */
static void draw_setup(void){
    draw_logo(ui.cx+290,ui.by+96,700,320);
    double b[4];find_rect(b);draw_bar(b[0],b[1],b[2],b[3],!busy);
    system_text(busy==1?L"Installing your game\u2026":L"Find your game (.bin)",b[0],b[1],b[2],b[3],25,FW_SEMIBOLD,busy?C_DIM:C_WHITE,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    system_text(L"Select your Road Rash: Jailbreak (USA) disc image: the .bin, or its .cue / .iso.\nIt is checked first, then installed on this PC. No game files are used before that.",ui.cx+190,b[1]+b[3]+20,900,60,17,FW_NORMAL,C_STEEL,DT_CENTER|DT_TOP|DT_WORDBREAK);
}
static HBITMAP render(HDC compatible,void**pixels){
    RECT client={0,0,render_width,render_height};if(mainwindow){GetClientRect(mainwindow,&client);render_width=client.right;render_height=client.bottom;}
    int w=client.right,h=client.bottom;if(w<1||h<1||w>7680||h>4320)return NULL;
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    HBITMAP bitmap=CreateDIBSection(compatible,&bi,DIB_RGB_COLORS,pixels,NULL,0);if(!bitmap)return NULL;
    HDC dc=CreateCompatibleDC(compatible);if(!dc){DeleteObject(bitmap);return NULL;}HGDIOBJ old=SelectObject(dc,bitmap);
    ui_pick_face();ui.dc=dc;ui.s.px=*pixels;ui.s.w=w;ui.s.h=h;ui_layout(w,h);if(selected>=row_count())selected=row_count()?row_count()-1:0;
    draw_background();
    if(!installed){draw_setup();draw_toast();}
    else{draw_header();draw_list();draw_card();draw_toast();draw_footer();}
    GdiFlush();SelectObject(dc,old);DeleteDC(dc);ui.dc=NULL;return bitmap;
}
#include "save_browser.h"
