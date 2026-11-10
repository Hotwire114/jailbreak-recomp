/* Rider look showroom. LGPL-2.1-or-later. Independently authored; no game
 * code or data is included, only the addresses and values the game uses.
 *
 * User settings > Rider look on the main menu opens the game itself as a
 * showroom: a Time Trial is started with the game's own menus (Solo > Time
 * Trials > Race, selections written as the game's cursor), the player's bike
 * stands on the start line (no input reaches the game), the race draws the
 * player's real rider and bike, and the look rows apply live through
 * rider_enhancements.cpp (look_apply, a few times a second). Back leaves the way
 * a player would: the game's pause menu, Quit, Yes, the results screen, back
 * to the main menu, where the PC menu opens again on User settings.
 *
 * The race overlay (RASHCDG, checked by its instructions):
 *  - 0x80067AC4 draws each rider, bike and car, before the view itself.
 *  - 0x800C8B24 draws one view (a0 = view) through the view's camera, whose
 *    pointer is at 0x8005AEC0 + 4 * view: +0x10 position << 10, +0x1C position,
 *    +0x5C rotation (3 x 3 s16, Q12; rows right, down (scaled by the vertical
 *    aspect, about 0.83), forward; view = R * (world - position)). The game
 *    copies it for its draws (0x800CCE08, 0x800D8868 + 0x70 * k, ...), so the
 *    circling camera is written at every race frame's start (0x8001CD5C) and
 *    kept: the parked race has nothing else to use the chase camera for.
 *  - 0x800C8CD4 draws a view's HUD when the byte 0x800D8060 + view is set:
 *    cleared at its entry, so the showroom has no speedometer or timers.
 * The side panel and the loading cover are host images (rr_ui_preview). */
#include "showroom.h"
#include "rider_enhancements.h"
#include "netplay_session.h"
#include "demo_courses.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include "hd_font_data.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" uint8_t* memory_get_ram_ptr(void);
extern "C" CPUState* debug_cpu_ptr;
extern "C" int rr_look_count(int kind);
extern "C" const char* rr_look_name(int kind,int value);
extern "C" uint32_t rr_tag_rgb(int value);
namespace {
using rrpc::RiderOptions;using rrpc::rider_options;using rrpc::save_rider_options;
const double kPi=3.14159265358979323846;
uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),4);return v;}
uint8_t byte_at(uint32_t p){return memory_get_ram_ptr()[p&0x1fffff];}
bool ram(uint32_t p,uint32_t n){return p>=0x80000000u&&p<0x80200000u&&n<=0x80200000u-p;}
bool racing(){uint32_t c=word(0x8005B2F8);return c>=0x80010000u&&ram(c,0x40)&&byte_at(c)==1;}
bool root_menu(){return word(0x8009C5C8)==0x800990B4u&&word(0x8009C5B8)==1&&word(0x8009C5BC)==3;}
bool frontend(){return word(0x80069D98)==0x25058C84u;}
bool race_overlay(){return word(0x800C6E98)==0x24A500A2u&&word(0x800C5F9C)==0x240500A9u&&word(0x800C8B24)==0x27BDFFD0u&&word(0x800C8CD4)==0x27BDFFE8u;}
uint32_t player_record(){uint32_t base=word(0x8005B3A0);if(!ram(base,16*0x448))return 0;for(unsigned i=0;i<16;i++)if((word(base+i*0x448+0xAC)&0xffff)==0)return base+i*0x448;return 0;}

