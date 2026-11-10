/* Online from the game's main menu. LGPL-2.1-or-later. Independently authored;
 * the screens are drawn here (Roboto Flex, hd_font_data.h), no artwork is stored.
 *
 * The launcher that started the game does the online work on its own Online
 * pages (lobby_page.h); online_link.h is the shared memory between the two.
 * Here the page is shown full screen in the game's style (steel-blue bars, the
 * lit row in the game's orange, gold lettering) and the player's input goes
 * back as commands: keys and the controller as the launcher's keys, typed
 * letters for the room chat, the mouse as a row pointed at and Enter. The
 * launcher's questions (a room code, yes / no) are asked here. When a race
 * starts the launcher says "closing": this screen says so and the game closes;
 * the launcher starts the online game, and this game again afterwards.
 * Started without the launcher (no link), the screen says how to play online. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "online_screen.h"
#include "online_link.h"
#include "mouse_menus.h"
#include "mod_plugins.h"
#include "hd_font_data.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace {
enum State{CLOSED,WAITING,LIVE,NO_LINK,REFUSED,STARTING};
State state=CLOSED;uint64_t state_at,last_key;
HANDLE link_map;OnlineLink*link;OnlineLink snap;
bool seen_open,quit_sent;
// A question from the launcher: the answer is typed here.
bool asking,answered;std::string answer;bool choice_yes;
int hover=-1,first_row;   // the row under the pointer, the first row shown
uint16_t last_pad=0xffff;

bool attach(){
    if(link)return true;wchar_t name[64];online_link_name(name,64,GetCurrentProcessId());
    link_map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!link_map)return false;
    link=(OnlineLink*)MapViewOfFile(link_map,FILE_MAP_ALL_ACCESS,0,0,sizeof(OnlineLink));if(!link){CloseHandle(link_map);link_map=nullptr;}
    return link!=nullptr;
}
// The page as the launcher last wrote it (it writes under an odd count).
bool read_page(){
    for(int k=0;k<4;k++){const LONG s=link->seq;if(s&1)continue;MemoryBarrier();std::memcpy(&snap,(const void*)link,offsetof(OnlineLink,cmd_head));MemoryBarrier();if(link->seq==s)return true;}
    return false;
}
void send(int kind,int value=0,const char*text=""){
    if(!link)return;const LONG head=link->cmd_head;if(head-link->cmd_tail>=OL_CMDS)return;
    OnlineCmd&c=link->cmd[head%OL_CMDS];c.kind=kind;c.value=value;int i=0;for(;text[i]&&i<99;i++)c.text[i]=(unsigned char)text[i];c.text[i]=0;
    MemoryBarrier();link->cmd_head=head+1;
}
void key(int vk){send(OL_KEY,vk);}
SDL_Window*window(){SDL_Window*w=SDL_GetKeyboardFocus();if(w)return w;int n=0;SDL_Window**all=SDL_GetWindows(&n);w=n?all[0]:nullptr;SDL_free(all);return w;}
void set_state(State s){state=s;state_at=SDL_GetTicks();}
void close_screen(){if(state==LIVE||state==WAITING)send(OL_CLOSE);set_state(CLOSED);asking=false;hover=-1;SDL_StopTextInput(window());psx_mod_counter_add("rr.online.closed",1);}
// The launcher's text, for the HD font (ASCII): dashes and quotes kept, the rest '?'.
std::string ascii(const wchar_t*w){
    std::string s;for(;*w;w++){wchar_t c=*w;if(c==L'\n'||(c>=32&&c<127))s+=(char)c;else if(c==0x2014||c==0x2013)s+='-';else if(c==0x2018||c==0x2019)s+='\'';else if(c==0x201C||c==0x201D)s+='"';else if(c==0x2026)s+="...";else if(c>=0xA0)s+='?';}
    return s;
}

// ---- input: keys, the controller and the mouse end here ----
enum{K_UP=VK_UP,K_DOWN=VK_DOWN,K_LEFT=VK_LEFT,K_RIGHT=VK_RIGHT,K_OK=VK_RETURN,K_BACK=VK_ESCAPE};
void give_answer(bool ok){send(OL_ANSWER,ok,answer.c_str());asking=false;answered=true;}
void input(int vk){
    if(state==NO_LINK||state==REFUSED){if(vk==K_OK||vk==K_BACK)close_screen();return;}
    if(state!=LIVE)return;
    if(asking){
        if(snap.ask_yesno){if(vk==K_LEFT||vk==K_RIGHT||vk==K_UP||vk==K_DOWN)choice_yes=!choice_yes;else if(vk==K_OK)give_answer(choice_yes);else if(vk==K_BACK)give_answer(false);}
        else if(vk==K_OK)give_answer(true);else if(vk==K_BACK)give_answer(false);else if(vk==VK_BACK&&!answer.empty())answer.pop_back();
        return;}
    key(vk);
}

// ---- drawing (1280x720, shown letterboxed 16:9) ----
constexpr int W=1280,H=720;
struct Image{int w=0,h=0;std::vector<uint32_t>px;};
enum{REGULAR=0,BOLD=1,BLACK=2};
double cap_units(int style){static double cache[RR_HD_STYLES];if(cache[style])return cache[style];
    const int16_t*d=rr_hd_data+rr_hd_index[style]['H'-RR_HD_FIRST];int nc=*d++,top=0;for(int c=0;c<nc;c++){int np=*d++;for(int i=0;i<np;i++,d+=2)top=std::min<int>(top,d[1]);}
    return cache[style]=top<0?-top:700;}
double text_width(int style,const char*s,double cap){double sc=cap/cap_units(style),w=0;for(;*s;s++){int c=(unsigned char)*s;if(c>=32&&c<=RR_HD_LAST)w+=rr_hd_advance[style][c-32]*sc;}return w;}
uint32_t mix(uint32_t d,uint32_t s,unsigned a){if(a>=255)return 0xff000000u|(s&0xffffff);uint32_t o=0xff000000u;for(int k=0;k<24;k+=8){unsigned x=(d>>k&255)*(255-a)+(s>>k&255)*a;o|=((x+127)/255)<<k;}return o;}
// One line, cap top at y, filled with even-odd spans at four sub-rows per pixel (as showroom.cpp).
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
void shadowed(Image&im,int style,const char*s,double x,double top,double cap,uint32_t rgb){text(im,style,s,x+cap*.06,top+cap*.06,cap,0xff000000u);text(im,style,s,x,top,cap,rgb);}
// A line that must fit: shortened with "..." (from the left: keep_end, for the chat draft).
std::string fit(int style,std::string s,double cap,double w,bool keep_end=false){
    if(text_width(style,s.c_str(),cap)<=w)return s;
    while(!s.empty()&&text_width(style,(keep_end?"..."+s:s+"...").c_str(),cap)>w){if(keep_end)s.erase(0,1);else s.pop_back();}
    return keep_end?"..."+s:s+"...";
}
void fill(Image&im,int x0,int y0,int x1,int y1,uint32_t top,uint32_t bottom,unsigned alpha=255){
    x0=std::max(0,x0);y0=std::max(0,y0);x1=std::min(im.w,x1);y1=std::min(im.h,y1);
    for(int y=y0;y<y1;y++){unsigned t=(unsigned)((y-y0)*255/std::max(1,y1-y0-1));uint32_t c=mix(top,bottom,t);for(int x=x0;x<x1;x++){uint32_t&p=im.px[(size_t)y*im.w+x];p=mix(p,c,alpha);}}
}
// Words wrapped into lines of width w; returns the y under the last line.
double paragraph(Image&im,int style,const std::string&s,double x,double y,double w,double cap,double line,uint32_t rgb,int max_lines=99){
    std::string cur;int lines=0;size_t i=0;
    auto flush=[&]{if(lines<max_lines)text(im,style,cur.c_str(),x,y,cap,rgb);y+=line;lines++;cur.clear();};
    while(i<=s.size()){size_t j=s.find_first_of(" \n",i);if(j==std::string::npos)j=s.size();std::string word=s.substr(i,j-i);
        std::string trial=cur.empty()?word:cur+" "+word;
        if(!cur.empty()&&text_width(style,trial.c_str(),cap)>w){flush();cur=word;}else cur=trial;
        if(j<s.size()&&s[j]=='\n')flush();i=j+1;}
    if(!cur.empty())flush();return y;
}
// The backdrop: night blue with the game's diagonal speed lines and a faint ONLINE.
const Image&backdrop(){
    static Image bg;if(!bg.px.empty())return bg;bg.w=W;bg.h=H;bg.px.assign((size_t)W*H,0xff000000u);fill(bg,0,0,W,H,0xff0e1d4cu,0xff03071au);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){int d=(x+2*y)%96;if(d<3){uint32_t&p=bg.px[(size_t)y*W+x];p=mix(p,0xff2a4290u,40);}}
    fill(bg,0,0,W,4,0xffe0a020u,0xff9a6010u);
    fill(bg,0,H-6,W,H,0xffe0a020u,0xff9a6010u);return bg;
}
// Rows: the list on the left, as the game's bars.
constexpr int LIST_X=64,LIST_Y=184,LIST_W=620,ROW_H=52,ROW_STEP=60,VISIBLE=7;
constexpr int CARD_X=724,CARD_Y=184,CARD_W=492,CARD_H=420;
void bar(Image&im,int y,bool lit,int kind,const std::string&label,const std::string&value){
    const int x0=LIST_X,x1=LIST_X+LIST_W;
    if(lit){fill(im,x0,y,x1,y+ROW_H,0xffe07a24u,0xff8a3a10u);fill(im,x0,y,x0+6,y+ROW_H,0xffffd060u,0xffe0a020u);fill(im,x0,y,x1,y+2,0xffffb060u,0xffffb060u);}
    else{fill(im,x0,y,x1,y+ROW_H,0xff1c2c66u,0xff0f1a44u);fill(im,x0,y,x1,y+2,0xff3a50a0u,0xff3a50a0u);}
    const bool info=kind==3;   // ROW_INFO
    const uint32_t lc=lit?0xffffffffu:info?0xff8a96b8u:0xffe8c070u,vc=lit?0xffffe0b0u:0xffb8c4e0u;
    std::string v=value;if(lit&&kind==2&&!v.empty())v="<  "+v+"  >";   // ROW_VALUE: Left / Right change it
    // The room's race vote: a gold VOTE tag before the course.
    const bool vote=label.rfind("Vote: ",0)==0;int lx=x0+24;
    if(vote){fill(im,x0+20,y+14,x0+76,y+38,0xffffd060u,0xffe0a020u);text(im,BLACK,"VOTE",x0+28,y+20,12,0xff101c44u);lx=x0+90;}
    const double vw=std::min(300.0,text_width(REGULAR,v.c_str(),15));
    v=fit(REGULAR,v,15,300);const std::string l=fit(lit?BOLD:REGULAR,vote?label.substr(6):label,18,x1-22-vw-16-lx);
    (lit?shadowed:text)(im,lit?BOLD:REGULAR,l.c_str(),lx,y+17,18,lc);
    text(im,REGULAR,v.c_str(),x1-22-text_width(REGULAR,v.c_str(),15),y+19,15,vc);
}
void card(Image&im,int x,int y,int w,int h){fill(im,x,y,x+w,y+h,0xff0a1434u,0xff050a1cu);fill(im,x,y,x+w,y+3,0xffe0a020u,0xffe0a020u);}
void header(Image&im,const char*kicker,const char*title){
    text(im,REGULAR,kicker,66,46,13,0xffe0a020u);shadowed(im,BLACK,title,64,68,46,0xffffffffu);
    const int tw=(int)text_width(BLACK,title,46);fill(im,64,128,64+std::min(tw,560),132,0xffffd060u,0xffe0a020u);
}
void hints(Image&im,const char*s){text(im,REGULAR,s,64,H-44,15,0xffe0a020u);}
void message_screen(Image&im,const char*title,const std::string&body,const char*hint){
    header(im,"ROAD RASH: JAILBREAK",title);card(im,64,184,760,300);paragraph(im,REGULAR,body,96,216,700,20,34,0xffe8ecf6u);hints(im,hint);
}

std::array<Image,3> pictures;std::atomic<int> front{-1};int drawn=-1;uint64_t drawn_key=~0ull;
uint64_t hash(const void*p,size_t n,uint64_t h){const uint8_t*b=(const uint8_t*)p;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
void draw(){
    const uint64_t now=SDL_GetTicks();const int blink=(int)(now/500%2),dots=(int)(now/400%4);
    uint64_t k=hash(&state,sizeof state,14695981039346656037ull);k=hash(&blink,sizeof blink,k);k=hash(&hover,sizeof hover,k);k=hash(&choice_yes,1,k);k=hash(&asking,1,k);k=hash(answer.data(),answer.size(),k);
    if(state==LIVE)k=hash(&snap,offsetof(OnlineLink,cmd_head),k);if(state==STARTING||state==WAITING)k=hash(&dots,sizeof dots,k);
    if(k==drawn_key&&front.load()>=0)return;drawn_key=k;
    const int slot=(drawn+1)%3;Image&im=pictures[slot];im=backdrop();
    if(state==NO_LINK)message_screen(im,"ONLINE","Online races run through the launcher. Start Road Rash: Jailbreak from the launcher (not straight from the game's folder), then pick Online on the main menu.\nThe launcher signs you in, finds rooms and starts the race for everyone.","Enter / Esc: back to the main menu");
    else if(state==REFUSED)message_screen(im,"ONLINE",ascii(snap.refused),"Enter / Esc: back to the main menu");
    else if(state==WAITING){std::string t="Connecting to the launcher";t.append(dots,'.');message_screen(im,"ONLINE",t,"Esc: back");}
    else if(state==STARTING){
        const char*t="STARTING ONLINE RACE";const double tw=text_width(BLACK,t,56);shadowed(im,BLACK,t,(W-tw)/2,270,56,0xffffffffu);
        fill(im,(int)(W-tw)/2,346,(int)(W+tw)/2,350,0xffffd060u,0xffe0a020u);
        std::string s="The game restarts in online mode. Hold tight";s.append(dots,'.');const double sw=text_width(REGULAR,"The game restarts in online mode. Hold tight...",20);text(im,REGULAR,s.c_str(),(W-sw)/2,372,20,0xffd8e0f0u);
    }else{
        std::string title=ascii(snap.title);header(im,snap.page==29?"ONLINE  /  ROOM":snap.page==38?"ONLINE  /  PUBLIC ROOMS":snap.page==39?"ONLINE  /  BIKES":"ROAD RASH: JAILBREAK  /  ONLINE",title.empty()?"ONLINE":title.c_str());
        // The stats strip, top right: players online, your rating and rank, who you are.
        {char count[16],rating[16],change[24];const int r=snap.mmr>0?snap.mmr:1000;
            if(snap.online_count>=0)std::snprintf(count,sizeof count,"%d",snap.online_count);else std::strcpy(count,"-");
            std::snprintf(rating,sizeof rating,"%d",r);if(snap.mmr_races)std::snprintf(change,sizeof change,"%+d",snap.mmr_last);else change[0]=0;
            const char*rank=r<1500?"ROOKIE":r<2500?"STREET RIDER":r<4000?"ROAD WARRIOR":r<6000?"PRO RASHER":r<8000?"CHAMPION":"LEGEND";
            const std::string who=snap.signed_in&&snap.account[0]?fit(BOLD,ascii(snap.account),17,170):"Guest";
            auto chip=[&](int x,int w,const char*label,const char*big,uint32_t big_rgb,const char*small,uint32_t small_rgb){
                fill(im,x,40,x+w,124,0xff142458u,0xff0a1434u);fill(im,x,40,x+w,43,0xffe0a020u,0xffe0a020u);
                text(im,BOLD,label,x+14,52,11,0xffe0a020u);shadowed(im,BLACK,big,x+14,70,24,big_rgb);text(im,REGULAR,fit(REGULAR,small,12,w-24).c_str(),x+14,103,12,small_rgb);};
            chip(W-64-176,176,"SIGNED IN AS",who.c_str(),0xffffffffu,snap.signed_in?"Discord":"Guest: room codes only",0xffa8b4d0u);
            chip(W-64-176-10-176,176,"YOUR RATING",rating,0xffffe0b0u,(std::string(rank)+"  "+change).c_str(),!snap.mmr_races?0xffa8b4d0u:snap.mmr_last>=0?0xff7ee08au:0xffff8a7au);
            chip(W-64-176-10-176-10-124,124,"ONLINE NOW",count,0xffffffffu,snap.online_count==1?"player":snap.online_count>=0?"players":snap.signed_in?"checking...":"sign in first",0xffa8b4d0u);}
        // Public rooms with none open.
        if(snap.page==38&&snap.rows<=1){card(im,LIST_X,LIST_Y+ROW_STEP+8,LIST_X+LIST_W-LIST_X,150);shadowed(im,BLACK,"NO OPEN ROOMS",LIST_X+28,LIST_Y+ROW_STEP+36,26,0xffffffffu);
            paragraph(im,REGULAR,"Nobody is waiting in a public room right now. Host one from Play online with \"List my room publicly\" on, and riders can find you here.",LIST_X+28,LIST_Y+ROW_STEP+78,LIST_W-56,15,24,0xffd8e0f0u,3);}
        // A notice from the launcher, under the rows.
        if(snap.notice[0]){std::string n=fit(BOLD,ascii(snap.notice),15,LIST_W-60);const double nw=text_width(BOLD,n.c_str(),15);const int x0=LIST_X,x1=x0+(int)nw+44;
            fill(im,x0,620,x1,654,0xffb0301au,0xff6a1408u);fill(im,x0,620,x0+4,654,0xffffd060u,0xffe0a020u);text(im,BOLD,n.c_str(),x0+22,630,15,0xffffffffu);}
        // The rows, scrolled to keep the lit one in view.
        const int n=snap.rows;if(snap.selected<first_row)first_row=snap.selected;if(snap.selected>=first_row+VISIBLE)first_row=snap.selected-VISIBLE+1;first_row=std::max(0,std::min(first_row,std::max(0,n-VISIBLE)));
        for(int i=first_row;i<n&&i<first_row+VISIBLE;i++)bar(im,LIST_Y+(i-first_row)*ROW_STEP,i==snap.selected,snap.kind[i],ascii(snap.label[i]),ascii(snap.value[i]));
        if(first_row>0)text(im,REGULAR,"more above",LIST_X+LIST_W-90,LIST_Y-24,13,0xff8a96b8u);
        if(first_row+VISIBLE<n)text(im,REGULAR,"more below",LIST_X+LIST_W-90,LIST_Y+VISIBLE*ROW_STEP-2,13,0xff8a96b8u);
        // The card: the room chat on the room page, otherwise the lit row's help.
        card(im,CARD_X,CARD_Y,CARD_W,CARD_H);const int ax=CARD_X+28,aw=CARD_W-56;
        if(snap.page==29){
            text(im,BLACK,"ROOM CHAT",ax,CARD_Y+24,22,0xffe0a020u);
            text(im,REGULAR,snap.chat_on?"Just start typing to talk to everyone.":"You can talk here once you are in the room.",ax,CARD_Y+60,14,0xffb8c4e0u);
            const int top=CARD_Y+88,box=CARD_Y+CARD_H-150,line=28;fill(im,ax,top,ax+aw,box-10,0xff030716u,0xff030716u,200);
            if(!snap.chats)text(im,REGULAR,"No messages yet. Say hello!",ax+(aw-text_width(REGULAR,"No messages yet. Say hello!",15))/2,(top+box)/2-14,15,0xff6a76a0u);
            const int fits=(box-10-top-8)/line,from=std::max(0,snap.chats-fits);int y=top+8;
            for(int i=from;i<snap.chats;i++,y+=line){
                if(!snap.chat_from[i][0]){text(im,REGULAR,fit(REGULAR,ascii(snap.chat_text[i]),14,aw-24).c_str(),ax+12,y+6,14,0xff8a96b8u);continue;}
                const std::string who=fit(BOLD,ascii(snap.chat_from[i]),15,160)+":";const double ww=text_width(BOLD,who.c_str(),15)+8;
                text(im,BOLD,who.c_str(),ax+12,y+5,15,0xffe8c070u);text(im,REGULAR,fit(REGULAR,ascii(snap.chat_text[i]),15,aw-24-ww).c_str(),ax+12+ww,y+5,15,0xffffffffu);}
            // The type box, with a blinking caret.
            const int by=box;fill(im,ax,by,ax+aw,by+44,0xff02050fu,0xff02050fu);fill(im,ax,by+42,ax+aw,by+44,snap.chat_on?0xffe0a020u:0xff3a4870u,snap.chat_on?0xffe0a020u:0xff3a4870u);
            if(snap.draft[0])text(im,REGULAR,(fit(REGULAR,ascii(snap.draft),16,aw-36,true)+(blink?"_":"")).c_str(),ax+14,by+14,16,0xffffffffu);
            else text(im,REGULAR,snap.chat_on?"Type a message, then press Enter":"",ax+14,by+14,16,0xff6a76a0u);
            paragraph(im,REGULAR,ascii(snap.help),ax,by+62,aw,13,20,0xffa8b4d0u,4);
        }else if(snap.page==39){
            // Bikes: the rank's four bikes as tiles, the chosen one lit, and its description.
            static const char*ranks[3]={"GRUNT","LIEUTENANT","CAPTAIN"};const int r=snap.tile_rank>=0&&snap.tile_rank<3?snap.tile_rank:0;
            text(im,BOLD,"RANK",ax,CARD_Y+24,13,0xffe0a020u);shadowed(im,BLACK,ranks[r],ax,CARD_Y+42,30,0xffffd060u);
            const int tw=(aw-12)/2,th=58,ty=CARD_Y+92;
            for(int k=0;k<4;k++){const int x=ax+(k%2)*(tw+12),y=ty+(k/2)*(th+10);const bool on=k==snap.tile_sel;
                fill(im,x,y,x+tw,y+th,on?0xffe07a24u:0xff1c2c66u,on?0xff8a3a10u:0xff0f1a44u);fill(im,x,y,x+tw,y+2,on?0xffffd060u:0xff3a50a0u,on?0xffffd060u:0xff3a50a0u);
                text(im,on?BOLD:REGULAR,fit(on?BOLD:REGULAR,ascii(snap.tile[k]),16,tw-24).c_str(),x+12,y+(th-16)/2,16,on?0xffffffffu:0xffe8c070u);
                if(on)text(im,BOLD,"YOURS",x+tw-12-text_width(BOLD,"YOURS",10),y+6,10,0xffffe0b0u);}
            const int dy=ty+2*(th+10)+10;
            if(snap.tile_sel<0){fill(im,ax,dy,ax+aw,dy+34,0xff2a1c08u,0xff1a1206u);text(im,BOLD,"RANDOM: one of these four each race",ax+12,dy+10,14,0xffffd060u);}
            paragraph(im,REGULAR,ascii(snap.help),ax,dy+(snap.tile_sel<0?48:4),aw,15,24,0xffd8e0f0u,6);
        }else{
            text(im,BOLD,"ABOUT THIS",ax,CARD_Y+26,14,0xffe0a020u);
            const std::string label=n>0&&snap.selected<n?ascii(snap.label[snap.selected]):"";shadowed(im,BLACK,fit(BLACK,label,26,aw).c_str(),ax,CARD_Y+50,26,0xffffffffu);
            paragraph(im,REGULAR,ascii(snap.help),ax,CARD_Y+100,aw,16,27,0xffd8e0f0u,11);
        }
        hints(im,snap.page==29&&snap.chat_on?"Type to chat, Enter sends      Arrows / Wheel: move      Esc / Right-click: back":"Enter / Click: select      Arrows / Wheel: move      Left / Right: change      Esc / Right-click: back");
        // A question: the page dims behind a box.
        if(asking){
            for(auto&p:im.px)p=mix(p,0xff000000u,140);
            const int bx=300,by=228,bw=680,bh=264;card(im,bx,by,bw,bh);
            shadowed(im,BLACK,fit(BLACK,ascii(snap.ask_title),28,bw-64).c_str(),bx+32,by+28,28,0xffffffffu);
            paragraph(im,REGULAR,ascii(snap.ask_caption),bx+32,by+76,bw-64,16,26,0xffd8e0f0u,2);
            auto button=[&](int x,const char*s,bool lit){fill(im,x,by+bh-70,x+150,by+bh-30,lit?0xffe07a24u:0xff1c2c66u,lit?0xff8a3a10u:0xff0f1a44u);text(im,BOLD,s,x+(150-text_width(BOLD,s,16))/2,by+bh-58,16,lit?0xffffffffu:0xffe8c070u);};
            if(snap.ask_yesno){button(bx+bw-360,"Yes",choice_yes);button(bx+bw-190,"No",!choice_yes);}
            else{const int fy=by+134;fill(im,bx+32,fy,bx+bw-32,fy+46,0xff02050fu,0xff02050fu);fill(im,bx+32,fy+44,bx+bw-32,fy+46,0xffe0a020u,0xffe0a020u);
                text(im,REGULAR,(fit(REGULAR,answer,18,bw-100,true)+(blink?"_":"")).c_str(),bx+48,fy+14,18,0xffffffffu);
                button(bx+bw-360,"OK",true);button(bx+bw-190,"Cancel",false);}
        }
    }
    drawn=slot;front.store(slot);
}

// The picture's place in the window (16:9, letterboxed), in window pixels.
void place(int ww,int wh,int&x,int&y,int&w,int&h){w=ww;h=ww*9/16;if(h>wh){h=wh;w=wh*16/9;}x=(ww-w)/2;y=(wh-h)/2;}
// A mouse position (window coordinates) in picture pixels.
bool to_picture(SDL_WindowID id,float fx,float fy,int&px,int&py){
    SDL_Window*w=SDL_GetWindowFromID(id);int ww=0,wh=0,pw=0,ph=0;if(!w||!SDL_GetWindowSize(w,&ww,&wh)||!SDL_GetWindowSizeInPixels(w,&pw,&ph)||ww<1||wh<1)return false;
    int x,y,vw,vh;place(pw,ph,x,y,vw,vh);if(vw<1||vh<1)return false;
    px=(int)((fx*pw/ww-x)*W/vw);py=(int)((fy*ph/wh-y)*H/vh);return true;
}
int row_at(int px,int py){
    if(state!=LIVE||asking||px<LIST_X||px>=LIST_X+LIST_W||py<LIST_Y)return -1;
    const int i=(py-LIST_Y)/ROW_STEP;if((py-LIST_Y)%ROW_STEP>=ROW_H||i>=VISIBLE)return -1;const int r=first_row+i;return r<snap.rows?r:-1;
}
// The question box's buttons: 1 the first (Yes / OK), 2 the second (No / Cancel).
int button_at(int px,int py){if(!asking||py<228+264-70||py>=228+264-30)return 0;const int x=300+680;return px>=x-360&&px<x-210?1:px>=x-190&&px<x-40?2:0;}
}

void rrpc::online_begin(){
    if(state!=CLOSED)return;
    psx_mod_counter_add("rr.online.opened",1);
    seen_open=quit_sent=asking=answered=false;answer.clear();hover=-1;first_row=0;last_pad=0xffff;
    if(!attach()){set_state(NO_LINK);return;}
    read_page();send(OL_OPEN);set_state(WAITING);SDL_StartTextInput(window());
}
bool rrpc::online_open(){return state!=CLOSED;}

void rrpc::online_tick(){
    if(state==CLOSED)return;
    const uint64_t now=SDL_GetTicks();
    if(link&&(state==WAITING||state==LIVE||state==STARTING)&&read_page()){
        if(snap.closing&&state!=STARTING){set_state(STARTING);asking=false;psx_mod_counter_add("rr.online.race_starting",1);}
        else if(state==WAITING&&snap.refused[0])set_state(REFUSED);
        else if(state==WAITING&&snap.open){set_state(LIVE);seen_open=true;}
        else if(state==LIVE&&!snap.open&&seen_open){set_state(CLOSED);asking=false;SDL_StopTextInput(window());return;}   // backed out of Play online
        if(state==LIVE){
            if(!snap.ask)answered=false;
            if(snap.ask&&!asking&&!answered){asking=true;choice_yes=false;answer=ascii(snap.ask_value);}
            if(!snap.ask)asking=false;
        }
    }
    if(state==WAITING&&now-state_at>4000)set_state(NO_LINK);   // the launcher is not answering
    // The race starts: the game closes (the framework's own quit: memory cards
    // flushed) once the screen has said so; the launcher waits for that.
    if(state==STARTING&&!quit_sent&&now-state_at>1200){quit_sent=true;SDL_Event e{};e.type=SDL_EVENT_QUIT;SDL_PushEvent(&e);}
    rrpc::mouse_menu_shown(hover>=0);
    draw();
}

bool rrpc::online_event(const SDL_Event&e){
    if(state==CLOSED)return false;
    switch(e.type){
    case SDL_EVENT_KEY_DOWN:{
        last_key=SDL_GetTicks();const SDL_Keycode k=e.key.key;
        if(asking&&!snap.ask_yesno&&k==SDLK_V&&(e.key.mod&SDL_KMOD_CTRL)){if(char*c=SDL_GetClipboardText()){for(char*p=c;*p&&(int)answer.size()<std::max(1,snap.ask_max);p++)if(*p>=32&&*p<127)answer+=*p;SDL_free(c);}return true;}
        if(asking&&snap.ask_yesno&&(k==SDLK_Y||k==SDLK_N)){choice_yes=k==SDLK_Y;input(K_OK);return true;}
        const int vk=k==SDLK_UP?VK_UP:k==SDLK_DOWN?VK_DOWN:k==SDLK_LEFT?VK_LEFT:k==SDLK_RIGHT?VK_RIGHT:k==SDLK_RETURN||k==SDLK_KP_ENTER?VK_RETURN:k==SDLK_ESCAPE?VK_ESCAPE:
            k==SDLK_BACKSPACE?VK_BACK:k==SDLK_HOME?VK_HOME:k==SDLK_END?VK_END:k==SDLK_PAGEUP?VK_PRIOR:k==SDLK_PAGEDOWN?VK_NEXT:0;
        if(vk)input(vk);return true;}
    case SDL_EVENT_KEY_UP:return true;
    case SDL_EVENT_TEXT_INPUT:{
        if(state!=LIVE)return true;
        // UTF-8 to UTF-16 characters, for the chat (the launcher ignores them elsewhere).
        const unsigned char*s=(const unsigned char*)e.text.text;
        while(*s){unsigned c=*s++;int more=c>=0xF0?3:c>=0xE0?2:c>=0xC0?1:0;c&=more==3?7:more==2?15:more==1?31:127;while(more--&&(*s&0xC0)==0x80)c=c<<6|(*s++&63);
            if(asking){if(!snap.ask_yesno&&c>=32&&c<127&&(int)answer.size()<std::max(1,snap.ask_max))answer+=(char)c;}
            else if(c>=32&&c<0xFFFF)send(OL_CHAR,(int)c);}
        return true;}
    case SDL_EVENT_MOUSE_MOTION:{int px,py;if(to_picture(e.motion.windowID,e.motion.x,e.motion.y,px,py)){const int r=row_at(px,py);if(r!=hover){hover=r;if(r>=0&&r!=snap.selected)send(OL_SELECT,r);}}return true;}
    case SDL_EVENT_MOUSE_BUTTON_DOWN:{
        int px,py;if(!to_picture(e.button.windowID,e.button.x,e.button.y,px,py))return true;
        if(e.button.button==SDL_BUTTON_RIGHT){input(K_BACK);return true;}
        if(e.button.button!=SDL_BUTTON_LEFT)return true;
        if(state==NO_LINK||state==REFUSED){close_screen();return true;}
        if(asking){const int b=button_at(px,py);if(b){if(snap.ask_yesno){choice_yes=b==1;input(K_OK);}else input(b==1?K_OK:K_BACK);}return true;}
        const int r=row_at(px,py);if(r>=0){send(OL_SELECT,r);key(VK_RETURN);}
        return true;}
    case SDL_EVENT_MOUSE_WHEEL:if(e.wheel.y)input(e.wheel.y>0?K_UP:K_DOWN);return true;
    case SDL_EVENT_MOUSE_BUTTON_UP:return true;
    default:return false;
    }
}

uint16_t rrpc::online_pad(uint16_t buttons){
    // The controller drives the screen; mapped keyboard keys arrive as key
    // events too and are taken from those (a pad edge right after a key is the key's).
    const uint16_t pressed=(uint16_t)(last_pad&~buttons);last_pad=buttons;
    if(SDL_GetTicks()-last_key>120){
        if(pressed&(1<<4))input(K_UP);if(pressed&(1<<6))input(K_DOWN);if(pressed&(1<<7))input(K_LEFT);if(pressed&(1<<5))input(K_RIGHT);
        if(pressed&((1<<14)|(1<<3)))input(K_OK);if(pressed&(1<<12))input(K_BACK);}
    // The game's attract movie starts when its pad is idle: a brief Select pulse
    // (no action on the main menu) keeps it on the menu under this screen.
    return (SDL_GetTicks()%1000)<100?0xfffe:0xffff;
}

int rrpc::online_overlay(int ww,int wh,const uint32_t**pixels,int*w,int*h,int*x,int*y,int*dw,int*dh){
    if(state==CLOSED||ww<=0||wh<=0)return 0;const int f=front.load();if(f<0)return 0;
    const Image&im=pictures[f];*pixels=im.px.data();*w=im.w;*h=im.h;place(ww,wh,*x,*y,*dw,*dh);return 1;
}
