/* Online two-player (delay-sync lockstep), PC side. LGPL-2.1-or-later.
 * Original author code; no game code or data.
 *
 * Lockstep needs the same guest RAM, VRAM, SPU and cycle count on both PCs.
 * The PC layer writes game state from local choices (names, taunt voices,
 * rider look, display and timing settings) and from the wall clock; online:
 *  - Session setup: the launcher's Online page exchanges both players' rider
 *    name, taunt voice and rider look and the host's settings that change game
 *    state, and writes them to RR_NET_SESSION. Both PCs apply both seats (seat
 *    0 = host = Player 1). Five-O keeps the game's riders except the name, as
 *    offline (taunt_voices.cpp, rider_enhancements.cpp).
 *  - Guest clock: short windows use the game's VBlank counter instead of
 *    SDL_GetTicks (pc_ms), and the PC layer's periodic game-state writes (rider
 *    look, level of detail, draw distance, first-frame settings) run from the
 *    emulated VBlank (net_guest_tick) instead of the host's event poll.
 *  - Off online: the PC menu and its pad and menu-table writes (rider_menu.cpp),
 *    mods (mod_assets.cpp), cheats (cheats_runtime.h), button-name prompts
 *    (controls_screen.cpp), the intro-movie skip, live changes of aspect, frame
 *    rate, draw distance and texture correction; frame rate 120 becomes 60 (its
 *    host-speed fallback is wall-clock driven).
 *  - Content gate: this build's fingerprint (build_rr.py: recipe outputs,
 *    adapter sources, generated-unit list, compiler flags) and the session's
 *    timing settings fold into the framework's content fingerprint, so peers
 *    with different builds or settings never start (NETPLAY-PLAN.md R2, R13). */
