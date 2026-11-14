/* Launcher updates, "Run setup again" and the antivirus notes. Independently authored;
 * LGPL-2.1-or-later. Included by launcher.c (after the setup code it uses).
 *
 * Updates: the newest GitHub release of the project (RR_UPDATE_API) is checked once a day
 * at most, and from Game installation > Check for updates. Only the release's player
 * package (an asset named JailbreakRecomp_<version>.zip) is considered. It is installed
 * only when GitHub lists a SHA-256 digest for that asset and the download matches it.
 * The package holds no game code or data (the release rules), so an update replaces the
 * launcher's own folder only: the player's installation (%LOCALAPPDATA%\RoadRashJailbreak:
 * disc, saves, settings, builds) is never touched, and the game rebuilds once on the next
 * Play when its build ID changed (launcher.c play_game_now).
 * Installing: every new file is first copied beside its target as <name>.new-update (a
 * failure there leaves the old version untouched), then each target is renamed to
 * <name>.old-update (Windows allows renaming the running launcher) and the new file put
 * in its place. The new launcher starts and the old files are deleted on its next start.
 * launcher.ini [update] check=0 turns the automatic check off; the button still works. */
#define RR_VERSION "1.2"
#define RR_UPDATE_API "https://api.github.com/repos/Hotwire114/jailbreak-recomp/releases/latest"
#define RR_UPDATE_PAGE L"https://github.com/Hotwire114/jailbreak-recomp/releases/latest"

static char update_url[1024],update_sha[65];static unsigned long long update_size;
static volatile LONG update_checking;static int update_manual;
static HANDLE update_thread;
static int setup_clean;   /* build_game: clear the old build folders first (Run setup again) */

static int version_newer(const char*a,const char*b){int x[3]={0},y[3]={0};sscanf(a,"%d.%d.%d",x,x+1,x+2);sscanf(b,"%d.%d.%d",y,y+1,y+2);
    for(int i=0;i<3;i++)if(x[i]!=y[i])return x[i]>y[i];return 0;}
/* JSON: the value after "key": (spaces allowed), searched from p up to end. */
static const char*json_value(const char*p,const char*end,const char*key){char want[64];snprintf(want,sizeof want,"\"%s\"",key);
    for(const char*k=strstr(p,want);k&&k<end;k=strstr(k+1,want)){const char*v=k+strlen(want);while(*v==' '||*v=='\n'||*v=='\r'||*v=='\t')v++;if(*v!=':')continue;v++;while(*v==' '||*v=='\n'||*v=='\r'||*v=='\t')v++;return v;}return NULL;}
static int json_copy(const char*v,char*out,size_t cap){if(!v||*v!='"')return 0;v++;size_t n=0;while(v[n]&&v[n]!='"')n++;if(!n||n>=cap)return 0;memcpy(out,v,n);out[n]=0;return 1;}

