/* Bug reports: one ZIP with a screenshot, the recent logs, launcher.ini and
 * the tuning file, plus a note. LGPL-2.1-or-later. Original author code.
 *
 * Entries are stored (no compression): the screenshot is a PNG already and
 * each log is cut to its last RR_REPORT_LOG_MAX bytes, so a report stays small
 * without a deflate implementation. Any ZIP tool opens it. */
#ifndef RR_REPORT_ZIP_H
#define RR_REPORT_ZIP_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RR_REPORT_LOG_MAX (2u*1024u*1024u)
#define RR_REPORT_MAX_ENTRIES 24
static inline unsigned rrz_crc32(const unsigned char*p,size_t n){
    static unsigned table[256];static int ready;
    if(!ready){for(unsigned i=0;i<256;i++){unsigned c=i;for(int k=0;k<8;k++)c=c&1?0xEDB88320u^(c>>1):c>>1;table[i]=c;}ready=1;}
    unsigned c=0xFFFFFFFFu;for(size_t i=0;i<n;i++)c=table[(c^p[i])&255]^(c>>8);return c^0xFFFFFFFFu;
}
typedef struct RrZip {FILE*f;unsigned count;struct {char name[64];unsigned crc,size,offset;} e[RR_REPORT_MAX_ENTRIES];unsigned short dtime,ddate;int failed;} RrZip;
static inline void rrz_u16(FILE*f,unsigned v){fputc((int)(v&255),f);fputc((int)(v>>8&255),f);}
static inline void rrz_u32(FILE*f,unsigned v){rrz_u16(f,v&0xFFFF);rrz_u16(f,v>>16);}
static inline int rrz_open(RrZip*z,const wchar_t*path){
    memset(z,0,sizeof *z);z->f=_wfopen(path,L"wb");if(!z->f)return 0;
    SYSTEMTIME t;GetLocalTime(&t);z->dtime=(unsigned short)(t.wHour<<11|t.wMinute<<5|t.wSecond/2);z->ddate=(unsigned short)((t.wYear-1980)<<9|t.wMonth<<5|t.wDay);return 1;
}
/* One stored entry; name is ASCII without folders. */
static inline int rrz_add(RrZip*z,const char*name,const void*data,size_t size){
    if(!z->f||z->count>=RR_REPORT_MAX_ENTRIES||size>0x7FFFFFFFu)return 0;
    size_t n=strlen(name);if(!n||n>=sizeof z->e[0].name)return 0;
    unsigned crc=rrz_crc32((const unsigned char*)data,size),off=(unsigned)ftell(z->f);
    rrz_u32(z->f,0x04034b50u);rrz_u16(z->f,10);rrz_u16(z->f,0);rrz_u16(z->f,0);rrz_u16(z->f,z->dtime);rrz_u16(z->f,z->ddate);
    rrz_u32(z->f,crc);rrz_u32(z->f,(unsigned)size);rrz_u32(z->f,(unsigned)size);rrz_u16(z->f,(unsigned)n);rrz_u16(z->f,0);
    fwrite(name,1,n,z->f);if(size&&fwrite(data,1,size,z->f)!=size)z->failed=1;
    memcpy(z->e[z->count].name,name,n+1);z->e[z->count].crc=crc;z->e[z->count].size=(unsigned)size;z->e[z->count].offset=off;z->count++;return !z->failed;
}
/* A file from disk, only its last `tail` bytes (0 = all). Missing files are
 * skipped; a log still being written is read shared. */
static inline int rrz_add_file(RrZip*z,const char*name,const wchar_t*path,unsigned tail){
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER size;if(!GetFileSizeEx(h,&size)||size.QuadPart>0x40000000){CloseHandle(h);return 0;}
    unsigned long long start=0,len=(unsigned long long)size.QuadPart;if(tail&&len>tail){start=len-tail;len=tail;}
    unsigned char*b=(unsigned char*)malloc(len?(size_t)len:1);if(!b){CloseHandle(h);return 0;}
    LARGE_INTEGER at;at.QuadPart=(LONGLONG)start;DWORD got=0;int ok=SetFilePointerEx(h,at,NULL,FILE_BEGIN)&&(len==0||ReadFile(h,b,(DWORD)len,&got,NULL));CloseHandle(h);
    if(ok)ok=rrz_add(z,name,b,got);free(b);return ok;
}
static inline int rrz_close(RrZip*z){
    if(!z->f)return 0;unsigned dir=(unsigned)ftell(z->f),dsize=0;
    for(unsigned i=0;i<z->count;i++){size_t n=strlen(z->e[i].name);
        rrz_u32(z->f,0x02014b50u);rrz_u16(z->f,20);rrz_u16(z->f,10);rrz_u16(z->f,0);rrz_u16(z->f,0);rrz_u16(z->f,z->dtime);rrz_u16(z->f,z->ddate);
        rrz_u32(z->f,z->e[i].crc);rrz_u32(z->f,z->e[i].size);rrz_u32(z->f,z->e[i].size);rrz_u16(z->f,(unsigned)n);rrz_u16(z->f,0);rrz_u16(z->f,0);rrz_u16(z->f,0);rrz_u16(z->f,0);rrz_u32(z->f,0);rrz_u32(z->f,z->e[i].offset);
        fwrite(z->e[i].name,1,n,z->f);dsize+=46+(unsigned)n;}
    rrz_u32(z->f,0x06054b50u);rrz_u16(z->f,0);rrz_u16(z->f,0);rrz_u16(z->f,z->count);rrz_u16(z->f,z->count);rrz_u32(z->f,dsize);rrz_u32(z->f,dir);rrz_u16(z->f,0);
    int ok=!z->failed&&!ferror(z->f);if(fclose(z->f))ok=0;z->f=NULL;return ok;
}
/* "Windows 10.0.26200" or, under Wine/Proton, "Wine 9.0 on Linux 6.5.0":
 * Linux reports then show the layer the game ran on. */
