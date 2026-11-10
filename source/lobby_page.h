/* Multiplayer > Play online: rooms of up to 8 players by room code (pages 28 and 29).
 * LGPL-2.1-or-later. Original author code.
 *
 * Rooms need no server of ours (strangers can also find them on the public
 * lobby, "Public rooms" below, with Discord sign-in). The host opens a room on its port (TCP for the
 * room, UDP for the race, the same number). The launcher asks the router to
 * forward both (UPnP, Windows' own NAT helper); without UPnP the host forwards
 * the port by hand. The room code is the host's public address and port;
 * friends on the same network use the local one. Joiners type the code.
 *
 * Room protocol (one text line each, strings hex-encoded UTF-8):
 *   joiner -> host   HELLO <fingerprint> <name> <settings message>, READY 0|1, CHAT <text>, BYE
 *   host -> joiner   REFUSE <reason>, ROOM <size> <you>, M <ready> <host> <name> (one per member), END,
 *                    CHAT <from> <text>, START <slot> <slots> <session id> <host message> <seat 2 message>
 * The settings messages are the Direct connection's (net_message): the build
 * fingerprint, each player's Player 1 name, voice and rider look, and from the
 * host the settings that change game state. Every PC writes the same session
 * file (seat 1 = host, seat 2 = the first joiner) and starts the framework's
 * delay-sync session: the host runs the hub on its UDP port, every joiner
 * dials it (psx_netplay.c host relay for 3+ seats).
 *
 * Seats: 1 = Player 1 (host), 2 = Player 2, 3-8 take over AI riders in
 * Skull-to-Skull (rider_takeover.cpp), with their own view on their PC. */
#define LB_ROOMS_PAGE 28
#define LB_ROOM_PAGE 29
#define LB_BROWSE_PAGE 38
#define LB_BIKES_PAGE 39   /* Bikes: one per rank, from Play online or a room (lb_bikes_from) */
#define LB_MAX 8
typedef struct {SOCKET s;char rx[8192];size_t rx_len;int hello,ready,vote;wchar_t name[64];char msg[2048];} LbPeer;   /* vote: 0 none, 1-3 */
typedef struct {int ready,host,me,mmr;wchar_t name[64];} LbMember;
typedef struct {wchar_t from[64],text[256];} LbChat;
static struct {
    int active,host,joined,ready,size,you,members,chats,connecting;
    LbMember member[LB_MAX];LbChat chat[24];
    wchar_t status[200],code[96],local_code[96];int code_state;   /* 0 looking, 1 port opened (UPnP), 2 forward by hand */
    char host_ip[96];int host_port;DWORD started;
} lb;
static SOCKET lb_listen=INVALID_SOCKET,lb_sock=INVALID_SOCKET;static LbPeer lb_peer[LB_MAX-1];
static char lb_rx[16384];static size_t lb_rx_len;static int lb_size=4;
/* Tests (--room-host N / --room-join CODE): start once N are in and ready; joiners mark ready. */
static int lb_test_autostart,lb_test_autoready;
static volatile LONG lb_upnp_done;static wchar_t lb_upnp_code[96];static int lb_upnp_mapped,lb_upnp_port;
/* The running game shows these pages in its own menus (online_link_host.h): its
   questions replace the launcher's dialogs, and a race starts once it has closed. */
static int ol_active,ol_launch_pending,ol_remove_index;
static void ol_ask(int kind,int yesno,const wchar_t*title,const wchar_t*caption,const wchar_t*value,int max);
/* ---- Rating (MMR) ------------------------------------------------------------
 * Online room races only. Everyone starts at 1000; 1-9999. After each race every
 * player is scored against every other player in it (Elo): beating a player is
 * worth 1, losing 0, against what the two ratings expect (1 / (1 + 10^((theirs -
 * yours) / 400))), 32 points a pair. So high places earn the most, a lower-rated
 * player gains even from a middle place while a higher-rated one loses there,
 * and bigger rooms carry more at stake. AI riders don't count.
 * There is no server: each PC keeps its own rating (launcher.ini [online] mmr)
 * and sends it with its settings (net_message "mmr"); the host puts every seat's
 * rating in START. The online game writes each race's order (the riders'
 * course distance, netplay_session.cpp race_results_tick) beside the session
 * file; when it closes, lb_mmr_apply scores the races in order. */
/* ---- Race vote and bikes ------------------------------------------------------
 * Start the race (host, everyone ready) first opens a vote: three random
 * courses, each with its rank (race id 1-18: rank r = (id-1)/6, Grunt /
 * Lieutenant / Captain), 15 seconds, one vote each; most votes wins, ties at
 * random. The winner goes to everyone in the host's settings (race, seed) and
 * the online game holds it on the setup screens (netplay_session.cpp
 * setup_tick), later races of the session getting a new random one. Each
 * player keeps a bike per rank (launcher.ini [online] bike_r0..2, -1 Random),
 * sent with their settings; Random is one of that rank's four bikes. */
static const int lb_rank_bikes[3][4]={{0,3,9,12},{1,4,10,13},{2,5,11,14}};
static int lb_bike_pref[3]={-2,-2,-2};
static int lb_bike(int r){if(lb_bike_pref[0]==-2)for(int i=0;i<3;i++){wchar_t k[12],v[12];swprintf(k,12,L"bike_r%d",i);GetPrivateProfileStringW(L"online",k,L"-1",v,12,ini);lb_bike_pref[i]=_wtoi(v);}return r>=0&&r<3?lb_bike_pref[r]:-1;}
static void lb_bike_step(int r,int dir){lb_bike(0);int at=-1;for(int k=0;k<4;k++)if(lb_rank_bikes[r][k]==lb_bike_pref[r])at=k;at=((at+1+dir)%5+5)%5-1;lb_bike_pref[r]=at<0?-1:lb_rank_bikes[r][at];
    wchar_t k[12],v[12];swprintf(k,12,L"bike_r%d",r);swprintf(v,12,L"%d",lb_bike_pref[r]);WritePrivateProfileStringW(L"online",k,v,ini);}
static void lb_bike_name(int b,wchar_t*out,size_t cap){char n[48]="";if(b<0){wcsncpy(out,L"Random",cap);return;}if(installed&&rr_fe_string(discfile,1006+3*b,n,sizeof n))swprintf(out,cap,L"%hs",n);else swprintf(out,cap,L"Bike %d",b+1);}
static const wchar_t*lb_rank_name(int r){return r==0?L"Grunt":r==1?L"Lieutenant":L"Captain";}
static void lb_race_name(int race,wchar_t*out,size_t cap){char n[48]="";if(installed&&rr_course_name(discfile,race,2,n,sizeof n))swprintf(out,cap,L"%hs",n);else swprintf(out,cap,L"Course %d",race);}
static struct {int active,option[3],tally[3],mine;DWORD started;} lb_vote;
static int lb_voted_race;static unsigned lb_seed;
static int lb_vote_rows(void){return lb_vote.active?3:0;}
static int lb_bikes_from=28,lb_bikes_row;   /* Bikes page: where Back returns to, and its row there */
#define MMR_START 1000
#define MMR_K 32.0
static int mmr_value=-1,mmr_last,mmr_races,mmr_pending,mmr_slot,mmr_slots,room_mmr[LB_MAX];
static int mmr_get(void){
    if(mmr_value<0){wchar_t v[16];mmr_value=(int)GetPrivateProfileIntW(L"online",L"mmr",MMR_START,ini);if(mmr_value<1||mmr_value>9999)mmr_value=MMR_START;
        GetPrivateProfileStringW(L"online",L"mmr_last",L"0",v,16,ini);mmr_last=_wtoi(v);mmr_races=(int)GetPrivateProfileIntW(L"online",L"mmr_races",0,ini);}
    return mmr_value;}
static void mmr_save(void){wchar_t v[16];swprintf(v,16,L"%d",mmr_value);WritePrivateProfileStringW(L"online",L"mmr",v,ini);swprintf(v,16,L"%d",mmr_last);WritePrivateProfileStringW(L"online",L"mmr_last",v,ini);swprintf(v,16,L"%d",mmr_races);WritePrivateProfileStringW(L"online",L"mmr_races",v,ini);}
/* One race: dist[] the seats' course distances (further = better place). */
static void mmr_score(int n,const long*dist,int*rating){
    int next[LB_MAX];
    for(int i=0;i<n;i++){double d=0;for(int j=0;j<n;j++){if(j==i)continue;const unsigned long a=(unsigned long)dist[i],b=(unsigned long)dist[j];double s=a>b?1:a<b?0:.5,   /* the distance is unsigned (it reads negative past 2^31) */e=1.0/(1.0+pow(10.0,(rating[j]-rating[i])/400.0));d+=MMR_K*(s-e);}
        int r=rating[i]+(int)floor(d+.5);next[i]=r<1?1:r>9999?9999:r;}
    memcpy(rating,next,sizeof(int)*n);}
static void lb_results_path(wchar_t*out){swprintf(out,32768,L"%ls.results",online_session);}
/* The online game has closed: its races, scored. */
static void lb_mmr_apply(void){
    if(!mmr_pending)return;mmr_pending=0;mmr_get();
    wchar_t path[32768];lb_results_path(path);FILE*f=_wfopen(path,L"rb");if(!f)return;
    const int before=room_mmr[mmr_slot];char line[512];int races=0;
    while(fgets(line,sizeof line,f)){long dist[8];int mode;if(sscanf(line,"race %d %ld %ld %ld %ld %ld %ld %ld %ld",&mode,dist,dist+1,dist+2,dist+3,dist+4,dist+5,dist+6,dist+7)!=9)continue;
        if(mmr_slots>=2&&mmr_slots<=8){mmr_score(mmr_slots,dist,room_mmr);races++;}}
    fclose(f);DeleteFileW(path);if(!races)return;
    mmr_last=room_mmr[mmr_slot]-before;mmr_value=room_mmr[mmr_slot];mmr_races+=races;mmr_save();
    wchar_t t[120];swprintf(t,120,L"ONLINE RATING %d (%ls%d)",mmr_value,mmr_last>=0?L"+":L"",mmr_last);notice(t);}

static void lb_hex(const wchar_t*w,char*out,size_t cap){
    char u[2400];int n=WideCharToMultiByte(CP_UTF8,0,w,-1,u,sizeof u,NULL,NULL);if(n<=1){snprintf(out,cap,"-");return;}
    size_t o=0;for(int i=0;i<n-1&&o+3<cap;i++)o+=(size_t)snprintf(out+o,cap-o,"%02x",(unsigned char)u[i]);out[o]=0;}
static void lb_hexa(const char*s,char*out,size_t cap){size_t o=0;if(!*s){snprintf(out,cap,"-");return;}for(;*s&&o+3<cap;s++)o+=(size_t)snprintf(out+o,cap-o,"%02x",(unsigned char)*s);out[o]=0;}
static void lb_unhexa(const char*h,char*out,size_t cap){size_t n=0;if(strcmp(h,"-"))for(;h[0]&&h[1]&&n+1<cap;h+=2){unsigned v;if(sscanf(h,"%2x",&v)!=1)break;out[n++]=(char)v;}out[n]=0;}
static void lb_unhex(const char*h,wchar_t*out,int cap){char u[2400];lb_unhexa(h,u,sizeof u);if(!MultiByteToWideChar(CP_UTF8,0,u,-1,out,cap))out[0]=0;}
static int lb_send_to(SOCKET s,const char*line){if(s==INVALID_SOCKET)return 0;char m[8192];int n=snprintf(m,sizeof m,"%s\n",line);
    u_long nb=0;ioctlsocket(s,FIONBIO,&nb);int ok=send(s,m,n,0)==n;nb=1;ioctlsocket(s,FIONBIO,&nb);return ok;}
