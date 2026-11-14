/* Game folder: where the installation lives (disc copy, build tools, the built game, saves).
 * LGPL-2.1-or-later. Original author code.
 *
 * The default is %LOCALAPPDATA%\RoadRashJailbreak. A player short of space on C: picks another
 * folder (Game installation > Change game folder, or when the first import finds too little
 * space); the installation then lives in <picked folder>\JailbreakRecomp and the default folder
 * keeps only install-location.txt, which names it. Moving copies the player's own files (saves,
 * settings, mods, music, the disc copy, build tools and downloads), removes the old copy and
 * restarts the launcher, which then rebuilds the game in the new folder: the build folders hold
 * absolute paths, so they are rebuilt rather than moved. */

static wchar_t default_store[32768];   /* %LOCALAPPDATA%\RoadRashJailbreak */
static int data_root_given;            /* --data-root: a test/development folder, never moved */
#define FOLDER_POINTER L"install-location.txt"
#define FOLDER_MARKER L"jailbreak-recomp-folder.txt"   /* in a folder the launcher made: safe to remove whole */
#define FOLDER_NAME L"JailbreakRecomp"
#define FOLDER_NEED_GB 10              /* disc copy, build tools and setup_game.py's NEED_GB */

static int is_dir(const wchar_t*p){DWORD a=GetFileAttributesW(p);return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY);}
static double free_gb(const wchar_t*p){ULARGE_INTEGER f;return GetDiskFreeSpaceExW(p,&f,NULL,NULL)?(double)f.QuadPart/1073741824.0:-1;}

/* Start-up (no --data-root): follow install-location.txt. A folder that can't be found (a drive
 * that's unplugged) is asked about; Cancel starts on the default folder this once. */
static void folder_load(void){
    wchar_t f[32768],p[32768]={0};join(f,default_store,FOLDER_POINTER);FILE*h=_wfopen(f,L"rb");if(!h)return;
    char b[8192]={0};fread(b,1,sizeof b-1,h);fclose(h);char*s=b;if(!memcmp(s,"\xEF\xBB\xBF",3))s+=3;s[strcspn(s,"\r\n")]=0;
    if(!s[0]||!MultiByteToWideChar(CP_UTF8,0,s,-1,p,32768))return;
    while(!is_dir(p)){wchar_t t[33000];swprintf(t,33000,L"Your game folder can't be found:\n%ls\n\nIf it's on a drive that's unplugged, plug it in and choose Retry.\nChoose Cancel to open the launcher without it (it's used again once it can be found).",p);
        if(MessageBoxW(NULL,t,APP_TITLE,MB_RETRYCANCEL|MB_ICONWARNING)!=IDRETRY)return;}
    wcscpy(store,p);
}

static int pick_folder(wchar_t*out,const wchar_t*title){
    IFileOpenDialog*d=NULL;int ok=0;CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    if(FAILED(CoCreateInstance(&CLSID_FileOpenDialog,NULL,CLSCTX_INPROC_SERVER,&IID_IFileOpenDialog,(void**)&d)))return 0;
    FILEOPENDIALOGOPTIONS o=0;IFileOpenDialog_GetOptions(d,&o);IFileOpenDialog_SetOptions(d,o|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);IFileOpenDialog_SetTitle(d,title);
    if(SUCCEEDED(IFileOpenDialog_Show(d,mainwindow))){IShellItem*it=NULL;
        if(SUCCEEDED(IFileOpenDialog_GetResult(d,&it))){PWSTR p=NULL;if(SUCCEEDED(IShellItem_GetDisplayName(it,SIGDN_FILESYSPATH,&p))){wcsncpy(out,p,32700);out[32700]=0;CoTaskMemFree(p);ok=1;}IShellItem_Release(it);}}
    IFileOpenDialog_Release(d);return ok;
}

/* Rebuilt in the new folder (absolute paths inside), or not the player's: left behind. */
static const wchar_t*folder_skip[]={L"compile",L"build-temp",L"runtime",L"runtime-build",L"runtime-netplay",L"runtime-netplay-build",L"runtime-original",L"runtime-project",
    L"update-stage",L"local-build.json",L"game.toml",L"game-original.toml",L"original-launch.json",L"runtime.pid",FOLDER_POINTER,FOLDER_MARKER,NULL};
static int folder_skipped(const wchar_t*name){for(int i=0;folder_skip[i];i++)if(!_wcsicmp(name,folder_skip[i]))return 1;return !_wcsnicmp(name,L"local-build.json",16);}

