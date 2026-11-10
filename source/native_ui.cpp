/* Preserve the owner's original UI font, bars, colours and draw timing.
 * Edge-directed magnification runs in the native GPU texture shader. No host
 * text replacement or time-expiring font overlay. LGPL-2.1-or-later. */
#include "native_ui.h"
#include "controls_screen.h"
#include "showroom.h"
#include "online_screen.h"
#include "rider_enhancements.h"
#include "netplay_session.h"
#include "game_editor.h"
#include "demo_courses.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include "gpu.h"
#include "owner_assets.h"
#include "present_image_ring.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "hd_font_data.h"
#include <SDL3/SDL.h>
extern "C" CPUState*debug_cpu_ptr;
extern "C" uint8_t*memory_get_ram_ptr(void);
extern "C" bool SDLCALL __real_SDL_GL_SwapWindow(SDL_Window*);
extern "C" void __real_gpu_vblank_tick(void);
extern "C" void rr_ws_tag_text(uint32_t prim,int own,double anchor);   // gpu_geometry.py
static std::atomic<bool> upscale{false};
static bool enabled;static rrpc::NativeMenuView menu;
static std::atomic<bool> wide_frontend{false},wide{false},in_race{false};
static std::atomic<bool> frontend{false};static std::atomic<int> preview{-1},aspect{0};
static std::array<std::vector<uint32_t>,9> pictures;static std::array<std::vector<uint32_t>,6> demo_pictures;
static void load_art(){static bool loaded;if(loaded||!enabled)return;loaded=true;wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))return;auto*s=wcsrchr(path,L'\\');if(!s)return;wcscpy(s+1,L"..\\game-disc.bin");rr_art_want_models(1);if(!rr_art_open(path))return;
 // Twice the game's picture slot (256x112, rr_ui_preview), filled: the art
 // covers it, centred, so no letterbox band shows inside the panel frame.
 for(int k=1;k<9;k++){auto*a=rr_art_get(k);if(!a||!a->pixels||!a->w||!a->h)continue;auto&p=pictures[k];p.resize(512*224);double scale=std::max(512.0/a->w,224.0/a->h),ox=(a->w-512/scale)/2,oy=(a->h-224/scale)/2;
  for(int y=0;y<224;y++)for(int x=0;x<512;x++){unsigned sx=std::min(a->w-1,(unsigned)(ox+(x+0.5)/scale)),sy=std::min(a->h-1,(unsigned)(oy+(y+0.5)/scale));p[(size_t)y*512+x]=0xff000000u|a->pixels[(size_t)sy*a->w+sx];}}
 // Demo courses (demo_courses.cpp): the beta loading card's polaroid on black,
 // its top 200 rows (above the loading bar) fitted whole into the slot.
 for(int k=0;k<6;k++){auto*a=rr_demo_card(k);if(!a||!a->pixels||a->w<64||a->h<64)continue;auto&p=demo_pictures[k];p.assign(512*224,0xff000000u);
  const unsigned ch=std::min(a->h,200u);const double scale=std::min(512.0/a->w,224.0/ch);const int dw=(int)(a->w*scale),dh=(int)(ch*scale),ox=(512-dw)/2,oy=(224-dh)/2;
  for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){unsigned sx=std::min(a->w-1,(unsigned)((x+0.5)/scale)),sy=std::min(ch-1,(unsigned)((y+0.5)/scale));p[(size_t)(oy+y)*512+ox+x]=0xff000000u|a->pixels[(size_t)sy*a->w+sx];}}
}
// The owner art filling the game's picture slot: a PC menu page's, else the
// selected demo course's card.
// Rider look preview (art 9): the rider on the bike in 3D (owner_assets.c
// rr_look_preview3d), turning once every 12 s. A worker thread draws it about
// 20 times a second while the page asks for it, into the buffer the renderer is
// not reading, so the emulation thread never waits; the flat outfit sheet when
// the disc's models could not be read.
static std::array<std::vector<uint32_t>,2> look_pictures;static std::atomic<int> look_front{-1};
static std::atomic<int> look_want{0};static std::atomic<long long> look_asked{0};
static long long look_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int rrpc::look_preview_art(int outfit,int colour,int paint){
 look_want.store(outfit*4096+colour*64+paint);look_asked.store(look_ms());
 static std::once_flag started;
 std::call_once(started,[]{std::thread([]{
  for(;;){
   const long long t=look_ms();
   if(t-look_asked.load()>500){std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;}
   const int k=look_want.load(),outfit=k/4096,colour=k/64%64,paint=k%64,back=look_front.load()==0?1:0;
   auto&p=look_pictures[back];p.assign(512*224,0xff000000u);
   if(rr_look_preview3d(outfit,colour,paint,std::fmod(t*0.03,360.0),p.data(),512,224,2)||rr_look_preview(outfit,colour,p.data(),512,224))look_front.store(back);
   std::this_thread::sleep_for(std::chrono::milliseconds(45));}
 }).detach();});
 return look_front.load()>=0?9:2;
}
static const std::vector<uint32_t>*slot_picture(){
 const int k=preview.load();if(k>=1&&k<9&&!pictures[k].empty())return &pictures[k];
 if(k==9){const int f=look_front.load();if(f>=0&&!look_pictures[f].empty())return &look_pictures[f];}
 const int d=rrpc::demo_preview();if(d>=0&&d<6&&!demo_pictures[d].empty())return &demo_pictures[d];
 return nullptr;
}
struct LabelSpan{uint32_t sp,start,arena;int x,y;bool fit,panel;uint32_t str;};static LabelSpan labels[32];
static void learn_glyphs(const LabelSpan&span,uint32_t end);   // HD font, below
static bool ram(uint32_t p,unsigned n){uint32_t o=p&0x1fffffffu;return (p<0x200000u||(p>=0x80000000u&&p<0x80200000u)||(p>=0xa0000000u&&p<0xa0200000u))&&o<=0x200000u&&n<=0x200000u-o;}
static uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),4);return v;}
static int half(uint32_t p){int16_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),2);return v;}
static void put(uint32_t p,int v){int16_t h=(int16_t)v;std::memcpy(memory_get_ram_ptr()+(p&0x1fffff),&h,2);}
static void picture_pace(CPUState*c,uint32_t pc);   // 60 Hz frontend, below
static bool sky_observe(uint32_t pc);   // widescreen horizon, below
extern "C" const char*rr_menu_title_text(uint64_t hash);   // menu_titles.cpp
extern "C" const char*rr_menu_title_match(uint64_t hash);  // menu_titles.cpp
namespace rrpc {
void menu_titles_page(int page);void menu_titles_tick();   // menu_titles.cpp
void native_ui_config(bool on,bool hd){enabled=on;load_art();upscale.store(on&&hd);}
void native_ui_menu(const NativeMenuView&v){menu=v;preview.store(v.open&&v.page>=0?v.art:-1);menu_titles_page(v.open&&v.page>=0?v.page:-1);}
void native_ui_scene(bool racing,bool front,int a){aspect.store(a);wide.store(a!=0);frontend.store(front&&!racing);wide_frontend.store(a!=0&&front&&!racing);in_race.store(racing);static int last=-1;if(last!=(int)racing){gpu_ws_set_auto_ui_squash(racing);last=racing;}}
void native_ui_font(CPUState*c){
 // Measure the complete native glyph run, never individual letters. Only our
 // borrowed option rows have these bounds. Original menus keep their layout.
 bool fit=menu.open&&menu.page>=0&&c->gpr[6]==37&&c->gpr[7]>=50&&c->gpr[7]<=190&&(c->gpr[7]-50)%20==0;   // rider_menu.cpp first_row
 if(enabled&&frontend.load())menu_titles_tick();   // each page's heading (menu_titles.cpp)
 // Labels are also tracked for HD UI: the GPU draws tagged lettering with the
 // text reconstruction in every aspect.
 if(!enabled||(!fit&&!wide.load()&&!upscale.load()))return;
 uint32_t arena=word(0x8005b470);if(!ram(arena,0x110))return;
 // Pause panel text: the menu rows (0x8002CC20) and the yes/no confirm line
 // (0x8002DA00..0x8002DC40, directly or through the 0x8002CD78 string wrapper,
 // whose caller is saved at sp+0x18). It belongs to the centred panel.
 uint32_t origin=c->gpr[31];if(origin==0x8002CDB8&&ram(c->gpr[29]+0x18,4))origin=word(c->gpr[29]+0x18);
 bool panel=origin==0x8002CC20||(origin>=0x8002DA00&&origin<0x8002DC40);
 for(auto&s:labels)if(!s.sp||s.sp==c->gpr[29]){s={c->gpr[29],word(arena+0x10c),arena,(int16_t)c->gpr[6],(int16_t)c->gpr[7],fit,panel,c->gpr[5]};psx_mod_counter_add("rr.ui.label_starts",1);break;}
}
static void finish(LabelSpan span,uint32_t end){
 if(!span.sp||!ram(span.arena,0x110))return;
 psx_mod_counter_add("rr.ui.label_ends",1);
 if(std::getenv("RR_FONT_TRACE")){static int n;if(n++<400)std::fprintf(stderr,"FONT sp=%x start=%x end=%x x=%d y=%d glyphs=%d op=%x/%x\n",span.sp,span.start,end,span.x,span.y,(int)(end-span.start)/40,ram(span.start,8)?word(span.start):0,ram(span.start,8)?word(span.start+4):0);}
 if(end<=span.start||end-span.start>40*64||(end-span.start)%40||!ram(span.start,end-span.start))return;
 // The primitive ring wraps: a label that does not fit before the ring's end is
 // written from the ring base (arena+0xF0) while the arena pointer we sampled
 // still advances past the end. Every few seconds one label was left untouched
 // (unfitted, stretched in widescreen) for that build.
 auto glyphs=[](uint32_t s,uint32_t n){if(!ram(s,n))return false;for(uint32_t p=s;p<s+n;p+=40)if((memory_get_ram_ptr()[(p+7)&0x1fffff]&0xfc)!=0x2c)return false;return true;};
 if(!glyphs(span.start,end-span.start)){uint32_t base=word(span.arena+0xf0)&0x1fffff,n=end-span.start;if(!glyphs(base,n))return;span.start=base;end=base+n;psx_mod_counter_add("rr.ui.wrapped_labels",1);}
 int left=32767,right=-32768,top=32767,bottom=-32768;
 for(uint32_t p=span.start;p<end;p+=40){if((memory_get_ram_ptr()[(p+7)&0x1fffff]&0xfc)!=0x2c)return;for(unsigned i=0;i<4;i++){int x=half(p+8+i*8),y=half(p+10+i*8);left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}}
 learn_glyphs(span,end);
 // 6-pixel right inset inside the original 164x18 bar. Uniform scaling keeps
 // the owner's lettering proportions; a long value can never cross a frame.
 double scale=std::min(1.0,std::min(141.0/std::max(1,right-left),14.0/std::max(1,bottom-top)));
 int x=std::max(37,std::min(left,178-(int)((right-left)*scale))),y=span.y+(14-(int)((bottom-top)*scale))/2;
 if(span.fit){if(scale<1.0||left<37||right>178||top<span.y||bottom>span.y+14){for(uint32_t p=span.start;p<end;p+=40)for(unsigned i=0;i<4;i++){put(p+8+i*8,x+(int)((half(p+8+i*8)-left)*scale));put(p+10+i*8,y+(int)((half(p+10+i*8)-top)*scale));}right=x+(int)((right-left)*scale);left=x;psx_mod_counter_add("rr.ui.fitted_labels",1);}psx_mod_counter_add("rr.ui.checked_labels",1);}
 if(!wide.load()&&!upscale.load())return;
 if(in_race.load()){
  // Race text (rider names under their health bars, messages) follows the HUD's
  // thirds rule, so a name keeps the screen edge of the bars it belongs to and a
  // centred message stays centred. Pause panel text stays with the centred panel
  // ("NO" of "RESTART GAME YES \ NO" starts right of 2/3 and was pulled to the
  // screen edge).
  int w=gpu_ws_display_width(),c=left+right,edge=span.panel?0:3*c<2*w?-1:3*c>4*w?1:0;
  for(uint32_t p=span.start;p<end;p+=40){if(wide.load())gpu_ws_tag_hud_prim(p,edge);rr_ws_tag_text(p,0,edge);}
  return;
 }
 // Menus, results and loading are stretched to the window. Each label keeps the
 // original glyph proportions by narrowing about the point the game laid it out
 // from (its x argument: the left of left-aligned text, the centre of centred
 // text, the right of right-aligned numbers), so it stays where the game put it
 // inside its stretched box. The tag lasts as long as the packet, so a label
 // the game redraws for several VBlanks never flips back to stretched.
 double anchor=std::min<double>(std::max(span.x,left),right);
 for(uint32_t p=span.start;p<end;p+=40)rr_ws_tag_text(p,1,anchor);
 psx_mod_counter_add("rr.ui.frontend_labels",1);
}
void native_ui_return(CPUState*c){for(auto&s:labels)if(s.sp==c->gpr[29]){auto span=s;s={};finish(span,word(span.arena+0x10c));break;}}
void native_ui_observe(uint32_t pc){if(sky_observe(pc))return;if(pc==0x800627BC||pc==0x80062824||pc==0x80062AB0||pc==0x80062C00){if(auto*c=debug_cpu_ptr)::picture_pace(c,pc);return;}if(pc!=0x8002d058&&pc!=0x8002d078)return;auto*c=debug_cpu_ptr;if(!c)return;for(auto&s:labels)if(s.sp==c->gpr[29]+0x58){auto span=s;s={};finish(span,pc==0x8002d058?span.start+c->gpr[21]*40:word(span.arena+0x10c));break;}}
}
// HD UI sampling mode for the GPU texture shader (gpu_presentation.py): 4 =
// smooth reconstruction of lettering/CLUT art (Catmull-Rom colour and coverage,
// hard 50% cut: round letter shapes, sharp edges); RR_UI_SMOOTH=0 = 3, xBR
// level 2 (edge-directed; kept for comparison, it left the font's staircase).
extern "C" int rr_ui_texture_filter(void){
 static int smooth=-1;if(smooth<0){const char*e=std::getenv("RR_UI_SMOOTH");smooth=!(e&&e[0]=='0');}
 return upscale.load()?(smooth?4:3):-1;
}
// Lettering style while HD UI is on: 1 = HD font (Roboto Flex drawn in each
// glyph's box, below). HD UI is one switch: on = smoothed UI art + HD text,
// off = the original look, so the old launcher.ini hd_font key is not read.
// RR_TEXT_FONT=original|hd|traced overrides it for A/B runs: 0 = the game's
// own glyphs with the HD UI smoothing, 2 = the outline traced from them.
static int hd_font_mode(){
 static int mode=-1;
 if(mode<0){
  const char*e=std::getenv("RR_TEXT_FONT");
  mode=!e?1:!std::strcmp(e,"original")?0:!std::strcmp(e,"traced")?2:1;
 }
 return mode;
}
// Mode for lettering tagged by native_ui (gpu_presentation.py s_rr_text): 6 =
// distance-field lettering from a per-page atlas (rr_text_sdf_build, with the HD
// font drawn over it by rr_text_hd_apply; the renderer falls back to the HD UI
// mode for pages whose CLUT is not a level ramp). RR_TEXT_SDF=0 or the original
// font keep the HD UI mode. The per-pixel two-layer reconstruction (mode 5,
// RR_TEXT_VEC=1) left blobs and stays experimental.
extern "C" int rr_ui_text_filter(void){
 static int vec=-1,sdf=-1;if(vec<0){const char*e=std::getenv("RR_TEXT_VEC");vec=e&&e[0]=='1';e=std::getenv("RR_TEXT_SDF");sdf=!(e&&e[0]=='0');}
 return upscale.load()?(vec?5:sdf&&hd_font_mode()?6:rr_ui_texture_filter()):-1;
}
// HD font: which character each glyph box of a font page holds is learned from
// the game itself. The text routine (func_8002CDC8) gets its string in a1, and
// writes one glyph packet per printable character, in order, so each packet's
// page, CLUT and texel box pair with a character (spaces draw nothing). A label
// whose packet count does not match its printable characters is not used.
// Learning happens when the packets are written, before the GPU draws them, so
// the renderer finds every box it draws already paired. Nothing is stored.
namespace {
// fill/ring: CLUT indices of a pixel-art glyph's fill and outline (HUD digits);
// -1: an anti-aliased font glyph, measured from its level ramp.
struct HdGlyph{int px,py,cx,cy,u,v,w,h;char ch;int fill,ring;uint64_t key;};
struct PageKind{int px,py,cx,cy;bool outlined;};
std::mutex hd_mu;                       // the glyph list and page kinds
std::vector<HdGlyph> hd_glyphs;         // append only
std::vector<uint64_t> hd_keys;          // sorted: one entry per learned box
std::vector<PageKind> page_kinds;
std::atomic<uint32_t> hd_count{0};
std::atomic<uint32_t> hd_pages{0};      // bit per texture page (GP0 page bits 0-4) with paired boxes
}
static uint64_t hd_key(int tp,int clut,int u,int v,int w,int h){
 return (uint64_t)(tp&0x1f)|(uint64_t)(clut&0xffff)<<5|(uint64_t)u<<21|(uint64_t)v<<29|(uint64_t)w<<37|(uint64_t)h<<45;
}
static void hd_add(int tp,int clut,int u,int v,int w,int h,char ch,int fill,int ring){   // hd_mu held
 const uint64_t key=hd_key(tp,clut,u,v,w,h);
 auto at=std::lower_bound(hd_keys.begin(),hd_keys.end(),key);
 if(at!=hd_keys.end()&&*at==key)return;
 hd_keys.insert(at,key);
 hd_glyphs.push_back({(tp&15)*64,((tp>>4)&1)*256,(clut&63)*16,(clut>>6)&511,u,v,w,h,ch,fill,ring,key});
 hd_pages.fetch_or(1u<<(tp&0x1f));
 hd_count.store((uint32_t)hd_glyphs.size());
 if(std::getenv("RR_FONT_TRACE"))std::fprintf(stderr,"HD glyph '%c' page %d,%d clut %d,%d box %d,%d %dx%d\n",ch,(tp&15)*64,((tp>>4)&1)*256,(clut&63)*16,(clut>>6)&511,u,v,w,h);
}
// The race HUD's numbers (speed, position, lap) are pixel-art sprites from two
// digit strips on page 960,0, not drawn by the text routine: white fill (CLUT
// index 0) with a black outline (index 1) for the large digits, white only for
// the small ones. Their characters follow the strips' order. Only coordinates
// and characters are listed here.
static void seed_hud(){   // hd_mu held
 if(hd_font_mode()!=1)return;
 const int tp=0x0f,big=(151<<6)|(976>>4),small=(151<<6)|(1008>>4);
 const char*digits="1234567890";
 for(int i=0;i<10;i++){hd_add(tp,big,12*i,94,12,16,digits[i],0,1);hd_add(tp,small,48+6*i,112,6,9,digits[i],0,-1);}
 hd_add(tp,small,108,112,6,9,'/',0,-1);hd_add(tp,small,119,112,2,9,'.',0,-1);
 // Race start countdown: 3, 2, 1 as 17-18 x 21 sprites on the same page (CLUT
 // 960,151, an anti-aliased level ramp with a dark rim), drawn mid-screen.
 const int count=(151<<6)|(960>>4);
 hd_add(tp,count,128,0,17,21,'3',-1,-1);hd_add(tp,count,145,0,18,21,'2',-1,-1);hd_add(tp,count,163,0,17,21,'1',-1,-1);
}
static std::once_flag hd_seeded;
static void ensure_seed(){std::call_once(hd_seeded,[]{std::lock_guard<std::mutex> lock(hd_mu);seed_hud();});}
static void note_page_kind(int px,int py,int cx,int cy,bool outlined){
 std::lock_guard<std::mutex> lock(hd_mu);
 for(auto&k:page_kinds)if(k.px==px&&k.py==py&&k.cx==cx&&k.cy==cy){k.outlined=outlined;return;}
 page_kinds.push_back({px,py,cx,cy,outlined});
}
static void learn_glyphs(const LabelSpan&span,uint32_t end){
 if(hd_font_mode()!=1||!span.str||end<=span.start)return;
 char chars[64];unsigned n=0;
 for(uint32_t p=span.str;ram(p,1);p++){const unsigned char c=memory_get_ram_ptr()[p&0x1fffff];if(!c)break;if(c>' '){if(n==64)return;chars[n++]=(char)c;}if(p-span.str>256)return;}
 const uint32_t count=(end-span.start)/40;
 if(n!=count){psx_mod_counter_add("rr.text.hd_unpaired_labels",1);return;}
 auto*r=memory_get_ram_ptr();
 ensure_seed();
 std::lock_guard<std::mutex> lock(hd_mu);
 for(uint32_t i=0;i<count;i++){
  const uint32_t p=(span.start+i*40)&0x1fffff;
  int u0=255,v0=255,u1=0,v1=0;
  for(unsigned k=0;k<4;k++){const int u=r[p+12+k*8],v=r[p+13+k*8];u0=std::min(u0,u);v0=std::min(v0,v);u1=std::max(u1,u);v1=std::max(v1,v);}
  const uint16_t clut=(uint16_t)(r[p+14]|r[p+15]<<8),tp=(uint16_t)(r[p+22]|r[p+23]<<8);
  if(((tp>>7)&3)||u1<=u0||v1<=v0||chars[i]<RR_HD_FIRST||chars[i]>RR_HD_LAST)continue;
  hd_add(tp,clut,u0,v0,u1-u0,v1-v0,chars[i],-1,-1);
 }
}
extern "C" uint32_t rr_text_hd_serial(void){return hd_count.load();}
// Some lettering on the same pages is drawn by other routines (the menus'
// "Select / Help" line, the HUD numbers): the renderer asks whether a primitive
// samples a box already paired with a character, and draws it with the HD font.
extern "C" int rr_text_hd_known(int tpage,int clut,int u,int v,int w,int h){
 ensure_seed();
 if(!(hd_pages.load()&(1u<<(tpage&0x1f)))||u<0||v<0||w<=0||h<=0||u>255||v>255||w>255||h>255)return 0;
 const uint64_t key=hd_key(tpage,clut,u,v,w,h);
 std::lock_guard<std::mutex> lock(hd_mu);
 return std::binary_search(hd_keys.begin(),hd_keys.end(),key);
}
// A page whose CLUT is not a level ramp (the HUD digits') still gets an atlas
// when HD glyphs are listed for it: empty, so only those boxes draw.
static bool hd_listed(int px,int py,int cx,int cy){
 std::lock_guard<std::mutex> lock(hd_mu);
 for(const auto&g:hd_glyphs)if(g.px==px&&g.py==py&&g.cx==cx&&g.cy==cy&&g.fill>=0)return true;
 return false;
}
// Traced lettering, built at run time from the owner's font pages (no artwork
// is stored or shipped). The game's fonts are anti-aliased: each 4-bit texel
// indexes a monotone CLUT ramp (grey 0..31 in steps of 2) and the colour comes
// from the vertex colour. Two kinds of page occur:
//  - coverage fonts (menus, help, race labels): the level is how much of the
//    texel the letter covers, so the letter's outline is where it crosses one
//    half of the piece's brightest level;
//  - outlined fonts (results): a bright fill inside a darker outline band of
//    mid levels, rimmed by the lowest levels. Recognised per page: most glyph
//    pieces' fill never touches the background.
// Steps:
//  1. Levels per texel from the CLUT ramp. Each 8-connected glyph piece keeps
//     its own brightest level m (its fill). Coverage pieces dimmer than 2/3
//     (thin bars) take a proportionally lower cut and hairlines dimmer than 1/2
//     (the box glyph) are scaled to their own peak, so each keeps its stroke;
//     stray specks of the lowest levels are dropped.
//  2. Marching squares on the texel-centre grid: at one half (coverage) or at
//     0.7 m (fill: the mid levels inside counters and between an i's dot and
//     stem stay open) and 0.4 m (outer edge of the outline) for outlined
//     pages.
//     About one vertex per texel, placed with sub-texel accuracy.
//  3. Corners (turns over 55 degrees) are pinned; Taubin smoothing (no
//     shrinking) removes the texel staircase between them, and a Catmull-Rom
//     spline through the smoothed vertices rounds curves.
//  4. The contours are baked into a K x K per texel RGBA atlas: R = signed
//     distance to the fill outline (inside positive, +-2 texels in 0..255),
//     G = the fill level m (bit 7: an outlined page, whose shadow rim the GPU
//     adds), B = signed distance to the outline's outer edge,
//     A = the median level of the piece's outline ring (outlined pages).
//     Coverage pages have no ring: B is the fill outline and A the fill level,
//     because their dim edge texels are the original's anti-aliasing against
//     black, which the GPU replaces with a one-pixel anti-aliased edge into the
//     background (gpu_presentation.py rr_text_sdf). Pieces over 1024 texels
//     are art sharing the page (HUD panels), not lettering, and are skipped.
// Returns 0 when the CLUT is not such a ramp (the page keeps the HD UI mode).
namespace {
struct SdfPt{float x,y;};
}
extern "C" int rr_text_sdf_build(const uint16_t*vram,int px,int py,int cx,int cy,uint8_t*out,int K){
 const int N=256,P=N+2,FW=N*K;const float BAND=1.2f,RANGE=2.0f;   // distances computed within BAND, encoded +-RANGE
 if(px<0||px>1024-64||(py!=0&&py!=256)||cx<0||cx>1024-16||cy<0||cy>511||K<1)return 0;
 const uint16_t*clut=vram+(size_t)cy*1024+cx;
 float L[16];
 for(int i=0;i<16;i++){const uint16_t c=clut[i];L[i]=(float)((c&31)+((c>>5)&31)+((c>>10)&31));}
 bool ramp=!clut[0]&&L[15]>0;
 for(int i=1;i<16;i++)if(!clut[i]||L[i]<L[i-1])ramp=false;
 if(!ramp){
  if(!hd_listed(px,py,cx,cy))return 0;
  std::memset(out,0,(size_t)FW*FW*4);   // distance -2: nothing outside the listed boxes
  return 1;
 }
 for(int i=0;i<16;i++)L[i]/=L[15];
 std::vector<uint8_t> idx((size_t)N*N);
 for(int v=0;v<N;v++)for(int u=0;u<N;u++)idx[v*N+u]=(vram[(size_t)(py+v)*1024+px+(u>>2)]>>((u&3)*4))&15;
 auto at=[&](int u,int v){return u<0||v<0||u>=N||v>=N?0:idx[v*N+u];};
 // 1. Glyph pieces, their fill level, and the page's kind.
 std::vector<int> lab((size_t)N*N,0),stack;std::vector<uint8_t> top(1,0);
 for(int s=0;s<N*N;s++){
  if(!idx[s]||lab[s])continue;
  const int id=(int)top.size();uint8_t m=0;lab[s]=id;stack.assign(1,s);
  while(!stack.empty()){const int p=stack.back();stack.pop_back();m=std::max(m,idx[p]);const int x=p%N,y=p/N;
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){const int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=N||yy>=N)continue;const int q=yy*N+xx;if(idx[q]&&!lab[q]){lab[q]=id;stack.push_back(q);}}}
  top.push_back(m);
 }
 const size_t np=top.size();
 std::vector<int> size(np,0),fills(np,0),touch(np,0);std::vector<std::array<int,16>> hist(np);
 for(auto&h:hist)h.fill(0);
 for(int v=0;v<N;v++)for(int u=0;u<N;u++){const int s=v*N+u,id=lab[s];if(!id)continue;const int m=top[id];
  size[id]++;hist[id][idx[s]]++;
  if(L[idx[s]]>=0.8f*L[m]){fills[id]++;if(!at(u-1,v)||!at(u+1,v)||!at(u,v-1)||!at(u,v+1))touch[id]++;}}
 int glyphs=0,sealed=0;
 for(size_t id=1;id<np;id++)if(size[id]>=12&&top[id]>=8){glyphs++;if(fills[id]&&!touch[id])sealed++;}
 const bool outlined=glyphs>0&&sealed*10>glyphs*4;
 const float T1=outlined?0.7f:0.5f,T2=0.4f;
 std::vector<uint8_t> line(np,0);
 for(size_t id=1;id<np;id++){const int m=top[id];line[id]=(uint8_t)m;
  if(!outlined)continue;int n=0;for(int k=1;k<=m;k++)if(L[k]>=T2*L[m]&&L[k]<T1*L[m])n+=hist[id][k];
  for(int k=1,c=0;k<=m&&n;k++)if(L[k]>=T2*L[m]&&L[k]<T1*L[m]&&(c+=hist[id][k])*2>=n){line[id]=(uint8_t)k;break;}}
 std::vector<float> G((size_t)P*P,0.0f);   // node (i,j) = texel (i-1,j-1), at (i-0.5,j-0.5)
 for(int v=0;v<N;v++)for(int u=0;u<N;u++){
  const int s=v*N+u;if(!idx[s])continue;const int m=top[lab[s]];if(m<=2||size[lab[s]]>1024)continue;   // specks; art, not lettering
  G[(size_t)(v+1)*P+u+1]=L[idx[s]]/(outlined||L[m]<0.5f?L[m]:std::min(1.0f,1.5f*L[m]));
 }
 auto g=[&](int i,int j){return G[(size_t)j*P+i];};
 std::vector<float> best((size_t)FW*FW,BAND);std::vector<int> owner((size_t)FW*FW,0);
 for(int fy=0;fy<FW;fy++)for(int fx=0;fx<FW;fx++)owner[(size_t)fy*FW+fx]=lab[(fy/K)*N+fx/K];
 int loops=0;
 // 2-4 for one cut: unsigned distance within the band, even-odd crossings per row.
 auto trace=[&](float TH,std::vector<float>&dist,std::vector<uint8_t>&par){
  // Marching squares; edge ids: 2*(j*P+i) horizontal (i,j)-(i+1,j), +1 vertical (i,j)-(i,j+1).
  std::vector<int> nxt((size_t)P*P*2,-1);
  for(int j=0;j<P-1;j++)for(int i=0;i<P-1;i++){
   const float a=g(i,j),b=g(i+1,j),c=g(i+1,j+1),d=g(i,j+1);
   const int m=(a>=TH)|((b>=TH)<<1)|((c>=TH)<<2)|((d>=TH)<<3);
   if(m==0||m==15)continue;
   const int T=2*(j*P+i),R=2*(j*P+i+1)+1,B=2*((j+1)*P+i),Lf=2*(j*P+i)+1;
   auto seg=[&](int e0,int e1){nxt[e0]=e1;};
   const bool cen=(a+b+c+d)*0.25f>=TH;
   switch(m){
    case 1:seg(Lf,T);break;case 2:seg(T,R);break;case 3:seg(Lf,R);break;case 4:seg(R,B);break;
    case 6:seg(T,B);break;case 7:seg(Lf,B);break;case 8:seg(B,Lf);break;case 9:seg(B,T);break;
    case 11:seg(B,R);break;case 12:seg(R,Lf);break;case 13:seg(R,T);break;case 14:seg(T,Lf);break;
    case 5:if(cen){seg(Lf,B);seg(R,T);}else{seg(Lf,T);seg(R,B);}break;
    case 10:if(cen){seg(T,Lf);seg(B,R);}else{seg(T,R);seg(B,Lf);}break;
   }
  }
  auto point=[&](int e){const int k=e>>1,i=k%P,j=k/P;
   if(!(e&1)){const float a=g(i,j),b=g(i+1,j);return SdfPt{i-0.5f+(TH-a)/(b-a),j-0.5f};}
   const float a=g(i,j),b=g(i,j+1);return SdfPt{i-0.5f,j-0.5f+(TH-a)/(b-a)};};
  auto piece=[&](int e){const int k=e>>1,i=k%P,j=k/P,i2=(e&1)?i:i+1,j2=(e&1)?j+1:j;int id=0;
   for(int t=0;t<2;t++){const int u=(t?i2:i)-1,v=(t?j2:j)-1;if(u>=0&&v>=0&&u<N&&v<N&&lab[v*N+u]&&top[lab[v*N+u]]>=top[id])id=lab[v*N+u];}
   return id;};
  std::vector<uint8_t> seen(nxt.size(),0);std::vector<SdfPt> v,w,sp;std::vector<char> pin;
  for(size_t e0=0;e0<nxt.size();e0++){
   if(nxt[e0]<0||seen[e0])continue;
   v.clear();for(int e=(int)e0;e>=0&&!seen[e];e=nxt[e]){seen[e]=1;v.push_back(point(e));}
   const int n=(int)v.size();if(n<3)continue;
   const int id=piece((int)e0);loops++;
   // 3. Corners, Taubin smoothing between them, Catmull-Rom spline.
   pin.assign(n,0);
   if(n>=8)for(int i=0;i<n;i++){const SdfPt&a=v[(i+n-2)%n],&b=v[i],&c=v[(i+2)%n];
    const float ax=b.x-a.x,ay=b.y-a.y,bx=c.x-b.x,by=c.y-b.y,la=std::sqrt(ax*ax+ay*ay),lb=std::sqrt(bx*bx+by*by);
    if(la>1e-6f&&lb>1e-6f&&(ax*bx+ay*by)/(la*lb)<0.5736f)pin[i]=1;}   // cos 55 degrees
   if(n>=8)for(int it=0;it<30;it++)for(int pass=0;pass<2;pass++){const float f=pass?-0.53f:0.5f;w=v;
    for(int i=0;i<n;i++)if(!pin[i]){const SdfPt&a=w[(i+n-1)%n],&c=w[(i+1)%n];v[i].x+=f*(0.5f*(a.x+c.x)-w[i].x);v[i].y+=f*(0.5f*(a.y+c.y)-w[i].y);}}
   sp.clear();
   for(int i=0;i<n;i++){SdfPt p0=v[(i+n-1)%n];const SdfPt p1=v[i],p2=v[(i+1)%n];SdfPt p3=v[(i+2)%n];
    if(pin[i])p0={2*p1.x-p2.x,2*p1.y-p2.y};
    if(pin[(i+1)%n])p3={2*p2.x-p1.x,2*p2.y-p1.y};
    for(int s=0;s<4;s++){const float t=s*0.25f,t2=t*t,t3=t2*t;
     sp.push_back({0.5f*(2*p1.x+(-p0.x+p2.x)*t+(2*p0.x-5*p1.x+4*p2.x-p3.x)*t2+(-p0.x+3*p1.x-3*p2.x+p3.x)*t3),
                   0.5f*(2*p1.y+(-p0.y+p2.y)*t+(2*p0.y-5*p1.y+4*p2.y-p3.y)*t2+(-p0.y+3*p1.y-3*p2.y+p3.y)*t3)});}}
   // 4. Distance within the band and row crossings (even-odd inside test).
   const int ns=(int)sp.size();
   for(int i=0;i<ns;i++){const SdfPt a=sp[i],b=sp[(i+1)%ns];
    const float dx=b.x-a.x,dy=b.y-a.y,l2=std::max(dx*dx+dy*dy,1e-12f);
    const int x0=std::max(0,(int)std::floor((std::min(a.x,b.x)-BAND)*K)),x1=std::min(FW-1,(int)std::ceil((std::max(a.x,b.x)+BAND)*K));
    const int y0=std::max(0,(int)std::floor((std::min(a.y,b.y)-BAND)*K)),y1=std::min(FW-1,(int)std::ceil((std::max(a.y,b.y)+BAND)*K));
    for(int fy=y0;fy<=y1;fy++){const float py2=(fy+0.5f)/K;
     for(int fx=x0;fx<=x1;fx++){const float px2=(fx+0.5f)/K;
      const float t=std::min(1.0f,std::max(0.0f,((px2-a.x)*dx+(py2-a.y)*dy)/l2)),ex=px2-a.x-t*dx,ey=py2-a.y-t*dy,d=std::sqrt(ex*ex+ey*ey);
      const size_t o=(size_t)fy*FW+fx;if(d<dist[o])dist[o]=d;if(d<best[o]){best[o]=d;owner[o]=id;}}}
    if(a.y!=b.y){const float lo=std::min(a.y,b.y),hi=std::max(a.y,b.y);
     for(int r=std::max(0,(int)std::ceil(lo*K-0.5f));r<FW;r++){const float yc=(r+0.5f)/K;if(yc>=hi)break;if(yc<lo)continue;
      const float xc=a.x+(yc-a.y)/(b.y-a.y)*(b.x-a.x);const int c=std::min(FW,std::max(0,(int)std::ceil(xc*K-0.5f)));par[(size_t)r*(FW+1)+c]^=1;}}
   }
  }
  for(int fy=0;fy<FW;fy++){uint8_t in=0;   // signed: inside positive
   for(int fx=0;fx<FW;fx++){in^=par[(size_t)fy*(FW+1)+fx];const size_t o=(size_t)fy*FW+fx;if(!in)dist[o]=-dist[o];}}
 };
 std::vector<float> df((size_t)FW*FW,BAND),de;std::vector<uint8_t> par((size_t)FW*(FW+1),0);
 trace(T1,df,par);
 if(outlined){de.assign((size_t)FW*FW,BAND);std::fill(par.begin(),par.end(),0);trace(T2,de,par);}
 auto enc=[&](float d){return (uint8_t)std::lround(std::min(255.0f,std::max(0.0f,128.0f+d*127.0f/RANGE)));};
 for(size_t o=0;o<(size_t)FW*FW;o++){const int id=owner[o];
  out[o*4]=enc(df[o]);out[o*4+1]=(uint8_t)(top[id]|(outlined?0x80:0));out[o*4+2]=enc(outlined?de[o]:df[o]);out[o*4+3]=line[id];}
 note_page_kind(px,py,cx,cy,outlined);
 psx_mod_counter_add("rr.text.sdf_loops",(uint32_t)loops);
 if(outlined)psx_mod_counter_add("rr.text.sdf_outlined_pages",1);
 return loops>0;
}
// HD font: draws the learned glyphs of one page (those after *done) into its
// atlas (the layout above), replacing the traced glyph in each box. Each
// Roboto Flex letter (a heavy, wide instance, like the original's design) is
// fitted to the original letter in its box: the same height, top, width and
// centre (the original's fill measured to sub-texel precision from the level
// profile), so the game's own layout and spacing stay. Coverage pages take the
// regular style with a thin dark edge (0.2 texel, level m/7); outlined pages
// the bold italic with their outline ring (0.6 texel, level 7m/15) and the
// GPU's shadow rim (G bit 7). G bit 6 marks an HD glyph: its outline need not
// follow the original texels. Returns 1 and the changed atlas rectangle
// (rect: x0,y0,x1,y1 exclusive, atlas pixels) when anything was drawn.
extern "C" int rr_text_hd_apply(const uint16_t*vram,int px,int py,int cx,int cy,uint8_t*out,int K,uint32_t*done,int*rect){
 const int N=256,FW=N*K;const float BAND=2.0f;
 std::vector<HdGlyph> todo;bool outlined=false;
 {
  std::lock_guard<std::mutex> lock(hd_mu);
  for(size_t i=*done;i<hd_glyphs.size();i++){const auto&g=hd_glyphs[i];if(g.px==px&&g.py==py&&g.cx==cx&&g.cy==cy)todo.push_back(g);}
  *done=(uint32_t)hd_glyphs.size();
  for(auto&k:page_kinds)if(k.px==px&&k.py==py&&k.cx==cx&&k.cy==cy)outlined=k.outlined;
 }
 if(todo.empty())return 0;
 const uint16_t*clut=vram+(size_t)cy*1024+cx;float L[16];
 for(int i=0;i<16;i++){const uint16_t c=clut[i];L[i]=(float)((c&31)+((c>>5)&31)+((c>>10)&31));}
 auto level=[&](int u,int v){return (vram[(size_t)(py+v)*1024+px+(u>>2)]>>((u&3)*4))&15;};
 auto enc=[&](float d){return (uint8_t)std::lround(std::min(255.0f,std::max(0.0f,128.0f+d*127.0f/BAND)));};
 rect[0]=rect[1]=FW;rect[2]=rect[3]=0;int drawn=0;
 std::vector<float> rows,cols;std::vector<SdfPt> pts;std::vector<int> starts;std::vector<float> xs;std::vector<uint64_t> failed;
 for(const auto&g:todo){
  // The original letter's fill: its level profile across rows and columns
  // (pixel-art digits: where the fill index is), its fill and ring levels.
  const bool art=g.fill>=0;
  int m=0,ringl=0,style=outlined?1:0;float thr=0.5f,edge=outlined?0.6f:0.2f;uint8_t flags=0x40|(outlined?0x80:0);
  // Outlined digits are slanted (bold italic); plain ones are upright (regular).
  if(art){m=g.fill;ringl=g.ring>=0?g.ring:g.fill;style=g.ring>=0?1:0;edge=g.ring>=0?0.9f:0.0f;flags=0x40;}
  else{
   for(int v=0;v<g.h;v++)for(int u=0;u<g.w;u++)m=std::max(m,level(g.u+u,g.v+v));
   if(m<3||L[m]<=0){failed.push_back(g.key);continue;}
   thr=(outlined?0.7f:0.5f)*L[m];ringl=std::max(1,outlined?(7*m+7)/15:m/7);
  }
  rows.assign(g.h,0.0f);cols.assign(g.w,0.0f);
  for(int v=0;v<g.h;v++)for(int u=0;u<g.w;u++){const int k=level(g.u+u,g.v+v);const float l=art?(k==g.fill?1.0f:0.0f):L[k];rows[v]=std::max(rows[v],l);cols[u]=std::max(cols[u],l);}
  auto first=[&](const std::vector<float>&p){for(int i=0;i<(int)p.size();i++)if(p[i]>=thr){const float a=i?p[i-1]:0.0f;return i-0.5f+(thr-a)/std::max(p[i]-a,1e-6f);}return -1.0f;};
  auto last=[&](const std::vector<float>&p){for(int i=(int)p.size()-1;i>=0;i--)if(p[i]>=thr){const float b=i+1<(int)p.size()?p[i+1]:0.0f;return i+0.5f+(p[i]-thr)/std::max(p[i]-b,1e-6f);}return -1.0f;};
  const float top=first(rows),bottom=last(rows),left=first(cols),right=last(cols);
  if(top<0||bottom<=top||right<=left){failed.push_back(g.key);continue;}
  // The Roboto Flex letter, in 1/1024 em.
  const int16_t*d=rr_hd_data+rr_hd_index[style][g.ch-RR_HD_FIRST];const int nc=*d++;
  pts.clear();starts.clear();
  for(int c=0;c<nc;c++){const int np=*d++;starts.push_back((int)pts.size());for(int i=0;i<np;i++){pts.push_back({(float)d[0],(float)d[1]});d+=2;}}
  starts.push_back((int)pts.size());
  if(pts.empty())continue;
  float gx0=1e9f,gy0=1e9f,gx1=-1e9f,gy1=-1e9f;
  for(auto&p:pts){gx0=std::min(gx0,p.x);gx1=std::max(gx1,p.x);gy0=std::min(gy0,p.y);gy1=std::max(gy1,p.y);}
  // Width: the original letter's own width, so the game's spacing reads as it
  // did (the original is a wide design; Roboto letters at their own width
  // looked spaced out), kept within 0.7-2x of the letter's proportions.
  const float sy=(bottom-top)/std::max(gy1-gy0,1.0f),gw=std::max(gx1-gx0,1.0f);
  const float sx=std::min(std::max((right-left)/gw,0.7f*sy),2.0f*sy);
  const float ox=g.u+0.5f*(left+right)-0.5f*(gx0+gx1)*sx,oy=g.v+top-gy0*sy;
  for(auto&p:pts){p.x=ox+p.x*sx;p.y=oy+p.y*sy;}
  // Signed distance (inside positive) over the box, at K x K per texel.
  const int fx0=g.u*K,fy0=g.v*K,fx1=std::min(FW,(g.u+g.w)*K),fy1=std::min(FW,(g.v+g.h)*K);
  const uint8_t gl=(uint8_t)(m|flags),ring=(uint8_t)ringl;
  for(int fy=fy0;fy<fy1;fy++){
   const float yc=(fy+0.5f)/K;xs.clear();
   for(int c=0;c<nc;c++)for(int i=starts[c];i<starts[c+1];i++){const SdfPt a=pts[i],b=pts[i+1<starts[c+1]?i+1:starts[c]];
    if((a.y<=yc)!=(b.y<=yc))xs.push_back(a.x+(yc-a.y)/(b.y-a.y)*(b.x-a.x));}
   std::sort(xs.begin(),xs.end());
   for(int fx=fx0;fx<fx1;fx++){
    const float xc=(fx+0.5f)/K;float best=BAND*BAND;
    for(int c=0;c<nc;c++)for(int i=starts[c];i<starts[c+1];i++){const SdfPt a=pts[i],b=pts[i+1<starts[c+1]?i+1:starts[c]];
     const float dx=b.x-a.x,dy=b.y-a.y,t=std::min(1.0f,std::max(0.0f,((xc-a.x)*dx+(yc-a.y)*dy)/std::max(dx*dx+dy*dy,1e-12f)));
     const float ex=xc-a.x-t*dx,ey=yc-a.y-t*dy;best=std::min(best,ex*ex+ey*ey);}
    const bool in=(std::lower_bound(xs.begin(),xs.end(),xc)-xs.begin())&1;
    const float dist=in?std::sqrt(best):-std::sqrt(best);
    uint8_t*o=out+((size_t)fy*FW+fx)*4;o[0]=enc(dist);o[1]=gl;o[2]=enc(dist+edge);o[3]=ring;
   }
  }
  rect[0]=std::min(rect[0],fx0);rect[1]=std::min(rect[1],fy0);rect[2]=std::max(rect[2],fx1);rect[3]=std::max(rect[3],fy1);drawn++;
 }
 if(!failed.empty()){   // boxes that hold no letter here keep the game's own drawing
  std::lock_guard<std::mutex> lock(hd_mu);
  for(uint64_t k:failed){auto at=std::lower_bound(hd_keys.begin(),hd_keys.end(),k);if(at!=hd_keys.end()&&*at==k)hd_keys.erase(at);}
 }
 psx_mod_counter_add("rr.text.hd_glyphs_drawn",(uint32_t)drawn);
 return drawn>0;
}
// HD font: menu title banners ("MAIN MENU:", "TIME TRIALS:"). Each is a 336x32
// full-colour picture the game loads at VRAM 512,480 and draws as two sprites
// over the menu backdrop: lettering painted into a slice of the background.
// With the HD font the renderer drops those sprites (the backdrop beneath is
// plain) and draws this image in their place: the banner's words in Roboto
// Flex Black, a translucent blue-grey gradient with a soft dark shadow, like
// the original's look. The words are found from a fingerprint of the picture
// (rr_banner_text); unknown pictures keep the original. The new lettering takes
// the original's cap height and span, measured from the picture (its letters
// are the pixels with red >= 8 of 31; the background is a dark blue).
extern "C" uint64_t rr_banner_hash(const uint16_t*vram,int x,int y,int w,int h){
 uint64_t hsh=14695981039346656037ull;
 for(int r=0;r<h;r++)for(int c=0;c<w;c++){hsh^=vram[(size_t)(y+r)*1024+x+c];hsh*=1099511628211ull;}
 return hsh;
}
extern "C" const char*rr_banner_text(uint64_t hash){
 // FNV-1a of the 336x32 picture at VRAM 512,480 (SLUS-01053, read with
 // RR_BANNER_TRACE=1); words only, no image data.
 static const struct{uint64_t hash;const char*text;}known[]={
  {0x27bdf9c05f4312afull,"MAIN MENU:"},
  {0x34ddafe83899f10dull,"SINGLE PLAYER:"},
  {0xbd1423157154659bull,"TIME TRIALS:"},
  {0xa03a43153689220cull,"JAIL BREAK:"},
  {0xdd2bab5306bd1082ull,"FIVE-O:"},
  {0x2ea01c88133e4f85ull,"MULTIPLAYER:"},
  {0x745e187ad7d27337ull,"OPTIONS:"},
 };
 // The owner's words for a title (F2 editor, Game text page).
 for(const auto&k:known)if(k.text&&k.hash==hash)return rrpc::game_text_title(k.text);
 if(const char*t=rr_menu_title_text(hash))return rrpc::game_text_title(t);   // headings menu_titles.cpp put in the slot
 if(const char*t=rr_menu_title_match(hash))return rrpc::game_text_title(t);   // any other title from the disc's TITLES.STR (race-end WRECKED! ...)
 if(std::getenv("RR_BANNER_TRACE"))std::fprintf(stderr,"rr banner: unknown picture %016llx\n",(unsigned long long)hash);
 return nullptr;
}
// Draws text into out (w*S x h*S premultiplied RGBA8, S pixels per texel).
extern "C" int rr_banner_build(const uint16_t*vram,int x,int y,int w,int h,const char*text,uint8_t*out,int S){
 // The original lettering's rows and span.
 std::vector<int> rows(h,0),cols(w,0);
 for(int r=0;r<h;r++)for(int c=0;c<w;c++)if((vram[(size_t)(y+r)*1024+x+c]&31)>=8){rows[r]++;cols[c]++;}
 const int peak=*std::max_element(rows.begin(),rows.end());if(peak<8)return 0;
 int top=-1,bottom=-1,left=-1,right=-1;
 for(int r=0;r<h;r++)if(rows[r]*5>=peak){if(top<0)top=r;bottom=r+1;}
 for(int c=0;c<w;c++)if(cols[c]>=2){if(left<0)left=c;right=c+1;}
 if(top<0||left<0||bottom-top<4||right-left<8)return 0;
 // Lay the words out in 1/1024 em (style 2: black).
 const int style=2;
 struct Placed{float pen;int ch;};std::vector<Placed> glyphs;float pen=0,inkl=1e9f,inkr=-1e9f;
 auto outline=[&](int ch,std::vector<SdfPt>&pts,std::vector<int>&starts){
  pts.clear();starts.clear();const int16_t*d=rr_hd_data+rr_hd_index[style][ch-RR_HD_FIRST];const int nc=*d++;
  for(int c=0;c<nc;c++){const int np=*d++;starts.push_back((int)pts.size());for(int i=0;i<np;i++){pts.push_back({(float)d[0],(float)d[1]});d+=2;}}
  starts.push_back((int)pts.size());};
 std::vector<SdfPt> pts;std::vector<int> starts;
 for(const char*p=text;*p;p++){const int ch=(unsigned char)*p;
  if(ch>=RR_HD_FIRST&&ch<=RR_HD_LAST){outline(ch,pts,starts);glyphs.push_back({pen,ch});
   for(auto&q:pts){inkl=std::min(inkl,pen+q.x);inkr=std::max(inkr,pen+q.x);}}
  if(ch>=32&&ch<=RR_HD_LAST)pen+=rr_hd_advance[style][ch-32];}
 if(glyphs.empty()||inkr<=inkl)return 0;
 outline('H',pts,starts);float capt=0;for(auto&q:pts)capt=std::min(capt,q.y);
 const float sy=(bottom-top)/std::max(-capt,1.0f),sx=std::min(std::max((right-left)/(inkr-inkl),0.8f*sy),1.25f*sy);
 const float ox=left-inkl*sx,oy=top-capt*sy;   // texel position of pen 0 / baseline
 // Coverage at S x S per texel, 4 sub-rows per pixel row, even-odd spans.
 const int W=w*S,H=h*S;std::vector<float> cov((size_t)W*H,0.0f);std::vector<std::pair<SdfPt,SdfPt>> edges;
 for(const auto&g:glyphs){outline(g.ch,pts,starts);
  for(size_t c=0;c+1<starts.size();c++)for(int i=starts[c];i<starts[c+1];i++){const SdfPt a=pts[i],b=pts[i+1<starts[c+1]?i+1:starts[c]];
   edges.push_back({{(ox+(g.pen+a.x)*sx)*S,(oy+a.y*sy)*S},{(ox+(g.pen+b.x)*sx)*S,(oy+b.y*sy)*S}});}}
 std::vector<float> xs;
 for(int py=0;py<H;py++)for(int k=0;k<4;k++){
  const float yc=py+(k+0.5f)*0.25f;xs.clear();
  for(const auto&e:edges)if((e.first.y<=yc)!=(e.second.y<=yc))xs.push_back(e.first.x+(yc-e.first.y)/(e.second.y-e.first.y)*(e.second.x-e.first.x));
  std::sort(xs.begin(),xs.end());
  for(size_t i=0;i+1<xs.size();i+=2){
   const float a=std::max(0.0f,xs[i]),b=std::min((float)W,xs[i+1]);if(b<=a)continue;
   float*row=&cov[(size_t)py*W];const int ia=(int)a,ib=(int)b;
   if(ia==ib){row[ia]+=0.25f*(b-a);continue;}
   row[ia]+=0.25f*(ia+1-a);for(int px=ia+1;px<ib;px++)row[px]+=0.25f;if(ib<W)row[ib]+=0.25f*(b-ib);
  }
 }
 // Soft shadow: the coverage blurred (three box passes) and offset down-right.
 const float capS=(bottom-top)*S;const int rad=std::max(1,(int)(0.06f*capS)),dx=(int)(0.08f*capS),dy=(int)(0.11f*capS);
 std::vector<float> sh(cov),tmp(sh.size());
 for(int pass=0;pass<3;pass++){
  for(int py=0;py<H;py++){float acc=0;const float*s=&sh[(size_t)py*W];float*t=&tmp[(size_t)py*W];
   for(int px=-rad;px<W+rad;px++){if(px+rad<W)acc+=s[px+rad];if(px-rad-1>=0)acc-=s[px-rad-1];if(px>=0&&px<W)t[px]=acc/(2*rad+1);}}
  for(int px=0;px<W;px++){float acc=0;
   for(int py=-rad;py<H+rad;py++){if(py+rad<H)acc+=tmp[(size_t)(py+rad)*W+px];if(py-rad-1>=0)acc-=tmp[(size_t)(py-rad-1)*W+px];if(py>=0&&py<H)sh[(size_t)py*W+px]=acc/(2*rad+1);}}
 }
 const float topc[3]={190/255.0f,200/255.0f,240/255.0f},botc[3]={120/255.0f,130/255.0f,190/255.0f},shc[3]={0,0,20/255.0f};
 for(int py=0;py<H;py++){
  const float t=std::min(1.0f,std::max(0.0f,(py+0.5f-top*S)/std::max(capS,1.0f)));
  for(int px=0;px<W;px++){
   const float at=0.82f*std::min(1.0f,cov[(size_t)py*W+px]);
   const int sx2=px-dx,sy2=py-dy;const float as=(sx2>=0&&sy2>=0)?0.8f*std::min(1.0f,sh[(size_t)sy2*W+sx2]):0.0f;
   const float a=at+as*(1-at);uint8_t*o=out+((size_t)py*W+px)*4;
   for(int i=0;i<3;i++){const float c=(topc[i]*(1-t)+botc[i]*t)*at+shc[i]*as*(1-at);o[i]=(uint8_t)std::lround(255*std::min(1.0f,c));}
   o[3]=(uint8_t)std::lround(255*std::min(1.0f,a));
  }
 }
 psx_mod_counter_add("rr.text.banners_built",1);
 return 1;
}
extern "C" int rr_ui_wide_frontend(void){return wide_frontend.load();}
extern "C" int rr_ui_race_active(void){return in_race.load()&&wide.load();}
extern "C" int rr_ui_in_race(void){return in_race.load();}
extern "C" int rr_ui_preview(int ww,int wh,const uint32_t**pixels,int*w,int*h,int*x,int*y,int*dw,int*dh){
 if(rrpc::online_overlay(ww,wh,pixels,w,h,x,y,dw,dh))return 1;     // the Online screens (online_screen.cpp)
 if(rrpc::showroom_overlay(ww,wh,pixels,w,h,x,y,dw,dh))return 1;   // Rider look showroom: its panel or loading cover
 if(rrpc::controls_overlay(ww,wh,pixels,w,h,x,y,dw,dh))return 1;   // PC bindings on the Controllers screen
 const auto*pic=slot_picture();if(!frontend.load()||!pic||ww<=0||wh<=0)return 0;
 double ratio=aspect.load()==2?21.0/9:aspect.load()==1?16.0/9:4.0/3;int vw=ww,vh=(int)(ww/ratio);if(vh>wh){vh=wh;vw=(int)(wh*ratio);}int ox=(ww-vw)/2,oy=(wh-vh)/2;
 // Exactly the game's picture slot (230,111 256x112, inside the backdrop's
 // panel frame), where its own menu pictures play.
 *pixels=pic->data();*w=512;*h=224;*x=ox+vw*230/512;*y=oy+vh*111/240;*dw=vw*256/512;*dh=vh*112/240;
 // Widescreen: the picture keeps its 4:3 proportions (like the game's own menu
 // pictures), centred in the slot; the panel frame narrows with it (rr_ui_panel).
 if(aspect.load()!=0){int nw=(int)(*dw*(aspect.load()==2?4.0/7.0:0.75));*x+=(*dw-nw)/2;*dw=nw;}
 return 1;
}
extern "C" bool SDLCALL __wrap_SDL_GL_SwapWindow(SDL_Window*w){
 // The renderer owns the presentation grid and the interpolation phase.
 // Sleeping here after composition would pace an already stale phase again.
 // Private evidence only: wall timestamps include both real/generated swaps.
 static FILE*trace=[](){const char*p=std::getenv("RR_PRESENT_TRACE");FILE*f=p?std::fopen(p,"w"):nullptr;if(f)std::setvbuf(f,nullptr,_IOFBF,262144);return f;}();
 if(trace&&!present_image_ring_frozen()){
  uint32_t first,last,frame=0;present_image_ring_sequence_span(&first,&last);uint64_t stamp=SDL_GetTicksNS(),hash=14695981039346656037ull;uint8_t*rgb=nullptr;int width=0,height=0;
  if(present_image_ring_get_sequence_rgb(last,&rgb,&width,&height,&frame)){for(size_t i=0;i<(size_t)width*height*3;i++){hash^=rgb[i];hash*=1099511628211ull;}std::free(rgb);}else hash=0;
  std::fprintf(trace,"%llu,%u,%llu\n",(unsigned long long)stamp,last,(unsigned long long)hash);
 }
 return __real_SDL_GL_SwapWindow(w);
}
extern "C" int rr_fast_get_mode(void);   // race_timing.py: 0 = Frame rate Original
// 60 Hz frontend (Frame rate: 60; RR_MENU60=0 keeps stock pacing for A/B).
// The frontend overlay's VBlank callback (0x80064C30) holds each swap until a
// VBlank counter (0x80088C44, reset after every swap) reaches 4 while
// 0x8009C5DA bit 0 is set, otherwise 2: menus ran at 15 or 30 Hz however fast
// a frame was ready. The counter is preset so every VBlank may swap a ready
// frame. Both hooks are guarded by the overlay's exact instructions, so they act
// only while the frontend overlay is resident.
static bool menu60(){
 static int env=-1;if(env<0){const char*e=std::getenv("RR_MENU60");env=!(e&&e[0]=='0');}
 return env&&rr_fast_get_mode()!=0;
}
static bool frontend_overlay(){
 return word(0x80064C88)==0x8C628C44u&&word(0x80064C90)==0x2C420004u&&word(0x80064CCC)==0x2C420002u;
}
static void menu_uncap(){
 if(!frontend_overlay())return;
 uint32_t b=word(0x80088C44);
 // The menu loop zeroes the counter after each swap: one shown menu frame.
 if(b==0)psx_mod_counter_add("rr.menu.frames",1);
 if(!menu60())return;
 if(b<3){b=3;std::memcpy(memory_get_ram_ptr()+(0x80088C44&0x1fffff),&b,4);}
}
// Fast CPU timing for the menu loop (race_timing.py rr_fast_gate_vblank): the
// frontend overlay's steady menu state (state 2; loading is 3, racing 1).
extern "C" int rr_menu_fast_ok(void){
 if(!menu60()||!frontend_overlay())return 0;
 const uint32_t ctx=word(0x8005B2F8);
 return ram(ctx,4)&&memory_get_ram_ptr()[ctx&0x1fffff]==2;
}
// Every animated menu picture (movie panels, the stopwatch, bikes) is one
// chunked MDEC stream stepped by the player 0x800627BC: one frame per track per
// call, once per menu pass. Uncapped, pictures would play as fast as the menu
// runs (1.25-2.3x measured). The stock cap gave them one frame per 4 VBlanks
// (15 Hz), so a call within 4 VBlanks of the last step of the same player
// (keyed by its track array, a0) passes a track count of 0 (a1): the player
// returns without decoding. The VBlank count is the game's own (gp+0x7E0,
// advanced by 0x8001B700).
// The player decodes each frame strip by strip straight into the current draw
// buffer (track position + the buffer's draw offset), after the pass has drawn
// its list, so a held pass would show the panel's backdrop instead of the
// picture. A held pass therefore copies the last frame from the other buffer
// (the one on screen, which holds it) with a GP0 VRAM copy: the GPU is idle
// here (the pass's DrawSync is done) and the copy is what MoveImage sends.
static uint16_t half16(uint32_t p){uint16_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),2);return v;}
// Menu pictures keep their proportions in widescreen. The player writes the
// frame's VRAM rectangle (work block *0x800810E8: +0 x, +2 y, +4 width, +6
// height, draw-buffer offset included) before it decodes the strips; at
// 0x80062AB0 / 0x80062C00 (both decode paths, after those stores) it is
// published for the renderer (gpu_presentation.py), which draws uploads inside
// it narrowed about the picture's centre. Full-screen frames (FMVs, 24-bit,
// own aspect option) are left alone. The same player also decodes multi-track
// stills into texture VRAM outside both draw buffers (title-screen and menu
// backdrop tiles at x >= 512, on entering a screen; a savestate already holds
// them): those are textures, drawn later by sprites, and stay unchanged, so
// only frames inside a draw buffer's clip rectangle (DRAWENV +0 x, +2 y,
// +4 width, +6 height in the display block *0x8005B470) are published.
struct PicRegion{std::atomic<int> x{0},y{0},w{0},h{0};std::atomic<uint32_t> vbl{0};};
static PicRegion pic_regions[4];
static void picture_region(){
 const uint32_t work=word(0x800810E8),disp=word(0x8005B470);if(!ram(work,8)||!ram(disp,0x100))return;
 const int x=(int16_t)half16(work),y=(int16_t)half16(work+2),w=half16(work+4),h=half16(work+6);
 if(w<=0||h<=0||w>=320||x<0||y<0||x+w>1024||y+h>512)return;
 bool shown=false;
 for(int k=0;k<2;k++){const uint32_t e=disp+16+k*0x70;const int cx=half(e),cy=half(e+2),cw=half(e+4),ch=half(e+6);
  if(cw>0&&ch>0&&x>=cx&&y>=cy&&x+w<=cx+cw&&y+h<=cy+ch)shown=true;}
 if(!shown)return;
 const uint32_t now=word(0x8005B46C);PicRegion*slot=&pic_regions[0];
 for(auto&r:pic_regions){if(r.x.load()==x&&r.y.load()==y&&r.w.load()==w&&r.h.load()==h){slot=&r;break;}if(now-r.vbl.load()>now-slot->vbl.load())slot=&r;}
 slot->x.store(x);slot->y.store(y);slot->w.store(w);slot->h.store(h);slot->vbl.store(now);
}
// RR_PIC_TRACE=<file>: one line per player step, hold, copy and frame publish
// (diagnostics only).
static int page_slot_xy(int&x,int&y);   // below
static FILE*pic_trace(){static FILE*f=[](){const char*p=std::getenv("RR_PIC_TRACE");FILE*t=p?std::fopen(p,"w"):nullptr;if(t)std::setvbuf(t,nullptr,_IOLBF,4096);return t;}();return f;}
static void picture_pace(CPUState*c,uint32_t pc){
 if(word(0x800627BC)!=0x27BDFFB0u||word(0x80062800)!=0x18E0014Cu)return;
 if(pc==0x80062AB0||pc==0x80062C00){
  if(FILE*t=pic_trace()){const uint32_t w=word(0x800810E8);if(ram(w,8))std::fprintf(t,"%u frame %d,%d %ux%u\n",word(0x8005B46C),(int16_t)half16(w),(int16_t)half16(w+2),half16(w+4),half16(w+6));}
  if(word(0x80062AB0)==0x3C088008u&&word(0x80062C00)==0x3C078008u)picture_region();
  return;
 }
 if(pc==0x800627BC)if(FILE*t=pic_trace()){const uint32_t k=c->gpr[4];std::fprintf(t,"%u call key=%08x tracks=%d pos=%d,%d\n",word(0x8005B46C),k,(int)c->gpr[5],ram(k,36)?(int)word(k+28):-1,ram(k,36)?(int)word(k+32):-1);}
 if(pc==0x80062824){
  // Per-track loop head. The player waits for each MDEC strip in a busy loop
  // bounded by 200000 iterations (0x30D40 at sp+24). Under the menus' fast CPU
  // timing those iterations pass 8x faster in emulated time and the wait gave
  // up before MDEC finished: full-screen FMVs lost their last 4 of 20 strips.
  // A longer bound only matters if MDEC stalls.
  const uint32_t at=c->gpr[29]+24;
  if(word(0x80062824)==0x0000B821u&&menu60()&&ram(at,4)&&word(at)==0x30D40u){uint32_t v=0x30D40u*16;std::memcpy(memory_get_ram_ptr()+(at&0x1fffff),&v,4);}
  return;
 }
 if(!menu60()){if((int)c->gpr[5]>0)psx_mod_counter_add("rr.menu.picture_steps",1);return;}   // stock: count only
 // The idle main menu plays the attract FMVs through the same player and track
 // array as its logo panel; on the way back the track is moved to the panel
 // (track +28 x, +32 y) while the work block still holds the full-screen
 // frame's size. A call whose track moved since that player's last step starts
 // a new stream and always steps, so a hold never pairs one stream's position
 // with another's frame size.
 struct Step{uint32_t key,vbl;uint64_t pos;};static Step last[4];static uint32_t stepped_key;
 const uint32_t key=c->gpr[4],vbl=word(0x8005B46C);const int tracks=(int)c->gpr[5];
 const uint64_t pos=ram(key,36)?(uint64_t)word(key+28)<<32|word(key+32):0;
 Step*s=nullptr;for(auto&l:last)if(l.key==key){s=&l;break;}
 if(!s){s=&last[0];for(auto&l:last)if(!l.key){s=&l;break;}*s={key,vbl-4,pos};}
 if(vbl-s->vbl>=4||s->pos!=pos){s->vbl=vbl;s->pos=pos;stepped_key=key;if(tracks>0)psx_mod_counter_add("rr.menu.picture_steps",1);return;}
 c->gpr[5]=0;psx_mod_counter_add("rr.menu.picture_holds",1);
 // Re-show the held frame. The frame size is in the player's work block
 // (*0x800810E8: +4 width, +6 height), valid for the player stepped last.
 const uint32_t disp=word(0x8005B470),work=word(0x800810E8);
 if(tracks!=1||stepped_key!=key||!ram(disp,0x100)||!ram(work,8)||!ram(key,36))return;
 const uint8_t cur=memory_get_ram_ptr()[(disp+5)&0x1fffff];if(cur>1)return;
 const int dx=(int16_t)half16(disp+16+cur*0x70+8),dy=(int16_t)half16(disp+16+cur*0x70+10);
 const int sx=(int16_t)half16(disp+16+(1-cur)*0x70+8),sy=(int16_t)half16(disp+16+(1-cur)*0x70+10);
 const int x=(int)word(key+28),y=(int)word(key+32),w=half16(work+4),h=half16(work+6);
 if((dx==sx&&dy==sy)||w<=0||h<=0||w>512||h>256||x<0||y<0||x+w>1024||y+h>512)return;
 // The work block's size belongs to this track only if its frame position (+0 x,
 // +2 y) is the track's in one of the two buffers: anything else would copy a
 // rectangle of the wrong size, out into the texture pages beside and below the
 // buffers.
 const int wx=(int16_t)half16(work),wy=(int16_t)half16(work+2);
 if(!(wx==x+sx&&wy==y+sy)&&!(wx==x+dx&&wy==y+dy))return;
 // Only two pictures are ever re-shown: the current page's panel (its 256x112
 // slot at 230,111, page_slot_xy) and a full-screen movie while the display
 // scans out 24-bit (the movie's frame at the buffer origin). Any other
 // rectangle, e.g. a movie frame held into a 15-bit menu page, is refused.
 int px=0,py=0;const bool panel=page_slot_xy(px,py)==1&&x==px&&y==py&&w==256&&h==112;
 GpuDisplayInfo di{};gpu_get_display_info(&di);
 const bool movie=di.depth24&&!di.disabled&&x==0&&y<=16&&w>=256;
 if(!panel&&!movie){
  psx_mod_counter_add("rr.menu.picture_copy_refused",1);
  if(FILE*t=pic_trace())std::fprintf(t,"%u refuse key=%08x %d,%d -> %d,%d %dx%d depth24=%d slot=%d,%d\n",vbl,key,x+sx,y+sy,x+dx,y+dy,w,h,di.depth24,px,py);
  return;
 }
 gpu_write_gp0(0x80000000u);
 gpu_write_gp0((uint32_t)(((y+sy)&0x1ff)<<16|((x+sx)&0x3ff)));
 gpu_write_gp0((uint32_t)(((y+dy)&0x1ff)<<16|((x+dx)&0x3ff)));
 gpu_write_gp0((uint32_t)(h<<16|w));
 psx_mod_counter_add("rr.menu.picture_copies",1);
 if(FILE*t=pic_trace())std::fprintf(t,"%u copy key=%08x %d,%d -> %d,%d %dx%d\n",vbl,key,x+sx,y+sy,x+dx,y+dy,w,h);
}
// Widescreen horizon (race overlay). The game builds just enough of the far
// horizon for its 4:3 view, so in 16:9 / 21:9 it ended inside the picture (the
// cloud strips are continued on the GPU side, gpu_geometry.py SKYLINE).
//  One player: 0x800644F4 draws 25 of the 110 columns of the streamed
//  panorama (struct *0x8005B278: +0x30 first drawn column, from the heading;
//  +0x34..+0x36 the columns resident in the 192-tile VRAM ring). Its streamer
//  (0x800656A8: a2..a3 = the window to keep resident) is given the window
//  widened by K columns on each side, and the builder starts up to K columns
//  earlier (+0x30) and draws up to 2K more (loop counter at scratchpad
//  0x1F800000, limit 25), over resident columns only. The game updates the
//  streamed window only when the heading reaches another column, so a window
//  whose margin is not resident yet is resubmitted (+0x2A = last window start).
//  Split screen has no streamed panorama: 0x80064CC8 draws a row of N = 7
//  columns (0x800D5EE8 +4) of a static 27-column sprite panorama, which the
//  framework narrows about the centre with the rest of the picture; N grows by
//  2K. Widescreen only (4:3 is unchanged); RR_SKY_EXTEND=0 turns it off.
// Panorama streaming under fast timing (every aspect, not switched off by
// RR_SKY_EXTEND). Panorama columns are MDEC-compressed: the streamer starts a
// decode (+0x18 = 1), the MDEC callback sets +0x18 = 2, and 0x800657A8
// uploads the tiles and starts the next pair. When a load is pending, the
// builder busy-waits for it and gives up after 10000 loop iterations without
// a finished decode (0x80064588: sltiu v0,v1,10000), resetting the MDEC
// (0x800647F0) with the resident range already advanced, so the ring holds
// blank or stale tiles that are then drawn as the horizon (olive and teal
// blocks a few seconds into a fresh race; owner capture play-cap/cap01: +0x34
// = +0x36 = 37 with the window at 38). The bound is counted in instructions;
// under fast timing an iteration costs 1/8 of a guest cycle per instruction,
// so it expired long before the decode, which keeps real MDEC timing. So the
// wait runs at stock CPU timing (race_timing.py rr_fast_note_wait), like the
// game's other hardware waits: the bound keeps its original meaning and the
// spin costs the host what it did at stock. (Holding the bound for up to 3
// VBlanks under fast timing instead fixed the tiles but spun 8x the loop per
// guest millisecond, and 120 FPS races fell back to 60 Hz every few seconds.)
// The wait starts at its bound check 0x80064588 and ends when the stream is
// idle (0x800645AC), when the builder gives up (0x800647F0) or returns.
extern "C" void rr_fast_note_wait(int waiting);   // race_timing.py
static void pano_wait(uint32_t pc){
 if(word(0x80064588)!=0x2C622710u||word(0x800647F0)!=0x8E02B278u)return;
 static bool waiting;
 if(pc==0x80064588){
  if(!waiting){waiting=true;rr_fast_note_wait(1);psx_mod_counter_add("rr.sky.pano_waits",1);}
  return;
 }
 if(pc==0x800647F0)psx_mod_counter_add("rr.sky.pano_wait_timeouts",1);
 if(waiting){waiting=false;rr_fast_note_wait(0);}
}
extern "C" uint8_t*memory_get_scratchpad_ptr(void);
static bool race_sky_code(){
 return word(0x80064B48)==0x28420019u&&word(0x80064834)==0x8E230024u&&word(0x80065440)==0x0C0195AAu&&
        word(0x80064E64)==0xA5020004u&&word(0x80064EE4)==0x02069821u;
}
static int sky_k(bool split){
 static int env=-1;if(env<0){const char*e=std::getenv("RR_SKY_EXTEND");env=!(e&&e[0]=='0');}
 const int a=aspect.load();if(!env||a<=0)return 0;
 // Split: 64-px sprites. 1P: columns of 360/110 degrees, 12-17 px wide at the
 // view edges. The 25 columns end 23-64 px inside the 21:9 view (guest x
 // 0..384); measured with K=6 at 21:9: -44..444 or wider; K=2 at 16:9 only
 // just reached 0..384 at some headings, hence 3.
 return split?(a==1?2:4):(a==1?3:6);
}
static int col110(int c){return ((c%110)+110)%110;}
static struct{uint32_t base;int f30,extra;bool shifted,count;}pano;
static void pano_restore(){
 if(pano.shifted&&word(0x8005B278)==pano.base)put(pano.base+0x30,pano.f30);
 pano.shifted=pano.count=false;
}
// Columns of margin resident around the drawn window: {right (before +0x30), left (after its end)}.
static bool pano_margin(uint32_t s,int f30,int k,int&right,int&left){
 const int f34=half(s+0x34),f36=half(s+0x36);
 if(f30<0||f30>109||f34<0||f34>109||f36<0||f36>109)return false;
 const int span=col110(f36-f34),last=col110(f30+24-f34);
 if(last>span)return false;
 right=std::min(k,col110(f30-f34));left=std::min(k,span-last);return true;
}
static bool sky_observe(uint32_t pc){
 switch(pc){
 case 0x80065174:case 0x800656A8:case 0x800645AC:case 0x80064834:case 0x80064B74:case 0x80064ED8:
 case 0x80064588:case 0x800647F0:break;
 default:return false;
 }
 CPUState*c=debug_cpu_ptr;
 if(!c||!race_sky_code())return true;
 if(pc==0x80064588||pc==0x800647F0||pc==0x800645AC||pc==0x80064B74)pano_wait(pc);
 if(pc==0x80064588||pc==0x800647F0)return true;
 if(pc==0x80064B74){pano_restore();return true;}   // builder exit (also its early returns)
 if(pc==0x80064ED8){   // split row: N, N/2 and the first column are derived again from N
  const int k=sky_k(true);if(!k||c->gpr[8]!=0x800D5EE8u)return true;
  const uint32_t view=word(0x8005B474)+c->gpr[17]*8;if(!ram(view,8))return true;
  const int w=half(view+4),n=std::min(27,((((w+63)>=0?w+63:w+126)>>6)+1|1)+2*k),col=(int)c->gpr[5];
  if(col<0||col>26)return true;
  int first=col-n/2;if(first<0)first+=27;
  put(0x800D5EE8u+4,n);c->gpr[6]=(uint32_t)n;c->gpr[3]=(uint32_t)(n/2);c->gpr[16]=(uint32_t)first;
  psx_mod_counter_add("rr.sky.split_rows",1);
  return true;
 }
 const uint32_t s=word(0x8005B278);if(!ram(s,0x40))return true;
 const int k=sky_k(false);
 if(pc==0x80065174){   // window update, before the builder
  pano_restore();
  static int cooldown;if(cooldown>0)cooldown--;
  int right,left;
  if(k&&word(s+0x18)==0&&cooldown==0&&pano_margin(s,half(s+0x30),k,right,left)&&(right<k||left<k)){
   put(s+0x2A,-1);cooldown=8;psx_mod_counter_add("rr.sky.pano_requests",1);
  }
  return true;
 }
 if(!k)return true;
 if(pc==0x800656A8){   // window to keep resident: a2 = first, a3 = last column
  const int f30=pano.shifted?pano.f30:half(s+0x30);
  c->gpr[6]=(uint32_t)col110(f30-k);c->gpr[7]=(uint32_t)col110(f30+24+k);
  return true;
 }
 if(pc==0x800645AC){   // streaming done: first column and resident range are final
  if(pano.shifted)return true;
  const int f30=half(s+0x30);int right,left;
  if(!pano_margin(s,f30,k,right,left)||right+left==0)return true;
  put(s+0x30,col110(f30-right));
  pano.base=s;pano.f30=f30;pano.extra=right+left;pano.shifted=pano.count=true;
  return true;
 }
 if(pc==0x80064834&&pano.count){   // first column of the loop
  uint8_t*sp=memory_get_scratchpad_ptr();int32_t i;std::memcpy(&i,sp,4);
  if(i==0){i=-pano.extra;std::memcpy(sp,&i,4);psx_mod_counter_add("rr.sky.pano_columns",(uint32_t)pano.extra);}
  pano.count=false;
 }
 return true;
}
// The menu page's picture slot. Every frontend page
// with a picture panel names it in a record of the page (menu *0x8009C5C8: +8
// count, +0x10 table of 120-byte records, +8 type):
//  - type 8 (+16 x, +18 y): the panel of the menu pages (logo, handcuffs,
//    stopwatch, bikes, skulls, the PC Options pictures);
//  - type 7, a picture by directory name (+16 name, +24 x, +26 y) whose picture
//    is 256x112 (the game's own Options page: CNLG, the controller).
// Both sit at 230,111, with a movie record at the same place; only that place
// counts (the CONTROLLERS page has a type-8 record at 0,0 and no panel: taken
// as a slot, its bars and title were narrowed about x 128). Their pictures
// are 256x112: a still (the sprite from texture VRAM 512,256, or the picture
// itself) and the animation the player decodes over it. The published frame
// rectangles above exist only while the player decodes, so a page showed its
// still in the stretched box and the box narrowed when the animation started
// (2-3 s after entering Single Player). The record is there from the page's
// first frame to its last, so the box, the still and the animation keep one
// size and centre. Steady menus only (*0x8005B2F8 state 2; loading is 3, with
// the frontend overlay gone).
static bool dir_picture_256x112(uint32_t name){
 for(int i=0;i<48;i++){const uint32_t e=0x8009E3C8+i*0x24;if(word(e+12)==name)return word(e+20)==(112u<<16|256u);}
 return false;
}
// 1: the current page has a slot at x,y (draw-buffer coordinates); 0: it has
// none; -1: no page (not a steady menu).
static int page_slot_xy(int&x,int&y){
 const uint32_t ctx=word(0x8005B2F8),m=word(0x8009C5C8);
 if(!ram(ctx,4)||memory_get_ram_ptr()[ctx&0x1fffff]!=2||!ram(m,0x14))return -1;
 const uint32_t table=word(m+0x10);const int count=(uint16_t)half(m+8);
 if(count<=0||count>40||!ram(table,(unsigned)count*120))return -1;
 for(int i=0;i<count;i++){const uint32_t r=table+i*120;
  if(half(r+8)==8){x=half(r+16);y=half(r+18);}
  else if(half(r+8)==7&&dir_picture_256x112(word(r+16))){x=half(r+24);y=half(r+26);}
  else continue;
  if(x==230&&y==111)return 1;}
 return 0;
}
// The slot in both draw buffers (DRAWENV +8 x, +10 y offsets).
static bool slot_buffers(int x,int y,int sx[2],int sy[2]){
 const uint32_t disp=word(0x8005B470);if(!ram(disp,0x100))return false;
 for(int k=0;k<2;k++){sx[k]=x+(int16_t)half16(disp+16+k*0x70+8);sy[k]=y+(int16_t)half16(disp+16+k*0x70+10);}
 return true;
}
// Which page a frame draws. On a page change the game switches the page
// pointer a frame or two before it stops drawing the old page (Game options ->
// Noise: two frames of the old page's prebuilt primitives under the new
// pointer), so those frames showed the old picture in the stretched box. Every
// page builds its own backdrop primitives (both alive across the change), so
// the backdrop packets each page drew while it was current tell which page a
// frame belongs to. The frame's slot (rr_ui_panel, at its backdrop, before
// anything else of the frame) then holds for the frame's picture sprites.
// A page without a slot record keeps the published animation rectangles, but
// only those its own pictures published (page_since: VBlank it became current).
struct PageSeen{uint32_t menu;int slot,x,y;uint32_t pk[8];int n;};
static PageSeen seen[2];   // current page, previous page (emulation thread)
static std::atomic<int> frame_slot{-1},frame_x{0},frame_y{0};static std::atomic<uint32_t> frame_vbl{0},page_since{0};
static int frame_page(uint32_t src,int&x,int&y){
 auto drew=[src](const PageSeen&s){for(int i=0;i<s.n;i++)if(s.pk[i]==src)return src!=0;return false;};
 const PageSeen*p=&seen[0];
 const int has=page_slot_xy(x,y);
 if(has<0){   // no page records (leaving the menus): a page's own backdrop still names it
  if(drew(seen[0]))p=&seen[0];else if(drew(seen[1]))p=&seen[1];else return -1;
 }else{
  const uint32_t m=word(0x8009C5C8);
  if(m!=seen[0].menu){seen[1]=seen[0];seen[0]=PageSeen{m,has,x,y,{},0};page_since.store(word(0x8005B46C));}
  else{seen[0].slot=has;seen[0].x=x;seen[0].y=y;}
  if(drew(seen[1])&&!drew(seen[0])){p=&seen[1];psx_mod_counter_add("rr.ui.panel_previous_page",1);}
  else if(src&&!drew(seen[0])&&seen[0].n<8)seen[0].pk[seen[0].n++]=src;
 }
 x=p->x;y=p->y;
 frame_slot.store(p->slot);frame_x.store(x);frame_y.store(y);frame_vbl.store(word(0x8005B46C));
 return p->slot;
}
// Renderer query (render thread): is this upload part of a current menu
// picture in a stretched widescreen screen? Returns the picture's rectangle
// (x1/y1 exclusive) and the narrowing factor that restores 4:3 proportions.
extern "C" int rr_ui_picture_at(int x0,int y0,int x1,int y1,int*rx0,int*ry0,int*rx1,int*ry1,float*f){
 if(!enabled||!wide.load()||in_race.load()||word(0x800627BC)!=0x27BDFFB0u)return 0;
 const uint32_t now=word(0x8005B46C);
 // The frame's slot (decided at its backdrop, rr_ui_panel), else the page's.
 int sx,sy,has=frame_slot.load(),px[2],py[2];
 if(has>=0&&now-frame_vbl.load()<=8u){sx=frame_x.load();sy=frame_y.load();}
 else has=page_slot_xy(sx,sy);
 if(has>0&&slot_buffers(sx,sy,px,py))for(int k=0;k<2;k++)
  if(x0>=px[k]&&x1<px[k]+256&&y0>=py[k]&&y1<py[k]+112){*rx0=px[k];*ry0=py[k];*rx1=px[k]+256;*ry1=py[k]+112;*f=aspect.load()==2?4.0f/7.0f:0.75f;return 1;}
 for(auto&r:pic_regions){
  const int x=r.x.load(),y=r.y.load(),w=r.w.load(),h=r.h.load();
  if(!w||now-r.vbl.load()>12u||(has>=0&&(int32_t)(r.vbl.load()-page_since.load())<0))continue;
  if(x0>=x&&x1<x+w&&y0>=y&&y1<y+h){*rx0=x;*ry0=y;*rx1=x+w;*ry1=y+h;*f=aspect.load()==2?4.0f/7.0f:0.75f;return 1;}
 }
 // PC menu pages show their own preview (rr_ui_preview) over the game's picture
 // panel, narrowed the same way; the game's still under it is a picture too.
 if(frontend.load()&&slot_picture())
  for(int by:{0,256})if(x0>=230&&x1<486&&y0>=111+by&&y1<223+by){*rx0=230;*ry0=111+by;*rx1=486;*ry1=223+by;*f=aspect.load()==2?4.0f/7.0f:0.75f;return 1;}
 return 0;
}
// Renderer query (gpu_geometry.py rr_panel_backdrop): the picture slot inside
// rows [y0,y1) of a full-screen backdrop sprite, in a stretched widescreen
// screen. The slot's frame is part of the backdrop; it narrows with the picture
// so the picture fills its box as at 4:3.
// The player decodes a step into one draw buffer only (steps come every 4
// VBlanks, so always the same one at 60 Hz) and picture_pace copies it into the
// other; that buffer's slot is the published one moved by the difference of
// the two buffers' draw offsets (DRAWENV +8 x, +10 y).
// src is the backdrop sprite's packet address: the page this frame draws
// (frame_page) decides, so its still and its animation get the same box.
extern "C" int rr_ui_panel(int y0,int y1,uint32_t src,int*rx0,int*ry0,int*rx1,int*ry1){
 if(!enabled||!wide.load()||in_race.load()||word(0x800627BC)!=0x27BDFFB0u)return 0;
 int px[2],py[2],fx,fy;const int has=frame_page(src,fx,fy);
 if(has>0){
  if(slot_buffers(fx,fy,px,py))for(int k=0;k<2;k++)if(py[k]>=y0&&py[k]+112<=y1){*rx0=px[k];*ry0=py[k];*rx1=px[k]+256;*ry1=py[k]+112;return 1;}
  return 0;
 }
 const uint32_t now=word(0x8005B46C),disp=word(0x8005B470);
 int sx=0,sy=0;
 if(ram(disp,0x100)){sx=(int16_t)half16(disp+16+0x70+8)-(int16_t)half16(disp+16+8);sy=(int16_t)half16(disp+16+0x70+10)-(int16_t)half16(disp+16+10);}
 for(auto&r:pic_regions){
  const int w=r.w.load(),h=r.h.load();
  if(w<128||h<64||now-r.vbl.load()>12u||(has==0&&(int32_t)(r.vbl.load()-page_since.load())<0))continue;
  for(int k:{0,1,-1}){const int x=r.x.load()+k*sx,y=r.y.load()+k*sy;
   if(y<y0||y+h>y1)continue;
   *rx0=x;*ry0=y;*rx1=x+w;*ry1=y+h;return 1;}
 }
 if(frontend.load()&&slot_picture())
  for(int by:{0,256})if(111+by>=y0&&223+by<=y1){*rx0=230;*ry0=111+by;*rx1=486;*ry1=223+by;return 1;}
 return 0;
}
// Online also the PC layer's game-state ticks (netplay_session.cpp): the
// emulated VBlank is the same guest moment on both PCs.
extern "C" void __wrap_gpu_vblank_tick(){menu_uncap();__real_gpu_vblank_tick();rrpc::net_guest_tick();rrpc::demo_tick();rrpc::unblink_players();rrpc::grid_tick();}