/* Swear filter for chat and names, applied on the PC that shows them: a word
 * (after 0->o, 1->i, 3->e, 4->a, 5->s, @->a, $->s) that starts with a stem, or is
 * one of the short words, keeps its first letter and the rest become *. */
static const char*const lb_bad_stems[]={"fuck","fuk","shit","cunt","bitch","wank","twat","slut","whore","nigg","nigga","fagg","retard","motherf","dickhead","bollock","asshole","arsehole","bastard","pussy","bellend","spastic","tosser","dildo",NULL};
static const char*const lb_bad_words[]={"fag","fags","dick","dicks","cock","cocks","arse","prick","pricks","piss","tits","knob","nonce","spaz","kys",NULL};
static void lb_clean(wchar_t*t){
    for(wchar_t*p=t;*p;){if(!(iswalnum(*p)||*p==L'@'||*p==L'$')){p++;continue;}
        wchar_t*w=p;char n[40];int k=0;
        for(;*p&&(iswalnum(*p)||*p==L'@'||*p==L'$');p++)if(k<39){wchar_t c=towlower(*p);n[k++]=(char)(c==L'0'?'o':c==L'1'?'i':c==L'3'?'e':c==L'4'?'a':c==L'5'||c==L'$'?'s':c==L'@'?'a':c<128?c:'?');}
        n[k]=0;
        int bad=0;for(int i=0;lb_bad_stems[i]&&!bad;i++)bad=!strncmp(n,lb_bad_stems[i],strlen(lb_bad_stems[i]));
        for(int i=0;lb_bad_words[i]&&!bad;i++)bad=!strcmp(n,lb_bad_words[i]);
        if(bad)for(wchar_t*q=w+1;q<p;q++)*q=L'*';}}
static void lb_add_chat(const wchar_t*from,const wchar_t*text){if(lb.chats==24){memmove(lb.chat,lb.chat+1,23*sizeof lb.chat[0]);lb.chats--;}wcsncpy(lb.chat[lb.chats].from,from,63);lb.chat[lb.chats].from[63]=0;wcsncpy(lb.chat[lb.chats].text,text,255);lb.chat[lb.chats].text[255]=0;
    lb_clean(lb.chat[lb.chats].from);lb_clean(lb.chat[lb.chats].text);lb.chats++;}

/* ---- Router port (UPnP through Windows' NATUPnP COM object) ---------------- */
typedef struct {HRESULT (STDMETHODCALLTYPE*QueryInterface)(void*,REFIID,void**);ULONG (STDMETHODCALLTYPE*AddRef)(void*);ULONG (STDMETHODCALLTYPE*Release)(void*);
    void*ti_count,*ti,*ids,*invoke;HRESULT (STDMETHODCALLTYPE*get_Static)(void*,void**);} LbNatVtbl;
typedef struct {HRESULT (STDMETHODCALLTYPE*QueryInterface)(void*,REFIID,void**);ULONG (STDMETHODCALLTYPE*AddRef)(void*);ULONG (STDMETHODCALLTYPE*Release)(void*);
    void*ti_count,*ti,*ids,*invoke,*new_enum,*item,*count;HRESULT (STDMETHODCALLTYPE*Remove)(void*,long,BSTR);
    HRESULT (STDMETHODCALLTYPE*Add)(void*,long,BSTR,long,BSTR,VARIANT_BOOL,BSTR,void**);} LbMapsVtbl;
typedef struct {HRESULT (STDMETHODCALLTYPE*QueryInterface)(void*,REFIID,void**);ULONG (STDMETHODCALLTYPE*AddRef)(void*);ULONG (STDMETHODCALLTYPE*Release)(void*);
    void*ti_count,*ti,*ids,*invoke;HRESULT (STDMETHODCALLTYPE*get_ExternalIP)(void*,BSTR*);} LbMapVtbl;
typedef struct {const void*v;} LbCom;
static const GUID lb_clsid_nat={0xAE1E00AA,0x3FD5,0x403C,{0x8A,0x27,0x2B,0xBD,0xC3,0x0C,0xD0,0xE1}};
static const GUID lb_iid_nat={0xB171C812,0xCC76,0x485A,{0x94,0xD8,0xB6,0xB3,0xA2,0x79,0x4E,0x99}};
static int lb_local_ip(char*out,size_t cap){
    SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(53);inet_pton(AF_INET,"8.8.8.8",&a.sin_addr);int len=sizeof a,ok=0;
    if(s!=INVALID_SOCKET&&!connect(s,(struct sockaddr*)&a,sizeof a)&&!getsockname(s,(struct sockaddr*)&a,&len))ok=inet_ntop(AF_INET,&a.sin_addr,out,cap)!=NULL;
    if(s!=INVALID_SOCKET)closesocket(s);return ok;}
static LbCom*lb_maps(void){
    LbCom*nat=NULL,*maps=NULL;if(FAILED(CoCreateInstance(&lb_clsid_nat,NULL,CLSCTX_INPROC_SERVER,&lb_iid_nat,(void**)&nat))||!nat)return NULL;
    ((const LbNatVtbl*)nat->v)->get_Static(nat,(void**)&maps);((const LbNatVtbl*)nat->v)->Release(nat);return maps;}
/* The public address without UPnP: ask a plain what-is-my-IP service once. */
static int lb_public_ip(char*out,size_t cap){
    int ok=0;HINTERNET s=WinHttpOpen(L"RoadRashJailbreak",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,NULL,NULL,0);if(!s)return 0;
    WinHttpSetTimeouts(s,4000,4000,4000,4000);HINTERNET c=WinHttpConnect(s,L"api.ipify.org",INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET r=c?WinHttpOpenRequest(c,L"GET",L"/",NULL,NULL,NULL,WINHTTP_FLAG_SECURE):NULL;
    if(r&&WinHttpSendRequest(r,NULL,0,NULL,0,0,0)&&WinHttpReceiveResponse(r,NULL)){DWORD n=0;char b[64]={0};if(WinHttpReadData(r,b,sizeof b-1,&n)&&n>6&&n<sizeof b){b[n]=0;size_t k=strspn(b,"0123456789.");b[k]=0;if(k>6){snprintf(out,cap,"%s",b);ok=1;}}}
    if(r)WinHttpCloseHandle(r);if(c)WinHttpCloseHandle(c);WinHttpCloseHandle(s);return ok;}
static unsigned __stdcall lb_upnp_worker(void*arg){
    int port=(int)(intptr_t)arg;CoInitializeEx(NULL,COINIT_MULTITHREADED);char local[64]="",ext[64]="";wchar_t wl[64];int mapped=0;
    if(lb_local_ip(local,sizeof local)){MultiByteToWideChar(CP_UTF8,0,local,-1,wl,64);LbCom*maps=lb_maps();
        if(maps){BSTR desc=SysAllocString(L"Road Rash Jailbreak online"),client=SysAllocString(wl);const wchar_t*proto[]={L"TCP",L"UDP"};
            for(int i=0;i<2;i++){BSTR p=SysAllocString(proto[i]);LbCom*m=NULL;
                if(SUCCEEDED(((const LbMapsVtbl*)maps->v)->Add(maps,port,p,port,client,VARIANT_TRUE,desc,(void**)&m))&&m){mapped++;
                    BSTR ip=NULL;if(!ext[0]&&SUCCEEDED(((const LbMapVtbl*)m->v)->get_ExternalIP(m,&ip))&&ip){WideCharToMultiByte(CP_UTF8,0,ip,-1,ext,sizeof ext,NULL,NULL);SysFreeString(ip);}
                    ((const LbMapVtbl*)m->v)->Release(m);}
                SysFreeString(p);}
            SysFreeString(desc);SysFreeString(client);((const LbMapsVtbl*)maps->v)->Release(maps);}}
    if(!ext[0]||!strcmp(ext,"0.0.0.0"))lb_public_ip(ext,sizeof ext);
    if(ext[0])swprintf(lb_upnp_code,96,L"%hs:%d",ext,port);else lb_upnp_code[0]=0;
    lb_upnp_mapped=mapped==2;lb_upnp_port=mapped?port:0;CoUninitialize();InterlockedExchange(&lb_upnp_done,1);return 0;}
static void lb_unmap(void){
    if(!lb_upnp_port)return;CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);LbCom*maps=lb_maps();
    if(maps){const wchar_t*proto[]={L"TCP",L"UDP"};for(int i=0;i<2;i++){BSTR p=SysAllocString(proto[i]);((const LbMapsVtbl*)maps->v)->Remove(maps,lb_upnp_port,p);SysFreeString(p);}((const LbMapsVtbl*)maps->v)->Release(maps);}
    lb_upnp_port=0;}

/* ---- Room ------------------------------------------------------------------- */
static void lb_reset(void){
    if(lb_sock!=INVALID_SOCKET)closesocket(lb_sock);if(lb_listen!=INVALID_SOCKET)closesocket(lb_listen);lb_sock=lb_listen=INVALID_SOCKET;
    for(int i=0;i<LB_MAX-1;i++){if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].s)closesocket(lb_peer[i].s);memset(lb_peer+i,0,sizeof lb_peer[i]);lb_peer[i].s=INVALID_SOCKET;}
    memset(&lb,0,sizeof lb);lb_rx_len=0;}
static int lb_guest_count(void){int n=0;for(int i=0;i<LB_MAX-1;i++)n+=lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello;return n;}
/* Host: the member table from the host and the joined peers, sent to everyone. */
static void lb_broadcast_room(void){
    lb.members=0;LbMember*m=lb.member+lb.members++;m->ready=1;m->host=1;m->me=1;m->mmr=mmr_get();wcsncpy(m->name,rider_names[0],63);
    for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello){m=lb.member+lb.members++;m->ready=lb_peer[i].ready;m->host=0;m->me=0;m->mmr=net_int(lb_peer[i].msg,"mmr",MMR_START);wcsncpy(m->name,lb_peer[i].name,63);}
    char line[512],name[200];int you=1;
    for(int i=0;i<LB_MAX-1;i++){if(lb_peer[i].s==INVALID_SOCKET||!lb_peer[i].hello)continue;
        snprintf(line,sizeof line,"ROOM %d %d",lb.size,you++);lb_send_to(lb_peer[i].s,line);
        for(int k=0;k<lb.members;k++){lb_hex(lb.member[k].name,name,sizeof name);snprintf(line,sizeof line,"M %d %d %s %d",lb.member[k].ready,lb.member[k].host,name,lb.member[k].mmr);lb_send_to(lb_peer[i].s,line);}
        lb_send_to(lb_peer[i].s,"END");}}
static void lb_broadcast_chat(const wchar_t*from,const wchar_t*text){
    lb_add_chat(from,text);char a[200],b[1200],line[1500];lb_hex(from,a,sizeof a);lb_hex(text,b,sizeof b);snprintf(line,sizeof line,"CHAT %s %s",a,b);
    for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello)lb_send_to(lb_peer[i].s,line);}
static void lb_drop(int i,const wchar_t*why){
    wchar_t t[160];if(lb_peer[i].hello){swprintf(t,160,L"%ls %ls.",lb_peer[i].name,why);}
    closesocket(lb_peer[i].s);int was=lb_peer[i].hello;memset(lb_peer+i,0,sizeof lb_peer[i]);lb_peer[i].s=INVALID_SOCKET;
    if(was){lb_broadcast_chat(L"",t);lb_broadcast_room();}}
