/* tuning.ini: the owner's adjustable launcher menus (items, order, page,
 * labels, help, page titles) and the level-of-detail steps, as one
 * human-readable file next to launcher.ini. LGPL-2.1-or-later. Original
 * author code. C and C++.
 *
 * The values compiled in below are the built-in defaults (current behaviour).
 * A missing file means "all defaults"; a line that cannot be used keeps its
 * default and is listed in tuning-problems.txt, so a broken file never stops
 * the launcher or the game. The Developer page of the launcher edits a draft
 * and writes the file only on Save, adding plain-English lines to
 * tuning-changes.log.
 *
 * File format: [section] and  name = value  lines; whole-line comments start
 * with ; or #. In values \n is a line break. Names ignore case. */
#ifndef RR_TUNING_H
#define RR_TUNING_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "model_detail.h"
#define TN_FN static inline

/* ---- Text layer: forgiving reader, ordered writer ------------------------ */
#define TNI_SEC 48
#define TNI_KEY 64
#define TNI_VAL 1200
typedef struct TniEntry {char sec[TNI_SEC],key[TNI_KEY];char*val;int line;int used;} TniEntry;
typedef struct TniFile {TniEntry*e;int n,cap;} TniFile;
typedef struct TniLog {char text[6000];int count;size_t len;} TniLog;
TN_FN void tni_log(TniLog*log,const char*fmt,...){
    if(!log)return;log->count++;if(log->len>=sizeof log->text-200)return;
    va_list ap;va_start(ap,fmt);int n=vsnprintf(log->text+log->len,sizeof log->text-log->len-3,fmt,ap);va_end(ap);
    if(n<0)n=0;log->len+=(size_t)n;if(log->len>sizeof log->text-3)log->len=sizeof log->text-3;
    log->text[log->len++]='\r';log->text[log->len++]='\n';log->text[log->len]=0;
}
TN_FN void tni_name(char*out,size_t cap,const char*s,size_t n){
    size_t o=0;int space=0;
    for(size_t i=0;i<n&&o+1<cap;i++){unsigned char c=(unsigned char)s[i];
        if(c==' '||c=='\t'){space=o>0;continue;}
        if(space){out[o++]=' ';space=0;if(o+1>=cap)break;}
        out[o++]=(char)tolower(c);}
    out[o]=0;
}
TN_FN void tni_free(TniFile*f){for(int i=0;i<f->n;i++)free(f->e[i].val);free(f->e);f->e=NULL;f->n=f->cap=0;}
TN_FN int tni_put(TniFile*f,const char*sec,const char*key,const char*val,int line){
    size_t m=strlen(val)+1;char*v=(char*)malloc(m);if(!v)return 0;memcpy(v,val,m);
    for(int i=0;i<f->n;i++)if(!strcmp(f->e[i].sec,sec)&&!strcmp(f->e[i].key,key)){free(f->e[i].val);f->e[i].val=v;f->e[i].line=line;return 1;}
    if(f->n==f->cap){int cap=f->cap?f->cap*2:256;TniEntry*e=(TniEntry*)realloc(f->e,sizeof(TniEntry)*(size_t)cap);if(!e){free(v);return 0;}f->e=e;f->cap=cap;}
    TniEntry*e=f->e+f->n++;memset(e,0,sizeof *e);strncpy(e->sec,sec,TNI_SEC-1);strncpy(e->key,key,TNI_KEY-1);e->val=v;e->line=line;return 1;
}
TN_FN int tni_parse(TniFile*f,const char*text,size_t len,TniLog*log){
    tni_free(f);char sec[TNI_SEC]="";size_t p=0;int line=0;
    if(len>=2&&(((unsigned char)text[0]==0xFF&&(unsigned char)text[1]==0xFE)||((unsigned char)text[0]==0xFE&&(unsigned char)text[1]==0xFF))){tni_log(log,"The file is saved as UTF-16 (Unicode). Save it as UTF-8 instead.");return 0;}
    if(len>=3&&(unsigned char)text[0]==0xEF&&(unsigned char)text[1]==0xBB&&(unsigned char)text[2]==0xBF)p=3;
    char*val=(char*)malloc(TNI_VAL);if(!val)return 0;
    while(p<len){
        size_t s=p;while(p<len&&text[p]!='\n')p++;size_t e=p;if(p<len)p++;line++;
        while(s<e&&(text[s]==' '||text[s]=='\t'))s++;while(e>s&&(text[e-1]=='\r'||text[e-1]==' '||text[e-1]=='\t'))e--;
        if(s==e||text[s]==';'||text[s]=='#')continue;
        if(text[s]=='['){size_t c=s+1;while(c<e&&text[c]!=']')c++;if(c==e||c==s+1){tni_log(log,"line %d: a section name needs [ and ]",line);sec[0]=0;continue;}tni_name(sec,sizeof sec,text+s+1,c-s-1);continue;}
        size_t q=s;while(q<e&&text[q]!='=')q++;
        if(q==e){tni_log(log,"line %d: expected  name = value",line);continue;}
        if(!sec[0]){tni_log(log,"line %d: a setting before the first [section] is ignored",line);continue;}
        char key[TNI_KEY];size_t ke=q;while(ke>s&&(text[ke-1]==' '||text[ke-1]=='\t'))ke--;tni_name(key,sizeof key,text+s,ke-s);
        if(!key[0]){tni_log(log,"line %d: the setting has no name",line);continue;}
        size_t v=q+1;while(v<e&&(text[v]==' '||text[v]=='\t'))v++;size_t ve=e;
        if(ve-v>=2&&text[v]=='"'&&text[ve-1]=='"'){v++;ve--;}
        size_t o=0;int cut=0;
        for(size_t i=v;i<ve;i++){char c=text[i];if(c=='\\'&&i+1<ve){char d=text[i+1];if(d=='n'){c='\n';i++;}else if(d=='\\'){c='\\';i++;}}if(o+1>=TNI_VAL){cut=1;break;}val[o++]=c;}
        val[o]=0;if(cut)tni_log(log,"line %d: [%s] %s is too long and was cut",line,sec,key);
        if(!tni_put(f,sec,key,val,line))break;
    }
    free(val);return 1;
}
TN_FN TniEntry*tni_find(TniFile*f,const char*sec,const char*key){
    char s[TNI_SEC],k[TNI_KEY];tni_name(s,sizeof s,sec,strlen(sec));tni_name(k,sizeof k,key,strlen(key));
    for(int i=0;i<f->n;i++)if(!strcmp(f->e[i].sec,s)&&!strcmp(f->e[i].key,k)){f->e[i].used=1;return f->e+i;}
    return NULL;
}
typedef struct TniOut {char*b;size_t n,cap;int failed;} TniOut;
TN_FN void tno_raw(TniOut*o,const char*s,size_t n){
    if(o->failed)return;if(o->n+n+1>o->cap){size_t cap=o->cap?o->cap*2:65536;while(cap<o->n+n+1)cap*=2;char*b=(char*)realloc(o->b,cap);if(!b){o->failed=1;return;}o->b=b;o->cap=cap;}
    memcpy(o->b+o->n,s,n);o->n+=n;o->b[o->n]=0;
}
TN_FN void tno_str(TniOut*o,const char*s){tno_raw(o,s,strlen(s));}
TN_FN void tno_free(TniOut*o){free(o->b);o->b=NULL;o->n=o->cap=0;}
TN_FN void tno_printf(TniOut*o,const char*fmt,...){char t[2048];va_list ap;va_start(ap,fmt);int n=vsnprintf(t,sizeof t,fmt,ap);va_end(ap);if(n<0)return;if(n>(int)sizeof t-1)n=(int)sizeof t-1;tno_raw(o,t,(size_t)n);}
TN_FN void tno_kv(TniOut*o,const char*key,const char*val){
    tno_printf(o,"%s = ",key);
    for(const char*p=val?val:"";*p;p++){if(*p=='\n')tno_raw(o,"\\n",2);else if(*p=='\\')tno_raw(o,"\\\\",2);else if(*p!='\r')tno_raw(o,p,1);}
    tno_raw(o,"\r\n",2);
}
TN_FN void tno_comment(TniOut*o,const char*text){
    const char*p=text;while(*p){const char*e=strchr(p,'\n');size_t n=e?(size_t)(e-p):strlen(p);tno_raw(o,"; ",2);tno_raw(o,p,n);tno_raw(o,"\r\n",2);if(!e)break;p=e+1;}
}

