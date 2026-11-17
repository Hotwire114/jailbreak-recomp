/* Game installation > Uninstall. LGPL-2.1-or-later. Original author code.
 *
 * Removes what the launcher and setup put in the game folder: the disc copy,
 * build tools, built games, downloads, reports and logs. The player's own files
 * (saves, settings, mods, music, cheats, achievements, the online sign-in key)
 * go only when they choose so; by default they stay, ready for a reinstall.
 *
 * Safety: it deletes by name, so anything the launcher didn't create is left
 * where it is (and counted). Links (junctions, symbolic links) are removed as
 * links, never followed. It refuses a drive root, a folder holding the user's
 * profile, Windows, Program Files or the launcher itself, and a folder that
 * isn't a launcher-made installation (the default folder, one with
 * jailbreak-recomp-folder.txt, or a --data-root test folder with an install in
 * it). It runs only while no setup, import, mod job or game is running.
 * Outside the game folder the launcher made only install-location.txt (a moved
 * game): it goes with the folder. Extended-length paths, as remove_tree.
 *
 * Two pages, so one stray press never uninstalls: Uninstall (keep or remove
 * your files) > Are you sure? (No is selected first). The launcher can't delete
 * its own running exe: it ends on its own screen, saying its folder can be
 * deleted now, with that folder open in Explorer. */
static int un_keep=1;   /* keep the player's own files */
enum {UN_GAME,UN_MINE,UN_OTHER,UN_SKIP};
static int un_ends(const wchar_t*s,const wchar_t*e){size_t a=wcslen(s),b=wcslen(e);return a>=b&&!_wcsicmp(s+a-b,e);}
/* Which of the game folder's entries the launcher or setup made, and whose they are. */
static int un_kind(const wchar_t*name,int dir){
    static const wchar_t*const mine[]={L"saves",L"mods",L"music",L"launcher.ini",L"tuning.ini",L"tuning.ini.bak",L"tuning-changes.log",L"cheats.ini",
        L"achievements.ini",L"achievements.ini.bak",L"netplay_secret",NULL};
    static const wchar_t*const game[]={L"game-disc.bin",L"installed-disc.cue",L"installed-disc.importing",L"inputs",L"compile",L"build-temp",L"toolkits",
        L"downloads",L"netplay-libs",L"update-stage",L"reports",L"runtime",L"runtime-build",L"runtime-netplay",L"runtime-netplay-build",L"runtime-original",
        L"runtime-project",L"runtime.pid",L"game.toml",L"game-original.toml",L"original-launch.json",L"build-status.txt",L"launch-status.txt",L"tuning-problems.txt",NULL};
    for(int i=0;mine[i];i++)if(!_wcsicmp(name,mine[i]))return UN_MINE;
    if(!_wcsicmp(name,FOLDER_MARKER)||!_wcsicmp(name,FOLDER_POINTER))return UN_SKIP;   /* dealt with last */
    for(int i=0;game[i];i++)if(!_wcsicmp(name,game[i]))return UN_GAME;
    if(!dir&&(!_wcsnicmp(name,L"local-build.json",16)||(!_wcsnicmp(name,L"netplay-session-",16)&&un_ends(name,L".ini"))||un_ends(name,L".log")))return UN_GAME;
    return UN_OTHER;
}
static int un_removed(int kind){return kind==UN_GAME||(kind==UN_MINE&&!un_keep);}
static void un_long(wchar_t*out,const wchar_t*p){if(wcsncmp(p,L"\\\\?\\",4))swprintf(out,32768,L"\\\\?\\%ls",p);else wcsncpy(out,p,32767),out[32767]=0;}
static LONG64 un_file_size(const WIN32_FIND_DATAW*d){return ((LONG64)d->nFileSizeHigh<<32)|d->nFileSizeLow;}

