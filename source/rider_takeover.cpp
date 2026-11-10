/* AI-rider takeover: players 3-8 each ride one of the AI riders in Skull-to-Skull.
 * LGPL-2.1-or-later. Original author code; no game code or data.
 *
 * Every rider runs the same simulation code and is steered by one control word,
 * rider+0x230 (riders: *0x8005B3A0, stride 0x448). Bit 27 (0x08000000) marks an
 * AI driver: it switches on the AI brain and AI steering. The input handler
 * 0x8001CB3C writes Player 1's and 2's words each frame from their pads
 * (accelerate 0x2 + press event 0x1, brake 0x40 + event 0x20, left 0x100,
 * right 0x200, 0x80 on any steer change, D-pad Down 0x8). Right after it, this
 * clears bit 27 of a taken-over rider and writes the same bits from that seat's
 * pad, so the rider drives exactly like a human one. The rider's index stays
 * above the human count, so its finish and results stay those of an AI rider.
 * Handing back (pad gone, rider finished, race over): bit 27 again.
 *
 * Seats: seat s >= 2 rides rider 2 + 2*(s-2) (riders 2, 4 ... 12). Its pad is the
 * SIO pad of port s, which netplay publishes identically on every PC, so the
 * takeover is deterministic online. Online it is on for every occupied seat
 * above 2; offline only with RR_TAKEOVER=1 (multitap pads 3-5, for tests).
 *
 * The game deactivates riders far from every human camera (0x80039F68): a
 * taken-over rider is always reported in range, so it keeps riding wherever it is.
 *
 * Attacks (punch, kick, weapon swings, weapon cycle) use the human combat code
 * for the rider: the decision of 0x800C2348 is ported here (it reads P1/P2-only
 * target arrays for other riders), from a controller record built in dead stack
 * below sp for this seat's pad; the attack is started with the game's own helpers
 * (behaviour push 0x800BCA68; the target is the nearest riding rider, found here, and is put in the
 * rider's rec+0x40 list, without which the hit flags drop a non-AI rider's hits), follow-ups during an
 * attack by 0x800C258C and weapon cycling by 0x800C2218, all through
 * psx_mod_call_guest_uncharged (zero guest time; the same on every PC).
 * Controls (the game's default preset): R1 / L1 punch-kick (with Up / Down for the
 * other moves), R2 weapon / special, Triangle cycles weapons.
 *
 * Own view: a takeover player sees their rider full screen with its HUD on their
 * own PC. After the game's frame function (0x80011C4C) has drawn the shared split
 * frame, psx_mod_render_local_view runs the game's own one-view render inside
 * the framework's sandbox (frozen time, every store undone afterwards): Player
 * 1's rider pointers set to this rider, one view, its own chase camera (kept
 * here between frames, stepped with the game's camera dt), then camera struct,
 * riders, track, sky, HUD and DrawOTag exactly as the game does for one view.
 * The canonical frame stays in the shared state; only this PC's picture changes.
 *
 * The recompiled code is continuation-passing: the input handler's symbol is
 * entered again at each continuation inside one frame, so the takeover runs
 * only where the handler really returns (pc = its return address, sp back). */