/* ---- Launcher menus: built-in defaults ----------------------------------- */
/* Pages by their number in launcher_skin.h; items by their native (page, row).
 * opens: the page an item opens (-1 none). */
typedef struct TnPage {const char*id;int num;const char*title;const char*name;} TnPage;
static const TnPage tn_pages[]={
 {"main",0,"MAIN MENU","Main menu"},{"options",7,"OPTIONS","Options"},{"display",2,"DISPLAY","Display"},
 {"user_settings",8,"USER SETTINGS","User settings"},{"rider_names",6,"RIDER NAMES","Rider names"},{"taunt_voices",14,"TAUNT VOICES","Taunt voices"},{"rider_look",15,"RIDER LOOK","Rider look"},
 {"achievements",10,"ACHIEVEMENTS","Achievements"},{"audio",13,"AUDIO","Audio"},{"system",11,"SYSTEM","System"},
 {"controls",1,"CONTROLS","Controls"},{"mods",3,"MODS","Mods"},{"installation",4,"INSTALLATION","Game installation"},
 {"cheats",19,"CHEATS","Cheats"},{"setup",16,"CHOOSE YOUR SETUP","First-run setup"},
 {"multiplayer",17,"MULTIPLAYER","Multiplayer"},{"online",18,"ONLINE","Online"},{"original",23,"ORIGINAL GAME","Original game"},
 {"display_screen",24,"SCREEN","Display: Screen"},{"display_picture",25,"PICTURE","Display: Picture"},{"display_detail",26,"DETAIL","Display: Detail"},{"display_effects",27,"EFFECTS","Display: Effects"}};
#define TN_PAGES ((int)(sizeof tn_pages/sizeof tn_pages[0]))
/* TN_FIRST: an item a tuning.ini written before it existed does not list goes
 * to the top of its page, not the bottom. */
