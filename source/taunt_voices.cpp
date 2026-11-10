/* Player taunt voices from the owner's taunt catalogue (DATA/AUDTAUNT.STR).
 * A catalogue block's kind is a character (owner_assets.c rr_voice_name); the
 * numbered sets are different lines for the same characters. At race start
 * the game streams a random run of seven blocks (0x8001A600: start block =
 * rand*133/256) into nine resident slots; kind 6 is Player 1's lines, 0x16
 * Player 2's. During the race the game keeps streaming single blocks (ids
 * without bit 15) that replace resident slots with new lines; a new kind-6
 * block ~15 s in is why the voice used to fall back to the original. When any
 * such block reaches the taunt-block handler (0x8001A0C0:
 * a0 = block + 0x20, a1 = id), its clip entries and ADPCM are replaced with the
 * chosen character's clips from the same set (the id, kinds and descriptor
 * layout stay), so every line the game later plays from that slot uses the
 * chosen voice with the game's own channel, volume and timing.
 * A player's rider also speaks from shared slots: a hit makes the victim say a
 * kind-3 line (kind 0xB when none is resident; the speech request 0x8001A760
 * with a1 = 1), and a cop player says kind 7. Hit lines are pain and crash
 * sounds and stay stock. Any other player line from a shared slot is moved,
 * once the request has picked a slot (0x8001AD30: s1 = speaker, fp = kind,
 * sp+68 bank, sp+72 sample), to that player's rebuilt slot.
 * Player lines are followed from the voice allocation (0x8001F174 entry,
 * 0x8001F2D4 return) to the SPU KEYON, where the clip heard is identified.
 * No original code or audio is embedded. LGPL-2.1-or-later. */
#include "taunt_voices.h"
#include "mod_assets.h"
#include "rider_enhancements.h"
#include "owner_assets.h"
#include "netplay_session.h"
#include "showroom.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include "spu.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
extern "C" CPUState*debug_cpu_ptr;
extern "C" uint8_t*memory_get_ram_ptr(void);
extern "C" const unsigned char*rr_owner_taunts(const wchar_t*,unsigned*);
extern "C" void __real_spu_write(uint32_t,uint32_t);
namespace {
constexpr uint32_t BLOCK_HANDLER=0x8001A0C0,SPEECH=0x8001AD30,ALLOC=0x8001F174,ALLOC_DONE=0x8001F2D4,SLOTS=0x800D6AA0;
constexpr unsigned DATA=0x60,BLOCK=0x4000;
// Catalogue blocks by set (1..27) and kind.
std::array<std::array<const unsigned char*,256>,28>blocks{};const unsigned char*catalogue;unsigned catalogue_size;bool initialized;
std::unordered_map<uint32_t,uint32_t>clip_of;   // ADPCM FNV-1a -> set<<16|kind<<8|variant
bool valid(uint32_t p,unsigned n){return p>=0x80010000&&p<0x80200000&&n<=0x80200000-p;}
uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),4);return v;}
uint32_t read32(const unsigned char*p){return p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24;}
void put32(unsigned char*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
// Blocks rebuilt this race (a run can carry two blocks of one kind; the game
// keeps one): stock id, the voice now in it, and the seat.
struct Swap{bool active=false;uint32_t id;unsigned voice,seat;};Swap swapped[8];unsigned swaps;
// Test only: RR_VOICE_TEST_KIND=<hex kind> also rebuilds that kind's blocks with
// Player 1's voice, e.g. 3, the hit lines every rider says, to exercise the
// rebuild and KEYON path without a Player 1 line.
unsigned test_kind(){static unsigned k=[]{const char*s=std::getenv("RR_VOICE_TEST_KIND");return s?(unsigned)std::strtoul(s,nullptr,16):0u;}();return k;}
// A player's line on its way to the SPU; `bank` is the slot's bank the request
// chose, `own` = no rebuilt slot to play it from (see substitute()).
struct Pending{bool active=false;unsigned seat,voice;uint64_t tick;uint32_t bank;bool own;};Pending allocating,speaking;
struct Binding{bool active=false;unsigned seat,voice,record,generation;uint64_t tick;bool own;};Binding binding[24];
FILE*log_file(const char*env,const wchar_t*name){
 if(const char*p=std::getenv(env))return std::fopen(p,"w");wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);auto*s=n?wcsrchr(path,L'\\'):nullptr;if(!s)return nullptr;
 wcscpy(s+1,L"voice_log.enable");if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return nullptr;wcscpy(s+1,name);return _wfopen(path,L"w");}