static wchar_t folder_target[32768],folder_error[400];static HANDLE folder_thread;static int folder_then;   /* 1: set up after the restart, 2: find the game */
static volatile LONG64 folder_total,folder_done;
/* copy 0: add up the sizes; 1: copy (folder_done counts the bytes). Paths in \\?\ form. */
static int folder_walk(const wchar_t*src,const wchar_t*dst,int copy,int top){
    WIN32_FIND_DATAW d;wchar_t pat[32768],s[32768],t[32768];swprintf(pat,32768,L"%ls\\*",src);HANDLE h=FindFirstFileW(pat,&d);if(h==INVALID_HANDLE_VALUE)return 1;int ok=1;
    do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L"..")||(top&&folder_skipped(d.cFileName)))continue;join(s,src,d.cFileName);join(t,dst,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;
            if(copy&&!CreateDirectoryW(t,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){ok=0;break;}if(!folder_walk(s,t,copy,0)){ok=0;break;}}
        else{LONG64 n=((LONG64)d.nFileSizeHigh<<32)|d.nFileSizeLow;if(copy){if(!CopyFileW(s,t,FALSE)){ok=0;break;}folder_done+=n;}else folder_total+=n;}
    }while(FindNextFileW(h,&d));FindClose(h);return ok;
}
/* Everything in dir but the pointer file; the folder itself too when it isn't the default one. */
static void folder_clear(const wchar_t*dir){
    if(_wcsicmp(dir,default_store)){remove_tree(dir);return;}
    wchar_t src[32800],p[32800],pat[32800];swprintf(src,32800,L"\\\\?\\%ls",dir);swprintf(pat,32800,L"%ls\\*",src);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pat,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L"..")||!_wcsicmp(d.cFileName,FOLDER_POINTER))continue;join(p,src,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)remove_tree(p);else{SetFileAttributesW(p,FILE_ATTRIBUTE_NORMAL);DeleteFileW(p);}}while(FindNextFileW(h,&d));FindClose(h);}
}
static int same_volume(const wchar_t*a,const wchar_t*b){wchar_t va[MAX_PATH],vb[MAX_PATH];return GetVolumePathNameW(a,va,MAX_PATH)&&GetVolumePathNameW(b,vb,MAX_PATH)&&!_wcsicmp(va,vb);}
static unsigned __stdcall folder_move_worker(void*arg){
    (void)arg;wchar_t src[32800],dst[32800],p[32768];swprintf(src,32800,L"\\\\?\\%ls",store);swprintf(dst,32800,L"\\\\?\\%ls",folder_target);
    folder_total=folder_done=0;folder_walk(src,dst,0,1);
    double need=(double)folder_total/1073741824.0+(same_volume(store,folder_target)?1:8),have=free_gb(folder_target);
    if(have>=0&&have<need){swprintf(folder_error,400,L"There isn't enough space there: %.1f GB free, about %.0f GB needed. Pick a folder on another drive.",have,need+0.5);folder_clear(folder_target);return 1;}
    if(!folder_walk(src,dst,1,1)){swprintf(folder_error,400,L"Your files couldn't be copied to the new folder (error %lu). Nothing was changed: the game stays where it was.",GetLastError());folder_clear(folder_target);return 1;}
    /* The new folder is complete: point to it, then free the old one. */
    int back=!_wcsicmp(folder_target,default_store);
    if(!back){join(p,folder_target,FOLDER_MARKER);FILE*f=_wfopen(p,L"wb");if(f){fputs("Jailbreak Recomp's game folder. The launcher may remove this whole folder when the game moves.\n",f);fclose(f);}}
    ensure(default_store);join(p,default_store,FOLDER_POINTER);
    if(back)DeleteFileW(p);
    else{static char u[32768*3];WideCharToMultiByte(CP_UTF8,0,folder_target,-1,u,sizeof u,NULL,NULL);FILE*f=_wfopen(p,L"wb");int ok=f&&fputs(u,f)>=0;if(f&&fclose(f))ok=0;
        if(!ok){DeleteFileW(p);swprintf(folder_error,400,L"The new game folder couldn't be saved. Nothing was changed: the game stays where it was.");folder_clear(folder_target);return 1;}}
    /* The old copy: the default folder (all but the pointer), or a folder the launcher made. */
    if(!_wcsicmp(store,default_store))folder_clear(store);else{join(p,store,FOLDER_MARKER);if(exists(p))remove_tree(store);}
    return 0;
}

