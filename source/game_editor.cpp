/* In-game editor (F2): a plain Windows window beside the game, on its own
 * thread, that edits game-edits.ini (next to launcher.ini). LGPL-2.1-or-later.
 * Original author code; no game code or data.
 *
 * Pages (the Page drop-down), one section of game-edits.ini each:
 *  "PC menu text" [pc menu text]: every label, label prefix ("Render" of
 *   "Render: 4K") and help line the PC menu has drawn this session. The menu
 *   asks menu_text() for each string as it draws.
 *  "Game text" [game text]: the game's own words as its text routine draws
 *   them (menus, race messages, results, rider and course names; menu titles
 *   through the HD banner, so only with HD UI on). The list fills from the
 *   player's own game at run time; nothing of the game is stored here. The
 *   replacement is drawn from a PC-owned copy for that one call
 *   (game_text_draw): the game's own string and table are never written.
 * A change shows on the next frame. Edits go into a draft; Save writes the
 * file (the old one is kept as .bak) and adds one dated line per change to
 * game-edits-changes.log; Discard goes back to the saved file. Off online. */
#include "game_editor.h"
#include "editor_page.h"
#include "netplay_session.h"
#include "cpu_state.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "mod_plugins.h"
extern "C" uint8_t* memory_get_ram_ptr(void);
namespace {
std::mutex mu;
struct Seen {std::string where, original;};
// One page's texts: what the game has drawn this session and the owner's text.
struct Book {
    const char *section, *name, *comment; size_t max;
    std::vector<Seen> seen;                       // in the order they were drawn
    std::map<std::string, size_t> seen_at;        // original -> index in seen
    std::map<std::string, std::string> saved, draft;   // original -> owner's text
};
Book books[2]={
    {"[pc menu text]","PC menu","; PC menu: built-in text = your text",31},
    {"[game text]","Game text","; The game's own words: built-in text = your text",63},
};
Book &pc=books[0], &game=books[1];
// Pages in their own source files (editor_page.h), after the text pages in
// the Page drop-down. Each keeps its own [section], state and lock.
using rrpc::editor::Page;
const std::vector<const Page*>& pages(){static const std::vector<const Page*> p={&rrpc::editor::race_hud_page(),&rrpc::editor::gameplay_page()};return p;}
bool loaded;
unsigned changes_serial;                      // bumps on every list or draft change
wchar_t file_path[32768], log_path[32768];
std::atomic<HWND> window{nullptr};
std::atomic<bool> running{false};

bool usable(const std::string& s,size_t max){if(s.empty()||s.size()>max)return false;for(unsigned char c:s)if(c<32||c>126)return false;return true;}
// Game text worth listing: has a letter (no bare numbers or times) and can be a
// line of the file (no leading comment or section mark, no " = ").
bool listable(const std::string& s){
    if(!usable(s,game.max)||s[0]==';'||s[0]=='#'||s[0]=='['||s.find(" = ")!=std::string::npos)return false;
    for(unsigned char c:s)if((c>='A'&&c<='Z')||(c>='a'&&c<='z'))return true;return false;
}
std::string trim(std::string s){while(!s.empty()&&(s.back()=='\n'||s.back()=='\r'||s.back()==' '||s.back()=='\t'))s.pop_back();size_t i=0;while(i<s.size()&&(s[i]==' '||s[i]=='\t'))i++;return s.substr(i);}
void paths(){
    wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);if(!n||n>=32768)return;auto*slash=std::wcsrchr(exe,L'\\');if(!slash)return;
    std::wcscpy(slash+1,L"..\\game-edits.ini");GetFullPathNameW(exe,32768,file_path,nullptr);
    std::wcscpy(slash+1,L"..\\game-edits-changes.log");GetFullPathNameW(exe,32768,log_path,nullptr);
}
// File: a [section] per page, then "original = your text" lines; ; comments.
void load_locked(){
    if(loaded)return;loaded=true;paths();
    FILE*f=file_path[0]?_wfopen(file_path,L"rb"):nullptr;char line[512];Book*in=nullptr;const Page*pin=nullptr;
    while(f&&std::fgets(line,sizeof line,f)){std::string s=trim(line);if(s.empty()||s[0]==';'||s[0]=='#')continue;
        if(s[0]=='['){in=nullptr;pin=nullptr;for(auto&b:books)if(s==b.section)in=&b;for(auto*p:pages())if(s==p->section)pin=p;continue;}
        if(pin){pin->load_line(s);continue;}if(!in)continue;
        size_t eq=s.find(" = ");if(eq==std::string::npos)continue;std::string a=trim(s.substr(0,eq)),b=trim(s.substr(eq+3));
        if(usable(a,in->max)&&usable(b,in->max)&&a!=b)in->saved[a]=b;}
    if(f)std::fclose(f);for(auto&b:books)b.draft=b.saved;for(auto*p:pages())p->loaded();
}
void discard_locked(){for(auto&b:books)b.draft=b.saved;changes_serial++;for(auto*p:pages())p->discard();}
unsigned pages_serial(){unsigned s=0;for(auto*p:pages())s+=p->serial();return s;}
int unsaved_locked(const Book&b){int n=0;for(auto&[k,v]:b.draft){auto it=b.saved.find(k);if(it==b.saved.end()||it->second!=v)n++;}for(auto&[k,v]:b.saved)if(!b.draft.count(k))n++;return n;}
int unsaved_locked(){int n=0;for(auto&b:books)n+=unsaved_locked(b);for(auto*p:pages())n+=p->unsaved();return n;}
bool save_locked(){
    if(!file_path[0])return false;
    std::wstring tmp=std::wstring(file_path)+L".tmp",bak=std::wstring(file_path)+L".bak";
    FILE*f=_wfopen(tmp.c_str(),L"wb");if(!f)return false;
    std::fputs("; Road Rash: Jailbreak - your in-game edits. Change them with F2 in the game.\r\n; Delete a line to get the built-in text back.\r\n",f);
    for(auto&b:books){std::fprintf(f,"\r\n%s\r\n%s\r\n",b.section,b.comment);
        for(auto&[k,v]:b.draft)if(k.find(" = ")==std::string::npos)std::fprintf(f,"%s = %s\r\n",k.c_str(),v.c_str());}
    for(auto*p:pages())p->write(f);
    bool ok=std::fclose(f)==0;if(!ok){DeleteFileW(tmp.c_str());return false;}
    CopyFileW(file_path,bak.c_str(),FALSE);if(!MoveFileExW(tmp.c_str(),file_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
    if(FILE*l=_wfopen(log_path,L"ab")){char stamp[32];std::time_t t=std::time(nullptr);std::tm tm{};localtime_s(&tm,&t);std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M",&tm);
        for(auto&b:books){
            auto where=[&](const std::string&k){auto it=b.seen_at.find(k);return it==b.seen_at.end()?std::string(b.name):std::string(b.name)+" > "+b.seen[it->second].where;};
            for(auto&[k,v]:b.draft){auto it=b.saved.find(k);std::string was=it==b.saved.end()?k:it->second;if(was!=v)std::fprintf(l,"%s  %s: \"%s\" -> \"%s\"\r\n",stamp,where(k).c_str(),was.c_str(),v.c_str());}
            for(auto&[k,v]:b.saved)if(!b.draft.count(k))std::fprintf(l,"%s  %s: \"%s\" -> \"%s\" (built-in)\r\n",stamp,where(k).c_str(),v.c_str(),k.c_str());}
        for(auto*p:pages())p->log(l,stamp);
        std::fclose(l);}
    for(auto&b:books)b.saved=b.draft;changes_serial++;for(auto*p:pages())p->commit();return true;
}
void see_locked(Book&b,const std::string&k,const char*where){
    if(!b.seen_at.count(k)&&b.seen.size()<2000){b.seen_at[k]=b.seen.size();b.seen.push_back({where?where:"",k});changes_serial++;}
}
// PC menu text (its built-in words or the owner's) is not the game's own.
bool pc_text_locked(const std::string&k){
    if(pc.seen_at.count(k))return true;for(auto&[o,v]:pc.draft)if(v==k)return true;
    return false;
}

// ---- Window -------------------------------------------------------------------
enum {ID_LIST=100,ID_EDIT,ID_APPLY,ID_RESET,ID_SAVE,ID_DISCARD,ID_LOG,ID_STATUS,ID_HINT,ID_PAGE};
HWND list,edit,status,hint,page_box;HFONT font;std::vector<std::string> rows;unsigned shown_serial=~0u;int page,shown_page=-1;
std::vector<HWND> page_wins;int extra=-1;unsigned shown_pserial=~0u;   // pages(): their windows, the one shown (-1: a text page)
const wchar_t* hints[2]={
    L"Pick a line, type your text, Apply (or Enter). The game shows it at once. Labels shrink to fit their bar; help lines show about 26 characters.",
    L"The game's own words as it draws them: menus, race messages, results, rider and course names. Visit a screen to list its text. Titles need HD UI on. Not in Five-O races.",
};
int screen_dpi(){HDC dc=GetDC(nullptr);int d=dc?GetDeviceCaps(dc,LOGPIXELSY):96;if(dc)ReleaseDC(nullptr,dc);return d>0?d:96;}
void open_log(){std::wstring cmd=L"notepad.exe \""+std::wstring(log_path)+L"\"";STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
    if(CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}}
std::wstring wide(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);std::wstring w(n?n-1:0,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,w.data(),n);return w;}
std::string narrow(const wchar_t*w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n?n-1:0,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);return s;}
void refresh(bool force){
    std::lock_guard<std::mutex> lock(mu);const unsigned ps=pages_serial();if(!force&&shown_serial==changes_serial&&shown_page==page&&shown_pserial==ps)return;
    shown_pserial=ps;
    if(extra>=0){int n=unsaved_locked();SetWindowTextW(status,n?(std::to_wstring(n)+L" unsaved change"+(n==1?L"":L"s")+L" (all pages) - Save keeps them.").c_str():L"All changes saved.");return;}
    bool same_page=shown_page==page;shown_serial=changes_serial;shown_page=page;Book&b=books[page];
    int sel=same_page?(int)SendMessageW(list,LB_GETCURSEL,0,0):-1,top=same_page?(int)SendMessageW(list,LB_GETTOPINDEX,0,0):0;
    SendMessageW(list,WM_SETREDRAW,FALSE,0);SendMessageW(list,LB_RESETCONTENT,0,0);rows.clear();
    for(auto&e:b.seen){auto it=b.draft.find(e.original);std::string line=e.where+"\t"+e.original+"\t"+(it==b.draft.end()?std::string("(built-in)"):it->second);
        SendMessageW(list,LB_ADDSTRING,0,(LPARAM)wide(line).c_str());rows.push_back(e.original);}
    if(sel>=0&&sel<(int)rows.size())SendMessageW(list,LB_SETCURSEL,sel,0);SendMessageW(list,LB_SETTOPINDEX,top,0);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
    int n=unsaved_locked();wchar_t s[240];
    if(rows.empty())std::swprintf(s,240,page?L"Nothing listed yet: visit the game's menus, a race and its results; their text appears here.":L"Nothing listed yet: open the PC menu in the game (Options, or F6 in a race's pause menu) and visit its pages.");
    else std::swprintf(s,240,L"%d text%ls listed. %ls",(int)rows.size(),rows.size()==1?L"":L"s",n?(std::to_wstring(n)+L" unsaved change"+(n==1?L"":L"s")+L" - Save keeps them.").c_str():L"All changes saved.");
    SetWindowTextW(status,s);
}
void select_row(){
    int sel=(int)SendMessageW(list,LB_GETCURSEL,0,0);std::string text;
    {std::lock_guard<std::mutex> lock(mu);if(sel<0||sel>=(int)rows.size()){SetWindowTextW(edit,L"");return;}auto&d=books[page].draft;auto it=d.find(rows[sel]);text=it==d.end()?rows[sel]:it->second;}
    SetWindowTextW(edit,wide(text).c_str());SendMessageW(edit,EM_SETSEL,0,-1);SetFocus(edit);
}
void apply(bool reset){
    int sel=(int)SendMessageW(list,LB_GETCURSEL,0,0);if(sel<0)return;wchar_t w[128];GetWindowTextW(edit,w,128);std::string t=trim(narrow(w));
    std::lock_guard<std::mutex> lock(mu);if(sel>=(int)rows.size())return;Book&b=books[page];const std::string&k=rows[sel];
    if(reset||t.empty()||t==k){b.draft.erase(k);changes_serial++;return;}
    if(!usable(t,b.max)||t.find(" = ")!=std::string::npos){MessageBoxW(window.load(),page?L"Use plain English letters, numbers and punctuation (the game's font), up to 63 characters.\n\nText about as long as the game's keeps the screen's layout.":L"Use plain English letters, numbers and punctuation (the game's font), up to 31 characters.\n\nLabels shrink to fit their bar; help lines show about 26 characters.",L"In-game editor",MB_OK|MB_ICONWARNING);return;}
    b.draft[k]=t;changes_serial++;
}
bool leave_ok(HWND w){
    int n;{std::lock_guard<std::mutex> lock(mu);n=unsaved_locked();}if(!n)return true;
    int a=MessageBoxW(w,L"You have unsaved in-game edits.\n\nSave them now?\n\nYes saves them, No throws them away, Cancel goes back to the editor.",L"In-game editor",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(a==IDCANCEL)return false;std::lock_guard<std::mutex> lock(mu);if(a==IDYES)save_locked();else discard_locked();return true;
}
HWND add(HWND p,const wchar_t*cls,const wchar_t*text,DWORD style,int id){HWND h=CreateWindowExW(std::wcscmp(cls,L"EDIT")==0||std::wcscmp(cls,L"LISTBOX")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,p,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
void layout(HWND w){
    RECT r;GetClientRect(w,&r);const int W=r.right,H=r.bottom,m=12;int dpi=screen_dpi();auto D=[&](int v){return MulDiv(v,dpi,96);};
    MoveWindow(page_box,m,m,D(170),D(300),TRUE);MoveWindow(hint,m+D(180),m,W-2*m-D(180),D(40),TRUE);
    int y=m+D(46),lh=H-y-D(120);MoveWindow(list,m,y,W-2*m,lh,TRUE);y+=lh+D(8);
    MoveWindow(edit,m,y,W-2*m-D(220),D(28),TRUE);MoveWindow(GetDlgItem(w,ID_APPLY),W-m-D(212),y,D(100),D(28),TRUE);MoveWindow(GetDlgItem(w,ID_RESET),W-m-D(104),y,D(104),D(28),TRUE);y+=D(36);
    MoveWindow(GetDlgItem(w,ID_SAVE),m,y,D(110),D(30),TRUE);MoveWindow(GetDlgItem(w,ID_DISCARD),m+D(118),y,D(150),D(30),TRUE);MoveWindow(GetDlgItem(w,ID_LOG),m+D(276),y,D(150),D(30),TRUE);y+=D(38);
    MoveWindow(status,m,y,W-2*m,D(24),TRUE);
    {const int py=m+D(46);for(HWND pw:page_wins)MoveWindow(pw,m,py,W-2*m,H-D(84)-py,TRUE);}
    int tabs[2]={D(page?60:30),D(page?220:130)};SendMessageW(list,LB_SETTABSTOPS,2,(LPARAM)tabs);
}
void show_page(HWND w,int p){
    extra=p>=2?p-2:-1;const int text_page=extra<0?SW_SHOW:SW_HIDE;
    for(int id:{ID_LIST,ID_EDIT,ID_APPLY,ID_RESET,ID_HINT})ShowWindow(GetDlgItem(w,id),text_page);
    for(size_t i=0;i<page_wins.size();i++)ShowWindow(page_wins[i],(int)i==extra?SW_SHOW:SW_HIDE);
    if(extra>=0){shown_pserial=~0u;refresh(true);return;}
    page=p;SetWindowTextW(hint,hints[p]);SendMessageW(edit,EM_LIMITTEXT,(WPARAM)books[p].max,0);SetWindowTextW(edit,L"");layout(w);refresh(true);}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{int dpi=screen_dpi();font=CreateFontW(-MulDiv(15,dpi,96),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        page_box=add(w,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ID_PAGE);
        SendMessageW(page_box,CB_ADDSTRING,0,(LPARAM)L"PC menu text");SendMessageW(page_box,CB_ADDSTRING,0,(LPARAM)L"Game text");
        page_wins.clear();for(auto*p:pages()){SendMessageW(page_box,CB_ADDSTRING,0,(LPARAM)p->title);page_wins.push_back(p->create(w,font));}
        SendMessageW(page_box,CB_SETCURSEL,0,0);extra=-1;
        hint=add(w,L"STATIC",hints[0],0,ID_HINT);
        list=add(w,L"LISTBOX",L"",WS_VSCROLL|LBS_NOTIFY|LBS_USETABSTOPS|LBS_NOINTEGRALHEIGHT|WS_TABSTOP,ID_LIST);
        edit=add(w,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,ID_EDIT);SendMessageW(edit,EM_LIMITTEXT,31,0);
        add(w,L"BUTTON",L"Apply",BS_DEFPUSHBUTTON|WS_TABSTOP,ID_APPLY);add(w,L"BUTTON",L"Built-in text",WS_TABSTOP,ID_RESET);
        add(w,L"BUTTON",L"Save changes",WS_TABSTOP,ID_SAVE);add(w,L"BUTTON",L"Discard unsaved",WS_TABSTOP,ID_DISCARD);add(w,L"BUTTON",L"Open change log",WS_TABSTOP,ID_LOG);
        status=add(w,L"STATIC",L"",0,ID_STATUS);layout(w);SetTimer(w,1,400,nullptr);refresh(true);return 0;}
    case WM_SIZE:layout(w);return 0;
    case WM_TIMER:refresh(false);return 0;
    case WM_COMMAND:switch(LOWORD(wp)){
        case ID_PAGE:if(HIWORD(wp)==CBN_SELCHANGE){int p=(int)SendMessageW(page_box,CB_GETCURSEL,0,0);if(p>=0&&p<2+(int)pages().size())show_page(w,p);}return 0;
        case ID_LIST:if(HIWORD(wp)==LBN_SELCHANGE||HIWORD(wp)==LBN_DBLCLK)select_row();return 0;
        case ID_APPLY:case IDOK:apply(false);refresh(true);return 0;
        case ID_RESET:apply(true);select_row();refresh(true);return 0;
        case ID_SAVE:{bool ok;{std::lock_guard<std::mutex> lock(mu);ok=save_locked();}if(!ok)MessageBoxW(w,L"game-edits.ini could not be saved. Check the installation folder.",L"In-game editor",MB_OK|MB_ICONWARNING);refresh(true);return 0;}
        case ID_DISCARD:{std::lock_guard<std::mutex> lock(mu);discard_locked();}refresh(true);select_row();return 0;
        case ID_LOG:if(GetFileAttributesW(log_path)!=INVALID_FILE_ATTRIBUTES)open_log();else MessageBoxW(w,L"No changes saved yet.",L"In-game editor",MB_OK);return 0;}
        break;
    case WM_CLOSE:if(leave_ok(w))DestroyWindow(w);return 0;
    case WM_DESTROY:KillTimer(w,1);DeleteObject(font);PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
void run(){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JailbreakGameEditor";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)/*IDC_ARROW*/);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));RegisterClassW(&wc);
    HWND w=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Road Rash: Jailbreak - In-game editor (F2)",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(860,screen_dpi(),96),MulDiv(600,screen_dpi(),96),nullptr,nullptr,wc.hInstance,nullptr);
    if(!w){running=false;return;}window=w;ShowWindow(w,SW_SHOWNORMAL);SetForegroundWindow(w);psx_mod_counter_add("rr.editor.opened",1);
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(m.message==WM_KEYDOWN&&m.wParam==VK_RETURN&&m.hwnd==edit){apply(false);refresh(true);continue;}if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    window=nullptr;running=false;
}

// ---- Game text in guest RAM ---------------------------------------------------
// The replacement goes 8 KB below the drawing call's stack pointer, in RAM the
// game never touches: the stack (top 0x801FFFxx) reaches at most ~0xE00 deep
// (low-water 0x801FF200, menus and results), and the race heap ends near
// 0x801FA000 (RAM dump, slot 1 and slot 6); 2 KB below sp is inside the
// stack's deepest use. The copy is used only where those bytes
// are all zero, and they are put back once the call has drawn the text
// (game_text_return).
const uint32_t kStackGap=0x2000;
struct TextLoan {uint32_t at=0,sp=0;unsigned n=0;uint8_t saved[64];} text_loan;
bool in_ram(uint32_t p,uint32_t n){return p>=0x80000000u&&p<0x80200000u&&n<=0x80200000u-p;}
uint8_t* guest(uint32_t p){return memory_get_ram_ptr()+(p&0x1fffff);}
void give_back(){if(text_loan.at){std::memcpy(guest(text_loan.at),text_loan.saved,text_loan.n);text_loan={};}}
std::set<std::string> interned;   // stable pointers for title text (game_text_title)
}
namespace rrpc {
void menu_text(char* text,size_t cap,const char* where){
    if(!text||!text[0]||cap<2||netplay())return;
    std::lock_guard<std::mutex> lock(mu);load_locked();std::string k(text);
    if(usable(k,pc.max))see_locked(pc,k,where);
    auto it=pc.draft.find(k);if(it==pc.draft.end())return;
    std::snprintf(text,cap,"%s",it->second.c_str());
}
bool game_text_draw(CPUState* c,const char* where){
    give_back();
    if(!c||!where||netplay())return false;
    uint32_t a1=c->gpr[5];if(!in_ram(a1,1))return false;
    const char*s=(const char*)guest(a1);size_t n=0,lim=std::min<size_t>(game.max+16,0x80200000u-a1);while(n<lim&&s[n])n++;if(!n||n>=lim)return false;
    // Spaces around the words (the game's alignment) stay as they are.
    size_t lead=0,tail=n;while(lead<n&&s[lead]==' ')lead++;while(tail>lead&&s[tail-1]==' ')tail--;
    std::string k(s+lead,tail-lead),r;
    {std::lock_guard<std::mutex> lock(mu);load_locked();
        if(!listable(k)||pc_text_locked(k))return false;
        see_locked(game,k,where);auto it=game.draft.find(k);if(it==game.draft.end())return false;
        r=std::string(lead,' ')+it->second+std::string(n-tail,' ');}
    if(r.size()+1>sizeof text_loan.saved)return false;
    uint32_t sp=c->gpr[29],at=(sp-kStackGap)&~7u;
    if(!in_ram(sp,4)||at<0x80100000u||!in_ram(at,sizeof text_loan.saved))return false;
    for(unsigned i=0;i<sizeof text_loan.saved;i++)if(guest(at)[i]){psx_mod_counter_add("rr.editor.game_text_no_room",1);return false;}
    text_loan.at=at;text_loan.sp=sp;text_loan.n=(unsigned)r.size()+1;std::memcpy(text_loan.saved,guest(at),text_loan.n);std::memcpy(guest(at),r.c_str(),text_loan.n);
    c->gpr[5]=at;psx_mod_counter_add("rr.editor.game_text_draws",1);
    if(std::getenv("RR_GAME_TEXT_TRACE")){static int shown;if(shown++<20)std::fprintf(stderr,"game text: sp=%08x copy=%08x \"%s\" -> \"%s\"\n",sp,at,k.c_str(),r.c_str());}
    return true;
}
void game_text_return(uint32_t sp){if(text_loan.at&&text_loan.sp==sp)give_back();}
const char* game_text_title(const char* text){
    if(!text||!text[0]||netplay())return text;
    std::lock_guard<std::mutex> lock(mu);load_locked();std::string k(text);
    std::string bare=!k.empty()&&k.back()==':'?k.substr(0,k.size()-1):k;
    if(!listable(k)||pc_text_locked(k)||pc_text_locked(bare))return text;   // PC pages' headings: PC menu text
    see_locked(game,k,"Titles");auto it=game.draft.find(k);if(it==game.draft.end())return text;
    return interned.insert(it->second).first->c_str();
}
void editor::game_edits_ensure_loaded(){std::lock_guard<std::mutex> lock(mu);load_locked();}
void game_editor_toggle(){
    if(netplay())return;
    {std::lock_guard<std::mutex> lock(mu);load_locked();}
    if(HWND w=window.load()){ShowWindow(w,SW_RESTORE);SetForegroundWindow(w);return;}
    bool expected=false;if(!running.compare_exchange_strong(expected,true))return;
    std::thread(run).detach();
}
}