// PS1 pad bits (active low).
enum:uint16_t{SELECT=1<<0,START=1<<3,UP=1<<4,RIGHT=1<<5,DOWN=1<<6,LEFT=1<<7,L1=1<<10,R1=1<<11,TRIANGLE=1<<12,CIRCLE=1<<13,CROSS=1<<14,SQUARE=1<<15};
int key_turn;uint64_t last_turn;bool key_q,key_e;   // Q / E held: turn the camera
// Time Trials > Race Options rows Opponents, Traffic, Cops: menu list descriptors
// {count 2, action 7, handler 0x0B / 0x0C / 0x0D, selected: 0 On, 1 Off}
// (frontend overlay data). The showroom turns each one Off with the game's own
// Right press, and back On afterwards, so the player's choice stays theirs.
// Cross leaves the page keeping the choices; Triangle throws them away.
const uint32_t OPTIONS=0x800884B0;uint32_t tt_menu;int opt_row,turned_off;
bool options_ready(){for(int i=0;i<3;i++)if((word(OPTIONS+8*i)&0xFFFFFF)!=(0x000702u|(uint32_t)(0x0B+i)<<16))return false;return true;}
bool option_on(int i){return options_ready()&&byte_at(OPTIONS+8*i+3)==0;}
// One look at the Race Options page (cursor at menu + 4: rows 2-4): the cursor
// is brought to opt_row with Down, then Right until the row reads what is wanted
// (off: every row Off, noting those turned; else: the turned rows On again);
// a press the menu misses is simply made again. 1 when all three rows are done.
int options_step(uint32_t menu,uint64_t now,bool off);
enum Phase{OFF,TO_RACE,LIVE,LEAVING};
Phase phase=OFF;int step;uint64_t step_at,press_until,phase_at,last_poll;uint32_t menu_at_step;
uint16_t last_real=0xffff;uint64_t last_key;
int row;double yaw,yaw_base;bool yaw_set;
std::atomic<int> shown{-1};

// One press: the button down for 120 ms, from now.
uint16_t pressing;
void press(uint16_t b,uint64_t now){pressing=b;press_until=now+130;}
// The page's own cursor is not readable in step with the presses, so the row
// under it is found by what a Right press changes: a row that ends up wrong
// gets Right again, then Down moves on (Up instead once Down stops moving).
// off: every row Off (turned_off: the rows that were On); else those On again.
int opt_sub,opt_last=-1,opt_dir=1,opt_same;uint8_t opt_before[3];
bool option_target(int i,bool off){return off?false:(turned_off>>i&1)!=0;}   // On wanted?
int options_step(uint32_t,uint64_t now,bool off){
    if(!options_ready())return 0;
    bool done=true;for(int i=0;i<3;i++)if(option_on(i)!=option_target(i,off))done=false;
    if(opt_sub==0){
        if(done){opt_sub=0;opt_last=-1;opt_same=0;return 1;}
        for(int i=0;i<3;i++)opt_before[i]=byte_at(OPTIONS+8*i+3);press(RIGHT,now);opt_sub=1;step_at=now+500;return 0;}
    int c=-1;for(int i=0;i<3;i++)if(byte_at(OPTIONS+8*i+3)!=opt_before[i])c=i;
    if(c<0){opt_sub=0;step_at=now+200;return 0;}   // the press was missed: again
    if(off&&opt_before[c]==0&&!(turned_off>>c&1)){turned_off|=1<<c;psx_mod_counter_add(c==0?"rr.showroom.opponents_off":c==1?"rr.showroom.traffic_off":"rr.showroom.cops_off",1);}
    if(option_on(c)!=option_target(c,off)){for(int i=0;i<3;i++)opt_before[i]=byte_at(OPTIONS+8*i+3);press(RIGHT,now);step_at=now+500;return 0;}
    if(done){opt_sub=0;opt_last=-1;opt_same=0;return 1;}
    if(c==opt_last&&++opt_same>=2){opt_dir=-opt_dir;opt_same=0;}else if(c!=opt_last)opt_same=0;
    opt_last=c;press(opt_dir>0?DOWN:UP,now);opt_sub=0;step_at=now+500;return 0;
}
uint16_t script_out(uint64_t now){return now<press_until?(uint16_t)~pressing:0xffff;}

