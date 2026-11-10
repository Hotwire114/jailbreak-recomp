/* Menu titles: every frontend page shows its own heading, in the game's title
 * lettering. LGPL-2.1-or-later. Independently authored; no artwork is stored.
 *
 * The game draws each page's heading ("MAIN MENU:", "TIME TRIALS:") from one
 * VRAM slot (512,480 336x32), which it fills with the page's banner picture.
 * The frontend keeps every banner in RAM as a BS-v2 (MDEC) still, listed in a
 * picture directory by a four-letter name; each page names its banner in a
 * picture record at 22,10. This keeps the slot right for the page on screen:
 *  - the game's own pages get their own banner (the game skipped the upload
 *    after the PC menu had borrowed the slot: Game options showed MAIN MENU:);
 *  - pages the game never had (the PC Options pages, and its own options page
 *    under our menu, "GAME OPTIONS:") get a heading set in the game's title
 *    lettering: letters cut at run time from the owner's banners (the column
 *    ranges below are coordinates, not image data), on the banners' common
 *    ground (their median away from the lettering).
 * Each picture put in the slot is registered with its words, so the HD font's
 * banner (native_ui.cpp rr_banner_text) shows the same heading. */
#include "gpu.h"
#include "mod_plugins.h"
#include "owner_assets.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
extern "C" uint8_t*memory_get_ram_ptr(void);
namespace {
constexpr uint32_t PICDIR=0x8009E3C8,MENU=0x8009C5C8,VBLANKS=0x8005AC8C+0x7E0;
constexpr int SX=512,SY=480,SW=336,SH=32,NDIR=48;
// Banner pictures by directory name, and their words.
struct Banner{const char*name,*text;};
const Banner banners[]={
 {"MAIN","MAIN MENU:"},{"SNGL","SINGLE PLAYER:"},{"TIME","TIME TRIALS:"},{"JAIL","JAIL BREAK:"},{"FIVE","FIVE-O:"},
 {"MULT","MULTIPLAYER:"},{"OPTN","OPTIONS:"},{"LOAD","LOAD:"},{"SAVE","SAVE:"},{"NOIS","NOISE:"},{"JUKE","JUKEBOX:"},
 {"CTLR","CONTROLLERS:"},{"CRED","CREDITS:"},{"TRPY","TROPHY ROOM:"},{"RSLT","RACE RESULTS:"},{"RAPS","RAP SHEET:"},
 {"SCAR","SIDECAR:"},{"COPS","COPS & ROBBERS:"},{"SKUL","SKULL TO SKULL:"},{"WNNR","WINNER!"},{"LOSR","LOSER!"},
 {"BSTD","BUSTED!"},{"WRKD","WRECKED!"},{"LVLP","LEVEL PROGRESSION:"}};
constexpr int NB=(int)(sizeof banners/sizeof banners[0]);
// Where each letter is cut from: banner and the columns of its ink [x0,x1).
// All from the 336-wide banners (CONTROLLERS: is a separate, bluer picture).
struct Src{char ch;const char*name;int x0,x1;};
const Src sources[]={
 {'A',"JAIL",21,41},{'B',"JAIL",78,95},{'C',"WRKD",74,92},{'D',"CRED",61,78},{'E',"CRED",44,58},{'F',"FIVE",4,19},
 {'G',"SNGL",54,71},{'H',"RAPS",92,109},{'I',"CRED",82,87},{'J',"JUKE",5,19},{'K',"WRKD",95,113},{'L',"SNGL",74,89},
 {'M',"MAIN",92,116},{'N',"SNGL",33,51},{'O',"TRPY",41,59},{'P',"SNGL",118,134},{'R',"CRED",23,41},{'S',"CRED",107,123},
 {'T',"CRED",89,105},{'U',"JUKE",22,38},{'V',"LVLP",37,55},{'W',"WNNR",1,31},{'X',"JUKE",116,132},{'Y',"TRPY",101,119},
 {':',"CRED",126,131},{'!',"WRKD",158,163},{'-',"FIVE",67,78}};
constexpr int GAP=2,SPACE=10,LEFT=4;   // the banners' own letter and word spacing
// PC menu pages (rider_menu.cpp page_kind) -> heading. 0 (the PC root) keeps
// the game's OPTIONS: banner; 2 (pause) is not a frontend page.
const char*pc_title(int page){
 switch(page){case 7:case 15:case 16:case 17:return "DISPLAY:";case 8:return "USER SETTINGS:";case 6:return "AUDIO:";case 11:return "SYSTEM:";
  case 10:return "TAUNT VOICES:";case 14:return "RIDER LOOK:";case 5:return "ACHIEVEMENTS:";case 1:return "RIDER NAMES:";}
 return nullptr;
}
std::atomic<int> pc_page{-1};
bool ram(uint32_t p,unsigned n){uint32_t o=p&0x1fffffffu;return p>=0x80000000u&&p<0x80200000u&&o<=0x200000u&&n<=0x200000u-o;}
uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),4);return v;}
int16_t half(uint32_t p){int16_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),2);return v;}
const uint8_t*bytes(uint32_t p){return memory_get_ram_ptr()+(p&0x1fffff);}
int banner_index(const uint8_t*name){for(int i=0;i<NB;i++)if(!std::memcmp(name,banners[i].name,4))return i;return -1;}
// A banner's picture in RAM: its directory entry places it in the title slot.
struct Blob{uint32_t data=0,size=0;int w=0;};
Blob find_blob(int b){
 for(int i=0;i<NDIR;i++){uint32_t e=PICDIR+i*0x24;if(!std::memcmp(bytes(e+12),banners[b].name,4)){
  Blob r;if(word(e+16)!=((uint32_t)SY<<16|SX))return r;uint32_t wh=word(e+20);int w=(int)(wh&0xffff);if((wh>>16)!=SH||w<8||w>SW)return r;
  uint32_t d=word(e+4);if(!ram(d,24)||std::memcmp(bytes(d),"MDEC",4))return r;const uint8_t*h=bytes(d);
  uint32_t len=(uint32_t)h[4]<<24|h[5]<<16|h[6]<<8|h[7];if(len<24||!ram(d,len)||(h[8]<<8|h[9])!=w||(h[10]<<8|h[11])!=SH)return r;
  r.data=d;r.size=len;r.w=w;return r;}}
 return Blob{};
}
uint64_t fnv(const void*p,size_t n,uint64_t h=14695981039346656037ull){const uint8_t*b=(const uint8_t*)p;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
uint64_t slot_hash(const uint16_t*px){uint64_t h=14695981039346656037ull;for(int i=0;i<SW*SH;i++){h^=px[i];h*=1099511628211ull;}return h;}   // = rr_banner_hash
// Decoded banners (0xRRGGBB, SW x SH, unused columns 0), the backdrop under
// them (median of the banners away from their lettering) and a key of the
// RAM they came from: a new frontend load decodes again.
struct Art{bool ok=false;int w=0;std::vector<uint32_t> px;};
struct Cache{uint64_t key=0;bool ready=false,failed=false;Art art[NB];std::vector<uint32_t> ground;
 struct Layer{std::vector<int> dx,y;std::vector<uint32_t> px;std::vector<uint8_t> letter;int w=0;bool ok=false,tried=false;}glyph[128];};
Cache cache;
uint64_t archive_key(){
 uint64_t h=0;for(int b=0;b<NB;b++){Blob r=find_blob(b);h=fnv(&r,sizeof r,h^b);if(r.data)h=fnv(bytes(r.data),std::min<uint32_t>(r.size,64),h);}
 return h;
}
int R(uint32_t c){return (c>>16)&255;}int G(uint32_t c){return (c>>8)&255;}int B(uint32_t c){return c&255;}
bool letter(uint32_t c){return R(c)>=0x38;}   // the lettering is light grey-blue, the ground saturated blue
int dist(uint32_t a,uint32_t b){return std::abs(R(a)-R(b))+std::abs(G(a)-G(b))+std::abs(B(a)-B(b));}
bool prepare(){
 const uint64_t key=archive_key();
 if(cache.key==key&&(cache.ready||cache.failed))return cache.ready;
 cache=Cache();cache.key=key;cache.failed=true;
 int decoded=0;
 for(int b=0;b<NB;b++){Blob r=find_blob(b);if(!r.data)continue;unsigned w=0,h=0;uint32_t*px=nullptr;
  if(!rr_mdec_decode(bytes(r.data),r.size,&w,&h,&px))continue;
  if((int)w==r.w&&(int)h==SH){Art&a=cache.art[b];a.ok=true;a.w=r.w;a.px.assign((size_t)SW*SH,0);for(int y=0;y<SH;y++)for(int x=0;x<a.w;x++)a.px[(size_t)y*SW+x]=px[(size_t)y*w+x]&0xffffff;decoded++;}
  std::free(px);}
 if(decoded<NB/2)return false;
 // The ground: per pixel, the median of banners with no lettering nearby
 // (their shadow falls down and right); holes filled from neighbours.
 std::vector<uint8_t> inked((size_t)NB*SW*SH,0);
 for(int b=0;b<NB;b++){const Art&a=cache.art[b];if(!a.ok)continue;uint8_t*n=&inked[(size_t)b*SW*SH];
  for(int y=0;y<SH;y++)for(int x=0;x<a.w;x++)if(letter(a.px[(size_t)y*SW+x]))
   for(int yy=std::max(0,y-2);yy<std::min(SH,y+7);yy++)for(int xx=std::max(0,x-2);xx<std::min(SW,x+8);xx++)n[(size_t)yy*SW+xx]=1;}
 std::vector<uint32_t>&g=cache.ground;g.assign((size_t)SW*SH,0xffffffffu);
 std::vector<int> ch[3];
 for(int i=0;i<SW*SH;i++){for(auto&c:ch)c.clear();
  for(int b=0;b<NB;b++){const Art&a=cache.art[b];if(!a.ok||i%SW>=a.w||inked[(size_t)b*SW*SH+i])continue;uint32_t c=a.px[i];ch[0].push_back(R(c));ch[1].push_back(G(c));ch[2].push_back(B(c));}
  if(ch[0].empty())continue;uint32_t v=0;for(int k=0;k<3;k++){auto m=ch[k].begin()+ch[k].size()/2;std::nth_element(ch[k].begin(),m,ch[k].end());v=v<<8|(uint32_t)*m;}g[i]=v;}
 for(int pass=0;pass<64;pass++){bool hole=false;std::vector<uint32_t> next=g;
  for(int y=0;y<SH;y++)for(int x=0;x<SW;x++){if(g[(size_t)y*SW+x]!=0xffffffffu)continue;int s[3]={0,0,0},n=0;
   const int nb[4][2]={{x-1,y},{x+1,y},{x,y-1},{x,y+1}};
   for(auto&q:nb)if(q[0]>=0&&q[0]<SW&&q[1]>=0&&q[1]<SH){uint32_t c=g[(size_t)q[1]*SW+q[0]];if(c!=0xffffffffu){s[0]+=R(c);s[1]+=G(c);s[2]+=B(c);n++;}}
   if(n)next[(size_t)y*SW+x]=(uint32_t)(s[0]/n)<<16|(uint32_t)(s[1]/n)<<8|(uint32_t)(s[2]/n);else hole=true;}
  g.swap(next);if(!hole)break;}
 cache.ready=true;cache.failed=false;psx_mod_counter_add("rr.titles.decoded",(uint32_t)decoded);
 return true;
}
// One letter: its ink plus its own anti-aliased rim and drop shadow (darker
// pixels down and right of the ink), never a neighbour's ink or shadow.
const Cache::Layer*glyph(char c){
 auto&L=cache.glyph[(unsigned char)c&127];if(L.tried)return L.ok?&L:nullptr;L.tried=true;
 const Src*s=nullptr;for(const auto&q:sources)if(q.ch==c)s=&q;if(!s)return nullptr;
 int b=-1;for(int i=0;i<NB;i++)if(!std::strcmp(banners[i].name,s->name))b=i;if(b<0||!cache.art[b].ok)return nullptr;
 const Art&a=cache.art[b];auto px=[&](int x,int y){return a.px[(size_t)y*SW+x];};
 // The lettering's rows (as rr_banner_build measures them): the backdrop's
 // light arc runs just above the letters and is never part of one.
 int rows[SH]={0},peak=0,top=-1,bottom=0;
 for(int y=0;y<SH;y++){for(int x=0;x<a.w;x++)rows[y]+=letter(px(x,y));peak=std::max(peak,rows[y]);}
 for(int y=0;y<SH;y++)if(rows[y]*5>=peak){if(top<0)top=y;bottom=y+1;}
 if(top<0)return nullptr;
 // The letter's ink: connected pieces at least 3 rows tall (strokes, a colon's
 // dots); the arc crossing the band's top row leaves 1-2 row slivers.
 const int cw=s->x1-s->x0;std::vector<uint8_t> ink((size_t)cw*SH,0),seen((size_t)cw*SH,0);std::vector<int> stack,piece;
 for(int y=top;y<bottom;y++)for(int x=0;x<cw;x++)ink[(size_t)y*cw+x]=letter(px(s->x0+x,y));
 for(int i=0;i<cw*SH;i++){if(!ink[i]||seen[i])continue;stack.assign(1,i);piece.clear();seen[i]=1;int y0=SH,y1=-1;
  while(!stack.empty()){const int k=stack.back();stack.pop_back();piece.push_back(k);const int x=k%cw,y=k/cw;y0=std::min(y0,y);y1=std::max(y1,y);
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){const int xx=x+dx,yy=y+dy;if(xx<0||xx>=cw||yy<0||yy>=SH)continue;const int n=yy*cw+xx;if(ink[n]&&!seen[n]){seen[n]=1;stack.push_back(n);}}}
  if(y1-y0+1<3)for(int k:piece)ink[k]=0;}
 auto own=[&](int x,int y){return x>=s->x0&&x<s->x1&&ink[(size_t)y*cw+x-s->x0];};
 for(int y=0;y<SH;y++)for(int x=std::max(0,s->x0-2);x<std::min(a.w,s->x1+7);x++){
  const uint32_t p=px(x,y),gr=cache.ground[(size_t)y*SW+x];int kind=0;
  if(own(x,y))kind=2;
  else if(letter(p)||dist(p,gr)<40||y<top)continue;
  else if(R(p)+G(p)+B(p)<R(gr)+G(gr)+B(gr)){
   for(int yy=std::max(0,y-5);yy<=y&&!kind;yy++)for(int xx=std::max(0,x-7);xx<=x&&!kind;xx++)if((yy!=y||xx!=x)&&own(xx,yy))kind=1;}
  else{for(int yy=std::max(0,y-1);yy<std::min(SH,y+2)&&!kind;yy++)for(int xx=std::max(0,x-1);xx<std::min(a.w,x+2)&&!kind;xx++)if(own(xx,yy))kind=1;}
  if(kind){L.dx.push_back(x-s->x0);L.y.push_back(y);L.px.push_back(p);L.letter.push_back(kind==2);}}
 L.w=s->x1-s->x0;L.ok=!L.px.empty();return L.ok?&L:nullptr;
}
uint16_t to15(uint32_t c){uint16_t v=(uint16_t)((R(c)>>3)|(G(c)>>3)<<5|(B(c)>>3)<<10);return v?v:1;}   // 0 is transparent
// The heading set in the game's lettering, left-aligned like the banners, on
// the banners' own ground, so it reads like the game's other headings.
bool compose(const char*text,std::vector<uint16_t>&out){
 out.resize((size_t)SW*SH);for(size_t i=0;i<out.size();i++)out[i]=to15(cache.ground[i]);struct Place{int x;const Cache::Layer*l;};std::vector<Place> places;int x=LEFT;
 for(const char*p=text;*p;p++){if(*p==' '){x+=SPACE;continue;}const auto*l=glyph(*p);if(!l)return false;places.push_back({x,l});x+=l->w+GAP;}
 if(x-GAP>SW-2)return false;
 for(int pass=0;pass<2;pass++)for(const auto&pl:places)for(size_t i=0;i<pl.l->px.size();i++){   // shadows first, ink over them
  if(pl.l->letter[i]!=pass)continue;int xx=pl.x+pl.l->dx[i];if(xx>=0&&xx<SW)out[(size_t)pl.l->y[i]*SW+xx]=to15(pl.l->px[i]);}
 return true;
}
bool art(int b,std::vector<uint16_t>&out){
 const Art&a=cache.art[b];if(!a.ok)return false;out.assign((size_t)SW*SH,0);
 for(int y=0;y<SH;y++)for(int x=0;x<a.w;x++)out[(size_t)y*SW+x]=to15(a.px[(size_t)y*SW+x]);
 return true;
}
// Pictures put in the slot, with their words (read by the renderer thread).
std::mutex known_mu;struct Known{uint64_t hash;const char*text;};std::vector<Known> known;
void remember(uint64_t h,const char*text){std::lock_guard<std::mutex> lock(known_mu);for(auto&k:known)if(k.hash==h){k.text=text;return;}if(known.size()>=64)known.erase(known.begin());known.push_back({h,text});}
// The wanted slot picture, kept until the page or the RAM changes.
struct Want{std::string spec;std::vector<uint16_t> px;uint64_t key=0;bool ok=false;uint32_t retry=0;}want;
void upload(const std::vector<uint16_t>&px){
 gpu_write_gp0(0x01000000u);   // texture cache
 gpu_write_gp0(0xA0000000u);gpu_write_gp0((uint32_t)SY<<16|SX);gpu_write_gp0((uint32_t)SH<<16|SW);
 for(int i=0;i<SW*SH;i+=2)gpu_write_gp0(px[i]|(uint32_t)px[i+1]<<16);
}
// The page's banner record: picture type 7 at 22,10 in the current menu.
int page_banner(){
 const uint32_t m=word(MENU);if(!ram(m,0x14))return -1;const uint32_t table=word(m+0x10);const int count=(uint16_t)half(m+8);
 if(count<=0||count>40||!ram(table,(unsigned)count*120))return -1;
 for(int i=0;i<count;i++){const uint32_t r=table+i*120;if(half(r+8)==7&&half(r+24)==22&&half(r+26)==10)return banner_index(bytes(r+16));}
 return -1;
}
uint32_t last_vblank;
}
extern "C" const char*rr_menu_title_text(uint64_t hash){
 std::lock_guard<std::mutex> lock(known_mu);for(const auto&k:known)if(k.hash==hash)return k.text;return nullptr;
}
// Any title banner the game itself puts in the slot, wherever it is shown (the
// race-end WRECKED! / BUSTED! / WINNER! included): the slot is compared with
// the owner's /DATA/FE/TITLES.STR stills (owner_assets.c rr_title_get, decoded
// from the validated disc), whose words follow the file's order. Our decode
// differs from the game's by rounding only; different titles differ by whole
// letters. Called by rr_banner_text (render thread) for an unknown fingerprint.
extern "C" const char*rr_menu_title_match(uint64_t hash){
 static const char*const words[]={"FIVE-O:","COPS & ROBBERS:","SKULL TO SKULL:","JAIL BREAK:","MAIN MENU:","MULTIPLAYER:","OPTIONS:",
  "RACE RESULTS:","RAP SHEET:","SIDECAR:","SINGLE PLAYER:","TROPHY ROOM:","TIME TRIALS:","LOAD:","SAVE:","NOISE:","JUKEBOX:","CREDITS:",
  "WINNER!","LOSER!","BUSTED!","WRECKED!","LEVEL PROGRESSION:"};
 constexpr int NT=(int)(sizeof words/sizeof words[0]);
 if(rr_title_count()!=NT)return nullptr;
 const uint16_t*v=gpu_get_vram();if(!v)return nullptr;
 std::vector<uint16_t> slot((size_t)SW*SH);for(int y=0;y<SH;y++)std::memcpy(&slot[(size_t)y*SW],v+(size_t)(SY+y)*1024+SX,SW*2);
 if(slot_hash(slot.data())!=hash)return nullptr;   // the game has moved on; asked again when the slot changes
 double best=1e9,second=1e9;int pick=-1;
 for(int t=0;t<NT;t++){const RrArt*a=rr_title_get(t);if(!a||!a->pixels||a->w!=(unsigned)SW||a->h!=(unsigned)SH)return nullptr;
  long sum=0;for(int i=0;i<SW*SH;i++){const uint32_t c=a->pixels[i];const uint16_t p=slot[i];
   sum+=std::abs((int)((c>>19)&31)-(p&31))+std::abs((int)((c>>11)&31)-((p>>5)&31))+std::abs((int)((c>>3)&31)-((p>>10)&31));}
  const double d=(double)sum/(SW*SH*3);if(d<best){second=best;best=d;pick=t;}else if(d<second)second=d;}
 if(pick<0||best>2.0||best*2>second)return nullptr;
 remember(hash,words[pick]);psx_mod_counter_add("rr.titles.matched",1);
 return words[pick];
}
namespace rrpc {
void menu_titles_page(int page){pc_page.store(page);}
// Called from the frontend's text routine (native_ui_font): the game is between
// GPU commands there, so the slot can be written. Once per VBlank.
void menu_titles_tick(){
 static int off=-1;if(off<0){const char*e=std::getenv("RR_MENU_TITLES");off=e&&e[0]=='0';}if(off)return;
 const uint32_t vb=word(VBLANKS);if(vb==last_vblank)return;last_vblank=vb;
 if(std::memcmp(bytes(PICDIR+12),"FIVE",4))return;   // the frontend's picture directory
 const int b=page_banner();if(b<0)return;
 const int page=pc_page.load();
 std::string spec;const char*text=nullptr;int pic=-1;
 if(page>=0&&b==6){if((text=pc_title(page)))spec=text;else{pic=6;spec="OPTN";}}
 else if(b==6){text="GAME OPTIONS:";spec=text;}   // the game's own options page, under our Options
 else{pic=b;spec=banners[b].name;}
 if(spec!=want.spec||!want.ok){
  if(!prepare())return;
  want=Want();want.spec=spec;want.key=cache.key;
  want.ok=pic>=0?art(pic,want.px):compose(text,want.px);
  if(!want.ok){psx_mod_counter_add("rr.titles.failed",1);return;}
  remember(slot_hash(want.px.data()),pic>=0?banners[pic].text:text);
 }
 const uint16_t*v=gpu_get_vram();if(!v)return;
 bool same=true;for(int y=0;y<SH&&same;y++)same=!std::memcmp(v+(size_t)(SY+y)*1024+SX,&want.px[(size_t)y*SW],SW*2);
 if(same)return;
 if(want.retry&&vb-want.retry<120)return;   // a write that did not stick is retried every 2 s at most
 if(cache.key!=archive_key()){want.ok=false;return;}
 upload(want.px);psx_mod_counter_add("rr.titles.uploads",1);
 bool stuck=true;for(int y=0;y<SH&&stuck;y++)stuck=!!std::memcmp(v+(size_t)(SY+y)*1024+SX,&want.px[(size_t)y*SW],SW*2);
 want.retry=stuck?vb:0;
}
}
