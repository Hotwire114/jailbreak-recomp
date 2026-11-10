/* Player mod packs from the launcher's Mods page. tools/mod_manager.py merges
 * the enabled .rrmod packs and writes runtime/mods/assets/assets.ini (paths
 * relative to the runtime folder). Enhanced only: pc_tick runs only with
 * rider-options.ini [pc] in_game_menu, which Original never has.
 *
 * Textures: the framework's texture-pack loader (gpu_hd_textures_configure;
 * DuckStation-style file names keyed by the uploaded VRAM words and palette)
 * on a folder merged from the enabled packs. Packs mark rider and bike
 * textures; in Five-O (race mode byte *(0x8005B2F8)+4 == 1) the loader uses
 * the merge without them, so Five-O keeps the game's riders (owner's rule).
 *
 * Sounds: at each KEYON the sample the voice starts on is identified in SPU
 * RAM (FNV-1a of its ADPCM up to the end flag, as taunt_voices.cpp does). A
 * sample with a replacement plays the pack's WAV instead, encoded to PS1
 * ADPCM at start-up into private SPU sample banks 1-12 (the voice previews use
 * 15-23, the taunt lines 24-31). Each bank starts with a silent looping block.
 * The game's pitch, ADSR, volume and KEYOFF stay. For that note the start
 * register points into the bank (put back when the game keys the voice again
 * without writing it), and loop-address writes are moved into the bank. Rider
 * and bike sounds are skipped in Five-O.
 *
 * Dump (Mods > Dump for modding): every texture the game draws is written as
 * PNG to mods/texture-dump/dumps by the framework dumper (the file name is the
 * replacement key); every sample a voice plays as WAV to mods/sound-dump, at
 * the rate it first played, with index.csv (id, rate, loop, seconds, where).
 * No game code or data is embedded here. LGPL-2.1-or-later. */
#include "mod_assets.h"
#include "netplay_session.h"
#include "mod_plugins.h"
#include "spu.h"
#include "gpu_hd_textures.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
extern "C" uint8_t*memory_get_ram_ptr(void);
extern "C" void __real_spu_write(uint32_t,uint32_t);
namespace {
constexpr uint32_t SPU_BYTES=0x80000,FIRST_BANK=1,MAX_BANKS=12,BANK_BYTES=0x80000;
std::wstring dir,ini;bool loaded;
std::wstring setting(const wchar_t*section,const wchar_t*key){wchar_t v[32768];GetPrivateProfileStringW(section,key,L"",v,32768,ini.c_str());return v;}
// A folder or file named relative to the runtime folder; empty stays empty.
std::wstring place(const std::wstring&rel){if(rel.empty())return rel;wchar_t full[32768];std::wstring p=rel.size()>1&&rel[1]==L':'?rel:dir+rel;return GetFullPathNameW(p.c_str(),32768,full,nullptr)?full:p;}
std::string utf8(const std::wstring&w){int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,nullptr,0,nullptr,nullptr);std::string s(n>1?n-1:0,'\0');if(n>1)WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,s.data(),n,nullptr,nullptr);return s;}
uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),4);return v;}
bool five_o(){uint32_t c=word(0x8005b2f8);return c>=0x80010000u&&c<0x801ffff0u&&memory_get_ram_ptr()[(c+4)&0x1fffff]==1;}
bool racing_now;

// ---- Textures ---------------------------------------------------------------
std::wstring tex_all,tex_safe,tex_dump;int tex_state=-1;   // 0 off, 1 all, 2 without rider/bike, 3 dump
void textures_tick(){
 int want=!tex_dump.empty()?3:tex_all.empty()?0:!tex_safe.empty()&&five_o()?2:1;
 if(want==tex_state)return;int was=tex_state;tex_state=want;
 if(!want){if(was>0)gpu_hd_textures_configure("",0,0,nullptr,0);return;}
 const std::wstring&root=want==3?tex_dump:want==2?tex_safe:tex_all;char error[512]="";
 if(gpu_hd_textures_configure(utf8(root).c_str(),want!=3,want==3,error,sizeof error)){
  psx_mod_counter_add(want==3?"rr.mods.texture_dump":want==2?"rr.mods.texture_packs_five_o":"rr.mods.texture_packs",1);
  std::fprintf(stderr,"rr mods: textures %s from %ls\n",want==3?"dumped":"replaced",root.c_str());
 }else{psx_mod_counter_add("rr.mods.texture_errors",1);std::fprintf(stderr,"rr mods: texture pack not loaded: %s\n",error);}
}