// ---- look rows (as the launcher's Rider look page) ----
// Panel rows 0-3 Player 1, 4-7 Player 2 (paint, name tag, outfit, colours);
// setting r = kind * 2 + seat, as rr_look_* and the launcher number them.
const int ROWS=8;
const char*kind_names[4]={"Bike paint","Name tag","Outfit","Colours"};
int setting_of(int row){return (row%4)*2+row/4;}
int seat_of(int row){return row/4;}
int&look_value(RiderOptions&o,int r){return r<2?o.paint[r]:r<4?o.tag[r-2]:r<6?o.outfit[r-4]:o.colour[r-6];}
void leave(uint64_t now,int from);
void change(int d){
    const int r=setting_of(row);RiderOptions next=rider_options();int n=rr_look_count(r/2);if(n<1)return;int&v=look_value(next,r);v=((v+d)%n+n)%n;
    if(save_rider_options(next)){rider_options()=next;psx_mod_counter_add("rr.showroom.changes",1);}}

// ---- host images: HD lettering (Roboto Flex outlines, hd_font_data.h) ----
struct Image{int w=0,h=0;std::vector<uint32_t>px;};
enum{REGULAR=0,BLACK=2};
double cap_units(int style){static double cache[RR_HD_STYLES];if(cache[style])return cache[style];
    const int16_t*d=rr_hd_data+rr_hd_index[style]['H'-RR_HD_FIRST];int nc=*d++,top=0;for(int c=0;c<nc;c++){int np=*d++;for(int i=0;i<np;i++,d+=2)top=std::min<int>(top,d[1]);}
    return cache[style]=top<0?-top:700;}