static int lb_scramble(wchar_t*code);
static void lb_host(void){
    lb_reset();WSADATA wsa;WSAStartup(MAKEWORD(2,2),&wsa);
    lb_listen=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons((u_short)online_port);a.sin_addr.s_addr=htonl(INADDR_ANY);
    if(lb_listen==INVALID_SOCKET||bind(lb_listen,(struct sockaddr*)&a,sizeof a)||listen(lb_listen,8)){wchar_t t[120];swprintf(t,120,L"PORT %d IS IN USE \u2014 CHOOSE ANOTHER ON THE PLAY ONLINE PAGE",online_port);notice(t);lb_reset();return;}
    u_long nb=1;ioctlsocket(lb_listen,FIONBIO,&nb);
    lb.active=lb.host=lb.joined=1;lb.size=lb_size;lb.started=GetTickCount();char local[64];
    if(lb_local_ip(local,sizeof local)){swprintf(lb.local_code,96,L"%hs:%d",local,online_port);lb_scramble(lb.local_code);}
    wcscpy(lb.code,L"Finding your address...");lb_upnp_done=0;uintptr_t t=_beginthreadex(NULL,0,lb_upnp_worker,(void*)(intptr_t)online_port,0,NULL);if(t)CloseHandle((HANDLE)t);
    lb_broadcast_room();open_page(LB_ROOM_PAGE);notice(L"ROOM OPEN \u2014 SHARE THE ROOM CODE WITH YOUR FRIENDS");}
/* Room codes read RR-XXXXX-XXXXX: the address and port (48 bits), scrambled and
 * written in a 32-letter alphabet, so the IP isn't on show at a glance or on stream.
 * It is not secret: whoever joins connects to that address. Old ip:port codes still work. */
static const char lb_b32[]="23456789ABCDEFGHJKMNPQRSTVWXYZ01";
static const unsigned long long lb_code_key=0x5A3C96E1B74DULL;
static int lb_scramble(wchar_t*code){   /* "a.b.c.d:port" in place to RR-XXXXX-XXXXX */
    unsigned a,b,c,d,p;wchar_t tail;if(swscanf(code,L"%u.%u.%u.%u:%u%lc",&a,&b,&c,&d,&p,&tail)!=5||a>255||b>255||c>255||d>255||p>65535)return 0;
    unsigned long long v=(((unsigned long long)a<<40|(unsigned long long)b<<32|c<<24|d<<16|p)^lb_code_key)&0xFFFFFFFFFFFFULL;
    v=(v*0x2545F491ULL+0x9E37ULL)&0xFFFFFFFFFFFFULL;   /* odd multiplier: reversible mod 2^48, spreads neighbouring addresses apart */
    v^=v>>24;   /* and the high half into the low, so one port doesn't give every code the same ending */
    char s[11];for(int i=9;i>=0;i--){s[i]=lb_b32[v&31];v>>=5;}swprintf(code,96,L"RR-%.5hs-%.5hs",s,s+5);return 1;}
static int lb_unscramble(const char*s,char*ip,size_t cap,int*port){
    if((s[0]=='R'||s[0]=='r')&&(s[1]=='R'||s[1]=='r'))s+=2;unsigned long long v=0;int n=0;
    for(;*s;s++){if(*s=='-'||*s==' ')continue;char ch=(char)toupper((unsigned char)*s);if(ch=='O')ch='0';else if(ch=='I'||ch=='L')ch='1';
        const char*k=strchr(lb_b32,ch);if(!k||!ch)return 0;v=v<<5|(unsigned long long)(k-lb_b32);if(++n>10)return 0;}
    if(n!=10)return 0;v&=0xFFFFFFFFFFFFULL;v^=v>>24;v=((v+0x1000000000000ULL-0x9E37ULL)*0xBCA141444C71ULL)&0xFFFFFFFFFFFFULL;v^=lb_code_key;   /* 0xBCA1..: the multiplier's inverse mod 2^48 */
    *port=(int)(v&0xFFFF);snprintf(ip,cap,"%u.%u.%u.%u",(unsigned)(v>>40&255),(unsigned)(v>>32&255),(unsigned)(v>>24&255),(unsigned)(v>>16&255));return *port>=1024&&*port<=65534;}
static int lb_parse_code(const wchar_t*code,char*ip,size_t cap,int*port){
    char c[160];WideCharToMultiByte(CP_UTF8,0,code,-1,c,sizeof c,NULL,NULL);char*s=c;while(*s==' ')s++;char*e=s+strlen(s);while(e>s&&e[-1]==' ')*--e=0;
    if(!strchr(s,'.')&&lb_unscramble(s,ip,cap,port))return 1;
    char*colon=strrchr(s,':');*port=online_port;if(colon){*colon=0;*port=atoi(colon+1);}if(!*s||*port<1024||*port>65534)return 0;snprintf(ip,cap,"%s",s);return 1;}
/* A public-room join tries the host's same-network code first, briefly (no router
 * loops a PC's own public address back to it reliably), then its public code. */
static wchar_t lb_next_code[96];static DWORD lb_join_ms=12000;
static void lb_join(const wchar_t*code);
static int lb_try_next(void){if(!lb_next_code[0])return 0;wchar_t c[96];wcscpy(c,lb_next_code);lb_next_code[0]=0;lb_join_ms=12000;lb_join(c);return 1;}
static void lb_join(const wchar_t*code){
    char host[96];int port;if(!lb_parse_code(code,host,sizeof host,&port)){notice(L"USE A ROOM CODE LIKE RR-7K3MQ-X9P2A");return;}
    lb_reset();WSADATA wsa;WSAStartup(MAKEWORD(2,2),&wsa);
    struct addrinfo hints={0},*res=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;char ps[16];snprintf(ps,16,"%d",port);
    if(getaddrinfo(host,ps,&hints,&res)||!res){notice(L"THAT ROOM CODE'S ADDRESS COULD NOT BE FOUND");return;}
    inet_ntop(AF_INET,&((struct sockaddr_in*)res->ai_addr)->sin_addr,lb.host_ip,sizeof lb.host_ip);lb.host_port=port;
    lb_sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);u_long nb=1;ioctlsocket(lb_sock,FIONBIO,&nb);connect(lb_sock,res->ai_addr,(int)res->ai_addrlen);freeaddrinfo(res);
    lb.active=1;lb.connecting=1;lb.started=GetTickCount();wcscpy(lb.status,L"Connecting...");swprintf(lb.code,96,L"%hs:%d",lb.host_ip,port);lb_scramble(lb.code);
    open_page(LB_ROOM_PAGE);notice(L"JOINING THE ROOM...");}
static void lb_close(void){
    if(!lb.active)return;
    if(lb.host)for(int i=0;i<LB_MAX-1;i++){if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello){char r[100];lb_hexa("The host closed the room",r,sizeof r);char line[160];snprintf(line,160,"REFUSE %s",r);lb_send_to(lb_peer[i].s,line);}}
    else if(lb_sock!=INVALID_SOCKET&&lb.joined)lb_send_to(lb_sock,"BYE");
    lb_reset();}
/* Everyone launches the same session: seat 1 = host, seat 2 = the first joiner. */
static void lb_launch(int slot,int slots,unsigned session,const char*host_msg,const char*p2_msg,const char*ratings){
    /* Every seat's rating, in seat order ("1000,1234,..."; an older host sends none). */
    mmr_get();for(int i=0;i<LB_MAX;i++)room_mmr[i]=MMR_START;room_mmr[slot<LB_MAX?slot:0]=mmr_value;
    {const char*r=ratings;for(int i=0;i<LB_MAX&&r&&*r;i++){int v=atoi(r);if(v>=1&&v<=9999&&i!=slot)room_mmr[i]=v;r=strchr(r,',');if(r)r++;}}
    mmr_slot=slot<LB_MAX?slot:0;mmr_slots=slots;mmr_pending=1;memset(&lb_vote,0,sizeof lb_vote);
    online_host_aspect=net_int(host_msg,"aspect",0);if(online_host_aspect<0||online_host_aspect>2)online_host_aspect=0;
    online_host_lod=net_int(host_msg,"lod_percent",100);online_host_delay=net_int(host_msg,"delay",2);if(online_host_delay<1||online_host_delay>8)online_host_delay=2;
    if(slot){MultiByteToWideChar(CP_UTF8,0,lb.host_ip,-1,online_peer,64);online_peer_port=lb.host_port;}else{online_peer[0]=0;online_peer_port=0;}
    if(!net_write_session(slot,host_msg,p2_msg)){notice(L"THE SESSION FILE COULD NOT BE WRITTEN");lb_reset();mmr_pending=0;return;}
    {wchar_t path[32768];lb_results_path(path);DeleteFileW(path);}   /* this session's races only */
    lb_reset();page=MULTI_PAGE;selected=1;list_first=0;nav_depth=0;
    /* No mod sync: online games are vanilla (netplay clears mods), and every PC must start at once. */
    online_slot=slot;room_slots=slots;room_session=session;online_launch=1;mod_checked=1;
    if(ol_active&&game_process){ol_launch_pending=1;return;}   /* from the game: it closes first, then the online game starts */
    play_game();}
/* The vote (host): three different random courses, sent to everyone. */
static void lb_vote_send(void){char line[64];snprintf(line,sizeof line,"TALLY %d %d %d",lb_vote.tally[0],lb_vote.tally[1],lb_vote.tally[2]);for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello)lb_send_to(lb_peer[i].s,line);}
static void lb_vote_count(void){memset(lb_vote.tally,0,sizeof lb_vote.tally);if(lb_vote.mine)lb_vote.tally[lb_vote.mine-1]++;for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello&&lb_peer[i].vote>=1&&lb_peer[i].vote<=3)lb_vote.tally[lb_peer[i].vote-1]++;lb_vote_send();}
static void lb_vote_begin(void){
    unsigned r=(GetTickCount()*2654435761u)^(unsigned)GetCurrentProcessId();memset(&lb_vote,0,sizeof lb_vote);
    for(int k=0;k<3;k++){int race;do{r=r*1103515245u+12345u;race=1+(int)((r>>8)%18u);}while((k>0&&race==lb_vote.option[0])||(k>1&&race==lb_vote.option[1]));lb_vote.option[k]=race;}
    for(int i=0;i<LB_MAX-1;i++)lb_peer[i].vote=0;lb_vote.active=1;lb_vote.started=GetTickCount();if(page==LB_ROOM_PAGE)selected=3;
    char line[64];snprintf(line,sizeof line,"VOTE %d %d %d",lb_vote.option[0],lb_vote.option[1],lb_vote.option[2]);
    for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello)lb_send_to(lb_peer[i].s,line);
    wchar_t c[96];swprintf(c,96,L"Vote for the next race: 15 seconds.");lb_broadcast_chat(L"",c);}
static void lb_start_now(void);
/* Host, each poll: the vote closes when everyone has voted or after 15 s; most votes win, ties at random. */
static void lb_vote_poll(void){
    if(!lb_vote.active||!lb.host)return;int voters=1+lb_guest_count(),cast=lb_vote.tally[0]+lb_vote.tally[1]+lb_vote.tally[2];
    if(cast<voters&&GetTickCount()-lb_vote.started<15000)return;
    int best=-1,ties=0;unsigned r=GetTickCount()*2246822519u;
    for(int k=0;k<3;k++){if(best<0||lb_vote.tally[k]>lb_vote.tally[best]){best=k;ties=1;}else if(lb_vote.tally[k]==lb_vote.tally[best]&&(r=r*1103515245u+12345u,(r>>8)%(unsigned)++ties==0))best=k;}
    lb_voted_race=lb_vote.option[best];lb_vote.active=0;
    wchar_t name[64],t[160];lb_race_name(lb_voted_race,name,64);swprintf(t,160,L"Next race: %ls (%ls).",name,lb_rank_name((lb_voted_race-1)/6));lb_broadcast_chat(L"",t);
    lb_start_now();}