// ---- WAV in, PS1 ADPCM out --------------------------------------------------
uint32_t le(const uint8_t*p,int n){uint32_t v=0;for(int i=n-1;i>=0;i--)v=v<<8|p[i];return v;}
// PCM 8/16/24/32-bit or 32-bit float, any channel count (mixed to mono), as -1..1.
bool read_wav(const std::wstring&path,std::vector<float>&out,unsigned&rate){
 FILE*f=_wfopen(path.c_str(),L"rb");if(!f)return false;std::vector<uint8_t>b;uint8_t chunk[65536];size_t n;
 while((n=std::fread(chunk,1,sizeof chunk,f))>0&&b.size()<(96u<<20))b.insert(b.end(),chunk,chunk+n);std::fclose(f);
 if(b.size()<12||std::memcmp(b.data(),"RIFF",4)||std::memcmp(b.data()+8,"WAVE",4))return false;
 unsigned format=0,channels=0,bits=0;const uint8_t*data=nullptr;size_t bytes=0;
 for(size_t p=12;p+8<=b.size();){uint32_t size=le(&b[p+4],4);const uint8_t*c=&b[p+8];size_t have=std::min<size_t>(size,b.size()-p-8);
  if(!std::memcmp(&b[p],"fmt ",4)&&have>=16){format=le(c,2);channels=le(c+2,2);rate=le(c+4,4);bits=le(c+14,2);if(format==0xFFFE&&have>=26)format=le(c+24,2);}
  else if(!std::memcmp(&b[p],"data",4)){data=c;bytes=have;}
  p+=8+size+(size&1);}
 if(!data||!channels||channels>8||rate<4000||rate>192000)return false;
 bool fl=format==3&&bits==32;if(!fl&&(format!=1||(bits!=8&&bits!=16&&bits!=24&&bits!=32)))return false;
 unsigned step=bits/8*channels;size_t frames=bytes/step;if(frames>rate*600u)frames=rate*600u;out.resize(frames);
 for(size_t i=0;i<frames;i++){double sum=0;for(unsigned k=0;k<channels;k++){const uint8_t*s=data+i*step+k*(bits/8);double v;
   if(fl){float x;std::memcpy(&x,s,4);v=x;}else if(bits==8)v=(s[0]-128)/128.0;else{int32_t x=(int32_t)(le(s,bits/8)<<(32-bits));v=x/2147483648.0;}
   sum+=v;}out[i]=(float)(sum/channels);}
 return frames>0;}
// Windowed-sinc resampling to `count` samples over the same duration.
std::vector<float>resample(const std::vector<float>&in,size_t count){
 if(count==in.size())return in;std::vector<float>out(count);double step=(double)in.size()/count,cut=std::min(1.0,1.0/step)*.97;const int taps=16;const double pi=3.14159265358979;
 for(size_t i=0;i<count;i++){double centre=(i+.5)*step-.5,sum=0,weight=0;long c=(long)std::floor(centre);
  for(long k=c-taps+1;k<=c+taps;k++){double x=centre-k;if(std::fabs(x)>=taps)continue;double w=(x==0?cut:std::sin(pi*x*cut)/(pi*x))*(.42+.5*std::cos(pi*x/taps)+.08*std::cos(2*pi*x/taps));
   long j=std::clamp<long>(k,0,(long)in.size()-1);sum+=in[j]*w;weight+=w;}
  out[i]=weight!=0?(float)(sum/weight):0.f;}
 return out;}
