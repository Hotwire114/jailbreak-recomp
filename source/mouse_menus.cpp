/* Mouse in the game's menus. LGPL-2.1-or-later. Independently authored; the
 * pointer is drawn here from a few numbers, no artwork is stored.
 *
 * The mouse never writes the game's cursor: it presses Player 1's buttons, so
 * every menu reacts exactly as to the keyboard (sounds, descriptions, our Quit
 * row and PC pages, the pause menu's fourth row).
 *  - Pointing at a row steps the cursor there with Up / Down, one press at a
 *    time, waiting for the selection to change before the next.
 *  - A left click on a row goes there and presses Cross; on a screen without
 *    rows (results, "Press START") it presses Cross. A right click presses
 *    Triangle (back). The wheel presses Up / Down.
 * Rows come from rider_menu.cpp for our own pages, the main menu's Quit row and
 * the pause menu; otherwise from the game's menu record: the current menu
 * *0x8009C5C8, selection +4, last row +6, row count +8, rows *(+0x10), 120
 * bytes each; the selection is the row's index (+8 type: 12 bar, 13 bar with
 * arrows; +18 negative for a hidden row; +36/+38 the label's x/y, its 164x18 bar 17 left and 4 above).
 * Mouse positions map through the last presented frame's letterbox: menus are
 * stretched to it; the pause panel keeps 4:3 proportions about the centre. */
#include "mouse_menus.h"
#include "netplay_session.h"
#include "mod_plugins.h"
#include "gpu_gl_renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
extern "C" uint8_t*memory_get_ram_ptr(void);
namespace {
constexpr uint32_t MENU=0x8009C5C8,CFG=0x8005B2F8;
constexpr int UP=4,DOWN=6,TRIANGLE=12,CROSS=14;
bool ram(uint32_t p,uint32_t n){return p>=0x80000000u&&p<0x80200000u&&n<=0x80200000u-p;}
uint32_t word(uint32_t p){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),4);return v;}
int half(uint32_t p){int16_t v;std::memcpy(&v,memory_get_ram_ptr()+(p&0x1fffff),2);return v;}
uint8_t byte(uint32_t p){return memory_get_ram_ptr()[p&0x1fffff];}

// From the event thread to the pad poll: the pointer in native pixels
// (stretched, and from the centre for the pause panel), and counts of what
// happened since.
std::atomic<int> at_x{-9999},at_y{-9999},at_cx{-9999};
std::atomic<unsigned> moves{0},lefts{0},rights{0};std::atomic<int> wheel{0};
std::atomic<bool> in_menu{false},over_row{false};
std::atomic<uint64_t> last_move{0};

// The game's own menu record, while a frontend menu is steady (state 2).
bool record_rows(rrpc::MouseRows&m,int nw,int nh){
    if(word(0x80069D98)!=0x25058C84u)return false;   // the frontend overlay (as rider_enhancements.cpp)
    const uint32_t cfg=word(CFG);if(!ram(cfg,4)||byte(cfg)!=2)return false;
    const uint32_t menu=word(MENU);if(!ram(menu,0x14))return false;
    const int count=half(menu+8),last=half(menu+6);const uint32_t rows=word(menu+0x10);
    m.count=0;m.current=half(menu+4);m.centred=false;
    if(count<1||count>40||!ram(rows,120u*count))return true;
    for(int i=0;i<count&&m.count<16;i++){
        const uint32_t r=rows+120u*i;const int type=byte(r+8),lit=half(r+18),x=half(r+36)-17,y=half(r+38)-4;   // the bar around the label
        if((type!=12&&type!=13)||lit<0||i>last||x<0||x>=nw||y<0||y>=nh)continue;   // +18 < 0: a hidden row
        m.row[m.count++]={x,y,164,18,i};
    }
    return true;
}

// Buttons sent as presses: held 4 polls, then 4 released, so the game sees each edge.
int queue[4],queued,pulse_bit=-1,pulse_left,gap_left;
void push(int bit){if(queued<4)queue[queued++]=bit;}
int target=-1,last_current,wait_left,tries;bool click_pending;
unsigned seen_moves,seen_lefts,seen_rights;