static void lb_start(void){
    int n=lb_guest_count();if(n<1){notice(L"WAITING FOR PLAYERS TO JOIN");return;}
    for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello&&!lb_peer[i].ready){wchar_t t[120];swprintf(t,120,L"%ls IS NOT READY",lb_peer[i].name);notice(t);return;}
    if(!lb_vote.active)lb_vote_begin();}
static void lb_start_now(void){
    int n=lb_guest_count();if(n<1){notice(L"EVERYONE LEFT THE ROOM");return;}
    lb_seed=((GetTickCount()*2654435761u)^(unsigned)GetCurrentProcessId())&0x7fffffffu;if(!lb_seed)lb_seed=1;
    char host_msg[2048],hm[4200],pm[4200],line[8600];net_message(host_msg,sizeof host_msg,0);const char*p2="";
    for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello){p2=lb_peer[i].msg;break;}
    unsigned session=(GetTickCount()*2654435761u)^(unsigned)GetCurrentProcessId();if(!session)session=1;
    lb_hexa(host_msg,hm,sizeof hm);lb_hexa(p2,pm,sizeof pm);char p2copy[2048];snprintf(p2copy,sizeof p2copy,"%s",p2);
    char ratings[96];int rn=snprintf(ratings,sizeof ratings,"%d",mmr_get());   /* seat order: the host, then the joined peers */
    for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello&&rn>0&&(size_t)rn<sizeof ratings)rn+=snprintf(ratings+rn,sizeof ratings-rn,",%d",net_int(lb_peer[i].msg,"mmr",MMR_START));
    int slot=1;for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello){snprintf(line,sizeof line,"START %d %d %u %s %s %s",slot++,n+1,session,hm,pm,ratings);lb_send_to(lb_peer[i].s,line);}
    lb_launch(0,n+1,session,host_msg,p2copy,ratings);}
static void lb_host_line(int i,char*l){
    char*t[6]={0};int n=0;for(char*p=strtok(l," ");p&&n<6;p=strtok(NULL," "))t[n++]=p;if(!n)return;LbPeer*p=lb_peer+i;
    if(!strcmp(t[0],"HELLO")&&n>=4&&!p->hello){
        char why[160]="";if(strcmp(t[1],online_fp))snprintf(why,sizeof why,"The host has a different game build - both need the same one");
        else if(lb_guest_count()+1>=lb.size)snprintf(why,sizeof why,"The room is full");
        if(why[0]){char r[400],line[420];lb_hexa(why,r,sizeof r);snprintf(line,sizeof line,"REFUSE %s",r);lb_send_to(p->s,line);closesocket(p->s);p->s=INVALID_SOCKET;return;}
        lb_unhex(t[2],p->name,64);if(!p->name[0])wcscpy(p->name,L"Rider");lb_unhexa(t[3],p->msg,sizeof p->msg);p->hello=1;
        wchar_t j[120];swprintf(j,120,L"%ls joined.",p->name);lb_broadcast_room();lb_broadcast_chat(L"",j);}
    else if(!p->hello)return;
    else if(!strcmp(t[0],"READY")&&n>=2){p->ready=atoi(t[1])!=0;lb_broadcast_room();}
    else if(!strcmp(t[0],"MSG")&&n>=2){lb_unhexa(t[1],p->msg,sizeof p->msg);lb_broadcast_room();}   /* new settings (bikes) */
    else if(!strcmp(t[0],"PICK")&&n>=2&&lb_vote.active){int k=atoi(t[1]);if(k>=1&&k<=3){p->vote=k;lb_vote_count();}}
    else if(!strcmp(t[0],"CHAT")&&n>=2){wchar_t text[256];lb_unhex(t[1],text,256);if(text[0])lb_broadcast_chat(p->name,text);}
    else if(!strcmp(t[0],"BYE"))lb_drop(i,L"left");
}
static void lb_guest_line(char*l){
    char*t[7]={0};int n=0;for(char*p=strtok(l," ");p&&n<7;p=strtok(NULL," "))t[n++]=p;if(!n)return;
    static LbMember next[LB_MAX];static int count;
    if(!strcmp(t[0],"REFUSE")&&n>=2){char r[200];lb_unhexa(t[1],r,sizeof r);wchar_t w[200];MultiByteToWideChar(CP_UTF8,0,r,-1,w,200);lb_reset();page=LB_ROOMS_PAGE;selected=0;list_first=0;notice(w);}
    else if(!strcmp(t[0],"ROOM")&&n>=3){lb.size=atoi(t[1]);lb.you=atoi(t[2]);count=0;lb.joined=1;lb.connecting=0;}
    else if(!strcmp(t[0],"M")&&n>=4&&count<LB_MAX){next[count].ready=atoi(t[1]);next[count].host=atoi(t[2]);lb_unhex(t[3],next[count].name,64);next[count].me=count==lb.you;next[count].mmr=n>=5?atoi(t[4]):MMR_START;count++;}
    else if(!strcmp(t[0],"END")){memcpy(lb.member,next,sizeof next);lb.members=count;if(lb.you<count)lb.ready=lb.member[lb.you].ready;if(lb_test_autoready&&!lb.ready){lb.ready=1;lb_send_to(lb_sock,"READY 1");}}
    else if(!strcmp(t[0],"CHAT")&&n>=3){wchar_t f[64],x[256];lb_unhex(t[1],f,64);lb_unhex(t[2],x,256);lb_add_chat(f,x);}
    else if(!strcmp(t[0],"VOTE")&&n>=4){memset(&lb_vote,0,sizeof lb_vote);for(int k=0;k<3;k++){int r=atoi(t[1+k]);lb_vote.option[k]=r>=1&&r<=18?r:1;}lb_vote.active=1;lb_vote.started=GetTickCount();if(page==LB_ROOM_PAGE)selected=3;}
    else if(!strcmp(t[0],"TALLY")&&n>=4){for(int k=0;k<3;k++)lb_vote.tally[k]=atoi(t[1+k]);}
    else if(!strcmp(t[0],"START")&&n>=6){char hm[2048],pm[2048];lb_unhexa(t[4],hm,sizeof hm);lb_unhexa(t[5],pm,sizeof pm);lb_launch(atoi(t[1]),atoi(t[2]),(unsigned)strtoul(t[3],NULL,10),hm,pm,n>=7?t[6]:"");}
}
static int lb_read(SOCKET s,char*buf,size_t*len,size_t cap,int peer){
    for(;;){int n=recv(s,buf+*len,(int)(cap-1-*len),0);if(n==0)return -1;if(n<0)return WSAGetLastError()==WSAEWOULDBLOCK?0:-1;*len+=(size_t)n;buf[*len]=0;
        char*e;while((e=strchr(buf,'\n'))!=NULL){*e=0;if(e>buf&&e[-1]=='\r')e[-1]=0;char line[8700];snprintf(line,sizeof line,"%s",buf);size_t used=(size_t)(e+1-buf);memmove(buf,e+1,*len-used+1);*len-=used;
            if(peer>=0)lb_host_line(peer,line);else lb_guest_line(line);if(!lb.active||(peer>=0&&lb_peer[peer].s==INVALID_SOCKET)||(peer<0&&lb_sock==INVALID_SOCKET))return 1;}
        if(*len>=cap-1)*len=0;}}
/* Timer (50 ms): accept joiners, read lines, follow the router lookup. */
static void pub_poll(void);static void pub_auth(void);static int pub_auth_ready;
static void lb_poll(HWND window){
    if(LB_PAGE(page))pub_auth();
    pub_poll();
    if(lb_vote.active){if(lb.active)lb_vote_poll();else memset(&lb_vote,0,sizeof lb_vote);}   /* host closes the vote; a left room has none */
    if(page==LB_BROWSE_PAGE||(page==LB_ROOMS_PAGE&&pub_auth_ready))InvalidateRect(window,NULL,FALSE);
    if(!lb.active)return;
    if(lb.host){
        if(lb_upnp_done==1){lb_upnp_done=2;if(lb_upnp_code[0]){wcscpy(lb.code,lb_upnp_code);lb_scramble(lb.code);lb.code_state=lb_upnp_mapped?1:2;}else{wcscpy(lb.code,lb.local_code[0]?lb.local_code:L"Unknown");lb.code_state=2;}}
        SOCKET s=accept(lb_listen,NULL,NULL);
        if(s!=INVALID_SOCKET){int slot=-1;for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s==INVALID_SOCKET){slot=i;break;}
            if(slot<0)closesocket(s);else{u_long nb=1;ioctlsocket(s,FIONBIO,&nb);memset(lb_peer+slot,0,sizeof lb_peer[slot]);lb_peer[slot].s=s;}}
        for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_read(lb_peer[i].s,lb_peer[i].rx,&lb_peer[i].rx_len,sizeof lb_peer[i].rx,i)<0)lb_drop(i,L"left");
        if(lb_test_autostart&&lb_guest_count()+1>=lb_test_autostart){int ready=1;for(int i=0;i<LB_MAX-1;i++)if(lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello&&!lb_peer[i].ready)ready=0;if(ready){lb_test_autostart=0;lb_start();return;}}
    }else if(lb_sock!=INVALID_SOCKET){
        if(lb.connecting==1){fd_set w,x;FD_ZERO(&w);FD_ZERO(&x);FD_SET(lb_sock,&w);FD_SET(lb_sock,&x);struct timeval tv={0,0};
            if(select(0,NULL,&w,&x,&tv)>0){if(FD_ISSET(lb_sock,&x)){if(lb_try_next())return;lb_reset();page=LB_ROOMS_PAGE;selected=0;notice(L"NO ROOM ANSWERED AT THAT CODE \u2014 CHECK IT, AND THAT THE HOST'S PORT IS OPEN");InvalidateRect(window,NULL,FALSE);return;}
                char msg[2048],hm[4200],name[200],line[4600];net_message(msg,sizeof msg,1);lb_hexa(msg,hm,sizeof hm);lb_hex(rider_names[0],name,sizeof name);
                snprintf(line,sizeof line,"HELLO %s %s %s",online_fp,name,hm);lb_send_to(lb_sock,line);lb.connecting=2;wcscpy(lb.status,L"Waiting for the host...");}
            else if(GetTickCount()-lb.started>lb_join_ms){if(lb_try_next())return;lb_reset();page=LB_ROOMS_PAGE;selected=0;notice(L"NO ROOM ANSWERED AT THAT CODE \u2014 CHECK IT, AND THAT THE HOST'S PORT IS OPEN");InvalidateRect(window,NULL,FALSE);return;}}
        else if(lb_read(lb_sock,lb_rx,&lb_rx_len,sizeof lb_rx,-1)<0&&lb.active){lb_reset();if(LB_PAGE(page)){page=LB_ROOMS_PAGE;selected=0;}notice(L"THE CONNECTION TO THE ROOM WAS LOST");}
    }
    if(LB_PAGE(page))InvalidateRect(window,NULL,FALSE);
}
/* ---- Public rooms (the recomp-net lobby, page 38) ------------------------------
 * Strangers find each other on the public lobby ws://netplay.retcomm.net:8765,
 * run by its operator; we run no server. It admits Discord-signed-in players
 * only (login_required): sign-in is recomp-net's own code (recomp_net_auth/,
 * MIT) through the operator's Discord app, so no app ID of ours. Its key file,
 * netplay_secret, sits in the store beside launcher.ini; it is a password and
 * never goes in a release or a report.
 * The lobby only lists rooms. A host with "List my room publicly" on also
 * creates a listing (op create, game_version = the build fingerprint). A
 * joiner takes a seat in that listing (op join); the host sends the seated
 * players its room code (op signal, type PUB_SIG_CODE, which no recomp-net
 * range uses), and the joiner joins by code as usual. Leaving the room, or the
 * race starting, leaves the listing; the host leaving closes it. */