const int F0[5]={0,60,115,98,122},F1[5]={0,0,-52,-55,-60};
int clamp16(int v){return v<-32768?-32768:v>32767?32767:v;}
// One 28-sample block: every filter and shift is tried against the decoder's
// own arithmetic (spu.c decode_block) and the smallest squared error kept.
void encode_block(const int16_t*x,int&h1,int&h2,uint8_t*out,uint8_t flags){
 long long best=-1;int bf=0,bs=0,bh1=h1,bh2=h2;uint8_t bn[28]{};
 for(int f=0;f<5;f++)for(int s=0;s<=12;s++){int a=h1,b=h2,stepv=1<<(12-s);long long err=0;uint8_t nib[28];
  for(int i=0;i<28&&(best<0||err<best);i++){int pred=(a*F0[f]+b*F1[f]+32)>>6,want=x[i]-pred;
   int q=want>=0?(want+stepv/2)/stepv:-((-want+stepv/2)/stepv);q=std::clamp(q,-8,7);int d=clamp16(q*stepv+pred);
   err+=(long long)(x[i]-d)*(x[i]-d);nib[i]=(uint8_t)(q&15);b=a;a=d;}
  if(best<0||err<best){best=err;bf=f;bs=s;bh1=a;bh2=b;std::memcpy(bn,nib,28);}}
 out[0]=(uint8_t)(bs|bf<<4);out[1]=flags;for(int i=0;i<14;i++)out[2+i]=(uint8_t)(bn[2*i]|bn[2*i+1]<<4);h1=bh1;h2=bh2;}
// Whole sample: loop -> loop start on the first block, end+repeat on the last;
// otherwise end on the last block (the voice then falls silent, as stock).
std::vector<uint8_t>encode(const std::vector<float>&pcm,bool loop,double gain){
 size_t blocks=std::max<size_t>(1,(pcm.size()+27)/28);std::vector<uint8_t>out(blocks*16);int h1=0,h2=0;
 for(size_t k=0;k<blocks;k++){int16_t x[28];for(int i=0;i<28;i++){size_t j=k*28+i;x[i]=j<pcm.size()?(int16_t)clamp16((int)std::lround(pcm[j]*gain*32767.0)):0;}
  uint8_t flags=0;if(loop&&k==0)flags|=4;if(k+1==blocks)flags|=loop?3:1;encode_block(x,h1,h2,&out[k*16],flags);}
 return out;}

// ---- Sound replacements ----------------------------------------------------
struct Rep{uint32_t bank,offset,bytes;bool rider;};
std::unordered_map<uint32_t,Rep>reps;std::vector<std::vector<uint8_t>>banks;
std::atomic<int>sound_state{0};   // 0 none, 1 encoding, 2 encoded (register banks), 3 playing
std::thread worker;std::wstring sound_dump;unsigned sound_errors;
struct Entry{uint32_t id;std::wstring file;unsigned rate;bool loop,rider;double gain;};
void encode_all(std::vector<Entry>list){
 for(auto&e:list){std::vector<float>pcm;unsigned rate=0;
  if(!read_wav(e.file,pcm,rate)){sound_errors++;std::fprintf(stderr,"rr mods: sound %08x: unreadable WAV %ls\n",e.id,e.file.c_str());continue;}
  // The pack's rate is the stock sample's own (its first-play pitch); the game's
  // pitch then plays the replacement at the stock speed. Loops get a whole
  // number of blocks (at most 1/56 slower or faster), so they close cleanly.
  size_t count=(size_t)std::llround((double)pcm.size()*e.rate/rate);if(e.loop)count=std::max<size_t>(28,(count+14)/28*28);
  auto adpcm=encode(resample(pcm,std::max<size_t>(count,1)),e.loop,e.gain);
  if(banks.empty()||banks.back().size()+adpcm.size()>BANK_BYTES){
   if(adpcm.size()+16>BANK_BYTES||banks.size()==MAX_BANKS){sound_errors++;std::fprintf(stderr,"rr mods: sound %08x: no bank space\n",e.id);continue;}
   banks.emplace_back(16,0);banks.back()[1]=7;}   // silent block that loops on itself
  auto&bank=banks.back();Rep r{FIRST_BANK+(uint32_t)banks.size()-1,(uint32_t)bank.size(),(uint32_t)adpcm.size(),e.rider};
  bank.insert(bank.end(),adpcm.begin(),adpcm.end());reps[e.id]=r;}
 sound_state.store(2,std::memory_order_release);}