/* Game installation > Change game folder (then=1), or the first import with too little space (then=2). */
static void folder_change(int then){
    if(busy)return;if(data_root_given){notice(L"THIS LAUNCHER WAS STARTED WITH --data-root: ITS FOLDER ISN'T MOVED");return;}
    if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"Close your game before moving it.");return;}}
    wchar_t picked[32768];if(!pick_folder(picked,L"Choose where to keep Jailbreak Recomp (a JailbreakRecomp folder is made inside)"))return;
    size_t n=wcslen(picked);while(n>3&&picked[n-1]==L'\\')picked[--n]=0;
    wchar_t target[32768];
    if(!_wcsicmp(picked,default_store)||!_wcsicmp(PathFindFileNameW(picked),FOLDER_NAME))wcscpy(target,picked);else swprintf(target,32768,L"%ls%ls%ls",picked,picked[n-1]==L'\\'?L"":L"\\",FOLDER_NAME);
    if(!_wcsicmp(target,store)){notice(L"THE GAME IS ALREADY IN THAT FOLDER");return;}
    size_t ls=wcslen(store),lt=wcslen(target);
    if((!_wcsnicmp(target,store,ls)&&target[ls]==L'\\')||(!_wcsnicmp(store,target,lt)&&store[lt]==L'\\')){MessageBoxW(mainwindow,L"Pick a folder outside the current game folder.",L"Game folder",MB_ICONWARNING);return;}
    if(is_dir(target)&&_wcsicmp(target,default_store)){wchar_t pat[32800];swprintf(pat,32800,L"%ls\\*",target);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pat,&d);int used=0;
        if(h!=INVALID_HANDLE_VALUE){do{if(wcscmp(d.cFileName,L".")&&wcscmp(d.cFileName,L".."))used=1;}while(!used&&FindNextFileW(h,&d));FindClose(h);}
        if(used){wchar_t t[33000];swprintf(t,33000,L"%ls already has files in it.\nPick another folder (or an empty one).",target);MessageBoxW(mainwindow,t,L"Game folder",MB_ICONWARNING);return;}}
    double have=free_gb(picked);wchar_t t[33500];
    if(!installed)swprintf(t,33500,L"Install Jailbreak Recomp in\n%ls ?\n\n%.1f GB free there (it needs about %d GB).\nNext, pick your game's .bin as usual.",target,have,FOLDER_NEED_GB);
    else swprintf(t,33500,L"Move Jailbreak Recomp to\n%ls ?\n\n%.1f GB free there.\nYour saves, settings, mods and installed game move with it, and the old copy is removed to free the space.%ls",
        target,have,installed?L"\n\nThe launcher then restarts and rebuilds the game in the new folder (15–25 minutes).":L"");
    if(MessageBoxW(mainwindow,t,L"Game folder",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    if(!is_dir(target)&&!CreateDirectoryW(target,NULL)){MessageBoxW(mainwindow,L"That folder can't be written to. Pick another one.",L"Game folder",MB_ICONWARNING);return;}
    wcscpy(folder_target,target);folder_then=then;folder_error[0]=0;
    uintptr_t th=_beginthreadex(NULL,0,folder_move_worker,NULL,0,NULL);if(!th){notice(L"The move could not start.");return;}
    folder_thread=(HANDLE)th;busy=6;notice(L"MOVING YOUR GAME FOLDER…");
}
/* WM_TIMER: progress, then restart in the new folder. */
static void folder_poll(void){
    if(!folder_thread)return;DWORD code;
    if(!GetExitCodeThread(folder_thread,&code)||code==STILL_ACTIVE){LONG64 t=folder_total,d=folder_done;wchar_t s[80];swprintf(s,80,L"MOVING YOUR GAME FOLDER… %d%%",t?(int)(d*100/t):0);notice(s);return;}
    CloseHandle(folder_thread);folder_thread=NULL;busy=0;
    if(code){notice(L"THE GAME FOLDER WASN'T MOVED");MessageBoxW(mainwindow,folder_error,L"Game folder",MB_ICONWARNING);return;}
    wchar_t exe[32768],cmd[32900];GetModuleFileNameW(NULL,exe,32768);
    swprintf(cmd,32900,L"\"%ls\" --launcher --second-instance%ls",exe,folder_then==2?L" --find-game":installed?L" --setup-on-start":L"");
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof si;
    if(CreateProcessW(NULL,cmd,NULL,NULL,FALSE,0,NULL,appdir,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);DestroyWindow(mainwindow);}
    else notice(L"GAME FOLDER MOVED — CLOSE AND REOPEN THE LAUNCHER");
}
/* The first import: too little space for the game here? Offer another folder. 0: stop here. */
static int folder_space_check(void){
    double have=free_gb(store);if(installed||have<0||have>=FOLDER_NEED_GB||data_root_given)return 1;
    wchar_t t[700],drive[MAX_PATH]=L"this drive";GetVolumePathNameW(store,drive,MAX_PATH);
    swprintf(t,700,L"There's only %.1f GB free on %ls.\nThe game needs about %d GB (your disc copy, the build tools and the built game).\n\nChoose another folder for it, for example on a different drive?\n\nYes: choose a folder\nNo: install here anyway",have,drive,FOLDER_NEED_GB);
    int a=MessageBoxW(mainwindow,t,L"Game folder",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(a==IDYES){folder_change(2);return 0;}return a==IDNO;
}