#include "recomp_net/auth.h"
#define PUB_URL "ws://netplay.retcomm.net:8765"
#define PUB_GAME "Road Rash: Jailbreak"
#define PUB_SIG_CODE 200
#define PUB_MAX 32
static struct {
    int state;            /* 0 off, 1 connecting, 2 connected, 3 welcomed (hello sent) */
    SOCKET s;char rx[65536];size_t rx_len;
    int in_room,is_host,waiting,want_list,want_create;DWORD wait_started,last_send;
    char lobby_id[48],host_id[48],sent_code[96];
    int rooms,listed,online;struct {char id[48];wchar_t name[64];int players,max,same;} room[PUB_MAX];   /* online: our game's signed-in players */
} pub;
static int pub_list_on=1,pub_auth_ready,pub_hosting;static DWORD pub_retry_at;   /* pub_hosting: our open room should be listed (relisted after a drop) */
static volatile LONG pub_conn_done;static SOCKET pub_conn_sock=INVALID_SOCKET;static char pub_conn_rx[4096];static size_t pub_conn_rx_len;
static void pub_auth(void){
    if(pub_auth_ready)return;pub_auth_ready=1;wchar_t w[32768];char path[32768];swprintf(w,32768,L"%ls\\netplay_secret",store);
    WideCharToMultiByte(CP_UTF8,0,w,-1,path,sizeof path,NULL,NULL);rnet_account_set_secret_path(path);rnet_account_init(PUB_URL);}
static int pub_signed_in(void){return pub_auth_ready&&rnet_account_state()==RNET_ACCOUNT_SIGNED_IN&&rnet_account_session()[0];}
static void pub_esc(const char*in,char*out,size_t cap){size_t o=0;for(;*in&&o+7<cap;in++){unsigned char c=(unsigned char)*in;
    if(c=='"'||c=='\\'){out[o++]='\\';out[o++]=(char)c;}else if(c<0x20)o+=(size_t)snprintf(out+o,cap-o,"\\u%04x",c);else out[o++]=(char)c;}out[o]=0;}
static void pub_wesc(const wchar_t*w,char*out,size_t cap){char u[400];if(!WideCharToMultiByte(CP_UTF8,0,w,-1,u,sizeof u,NULL,NULL))u[0]=0;pub_esc(u,out,cap);}
/* Flat JSON reads: the first "key": in json. Strings are unescaped (\uXXXX to UTF-8). */
static const char*pub_key(const char*json,const char*key){char k[48];snprintf(k,sizeof k,"\"%s\":",key);const char*p=strstr(json,k);if(!p)return NULL;p+=strlen(k);while(*p==' ')p++;return p;}
static int pub_str(const char*json,const char*key,char*out,size_t cap){
    const char*p=pub_key(json,key);size_t o=0;out[0]=0;if(!p||*p!='"')return 0;
    for(p++;*p&&*p!='"'&&o+4<cap;p++){if(*p!='\\'){out[o++]=*p;continue;}p++;if(!*p)break;
        if(*p=='u'){unsigned v=0;if(sscanf(p+1,"%4x",&v)!=1)break;p+=4;
            if(v<0x80)out[o++]=(char)v;else if(v<0x800){out[o++]=(char)(0xC0|v>>6);out[o++]=(char)(0x80|(v&63));}else{out[o++]=(char)(0xE0|v>>12);out[o++]=(char)(0x80|(v>>6&63));out[o++]=(char)(0x80|(v&63));}}
        else out[o++]=*p=='n'?'\n':*p=='t'?'\t':*p;}
    out[o]=0;return 1;}
static int pub_int(const char*json,const char*key,int def){const char*p=pub_key(json,key);return p&&(*p=='-'||(*p>='0'&&*p<='9'))?atoi(p):def;}
/* public-lobby.log in the store: what the lobby said and what we sent (never the hello, which carries the session). */
static void pub_log(const char*what,const char*text){wchar_t p[32768];swprintf(p,32768,L"%ls\\public-lobby.log",store);FILE*f=_wfopen(p,L"ab");if(!f)return;
    SYSTEMTIME t;GetLocalTime(&t);fprintf(f,"%02d:%02d:%02d [%lu] %s %.600s\r\n",t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),what,text?text:"");fclose(f);}
static void pub_drop(void){if(pub.state)pub_log("drop","");if(pub.s!=INVALID_SOCKET&&pub.s)closesocket(pub.s);int list=pub.want_list;memset(&pub,0,sizeof pub);pub.s=INVALID_SOCKET;pub.want_list=list;}
/* One masked frame (client frames must be masked); opcode 1 text, 10 pong. */
static int pub_frame(int op,const char*data,size_t n){
    if(pub.s==INVALID_SOCKET||pub.state<2)return 0;unsigned char f[8192];size_t h=0;if(n>sizeof f-16)return 0;
    f[h++]=(unsigned char)(0x80|op);if(n<126)f[h++]=(unsigned char)(0x80|n);else{f[h++]=0x80|126;f[h++]=(unsigned char)(n>>8);f[h++]=(unsigned char)n;}
    unsigned char m[4];BCryptGenRandom(NULL,m,4,BCRYPT_USE_SYSTEM_PREFERRED_RNG);memcpy(f+h,m,4);h+=4;for(size_t i=0;i<n;i++)f[h+i]=(unsigned char)(data[i]^m[i&3]);
    u_long nb=0;ioctlsocket(pub.s,FIONBIO,&nb);int ok=send(pub.s,(const char*)f,(int)(h+n),0)==(int)(h+n);nb=1;ioctlsocket(pub.s,FIONBIO,&nb);if(!ok)pub_drop();else pub.last_send=GetTickCount();return ok;}
static int pub_send(const char*json){if(strncmp(json,"{\"op\":\"hello\"",13))pub_log(pub.state>=2?"send":"send-not-connected",json);return pub_frame(1,json,strlen(json));}
/* Connects and upgrades to a WebSocket off the window thread; pub_poll picks the socket up. */
static unsigned __stdcall pub_conn_worker(void*arg){
    (void)arg;SOCKET s=INVALID_SOCKET;char host[128],port[8]="80";const char*h=strstr(PUB_URL,"//")+2;snprintf(host,sizeof host,"%s",h);char*c=strchr(host,':');if(c){*c=0;snprintf(port,sizeof port,"%s",c+1);}
    struct addrinfo hints={0},*res=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;pub_conn_rx_len=0;
    if(!getaddrinfo(host,port,&hints,&res)&&res){s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);DWORD tv=6000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&tv,sizeof tv);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&tv,sizeof tv);
        if(s!=INVALID_SOCKET&&connect(s,res->ai_addr,(int)res->ai_addrlen)){closesocket(s);s=INVALID_SOCKET;}freeaddrinfo(res);}
    if(s!=INVALID_SOCKET){unsigned char k[16];char key[32],req[512];BCryptGenRandom(NULL,k,16,BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";int o=0;
        for(int i=0;i<16;i+=3){unsigned v=k[i]<<16|(i+1<16?k[i+1]<<8:0)|(i+2<16?k[i+2]:0);key[o++]=b64[v>>18&63];key[o++]=b64[v>>12&63];key[o++]=i+1<16?b64[v>>6&63]:'=';key[o++]=i+2<16?b64[v&63]:'=';}key[o]=0;
        int n=snprintf(req,sizeof req,"GET / HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",host,port,key);
        char*end=NULL;if(send(s,req,n,0)==n)for(;;){int r=recv(s,pub_conn_rx+pub_conn_rx_len,(int)(sizeof pub_conn_rx-1-pub_conn_rx_len),0);if(r<=0)break;pub_conn_rx_len+=(size_t)r;pub_conn_rx[pub_conn_rx_len]=0;
            if((end=strstr(pub_conn_rx,"\r\n\r\n"))!=NULL||pub_conn_rx_len>=sizeof pub_conn_rx-1)break;}
        if(!end||strncmp(pub_conn_rx,"HTTP/1.1 101",12)){closesocket(s);s=INVALID_SOCKET;}
        else{size_t used=(size_t)(end+4-pub_conn_rx);memmove(pub_conn_rx,end+4,pub_conn_rx_len-used);pub_conn_rx_len-=used;u_long nb=1;ioctlsocket(s,FIONBIO,&nb);}}
    pub_conn_sock=s;InterlockedExchange(&pub_conn_done,1);return 0;}
static void pub_connect(void){
    if(pub.state||!pub_signed_in())return;WSADATA wsa;WSAStartup(MAKEWORD(2,2),&wsa);pub.state=1;pub.s=INVALID_SOCKET;pub_conn_done=0;
    uintptr_t t=_beginthreadex(NULL,0,pub_conn_worker,NULL,0,NULL);if(t)CloseHandle((HANDLE)t);else pub.state=0;}
static void pub_request_list(void){pub.want_list=1;pub.listed=0;if(pub.state==3)pub_send("{\"op\":\"list\",\"game_name\":\"" PUB_GAME "\"}");else pub_connect();}
static void pub_create(void){
    char name[300],dn[200],line[900];wchar_t w[96];swprintf(w,96,L"%ls's room",rider_names[0][0]?rider_names[0]:L"Rider");pub_wesc(w,name,sizeof name);pub_wesc(rider_names[0],dn,sizeof dn);
    snprintf(line,sizeof line,"{\"op\":\"create\",\"name\":\"%s\",\"game_name\":\"" PUB_GAME "\",\"game_version\":\"%s\",\"password\":\"\",\"max_slots\":%d,\"allow_spectators\":false,\"host_bind\":\"0.0.0.0:%d\",\"display_name\":\"%s\"}",
        name,online_fp,lb.size,online_port,dn[0]?dn:"Host");pub.want_create=0;pub.sent_code[0]=0;pub_send(line);}
static void pub_join(int i){
    {char t[160];snprintf(t,sizeof t,"room %d of %d, state %d, in_room %d",i,pub.rooms,pub.state,pub.in_room);pub_log("join-click",t);}
    if(i<0||i>=pub.rooms)return;if(!pub.room[i].same){notice(L"THAT ROOM IS ON A DIFFERENT GAME BUILD — BOTH PLAYERS NEED THE SAME ONE");return;}
    if(pub.in_room){notice(L"LEAVE YOUR ROOM FIRST");return;}
    char id[100],dn[200],line[700];pub_esc(pub.room[i].id,id,sizeof id);pub_wesc(rider_names[0],dn,sizeof dn);
    snprintf(line,sizeof line,"{\"op\":\"join\",\"lobby_id\":\"%s\",\"password\":\"\",\"guest_bind\":\"0.0.0.0:%d\",\"display_name\":\"%s\",\"game_name\":\"" PUB_GAME "\",\"game_version\":\"%s\"}",id,online_port+1,dn[0]?dn:"Rider",online_fp);
    if(pub_send(line)){pub.waiting=1;pub.wait_started=GetTickCount();notice(L"ASKING THE HOST FOR ITS ROOM CODE...");}
    else{notice(L"THE PUBLIC LOBBY CONNECTION DROPPED — RECONNECTING, THEN TRY AGAIN");pub.rooms=0;pub_request_list();}}
