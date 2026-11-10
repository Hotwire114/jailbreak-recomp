/* Cheats, runtime side (formats and verified addresses: cheat_codes.h).
 * LGPL-2.1-or-later. Original author code. Included once, at the end of
 * pc_services.cpp; pc_tick calls cheats_tick every frame.
 *
 * cheats.ini (beside launcher.ini) is read at start-up and again whenever
 * its time stamp changes (checked twice a second), so the launcher and the
 * in-game menu can switch cheats while the game runs. Writes go through the
 * CPU store path (psx_mod_host_write_ram) and only when the value differs,
 * so a code that patches game code is handled like a store by the game.
 *
 * - Built-in race codes run only while racing (race flag 1) and only when
 *   the race layout is the one they were verified on: race context at
 *   0x800D5D38, the first actor is rider 0 (Player 1) with its record at
 *   0x800D5758. Otherwise nothing is written (counter rr.cheat.layout_skip).
 * - Unlock toggles OR the game's own cheat bits into 0x8005AE7C (only on the
 *   disc whose code table at 0x800540B8 points at "KLFSDA"); switching one
 *   off clears only a bit this code set.
 * - Custom codes run every frame, in menus too, like a GameShark. */
#pragma once
#include "cheat_codes.h"
namespace {
struct RcCustom {RcLine line[RC_MAX_LINES];int n;bool on;};
struct RcState {
    bool init,on[RC_CHEATS];wchar_t path[32768];ULONGLONG stamp;uint64_t checked;
    RcLine race[RC_RACE_CHEATS][4];int race_n[RC_RACE_CHEATS];RcCustom custom[RC_MAX_CUSTOM];int custom_n;unsigned unlock_ours;
} rc;
uint32_t rc_word(uint32_t a){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(a&0x1ffffc),4);return v;}
ULONGLONG rc_file_stamp(){WIN32_FILE_ATTRIBUTE_DATA d;if(!GetFileAttributesExW(rc.path,GetFileExInfoStandard,&d))return 0;return (ULONGLONG)d.ftLastWriteTime.dwHighDateTime<<32|d.ftLastWriteTime.dwLowDateTime;}
void rc_read(){
    rc.stamp=rc_file_stamp();
    for(int i=0;i<RC_CHEATS;i++){wchar_t id[32];MultiByteToWideChar(CP_UTF8,0,rc_cheats[i].id,-1,id,32);rc.on[i]=GetPrivateProfileIntW(L"cheats",id,0,rc.path)!=0;}
    int n=GetPrivateProfileIntW(L"custom",L"count",0,rc.path);if(n<0)n=0;if(n>RC_MAX_CUSTOM)n=RC_MAX_CUSTOM;rc.custom_n=0;
    for(int k=1;k<=n;k++){wchar_t key[16],text[RC_TEXT];char utf[RC_TEXT];RcCustom&c=rc.custom[rc.custom_n];
        std::swprintf(key,16,L"code%d",k);GetPrivateProfileStringW(L"custom",key,L"",text,RC_TEXT,rc.path);WideCharToMultiByte(CP_UTF8,0,text,-1,utf,RC_TEXT,nullptr,nullptr);
        std::swprintf(key,16,L"on%d",k);c.on=GetPrivateProfileIntW(L"custom",key,0,rc.path)!=0;
        c.n=rc_parse(utf,c.line,RC_MAX_LINES,nullptr,0);if(c.n>0)rc.custom_n++;else psx_mod_counter_add("rr.cheat.custom_invalid",1);}
    psx_mod_counter_add("rr.cheat.loaded",1);
}
void rc_load(){
    if(rc.init)return;rc.init=true;
    DWORD n=GetModuleFileNameW(nullptr,rc.path,32768);auto*slash=n&&n<32768?std::wcsrchr(rc.path,L'\\'):nullptr;
    if(!slash){rc.path[0]=0;return;}wchar_t rel[32768];std::wcscpy(slash+1,L"..\\cheats.ini");std::wcscpy(rel,rc.path);GetFullPathNameW(rel,32768,rc.path,nullptr);
    for(int i=0;i<RC_RACE_CHEATS;i++)rc.race_n[i]=rc_parse(rc_cheats[i].codes,rc.race[i],4,nullptr,0);
    rc_read();
}
void rc_store(uint32_t a,const void*v,uint32_t bytes){
    if(std::memcmp(memory_get_ram_ptr()+(a&0x1fffff),v,bytes)){psx_mod_host_write_ram(a,v,bytes);psx_mod_counter_add("rr.cheat.writes",1);}
}
bool rc_test(const RcLine&c){
    auto*r=memory_get_ram_ptr()+(c.addr&0x1fffff);uint16_t v=c.type<0xE0?(uint16_t)(r[0]|r[1]<<8):r[0];
    switch(c.type&3){case 0:return v==c.value;case 1:return v!=c.value;case 2:return v<c.value;default:return v>c.value;}
}
void rc_run(const RcLine*l,int n){
    for(int i=0;i<n;i++){const RcLine&c=l[i];
        if(rc_is_condition(c.type)){if(!rc_test(c)){int j=i+1;while(j<n&&rc_is_condition(l[j].type))j++;i=j;}continue;}
        if(c.type==0x80){uint8_t b[2]={(uint8_t)c.value,(uint8_t)(c.value>>8)};rc_store(c.addr,b,2);}else{uint8_t b=(uint8_t)c.value;rc_store(c.addr,&b,1);}
    }
}
// The layout the built-in race codes were verified on (cheat_codes.h).
bool rc_race_layout(){
    uint32_t ctx=rc_word(0x8005B2F8),base=rc_word(0x8005B3A0);
    if(ctx!=0x800D5D38u||memory_get_ram_ptr()[ctx&0x1fffff]!=1)return false;
    if(base<0x80010000u||base+0x448>0x80200000u)return false;
    return rc_word(base+0x43C)==0x800D5758u&&(rc_word(base+0xAC)&0xffff)==0;
}
bool rc_save_int(const char*section,const char*key,int v){wchar_t s[32],k[64],t[16];MultiByteToWideChar(CP_UTF8,0,section,-1,s,32);MultiByteToWideChar(CP_UTF8,0,key,-1,k,64);std::swprintf(t,16,L"%d",v);
    bool ok=WritePrivateProfileStringW(s,k,t,rc.path)!=0;WritePrivateProfileStringW(nullptr,nullptr,nullptr,rc.path);rc.stamp=rc_file_stamp();return ok;}
}
namespace rrpc {
bool cheats_active(){rc_load();for(int i=0;i<RC_CHEATS;i++)if(rc.on[i])return true;for(int k=0;k<rc.custom_n;k++)if(rc.custom[k].on)return true;return false;}
void cheats_tick(bool racing){
    rc_load();if(!rc.path[0])return;
    const uint64_t now=SDL_GetTicks();if(now-rc.checked>=500){rc.checked=now;if(rc_file_stamp()!=rc.stamp)rc_read();}
    bool race_on=false;for(int i=0;i<RC_RACE_CHEATS;i++)race_on|=rc.on[i];
    if(race_on&&racing){if(rc_race_layout()){for(int i=0;i<RC_RACE_CHEATS;i++)if(rc.on[i])rc_run(rc.race[i],rc.race_n[i]);psx_mod_counter_add("rr.cheat.race_frames",1);}
        else psx_mod_counter_add("rr.cheat.layout_skip",1);}
    unsigned want=0;for(int i=RC_RACE_CHEATS;i<RC_CHEATS;i++)if(rc.on[i])want|=rc_cheats[i].unlock;
    if((want||rc.unlock_ours)&&rc_word(0x800540B8)==0x8005AEB0u&&!std::memcmp(memory_get_ram_ptr()+0x5AEB0,"KLFSDA",6)){
        uint32_t cur=rc_word(RC_UNLOCK_WORD),drop=rc.unlock_ours&~want,next=(cur|want)&~drop;
        rc.unlock_ours=(rc.unlock_ours|(want&~cur))&~drop;
        if(next!=cur){uint8_t b[4]={(uint8_t)next,(uint8_t)(next>>8),(uint8_t)(next>>16),(uint8_t)(next>>24)};rc_store(RC_UNLOCK_WORD,b,4);}}
    for(int k=0;k<rc.custom_n;k++)if(rc.custom[k].on)rc_run(rc.custom[k].line,rc.custom[k].n);
}
int cheat_count(){return RC_CHEATS;}
const char* cheat_name(int i){return i>=0&&i<RC_CHEATS?rc_cheats[i].name:"";}
bool cheat_on(int i){rc_load();return i>=0&&i<RC_CHEATS&&rc.on[i];}
bool cheat_toggle(int i){rc_load();if(i<0||i>=RC_CHEATS||!rc.path[0])return false;bool next=!rc.on[i];if(!rc_save_int("cheats",rc_cheats[i].id,next))return false;rc.on[i]=next;psx_mod_counter_add("rr.cheat.toggled",1);return true;}
int custom_count(){rc_load();return rc.custom_n;}
int custom_on_count(){rc_load();int n=0;for(int k=0;k<rc.custom_n;k++)n+=rc.custom[k].on;return n;}
bool custom_toggle_all(){
    rc_load();if(!rc.custom_n||!rc.path[0])return false;bool next=custom_on_count()<rc.custom_n;
    // cheats.ini numbers valid codes among possibly invalid ones: switch every saved slot.
    int n=GetPrivateProfileIntW(L"custom",L"count",0,rc.path);bool ok=true;
    for(int k=1;k<=n&&k<=RC_MAX_CUSTOM;k++){char key[16];std::snprintf(key,16,"on%d",k);ok=rc_save_int("custom",key,next)&&ok;}
    if(ok)for(int k=0;k<rc.custom_n;k++)rc.custom[k].on=next;return ok;
}
}