void sounds_tick(){
 if(sound_state.load(std::memory_order_acquire)!=2)return;if(worker.joinable())worker.join();
 for(size_t i=0;i<banks.size();i++)if(!spu_register_sample_bank(FIRST_BANK+(uint32_t)i,banks[i].data(),(uint32_t)banks[i].size())){
  for(auto it=reps.begin();it!=reps.end();)if(it->second.bank==FIRST_BANK+i)it=reps.erase(it);else ++it;sound_errors++;}
 psx_mod_counter_add("rr.mods.sounds_loaded",(uint32_t)reps.size());if(sound_errors)psx_mod_counter_add("rr.mods.sound_errors",sound_errors);
 std::fprintf(stderr,"rr mods: %zu sound replacements in %zu banks, %u errors\n",reps.size(),banks.size(),sound_errors);
 sound_state.store(3,std::memory_order_release);}
// The sample at an SPU address, up to and including the block with the end flag.
uint32_t sample_id(uint32_t start,uint32_t&bytes){
 const uint8_t*ram=spu_get_ram();uint32_t h=2166136261u;bytes=0;if(!ram)return 0;
 for(uint32_t n=0;n<SPU_BYTES;n+=16){const uint8_t*b=ram+((start+n)&(SPU_BYTES-16));for(int i=0;i<16;i++)h=(h^b[i])*16777619u;if(b[1]&1){bytes=n+16;return h;}}
 return 0;}
// Per channel: the note playing from a bank, and what the game last wrote.
struct Voice{bool playing,stale;uint16_t ours,stock;uint32_t stock_start,stock_bytes;Rep rep;bool repeat_known;uint16_t repeat;};Voice voices[24];
uint16_t moved_repeat(const Voice&v,uint16_t value){
 uint32_t a=(uint32_t)(value&~1u)*8;if(a<v.stock_start||a>=v.stock_start+v.stock_bytes)return 0;   // outside: the bank's silent loop
 uint32_t off=(uint32_t)((uint64_t)(a-v.stock_start)*v.rep.bytes/v.stock_bytes)&~15u;if(off+16>v.rep.bytes)off=v.rep.bytes-16;
 return (uint16_t)((v.rep.offset+off)>>3);}
