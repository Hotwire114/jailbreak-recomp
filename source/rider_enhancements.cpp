/* Independently authored presentation extensions, LGPL-2.1-or-later.
 * Native font functions and all glyphs are compiled/read from the owner's
 * game. This source contains addresses/interfaces, not game instructions.
 * Options are read beside the local executable. Original has no options file.
 */
#include "cpu_state.h"
#include <algorithm>
#include <utility>
#include "mod_plugins.h"
#include "rider_enhancements.h"
#include "native_ui.h"
#include "taunt_voices.h"
#include "controls_screen.h"
#include "owner_assets.h"
#include "netplay_session.h"
#include "game_editor.h"
#include "demo_courses.h"
#include "showroom.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <cmath>
extern "C" void __real_func_8002CDC8(CPUState*);
extern "C" void __real_debug_server_cyc_observe(uint32_t);
extern "C" CPUState* debug_cpu_ptr;
extern "C" uint8_t* memory_get_ram_ptr(void);
static rrpc::RiderOptions options;static bool initialized;
static wchar_t options_path[32768],launcher_path[32768];
struct FontSpan {uint32_t sp,start,arena;int x,y;};
static FontSpan spans[16];
static bool name_valid(const char*s){
    size_t n=std::strlen(s);if(!n||n>8)return false;bool letter=false;
    for(size_t i=0;i<n;i++){char c=s[i];if(c!=' ')letter=true;
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||std::strchr(" '-_.",c)))return false;
    }return letter;
}
static void load_options(){
    if(initialized)return;initialized=true;
    wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return;
    wchar_t*slash=wcsrchr(path,L'\\');if(!slash)return;wcscpy(slash+1,L"rider-options.ini");
    wcscpy(options_path,path);wcscpy(slash+1,L"..\\launcher.ini");GetFullPathNameW(path,32768,launcher_path,nullptr);
    options.menu=GetPrivateProfileIntW(L"pc",L"in_game_menu",0,options_path)!=0;
    if(!options.menu){options.original_rate=GetPrivateProfileIntW(L"original",L"frame_rate",0,options_path);if(options.original_rate<0||options.original_rate>2)options.original_rate=0;
        options.original_quit=GetPrivateProfileIntW(L"original",L"esc_quits",0,options_path)!=0;}
    options.names=GetPrivateProfileIntW(L"riders",L"rename",0,options_path)!=0;
    options.hd=GetPrivateProfileIntW(L"display",L"hd_ui",1,launcher_path)!=0;
    for(int i=0;i<2;i++){wchar_t key[12];swprintf(key,12,L"voice%d",i+1);options.voice[i]=GetPrivateProfileIntW(L"audio",key,0,launcher_path);if(options.voice[i]<0||options.voice[i]>27)options.voice[i]=0;}
    options.large=GetPrivateProfileIntW(L"hud",L"larger_text",0,options_path)!=0;
    for(int i=0;i<2;i++){const wchar_t*keys[]={L"paint",L"tag",L"outfit",L"colour"};int*values[]={options.paint,options.tag,options.outfit,options.colour};
        for(int k=0;k<RR_LOOK_KINDS;k++){wchar_t key[12];swprintf(key,12,L"%ls%d",keys[k],i+1);int v=GetPrivateProfileIntW(L"riders",key,0,launcher_path);values[k][i]=v>0&&v<rr_look_count(k)?v:0;}}
    for(unsigned i=0;i<2;i++){
        wchar_t text[64],key[8];swprintf(key,8,L"name%u",i+1);
        GetPrivateProfileStringW(L"riders",key,i?L"Player2":L"Player1",text,64,options_path);size_t len=wcslen(text);
        if(len>8)continue;bool ascii=true;for(size_t j=0;j<len;j++){if(text[j]>127)ascii=false;options.name[i][j]=(char)text[j];}
        if(!ascii||!name_valid(options.name[i]))options.name[i][0]=0;
    }
    // Online: both seats' names, voices and looks and the host's larger labels,
    // from the session setup both PCs exchanged (netplay_session.cpp).
    if(rrpc::netplay())if(const auto*s=rrpc::net_session()){
        options.names=s->seat[0].rename||s->seat[1].rename;
        for(int i=0;i<2;i++){const auto&seat=s->seat[i];std::memset(options.name[i],0,9);if(seat.rename)std::memcpy(options.name[i],seat.name,9);
            options.voice[i]=seat.voice;options.paint[i]=seat.paint;options.tag[i]=seat.tag;options.outfit[i]=seat.outfit;options.colour[i]=seat.colour;}
        if(s->larger_text>=0)options.large=s->larger_text!=0;
    }
    rrpc::native_ui_config(options.menu,options.hd);
#ifdef RR_PRIVATE_EXPERIMENT
    // Private test binaries can compare options without modifying installs.
    options.large=getenv("RR_CLEAR_TEXT")&&*getenv("RR_CLEAR_TEXT")=='1';
    for(unsigned i=0;i<2;i++){const char*p=getenv(i?"RR_RIDER_NAME2":"RR_RIDER_NAME1");if(p&&name_valid(p)){options.names=true;std::strcpy(options.name[i],p);}}
#endif
}
namespace rrpc {
RiderOptions& rider_options(){load_options();return options;}
bool save_rider_options(const RiderOptions&next){
    if(netplay())return false;   // online the session's choices apply; settings change between sessions
    if(!options.menu||!options_path[0]||!launcher_path[0])return false;
    for(unsigned i=0;i<2;i++)if(!name_valid(next.name[i]))return false;
    bool ok=true;const wchar_t*paths[]={options_path,launcher_path};
    for(auto path:paths){
        ok=WritePrivateProfileStringW(L"display",L"hd_ui",next.hd?L"1":L"0",path)&&ok;
        for(int i=0;i<2;i++){wchar_t key[12],value[12];swprintf(key,12,L"voice%d",i+1);swprintf(value,12,L"%d",next.voice[i]);ok=WritePrivateProfileStringW(L"audio",key,value,path)&&ok;}
        ok=WritePrivateProfileStringW(L"hud",L"larger_text",next.large?L"1":L"0",path)&&ok;
        ok=WritePrivateProfileStringW(L"riders",L"rename",next.names?L"1":L"0",path)&&ok;
        for(int i=0;i<2;i++){const wchar_t*keys[]={L"paint",L"tag",L"outfit",L"colour"};const int*values[]={next.paint,next.tag,next.outfit,next.colour};
            for(int k=0;k<RR_LOOK_KINDS;k++){wchar_t key[12],value[12];swprintf(key,12,L"%ls%d",keys[k],i+1);swprintf(value,12,L"%d",values[k][i]);ok=WritePrivateProfileStringW(L"riders",key,value,path)&&ok;}}
        for(unsigned i=0;i<2;i++){wchar_t key[8],text[9]{};swprintf(key,8,L"name%u",i+1);for(unsigned j=0;next.name[i][j];j++)text[j]=next.name[i][j];ok=WritePrivateProfileStringW(L"riders",key,text,path)&&ok;}
        // Flushing a profile cache has no string-copy result to validate.
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path);
    }if(ok)rrpc::native_ui_config(next.menu,next.hd);return ok;
}
}
static bool range(uint32_t p,uint32_t n){uint32_t o=p&0x1fffffff;
    return (p<0x200000u||(p>=0x80000000u&&p<0x80200000u)||(p>=0xA0000000u&&p<0xA0200000u))&&n<=0x200000u-o;
}
static uint32_t word(uint32_t p){p&=0x1fffff;auto*r=memory_get_ram_ptr();return r[p]|(uint32_t)r[p+1]<<8|(uint32_t)r[p+2]<<16|(uint32_t)r[p+3]<<24;}
static int16_t half(uint32_t p){p&=0x1fffff;auto*r=memory_get_ram_ptr();return (int16_t)(r[p]|(uint16_t)r[p+1]<<8);}
static void put_half(uint32_t p,int v){p&=0x1fffff;auto*r=memory_get_ram_ptr();r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
static bool race(){uint32_t c=word(0x8005B2F8);return c>=0x80010000u&&range(c,0x40)&&memory_get_ram_ptr()[c&0x1fffff]==1;}
static uint8_t byte_at(uint32_t p){return memory_get_ram_ptr()[p&0x1fffff];}
// Custom names wherever a player is named (Custom names on). All text goes
// through 0x8002CDC8 (string pointer in a1); the string-id wrappers 0x8002CD78
// and 0x8002CB08 keep their caller at sp+0x18 / sp+0x2C.
//  Race overlay (RASHCDG, races and their results): the Player 1/2 strings
//  (ids 30/31 race names, 162/163 result rows, 171/172 two-player tables), the
//  names under the health bars (0x80061EE4 / 0x8005FB3C, also while paused) and
//  the player rows of the standings (0x800C5DF0: record+0x26, which for a player
//  is Player n, a Jail Break alias or a Five-O name). They are drawn from the
//  nine-byte Player 1/2 slots, which hold the custom name; actor IDs and
//  profiles stay stock.
//  Frontend (RASHCDF): the profile alias (status page 0x800746CC, menu panel
//  0x80072A3C); the save list (0x80071DEC) keeps the riders' names. The name is
//  written over the drawn string for that call only (lend_name/give_back).
//  Trophy Room: a Time Trial record's name entry starts with the custom name.
static bool race_overlay(){return word(0x800C6E98)==0x24A500A2u&&word(0x800C5F9C)==0x240500A9u;}
static bool frontend_overlay(){return word(0x80069D98)==0x25058C84u;}
static uint32_t text_origin(CPUState*c){
    uint32_t ra=c->gpr[31],sp=c->gpr[29];
    if(ra==0x8002CDB8&&range(sp+0x18,4))return word(sp+0x18);
    if((ra==0x8002CB88||ra==0x8002CC20)&&range(sp+0x2C,4))return word(sp+0x2C);
    return ra;
}
// The seat whose name this draw shows, or -1.
static int named_seat(CPUState*c,uint32_t origin,uint32_t table){
    uint32_t cfg=word(0x8005B2F8),a1=c->gpr[5];unsigned players=range(cfg,0x34)?word(cfg+0x30):0;
    if(race_overlay()){
        if(origin==0x80061EE4)return c->gpr[16]<players?(int)c->gpr[16]:-1;
        if(origin==0x8005FB3C)return byte_at(cfg+6)<players?byte_at(cfg+6):-1;
        if(!range(table,173*4))return -1;
        if(origin==0x800C5FDC){
            // Team modes name the team here (169/170). The caller's s4 (saved by
            // 0x8002CB08 at sp+40) is the rider.
            uint32_t rider=word(c->gpr[29]+40);unsigned seat=range(rider,0x448)?(uint16_t)half(rider+0xAC):99;
            return seat<players&&a1!=word(table+169*4)&&a1!=word(table+170*4)?(int)seat:-1;
        }
        static const unsigned ids[]={30,162,171};
        for(int seat=0;seat<2;seat++)for(unsigned id:ids)if(a1==word(table+(id+seat)*4))return seat;
        return -1;
    }
    // The save list's profile names (0x80071DEC) stay the game's own: in Jail
    // Break each profile is named after the rider chosen for it (Switch, Kat ...),
    // which tells the saves apart; the custom name would make them all alike.
    if(frontend_overlay()&&((origin==0x800746CC&&word(0x80074674)==0x240500DAu)||
                            (origin==0x80072A3C&&word(0x800729FC)==0x0C01CAA0u)))return 0;
    return -1;
}
// Race overlay: the Player 1/2 slot holding the custom name (or, with names
// off, the stock text again where this code wrote a name).
static uint32_t written_at[2];static char written[2][9];
static uint32_t race_name(uint32_t table,int seat){
    uint32_t text=word(table+(30+seat)*4);if(!range(text,9))return 0;
    auto*r=memory_get_ram_ptr()+(text&0x1fffff);size_t len=std::strlen(options.name[seat]);
    if(std::memcmp(r,options.name[seat],len+1)){std::memset(r,0,9);std::memcpy(r,options.name[seat],len);}
    written_at[seat]=text;std::memcpy(written[seat],r,9);return text;
}
static void stock_names(uint32_t table){
    for(int seat=0;seat<2;seat++){uint32_t text=written_at[seat];
        if(!text||!range(table,32*4)||word(table+(30+seat)*4)!=text||!range(text,9))continue;
        auto*r=memory_get_ram_ptr()+(text&0x1fffff);
        if(!std::memcmp(r,written[seat],9)){char stock[9]="Player 1";stock[7]=(char)('1'+seat);std::memcpy(r,stock,9);}
        written_at[seat]=0;}
}
// Frontend: the name over the drawn string, the original bytes back once the
// call has returned (after the HD-font pass has read the text).
struct Loan{uint32_t at=0,sp=0;unsigned n=0;uint8_t saved[16];};static Loan loan;
static void give_back(){if(loan.at){std::memcpy(memory_get_ram_ptr()+(loan.at&0x1fffff),loan.saved,loan.n);loan={};}}
static bool lend_name(CPUState*c,int seat){
    uint32_t at=c->gpr[5];size_t len=std::strlen(options.name[seat]);if(!range(at,16))return false;
    give_back();auto*r=memory_get_ram_ptr()+(at&0x1fffff);size_t old=0;while(old<15&&r[old])old++;
    loan.at=at;loan.sp=c->gpr[29];loan.n=(unsigned)(old>len?old:len)+1;std::memcpy(loan.saved,r,loan.n);
    std::memset(r,0,loan.n);std::memcpy(r,options.name[seat],len);return true;
}
// Trophy Room name entry (RASHCDF): a Time Trial time that makes the course's
// top eight is named on a keyboard (buffer 0x80088C84, eight characters, '_'
// empty; cursor 0x80088DF0); END stores it in the record (0x80069BF0, spaces
// for '_'). When that keyboard appears untouched it starts with the custom
// name, cursor after it; the player can still change it. The same keyboard on
// the code-entry page (0x80069618) is left alone: this needs the pending record
// (course 0x800D80E0, player 0x800D80EB, time 0x800D81D8 + 36n, beating the
// course's eighth time at 0x80053A88 + 0xB0 * course + 0xA8).
static void record_name_entry(uint32_t origin){
    static uint64_t last,filled_at;static char filled[8];static uint32_t filled_len;
    if(origin!=0x800757C8||word(0x800757C0)!=0x0C00B2C2u||!frontend_overlay())return;   // the keyboard's SPC label
    auto*buffer=memory_get_ram_ptr()+(0x80088C84&0x1fffff);
    uint64_t now=rrpc::pc_ms();bool fresh=now-last>400;last=now;   // guest clock online
    if(!fresh){
        // The page resets its cursor when it starts; keep it after the name.
        if(filled_len&&now-filled_at<1000&&!word(0x80088DF0)&&!std::memcmp(buffer,filled,8))psx_mod_write_word(0x80088DF0,filled_len);
        return;
    }
    filled_len=0;
    unsigned seat=byte_at(0x800D80EB);int course=(int8_t)byte_at(0x800D80E0)-56;
    if(seat>1||!options.name[seat][0]||course<0||course>8)return;
    uint32_t time=word(0x800D81D8+seat*36);
    if(!time||time>=word(0x80053A88+course*0xB0+0xA8))return;
    if(std::memcmp(buffer,"________",8)||word(0x80088DF0))return;
    size_t len=std::strlen(options.name[seat]);
    for(size_t i=0;i<8;i++)buffer[i]=i<len&&options.name[seat][i]!=' '?options.name[seat][i]:'_';
    std::memcpy(filled,buffer,8);filled_len=(uint32_t)len;filled_at=now;
    psx_mod_write_word(0x80088DF0,filled_len);psx_mod_counter_add("rr.rider.record_name_prefills",1);
}
// Rider look, per seat (Rider look page; owner_assets.c rr_look_*). Nothing
// changes in a cop race (race mode byte *(0x8005B2F8)+4 bit 0: Five-O is 1):
// the cop keeps the game's look and only the name applies, as in taunt_voices.cpp.
//  Paint. An object with +0x24 bit 10 set (bit 11 clear; bit 11 is the game's
//  blink, a shared palette) draws with its own palette, slot k = +0x24 bits
//  12-17, which 0x800251E4 places at CLUT row 511 - k/3, x = x0 + (k % N) * M
//  + 640 (x0 half +0x70, N byte +0x74, M byte +0x76 of 0x80053254 + 0xB0 *
//  (players - 1); 1P and 2P: N 3, M 128). The bike is the rider record itself;
//  its paint entries (paint_mask below) get the preset hue in VRAM (GP0 0xA0).
//  The mapping is idempotent, so a savestate holding a painted palette is not
//  painted twice, and a palette the game uploads again is painted again.
//  Outfit and colours, not in Jail Break (mode bit 0x20; the character the
//  player chose stays): see dress_rider. Both rider models have the same
//  17/17/12/4 parts per LOD.
extern "C" void gpu_write_gp0(uint32_t);
extern "C" int gl_renderer_cpu_auth_dual(void);
extern "C" void gl_renderer_restage_vram_after_savestate(void);
// Palette rows and texture pages are only ever uploaded: read from gpu.c's array, no renderer sync (gpu_geometry.py VRAM_PEEK_CPU).
extern "C" uint16_t gpu_vram_peek_cpu(int,int);
static unsigned race_mode(){uint32_t c=word(0x8005B2F8);return range(c,8)?byte_at(c+4):0;}
static uint32_t seat_record(unsigned seat){
    uint32_t base=word(0x8005B3A0);if(!range(base,16*0x448))return 0;
    for(unsigned i=0;i<16;i++)if((uint16_t)half(base+i*0x448+0xAC)==seat)return base+i*0x448;
    return 0;
}
//  Only the bike's paint changes. A bike palette is a row of colour ramps
//  (light to dark): chrome and tyres, body colours, decals, lights. The bike's
//  own 128x60 texture (texture table *(0x8005B2E4) + 12 * record +0x4A: tpage
//  +8, u +2, v +3; the wheels come from a shared texture) says how much of the
//  bike each entry covers. The paint is the ramp of 12+ entries, mostly
//  coloured (chroma 9+ of 31, saturation 35%+), covering the most of that
//  texture; ramps of 10+ entries of the same hue (within 25 degrees) with 30%+
//  of its coverage join it (two-tone bodywork). Those entries get the preset
//  hue, their saturation and brightness kept; greys, black, white, chrome,
//  decals and lights are left. Checked offline on every bike texture of the
//  disc (BBLEVEL1-3, BBLEVJBD/K, CRUISES1-3, SPORTS1-3): the white police bikes
//  have no such ramp and stay as they are. Ramps start where brightness rises,
//  so the choice is the same on a palette painted before (savestates).
struct Shade{int r,g,b,v,mn;};
static Shade shade(uint16_t c){Shade s{c&31,c>>5&31,c>>10&31,0,0};s.v=s.r>s.g?(s.r>s.b?s.r:s.b):(s.g>s.b?s.g:s.b);s.mn=s.r<s.g?(s.r<s.b?s.r:s.b):(s.g<s.b?s.g:s.b);return s;}
static bool coloured(const Shade&s){return s.v-s.mn>=9&&(s.v-s.mn)*100>=s.v*35;}
static double hue_of(const Shade&s){
    double d=s.v-s.mn,h=s.v==s.r?(s.g-s.b)/d:s.v==s.g?(s.b-s.r)/d+2:(s.r-s.g)/d+4;h*=60;return h<0?h+360:h;}
static double hue_gap(double a,double b){double d=std::fabs(a-b);return d>180?360-d:d;}
static bool paint_mask(const uint16_t*pal,unsigned m,const unsigned*use,bool*mask){
    struct Ramp{unsigned a,b,use,col;double sx,sy;};Ramp ramps[128];unsigned n=0;
    for(unsigned i=0;i<m;i++){
        Shade s=shade(pal[i]);bool start=!n;
        if(n){Shade p=shade(pal[i-1]);start=s.v>p.v+1||((pal[i]&0x7fff)==0)!=((pal[i-1]&0x7fff)==0)||(coloured(s)&&coloured(p)&&hue_gap(hue_of(s),hue_of(p))>45);}
        if(start)ramps[n++]={i,i,0,0,0,0};
        Ramp&r=ramps[n-1];r.b=i;r.use+=use[i];
        if(coloured(s)){double h=hue_of(s)*3.14159265358979/180;r.col+=use[i];r.sx+=std::cos(h)*use[i];r.sy+=std::sin(h)*use[i];}
    }
    int best=-1;for(unsigned i=0;i<n;i++){const Ramp&r=ramps[i];if(r.b-r.a+1>=12&&r.col&&r.col*2>=r.use&&(best<0||r.col>ramps[best].col))best=(int)i;}
    std::memset(mask,0,m);if(best<0)return false;
    auto hue=[](const Ramp&r){double h=std::atan2(r.sy,r.sx)*180/3.14159265358979;return h<0?h+360:h;};double h0=hue(ramps[best]);
    for(unsigned i=0;i<n;i++){const Ramp&r=ramps[i];
        bool join=(int)i==best||(r.b-r.a+1>=10&&r.col*2>=r.use&&r.col*10>=ramps[best].col*3&&hue_gap(hue(r),h0)<=25);
        if(join)for(unsigned j=r.a;j<=r.b;j++)mask[j]=true;}
    return true;
}
// Texels of the bike's own texture per palette index (8-bit texture page).
static bool bike_texels(uint32_t rec,unsigned*use){
    uint32_t table=word(0x8005B2E4);unsigned index=(uint16_t)half(rec+0x4A);uint32_t e=table+index*12;
    if(index>=64||!range(e,12))return false;unsigned tpage=(uint16_t)half(e+8);if((tpage>>7&3)!=1)return false;
    int tx=(int)(tpage&15)*64+byte_at(e+2)/2,ty=(int)(tpage>>4&1)*256+byte_at(e+3);if(tx+64>1024||ty+60>512)return false;
    std::memset(use,0,256*sizeof*use);
    for(int y=0;y<60;y++)for(int x=0;x<64;x++){uint16_t w=gpu_vram_peek_cpu(tx+x,ty+y);use[w&255]++;use[w>>8]++;}
    return true;
}
static uint16_t paint_entry(uint16_t c,int hue){
    Shade s=shade(c);if(s.v<=2||s.v==s.mn)return c;int r,g,b,v=s.v,mn=s.mn;
    int f=hue%60,d=((v-mn)*f+30)/60,t=mn+d,q=v-d;
    switch(hue/60%6){case 0:r=v;g=t;b=mn;break;case 1:r=q;g=v;b=mn;break;case 2:r=mn;g=v;b=t;break;case 3:r=mn;g=q;b=v;break;case 4:r=t;g=mn;b=v;break;default:r=v;g=mn;b=q;}
    return (uint16_t)((c&0x8000)|r|g<<5|b<<10);
}
struct Seen{uint32_t rec;int x,y;unsigned index;uint16_t written[128],original[128];bool mask[128],valid;};static Seen seen[2];
// Paint back to Original (the showroom steps through it): the game's own palette
// again, only while the row still holds what paint_bike wrote.
static void unpaint_bike(uint32_t rec){
    Seen&c=seen[rec==seat_record(0)?0:1];if(!c.valid||c.rec!=rec)return;
    uint16_t now[128];for(unsigned i=0;i<128;i++)now[i]=gpu_vram_peek_cpu(c.x+(int)i,c.y);
    if(!std::memcmp(now,c.original,256)||std::memcmp(now,c.written,256))return;
    gpu_write_gp0(0xA0000000u);gpu_write_gp0((uint32_t)c.y<<16|(uint32_t)c.x);gpu_write_gp0(1u<<16|128u);
    for(unsigned i=0;i<128;i+=2)gpu_write_gp0(c.original[i]|(uint32_t)c.original[i+1]<<16);
    std::memcpy(c.written,c.original,256);psx_mod_counter_add("rr.look.paint_restores",1);
}
static void paint_bike(uint32_t rec,unsigned players,int hue){
    uint32_t w24=word(rec+0x24),t=0x80053254+0xB0*(players-1);if(!(w24&0x400)||(w24&0x800))return;
    unsigned k=w24>>12&63,n=byte_at(t+0x74),m=byte_at(t+0x76);if(!n||!m||m>128)return;
    int x=half(t+0x70)+(int)(k%n*m)+640,y=511-(int)(k/3);if(x<0||x+(int)m>1024||y<0)return;
    // The paint entries, worked out again whenever the palette is not the one
    // written last (a new race, the game's own upload, a savestate).
    Seen&c=seen[rec==seat_record(0)?0:1];uint16_t now[128],want[128];
    for(unsigned i=0;i<m;i++)now[i]=gpu_vram_peek_cpu(x+(int)i,y);
    unsigned index=(uint16_t)half(rec+0x4A);
    if(!c.valid||c.rec!=rec||c.x!=x||c.y!=y||c.index!=index||std::memcmp(now,c.written,m*2)){
        static unsigned use[256];c={rec,x,y,index,{},{},{},false};std::memcpy(c.original,now,m*2);   // the game's own palette
        if(!bike_texels(rec,use)||!paint_mask(now,m,use,c.mask)){std::memcpy(c.written,now,m*2);c.valid=true;psx_mod_counter_add("rr.look.paint_none",1);return;}
        c.valid=true;psx_mod_counter_add("rr.look.paint_masks",1);
    }
    bool same=true;
    for(unsigned i=0;i<m;i++){want[i]=c.mask[i]?paint_entry(now[i],hue):now[i];same=same&&want[i]==now[i];}
    std::memcpy(c.written,want,m*2);
    if(same)return;
    gpu_write_gp0(0xA0000000u);gpu_write_gp0((uint32_t)y<<16|(uint32_t)x);gpu_write_gp0(1u<<16|m);
    for(unsigned i=0;i<m;i+=2)gpu_write_gp0(want[i]|(uint32_t)(i+1<m?want[i+1]:0)<<16);
    psx_mod_counter_add("rr.look.paint_writes",1);
    // Online (netplay dual raster) the software VRAM is the authority and the
    // OpenGL picture is presentation only: the upload above reached the
    // authority (identical on both PCs) but the GL picture kept the stock
    // palette. Re-stage CPU VRAM into the GL surface (presentation only; once
    // per palette change).
    if(rrpc::netplay()&&gl_renderer_cpu_auth_dual()){gl_renderer_restage_vram_after_savestate();psx_mod_counter_add("rr.look.paint_restage",1);}
}
// A palette slot's VRAM row (as paint_bike): x, y and width m; false if out of range.
static bool palette_xy(unsigned k,unsigned players,int&x,int&y,unsigned&m){
    uint32_t t=0x80053254+0xB0*(players-1);unsigned n=byte_at(t+0x74);m=byte_at(t+0x76);if(!n||!m||m>128)return false;
    x=half(t+0x70)+(int)(k%n*m)+640;y=511-(int)(k/3);return x>=0&&x+(int)m<=1024&&y>=0;
}
//  Outfit (gang and rank, owner_assets.c outfits): the player's rider object
//  (record +0x38) gets that body's model table entry (0x800CE1B0 + 16 * model,
//  id 0x96 DeSades / 0x9F Kaffe Boys; +8 = -1 so the per-view LOD swap
//  0x8001298C loads its mesh next frame), the outfit's texture set (+0x4A) and
//  the build (+0x4C) of an AI rider of that rank, else of that body, when one
//  is in the race. Colours: the chosen palette slot's colours (the outfit's own
//  rank colours by default) are copied into the player's own palette slot, so
//  no AI rider changes; the player's original colours come back with Original.
static void dress_rider(uint32_t rec,unsigned players,unsigned seat,int outfit,int colour){
    uint32_t base=word(0x8005B3A0),rider=word(rec+0x38);if(!range(rider,0x64)||!range(base,16*0x448))return;
    uint32_t r24=word(rider+0x24);if(!(r24&0x400)||(r24&0x800))return;
    int model=0,set=0,slot=-1,profile=0;const bool dressed=rr_outfit_look(outfit,&model,&set,&slot,&profile)!=0;bool changed=false;
    // The rider as the race set it up (model, texture set, build), noted before
    // any outfit: Original puts it back (the showroom steps through Original).
    struct Own{uint32_t rec,rider,desc,build;uint16_t set;bool valid;};static Own own[2];Own&o0=own[seat&1];
    if(!o0.valid||o0.rec!=rec||o0.rider!=rider){o0={rec,rider,word(rider+0x60),word(rider+0x4C),(uint16_t)half(rider+0x4A),true};}
    if(!dressed&&o0.valid){
        if(word(rider+0x60)!=o0.desc){psx_mod_write_word(rider+0x60,o0.desc);psx_mod_write_byte(rider+8,0xFF);changed=true;}
        if((uint16_t)half(rider+0x4A)!=o0.set){psx_mod_write_half(rider+0x4A,o0.set);changed=true;}
        if(word(rider+0x4C)!=o0.build){psx_mod_write_word(rider+0x4C,o0.build);changed=true;}
        if(changed)psx_mod_counter_add("rr.look.outfit_restores",1);
    }
    if(dressed){
        const uint32_t desc=0x800CE1B0u+16u*(unsigned)model;const uint8_t id=model==3?0x96:0x9F;
        if(!range(desc,16)||(word(desc)&0xFFFF)!=id){psx_mod_counter_add("rr.look.outfit_no_model",1);return;}
        if(word(rider+0x60)!=desc){psx_mod_write_word(rider+0x60,desc);psx_mod_write_byte(rider+8,0xFF);changed=true;}
        if((uint16_t)half(rider+0x4A)!=(uint16_t)set){psx_mod_write_half(rider+0x4A,(uint16_t)set);changed=true;}
        uint32_t build=0;
        for(int pass=0;pass<2&&!build;pass++)for(unsigned i=0;i<16&&!build;i++){uint32_t r=base+i*0x448,p=word(r+0x43C),o=word(r+0x38);
            if((uint16_t)half(r+0xAC)<players||!range(o,0x64)||!range(p,2))continue;
            if(pass?word(o+0x60)==desc:byte_at(p+1)==profile)build=word(o+0x4C);}
        if(build&&word(rider+0x4C)!=build){psx_mod_write_word(rider+0x4C,build);changed=true;}
        if(changed)psx_mod_counter_add("rr.look.outfit_writes",1);
    }
    // Colours.
    const int chosen=rr_colour_slot(colour),source=chosen>=0?chosen:dressed?slot:-1;
    struct Kept{uint32_t rec;int x,y;uint16_t original[128];bool valid;};static Kept kept[2];
    int dx,dy,sx,sy;unsigned m,m2;if(!palette_xy(r24>>12&63,players,dx,dy,m))return;
    Kept&k=kept[seat&1];uint16_t now[128],want[128];for(unsigned i=0;i<m;i++)now[i]=gpu_vram_peek_cpu(dx+(int)i,dy);
    if(!k.valid||k.rec!=rec||k.x!=dx||k.y!=dy){k.rec=rec;k.x=dx;k.y=dy;std::memcpy(k.original,now,m*2);k.valid=true;}   // a new race: its own colours
    if(source>=0){if(!palette_xy((unsigned)source,players,sx,sy,m2)||m2!=m)return;for(unsigned i=0;i<m;i++)want[i]=gpu_vram_peek_cpu(sx+(int)i,sy);}
    else std::memcpy(want,k.original,m*2);
    if(!std::memcmp(now,want,m*2))return;
    gpu_write_gp0(0xA0000000u);gpu_write_gp0((uint32_t)dy<<16|(uint32_t)dx);gpu_write_gp0(1u<<16|m);
    for(unsigned i=0;i<m;i+=2)gpu_write_gp0(want[i]|(uint32_t)(i+1<m?want[i+1]:0)<<16);
    psx_mod_counter_add("rr.look.colour_writes",1);
    if(rrpc::netplay()&&gl_renderer_cpu_auth_dual())gl_renderer_restage_vram_after_savestate();
}
// A few times a second while racing, from the host's event poll (rider_menu.cpp
// SDL_PollEvent hook, as pc_tick): the emulation thread, between guest blocks.
// The two-player HUD draws no text, so the text routine is no tick there.
// Online the host poll's timing differs between the PCs: look_guest_tick runs
// it from the emulated VBlank instead (netplay_session.cpp, every 12th).
static void look_apply();
// No blue flash on the players (owner, 5 Nov): in races with two or more human
// players the game blinks the human riders and bikes solid blue through the
// shared-palette bit (+0x24 bit 11, see Paint above), seen 4-25 frames in 60;
// AI riders never get it. Cleared every VBlank (native_ui.cpp
// __wrap_gpu_vblank_tick: a guest-defined point, so online PCs do the same).
void rrpc::unblink_players(){
    const uint32_t cfg=word(0x8005B2F8);if(!range(cfg,0x40)||memory_get_ram_ptr()[cfg&0x1fffff]!=1)return;   // racing
    if(memory_get_ram_ptr()[0x0D5D68]<2)return;   // human players (as pc_services.cpp race_results)
    for(unsigned seat=0;seat<2;seat++){const uint32_t rec=seat_record(seat);if(!rec)continue;
        const uint32_t obj=word(rec+0x38),b=word(rec+0x24);if(b&0x800)psx_mod_write_word(rec+0x24,b&~0x800u);
        if(range(obj,0x28)){const uint32_t o=word(obj+0x24);if(o&0x800)psx_mod_write_word(obj+0x24,o&~0x800u);}}
}
// The game can set the bit again between that VBlank and the draw, so it is
// also cleared as each human's bike / rider reaches the object draw 0x80067AC4
// (object in a0; see Draw distance in pc_services.cpp).
static void unblink_draw(uint32_t pc){
    if(pc!=0x80067AC4u||!debug_cpu_ptr)return;const uint32_t obj=debug_cpu_ptr->gpr[4];if(!range(obj,0x28))return;
    const uint32_t f=word(obj+0x24);if(!(f&0x800)||memory_get_ram_ptr()[0x0D5D68]<2)return;
    for(unsigned seat=0;seat<2;seat++){const uint32_t rec=seat_record(seat);if(rec&&(obj==rec||obj==word(rec+0x38))){psx_mod_write_word(obj+0x24,f&~0x800u);psx_mod_counter_add("rr.rider.unblinked",1);return;}}
}
void rrpc::look_tick(){
    if(rrpc::netplay())return;
    load_options();
    static uint64_t last;uint64_t now=GetTickCount64();if(now-last<200)return;last=now;
    look_apply();
}
void rrpc::look_guest_tick(){load_options();look_apply();}
static void look_apply(){
    if(!race())return;uint32_t cfg=word(0x8005B2F8);unsigned players=word(cfg+0x30),mode=race_mode();
    if(players<1||players>2||(mode&1))return;
    // The showroom (showroom.cpp) is a one-player race: its rider wears the look
    // of the seat whose row is selected, and steps back to Original too.
    const int shown=rrpc::showroom_seat();
    for(unsigned seat=0;seat<players;seat++){uint32_t rec=seat_record(seat);if(!rec)continue;const unsigned o=shown>=0&&seat==0?(unsigned)shown:seat;
        int hue=rr_paint_hue(options.paint[o]);if(hue>=0)paint_bike(rec,players,hue);else if(shown>=0)unpaint_bike(rec);
        if(!(mode&0x20))dress_rider(rec,players,seat,options.outfit[o],options.colour[o]);}
}
// The screen a text draw belongs to, for the in-game editor's Game text page
// (game_editor.cpp); nullptr in a cop race (Five-O keeps the game's words).
static const char* text_screen(){
    if(frontend_overlay())return "Menus";
    if(!race_overlay())return "Other";
    if(race_mode()&1)return nullptr;
    uint32_t cfg=word(0x8005B2F8);unsigned flag=range(cfg,4)?byte_at(cfg):0;
    return flag==1?"Race":flag==6?"Results":"Race screens";
}
// A label's width as 0x8002D0D8 measures it: per character the signed advance
// (+8) of its 11-byte glyph entry (font 0x800D8078 + 24 * font: +8 table indexed
// by character - 32, else searched; entry count at *(+4)+10).
static int text_width(uint32_t font,uint32_t text){
    if(font==0xffffffffu||font>63||!range(text,1))return 0;
    uint32_t f=0x800D8078+font*24,glyphs=word(f+8),count=range(word(f+4),12)?(uint16_t)half(word(f+4)+10):0;int w=0;
    for(unsigned i=0;i<64&&range(text+i,1)&&byte_at(text+i);i++){unsigned ch=byte_at(text+i);uint32_t e=glyphs+(ch-32)*11;
        if(ch<32||!range(e,11)||byte_at(e)!=ch){e=0;for(unsigned k=0;k<count&&k<256;k++)if(range(glyphs+k*11,11)&&byte_at(glyphs+k*11)==ch){e=glyphs+k*11;break;}}
        if(e)w+=(int8_t)byte_at(e+8);}
    return (int16_t)w;
}
extern "C" void __wrap_func_8002CDC8(CPUState*c){
    // Native CPS dispatch also re-enters this symbol at continuation PCs.
    // Menu/font arguments are valid only at the real function entry.
    if(c->pc&&c->pc!=0x8002CDC8){__real_func_8002CDC8(c);rrpc::native_ui_return(c);if(loan.at&&loan.sp==c->gpr[29])give_back();rrpc::game_text_return(c->gpr[29]);return;}
    load_options();
    // "Press START To Continue." etc. name the player's key / button (controls_screen.cpp).
    // Not online: the text is guest RAM and each PC has its own keys.
    if(!rrpc::netplay())rrpc::controls_prompt_text(c->gpr[5]);
    // Time Trial's demo courses: the page's summary names the selected one.
    rrpc::demo_text(c);
    const uint32_t game_string=c->gpr[5];   // the PC menu may point a1 at its own text
    if(rrpc::rider_menu_font(c))return;
    uint32_t origin=text_origin(c),table=word(c->gpr[28]+0x8b8);bool named=false;
    if(options.names){
        int seat=named_seat(c,origin,table);
        if(seat>=0&&seat<2&&options.name[seat][0]){named=true;
            // 0x8002CB08 right-aligns (mode 1) or centres (mode 2, results rows)
            // with the stock name's width before this call (mode at its sp+0x44):
            // move x by the width difference so the custom name sits the same way.
            unsigned mode=c->gpr[31]==0x8002CC20&&range(c->gpr[29]+0x44,4)?word(c->gpr[29]+0x44):0;
            int before=mode==1||mode==2?text_width(c->gpr[4],c->gpr[5]):0;
            if(race_overlay()){uint32_t text=race_name(table,seat);if(text){c->gpr[5]=text;psx_mod_counter_add("rr.rider.name_draws",1);}}
            else if(lend_name(c,seat))psx_mod_counter_add("rr.rider.frontend_name_draws",1);
            if(mode==1||mode==2){int after=text_width(c->gpr[4],c->gpr[5]),dx=mode==2?(before>>1)-(after>>1):before-after;
                c->gpr[6]=(uint32_t)(int32_t)(int16_t)((int16_t)c->gpr[6]+dx);}
        }
        record_name_entry(origin);
    }else if(race_overlay()&&(written_at[0]||written_at[1]))stock_names(table);
    // The owner's words for the game's own text (F2 editor, Game text page);
    // custom names stay as they are.
    if(!named&&c->gpr[5]==game_string)rrpc::game_text_draw(c,text_screen());
    rrpc::native_ui_font(c);
    __real_func_8002CDC8(c);
    rrpc::native_ui_return(c);
    // As for the lent name: only once the call has really returned (sp back at
    // its entry value); a call that continues natively returns its copy later.
    if(loan.at&&loan.sp==c->gpr[29])give_back();
    rrpc::game_text_return(c->gpr[29]);
}
static void enlarge_race_text(uint32_t pc);
static void tag_colour(uint32_t pc);
// Fair starts (owner, 5 Nov): in races with two or more human players everyone
// starts in a random place, as in Mario Kart, instead of the humans side by
// side behind the whole pack. The race loader 0x80069834 copies the race's
// grid record (STARTDFA.BIN, 292 bytes: 18, 16, then 18 x {rider, lateral,
// longitudinal}, 16.16; the AI at 230..6 ahead, the players' slots 16 / 17 at
// -10) out of the loaded file. It is reached through a jump table and is not a
// watched PC, so every VBlank while a race loads (native_ui.cpp
// __wrap_gpu_vblank_tick, a guest-defined point: online PCs do the same) the
// copy is looked for (the only matching record without a neighbour 292 bytes
// either side, unlike the file) and its 18 positions shuffled once. The seed is
// the game's VBlank count.
static uint32_t grid_done;   // this load's shuffle seed (0: not loading)
// A record: N entries (18 in STARTDFA, 16 in the two-player STARTDFB.BIN, where
// the players start at 0 with the AI 48..224 ahead), the first Player 1's (16),
// rider numbers distinct within 1-18.
static int grid_record(const uint8_t*r,uint32_t a){
    if(a+292>0x200000)return 0;uint32_t n,first;std::memcpy(&n,r+a,4);std::memcpy(&first,r+a+4,4);if((n!=16&&n!=18)||first!=16)return 0;
    unsigned seen=0;for(uint32_t e=0;e<n;e++){uint32_t idx;std::memcpy(&idx,r+a+4+12*e,4);if(idx<1||idx>18||(seen>>idx&1))return 0;seen|=1u<<idx;}
    return (int)n;
}
// The game copies the record more than once while loading (online the copy the race used was
// taken after a one-off shuffle), so every record in RAM, the loaded file's too, is arranged
// the same way: positions sorted back to the file's order (front first), then one permutation
// fixed for this load. Doing it again changes nothing, so any copy, whenever it is taken, holds it.
static void fair_grid(uint32_t seed0){
    auto*r=memory_get_ram_ptr();if(r[0x0D5D68]<2)return;   // human players this race (as pc_services.cpp)
    int changed=0;
    for(uint32_t a=0x10000;a+292<=0x200000;a+=4){
        const int n=grid_record(r,a);if(!n)continue;
        struct Slot{int32_t lat,lon;};Slot pos[18],now[18];for(int e=0;e<n;e++){std::memcpy(&pos[e],r+a+8+12*e,8);now[e]=pos[e];}
        std::sort(pos,pos+n,[](const Slot&x,const Slot&y){return x.lon!=y.lon?x.lon>y.lon:x.lat<y.lat;});
        uint32_t seed=seed0;
        for(int e=n-1;e>0;e--){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;const int k=(int)(seed%(uint32_t)(e+1));std::swap(pos[e],pos[k]);}
        if(std::memcmp(pos,now,sizeof(Slot)*n)){changed++;for(int e=0;e<n;e++){psx_mod_write_word(0x80000000u+a+8+12*e,(uint32_t)pos[e].lat);psx_mod_write_word(0x80000000u+a+12+12*e,(uint32_t)pos[e].lon);}}
        a+=288;
    }
    if(changed)psx_mod_counter_add("rr.grid.shuffled",changed);
}
void rrpc::grid_tick(){
    const uint32_t cfg=word(0x8005B2F8);if(!range(cfg,0x40))return;const uint8_t state=memory_get_ram_ptr()[cfg&0x1fffff];
    if(state!=3){grid_done=0;return;}             // not loading: the next load is a new race
    if(!grid_done){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(0x8005B46C&0x1fffff),4);grid_done=v*2654435761u^0x9E3779B9u;if(!grid_done)grid_done=1;}   // this load's seed
    fair_grid(grid_done);
}
extern "C" void __wrap_debug_server_cyc_observe(uint32_t pc){
    __real_debug_server_cyc_observe(pc);
    unblink_draw(pc);
    rrpc::rider_menu_observe(pc);
    rrpc::showroom_observe(pc);
    rrpc::voice_observe(pc);
    rrpc::demo_observe(pc);
    // Enlarge first: the widescreen HUD tag guards the label's final vertices.
    enlarge_race_text(pc);
    tag_colour(pc);
    rrpc::native_ui_observe(pc);
    // A lent frontend name goes back after the HD-font pass read the label.
    if((pc==0x8002D058||pc==0x8002D078)&&loan.at&&debug_cpu_ptr&&debug_cpu_ptr->gpr[29]+0x58==loan.sp)give_back();
    if((pc==0x8002D058||pc==0x8002D078)&&debug_cpu_ptr)rrpc::game_text_return(debug_cpu_ptr->gpr[29]+0x58);
}
static void enlarge_race_text(uint32_t pc){
    if(pc!=0x8002CDC8&&pc!=0x8002D058&&pc!=0x8002D078)return;
    load_options();CPUState*c=debug_cpu_ptr;if(!options.large||!c||!race()||rrpc::rider_menu_drawing())return;
    if(pc==0x8002CDC8){
        uint32_t arena=word(0x8005B470);if(!range(arena,0x110))return;
        FontSpan*slot=nullptr;for(auto&span:spans)if(span.sp==c->gpr[29]){slot=&span;break;}
        if(!slot)for(auto&span:spans)if(!span.sp){slot=&span;break;}
        if(slot){*slot={c->gpr[29],word(arena+0x10c),arena,(int16_t)c->gpr[6],(int16_t)c->gpr[7]};psx_mod_counter_add("rr.text.captures",1);}
        return;
    }
    FontSpan span{};for(auto&item:spans)if(item.sp==c->gpr[29]+0x58){span=item;item.sp=0;break;}
    if(!span.sp||!range(span.arena,0x110))return;
    uint32_t start=span.start,end=pc==0x8002D058?start+c->gpr[21]*40:word(span.arena+0x10c);
    if(end<start||end-start>40*64||(end-start)%40||!range(start,end-start))return;
    auto*r=memory_get_ram_ptr();bool fits=true;
    // A label that wrapped the primitive ring was written from its base (arena+0xF0).
    auto glyphs=[&](uint32_t s,uint32_t n){if(!range(s,n))return false;for(uint32_t p=s;p<s+n;p+=40)if((r[(p+7)&0x1fffff]&0xfcu)!=0x2cu)return false;return true;};
    if(end>start&&!glyphs(start,end-start)){uint32_t base=word(span.arena+0xF0)&0x1fffff,n=end-start;if(glyphs(base,n)){start=base;end=base+n;}}
    for(uint32_t p=start;p<end;p+=40){
        if((r[(p+7)&0x1fffff]&0xfcu)!=0x2cu){fits=false;break;}
        for(unsigned i=0;i<4;i++){int x=span.x+(half(p+8+i*8)-span.x)*5/4,y=span.y+(half(p+10+i*8)-span.y)*5/4;if(x<0||x>=384||y<0||y>=240)fits=false;}
    }
    // Keep an entire label at its original size if enlargement would clip.
    if(!fits)return;
    for(uint32_t p=start;p<end;p+=40){
        for(unsigned i=0;i<4;i++){put_half(p+8+i*8,span.x+(half(p+8+i*8)-span.x)*5/4);put_half(p+10+i*8,span.y+(half(p+10+i*8)-span.y)*5/4);}
        psx_mod_counter_add("rr.text.scaled_glyphs",1);
    }
}
// Name tag colour: the player's race name (under the health bars, 0x80061EE4 /
// 0x8005FB3C, any name setting) is tinted once its glyph packets are written,
// as for large labels above; the glyphs are modulated quads (0x2C-0x2F), so the
// preset replaces their vertex colour (+4..+6) at the label's own brightness.
// Not in a cop race (see look_tick).
struct TagSpan{uint32_t sp,start,arena,rgb;};static TagSpan tag_spans[4];
static void tag_colour(uint32_t pc){
    if(pc!=0x8002CDC8&&pc!=0x8002D058&&pc!=0x8002D078)return;
    CPUState*c=debug_cpu_ptr;if(!c)return;
    if(pc==0x8002CDC8){
        load_options();if(!race_overlay()||rrpc::rider_menu_drawing()||(race_mode()&1))return;
        uint32_t origin=text_origin(c);if(origin!=0x80061EE4&&origin!=0x8005FB3C)return;
        int seat=named_seat(c,origin,word(c->gpr[28]+0x8b8));uint32_t rgb=seat==0||seat==1?rr_tag_rgb(options.tag[seat]):0,arena=word(0x8005B470);
        if(!rgb||!range(arena,0x110))return;
        TagSpan*slot=nullptr;for(auto&s:tag_spans)if(s.sp==c->gpr[29]){slot=&s;break;}
        if(!slot)for(auto&s:tag_spans)if(!s.sp){slot=&s;break;}
        if(slot)*slot={c->gpr[29],word(arena+0x10c),arena,rgb};
        return;
    }
    TagSpan span{};for(auto&s:tag_spans)if(s.sp&&s.sp==c->gpr[29]+0x58){span=s;s.sp=0;break;}
    if(!span.sp||!range(span.arena,0x110))return;
    uint32_t start=span.start,end=pc==0x8002D058?start+c->gpr[21]*40:word(span.arena+0x10c);
    if(end<start||end-start>40*64||(end-start)%40||!range(start,end-start))return;
    auto*r=memory_get_ram_ptr();
    auto glyphs=[&](uint32_t s,uint32_t n){if(!range(s,n))return false;for(uint32_t p=s;p<s+n;p+=40)if((r[(p+7)&0x1fffff]&0xfcu)!=0x2cu)return false;return true;};
    if(end>start&&!glyphs(start,end-start)){uint32_t base=word(span.arena+0xF0)&0x1fffff,n=end-start;if(!glyphs(base,n))return;start=base;end=base+n;}
    for(uint32_t p=start;p<end;p+=40){uint8_t*q=r+((p+4)&0x1fffff);unsigned top=q[0]>q[1]?(q[0]>q[2]?q[0]:q[2]):(q[1]>q[2]?q[1]:q[2]);
        q[0]=(uint8_t)((span.rgb>>16&255)*top/255);q[1]=(uint8_t)((span.rgb>>8&255)*top/255);q[2]=(uint8_t)((span.rgb&255)*top/255);
        psx_mod_counter_add("rr.look.tag_glyphs",1);}
}