double text_width(int style,const char*s,double cap){double sc=cap/cap_units(style),w=0;for(;*s;s++){int c=(unsigned char)*s;if(c>=32&&c<=RR_HD_LAST)w+=rr_hd_advance[style][c-32]*sc;}return w;}
uint32_t mix(uint32_t d,uint32_t s,unsigned a){if(a>=255)return 0xff000000u|(s&0xffffff);uint32_t o=0xff000000u;for(int k=0;k<24;k+=8){unsigned x=(d>>k&255)*(255-a)+(s>>k&255)*a;o|=((x+127)/255)<<k;}return o;}
// One line, cap top at y, filled with even-odd spans at four sub-rows per pixel.
void text(Image&im,int style,const char*s,double x,double top,double cap,uint32_t rgb){
    struct Edge{float x0,y0,x1,y1;};std::vector<Edge>e;double sc=cap/cap_units(style),pen=x,base=top+cap,ylo=1e9,yhi=-1e9,xlo=1e9,xhi=-1e9;
    for(;*s;s++){int c=(unsigned char)*s;
        if(c>=RR_HD_FIRST&&c<=RR_HD_LAST){const int16_t*d=rr_hd_data+rr_hd_index[style][c-RR_HD_FIRST];int nc=*d++;
            for(int k=0;k<nc;k++){int np=*d++;for(int j=0;j<np;j++){const int16_t*a=d+j*2,*b=j+1<np?a+2:d;
                Edge q{(float)(pen+a[0]*sc),(float)(base+a[1]*sc),(float)(pen+b[0]*sc),(float)(base+b[1]*sc)};e.push_back(q);
                ylo=std::min<double>(ylo,q.y0);yhi=std::max<double>(yhi,q.y0);xlo=std::min<double>(xlo,q.x0);xhi=std::max<double>(xhi,q.x0);}d+=np*2;}}
        if(c>=32&&c<=RR_HD_LAST)pen+=rr_hd_advance[style][c-32]*sc;}
    if(e.empty())return;
    const int X0=std::max(0,(int)std::floor(xlo)),X1=std::min(im.w,(int)std::ceil(xhi)+1),Y0=std::max(0,(int)std::floor(ylo)),Y1=std::min(im.h,(int)std::ceil(yhi)+1);if(X1<=X0)return;
    std::vector<float>cov(X1-X0),xs;
    for(int y=Y0;y<Y1;y++){std::fill(cov.begin(),cov.end(),0.f);
        for(int k=0;k<4;k++){float yc=y+(k+.5f)*.25f;xs.clear();
            for(auto&q:e)if((q.y0<=yc)!=(q.y1<=yc))xs.push_back(q.x0+(yc-q.y0)/(q.y1-q.y0)*(q.x1-q.x0)-X0);
            std::sort(xs.begin(),xs.end());
            for(size_t i=0;i+1<xs.size();i+=2){float a=std::max(0.f,xs[i]),b=std::min((float)(X1-X0),xs[i+1]);if(b<=a)continue;int ia=(int)a,ib=(int)b;
                if(ia==ib){cov[ia]+=.25f*(b-a);continue;}cov[ia]+=.25f*(ia+1-a);for(int p=ia+1;p<ib;p++)cov[p]+=.25f;if(ib<X1-X0)cov[ib]+=.25f*(b-ib);}}
        uint32_t*out=im.px.data()+(size_t)y*im.w+X0;for(int i=0;i<X1-X0;i++){float v=cov[i];if(v<=0)continue;out[i]=mix(out[i],rgb,v>=1?255:(unsigned)(v*255+.5f));}}
}
void fill(Image&im,int x0,int y0,int x1,int y1,uint32_t top,uint32_t bottom){
    x0=std::max(0,x0);y0=std::max(0,y0);x1=std::min(im.w,x1);y1=std::min(im.h,y1);
    for(int y=y0;y<y1;y++){unsigned t=(unsigned)((y-y0)*255/std::max(1,y1-y0-1));uint32_t c=mix(top,bottom,t);for(int x=x0;x<x1;x++)im.px[(size_t)y*im.w+x]=c;}
}
// Three buffers: the render thread reads the front one while the next is drawn.
std::array<Image,3> panels;std::atomic<int> panel_front{-1};int panel_drawn=-1;uint64_t panel_key=~0ull;
std::array<Image,2> covers;std::atomic<int> cover_front{-1};
const int PW=640,PH=1080;
void draw_cover(int k,const char*line1,const char*line2){
    Image&im=covers[k];im.w=960;im.h=540;im.px.assign((size_t)im.w*im.h,0xff000000u);fill(im,0,0,im.w,im.h,0xff0a1430u,0xff03060fu);
    double w1=text_width(BLACK,line1,34);text(im,BLACK,line1,(im.w-w1)/2,236,34,0xffffffffu);
    double w2=text_width(REGULAR,line2,17);text(im,REGULAR,line2,(im.w-w2)/2,296,17,0xffb8c4e0u);
}
void draw_panel(){
    RiderOptions o=rider_options();uint64_t key=(uint64_t)row;for(int r=0;r<8;r++)key=key*61+(uint64_t)look_value(o,r);
    if(key==panel_key&&panel_front.load()>=0)return;panel_key=key;
    const int k=(panel_drawn+1)%3;Image&im=panels[k];im.w=PW;im.h=PH;im.px.assign((size_t)PW*PH,0xff000000u);
    fill(im,0,0,PW,PH,0xff101c44u,0xff050a1cu);fill(im,PW-4,0,PW,PH,0xffe0a020u,0xffe0a020u);
    text(im,REGULAR,"SHOWROOM",48,64,15,0xffe0a020u);text(im,BLACK,"RIDER LOOK",46,92,46,0xffffffffu);
    // Player 1's four rows, then Player 2's, each under its heading. Two lines a
    // row: the setting, then its value (long outfit names fit).
    auto bar=[&](int y,bool sel,const char*label,const char*value){
        fill(im,40,y,PW-40,y+74,sel?0xffd06a1cu:0xff1c2c66u,sel?0xff8a3a10u:0xff101c48u);
        if(!sel)fill(im,40,y,PW-40,y+2,0xff3a50a0u,0xff3a50a0u);
        text(im,REGULAR,label,62,y+11,15,sel?0xffffe0b0u:0xffe8c070u);text(im,REGULAR,value,62,y+38,22,0xffffffffu);
        if(sel){text(im,REGULAR,"<",PW-112,y+38,22,0xffffffffu);text(im,REGULAR,">",PW-76,y+38,22,0xffffffffu);}};
    for(int s=0;s<2;s++){char head[16];std::snprintf(head,sizeof head,"PLAYER %d",s+1);text(im,BLACK,head,46,170+s*370,20,0xffe0a020u);}
    for(int d=0;d<ROWS;d++){const int r=setting_of(d),y=204+(d/4)*370+(d%4)*82;
        const char*name=rr_look_name(r/2,look_value(o,r));bar(y,d==row,kind_names[r/2],name?name:"?");
        // Name tag rows: the showroom has no HUD, so the tag shows here, in its colour.
        if(r/2==1){const int seat=r%2;char tag[16];std::snprintf(tag,sizeof tag,"%s",o.names&&o.name[seat][0]?o.name[seat]:seat?"Player 2":"Player 1");
            const uint32_t rgb=rr_tag_rgb(look_value(o,r));const double x=PW-140-text_width(1,tag,22);
            text(im,1,tag,x+2,y+40,22,0xff000000u);text(im,1,tag,x,y+38,22,0xff000000u|(rgb?rgb:0xf0f0f0u));}}
    const char*l1[]={"Your bike's colour.","Your name's colour under the health bars in races.","Gang and rank, or police. Not in Jail Break.","Your rider's colours. Not in Jail Break."};
    const int hy=204+370+4*82+12;text(im,REGULAR,l1[setting_of(row)/2],48,hy,16,0xffd8e0f0u);
    char seat[64];std::snprintf(seat,sizeof seat,"Showing Player %d's look. Not in Five-O races.",seat_of(row)+1);text(im,REGULAR,seat,48,hy+32,16,0xffa8b4d0u);
    text(im,REGULAR,"Up/Down: choose    Left/Right: change",48,PH-92,15,0xffe0a020u);
    text(im,REGULAR,"L1/R1 or Q/E: turn the camera",48,PH-66,15,0xffe0a020u);
    text(im,REGULAR,"Back (Esc / Triangle): main menu",48,PH-40,15,0xffe0a020u);
    panel_drawn=k;panel_front.store(k);
}