// Diagnostics: RR_VOICE_LOG / RR_VOICE_TRACE=<path>, or a file voice_log.enable
// beside the runtime (then voice_log.txt / voice_trace.txt there).
FILE*vlog(){static FILE*f=log_file("RR_VOICE_LOG",L"voice_log.txt");return f;}
FILE*vtrace(){static FILE*f=log_file("RR_VOICE_TRACE",L"voice_trace.txt");return f;}
uint32_t fnv(const unsigned char*p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
// Clip entries: count in the low byte at +0x2C, table offset (from +0x28) at
// +0x38, 16 bytes each: three attribute words (ADSR, pitch) and the ADPCM
// offset from +0x60. A clip ends at the first frame with the end flag.
unsigned entries(const unsigned char*b){unsigned n=b[0x2c],t=read32(b+0x38);return n&&n<=2&&t>=0x14&&t+0x28+n*16<=DATA?n:0;}
const unsigned char*entry(const unsigned char*b,unsigned i){return b+0x28+read32(b+0x38)+i*16;}
struct Clip{const unsigned char*attr=nullptr,*data=nullptr;unsigned bytes=0;};
bool clip(const unsigned char*b,unsigned variant,Clip&c){if(!b||variant>=entries(b))return false;const unsigned char*e=entry(b,variant);unsigned start=DATA+read32(e+12),end=start;if(start%16||start>=BLOCK)return false;
 while(end+16<=BLOCK&&!(b[end+1]&1))end+=16;if(end+16>BLOCK)return false;c={e,b+start,end+16-start};return true;}
void init(){
 if(initialized||!rrpc::rider_options().menu)return;initialized=true;wchar_t path[32768];if(!GetModuleFileNameW(NULL,path,32768))return;auto*slash=wcsrchr(path,L'\\');if(!slash)return;wcscpy(slash+1,L"..\\game-disc.bin");unsigned size=0;const auto*b=rr_owner_taunts(path,&size);if(!b||size%BLOCK)return;
 catalogue=b;catalogue_size=size;
 for(unsigned p=0;p+BLOCK<=size;p+=BLOCK){unsigned id=read32(b+p),set=(id>>8&255)-0x80,kind=id&255;if((id&0xffff0000)!=0xa0000000||set<1||set>27||blocks[set][kind])continue;blocks[set][kind]=b+p;
  for(unsigned i=0;i<entries(b+p);i++){Clip c;if(clip(b+p,i,c))clip_of.emplace(fnv(c.data,c.bytes),set<<16|kind<<8|i);}}
}
// The chosen character's clips for a slot whose stock block is from set `set`:
// that set's line (or the next set's that has the character) and, for a
// two-entry slot, a different line from a following set that fits beside it.
bool pick(unsigned voice,unsigned seat,unsigned set,Clip&a,Clip&b){unsigned kind,variant;if(!rr_voice_role((int)voice,(int)seat,&kind,&variant))return false;
 unsigned from=0;for(unsigned i=0;i<27&&!from;i++){unsigned s=(set-1+i)%27+1;if(clip(blocks[s][kind],variant==2?0:variant,a))from=s;}if(!from)return false;b=a;
 if(variant==2){clip(blocks[from][kind],1,b);return true;}
 uint32_t first=fnv(a.data,a.bytes);
 for(unsigned i=1;i<27;i++){unsigned s=(from-1+i)%27+1;Clip c;if(clip(blocks[s][kind],variant,c)&&a.bytes+c.bytes<=BLOCK-DATA&&fnv(c.data,c.bytes)!=first){b=c;break;}}
 return true;}
// Rewrite a resident block in RAM: same id, kinds and entry layout, the chosen clips.
void rebuild(unsigned char*dst,const Clip&a,const Clip&b){unsigned n=entries(dst);
 unsigned char*e0=dst+0x28+read32(dst+0x38);std::memcpy(e0,a.attr,12);put32(e0+12,0);
 unsigned second=a.bytes+b.bytes<=BLOCK-DATA&&b.data!=a.data?a.bytes:0;
 if(n>1){std::memcpy(e0+16,b.attr,12);put32(e0+28,second);}
 std::memset(dst+DATA,0,BLOCK-DATA);std::memcpy(dst+DATA,a.data,a.bytes);if(n>1&&second)std::memcpy(dst+DATA+second,b.data,b.bytes);}
// Identify the ADPCM a voice starts on (SPU byte address) against the catalogue.
uint32_t identify(uint32_t addr){unsigned char buf[BLOCK];uint32_t n=spu_ram_peek(addr,buf,sizeof buf),end=0;while(end+16<=n&&!(buf[end+1]&1))end+=16;if(end+16>n)return 0;auto it=clip_of.find(fnv(buf,end+16));return it==clip_of.end()?0:it->second;}
uint32_t half(uint32_t p){uint16_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),2);return v;}
// Five-O (race mode byte *(0x8005B2F8)+4 == 1): the player is a cop and keeps
// the game's own cop voice and lines (owner's choice); only the name changes.
bool five_o(){uint32_t cfg=word(0x8005b2f8);return valid(cfg,8)&&memory_get_ram_ptr()[(cfg+4)&0x1fffff]==1;}
// The resident slot (of nine, 32 bytes: bank, id, state, kind per variant,
// sample per variant) holding this seat's block rebuilt with its voice, or -1.
int own_slot(unsigned seat){
 for(int i=0;i<9;i++){uint32_t s=SLOTS+i*32,id=word(s+4);if((id&255)!=(seat<<4|6)||word(s+12)!=(seat<<4|6))continue;
  for(auto&w:swapped)if(w.active&&w.seat==seat&&w.id==id&&w.voice==(unsigned)rrpc::rider_options().voice[seat])return i;}
 return -1;}
