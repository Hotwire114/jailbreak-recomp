/* Independently authored Discord IPC client. LGPL-2.1-or-later.
 * All pipe I/O runs on a worker with cancellable, bounded waits. Only coarse
 * menu/race/pause activity is sent; rider names, saves and paths stay local.
 * Protocol: https://github.com/discord/discord-rpc/blob/master/documentation/hard-mode.md
 */
#include "discord_presence.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
namespace {
HANDLE worker,stop_event; std::atomic<int> context{0},status{0}; char app_id[24];
// 0 disabled, 1 looking, 2 handshaking, 3 connected, 4 rejected
bool transfer(HANDLE pipe,void* data,DWORD size,bool write){
    auto*p=static_cast<unsigned char*>(data);
    while(size){
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ov.hEvent)return false;
        DWORD got=0;BOOL ok=write?WriteFile(pipe,p,size,&got,&ov):ReadFile(pipe,p,size,&got,&ov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){
            HANDLE events[]={stop_event,ov.hEvent};DWORD wait=WaitForMultipleObjects(2,events,FALSE,250);
            if(wait==WAIT_OBJECT_0+1)ok=GetOverlappedResult(pipe,&ov,&got,FALSE);
            else{CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&got,TRUE);ok=FALSE;}
        }
        CloseHandle(ov.hEvent);if(!ok||!got)return false;p+=got;size-=got;
    }return true;
}
bool send(HANDLE pipe,unsigned opcode,const char*data,unsigned len){
    unsigned header[]={opcode,len};return transfer(pipe,header,8,true)&&transfer(pipe,(void*)data,len,true);
}
bool receive(HANDLE pipe,unsigned&opcode,char*data,unsigned cap){
    unsigned h[2];if(!transfer(pipe,h,8,false)||h[1]>=cap)return false;opcode=h[0];
    if(h[1]&&!transfer(pipe,data,h[1],false))return false;data[h[1]]=0;return true;
}
DWORD WINAPI run(void*){
    const auto started=static_cast<long long>(std::time(nullptr));unsigned nonce=0;
    while(WaitForSingleObject(stop_event,0)==WAIT_TIMEOUT){
        status=1;HANDLE pipe=INVALID_HANDLE_VALUE;
        for(int i=0;i<10;i++){wchar_t name[80];
#ifdef RR_PRESENCE_TEST
            std::swprintf(name,80,L"\\\\.\\pipe\\rr-presence-test-%d",i);
#else
            std::swprintf(name,80,L"\\\\.\\pipe\\discord-ipc-%d",i);
#endif
            pipe=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(pipe!=INVALID_HANDLE_VALUE)break;}
        if(pipe==INVALID_HANDLE_VALUE){if(WaitForSingleObject(stop_event,5000)!=WAIT_TIMEOUT)break;continue;}
        status=2;char text[4096];unsigned op=0;
        std::snprintf(text,sizeof text,"{\"v\":1,\"client_id\":\"%s\"}",app_id);
        bool ok=send(pipe,0,text,(unsigned)std::strlen(text));
        // Discord may take a moment to dispatch READY. Never block the game.
        unsigned waited=0;DWORD available=0;
        while(ok&&waited<5000){
            if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)){ok=false;break;}
            if(available>=8)break;
            if(WaitForSingleObject(stop_event,50)!=WAIT_TIMEOUT){ok=false;break;}waited+=50;
        }
        ok=ok&&available>=8&&receive(pipe,op,text,sizeof text)&&op==1&&std::strstr(text,"READY");
        int previous=-1;ULONGLONG last=0;
        while(ok&&WaitForSingleObject(stop_event,100)==WAIT_TIMEOUT){
            int current=context.load();ULONGLONG now=GetTickCount64();
            if(current!=previous||now-last>15000){
                const char*state=current==1?"On the road":current==2?"Race paused":"In the main menu";
                std::snprintf(text,sizeof text,"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"details\":\"Road Rash: Jailbreak - Native PC\",\"state\":\"%s\",\"timestamps\":{\"start\":%lld},\"instance\":false}},\"nonce\":\"rr-%u\"}",GetCurrentProcessId(),state,started,++nonce);
                ok=send(pipe,1,text,(unsigned)std::strlen(text));previous=current;last=now;
            }
            DWORD available_bytes=0;
            if(ok&&!PeekNamedPipe(pipe,nullptr,0,nullptr,&available_bytes,nullptr))ok=false;
            if(ok&&available_bytes>=8){
                ok=receive(pipe,op,text,sizeof text);
                if(ok&&op==3)ok=send(pipe,4,text,(unsigned)std::strlen(text));
                else if(ok&&(op==2||std::strstr(text,"\"ERROR\""))){status=4;ok=false;}
                else if(ok&&op==1&&std::strstr(text,"SET_ACTIVITY"))status=3;
            }
        }
        if(status==3){
            // Best-effort clear. Closing the pipe also ends the session.
            std::snprintf(text,sizeof text,"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":null},\"nonce\":\"rr-clear\"}",GetCurrentProcessId());
            send(pipe,1,text,(unsigned)std::strlen(text));
        }
        CloseHandle(pipe);if(WaitForSingleObject(stop_event,5000)!=WAIT_TIMEOUT)break;
    }status=0;return 0;
}
}
namespace rrpc {
bool presence_start(const char*id){
    if(worker)return true;size_t n=id?std::strlen(id):0;if(n<17||n>20)return false;
    for(size_t i=0;i<n;i++)if(id[i]<'0'||id[i]>'9')return false;
    std::strcpy(app_id,id);stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop_event)return false;
    worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);if(!worker){CloseHandle(stop_event);stop_event=nullptr;return false;}return true;
}
void presence_context(int value){context=value;}
void presence_stop(){if(!worker)return;SetEvent(stop_event);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);CloseHandle(stop_event);worker=stop_event=nullptr;status=0;}
const char* presence_status(){switch(status.load()){case 1:return "Waiting for Discord";case 2:return "Connecting";case 3:return "Connected";case 4:return "App ID rejected";default:return "Off";}}
}