/* ---- Sizes, added up in the background when the page opens ---------------- */
static wchar_t un_root[32768];static volatile LONG64 un_bytes[3];static volatile LONG un_others,un_sizing;static HANDLE un_size_thread;
static LONG64 un_tree_bytes(const wchar_t*dir){
    WIN32_FIND_DATAW d;wchar_t pat[32768],p[32768];LONG64 n=0;swprintf(pat,32768,L"%ls\\*",dir);HANDLE h=FindFirstFileW(pat,&d);if(h==INVALID_HANDLE_VALUE)return 0;
    do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;
        if(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;   /* a link's target is not ours */
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){join(p,dir,d.cFileName);n+=un_tree_bytes(p);}else n+=un_file_size(&d);}while(FindNextFileW(h,&d));
    FindClose(h);return n;
}
static unsigned __stdcall un_size_worker(void*arg){
    (void)arg;wchar_t root[32768],pat[32768],p[32768];un_long(root,un_root);swprintf(pat,32768,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pat,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;const int dir=(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0,k=un_kind(d.cFileName,dir);
        if(k==UN_SKIP)continue;if(k==UN_OTHER)InterlockedIncrement(&un_others);
        LONG64 n=0;if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){if(dir){join(p,root,d.cFileName);n=un_tree_bytes(p);}else n=un_file_size(&d);}
        InterlockedExchangeAdd64(&un_bytes[k],n);}while(FindNextFileW(h,&d));FindClose(h);}
    InterlockedExchange(&un_sizing,0);if(mainwindow)InvalidateRect(mainwindow,NULL,FALSE);return 0;
}
static void un_size_text(LONG64 n,wchar_t*out,size_t cap){if(n>=1073741824LL)swprintf(out,cap,L"%.1f GB",n/1073741824.0);else swprintf(out,cap,L"%.0f MB",n/1048576.0+(n>0?.5:0));}

/* ---- Is it safe to remove this folder now? NULL: yes; else why not. ------- */
static int un_holds(const wchar_t*folder,const wchar_t*path){   /* path is folder or inside it */
    size_t n=wcslen(folder);if(!n||!path||!path[0])return 0;return !_wcsnicmp(path,folder,n)&&(path[n]==0||path[n]==L'\\'||folder[n-1]==L'\\');
}
static int un_game_pid_running(void){
    wchar_t p[32768],image[32768];join(p,store,L"runtime.pid");FILE*f=_wfopen(p,L"rb");if(!f)return 0;unsigned long pid=0;int got=fscanf(f,"%lu",&pid)==1;fclose(f);if(!got||!pid)return 0;
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,(DWORD)pid);if(!h)return 0;DWORD code=0,n=32768;int run=GetExitCodeProcess(h,&code)&&code==STILL_ACTIVE&&QueryFullProcessImageNameW(h,0,image,&n)&&un_holds(store,image);
    CloseHandle(h);return run;
}
static const wchar_t*un_refusal(void){
    static wchar_t why[600];
    if(busy)return L"Wait until the current job (setup, import, mods or a move) has finished.";
    if(mo_game_running()||un_game_pid_running())return L"Close the game first.";
    wchar_t full[32768];if(!GetFullPathNameW(store,32768,full,NULL))return L"The game folder can't be read.";size_t n=wcslen(full);while(n>3&&full[n-1]==L'\\')full[--n]=0;
    if(!is_dir(full))return L"There is no game folder to remove.";
    if(PathIsRootW(full)||n<4)return L"The game folder is a whole drive. The launcher won't remove that.";
    static const wchar_t*const vars[]={L"USERPROFILE",L"SystemRoot",L"ProgramFiles",L"ProgramFiles(x86)",L"ProgramW6432",L"PUBLIC",L"APPDATA",L"LOCALAPPDATA",L"ProgramData"};
    for(size_t i=0;i<sizeof vars/sizeof vars[0];i++){wchar_t v[32768];DWORD k=GetEnvironmentVariableW(vars[i],v,32768);if(k&&k<32768&&un_holds(full,v)){swprintf(why,600,L"The game folder holds %ls. The launcher won't remove that.",v);return why;}}
    if(un_holds(full,appdir))return L"The game folder holds the launcher itself. The launcher won't remove that.";
    wchar_t p[32768];join(p,full,FOLDER_MARKER);
    int ours=!_wcsicmp(full,default_store)||exists(p);
    if(!ours&&data_root_given){const wchar_t*signs[]={L"game-disc.bin",L"local-build.json",L"launcher.ini"};for(int i=0;i<3&&!ours;i++){join(p,full,signs[i]);ours=exists(p);}}
    if(!ours)return L"This folder wasn't made by the launcher, so it isn't removed.";
    return NULL;
}