// Lines with no rebuilt slot to play from: a Five-O cop player speaks the cops'
// shared kind-7 lines and that race keeps no slot of the player's own kind (its
// free slots get no header buffer at race start), and a race resumed from a
// savestate holds the blocks streamed before it was saved. At such a line's
// KEYON the voice reads the chosen character's clip from a private sample bank
// (start address 16) instead of SPU RAM; the game's ADSR and pitch stay (every
// taunt clip carries the same attributes). Banks 24-31, one per clip, kept.
constexpr uint32_t FIRST_BANK=24,BANKS=8;
struct Private{const unsigned char*clip;uint32_t bank;};Private privates[BANKS];unsigned private_count;
uint32_t private_bank(const Clip&c){
 for(unsigned i=0;i<private_count;i++)if(privates[i].clip==c.data)return privates[i].bank;
 if(private_count==BANKS)return 0;
 std::vector<unsigned char>data(16+((c.bytes+15)&~15u));std::memcpy(data.data()+16,c.data,c.bytes);
 uint32_t bank=FIRST_BANK+private_count;if(!spu_register_sample_bank(bank,data.data(),(uint32_t)data.size()))return 0;
 privates[private_count++]={c.data,bank};return bank;}
bool substitute(unsigned channel,const Binding&b,Clip&used){
 Clip a,c;if(!pick(b.voice,b.seat,1,a,c))return false;static unsigned turn[2];used=(turn[b.seat&1]++&1)?c:a;
 uint32_t bank=private_bank(used);if(!bank)return false;
 __real_spu_write(0x1f801c06+channel*16,16>>3);   // start address (8-byte units) inside the bank
 return spu_bind_next_voice_bank(channel,bank)!=0;}
