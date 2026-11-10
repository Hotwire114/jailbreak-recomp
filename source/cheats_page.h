/* Launcher Cheats page (page CHEAT_PAGE). LGPL-2.1-or-later. Original author
 * code. Included by launcher.c before native_action. Codes, ids and the
 * verified addresses: cheat_codes.h; the runtime applies them
 * (cheats_runtime.h) and re-reads cheats.ini when it changes, so a switch
 * here also applies to a running Enhanced game.
 * Native rows: 0..RC_CHEATS-1 the built-in toggles, then "Turn all cheats
 * off", "Add custom code", then one row per custom code (tuning.h group
 * "custom_codes"; Enter switches it, Del / pad X removes it). */
static int cp_on[RC_CHEATS],cp_custom_n,cp_custom_on[RC_MAX_CUSTOM],cp_loaded;
static wchar_t cp_custom_name[RC_MAX_CUSTOM][RC_NAME];static char cp_custom_code[RC_MAX_CUSTOM][RC_TEXT];
static void cp_add_code(void);
static void cp_path(wchar_t*out){join(out,store,L"cheats.ini");}
static void cp_load(void){
    wchar_t path[32768];cp_path(path);cp_loaded=1;
    for(int i=0;i<RC_CHEATS;i++){wchar_t id[32];swprintf(id,32,L"%hs",rc_cheats[i].id);cp_on[i]=GetPrivateProfileIntW(L"cheats",id,0,path)!=0;}
    int n=GetPrivateProfileIntW(L"custom",L"count",0,path);if(n<0)n=0;if(n>RC_MAX_CUSTOM)n=RC_MAX_CUSTOM;cp_custom_n=0;
    for(int k=1;k<=n;k++){wchar_t key[16],text[RC_TEXT];RcLine l[RC_MAX_LINES];int c=cp_custom_n;
        swprintf(key,16,L"code%d",k);GetPrivateProfileStringW(L"custom",key,L"",text,RC_TEXT,path);WideCharToMultiByte(CP_UTF8,0,text,-1,cp_custom_code[c],RC_TEXT,NULL,NULL);
        if(rc_parse(cp_custom_code[c],l,RC_MAX_LINES,NULL,0)<=0)continue;   /* hand-edited and broken: dropped on the next save */
        swprintf(key,16,L"name%d",k);GetPrivateProfileStringW(L"custom",key,L"Custom code",cp_custom_name[c],RC_NAME,path);
        swprintf(key,16,L"on%d",k);cp_custom_on[c]=GetPrivateProfileIntW(L"custom",key,0,path)!=0;cp_custom_n++;}
}
static int cp_save(void){
    wchar_t path[32768],key[16],t[RC_TEXT];cp_path(path);int ok=1;
    for(int i=0;i<RC_CHEATS;i++){wchar_t id[32];swprintf(id,32,L"%hs",rc_cheats[i].id);ok=WritePrivateProfileStringW(L"cheats",id,cp_on[i]?L"1":L"0",path)&&ok;}
    WritePrivateProfileStringW(L"custom",NULL,NULL,path);swprintf(t,16,L"%d",cp_custom_n);ok=WritePrivateProfileStringW(L"custom",L"count",t,path)&&ok;
    for(int k=0;k<cp_custom_n;k++){
        swprintf(key,16,L"name%d",k+1);ok=WritePrivateProfileStringW(L"custom",key,cp_custom_name[k],path)&&ok;
        swprintf(key,16,L"code%d",k+1);MultiByteToWideChar(CP_UTF8,0,cp_custom_code[k],-1,t,RC_TEXT);ok=WritePrivateProfileStringW(L"custom",key,t,path)&&ok;
        swprintf(key,16,L"on%d",k+1);ok=WritePrivateProfileStringW(L"custom",key,cp_custom_on[k]?L"1":L"0",path)&&ok;}
    WritePrivateProfileStringW(NULL,NULL,NULL,path);
    if(!ok)notice(L"CHEATS COULD NOT BE SAVED — CHECK YOUR INSTALLATION FOLDER");
    return ok;
}
static int cp_custom_count(void){if(!cp_loaded)cp_load();return cp_custom_n;}
static int cp_on_count(void){if(!cp_loaded)cp_load();int n=0;for(int i=0;i<RC_CHEATS;i++)n+=cp_on[i];for(int k=0;k<cp_custom_n;k++)n+=cp_custom_on[k];return n;}
static int cp_active(void){return cp_on_count()>0;}
static int cp_row_count(void){return RC_CHEATS+2+cp_custom_count();}
static int cp_row(int item,wchar_t*label,wchar_t*value,size_t cap){
    if(!cp_loaded)cp_load();label[0]=value[0]=0;
    if(item>=0&&item<RC_CHEATS){swprintf(label,cap,L"%hs",rc_cheats[item].name);wcsncpy(value,cp_on[item]?L"On":L"Off",cap);return ROW_VALUE;}
    if(item==RC_CHEATS){wcsncpy(label,L"Turn all cheats off",cap);if(cp_active())swprintf(value,cap,L"%d on",cp_on_count());return ROW_ACTION;}
    if(item==RC_CHEATS+1){wcsncpy(label,L"Add custom code",cap);if(cp_custom_n)swprintf(value,cap,L"%d of %d",cp_custom_n,RC_MAX_CUSTOM);return ROW_ACTION;}
    int k=item-RC_CHEATS-2;if(k>=0&&k<cp_custom_n){wcsncpy(label,cp_custom_name[k],cap);wcsncpy(value,cp_custom_on[k]?L"On":L"Off",cap);return ROW_VALUE;}
    return ROW_INFO;
}
static const wchar_t*cp_help(int item){
    static wchar_t text[700];const wchar_t*tail=enhanced_mode?L"":L"\nCheats apply in Enhanced only; Original always plays stock.";
    if(item>=0&&item<RC_CHEATS){swprintf(text,700,L"%hs%ls%ls",rc_cheats[item].help,item<RC_RACE_CHEATS?L"\nRaces only, Player 1. Achievements stay locked while it is on.":L"\nAchievements stay locked while it is on.",tail);return text;}
    if(item==RC_CHEATS){swprintf(text,700,L"Switch every built-in cheat and custom code off.%ls",tail);return text;}
    if(item==RC_CHEATS+1){swprintf(text,700,L"Paste your own GameShark codes (80, 30, D0-D3, E0-E3) and give them a name.\nNew codes start switched off. Up to %d codes.%ls",RC_MAX_CUSTOM,tail);return text;}
    int k=item-RC_CHEATS-2;if(k>=0&&k<cp_custom_n){wchar_t codes[RC_TEXT];MultiByteToWideChar(CP_UTF8,0,cp_custom_code[k],-1,codes,RC_TEXT);
        for(wchar_t*p=codes;*p;p++)if(*p==L';')*p=L' ';
        swprintf(text,700,L"%ls\nEnter switches it %ls (applies at once, in menus too). Del or the X button removes it.%ls",codes,cp_custom_on[k]?L"off":L"on",tail);return text;}
    return L"";
}
static void cp_action(int item){
    if(!cp_loaded)cp_load();
    if(item>=0&&item<RC_CHEATS){cp_on[item]=!cp_on[item];cp_save();return;}
    if(item==RC_CHEATS){for(int i=0;i<RC_CHEATS;i++)cp_on[i]=0;for(int k=0;k<cp_custom_n;k++)cp_custom_on[k]=0;if(cp_save())notice(L"ALL CHEATS OFF");return;}
    if(item==RC_CHEATS+1){cp_add_code();return;}
    int k=item-RC_CHEATS-2;if(k>=0&&k<cp_custom_n){cp_custom_on[k]=!cp_custom_on[k];cp_save();}
}
/* Del / pad X on a custom code: remove it after a prompt. 0 when the row is not one. */
static int cp_delete(void){
    int np;if(!nat_enter(selected,&np))return 0;int handled=0,k=selected-RC_CHEATS-2;
    if(page==CHEAT_PAGE&&k>=0&&k<cp_custom_count()){wchar_t text[200];handled=1;
        swprintf(text,200,L"Remove the custom code “%ls”?",cp_custom_name[k]);
        if(MessageBoxW(mainwindow,text,L"Remove custom code",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){
            for(int j=k;j+1<cp_custom_n;j++){wcscpy(cp_custom_name[j],cp_custom_name[j+1]);strcpy(cp_custom_code[j],cp_custom_code[j+1]);cp_custom_on[j]=cp_custom_on[j+1];}
            cp_custom_n--;cp_save();}}
    nat_leave(np);InvalidateRect(mainwindow,NULL,FALSE);return handled;
}
typedef struct CodePrompt {HWND name,codes;int accepted;} CodePrompt;
static int cp_name_valid(const wchar_t*s){size_t n=wcslen(s);int nonspace=0;if(!n||n>=RC_NAME)return 0;for(size_t i=0;i<n;i++){wchar_t c=s[i];if(c!=L' ')nonspace=1;if(c<32||c>126)return 0;}return nonspace;}
static LRESULT CALLBACK cp_dialog_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return DefWindowProcW(window,msg,wp,lp);}
    CodePrompt*p=(CodePrompt*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(msg==WM_COMMAND&&LOWORD(wp)==IDOK){
        wchar_t name[64],wide[2048];char codes[2048],why[96];RcLine l[RC_MAX_LINES];GetWindowTextW(p->name,name,64);GetWindowTextW(p->codes,wide,2048);
        while(name[0]==L' ')memmove(name,name+1,wcslen(name)*sizeof *name);{size_t n=wcslen(name);while(n&&name[n-1]==L' ')name[--n]=0;}
        if(!cp_name_valid(name)){MessageBoxW(window,L"Give the code a name: 1–31 plain characters (letters, numbers, spaces, punctuation).",L"Add custom code",MB_OK);return 0;}
        WideCharToMultiByte(CP_UTF8,0,wide,-1,codes,sizeof codes,NULL,NULL);int n=rc_parse(codes,l,RC_MAX_LINES,why,sizeof why);
        if(n<=0){wchar_t text[200];swprintf(text,200,L"%hs\n\nEach line: 8 hex digits, a space, 4 hex digits (for example 800D5766 5A5A).",n<0?why:"Paste at least one code.");MessageBoxW(window,text,L"Add custom code",MB_OK|MB_ICONWARNING);return 0;}
        int k=cp_custom_n;wcscpy(cp_custom_name[k],name);rc_format(l,n,cp_custom_code[k],RC_TEXT);cp_custom_on[k]=0;cp_custom_n++;
        if(cp_save()){p->accepted=1;notice(L"CUSTOM CODE ADDED — SELECT IT TO SWITCH IT ON");}else cp_custom_n--;
        DestroyWindow(window);return 0;}
    if((msg==WM_COMMAND&&LOWORD(wp)==IDCANCEL)||msg==WM_CLOSE){DestroyWindow(window);return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
static void cp_add_code(void){
    if(!cp_loaded)cp_load();if(cp_custom_n>=RC_MAX_CUSTOM){notice(L"UP TO 16 CUSTOM CODES — REMOVE ONE FIRST (DEL)");return;}
    WNDCLASSW wc={0};wc.lpfnWndProc=cp_dialog_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"JailbreakCheatCode";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    CodePrompt state={0};RECT r;GetWindowRect(mainwindow,&r);UINT dpi=window_dpi(mainwindow);
#define D(v) MulDiv(v,dpi,96)
    HWND dialog=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Add custom code",WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-D(480))/2,r.top+(r.bottom-r.top-D(430))/2,D(480),D(430),mainwindow,NULL,wc.hInstance,&state);if(!dialog)return;
    HWND l1=CreateWindowW(L"STATIC",L"Name",WS_CHILD|WS_VISIBLE,D(22),D(16),D(420),D(26),dialog,NULL,wc.hInstance,NULL);
    state.name=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,D(22),D(44),D(420),D(32),dialog,(HMENU)100,wc.hInstance,NULL);SendMessageW(state.name,EM_LIMITTEXT,RC_NAME-1,0);
    HWND l2=CreateWindowW(L"STATIC",L"GameShark codes, one per line (80 / 30 writes, D0-D3 / E0-E3 conditions)",WS_CHILD|WS_VISIBLE,D(22),D(88),D(420),D(44),dialog,NULL,wc.hInstance,NULL);
    state.codes=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,D(22),D(134),D(420),D(180),dialog,(HMENU)101,wc.hInstance,NULL);SendMessageW(state.codes,EM_LIMITTEXT,2000,0);
    HWND ok=CreateWindowW(L"BUTTON",L"Add code",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,D(238),D(332),D(113),D(34),dialog,(HMENU)IDOK,wc.hInstance,NULL),cancel=CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,D(358),D(332),D(83),D(34),dialog,(HMENU)IDCANCEL,wc.hInstance,NULL);
    HFONT font=CreateFontW(-D(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI"),mono=CreateFontW(-D(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,FIXED_PITCH,L"Consolas");
#undef D
    HWND widgets[]={l1,state.name,l2,ok,cancel};for(int i=0;i<5;i++)SendMessageW(widgets[i],WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(state.codes,WM_SETFONT,(WPARAM)mono,TRUE);
    EnableWindow(mainwindow,FALSE);ShowWindow(dialog,SW_SHOW);SetFocus(state.name);MSG msg;
    while(IsWindow(dialog)&&GetMessageW(&msg,NULL,0,0)>0){
        /* Enter adds a line in the codes box and confirms elsewhere. */
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&GetFocus()!=state.codes){SendMessageW(dialog,WM_COMMAND,IDOK,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_TAB){HWND next=GetNextDlgTabItem(dialog,GetFocus(),GetKeyState(VK_SHIFT)<0);if(next)SetFocus(next);continue;}
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    EnableWindow(mainwindow,TRUE);SetForegroundWindow(mainwindow);DeleteObject(font);DeleteObject(mono);InvalidateRect(mainwindow,NULL,FALSE);
}