#include "netplay_session.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
extern "C" {
uint8_t*memory_get_ram_ptr(void);
uint16_t sio_get_pad_buttons_slot(int slot);
int sio_get_pad_connected(int slot);
int psx_netplay_active(void);
int psx_netplay_seat_count(void);
int psx_netplay_local_slot(void);
uint32_t psx_netplay_peer_seats(void);
void __real_func_8001CB3C(CPUState*);
void __real_func_80039F68(CPUState*);
void __real_func_80011C4C(CPUState*);
// Fast-timing state (race_timing.py) that guest code advances and the framework's sandbox does not checkpoint.
extern uint32_t g_rr_fast_raw,g_rr_edge_pc,g_rr_edge_budget;extern uint64_t g_rr_edge_stores,g_rr_edge_mmio;
void __real_func_80050678(CPUState*);
uint32_t psx_mod_call_guest_uncharged(CPUState*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,int*);
}
#ifndef PSX_MAX_PLAYERS
#define PSX_MAX_PLAYERS 1
#endif
namespace {
constexpr uint32_t CFG=0x8005B2F8,RIDERS=0x8005B3A0,STRIDE=0x448,AI=0x08000000u;
constexpr int MODE_SKULL=16,MAX_SEATS=8;
constexpr uint32_t TARGET_RANGE=600;   // attack target search, x/z units (a human punch landed at 116)
uint32_t rd32(uint32_t a){uint32_t v;std::memcpy(&v,memory_get_ram_ptr()+(a&0x1FFFFF),4);return v;}
uint16_t rd16(uint32_t a){uint16_t v;std::memcpy(&v,memory_get_ram_ptr()+(a&0x1FFFFF),2);return v;}
uint8_t rd8(uint32_t a){return memory_get_ram_ptr()[a&0x1FFFFF];}
bool ram(uint32_t a){return a>=0x80010000u&&a<0x801FF000u;}
int target_of(int seat){return 2+2*(seat-2);}
// Riders currently driven by a seat (for the activity check); 0 = none.
uint32_t taken[MAX_SEATS];
// Each present seat's rider while racing, taken over or not (crashed and recovering): kept in range, so the
// game does not switch it off far from P1/P2, where the seat could never take it back.
uint32_t seat_rider[MAX_SEATS];
// Seats whose rider crashed and is being put back on its bike by the AI (bit = seat).
uint32_t recovering;
// Seats whose player is present: online the session's occupied seats, offline (tests) connected multitap pads.
// RR_TAKEOVER=2 (scripted tests, which can only drive pads 1-2): seat 2 always present, ridden from pad 1,
// on the riding AI rider nearest Player 1 (see drive). RR_TAKEOVER=3: the same, but the AI rides it except
// while pad 1 holds R1 / L1 / R2 and 90 frames after, and each window takes the riding AI rider with the
// closest riding neighbour (attack tests without scripted steering).
int test_mode(){static int t=-1;if(t<0){const char*e=std::getenv("RR_TAKEOVER");t=e&&e[0]>='1'&&e[0]<='3'?e[0]-'0':0;}return t;}
int test_window;
uint32_t seat_mask(){
    if(psx_netplay_active()){int n=psx_netplay_seat_count();uint32_t m=psx_netplay_peer_seats()|(1u<<psx_netplay_local_slot());return n>0?m&((1u<<n)-1):0;}
    const int test=test_mode();if(test>=2)return 1u<<2;
    if(!test)return 0;uint32_t m=0;for(int s=2;s<MAX_SEATS&&s<PSX_MAX_PLAYERS;s++)if(sio_get_pad_connected(s))m|=1u<<s;return m;
}
void release(int seat){uint32_t r=taken[seat];taken[seat]=0;if(!ram(r))return;uint32_t w=rd32(r+0x230);if(!(w&AI))psx_mod_write_word(r+0x230,w|AI);}
// Controller-record slots (mask table 0x80052658) the combat code reads, and their pad bits.
constexpr int SLOT_UP=2,SLOT_DOWN=3,SLOT_TRI=6,SLOT_L1=8,SLOT_R1=10,SLOT_R2=11;
const struct {int slot,bit;} COMBAT[]={{SLOT_UP,4},{SLOT_DOWN,6},{SLOT_TRI,12},{SLOT_L1,10},{SLOT_R1,11},{SLOT_R2,9}};
int32_t prev_ts[MAX_SEATS][16];
// Test counter: the target's health (controller record +0x0F) shortly after an attack on it.
struct Watch {uint32_t rec;uint8_t health;int frames;} watch[MAX_SEATS];
void watch_hits(int s){Watch&w=watch[s];if(!w.frames||--w.frames)return;
    if(ram(w.rec)&&rd8(w.rec+0x0F)<w.health)psx_mod_counter_add("rr.takeover.hits",1);}
// The hit flags (0x800BCF6C) let a rider that is neither human nor AI-driven hit only the riders in its
// rec+0x40 list (5-bit index+1 entries, checked by 0x800A8C48). The game rewrites the list, so the
// target is put first on every frame of the attack.
void list_target(uint32_t rec,uint32_t t){
    const uint32_t l=rd16(rec+0x40);for(uint32_t k=l;k;k>>=5)if((k&0x1F)==t+1)return;
    psx_mod_write_half(rec+0x40,(uint16_t)(((l<<5)|(t+1))&0x7FFF));}
uint32_t call(CPUState*c,uint32_t fn,uint32_t ret,uint32_t a0,uint32_t a1,uint32_t a2){return psx_mod_call_guest_uncharged(c,fn,ret,a0,a1,a2,0,0,nullptr);}
// 0x800C2348's choice (weapon w = rec+0x2E): attack code, 0 none, -1 refused.
int decide(bool r2,bool r1,bool l1,bool up,bool down,int w){
    if(r2)return down?77:up?75:71;
    if(r1&&down)return w==9?38:(w==1||w==5)?148:(w>=6&&w<=8)?-1:0;
    if(r1&&up)return (w==0||w==2||w==3||w==8)?148:(w==6||w==7)?-1:0;
    if(l1&&up)return (w==4||w==6||w==7)?148:w==8?-1:0;
    if(r1)return w==9?32:(w>=6&&w<=8)?-1:142;
    if(l1)return w==9?36:(w>=6&&w<=8)?-1:146;
    return 0;
}
void attack(CPUState*c,int s,uint32_t r,uint32_t v,uint32_t rec,uint32_t cfg,uint16_t pad){
    const uint32_t sp=c->gpr[29],REC=sp-0xE0,ENT=sp-0x10,clock=rd32(cfg+0x0C);
    c->gpr[29]=sp-0x100;   // the callee frames go below the record
    for(uint32_t o=0;o<0xC0;o+=4)psx_mod_write_word(REC+o,0);
    psx_mod_write_word(REC,clock);psx_mod_write_word(REC+0xB4,1);psx_mod_write_word(REC+0xB8,0x800526A4u);
    const uint16_t lo=(uint16_t)(~pad&0xFF),hi=(uint16_t)((~pad>>8)&0xFF);psx_mod_write_word(REC+4,(uint32_t)(hi|(lo<<8)));
    bool press[16]{},held[16]{};
    for(const auto&k:COMBAT){
        const bool h=!(pad&(1u<<k.bit));int32_t&p=prev_ts[s][k.slot];int32_t ts;uint8_t tap=0;
        if(h){if(p>0)ts=p;else{ts=(int32_t)clock-1;tap=1;press[k.slot]=true;}}
        else ts=p>0?-(int32_t)((clock-(uint32_t)p)>1?clock-(uint32_t)p:1):0;
        held[k.slot]=h;p=ts>0?ts:0;
        psx_mod_write_word(REC+0x14+8*k.slot,(uint32_t)ts);psx_mod_write_byte(REC+0x1A+8*k.slot,tap);}
    const uint32_t top=r+0x3B4+8*(uint32_t)(int32_t)(int8_t)rd8(r+0x3B2);
    if(rd16(top)==16){if(rd16(top+2)<16)list_target(rec,rd16(top+2));
        call(c,0x800C258C,0x8001D380,REC,r,0);}   // during an attack: follow-up combos
    else{
        const bool any=press[SLOT_R2]||press[SLOT_R1]||press[SLOT_L1];
        const int code=decide(press[SLOT_R2],press[SLOT_R1],press[SLOT_L1],held[SLOT_UP],held[SLOT_DOWN],(int)rd8(rec+0x2E));
        if(any&&code<=0)psx_mod_counter_add(code<0?"rr.takeover.atk_refused":"rr.takeover.atk_no_move",1);
        if(code>0){
            psx_mod_write_byte(rec+0x3C,(uint8_t)code);
            if(rd32(rec+0x28)||rd8(rec+0x27)>=248)psx_mod_counter_add("rr.takeover.atk_busy",1);
            if(!rd32(rec+0x28)&&rd8(rec+0x27)<248){
                // Target: the nearest riding rider (224 = none). The game's search 0x8008B428 looks around
                // P1/P2, so for this rider it named riders thousands of units away.
                uint32_t t=224;{const uint32_t base=rd32(RIDERS);uint32_t best=TARGET_RANGE;
                    const int32_t x0=(int32_t)rd32(r+0x0C),z0=(int32_t)rd32(r+0x14);
                    for(uint32_t i=0;i<16;i++){const uint32_t ri=base+i*STRIDE;if(ri==r||!rd16(ri+0x140))continue;
                        const uint32_t dx=(uint32_t)std::abs((int32_t)rd32(ri+0x0C)-x0),dz=(uint32_t)std::abs((int32_t)rd32(ri+0x14)-z0),d=dx>dz?dx:dz;
                        if(d<best){best=d;t=i;}}}
                psx_mod_counter_add(t<16?"rr.takeover.atk_target":"rr.takeover.atk_no_target",1);
                if(t<16){const uint32_t vr=rd32(rd32(RIDERS)+t*STRIDE+0x43C);if(ram(vr)){watch[s]={vr,rd8(vr+0x0F),40};}list_target(rec,t);}
                if(!(rd16(top)==16&&rd16(top+2)==t)){psx_mod_write_half(ENT,16);psx_mod_write_half(ENT+2,(uint16_t)t);psx_mod_write_word(ENT+4,0);
                    call(c,0x800BCA68,0x800C2020,ENT,2,r);}}
            psx_mod_write_byte(v+0x23D,0);psx_mod_counter_add("rr.takeover.attacks",1);}
    }
    if(press[SLOT_TRI]&&rd16(v+0x220)&&!(rd8(rec)&0x40))call(c,0x800C2218,0x8001D3EC,REC,r,0xFFFFFFFFu);   // weapon cycle
    c->gpr[29]=sp;
}
void drive(CPUState*c){
    const uint32_t cfg=rd32(CFG);
    const uint32_t seats=ram(cfg)?seat_mask():0;
    const bool racing=ram(cfg)&&rd8(cfg)==1&&rd8(cfg+4)==MODE_SKULL;
    const uint32_t base=rd32(RIDERS);const int humans=ram(cfg)?(int)rd32(cfg+0x30):2;
    for(int s=2;s<MAX_SEATS;s++){
        seat_rider[s]=0;
        if(!racing||!(seats&(1u<<s))||!ram(base)){if(taken[s])release(s);recovering&=~(1u<<s);continue;}
        int t=target_of(s);
        const bool tst=test_mode()>=2&&!psx_netplay_active();
        if(tst){   // tests: the riding AI rider nearest Player 1 (mode 3: the one with the closest riding neighbour, chosen per window)
            static int pick=-1;
            if(test_mode()==3&&!taken[s])pick=-1;
            auto gap=[&](int a,int b){const uint32_t ra=base+(uint32_t)a*STRIDE,rb=base+(uint32_t)b*STRIDE;
                const uint32_t dx=(uint32_t)std::abs((int32_t)rd32(ra+0x0C)-(int32_t)rd32(rb+0x0C)),dz=(uint32_t)std::abs((int32_t)rd32(ra+0x14)-(int32_t)rd32(rb+0x14));
                return dx>dz?dx:dz;};
            auto riding=[&](int i){return rd16(base+(uint32_t)i*STRIDE+0x140)!=0;};
            if(pick<0){uint32_t best=~0u;
                for(int i=humans;i<16;i++){if(!riding(i))continue;
                    if(test_mode()!=3){const uint32_t d=gap(i,0);if(d<best){best=d;pick=i;}continue;}
                    for(int j=0;j<16;j++)if(j!=i&&riding(j)){const uint32_t d=gap(i,j);if(d<best){best=d;pick=i;}}}}
            if(pick>=0)t=pick;}
        if(t<humans||t>=16){if(taken[s])release(s);continue;}
        const uint32_t r=base+(uint32_t)t*STRIDE,v=rd32(r+0x354);
        if(taken[s]&&taken[s]!=r)release(s);
        seat_rider[s]=r;
        // Not riding (before the start, or crashed and waiting to respawn): the AI's own
        // respawn path brings it back, so hand it over until it rides again.
        if(!ram(v)||!rd16(r+0x140)||!rd16(v+0x140)){if(taken[s])release(s);continue;}
        const uint32_t rec=rd32(r+0x43C);
        if(ram(rec)&&rd32(rec+0x28)){if(taken[s])release(s);continue;}  // finished: the AI rides it home
        // Crashed (the game clears the word and the bike stops): the AI's recovery puts the rider
        // back on its bike, then the seat takes over again once it rides off.
        {const uint32_t cw=rd32(r+0x230);const int32_t spd=(int32_t)rd32(r+0x240);
         if(taken[s]&&!(cw&~AI)&&spd==0){release(s);recovering|=1u<<s;psx_mod_counter_add("rr.takeover.recover",1);continue;}
         if(recovering&(1u<<s)){if(spd<200000)continue;recovering&=~(1u<<s);}}
        const uint16_t pad=sio_get_pad_buttons_slot(tst?0:s);   // active low
        if(tst&&test_mode()==3){
            const uint16_t m=(1u<<11)|(1u<<10)|(1u<<9);
            if((pad&m)!=m)test_window=90;else if(test_window)test_window--;   // a combat button held (active low)
            if(!test_window){if(taken[s])release(s);continue;}
            psx_mod_counter_add("rr.takeover.test_frames",1);}
        if(!taken[s])psx_mod_counter_add("rr.takeover.seats",1);
        taken[s]=r;
        auto held=[&](int bit){return !(pad&(1u<<bit));};
        const bool accel=held(14),brake=held(15),left=held(7),right=held(5),down=held(6);   // Cross, Square, D-pad
        uint32_t w=rd32(r+0x230)&~AI,set=0,clr=0;
        if((int32_t)rd32(v+0x25C)<2){                                    // on a bike
            if(accel){if(!(w&0x2))set|=0x3;}else clr|=0x3;
            if(brake){if(!(w&0x40))set|=0x60;}else clr|=0x60;
            if(left){if(!(w&0x100))set|=0x100|0x80;}else if(w&0x100){set|=0x80;clr|=0x100;}
            if(right){if(!(w&0x200))set|=0x200|0x80;}else if(w&0x200){set|=0x80;clr|=0x200;}
            if(!(set&0x80))clr|=0x80;                                    // the steer change event lasts one frame
            if(down)set|=0x8;else clr|=0x8;
            if(!accel&&(w&0x1))clr|=0x1;if(!brake&&(w&0x20))clr|=0x20;
            if(accel&&(w&0x2))clr|=0x1;if(brake&&(w&0x40))clr|=0x20;     // press events last one frame
        }
        psx_mod_write_word(r+0x230,(w&~clr)|set);
        watch_hits(s);
        if((int32_t)rd32(v+0x25C)<2&&ram(rec))attack(c,s,r,v,rec,cfg,pad);
    }
}
}
// The game's input handler: Player 1's and 2's words are written here; the takeover seats' right after.
namespace {struct Pending {uint32_t ra,sp;bool on;} handler_call,range_call,frame_call,cam_call;uint32_t range_a0,cam_a0,cam_a1;
// ---- own view ----
constexpr uint32_t CAM0=0x800CD898,CAM_SIZE=0x46C,DB_PTR=0x8005B470,P1_RIDER=0x8005B38C,VIEW_RIDERS=0x8005B268,VIEW_RECTS=0x8005B474;
uint32_t own_buffer;   // the frame buffer the local view draws into
uint8_t own_cam[CAM_SIZE];uint32_t own_cam_rider;uint32_t own_dt;   // our chase camera between frames; game dt not yet applied
uint32_t gcall(CPUState*c,uint32_t fn,uint32_t ret,uint32_t a0=0,uint32_t a1=0,uint32_t a2=0,uint32_t a3=0){return psx_mod_call_guest_uncharged(c,fn,ret,a0,a1,a2,a3,0,nullptr);}
void wr_block(uint32_t a,const uint8_t*p,uint32_t n){for(uint32_t o=0;o<n;o+=4){uint32_t v;std::memcpy(&v,p+o,4);psx_mod_write_word(a+o,v);}}
int own_view_draw(CPUState*c,void*user,uint32_t){
    const uint32_t r=(uint32_t)(uintptr_t)user,cfg=rd32(CFG),db=rd32(DB_PTR),d=own_buffer,sp=c->gpr[29];
    c->gpr[29]=sp-0x100;const uint32_t fsp=c->gpr[29];psx_mod_counter_add("rr.takeover.ov_enter",1);
    psx_mod_write_byte(db+0x10+0x70*d+0x18,1);   // the draw env clears the buffer
    gcall(c,0x80048E24,0x8001B780,db+0x10+0x70*d);                       // PutDrawEnv: this buffer
    // One view, for this rider: Player 1's pointers, the view count, the frame-ready flag (no VBlank comes in frozen time).
    psx_mod_write_word(P1_RIDER,r);psx_mod_write_word(VIEW_RIDERS,r);psx_mod_write_word(cfg+0x30,1);psx_mod_write_byte(db+4,1);
    // State the one-view path reads but only race init sets: the sky gradient quad (0x800523D0, still
    // holding view 2's rectangle after the split frame), the static panorama for one view (0x8005B2D0;
    // the streamed one does not exist in a split race), a zero-height divider, and a clear of the buffer.
    {const uint16_t sky[]={0,0,384,0,384};for(int i=0;i<5;i++)psx_mod_write_half(0x800523D0+2*i,sky[i]);psx_mod_write_half(0x800523DC,0);}
    psx_mod_write_word(0x8005B2D0,1);
    {const uint32_t vr=rd32(VIEW_RECTS);if(ram(vr))psx_mod_write_half(vr+6,rd16(vr+10));}
    psx_mod_write_byte(db+0x10+0x70*d+0x18,1);
    // Chase camera in slot 0: ours, stepped by the game's dt; kept for the next frame right after the step.
    if(own_cam_rider!=r){   // a fresh chase camera behind this rider, as race init makes one (RASHCDI 0x80067708)
        psx_mod_write_word(fsp+16,0);psx_mod_write_word(fsp+20,1);gcall(c,0x8002F308,0x800125DC,r,CAM0,159,0);psx_mod_write_word(CAM0+0x2C,0);
        std::memcpy(own_cam,memory_get_ram_ptr()+(CAM0&0x1FFFFF),CAM_SIZE);own_cam_rider=r;}
    wr_block(CAM0,own_cam,CAM_SIZE);psx_mod_write_word(CAM0+0x238,r);
    for(uint32_t o=0;o<12;o+=4)psx_mod_write_word(CAM0+0x1D4+o,rd32(CAM0+0xB8+o));
    if(own_dt){gcall(c,0x800881B4,0x800125DC,CAM0,own_dt);
        if((rd32(CAM0+0x224)&0x100)&&gcall(c,0x800A421C,0x800125F8,CAM0))gcall(c,0x80086E1C,0x80012608,CAM0);
        own_dt=0;}
    std::memcpy(own_cam,memory_get_ram_ptr()+(CAM0&0x1FFFFF),CAM_SIZE);
    psx_mod_counter_add("rr.takeover.ov_camera",1);
    static int stage=-1;if(stage<0){const char*e=std::getenv("RR_OWNVIEW_STAGE");stage=e?std::atoi(e):9;}   // tests: stop after a stage
    if(stage<=1){c->gpr[29]=sp;return 0;}
    gcall(c,0x8008CFDC,0x80011DB4);                                      // visibility and detail for this camera
    gcall(c,0x800C89A0,0x80011E44);gcall(c,0x8004D184,0x80012098,192,120);
    gcall(c,0x8002F2E8,0x80011FB0,0);gcall(c,0x800674C8,0x80011FB8);
    gcall(c,0x8008D56C,0x80012000,0);gcall(c,0x800358C0,0x80012008,0);gcall(c,0x800C8B24,0x80012010,0);
    if(rd32(0x8005B314))gcall(c,0x800A2138,0x8001202C,0);
    gcall(c,0x80035958,0x80012034,0);gcall(c,0x800C8CD4,0x8001203C,0);
    if(rd8(0x800D8060))gcall(c,0x8002C928,0x80012068,0,rd32(0x800CD660));
    psx_mod_counter_add("rr.takeover.ov_view",1);if(stage<=2){c->gpr[29]=sp;return 0;}
    gcall(c,0x8004D184,0x8001209C,192,120);gcall(c,0x8001E084,0x800120A4);gcall(c,0x8004CE14,0x800120BC,0x800CD668,1,1,0);
    {const uint32_t t0=rd32(0x8005B5AC),clip=rd32(0x800CD660);
     psx_mod_write_word(0x800CD668,rd32(t0+0x2C)|0x01000000u);psx_mod_write_word(t0+0x2C,0x800CD668u&0xFFFFFF);
     psx_mod_write_word(fsp+16,0);psx_mod_write_word(fsp+20,0);psx_mod_write_word(fsp+24,t0+0x2C);
     gcall(c,0x8001C304,0x8001211C,(uint32_t)(int32_t)(int16_t)rd16(clip),(uint32_t)(int32_t)(int16_t)rd16(clip+2),384,240);
     psx_mod_counter_add("rr.takeover.ov_scene",1);gcall(c,0x8005E848,0x80012124);gcall(c,0x8002CA5C,0x8001212C);       // the race HUD for this rider
     gcall(c,0x80048DB4,0x800121B0,t0+0x2C);}
    c->gpr[29]=sp;psx_mod_counter_add("rr.takeover.own_views",1);return 1;
}
void own_view(CPUState*c){
    if(!psx_netplay_active())return;
    const int seat=psx_netplay_local_slot();if(seat<2||seat>=MAX_SEATS)return;
    const uint32_t cfg=rd32(CFG),base=rd32(RIDERS),db=rd32(DB_PTR);
    if(!ram(cfg)||rd8(cfg)!=1||rd8(cfg+4)!=MODE_SKULL||!ram(base)||!ram(db)){own_cam_rider=0;own_dt=0;return;}
    const int t=target_of(seat);const uint32_t r=base+(uint32_t)t*STRIDE;
    // The game's camera dt this frame (0x80012524: 218 x ticks, ticks at cfg+0x1C, up to 30).
    if(own_cam_rider){uint32_t ticks=rd32(cfg+0x1C);if(ticks>30)ticks=30;own_dt+=218*ticks;if(own_dt>218u*30)own_dt=218u*30;}
    const uint32_t d=rd8(db+5),env=db+0x6C+0x70*d;
    own_buffer=d;PSXModRenderPass rect{};rect.struct_size=sizeof rect;rect.x=rd16(env);rect.y=rd16(env+2);rect.w=rd16(env+4);rect.h=rd16(env+6);
    if(!rect.w||!rect.h)return;
    // The sandbox restores the machine but not our fast-timing counters: keep them, or this PC's clock drifts from the others.
    const uint32_t raw=g_rr_fast_raw,epc=g_rr_edge_pc,ebud=g_rr_edge_budget;const uint64_t est=g_rr_edge_stores,emm=g_rr_edge_mmio;
    const int shown=psx_mod_render_local_view(c,&rect,own_view_draw,(void*)(uintptr_t)r);
    g_rr_fast_raw=raw;g_rr_edge_pc=epc;g_rr_edge_budget=ebud;g_rr_edge_stores=est;g_rr_edge_mmio=emm;
    if(!shown){char n[48];std::snprintf(n,sizeof n,"rr.takeover.own_view_refused_%u",psx_mod_render_local_view_status());psx_mod_counter_add(n,1);}
}
}
// The game's frame function: once it has drawn the shared frame, this PC's own view replaces the picture.
extern "C" void __wrap_func_80011C4C(CPUState*c){
    if(!c->pc||c->pc==0x80011C4Cu)frame_call={c->gpr[31],c->gpr[29],true};
    __real_func_80011C4C(c);
    if(frame_call.on&&c->pc==frame_call.ra&&c->gpr[29]==frame_call.sp){frame_call.on=false;own_view(c);}
}

