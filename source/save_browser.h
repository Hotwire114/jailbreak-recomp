/* Road Rash Jailbreak launcher: Saves browser. LGPL-2.1-or-later.
 * Original author code; no game code or data.
 *
 * Pages: 20 SAVES (memory-card saves, library, quick resume entry),
 * 21 QUICK RESUME (the in-game pause menu's player savestates),
 * 22 SAVE (actions for one memory-card or library save).
 *
 * Memory cards are the runtime's raw 128 KB PS1 cards (saves/card1.mcd,
 * card2.mcd): block 0 holds the "MC" header and 15 directory frames of 128
 * bytes (state 0x51 first block / 0x52 middle / 0x53 last / 0xA0 free, size,
 * next block - 1 or 0xFFFF, file name, XOR checksum in byte 127); blocks 1-15
 * hold the files, a save's first block starting with the "SC" title frame and
 * its icon. Road Rash: Jailbreak writes one 1-block file, BASLUS-01053ROADRASH,
 * per card. A save outside the cards is a standard .mcs file: that save's
 * directory frame followed by its blocks.
 *
 * Every change to a card file: refused while the game runs, the whole card
 * backed up first (saves/backups/edits/<time>/), written to a temporary file
 * and swapped in, then read back and compared; a replaced or deleted save is
 * kept as an .mcs in saves/backups/replaced or deleted. Before every launch
 * both cards are copied to saves/backups/auto/<time>/ (last 20 kept, skipped
 * when unchanged). Nothing is ever hard-deleted.
 *
 * Quick resume states live in saves/openbios/quick (written by the runtime's
 * quick_resume.cpp); Resume starts the game with RR_QUICK_RESUME=<slot>. */
#ifndef RR_SAVE_BROWSER_H
#define RR_SAVE_BROWSER_H
#include "course_names.h"
#include <xinput.h>
#include "sony_pad.h"
enum {SB_PAGE_SAVES=20,SB_PAGE_QUICK=21,SB_PAGE_SAVE=22,SB_QUICK_SLOTS=4,SB_MAX=40};
#define SB_CARD_SIZE 131072
#define SB_BLOCK 8192
#define SB_PRODUCT "BASLUS-01053"
/* Skin and launcher pieces used here, defined further on. */
static void fill(double,double,double,double,uint32_t,unsigned);
static void outline(double,double,double,double,uint32_t,unsigned);
static void resample(int,int,int,int,const uint32_t*,int,int,int,int,int,int,unsigned);
static void px_fill(int,int,int,int,uint32_t,unsigned);
static int P(double);
static void text(const wchar_t*,double,double,double,double,double,int,uint32_t,UINT);
static void open_page(int);

typedef struct SbSave {int where;            /* 1, 2 = memory card; 0 = library */
    wchar_t file[MAX_PATH];                   /* library file name */
    char name[22];wchar_t title[64];int blocks;unsigned char*data;size_t size;unsigned long long hash;FILETIME date;} SbSave;
typedef struct SbQuick {int used,race,players;long long saved;wchar_t course[40];} SbQuick;
static SbSave sb_saves[SB_MAX];static int sb_count,sb_card_ok[2],sb_cur=-1;
static SbQuick sb_quick[SB_QUICK_SLOTS];
static DWORD sb_scan_tick;static int sb_confirm_row=-1;static DWORD sb_confirm_tick;
static wchar_t sb_detail[800];

static void sb_path(wchar_t*out,const wchar_t*name){wchar_t saves[32768];join(saves,store,L"saves");join(out,saves,name);}
static void sb_stamp(wchar_t*out,size_t cap){SYSTEMTIME t;GetLocalTime(&t);swprintf(out,cap,L"%04d-%02d-%02d %02d%02d%02d",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);}
static int sb_game_running(void){DWORD code;return game_process&&GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE;}
static unsigned long long sb_fnv(const unsigned char*p,size_t n){unsigned long long h=1469598103934665603ULL;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ULL;}return h;}
static unsigned char*sb_read(const wchar_t*path,size_t*size,size_t max){
    FILE*f=_wfopen(path,L"rb");if(!f)return NULL;_fseeki64(f,0,SEEK_END);long long n=_ftelli64(f);rewind(f);
    unsigned char*b=n>0&&(size_t)n<=max?malloc((size_t)n):NULL;if(b&&fread(b,1,(size_t)n,f)!=(size_t)n){free(b);b=NULL;}fclose(f);if(b)*size=(size_t)n;return b;
}
/* Whole-file write through a temporary file, swapped in at the end. */
static int sb_write(const wchar_t*path,const void*data,size_t size){
    wchar_t temp[32768];swprintf(temp,32768,L"%ls.writing",path);FILE*f=_wfopen(temp,L"wb");if(!f)return 0;
    int ok=fwrite(data,1,size,f)==size;if(fflush(f))ok=0;if(fclose(f))ok=0;
    if(ok)ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)DeleteFileW(temp);return ok;
}
static void sb_frame_checksum(unsigned char*f){unsigned char x=0;for(int i=0;i<127;i++)x^=f[i];f[127]=x;}
static int sb_card_load(int card,unsigned char*buf){
    wchar_t path[32768],name[16];swprintf(name,16,L"card%d.mcd",card);sb_path(path,name);size_t n=0;unsigned char*b=sb_read(path,&n,SB_CARD_SIZE);
    int ok=b&&n==SB_CARD_SIZE&&b[0]=='M'&&b[1]=='C';if(ok)memcpy(buf,b,SB_CARD_SIZE);free(b);return ok;
}
/* One file's chain from its first directory frame as an .mcs image; 0 if the chain is inconsistent. */
static int sb_extract(const unsigned char*card,int first,unsigned char**out,size_t*size){
    const unsigned char*d=card+first*128;unsigned bytes=le32(d+4),n=bytes/SB_BLOCK;int chain[15],count=0,b=first;
    if(le32(d)!=0x51||!n||n>15||bytes%SB_BLOCK)return 0;
    while(count<15){chain[count++]=b;unsigned next=le16(card+b*128+8);if(next==0xFFFF)break;b=(int)next+1;if(b<1||b>15)return 0;for(int i=0;i<count;i++)if(chain[i]==b)return 0;}
    if((unsigned)count!=n)return 0;
    unsigned char*m=malloc(128+(size_t)n*SB_BLOCK);if(!m)return 0;memcpy(m,d,128);m[8]=m[9]=0xFF;sb_frame_checksum(m);
    for(int i=0;i<count;i++)memcpy(m+128+(size_t)i*SB_BLOCK,card+(size_t)chain[i]*SB_BLOCK,SB_BLOCK);
    *out=m;*size=128+(size_t)n*SB_BLOCK;return 1;
}
static int sb_valid_image(const unsigned char*m,size_t size){return m&&size>=128+SB_BLOCK&&(size-128)%SB_BLOCK==0&&(size-128)/SB_BLOCK<=15&&le32(m)==0x51&&le32(m+4)==size-128&&m[128]=='S'&&m[129]=='C'&&m[10];}
/* Title from the save's SC frame (Shift-JIS, often full-width), as ASCII. */
static void sb_title(const unsigned char*m,wchar_t*out,int cap){
    char raw[65];memcpy(raw,m+128+4,64);raw[64]=0;wchar_t w[80];int n=MultiByteToWideChar(932,0,raw,-1,w,80);if(n<=0){wcsncpy(out,L"PlayStation save",cap);return;}
    int k=0;for(int i=0;w[i]&&k<cap-1;i++){wchar_t c=w[i];if(c>=0xFF01&&c<=0xFF5E)c=(wchar_t)(c-0xFEE0);else if(c==0x3000)c=L' ';if(c<32||c>126)continue;if(c==L' '&&(!k||out[k-1]==L' '))continue;out[k++]=c;}
    while(k&&out[k-1]==L' ')k--;out[k]=0;if(!k)wcsncpy(out,L"PlayStation save",cap);
}
/* First-seen time per save content, so a card save keeps the date it appeared. */
static FILETIME sb_date(unsigned long long hash,FILETIME fallback){
    wchar_t path[32768],key[24],value[32];sb_path(path,L"save-dates.ini");swprintf(key,24,L"%016llx",hash);
    GetPrivateProfileStringW(L"dates",key,L"",value,32,path);unsigned long long t=wcstoull(value,NULL,10);
    if(!t){t=((unsigned long long)fallback.dwHighDateTime<<32)|fallback.dwLowDateTime;swprintf(value,32,L"%llu",t);WritePrivateProfileStringW(L"dates",key,value,path);}
    FILETIME ft={(DWORD)t,(DWORD)(t>>32)};return ft;
}
static void sb_when(FILETIME ft,wchar_t*out,size_t cap){FILETIME local;SYSTEMTIME s;FileTimeToLocalFileTime(&ft,&local);FileTimeToSystemTime(&local,&s);
    const wchar_t*mon[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};swprintf(out,cap,L"%d %ls %d, %02d:%02d",s.wDay,mon[(s.wMonth+11)%12],s.wYear,s.wHour,s.wMinute);}