/* The newest release's player package: 1 newer (update_* set), 0 up to date, -1 no answer. */
static int update_query(void){
    wchar_t dir[32768],path[32768];join(dir,store,L"downloads");ensure(dir);join(path,dir,L"latest-release.json");
    if(!http_download(RR_UPDATE_API,path))return -1;
    FILE*f=_wfopen(path,L"rb");if(!f)return -1;static char json[1<<20];size_t n=fread(json,1,sizeof json-1,f);fclose(f);json[n]=0;DeleteFileW(path);
    const char*end=json+n,*assets=json_value(json,end,"assets");if(!assets)return -1;
    char newest[16]="";update_url[0]=update_sha[0]=0;update_size=0;
    for(const char*p=assets;(p=json_value(p,end,"name"))!=NULL;p++){
        char name[128];if(!json_copy(p,name,sizeof name))continue;
        size_t len=strlen(name);if(strncmp(name,"JailbreakRecomp_",16)||len<21||_stricmp(name+len-4,".zip"))continue;
        char ver[16];size_t vl=len-20;if(vl>=sizeof ver)continue;memcpy(ver,name+16,vl);ver[vl]=0;
        if(strspn(ver,"0123456789.")!=vl||(newest[0]&&!version_newer(ver,newest)))continue;
        /* This asset's fields come after its name and before the next asset's name. */
        const char*next=json_value(p+1,end,"name");const char*stop=next?next:end;
        const char*u=json_value(p,stop,"browser_download_url"),*d=json_value(p,stop,"digest"),*s=json_value(p,stop,"size");
        char url[1024],digest[80];if(!json_copy(u,url,sizeof url)||strncmp(url,"https://github.com/",19))continue;
        strcpy(newest,ver);strcpy(update_url,url);update_size=s?_strtoui64(s,NULL,10):0;
        update_sha[0]=0;if(json_copy(d,digest,sizeof digest)&&!strncmp(digest,"sha256:",7)&&strlen(digest)==71)strcpy(update_sha,digest+7);
    }
    if(!newest[0])return -1;
    if(!version_newer(newest,RR_VERSION))return 0;
    MultiByteToWideChar(CP_UTF8,0,newest,-1,update_version,16);return 1;
}
static unsigned __stdcall update_check_worker(void*arg){(void)arg;int r=update_query();PostMessageW(mainwindow,WM_APP+3,(WPARAM)(r+1),0);return 0;}
static void update_check(int manual){
    if(InterlockedCompareExchange(&update_checking,1,0))return;update_manual=manual;
    if(manual)notice(L"CHECKING FOR UPDATES…");
    uintptr_t t=_beginthreadex(NULL,0,update_check_worker,NULL,0,NULL);if(!t){update_checking=0;return;}CloseHandle((HANDLE)t);
}
/* Start-up: once a day at most, unless launcher.ini [update] check=0. */
static void update_check_daily(void){
    if(!GetPrivateProfileIntW(L"update",L"check",1,ini))return;
    wchar_t last[32];GetPrivateProfileStringW(L"update",L"last_check",L"0",last,32,ini);
    unsigned long long now=(unsigned long long)time(NULL),then=_wcstoui64(last,NULL,10);
    if(then&&now>=then&&now-then<20*3600)return;
    swprintf(last,32,L"%llu",now);WritePrivateProfileStringW(L"update",L"last_check",last,ini);update_check(0);
}
/* WM_APP+3: r = query result + 1. */
static void update_checked(int r){
    update_checking=0;
    if(r==2){wchar_t t[160];swprintf(t,160,L"VERSION %ls IS AVAILABLE — GAME INSTALLATION > UPDATE TO %ls",update_version,update_version);if(!busy||update_manual)notice(t);}
    else if(update_manual){wchar_t t[160];if(r==1)swprintf(t,160,L"YOU HAVE THE LATEST VERSION (%hs)",RR_VERSION);else wcscpy(t,L"Could not reach GitHub to check for updates. Check your internet connection.");notice(t);}
    update_manual=0;InvalidateRect(mainwindow,NULL,FALSE);
}

/* Every file under dir (relative path rel), deepest first. */
typedef int(*upd_visit)(const wchar_t*full,const wchar_t*rel);
static int upd_walk(const wchar_t*dir,const wchar_t*rel,upd_visit visit){
    wchar_t*pat=malloc(3*32768*sizeof(wchar_t));if(!pat)return 0;wchar_t*full=pat+32768,*sub=pat+2*32768;int ok=1;
    swprintf(pat,32768,L"%ls%ls%ls\\*",dir,rel[0]?L"\\":L"",rel);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pat,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;
        if(rel[0])swprintf(sub,32768,L"%ls\\%ls",rel,d.cFileName);else wcscpy(sub,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)ok=upd_walk(dir,sub,visit)&&ok;
        else{join(full,dir,sub);ok=visit(full,sub)&&ok;}}while(ok&&FindNextFileW(h,&d));FindClose(h);}
    free(pat);return ok;
}
static const wchar_t*upd_stage;   /* the unpacked release's JailbreakRecomp folder */
static int upd_has_suffix(const wchar_t*s,const wchar_t*suffix){size_t a=wcslen(s),b=wcslen(suffix);return a>=b&&!_wcsicmp(s+a-b,suffix);}
static int upd_copy_new(const wchar_t*full,const wchar_t*rel){wchar_t to[32768];swprintf(to,32768,L"%ls\\%ls.new-update",appdir,rel);
    wchar_t parent[32768];wcscpy(parent,to);PathRemoveFileSpecW(parent);ensure(parent);return CopyFileW(full,to,FALSE);}