// The pointer: an arrow in the menus' gold with a dark rim and a soft shadow;
// brighter over a row. Drawn at the window's scale.
SDL_Cursor*cursors[2];int cursor_px;bool shown;int lit_shown=-1;
SDL_Cursor*make_cursor(int px,bool lit){
    static const float P[][2]={{0,0},{0,16},{3.8f,12.4f},{6.6f,18.8f},{9.2f,17.7f},{6.4f,11.4f},{11.4f,11.4f}};constexpr int N=7;
    const float k=px/20.f;const int W=(int)std::ceil(14*k)+3,H=(int)std::ceil(21*k)+3;const float m=1.f;
    SDL_Surface*s=SDL_CreateSurface(W,H,SDL_PIXELFORMAT_ARGB8888);if(!s)return nullptr;
    auto sd=[&](float x,float y){   // signed distance to the arrow, in arrow units (negative inside)
        bool in=false;float d=1e9f;
        for(int i=0,j=N-1;i<N;j=i++){
            const float ax=P[j][0],ay=P[j][1],bx=P[i][0],by=P[i][1];
            if((ay>y)!=(by>y)&&x<(bx-ax)*(y-ay)/(by-ay)+ax)in=!in;
            const float ex=bx-ax,ey=by-ay,t=std::clamp(((x-ax)*ex+(y-ay)*ey)/(ex*ex+ey*ey),0.f,1.f),dx=x-ax-t*ex,dy=y-ay-t*ey;
            d=std::min(d,std::sqrt(dx*dx+dy*dy));
        }
        return in?-d:d;};
    const float rim=1.25f;
    for(int y=0;y<H;y++){uint32_t*row=(uint32_t*)((uint8_t*)s->pixels+y*s->pitch);for(int x=0;x<W;x++){
        const float ux=(x+.5f-m)/k,uy=(y+.5f-m)/k,d=sd(ux,uy)*k;
        const float shape=std::clamp(.5f-d,0.f,1.f),fill=std::clamp(.5f-(d+rim*k),0.f,1.f);
        const float shadow=.45f*std::clamp(.5f-sd(ux-1.6f,uy-1.6f)*k,0.f,1.f);
        const float g=std::clamp(uy/18.f,0.f,1.f);   // top to bottom
        float r=255,gr=lit?255-50*g:214-64*g,b=lit?230-150*g:92-70*g;
        r=8+(r-8)*fill;gr=14+(gr-14)*fill;b=40+(b-40)*fill;   // the rim, then the fill inside it
        const float a=shape+shadow*(1-shape);
        const float cr=a>0?r*shape/a:0,cg=a>0?gr*shape/a:0,cb=a>0?b*shape/a:0;   // the shadow is black
        row[x]=(uint32_t)(a*255+.5f)<<24|(uint32_t)(cr+.5f)<<16|(uint32_t)(cg+.5f)<<8|(uint32_t)(cb+.5f);
    }}
    SDL_Cursor*c=SDL_CreateColorCursor(s,(int)m,(int)m);SDL_DestroySurface(s);return c;
}
}

void rrpc::mouse_event(const SDL_Event&e){
    float fx,fy;SDL_WindowID id;
    if(e.type==SDL_EVENT_MOUSE_MOTION){fx=e.motion.x;fy=e.motion.y;id=e.motion.windowID;}
    else if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN){fx=e.button.x;fy=e.button.y;id=e.button.windowID;}
    else if(e.type==SDL_EVENT_MOUSE_WHEEL){wheel.fetch_add(e.wheel.y>0?-1:e.wheel.y<0?1:0);last_move.store(SDL_GetTicks());return;}
    else return;
    SDL_Window*w=SDL_GetWindowFromID(id);int ww=0,wh=0,pw=0,ph=0;if(!w||!SDL_GetWindowSize(w,&ww,&wh)||!SDL_GetWindowSizeInPixels(w,&pw,&ph)||ww<1||wh<1)return;
    const float px=fx*pw/ww,py=fy*ph/wh;
    int lx=0,ly=0,lw=pw,lh=ph;GlPresEvent p{};const uint64_t n=gl_renderer_pres_total();
    if(n&&gl_renderer_pres_get(n-1,&p)&&p.lw>0&&p.lh>0&&p.path!=GL_PRES_BLANK){lx=p.lx;ly=p.ly;lw=p.lw;lh=p.lh;}
    const int nw=(int)psx_mod_display_width(),nh=(int)psx_mod_display_height();if(nw<1||nh<1)return;
    at_x.store((int)std::floor((px-lx)*nw/lw));at_y.store((int)std::floor((py-ly)*nh/lh));
    at_cx.store((int)std::floor((px-(lx+lw*.5f))*nw/(lh*4.f/3.f)));
    last_move.store(SDL_GetTicks());moves.fetch_add(1);
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN){if(e.button.button==SDL_BUTTON_LEFT)lefts.fetch_add(1);else if(e.button.button==SDL_BUTTON_RIGHT)rights.fetch_add(1);}
}