/* ---- Removing ------------------------------------------------------------- */
static volatile LONG64 un_done;static volatile LONG un_failed;static HANDLE un_thread;static LONG64 un_planned;static int un_gone;
static void un_remove(const wchar_t*p,const WIN32_FIND_DATAW*d){
    const DWORD a=d->dwFileAttributes;
    if(a&FILE_ATTRIBUTE_REPARSE_POINT){if(a&FILE_ATTRIBUTE_DIRECTORY){if(!RemoveDirectoryW(p))InterlockedIncrement(&un_failed);}else{SetFileAttributesW(p,FILE_ATTRIBUTE_NORMAL);if(!DeleteFileW(p))InterlockedIncrement(&un_failed);}return;}
    if(a&FILE_ATTRIBUTE_DIRECTORY){
        WIN32_FIND_DATAW c;wchar_t pat[32768],q[32768];swprintf(pat,32768,L"%ls\\*",p);HANDLE h=FindFirstFileW(pat,&c);
        if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(c.cFileName,L".")||!wcscmp(c.cFileName,L".."))continue;join(q,p,c.cFileName);un_remove(q,&c);}while(FindNextFileW(h,&c));FindClose(h);}
        if(a&FILE_ATTRIBUTE_READONLY)SetFileAttributesW(p,FILE_ATTRIBUTE_NORMAL);if(!RemoveDirectoryW(p))InterlockedIncrement(&un_failed);return;}
    SetFileAttributesW(p,FILE_ATTRIBUTE_NORMAL);if(DeleteFileW(p))InterlockedExchangeAdd64(&un_done,un_file_size(d));else InterlockedIncrement(&un_failed);
}
static int un_empty_but(const wchar_t*dir,const wchar_t*except){
    WIN32_FIND_DATAW d;wchar_t pat[32768];swprintf(pat,32768,L"%ls\\*",dir);HANDLE h=FindFirstFileW(pat,&d);if(h==INVALID_HANDLE_VALUE)return 1;int empty=1;
    do{if(wcscmp(d.cFileName,L".")&&wcscmp(d.cFileName,L"..")&&(!except||_wcsicmp(d.cFileName,except)))empty=0;}while(empty&&FindNextFileW(h,&d));FindClose(h);return empty;
}
static unsigned __stdcall un_worker(void*arg){
    (void)arg;wchar_t root[32768],pat[32768],p[32768];un_long(root,un_root);swprintf(pat,32768,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pat,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;
        if(un_removed(un_kind(d.cFileName,(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0))){join(p,root,d.cFileName);un_remove(p,&d);}}while(FindNextFileW(h,&d));FindClose(h);}
    /* Nothing of the player's left: the folder itself, then (a moved game) the pointer to it. */
    const int custom=_wcsicmp(un_root,default_store)!=0;
    if(un_empty_but(root,custom?FOLDER_MARKER:NULL)){if(custom){join(p,root,FOLDER_MARKER);DeleteFileW(p);}un_gone=RemoveDirectoryW(root)!=0;}
    if(un_gone&&custom&&!data_root_given){wchar_t q[32768];join(q,default_store,FOLDER_POINTER);DeleteFileW(q);un_long(q,default_store);if(un_empty_but(q,NULL))RemoveDirectoryW(q);}
    return 0;
}