static void sb_short(FILETIME ft,wchar_t*out,size_t cap){FILETIME local;SYSTEMTIME s;FileTimeToLocalFileTime(&ft,&local);FileTimeToSystemTime(&local,&s);
    const wchar_t*mon[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};SYSTEMTIME now;GetLocalTime(&now);if(now.wYear==s.wYear&&now.wMonth==s.wMonth&&now.wDay==s.wDay)swprintf(out,cap,L"Today %02d:%02d",s.wHour,s.wMinute);else swprintf(out,cap,L"%d %ls %02d:%02d",s.wDay,mon[(s.wMonth+11)%12],s.wHour,s.wMinute);}
/* Library rows: the file name without its time stamp ("2026-10-16 111140 card1.mcs" -> "Copy of card 1"). */
static void sb_library_name(const wchar_t*file,wchar_t*out,size_t cap){const wchar_t*s=file;if(wcslen(s)>18&&s[4]==L'-'&&s[7]==L'-'&&s[10]==L' ')s+=18;
    wchar_t stem[MAX_PATH];wcsncpy(stem,s,MAX_PATH-1);stem[MAX_PATH-1]=0;wchar_t*dot=wcsrchr(stem,L'.');if(dot)*dot=0;
    if(!wcsncmp(stem,L"card",4)&&(stem[4]==L'1'||stem[4]==L'2'))swprintf(out,cap,L"Copy of card %lc%ls",stem[4],stem+5);else if(!wcsncmp(stem,L"imported",8))swprintf(out,cap,L"Imported save%ls",stem+8);else wcsncpy(out,stem,cap);}