/* Host: the room code to everyone seated in the listing, once the router lookup has it. */
static void pub_send_code(int force){
    if(!pub.in_room||!pub.is_host||!lb.active||!lb.host||!lb.code_state)return;char code[200],local[96]="";WideCharToMultiByte(CP_UTF8,0,lb.code,-1,code,96,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,lb.local_code,-1,local,sizeof local,NULL,NULL);if(local[0]&&strcmp(local,code)){strcat(code," ");strcat(code,local);}   /* "public same-network" */
    if(!force&&!strcmp(code,pub.sent_code))return;snprintf(pub.sent_code,sizeof pub.sent_code,"%s",code);char id[100],c[200],line[500];pub_esc(pub.lobby_id,id,sizeof id);pub_esc(code,c,sizeof c);
    snprintf(line,sizeof line,"{\"op\":\"signal\",\"lobby_id\":\"%s\",\"to_player_id\":\"\",\"type\":%d,\"flag\":0,\"text\":\"%s\"}",id,PUB_SIG_CODE,c);pub_send(line);}
static void pub_wait_failed(const wchar_t*why){pub.waiting=0;if(pub.in_room)pub_send("{\"op\":\"leave\"}");pub.in_room=0;notice(why);}
static void pub_message(const char*j){
    char op[32];pub_str(j,"op",op,sizeof op);if(strcmp(op,"lobby_list")||!pub.listed)pub_log("recv",j);   /* the list repeats every second or so */
    if(!strcmp(op,"welcome")){char sess[1200],hello[1500],dn[200];pub_esc(rnet_account_session(),sess,sizeof sess);pub_wesc(rider_names[0],dn,sizeof dn);
        snprintf(hello,sizeof hello,"{\"op\":\"hello\",\"display_name\":\"%s\",\"game_name\":\"" PUB_GAME "\",\"session\":\"%s\"}",dn[0]?dn:"Rider",sess);pub_send(hello);pub.state=3;
        if(pub.want_list)pub_request_list();if(pub.want_create)pub_create();}
    else if(!strcmp(op,"error")){char code[48];pub_str(j,"code",code,sizeof code);
        if(!strcmp(code,"login_required")){pub_drop();notice(L"THE PUBLIC LOBBY DID NOT ACCEPT YOUR SIGN-IN — SIGN IN AGAIN");}
        else if(pub.waiting)pub_wait_failed(!strcmp(code,"lobby_full")?L"THAT ROOM IS FULL":L"THAT ROOM COULD NOT BE JOINED — REFRESH THE LIST");
        else{wchar_t t[120];swprintf(t,120,L"PUBLIC LOBBY: %hs",code);notice(t);}}
    else if(!strcmp(op,"lobby_list")){
        const char*p=pub_key(j,"lobbies"),*end=NULL;pub.rooms=0;pub.listed=1;if(p)p=strchr(p,'[');
        if(p){int depth=0;const char*e=p;do{depth+=*e=='['?1:*e==']'?-1:0;e++;}while(*e&&depth>0);end=e;}   /* only the lobbies array: "players" follows it */
        while(p&&pub.rooms<PUB_MAX&&(p=strchr(p,'{'))!=NULL&&p<end){int depth=0;const char*e=p;do{depth+=*e=='{'?1:*e=='}'?-1:0;e++;}while(*e&&depth>0);
            char o[1024],gn[80],gv[80],name[200];size_t n=(size_t)(e-p);if(n>=sizeof o)n=sizeof o-1;memcpy(o,p,n);o[n]=0;p=e;
            pub_str(o,"game_name",gn,sizeof gn);if(strcmp(gn,PUB_GAME))continue;
            pub_str(o,"game_version",gv,sizeof gv);pub_str(o,"name",name,sizeof name);
            __typeof__(pub.room[0])*r=pub.room+pub.rooms++;pub_str(o,"lobby_id",r->id,sizeof r->id);if(!MultiByteToWideChar(CP_UTF8,0,name,-1,r->name,64))wcscpy(r->name,L"Room");
            r->players=pub_int(o,"player_count",1);r->max=pub_int(o,"max_slots",2);r->same=!strcmp(gv,online_fp);}
        /* Players online: the signed-in players whose game is ours (nothing else of theirs is kept). */
        const char*q=pub_key(j,"players");pub.online=0;if(q)q=strchr(q,'[');
        if(q){int depth=0;const char*e=q;do{depth+=*e=='['?1:*e==']'?-1:0;e++;}while(*e&&depth>0);const char*qend=e;
            while((q=strchr(q,'{'))!=NULL&&q<qend){int d=0;const char*f=q;do{d+=*f=='{'?1:*f=='}'?-1:0;f++;}while(*f&&d>0);
                char o[1024],gn[80];size_t n=(size_t)(f-q);if(n>=sizeof o)n=sizeof o-1;memcpy(o,q,n);o[n]=0;q=f;
                if(pub_str(o,"game_name",gn,sizeof gn)&&!strcmp(gn,PUB_GAME))pub.online++;}}}
    else if(!strcmp(op,"created")){pub.in_room=1;pub.is_host=1;pub_str(j,"lobby_id",pub.lobby_id,sizeof pub.lobby_id);pub_send_code(1);}
    else if(!strcmp(op,"joined")){pub.in_room=1;pub.is_host=0;pub_str(j,"lobby_id",pub.lobby_id,sizeof pub.lobby_id);pub_str(j,"host_player_id",pub.host_id,sizeof pub.host_id);}
    else if(!strcmp(op,"lobby_update")){char h[48];if(pub_str(j,"host_player_id",h,sizeof h)&&h[0])snprintf(pub.host_id,sizeof pub.host_id,"%s",h);pub_send_code(1);}
    else if(!strcmp(op,"signal")){char from[48],code[160];pub_str(j,"from_player_id",from,sizeof from);
        if(pub.waiting&&pub_int(j,"type",0)==PUB_SIG_CODE&&(!pub.host_id[0]||!strcmp(from,pub.host_id))&&pub_str(j,"text",code,sizeof code)&&code[0]){
            char*local=strchr(code,' ');if(local)*local++=0;wchar_t w[96],l[96]=L"";MultiByteToWideChar(CP_UTF8,0,code,-1,w,96);if(local)MultiByteToWideChar(CP_UTF8,0,local,-1,l,96);
            pub.waiting=0;if(l[0]){wcscpy(lb_next_code,w);lb_join_ms=2500;lb_join(l);}else{lb_next_code[0]=0;lb_join_ms=12000;lb_join(w);}
            if(!lb.active)pub_wait_failed(L"THE HOST'S ROOM CODE DID NOT WORK");}}
    else if(!strcmp(op,"lobby_closed")||!strcmp(op,"left")||!strcmp(op,"kicked")){int was=pub.waiting;pub.in_room=pub.is_host=pub.waiting=0;if(was)notice(L"THAT ROOM CLOSED");}
}
/* Timer (50 ms): sign-in worker, the connection, frames in, leaving the listing once our room has gone. */
static void pub_host_listing(void);
static void pub_poll(void){
    if(!pub_auth_ready)return;rnet_account_pump();
    if(pub_hosting&&!(lb.active&&lb.host))pub_hosting=0;
    if(pub_hosting&&!pub.state&&GetTickCount()-pub_retry_at>5000){pub_retry_at=GetTickCount();pub_log("relist","");pub_host_listing();}
    /* On the Online pages, signed in: the lobby's list (rooms, players online) once sign-in is ready. */
    else if(LB_PAGE(page)&&pub_signed_in()&&!pub.state&&!pub.listed&&!pub.want_list&&GetTickCount()-pub_retry_at>5000){pub_retry_at=GetTickCount();pub_request_list();}
    if(pub.state==1&&pub_conn_done){pub_conn_done=0;pub.s=pub_conn_sock;
        if(pub.s==INVALID_SOCKET){pub_log("connect-failed","");pub.state=0;if(pub.want_list||pub.want_create)notice(L"THE PUBLIC LOBBY COULD NOT BE REACHED");pub.want_list=pub.want_create=0;}
        else{pub_log("connected","");pub.state=2;memcpy(pub.rx,pub_conn_rx,pub_conn_rx_len);pub.rx_len=pub_conn_rx_len;}}
    if(pub.state<2)return;
    for(;;){int n=recv(pub.s,pub.rx+pub.rx_len,(int)(sizeof pub.rx-1-pub.rx_len),0);if(n==0||(n<0&&WSAGetLastError()!=WSAEWOULDBLOCK)){pub_drop();return;}if(n<0)break;pub.rx_len+=(size_t)n;if(pub.rx_len>=sizeof pub.rx-1)break;}
    while(pub.rx_len>=2){unsigned char*b=(unsigned char*)pub.rx;size_t h=2,len=b[1]&127;int op=b[0]&15;
        if(len==126){if(pub.rx_len<4)break;len=(size_t)b[2]<<8|b[3];h=4;}else if(len==127){pub_drop();return;}
        if(len>=sizeof pub.rx-8){pub_drop();return;}if(pub.rx_len<h+len)break;
        char msg[65536];memcpy(msg,pub.rx+h,len);msg[len]=0;memmove(pub.rx,pub.rx+h+len,pub.rx_len-h-len);pub.rx_len-=h+len;
        if(op==8){pub_drop();return;}if(op==9){pub_frame(10,msg,len);continue;}if(op==1)pub_message(msg);if(pub.s==INVALID_SOCKET)return;}
    if(pub.waiting&&GetTickCount()-pub.wait_started>20000)pub_wait_failed(L"THE HOST DID NOT SEND ITS ROOM CODE — TRY ANOTHER ROOM");
    if(pub.in_room&&!pub.waiting&&!lb.active){pub_send("{\"op\":\"leave\"}");pub.in_room=pub.is_host=0;}
    if(pub.in_room&&pub.is_host)pub_send_code(0);
    if(pub.state>=2&&GetTickCount()-pub.last_send>15000)pub_frame(9,"",0);   /* keepalive, so an idle lobby connection isn't dropped */
}
/* Host: list the room just opened, when signed in and listing is on. */
static void pub_host_listing(void){if(!pub_list_on||!pub_signed_in())return;pub_hosting=1;pub.want_create=1;if(pub.state==3)pub_create();else pub_connect();}

/* Play online page: Host, Join, Public rooms, Discord account, List publicly, Room size, Port, Input delay.
 * Room page: Start/Ready, Room code, Chat, Leave, members, chat lines. Public rooms page: Refresh, the rooms. */