// The request -> allocation -> KEYON windows below: the wall clock offline, the
// game's VBlank counter online, so both PCs bind the same lines (netplay_session.h).
uint64_t clock_ms(){return rrpc::netplay()?rrpc::pc_ms():SDL_GetTicks();}
bool fresh(const Binding&b,unsigned channel){return clock_ms()-b.tick<=750&&valid(b.record,44)&&word(b.record+4)==b.generation&&word(b.record+0x1c)==channel;}
}
namespace rrpc {
void voice_tick(){init();}
int voice_next(int n,int d,int seat){init();return rr_voice_next_role(catalogue,catalogue_size,n,seat,d<0?-1:1);}
const char*voice_name(int n){return rr_voice_name(n);}
// Menu preview: one clip of the chosen character on a title-owned SPU voice
// (spu_private_voice_play, private slot 0: none of the game's 24 channels),
// with the clip's own ADSR and pitch. Each voice has one preview clip, in
// sample bank 14 + voice (banks 15-23; the line substitutions use 24-31).
void voice_preview(int voice,int seat){
 init();const unsigned char*adpcm;unsigned bytes;uint32_t attr[3];
 // Rider names that share a voice (owner_assets.c) preview through that voice's bank.
 voice=rr_voice_canonical(voice);
 if(voice<=0||voice>9||!catalogue||!rr_voice_clip(catalogue,catalogue_size,voice,seat,0,&adpcm,&bytes,attr))return;
 std::vector<unsigned char>data(16+((bytes+15)&~15u));std::memcpy(data.data()+16,adpcm,bytes);
 uint32_t bank=14+(uint32_t)voice;if(!spu_register_sample_bank(bank,data.data(),(uint32_t)data.size()))return;
 const uint16_t regs[8]={0x3000,0x3000,(uint16_t)(attr[2]>>16),16>>3,(uint16_t)(attr[1]>>16),(uint16_t)attr[2],0,16>>3};
 spu_private_voice_stop(1u);if(spu_private_voice_play(0,bank,regs,0))psx_mod_counter_add("rr.voice.previews",1);
}
void voice_observe(uint32_t pc){
 if(pc!=BLOCK_HANDLER&&pc!=SPEECH&&pc!=ALLOC&&pc!=ALLOC_DONE)return;if(!rider_options().menu)return;auto*c=debug_cpu_ptr;if(!c)return;
 if(pc==SPEECH){
  uint32_t base=word(0x8005b3a0),rider=c->gpr[17],sp=c->gpr[29],cfg=word(0x8005b2f8);unsigned kind=c->gpr[30];
  if(!valid(sp,0x88)||!valid(rider,0x448)||!valid(cfg,0x34))return;
  unsigned seat=half(rider+0xac);bool player=seat<2&&seat<word(cfg+0x30);
  // Pain and crash lines stay stock: a hit (request a1 = 1) makes the victim say
  // kind 3, or 0xB when none is resident, from pools every rider shares (no
  // character has its own), so a knocked-off player screams as in the stock game.
  bool pain=word(sp+0x84)!=0||kind==3||kind==0xB;
  int moved=-1;bool own=false;unsigned voice=player&&!five_o()&&!pain?rider_options().voice[seat]:0;
  if(voice){
   int slot=own_slot(seat);
   if(slot>=0&&kind!=(seat<<4|6)){
    // The player's line from a shared slot: play it from the player's own rebuilt
    // slot instead (alternating its two lines when it has two).
    static unsigned turn[2];uint32_t s=SLOTS+slot*32;unsigned v=word(s+12+4*(turn[seat]&1))==(seat<<4|6)?turn[seat]&1:0;turn[seat]++;
    psx_mod_write_word(sp+68,word(s));psx_mod_write_word(sp+72,word(s+20+4*v));moved=slot;
    psx_mod_counter_add(seat?"rr.voice.p2_moved_lines":"rr.voice.p1_moved_lines",1);
   }else if(slot<0){init();own=true;psx_mod_counter_add(seat?"rr.voice.p2_bank_lines":"rr.voice.p1_bank_lines",1);}   // substitute() at its KEYON
  }
  if(player)speaking={true,seat,voice,clock_ms(),word(sp+68),own};
  if(FILE*f=vlog()){std::fprintf(f,"t=%llu actor=%d seat=%d kind=%02x hit=%u slot=%u variant=%u bank=%08x sample=%08x moved_to=%d%s\n",(unsigned long long)SDL_GetTicks(),rider>=base?(int)((rider-base)/0x448):-1,player?(int)seat+1:0,kind,word(sp+0x84)!=0,c->gpr[20],c->gpr[22],word(sp+68),word(sp+72),moved,own?" own_bank":"");std::fflush(f);}
  return;}
 if(pc==BLOCK_HANDLER){
  // The race-start run passes ids with bit 15 set (0x89xx = set 9); blocks the
  // game streams later in the race (new lines every so often) come without it
  // (0x09xx). Both are rebuilt, or a mid-race block put the stock voice back.
  uint32_t id=c->gpr[5]&0xffff,at=c->gpr[4]-0x20;unsigned kind=id&255,set=id>>8&0x7f;
  if(FILE*f=vtrace()){std::fprintf(f,"t=%llu stream id=%04x kind=%02x set=%u header=%08x\n",(unsigned long long)SDL_GetTicks(),id,kind,set,valid(at,4)?word(at):0);std::fflush(f);}
  if(kind!=6&&kind!=0x16&&(!test_kind()||kind!=test_kind()))return;unsigned seat=kind==0x16;
  unsigned voice=five_o()?0:rider_options().voice[seat];if(!voice||set<1||set>27)return;init();
  if(!valid(at,BLOCK)||(word(at)&~0x8000u)!=(0xa0000000u|(id&0x7fff)))return;unsigned char*dst=memory_get_ram_ptr()+(at&0x1fffff);if(!entries(dst))return;
  Clip a,b;if(!pick(voice,seat,set,a,b))return;rebuild(dst,a,b);
  Swap*w=nullptr;for(auto&s:swapped)if(s.active&&s.id==id)w=&s;if(!w)w=&swapped[swaps++%8];
  *w={true,id,voice,seat};psx_mod_counter_add(seat?"rr.voice.p2_block_swaps":"rr.voice.p1_block_swaps",1);
  if(FILE*f=vtrace()){std::fprintf(f,"t=%llu block id=%04x seat=%u voice=%u (%s) clips=%u+%u bytes\n",(unsigned long long)SDL_GetTicks(),id,seat+1,voice,rr_voice_name((int)voice),a.bytes,b.data!=a.data?b.bytes:0);std::fflush(f);}
  return;
 }
 if(pc==ALLOC){allocating={};uint32_t bank=c->gpr[4];
  // A player's line (any slot) allocates its voice right after the request.
  const uint64_t now=clock_ms();
  if(speaking.active&&speaking.bank==bank&&now-speaking.tick<=100){allocating=speaking;allocating.tick=now;speaking={};return;}
  if(speaking.active&&now-speaking.tick>100)speaking={};
  for(unsigned i=0;i<9;i++)if(word(SLOTS+i*32)==bank)for(auto&w:swapped)if(w.active&&word(SLOTS+i*32+4)==w.id){allocating={true,w.seat,w.voice,now};return;}
  return;}
 if(allocating.active){auto a=allocating;allocating={};const uint64_t now=clock_ms();if(now-a.tick>500||!valid(c->gpr[2],44))return;unsigned record=c->gpr[2],channel=word(record+0x1c);if(channel>=24)return;binding[channel]={true,a.seat,a.voice,record,word(record+4),now,a.own};}
}
}
extern "C" void __wrap_spu_write(uint32_t addr,uint32_t value){
 if(addr!=0x1f801d88&&addr!=0x1f801d8a){__real_spu_write(addr,rrpc::mod_sound_register(addr,value));return;}   // sound packs: mod_assets.cpp
 // The Rider look showroom presses through menus behind its cover: no sound starts then (showroom.cpp).
 if(rrpc::showroom_mute()){psx_mod_counter_add("rr.showroom.muted_keyons",1);return;}
 unsigned first=addr==0x1f801d88?0:16,limit=first?8:16;
 // Before the KEYON: a player's line with no rebuilt slot reads the chosen clip.
 Clip used[24];bool swapped_in[24]{};
 for(unsigned i=0;i<limit;i++)if(value&(1u<<i)){unsigned channel=first+i;const auto&b=binding[channel];
  if(b.active&&b.own&&b.voice&&fresh(b,channel))swapped_in[channel]=substitute(channel,b,used[channel]);
  rrpc::mod_sound_keyon(channel,swapped_in[channel]);}
 __real_spu_write(addr,value);
 for(unsigned i=0;i<limit;i++)if(value&(1u<<i)){unsigned channel=first+i;auto&b=binding[channel];if(!b.active)continue;b.active=false;
  if(!fresh(b,channel))continue;
  psx_mod_counter_add(b.seat?"rr.voice.p2_actual_keyon":"rr.voice.p1_actual_keyon",1);
  // Which catalogue clip is heard: the chosen character's, or another voice
  // (stock lines, or main-bank samples that are not in the catalogue).
  unsigned start=spu_read(0x1f801c00+channel*16+6)*8;unsigned want,variant;uint32_t who=0;
  if(swapped_in[channel]){auto it=clip_of.find(fnv(used[channel].data,used[channel].bytes));who=it==clip_of.end()?0:it->second;}
  else if(b.voice)who=identify(start);
  bool chosen=b.voice&&who&&rr_voice_role((int)b.voice,(int)b.seat,&want,&variant)&&(who>>8&255)==want;
  if(b.voice)psx_mod_counter_add(chosen?(b.seat?"rr.voice.p2_chosen_keyon":"rr.voice.p1_chosen_keyon"):(b.seat?"rr.voice.p2_other_keyon":"rr.voice.p1_other_keyon"),1);
  if(FILE*f=vtrace()){
   std::fprintf(f,"t=%llu keyon seat=%u channel=%u voice=%u (%s) start=%05x heard set=%u kind=%02x variant=%u%s%s\n",(unsigned long long)SDL_GetTicks(),b.seat+1,channel,b.voice,rr_voice_name((int)b.voice),start,who>>16,who>>8&255,who&255,b.voice?(chosen?" chosen":" OTHER"):"",swapped_in[channel]?" (private bank)":"");std::fflush(f);}
 }
}
