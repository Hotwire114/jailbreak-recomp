/* Quick resume: player savestates from the in-game pause menu.
 * LGPL-2.1-or-later. Independently authored; no game code or data.
 *
 * Uses the framework's own savestate path (savestate.h): a request is staged
 * and runs at the next safe block boundary (savestate_poll), a load restores
 * the whole machine and resumes. Player slots live in <dir>/quick, where <dir>
 * is the runtime's savestate folder (saves/openbios): the folder is switched to
 * quick only while a staged request runs, then switched back, so the
 * developer slots 1-11 in saves/openbios are never written. Everything here
 * runs on the emulation thread (pc_tick and the PC menu's input hooks), the
 * same thread as savestate_poll.
 *
 * Next to each state, state_<entry>_slotNN.info records the race id, player
 * count, course name and time; the launcher's Saves page reads it too. */
#include "quick_resume.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#include "disc_reader.h"
#include "course_names.h"
#pragma clang diagnostic pop
#include "savestate.h"
#include "psx_netplay.h"
#include "mod_plugins.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" uint8_t* memory_get_ram_ptr(void);
namespace {
char home[512],root[512],token[32],quick[600];uint32_t wordsum,checksum,entry_pc;bool ready;
enum Op{NONE,SAVE,LOAD};Op staged=NONE,running=NONE;int op_slot;uint64_t stage_at,run_at;unsigned gen0;
struct Race{int race,players;};Race op_race;
struct Info{bool loaded,used;int race,players;long long saved;char course[32];};Info info[rrpc::quick_slots];
uint32_t word(uint32_t p){auto*r=memory_get_ram_ptr()+(p&0x1fffff);return r[0]|(uint32_t)r[1]<<8|(uint32_t)r[2]<<16|(uint32_t)r[3]<<24;}
bool setup(){
    if(ready)return true;const char*d=savestate_dir();if(!d||!d[0])return false;
    std::snprintf(home,sizeof home,"%s",d);std::snprintf(root,sizeof root,"%s",savestate_root_dir());std::snprintf(token,sizeof token,"%s",savestate_bios_token());
    wordsum=savestate_openbios_wordsum();savestate_get_integrity(&checksum,&entry_pc);
    if(std::snprintf(quick,sizeof quick,"%s/quick",home)>=(int)sizeof quick)return false;
    ready=true;return true;
}
// savestate_configure without a BIOS token uses the folder as is and keeps the
// remembered save root; with the token it rebuilds <root>/<token> (the start-up
// call; its one-time legacy migration is already marked done).
void use_quick(){savestate_configure(quick,checksum,entry_pc,nullptr,0);}
void use_home(){if(token[0]&&root[0])savestate_configure(root,checksum,entry_pc,token,wordsum);else savestate_configure(home,checksum,entry_pc,nullptr,0);}
void status(unsigned*gen,int*ok){char s[256];savestate_status_json(s,sizeof s);const char*g=std::strstr(s,"\"generation\":"),*k=std::strstr(s,"\"last_ok\":");*gen=g?(unsigned)std::strtoul(g+13,nullptr,10):0;*ok=k?std::atoi(k+10):0;}
void path(int slot,const char*ext,char*out,size_t cap){std::snprintf(out,cap,"%s/state_%08X_slot%02d.%s",quick,(unsigned)entry_pc,slot,ext);}
bool slot_exists(int slot){char p[700];path(slot,"pst",p,sizeof p);FILE*f=std::fopen(p,"rb");if(!f)return false;std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::fclose(f);return n>0;}
// The paused race: race config *(0x8005B2F8), flag byte 3 while paused,
// +0x30 player count, +0x40 race id (TRACK-MODDING.md 2.1).
bool race_now(Race*r){
    uint32_t cfg=word(0x8005B2F8);if(cfg<0x80010000u||cfg>=0x801FFF00u)return false;
    auto*m=memory_get_ram_ptr()+(cfg&0x1fffff);if(m[0]!=3&&m[0]!=1)return false;
    r->players=m[0x30];r->race=(int)word(cfg+0x40);return r->players>=1&&r->players<=2&&r->race>=1&&r->race<=64;
}
void course(int race,int players,char*out,size_t cap){
    wchar_t exe[32768],disc[32768];out[0]=0;DWORD n=GetModuleFileNameW(nullptr,exe,32768);if(!n||n>=32768)return;
    wchar_t*s=wcsrchr(exe,L'\\');if(!s)return;wcscpy(s+1,L"..\\game-disc.bin");if(!GetFullPathNameW(exe,32768,disc,nullptr))return;
    if(!rr_course_name(disc,race,players,out,cap))std::snprintf(out,cap,"Race %d",race);
}
void write_info(int slot){
    char p[700],name[32];path(slot,"info",p,sizeof p);course(op_race.race,op_race.players,name,sizeof name);
    FILE*f=std::fopen(p,"wb");if(!f)return;
    std::fprintf(f,"[quick]\nrace=%d\nplayers=%d\ncourse=%s\nsaved=%lld\n",op_race.race,op_race.players,name,(long long)std::time(nullptr));
    std::fclose(f);
}
const Info&slot_info(int slot){
    Info&i=info[slot];if(i.loaded)return i;i={};i.loaded=true;if(!setup())return i;
    i.used=slot_exists(slot);if(!i.used)return i;
    char p[700],line[128];path(slot,"info",p,sizeof p);FILE*f=std::fopen(p,"rb");if(!f)return i;
    while(std::fgets(line,sizeof line,f)){char*e=std::strchr(line,'=');if(!e)continue;*e++=0;e[std::strcspn(e,"\r\n")]=0;
        if(!std::strcmp(line,"race"))i.race=std::atoi(e);else if(!std::strcmp(line,"players"))i.players=std::atoi(e);
        else if(!std::strcmp(line,"saved"))i.saved=std::atoll(e);else if(!std::strcmp(line,"course"))std::snprintf(i.course,sizeof i.course,"%s",e);}
    std::fclose(f);return i;
}
bool busy(){return staged!=NONE||running!=NONE||savestate_pending();}
const char*refusal(){
    if(!setup()||savestate_title_blocked())return "NOT AVAILABLE";
    if(psx_netplay_active())return "NOT IN ONLINE PLAY";
    if(busy())return "PLEASE WAIT";
    return nullptr;
}
}
namespace rrpc {
bool quick_busy(){return busy();}
const char* quick_save(int slot){
    if(slot<0||slot>=quick_slots)return "NOT AVAILABLE";if(const char*r=refusal())return r;
    if(!race_now(&op_race))return "ONLY DURING A RACE";
    // A few frames after the PC menu closes, so the game has redrawn its own
    // pause menu: the slot thumbnail is taken from the displayed frame.
    staged=SAVE;op_slot=slot;stage_at=SDL_GetTicks()+120;psx_mod_counter_add("rr.quick.save_staged",1);return nullptr;
}
const char* quick_load(int slot){
    if(slot<0||slot>=quick_slots)return "NOT AVAILABLE";if(const char*r=refusal())return r;
    if(!slot_exists(slot))return "SLOT IS EMPTY";
    char reason[96];use_quick();int ok=savestate_slot_compatible(slot,reason,sizeof reason);use_home();
    if(!ok){std::fprintf(stderr,"quick resume: slot %d not loadable (%s)\n",slot+1,reason);return "OLDER GAME BUILD";}
    staged=LOAD;op_slot=slot;stage_at=SDL_GetTicks()+50;psx_mod_counter_add("rr.quick.load_staged",1);return nullptr;
}
void quick_label(int slot,char*out,size_t cap){
    if(slot<0||slot>=quick_slots||!cap)return;const Info&i=slot_info(slot);
    if(!i.used){std::snprintf(out,cap,"Empty");return;}
    char when[16]="";if(i.saved){std::time_t t=(std::time_t)i.saved,now=std::time(nullptr);std::tm a=*std::localtime(&t),b=*std::localtime(&now);
        if(a.tm_year==b.tm_year&&a.tm_yday==b.tm_yday)std::strftime(when,sizeof when,"%H:%M",&a);else std::strftime(when,sizeof when,"%d %b",&a);}
    char name[12];std::snprintf(name,sizeof name,"%s",i.course[0]?i.course:"Race");
    std::snprintf(out,cap,"%s%s %s",name,i.players==2?" 2P":"",when);
}
void quick_tick(){
    const uint64_t now=SDL_GetTicks();
    static bool boot_checked;
    if(!boot_checked&&setup()){boot_checked=true;
        // Launcher Saves > Quick resume: RR_QUICK_RESUME=<slot 1..4> for this run.
        if(const char*e=std::getenv("RR_QUICK_RESUME")){int n=std::atoi(e);
            if(n>=1&&n<=quick_slots&&!psx_netplay_active()&&slot_exists(n-1)){staged=LOAD;op_slot=n-1;stage_at=now;psx_mod_counter_add("rr.quick.resume_request",1);}
            else std::fprintf(stderr,"quick resume: RR_QUICK_RESUME=%s ignored\n",e);}}
    if(staged!=NONE&&now>=stage_at){
        int ok;status(&gen0,&ok);use_quick();
        ok=staged==SAVE?savestate_request_save(op_slot):savestate_request_load(op_slot);
        if(!ok){use_home();staged=NONE;psx_mod_counter_add("rr.quick.refused",1);return;}
        running=staged;staged=NONE;run_at=now;return;
    }
    if(running==NONE)return;
    // A save may wait up to 2 s for a safe resume point; a load has already
    // restored the machine (and resumed) by the time this runs again.
    if(savestate_pending()&&now-run_at<10000)return;
    use_home();unsigned gen;int ok;status(&gen,&ok);ok=ok&&gen!=gen0;
    if(running==SAVE&&ok)write_info(op_slot);
    std::fprintf(stderr,"quick resume: %s slot %d %s\n",running==SAVE?"save":"load",op_slot+1,ok?"ok":"FAILED");std::fflush(stderr);
    psx_mod_counter_add(running==SAVE?(ok?"rr.quick.saved":"rr.quick.save_failed"):(ok?"rr.quick.loaded":"rr.quick.load_failed"),1);
    info[op_slot].loaded=false;running=NONE;
}
}