enum {TN_GROUP=1,TN_FIXED=2,TN_FIRST=4};
typedef struct TnItem {const char*id;int page,row,opens;unsigned flags;const char*label;const char*help;} TnItem;
static const TnItem tn_items[]={
 {"play",0,0,-1,0,"Play","Start Road Rash: Jailbreak with your display settings, music and enabled mods.\nPlay original game, below, starts the unmodified game."},
 {"play_original",0,9,23,0,"Play original game","The unmodified game: no PC menu, mods, cheats or achievements. Same memory cards.\nA few opt-in options (resolution, window, frame rate ...), all off to start."},
 {"saves",0,1,20,0,"Saves","Your memory-card saves and quick resume slots: play, copy, export, import and delete.\nCards are backed up before every launch."},
 {"multiplayer",0,8,17,0,"Multiplayer","Two players on one PC are in the game's own Multiplayer menu.\nOnline: two PCs, one player each."},
 {"mods",0,2,3,0,"Mods","Install .rrmod packs (artwork, textures, sounds, models) and switch them on or off.\nMods apply in Enhanced; Original always stays stock."},
 {"cheats",0,7,19,0,"Cheats","GameShark codes and the game's own unlocks, for Enhanced. All off by default.\nAchievements stay locked while any cheat is on."},
 {"options",0,3,7,0,"Options","Display, rider, audio and system settings, including controls."},
 {"installation",0,4,4,0,"Game installation","Repair, verify and prepare your own copy of the game.\nNo disc is needed after installation."},
 {"quit",0,5,-1,0,"Quit","Close the launcher.\nYour settings are already saved."},
 {"player1",1,0,-1,0,"Player 1","Choose the device you play with. A controller plays the game's analog setup.\nApplies on your next launch."},
 {"player2",1,1,-1,0,"Player 2","Player 2 uses your controller (analog) while you keep the keyboard. In game, Game options > Controller > 2 sets it up. Applies on your next launch."},
 {"player3",1,2,-1,0,"Player 3","A controller on the game's multitap, for Side Car (Versus) with up to 4 players; all controllers then play digital. Pads go to players in the order they were plugged in, Player 1 first. Next launch."},
 {"player4",1,3,-1,0,"Player 4","A controller on the game's multitap, for Side Car (Versus) with up to 4 players; all controllers then play digital. Pads go to players in the order they were plugged in, Player 1 first. Next launch."},
 {"bindings_for",1,4,-1,0,"Edit bindings for","Choose whether to edit keyboard keys or controller buttons.\nIn game, Game options > Controller shows every action with your keys."},
 {"key_accelerate",1,5,-1,0,"Accelerate","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_brake",1,6,-1,0,"Brake","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_left",1,7,-1,0,"Steer left","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_right",1,8,-1,0,"Steer right","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_attack",1,9,-1,0,"Primary attack","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_attack2",1,10,-1,0,"Alternate attack","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_kick",1,11,-1,0,"Kick","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_pause",1,12,-1,0,"Pause","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"key_camera",1,13,-1,0,"Change camera","Select, then press the new key. Names follow the game's Digital 1 setup; in game, Game options > Controller shows every action. Next launch."},
 {"preset",2,15,-1,TN_FIRST,"Preset","Recommended, Classic look or High end set the picture and performance options together.\nChanging any one of them shows Custom."},
 {"display_screen",2,21,24,0,"Screen","Render resolution, window, aspect ratio, frame rate, VSync and input delay."},
 {"display_picture",2,22,25,0,"Picture","Texture filter and correction, HD UI, HUD labels, movies, anti-aliasing and scanlines."},
 {"display_detail",2,23,26,0,"Detail","Level of detail and draw distance: how far away riders, bikes and traffic stay detailed and visible."},
 {"display_effects",2,24,27,0,"Effects","Bloom, sharpening, colour, vignette and shadow smoothing on the race picture."},
 {"render_resolution",2,0,-1,0,"Render resolution","The game's internal resolution, in whole multiples of the PS1 picture."},
 {"texture_filter",2,1,-1,0,"Texture filter","Sharp keeps the PS1's crisp texels; Smooth blends them; xBR rounds off blocky edges and curves (sky, signs, HUD art) at high resolution."},
 {"window",2,2,-1,0,"Window","Play in a window or fullscreen. The picture keeps its aspect ratio."},
 {"texture_correction",2,3,-1,0,"Precise vertices","No PS1 \"wobble\", perspective-correct textures. Heavy on the CPU: holds 60 FPS, not 120."},
 {"hd_ui",2,4,-1,0,"HD UI","On: HD lettering, smoothed menus and HUD.\nOff: the game's original look."},
 {"hud_labels",2,5,-1,0,"HUD labels","Larger rider names and race messages. Also in the pause options."},
 {"aspect_ratio",2,6,-1,0,"Aspect ratio","Menus and races in 4:3, 16:9 or 21:9: a wider view, original HUD."},
 {"frame_rate",2,7,-1,0,"Frame rate","Real game frames at the original game speed.\n120 FPS needs a 120 Hz+ display and a fast PC."},
 {"movies",2,8,-1,0,"Movies","Full-screen movies: original 4:3 frame, or stretched to fill a widescreen display."},
 {"level_of_detail",2,9,-1,0,"Level of detail","How far away riders and bikes keep full detail. The PS1 swapped in low-detail models almost at once."},
 {"anti_aliasing",2,10,-1,0,"Anti-aliasing","Smooths jagged edges. FXAA is light on the GPU; Supersample is the cleanest but heavy."},
 {"vsync",2,11,-1,0,"VSync","On: no tearing on a 60 Hz display (120 Hz at 120 FPS). Off: least delay, may tear. Game speed is unchanged."},
 {"low_latency",2,12,-1,0,"Low-latency input","On: controls are read again just before each frame, for about one frame less delay. Off: standard timing."},
 {"scanlines",2,13,-1,0,"Scanlines","Dark gaps between the PS1's picture lines, like an old CRT TV: Light, Medium or Strong.\nOff: the clean picture."},
 {"draw_distance",2,14,-1,0,"Draw distance","How far away riders, bikes and traffic are drawn. Original: the PS1's, where they pop in."},
 {"bloom",2,16,-1,0,"Bloom","Bloom: a soft glow around bright areas such as lights and the sky.\nRaces only; HUD and menus untouched. Also in game: Display > Effects."},
 {"sharpening",2,17,-1,0,"Sharpening","Sharpening: crisper detail without halos (contrast-adaptive).\nRaces only; HUD and menus untouched. Also in game: Display > Effects."},
 {"colour",2,18,-1,0,"Colour","Colour: livelier colour (vibrance) and a little more contrast.\nRaces only; HUD and menus untouched. Also in game: Display > Effects."},
 {"vignette",2,19,-1,0,"Vignette","Vignette: darker picture corners, drawing the eye to the road.\nRaces only; HUD and menus untouched. Also in game: Display > Effects."},
 {"shadow_smoothing",2,20,-1,0,"Shadow smoothing","Shadow smoothing: soft edges on the bikes' and riders' shadows instead of the PS1's hard pixel steps.\nRaces only. Also in game: Display > Effects."},
 {"install_mod",3,0,-1,0,"Install a mod","Choose an .rrmod pack to install.\nNew mods start switched off."},
 {"mods_folder",3,1,-1,0,"Open mods folder","Open the mods folder in your installation.\nRemoved mods are kept in mods\\removed."},
 {"mod_dump",3,2,-1,0,"Dump for modding","On: Enhanced games save the textures, sounds and models they use to the mods folder (texture-dump, sound-dump, model-dump), to edit for your own pack. Texture packs pause while it is on."},
 {"installed_mods",3,3,-1,TN_GROUP|TN_FIXED,"Installed mods","Each installed mod: Enter or click switches it on or off.\nDel or the X button removes it (moved to mods\\removed)."},
 {"reimport",4,0,-1,0,"Repair / re-import game","Re-import your .bin (or .cue / .iso) to repair the installed game data."},
 {"open_folder",4,1,-1,0,"Open installation folder","Open the folder that holds your installed game, local build and saves."},
 {"prepare_build",4,2,-1,0,"Run setup again","If the game will not start or setup went wrong: rebuild it from scratch from your installed game (15-25 minutes).\nSaves and settings are kept."},
 {"report",4,3,-1,0,"Report a problem","Save a bug report: a picture of this screen, the game's recent logs and your settings, with a short note.\nIt goes into the reports folder of your installation."},
 {"custom_names",6,0,-1,0,"Custom names","Custom names appear in supported race HUD labels. Characters and saved profiles stay unchanged."},
 {"name1",6,1,-1,0,"Player 1 name","Type a name of up to 8 characters.\nSaved on this PC."},
 {"name2",6,2,-1,0,"Player 2 name","Type a name of up to 8 characters.\nSaved on this PC."},
 {"display",7,0,2,0,"Display","Resolution, textures, HD UI, aspect ratio, frame rate and level of detail."},
 {"user_settings",7,1,8,0,"User settings","Everything about your riders: names and taunt voices.\nAlso your achievements. Rider look is in the game: Options > User settings."},
 {"audio",7,2,13,0,"Audio","Music: the original soundtrack or your own WAV tracks."},
 {"system",7,3,11,0,"System","Loading speed, intro movies, controls and start-up."},
 {"rider_names",8,0,6,0,"Rider names","Both riders' names, shown in races.\nSaved on this PC."},
 {"taunt_voices",8,1,14,0,"Taunt voices","Your taunts in another rider's voice.\nPlayer 1 and Player 2 pick separately."},
 {"achievements",8,2,10,0,"Achievements","Local achievements.\nSaved on this PC."},
 {"rider_look",8,3,15,0,"Rider look","Bike paint, name tag colour, outfit and colours.\nPlayer 1 and Player 2 pick separately."},
 {"achievement_list",10,0,-1,TN_GROUP|TN_FIXED,"Achievements","Each achievement: what to do, your progress and when it unlocked."},
 {"reset_achievements",10,40,-1,0,"Reset all achievements","Locks every achievement again and clears all progress (races, wins, riding time).\nThe old file is kept as achievements.ini.bak. Close the game first."},
 {"discord",11,0,-1,0,"Discord presence","Optional Discord playing status.\nAlso in game: Options > System."},
 {"loading_speed",11,1,-1,0,"Loading speed","Fast: a 4x disc drive, so races load in about half a second. Movies keep their own pace.\nOriginal: the PS1's own load times. Applies on your next launch."},
 {"intro_movies",11,2,-1,0,"Intro movies","Skip the start-up movies and go straight to the title screen. Story scenes and the menu's idle movies still play.\nApplies on your next launch."},
 {"controls",11,3,1,0,"Controls","Devices, Player 2 and key / button maps. The game's own Analog / Digital setup is in game: Options > System > Game options > Controller."},
 {"start_up",11,4,-1,0,"Start-up","Straight to game: once set up, opening Jailbreak Recomp starts the game at once and the launcher closes with it.\nHold Shift while opening it to get the launcher back."},
 {"music",13,0,-1,0,"Music","Choose the original soundtrack or your own music.\nCustom tracks replace race music in Enhanced."},
 {"track",13,1,-1,0,"Track","Select an imported track. Left / Right to browse.\nIn game: Left / Right to browse; volume in Game options > Noise."},
 {"add_wav",13,2,-1,0,"Add WAV tracks","Copy your own WAV into the local music library.\nUp to 128 tracks, 64 MB each. Your source file stays where it is."},
 {"voice1",14,0,-1,0,"Player 1 voice","Your taunts in another rider's voice.\nPlayer 1 and Player 2 pick separately.\nApplies to Enhanced races."},
 {"voice2",14,1,-1,0,"Player 2 voice","Your taunts in another rider's voice.\nPlayer 1 and Player 2 pick separately.\nApplies to Enhanced races."},
 {"reset_voices",14,2,-1,0,"Reset voices","Both players back to their own voices."},
 {"paint1",15,0,-1,0,"Player 1 bike paint","Your bike in another colour. Original keeps the game's paint.\nNot in Five-O races. Applies to Enhanced races."},
 {"paint2",15,1,-1,0,"Player 2 bike paint","Your bike in another colour. Original keeps the game's paint.\nNot in Five-O races. Applies to Enhanced races."},
 {"tag1",15,2,-1,0,"Player 1 name tag","The colour of your name under your health bars in races.\nNot in Five-O races. Applies to Enhanced races."},
 {"tag2",15,3,-1,0,"Player 2 name tag","The colour of your name under your health bars in races.\nNot in Five-O races. Applies to Enhanced races."},
 {"outfit1",15,4,-1,0,"Player 1 outfit","Ride as a gang member or police: DeSades or Kaffe Boys, Captain, Lieutenant or Grunt. Not in Jail Break, where your chosen character stays, and not in Five-O.\nApplies to Enhanced races."},
 {"outfit2",15,5,-1,0,"Player 2 outfit","Ride as a gang member or police: DeSades or Kaffe Boys, Captain, Lieutenant or Grunt. Not in Jail Break, where your chosen character stays, and not in Five-O.\nApplies to Enhanced races."},
 {"colour1",15,6,-1,0,"Player 1 colours","Your rider in another colour scheme: the gangs' and police colours or schemes no rider wears. Original keeps the outfit's own colours.\nNot in Five-O races. Applies to Enhanced races."},
 {"colour2",15,7,-1,0,"Player 2 colours","Your rider in another colour scheme: the gangs' and police colours or schemes no rider wears. Original keeps the outfit's own colours.\nNot in Five-O races. Applies to Enhanced races."},
 {"reset_look",15,8,-1,0,"Reset rider look","Both players back to their own paint, name tag, rider and colours."},
 {"cheat_health",19,0,-1,0,"Infinite health","Player 1's health stays full.\nRaces only, Player 1."},
 {"cheat_bike",19,1,-1,0,"Infinite bike damage","Player 1's bike never wrecks.\nRaces only, Player 1."},
 {"cheat_nitro",19,2,-1,0,"Infinite nitro","Player 1 always has 10 nitros available.\nRaces only, Player 1."},
 {"cheat_win",19,3,-1,0,"Always win (stats screen)","Player 1 is listed first on the race stats screen.\nRaces only, Player 1."},
 {"cheat_timer",19,4,-1,0,"Stop timer","Holds the race clock at zero (shared by both riders)."},
 {"cheat_weapons",19,5,-1,0,"All weapons","Picking up your first weapon gives Player 1 every weapon.\nRaces only, Player 1."},
 {"cheat_jb_courses",19,6,-1,0,"Jail Break: all courses","Choose any course in Jail Break (L1 / R1 on Course).\nThe game's own code KLFSDA."},
 {"cheat_fiveo_courses",19,7,-1,0,"Five-O: all courses","Choose any course in Five-O (L1 / R1).\nThe game's own code BDK."},
 {"cheat_sidecar_courses",19,8,-1,0,"Side Car: all courses","Choose any course in Side Car (L1 / R1).\nThe game's own code CMB."},
 {"cheat_jb_nitros",19,9,-1,0,"Jail Break: four nitros","Start Jail Break races with four nitros.\nThe game's own code FDMFG."},
 {"cheats_off",19,10,-1,0,"Turn all cheats off","Switch every built-in cheat and custom code off."},
 {"add_code",19,11,-1,0,"Add custom code","Paste your own GameShark codes and give them a name.\nNew codes start switched off."},
 {"custom_codes",19,12,-1,TN_GROUP|TN_FIXED,"Custom codes","Each custom code: Enter or click switches it on or off.\nDel or the X button removes it."},
 {"original_play",23,0,-1,0,"Play original game","The unmodified game: no PC menu, mods, cheats or achievements. Same memory cards.\nWith every option below off, it is the stock game."},
 {"original_resolution",23,1,-1,0,"Resolution","Draw the game sharper, in whole multiples of the PS1 picture. Native is the PS1's own.\nApplies on your next Original launch."},
 {"original_filter",23,2,-1,0,"Texture filter","Sharp is the PS1's own texels; Smooth blends them; xBR rounds off blocky edges.\nApplies on your next Original launch."},
 {"original_window",23,3,-1,0,"Window","Start in a window or fullscreen. Alt+Enter switches while you play.\nApplies on your next Original launch."},
 {"original_aspect",23,4,-1,0,"Aspect ratio","16:9 or 21:9 widen the picture. The original HUD and menus are not adjusted and may look stretched.\nNext Original launch."},
 {"original_frame_rate",23,5,-1,0,"Frame rate","Original: the game's own frame rate and timing. 60 or 120 FPS are opt-in and are not the original timing.\nNext Original launch."},
 {"original_esc",23,6,-1,0,"Esc closes the game","On: Esc closes the game and returns to the launcher (the original game has no quit option).\nOff: close the window instead."},
 {"original_reset",23,7,-1,0,"Back to original settings","Every option on this page back to the stock game's own."},
 {"online_lobby",17,1,28,0,"Find a game","Play over the internet through the public lobby: browse rooms or open your own, up to 8 players.\nPlayers 3-8 take over AI riders in Skull-to-Skull."},
 {"online",17,0,18,0,"Direct connection","Two PCs on your network, one player each, by IP address.\nExperimental."},
 {"online_host",18,0,-1,0,"Host a game","Wait for the other player to join on your port. Your rider name, taunt voice and rider look are sent; your display timing is used by both PCs."},
 {"online_join",18,1,-1,0,"Join a game","Connect to the host's address and port. Both PCs then start the game; you are Player 2.\nYour name, voice and rider look are sent."},
 {"online_address",18,2,-1,0,"Host address","The host PC's IP address on your network (for Join), e.g. 192.168.1.20."},
 {"online_port",18,3,-1,0,"Port","Ports used: this one on the host, the next one on the joining PC (UDP and TCP). Allow both in your firewall."},
 {"online_delay",18,4,-1,0,"Input delay","Frames between a button press and the game seeing it on both PCs. 2 suits a home network; raise it if the game stutters."},
 {"online_status",18,5,-1,0,"Status","Whether this build can play online."},
 {"setup_recommended",16,0,-1,TN_FIXED,"Recommended","1080p, 16:9, 60 FPS, HD menus and more detailed riders. Runs well on most PCs.\nChange it later in Options > Display."},
 {"setup_classic",16,1,-1,TN_FIXED,"Classic look","The PS1's look: 4:3, its own frame rate and menus, drawn sharp. Saves and achievements still work.\nChange it later in Options > Display."},
 {"setup_high",16,2,-1,TN_FIXED,"High end","1440p, 16:9, 120 FPS, supersampling and the most detailed riders. Needs a fast PC and a 120 Hz display.\nChange it later in Options > Display."},
 /* 1.2: last in the table, so files written before it place it after Report a problem. */
 {"updates",4,4,-1,0,"Check for updates","Look for a new version on GitHub (the launcher also checks once a day).\nUpdates keep your saves, settings and installed game."}};
#define TN_ITEMS ((int)(sizeof tn_items/sizeof tn_items[0]))
#define TN_ROWS 32
/* Items shown by default on another page than their native one: the Display
 * settings in their groups (Screen 24, Picture 25, Detail 26, Effects 27). */
static const struct {const char*id;int page;} tn_homes[]={
 {"render_resolution",24},{"window",24},{"aspect_ratio",24},{"frame_rate",24},{"vsync",24},{"low_latency",24},
 {"texture_filter",25},{"texture_correction",25},{"hd_ui",25},{"hud_labels",25},{"movies",25},{"anti_aliasing",25},{"scanlines",25},
 {"level_of_detail",26},{"draw_distance",26},
 {"bloom",27},{"sharpening",27},{"colour",27},{"vignette",27},{"shadow_smoothing",27}};
/* The page number an item is shown on by default. */
static int tn_home(int item){for(size_t k=0;k<sizeof tn_homes/sizeof tn_homes[0];k++)if(!strcmp(tn_homes[k].id,tn_items[item].id))return tn_homes[k].page;return tn_items[item].page;}
#define TN_LABEL 48
#define TN_HELP 512
#define TN_TITLE 32
#define TN_LOD_MAX 12

/* ---- Pictures: the owner-disc still an item shows ------------------------- */
/* kind = owner_assets.h RR_ART_* (decoded from the player's disc, never shipped);
 * -1 = automatic (the launcher's own choice for that row). */
typedef struct TnPicture {const char*name;signed char kind;} TnPicture;
static const TnPicture tn_pictures[]={{"automatic",-1},{"logo",7},{"stone logo",15},{"controller",1},{"ps1 controller",5},{"riders",2},{"taunting rider",13},
 {"trophy",3},{"memory card",8},{"music",6},{"stopwatch",11},{"chopper",4},{"sports bike",16},{"race bike",17},{"course map",18},{"skulls",19},{"wreck",12},{"warrant",14},{"jail",20},
 {"beta meadows",21},{"beta countryside",22},{"beta alpine",23},{"beta rash city",24},{"beta cactus hills",25},{"beta coast",26},{"beta map",27},
 {"switch",28},{"stacks",29},{"brick",30},{"chops",31},{"kat",32},{"dru",33},{"gang",34},
 {"cop",35},{"donut cop",36},{"lady cop",37},{"helmet cop",38},{"caged",39},{"grabbed",40},
 {"free spaz",41},{"spaz on the phone",42},{"spaz escorted",43},{"bar",44},{"mugshot",45}};
#define TN_PICTURES ((int)(sizeof tn_pictures/sizeof tn_pictures[0]))
TN_FN int tn_picture_index(int kind){for(int i=0;i<TN_PICTURES;i++)if(tn_pictures[i].kind==kind)return i;return 0;}

/* ---- Display presets (Options > Display > Preset, first-run page) ---------- */
/* Fields in launcher.c's order (preset_field). Values are option steps; level
 * of detail -1 = the recommended maximum. */
typedef struct TnPresetField {const char*id,*name;int steps;const char*values[6];} TnPresetField;
static const TnPresetField tn_preset_fields[]={
 {"resolution","Render resolution",5,{"Native","720p","1080p","1440p","4K"}},{"texture_filter","Texture filter",3,{"Sharp","Smooth","xBR"}},
 {"texture_correction","Precise vertices",3,{"Off","Off","On"}},{"hd_ui","HD UI",2,{"Off","On"}},{"hud_labels","HUD labels",2,{"Original","Large"}},
 {"aspect_ratio","Aspect ratio",3,{"4:3","16:9","21:9"}},{"frame_rate","Frame rate",3,{"Original","60 FPS","120 FPS"}},{"movies","Movies",2,{"4:3","Widescreen"}},
 {"level_of_detail","Level of detail",0,{0}},{"anti_aliasing","Anti-aliasing",5,{"Off","FXAA","FXAA HQ","Supersample 2x","Supersample 4x"}},
 {"scanlines","Scanlines",4,{"Off","Light","Medium","Strong"}},{"draw_distance","Draw distance",4,{"Original","1.5x","2x","3x"}},
 {"vsync","VSync",2,{"Off","On"}},{"low_latency","Low-latency input",2,{"Off","On"}},
 {"bloom","Bloom",3,{"Off","Light","Strong"}},{"sharpening","Sharpening",3,{"Off","Light","Strong"}},{"colour","Colour",3,{"Off","Light","Strong"}},{"vignette","Vignette",3,{"Off","Light","Strong"}},
 {"shadow_smoothing","Shadow smoothing",3,{"Off","Light","Strong"}}};
#define TN_PRESET_FIELDS ((int)(sizeof tn_preset_fields/sizeof tn_preset_fields[0]))
static const char*const tn_preset_ids[3]={"recommended","classic look","high end"};
static const char*const tn_preset_names[3]={"Recommended","Classic look","High end"};
static const int tn_preset_builtin[3][TN_PRESET_FIELDS]={{2,0,0,1,1,1,1,0,2,1,0,0,1,1,0,0,0,0,0},{2,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0},{3,0,0,1,1,1,2,0,-1,3,0,2,1,1,0,0,0,0,0}};   /* post effects and shadow smoothing Off in every preset */

/* ---- House defaults: the owner's own text and pictures -------------------- */
/* From the owner's tuning.ini (16 Oct 2026): names (l), help (h), page titles (t)
 * and pictures (p) that replace the code's values above as the built-in defaults.
 * tn_items keeps the code's own help, so the launcher still knows which help is
 * its live text (preset, counts, warnings) and which is the owner's. */
typedef struct TnHouse {char field;const char*id,*value;} TnHouse;
static const TnHouse tn_house[]={
 {'h',"play","Start Road Rash: Jailbreak with your display settings, music and enabled mods. Enhancements are included in this mode."},
 {'h',"play_original","The untouched PS1 game, one ride: no PC options, mods or achievements. Same memory cards."},
 {'p',"play_original","stone logo"},
 {'h',"multiplayer","Two riders, one road. Online play is on its way."},
 {'p',"multiplayer","skulls"},
 {'h',"mods","Install .rrmod packs (artwork, textures, sounds, models) and switch them on or off.\nMods apply in the main \"Enhanced\" version. \nThe \"Original\" always stays stock."},
 {'p',"mods","taunting rider"},
 {'h',"options","Display, rider, audio and what's under the hood, controls included."},
 {'h',"quit","Park it and walk away.\nYour settings are already saved."},
 {'h',"render_resolution","How sharp the road looks, in whole multiples of the PS1 picture."},
 {'h',"texture_filter","Sharp keeps the PS1's crisp pixels; Smooth blends them; xBR rounds off blocky edges and curves at high resolution."},
 {'h',"hud_labels","Bigger rider names and race messages, easier to read at speed."},
 {'h',"aspect_ratio","Menus and races in 4:3, 16:9 or 21:9: more road on screen, same HUD."},
 {'h',"scanlines","Dark gaps between the picture lines, like an old CRT TV: Light, Medium or Strong.\nOff: the clean picture."},
 {'h',"draw_distance","How far ahead you see riders, bikes and traffic. Original: the PS1's, where they pop in late."},
 {'h',"report","Crashed? Save a report: a picture of this screen, the game's recent logs and your settings, with a short note.\nIt lands in the reports folder."},
 {'l',"music","Soundtrack"},
 {'h',"music","Ride to the original soundtrack or your own tunes.\nYour tracks replace the race music."},
 {'h',"track","Pick one of your imported tracks. Left / Right to browse.\nIn game: Left / Right to browse; volume in Game options > Noise."},
 {'h',"voice1","Talk trash in another rider's voice.\nRider 1 and Rider 2 pick separately.\nPlayer 1 and Player 2 pick separately.\nApplies to Enhanced races."},
 {'h',"voice2","Talk trash in another rider's voice.\nRider 1 and Rider 2 pick separately.\nPlayer 1 and Player 2 pick separately.\nApplies to Enhanced races."},
 {'h',"paint1","A fresh coat for your bike. Original keeps the game's paint.\nNot in Five-O races."},
 {'h',"paint2","A fresh coat for your bike. Original keeps the game's paint.\nNot in Five-O races."},
 {'l',"reset_look","Back to stock"},
 {'h',"reset_look","Both riders back to their own paint, name tag, rider and colours."},
 {'h',"cheat_win","Rider 1 tops the race stats screen, whatever happened out there."},
 {'l',"cheat_timer","Freeze the clock"},
 {'h',"cheat_weapons","Grab your first weapon and Rider 1 gets the lot.\nRaces only."},
 {'l',"setup_recommended","Street legal (Recommended)"},
 {'p',"setup_recommended","sports bike"},
 {'l',"setup_classic","Old school"},
 {'h',"setup_classic","The PS1 the way you remember it: 4:3, its own frame rate and menus, drawn sharp. Saves and achievements still work.\nCan be changed later in Options > Display."},
 {'p',"setup_classic","chopper"},
 {'l',"setup_high","Fully loaded"}};
#define TN_HOUSE ((int)(sizeof tn_house/sizeof tn_house[0]))

/* ---- The resolved settings ----------------------------------------------- */
typedef struct RrTuning {
    short order[TN_PAGES][TN_ROWS];   /* items shown on each page, top to bottom (hidden ones included) */
    unsigned char count[TN_PAGES];
    unsigned char hidden[TN_ITEMS];
    char label[TN_ITEMS][TN_LABEL];
    char help[TN_ITEMS][TN_HELP];
    char title[TN_PAGES][TN_TITLE];
    int lod[TN_LOD_MAX],lod_n,lod_safe;   /* percent per step; lod_safe = index of the recommended maximum */
    signed char picture[TN_ITEMS];        /* RR_ART_* kind, -1 automatic */
    int preset[3][TN_PRESET_FIELDS];      /* Display preset values (tn_preset_fields) */
} RrTuning;
TN_FN int tn_page_of_num(int num){for(int i=0;i<TN_PAGES;i++)if(tn_pages[i].num==num)return i;return -1;}
TN_FN int tn_item_at(int num,int row){for(int i=0;i<TN_ITEMS;i++)if(tn_items[i].page==num&&tn_items[i].row==row)return i;return -1;}
TN_FN int tn_item_id(const char*id){for(int i=0;i<TN_ITEMS;i++)if(!strcmp(tn_items[i].id,id))return i;return -1;}
TN_FN int tn_page_id(const char*id){for(int i=0;i<TN_PAGES;i++)if(!strcmp(tn_pages[i].id,id))return i;return -1;}
/* The page (index) an item is on now, and its position there. */
TN_FN int tn_where(const RrTuning*t,int item,int*pos){for(int p=0;p<TN_PAGES;p++)for(int k=0;k<t->count[p];k++)if(t->order[p][k]==item){if(pos)*pos=k;return p;}return -1;}
TN_FN void tn_copy(char*dst,size_t cap,const char*src){size_t n=strlen(src);if(n>cap-1)n=cap-1;memcpy(dst,src,n);dst[n]=0;}
TN_FN void tn_defaults(RrTuning*t){
    memset(t,0,sizeof *t);
    for(int i=0;i<TN_ITEMS;i++){int p=tn_page_of_num(tn_home(i));if(p>=0&&t->count[p]<TN_ROWS)t->order[p][t->count[p]++]=(short)i;
        tn_copy(t->label[i],TN_LABEL,tn_items[i].label);tn_copy(t->help[i],TN_HELP,tn_items[i].help);}
    for(int p=0;p<TN_PAGES;p++)tn_copy(t->title[p],TN_TITLE,tn_pages[p].title);
    t->lod_n=rr_detail_steps<TN_LOD_MAX?rr_detail_steps:TN_LOD_MAX;for(int i=0;i<t->lod_n;i++)t->lod[i]=rr_detail_percent[i];t->lod_safe=rr_detail_safe<t->lod_n?rr_detail_safe:t->lod_n-1;
    for(int i=0;i<TN_ITEMS;i++)t->picture[i]=-1;memcpy(t->preset,tn_preset_builtin,sizeof t->preset);
    for(int h=0;h<TN_HOUSE;h++){const TnHouse*e=tn_house+h;
        if(e->field=='t'){for(int p=0;p<TN_PAGES;p++)if(!strcmp(tn_pages[p].id,e->id))tn_copy(t->title[p],TN_TITLE,e->value);continue;}
        int i=tn_item_id(e->id);if(i<0)continue;
        if(e->field=='l')tn_copy(t->label[i],TN_LABEL,e->value);else if(e->field=='h')tn_copy(t->help[i],TN_HELP,e->value);
        else if(e->field=='p')for(int j=0;j<TN_PICTURES;j++)if(!strcmp(tn_pictures[j].name,e->value))t->picture[i]=tn_pictures[j].kind;}
}
/* A preset value as text, and back (level of detail: "recommended maximum" or a step name). */
TN_FN void tn_lod_name(int percent,char*out,size_t cap);
TN_FN void tn_preset_text(const RrTuning*t,int k,int v,char*out,size_t cap){
    const TnPresetField*f=tn_preset_fields+k;
    if(!f->steps){if(v<0){snprintf(out,cap,"recommended maximum");return;}tn_lod_name(t->lod[v<t->lod_n?v:t->lod_n-1],out,cap);return;}
    snprintf(out,cap,"%s",f->values[v>=0&&v<f->steps?v:0]);
}
TN_FN int tn_preset_parse(const RrTuning*t,int k,const char*s,int*v){
    const TnPresetField*f=tn_preset_fields+k;char want[64],have[64];tni_name(want,sizeof want,s,strlen(s));
    if(!f->steps){if(!strcmp(want,"recommended maximum")||!strcmp(want,"recommended")){*v=-1;return 1;}
        for(int i=0;i<t->lod_n;i++){char n[16];tn_lod_name(t->lod[i],n,sizeof n);tni_name(have,sizeof have,n,strlen(n));if(!strcmp(want,have)){*v=i;return 1;}}return 0;}
    for(int i=0;i<f->steps;i++){tni_name(have,sizeof have,f->values[i],strlen(f->values[i]));if(!strcmp(want,have)){*v=i;return 1;}}return 0;
}
/* Which pages can be reached from the main menu through visible items. */
TN_FN void tn_reach(const RrTuning*t,int*reach){
    for(int p=0;p<TN_PAGES;p++)reach[p]=0;reach[tn_page_id("main")]=1;
    for(int changed=1;changed;){changed=0;
        for(int p=0;p<TN_PAGES;p++)if(reach[p])for(int k=0;k<t->count[p];k++){int i=t->order[p][k];if(t->hidden[i])continue;int q=tn_page_of_num(tn_items[i].opens);if(q>=0&&!reach[q]){reach[q]=1;changed=1;}}}
}
TN_FN void tn_move(RrTuning*t,int item,int page,int pos){
    int pos0,p0=tn_where(t,item,&pos0);if(p0<0||page<0||page>=TN_PAGES)return;
    if(p0!=page&&t->count[page]>=TN_ROWS)return;
    for(int k=pos0;k+1<t->count[p0];k++)t->order[p0][k]=t->order[p0][k+1];t->count[p0]--;
    if(pos<0||pos>t->count[page])pos=t->count[page];
    for(int k=t->count[page];k>pos;k--)t->order[page][k]=t->order[page][k-1];t->order[page][pos]=(short)item;t->count[page]++;
}
/* Puts back anything that would lock a page away or that cannot move. A page
 * link hidden on purpose, on its own page, stays hidden (that page is retired;
 * the Developer editor, always on the main menu, can show it again). */
TN_FN void tn_fix_layout(RrTuning*t,TniLog*log){
    for(int i=0;i<TN_ITEMS;i++)if(tn_items[i].flags&TN_FIXED){int home=tn_page_of_num(tn_home(i));
        if(t->hidden[i]){t->hidden[i]=0;tni_log(log,"\"%s\" cannot be hidden; it is shown again",tn_items[i].id);}
        if(tn_where(t,i,NULL)!=home){tn_move(t,i,home,-1);tni_log(log,"\"%s\" cannot leave its page; it is back on %s",tn_items[i].id,tn_pages[home].id);}}
    for(int guard=0;guard<TN_PAGES;guard++){
        int reach[TN_PAGES],fixed=0;tn_reach(t,reach);
        for(int p=0;p<TN_PAGES;p++)if(!reach[p])for(int i=0;i<TN_ITEMS;i++)if(tn_items[i].opens==tn_pages[p].num){
            int home=tn_page_of_num(tn_home(i));if(t->hidden[i]&&tn_where(t,i,NULL)==home)continue;
            tni_log(log,"the %s page could not be reached from the main menu; \"%s\" is shown on %s again",tn_pages[p].id,tn_items[i].id,tn_pages[home].id);
            t->hidden[i]=0;if(tn_where(t,i,NULL)!=home)tn_move(t,i,home,-1);fixed=1;}
        if(!fixed)break;
    }
}
/* Text the launcher's ASCII game font can show. Typographic quotes and
 * dashes become plain ones; anything else outside ASCII is refused. */
TN_FN int tn_clean_text(char*out,size_t cap,const char*in,int lines){
    size_t o=0;const unsigned char*s=(const unsigned char*)in;
    for(;*s;){unsigned c=*s;
        if(c==0xE2&&s[1]==0x80&&s[2]){unsigned d=s[2];char r=d==0x98||d==0x99?'\'':d==0x9C||d==0x9D?'"':d==0x93||d==0x94?'-':d==0xA6?'.':0;if(!r)return 0;
            if(d==0xA6){for(int k=0;k<3&&o+1<cap;k++)out[o++]='.';}else if(o+1<cap)out[o++]=r;s+=3;continue;}
        if(c>=0x80)return 0;
        if(c=='\n'){if(!lines)return 0;}else if(c=='\t')c=' ';else if(c<32||c>126)return 0;
        if(o+1>=cap)return -1;out[o++]=(char)c;s++;}
    while(o&&out[o-1]==' ')o--;out[o]=0;return o?1:0;
}
TN_FN void tn_text(char*dst,size_t cap,int lines,TniFile*f,const char*sec,const char*key,TniLog*log){
    TniEntry*e=tni_find(f,sec,key);if(!e)return;char tmp[TNI_VAL];int r=tn_clean_text(tmp,cap<sizeof tmp?cap:sizeof tmp,e->val,lines);
    if(r==1)tn_copy(dst,cap,tmp);
    else if(r==-1)tni_log(log,"line %d: [%s] %s is too long (up to %u characters); the built-in text is used",e->line,sec,key,(unsigned)cap-1);
    else tni_log(log,"line %d: [%s] %s is empty or uses characters the game's font does not have; the built-in text is used",e->line,sec,key);
}
TN_FN int tn_ints(const char*s,int*out,int max){int n=0;while(*s){while(*s==' '||*s==','||*s=='\t')s++;if(!*s)break;char*end;long v=strtol(s,&end,10);if(end==s)return -1;if(n>=max)return -2;out[n++]=(int)v;s=end;while(*s==' '||*s=='\t')s++;if(*s&&*s!=',')return -1;}return n;}
/* Reads the file's settings over the built-in defaults. */
TN_FN void tn_resolve(RrTuning*t,TniFile*f,TniLog*log){
    tn_defaults(t);
    /* Menus: each page's list, in order; items listed nowhere come back on their own page. */
    int placed[TN_ITEMS];for(int i=0;i<TN_ITEMS;i++)placed[i]=0;
    int any=0;RrTuning fresh;memcpy(&fresh,t,sizeof fresh);
    for(int p=0;p<TN_PAGES;p++)fresh.count[p]=0;
    for(int p=0;p<TN_PAGES;p++){TniEntry*e=tni_find(f,"launcher menu",tn_pages[p].id);
        if(!e){continue;}any=1;
        const char*s=e->val;char id[64];
        while(*s){while(*s==' '||*s==','||*s=='\t')s++;size_t n=0;while(s[n]&&s[n]!=',')n++;tni_name(id,sizeof id,s,n);s+=n;if(!id[0])continue;
            int i=tn_item_id(id);
            if(i<0){tni_log(log,"line %d: [launcher menu] %s: there is no item called \"%s\"",e->line,tn_pages[p].id,id);continue;}
            if(placed[i]){tni_log(log,"line %d: [launcher menu] %s: \"%s\" is already on a page",e->line,tn_pages[p].id,id);continue;}
            if(fresh.count[p]>=TN_ROWS){tni_log(log,"line %d: [launcher menu] %s has more than %d items",e->line,tn_pages[p].id,TN_ROWS);continue;}
            fresh.order[p][fresh.count[p]++]=(short)i;placed[i]=1;}
    }
    if(any){
        for(int p=0;p<TN_PAGES;p++){int listed=tni_find(f,"launcher menu",tn_pages[p].id)!=NULL;
            if(!listed)for(int k=0;k<t->count[p];k++){int i=t->order[p][k];if(!placed[i]&&fresh.count[p]<TN_ROWS){fresh.order[p][fresh.count[p]++]=(short)i;placed[i]=1;}}}
        for(int i=0;i<TN_ITEMS;i++)if(!placed[i]){int p=tn_page_of_num(tn_home(i));if(fresh.count[p]<TN_ROWS){
            /* Otherwise right after the item listed before it here (same page), or last. */
            int at=fresh.count[p],found=0;
            if(tn_items[i].flags&TN_FIRST)at=0;
            else for(int j=i-1;j>=0&&!found;j--)if(tn_home(j)==tn_home(i))for(int k=0;k<fresh.count[p];k++)if(fresh.order[p][k]==j){at=k+1;found=1;break;}
            for(int k=fresh.count[p];k>at;k--)fresh.order[p][k]=fresh.order[p][k-1];fresh.order[p][at]=(short)i;fresh.count[p]++;placed[i]=1;}}
        memcpy(t->order,fresh.order,sizeof t->order);memcpy(t->count,fresh.count,sizeof t->count);
        /* A file written before the Display groups lists every Display setting on
           "display": while it lists no group page, those settings move to their
           group (in the built-in order); anything the player moved elsewhere stays. */
        const int display=tn_page_id("display");
        for(int i=0;i<TN_ITEMS;i++){int home=tn_home(i);if(home<24||home>27)continue;int g=tn_page_of_num(home);
            if(g<0||tni_find(f,"launcher menu",tn_pages[g].id)||tn_where(t,i,NULL)!=display)continue;tn_move(t,i,g,-1);}
    }
    TniEntry*h=tni_find(f,"launcher menu","hidden");
    if(h){const char*s=h->val;char id[64];
        while(*s){while(*s==' '||*s==','||*s=='\t')s++;size_t n=0;while(s[n]&&s[n]!=',')n++;tni_name(id,sizeof id,s,n);s+=n;if(!id[0])continue;
            int i=tn_item_id(id);if(i<0)tni_log(log,"line %d: [launcher menu] hidden: there is no item called \"%s\"",h->line,id);else t->hidden[i]=1;}}
    tn_fix_layout(t,log);
    for(int p=0;p<TN_PAGES;p++)tn_text(t->title[p],TN_TITLE,0,f,"launcher titles",tn_pages[p].id,log);
    for(int i=0;i<TN_ITEMS;i++){tn_text(t->label[i],TN_LABEL,0,f,"launcher labels",tn_items[i].id,log);tn_text(t->help[i],TN_HELP,1,f,"launcher help",tn_items[i].id,log);
        /* 1.2 renamed "Prepare local game build": a file holding the old built-in text gets the new. */
        if(!strcmp(tn_items[i].id,"prepare_build")){if(!strcmp(t->label[i],"Prepare local game build"))tn_copy(t->label[i],TN_LABEL,tn_items[i].label);
            if(!strncmp(t->help[i],"Rebuild the local native game from your installed data.",56))tn_copy(t->help[i],TN_HELP,tn_items[i].help);}}
    /* Level of detail. */
    TniEntry*e=tni_find(f,"level of detail","steps");
    if(e){int v[TN_LOD_MAX],n=tn_ints(e->val,v,TN_LOD_MAX),ok=n>=2;const char*why="it needs 2 to 12 numbers separated by commas";
        if(ok&&v[0]!=100){ok=0;why="the first step must be 100 (the original game)";}
        for(int i=1;ok&&i<n;i++){if(v[i]<=v[i-1]){ok=0;why="each step must be larger than the one before";}else if(v[i]%25||v[i]>3200){ok=0;why="steps are multiples of 25, up to 3200";}}
        if(ok){t->lod_n=n;for(int i=0;i<n;i++)t->lod[i]=v[i];if(t->lod_safe>=n)t->lod_safe=n-1;}
        else tni_log(log,"line %d: [level of detail] steps: %s; the built-in steps are used",e->line,why);}
    e=tni_find(f,"level of detail","recommended");
    if(e){char*end;long v=strtol(e->val,&end,10);int k=-1;for(int i=0;i<t->lod_n;i++)if(t->lod[i]==v)k=i;
        if(end==e->val||k<0)tni_log(log,"line %d: [level of detail] recommended must be one of the steps",e->line);else t->lod_safe=k;}
    /* Pictures and presets (after the level-of-detail steps, which presets name). */
    for(int i=0;i<TN_ITEMS;i++){TniEntry*p=tni_find(f,"launcher pictures",tn_items[i].id);if(!p)continue;char want[64],have[64];tni_name(want,sizeof want,p->val,strlen(p->val));int k=-2;
        for(int j=0;j<TN_PICTURES;j++){tni_name(have,sizeof have,tn_pictures[j].name,strlen(tn_pictures[j].name));if(!strcmp(want,have))k=tn_pictures[j].kind;}
        if(k==-2)tni_log(log,"line %d: [launcher pictures] %s: there is no picture called \"%s\"",p->line,tn_items[i].id,p->val);else t->picture[i]=(signed char)k;}
    for(int p=0;p<3;p++){char sec[48];snprintf(sec,sizeof sec,"preset %s",tn_preset_ids[p]);
        for(int k=0;k<TN_PRESET_FIELDS;k++){TniEntry*v=tni_find(f,sec,tn_preset_fields[k].id);if(!v)continue;int x;
            if(tn_preset_parse(t,k,v->val,&x))t->preset[p][k]=x;else tni_log(log,"line %d: [%s] %s: \"%s\" is not one of its values",v->line,sec,tn_preset_fields[k].id,v->val);}}
    for(int i=0;i<f->n;i++)if(!f->e[i].used)tni_log(log,"line %d: [%s] %s is not a setting this version knows (ignored)",f->e[i].line,f->e[i].sec,f->e[i].key);
}
/* The whole file, every value written out, with short explanations. */
TN_FN void tn_write(const RrTuning*t,TniOut*o){
    tno_comment(o,"Road Rash: Jailbreak (Enhanced) - tuning file.\n"
#ifdef RR_PUBLIC_RELEASE
        "Change it by hand; the launcher reads it when it starts.\n"
#else
        "Change it with the launcher's Developer page (Ctrl+Shift+D on the main menu), or by hand.\n"
#endif
        "Delete a line to get its built-in value back. A value that cannot be used keeps its\n"
        "built-in value and is listed in tuning-problems.txt next to this file.\n"
        "Saved changes are listed in tuning-changes.log.");
    tno_str(o,"\r\n[launcher menu]\r\n");
    tno_comment(o,"Each line is one launcher page: its items, top to bottom. Move a name to another\n"
        "line to move the item to that page. Items in \"hidden\" are not shown.");
    for(int p=0;p<TN_PAGES;p++){char line[TN_ROWS*24]="";for(int k=0;k<t->count[p];k++){if(k)strcat(line,", ");strcat(line,tn_items[t->order[p][k]].id);}tno_kv(o,tn_pages[p].id,line);}
    {char line[TN_ITEMS*24]="";int n=0;for(int i=0;i<TN_ITEMS;i++)if(t->hidden[i]){if(n++)strcat(line,", ");strcat(line,tn_items[i].id);}tno_kv(o,"hidden",line);}
    tno_str(o,"\r\n[launcher titles]\r\n");
    for(int p=0;p<TN_PAGES;p++)tno_kv(o,tn_pages[p].id,t->title[p]);
    tno_str(o,"\r\n[launcher labels]\r\n");tno_comment(o,"The name shown on each item (up to 47 characters).");
    for(int i=0;i<TN_ITEMS;i++)tno_kv(o,tn_items[i].id,t->label[i]);
    tno_str(o,"\r\n[launcher help]\r\n");tno_comment(o,"The text in the panel on the right. \\n starts a new line.\n"
        "While an item's help is the built-in text, the launcher still adds its usual notes (mode, counts, warnings).");
    for(int i=0;i<TN_ITEMS;i++)tno_kv(o,tn_items[i].id,t->help[i]);
    tno_str(o,"\r\n[level of detail]\r\n");
    tno_comment(o,"How far away riders and bikes keep full detail, in percent of the original game's distance.\n"
        "steps: 2 to 12 values, the first is 100 (the original), then larger ones, multiples of 25, up to 3200.\n"
        "recommended: the highest step that holds 60 FPS everywhere; steps above it show a warning.");
    {char line[160]="";for(int i=0;i<t->lod_n;i++){char v[16];snprintf(v,sizeof v,i?", %d":"%d",t->lod[i]);strcat(line,v);}tno_kv(o,"steps",line);
     char v[16];snprintf(v,sizeof v,"%d",t->lod[t->lod_safe]);tno_kv(o,"recommended",v);}
    tno_str(o,"\r\n[launcher pictures]\r\n");
    {char names[600]="";for(int j=0;j<TN_PICTURES;j++){if(j)strcat(names,", ");strcat(names,tn_pictures[j].name);}
     char c[700];snprintf(c,sizeof c,"The picture each item shows, from your own game disc. One of:\n%s",names);tno_comment(o,c);}
    for(int i=0;i<TN_ITEMS;i++)tno_kv(o,tn_items[i].id,tn_pictures[tn_picture_index(t->picture[i])].name);
    for(int p=0;p<3;p++){tno_printf(o,"\r\n[preset %s]\r\n",tn_preset_ids[p]);
        if(!p)tno_comment(o,"The Display presets: what each one sets. Values are the names shown on Options > Display;\n"
            "level_of_detail is a step name (Original, 2x ...) or \"recommended maximum\".");
        for(int k=0;k<TN_PRESET_FIELDS;k++){char v[48];tn_preset_text(t,k,t->preset[p][k],v,sizeof v);tno_kv(o,tn_preset_fields[k].id,v);}}
}
/* ---- Plain-English differences (change log, "my changes") ----------------- */
typedef void (*TnLine)(void*ctx,const char*line);
TN_FN void tn_quote(char*out,size_t cap,const char*s){size_t o=0;out[o++]='"';for(;*s&&o+6<cap;s++){if(*s=='\n'){out[o++]=' ';out[o++]='/';out[o++]=' ';}else out[o++]=*s;}if(*s){out[o++]='.';out[o++]='.';out[o++]='.';}out[o++]='"';out[o]=0;}
TN_FN void tn_lod_name(int percent,char*out,size_t cap){if(percent==100)snprintf(out,cap,"Original");else if(percent%100)snprintf(out,cap,"%d.%02dx",percent/100,percent%100);else snprintf(out,cap,"%dx",percent/100);
    size_t n=strlen(out);if(n>2&&out[n-2]=='0'&&out[n-1]=='x'&&strchr(out,'.')){out[n-2]='x';out[n-1]=0;}}
TN_FN void tn_diff(const RrTuning*a,const RrTuning*b,TnLine emit,void*ctx){
    char line[1400],qa[300],qb[300];
    for(int p=0;p<TN_PAGES;p++)if(strcmp(a->title[p],b->title[p])){tn_quote(qa,sizeof qa,a->title[p]);tn_quote(qb,sizeof qb,b->title[p]);snprintf(line,sizeof line,"%s page title: %s -> %s",tn_pages[p].name,qa,qb);emit(ctx,line);}
    for(int i=0;i<TN_ITEMS;i++){
        int pa=tn_where(a,i,NULL),pb=tn_where(b,i,NULL);
        if(strcmp(a->label[i],b->label[i])){tn_quote(qa,sizeof qa,a->label[i]);tn_quote(qb,sizeof qb,b->label[i]);snprintf(line,sizeof line,"%s > %s: name %s -> %s",tn_pages[pb].name,tn_items[i].label,qa,qb);emit(ctx,line);}
        if(strcmp(a->help[i],b->help[i])){tn_quote(qa,sizeof qa,a->help[i]);tn_quote(qb,sizeof qb,b->help[i]);snprintf(line,sizeof line,"%s > %s: help %s -> %s",tn_pages[pb].name,tn_items[i].label,qa,qb);emit(ctx,line);}
        if(a->hidden[i]!=b->hidden[i]){snprintf(line,sizeof line,"%s > %s: %s",tn_pages[pb].name,tn_items[i].label,b->hidden[i]?"hidden":"shown again");emit(ctx,line);}
        if(pa!=pb){snprintf(line,sizeof line,"%s: moved from %s to %s",tn_items[i].label,tn_pages[pa].name,tn_pages[pb].name);emit(ctx,line);}
        if(a->picture[i]!=b->picture[i]){snprintf(line,sizeof line,"%s > %s: picture %s -> %s",tn_pages[pb].name,tn_items[i].label,tn_pictures[tn_picture_index(a->picture[i])].name,tn_pictures[tn_picture_index(b->picture[i])].name);emit(ctx,line);}
    }
    for(int p=0;p<3;p++)for(int k=0;k<TN_PRESET_FIELDS;k++)if(a->preset[p][k]!=b->preset[p][k]){char va[48],vb[48];tn_preset_text(a,k,a->preset[p][k],va,sizeof va);tn_preset_text(b,k,b->preset[p][k],vb,sizeof vb);
        snprintf(line,sizeof line,"Preset %s > %s: %s -> %s",tn_preset_names[p],tn_preset_fields[k].name,va,vb);emit(ctx,line);}
    for(int p=0;p<TN_PAGES;p++){int same=a->count[p]==b->count[p];for(int k=0;same&&k<a->count[p];k++)same=a->order[p][k]==b->order[p][k];
        if(!same){size_t n=(size_t)snprintf(line,sizeof line,"%s order: ",tn_pages[p].name);for(int k=0;k<b->count[p]&&n<sizeof line-60;k++)n+=(size_t)snprintf(line+n,sizeof line-n,"%s%s",k?", ":"",tn_items[b->order[p][k]].label);emit(ctx,line);}}
    int lod_same=a->lod_n==b->lod_n;for(int i=0;lod_same&&i<a->lod_n;i++)lod_same=a->lod[i]==b->lod[i];
    if(!lod_same){size_t n=(size_t)snprintf(line,sizeof line,"Level of detail steps: ");for(int i=0;i<b->lod_n;i++){char v[16];tn_lod_name(b->lod[i],v,sizeof v);n+=(size_t)snprintf(line+n,sizeof line-n,"%s%s",i?", ":"",v);}emit(ctx,line);}
    if(a->lod[a->lod_safe]!=b->lod[b->lod_safe]){char va[16],vb[16];tn_lod_name(a->lod[a->lod_safe],va,sizeof va);tn_lod_name(b->lod[b->lod_safe],vb,sizeof vb);snprintf(line,sizeof line,"Level of detail recommended maximum: %s -> %s",va,vb);emit(ctx,line);}
}
TN_FN void tn_count_line(void*ctx,const char*line){(void)line;(*(int*)ctx)++;}
TN_FN int tn_changes(const RrTuning*a,const RrTuning*b){int n=0;tn_diff(a,b,tn_count_line,&n);return n;}

/* ---- Files ---------------------------------------------------------------- */
TN_FN char*tn_read_all(const wchar_t*path,size_t*len){
    FILE*f=_wfopen(path,L"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0||n>4*1024*1024){fclose(f);return NULL;}
    char*b=(char*)malloc((size_t)n+1);if(!b){fclose(f);return NULL;}size_t got=fread(b,1,(size_t)n,f);fclose(f);b[got]=0;if(len)*len=got;return b;
}
/* Loads `path` over the defaults. Returns 0 if the file does not exist. */
TN_FN int tn_load(const wchar_t*path,RrTuning*t,TniLog*log){
    size_t len=0;char*text=tn_read_all(path,&len);tn_defaults(t);if(!text)return 0;
    TniFile f={0};if(tni_parse(&f,text,len,log))tn_resolve(t,&f,log);tni_free(&f);free(text);return 1;
}
TN_FN int tn_write_file(const wchar_t*path,const char*data,size_t n){
    wchar_t tmp[32768];swprintf(tmp,32768,L"%ls.tmp",path);FILE*f=_wfopen(tmp,L"wb");if(!f)return 0;
    int ok=fwrite(data,1,n,f)==n;if(fclose(f))ok=0;if(!ok){DeleteFileW(tmp);return 0;}
    return MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
/* Saves (the previous file is kept as tuning.ini.bak). */
TN_FN int tn_save(const wchar_t*path,const RrTuning*t){
    TniOut o={0};tn_write(t,&o);if(o.failed||!o.b){tno_free(&o);return 0;}
    wchar_t bak[32768];swprintf(bak,32768,L"%ls.bak",path);CopyFileW(path,bak,FALSE);
    int ok=tn_write_file(path,o.b,o.n);tno_free(&o);return ok;
}
typedef struct TnLogCtx {FILE*f;char stamp[32];int n;} TnLogCtx;
TN_FN void tn_log_line(void*ctx,const char*line){TnLogCtx*c=(TnLogCtx*)ctx;fprintf(c->f,"%s  %s\r\n",c->stamp,line);c->n++;}
/* Appends one dated line per change from `before` to `after`. */
TN_FN int tn_log_changes(const wchar_t*log_path,const RrTuning*before,const RrTuning*after,const char*where){
    FILE*f=_wfopen(log_path,L"ab");if(!f)return 0;TnLogCtx c;c.f=f;c.n=0;SYSTEMTIME s;GetLocalTime(&s);
    snprintf(c.stamp,sizeof c.stamp,"%04u-%02u-%02u %02u:%02u",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute);
    tn_diff(before,after,tn_log_line,&c);if(c.n)fprintf(f,"%s  (saved from the %s)\r\n",c.stamp,where);
    int ok=!ferror(f);if(fclose(f))ok=0;return ok;
}
#endif
