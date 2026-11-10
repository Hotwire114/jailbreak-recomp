/* Built-in Jailbreak PC main-menu extension. LGPL-2.1-or-later.
 * Independently authored. Redirects the original menu's text arguments and
 * native selection field; original font rendering, textures and timing run
 * normally. No game instructions, glyphs or assets are included. SDL and pad
 * adapters consume editing input while the original menu remains active.
 */
#include "rider_enhancements.h"
#include "pc_services.h"
#include "gfx_options.h"
#include "native_ui.h"
#include "taunt_voices.h"
#include "custom_music.h"
#include "quick_resume.h"
#include "controls_screen.h"
#include "netplay_session.h"
#include "game_editor.h"
#include "cpu_state.h"
#include "mod_plugins.h"
#include "showroom.h"
#include "mouse_menus.h"
#include "online_screen.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
extern "C" uint8_t* memory_get_ram_ptr(void);
extern "C" CPUState* debug_cpu_ptr;
extern "C" bool SDLCALL __real_SDL_PollEvent(SDL_Event*);
extern "C" void __real_sio_set_pad_state_slot(int,uint16_t);
extern "C" void rr_fast_set_mode(int);                     // race timing (fast_timing.py), as Display > Frame rate
extern "C" void rr_fps120_set(int);
extern "C" int rr_look_count(int kind);                    // owner_assets.c: rider look presets
extern "C" const char*rr_look_name(int kind,int value);
namespace {
bool open,editing,options_selected;int row,seat,cursor,page_kind;char draft[9],message[32];uint32_t saved_selection;
uint32_t borrowed;uint8_t borrowed_original[256],borrowed_current[256];bool borrowed_active;
char labels[10][32],description[4][32],select_prompt[12],back_prompt[12];
const unsigned expanded_count=15,label_offset=2816;
// First PC-menu row label y; rows every 20. The game's own menus start at 50
// (bar at 46), right under the heading; 40 put the first bar over "OPTIONS:".
// Eight rows still end (206) above the Select/Help line (212).
const int first_row=50;
uint32_t expanded,original_table,original_count;uint16_t original_last;bool expanded_active;
uint8_t expanded_original[3600];
uint64_t last_pause,last_main,last_keyboard,bypass_until;uint16_t last_buttons=0xffff;bool release_guard;
char pending_text_key;
bool jukebox_loan;int jukebox_page;
// Pause menu. The game's own pause menu gets a fourth row, OPTIONS, under
// RESTART (pause_drawer): while paused its item count 0x800540D8 is 4, so the
// game's Up/Down reach it, and Cross there opens the PC pause page. The row's
// text lives in the hidden result-message span the PC pause page borrows
// (pause_loan; the last 8 of its 256 bytes). pause_sub 1 = Quick resume
// (player save slots, quick_resume.cpp).
int pause_sub,quick_slot;bool pause_loan,fake_sel,row4_drawing;
// Where the pause rows were last drawn (label y, by line), for the mouse (mouse_menus.cpp).
int pause_row_y[4];uint64_t pause_row_at[4];
const uint32_t pause_lit=0x348A32,pause_dim=0x505A50;   // the game's selected / plain row colours
// Main menu Quit row. While the game shows its root menu (and the PC menu is
// closed) the borrowed layout below is kept with four rows: the game's own Solo,
// Multiplayer and Options records and a Quit row (main_rows). The game still
// navigates its three rows itself; only stepping onto / off Quit is ours
// (quit_row: the game's cursor stays on Options while Quit is lit). Before any
// press that makes the game leave the root menu (either pad), and as soon as it
// has left (the attract movie after ~13 s idle), the game's own tables come back
// (main_drop); main_hold keeps them until the root menu shows again. Quit opens
// page 13, the confirmation in the style of the game's "Abort Game?": the row
// becomes "Quit Game?" with arrows, No / Yes beside it, No first; Yes closes the
// game through the framework's own quit (SDL quit event: memory cards flushed).
// The Online row sits between Options and Quit the same way (online_row; Cross
// opens the Online screens, online_screen.cpp).
bool main_rows,quit_row,online_row,main_hold,quit_yes;uint16_t main_mask,last_buttons2=0xffff;
const unsigned quit_help=label_offset+8*32,online_help=quit_help+4*32;   // the Quit / Online rows' description lines (4 x 32, in the borrowed span)
uint64_t pending_text_tick;
const char alphabet[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'-_.";
bool ram(uint32_t p,uint32_t n){uint32_t o=p&0x1fffffff;return (p<0x200000u||(p>=0x80000000u&&p<0x80200000u)||(p>=0xa0000000u&&p<0xa0200000u))&&o<=0x200000u&&n<=0x200000u-o;}
bool paused(){return last_pause&&SDL_GetTicks()-last_pause<250;}
bool main_context();
uint32_t word(uint32_t);
bool main_menu(){uint32_t cfg=word(0x8005B2F8);bool racing=cfg>=0x80010000&&ram(cfg,0x40)&&memory_get_ram_ptr()[cfg&0x1fffff]==1;return last_main&&main_context()&&!racing;}
// Online the PC menu stays closed: it writes menu tables, borrowed text and
// pad 1 / 2 states from this PC's keys and clock (netplay_session.cpp).
bool available(){
    if(rrpc::netplay())return false;
    if(borrowed_active&&std::memcmp(memory_get_ram_ptr()+(borrowed&0x1fffff),borrowed_current,256))return false;
    if(expanded_active&&(word(0x800990C4)!=expanded||(word(0x800990BC)&0xffff)!=expanded_count))return false;
    return page_kind==2?paused():main_menu();
}
bool racing(){uint32_t cfg=word(0x8005B2F8);return cfg>=0x80010000&&ram(cfg,0x40)&&memory_get_ram_ptr()[cfg&0x1fffff]==1;}
bool jukebox_context(){return word(0x8009C5C8)==0x80099244&&word(0x8009C5B8)==1&&word(0x8009C5BC)==3&&word(0x80099254)==0x800972DC&&(word(0x8009924C)&0xffff)==8;}
uint32_t word(uint32_t p){auto*r=memory_get_ram_ptr()+(p&0x1fffff);return r[0]|(uint32_t)r[1]<<8|(uint32_t)r[2]<<16|(uint32_t)r[3]<<24;}
uint16_t main_selection(){return (uint16_t)word(0x800990B8);}
bool main_context(){return word(0x8009C5C8)==0x800990B4&&word(0x8009C5B8)==1&&word(0x8009C5BC)==3;}
bool layout(bool flat){
    if(!flat){if(expanded_active&&main_context()&&word(0x800990C4)==expanded&&(word(0x800990BC)&0xffff)==expanded_count){std::memcpy(memory_get_ram_ptr()+(expanded&0x1fffff),expanded_original,sizeof expanded_original);psx_mod_write_word(0x800990C4,original_table);psx_mod_write_word(0x800990BC,original_count);psx_mod_write_half(0x800990BA,original_last);}expanded_active=false;return true;}
    if(expanded_active&&word(0x800990C4)==expanded)return true;
    if(!main_context()||word(0x8009C5BC)!=3)return false;
    original_table=word(0x800990C4);original_count=word(0x800990BC);original_last=(uint16_t)(word(0x800990B8)>>16);
    if(!ram(original_table,sizeof expanded_original)||(original_count&0xffff)!=10||original_last!=4||word(0x800990D8)!=original_table+1200||word(0x800990EC)!=original_table+2400)return false;
    // Borrow the root table and the following hidden menu table only while
    // PC pages consume input. Back up the complete span, including text.
    // This avoids changing snapshot memory layout or allocating guest memory.
    expanded=original_table;std::memcpy(expanded_original,memory_get_ram_ptr()+(expanded&0x1fffff),sizeof expanded_original);
    for(unsigned i=0;i<expanded_count;i++){
        // Rows 0-2 keep the game's own Solo / Multiplayer / Options records (the
        // main menu's Quit row needs them as they are); the extra rows copy Solo.
        unsigned source=i<5?i:i<10?2:i-5;
        for(unsigned b=0;b<120;b++)psx_mod_write_byte(expanded+i*120+b,expanded_original[source*120+b]);
        if(i>=2&&i<10){int y=first_row+(i-2)*20;psx_mod_write_half(expanded+i*120+18,(uint16_t)i);psx_mod_write_half(expanded+i*120+38,y);psx_mod_write_half(expanded+i*120+50,y-4);}
        if(source==8||source==9){psx_mod_write_half(expanded+i*120+26,226);psx_mod_write_half(expanded+i*120+34,226);}
    }
    // The banner record's name (OPTN for the PC pages, MAIN for the main menu's
    // rows) is written per page by page() / main_records().
    // Keep the game's three-way logic/description dispatch on valid stock
    // selection 2. The extra rows are presentation records, handled by us.
    psx_mod_write_word(0x800990C4,expanded);psx_mod_write_word(0x800990BC,(original_count&0xffff0000)|expanded_count);expanded_active=true;return true;
}
// PC pages (page_kind): 0 Options (Display, User settings, Audio, Game settings), the
// same four categories as the launcher's Options; 7 Display; 8 User settings =
// everything about a rider (1 Rider names, 10 Taunt voices) and 5 Achievements;
// 6 Audio (music, the launcher's Audio page); Game settings opens the game's own
// options page (11, the old System page with Discord, is no longer reached); 2 the pause page. 15 is Display's second screen, More options
// (frame rate, anti-aliasing, scanlines, draw distance, soft shadows); its last
// row opens 16, Effects (bloom, sharpening, colour, vignette). 14 Rider look (User settings' fourth row).
// Achievements (5): eight rows over the list and its last row, Reset all
// achievements; ach_index is the lit entry, ach_top the first one shown.
int ach_index,ach_top;bool ach_confirm;
// Display (7) groups the launcher's way: Screen (17), Picture (15), the draw
// distance (Detail) and Effects (16).
int page_count(){return page_kind==13?5:page_kind==0||page_kind==8||page_kind==7||page_kind==17?4:page_kind==5||page_kind==14?8:page_kind==11?2:page_kind==15?6:page_kind==16?5:3;}
int parent_kind(int k){return k==1||k==10||k==5||k==14?8:k==2?2:k==15||k==16||k==17?7:0;}
int row_in_parent(int k){return k==10||k==8||k==15?1:k==14||k==6?2:k==11||k==5||k==16?3:0;}
// change_graphics items (pc_services.h) of the Display rows; -1: HD UI / HUD labels
// (rider options), -2: opens a page.
static int display_item(int kind,int r){
    if(kind==17){const int s[]={0,2,4,5};return s[r&3];}
    if(kind==15){const int p[]={1,3,6,7,-1,-1};return r<6?p[r]:-1;}
    if(kind==16)return r<4?9+r:13;
    return r==2?8:-2;
}
// Rider look rows: 0/1 paint, 2/3 name tag, 4/5 outfit, 6/7 colours (Player 1, Player 2).
// The launcher's page also has "Reset rider look"; here every row steps back to Original.
int&look_value(rrpc::RiderOptions&o,int r){return r<2?o.paint[r]:r<4?o.tag[r-2]:r<6?o.outfit[r-4]:o.colour[r-6];}
int look_step(int r,int v,int d){int n=rr_look_count(r/2);return ((v+d)%n+n)%n;}
// Borrowed-layout record fields (120-byte frontend menu records): +8 type (0x0C
// bar, 0x0D bar with arrows), +18 the selection that lights it, +20 string id,
// +36/+38 label x/y, +48/+50 bar x/y, +52.. the arrows' texture, colour and x/y.
// Record 1 +16 is the banner's name; record 10 +18 the description box's y;
// records 13 and 14 +26/+34 the prompt line's y.
void rec_half(unsigned rec,unsigned off,int v){psx_mod_write_half(expanded+rec*120+off,(uint16_t)v);}
void row_arrows(unsigned row,bool on){
    const unsigned rec=row+2;rec_half(rec,8,on?0x0d:0x0c);
    psx_mod_write_word(expanded+rec*120+52,on?0x57414C42u:0);psx_mod_write_word(expanded+rec*120+56,on?0x00808080u:0);   // "BLAW", as the game's arrowed rows
    rec_half(rec,60,on?142:0);rec_half(rec,62,on?first_row-4+20*(int)row:0);
}
void layout_style(bool main_banner,int rows){
    uint32_t name;std::memcpy(&name,expanded_original+120+16,4);
    psx_mod_write_word(expanded+120+16,main_banner?name:0x4e54504f);
    rec_half(10,18,rows>=5?151:rows==4?131:123);   // the description below the rows, not under the last bar
    for(unsigned rec=13;rec<15;rec++){rec_half(rec,26,rows>=4?212:226);rec_half(rec,34,rows>=4?212:226);}
}
// A row record's label back in its own place with Solo's text (the Quit Game?
// value borrows the row under the question).
void row_plain(unsigned rec){rec_half(rec,36,37);rec_half(rec,38,first_row+20*((int)rec-2));uint16_t s;std::memcpy(&s,expanded_original+2*120+20,2);rec_half(rec,20,s);}
void row4_plain(){row_plain(6);row_plain(7);}
// The main menu's five rows (PC menu closed). The game lights the row at its
// selection while it draws them; with Online or Quit lit the selection is 5 or
// 6 during the row draw only (rider_menu_font), as for the PC pages.
void main_records(){
    if(main_selection()<4||main_selection()>6)quit_row=online_row=false;   // 5 / 6: polled inside the row draw
    static const char quit_lines[4][32]={"Close the game and go","back to Windows.","",""};   // within the 4:3 description box
    static const char online_lines[4][32]={"Race people online:","rooms, room codes","and chat.",""};
    for(unsigned i=0;i<8;i++){
        rec_half(i+2,18,i<5?(int)i+2:0xfffe);rec_half(i+2,48,i<5?20:0xfe00);
        const char*name=i==3?"Online":i==4?"Quit":"";
        for(unsigned j=0;j<32;j++)psx_mod_write_byte(expanded+label_offset+i*32+j,j<=std::strlen(name)?name[j]:0);
    }
    for(unsigned j=0;j<128;j++){psx_mod_write_byte(expanded+quit_help+j,quit_lines[j/32][j%32]);psx_mod_write_byte(expanded+online_help+j,online_lines[j/32][j%32]);}
    row_arrows(3,false);row_arrows(4,false);row4_plain();layout_style(true,5);
}
// The game's own tables back, wherever the game is now (it has left the root
// menu, or is about to on this press).
void main_drop(){
    if(expanded_active&&word(0x800990C4)==expanded&&(word(0x800990BC)&0xffff)==expanded_count){
        std::memcpy(memory_get_ram_ptr()+(expanded&0x1fffff),expanded_original,sizeof expanded_original);
        psx_mod_write_word(0x800990C4,original_table);psx_mod_write_word(0x800990BC,original_count);psx_mod_write_half(0x800990BA,original_last);
        psx_mod_counter_add("rr.menu.main_rows_dropped",1);
    }
    expanded_active=false;main_rows=false;quit_row=online_row=false;
}
void sync_selection(){if(open&&page_kind!=2&&main_context())psx_mod_write_half(0x800990B8,(uint16_t)(row?4:2));}
void release_text(){if(borrowed_active){auto*r=memory_get_ram_ptr()+(borrowed&0x1fffff);if(!std::memcmp(r,borrowed_current,256))std::memcpy(r,borrowed_original,256);borrowed_active=false;}}
bool borrow_text(uint32_t address){
    if(borrowed_active)return true;if(!ram(address,256))return false;
    auto*r=memory_get_ram_ptr()+(address&0x1fffff);
    // Borrow only a verified, currently hidden text-resource span. Never code,
    // pointers or game state. Return it before the original menus can use it.
    for(unsigned i=0;i<256;i++)if(r[i]&&!(r[i]>=32&&r[i]<127))return false;
    std::memcpy(borrowed_original,r,256);std::memcpy(borrowed_current,r,256);borrowed=address;borrowed_active=true;return true;
}
bool valid(char c){return c&&std::strchr(alphabet,c);}
SDL_Window* text_window(){SDL_Window*w=SDL_GetKeyboardFocus();if(w)return w;int count=0;SDL_Window**windows=SDL_GetWindows(&count);w=count==1?windows[0]:nullptr;SDL_free(windows);return w;}
// keep_main: back on the main menu, which keeps the borrowed layout for its
// four rows (main_rows) instead of restoring it for a frame.
void close(bool keep_main=false){bool owned=borrowed_active&&!std::memcmp(memory_get_ram_ptr()+(borrowed&0x1fffff),borrowed_current,256)&&(!expanded_active||(word(0x800990BC)&0xffff)==expanded_count);if(owned&&open&&page_kind!=2&&main_context()&&saved_selection>=2&&saved_selection<=4)psx_mod_write_half(0x800990B8,(uint16_t)saved_selection);uint32_t lent=borrowed;bool back_to_pause=open&&page_kind==2;
    keep_main=keep_main&&open&&page_kind!=2&&expanded_active&&main_context()&&word(0x800990C4)==expanded&&(word(0x800990BC)&0xffff)==expanded_count;
    if(keep_main){main_rows=true;main_records();}else{layout(false);main_rows=false;quit_row=online_row=false;}release_text();rrpc::native_ui_menu({});open=editing=false;pause_sub=0;pause_loan=false;
    // Back on the game's pause menu: OPTIONS stays drawn (and selected) without a gap.
    if(back_to_pause&&paused()&&!rrpc::quick_busy()&&borrow_text(lent)){std::memcpy(memory_get_ram_ptr()+(lent&0x1fffff)+248,"OPTIONS",8);std::memcpy(borrowed_current+248,"OPTIONS",8);pause_loan=true;}release_guard=true;rrpc::music_preview(false);SDL_StopTextInput(text_window());}
void start(bool pause){if(rrpc::netplay()||!rrpc::rider_options().menu||!(pause?paused():main_menu()))return;
    // Frontend control-help strings are hidden while the root menu is active.
    // In a race, the frontend-only result-message block is hidden during pause.
    uint32_t address=0x800A3C00;
    if(pause){auto*cpu=debug_cpu_ptr;uint32_t table=cpu?word(cpu->gpr[28]+0x8b8):0;if(!ram(table,4*130))return;address=word(table+128*4);}
    if(!borrow_text(address))return;
    saved_selection=pause?0:main_selection();open=true;editing=false;row=0;page_kind=pause?2:0;message[0]=0;release_guard=true;if(!pause&&!layout(true)){close();return;}if(!pause)layout_style(false,3);sync_selection();psx_mod_counter_add(pause?"rr.menu.pause_open":"rr.menu.open",1);}
// Quit on the main menu: the confirmation page, No first.
void confirm_quit(){start(false);if(!open||page_kind!=0)return;page_kind=13;row=4;quit_yes=false;quit_row=true;online_row=false;layout_style(true,5);psx_mod_write_half(0x800990B8,4);psx_mod_counter_add("rr.menu.quit_confirm",1);}
// Yes: the game's own tables and text back first, then the framework's normal
// quit (main.cpp: SDL quit -> shutdown_runtime, which flushes the memory cards;
// our achievements are flushed at exit, settings are written when changed).
void quit_game(){close();main_hold=true;SDL_Event e{};e.type=SDL_EVENT_QUIT;SDL_PushEvent(&e);psx_mod_counter_add("rr.menu.quit",1);}
// The stock pause menu's OPTIONS text: lent while the game shows its pause
// menu, never while a quick save / load is pending (the snapshot keeps the
// game's own text).
void pause_lend(CPUState*c){
    if(pause_loan||borrowed_active||!rrpc::rider_options().menu||rrpc::quick_busy())return;
    uint32_t table=word(c->gpr[28]+0x8b8);if(!ram(table,4*130)||!borrow_text(word(table+128*4)))return;
    std::memcpy(memory_get_ram_ptr()+(borrowed&0x1fffff)+248,"OPTIONS",8);std::memcpy(borrowed_current+248,"OPTIONS",8);pause_loan=true;
}
bool row4_ready(){return rrpc::rider_options().menu&&paused()&&borrowed_active&&(open?page_kind==2:pause_loan);}
uint16_t pause_cursor(){return (uint16_t)(word(0x800540D8)>>16);}
// The game's pause drawer 0x8002D718 draws the heading and three rows through
// 0x8002CB08 (string index, rectangle, colour, centred) and draws no row when
// its cursor 0x800540DA is past RESTART. At its entry the cursor reads 0 for the
// stock rows (RESUME is then drawn plain, see rider_menu_font); after its last
// call (continuation 0x8002D9CC) one more call draws the fourth row 15 lines
// lower, made as the game makes its own calls: arguments in registers and on
// the drawer's frame, $ra = the continuation, pc = the callee.
// Every pad poll with the PC menu closed: keep the main menu's four rows while
// the root menu shows; drop them as soon as the game has left it.
void main_tick(){
    if(!rrpc::rider_options().menu||open)return;
    const bool root=main_context();
    if(!root)main_hold=false;
    if(main_rows&&!root){main_drop();return;}
    if(!main_rows&&!expanded_active&&root&&main_menu()&&!main_hold&&!release_guard&&SDL_GetTicks()>=bypass_until&&layout(true)){main_rows=true;psx_mod_counter_add("rr.menu.main_rows",1);}
    if(main_rows)main_records();
}
bool pause_drawer(CPUState*c){
    if(c->pc==0||c->pc==0x8002D718u){fake_sel=row4_drawing=false;if(row4_ready()&&pause_cursor()==3){psx_mod_write_half(0x800540DA,0);fake_sel=true;}return false;}
    if(c->pc!=0x8002D9CCu)return false;
    bool lit=fake_sel;if(fake_sel){psx_mod_write_half(0x800540DA,3);fake_sel=false;}
    if(row4_drawing){row4_drawing=false;return false;}
    uint32_t sp=c->gpr[29];if(!row4_ready()||!ram(sp+16,16))return false;
    psx_mod_write_half(sp+26,(uint16_t)((int16_t)(word(sp+26)&0xffff)+15));
    psx_mod_write_word(sp+16,lit&&!open?pause_lit:pause_dim);psx_mod_write_word(sp+20,2);
    c->gpr[4]=word(0x8005AD2C);c->gpr[5]=4;c->gpr[6]=sp+24;c->gpr[7]=word(0x8005B5AC)+20;
    c->gpr[31]=0x8002D9CCu;c->pc=0x8002CB08u;row4_drawing=true;psx_mod_counter_add("rr.menu.pause_row4",1);return true;
}
bool persist(rrpc::RiderOptions next){if(!rrpc::save_rider_options(next)){std::strcpy(message,"SAVE FAILED - TRY AGAIN");return false;}rrpc::rider_options()=next;message[0]=0;psx_mod_counter_add("rr.menu.saved",1);return true;}
void begin_edit(){seat=row-1;cursor=0;std::memset(draft,' ',8);draft[8]=0;const char*name=rrpc::rider_options().name[seat];std::memcpy(draft,name,std::strlen(name));editing=true;message[0]=0;SDL_StartTextInput(text_window());}
void accept(){if(!editing){
    if(page_kind==13){if(quit_yes)quit_game();else{close(true);quit_row=true;main_records();}return;}
    // Options' fourth row, Game settings: the game's own options page (as the old System > Game options).
    if(page_kind==0&&row==3){close();main_hold=true;if(main_context())psx_mod_write_half(0x800990B8,4);bypass_until=SDL_GetTicks()+1000;}
    else if(page_kind==0){page_kind=([](int n){const int p[]={7,8,6,11};return p[n];})(row);row=0;if(page_kind==6)rrpc::music_preview(true);sync_selection();}
    else if(page_kind==7&&row!=2){page_kind=row==0?17:row==1?15:16;row=0;message[0]=0;sync_selection();}   // Screen, Picture, Effects
    else if(page_kind==7||page_kind==15||page_kind==16||page_kind==17){int item=display_item(page_kind,row);
        if(item>=0){if(!rrpc::change_graphics(item))std::strcpy(message,"CHANGE FAILED");else message[0]=0;}
        else{auto next=rrpc::rider_options();if(row==4)next.hd=!next.hd;else next.large=!next.large;persist(next);}}
    // Rider look: the showroom (showroom.cpp), a race the game draws itself; the
    // page of rows when it cannot start.
    else if(page_kind==8&&row==2&&rrpc::showroom_begin())return;
    else if(page_kind==8){page_kind=([](int n){const int p[]={1,10,14,5};return p[n];})(row);row=0;ach_index=ach_top=0;ach_confirm=false;sync_selection();}
    else if(page_kind==5){if(ach_index<rrpc::achievement_count())return;if(!ach_confirm){ach_confirm=true;return;}
        ach_confirm=false;std::strcpy(message,rrpc::reset_achievements()?"ACHIEVEMENTS RESET":"RESET FAILED");}
    else if(page_kind==14){auto next=rrpc::rider_options();if(row<8)look_value(next,row)=look_step(row,look_value(next,row),1);persist(next);}
    else if(page_kind==11){if(row==0){if(!rrpc::toggle_discord())std::strcpy(message,rrpc::discord_configured()?"COULD NOT SAVE":"DISCORD NEEDS AN APP ID");}else {close();main_hold=true;if(main_context())psx_mod_write_half(0x800990B8,4);bypass_until=SDL_GetTicks()+1000;}}
    else if(page_kind==10){if(row<2){auto next=rrpc::rider_options();next.voice[row]=rrpc::voice_next(next.voice[row],1,row);if(persist(next))rrpc::voice_preview(next.voice[row],row);}else {auto next=rrpc::rider_options();next.voice[0]=next.voice[1]=0;persist(next);}}
    // Quick resume closes the PC menu first: the game's own pause-menu text is
    // back in RAM (and on screen) before the snapshot is taken.
    else if(page_kind==2&&pause_sub){if(row==2){pause_sub=0;row=1;message[0]=0;}else{const char*why=row?rrpc::quick_load(quick_slot):rrpc::quick_save(quick_slot);if(why)std::snprintf(message,32,"%s",why);else close();}}
    else if(page_kind==2){if(row==0){auto next=rrpc::rider_options();next.large=!next.large;persist(next);}else if(row==1){pause_sub=1;row=0;message[0]=0;}else close();}
    else if(page_kind==1){if(row==0){auto next=rrpc::rider_options();next.names=!next.names;persist(next);}else if(row<3)begin_edit();}
    else if(page_kind==6){if(row==2)rrpc::music_import();else rrpc::music_change(row,1);}
    return;}
    char name[9];std::memcpy(name,draft,9);int n=8;while(n&&name[n-1]==' ')name[--n]=0;
    if(!n){std::strcpy(message,"ENTER A NAME FIRST");return;}
    auto next=rrpc::rider_options();std::strcpy(next.name[seat],name);next.names=true;
    if(persist(next)){editing=false;SDL_StopTextInput(text_window());rrpc::personal_touch();psx_mod_counter_add("rr.menu.name_saved",1);}
}
void cancel(){if(editing){editing=false;message[0]=0;SDL_StopTextInput(text_window());psx_mod_counter_add("rr.menu.cancel",1);}else if(page_kind==2&&pause_sub){pause_sub=0;row=1;message[0]=0;}else if(page_kind==13){close(true);quit_row=true;main_records();}else if(page_kind==0)close(true);else if(page_kind==2)close();else{int old=page_kind;page_kind=parent_kind(old);row=row_in_parent(old);rrpc::music_preview(false);message[0]=0;sync_selection();}}
void move(int dx,int dy){if(!editing){if(page_kind==13){if(dx){quit_yes=!quit_yes;message[0]=0;}return;}if(page_kind==2&&pause_sub&&dx&&row<2){quick_slot=(quick_slot+dx+rrpc::quick_slots)%rrpc::quick_slots;message[0]=0;return;}if(page_kind==6&&dx){rrpc::music_change(row,dx);return;}if(page_kind==10&&dx&&row<2){auto next=rrpc::rider_options();next.voice[row]=rrpc::voice_next(next.voice[row],dx,row);if(persist(next))rrpc::voice_preview(next.voice[row],row);return;}
    if(page_kind==14&&dx&&row<8){auto next=rrpc::rider_options();look_value(next,row)=look_step(row,look_value(next,row),dx);persist(next);return;}
    if(page_kind==5&&dy){int n=rrpc::achievement_count()+1;ach_index=(ach_index+dy+n)%n;if(ach_index<ach_top)ach_top=ach_index;if(ach_index>=ach_top+8)ach_top=ach_index-7;row=ach_index-ach_top;ach_confirm=false;message[0]=0;return;}
    if(dx&&(page_kind==15||page_kind==16||page_kind==17||(page_kind==7&&row==2))){int item=display_item(page_kind,row);
        if(item>=0){if(!rrpc::change_graphics(item,dx))std::strcpy(message,"CHANGE FAILED");else message[0]=0;}
        else{auto next=rrpc::rider_options();if(row==4)next.hd=!next.hd;else next.large=!next.large;persist(next);}
        return;}
    int count=page_count();row=(row+dy+count)%count;message[0]=0;return;}if(cursor>7)cursor=7;cursor=(cursor+dx+8)%8;if(dy){const char*p=std::strchr(alphabet,draft[cursor]);int i=p?int(p-alphabet):0,count=sizeof(alphabet)-1;draft[cursor]=alphabet[(i+dy+count)%count];}}
void backspace(){if(!editing)return;if(cursor)cursor--;for(int i=cursor;i<7;i++)draft[i]=draft[i+1];draft[7]=' ';psx_mod_counter_add("rr.menu.backspace",1);}
void type(const char*s){if(!editing)return;for(;*s;s++)if(valid(*s)&&cursor<8){draft[cursor++]=*s;psx_mod_counter_add("rr.menu.typed_character",1);}}
void page(){
    std::memset(labels,0,sizeof labels);std::memset(description,0,sizeof description);
    if(page_kind==13){
        // The main menu with its Quit row as the game's "Abort Game? < No >" row.
        const char*names[]={"Solo","Multiplayer","Options","Online","Quit Game?"};for(int i=0;i<5;i++)std::strcpy(labels[i],names[i]);
        std::strcpy(description[0],"Close the game now?");std::strcpy(description[1],"Your saves, settings and");std::strcpy(description[2],"achievements are kept.");
    }else if(page_kind==0){
        const char*names[]={"Display","User settings","Audio","Game settings"};for(int i=0;i<4;i++)std::strcpy(labels[i],names[i]);
        const char*help[]={"Screen, picture, effects.","Names, voices, looks.","Original or your own music.","Saves, sound and controls."};std::strcpy(description[0],help[row]);std::strcpy(description[1],row==3?"The game's own options.":"Select to open this category.");
    }else if(page_kind==7){
        // Display: the launcher's groups (Screen, Picture, Detail, Effects). The
        // draw distance is Detail's only live setting, so it sits here as a value.
        // Labels are short forms: wider than the bar (141 px in the game font) they are shrunk.
        auto&o=rrpc::graphics_options();
        std::strcpy(labels[0],"Screen");std::strcpy(labels[1],"Picture");std::snprintf(labels[2],32,"Distance: %s",rr_draw_name(o.draw));std::strcpy(labels[3],"Effects");
        const char*l1[]={"Resolution, window,","Filter, textures, AA,","How far away riders and","Bloom, sharpening, colour,"};
        const char*l2[]={"aspect ratio, frame rate.","scanlines, HD UI, labels.","traffic are drawn.","vignette, soft shadows."};
        std::strcpy(description[0],l1[row]);std::strcpy(description[1],l2[row]);std::strcpy(description[2],row==2?"Left/Right: change.":"Select to open.");
    }else if(page_kind==17){
        // Screen: render resolution, window, aspect ratio and frame rate, applied at once.
        auto&o=rrpc::graphics_options();const char*res[]={"Native","720p","1080p","1440p","4K"},*aspects[]={"4:3","16:9","21:9"},*rates[]={"Original","60 FPS","120 FPS"};
        std::snprintf(labels[0],32,"Render: %s",res[o.resolution]);std::strcpy(labels[1],o.fullscreen?"Fullscreen":"Windowed");
        std::snprintf(labels[2],32,"Aspect ratio: %s",aspects[o.aspect]);std::snprintf(labels[3],32,"Frame rate: %s",rates[o.motion]);
        const char*l1[]={"Internal render target.","Fullscreen or windowed.","Menu and gameplay aspect.",o.motion==2?"120 FPS needs a 120 Hz":"Real game frames at the"};
        const char*l2[]={"Whole pixel multiples.","Uses your chosen aspect.","Wider view; original HUD.",o.motion==2?"display and a fast PC.":"original game speed."};
        std::strcpy(description[0],l1[row]);std::strcpy(description[1],l2[row]);std::strcpy(description[2],"Left/Right: change.");
    }else if(page_kind==15){
        // Picture: texture filter and correction, anti-aliasing, scanlines (gfx_options.h),
        // HD UI and HUD labels. Select or Right: next value, Left: previous.
        auto&o=rrpc::graphics_options();
        std::snprintf(labels[0],32,"Filter: %s",rr_filter_name(o.filter));std::snprintf(labels[1],32,"Precise vertices: %s",rr_tc_name(o.texture_correction));
        std::snprintf(labels[2],32,"Anti-aliasing: %s",o.aa==3?"SSAA 2x":o.aa==4?"SSAA 4x":rr_aa_name(o.aa));std::snprintf(labels[3],32,"Scanlines: %s",rr_scanline_name(o.scanlines));
        std::snprintf(labels[4],32,"HD UI: %s",rrpc::rider_options().hd?"On":"Off");std::snprintf(labels[5],32,"Labels: %s",rrpc::rider_options().large?"Large":"Original");
        const char*l1[]={"Sharp, smooth or xBR.","No PS1 \"wobble\", true",o.aa>=3?"Heavy on the GPU.":"Smooths jagged edges.","Old TV picture lines.","Smooth UI art and HD text.","Larger race labels."};
        const char*l2[]={"Applies immediately.","perspective textures.",o.aa>=3?"Lower it if races stutter.":"FXAA is light on the GPU.","Off: the clean picture.","Off: the original look.","Also in pause options."};
        if(row==1&&rr_tc_heavy(o.texture_correction)){l1[1]="Heavy on the CPU:";l2[1]="60 FPS, not 120.";}
        if((row==0||row==2)&&rr_xbr_ss_heavy(o.filter,o.aa)){l1[row]="xBR + SSAA: very heavy.";l2[row]="Lower one if races stutter.";}
        std::strcpy(description[0],l1[row]);std::strcpy(description[1],l2[row]);std::strcpy(description[2],"Left/Right: change.");
    }else if(page_kind==16){
        // Effects: post effects on the race picture (change_graphics 9-12) and
        // shadow smoothing (13). Races only, never the HUD.
        auto&o=rrpc::graphics_options();
        const char*fx[]={"Bloom","Sharpen","Colour","Vignette"};for(int e=0;e<4;e++)std::snprintf(labels[e],32,"%s: %s",fx[e],rr_post_name(o.post[e]));
        std::snprintf(labels[4],32,"Soft shadows: %s",rr_shadow_name(o.shadow));
        const char*l1[]={"Soft glow on lights, sky.","Crisper detail, no halos.","Richer colour, contrast.","Darker picture corners.","Soft edges on bike shadows."};
        const bool heavy=row<4?rr_post_heavy(row,o.post[row]):rr_shadow_heavy(o.shadow);
        std::strcpy(description[0],heavy?"Heavy on the GPU.":l1[row]);std::strcpy(description[1],heavy?"Lower it if races stutter.":"Races only; HUD untouched.");std::strcpy(description[2],"Left/Right: change.");
    }else if(page_kind==8){
        const char*names[]={"Rider names","Taunt voices","Rider look","Achievements"},*l1[]={"Both riders' race names.","Taunts in a rider's voice.","Bike paint, name tag","Local achievements."},*l2[]={"Saved on this PC.","Each player picks one.","colour and outfit.","Saved on this PC."};
        for(int i=0;i<4;i++)std::strcpy(labels[i],names[i]);std::strcpy(description[0],l1[row]);std::strcpy(description[1],l2[row]);
    }else if(page_kind==14){
        // Rider look: per player, applied in races (rider_enhancements.cpp).
        auto o=rrpc::rider_options();const char*what[]={"paint","tag","outfit","colours"};
        for(int r=0;r<8;r++)std::snprintf(labels[r],32,"P%d %s: %s",r%2+1,what[r/2],rr_look_name(r/2,look_value(o,r)));
        const char*l1[]={"Your bike's colour.","Your race name's colour.","Gang and rank, or police.","Your rider's colours."},*l2[]={"Left/Right: choose.","Left/Right: choose.","Not in Jail Break.","Not in Jail Break."};
        std::strcpy(description[0],l1[(row/2)&3]);std::strcpy(description[1],l2[(row/2)&3]);std::strcpy(description[2],"Not in Five-O races.");
    }else if(page_kind==11){std::snprintf(labels[0],32,"Discord: %s",rrpc::graphics_options().discord?"On":"Off");std::strcpy(labels[1],"Game options");std::strcpy(description[0],row?"Original game settings.":"Optional playing status.");std::strcpy(description[1],row?"Saves, sound and controls.":rrpc::discord_status());
    }else if(page_kind==10){for(int i=0;i<2;i++)std::snprintf(labels[i],32,"Player %d: %s",i+1,rrpc::voice_name(rrpc::rider_options().voice[i]));std::strcpy(labels[2],"Reset voices");
        std::strcpy(description[0],row==2?"Both players back to":"Taunts in a rider's voice.");std::strcpy(description[1],row==2?"their own voices.":"Left/Right: choose a voice.");
    }else if(page_kind==6){
        rrpc::music_labels(labels,description,row);
    }else if(page_kind==2&&pause_sub){
        // Pause rows: RESUME, QUIT and RESTART lines, then the fourth line,
        // which shows the slot (course, time) or why a request was refused.
        std::strcpy(labels[0],"QUICK RESUME");std::snprintf(labels[1],32,"Save  < %d >",quick_slot+1);std::snprintf(labels[2],32,"Load  < %d >",quick_slot+1);std::strcpy(description[3],"Back");
        if(message[0])std::snprintf(description[2],32,"%s",message);else rrpc::quick_label(quick_slot,description[2],32);
    }else if(page_kind==2){
        std::strcpy(labels[0],"PAUSE OPTIONS");std::snprintf(labels[1],32,"Labels: %s",rrpc::rider_options().large?"Large":"Original");std::strcpy(labels[2],"Quick resume");std::strcpy(description[3],"Back");
    }else if(page_kind==5){
        const int n=rrpc::achievement_count();
        for(int i=0;i<8;i++){int k=ach_top+i;if(k<n)std::snprintf(labels[i],32,"%s %s",rrpc::achievement_unlocked(k)?"+":"-",rrpc::achievement_name(k));else if(k==n)std::strcpy(labels[i],"Reset all");}
        if(ach_index<n){const char*h=rrpc::achievement_description(ach_index),*nl=std::strchr(h,'\n');
            if(nl){std::snprintf(description[0],32,"%.*s",(int)(nl-h),h);std::snprintf(description[1],32,"%s",nl+1);}else std::snprintf(description[0],32,"%s",h);
            if(rrpc::achievement_unlocked(ach_index))std::strcpy(description[2],"UNLOCKED");else{rrpc::achievement_progress(ach_index,description[2],32);if(!description[2][0])std::strcpy(description[2],"LOCKED");}}
        else{std::strcpy(description[0],ach_confirm?"Select again to reset.":"Locks every achievement");std::strcpy(description[1],ach_confirm?"Move or Back: keep them.":"and clears all progress.");
            std::snprintf(description[2],32,"%d of %d unlocked",rrpc::achievements_unlocked(),n);}
    }else if(!editing){
        auto&o=rrpc::rider_options();
        std::snprintf(labels[0],32,"Names: %s",o.names?"On":"Off");for(int i=0;i<2;i++)std::snprintf(labels[i+1],32,"Player %d: %s",i+1,o.name[i]);
        if(row==0){std::strcpy(description[0],"Use custom names on");std::strcpy(description[1],"the race display.");}
        else{std::snprintf(description[0],32,"Change Player %d's name.",row);std::strcpy(description[1],"Up to 8 characters.");}
    }else{
        std::snprintf(labels[0],32,"Player %d name",seat+1);unsigned n=0;for(int i=0;i<8;i++){if(i==(cursor<8?cursor:7))labels[1][n++]='[';labels[1][n++]=draft[i]==' '?'_':draft[i];if(i==(cursor<8?cursor:7))labels[1][n++]=']';labels[1][n++]=' ';}
        std::strcpy(labels[2],"Save name");std::strcpy(description[0],"Type your name, or use");std::strcpy(description[1],"Left/Right to move and");std::strcpy(description[2],"Up/Down: change letter.");
    }
    if(message[0])std::strcpy(description[0],message);
    std::strcpy(select_prompt,editing?": Save":page_kind==1&&row?": Edit":": Select");std::strcpy(back_prompt,": Back");
    // The owner's text (F2 in-game editor, game_editor.cpp): whole help lines,
    // and a label's words before ": " (its value stays live). Not while a name
    // is being typed.
    const char*where=page_kind==13?"Main menu":page_kind==0?"Options":page_kind==7||page_kind==15||page_kind==16||page_kind==17?"Display":page_kind==8?"User settings":page_kind==11?"System":page_kind==10?"Taunt voices":page_kind==14?"Rider look":page_kind==6?"Audio":page_kind==5?"Achievements":page_kind==2?(pause_sub?"Quick resume":"Pause options"):"Rider names";
    if(!editing){
        for(int i=0;i<8;i++){char*s=labels[i];if(!s[0])continue;char*colon=std::strstr(s,": ");
            if(colon&&colon>s){char head[32],tail[32];std::snprintf(head,sizeof head,"%.*s",(int)(colon-s),s);std::snprintf(tail,sizeof tail,"%s",colon);rrpc::menu_text(head,sizeof head,where);std::snprintf(s,32,"%s%s",head,tail);}
            else rrpc::menu_text(s,32,where);}
        for(int i=0;i<4;i++)rrpc::menu_text(description[i],32,where);
    }
    if(!borrowed_active)return;
    auto*r=memory_get_ram_ptr()+(borrowed&0x1fffff);std::memcpy(r,labels,96);std::memcpy(r+96,description,128);std::memcpy(r+224,select_prompt,12);std::memcpy(r+236,back_prompt,12);std::memcpy(borrowed_current,r,256);
    if(page_kind!=2&&expanded_active){
        for(unsigned i=0;i<8;i++){for(unsigned j=0;j<32;j++)psx_mod_write_byte(expanded+label_offset+i*32+j,i<(unsigned)page_count()?labels[i][j]:0);
            psx_mod_write_half(expanded+(i+2)*120+18,i==(unsigned)row?i+2:0xfffe);psx_mod_write_half(expanded+(i+2)*120+48,i<(unsigned)page_count()?20:0xfe00);}
        // Quit Game?: arrows on the row, and the value (the game's own "Yes" /
        // "No", strings 4 / 5) as the fifth row's label beside it, bar hidden.
        const bool quit=page_kind==13;row_arrows(3,false);row_arrows(4,quit);
        if(quit){row_plain(6);rec_half(7,36,190);rec_half(7,38,first_row+80);rec_half(7,20,quit_yes?4:5);}else row4_plain();
        layout_style(quit,quit?5:3);
        rrpc::NativeMenuView view{};view.open=true;view.page=page_kind;view.count=page_count();view.selected=row;
        const char*title=quit?"MAIN MENU":page_kind==0?"OPTIONS":page_kind==7||page_kind==15||page_kind==16||page_kind==17?"DISPLAY":page_kind==8?"USER SETTINGS":page_kind==11?"SYSTEM":page_kind==10?"TAUNT VOICES":page_kind==14?"RIDER LOOK":page_kind==6?"AUDIO":page_kind==5?"ACHIEVEMENTS":"RIDER NAMES";
        std::snprintf(view.title,sizeof view.title,"%s",title);rrpc::menu_text(view.title,sizeof view.title,where);for(int i=0;i<view.count;i++)std::strcpy(view.labels[i],labels[i]);for(int i=0;i<4;i++)std::strcpy(view.help[i],description[i]);
        view.art=quit?7:page_kind==5?3:page_kind==14?rrpc::look_preview_art(rrpc::rider_options().outfit[row<8?row%2:0],rrpc::rider_options().colour[row<8?row%2:0],rrpc::rider_options().paint[row<8?row%2:0]):page_kind==1||page_kind==10||page_kind==14?2:page_kind==11?(row?4:2):page_kind==6?6:page_kind==8?(row==3?3:2):page_kind==0?([](int n){const int p[]={7,2,6,4};return p[n];})(row):page_kind==7||page_kind==15||page_kind==16||page_kind==17?7:row==1||row==3?4:row==5?2:7;
        rrpc::native_ui_menu(view);
    }
    psx_mod_counter_add("rr.menu.draw",1);
}
}
namespace rrpc {
bool rider_menu_drawing(){return open&&page_kind==2&&paused();}
// The showroom: the game's own root menu tables back before it presses
// through them, and User settings again (Rider look lit) when it is done.
void rider_menu_leave(){close();main_hold=true;main_drop();release_guard=false;}
bool rider_menu_open_look(){if(open||!main_menu())return false;start(false);if(!open)return false;page_kind=8;row=2;release_guard=true;sync_selection();return true;}
bool rider_menu_font(CPUState*c){
    // Online: no Quit row, pause OPTIONS row or jukebox library text (guest RAM).
    if(netplay()||!rider_options().menu||!ram(c->gpr[5],32))return false;
    if(open&&!available())close();
    const char*t=(const char*)memory_get_ram_ptr()+(c->gpr[5]&0x1fffff);
    if(jukebox_loan&&!jukebox_context()){release_text();jukebox_loan=false;rrpc::music_preview(false);}
    if(!open&&jukebox_context()){
        if(!borrowed_active&&!borrow_text(0x800A3C00))return false;
        if(!jukebox_loan&&music_is_custom())psx_mod_write_half(0x8009C5D6,0);jukebox_loan=true;
        char text[8][32]{};bool custom=music_is_custom();
        int count=music_track_count(),pages=(count+4)/5;if(jukebox_page>=pages)jukebox_page=0;
        for(int i=0;i<5;i++)music_track_label(jukebox_page*5+i,text[i],25);
        // Button names follow Player 1's keys / controller (controls_screen.cpp): 13 Select, 4 Square.
        std::snprintf(text[5],32,"%s: %s / F7: Add",rrpc::controls_button_name(13),custom?"Original":"Custom");
        if(custom)std::snprintf(text[6],32,"Custom library %d/%d",jukebox_page+1,pages?pages:1);else std::strcpy(text[6],"Original soundtrack");
        std::snprintf(text[7],32,"%s: Play   Left/Right: Page",rrpc::controls_button_name(4));
        auto*r=memory_get_ram_ptr()+(borrowed&0x1fffff);std::memcpy(r,text,sizeof text);std::memcpy(borrowed_current,r,256);
        if(c->gpr[31]==0x8002CC20&&c->gpr[7]==46){if(c->gpr[6]>100&&c->gpr[6]<300){c->gpr[5]=borrowed+192;c->gpr[6]=92;}else if(c->gpr[6]>300&&custom)c->gpr[5]=borrowed+31;}
        if(c->gpr[31]==0x8002CC20&&c->gpr[7]>=66&&c->gpr[7]<=122&&(c->gpr[7]-66)%14==0&&custom){
            unsigned i=(c->gpr[7]-66)/14;
            if(c->gpr[6]>70&&c->gpr[6]<300){c->gpr[5]=borrowed+32*i;c->gpr[6]=95;}
            else c->gpr[5]=borrowed+31; // Empty NUL-terminated cell.
        }
        if((c->gpr[31]==0x80066514||c->gpr[31]==0x800665B8)&&c->gpr[7]==172){c->gpr[5]=borrowed+160;c->gpr[6]=45;}
        else if(custom&&(c->gpr[31]==0x80066514||c->gpr[31]==0x800665B8)&&c->gpr[7]==150){c->gpr[5]=borrowed+224;c->gpr[6]=45;}
        else if(custom&&(c->gpr[31]==0x80066514||c->gpr[31]==0x800665B8)&&c->gpr[7]==161)c->gpr[5]=borrowed+31;
    }
    const bool main_parent=c->gpr[31]==0x8002CDB8&&main_context()&&ram(c->gpr[29]+0x18,4)&&word(c->gpr[29]+0x18)==0x8006F968;
    const bool main_call=main_parent&&c->gpr[6]==37;
    if(main_call&&(c->gpr[7]==50||c->gpr[7]==40)){last_main=SDL_GetTicks();if(open&&page_kind!=2)page();}
    // The expanded highlight belongs only to the native draw interval. At
    // the first label its box is already drawn using a safe stock selection;
    // the remaining boxes then see the real PC row. Restore before original
    // help/action dispatch, which only has three valid entries.
    if(main_call&&open&&page_kind!=2&&expanded_active){if(c->gpr[7]==(unsigned)first_row)psx_mod_write_half(0x800990B8,(uint16_t)(row+2));else if(c->gpr[7]==(unsigned)(first_row+140))psx_mod_write_half(0x800990B8,4);}
    if(main_call&&!open&&main_rows&&(quit_row||online_row)){if(c->gpr[7]==(unsigned)first_row)psx_mod_write_half(0x800990B8,quit_row?6:5);else if(c->gpr[7]==(unsigned)(first_row+140))psx_mod_write_half(0x800990B8,4);}
    if(main_call&&c->gpr[7]==90&&(!expanded_active||main_rows)&&!open)options_selected=main_selection()==4&&!quit_row&&!online_row;
    const uint32_t extra_help=quit_row?quit_help:online_help;   // the lit extra row's description
    // Description lines (the game's help routine): four lines 11 apart from the
    // description box's y (123, or 131 under the main menu's fourth row).
    const int help_top=expanded_active?(int16_t)(word(expanded+10*120+18)&0xffff):123;
    const int help_line=(c->gpr[31]==0x80066514||c->gpr[31]==0x800665B8)&&(int)c->gpr[7]>=help_top&&(int)c->gpr[7]<=help_top+33?((int)c->gpr[7]-help_top)/11:-1;
    static rrpc::NativeMenuView stock;
    if(!open&&main_menu()){
        if(main_call&&c->gpr[7]==50){stock={};stock.open=true;stock.page=-1;stock.count=main_rows?5:3;stock.selected=(int)main_selection()-2;if(stock.selected<0||stock.selected>2)stock.selected=0;if(main_rows&&online_row)stock.selected=3;if(main_rows&&quit_row)stock.selected=4;
            std::strcpy(stock.title,"MAIN MENU");std::strcpy(stock.labels[0],"Solo");std::strcpy(stock.labels[1],"Multiplayer");std::strcpy(stock.labels[2],"Options");std::strcpy(stock.labels[3],"Online");std::strcpy(stock.labels[4],"Quit");stock.art=stock.selected==0?4:stock.selected==1?2:stock.selected==2?5:stock.selected==3?2:7;
            if(stock.selected>=3)stock.page=12;}   // Online / Quit: our picture over the game's (only PC pages, page >= 0, get one)
        if(help_line>=0){std::strncpy(stock.help[help_line],main_rows&&(quit_row||online_row)?(const char*)memory_get_ram_ptr()+((expanded+extra_help+32*help_line)&0x1fffff):t,31);stock.help[help_line][31]=0;}
        rrpc::native_ui_menu(stock);
    }
    // The main menu's rows: the game's own Solo / Multiplayer / Options labels;
    // Quit and the hidden rows from the borrowed span; Quit's description.
    if(!open&&main_rows&&main_menu()){
        if(main_call){for(int i=3;i<8;i++)if(c->gpr[7]==(unsigned)(first_row+20*i))c->gpr[5]=expanded+label_offset+32*i;}
        else if(help_line>=0&&(quit_row||online_row))c->gpr[5]=expanded+extra_help+32*help_line;
    }

    if(open&&page_kind!=2&&main_menu()&&borrowed_active){
        if(main_call){for(int i=0;i<8;i++)if(c->gpr[7]==(unsigned)(first_row+20*i))c->gpr[5]=expanded+label_offset+32*i;}
        else if(help_line>=0){c->gpr[5]=borrowed+96+help_line*32;if(page_kind!=13){c->gpr[6]=230;c->gpr[7]=50+help_line*11;}}
        else if(c->gpr[31]==0x8002CB88&&(c->gpr[7]==212||c->gpr[7]==226)){if(c->gpr[6]==40)c->gpr[5]=borrowed+224;else if(c->gpr[6]==140)c->gpr[5]=borrowed+236;if(page_kind==0)c->gpr[7]=226;}

    }
    if(c->gpr[31]==0x8002CC20&&!std::strcmp(t,"PAUSE MENU")){
        last_pause=SDL_GetTicks();
        if(!open)pause_lend(c);
        if(open&&page_kind==2){page();c->gpr[5]=borrowed;c->gpr[6]=139;}
    }
    if(c->gpr[31]==0x8002CC20&&paused()){int line=row4_drawing?3:!std::strcmp(t,"RESUME")?0:!std::strcmp(t,"QUIT")?1:!std::strcmp(t,"RESTART")?2:-1;if(line>=0){pause_row_y[line]=(int16_t)c->gpr[7];pause_row_at[line]=SDL_GetTicks();}}
    if(c->gpr[31]==0x8002CC20&&paused()&&borrowed_active&&(open?page_kind==2:pause_loan)&&ram(c->gpr[29]+20,4)){
        // Pause rows RESUME, QUIT, RESTART and the added fourth row. With the PC
        // page open: its text, one left edge, and the game's own colours for
        // the selected / other rows (argument 6, the colour, on the stack).
        int line=row4_drawing?3:!std::strcmp(t,"RESUME")?0:!std::strcmp(t,"QUIT")?1:!std::strcmp(t,"RESTART")?2:-1;
        if(line>=0&&open){const unsigned text[]={32,64,192,160};c->gpr[5]=borrowed+text[line];c->gpr[6]=112;psx_mod_write_word(c->gpr[29]+20,line==row?pause_lit:pause_dim);}
        else if(line==3)c->gpr[5]=borrowed+248;
        else if(line>=0&&fake_sel)psx_mod_write_word(c->gpr[29]+20,pause_dim);   // the cursor is on OPTIONS
    }
    return false;
}
void rider_menu_observe(uint32_t pc){(void)pc;}

}
extern "C" bool SDLCALL __wrap_SDL_PollEvent(SDL_Event*event){
    if(rrpc::rider_options().menu){rrpc::pc_tick(racing(),paused());rrpc::quick_tick();rrpc::look_tick();}
    // Original game options: an opt-in frame rate (the same race timing as
    // Enhanced's Display > Frame rate), set once; Esc closes the game.
    else{static bool rate_set;const int rate=rrpc::rider_options().original_rate;if(rate&&!rate_set){rate_set=true;rr_fast_set_mode(1);rr_fps120_set(rate==2);}}
    // Pause menu fourth row: 4 items only while it can be drawn; back to the
    // stock 3 (cursor home if it was on OPTIONS) otherwise.
    if(pause_loan&&!open&&!paused()){release_text();pause_loan=false;}
    if(!rrpc::netplay()){uint32_t menu=word(0x800540D8);uint16_t count=(uint16_t)menu,want=row4_ready()?4:3;
     if((count==3||count==4)&&count!=want){psx_mod_write_half(0x800540D8,want);if(want==3&&(menu>>16)==3)psx_mod_write_half(0x800540DA,0);}}
    rrpc::online_tick();  // the Online screens (online_screen.cpp)
    rrpc::mouse_tick();   // the menu pointer shown / hidden (mouse_menus.cpp)
    if(!event)return __real_SDL_PollEvent(event);
    while(__real_SDL_PollEvent(event)){
        rrpc::controls_event(event);   // Player 1's last-used device for the menu prompts (controls_screen.cpp)
        rrpc::mouse_event(*event);
        if(rrpc::online_event(*event))continue;
        if(rrpc::showroom_event(*event))continue;
        if(rrpc::rider_options().original_quit&&event->type==SDL_EVENT_KEY_DOWN&&!event->key.repeat&&event->key.key==SDLK_ESCAPE){event->type=SDL_EVENT_QUIT;return true;}
        if(open&&!available())close();
        if(!open&&rrpc::rider_options().menu&&!rrpc::netplay()&&jukebox_context()&&event->type==SDL_EVENT_KEY_DOWN&&!event->key.repeat&&event->key.key==SDLK_F7){rrpc::music_import();continue;}
#ifndef RR_PUBLIC_RELEASE   // the public release has no editors
        if(event->type==SDL_EVENT_KEY_DOWN&&event->key.key==SDLK_F2&&!event->key.repeat&&rrpc::rider_options().menu&&!rrpc::netplay()){rrpc::game_editor_toggle();continue;}   // in-game editor (game_editor.cpp)
#endif
        if(event->type==SDL_EVENT_KEY_DOWN&&event->key.key==SDLK_F6&&!event->key.repeat&&rrpc::rider_options().menu&&(main_menu()||paused())){if(open)close(main_menu());else start(!main_menu());continue;}
        if(!open)return true;
        if(event->type==SDL_EVENT_KEY_DOWN){
            const auto key=event->key.key;
            const bool handled=editing||key==SDLK_ESCAPE||key==SDLK_RETURN||key==SDLK_KP_ENTER||key==SDLK_UP||key==SDLK_DOWN||key==SDLK_LEFT||key==SDLK_RIGHT||key==SDLK_BACKSPACE;
            if(handled)last_keyboard=SDL_GetTicks();
            if(editing&&key==SDLK_A&&(event->key.mod&SDL_KMOD_CTRL)){std::memset(draft,' ',8);cursor=0;}
            else if(key==SDLK_ESCAPE)cancel();else if(key==SDLK_RETURN||key==SDLK_KP_ENTER)accept();
            else if(key==SDLK_UP)move(0,-1);else if(key==SDLK_DOWN)move(0,1);else if(key==SDLK_LEFT)move(-1,0);else if(key==SDLK_RIGHT)move(1,0);else if(key==SDLK_BACKSPACE)backspace();
            else if(editing&&!(event->key.mod&(SDL_KMOD_CTRL|SDL_KMOD_ALT|SDL_KMOD_GUI))&&key>=32&&key<127){
                char letter=(char)key;bool upper=((event->key.mod&SDL_KMOD_SHIFT)!=0)^((event->key.mod&SDL_KMOD_CAPS)!=0);
                if(letter>='a'&&letter<='z'&&upper)letter=(char)(letter-'a'+'A');
                if((event->key.mod&SDL_KMOD_SHIFT)&&letter=='-')letter='_';
                if(valid(letter)){char text[2]={letter,0};type(text);pending_text_key=letter;pending_text_tick=SDL_GetTicks();}
            }
            continue;
        }
        if(event->type==SDL_EVENT_TEXT_INPUT){const char*t=event->text.text;char a=pending_text_key,b=t[0];if(a>='A'&&a<='Z')a=(char)(a-'A'+'a');if(b>='A'&&b<='Z')b=(char)(b-'A'+'a');if(a&&SDL_GetTicks()-pending_text_tick<150&&(a==b||(a=='_'&&b=='-')))t++;pending_text_key=0;type(t);continue;}
        if(event->type==SDL_EVENT_KEY_UP)continue;
        if(event->type==SDL_EVENT_WINDOW_FOCUS_GAINED&&editing){SDL_StartTextInput(SDL_GetWindowFromID(event->window.windowID));return true;}
        if(event->type==SDL_EVENT_WINDOW_FOCUS_LOST){last_buttons=0xffff;release_guard=true;pending_text_key=0;return true;}
        return true;
    }return false;
}
extern "C" void __wrap_sio_set_pad_state_slot(int slot,uint16_t buttons){
    // The PC menu is pad 1's. Pad 2 reaches the game except while the menu is open
    // or steering the original menu: the original main menu under it follows pad 2
    // too, and pad 2's Cross entered Solo behind the open PC menu.
    // Online the framework publishes both seats' pads here: they reach the game
    // unchanged (no DualShock ANALOG emulation, intro skip or menu masks, which
    // follow this PC's devices and clock).
    if(rrpc::netplay()){__real_sio_set_pad_state_slot(slot,buttons);return;}
    buttons=rrpc::controls_pad(slot,buttons);   // controller seats: DualShock + ANALOG button (controls_screen.cpp)
    if(rrpc::online_open()){__real_sio_set_pad_state_slot(slot,slot==0?rrpc::online_pad(buttons):0xffff);return;}   // the Online screens have the pads
    if(slot==0)buttons=rrpc::mouse_pad(buttons);   // mouse hover / clicks as Player 1's presses (mouse_menus.cpp)
    if(slot==0)buttons=rrpc::intro_skip_filter(buttons);
    {uint16_t out;if(rrpc::showroom_pad(slot,buttons,out)){if(slot==0)last_buttons=buttons;__real_sio_set_pad_state_slot(slot,out);return;}}
    if(slot==1&&rrpc::rider_options().menu&&(open||SDL_GetTicks()<bypass_until)){__real_sio_set_pad_state_slot(slot,0xffff);return;}
    if(slot==1){
        // Pad 2 moves the root menu too: its Cross / Start leaves on the game's own tables.
        const uint16_t pressed2=(uint16_t)(last_buttons2&~buttons);last_buttons2=buttons;
        if(main_rows&&(pressed2&((1<<14)|(1<<3)))){main_hold=true;main_drop();}
    }
    if(slot!=0||!rrpc::rider_options().menu){__real_sio_set_pad_state_slot(slot,buttons);return;}
    if(open&&!available())close();
    uint16_t pressed=(uint16_t)(last_buttons&~buttons);last_buttons=buttons;
    if(!open&&jukebox_context()){
        if(pressed&1){rrpc::music_change(0,1);rrpc::music_preview(rrpc::music_is_custom());buttons|=1;}
        if(rrpc::music_is_custom()){
            int total=rrpc::music_track_count();
            if(total&&(pressed&((1<<4)|(1<<6)))){int index=jukebox_page*5+((word(0x8009C5D4)>>16)&0xffff);index=(index+((pressed&(1<<6))?1:total-1))%total;jukebox_page=index/5;psx_mod_write_half(0x8009C5D6,index%5);}
            if(pressed&((1<<7)|(1<<5))){int pages=(rrpc::music_track_count()+4)/5;if(pages)jukebox_page=(jukebox_page+((pressed&(1<<5))?1:pages-1))%pages;}
            if(pressed&(1<<15)){unsigned index=(word(0x8009C5D4)>>16)&0xffff;if(index<5){rrpc::music_select(jukebox_page*5+index);rrpc::music_preview(true);}}
            buttons|=(1<<4)|(1<<6)|(1<<7)|(1<<5)|(1<<15);
        }
    }
    if(SDL_GetTicks()<bypass_until){if(!main_context()){bypass_until=0;release_guard=true;__real_sio_set_pad_state_slot(slot,0xffff);}else __real_sio_set_pad_state_slot(slot,0xbfff);return;}
    main_tick();
    if(!open&&main_rows&&main_menu()&&!release_guard){
        // The Online and Quit rows: Down from Options lights Online, Down again
        // Quit, Up from Solo lights Quit (the game's cursor stays on Options);
        // Cross / Start opens Online, or asks about Quit.
        const uint16_t up=1<<4,down=1<<6,act=(1<<14)|(1<<3),sel=main_selection()>=5?4:main_selection();uint16_t take=0;
        if(online_row){
            if(pressed&up){online_row=false;take=up;}
            else if(pressed&down){online_row=false;quit_row=true;take=down;}
            else if(pressed&act){main_mask|=act;rrpc::online_begin();__real_sio_set_pad_state_slot(slot,0xffff);return;}
        }else if(quit_row){
            if(pressed&up){quit_row=false;online_row=true;take=up;}
            else if(pressed&down){quit_row=false;psx_mod_write_half(0x800990B8,2);take=down;}
            else if(pressed&act){main_mask|=act;confirm_quit();__real_sio_set_pad_state_slot(slot,0xffff);return;}
        }else if(sel==4&&(pressed&down)){online_row=true;take=down;}
        else if(sel==2&&(pressed&up)){quit_row=true;psx_mod_write_half(0x800990B8,4);take=up;}
        else if((pressed&act)&&!(sel==4&&(pressed&(1<<14)))){main_hold=true;main_drop();}   // Solo / Multiplayer: the game acts on its own tables
        // A Select pulse for a step the game does not see keeps its idle (attract) timer honest.
        if(take){main_mask|=take;main_records();buttons&=(uint16_t)~1u;psx_mod_counter_add("rr.menu.quit_row_steps",1);}
    }
    main_mask&=(uint16_t)~buttons;   // a taken button stays hidden from the game until it is released
    buttons|=main_mask;
    if(!open&&((main_menu()&&(pressed&1))||(main_menu()&&options_selected&&!quit_row&&!online_row&&(pressed&(1<<14)))||(paused()&&(pressed&1))||(paused()&&row4_ready()&&pause_cursor()==3&&(pressed&(1<<14))))){start(!main_menu());__real_sio_set_pad_state_slot(slot,0xffff);return;}
    if(release_guard){__real_sio_set_pad_state_slot(slot,0xffff);if(buttons==0xffff)release_guard=false;return;}
    if(open){
        // Mapped keyboard buttons are also present here; SDL handles them once.
        if(SDL_GetTicks()-last_keyboard>80){if(pressed&(1<<4))move(0,-1);if(pressed&(1<<6))move(0,1);if(pressed&(1<<7))move(-1,0);if(pressed&(1<<5))move(1,0);if(pressed&((1<<14)|(1<<3)))accept();if(pressed&(1<<13))cancel();if(pressed&(1<<15))backspace();}
        sync_selection();
        // The original frontend enters its attract demo when its pad is idle.
        // Select has no root-menu action; a brief pulse keeps the original
        // idle logic aware of PC menu activity without navigating stock rows.
        // Never send it in a race: Select controls the original camera there.
        const uint16_t idle=page_kind!=2&&(SDL_GetTicks()%1000)<100?0xfffe:0xffff;
        __real_sio_set_pad_state_slot(slot,idle);return;
    }
    __real_sio_set_pad_state_slot(slot,buttons);
}

// The rows the mouse can point at on our menus: the PC pages (rows from
// first_row, every 20), the main menu's four rows with Quit, and the pause menu
// (each row's label y as last drawn, across the centred panel). false: the
// game's own menu record decides (mouse_menus.cpp).
bool rrpc::menu_mouse_rows(MouseRows&m){
    if(rrpc::netplay())return false;
    const uint64_t now=SDL_GetTicks();
    auto pause_rows=[&](int n){m.centred=true;m.count=0;for(int i=0;i<n&&i<4;i++)if(now-pause_row_at[i]<500)m.row[m.count++]={-80,pause_row_y[i]-3,160,15,i};};
    if(open){
        if(page_kind==2){pause_rows(row4_ready()?4:3);m.current=row;return true;}
        const int n=page_count();m.count=0;for(int i=0;i<n&&i<16;i++)m.row[m.count++]={20,first_row-4+20*i,164,18,i};m.current=row;return true;
    }
    if(paused()){pause_rows(row4_ready()?4:3);m.current=pause_cursor();return true;}
    if(main_rows&&main_menu()){m.count=0;for(int i=0;i<5;i++)m.row[m.count++]={20,first_row-4+20*i,164,18,i+2};m.current=online_row?5:quit_row?6:main_selection();return true;}
    return false;
}

// Pause drawer (see pause_drawer). Linked with --wrap=func_8002D718.
extern "C" void __real_func_8002D718(CPUState*);
extern "C" void __wrap_func_8002D718(CPUState*c){if(!pause_drawer(c))__real_func_8002D718(c);}