// Dump: each new sample as WAV at the rate it plays at, plus an index line.
std::unordered_set<uint32_t>dumped;unsigned dumps;
void dump_sound(uint32_t id,uint32_t start,uint32_t bytes,unsigned ch){
 if(sound_dump.empty()||dumps>=4000||!dumped.insert(id).second)return;
 wchar_t name[16];std::swprintf(name,16,L"%08x.wav",id);std::wstring path=sound_dump+L"\\"+name;
 if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)return;
 const uint8_t*ram=spu_get_ram();std::vector<int16_t>pcm;int h1=0,h2=0;bool loop=false;
 for(uint32_t n=0;n<bytes;n+=16){const uint8_t*b=ram+((start+n)&(SPU_BYTES-16));int s=std::min(b[0]&15,12),f=b[0]>>4;if(f>4)f=0;if(b[1]&4)loop=true;if(n+16==bytes&&(b[1]&2))loop=true;
  for(int i=0;i<28;i++){int nib=(b[2+i/2]>>(i&1?4:0))&15;if(nib&8)nib-=16;int v=clamp16(((nib<<12)>>s)+((h1*F0[f]+h2*F1[f]+32)>>6));h2=h1;h1=v;pcm.push_back((int16_t)v);}}
 unsigned pitch=spu_read(0x1f801c04+ch*16)&0x3fff,rate=pitch?(unsigned)std::lround(44100.0*pitch/4096):22050;rate=std::clamp(rate,1000u,176400u);
 FILE*f=_wfopen(path.c_str(),L"wb");if(!f)return;uint32_t data=(uint32_t)pcm.size()*2;
 uint8_t hdr[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a'};
 auto put=[&](int at,uint32_t v){for(int i=0;i<4;i++)hdr[at+i]=(uint8_t)(v>>8*i);};put(4,36+data);put(24,rate);put(28,rate*2);put(40,data);
 std::fwrite(hdr,1,44,f);std::fwrite(pcm.data(),2,pcm.size(),f);std::fclose(f);dumps++;psx_mod_counter_add("rr.mods.sound_dumps",1);
 std::wstring index=sound_dump+L"\\index.csv";bool fresh=GetFileAttributesW(index.c_str())==INVALID_FILE_ATTRIBUTES;
 if(FILE*x=_wfopen(index.c_str(),L"ab")){if(fresh)std::fputs("sample,rate,loop,seconds,where\n",x);std::fprintf(x,"%08x,%u,%d,%.3f,%s\n",id,rate,loop?1:0,pcm.size()/(double)rate,racing_now?"race":"menu");std::fclose(x);}
}
// ---- Models ----------------------------------------------------------------
// The race loader (overlay 0x8005C0C4) registers each RMD3 model in a 50-slot
// table at 0x800CE1B0 (16 bytes: +0 id, +4 LOD count, +6 complete, +8 LOD
// array); a LOD entry is {DOD3*, DPD3* array, BBD3*}. The blocks stay where
// they were read, offsets turned into pointers: DOD3 +0x18 part count, +0x24
// vertex list (count, SVECTOR[]); DPD3 +0x14 face list (count, 20-byte faces).
// A replacement (tools/models.py) keeps the vertex slots and parts, so it is
// written over the original in place (faces up to each part's original count)
// and every pointer, part range and vertex-indexed list stays valid. The id is
// FNV-1a of the vertex list and face lists, bytes the loader leaves unchanged.
struct Mesh{uint32_t nv=0;std::vector<uint32_t>faces;std::vector<uint8_t>verts;std::vector<std::vector<uint8_t>>lists;bool rider=false;};
std::unordered_map<uint32_t,Mesh>meshes;
struct Resident{uint32_t sig[4]{};bool five=false,ours=false;uint32_t ours_hash=0,mesh=0;std::vector<uint8_t>verts;std::vector<std::vector<uint8_t>>lists;};
std::unordered_map<uint32_t,Resident>resident;   // by DOD3 address
bool valid(uint32_t p,uint32_t n){return p>=0x80010000u&&p<0x80200000u&&n<=0x80200000u-p;}
uint8_t*at(uint32_t p){return memory_get_ram_ptr()+(p&0x1fffff);}
uint32_t fnv(const uint8_t*p,size_t n,uint32_t h=2166136261u){for(size_t i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
bool load_mesh(const std::wstring&path,bool rider){
 FILE*f=_wfopen(path.c_str(),L"rb");if(!f)return false;std::vector<uint8_t>b(1<<20);size_t n=std::fread(b.data(),1,b.size(),f);std::fclose(f);b.resize(n);
 auto u=[&](size_t o){return o+4<=b.size()?le(&b[o],4):0u;};
 if(n<24||std::memcmp(b.data(),"RRMS",4)||u(4)!=1)return false;uint32_t id=u(8),nv=u(12),ng=u(16);if(!nv||nv>4096||!ng||ng>64)return false;
 Mesh m;m.nv=nv;m.rider=rider;size_t p=24;for(uint32_t g=0;g<ng;g++){m.faces.push_back(u(p));p+=4;}
 if(p+nv*8>n)return false;m.verts.assign(b.begin()+p,b.begin()+p+nv*8);p+=nv*8;
 for(uint32_t c:m.faces){if(c>4096||p+c*20>n)return false;m.lists.emplace_back(b.begin()+p,b.begin()+p+c*20);p+=c*20;}
 meshes[id]=std::move(m);return true;}
// The face lists of one LOD (count words included), or false if not readable.
bool lod_lists(uint32_t arr,uint32_t parts,std::vector<uint32_t>&lists){
 lists.clear();for(uint32_t g=0;g<parts;g++){uint32_t dpd=word(arr+4*g);if(!valid(dpd,0x1c))return false;uint32_t l=word(dpd+0x14);if(!valid(l,4)||!valid(l,4+word(l)*20))return false;lists.push_back(l);}
 return true;}
uint32_t lod_hash(uint32_t vl,uint32_t nv,const std::vector<uint32_t>&lists){uint32_t h=fnv(at(vl),4+nv*8);for(uint32_t l:lists)h=fnv(at(l),4+word(l)*20,h);return h;}
void models_tick(){
 if(meshes.empty())return;bool fo=five_o();uint8_t*ram=memory_get_ram_ptr();std::vector<uint32_t>lists;
 for(uint32_t s=0;s<50;s++){uint32_t a=0x800ce1b0+s*16,lods=word(a+8);unsigned n=ram[(a+4)&0x1fffff];
  if(!word(a)||ram[(a+6)&0x1fffff]!=1||!valid(lods,n*12))continue;   // every LOD of the model has arrived
  for(unsigned k=0;k<n;k++){uint32_t d=word(lods+12*k),arr=word(lods+12*k+4);if(!valid(d,0x34))continue;
   uint32_t vl=word(d+0x24),parts=ram[(d+0x18)&0x1fffff]|ram[(d+0x19)&0x1fffff]<<8;if(!valid(vl,4)||!parts||parts>64||!valid(arr,parts*4))continue;
   uint32_t nv=word(vl);if(!nv||nv>4096||!valid(vl,4+nv*8))continue;
   uint32_t sig[4]={vl,nv,word(vl+4),word(vl+8)};Resident&r=resident[d];
   if(!std::memcmp(r.sig,sig,sizeof sig)&&r.five==fo)continue;   // nothing new here
   if(!lod_lists(arr,parts,lists))continue;uint32_t h=lod_hash(vl,nv,lists);
   if(r.ours&&h==r.ours_hash){
    if(meshes[r.mesh].rider&&fo&&r.lists.size()==parts){   // Five-O keeps the game's riders and bikes
     std::memcpy(at(vl),r.verts.data(),r.verts.size());for(uint32_t g=0;g<parts;g++)std::memcpy(at(lists[g]),r.lists[g].data(),r.lists[g].size());
     r.ours=false;psx_mod_counter_add("rr.mods.model_restores",1);}
   }else{
    r.ours=false;auto it=meshes.find(h);const Mesh*m=it==meshes.end()?nullptr:&it->second;
    bool fits=m&&m->nv==nv&&m->faces.size()==parts&&!(m->rider&&fo);
    for(uint32_t g=0;fits&&g<parts;g++)fits=word(lists[g])>=m->faces[g];
    if(fits){
     r.mesh=h;r.verts.assign(at(vl),at(vl)+4+nv*8);r.lists.clear();for(uint32_t l:lists)r.lists.emplace_back(at(l),at(l)+4+word(l)*20);
     // Vertices keep each slot's pad half-word (the original's), faces fill each part's list.
     for(uint32_t i=0;i<nv;i++)std::memcpy(at(vl+4+i*8),&m->verts[i*8],6);
     for(uint32_t g=0;g<parts;g++){psx_mod_write_word(lists[g],m->faces[g]);if(!m->lists[g].empty())std::memcpy(at(lists[g]+4),m->lists[g].data(),m->lists[g].size());}
     r.ours=true;r.ours_hash=lod_hash(vl,nv,lists);psx_mod_counter_add("rr.mods.model_swaps",1);
    }else if(m)psx_mod_counter_add(m->rider&&fo?"rr.mods.model_five_o_skips":"rr.mods.model_misfits",1);
   }
   uint32_t now[4]={vl,nv,word(vl+4),word(vl+8)};std::memcpy(r.sig,now,sizeof now);r.five=fo;}}
}
void load(){
 if(loaded)return;loaded=true;
 if(rrpc::netplay())return;   // online: no packs (they change sounds and models per PC; the launcher refuses mods online)
 wchar_t p[32768];DWORD n=GetModuleFileNameW(nullptr,p,32768);if(!n||n>=32768)return;auto*s=std::wcsrchr(p,L'\\');if(!s)return;s[1]=0;dir=p;
 ini=dir+L"mods\\assets\\assets.ini";if(GetFileAttributesW(ini.c_str())==INVALID_FILE_ATTRIBUTES)return;
 tex_all=place(setting(L"textures",L"all"));tex_safe=place(setting(L"textures",L"safe"));tex_dump=place(setting(L"dump",L"textures"));
 sound_dump=place(setting(L"dump",L"sounds"));if(!sound_dump.empty())CreateDirectoryW(sound_dump.c_str(),nullptr);
 std::vector<Entry>list;int count=GetPrivateProfileIntW(L"sounds",L"count",0,ini.c_str());
 for(int i=0;i<count&&i<4096;i++){wchar_t sec[16];std::swprintf(sec,16,L"sound%d",i);Entry e;e.id=(uint32_t)std::wcstoul(setting(sec,L"sample").c_str(),nullptr,16);e.file=place(setting(sec,L"file"));
  e.rate=GetPrivateProfileIntW(sec,L"rate",0,ini.c_str());e.loop=GetPrivateProfileIntW(sec,L"loop",0,ini.c_str())!=0;e.rider=GetPrivateProfileIntW(sec,L"rider",0,ini.c_str())!=0;
  e.gain=std::clamp((int)GetPrivateProfileIntW(sec,L"volume",100,ini.c_str()),1,400)/100.0;if(e.id&&!e.file.empty()&&e.rate>=1000&&e.rate<=176400)list.push_back(std::move(e));}
 if(!list.empty()){sound_state=1;worker=std::thread(encode_all,std::move(list));}
 int models=GetPrivateProfileIntW(L"models",L"count",0,ini.c_str()),bad=0;
 for(int i=0;i<models&&i<4096;i++){wchar_t sec[16];std::swprintf(sec,16,L"model%d",i);
  if(!load_mesh(place(setting(sec,L"file")),GetPrivateProfileIntW(sec,L"rider",0,ini.c_str())!=0))bad++;}
 if(models){psx_mod_counter_add("rr.mods.models_loaded",(uint32_t)meshes.size());std::fprintf(stderr,"rr mods: %zu model replacements, %d unreadable\n",meshes.size(),bad);}
}
}
namespace rrpc {
void mods_tick(bool racing){
 load();racing_now=racing;if(ini.empty())return;
 // The texture loader switches the renderer's VRAM authority: wait for the window.
 static bool video;if(!video){int count=0;auto**w=SDL_GetWindows(&count);SDL_free(w);video=count>0;}
 if(video)textures_tick();sounds_tick();models_tick();}
uint32_t mod_sound_register(uint32_t addr,uint32_t value){
 if(sound_state.load(std::memory_order_relaxed)!=3||addr<0x1f801c00||addr>=0x1f801d80)return value;
 unsigned ch=(addr-0x1f801c00)>>4,reg=(addr&15)>>1;Voice&v=voices[ch];
 if(reg==3)v.stale=false;   // the game set a new start; ours is gone
 if(reg==7){if(v.playing&&(uint16_t)value==spu_read(addr))return value;   // a loop point read back and restored
  v.repeat=(uint16_t)value;v.repeat_known=true;if(v.playing){psx_mod_counter_add("rr.mods.sound_loop_moves",1);return moved_repeat(v,(uint16_t)value);}}
 return value;}
void mod_sound_keyon(unsigned ch,bool taken){
 int state=sound_state.load(std::memory_order_relaxed);if(ch>=24||(state!=3&&sound_dump.empty()))return;
 Voice&v=voices[ch];if(taken){v.playing=v.stale=false;return;}uint32_t start_reg=0x1f801c06+ch*16,repeat_reg=0x1f801c0e + ch*16;uint16_t reg=(uint16_t)spu_read(start_reg),stock=v.stale&&reg==v.ours?v.stock:reg;
 bool was_playing=v.playing;v.playing=false;v.stale=false;uint32_t start=(uint32_t)(stock&~1u)*8,bytes;uint32_t id=sample_id(start,bytes);
 if(id)dump_sound(id,start,bytes,ch);
 const Rep*found=nullptr;if(state==3&&id){auto it=reps.find(id);if(it!=reps.end())found=&it->second;}   // reps is the worker's until state 3
 if(!found||(found->rider&&five_o())){
  // Not replaced: the voice reads the game's own sample and loop point again.
  if(stock!=reg)__real_spu_write(start_reg,stock);if(was_playing&&v.repeat_known)__real_spu_write(repeat_reg,v.repeat);return;}
 const Rep&r=*found;uint16_t ours=(uint16_t)(r.offset>>3);
 __real_spu_write(start_reg,ours);if(!spu_bind_next_voice_bank(ch,r.bank)){__real_spu_write(start_reg,stock);return;}
 v={true,true,ours,stock,start,bytes,r,v.repeat_known,v.repeat};
 if(v.repeat_known)__real_spu_write(repeat_reg,moved_repeat(v,v.repeat));
 psx_mod_counter_add("rr.mods.sound_swaps",1);}
}