static int lb_row_count(void){return page==LB_ROOMS_PAGE?10:page==LB_BROWSE_PAGE?1+pub.rooms:page==LB_BIKES_PAGE?3:3+lb_vote_rows()+lb.members+1;}   /* room: ... members, Your bikes */
static const wchar_t*lb_seat_name(int slot){static wchar_t t[40];if(slot==0)return L"Player 1";if(slot==1)return L"Player 2";swprintf(t,40,L"AI rider takeover %d",slot-1);return t;}
static int lb_row(int item,wchar_t*label,wchar_t*value,size_t cap){
    if(page==LB_ROOMS_PAGE){
        int st=pub_auth_ready?rnet_account_state():RNET_ACCOUNT_GUEST;
        switch(item){
        case 0:wcsncpy(label,L"Host a room",cap);swprintf(value,cap,L"%d players",lb_size);return ROW_ACTION;
        case 1:wcsncpy(label,L"Join a room",cap);wcsncpy(value,L"Enter a room code",cap);return ROW_ACTION;
        case 2:wcsncpy(label,L"Public rooms",cap);
            if(!pub_signed_in())wcsncpy(value,L"Sign in first",cap);else if(pub.listed)swprintf(value,cap,L"%d player%ls online",pub.online,pub.online==1?L"":L"s");else wcsncpy(value,L"Find a room to join",cap);
            return pub_signed_in()?ROW_MENU:ROW_INFO;
        case 3:wcsncpy(label,L"Discord sign-in",cap);
            if(st==RNET_ACCOUNT_SIGNED_IN){const char*h=rnet_account_handle();if(!h[0])h=rnet_account_username();swprintf(value,cap,L"%hs",h[0]?h:"Signed in");}
            else wcsncpy(value,st==RNET_ACCOUNT_WAITING?L"Finish in your browser...":st==RNET_ACCOUNT_FAILED?L"Failed — try again":L"Sign in",cap);
            return ROW_ACTION;
        case 4:wcsncpy(label,L"List my room publicly",cap);wcsncpy(value,pub_list_on?L"Yes":L"No",cap);return ROW_VALUE;
        case 5:wcsncpy(label,L"Room size",cap);swprintf(value,cap,L"%d players",lb_size);return ROW_VALUE;
        case 6:wcsncpy(label,L"Port",cap);swprintf(value,cap,L"%d",online_port);return ROW_VALUE;
        case 7:wcsncpy(label,L"Input delay",cap);swprintf(value,cap,L"%d frame%ls",online_delay,online_delay==1?L"":L"s");return ROW_VALUE;
        case 8:{wcsncpy(label,L"Your bikes",cap);int set=0;for(int r=0;r<3;r++)set+=lb_bike(r)>=0;if(!set)wcsncpy(value,L"Random for every rank",cap);else swprintf(value,cap,L"%d of 3 ranks chosen",set);return ROW_MENU;}
        default:wcsncpy(label,L"Your rating",cap);mmr_get();
            if(mmr_races)swprintf(value,cap,L"%d MMR  (last %ls%d)",mmr_value,mmr_last>=0?L"+":L"",mmr_last);else swprintf(value,cap,L"%d MMR",mmr_value);return ROW_INFO;}}
    if(page==LB_BIKES_PAGE){swprintf(label,cap,L"%ls",lb_rank_name(item));lb_bike_name(lb_bike(item),value,cap);return ROW_VALUE;}
    if(page==LB_BROWSE_PAGE){
        if(item==0){wcsncpy(label,L"Refresh",cap);
            if(pub.waiting)wcsncpy(value,L"Asking the host for its room code...",cap);else if(pub.state&&pub.state<3)wcsncpy(value,L"Connecting...",cap);
            else if(!pub.listed)wcsncpy(value,pub.want_list?L"Looking...":L"",cap);else swprintf(value,cap,L"%d room%ls",pub.rooms,pub.rooms==1?L"":L"s");return ROW_ACTION;}
        int i=item-1;if(i>=pub.rooms)return ROW_INFO;wcsncpy(label,pub.room[i].name,cap);
        if(!pub.room[i].same)wcsncpy(value,L"Different game build",cap);else swprintf(value,cap,L"%d / %d players",pub.room[i].players,pub.room[i].max);
        return pub.room[i].same&&pub.room[i].players<pub.room[i].max?ROW_ACTION:ROW_INFO;}
    if(item==0){
        if(lb.host){wcsncpy(label,L"Start the race",cap);int n=lb_guest_count(),waiting=0;for(int i=0;i<LB_MAX-1;i++)waiting+=lb_peer[i].s!=INVALID_SOCKET&&lb_peer[i].hello&&!lb_peer[i].ready;
            if(lb_vote.active){DWORD left=GetTickCount()-lb_vote.started;swprintf(value,cap,L"Voting: %u s left",left<15000?(unsigned)(15000-left+999)/1000:0u);return ROW_INFO;}
            if(!n)wcsncpy(value,L"Waiting for players",cap);else if(waiting)swprintf(value,cap,L"%d not ready",waiting);else wcsncpy(value,L"Everyone is ready",cap);return n&&!waiting?ROW_ACTION:ROW_INFO;}
        wcsncpy(label,L"Ready",cap);
        if(lb_vote.active){DWORD left=GetTickCount()-lb_vote.started;swprintf(value,cap,L"Voting: %u s left",left<15000?(unsigned)(15000-left+999)/1000:0u);return ROW_INFO;}
        wcsncpy(value,!lb.joined?lb.status:lb.ready?L"Yes":L"No",cap);return lb.joined?ROW_VALUE:ROW_INFO;}
    if(item==1){wcsncpy(label,L"Room code",cap);wcsncpy(value,lb.code,cap);return lb.host?ROW_ACTION:ROW_INFO;}
    if(item==2){wcsncpy(label,lb.host?L"Close the room":L"Leave the room",cap);return ROW_ACTION;}
    if(lb_vote.active&&item>=3&&item<6){const int k=item-3,race=lb_vote.option[k];wchar_t n[64];lb_race_name(race,n,64);swprintf(label,cap,L"Vote: %ls",n);
        swprintf(value,cap,L"%ls  %d vote%ls%ls",lb_rank_name((race-1)/6),lb_vote.tally[k],lb_vote.tally[k]==1?L"":L"s",lb_vote.mine==k+1?L"  (yours)":L"");return ROW_ACTION;}
    int i=item-3-lb_vote_rows();
    if(i==lb.members){wcsncpy(label,L"Your bikes",cap);wchar_t a[40],b[40],c[40];lb_bike_name(lb_bike(0),a,40);lb_bike_name(lb_bike(1),b,40);lb_bike_name(lb_bike(2),c,40);swprintf(value,cap,L"%ls / %ls / %ls",a,b,c);return ROW_MENU;}
    if(i<lb.members){LbMember*m=lb.member+i;swprintf(label,cap,L"%d  %ls%ls",i+1,m->name,m->me?L" (you)":L"");
        swprintf(value,cap,L"%d MMR  %ls  %ls",m->mmr?m->mmr:MMR_START,lb_seat_name(i),m->host?L"Host":m->ready?L"Ready":L"Not ready");return lb.host&&!m->host?ROW_ACTION:ROW_INFO;}
    return ROW_INFO;
}
/* Room chat: typing on the room page goes into the box under the chat; Enter sends it.
 * Backspace edits it (never leaves the page); Esc clears it first. Space always types. */
static wchar_t lb_draft[201];
static int lb_chat_key(WPARAM k){
    if(page!=LB_ROOM_PAGE||!lb.joined)return 0;size_t n=wcslen(lb_draft);
    if(k==VK_BACK){if(n)lb_draft[n-1]=0;return 1;}
    if(k==VK_SPACE)return 1;   /* typed by WM_CHAR */
    if(k==VK_ESCAPE&&n){lb_draft[0]=0;return 1;}
    if(k==VK_RETURN&&n){char a[1200],line[1300];
        if(lb.host)lb_broadcast_chat(rider_names[0],lb_draft);else{lb_hex(lb_draft,a,sizeof a);snprintf(line,sizeof line,"CHAT %s",a);lb_send_to(lb_sock,line);}
        lb_draft[0]=0;return 1;}
    return 0;}
static int lb_chat_char(WPARAM c){
    if(page!=LB_ROOM_PAGE||!lb.joined||c<32||c==127)return 0;size_t n=wcslen(lb_draft);
    if(c==L' '&&!n)return 1;if(n<200){lb_draft[n]=(wchar_t)c;lb_draft[n+1]=0;}return 1;}