static void sb_add(int where,const wchar_t*file,unsigned char*m,size_t size,FILETIME fallback){
    if(sb_count>=SB_MAX||!sb_valid_image(m,size)){free(m);return;}SbSave*s=sb_saves+sb_count++;memset(s,0,sizeof*s);
    s->where=where;if(file)wcsncpy(s->file,file,MAX_PATH-1);memcpy(s->name,m+10,21);s->name[21]=0;s->data=m;s->size=size;s->blocks=(int)((size-128)/SB_BLOCK);
    sb_title(m,s->title,64);s->hash=sb_fnv(m+128,size-128);s->date=sb_date(s->hash,fallback);
}
static void sb_scan_quick(void){
    wchar_t dir[32768];sb_path(dir,L"openbios\\quick");
    for(int i=0;i<SB_QUICK_SLOTS;i++){SbQuick*q=sb_quick+i;memset(q,0,sizeof*q);wchar_t p[32768];swprintf(p,32768,L"%ls\\state_8004032C_slot%02d.pst",dir,i);
        WIN32_FILE_ATTRIBUTE_DATA a;if(!GetFileAttributesExW(p,GetFileExInfoStandard,&a)||(!a.nFileSizeLow&&!a.nFileSizeHigh))continue;q->used=1;
        q->saved=(long long)((((unsigned long long)a.ftLastWriteTime.dwHighDateTime<<32)|a.ftLastWriteTime.dwLowDateTime)/10000000ULL-11644473600ULL);
        swprintf(p,32768,L"%ls\\state_8004032C_slot%02d.info",dir,i);FILE*f=_wfopen(p,L"rb");char line[128];
        if(f){while(fgets(line,sizeof line,f)){char*e=strchr(line,'=');if(!e)continue;*e++=0;e[strcspn(e,"\r\n")]=0;
            if(!strcmp(line,"race"))q->race=atoi(e);else if(!strcmp(line,"players"))q->players=atoi(e);else if(!strcmp(line,"saved"))q->saved=_atoi64(e);
            else if(!strcmp(line,"course"))MultiByteToWideChar(CP_UTF8,0,e,-1,q->course,40);}fclose(f);}
        /* Course names come from the validated disc when the runtime could not read them. */
        if(q->race&&(!q->course[0]||!wcsncmp(q->course,L"Race ",5))&&installed){char n[40];if(rr_course_name(discfile,q->race,q->players,n,sizeof n))MultiByteToWideChar(CP_UTF8,0,n,-1,q->course,40);}
    }
}
static void sb_scan(void){
    for(int i=0;i<sb_count;i++)free(sb_saves[i].data);sb_count=0;
    static unsigned char card[SB_CARD_SIZE];
    for(int c=1;c<=2;c++){sb_card_ok[c-1]=sb_card_load(c,card);if(!sb_card_ok[c-1])continue;
        wchar_t path[32768],name[16];swprintf(name,16,L"card%d.mcd",c);sb_path(path,name);WIN32_FILE_ATTRIBUTE_DATA a;FILETIME when={0};if(GetFileAttributesExW(path,GetFileExInfoStandard,&a))when=a.ftLastWriteTime;
        for(int i=1;i<=15;i++){const unsigned char*d=card+i*128;if(le32(d)!=0x51||strncmp((const char*)d+10,SB_PRODUCT,12))continue;unsigned char*m;size_t n;if(sb_extract(card,i,&m,&n))sb_add(c,NULL,m,n,when);}}
    wchar_t pattern[32768];sb_path(pattern,L"library\\*.mcs");WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(pattern,&fd);
    if(h!=INVALID_HANDLE_VALUE){do{if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;wchar_t p[32768],rel[MAX_PATH+16];swprintf(rel,MAX_PATH+16,L"library\\%ls",fd.cFileName);sb_path(p,rel);size_t n=0;unsigned char*m=sb_read(p,&n,128+15*SB_BLOCK);if(m)sb_add(0,fd.cFileName,m,n,fd.ftLastWriteTime);}while(FindNextFileW(h,&fd));FindClose(h);}
    sb_scan_quick();sb_scan_tick=GetTickCount();
}
static void sb_refresh(void){if(!sb_scan_tick||GetTickCount()-sb_scan_tick>1500)sb_scan();}
static int sb_card_rows(int c){int n=0;for(int i=0;i<sb_count;i++)n+=sb_saves[i].where==c;return n?n:1;}
/* SAVES rows: Quick resume, card 1 saves (or an empty row), card 2, library, import, folder. */
static int sb_saves_rows(void){int n=1+sb_card_rows(1)+sb_card_rows(2);for(int i=0;i<sb_count;i++)n+=!sb_saves[i].where;return n+2;}
static int sb_row_save(int item,int*empty_card){*empty_card=0;if(item<1)return -1;item--;
    for(int c=1;c<=2;c++){int first=1;for(int i=0;i<sb_count;i++)if(sb_saves[i].where==c){if(!item)return i;item--;first=0;}if(first){if(!item){*empty_card=c;return -1;}item--;}}
    for(int i=0;i<sb_count;i++)if(!sb_saves[i].where){if(!item)return i;item--;}return -2-item;}
/* The profiles inside a Road Rash save (PROGRESS-0.15-saves.md): 10 slots of
 * 0x1E4 bytes from block offset 0x834, read the way the game's Load screen
 * (RASHCDF 0x80071B00) reads them: +0 nonzero = empty, +0x0D/+0x0E name,
 * +0xDC mode, +0xE0 rank, +0xED qualifications; the game's own per-slot check
 * (RASHCDF 0x8005E9D0, stored at +0x1DC/+0x1E0) marks a damaged slot. The
 * words are the game's strings from the owner's disc. One line per profile,
 * at most max_lines; returns the number of profiles. */
static int sb_profiles(const SbSave*s,wchar_t*out,size_t cap,int max_lines){
    out[0]=0;if(!s||s->size<128+SB_BLOCK||strncmp(s->name,SB_PRODUCT,12))return 0;
    const unsigned char*b=s->data+128;int used=0,shown=0;size_t len=0;
    for(int k=0;k<10;k++){const unsigned char*p=b+0x834+k*0x1E4;if(le32(p))continue;used++;if(shown>=max_lines)continue;shown++;
        unsigned s1=0,s2=0;for(unsigned i=0;i<0x1DC;i++){s1+=p[i]+i*(unsigned)(k+2);s2=((p[i]^s2)<<1)+i*(unsigned)(k+1);}
        unsigned mode=le32(p+0xDC),rank=p[0xE0];signed char na=(signed char)p[0x0D],nb=(signed char)p[0x0E];
        int name=mode==1?294:mode!=32?295:na==1?(nb==1?282:nb==2?288:276):(nb==1?264:nb==2?270:258);
        int mid=mode==1?45:mode==4?47:mode==8?296:mode==16?75:mode==17?77:mode==24?81:mode==32?43:-1;
        int rid=(mode==1?1538:1535)+(int)(rank<6?rank/2:0);
        char a[40]="",m[40]="",r[40]="";rr_fe_string(discfile,name,a,40);rr_fe_string(discfile,mid,m,40);rr_fe_string(discfile,rid,r,40);
        /* Jail Break: the profile is named after the rider chosen for it. */
        int w=swprintf(out+len,cap-len,L"%ls%ls%hs - %hs, %hs, %u of %d%ls",len?L"\n":L"",mode==32&&a[0]?L"Rider ":L"",a[0]?a:"Profile",m[0]?m:"?",r[0]?r:"?",p[0xED],mode==32?9:6,
            s1==le32(p+0x1DC)&&s2==le32(p+0x1E0)?L"":L" (damaged)");if(w>0)len+=(size_t)w;}
    if(used>shown){int w=swprintf(out+len,cap-len,L"\n+%d more profile%ls",used-shown,used-shown==1?L"":L"s");if(w>0)len+=(size_t)w;}
    return used;
}
static int sb_quick_used(void){int n=0;for(int i=0;i<SB_QUICK_SLOTS;i++)n+=sb_quick[i].used;return n;}
/* SAVE page actions for sb_cur. */
enum {SB_PLAY,SB_TO_CARD1,SB_TO_CARD2,SB_TO_LIBRARY,SB_EXPORT,SB_DELETE};
static int sb_actions(int*out){if(sb_cur<0||sb_cur>=sb_count)return 0;int n=0,w=sb_saves[sb_cur].where;out[n++]=SB_PLAY;
    if(w!=1)out[n++]=SB_TO_CARD1;if(w!=2)out[n++]=SB_TO_CARD2;if(w)out[n++]=SB_TO_LIBRARY;out[n++]=SB_EXPORT;out[n++]=SB_DELETE;return n;}