static int upd_swap(const wchar_t*full,const wchar_t*rel){(void)full;wchar_t target[32768],old[32768],fresh[32768];join(target,appdir,rel);
    swprintf(old,32768,L"%ls.old-update",target);swprintf(fresh,32768,L"%ls.new-update",target);
    if(exists(target)&&!MoveFileExW(target,old,MOVEFILE_REPLACE_EXISTING))return 0;return MoveFileExW(fresh,target,MOVEFILE_REPLACE_EXISTING);}
static int upd_drop_new(const wchar_t*full,const wchar_t*rel){(void)rel;if(upd_has_suffix(full,L".new-update"))DeleteFileW(full);return 1;}
static int upd_drop_old(const wchar_t*full,const wchar_t*rel){(void)rel;if(upd_has_suffix(full,L".old-update"))DeleteFileW(full);return 1;}
/* The old version's files, left by the last update (the old launcher may still be closing). */
static unsigned __stdcall update_cleanup_worker(void*arg){(void)arg;Sleep(4000);upd_walk(appdir,L"",upd_drop_old);upd_walk(appdir,L"",upd_drop_new);return 0;}

static wchar_t update_error[200];
static unsigned __stdcall update_install_worker(void*arg){
    (void)arg;wchar_t dir[32768],zip[32768],partial[32768],stage[32768],root[32768];
    join(dir,store,L"downloads");ensure(dir);swprintf(zip,32768,L"%ls\\JailbreakRecomp_%ls.zip",dir,update_version);swprintf(partial,32768,L"%ls.partial",zip);
    char size[32];snprintf(size,sizeof size,"%llu",update_size);
    if(!archive_ok(zip,size,update_sha)){int got=0;
        for(int attempt=1;attempt<=3&&!got;attempt++){got=http_download(update_url,partial)&&archive_ok(partial,size,update_sha);if(!got)Sleep(2000);}
        if(!got||!MoveFileExW(partial,zip,MOVEFILE_REPLACE_EXISTING)){DeleteFileW(partial);wcscpy(update_error,L"The update could not be downloaded, or it did not match GitHub's checksum. Nothing was changed.");return 1;}}
    join(stage,store,L"update-stage");remove_tree(stage);ensure(stage);char why[300];
    if(!zip_extract(zip,stage,NULL,why,sizeof why)){remove_tree(stage);wcscpy(update_error,L"The update could not be unpacked. Nothing was changed.");return 1;}
    join(root,stage,L"JailbreakRecomp");wchar_t exe[32768];join(exe,root,L"RoadRashJailbreak.exe");
    if(!exists(exe)){remove_tree(stage);wcscpy(update_error,L"The download is not a Jailbreak Recomp package. Nothing was changed.");return 1;}
    upd_stage=root;
    if(!upd_walk(root,L"",upd_copy_new)){upd_walk(appdir,L"",upd_drop_new);remove_tree(stage);wcscpy(update_error,L"The update could not be copied into the launcher's folder. Nothing was changed.");return 1;}
    int ok=upd_walk(root,L"",upd_swap);remove_tree(stage);
    if(!ok){wcscpy(update_error,L"The update was only partly installed. Download the release from GitHub and extract it over this folder.");return 1;}
    DeleteFileW(zip);return 0;
}
static void update_install(void){
    if(busy||!update_url[0])return;
    if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"Close your game before updating.");return;}}
    if(!update_sha[0]){notice(L"GitHub did not list a checksum for this update. Download it from the releases page instead.");ShellExecuteW(mainwindow,L"open",RR_UPDATE_PAGE,NULL,NULL,SW_SHOWNORMAL);return;}
    wchar_t probe[32768];join(probe,appdir,L"update-write-test.tmp");HANDLE h=CreateFileW(probe,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if(h==INVALID_HANDLE_VALUE){MessageBoxW(mainwindow,L"The launcher's folder can't be changed from here (for example, it is in Program Files).\nThe releases page opens instead: download the new version and extract it over this folder.",L"Update",MB_ICONINFORMATION);ShellExecuteW(mainwindow,L"open",RR_UPDATE_PAGE,NULL,NULL,SW_SHOWNORMAL);return;}
    CloseHandle(h);
    wchar_t text[600];swprintf(text,600,L"Update to version %ls?\n\nThe launcher downloads it from GitHub (%llu MB), checks it and restarts.\nYour saves, settings and installed game are kept. If the game itself changed, it rebuilds once when you next press Play.",update_version,(update_size+(1<<20)-1)>>20);
    if(MessageBoxW(mainwindow,text,L"Update",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    uintptr_t t=_beginthreadex(NULL,0,update_install_worker,NULL,0,NULL);if(!t){notice(L"The update could not start.");return;}
    update_thread=(HANDLE)t;busy=5;wchar_t s[120];swprintf(s,120,L"DOWNLOADING VERSION %ls…",update_version);notice(s);
}
/* WM_TIMER: the install thread finished. */
static void update_poll(void){
    if(!update_thread)return;DWORD code;if(!GetExitCodeThread(update_thread,&code)||code==STILL_ACTIVE)return;
    CloseHandle(update_thread);update_thread=NULL;busy=0;
    if(code){notice(update_error);MessageBoxW(mainwindow,update_error,L"Update",MB_ICONWARNING);return;}
    wchar_t exe[32768],cmd[32800];join(exe,appdir,L"RoadRashJailbreak.exe");swprintf(cmd,32800,L"\"%ls\" --launcher --second-instance",exe);
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof si;
    if(CreateProcessW(NULL,cmd,NULL,NULL,FALSE,0,NULL,appdir,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        /* --second-instance: this window may still be open when the new launcher looks for one. */
        DestroyWindow(mainwindow);}
    else notice(L"UPDATED — CLOSE AND REOPEN THE LAUNCHER");
}

/* Game installation > Run setup again: a clean rebuild (build folders and the build receipt
 * cleared; the installed disc, saves, settings, build tools and compile cache are kept). */
static void run_setup_again(void){
    if(busy)return;if(!installed){notice(L"Import your original game first.");return;}
    if(game_process){DWORD code;if(GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE){notice(L"Close your game before running setup again.");return;}}
    if(MessageBoxW(mainwindow,L"Run setup again?\n\nThe game is rebuilt from scratch from your installed disc. This can take 15–25 minutes.\nYour saves, settings and installed game are kept.",L"Run setup again",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    setup_clean=1;build_game();
}
static void setup_clear_old_build(void){
    if(!setup_clean)return;setup_clean=0;setup_status("Clearing the old build");
    const wchar_t*dirs[]={L"runtime-build",L"runtime-netplay-build"};wchar_t p[32768];
    for(int i=0;i<2;i++){join(p,store,dirs[i]);if(GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES)remove_tree(p);}
    join(p,store,L"local-build.json");DeleteFileW(p);
}

/* Shown in turn with the setup progress: antivirus scans slow setup, and players think it froze. */
static const wchar_t*setup_antivirus_note(void){
    return busy==2&&(GetTickCount()/7000)%3==2?L"Your antivirus may scan each new file while setup runs. That can slow it down: it is safe to wait.":NULL;}
/* The first start of a new build: antivirus can hold a new program for minutes. */
static void first_start_note(void){
    wchar_t receipt[32768];join(receipt,store,L"local-build.json");FILETIME t=file_stamp(receipt);
    wchar_t now[40],seen[40];swprintf(now,40,L"%08lx%08lx",t.dwHighDateTime,t.dwLowDateTime);GetPrivateProfileStringW(L"update",L"started_build",L"",seen,40,ini);
    if(!wcscmp(now,seen))return;WritePrivateProfileStringW(L"update",L"started_build",now,ini);
    notice(L"FIRST START OF THIS BUILD — YOUR ANTIVIRUS MAY CHECK IT FIRST, WHICH CAN TAKE A FEW MINUTES");
}