// ---- camera ----
uint8_t saved_cam[0x70];
struct Tune{double radius=260,height=-110,aim=-95,pan=14,speed=10;};
Tune tune(){static Tune t;static bool read;if(!read){read=true;const char*e=std::getenv("RR_SHOWROOM_CAM");if(e)std::sscanf(e,"%lf,%lf,%lf,%lf,%lf",&t.radius,&t.height,&t.aim,&t.pan,&t.speed);}return t;}
void put16(uint8_t*p,int v){int16_t h=(int16_t)std::clamp(v,-32768,32767);std::memcpy(p,&h,2);}
void camera_in(uint32_t view){
    if(view!=0)return;const uint32_t cam=word(0x8005AEC0);const uint32_t rec=player_record();if(!ram(cam,0x70)||!rec)return;
    uint8_t*r=memory_get_ram_ptr();std::memcpy(saved_cam,r+(cam&0x1fffff),0x70);
    int16_t m[9];std::memcpy(m,saved_cam+0x5C,18);
    const double down_scale=[&]{double l=std::sqrt((double)m[3]*m[3]+(double)m[4]*m[4]+(double)m[5]*m[5])/4096;return l>.5&&l<1.2?l:.833;}();
    if(!yaw_set){yaw_base=std::atan2((double)m[6],(double)m[8])*180/kPi+180-35;yaw_set=true;}   // first view: the bike's front three-quarter
    const Tune t=tune();const double a=(yaw_base+yaw)*kPi/180;
    int32_t b[3];std::memcpy(b,r+((rec+0xC)&0x1fffff),12);
    const double T[3]={(double)b[0],b[1]+t.aim,(double)b[2]},C[3]={b[0]+t.radius*std::sin(a),b[1]+t.height,b[2]+t.radius*std::cos(a)};
    double f[3]={T[0]-C[0],T[1]-C[1],T[2]-C[2]};auto unit=[](double*v){double n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(n>0)for(int i=0;i<3;i++)v[i]/=n;};unit(f);
    // Turned a little to the left of the bike, so it stands in the picture's
    // right part, clear of the panel.
    const double p=t.pan*kPi/180,fx=f[0]*std::cos(p)-f[2]*std::sin(p),fz=f[0]*std::sin(p)+f[2]*std::cos(p);f[0]=fx;f[2]=fz;unit(f);
    double rt[3]={f[2],0,-f[0]};unit(rt);const double dn[3]={f[1]*rt[2]-f[2]*rt[1],f[2]*rt[0]-f[0]*rt[2],f[0]*rt[1]-f[1]*rt[0]};
    uint8_t*c=r+(cam&0x1fffff);
    for(int i=0;i<3;i++){int32_t v=(int32_t)std::lround(C[i]);std::memcpy(c+0x1C+4*i,&v,4);int32_t fine=v*1024;std::memcpy(c+0x10+4*i,&fine,4);}
    for(int i=0;i<3;i++){put16(c+0x5C+2*i,(int)std::lround(rt[i]*4096));put16(c+0x62+2*i,(int)std::lround(dn[i]*4096*down_scale));put16(c+0x68+2*i,(int)std::lround(f[i]*4096));}
    // The sky turns by +0x7C, the heading atan2(x, z) of the forward row in 4096ths of a turn (0x800650D0).
    put16(c+0x7C,(int)std::lround(std::atan2(f[0],f[2])*4096/(2*kPi))&0xFFF);
    psx_mod_counter_add("rr.showroom.camera",1);
}
// from 1: in the race (the pause menu first); 6: the race is already over.
void leave(uint64_t now,int from){phase=LEAVING;step=from;step_at=now;phase_at=now;shown.store(-1);
    draw_cover(1,"BACK TO THE MENU","Your rider look is saved.");cover_front.store(1);}
}
// gpu_geometry.py HUD_EDIT (GPU side): the race's 2D overlay is moved off screen.
extern "C" int rr_showroom_hud_off(void){return phase==LIVE;}
namespace rrpc {
bool showroom_active(){return phase!=OFF;}
bool showroom_mute(){return phase==TO_RACE||phase==LEAVING;}
int showroom_seat(){return shown.load();}
bool showroom_begin(){
    if(phase!=OFF||netplay()||!rider_options().menu||!root_menu())return false;
    // The stage: a demo course (demo_courses.cpp), Meadows unless RR_SHOWROOM_STAGE says.
    const char*s=std::getenv("RR_SHOWROOM_STAGE");demo_showroom(s?std::clamp(std::atoi(s),0,5):0);
    rider_menu_leave();phase=TO_RACE;step=0;turned_off=0;tt_menu=0;phase_at=SDL_GetTicks();step_at=phase_at+150;press_until=0;yaw=0;yaw_set=false;row=0;
    draw_cover(0,"OPENING THE SHOWROOM","Loading your rider and bike...");cover_front.store(0);psx_mod_counter_add("rr.showroom.opened",1);return true;
}
bool showroom_pad(int slot,uint16_t real,uint16_t&out){
    if(phase==OFF)return false;
    out=0xffff;if(slot!=0)return true;   // pad 2 stays out of it
    const uint64_t now=SDL_GetTicks();const uint16_t pressed=(uint16_t)(last_real&~real);last_real=real;
    if(phase==TO_RACE){
        // Solo, Time Trials, Race: each menu's cursor (menu + 4) set, then Cross.
        const uint32_t menu=word(0x8009C5C8);
        if(now>=step_at&&now>=press_until){
            if(step==0&&root_menu()){psx_mod_write_half(0x800990B8,2);press(CROSS,now);menu_at_step=menu;step=1;step_at=now+300;}
            else if(step==1&&frontend()&&menu!=menu_at_step&&ram(menu,8)){psx_mod_write_half(menu+4,4);press(CROSS,now);menu_at_step=menu;step=2;step_at=now+300;}
            // Time Trials: Race Options first, every one of Opponents / Traffic / Cops Off.
            else if(step==2&&frontend()&&menu!=menu_at_step&&ram(menu,8)){tt_menu=menu;psx_mod_write_half(menu+4,7);step=3;step_at=now+400;}
            else if(step==3){press(CROSS,now);menu_at_step=menu;step=4;step_at=now+300;}
            else if(step==4&&menu!=menu_at_step&&options_ready()){opt_row=0;turned_off=0;opt_sub=0;opt_last=-1;opt_same=0;opt_dir=1;step=5;step_at=now+500;}
            else if(step==5){const int r=options_step(menu,now,true);if(r>0){press(CROSS,now);step=7;step_at=now+500;}}
            else if(step==7&&menu==tt_menu){psx_mod_write_half(menu+4,2);step=8;step_at=now+400;}
            else if(step==8){press(CROSS,now);step=9;step_at=now+800;}
            else if(step==9&&!racing()){press(CROSS,now);step_at=now+700;}   // loading screens
        }
        if(step==9&&racing()){phase=LIVE;phase_at=now;press_until=0;shown.store(seat_of(row));cover_front.store(-1);panel_key=~0ull;draw_panel();psx_mod_counter_add("rr.showroom.live",1);}
        else if(now-phase_at>90000){phase=OFF;demo_showroom(-1);cover_front.store(-1);psx_mod_counter_add("rr.showroom.gave_up",1);}
        out=script_out(now);return true;
    }
    if(phase==LIVE){
        if(!racing()){leave(now,6);return true;}
        // Turning: L1 / R1 (or Q / E) turn the camera; it turns slowly by itself
        // again two seconds after they are let go.
        const int turn=((real&L1)?0:-1)+((real&R1)?0:1)+key_turn;if(turn)last_turn=now;
        const double dt=now-last_poll<200?(double)(now-last_poll):0;last_poll=now;
        yaw+=dt*(turn?turn*70.0:now-last_turn>2000?tune().speed:0)/1000.0;
        if(now-last_key>80){
            if(pressed&UP){row=(row+ROWS-1)%ROWS;}if(pressed&DOWN){row=(row+1)%ROWS;}
            if(pressed&LEFT)change(-1);if(pressed&(RIGHT|CROSS))change(1);
            if(pressed&(TRIANGLE|CIRCLE)){leave(now,1);return true;}
        }
        shown.store(seat_of(row));draw_panel();return true;
    }
    // Leaving: the game's pause menu, QUIT, YES, the results screen (MENU), then
    // Triangle back up to the main menu, where the PC menu opens again.
    if(phase==LEAVING){
        static const uint16_t quit[]={START,DOWN,CROSS,LEFT,CROSS};static const int gap[]={900,500,900,500,2500};
        if(now>=step_at&&now>=press_until){
            const uint32_t menu=word(0x8009C5C8);
            if(step>=1&&step<=5){press(quit[step-1],now);step_at=now+gap[step-1];step++;}
            else if(!frontend()){press(START,now);step_at=now+900;}          // results: Press MENU
            // Back on Time Trials: the Race Options the showroom turned Off, On again.
            else if(turned_off&&step<20&&menu==tt_menu){psx_mod_write_half(menu+4,7);step=20;step_at=now+400;}
            else if(step==20){press(CROSS,now);menu_at_step=menu;step=21;step_at=now+300;}
            else if(step==21&&menu!=menu_at_step&&options_ready()){opt_row=0;opt_sub=0;opt_last=-1;opt_same=0;opt_dir=1;step=22;step_at=now+500;}
            else if(step==22){if(options_step(menu,now,false)>0){press(CROSS,now);turned_off=0;step=24;step_at=now+500;}}
            else if(step>=20&&step<24){}   // waiting for the page
            else if(!root_menu()){press(TRIANGLE,now);step_at=now+900;}      // Time Trials, Solo
            else if(rider_menu_open_look()||now-phase_at>120000){phase=OFF;demo_showroom(-1);cover_front.store(-1);psx_mod_counter_add("rr.showroom.closed",1);}
        }
        out=script_out(now);return true;
    }
    return false;
}
bool showroom_event(const SDL_Event&e){
    if(phase==OFF)return false;
    if(e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP){const bool down=e.type==SDL_EVENT_KEY_DOWN;
        if(e.key.key==SDLK_Q)key_q=down;else if(e.key.key==SDLK_E)key_e=down;key_turn=(key_e?1:0)-(key_q?1:0);}
    if(e.type==SDL_EVENT_KEY_UP)return true;
    if(e.type!=SDL_EVENT_KEY_DOWN)return false;
    if(phase==LIVE&&!e.key.repeat){
        const auto k=e.key.key;last_key=SDL_GetTicks();
        if(k==SDLK_UP)row=(row+ROWS-1)%ROWS;else if(k==SDLK_DOWN)row=(row+1)%ROWS;else if(k==SDLK_LEFT)change(-1);else if(k==SDLK_RIGHT||k==SDLK_RETURN||k==SDLK_KP_ENTER)change(1);
        else if(k==SDLK_ESCAPE||k==SDLK_BACKSPACE){leave(last_key,1);return true;}
        shown.store(seat_of(row));
    }
    return true;
}
// The other riders: their bikes (the 16 race records) and riders (record +0x38).
// The object draw 0x80067AC4 (a0 = object) draws nothing for an object whose
// +0x34 is 0 and +0xB0 is -1: so for the call only, and put back at its return
// block 0x80068438.
uint32_t hidden;uint32_t hidden_words[2];
bool other_rider(uint32_t obj){
    const uint32_t base=word(0x8005B3A0),me=player_record();if(!ram(base,16*0x448)||!me||obj==me||obj==word(me+0x38))return false;
    if(obj>=base&&obj<base+16*0x448&&(obj-base)%0x448==0)return true;
    for(unsigned i=0;i<16;i++){const uint32_t r=base+i*0x448;if(r!=me&&word(r+0x38)==obj)return true;}
    return false;
}
void unhide(){if(!hidden)return;uint8_t*r=memory_get_ram_ptr();std::memcpy(r+((hidden+0x34)&0x1fffff),&hidden_words[0],4);std::memcpy(r+((hidden+0xB0)&0x1fffff),&hidden_words[1],4);hidden=0;}
void showroom_observe(uint32_t pc){
    if(pc!=0x8001CD5C&&pc!=0x80067AC4&&pc!=0x80068438&&pc!=0x800C8B24&&pc!=0x800C8CD4)return;
    if(pc==0x80068438){unhide();return;}
    if(phase!=LIVE||!debug_cpu_ptr||!race_overlay())return;
    // The race frame starts at 0x8001CD5C: the showroom camera is in place
    // before the game copies it for its draws (road, riders, scenery), and again
    // before the riders (0x80067AC4) and the view, in case the race moved it.
    if(pc==0x8001CD5C){camera_in(0);return;}
    if(pc==0x80067AC4){if(word(0x80067AC4)!=0x27BDFF88u||word(0x80068438)!=0x8FBF0074u)return;camera_in(0);
        unhide();const uint32_t obj=debug_cpu_ptr->gpr[4];
        static const bool hide=[]{const char*e=std::getenv("RR_SHOWROOM_HIDE");return !e||e[0]!='0';}();   // dev: 0 keeps the other riders drawn
        if(hide&&ram(obj,0xB4)&&other_rider(obj)){uint8_t*r=memory_get_ram_ptr();hidden=obj;std::memcpy(&hidden_words[0],r+((obj+0x34)&0x1fffff),4);std::memcpy(&hidden_words[1],r+((obj+0xB0)&0x1fffff),4);
            const uint32_t z=0,none=0xffffffffu;std::memcpy(r+((obj+0x34)&0x1fffff),&z,4);std::memcpy(r+((obj+0xB0)&0x1fffff),&none,4);}
        return;}
    const uint32_t view=debug_cpu_ptr->gpr[4];if(view>3)return;
    if(pc==0x800C8B24)camera_in(view);
    else psx_mod_write_byte(0x800D8060+view,0);
}
int showroom_overlay(int ww,int wh,const uint32_t**pixels,int*w,int*h,int*x,int*y,int*dw,int*dh){
    if(phase==OFF||ww<=0||wh<=0)return 0;
    const int c=cover_front.load();
    if(c>=0){const Image&im=covers[c];*pixels=im.px.data();*w=im.w;*h=im.h;*x=0;*y=0;*dw=ww;*dh=wh;return 1;}
    const int p=panel_front.load();if(phase!=LIVE||p<0)return 0;
    const Image&im=panels[p];*pixels=im.px.data();*w=im.w;*h=im.h;*x=0;*y=0;*dh=wh;*dw=wh*PW/PH;return 1;
}
}