void rrpc::mouse_tick(){
    const bool want=in_menu.load()&&SDL_GetTicks()-last_move.load()<3000&&last_move.load();
    if(want){
        SDL_Window*w=SDL_GetMouseFocus();int ph=0;if(w)SDL_GetWindowSizeInPixels(w,nullptr,&ph);
        const int px=std::clamp(ph?ph*30/1080:30,20,96);
        if(px!=cursor_px){for(auto&c:cursors){if(c)SDL_DestroyCursor(c);c=nullptr;}cursor_px=px;lit_shown=-1;}
        const int lit=over_row.load()?1:0;
        if(!cursors[lit])cursors[lit]=make_cursor(px,lit);
        if(lit!=lit_shown&&cursors[lit]){SDL_SetCursor(cursors[lit]);lit_shown=lit;}
        if(!shown){SDL_ShowCursor();shown=true;}
    }else if(shown){SDL_HideCursor();shown=false;}
}

void rrpc::mouse_menu_shown(bool over){in_menu.store(true);over_row.store(over);}

uint16_t rrpc::mouse_pad(uint16_t buttons){
    if(rrpc::netplay())return buttons;
    const int nw=(int)psx_mod_display_width(),nh=(int)psx_mod_display_height();
    MouseRows m;const bool menu=nw>0&&nh>0&&(rrpc::menu_mouse_rows(m)||record_rows(m,nw,nh));
    in_menu.store(menu);
    const unsigned mv=moves.load(),l=lefts.load(),r=rights.load();const int wh=wheel.exchange(0);
    if(!menu){seen_moves=mv;seen_lefts=l;seen_rights=r;over_row.store(false);target=-1;click_pending=false;queued=0;
        if(pulse_bit>=0&&pulse_left-->0)buttons&=(uint16_t)~(1u<<pulse_bit);else pulse_bit=-1;return buttons;}
    const int x=m.centred?at_cx.load()+nw/2:at_x.load(),y=at_y.load();int hit=-1;
    for(int i=0;i<m.count;i++){const auto&w=m.row[i];const int rx=m.centred?w.x+nw/2:w.x;if(x>=rx&&x<rx+w.w&&y>=w.y&&y<w.y+w.h){hit=i;break;}}
    over_row.store(hit>=0);
    if(mv!=seen_moves){seen_moves=mv;if(hit>=0&&m.row[hit].id!=m.current){target=m.row[hit].id;tries=0;wait_left=0;}}
    if(l!=seen_lefts){seen_lefts=l;if(hit>=0){target=m.row[hit].id;click_pending=true;tries=0;}else if(m.count==0&&!m.centred)push(CROSS);}
    if(r!=seen_rights){seen_rights=r;push(TRIANGLE);}
    for(int i=0;i<std::min(std::abs(wh),3);i++)push(wh<0?UP:DOWN);
    if(wh){target=-1;click_pending=false;}
    // One press at a time.
    if(pulse_bit>=0){if(pulse_left>0){pulse_left--;buttons&=(uint16_t)~(1u<<pulse_bit);return buttons;}if(gap_left-->0)return buttons;pulse_bit=-1;}
    int next=-1;
    if(queued){next=queue[0];std::memmove(queue,queue+1,sizeof(int)*--queued);}
    else if(target>=0){
        if(m.current==target){if(click_pending)next=CROSS;target=-1;click_pending=false;}
        else if(wait_left>0&&m.current==last_current)wait_left--;
        else if(++tries>12){target=-1;click_pending=false;}   // the row cannot be reached by stepping
        else{next=target>m.current?DOWN:UP;last_current=m.current;wait_left=15;}
    }
    if(next>=0){pulse_bit=next;pulse_left=3;gap_left=4;buttons&=(uint16_t)~(1u<<next);psx_mod_counter_add("rr.mouse.presses",1);}
    return buttons;
}
