/* Owner-local WAV library. LGPL-2.1-or-later. No bundled music.
 * Replaces the streamed stereo soundtrack in the presentation mixer.
 * Guest sound state, decoding, disc reads, effects and voices stay native. */
#include "custom_music.h"
#include "rider_enhancements.h"
#include "mod_plugins.h"
#include "owner_assets.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cstring>
#include <cstdio>
extern "C" uint32_t spu_read(uint32_t);
extern "C" uint8_t*memory_get_ram_ptr(void);
namespace {
namespace fs=std::filesystem;
fs::path directory,settings;std::vector<fs::path> tracks;
std::mutex pending_mutex;std::vector<fs::path> pending;
bool initialized,custom,race_active,paused_active,preview;int selected;
uint8_t*pcm;int pcm_bytes,position;SDL_AudioStream*stream;char status[32];
void save(){WritePrivateProfileStringW(L"music",L"source",custom?L"custom":L"original",settings.c_str());WritePrivateProfileStringW(L"music",L"track",tracks.empty()?L"":tracks[selected].filename().c_str(),settings.c_str());}
void stop(){if(stream){SDL_DestroyAudioStream(stream);stream=nullptr;}SDL_free(pcm);pcm=nullptr;pcm_bytes=position=0;}
void scan(){
    std::error_code ec;auto previous=tracks.empty()?fs::path{}:tracks[selected].filename();tracks.clear();
    fs::create_directories(directory,ec);
    for(fs::directory_iterator it(directory,ec),end;it!=end&&!ec;it.increment(ec)){
        auto p=it->path();if(!it->is_regular_file(ec)||it->is_symlink(ec)||_wcsicmp(p.extension().c_str(),L".wav"))continue;
        auto bytes=it->file_size(ec);if(!ec&&bytes>44&&bytes<=64*1024*1024)tracks.push_back(p);
        if(tracks.size()==128)break;
    }
    std::sort(tracks.begin(),tracks.end(),[](auto&a,auto&b){return _wcsicmp(a.filename().c_str(),b.filename().c_str())<0;});
    selected=0;for(size_t i=0;i<tracks.size();i++)if(tracks[i].filename()==previous)selected=(int)i;
}
bool load_track(){
    stop();if(tracks.empty()){std::strcpy(status,"ADD WAV FILES FIRST");return false;}
    SDL_AudioSpec spec{};uint8_t*input=nullptr;uint32_t length=0;
    auto utf8=tracks[selected].u8string();if(!SDL_LoadWAV(utf8.c_str(),&spec,&input,&length)){std::strcpy(status,"CANNOT DECODE THIS WAV");return false;}
    SDL_AudioSpec target{SDL_AUDIO_S16,2,44100};
    unsigned frame_bytes=(SDL_AUDIO_BITSIZE(spec.format)/8)*spec.channels;
    bool valid_format=spec.freq>=8000&&spec.freq<=192000&&spec.channels>=1&&spec.channels<=8&&frame_bytes>0;
    uint64_t converted=valid_format?((uint64_t)length/frame_bytes*44100/spec.freq+1)*4:0;
    bool ok=valid_format&&length<=64*1024*1024&&converted<=128*1024*1024&&SDL_ConvertAudioSamples(&spec,input,(int)length,&target,&pcm,&pcm_bytes);
    SDL_free(input);if(!ok||pcm_bytes<4||pcm_bytes>128*1024*1024){stop();std::strcpy(status,"UNSUPPORTED / TOO LONG");return false;}
    status[0]=0;psx_mod_counter_add("rr.music.loaded",1);return true;
}
void init(){
    if(initialized)return;initialized=true;wchar_t exe[32768];if(!GetModuleFileNameW(nullptr,exe,32768))return;
    auto root=fs::path(exe).parent_path().parent_path();directory=root/L"music";settings=root/L"launcher.ini";scan();
    wchar_t source[16],name[260];GetPrivateProfileStringW(L"music",L"source",L"original",source,16,settings.c_str());custom=!std::wcscmp(source,L"custom");
    GetPrivateProfileStringW(L"music",L"track",L"",name,260,settings.c_str());for(size_t i=0;i<tracks.size();i++)if(tracks[i].filename()==name)selected=(int)i;
    if(custom&&!load_track())custom=false;
}
void SDLCALL chosen(void*,const char*const*files,int){
    if(!files)return;std::lock_guard<std::mutex>guard(pending_mutex);
    for(int i=0;files[i]&&pending.size()<128;i++)pending.push_back(fs::u8path(files[i]));
}
// Music in two-player races. The game plays none there (its music pair, SPU
// voices 22 / 23, never starts and their sound RAM holds other samples), online
// as offline. Here the soundtrack album is decoded from the owner's disc
// (owner_assets.c rr_owner_album_read: 8 KB left / 8 KB right PS1 ADPCM) at the
// game's own rate (pitch 0x5CE: 0x5CE/0x1000 source samples per 44.1 kHz
// frame), or the chosen custom track plays, at the one-player pair's usual
// level. Host output only: guest state is untouched, so online stays in step.
// Decoding runs on the event thread (music_tick) a second ahead into a ring
// the mixer drains; the album position carries on from race to race.
bool multi;
constexpr float multi_gain=.38f;   // (0x1880 << 1) x 2 of the one-player pair's volumes / 65536
constexpr int RING=44100*4;std::vector<int16_t> ring(RING*2);std::atomic<int> ring_read{0},ring_write{0};
unsigned album_at;bool album_missing;int hist[2][2];double phase;
std::vector<int16_t> src[2];size_t src_at;
void adpcm(const uint8_t*a,unsigned bytes,int*h,std::vector<int16_t>&out){
    static const int k0[5]={0,60,115,98,122},k1[5]={0,0,-52,-55,-60};
    for(unsigned p=0;p+16<=bytes;p+=16){int shift=a[p]&15,f=a[p]>>4;if(f>4)f=4;if(shift>12)shift=9;
        for(int i=0;i<28;i++){int s=(int16_t)(((a[p+2+i/2]>>((i&1)*4))&15)<<12)>>shift;s+=(h[0]*k0[f]+h[1]*k1[f]+32)>>6;s=std::max(-32768,std::min(32767,s));out.push_back((int16_t)s);h[1]=h[0];h[0]=s;}}
}
bool album_chunk(){
    static wchar_t disc[32768];if(!disc[0]){if(!GetModuleFileNameW(nullptr,disc,32768))return false;wchar_t*s=wcsrchr(disc,L'\\');if(!s)return false;wcscpy(s+1,L"..\\game-disc.bin");}
    uint8_t pair[0x4000];unsigned n=rr_owner_album_read(disc,album_at,pair,sizeof pair);
    if(n<sizeof pair){if(!album_at){album_missing=true;return false;}album_at=0;n=rr_owner_album_read(disc,0,pair,sizeof pair);if(n<sizeof pair){album_missing=true;return false;}}
    album_at+=sizeof pair;
    for(int c=0;c<2;c++){src[c].erase(src[c].begin(),src[c].begin()+std::min(src_at,src[c].size()));adpcm(pair+c*0x2000,0x2000,hist[c],src[c]);}
    src_at=0;return true;
}
void fill_ring(){
    const int free_frames=RING-1-((ring_write.load()-ring_read.load()+RING)%RING);if(free_frames<44100/2)return;
    int w=ring_write.load();const double step=0x5CE/4096.0;
    for(int i=0;i<std::min(free_frames,44100);i++){
        int16_t f[2];
        if(custom&&pcm){std::memcpy(f,pcm+position,4);position+=4;if(position>=pcm_bytes)position=0;}
        else{while(src_at+1>=src[0].size()||src_at+1>=src[1].size()){if(album_missing||!album_chunk())return;}
            for(int c=0;c<2;c++)f[c]=(int16_t)(src[c][src_at]+(src[c][src_at+1]-src[c][src_at])*phase);
            phase+=step;while(phase>=1){phase-=1;src_at++;}}
        ring[w*2]=f[0];ring[w*2+1]=f[1];w=(w+1)%RING;ring_write.store(w);}
}
void queue_preview(){
    if(!preview||!custom||race_active||!pcm)return;
    if(!stream){SDL_AudioSpec spec{SDL_AUDIO_S16,2,44100};stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);if(!stream){std::strcpy(status,"AUDIO DEVICE UNAVAILABLE");return;}SDL_ResumeAudioStreamDevice(stream);}
    // Queue a small bounded window, so navigation and pause stay responsive.
    if(SDL_GetAudioStreamQueued(stream)<44100/5*4){int n=std::min(4410*4,pcm_bytes-position);if(n>0&&SDL_PutAudioStreamData(stream,pcm+position,n)){position+=n;psx_mod_counter_add("rr.music.preview_frames",n/4);if(position==pcm_bytes)position=0;}}
}
}
extern "C" unsigned rr_music_voice_mask(void){
    if(!initialized||!custom||!pcm||(!race_active&&!preview))return 0;
    // A wet music pair would feed guest-visible reverb RAM. Keep that path
    // canonical instead of filtering it; only the title's dry pair qualifies.
    if(spu_read(0x1f801d9a)&0xc0)return 0;
    // Jailbreak's streamed stereo pair has matching envelope/rate and two
    // 64K ring buffers. Check its signature before filtering these voices.
    unsigned a=spu_read(0x1f801d66)*8,b=spu_read(0x1f801d76)*8;
    if(a<0x40000||a>=0x78000||b<0x40000||b>=0x78000||(a>b?a-b:b-a)!=0x10000||spu_read(0x1f801d64)!=spu_read(0x1f801d74)||spu_read(0x1f801d68)!=0x98ff||spu_read(0x1f801d78)!=0x98ff)return 0;
    return (1u<<22)|(1u<<23);
}
extern "C" void rr_music_mix(int16_t*out,int frames){
    if(initialized&&multi&&race_active&&!paused_active){
        // Two-player races: the ring filled by music_tick (album or custom track).
        int r=ring_read.load();const int w=ring_write.load();int mixed=0;
        for(int i=0;i<frames&&r!=w;i++,mixed++){for(int c=0;c<2;c++){int s=out[i*2+c]+(int)(ring[r*2+c]*multi_gain);out[i*2+c]=(int16_t)std::max(-32768,std::min(32767,s));}r=(r+1)%RING;}
        ring_read.store(r);psx_mod_counter_add("rr.music.multi_frames",mixed);return;}
    if(!initialized||!custom||!pcm||!race_active||paused_active||!rr_music_voice_mask())return;
    int raw=0;for(unsigned a:{0x1f801d60u,0x1f801d62u,0x1f801d70u,0x1f801d72u})raw+=std::abs((int16_t)(spu_read(a)<<1));
    float gain=std::min(raw/65536.0f,1.0f);
    for(int i=0;i<frames;i++){
        int16_t sample[2];std::memcpy(sample,pcm+position,4);position+=4;if(position>=pcm_bytes)position=0;
        for(int c=0;c<2;c++){int s=out[i*2+c]+(int)(sample[c]*gain);out[i*2+c]=(int16_t)std::max(-32768,std::min(32767,s));}
    }
    psx_mod_counter_add("rr.music.custom_frames",frames);
}
namespace rrpc {
bool music_is_custom(){init();return custom;}
int music_track_count(){init();return (int)tracks.size();}
void music_track_label(int index,char*text,unsigned capacity){init();if(!capacity)return;text[0]=0;if(index<0||index>=(int)tracks.size())return;auto s=tracks[index].stem().wstring();unsigned n=std::min((unsigned)s.size(),capacity-1);for(unsigned i=0;i<n;i++)text[i]=s[i]>=32&&s[i]<127?(char)s[i]:'?';text[n]=0;}
void music_select(int index){init();if(index>=0&&index<(int)tracks.size()){selected=index;if(load_track()){custom=true;save();}}}
void music_tick(bool race,bool paused){
    init();race_active=race;paused_active=paused;
    // Human players in this race (as pc_services.cpp race_results); two or more: our music.
    {const bool was=multi;multi=race&&memory_get_ram_ptr()[0x0D5D68]>=2;if(multi&&!was){ring_read.store(ring_write.load());psx_mod_counter_add("rr.music.multi_races",1);}if(multi&&!paused)fill_ring();}
    if(race&&stream){SDL_DestroyAudioStream(stream);stream=nullptr;}
    std::vector<fs::path> imports;{std::lock_guard<std::mutex>guard(pending_mutex);imports.swap(pending);}
    if(!imports.empty()){
        unsigned added=0;for(auto&p:imports){std::error_code ec;auto n=fs::file_size(p,ec);if(ec||n>64*1024*1024||_wcsicmp(p.extension().c_str(),L".wav"))continue;
            // Validate before copying. A failed/duplicate import preserves files.
            auto utf8=p.u8string();SDL_AudioSpec spec{};uint8_t*data=nullptr;uint32_t length=0;
            if(!SDL_LoadWAV(utf8.c_str(),&spec,&data,&length))continue;SDL_free(data);
            if(fs::copy_file(p,directory/p.filename(),fs::copy_options::none,ec))added++;
        }
        scan();if(added){std::snprintf(status,32,"ADDED %u TRACKS",added);psx_mod_counter_add("rr.music.imported",added);}else std::strcpy(status,"NO NEW VALID WAV FILES");
    }
    queue_preview();
}
void music_preview(bool enabled){init();preview=enabled;if(!enabled&&stream){SDL_DestroyAudioStream(stream);stream=nullptr;}}
void music_change(int row,int direction){
    init();status[0]=0;
    if(row==0){if(custom){custom=false;stop();}else if(load_track())custom=true;}
    else if(row==1&&!tracks.empty()){selected=(selected+direction+(int)tracks.size())%(int)tracks.size();bool previous=custom;if(!load_track())custom=false;else custom=previous;}
    save();
}
void music_import(){init();static const SDL_DialogFileFilter filters[]={{"Wave audio (*.wav)","wav"}};SDL_ShowOpenFileDialog(chosen,nullptr,SDL_GetKeyboardFocus(),filters,1,nullptr,true);}
void music_labels(char(*labels)[32],char(*help)[32],int row){
    init();std::snprintf(labels[0],32,"Music: %s",custom?"Custom":"Original");
    if(tracks.empty())std::strcpy(labels[1],"Track: None");   // fits the bar unshrunkelse{auto stem=tracks[selected].stem().wstring();char title[22]{};for(size_t i=0;i<stem.size()&&i<21;i++)title[i]=stem[i]>=32&&stem[i]<127?(char)stem[i]:'?';std::snprintf(labels[1],32,"%d: %s",selected+1,title);}
    std::strcpy(labels[2],"Add WAV tracks");   // as in the launcher's Audio page
    const char*first[]={"Choose original or custom.","Left/Right: choose track.","Import your own music."};std::strcpy(help[0],status[0]?status:first[row]);
    if(row==0)std::strcpy(help[1],"Original remains available.");else if(row==1)std::snprintf(help[1],32,"%u custom tracks on this PC",(unsigned)tracks.size());else std::strcpy(help[1],"WAV, up to 64 MB per track.");
    std::strcpy(help[2],"Volume: Game options > Noise");
}
}