#include "netplay_session.h"
#include "pc_services.h"
#include "owner_assets.h"
#include "mod_plugins.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
extern "C" uint8_t*memory_get_ram_ptr(void);
extern "C" int psx_netplay_active(void);
#ifndef RR_BUILD_FINGERPRINT
#define RR_BUILD_FINGERPRINT "unfingerprinted"
#endif
namespace {
constexpr uint32_t VBLANKS=0x8005B46C;   // gp+0x7E0, incremented by the VBlank handler 0x8001B700
rrpc::NetSession session;bool session_read;const rrpc::NetSession*session_ptr;
bool name_ok(const char*s){
    size_t n=std::strlen(s);if(!n||n>8)return false;bool letter=false;
    for(size_t i=0;i<n;i++){char c=s[i];if(c!=' ')letter=true;if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||std::strchr(" '-_.",c)))return false;}
    return letter;
}
int clamp_look(int kind,int v){return v>0&&v<rr_look_count(kind)?v:0;}
void read_session(){
    session_read=true;if(!rrpc::netplay())return;
    std::memset(&session,0,sizeof session);
    for(int i=0;i<2;i++){std::snprintf(session.seat[i].name,9,"Player%d",i+1);}
    session.motion=session.aspect=session.lod_percent=session.draw=session.fast_loading=session.larger_text=-1;   // -1: this PC's own
    session_ptr=&session;
    const wchar_t*path=_wgetenv(L"RR_NET_SESSION");
    if(!path||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES){std::fprintf(stderr,"rr netplay: no session file (RR_NET_SESSION); both riders stock\n");return;}
    for(int i=0;i<2;i++){wchar_t sec[8],text[64];std::swprintf(sec,8,L"seat%d",i+1);rrpc::NetSeat&s=session.seat[i];
        GetPrivateProfileStringW(sec,L"name",L"",text,64,path);char name[9]{};size_t len=std::wcslen(text);bool ascii=len<=8;
        for(size_t j=0;ascii&&j<len;j++){if(text[j]>127)ascii=false;else name[j]=(char)text[j];}
        if(ascii&&name_ok(name))std::memcpy(s.name,name,9);
        s.rename=GetPrivateProfileIntW(sec,L"rename",0,path)!=0;
        s.voice=GetPrivateProfileIntW(sec,L"voice",0,path);if(s.voice<0||s.voice>27)s.voice=0;
        s.paint=clamp_look(0,GetPrivateProfileIntW(sec,L"paint",0,path));s.tag=clamp_look(1,GetPrivateProfileIntW(sec,L"tag",0,path));s.outfit=clamp_look(2,GetPrivateProfileIntW(sec,L"outfit",0,path));s.colour=clamp_look(3,GetPrivateProfileIntW(sec,L"colour",0,path));
        for(int r=0;r<3;r++){wchar_t key[12];std::swprintf(key,12,L"bike_r%d",r);wchar_t v[12];GetPrivateProfileStringW(sec,key,L"",v,12,path);s.bike[r]=v[0]?_wtoi(v):-1;}}
    auto host=[&](const wchar_t*key,int lo,int hi){int v=GetPrivateProfileIntW(L"host",key,-1,path);return v>=lo&&v<=hi?v:-1;};
    session.race=std::max(0,host(L"race",1,18));session.seed=GetPrivateProfileIntW(L"host",L"seed",1,path);
    session.motion=host(L"native_60fps",0,2);if(session.motion==2)session.motion=1;   // 120 FPS: see the header
    session.aspect=host(L"aspect",0,2);session.lod_percent=host(L"lod_percent",100,10000);session.draw=host(L"draw_distance",0,15);
    session.fast_loading=host(L"fast_loading",0,1);session.larger_text=host(L"larger_text",0,1);
    std::fprintf(stderr,"rr netplay: session P1 \"%s\" voice %d look %d/%d/%d, P2 \"%s\" voice %d look %d/%d/%d; host frame rate %d aspect %d lod %d%% draw %d fast loading %d larger text %d\n",
        session.seat[0].rename?session.seat[0].name:"-",session.seat[0].voice,session.seat[0].paint,session.seat[0].tag,session.seat[0].outfit,
        session.seat[1].rename?session.seat[1].name:"-",session.seat[1].voice,session.seat[1].paint,session.seat[1].tag,session.seat[1].outfit,
        session.motion,session.aspect,session.lod_percent,session.draw,session.fast_loading,session.larger_text);
}
bool sha256_hex(const std::string&in,char out[65]){
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE h=nullptr;UCHAR d[32];bool ok=false;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0){
        if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)==0){
            ok=BCryptHashData(h,(PUCHAR)in.data(),(ULONG)in.size(),0)==0&&BCryptFinishHash(h,d,32,0)==0;BCryptDestroyHash(h);}
        BCryptCloseAlgorithmProvider(alg,0);}
    if(ok)for(int i=0;i<32;i++)std::snprintf(out+2*i,3,"%02x",d[i]);
    return ok;
}
}
namespace rrpc {
// RR_NETPLAY_TEST=1: the PC layer's online behaviour without a session (the
// framework refuses PSX_NETPLAY=1 in a build without its netplay libraries).
bool netplay(){static int env=-1;if(env<0){const char*e=std::getenv("PSX_NETPLAY"),*t=std::getenv("RR_NETPLAY_TEST");env=(e&&e[0]&&e[0]!='0')||(t&&t[0]=='1');}return env||psx_netplay_active();}
const NetSession* net_session(){if(!session_read)read_session();return session_ptr;}
uint32_t guest_vblanks(){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(VBLANKS&0x1fffff),4);return v;}
uint64_t pc_ms(){return netplay()?(uint64_t)guest_vblanks()*50/3:GetTickCount64();}
int races_done;   // results screens seen this session (race_results_tick)
// Course, rank and bikes from the room (lobby_page.h vote, owner 5 Nov): on the
// multiplayer setup screens (Skull-to-Skull and Cops & Robbers, Player 1 and 2:
// menus 0x8009917C / 0x80099190 / 0x800991A4 / 0x800991B8) the frontend's rank
// (0x800D80DC: 0 Grunt, 2 Lieutenant, 4 Captain; its row's index 0x8008709B), race id
// (0x800D80E0, its row's index 0x80088023 = race - 1: rank n
// has races 6n/2+1 .. +6) and each player's bike (per-player settings
// 0x800D81D8 + 36n, +7; rank 0: 0 3 9 12, rank 2: 1 4 10 13, rank 4: 2 5 11
// 14) are held every VBlank, so both PCs race what the room chose. The first
// race is the vote's; each later one a new random course and rank from the
// shared seed. A player's preset bike for the rank, else a random one of its four.
const uint8_t rank_bikes[3][4]={{0,3,9,12},{1,4,10,13},{2,5,11,14}};
uint32_t mix32(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
void setup_tick(){
    const auto*s=rrpc::net_session();if(!s||s->race<1)return;
    uint32_t menu;std::memcpy(&menu,memory_get_ram_ptr()+(0x8009C5C8&0x1fffff),4);
    if(menu!=0x8009917Cu&&menu!=0x80099190u&&menu!=0x800991A4u&&menu!=0x800991B8u)return;
    const uint32_t h=mix32((uint32_t)s->seed*0x9E3779B1u+(uint32_t)races_done);
    const int race=races_done==0?s->race:1+(int)(h%18u),rank=(race-1)/6;
    // The menu rebuilds the race id from its rows' list indices every frame (same-PC test, 5 Nov:
    // only the rank byte held, the race fell back to the rank's first course), so those too:
    // the Rank row's index 0x8008709B (0-2) and the Course row's 0x80088023 (race id - 1).
    psx_mod_write_byte(0x8008709B,(uint8_t)rank);psx_mod_write_byte(0x80088023,(uint8_t)(race-1));
    psx_mod_write_byte(0x800D80DC,(uint8_t)(rank*2));psx_mod_write_byte(0x800D80E0,(uint8_t)race);
    for(int seat=0;seat<2;seat++){int b=s->seat[seat].bike[rank];bool ok=false;for(int k=0;k<4;k++)ok|=rank_bikes[rank][k]==b;
        if(!ok)b=rank_bikes[rank][mix32(h+0x51ED27u*(seat+1))%4u];
        psx_mod_write_byte(0x800D81D8+36*seat+7,(uint8_t)b);
        // Each player's Bike row rebuilds the bike from its index (Player 1 0x800880BB, Player 2
        // 0x800880C3), a list of the twelve solo bikes in id order, the sidecars 6-8 left out.
        psx_mod_write_byte(seat?0x800880C3:0x800880BB,(uint8_t)(b<6?b:b-3));}
}
// Online race results, for the launcher's ratings (lobby_page.h lb_mmr_apply):
// how far along the course each seat's rider got (rider record +0xC0, which
// orders the riders exactly as the HUD's places; riders *0x8005B3A0, stride
// 0x448; seat 0 / 1 records 0 / 1, takeover seats 2-7 records 2, 4 .. 12 as
// rider_takeover.cpp target_of). The last values while racing (race flag
// *(0x8005B2F8) 1) are written when the results show (6): one line per race,
// "race <mode> <d0> .. <d7>", to RR_NET_SESSION + ".results". Every PC writes
// the same numbers: the game state is the same on all of them.
void race_results_tick(){
    auto*r=memory_get_ram_ptr();uint32_t cfg;std::memcpy(&cfg,r+(0x8005B2F8&0x1fffff),4);if((cfg&0xffe00000)!=0x80000000)return;
    static bool raced;static int mode;static int32_t got[8];const int flag=r[cfg&0x1fffff];
    if(flag==1){uint32_t base;std::memcpy(&base,r+(0x8005B3A0&0x1fffff),4);if((base&0xffe00000)!=0x80000000||(base&0x1fffff)+16*0x448>0x200000)return;
        raced=true;mode=r[(cfg+4)&0x1fffff];
        for(int s=0;s<8;s++){const int rec=s<2?s:2+2*(s-2);std::memcpy(&got[s],r+((base+rec*0x448+0xC0)&0x1fffff),4);}
        return;}
    if(flag!=6||!raced)return;raced=false;
    const wchar_t*session=_wgetenv(L"RR_NET_SESSION");if(!session||!*session)return;
    std::wstring path=std::wstring(session)+L".results";
    races_done++;
    if(FILE*f=_wfopen(path.c_str(),L"a")){std::fprintf(f,"race %d",mode);for(int s=0;s<8;s++)std::fprintf(f," %ld",(long)got[s]);std::fprintf(f,"\n");std::fclose(f);}
    psx_mod_counter_add("rr.net.race_result",1);
}
void net_guest_tick(){
    if(!netplay())return;
    static uint32_t last;static bool started;uint32_t v=guest_vblanks();
    if(!started){if(!v)return;   // the game's VBlank handler runs: past the BIOS boot
        started=true;last=v;pc_guest_start();psx_mod_counter_add("rr.net.guest_start",1);
        std::fprintf(stderr,"rr netplay: PC layer on the guest clock from VBlank %u\n",v);return;}
    if(v==last)return;last=v;
    race_results_tick();
    setup_tick();
    // Rider look every VBlank: paint and outfit only write when the game's
    // palette or model differs, so a palette the game uploads again is painted
    // again within the frame instead of showing stock for up to 12 frames.
    look_guest_tick();
    if(v%12)return;   // tables five times a second, as the host-poll ticks did
    pc_guest_tick();psx_mod_counter_add("rr.net.guest_ticks",1);
}
}
extern "C" void rr_netplay_fold_fingerprint(char*fp){
    const rrpc::NetSession*s=rrpc::net_session();auto&g=rrpc::graphics_options();
    char settings[160];
    std::snprintf(settings,sizeof settings,"motion=%d aspect=%d lod=%d draw=%d fast_loading=%d larger_text=%d",
        s&&s->motion>=0?s->motion:g.motion,s&&s->aspect>=0?s->aspect:g.aspect,s&&s->lod_percent>=0?s->lod_percent:-1,
        s&&s->draw>=0?s->draw:g.draw,s&&s->fast_loading>=0?s->fast_loading:-1,s&&s->larger_text>=0?s->larger_text:-1);
    // Test and A/B switches that change guest timing or game-state writes.
    static const char*const env[]={"RR_FAST_DIV","RR_FAST_MODE","RR_PERF","RR_BG_GUARD","RR_MENU60","RR_SKY_EXTEND","RR_MENU_TITLES","RR_LOD_SCALE",
        "RR_DRAW_SCALE","RR_DISC_SPEED","RR_LOAD_ACCEL","RR_TEXT_FONT","RR_VOICE_TEST_KIND","RR_CLEAR_TEXT"};
    std::string in=std::string(fp)+"|"+RR_BUILD_FINGERPRINT+"|"+settings;
    for(const char*name:env)if(const char*v=std::getenv(name)){in+="|";in+=name;in+="=";in+=v;}
    char out[65];if(!sha256_hex(in,out))return;   // the framework's own fingerprint stays
    std::memcpy(fp,out,65);
    std::fprintf(stderr,"rr netplay: content fingerprint %s (build %s; %s)\n",fp,RR_BUILD_FINGERPRINT,settings);
}