static int sb_row_count(void){sb_refresh();if(page==SB_PAGE_SAVES)return sb_saves_rows();if(page==SB_PAGE_QUICK)return SB_QUICK_SLOTS;int a[8];return sb_actions(a);}
static const wchar_t*sb_page_title(int p){return p==SB_PAGE_QUICK?L"QUICK RESUME":p==SB_PAGE_SAVE?L"SAVE":L"SAVES";}
static void sb_quick_value(int i,wchar_t*value,size_t cap){SbQuick*q=sb_quick+i;if(!q->used){wcsncpy(value,L"Empty",cap);return;}
    wchar_t when[48];FILETIME ft;unsigned long long t=((unsigned long long)q->saved+11644473600ULL)*10000000ULL;ft.dwLowDateTime=(DWORD)t;ft.dwHighDateTime=(DWORD)(t>>32);sb_short(ft,when,48);
    swprintf(value,cap,L"%ls%ls  %ls",q->course[0]?q->course:L"Race",q->players==2?L" 2P":L"",when);}
/* Row value for a quick slot: course and time of day (today) or date. */
static void sb_quick_short(int i,wchar_t*value,size_t cap){SbQuick*q=sb_quick+i;if(!q->used){wcsncpy(value,L"Empty",cap);return;}
    unsigned long long t=((unsigned long long)q->saved+11644473600ULL)*10000000ULL;FILETIME ft={(DWORD)t,(DWORD)(t>>32)},local;SYSTEMTIME s,now;FileTimeToLocalFileTime(&ft,&local);FileTimeToSystemTime(&local,&s);GetLocalTime(&now);
    const wchar_t*mon[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};wchar_t when[16];
    if(now.wYear==s.wYear&&now.wMonth==s.wMonth&&now.wDay==s.wDay)swprintf(when,16,L"%02d:%02d",s.wHour,s.wMinute);else swprintf(when,16,L"%d %ls",s.wDay,mon[(s.wMonth+11)%12]);
    swprintf(value,cap,L"%ls%ls  %ls",q->course[0]?q->course:L"Race",q->players==2?L" 2P":L"",when);}