/* ---- Pages ---------------------------------------------------------------- */
static void un_open(void){
    if(!un_size_thread||WaitForSingleObject(un_size_thread,0)==WAIT_OBJECT_0){
        if(un_size_thread)CloseHandle(un_size_thread);wcscpy(un_root,store);for(int i=0;i<3;i++)un_bytes[i]=0;un_others=0;un_sizing=1;
        uintptr_t t=_beginthreadex(NULL,0,un_size_worker,NULL,0,NULL);un_size_thread=(HANDLE)t;if(!t)un_sizing=0;}
    un_keep=1;open_page(UNINSTALL_PAGE);
}
static void un_where(wchar_t*out,size_t cap){if(!PathCompactPathExW(out,un_root[0]?un_root:store,(UINT)(cap<48?cap:48),0)){wcsncpy(out,store,cap-1);out[cap-1]=0;}}
static int un_row(int item,wchar_t*label,wchar_t*value,size_t cap){
    wchar_t s[32];un_size_text(un_bytes[UN_GAME]+(un_keep?0:un_bytes[UN_MINE]),s,32);
    if(page==UNINSTALL_PAGE){if(!item){wcsncpy(label,L"Your saves and settings",cap);wcsncpy(value,un_keep?L"Keep":L"Remove too",cap);return ROW_VALUE;}
        wcsncpy(label,L"Uninstall",cap);if(!un_sizing)swprintf(value,cap,L"frees %ls",s);return ROW_MENU;}
    if(!item){wcsncpy(label,L"No, keep the game",cap);return ROW_ACTION;}
    wcsncpy(label,L"Yes, uninstall",cap);if(!un_sizing)swprintf(value,cap,L"frees %ls",s);return ROW_ACTION;
}
static const wchar_t*un_help(void){
    static wchar_t t[900];wchar_t where[64],game[32],mine[32],left[120]=L"";un_where(where,64);
    if(un_sizing)return L"Adding up what is in your game folder...";
    un_size_text(un_bytes[UN_GAME],game,32);un_size_text(un_bytes[UN_MINE],mine,32);
    if(un_others)swprintf(left,120,L" %ld other item%ls there %ls left alone.",(long)un_others,un_others==1?L"":L"s",un_others==1?L"is":L"are");
    const wchar_t*why=un_refusal();
    if(page==UNINSTALL_PAGE&&selected==0)swprintf(t,900,L"Keep: your saves, settings, mods and music (%ls) stay in %ls, ready if you install again.\nRemove too: they are deleted with the game.",mine,where);
    else if(page==UNINSTALL_CONFIRM&&selected==0)swprintf(t,900,L"Nothing is removed. Back to Uninstall.");
    else swprintf(t,900,L"Removes the disc copy, build tools, built games, downloads and logs (%ls) from %ls.\n%ls (%ls).%ls%ls",
        game,where,un_keep?L"Your saves, settings, mods and music stay":L"Your saves, settings, mods and music go too",mine,left,
        page==UNINSTALL_CONFIRM?L"\nThis can't be undone.":L"");
    if(why&&!(page==UNINSTALL_CONFIRM&&selected==0)){size_t n=wcslen(t);swprintf(t+n,900-n,L"\nNot now: %ls",why);}
    return t;
}
static void un_action(void){
    if(page==UNINSTALL_PAGE&&selected==0){un_keep=!un_keep;InvalidateRect(mainwindow,NULL,FALSE);return;}
    if(page==UNINSTALL_CONFIRM&&selected==0){go_back();return;}
    const wchar_t*why=un_refusal();if(why){notice(why);return;}
    if(un_sizing){notice(L"STILL ADDING UP YOUR GAME FOLDER — TRY AGAIN IN A MOMENT");return;}
    if(page==UNINSTALL_PAGE){open_page(UNINSTALL_CONFIRM);return;}
    if(_wcsicmp(un_root,store)){notice(L"THE GAME FOLDER CHANGED — OPEN UNINSTALL AGAIN");return;}
    un_done=0;un_failed=0;un_gone=0;un_planned=un_bytes[UN_GAME]+(un_keep?0:un_bytes[UN_MINE]);
    uintptr_t t=_beginthreadex(NULL,0,un_worker,NULL,0,NULL);if(!t){notice(L"Uninstall could not start.");return;}
    un_thread=(HANDLE)t;busy=7;notice(L"UNINSTALLING…");
}
static void un_adjust(int dir){(void)dir;if(page==UNINSTALL_PAGE&&selected==0)un_keep=!un_keep;InvalidateRect(mainwindow,NULL,FALSE);}
/* WM_TIMER: progress, then the launcher's own screen with what happened. */
static void un_poll(void){
    if(!un_thread)return;DWORD code;wchar_t s[120];
    if(!GetExitCodeThread(un_thread,&code)||code==STILL_ACTIVE){swprintf(s,120,L"UNINSTALLING… %d%%",un_planned?(int)(un_done*100/un_planned):0);notice(s);return;}
    CloseHandle(un_thread);un_thread=NULL;busy=0;
    wchar_t freed[32],where[64];un_size_text(un_done,freed,32);un_where(where,64);
    swprintf(un_result,700,L"Jailbreak Recomp is uninstalled: %ls freed.",freed);
    size_t k=wcslen(un_result);
    if(un_keep)k+=swprintf(un_result+k,700-k,L" Your saves, settings, mods and music are kept in %ls.",where);
    else if(!un_gone)k+=swprintf(un_result+k,700-k,L" Files the launcher didn't make are still in %ls.",where);
    if(un_failed)k+=swprintf(un_result+k,700-k,L" %ld item%ls couldn't be removed (in use?).",(long)un_failed,un_failed==1?L"":L"s");
    swprintf(un_result+k,700-k,L"\nTo remove the launcher too, close it and delete its folder (opened for you).");
    installed=0;forget_artwork();un_finished=1;page=selected=list_first=nav_depth=0;binding=-1;
    if(!preview_only){wchar_t args[32900];swprintf(args,32900,L"/select,\"%ls\"",appdir);ShellExecuteW(NULL,L"open",L"explorer.exe",args,NULL,SW_SHOWNORMAL);}
    notice(L"UNINSTALLED");InvalidateRect(mainwindow,NULL,FALSE);
}
