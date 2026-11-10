/* Cheats: GameShark-style codes and the game's own unlock flags.
 * LGPL-2.1-or-later. Original author code; no game code or data.
 * Shared by the launcher (C: validation, the Cheats page, cheats_page.h) and
 * the runtime (C++: cheats_runtime.h applies them every frame).
 *
 * Settings: cheats.ini in the installation folder, beside launcher.ini.
 *   [cheats]  <id>=0/1 for each built-in toggle (rc_cheats below)
 *   [custom]  count=N, nameK / codeK / onK for K = 1..N (the owner's codes)
 * Everything is off by default. Enhanced only: Original runs the stock
 * runtime, which has no cheat code. Achievements do not unlock while any
 * cheat is on (pc_services.cpp award).
 *
 * Code format (PS1 GameShark / Action Replay), one code per line; lines are
 * separated by new lines, ';' or '|'; spaces are optional:
 *   80aaaaaa vvvv  write the 16-bit value vvvv at 0x80aaaaaa (even address)
 *   30aaaaaa 00vv  write the 8-bit value vv
 *   D0aaaaaa vvvv  the next line runs only while the 16-bit value == vvvv
 *   D1 / D2 / D3   ... != / < / > vvvv
 *   E0aaaaaa 00vv  the next line runs only while the 8-bit value == vv
 *   E1 / E2 / E3   ... != / < / > vv
 * A false condition skips the line it guards (and, if that line is a
 * condition too, the line that one guards). Addresses are main RAM
 * (aaaaaa < 0x200000). Increment codes (10/11/20/21) and the like are not
 * supported: applied every frame they would run away.
 *
 * Built-in codes (verified in races on the NTSC-U disc, SLUS-010.53):
 *   Riders: the actor table *(0x8005B3A0) holds 0x448-byte riders; actor
 *   +0xAC is the rider index (0 = Player 1, 1 = Player 2 in split screen)
 *   and actor +0x43C points to the rider's race record (records from
 *   0x800D5758, exe 0x80011E64): Player 1's is 0x800D5758 in one-player and
 *   split-screen races; the next rider's is +0x90 in one-player races, Player
 *   2's +0x48 (0x800D57A0) in split screen. Per-rider pick-up
 *   table 0x800D81D8 + 0x24 * index (race overlay 0x80062384): +0x0C/+0x0E
 *   weapon bits, +0x14 nitros available (compared with the used count,
 *   actor +0x350). Race context *(0x8005B2F8) = 0x800D5D38 (set at exe
 *   0x80011738): +0 race flag, +0x10 race clock (deadlines at exe 0x8001B368).
 *   So every built-in race code hits rider 0 = Player 1 (or the race clock);
 *   the runtime applies them only in a race whose layout matches (see
 *   cheats_runtime.h), so Player 2's seat is never touched.
 *   Unlocks: the game's own cheat entry (Options > highlight Multiplayer,
 *   hold L1+L2+R1+R2+Left, press X; frontend 0x800697F4) ORs bit i of the
 *   word 0x8005AE7C for code i of the table at 0x800540B8: KLFSDA 0x01,
 *   BDK 0x02, CMB 0x04, DIEOF 0x08, EQWJD 0x10, FDMFG 0x20, GPOWOP 0x40,
 *   HAMZNX 0x80. Only 0x01/0x02/0x04 (course choice with L1/R1 in Jail Break
 *   0x20, Five-O 1, Side Car 8: frontend 0x80066AA8) and 0x20 (four nitros
 *   in Jail Break: frontend 0x8007F6F0 sets 0x800D80F5 = 4) are read. The
 *   word lives in RAM only (never saved to the memory card). */
#ifndef RR_CHEAT_CODES_H
#define RR_CHEAT_CODES_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct RcLine {uint8_t type;uint32_t addr;uint16_t value;} RcLine;
#define RC_MAX_LINES 32
#define RC_MAX_CUSTOM 16
#define RC_NAME 32
#define RC_TEXT 512   /* RC_MAX_LINES codes as "TTAAAAAA VVVV;" */
#define RC_UNLOCK_WORD 0x8005AE7Cu