static int sb_row(int item,wchar_t*label,wchar_t*value,size_t cap){
    int confirm=sb_confirm_row==item&&GetTickCount()-sb_confirm_tick<4000;
    if(page==SB_PAGE_QUICK){swprintf(label,cap,L"Slot %d",item+1);if(confirm)wcsncpy(value,L"Delete again to confirm",cap);else sb_quick_short(item,value,cap);return sb_quick[item].used?ROW_ACTION:ROW_INFO;}
    if(page==SB_PAGE_SAVE){int a[8],n=sb_actions(a);if(item>=n)return ROW_INFO;SbSave*s=sb_saves+sb_cur;
        switch(a[item]){
        case SB_PLAY:wcsncpy(label,L"Play and load it",cap);if(s->where!=1)wcsncpy(value,L"Puts it on card 1",cap);break;
        case SB_TO_CARD1:wcsncpy(label,L"Copy to memory card 1",cap);break;
        case SB_TO_CARD2:wcsncpy(label,L"Copy to memory card 2",cap);break;
        case SB_TO_LIBRARY:wcsncpy(label,L"Duplicate to the save library",cap);break;
        case SB_EXPORT:wcsncpy(label,L"Export to a file",cap);wcsncpy(value,L".mcs",cap);break;
        case SB_DELETE:wcsncpy(label,L"Delete",cap);wcsncpy(value,confirm?L"Select again to delete":L"A backup is kept",cap);break;}
        return ROW_ACTION;}
    if(item==0){wcsncpy(label,L"Quick resume",cap);swprintf(value,cap,L"%d of %d used",sb_quick_used(),SB_QUICK_SLOTS);return ROW_MENU;}
    int empty,i=sb_row_save(item,&empty);
    if(i>=0){SbSave*s=sb_saves+i;if(s->where)swprintf(label,cap,L"Card %d",s->where);else sb_library_name(s->file,label,cap);sb_short(s->date,value,cap);return ROW_MENU;}
    if(empty){swprintf(label,cap,L"Card %d",empty);wcsncpy(value,sb_card_ok[empty-1]?L"No Road Rash save":L"Not created yet",cap);return ROW_INFO;}
    if(i==-2){wcsncpy(label,L"Import a save file",cap);wcsncpy(value,L".mcs",cap);return ROW_ACTION;}
    wcsncpy(label,L"Open the saves folder",cap);return ROW_ACTION;
}
static const wchar_t*sb_help(void){
    wchar_t when[64];
    if(page==SB_PAGE_QUICK){SbQuick*q=sb_quick+selected;if(selected<0||selected>=SB_QUICK_SLOTS)return L"";
        if(!q->used)return L"Empty slot.\nIn a race: pause, choose OPTIONS > Quick resume > Save.";
        sb_quick_value(selected,when,64);swprintf(sb_detail,400,L"%ls\n%ls",when,enhanced_mode?L"Select starts the game and resumes this race, paused, where you saved it. Delete moves the slot to the backups folder.":L"Needs Enhanced: switch the Play row to Enhanced.");return sb_detail;}
    if(page==SB_PAGE_SAVE){if(sb_cur<0||sb_cur>=sb_count)return L"";SbSave*s=sb_saves+sb_cur;sb_when(s->date,when,64);
        wchar_t where[64];if(s->where)swprintf(where,64,L"Memory card %d",s->where);else wcsncpy(where,L"Save library",64);
        int a[8],n=sb_actions(a);const wchar_t*what=L"";
        switch(selected>=0&&selected<n?a[selected]:-1){case SB_PLAY:what=s->where==1?L"Starts the game; load it in Options > System > Game options > Load.":L"Puts it on card 1 (its save is kept as a backup) and starts the game; load it in Game options > Load.";break;
            case SB_TO_CARD1:case SB_TO_CARD2:what=L"Writes a copy to that card. A save already there is replaced and kept in backups.";break;
            case SB_TO_LIBRARY:what=L"Keeps a copy in the save library on this PC.";break;case SB_EXPORT:what=L"Saves a standard .mcs file other emulators and tools read.";break;
            case SB_DELETE:what=L"Moves it to the backups folder. Select twice to confirm.";break;}
        wchar_t prof[400];if(!sb_profiles(s,prof,400,2))wcscpy(prof,L"No profiles saved yet");
        swprintf(sb_detail,800,L"%ls\n%ls, %ls\n%ls",prof,where,when,what);return sb_detail;}
    int empty,i=sb_row_save(selected,&empty);
    if(selected==0)return L"Resume a race from the pause menu's player save slots.\nIn a race: pause, choose OPTIONS > Quick resume.";
    if(i>=0){SbSave*s=sb_saves+i;sb_when(s->date,when,64);wchar_t prof[400];if(!sb_profiles(s,prof,400,3))wcscpy(prof,L"No profiles saved yet");
        swprintf(sb_detail,800,L"%ls\n%ls %ls",prof,s->where?L"On the memory card since":L"In the library since",when);return sb_detail;}
    if(empty)return L"The game saves its progress here from Options > System > Game options > Save.";
    if(i==-2)return L"Add a Road Rash: Jailbreak .mcs save to the save library.";
    return L"Open the folder with your memory cards, save library, quick resume slots and backups.";
}
/* ---- Card changes ------------------------------------------------------------ */
static int sb_backup_card(int card,const wchar_t*kind,const wchar_t*stamp){
    wchar_t src[32768],dir[32768],dst[32768],name[16],rel[96];swprintf(name,16,L"card%d.mcd",card);sb_path(src,name);
    if(GetFileAttributesW(src)==INVALID_FILE_ATTRIBUTES)return 1;swprintf(rel,96,L"backups\\%ls\\%ls",kind,stamp);sb_path(dir,rel);ensure(dir);join(dst,dir,name);return CopyFileW(src,dst,FALSE)!=0;
}
static int sb_keep(const unsigned char*m,size_t size,const wchar_t*kind,const wchar_t*label){
    wchar_t dir[32768],path[32768],stamp[32],rel[64],file[160];sb_stamp(stamp,32);swprintf(rel,64,L"backups\\%ls",kind);sb_path(dir,rel);ensure(dir);
    swprintf(file,160,L"%ls %ls.mcs",stamp,label);join(path,dir,file);return sb_write(path,m,size);
}
/* Puts an .mcs image on a card: replaces a file of the same name, verified by reading it back. */
static int sb_card_put(int card,const unsigned char*m,size_t size){
    static unsigned char buf[SB_CARD_SIZE];if(!sb_valid_image(m,size)){notice(L"THAT FILE IS NOT A VALID PLAYSTATION SAVE");return 0;}
    if(!sb_card_load(card,buf)){notice(L"THAT MEMORY CARD IS NOT CREATED YET — START THE GAME ONCE");return 0;}
    wchar_t stamp[32],label[32];sb_stamp(stamp,32);if(!sb_backup_card(card,L"edits",stamp)){notice(L"COULD NOT BACK UP THE CARD — NOTHING WAS CHANGED");return 0;}
    char name[22];memcpy(name,m+10,21);name[21]=0;
    for(int i=1;i<=15;i++){unsigned char*d=buf+i*128;if(le32(d)!=0x51||strncmp((char*)d+10,name,21))continue;
        unsigned char*old;size_t n;if(sb_extract(buf,i,&old,&n)){swprintf(label,32,L"card%d replaced",card);int kept=sb_keep(old,n,L"replaced",label);free(old);if(!kept){notice(L"COULD NOT KEEP THE OLD SAVE — NOTHING WAS CHANGED");return 0;}}
        for(int b=i,guard=0;b>=1&&b<=15&&guard<15;guard++){unsigned char*f=buf+b*128;unsigned next=le16(f+8);memset(f,0,128);f[0]=0xA0;f[8]=f[9]=0xFF;sb_frame_checksum(f);if(next==0xFFFF)break;b=(int)next+1;}}
    int blocks=(int)((size-128)/SB_BLOCK),slot[15],n=0;
    for(int i=1;i<=15&&n<blocks;i++)if((le32(buf+i*128)&0xF0)==0xA0)slot[n++]=i;
    if(n<blocks){notice(L"NOT ENOUGH FREE BLOCKS ON THAT CARD — NOTHING WAS CHANGED");return 0;}
    for(int k=0;k<blocks;k++){unsigned char*f=buf+slot[k]*128;memset(f,0,128);
        unsigned state=blocks==1||k==0?0x51:k==blocks-1?0x53:0x52;f[0]=(unsigned char)state;
        if(!k){unsigned bytes=(unsigned)(size-128);f[4]=(unsigned char)bytes;f[5]=(unsigned char)(bytes>>8);f[6]=(unsigned char)(bytes>>16);memcpy(f+10,m+10,21);}
        unsigned next=k<blocks-1?(unsigned)slot[k+1]-1:0xFFFF;f[8]=(unsigned char)next;f[9]=(unsigned char)(next>>8);sb_frame_checksum(f);
        memcpy(buf+(size_t)slot[k]*SB_BLOCK,m+128+(size_t)k*SB_BLOCK,SB_BLOCK);}
    wchar_t path[32768],file[16];swprintf(file,16,L"card%d.mcd",card);sb_path(path,file);
    if(!sb_write(path,buf,SB_CARD_SIZE)){notice(L"COULD NOT WRITE THE CARD — IT WAS NOT CHANGED");return 0;}
    /* Read back: the file chain must give exactly the image written. */
    static unsigned char check[SB_CARD_SIZE];int ok=sb_card_load(card,check)&&!memcmp(check,buf,SB_CARD_SIZE);
    if(ok){ok=0;for(int i=1;i<=15;i++)if(le32(check+i*128)==0x51&&!strncmp((char*)check+i*128+10,name,21)){unsigned char*v;size_t vn;if(sb_extract(check,i,&v,&vn)){ok=vn==size&&!memcmp(v+128,m+128,size-128);free(v);}break;}}
    if(!ok){wchar_t back[32768],rel[96];swprintf(rel,96,L"backups\\edits\\%ls\\%ls",stamp,file);sb_path(back,rel);CopyFileW(back,path,FALSE);notice(L"THE CARD DID NOT READ BACK CORRECTLY — RESTORED FROM ITS BACKUP");return 0;}
    return 1;
}
static int sb_card_remove(int card,const char*name){
    static unsigned char buf[SB_CARD_SIZE];if(!sb_card_load(card,buf))return 0;wchar_t stamp[32],label[32];sb_stamp(stamp,32);
    for(int i=1;i<=15;i++){unsigned char*d=buf+i*128;if(le32(d)!=0x51||strncmp((char*)d+10,name,21))continue;
        unsigned char*old;size_t n;if(!sb_extract(buf,i,&old,&n))return 0;swprintf(label,32,L"card%d",card);int kept=sb_keep(old,n,L"deleted",label);free(old);
        if(!kept||!sb_backup_card(card,L"edits",stamp))return 0;
        for(int b=i,guard=0;b>=1&&b<=15&&guard<15;guard++){unsigned char*f=buf+b*128;unsigned next=le16(f+8);memset(f,0,128);f[0]=0xA0;f[8]=f[9]=0xFF;sb_frame_checksum(f);if(next==0xFFFF)break;b=(int)next+1;}
        wchar_t path[32768],file[16];swprintf(file,16,L"card%d.mcd",card);sb_path(path,file);return sb_write(path,buf,SB_CARD_SIZE);}
    return 0;
}
/* Before every launch: both cards to backups/auto/<time>, unless unchanged since the last copy. */
static void sb_backup_cards(void){
    wchar_t pattern[32768],last[MAX_PATH]=L"";sb_path(pattern,L"backups\\auto\\*");WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileW(pattern,&fd);wchar_t names[64][MAX_PATH];int count=0;
    if(h!=INVALID_HANDLE_VALUE){do{if((fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&fd.cFileName[0]!=L'.'&&count<64)wcscpy(names[count++],fd.cFileName);}while(FindNextFileW(h,&fd));FindClose(h);}
    for(int i=0;i<count;i++)if(wcscmp(names[i],last)>0)wcscpy(last,names[i]);
    int same=last[0]!=0;
    for(int c=1;c<=2&&same;c++){wchar_t a[32768],b[32768],name[16],rel[MAX_PATH+40];swprintf(name,16,L"card%d.mcd",c);sb_path(a,name);swprintf(rel,MAX_PATH+40,L"backups\\auto\\%ls\\%ls",last,name);sb_path(b,rel);
        size_t na=0,nb=0;unsigned char*x=sb_read(a,&na,SB_CARD_SIZE),*y=sb_read(b,&nb,SB_CARD_SIZE);same=(!x&&!y)||(x&&y&&na==nb&&!memcmp(x,y,na));free(x);free(y);}
    if(same)return;wchar_t stamp[32];sb_stamp(stamp,32);sb_backup_card(1,L"auto",stamp);sb_backup_card(2,L"auto",stamp);
    /* Keep the newest 20. */
    while(count>=20){int oldest=0;for(int i=1;i<count;i++)if(wcscmp(names[i],names[oldest])<0)oldest=i;wchar_t dir[32768],rel[MAX_PATH+20],f[32768];swprintf(rel,MAX_PATH+20,L"backups\\auto\\%ls",names[oldest]);sb_path(dir,rel);
        for(int c=1;c<=2;c++){wchar_t name[16];swprintf(name,16,L"card%d.mcd",c);join(f,dir,name);DeleteFileW(f);}RemoveDirectoryW(dir);wcscpy(names[oldest],names[--count]);}
}
static int sb_library_add(const unsigned char*m,size_t size,const wchar_t*label){
    wchar_t dir[32768],path[32768],stamp[32],file[160];sb_path(dir,L"library");ensure(dir);sb_stamp(stamp,32);
    for(int k=1;k<100;k++){swprintf(file,160,k==1?L"%ls %ls.mcs":L"%ls %ls (%d).mcs",stamp,label,k);join(path,dir,file);if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)break;}
    return sb_write(path,m,size);
}
static void sb_import(void){
    wchar_t path[32768]={0};OPENFILENAMEW d={0};d.lStructSize=sizeof d;d.hwndOwner=mainwindow;d.lpstrFilter=L"PlayStation save (*.mcs)\0*.mcs\0";d.lpstrFile=path;d.nMaxFile=32768;d.lpstrTitle=L"Import a Road Rash: Jailbreak save";wchar_t docs[MAX_PATH];if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_PERSONAL,NULL,0,docs)))d.lpstrInitialDir=docs;d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&d))return;size_t n=0;unsigned char*m=sb_read(path,&n,128+15*SB_BLOCK);
    if(!sb_valid_image(m,n)||strncmp((char*)m+10,SB_PRODUCT,12)){free(m);notice(L"THAT FILE IS NOT A ROAD RASH: JAILBREAK SAVE");return;}
    int ok=sb_library_add(m,n,L"imported");free(m);sb_scan_tick=0;notice(ok?L"SAVE IMPORTED INTO THE LIBRARY — SELECT IT TO PUT IT ON A CARD":L"THE SAVE COULD NOT BE IMPORTED");
}
static void sb_export(SbSave*s){
    wchar_t path[32768],stamp[32];sb_stamp(stamp,32);swprintf(path,32768,L"Road Rash Jailbreak %ls.mcs",stamp);OPENFILENAMEW d={0};d.lStructSize=sizeof d;d.hwndOwner=mainwindow;d.lpstrFilter=L"PlayStation save (*.mcs)\0*.mcs\0";d.lpstrFile=path;d.nMaxFile=32768;d.lpstrDefExt=L"mcs";d.lpstrTitle=L"Export save";wchar_t docs[MAX_PATH];if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_PERSONAL,NULL,0,docs)))d.lpstrInitialDir=docs;d.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&d))return;notice(sb_write(path,s->data,s->size)?L"SAVE EXPORTED":L"THE SAVE COULD NOT BE EXPORTED");
}
static void sb_quick_resume(int slot){
    if(!sb_quick[slot].used)return;if(!enhanced_mode){notice(L"QUICK RESUME NEEDS ENHANCED — SWITCH THE PLAY ROW TO ENHANCED");return;}
    if(sb_game_running()){notice(L"YOUR GAME IS ALREADY RUNNING — LOAD THE SLOT FROM ITS PAUSE MENU");return;}
    resume_slot=slot+1;play_game();
}
static void sb_quick_delete(int slot){
    wchar_t dir[32768],dst[32768],stamp[32],rel[80];sb_path(dir,L"openbios\\quick");sb_stamp(stamp,32);swprintf(rel,80,L"backups\\quick-deleted\\%ls",stamp);sb_path(dst,rel);ensure(dst);
    const wchar_t*ext[]={L"pst",L"thumb",L"info",L"hdres"};int ok=1;
    for(int i=0;i<4;i++){wchar_t a[32768],b[32768],name[64];swprintf(name,64,L"state_8004032C_slot%02d.%ls",slot,ext[i]);join(a,dir,name);join(b,dst,name);if(GetFileAttributesW(a)!=INVALID_FILE_ATTRIBUTES&&!MoveFileExW(a,b,MOVEFILE_COPY_ALLOWED))ok=0;}
    sb_scan_tick=0;notice(ok?L"SLOT MOVED TO saves\\backups\\quick-deleted":L"THE SLOT COULD NOT BE MOVED");
}
static int sb_confirmed(void){int again=sb_confirm_row==selected&&GetTickCount()-sb_confirm_tick<4000;sb_confirm_row=again?-1:selected;sb_confirm_tick=GetTickCount();return again;}
/* Enter / A / click. */
static void sb_action(void){
    sb_refresh();
    if(page==SB_PAGE_QUICK){sb_quick_resume(selected);return;}
    if(page==SB_PAGE_SAVES){
        if(selected==0){open_page(SB_PAGE_QUICK);return;}
        int empty,i=sb_row_save(selected,&empty);if(i>=0){sb_cur=i;open_page(SB_PAGE_SAVE);return;}
        if(i==-2)sb_import();else if(i==-3){wchar_t p[32768];sb_path(p,L"");ensure(p);ShellExecuteW(mainwindow,L"open",p,NULL,NULL,SW_SHOWNORMAL);}return;}
    int a[8],n=sb_actions(a);if(selected<0||selected>=n)return;SbSave*s=sb_saves+sb_cur;int act=a[selected];
    if(act==SB_EXPORT){sb_export(s);return;}
    if(act==SB_TO_LIBRARY){wchar_t label[32];swprintf(label,32,L"card%d",s->where);notice(sb_library_add(s->data,s->size,label)?L"COPIED TO THE SAVE LIBRARY":L"THE SAVE COULD NOT BE COPIED");sb_scan_tick=0;return;}
    if(sb_game_running()){notice(L"CLOSE YOUR GAME BEFORE CHANGING ITS MEMORY CARDS");return;}
    if(act==SB_TO_CARD1||act==SB_TO_CARD2){int card=act==SB_TO_CARD1?1:2;if(sb_card_put(card,s->data,s->size)){notice(card==1?L"COPIED TO MEMORY CARD 1 — VERIFIED":L"COPIED TO MEMORY CARD 2 — VERIFIED");sb_scan_tick=0;}return;}
    if(act==SB_DELETE){if(!sb_confirmed())return;int ok;
        if(s->where)ok=sb_card_remove(s->where,s->name);
        else{wchar_t src[32768],dir[32768],dst[32768],rel[MAX_PATH+16];swprintf(rel,MAX_PATH+16,L"library\\%ls",s->file);sb_path(src,rel);sb_path(dir,L"backups\\deleted");ensure(dir);join(dst,dir,s->file);ok=MoveFileExW(src,dst,MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED)!=0;}
        notice(ok?L"SAVE MOVED TO saves\\backups\\deleted":L"THE SAVE COULD NOT BE DELETED");sb_scan_tick=0;sb_cur=-1;page=SB_PAGE_SAVES;selected=0;list_first=0;return;}
    if(act==SB_PLAY){if(s->where!=1&&!sb_card_put(1,s->data,s->size))return;sb_scan_tick=0;
        play_game();if(sb_game_running())notice(L"GAME RUNNING — LOAD YOUR SAVE IN OPTIONS > SYSTEM > GAME OPTIONS > LOAD");}
}
/* Del / X button: delete with a second press. Returns 1 when handled. */
static int sb_delete(void){
    if(page==SB_PAGE_QUICK){if(selected>=0&&selected<SB_QUICK_SLOTS&&sb_quick[selected].used&&sb_confirmed())sb_quick_delete(selected);InvalidateRect(mainwindow,NULL,FALSE);return 1;}
    if(page==SB_PAGE_SAVE){int a[8],n=sb_actions(a);for(int i=0;i<n;i++)if(a[i]==SB_DELETE){if(selected!=i){selected=i;sb_confirm_row=-1;}sb_action();}InvalidateRect(mainwindow,NULL,FALSE);return 1;}
    return page==SB_PAGE_SAVES;
}
/* Card art: the quick-resume thumbnail, or the save's own memory-card icon. */
static void sb_draw_art(double x,double y,double w,double h){
    if(page==SB_PAGE_QUICK&&selected>=0&&selected<SB_QUICK_SLOTS&&sb_quick[selected].used){
        wchar_t p[32768],rel[80];swprintf(rel,80,L"openbios\\quick\\state_8004032C_slot%02d.thumb",selected);sb_path(p,rel);size_t n=0;unsigned char*t=sb_read(p,&n,1<<20);
        if(t&&n>=12&&!memcmp(t,"PSTH",4)){unsigned tw=le32(t+4),th=le32(t+8);if(tw&&th&&tw<=512&&th<=512&&n>=12+(size_t)tw*th*4){
            fill(x,y,w,h,RGBX(0,0,0),255);double dw=h*4/3,dx=x+(w-dw)/2;int X=P(dx),Y=P(y);resample(X,Y,P(dx+dw)-X,P(y+h)-Y,(const uint32_t*)(t+12),(int)tw,0,0,(int)tw,(int)th,0,255);outline(x,y,w,h,C_STEEL,200);}}
        free(t);return;}
    int empty,i=page==SB_PAGE_SAVE?sb_cur:page==SB_PAGE_SAVES?sb_row_save(selected,&empty):-1;if(i<0||i>=sb_count)return;
    const unsigned char*b=sb_saves[i].data+128;fill(x,y,w,h,RGBX(1,6,24),235);double s=fmin(w,h)*.62/16,ix=x+(w-16*s)/2,iy=y+(h-16*s)/2;
    /* A Jail Break profile shows its rider's portrait (the game's own, from the
       disc; +0x0D/+0x0E as sb_profiles reads them), the card icon in a corner. */
    if(sb_saves[i].size>=128+SB_BLOCK&&!strncmp(sb_saves[i].name,SB_PRODUCT,12))for(int k=0;k<10;k++){const unsigned char*p=b+0x834+k*0x1E4;if(le32(p))continue;
        if(le32(p+0xDC)!=32)break;signed char na=(signed char)p[0x0D],nb=(signed char)p[0x0E];if(nb<0||nb>2)break;
        const RrArt*a=rr_art_get(RR_ART_SWITCH+(na==1?3:0)+nb);if(a&&a->pixels){cover(a->pixels,(int)a->w,(int)a->h,x,y,w,h);s=h*.28/16;ix=x+10;iy=y+h-16*s-10;}
        break;}
    for(int py=0;py<16;py++)for(int pxl=0;pxl<16;pxl++){unsigned v=b[0x80+py*8+pxl/2];v=pxl&1?v>>4:v&15;unsigned c=le16(b+0x60+v*2);if(!c)continue;
        uint32_t rgb=RGBX((c&31)*255/31,(c>>5&31)*255/31,(c>>10&31)*255/31);int X=P(ix+pxl*s),Y=P(iy+py*s);px_fill(X,Y,P(ix+(pxl+1)*s)-X,P(iy+(py+1)*s)-Y,rgb,255);}
    outline(x,y,w,h,C_STEEL,200);
}
/* ---- Controller ---------------------------------------------------------------- */
/* Pad 1 drives the launcher like the keyboard: D-pad / left stick move (with
 * repeat), A or Start select, B or Back go back, X deletes, LB / RB page (in
 * PlayStation terms Cross or Options, Circle or Share, Square, L1 / R1). XInput
 * pads and Sony pads (sony_pad.h, raw HID) are merged before presses are
 * counted, so one pad seen both ways (Steam Input) presses once. The kind of pad
 * that pressed last picks the names shown (pad_kind_used, launcher.c). */