extern "C" void __wrap_func_8001CB3C(CPUState*c){
    if(!c->pc||c->pc==0x8001CB3Cu)handler_call={c->gpr[31],c->gpr[29],true};   // a fresh call (not a continuation)
    __real_func_8001CB3C(c);
    if(handler_call.on&&c->pc==handler_call.ra&&c->gpr[29]==handler_call.sp){handler_call.on=false;drive(c);}
}
// Rider activity range (0x80039F68, a0 = rider+0xAC): a taken-over rider always rides.
extern "C" void __wrap_func_80039F68(CPUState*c){
    if(!c->pc||c->pc==0x80039F68u){range_call={c->gpr[31],c->gpr[29],true};range_a0=c->gpr[4];}
    __real_func_80039F68(c);
    if(range_call.on&&c->pc==range_call.ra&&c->gpr[29]==range_call.sp){range_call.on=false;
        for(int s=2;s<MAX_SEATS;s++)if((taken[s]&&taken[s]+0xAC==range_a0)||(seat_rider[s]&&seat_rider[s]+0xAC==range_a0)){c->gpr[2]=1;break;}}
}

// SPU key on/off (0x80050678): sound the frame's code would start or stop. Inside the own-view
// sandbox it must not touch the SPU (a device read there rolls the whole view back), and
// nothing it would do survives the sandbox anyway: return at once.
extern "C" void __wrap_func_80050678(CPUState*c){
    if(psx_mod_local_view_scope()&&(!c->pc||c->pc==0x80050678u)){c->gpr[2]=0;c->pc=c->gpr[31];return;}
    __real_func_80050678(c);
}