static inline void rr_report_system(char*out,size_t cap){
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    typedef const char*(__cdecl*WineVersion)(void);typedef void(__cdecl*WineHost)(const char**,const char**);typedef LONG(WINAPI*RtlVersion)(OSVERSIONINFOW*);
    WineVersion wine=nt?(WineVersion)(void*)GetProcAddress(nt,"wine_get_version"):NULL;
    WineHost host=nt?(WineHost)(void*)GetProcAddress(nt,"wine_get_host_version"):NULL;
    RtlVersion rtl=nt?(RtlVersion)(void*)GetProcAddress(nt,"RtlGetVersion"):NULL;
    if(wine){const char*sys="?",*rel="?";if(host)host(&sys,&rel);snprintf(out,cap,"Wine %s on %s %s",wine(),sys?sys:"?",rel?rel:"?");return;}
    OSVERSIONINFOW v;memset(&v,0,sizeof v);v.dwOSVersionInfoSize=sizeof v;
    if(rtl&&rtl(&v)==0)snprintf(out,cap,"Windows %lu.%lu.%lu",v.dwMajorVersion,v.dwMinorVersion,v.dwBuildNumber);else snprintf(out,cap,"Windows");
}
/* Writes store\reports\report-YYYYMMDD-HHMMSS-<source>.zip. store is the
 * installation root (launcher.ini there, the runtime in store\runtime).
 * png_path may be NULL; note and context are UTF-8. */
static inline int rr_report_write(const wchar_t*store,const char*source,const wchar_t*png_path,const char*note,const char*context,wchar_t*out,size_t out_cap){
    wchar_t path[32768],file[32768];swprintf(file,32768,L"%ls\\reports",store);CreateDirectoryW(file,NULL);
    SYSTEMTIME t;GetLocalTime(&t);
    swprintf(path,32768,L"%ls\\report-%04u%02u%02u-%02u%02u%02u-%hs.zip",file,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,source);
    RrZip z;if(!rrz_open(&z,path))return 0;
    size_t cap=strlen(note?note:"")+strlen(context?context:"")+512;char*text=(char*)malloc(cap);
    char sys[160];rr_report_system(sys,sizeof sys);
    if(text){int n=snprintf(text,cap,"Road Rash: Jailbreak bug report\r\nWritten: %04u-%02u-%02u %02u:%02u:%02u from the %s\r\nSystem: %s\r\n\r\nNote:\r\n%s\r\n\r\n%s",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,source,sys,note&&*note?note:"(none)",context?context:"");
        if(n>0)rrz_add(&z,"note.txt",text,(size_t)(n<(int)cap?n:(int)cap-1));free(text);}
    if(png_path)rrz_add_file(&z,"screenshot.png",png_path,0);
    static const struct {const wchar_t*rel;const char*name;unsigned tail;} files[]={
        {L"runtime\\psx_crash.txt","psx_crash.txt",RR_REPORT_LOG_MAX},{L"runtime\\psx_last_run_report.json","psx_last_run_report.json",RR_REPORT_LOG_MAX},
        {L"runtime\\rr_native_gaps.log","rr_native_gaps.log",RR_REPORT_LOG_MAX},{L"runtime.log","runtime.log",RR_REPORT_LOG_MAX},
        {L"launcher.ini","launcher.ini",0},{L"tuning.ini","tuning.ini",0},{L"tuning-changes.log","tuning-changes.log",RR_REPORT_LOG_MAX},
        {L"tuning-problems.txt","tuning-problems.txt",0},{L"game.toml","game.toml",0},{L"local-build.json","local-build.json",0},
        {L"setup.log","setup.log",RR_REPORT_LOG_MAX},{L"build.log","build.log",RR_REPORT_LOG_MAX},
        {L"build-online.log","build-online.log",RR_REPORT_LOG_MAX}};
    for(size_t i=0;i<sizeof files/sizeof files[0];i++){swprintf(file,32768,L"%ls\\%ls",store,files[i].rel);rrz_add_file(&z,files[i].name,file,files[i].tail);}
    int ok=rrz_close(&z);if(!ok)DeleteFileW(path);
    if(out&&out_cap){wcsncpy(out,path,out_cap-1);out[out_cap-1]=0;}
    return ok;
}
#endif
