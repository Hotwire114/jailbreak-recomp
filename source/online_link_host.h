/* Online from the game's own menus, launcher side (online_link.h has the
 * design). LGPL-2.1-or-later. Original author code.
 * While the game shows the Online pages (ol_active) the launcher stays on them,
 * hidden or not: the game's keys arrive as this window's own key messages, so
 * the pages behave exactly as in the launcher. Starting a race closes the game
 * first (closing); when the online game ends, the offline game starts again. */
#include "online_link.h"
static HANDLE ol_map;static OnlineLink*ol;static int ol_return;
static void ol_detach(void){if(ol)UnmapViewOfFile(ol);if(ol_map)CloseHandle(ol_map);ol=NULL;ol_map=NULL;ol_active=0;}
static void ol_attach(DWORD pid){
    ol_detach();if(!pid)return;wchar_t name[64];online_link_name(name,64,pid);
    ol_map=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(OnlineLink),name);if(!ol_map)return;
    ol=(OnlineLink*)MapViewOfFile(ol_map,FILE_MAP_ALL_ACCESS,0,0,sizeof(OnlineLink));if(!ol){CloseHandle(ol_map);ol_map=NULL;return;}
    memset(ol,0,sizeof *ol);}
static void ol_ask(int kind,int yesno,const wchar_t*title,const wchar_t*caption,const wchar_t*value,int max){
    if(!ol)return;ol->ask=kind;ol->ask_yesno=yesno;ol->ask_max=max;
    wcsncpy(ol->ask_title,title,63);ol->ask_title[63]=0;wcsncpy(ol->ask_caption,caption,127);ol->ask_caption[127]=0;wcsncpy(ol->ask_value,value,99);ol->ask_value[99]=0;}
static void ol_answer(int ok,const wchar_t*text){
    const int kind=ol->ask;ol->ask=0;if(!ok)return;
    if(kind==1)lb_join_typed(text);else if(kind==2)lb_port_typed(text);
    else if(kind==3)lb_sign_out();else if(kind==4&&page==LB_ROOM_PAGE&&lb.host)lb_remove(ol_remove_index);}
static void ol_open(void){
    ol->refused[0]=0;
    if(!lobby_ready){wcscpy(ol->refused,L"Online play needs the online build of the game. Open the launcher's Multiplayer page to set it up.");return;}
    ol_active=1;nav_depth=0;list_first=0;page=LB_ROOMS_PAGE;selected=0;status_tick=0;pub_auth();if(pub_signed_in())pub_request_list();}   /* the list brings the players-online count */   /* no old launcher notice on the game's screen */
static void ol_close(void){if(page==LB_ROOM_PAGE)lb_close();ol_active=0;ol->ask=0;page=selected=list_first=nav_depth=0;}
/* Timer (50 ms): the game's commands, then what the page shows. */
static void ol_poll(void){
    if(!ol)return;
    while(ol->cmd_tail!=ol->cmd_head){OnlineCmd c=ol->cmd[ol->cmd_tail%OL_CMDS];c.text[99]=0;ol->cmd_tail++;
        if(c.kind==OL_OPEN)ol_open();
        else if(!ol_active)continue;
        else if(c.kind==OL_CLOSE)ol_close();
        else if(c.kind==OL_ANSWER)ol_answer(c.value,c.text);
        else if(ol->ask)continue;   /* the question is on the game's screen */
        else if(c.kind==OL_KEY)SendMessageW(mainwindow,WM_KEYDOWN,(WPARAM)c.value,0);
        else if(c.kind==OL_CHAR)SendMessageW(mainwindow,WM_CHAR,(WPARAM)c.value,0);
        else if(c.kind==OL_SELECT&&c.value>=0&&c.value<lb_row_count())selected=c.value;}
    if(ol_active&&!ol_launch_pending&&!LB_PAGE(page)){ol_active=0;ol->ask=0;page=selected=list_first=nav_depth=0;}   /* backed out of Play online */
    InterlockedIncrement(&ol->seq);
    ol->open=ol_active;ol->closing=ol_launch_pending;ol->page=page;
    const int n=ol_active&&!ol_launch_pending?lb_row_count():0;ol->rows=n<OL_ROWS?n:OL_ROWS;ol->selected=selected;
    for(int i=0;i<ol->rows;i++){ol->label[i][0]=ol->value[i][0]=0;ol->kind[i]=lb_row(i,ol->label[i],ol->value[i],64);ol->label[i][63]=ol->value[i][63]=0;}
    wcsncpy(ol->title,ol_active?lb_page_title(page):L"",31);ol->title[31]=0;
    wcsncpy(ol->help,ol_active&&!ol_launch_pending?lb_help():L"",699);ol->help[699]=0;
    wcsncpy(ol->notice,status_tick&&GetTickCount()-status_tick<6000?status_text:L"",255);ol->notice[255]=0;
    ol->chat_on=ol_active&&page==LB_ROOM_PAGE&&lb.joined;ol->chats=lb.chats<OL_CHATS?lb.chats:OL_CHATS;
    for(int i=0;i<ol->chats;i++){wcsncpy(ol->chat_from[i],lb.chat[i].from,63);ol->chat_from[i][63]=0;wcsncpy(ol->chat_text[i],lb.chat[i].text,255);ol->chat_text[i][255]=0;}
    wcsncpy(ol->draft,lb_draft,200);ol->draft[200]=0;
    {const int r=selected>=0&&selected<3?selected:0;ol->tile_rank=r;ol->tile_sel=-1;
     for(int k=0;k<4;k++){lb_bike_name(lb_rank_bikes[r][k],ol->tile[k],40);if(lb_bike(r)==lb_rank_bikes[r][k])ol->tile_sel=k;}}
    ol->signed_in=pub_signed_in();ol->online_count=ol->signed_in&&pub.listed?pub.online:-1;
    ol->mmr=mmr_get();ol->mmr_last=mmr_last;ol->mmr_races=mmr_races;ol->account[0]=0;
    if(ol->signed_in){const char*h=rnet_account_handle();if(!h[0])h=rnet_account_username();if(!MultiByteToWideChar(CP_UTF8,0,h,-1,ol->account,64))ol->account[0]=0;}
    InterlockedIncrement(&ol->seq);}
/* The game has closed. 1: handled here (the online race starts, or the
   offline game comes back after it); 0: the launcher's usual handling. */
static int ol_game_exited(void){
    if(ol_launch_pending){ol_launch_pending=0;ol_detach();ol_return=1;online_launch=1;mod_checked=1;play_game();
        if(!game_process){ol_return=0;quick_reveal();}return 1;}
    if(ol_return){ol_return=0;online_launch=0;mod_checked=0;play_game();if(!game_process&&!play_after_mod_check)quick_reveal();return 1;}
    return 0;}