/* Room page: the right-hand card is the room chat, newest at the bottom, the type box, and the selected row's help. */
static void lb_draw_chat(double x,double y,double w,double h){
    double ax=x+20,aw=w-40,ty=y+22;
    text(L"ROOM CHAT",ax,ty,aw,30,25,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE);fill(ax,ty+38,64,2,C_GOLD,200);
    text(lb.joined?L"Just start typing to talk to everyone in the room.":L"You can talk here once you are in the room.",ax,ty+48,aw,24,16,FW_NORMAL,C_STEEL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    double help_h=78,box_h=40,top=ty+84,bottom=y+h-help_h-box_h-24,line=30;
    /* The type box, with a caret that blinks */
    double by=bottom+10;fill(ax,by,aw,box_h,RGBX(1,6,24),220);outline(ax,by,aw,box_h,lb.joined?C_GOLD:C_STEEL,lb.joined?200:90);
    if(lb_draft[0]){wchar_t s[204];swprintf(s,204,L"%ls%ls",lb_draft,GetTickCount()/500%2?L"_":L"");text(s,ax+12,by,aw-24,box_h,17,FW_NORMAL,C_WHITE,(text_width(s,17,FW_NORMAL,0)>aw-24?DT_RIGHT:DT_LEFT)|DT_VCENTER|DT_SINGLELINE);}   /* long: the end stays in view */
    else text(lb.joined?L"Type a message, then press Enter":L"",ax+12,by,aw-24,box_h,17,FW_NORMAL,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);fill(ax,top,aw,bottom-top,RGBX(1,6,24),150);outline(ax,top,aw,bottom-top,C_STEEL,90);
    if(!lb.chats)text(L"No messages yet. Say hello!",ax+14,top,aw-28,bottom-top,17,FW_NORMAL,C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    int fit=(int)((bottom-top-12)/line),first=lb.chats>fit?lb.chats-fit:0;double ly=top+6;
    for(int i=first;i<lb.chats;i++,ly+=line){LbChat*c=lb.chat+i;
        if(!c->from[0]){text(c->text,ax+14,ly,aw-28,line,16,FW_NORMAL,C_STEEL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);continue;}
        wchar_t who[80];swprintf(who,80,L"%ls:",c->from);double ww=text_width(who,17,FW_SEMIBOLD,0)+8;
        text(who,ax+14,ly,ww,line,17,FW_SEMIBOLD,C_GOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        text(c->text,ax+14+ww,ly,aw-28-ww,line,17,FW_NORMAL,C_WHITE,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
    text(menu_help(),ax,by+box_h+12,aw,help_h-4,16,FW_NORMAL,C_STEEL,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_EDITCONTROL);}
static const wchar_t*lb_page_title(int p){return p==LB_ROOM_PAGE?L"ROOM":p==LB_BROWSE_PAGE?L"PUBLIC ROOMS":p==LB_BIKES_PAGE?L"BIKES":L"PLAY ONLINE";}
static const wchar_t*lb_help(void){
    static wchar_t t[700];
    if(page==LB_ROOMS_PAGE){const wchar_t*h[]={
        L"Open a room on this PC. You are Player 1 and start the race when everyone is ready; your frame rate, aspect, detail and loading speed apply to everyone.\nThe launcher asks your router to open the port; if it can't, forward it (TCP and UDP) by hand.",
        L"Type the room code the host gave you, e.g. RR-7K3MQ-X9P2A. On the same network, use the host's same-network code.",
        L"Rooms other players have listed on the public lobby. Pick one to join; its host's room code comes to you automatically.",
        L"The public lobby lets in Discord-signed-in players only. Select to sign in: your browser opens Discord's page; choose Authorize, then come back here.\nWhen signed in, select to sign out.",
        L"Yes: a room you host also appears in Public rooms while it waits for players (needs sign-in). Your address is only sent to players who join it.\nNo: friends join with the room code only.",
        L"Up to 8. Players 1 and 2 ride as in the game's two-player modes; players 3-8 each take over an AI rider in Skull-to-Skull, with their own view and HUD. Left / Right to change.",
        L"The host's port: TCP for the room and UDP for the race. Joiners need nothing open. Left / Right or type.",
        L"Frames between a button press and the game seeing it on every PC. 2 suits nearby players; raise it if the race stutters.",
        L"The bike you ride online at each rank. Each rank has its own four bikes; Random gives you one of them each race. You can also change them from a room.",
        L"Your online rating. Everyone starts at 1000 (up to 9999). Each room race scores you against every other player: finish ahead of higher-rated riders to climb fastest. A middle place still earns points while your rating is low, and costs points once it is high. Bigger rooms move more points. AI riders don't count."};
        return h[selected>=0&&selected<10?selected:0];}
    if(page==LB_BIKES_PAGE){   /* the chosen bike's own description (the game's text, owner's disc) */
        const int r=selected>=0&&selected<3?selected:0,b=lb_bike(r);char d[200]="";
        if(b<0){swprintf(t,700,L"Random: one of the %ls bikes each race, picked when the race starts.\nLeft / Right to choose a bike for %ls races.",lb_rank_name(r),lb_rank_name(r));return t;}
        if(installed)rr_fe_string(discfile,1007+3*b,d,sizeof d);wchar_t n[48];lb_bike_name(b,n,48);swprintf(t,700,L"%ls: %hs\nYour bike in %ls races. Left / Right to change.",n,d,lb_rank_name(r));return t;}
    if(page==LB_BROWSE_PAGE)return selected==0?L"Asks the public lobby for open rooms again.":
        L"Select to join. Rooms on a different game build can't be joined: both players need the same version.";
    if(selected==0)return lb.host?L"Starts the race for everyone once every player is ready.\nIn the game, Player 1 picks the mode (Skull-to-Skull for more than two) and Player 2 confirms on their screen.":
        L"Tell the host you are ready. Left / Right or Select to switch.\nIn the game, Player 1 picks the mode and course; if you are Player 2, confirm on the Player 2 screen.";
    if(selected==1){if(!lb.host)return L"The room you are in.";
        swprintf(t,700,L"%ls\n%ls%ls%ls",lb.code_state==1?L"Your router opened the port: friends anywhere can join with this code. Select to copy it.":lb.code_state==2?L"Your router did not open the port. Forward it (TCP and UDP) to this PC for friends outside your network. Select to copy the code.":L"Asking your router to open the port...",
            lb.local_code[0]?L"Same network: ":L"",lb.local_code,L"");return t;}
    if(selected==2)return lb.host?L"Closes the room for everyone.":L"Back to Play online.";
    if(lb_vote.active&&selected>=3&&selected<6)return L"Vote for the next race: select a course (its rank shown on the right). Most votes win; a tie is settled at random. The vote ends when everyone has voted, or after 15 seconds.\nYour bike for that rank is set on Play online.";
    int i=selected-3-lb_vote_rows();if(i>=0&&i<lb.members){swprintf(t,700,L"%ls rides as %ls.%ls%ls",lb.member[i].name,lb_seat_name(i),i>=2?L" They drive one of the AI riders in Skull-to-Skull, with their own view and HUD.":L"",lb.host&&!lb.member[i].host?L"\nSelect to remove them from the room.":L"");return t;}
    return L"Room chat.";
}
static void lb_copy(const wchar_t*text){if(!OpenClipboard(mainwindow))return;EmptyClipboard();size_t n=(wcslen(text)+1)*sizeof(wchar_t);HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE,n);if(g){memcpy(GlobalLock(g),text,n);GlobalUnlock(g);SetClipboardData(CF_UNICODETEXT,g);}CloseClipboard();}
/* The answers to the Online page's questions (the launcher's dialogs, or the game's). */
static void lb_join_typed(const wchar_t*t){if(!t[0])return;wcsncpy(online_address,t,63);online_address[63]=0;save_settings();lb_next_code[0]=0;lb_join_ms=12000;lb_join(t);}
static void lb_port_typed(const wchar_t*t){int p=_wtoi(t);if(p>=1024&&p<=65534){online_port=p;save_settings();}else notice(L"CHOOSE A PORT FROM 1024 TO 65534");}
static void lb_sign_out(void){rnet_account_sign_out();pub_drop();}
static void lb_remove(int i){int k=0;for(int p=0;p<LB_MAX-1;p++)if(lb_peer[p].s!=INVALID_SOCKET&&lb_peer[p].hello&&++k==i){char r[100],l2[140];lb_hexa("The host removed you from the room",r,sizeof r);snprintf(l2,140,"REFUSE %s",r);lb_send_to(lb_peer[p].s,l2);lb_drop(p,L"was removed");break;}}
/* A joiner's bikes changed in the room: its settings again, for the host's START. */
static void lb_bikes_changed(void){
    if(!lb.active||lb.host||!lb.joined||lb_sock==INVALID_SOCKET)return;
    char m[2048],h[4200],line[4300];net_message(m,sizeof m,1);lb_hexa(m,h,sizeof h);snprintf(line,sizeof line,"MSG %s",h);lb_send_to(lb_sock,line);}
/* A game of ours still running blocks hosting and joining, unless that game is
   the one showing these pages (it closes when the race starts). */
static int lb_game_running(void){if(!game_process||ol_active)return 0;DWORD code;return GetExitCodeProcess(game_process,&code)&&code==STILL_ACTIVE;}
static void lb_action(void){
    pub_auth();
    if(page==LB_ROOMS_PAGE){
        if(selected<=2&&(mods_on()||cp_active())){notice(L"SWITCH OFF YOUR MODS AND CHEATS TO PLAY ONLINE");return;}
        if(selected<=2&&lb_game_running()){notice(L"CLOSE YOUR GAME FIRST");return;}
        if(selected==0){lb_host();if(lb.active)pub_host_listing();}
        else if(selected==1){if(ol_active){ol_ask(1,0,L"Join a room",L"Room code from the host (RR-XXXXX-XXXXX):",online_address,90);return;}
            wchar_t t[96]=L"";wcscpy(t,online_address);if(prompt_text(L"Join a room",L"Room code from the host (RR-XXXXX-XXXXX):",t,90,0))lb_join_typed(t);}
        else if(selected==2){if(!pub_signed_in()){notice(L"SIGN IN WITH DISCORD FIRST");return;}pub.rooms=0;open_page(LB_BROWSE_PAGE);pub_request_list();}
        else if(selected==3){int st=rnet_account_state();
            if(st==RNET_ACCOUNT_SIGNED_IN){if(ol_active)ol_ask(3,1,L"Discord sign-in",L"Sign out of the public lobby on this PC?",L"",0);
                else if(MessageBoxW(mainwindow,L"Sign out of the public lobby on this PC?",L"Discord sign-in",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)lb_sign_out();}
            else if(st!=RNET_ACCOUNT_WAITING){if(rnet_account_login_begin()==0)notice(L"FINISH SIGNING IN IN YOUR BROWSER, THEN COME BACK");else notice(L"SIGN-IN COULD NOT START — CHECK YOUR CONNECTION");}}
        else if(selected==4)pub_list_on=!pub_list_on;
        else if(selected==5){lb_size=lb_size%8+1;if(lb_size<2)lb_size=2;}
        else if(selected==6){wchar_t t[16];swprintf(t,16,L"%d",online_port);if(ol_active){ol_ask(2,0,L"Port",L"Port 1024-65534 (TCP and UDP):",t,5);return;}if(prompt_text(L"Port",L"Port 1024-65534 (TCP and UDP):",t,8,0))lb_port_typed(t);}
        else if(selected==7){online_delay=online_delay%8+1;save_settings();}
        else if(selected==8){lb_bikes_from=LB_ROOMS_PAGE;lb_bikes_row=8;page=LB_BIKES_PAGE;selected=0;list_first=0;}
        return;}
    if(page==LB_BIKES_PAGE){lb_bike_step(selected>=0&&selected<3?selected:0,1);lb_bikes_changed();return;}
    if(page==LB_BROWSE_PAGE){
        {char t[64];snprintf(t,sizeof t,"row %d, mods %d, game %d",selected,mods_on()||cp_active(),game_process!=NULL);pub_log("browse-select",t);}
        if(pub.waiting){notice(L"STILL ASKING THE HOST FOR ITS ROOM CODE...");return;}
        if(selected==0)pub_request_list();
        else{if(mods_on()||cp_active()){notice(L"SWITCH OFF YOUR MODS AND CHEATS TO PLAY ONLINE");return;}
            if(lb_game_running()){notice(L"CLOSE YOUR GAME FIRST");return;}
            pub_join(selected-1);}
        return;}
    char line[1300];
    if(lb_vote.active&&selected>=3&&selected<6){const int k=selected-2;lb_vote.mine=k;   /* a vote: the host counts it, a joiner sends it */
        if(lb.host)lb_vote_count();else if(lb.joined){snprintf(line,sizeof line,"PICK %d",k);lb_send_to(lb_sock,line);}return;}
    if(selected==0){if(lb.host)lb_start();else if(lb.joined){snprintf(line,sizeof line,"READY %d",!lb.ready);lb.ready=!lb.ready;lb_send_to(lb_sock,line);}return;}
    if(selected==1){if(lb.host&&lb.code_state){lb_copy(lb.code);notice(L"ROOM CODE COPIED");}return;}
    if(selected==2){lb_close();go_back();return;}
    int i=selected-3-lb_vote_rows();
    if(i==lb.members){lb_bikes_from=LB_ROOM_PAGE;lb_bikes_row=selected;page=LB_BIKES_PAGE;selected=0;list_first=0;return;}   /* Your bikes */
    if(lb.host&&i>0&&i<lb.members){wchar_t q[160];swprintf(q,160,L"Remove %ls from the room?",lb.member[i].name);
        if(ol_active){ol_remove_index=i;ol_ask(4,1,L"Room",q,L"",0);}
        else if(MessageBoxW(mainwindow,q,L"Room",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)lb_remove(i);}
}
static void lb_adjust(int dir){
    if(page==LB_ROOMS_PAGE){
        if(selected==4)pub_list_on=!pub_list_on;
        else if(selected==5){lb_size+=dir;if(lb_size<2)lb_size=2;if(lb_size>8)lb_size=8;}
        else if(selected==6){int p=online_port+dir;if(p>=1024&&p<=65534){online_port=p;save_settings();}}
        else if(selected==7){int d=online_delay+dir;if(d>=1&&d<=8){online_delay=d;save_settings();}}
        }
    else if(page==LB_BIKES_PAGE&&selected>=0&&selected<3){lb_bike_step(selected,dir);lb_bikes_changed();}
    else if(page==LB_ROOM_PAGE&&selected==0&&!lb.host&&lb.joined){char line[32];lb.ready=!lb.ready;snprintf(line,sizeof line,"READY %d",lb.ready);lb_send_to(lb_sock,line);}
}
/* Back from the room leaves (or, for the host, closes) it. */
static int lb_back(void){
    if(page==LB_BIKES_PAGE){page=lb_bikes_from==LB_ROOM_PAGE&&lb.active?LB_ROOM_PAGE:LB_ROOMS_PAGE;selected=lb_bikes_row;list_first=0;if(selected>=lb_row_count())selected=0;return 1;}   /* back where it was opened */
    if(page==LB_ROOM_PAGE)lb_close();return 0;}