static DWORD (WINAPI*sb_xinput_get)(DWORD,XINPUT_STATE*);static int sb_pad_used;static DWORD sb_pad_last;
static void sb_pad_poll(HWND window){
    static int loaded;if(!loaded){loaded=1;HMODULE x=LoadLibraryW(L"xinput1_4.dll");if(!x)x=LoadLibraryW(L"xinput9_1_0.dll");if(x)*(FARPROC*)&sb_xinput_get=GetProcAddress(x,"XInputGetState");}
    static WORD held;static DWORD repeat_at;static int dir_held;
    XINPUT_STATE st;SonyPadState ps;
    const int have_x=sb_xinput_get&&sb_xinput_get(0,&st)==ERROR_SUCCESS,have_ps=sony_poll(&ps);   /* the Sony reader keeps draining reports */
    if(GetForegroundWindow()!=window||(!have_x&&!have_ps)){held=0;dir_held=0;return;}
    WORD bx=have_x?st.Gamepad.wButtons:0,bp=have_ps?ps.buttons:0,b=bx|bp;
    short xlx=have_x?st.Gamepad.sThumbLX:0,xly=have_x?st.Gamepad.sThumbLY:0,plx=have_ps?ps.lx:0,ply=have_ps?ps.ly:0;
    short lx=abs(plx)>abs(xlx)?plx:xlx,ly=abs(ply)>abs(xly)?ply:xly;
    int dir=(b&XINPUT_GAMEPAD_DPAD_UP)||ly>16000?VK_UP:(b&XINPUT_GAMEPAD_DPAD_DOWN)||ly<-16000?VK_DOWN:(b&XINPUT_GAMEPAD_DPAD_LEFT)||lx<-16000?VK_LEFT:(b&XINPUT_GAMEPAD_DPAD_RIGHT)||lx>16000?VK_RIGHT:0;
    const int sony_moved=(bp&(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT))||abs(plx)>16000||abs(ply)>16000;
    DWORD now=GetTickCount();WORD pressed=(WORD)(b&~held);held=b;
    if(dir&&(dir!=dir_held||now>=repeat_at)){repeat_at=now+(dir!=dir_held?380:110);if(dir!=dir_held)pad_kind_used(sony_moved);dir_held=dir;sb_pad_used=1;sb_pad_last=now;SendMessageW(window,WM_KEYDOWN,dir,0);}else if(!dir)dir_held=0;
    const struct {WORD bit;WPARAM key;} map[]={{XINPUT_GAMEPAD_A,VK_RETURN},{XINPUT_GAMEPAD_START,VK_RETURN},{XINPUT_GAMEPAD_B,VK_ESCAPE},{XINPUT_GAMEPAD_BACK,VK_ESCAPE},{XINPUT_GAMEPAD_X,VK_DELETE},{XINPUT_GAMEPAD_LEFT_SHOULDER,VK_PRIOR},{XINPUT_GAMEPAD_RIGHT_SHOULDER,VK_NEXT}};
    for(int i=0;i<7;i++)if(pressed&map[i].bit){pad_kind_used((bp&map[i].bit)!=0);sb_pad_used=1;sb_pad_last=now;SendMessageW(window,WM_KEYDOWN,map[i].key,0);}
}
/* Footer keycaps follow the last device used: the keyboard's key, or the pad's
 * button for the kind of pad last used (Xbox-style letters or PlayStation symbols). */
static const wchar_t*sb_key(const wchar_t*keyboard,const wchar_t*xbox,const wchar_t*playstation){if(preview_pad>=0)return preview_pad?playstation:xbox;return sb_pad_used&&GetTickCount()-sb_pad_last<60000?(pad_ps?playstation:xbox):keyboard;}
#endif