typedef struct RcCheat {const char *id,*name,*codes;unsigned unlock;const char *help;} RcCheat;
/* Race cheats first (RC_RACE_CHEATS), then the game's unlock flags. */
static const RcCheat rc_cheats[]={
 {"health","Infinite health","800D5766 5A5A",0,"Player 1's health stays full.\nGameShark 800D5766 5A5A (Player 1's race record)."},
 {"bike","Infinite bike damage","800D577C 4040",0,"Player 1's bike never wrecks: its condition stays full.\nGameShark 800D577C 4040."},
 {"nitro","Infinite nitro","300D81EC 000A",0,"Player 1 always has 10 nitros available (nitro bikes: Down + X twice).\nGameShark 300D81EC 000A."},
 {"win","Always win (stats screen)","800D577E 016E",0,"Player 1 is listed in first place on the race stats screen.\nGameShark 800D577E 016E."},
 {"timer","Stop timer","800D5D48 0000",0,"Holds the race clock at zero (both riders: the clock is shared). Timed events may not end.\nGameShark 800D5D48 0000."},
 {"weapons","All weapons","800D81E4 FFFF;800D81E6 FFFF",0,"Picking up your first weapon gives Player 1 every weapon.\nGameShark 800D81E4 FFFF + 800D81E6 FFFF."},
 {"jb_courses","Jail Break: all courses",NULL,0x01,"Choose any course in Jail Break: Solo > Jail Break > Continue, highlight Course, L1 / R1.\nThe game's own code KLFSDA."},
 {"fiveo_courses","Five-O: all courses",NULL,0x02,"Choose any course in Five-O: Solo > Five-O > Race, L1 / R1.\nThe game's own code BDK."},
 {"sidecar_courses","Side Car: all courses",NULL,0x04,"Choose any course in Side Car: Multiplayer > Side Car (Co-op) > Race, L1 / R1.\nThe game's own code CMB."},
 {"jb_nitros","Jail Break: four nitros",NULL,0x20,"Start Jail Break races with four nitros (Down + X twice).\nThe game's own code FDMFG."}};
#define RC_CHEATS ((int)(sizeof rc_cheats/sizeof rc_cheats[0]))
#define RC_RACE_CHEATS 6

static inline int rc_hex(int c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static inline int rc_known(int t){return t==0x80||t==0x30||(t>=0xD0&&t<=0xD3)||(t>=0xE0&&t<=0xE3);}
static inline int rc_is_condition(int t){return t>=0xD0;}
/* Parses codes into out (cap lines). Returns the line count (0 = none), or
 * -1 with a short reason in err ("Line 2: ..."). */
static inline int rc_parse(const char *text,RcLine *out,int cap,char *err,size_t errcap){
    int n=0,line=0;const char *p=text?text:"";if(err&&errcap)err[0]=0;
    while(*p){
        char digits[16];int nd=0,bad=0;
        while(*p&&*p!='\n'&&*p!='\r'&&*p!=';'&&*p!='|'){int c=(unsigned char)*p++;if(c==' '||c=='\t'||c==',')continue;if(rc_hex(c)<0){bad=1;continue;}if(nd<16)digits[nd]=(char)c;nd++;}
        if(*p)p++;
        if(!nd&&!bad)continue;line++;
        if(bad||nd!=12){if(err)snprintf(err,errcap,"Line %d: a code is 8 + 4 hex digits",line);return -1;}
        uint32_t w=0;uint16_t v=0;for(int i=0;i<8;i++)w=w<<4|(uint32_t)rc_hex(digits[i]);for(int i=8;i<12;i++)v=(uint16_t)(v<<4|rc_hex(digits[i]));
        int t=(int)(w>>24);uint32_t a=w&0xFFFFFF;
        if(!rc_known(t)){if(err)snprintf(err,errcap,"Line %d: type %02X is not supported",line,t);return -1;}
        if(a>=0x200000){if(err)snprintf(err,errcap,"Line %d: address is outside main RAM",line);return -1;}
        int wide=t==0x80||(t>=0xD0&&t<=0xD3);
        if(wide&&(a&1)){if(err)snprintf(err,errcap,"Line %d: 16-bit codes need an even address",line);return -1;}
        if(!wide&&v>0xFF){if(err)snprintf(err,errcap,"Line %d: 8-bit codes take 00vv",line);return -1;}
        if(n>=cap){if(err)snprintf(err,errcap,"At most %d lines per code",cap);return -1;}
        out[n].type=(uint8_t)t;out[n].addr=0x80000000u|a;out[n].value=v;n++;
    }
    if(n&&rc_is_condition(out[n-1].type)){if(err)snprintf(err,errcap,"Line %d: a condition needs a line after it",line);return -1;}
    return n;
}
/* Reformats valid codes as "TTAAAAAA VVVV;..." (the cheats.ini form). */
static inline void rc_format(const RcLine *l,int n,char *out,size_t cap){
    size_t k=0;if(cap)out[0]=0;
    for(int i=0;i<n&&k+15<cap;i++)k+=(size_t)snprintf(out+k,cap-k,"%s%02X%06X %04X",i?";":"",l[i].type,(unsigned)(l[i].addr&0xFFFFFF),l[i].value);
}
#ifdef __cplusplus
namespace rrpc {
/* Runtime (cheats_runtime.h, compiled into pc_services.cpp). */
bool cheats_active();            // any cheat on: achievements stay locked
void cheats_tick(bool racing);   // once per frame, from pc_tick
int cheat_count();               // built-ins
const char* cheat_name(int i);
bool cheat_on(int i);
bool cheat_toggle(int i);        // writes cheats.ini; false if it could not be saved
int custom_count();
int custom_on_count();
bool custom_toggle_all();        // all on if any is off, else all off
}
#endif
#endif
